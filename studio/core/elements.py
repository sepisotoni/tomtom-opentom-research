"""
TomTom Face Studio - Watch Face Elements Model & Rendering Helper
===============================================================
Manages watch face layout elements (Digital Time, Date, Custom Text, Status Icons).
Renders elements onto the 320x240 display canvas for desktop preview.
"""

from typing import Dict, Any, Tuple
from datetime import datetime
from PIL import Image, ImageDraw, ImageFont


# Built-in 2-frame monochrome icon bitmasks (16x16)
ICON_MASKS = {
    "battery_low": [
        # Frame 0: Empty battery with warning outline
        0b0000000000000000,
        0b0111111111111000,
        0b1000000000000100,
        0b1000000000000110,
        0b1011100001110110,
        0b1011100001110110,
        0b1011100001110110,
        0b1000000000000110,
        0b1000000000000110,
        0b1011100001110110,
        0b1011100001110110,
        0b1011100001110110,
        0b1000000000000100,
        0b0111111111111000,
        0b0000000000000000,
        0b0000000000000000
    ],
    "gps_fix": [
        # Location Pin / Satellite Fix icon
        0b0000011100000000,
        0b0000111110000000,
        0b0001110111000000,
        0b0001100011000000,
        0b0001100011000000,
        0b0001110111000000,
        0b0000111110000000,
        0b0000011100000000,
        0b0000001000000000,
        0b0000001000000000,
        0b0000010100000000,
        0b0000010100000000,
        0b0000100010000000,
        0b0001000001000000,
        0b0000000000000000,
        0b0000000000000000
    ],
    "weather_unavailable": [
        # Cloud with Exclamation mark icon
        0b0000011100000000,
        0b0001111111000000,
        0b0011000001100000,
        0b0110001000110000,
        0b1100001000011000,
        0b1000001000001000,
        0b1000000000001000,
        0b1100001000011000,
        0b0111001001110000,
        0b0001111111000000,
        0b0000000000000000,
        0b0000001000000000,
        0b0000001000000000,
        0b0000000000000000,
        0b0000001000000000,
        0b0000000000000000
    ]
}


def hex_to_rgb(hex_str: str) -> Tuple[int, int, int]:
    """Convert hex string (#RRGGBB) to (R, G, B) tuple."""
    hex_str = hex_str.lstrip("#")
    if len(hex_str) == 6:
        return (int(hex_str[0:2], 16), int(hex_str[2:4], 16), int(hex_str[4:6], 16))
    return (255, 255, 255)


def render_element(draw: ImageDraw.ImageDraw, element: Dict[str, Any], sim_time: datetime, frame_tick: int = 0):
    """Render an individual element onto the PIL ImageDraw context."""
    el_type = element.get("type")
    x = int(element.get("x", 0))
    y = int(element.get("y", 0))
    color_rgb = hex_to_rgb(element.get("color", "#FFFFFF"))

    font_size = int(element.get("font_size", 24))
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", font_size)
    except IOError:
        font = ImageFont.load_default()

    if el_type == "digital_time":
        fmt_str = element.get("format", "HH:MM")
        is_12h = element.get("is_12h", False)
        layout = element.get("layout", "horizontal")

        if is_12h:
            hour = sim_time.strftime("%I").lstrip("0")
            if not hour:
                hour = "12"
            time_str = f"{hour}:{sim_time.strftime('%M')}"
            if "SS" in fmt_str:
                time_str += f":{sim_time.strftime('%S')}"
            if element.get("show_ampm", True):
                time_str += f" {sim_time.strftime('%p')}"
        else:
            if "SS" in fmt_str:
                time_str = sim_time.strftime("%H:%M:%S")
            else:
                time_str = sim_time.strftime("%H:%M")

        if layout == "stacked":
            hour_text, minute_text = time_str.split(":", 1)
            if " " in minute_text:
                minute_text, suffix = minute_text.split(" ", 1)
            else:
                suffix = ""
            row_size = max(8, min(font_size, (int(element.get("height", 60)) - 4) // 2))
            try:
                row_font = ImageFont.truetype(
                    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                    row_size,
                )
            except IOError:
                row_font = ImageFont.load_default()
            lines = [hour_text, minute_text]
            if suffix:
                lines[1] += " " + suffix
            row_height = int(element.get("height", row_size * 2)) // 2
            for row, line in enumerate(lines):
                bounds = draw.textbbox((0, 0), line, font=row_font)
                line_width = bounds[2] - bounds[0]
                line_height = bounds[3] - bounds[1]
                left = x + (int(element.get("width", line_width)) - line_width) // 2
                top = y + row * row_height + (row_height - line_height) // 2
                draw.text((left, top), line, fill=color_rgb, font=row_font)
        else:
            draw.text((x, y), time_str, fill=color_rgb, font=font)

    elif el_type == "date":
        # Format string e.g. "SUN, SEP 27"
        date_str = sim_time.strftime("%a, %b %d").upper()
        draw.text((x, y), date_str, fill=color_rgb, font=font)

    elif el_type == "text":
        label_text = element.get("text", "LABEL")
        draw.text((x, y), label_text, fill=color_rgb, font=font)

    elif el_type == "status_icon":
        icon_name = element.get("icon_type", "battery_low")
        w = int(element.get("width", 24))
        h = int(element.get("height", 24))
        mask_lines = ICON_MASKS.get(icon_name)

        # Bounded animation: toggle frame on tick
        anim_blink = element.get("animate_blink", False)
        if anim_blink and (frame_tick % 2 == 1):
            # Flash / hide icon on alternate tick for preview animation
            return

        if mask_lines:
            scale_x = w / 16.0
            scale_y = h / 16.0
            for row in range(16):
                bits = mask_lines[row]
                for col in range(16):
                    if (bits & (1 << (15 - col))):
                        px = x + int(col * scale_x)
                        py = y + int(row * scale_y)
                        pw = max(1, int(scale_x))
                        ph = max(1, int(scale_y))
                        draw.rectangle([px, py, px + pw - 1, py + ph - 1], fill=color_rgb)
        else:
            # Fallback icon square
            draw.rectangle([x, y, x + w - 1, y + h - 1], outline=color_rgb, width=2)
            draw.text((x + 4, y + 2), "!", fill=color_rgb, font=font)
