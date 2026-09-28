"""Validated multi-face gallery package creation and inspection."""

import hashlib
import io
import json
import math
import re
import zipfile
from datetime import datetime
from typing import Any, Dict, List, Optional, Sequence, Tuple

from PIL import Image, ImageDraw, ImageFont

from studio.core.exporter import validate_face_project
from studio.core.elements import render_element
from studio.core.formats import FORMAT_REGISTRY, get_project_formats, make_format_catalog
from studio.core.data_fields import (
    get_union_data_requirements,
    get_provider_capabilities,
    validate_data_requirements,
)
from studio.core.rules import RuleEngine
from studio.core.renderers import DEFAULT_RENDERER_ID, RENDERER_REGISTRY

MAX_FACES = 32
MAX_FACE_PACKAGE_BYTES = 2 * 1024 * 1024
MAX_GALLERY_BYTES = 64 * 1024 * 1024
MAX_PREVIEW_BYTES = 512 * 1024
PACKAGE_ENTRIES = {"manifest.json", "assets/bg.rgb565"}
SAFE_ID = re.compile(r"^[a-z0-9][a-z0-9_-]{0,47}$")


def _safe_archive_names(archive: zipfile.ZipFile) -> List[str]:
    names = []
    for info in archive.infolist():
        name = info.filename
        parts = name.split("/")
        if (
            not name
            or name.startswith("/")
            or "\\" in name
            or any(part in ("", ".", "..") for part in parts)
            or (info.external_attr >> 16) & 0o170000 == 0o120000
        ):
            raise ValueError("Archive contains an unsafe path.")
        if info.is_dir():
            raise ValueError("Directory entries are not allowed.")
        if info.file_size > MAX_FACE_PACKAGE_BYTES:
            raise ValueError("Archive entry exceeds the permitted size.")
        names.append(name)
    if len(names) != len(set(names)):
        raise ValueError("Archive contains duplicate paths.")
    return names


def _validate_face_bytes(package_bytes: bytes) -> Tuple[Dict[str, Any], bytes]:
    if len(package_bytes) > MAX_FACE_PACKAGE_BYTES:
        raise ValueError("Face package exceeds the 2 MiB limit.")
    with zipfile.ZipFile(io.BytesIO(package_bytes), "r") as archive:
        names = _safe_archive_names(archive)
        if set(names) != PACKAGE_ENTRIES:
            raise ValueError("Face package must contain only manifest.json and assets/bg.rgb565.")
        manifest = json.loads(archive.read("manifest.json").decode("utf-8"))
        errors = validate_face_project(manifest)
        if errors:
            raise ValueError("; ".join(errors))
        background = archive.read("assets/bg.rgb565")
        if len(background) != 320 * 240 * 2:
            raise ValueError("Face background must be exactly 153600 bytes.")
        return manifest, background


def _preview_png(background: bytes, manifest: Dict[str, Any]) -> bytes:
    pixels = []
    for index in range(0, len(background), 2):
        value = background[index] | (background[index + 1] << 8)
        pixels.append((
            ((value >> 11) & 0x1F) * 255 // 31,
            ((value >> 5) & 0x3F) * 255 // 63,
            (value & 0x1F) * 255 // 31,
        ))
    image = Image.new("RGB", (320, 240))
    image.putdata(pixels)
    draw = ImageDraw.Draw(image)
    renderer_id = manifest.get("renderer_id", DEFAULT_RENDERER_ID)
    renderer = RENDERER_REGISTRY.get(renderer_id)
    if renderer is not None and renderer.get("native_face_index") is not None:
        colors = {
            0: (130, 235, 255),
            1: (0, 245, 255),
            2: (232, 218, 242),
            3: (255, 115, 65),
            4: (96, 165, 250),
        }
        color = colors[renderer["native_face_index"]]
        font = ImageFont.load_default()
        if renderer["native_face_index"] == 4:
            draw.text((36, 45), "LIVE SYSTEM", fill=color, font=font)
            draw.text((36, 70), "TELEMETRY", fill=(220, 228, 240), font=font)
            draw.text((36, 105), "10:08  UTC", fill=(110, 240, 180), font=font)
        else:
            draw.text((36, 58), "10", fill=color, font=font, stroke_width=1)
            draw.text((174, 58), "08", fill=color, font=font, stroke_width=1)
            if renderer["native_face_index"] == 1:
                for x in range(24, 296):
                    y = 174 + int(4 * math.sin(x / 15.0))
                    draw.line((x, y, x, y + 1), fill=(20, 180, 255))
        image.thumbnail((160, 120), Image.Resampling.LANCZOS)
        output = io.BytesIO()
        image.save(output, format="PNG", optimize=True)
        return output.getvalue()
    preview_time = datetime(2026, 1, 1, 10, 8, 0)
    state = {
        "battery_percent": 80,
        "gps_status": 0,
        "weather_status": 1,
        "charging": False,
    }
    rules = manifest.get("rules", [])
    _formats, default_format = get_project_formats(manifest)
    for element in manifest.get("elements", []):
        if not RuleEngine.is_element_visible(element, rules, state):
            continue
        preview_element = dict(element)
        if element.get("type") == "digital_time":
            preview_element["layout"] = default_format or "horizontal"
        render_element(draw, preview_element, preview_time)
    output = io.BytesIO()
    image.thumbnail((160, 120), Image.Resampling.LANCZOS)
    image.save(output, format="PNG", optimize=True)
    return output.getvalue()


def export_ttgallery(
    face_packages: Sequence[str], output_path: str, gallery_name: str,
    version: str = "1.0.0",
) -> Tuple[bool, List[str]]:
    """Create a gallery ZIP from validated .ttface packages."""
    if not isinstance(gallery_name, str) or not gallery_name.strip():
        return False, ["Gallery name must not be empty."]
    if not face_packages or len(face_packages) > MAX_FACES:
        return False, [f"Gallery must contain 1 to {MAX_FACES} faces."]

    entries: Dict[str, bytes] = {}
    faces = []
    used_ids = set()
    try:
        for package_path in face_packages:
            with open(package_path, "rb") as package_file:
                package_bytes = package_file.read(MAX_FACE_PACKAGE_BYTES + 1)
            manifest, background = _validate_face_bytes(package_bytes)
            base_id = re.sub(
                r"[^a-z0-9_-]+", "-", manifest["face_name"].strip().lower()
            ).strip("-_")
            face_id = base_id[:48] or "face"
            if not SAFE_ID.fullmatch(face_id) or face_id in used_ids:
                return False, [f"Face names must produce unique safe IDs: '{face_id}'."]
            used_ids.add(face_id)

            package_entry = f"faces/{face_id}.ttface"
            preview_entry = f"previews/{face_id}.png"
            preview = _preview_png(background, manifest)
            formats, default_format = get_project_formats(manifest)
            renderer_id = manifest.get(
                "renderer_id", DEFAULT_RENDERER_ID
            )
            face_data_reqs = manifest.get("data_requirements", [])
            entries[package_entry] = package_bytes
            entries[preview_entry] = preview
            faces.append({
                "id": face_id,
                "name": manifest["face_name"],
                "version": manifest.get("version", "1.0.0"),
                "formats": formats,
                "default_format": default_format,
                "renderer_id": renderer_id,
                "renderer_id": renderer_id,
                "data_requirements": face_data_reqs if isinstance(face_data_reqs, list) else [],
                "package": package_entry,
                "preview": preview_entry,
                "sha256": hashlib.sha256(package_bytes).hexdigest(),
                "preview_sha256": hashlib.sha256(preview).hexdigest(),
            })
            if sum(len(content) for content in entries.values()) > MAX_GALLERY_BYTES:
                return False, ["Gallery exceeds the 64 MiB package limit."]

        union_reqs = get_union_data_requirements([
            f.get("data_requirements", []) for f in faces
        ])
        capabilities = get_provider_capabilities(union_reqs)

        gallery_manifest = {
            "gallery_version": 1,
            "runtime_api": "ttface-manifest-v2",
            "name": gallery_name.strip(),
            "version": version,
            "target_device": "TomTom ONE v6 (Model 19)",
            "face_count": len(faces),
            "formats": make_format_catalog([
                format_id for face in faces for format_id in face["formats"]
            ]),
            "data_requirements": union_reqs,
            "provider_capabilities": capabilities,
            "faces": faces,
        }
        with zipfile.ZipFile(output_path, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr(
                "gallery.json",
                json.dumps(gallery_manifest, indent=2, sort_keys=True),
            )
            for name, content in sorted(entries.items()):
                archive.writestr(name, content)
        return True, []
    except (OSError, ValueError, UnicodeError, json.JSONDecodeError,
            zipfile.BadZipFile, KeyError, TypeError) as error:
        return False, [f"Gallery export failed: {error}"]


def inspect_ttgallery(
    file_path: str,
) -> Tuple[Optional[Dict[str, Any]], List[str]]:
    """Validate gallery structure, nested face packages, and checksums."""
    try:
        with zipfile.ZipFile(file_path, "r") as archive:
            names = _safe_archive_names(archive)
            if "gallery.json" not in names:
                return None, ["Gallery package is missing gallery.json."]
            if len(archive.namelist()) > MAX_FACES * 2 + 1:
                return None, ["Gallery contains too many entries."]
            if sum(info.file_size for info in archive.infolist()) > MAX_GALLERY_BYTES:
                return None, ["Gallery exceeds the 64 MiB package limit."]

            manifest = json.loads(archive.read("gallery.json").decode("utf-8"))
            if (
                not isinstance(manifest, dict)
                or type(manifest.get("gallery_version")) is not int
                or manifest["gallery_version"] != 1
            ):
                return None, ["Unsupported gallery manifest version."]
            if manifest.get("runtime_api") != "ttface-manifest-v2":
                return None, ["Unsupported gallery runtime API."]
            if (
                not isinstance(manifest.get("name"), str)
                or not manifest["name"].strip()
                or not isinstance(manifest.get("version"), str)
            ):
                return None, ["Gallery name or version is invalid."]
            faces = manifest.get("faces")
            if (
                not isinstance(faces, list)
                or not 1 <= len(faces) <= MAX_FACES
                or type(manifest.get("face_count")) is not int
                or manifest.get("face_count") != len(faces)
            ):
                return None, ["Gallery face list/count is invalid."]

            expected = {"gallery.json"}
            seen_ids = set()
            declared_formats = set()
            for face in faces:
                if not isinstance(face, dict):
                    return None, ["Gallery face descriptor must be an object."]
                face_id = face.get("id")
                package_path = face.get("package")
                preview_path = face.get("preview")
                renderer_id = face.get("renderer_id", DEFAULT_RENDERER_ID)
                if (
                    not isinstance(face_id, str)
                    or not SAFE_ID.fullmatch(face_id)
                    or face_id in seen_ids
                    or package_path != f"faces/{face_id}.ttface"
                    or preview_path != f"previews/{face_id}.png"
                    or not isinstance(face.get("formats"), list)
                    or not isinstance(renderer_id, str)
                    or renderer_id not in RENDERER_REGISTRY
                ):
                    return None, ["Gallery contains an invalid or duplicate face descriptor."]
                face_formats = face["formats"]
                default_format = face.get("default_format")
                if (
                    any(
                        not isinstance(format_id, str)
                        or format_id not in FORMAT_REGISTRY
                        for format_id in face_formats
                    )
                    or len(face_formats) != len(set(face_formats))
                    or (face_formats and default_format not in face_formats)
                    or (not face_formats and default_format not in (None, ""))
                ):
                    return None, [f"Face format declaration is invalid: {face_id}."]
                seen_ids.add(face_id)
                declared_formats.update(face_formats)
                expected.update((package_path, preview_path))

                package_bytes = archive.read(package_path)
                preview_bytes = archive.read(preview_path)
                if len(preview_bytes) > MAX_PREVIEW_BYTES:
                    return None, [f"Face preview exceeds the size limit: {face_id}."]
                if hashlib.sha256(package_bytes).hexdigest() != face.get("sha256"):
                    return None, [f"Face package checksum failed: {face_id}."]
                if hashlib.sha256(preview_bytes).hexdigest() != face.get("preview_sha256"):
                    return None, [f"Face preview checksum failed: {face_id}."]
                nested_manifest, _ = _validate_face_bytes(package_bytes)
                if nested_manifest.get("face_name") != face.get("name"):
                    return None, [f"Face name does not match package: {face_id}."]
                if nested_manifest.get(
                    "renderer_id", DEFAULT_RENDERER_ID
                ) != renderer_id:
                    return None, [f"Face renderer does not match package: {face_id}."]
                nested_formats, nested_default = get_project_formats(nested_manifest)
                if nested_formats != face_formats or nested_default != default_format:
                    return None, [f"Face formats do not match package: {face_id}."]
                with Image.open(io.BytesIO(preview_bytes)) as preview:
                    if preview.format != "PNG" or preview.size != (160, 120):
                        return None, [f"Invalid face preview image: {face_id}."]

            if "data_requirements" in manifest:
                is_valid, req_errs = validate_data_requirements(manifest["data_requirements"])
                if not is_valid:
                    return None, [f"Gallery data requirements invalid: {req_errs[0]}"]

            if set(names) != expected:
                return None, ["Gallery contains unlisted or missing files."]
            if manifest.get("formats") != make_format_catalog(
                sorted(declared_formats)
            ):
                return None, ["Gallery format catalog does not match its faces."]
            return manifest, []
    except (OSError, ValueError, UnicodeError, json.JSONDecodeError,
            zipfile.BadZipFile, KeyError, TypeError) as error:
        return None, [f"Gallery inspection failed: {error}"]


def plan_gallery_changes(
    installed_manifest: Dict[str, Any], desired_manifest: Dict[str, Any],
) -> Dict[str, List[str]]:
    """Compute an authoritative add/update/remove plan from two gallery manifests."""
    def face_hashes(manifest: Dict[str, Any]) -> Dict[str, str]:
        faces = manifest.get("faces")
        if not isinstance(faces, list):
            raise ValueError("Gallery manifest faces must be a list.")
        result = {}
        for face in faces:
            if (
                not isinstance(face, dict)
                or not isinstance(face.get("id"), str)
                or not SAFE_ID.fullmatch(face["id"])
                or not isinstance(face.get("sha256"), str)
                or not re.fullmatch(r"[0-9a-f]{64}", face["sha256"])
            ):
                raise ValueError("Gallery manifest contains an invalid face descriptor.")
            if face["id"] in result:
                raise ValueError("Gallery manifest contains duplicate face IDs.")
            result[face["id"]] = face["sha256"]
        return result

    old = face_hashes(installed_manifest)
    new = face_hashes(desired_manifest)
    return {
        "add": sorted(set(new) - set(old)),
        "update": sorted(face_id for face_id in set(old) & set(new)
                         if old[face_id] != new[face_id]),
        "remove": sorted(set(old) - set(new)),
    }
