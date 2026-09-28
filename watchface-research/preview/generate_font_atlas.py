#!/usr/bin/env python3
"""Rasterize a TTF/OTF font into the TomTom preview's monochrome digit atlas."""

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


CELL_WIDTH = 100
CELL_HEIGHT = 170
SUPERSAMPLE = 4
MAX_GLYPH_WIDTH = 88
MAX_GLYPH_HEIGHT = 144
PROBE_SIZE = 200


def set_variations(font, weight, width):
    axes = font.get_variation_axes()
    if not axes:
        return

    values = []
    for axis in axes:
        name = axis["name"].decode("ascii", "replace").lower()
        default = axis["default"]
        if "weight" in name:
            value = weight
        elif "width" in name:
            value = width
        else:
            value = default
        values.append(max(axis["minimum"], min(axis["maximum"], value)))
    font.set_variation_by_axes(values)


def load_font(path, size, weight, width):
    font = ImageFont.truetype(str(path), size)
    set_variations(font, weight, width)
    return font


def build_atlas(font_path, output_path, weight, width):
    probe = load_font(font_path, PROBE_SIZE, weight, width)
    boxes = [probe.getbbox(str(digit)) for digit in range(10)]
    widest = max(box[2] - box[0] for box in boxes)
    tallest = max(box[3] - box[1] for box in boxes)
    scale = min(MAX_GLYPH_WIDTH / widest, MAX_GLYPH_HEIGHT / tallest)
    font_size = max(1, int(PROBE_SIZE * scale))
    font = load_font(font_path, font_size * SUPERSAMPLE,
                     weight, width)

    atlas = Image.new("L", (CELL_WIDTH * 10, CELL_HEIGHT), 0)
    for digit in range(10):
        glyph = Image.new(
            "L", (CELL_WIDTH * SUPERSAMPLE, CELL_HEIGHT * SUPERSAMPLE), 0
        )
        draw = ImageDraw.Draw(glyph)
        box = font.getbbox(str(digit))
        glyph_width = box[2] - box[0]
        glyph_height = box[3] - box[1]
        origin_x = (glyph.width - glyph_width) // 2 - box[0]
        origin_y = (glyph.height - glyph_height) // 2 - box[1]
        draw.text((origin_x, origin_y), str(digit), font=font, fill=255)
        glyph = glyph.resize((CELL_WIDTH, CELL_HEIGHT), Image.Resampling.LANCZOS)
        atlas.paste(glyph, (digit * CELL_WIDTH, 0))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("wb") as output:
        output.write(
            f"P5\n{atlas.width} {atlas.height}\n255\n".encode("ascii")
        )
        output.write(atlas.tobytes())
    return font_size, atlas


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=Path, help="input TTF or OTF font")
    parser.add_argument("output", type=Path, help="output 1000x170 PGM atlas")
    parser.add_argument("--weight", type=int, default=700,
                        help="variable font weight axis (default: 700)")
    parser.add_argument("--width", type=int, default=88,
                        help="variable font width axis (default: 88)")
    args = parser.parse_args()

    font_size, atlas = build_atlas(
        args.font, args.output, args.weight, args.width
    )
    font = load_font(args.font, font_size, args.weight, args.width)
    print(f"Font: {font.getname()[0]}")
    print(
        f"Raster size: {font_size}px; requested weight={args.weight}, "
        f"width={args.width}; atlas: {args.output}"
    )


if __name__ == "__main__":
    main()
