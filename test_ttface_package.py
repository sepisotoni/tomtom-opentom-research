import os
import json
import subprocess
import tempfile
import unittest
import zipfile


ROOT = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(
    ROOT, "watchface-research", "project", "applications", "src", "tools"
)
MICROWINDOWS = os.path.join(ROOT, "watchface-research", "microwindows")
BG_SIZE = 320 * 240 * 2


class TestTTFacePackageExtractor(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build_dir = tempfile.TemporaryDirectory(prefix="ttface-build-")
        cls.extractor = os.path.join(cls.build_dir.name, "ttface-package-test")
        subprocess.run(
            [
                "cc",
                "-std=c99",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I" + MICROWINDOWS,
                os.path.join(TOOLS, "ttface_package_test.c"),
                os.path.join(TOOLS, "ttface_package.c"),
                "-lz",
                "-o",
                cls.extractor,
            ],
            check=True,
            cwd=ROOT,
        )

    @classmethod
    def tearDownClass(cls):
        cls.build_dir.cleanup()

    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory(prefix="ttface-package-")
        self.addCleanup(self.temp_dir.cleanup)
        self.archive = os.path.join(self.temp_dir.name, "test.ttface")
        self.destination = os.path.join(self.temp_dir.name, "staged")

    def write_package(self, entries, compression=zipfile.ZIP_DEFLATED):
        with zipfile.ZipFile(self.archive, "w", compression) as package:
            for name, data in entries:
                package.writestr(name, data)

    def run_extractor(self):
        return subprocess.run(
            [self.extractor, self.archive, self.destination],
            check=False,
            capture_output=True,
            text=True,
        )

    def assert_rejected(self):
        self.assertNotEqual(self.run_extractor().returncode, 0)

    def test_extracts_deflated_and_stored_entries(self):
        manifest = b'{"manifest_version":1,"canvas":{"width":320,"height":240}}'
        background = bytes((i % 256 for i in range(BG_SIZE)))
        self.write_package([
            ("manifest.json", manifest),
            ("assets/bg.rgb565", background),
        ])

        result = self.run_extractor()
        self.assertEqual(result.returncode, 0, result.stderr)
        with open(os.path.join(self.destination, "manifest.json"), "rb") as f:
            self.assertEqual(f.read(), manifest)
        with open(os.path.join(self.destination, "assets", "bg.rgb565"), "rb") as f:
            self.assertEqual(f.read(), background)

    def test_extracts_face_studio_sample(self):
        sample = os.path.join(ROOT, "samples", "cyberpunk_retro.ttface")
        destination = os.path.join(self.temp_dir.name, "sample-staged")
        result = subprocess.run(
            [self.extractor, sample, destination],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        with open(os.path.join(destination, "manifest.json"), encoding="utf-8") as f:
            manifest = json.load(f)
        self.assertEqual(manifest["manifest_version"], 1)
        self.assertEqual(manifest["canvas"], {"width": 320, "height": 240})
        self.assertEqual(
            os.path.getsize(os.path.join(destination, "assets", "bg.rgb565")),
            BG_SIZE,
        )

    def test_rejects_path_traversal(self):
        self.write_package([
            ("manifest.json", b"{}"),
            ("assets/bg.rgb565", bytes(BG_SIZE)),
            ("../escape", b"not allowed"),
        ])
        self.assert_rejected()
        self.assertFalse(os.path.exists(os.path.join(self.temp_dir.name, "escape")))

    def test_rejects_duplicate_entry(self):
        self.write_package([
            ("manifest.json", b"{}"),
            ("manifest.json", b'{"manifest_version":1}'),
            ("assets/bg.rgb565", bytes(BG_SIZE)),
        ])
        self.assert_rejected()

    def test_rejects_wrong_asset_size(self):
        self.write_package([
            ("manifest.json", b"{}"),
            ("assets/bg.rgb565", bytes(BG_SIZE - 1)),
        ])
        self.assert_rejected()

    def test_rejects_missing_required_entry(self):
        self.write_package([("manifest.json", b"{}")])
        self.assert_rejected()

    def test_rejects_corrupt_payload_crc(self):
        self.write_package([
            ("manifest.json", b'{"manifest_version":1}'),
            ("assets/bg.rgb565", bytes(BG_SIZE)),
        ])
        with zipfile.ZipFile(self.archive, "r") as package:
            infos = package.infolist()
            contents = {info.filename: package.read(info) for info in infos}
        contents["assets/bg.rgb565"] = bytes([1]) + contents["assets/bg.rgb565"][1:]
        with zipfile.ZipFile(self.archive, "w", zipfile.ZIP_STORED) as package:
            for info in infos:
                package.writestr(info.filename, contents[info.filename])
        with open(self.archive, "r+b") as package:
            package.seek(14)
            crc_byte = package.read(1)
            package.seek(14)
            package.write(bytes([crc_byte[0] ^ 0x01]))
        self.assert_rejected()


if __name__ == "__main__":
    unittest.main()
