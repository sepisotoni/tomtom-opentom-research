"""
TomTom Face Studio - Comprehensive Test Suite
============================================
Tests canvas drawing, project save/load, .ttface package export/import,
rule engine evaluation, background path matching, and manifest validation.
"""

import os
import shutil
import unittest
from PIL import Image

from studio.core.canvas import CanvasModel
from studio.core.elements import render_element, hex_to_rgb
from studio.core.rules import RuleEngine
from studio.core.exporter import (
    validate_face_project,
    export_ttface_package,
    color_hex_to_rgb565,
    image_to_rgb565_bytes
)
from studio.core.importer import (
    save_project_file,
    load_project_file,
    inspect_ttface_package
)


class TestTomTomFaceStudio(unittest.TestCase):

    def setUp(self):
        self.test_dir = "/tmp/ttface_studio_test"
        os.makedirs(self.test_dir, exist_ok=True)

    def tearDown(self):
        if os.path.exists(self.test_dir):
            shutil.rmtree(self.test_dir)

    def test_canvas_drawing_and_undo(self):
        """Test drawing pixels, flood fill, and undo/redo operations."""
        canvas = CanvasModel("#000000")
        self.assertEqual(canvas.get_pixel_color(10, 10), "#000000")

        # Draw pixel
        canvas.push_history()
        canvas.draw_pixel(10, 10, "#FF0000", size=1)
        self.assertEqual(canvas.get_pixel_color(10, 10), "#FF0000")

        # Undo pixel draw
        self.assertTrue(canvas.undo())
        self.assertEqual(canvas.get_pixel_color(10, 10), "#000000")

        # Redo pixel draw
        self.assertTrue(canvas.redo())
        self.assertEqual(canvas.get_pixel_color(10, 10), "#FF0000")

    def test_rgb565_conversion(self):
        """Test RGB888 to RGB565 color conversion."""
        # Pure Red: 255, 0, 0 -> RGB565: (31 << 11) = 0xF800
        val_red = color_hex_to_rgb565("#FF0000")
        self.assertEqual(val_red, 0xF800)

        # 320x240 image conversion bytes length check
        img = Image.new("RGB", (320, 240), (255, 0, 0))
        data_bytes = image_to_rgb565_bytes(img, 320, 240)
        self.assertEqual(len(data_bytes), 320 * 240 * 2)

    def test_rule_engine(self):
        """Test declarative rule evaluator logic gates."""
        rules_def = [
            {"name": "battery_low", "signal": "battery_percent", "op": "<=", "value": 20},
            {"name": "gps_fix", "signal": "gps_status", "op": "==", "value": 1},
            {"name": "weather_unavailable", "signal": "weather_status", "op": "==", "value": 0}
        ]

        # Case 1: Low battery (15%) -> battery_low rule MUST be True
        state_low_bat = {"battery_percent": 15, "gps_status": 1, "weather_status": 1}
        el_bat = {"id": "bat_warn", "rule": "battery_low"}
        self.assertTrue(RuleEngine.is_element_visible(el_bat, rules_def, state_low_bat))

        # Case 2: Normal battery (80%) -> battery_low rule MUST be False
        state_norm_bat = {"battery_percent": 80, "gps_status": 1, "weather_status": 1}
        self.assertFalse(RuleEngine.is_element_visible(el_bat, rules_def, state_norm_bat))

        # Case 3: Weather unavailable (status 0) -> weather_unavailable rule MUST be True
        state_w_off = {"battery_percent": 80, "gps_status": 1, "weather_status": 0}
        el_weather = {"id": "w_warn", "rule": "weather_unavailable"}
        self.assertTrue(RuleEngine.is_element_visible(el_weather, rules_def, state_w_off))

    def test_unknown_and_invalid_rules_fail_closed(self):
        """Test rule evaluation NEVER silently shows elements for unknown or invalid rules."""
        rules_def = [
            {"name": "invalid_op_rule", "signal": "battery_percent", "op": "INVALID_OP", "value": 20},
            {"name": "invalid_sig_rule", "signal": "unsupported_sig", "op": "==", "value": 1}
        ]
        state = {"battery_percent": 10, "gps_status": 1}

        # Unknown rule name -> MUST return False
        el_unknown = {"id": "test1", "rule": "non_existent_rule"}
        self.assertFalse(RuleEngine.is_element_visible(el_unknown, rules_def, state))

        # Invalid operator rule -> MUST return False
        el_bad_op = {"id": "test2", "rule": "invalid_op_rule"}
        self.assertFalse(RuleEngine.is_element_visible(el_bad_op, rules_def, state))

        # Invalid signal rule -> MUST return False
        el_bad_sig = {"id": "test3", "rule": "invalid_sig_rule"}
        self.assertFalse(RuleEngine.is_element_visible(el_bad_sig, rules_def, state))

    def test_validation_malformed_field_types_and_out_of_bounds(self):
        """Test validation catches out of bounds, malformed types, and background path mismatches."""
        invalid_proj = {
            "metadata": {"name": "Bad Face"},
            "canvas": {"width": 320, "height": 240},
            "background": {"file": "wrong_path.rgb565"},  # Path mismatch!
            "elements": [
                {"type": "digital_time", "x": -10, "y": 0, "width": 100, "height": 40},  # Out of bounds (x < 0)
                {"type": "text", "x": 250, "y": 200, "width": 100, "height": 50},       # Out of bounds (x+w > 320)
                {"type": "date", "x": "10", "y": 10, "width": 100, "height": 40}        # Malformed field type (str)
            ],
            "rules": [
                {"name": "r1", "signal": "cpu_temperature", "op": "=="},                 # Unsupported signal
                {"name": "r2", "signal": "battery_percent", "op": "~="}                   # Unsupported operator
            ]
        }
        errors = validate_face_project(invalid_proj)
        self.assertTrue(any("extends outside 320x240 screen boundary" in err for err in errors))
        self.assertTrue(any("field 'x' must be an integer" in err for err in errors))
        self.assertTrue(any("unsupported or malformed signal" in err for err in errors))
        self.assertTrue(any("unsupported operator" in err for err in errors))
        self.assertTrue(any("must match ZIP entry 'assets/bg.rgb565'" in err for err in errors))

    def test_project_save_and_load(self):
        """Test .ttproj editor project file save and load."""
        proj_file = os.path.join(self.test_dir, "sample.ttproj")
        proj_data = {
            "metadata": {"name": "Test Face", "version": "1.0.0", "author": "Tester"},
            "elements": [{"id": "t1", "type": "digital_time", "x": 10, "y": 20, "width": 100, "height": 40}]
        }
        canvas_img = Image.new("RGB", (320, 240), "#84EBFF")

        save_project_file(proj_file, proj_data, canvas_img)
        self.assertTrue(os.path.exists(proj_file))

        loaded_data, loaded_img = load_project_file(proj_file)
        self.assertEqual(loaded_data["metadata"]["name"], "Test Face")
        self.assertEqual(loaded_img.size, (320, 240))

    def test_package_export_and_import(self):
        """Test .ttface zip package export, background path match, and inspection."""
        export_file = os.path.join(self.test_dir, "sample.ttface")
        proj_data = {
            "metadata": {"name": "Cyberpunk Sample", "version": "1.0.0", "author": "Owner"},
            "background_color": "#000000",
            "canvas": {"width": 320, "height": 240},
            "display": {
                "formats": ["horizontal", "stacked"],
                "default_format": "stacked",
            },
            "elements": [{"id": "t1", "type": "digital_time", "x": 50, "y": 50, "width": 100, "height": 40}],
            "rules": []
        }
        bg_img = Image.new("RGB", (320, 240), "#000000")

        success, errs = export_ttface_package(proj_data, bg_img, export_file)
        self.assertTrue(success, f"Export failed: {errs}")
        self.assertTrue(os.path.exists(export_file))

        # Inspect exported package
        manifest, loaded_bg, val_errs = inspect_ttface_package(export_file)
        self.assertIsNotNone(manifest)
        self.assertEqual(manifest["face_name"], "Cyberpunk Sample")
        self.assertEqual(manifest["renderer_id"], "ttface.elements.v1")
        self.assertEqual(manifest["background"]["file"], "assets/bg.rgb565")
        self.assertEqual(
            manifest["display"],
            {
                "formats": ["horizontal", "stacked"],
                "default_format": "stacked",
            },
        )
        self.assertEqual(loaded_bg.size, (320, 240))
        self.assertEqual(len(val_errs), 0)

    def test_display_format_validation(self):
        """Faces cannot declare formats absent from the shared renderer."""
        project = {
            "metadata": {"name": "Layout Test"},
            "canvas": {"width": 320, "height": 240},
            "display": {
                "formats": ["diagonal"],
                "default_format": "diagonal",
            },
            "elements": [],
            "rules": [],
        }
        errors = validate_face_project(project)
        self.assertTrue(any("unsupported format ID" in error for error in errors))

    def test_rejects_undeclared_default_format(self):
        project = {
            "metadata": {"name": "Bad Default"},
            "canvas": {"width": 320, "height": 240},
            "display": {
                "formats": ["horizontal"],
                "default_format": "stacked",
            },
            "elements": [
                {"id": "clock", "type": "digital_time", "x": 0, "y": 0,
                 "width": 100, "height": 40}
            ],
            "rules": [],
        }
        errors = validate_face_project(project)
        self.assertTrue(any("Default display format" in error for error in errors))

    def test_rejects_unknown_renderer_id(self):
        project = {
            "metadata": {"name": "Unknown Renderer"},
            "renderer_id": "opentom.unavailable.renderer.v1",
            "canvas": {"width": 320, "height": 240},
            "elements": [],
            "rules": [],
        }
        errors = validate_face_project(project)
        self.assertTrue(any("Unsupported face renderer" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
