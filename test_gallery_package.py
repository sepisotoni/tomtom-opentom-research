import os
import tempfile
import unittest
import zipfile

from PIL import Image

from studio.core.exporter import export_ttface_package
from studio.core.gallery import (
    export_ttgallery,
    inspect_ttgallery,
    plan_gallery_changes,
)
from studio.core.renderers import DEFAULT_RENDERER_ID, RENDERER_REGISTRY


class TestGalleryPackage(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory(prefix="ttgallery-test-")
        self.addCleanup(self.temp_dir.cleanup)

    def make_face(self, name, layout, renderer_id=DEFAULT_RENDERER_ID):
        path = os.path.join(self.temp_dir.name, name + ".ttface")
        project = {
            "metadata": {"name": name, "version": "1.0.0", "author": "Tests"},
            "renderer_id": renderer_id,
            "canvas": {"width": 320, "height": 240},
            "display": {
                "formats": [layout],
                "default_format": layout,
            },
            "background_color": "#101020",
            "elements": [{
                "id": "clock",
                "type": "digital_time",
                "format": "HH:MM",
                "x": 60,
                "y": 60,
                "width": 200,
                "height": 100,
                "color": "#FFFFFF",
                "font_size": 36,
                "is_12h": False,
                "rule": None,
            }],
            "rules": [],
        }
        success, errors = export_ttface_package(
            project, Image.new("RGB", (320, 240), "#101020"), path
        )
        self.assertTrue(success, errors)
        return path

    def test_builds_and_inspects_multi_face_gallery(self):
        horizontal = self.make_face("Roboto Face", "horizontal")
        stacked = self.make_face("Ubuntu Face", "stacked")
        gallery_path = os.path.join(self.temp_dir.name, "collection.ttgallery")

        success, errors = export_ttgallery(
            [horizontal, stacked], gallery_path, "My Collection"
        )
        self.assertTrue(success, errors)
        manifest, validation_errors = inspect_ttgallery(gallery_path)

        self.assertEqual(validation_errors, [])
        self.assertEqual(manifest["face_count"], 2)
        self.assertEqual(manifest["runtime_api"], "ttface-manifest-v2")
        self.assertEqual(
            [face["renderer_id"] for face in manifest["faces"]],
            [DEFAULT_RENDERER_ID, DEFAULT_RENDERER_ID],
        )
        self.assertEqual(
            [face["default_format"] for face in manifest["faces"]],
            ["horizontal", "stacked"],
        )
        self.assertEqual(
            [format_def["id"] for format_def in manifest["formats"]],
            ["horizontal", "stacked"],
        )
        with zipfile.ZipFile(gallery_path) as archive:
            self.assertIn("faces/roboto-face.ttface", archive.namelist())
            self.assertIn("previews/ubuntu-face.png", archive.namelist())
            from io import BytesIO

            preview = Image.open(BytesIO(archive.read("previews/ubuntu-face.png")))
            self.assertGreater(len(preview.getcolors(160 * 120) or []), 1)

    def test_packages_native_legacy_faces_with_renderer_metadata(self):
        renderer_id = "opentom.builtin.hydro-aqua.v1"
        face = self.make_face("Hydro Aqua Wave", "horizontal", renderer_id)
        gallery_path = os.path.join(self.temp_dir.name, "legacy.ttgallery")

        success, errors = export_ttgallery([face], gallery_path, "Legacy")
        self.assertTrue(success, errors)
        manifest, validation_errors = inspect_ttgallery(gallery_path)

        self.assertEqual(validation_errors, [])
        self.assertEqual(
            manifest["faces"][0]["renderer_id"], renderer_id
        )
        self.assertIn(renderer_id, RENDERER_REGISTRY)
        with zipfile.ZipFile(gallery_path) as archive:
            from io import BytesIO

            preview = Image.open(
                BytesIO(archive.read("previews/hydro-aqua-wave.png"))
            )
            self.assertEqual(preview.size, (160, 120))

    def test_rejects_gallery_path_traversal(self):
        face = self.make_face("Safe Face", "horizontal")
        gallery_path = os.path.join(self.temp_dir.name, "safe.ttgallery")
        success, errors = export_ttgallery([face], gallery_path, "Safe")
        self.assertTrue(success, errors)

        with zipfile.ZipFile(gallery_path, "a") as archive:
            archive.writestr("../outside", b"not allowed")

        manifest, validation_errors = inspect_ttgallery(gallery_path)
        self.assertIsNone(manifest)
        self.assertTrue(any("unsafe path" in error for error in validation_errors))

    def test_rejects_duplicate_face_ids(self):
        first = self.make_face("Same Name", "horizontal")
        second = self.make_face("Same Name", "stacked")
        gallery_path = os.path.join(self.temp_dir.name, "duplicate.ttgallery")

        success, errors = export_ttgallery(
            [first, second], gallery_path, "Duplicate Gallery"
        )

        self.assertFalse(success)
        self.assertTrue(any("unique safe IDs" in error for error in errors))
        self.assertFalse(os.path.exists(gallery_path))

    def test_detects_modified_face_payload(self):
        face_path = self.make_face("Integrity Face", "stacked")
        gallery_path = os.path.join(self.temp_dir.name, "integrity.ttgallery")
        success, errors = export_ttgallery(
            [face_path], gallery_path, "Integrity Gallery"
        )
        self.assertTrue(success, errors)

        with zipfile.ZipFile(gallery_path) as archive:
            entries = {name: archive.read(name) for name in archive.namelist()}
        package_path = "faces/integrity-face.ttface"
        entries[package_path] = entries[package_path][:-1] + bytes(
            [entries[package_path][-1] ^ 1]
        )
        with zipfile.ZipFile(gallery_path, "w", zipfile.ZIP_DEFLATED) as archive:
            for name, content in entries.items():
                archive.writestr(name, content)

        manifest, validation_errors = inspect_ttgallery(gallery_path)
        self.assertIsNone(manifest)
        self.assertTrue(any("checksum" in error for error in validation_errors))

    def test_gallery_replacement_plan_includes_removals(self):
        installed = {
            "faces": [
                {"id": "keep", "sha256": "a" * 64},
                {"id": "change", "sha256": "b" * 64},
                {"id": "remove", "sha256": "c" * 64},
            ]
        }
        desired = {
            "faces": [
                {"id": "keep", "sha256": "a" * 64},
                {"id": "change", "sha256": "d" * 64},
                {"id": "add", "sha256": "e" * 64},
            ]
        }

        self.assertEqual(
            plan_gallery_changes(installed, desired),
            {"add": ["add"], "update": ["change"], "remove": ["remove"]},
        )


if __name__ == "__main__":
    unittest.main()
