"""Build installable .ttface packages for the five original C watchfaces."""

import argparse
import os
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from studio.core.exporter import export_ttface_package
from studio.core.gallery import export_ttgallery
from studio.core.renderers import RENDERER_REGISTRY


LEGACY_RENDERERS = [
    renderer_id
    for renderer_id, renderer in RENDERER_REGISTRY.items()
    if renderer["native_face_index"] is not None
]


def build_packages(output_dir: str) -> str:
    os.makedirs(output_dir, exist_ok=True)
    package_paths = []
    for renderer_id in LEGACY_RENDERERS:
        renderer = RENDERER_REGISTRY[renderer_id]
        name = renderer["name"]
        project = {
            "metadata": {
                "name": name,
                "version": "1.0.0",
                "author": "OpenTom",
            },
            "renderer_id": renderer_id,
            "canvas": {"width": 320, "height": 240},
            "display": {"formats": [], "default_format": ""},
            "background_color": "#000000",
            "elements": [],
            "rules": [],
        }
        filename = renderer_id.rsplit(".", 2)[-2] + ".ttface"
        package_path = os.path.join(output_dir, filename)
        success, errors = export_ttface_package(
            project, Image.new("RGB", (320, 240), "#000000"), package_path
        )
        if not success:
            raise RuntimeError(
                f"Could not package {name}: {'; '.join(errors)}"
            )
        package_paths.append(package_path)

    gallery_path = os.path.join(output_dir, "TomTom-Legacy-Faces.ttgallery")
    success, errors = export_ttgallery(
        package_paths, gallery_path, "TomTom Legacy Faces"
    )
    if not success:
        raise RuntimeError(f"Could not build legacy gallery: {'; '.join(errors)}")
    return gallery_path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "output_dir",
        nargs="?",
        default=os.path.expanduser("~/Downloads/TomTom-Legacy-Faces"),
    )
    args = parser.parse_args()
    gallery_path = build_packages(args.output_dir)
    print(f"Created five legacy .ttface packages and gallery: {gallery_path}")


if __name__ == "__main__":
    main()
