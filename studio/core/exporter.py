"""
TomTom Face Studio - Package Exporter & Manifest Validator
=========================================================
Handles conversion of face designs, background images, and icon assets
into the deterministic `.ttface` format for Nano-X on TomTom ONE.
"""

import os
import json
import zipfile
import struct
from PIL import Image

from studio.core.formats import FORMAT_REGISTRY, get_project_formats
from studio.core.renderers import (
    DEFAULT_RENDERER_ID,
    RENDERER_REGISTRY,
    get_renderer_id,
)
from studio.core.data_fields import validate_data_requirements

SUPPORTED_SIGNALS = {
    "battery_percent",
    "battery_low",
    "gps_status",
    "gps_fix",
    "weather_status",
    "weather_unavailable",
    "charging"
}

SUPPORTED_OPERATORS = {"==", "!=", "<=", ">=", "<", ">"}
SUPPORTED_ELEMENT_TYPES = {"digital_time", "date", "text", "status_icon"}


def color_hex_to_rgb565(hex_color: str) -> int:
    """Convert hex color string (#RRGGBB) to 16-bit RGB565 integer."""
    hex_color = str(hex_color).lstrip("#")
    if len(hex_color) == 6:
        try:
            r = int(hex_color[0:2], 16)
            g = int(hex_color[2:4], 16)
            b = int(hex_color[4:6], 16)
            return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        except ValueError:
            pass
    return 0xFFFF  # White default fallback


def image_to_rgb565_bytes(image: Image.Image, width: int = 320, height: int = 240) -> bytes:
    """Convert a PIL Image to 16-bit RGB565 raw little-endian byte array."""
    img = image.convert("RGB").resize((width, height), Image.Resampling.NEAREST)
    raw_data = bytearray()
    for y in range(height):
        for x in range(width):
            r, g, b = img.getpixel((x, y))
            val = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            raw_data.extend(struct.pack("<H", val))
    return bytes(raw_data)


def validate_face_project(project_data: dict) -> list:
    """
    Validate a project dictionary against TomTom ONE hardware constraints.
    Returns a list of error string messages. Empty list = valid.
    """
    errors = []

    if not isinstance(project_data, dict):
        return ["Validation Error: Project data must be a JSON object."]

    renderer_id = get_renderer_id(project_data)
    if renderer_id not in RENDERER_REGISTRY:
        errors.append(
            f"Validation Error: Unsupported face renderer '{renderer_id}'."
        )

    display = project_data.get("display", {})
    if not isinstance(display, dict):
        errors.append("Validation Error: Display settings must be an object.")
    else:
        formats, default_format = get_project_formats(project_data)
        has_digital_time = any(
            isinstance(element, dict) and element.get("type") == "digital_time"
            for element in project_data.get("elements", [])
        ) if isinstance(project_data.get("elements", []), list) else False
        if formats:
            if any(
                not isinstance(format_id, str)
                or format_id not in FORMAT_REGISTRY
                for format_id in formats
            ):
                errors.append(
                    "Validation Error: Display formats contain an unsupported format ID."
                )
            elif len(formats) != len(set(formats)):
                errors.append(
                    "Validation Error: Display formats must not contain duplicates."
                )
            elif default_format not in formats:
                errors.append(
                    "Validation Error: Default display format must be listed as supported."
                )
        elif has_digital_time:
            errors.append(
                "Validation Error: A digital-time face must support at least one display format."
            )
        elif default_format:
            errors.append(
                "Validation Error: A face without display formats cannot set a default format."
            )

    meta = project_data.get("metadata", {})
    name = meta.get("name") if isinstance(meta, dict) else None
    if not name:
        name = project_data.get("face_name")
    if not name or not isinstance(name, str):
        errors.append("Validation Error: Project name must be a non-empty string.")

    canvas = project_data.get("canvas", {})
    if not isinstance(canvas, dict):
        errors.append("Validation Error: Canvas specification must be a dictionary.")
    else:
        cw = canvas.get("width")
        ch = canvas.get("height")
        if not isinstance(cw, int) or isinstance(cw, bool) or cw != 320 or \
           not isinstance(ch, int) or isinstance(ch, bool) or ch != 240:
            errors.append(f"Validation Error: Canvas dimensions must be integer 320x240. Found {cw}x{ch}.")

    elements = project_data.get("elements", [])
    if not isinstance(elements, list):
        errors.append("Validation Error: Elements field must be a list.")
    else:
        for idx, el in enumerate(elements):
            if not isinstance(el, dict):
                errors.append(f"Validation Error: Element #{idx+1} must be an object.")
                continue

            el_type = el.get("type")
            if not isinstance(el_type, str) or el_type not in SUPPORTED_ELEMENT_TYPES:
                errors.append(f"Validation Error: Element #{idx+1} has unknown or non-string type '{el_type}'.")

            x = el.get("x")
            y = el.get("y")
            w = el.get("width")
            h = el.get("height")

            # Reject malformed field types
            for field_name, field_val in [("x", x), ("y", y), ("width", w), ("height", h)]:
                if not isinstance(field_val, int) or isinstance(field_val, bool):
                    errors.append(f"Validation Error: Element #{idx+1} field '{field_name}' must be an integer.")

            # Reject out-of-bounds elements
            if isinstance(x, int) and not isinstance(x, bool) and \
               isinstance(y, int) and not isinstance(y, bool) and \
               isinstance(w, int) and not isinstance(w, bool) and \
               isinstance(h, int) and not isinstance(h, bool):
                if x < 0 or y < 0 or w <= 0 or h <= 0 or (x + w) > 320 or (y + h) > 240:
                    errors.append(f"Validation Error: Element #{idx+1} ('{el.get('id', 'unnamed')}') extends outside 320x240 screen boundary (x={x}, y={y}, w={w}, h={h}).")

            font_sz = el.get("font_size")
            if font_sz is not None and (not isinstance(font_sz, int) or isinstance(font_sz, bool) or font_sz <= 0):
                errors.append(f"Validation Error: Element #{idx+1} font_size must be a positive integer.")

    rules = project_data.get("rules", [])
    if not isinstance(rules, list):
        errors.append("Validation Error: Rules field must be a list.")
    else:
        for idx, rule in enumerate(rules):
            if not isinstance(rule, dict):
                errors.append(f"Validation Error: Rule #{idx+1} must be an object.")
                continue

            sig = rule.get("signal")
            if not isinstance(sig, str) or sig not in SUPPORTED_SIGNALS:
                errors.append(f"Validation Error: Rule #{idx+1} references unsupported or malformed signal '{sig}'. Supported: {sorted(list(SUPPORTED_SIGNALS))}")

            op = rule.get("op")
            if not isinstance(op, str) or op not in SUPPORTED_OPERATORS:
                errors.append(f"Validation Error: Rule #{idx+1} uses unsupported operator '{op}'. Supported: {sorted(list(SUPPORTED_OPERATORS))}")

    # Validate background file matching ZIP entry path
    bg = project_data.get("background", {})
    if isinstance(bg, dict) and "file" in bg:
        bg_file = bg.get("file")
        if bg_file != "assets/bg.rgb565":
            errors.append(f"Validation Error: Manifest background file '{bg_file}' must match ZIP entry 'assets/bg.rgb565'.")

    # Validate data requirements if specified
    data_reqs = project_data.get("data_requirements")
    if data_reqs is not None:
        _, req_errs = validate_data_requirements(data_reqs)
        errors.extend(req_errs)

    return errors


def export_ttface_package(project_data: dict, bg_image: Image.Image, output_path: str) -> tuple[bool, list]:
    """
    Exports a project dictionary + background image into a .ttface zip package.
    Returns (success_boolean, list_of_validation_messages).
    """
    errors = validate_face_project(project_data)
    blocking_errors = [e for e in errors if e.startswith("Validation Error:")]
    if blocking_errors:
        return False, errors

    try:
        manifest = {
            "manifest_version": 1,
            "face_name": project_data.get("metadata", {}).get("name") or project_data.get("face_name", "Untitled Face"),
            "version": project_data.get("metadata", {}).get("version", "1.0.0"),
            "author": project_data.get("metadata", {}).get("author", "Unknown"),
            "target_device": "TomTom ONE v6 (Model 19)",
            "renderer_id": project_data.get(
                "renderer_id", DEFAULT_RENDERER_ID
            ),
            "canvas": {
                "width": 320,
                "height": 240
            },
            "display": {},
            "data_requirements": project_data.get("data_requirements", []),
            "background": {
                "type": "raw_rgb565",
                "file": "assets/bg.rgb565",
                "color": project_data.get("background_color", "#000000")
            },
            "elements": project_data.get("elements", []),
            "rules": project_data.get("rules", [])
        }
        display_formats, default_format = get_project_formats(project_data)
        if display_formats:
            manifest["display"] = {
                "formats": display_formats,
                "default_format": default_format,
            }

        with zipfile.ZipFile(output_path, "w", zipfile.ZIP_DEFLATED) as zf:
            # Write manifest.json
            zf.writestr("manifest.json", json.dumps(manifest, indent=2))

            # Write background binary RGB565 (320x240 = 153,600 bytes)
            if bg_image is not None:
                bg_bytes = image_to_rgb565_bytes(bg_image, 320, 240)
            else:
                bg_color = project_data.get("background_color", "#000000")
                rgb565_val = color_hex_to_rgb565(bg_color)
                bg_bytes = struct.pack("<H", rgb565_val) * (320 * 240)

            zf.writestr("assets/bg.rgb565", bg_bytes)

        return True, errors
    except Exception as e:
        return False, [f"Export Failed with Exception: {str(e)}"]
