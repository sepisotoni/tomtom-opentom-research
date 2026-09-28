"""
TomTom Face Studio - Project File & Package Importer
===================================================
Handles saving and loading `.ttproj` editor project files,
as well as inspecting and loading `.ttface` exported packages.
"""

import os
import json
import base64
import zipfile
import io
import struct
from PIL import Image
from typing import Tuple, Dict, Any, List, Optional
from studio.core.exporter import validate_face_project


def save_project_file(file_path: str, project_data: Dict[str, Any], canvas_image: Image.Image):
    """Save editor state and pixel canvas to a .ttproj JSON file."""
    buffer = io.BytesIO()
    canvas_image.save(buffer, format="PNG")
    b64_png = base64.b64encode(buffer.getvalue()).decode("utf-8")

    payload = {
        "file_format": "ttproj",
        "version": 1,
        "project": project_data,
        "canvas_png_b64": b64_png
    }

    with open(file_path, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2)


def load_project_file(file_path: str) -> Tuple[Dict[str, Any], Image.Image]:
    """Load editor state and pixel canvas from a .ttproj JSON file."""
    with open(file_path, "r", encoding="utf-8") as f:
        payload = json.load(f)

    project_data = payload.get("project", {})
    b64_png = payload.get("canvas_png_b64")

    if b64_png:
        img_bytes = base64.b64decode(b64_png)
        canvas_img = Image.open(io.BytesIO(img_bytes)).convert("RGB")
    else:
        bg_color = project_data.get("background_color", "#000000")
        canvas_img = Image.new("RGB", (320, 240), bg_color)

    return project_data, canvas_img


def inspect_ttface_package(file_path: str) -> Tuple[Optional[Dict[str, Any]], Optional[Image.Image], List[str]]:
    """
    Inspect an exported .ttface package zip.
    Returns (manifest_dict, bg_image, validation_errors).
    """
    if not os.path.exists(file_path):
        return None, None, [f"Error: File '{file_path}' does not exist."]

    try:
        with zipfile.ZipFile(file_path, "r") as zf:
            if "manifest.json" not in zf.namelist():
                return None, None, ["Error: Invalid .ttface package. Missing 'manifest.json'."]

            manifest_bytes = zf.read("manifest.json")
            manifest = json.loads(manifest_bytes.decode("utf-8"))

            val_errors = validate_face_project(manifest)

            bg_file_path = manifest.get("background", {}).get("file") if isinstance(manifest.get("background"), dict) else None
            if bg_file_path and bg_file_path not in zf.namelist():
                val_errors.append(f"Validation Error: Background file '{bg_file_path}' listed in manifest is missing from ZIP entries.")

            bg_image = None
            if bg_file_path and bg_file_path in zf.namelist():
                raw_bytes = zf.read(bg_file_path)
                if len(raw_bytes) == 320 * 240 * 2:
                    # Convert RGB565 bytes to PIL Image
                    img = Image.new("RGB", (320, 240))
                    pixels = []
                    for i in range(0, len(raw_bytes), 2):
                        val = struct.unpack("<H", raw_bytes[i:i+2])[0]
                        r = ((val >> 11) & 0x1F) << 3
                        g = ((val >> 5) & 0x3F) << 2
                        b = (val & 0x1F) << 3
                        pixels.append((r, g, b))
                    img.putdata(pixels)
                    bg_image = img

            return manifest, bg_image, val_errors
    except Exception as e:
        return None, None, [f"Error inspecting .ttface package: {str(e)}"]
