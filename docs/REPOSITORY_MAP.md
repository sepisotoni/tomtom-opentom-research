# Repository map

This is a navigation index, not a complete file listing. Use each directory's
local README for implementation-specific details. Paths below are relative to
the repository root.

## Root

- `README.md`: short project overview and links into these focused guides.
- `AI.md`, `CLAUDE.md`, `Claude.md`: AI-agent navigation and instructions.
- `DOCS_FACE_STUDIO.md`: desktop editor workflow and `.ttface` interchange
  contract.
- `FEATURES_FOR_REVIEW.md`: current app feature inventory and the still-open
  design choice for using the TomTom as a PC display.
- `run_studio.sh`, `build_studio.py`: launch and package the desktop editor.
- `test_studio.py`, `test_ttface_package.py`, `test_gallery_package.py`,
  `test_device_control.py`, `test_weather_cache.py`: desktop/package/service
  regression tests.
- `generate_samples.py`, `samples/`: example projects and package content.
- `TomTom 1 Project/`: large preserved device-data/reference tree. It is not
  the source tree to edit for the current renderer.

## `studio/` — desktop Face Studio

- `tomtom_face_studio.py`: PyQt application/window assembly.
- `core/canvas.py`, `elements.py`, `formats.py`, `rules.py`, and
  `data_fields.py`: editable model and validation rules.
- `core/importer.py`, `exporter.py`, `gallery.py`, `renderers.py`: image
  import, package IO, gallery, and preview rendering.
- `core/device_control.py`: bounded fixed-command client for face selection
  over the device USB control service.
- `core/weather.py`: desktop-side weather helpers and simulation.

See `DOCS_FACE_STUDIO.md` for usage and the package contract.

## `face-studio-native/` — standalone Windows Face Studio

- `src/app/main.cpp`: native Win32 editor and device-control UI.
- `src/core/`: shared C++17 canvas, rendering, package/gallery IO, validation,
  and bounded USB control client.
- `tests/`: native core/device protocol tests and Python parity checker.
- `README.md`: features, security limits, Windows/MinGW build, and current
  differences from the Python reference app.

The native app is a separate implementation. Build and validate it on its own;
do not infer that the older Python UI or a Linux ELF build is the Windows `.exe`.

## `watchface-research/` — OpenTom and preview research

- `README.md`: hardware/build handoff for the OpenTom work area.
- `GEMINI_TASK.md`: task scope and engineering constraints from the original
  device work.
- `context/HARDWARE_FACTS.md`: observed hardware facts; distinguish observations
  from hypotheses.
- `project/applications/src/tools/watchface.c`: packaged/runtime watchface
  implementation.
- `project/applications/src/tools/ttface_loader.h` and
  `ttface_package.c`: package parsing/extraction work; check the README for
  integration status before assuming boot uses it.
- `project/applications/src/tools/power_button.c` and
  `power_button_policy.h`: power-button daemon and policy.
- `project/src/opentom_skel/start.sh`: deployed startup template and daemon
  supervision; other files under `bin/` are device-side helper sources.
- `project/tests/`: host-buildable policy/parser tests.
- `baseline/`: historical executable baseline, not a build output for the
  current preview.
- `microwindows/`: Nano-X public headers/config/client library used by the
  research checkout.
- `preview/live_watchface.c`: independent Nano-X gallery renderer installed
  under `preview-gallery/`; it is not `watchface.c`.
- `preview/info_anim/`: integer/C89 animation state module and tests integrated
  by the gallery renderer.
- `preview/notification_event.h`: bounded WhatsApp-style placeholder event
  parser and timing helpers.
- `preview/assets/`: licensed references, editable source images, and
  generated device sprite headers.
- `preview/digit-atlas-*.pgm`: grayscale digit atlases loaded by the renderer.
- `preview/watchface.cfg`: face cycling, atlas names, and artwork directory.
- `preview/convert_icon.py`, `generate_font_atlas.py`,
  `generate_numerals_atlas.py`: desktop-side asset generation tools.
- `preview/README.md`, `VISUAL_DESIGN_HANDOFF.md`,
  `MATERIAL_WEATHER_ICON_AUDIT.md`: renderer, design, and asset documentation.

The preview gallery has face IDs `0`–`8`. The recent font/rounded designs are
IDs `5`–`8`: Numerals Duo, Roboto, Ubuntu, and Nunito. The selected `default_face`
in the config and the persisted `current_face` file are distinct settings.

## `tomtom-relay/` — optional Linux-host service

- `src/main.rs`: bounded HTTP API, file storage, weather forwarding, and icon
  proxy. Default bind is the direct USB host address `192.168.101.114:18744`.
- `install-local.sh`: local user-service installation.
- `install-usb-link.sh` and `usb-link.network`: optional narrow USB interface
  setup; inspect the connected USB path before installing it.
- `tomtom-relay.service`: user systemd unit.
- `README.md`: endpoints, environment variables, deployment limits, and tests.

## `weather-service/` — Google Weather API adapter

- `app.py`: standard-library HTTP service and bounded validation.
- `render.yaml`: Render Blueprint (`rootDir: weather-service`, `python app.py`).
- `test_app.py`: API behavior tests.
- `README.md`: Google Cloud, Render, consent, attribution, and no-store policy.

## Source-of-truth reminders

- A generated executable is not source; rebuild it from the matching source
  and target toolchain.
- Device contents can lag or differ from this checkout. Confirm running
  processes, deployed file paths, and status through
  [`DEVICE_OPERATIONS.md`](DEVICE_OPERATIONS.md) before making claims.
- Avoid a recursive full file dump for routine navigation; search the
  subsystem and read its focused README.
