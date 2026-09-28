# Visual design and icon-animation handoff

This is the implementation brief for visual work on the TomTom face preview.
The designer may work from editable PNG/SVG artwork, but the device renderer
is native C and Nano-X: a browser rendering is not evidence that an icon fits
or animates correctly on the TomTom.

## Target and source-of-truth files

- Display: 320x240 pixels, RGB565 framebuffer.
- Graphics: Nano-X/Microwindows.
- CPU: ARM926EJ-S, no hardware floating-point unit.
- C compatibility: ANSI C89 for the legacy ARM GCC 3.3.4 toolchain. Keep
  declarations at the start of blocks; avoid C99 syntax, variable-length
  arrays, heap allocation in frame rendering, and costly floating-point work.
- `live_watchface.c` is the newer full-canvas preview and contains the current
  split-time and weather rendering. It is separate from
  `project/applications/src/tools/watchface.c`, the packaged-face runtime.
- `watchface.c` has an `OverlayDrawHook`; its current event loop waits about
  one second normally and uses a shorter wait for the existing wave face.
  `live_watchface.c` has its own event loop and must be checked separately.
- `/mnt/sdcard/opentom/preview-gallery/current_face` selects the active preview
  face. The host's Face Studio is not a general-purpose device shell or a
  replacement for testing the actual Nano-X renderer.

Keep experimental design/animation code in its own source module until the
user asks to integrate it. Do not silently alter the packaged-face loader,
boot behavior, weather consent, or the other face styles as part of a visual
prototype.

## Existing artwork and icon assets

- `digit-atlas-numerals.svg` is editable Numerals Duo vector artwork;
  `digit-atlas-numerals.pgm` is the preview's grayscale atlas.
- `digit-atlas-*.pgm` files are compact digit masks used by `live_watchface.c`.
- `assets/material-weather/` contains selected Apache-2.0 Material Design SVG
  references and its license. The preview draws native C weather symbols; it
  does not rasterize or load these SVGs at runtime.
- `generate_numerals_atlas.py` and `generate_font_atlas.py` show the existing
  Pillow-based source-to-atlas approach.
- `convert_icon.py` converts a transparent PNG to a bounded C89 RGB565 sprite
  and 1-bit opacity mask. It rejects artwork larger than 64x64 rather than
  silently resampling a design.

For a new icon, keep the editable source (SVG or RGBA PNG) and generated C
header. Use a descriptive name, document the source/license, and inspect the
icon at both native size and the intended scaled size. A transparent PNG can
be converted with:

```sh
python3 watchface-research/preview/convert_icon.py \
  icon-source.png \
  watchface-research/preview/generated/weather_icon.h \
  --symbol weather_icon
```

The header contains static RGB565 colors plus an opacity mask, so black is a
valid opaque color and does not need to be reserved as a transparency key.
Alpha values below 128 are transparent by default.
For pixel `(x, y)`, the opacity bit is in
`icon_opacity[y * ICON_MASK_STRIDE + x / 8]`, with bit
`7 - (x % 8)` set for opaque pixels. Export SVG concepts to a transparent PNG
before conversion; the runtime does not parse SVG.

## Animation design and implementation constraints

Before coding, specify the trigger, total duration, target frame rate, motion
curve, end state, and exact icon bounds. Prefer a short one-shot entrance or
subtle pulse that returns to a static low-power state. Ordinary weather should
not animate continuously.

Implementation guidance:

1. Represent time, phase, position, and scale with integer/fixed-point state.
   Avoid `sin`, floating point, and per-frame allocation.
2. Keep the animation state in a small struct and derive each frame from an
   elapsed monotonic tick. Do not advance by assuming every frame arrived on
   schedule.
3. Use the smallest practical bounding box, restore the old rectangle from the
   face/backbuffer before drawing the next frame, and redraw the complete face
   after an expose, resize, face change, or animation completion as needed.
4. Use Nano-X calls already supported by the target headers. Do not assume
   alpha blending or GPU acceleration; use the generated opacity mask or
   opaque RGB565 pixels.
5. Keep the normal event wait long while static. During an active animation,
   use a bounded short timeout (typically 30-50 ms), then return to the
   ordinary idle wait. Avoid busy loops and do not add background polling.
6. Test animation boundaries: first/last frame, wrap or completion, time jumps,
   face changes mid-animation, and display expose. State clearly whether the
   result is an in-device implementation, host-only logic test, or static
   artwork preview.

Do not claim a full ARM build is impossible solely because the compiler is not
checked into this repository: a compatible compiler may exist in a separate
full OpenTom checkout. Conversely, do not claim the ARM build passed unless
that exact build was run. The full build also needs the matching Nano-X library
and Barcelona headers/sysroot; see the build recipe in `README.md`.

## Hand-off checklist for a visual change

- Include the editable source asset and license/source attribution.
- Include the converter command and generated dimensions/format.
- State the exact C file/module that consumes the asset and its runtime scope.
- Provide before/after captures at 320x240 if available; never substitute a
  browser-only mockup for the device-renderer preview.
- Run the icon converter tests, strict C syntax/build checks that are
  available, and `git diff --check`.
- Do not deploy or restart the physical TomTom unless explicitly requested.
