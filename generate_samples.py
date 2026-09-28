"""
Generator for Sample Hand-Drawn Face Project & Exported .ttface Package
========================================================================
Creates a rich sample watch face ('cyberpunk_retro') featuring hand-drawn
pixel art, starry night background, digital time/date, status icons, and rules.
"""

import os
import json
from PIL import Image, ImageDraw

from studio.core.canvas import CanvasModel
from studio.core.exporter import export_ttface_package
from studio.core.importer import save_project_file


def create_sample():
    os.makedirs("samples", exist_ok=True)

    # 1. Create 320x240 hand-drawn pixel art background image
    canvas = CanvasModel("#0A0A1A")  # Deep space dark blue

    # Draw starry sky
    stars = [
        (15, 20), (45, 50), (85, 15), (120, 40), (160, 25), (200, 60), (240, 15), (290, 45),
        (30, 90), (70, 110), (150, 100), (220, 95), (280, 120), (105, 130), (250, 140)
    ]
    for sx, sy in stars:
        canvas.draw_pixel(sx, sy, "#FFFFFF", size=1)
        canvas.draw_pixel(sx+1, sy, "#84EBFF", size=1)

    # Draw retro synthwave glowing sun (circle at center)
    img_draw = ImageDraw.Draw(canvas.image)
    img_draw.ellipse([130, 100, 190, 160], fill=(255, 115, 65))

    # Cut horizontal scanlines across sun
    for y_line in range(125, 160, 6):
        img_draw.line([(130, y_line), (190, y_line)], fill=(10, 10, 26), width=2)

    # Draw neon perspective grid lines at bottom (y=170 to y=240)
    img_draw.line([(0, 170), (320, 170)], fill=(0, 245, 255), width=2)
    for gy in range(180, 240, 12):
        img_draw.line([(0, gy), (320, gy)], fill=(140, 40, 200), width=1)

    # Perspective vertical grid lines
    vanish_x, vanish_y = 160, 170
    for bottom_x in range(0, 321, 32):
        img_draw.line([(vanish_x, vanish_y), (bottom_x, 240)], fill=(140, 40, 200), width=1)

    # 2. Define project structure
    project_data = {
        "metadata": {
            "name": "Cyberpunk Retro",
            "version": "1.0.0",
            "author": "TomTom Face Studio"
        },
        "background_color": "#0A0A1A",
        "canvas": {"width": 320, "height": 240},
        "elements": [
            {
                "id": "main_clock",
                "type": "digital_time",
                "format": "HH:MM",
                "x": 65,
                "y": 25,
                "width": 190,
                "height": 55,
                "color": "#84EBFF",
                "font_size": 48,
                "is_12h": False,
                "rule": None
            },
            {
                "id": "date_line",
                "type": "date",
                "x": 95,
                "y": 78,
                "width": 130,
                "height": 22,
                "color": "#FF7341",
                "font_size": 18,
                "rule": None
            },
            {
                "id": "bat_warning",
                "type": "status_icon",
                "icon_type": "battery_low",
                "x": 285,
                "y": 10,
                "width": 24,
                "height": 24,
                "color": "#FF4444",
                "rule": "battery_low"
            },
            {
                "id": "gps_indicator",
                "type": "status_icon",
                "icon_type": "gps_fix",
                "x": 10,
                "y": 10,
                "width": 24,
                "height": 24,
                "color": "#00FFCC",
                "rule": "gps_fix"
            },
            {
                "id": "weather_alert",
                "type": "status_icon",
                "icon_type": "weather_unavailable",
                "x": 40,
                "y": 10,
                "width": 24,
                "height": 24,
                "color": "#FFAA00",
                "rule": "weather_unavailable"
            }
        ],
        "rules": [
            {"name": "battery_low", "signal": "battery_percent", "op": "<=", "value": 20},
            {"name": "gps_fix", "signal": "gps_status", "op": "==", "value": 1},
            {"name": "weather_unavailable", "signal": "weather_status", "op": "==", "value": 0}
        ]
    }

    # Save .ttproj
    proj_path = "samples/cyberpunk_retro.ttproj"
    save_project_file(proj_path, project_data, canvas.image)
    print(f"Saved sample project: {proj_path}")

    # Export .ttface package
    package_path = "samples/cyberpunk_retro.ttface"
    ok, errs = export_ttface_package(project_data, canvas.image, package_path)
    if ok:
        print(f"Exported sample package cleanly: {package_path}")
    else:
        print(f"Sample export failed: {errs}")


if __name__ == "__main__":
    create_sample()
