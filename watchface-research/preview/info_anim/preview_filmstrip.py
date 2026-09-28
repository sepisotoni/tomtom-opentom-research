#!/usr/bin/env python3
"""Render filmstrips of the C animation module's actual output.

Uses the compiled test binary in 'dump' mode (so the numbers come from
info_anim.c, not a re-implementation) and the repo's real digit atlas.
This is a PC preview for tuning timing/layout - not proof it runs on the
device.

  gcc -std=c89 -O2 info_anim.c test_info_anim.c -o /tmp/t_ia
  python3 preview_filmstrip.py /tmp/t_ia filmstrip.png
"""
import csv, io, subprocess, sys
from PIL import Image, ImageDraw

ATLAS = "../digit-atlas-numerals.pgm"
CELL_W, CELL_H = 100, 170
BG = (8, 10, 12)
ORANGE = (255, 138, 36)
MODES = [  # title, kind, stacked, hour_only, digits
    ("Side-by-side (generic) -> info", 0, 0, 0, "1234"),
    ("Stacked (Numerals Duo) -> info", 1, 1, 0, "1234"),
    ("Stacked, hour only (min = 00) -> info", 1, 1, 1, "12"),
    ("Side-by-side, hour only -> info", 0, 0, 1, "12"),
]
FRAMES_MS = [0, 100, 200, 300, 400, 500, 600]
SCALE = 2          # 320x240 -> 160x120


def run_dump(binary, kind, stacked, hour):
    out = subprocess.check_output([binary, "dump", str(kind), str(stacked), str(hour)])
    return {int(r["t"]): r for r in csv.DictReader(io.StringIO(out.decode()))}


def draw_digit(img, atlas, ch, x, y, w, h, alpha=255):
    d = int(ch)
    cell = atlas.crop((d * CELL_W, 0, (d + 1) * CELL_W, CELL_H)).resize(
        (max(1, w // SCALE), max(1, h // SCALE)), Image.NEAREST)
    solid = Image.new("RGB", cell.size, ORANGE)
    img.paste(solid, (x // SCALE, y // SCALE), cell)


def frame(row, digits, atlas):
    img = Image.new("RGB", (320 // SCALE, 240 // SCALE), BG)
    dr = ImageDraw.Draw(img)
    div = int(row["div"])
    if div:
        dr.line([(159 // SCALE, 0), (159 // SCALE, (240 * div // 1024) // SCALE)],
                fill=(40, 70, 90))
    pa = int(row["panel"])
    if pa:   # placeholder for the weather panel; real content comes later
        c = tuple(int(v * pa / 255) for v in (110, 150, 190))
        dr.rectangle([12 // SCALE, 80 // SCALE, 110 // SCALE, 150 // SCALE], outline=c)
        dr.text((22 // SCALE, 105 // SCALE), "WEATHER", fill=c)
    for i in range(int(row["n"])):
        draw_digit(img, atlas, digits[i], int(row["x%d" % i]), int(row["y%d" % i]),
                   int(row["w%d" % i]), int(row["h%d" % i]))
    for text_x, dx, a in ((160, int(row["cd"]), int(row["ca"])),
                          (60, int(row["ld"]), int(row["la"]))):
        if a > 0:
            c = tuple(int(v * a / 255) for v in (120, 130, 140))
            dr.text(((text_x + dx - 30) // SCALE, 8 // SCALE), "TUE - SEP 29", fill=c)
    return img


def main():
    binary, out = sys.argv[1], sys.argv[2]
    atlas = Image.open(ATLAS).convert("L")
    fw, fh = 320 // SCALE, 240 // SCALE
    pad, top = 6, 16
    sheet = Image.new("RGB", (len(FRAMES_MS) * (fw + pad) + pad,
                              len(MODES) * (fh + top + pad) + pad), (30, 30, 34))
    d = ImageDraw.Draw(sheet)
    for r, (title, kind, stacked, hour, digits) in enumerate(MODES):
        rows = run_dump(binary, kind, stacked, hour)
        y0 = pad + r * (fh + top + pad)
        d.text((pad, y0), title, fill=(220, 220, 220))
        for c, t in enumerate(FRAMES_MS):
            x0 = pad + c * (fw + pad)
            sheet.paste(frame(rows[t], digits, atlas), (x0, y0 + top - 2))
            d.text((x0 + 2, y0 + top - 2 + fh - 10), "%d ms" % t, fill=(90, 90, 100))
    sheet.save(out)
    print("wrote", out, sheet.size)


if __name__ == "__main__":
    main()
