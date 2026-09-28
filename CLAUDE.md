# Claude project instructions

This repository contains desktop authoring software, an experimental Nano-X
watchface preview, an OpenTom device runtime, and separate host/weather
services. Read the task-specific source and documentation before editing;
similar-looking watchface files can belong to different runtimes.

## Visual watchface work

For icon, artwork, or animation tasks, start with:

- [`watchface-research/preview/VISUAL_DESIGN_HANDOFF.md`](watchface-research/preview/VISUAL_DESIGN_HANDOFF.md)
- [`watchface-research/preview/README.md`](watchface-research/preview/README.md)
- [`watchface-research/context/HARDWARE_FACTS.md`](watchface-research/context/HARDWARE_FACTS.md)
- [`watchface-research/preview/MATERIAL_WEATHER_ICON_AUDIT.md`](watchface-research/preview/MATERIAL_WEATHER_ICON_AUDIT.md)

The actual device is a 320x240 RGB565 Nano-X display. Do not create browser
mockups or add HTML/JavaScript for embedded rendering. Keep editable artwork
and generated assets together under `watchface-research/preview/`. Use the
existing 320x240 Nano-X renderer for device-oriented previews. The C89
`watchface-research/preview/info_anim/` module is integrated into
`preview/live_watchface.c`; this is distinct from the production
packaged-face runtime.

Use `watchface-research/preview/convert_icon.py` to convert a transparent PNG
into a bounded C89 RGB565 sprite header. Commit the editable source image and
generator/tests as appropriate; do not commit compiled device executables or
temporary build output.

The full ARM GCC 3.3.4 toolchain and matching Nano-X/sysroot are not guaranteed
to exist in a GitHub-only clone. Never claim an ARM build succeeded unless it
was actually performed. C intended for this target must remain C89-compatible,
avoid heap allocation in the render loop, and be verified with the available
host compiler/tests when the target toolchain is unavailable.

## Device and network safety

- Do not deploy, restart, or reboot the TomTom unless the user asks for that
  device operation in the current task.
- Do not log, commit, or expose GPS coordinates, API tokens, or weather payloads.
- Weather content is transient and must not be cached to persistent device or
  host storage. Preserve the existing explicit location-consent and
  Google-attribution requirements.
- The direct USB control service is unauthenticated. Never expose it beyond the
  isolated USB subnet.
- Verify the actual hardware and running source before relying on values in
  older narrative/specification documents; distinguish measured facts from
  design assumptions.

## Validation and delivery

Run the narrow tests for the changed area, check `git diff --check`, and report
which checks were actually run. Keep commits focused and never stage unrelated
worktree changes.
