#!/usr/bin/env python3
"""Convert a small transparent PNG to a static C89 RGB565 sprite header."""

import argparse
import re
from pathlib import Path

from PIL import Image


MAX_DIMENSION = 64


def rgb565(red, green, blue):
    return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)


def c_identifier(value):
    value = re.sub(r"[^A-Za-z0-9_]", "_", value)
    if not value or value[0].isdigit():
        value = f"icon_{value}"
    return value


def format_values(values, per_line=8):
    lines = []
    for start in range(0, len(values), per_line):
        lines.append("    " + ", ".join(values[start:start + per_line]))
    return ",\n".join(lines)


def convert_icon(input_path, output_path, symbol, alpha_threshold=128):
    try:
        with Image.open(input_path) as source:
            image = source.convert("RGBA")
    except OSError as error:
        raise ValueError("input must be a readable PNG; rasterize SVG artwork first") from error
    width, height = image.size
    if not width or not height or width > MAX_DIMENSION or height > MAX_DIMENSION:
        raise ValueError(
            f"icon must be between 1x1 and {MAX_DIMENSION}x{MAX_DIMENSION}"
        )

    pixels = list(image.getdata())
    colors = [f"0x{rgb565(r, g, b):04x}" for r, g, b, _ in pixels]
    mask_bytes = []
    for row in range(height):
        for byte_column in range((width + 7) // 8):
            value = 0
            for bit in range(8):
                x = byte_column * 8 + bit
                if x < width and pixels[row * width + x][3] >= alpha_threshold:
                    value |= 1 << (7 - bit)
            mask_bytes.append(f"0x{value:02x}")

    symbol = c_identifier(symbol)
    guard = c_identifier(f"TT_ICON_{symbol}_H").upper()
    output = "\n".join(
        (
            f"#ifndef {guard}",
            f"#define {guard}",
            "",
            f"#define {symbol.upper()}_WIDTH {width}",
            f"#define {symbol.upper()}_HEIGHT {height}",
            f"#define {symbol.upper()}_MASK_STRIDE {(width + 7) // 8}",
            "",
            f"static const unsigned short {symbol}_pixels[{width * height}] = {{",
            format_values(colors),
            "};",
            "",
            f"static const unsigned char {symbol}_opacity["
            f"{len(mask_bytes)}] = {{",
            format_values(mask_bytes),
            "};",
            "",
            f"#endif /* {guard} */",
            "",
        )
    )
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(output, encoding="ascii")
    return width, height


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="small PNG image with optional alpha")
    parser.add_argument("output", type=Path, help="generated C header path")
    parser.add_argument(
        "--symbol",
        help="C identifier prefix (defaults to the input filename)",
    )
    parser.add_argument(
        "--alpha-threshold",
        type=int,
        default=128,
        choices=range(256),
        metavar="0..255",
        help="minimum alpha considered opaque (default: 128)",
    )
    args = parser.parse_args()
    symbol = args.symbol or args.input.stem
    try:
        width, height = convert_icon(
            args.input, args.output, symbol, args.alpha_threshold
        )
    except ValueError as error:
        parser.error(str(error))
    print(f"Wrote {width}x{height} RGB565 icon header: {args.output}")


if __name__ == "__main__":
    main()
