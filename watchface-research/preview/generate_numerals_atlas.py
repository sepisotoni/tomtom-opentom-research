#!/usr/bin/env python3
"""Generate smooth, rounded TomTom Numerals Duo glyph assets."""

from pathlib import Path

from PIL import Image, ImageDraw


CELL_WIDTH = 100
CELL_HEIGHT = 170
SUPERSAMPLE = 4
STROKE_WIDTH = 25
STEPS_PER_CURVE = 24
OUTPUT_DIR = Path(__file__).resolve().parent

# Each path is a start point followed by groups of three cubic Bezier points.
GLYPHS = (
    (((50, 18), (72, 18), (80, 37), (80, 84),
      (80, 131), (72, 151), (50, 151),
      (28, 151), (20, 131), (20, 84),
      (20, 37), (28, 18), (50, 18)),),
    (((43, 39), (50, 29), (58, 22), (66, 19),
      (67, 50), (64, 103), (66, 151)),),
    (((26, 43), (27, 23), (44, 17), (61, 20),
      (78, 23), (82, 38), (79, 53),
      (77, 69), (63, 81), (50, 97),
      (36, 113), (23, 129), (24, 145),
      (28, 153), (60, 158), (78, 139)),),
    (((28, 31), (39, 19), (62, 17), (73, 30),
      (86, 43), (78, 65), (54, 79),
      (76, 79), (84, 96), (79, 119),
      (75, 145), (47, 156), (29, 141)),),
    (((76, 151), (76, 111), (76, 61), (76, 20)),
     ((18, 89), (29, 70), (39, 43), (49, 23)),
     ((20, 89), (40, 85), (60, 89), (80, 89))),
    (((76, 22), (61, 22), (45, 22), (30, 22),
      (29, 38), (29, 52), (30, 67),
      (47, 64), (67, 65), (76, 79),
      (87, 92), (83, 122), (72, 140),
      (61, 158), (39, 154), (25, 140)),),
    (((70, 25), (51, 17), (34, 25), (28, 42),
      (20, 62), (21, 109), (30, 134),
      (38, 153), (62, 156), (76, 139),
      (92, 119), (78, 95), (55, 91),
      (38, 88), (27, 96), (24, 108)),),
    (((25, 23), (42, 23), (60, 23), (78, 23),
      (69, 52), (56, 101), (42, 151)),),
    (((50, 84), (26, 74), (22, 55), (34, 35),
      (44, 18), (62, 18), (72, 36),
      (83, 55), (72, 74), (50, 84)),
     ((50, 84), (26, 94), (22, 116), (34, 137),
      (44, 156), (62, 156), (73, 137),
      (84, 116), (73, 94), (50, 84))),
    (((62, 151), (76, 129), (82, 99), (78, 75),
      (78, 43), (68, 19), (49, 19),
      (28, 19), (19, 35), (19, 57),
      (19, 81), (32, 95), (51, 95),
      (68, 95), (78, 86), (78, 75)),),
)


def cubic_points(control_points):
    if (len(control_points) - 1) % 3:
        raise ValueError("Bezier paths must contain a start point and 3-point segments")
    points = []
    for offset in range(0, len(control_points) - 1, 3):
        p0, p1, p2, p3 = control_points[offset:offset + 4]
        for step in range(STEPS_PER_CURVE + 1):
            t = step / STEPS_PER_CURVE
            u = 1.0 - t
            x = u**3 * p0[0] + 3 * u**2 * t * p1[0] + 3 * u * t**2 * p2[0] + t**3 * p3[0]
            y = u**3 * p0[1] + 3 * u**2 * t * p1[1] + 3 * u * t**2 * p2[1] + t**3 * p3[1]
            points.append((round(x * SUPERSAMPLE), round(y * SUPERSAMPLE)))
    return points


def render_glyph(paths, digit):
    size = (CELL_WIDTH * SUPERSAMPLE, CELL_HEIGHT * SUPERSAMPLE)
    mask = Image.new("L", size, 0)
    draw = ImageDraw.Draw(mask)
    width = STROKE_WIDTH * SUPERSAMPLE
    radius = width // 2
    for path in paths:
        points = cubic_points(path)
        draw.line(points, fill=255, width=width, joint="curve")
        for x, y in (points[0], points[-1]):
            draw.ellipse((x - radius, y - radius, x + radius, y + radius),
                         fill=255)
    if digit == 4:
        joint_x = 76 * SUPERSAMPLE
        joint_y = 89 * SUPERSAMPLE
        joint_radius = (STROKE_WIDTH // 2 + 2) * SUPERSAMPLE
        draw.ellipse((joint_x - joint_radius, joint_y - joint_radius,
                      joint_x + joint_radius, joint_y + joint_radius),
                     fill=255)
    return mask.resize((CELL_WIDTH, CELL_HEIGHT), Image.Resampling.LANCZOS)


def svg_path(points, offset_x):
    x, y = points[0]
    chunks = [f"M {x + offset_x:.1f} {y:.1f}"]
    for offset in range(1, len(points), 3):
        c1, c2, end = points[offset:offset + 3]
        chunks.append(
            f"C {c1[0] + offset_x:.1f} {c1[1]:.1f} "
            f"{c2[0] + offset_x:.1f} {c2[1]:.1f} "
            f"{end[0] + offset_x:.1f} {end[1]:.1f}"
        )
    return " ".join(chunks)


def main():
    atlas = Image.new("L", (CELL_WIDTH * len(GLYPHS), CELL_HEIGHT), 0)
    svg_paths = []
    for digit, paths in enumerate(GLYPHS):
        atlas.paste(render_glyph(paths, digit), (digit * CELL_WIDTH, 0))
        for path in paths:
            svg_paths.append(
                f'    <path d="{svg_path(path, digit * CELL_WIDTH)}"/>'
            )

    pgm_path = OUTPUT_DIR / "digit-atlas-numerals.pgm"
    svg_pathname = OUTPUT_DIR / "digit-atlas-numerals.svg"
    with pgm_path.open("wb") as output:
        output.write(f"P5\n{atlas.width} {atlas.height}\n255\n".encode("ascii"))
        output.write(atlas.tobytes())

    svg = "\n".join((
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{atlas.width}" '
        f'height="{atlas.height}" viewBox="0 0 {atlas.width} {atlas.height}">',
        '  <rect width="100%" height="100%" fill="#000"/>',
        f'  <g fill="none" stroke="#fff" stroke-width="{STROKE_WIDTH}"',
        '     stroke-linecap="round" stroke-linejoin="round">',
        *svg_paths,
        '  </g>',
        '</svg>',
        '',
    ))
    svg_pathname.write_text(svg, encoding="utf-8")
    print(pgm_path)
    print(svg_pathname)


if __name__ == "__main__":
    main()
