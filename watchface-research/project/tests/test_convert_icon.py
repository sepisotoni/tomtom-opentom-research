import importlib.util
import re
import tempfile
import unittest
from pathlib import Path

from PIL import Image


SCRIPT = (
    Path(__file__).resolve().parents[2]
    / "preview"
    / "convert_icon.py"
)
SPEC = importlib.util.spec_from_file_location("convert_icon", SCRIPT)
convert_icon = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(convert_icon)


class ConvertIconTests(unittest.TestCase):
    def test_emits_rgb565_pixels_and_alpha_mask(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "tiny-icon.png"
            output = root / "tiny_icon.h"
            image = Image.new("RGBA", (2, 2))
            image.putdata(
                [
                    (255, 0, 0, 255),
                    (0, 0, 0, 0),
                    (0, 255, 0, 127),
                    (0, 0, 255, 128),
                ]
            )
            image.save(source)

            self.assertEqual(
                convert_icon.convert_icon(source, output, "tiny-icon"),
                (2, 2),
            )
            header = output.read_text(encoding="ascii")
            self.assertIn("#define TINY_ICON_WIDTH 2", header)
            self.assertIn("static const unsigned short tiny_icon_pixels[4]", header)
            self.assertIn("0xf800, 0x0000, 0x07e0, 0x001f", header)
            mask = re.search(
                r"tiny_icon_opacity\[\d+\] = \{\s*([^}]+)", header
            ).group(1)
            self.assertIn("0x80", mask)
            self.assertIn("0x40", mask)

    def test_rejects_oversized_icon(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "oversized.png"
            Image.new("RGBA", (65, 1)).save(source)
            with self.assertRaisesRegex(ValueError, "64x64"):
                convert_icon.convert_icon(source, root / "out.h", "large")

    def test_wide_official_attribution_logo_uses_explicit_bound(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "maps-logo.png"
            output = root / "maps_logo.h"
            Image.new("RGBA", (105, 22), (255, 255, 255, 255)).save(source)

            self.assertEqual(
                convert_icon.convert_icon(
                    source, output, "maps_logo", max_dimension=128
                ),
                (105, 22),
            )
            header = output.read_text(encoding="ascii")
            self.assertIn("#define MAPS_LOGO_WIDTH 105", header)
            self.assertIn("#define MAPS_LOGO_HEIGHT 22", header)

    def test_c_identifier_is_safe(self):
        self.assertEqual(convert_icon.c_identifier("3-rain/icon"), "icon_3_rain_icon")


if __name__ == "__main__":
    unittest.main()
