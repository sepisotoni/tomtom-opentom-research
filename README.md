# TomTom Face Studio and OpenTom Research

This repository combines a desktop watch-face authoring tool, an experimental
TomTom preview renderer, OpenTom device-source research, and optional weather
services. These pieces have different runtimes and deployment paths; use the
map below before changing or installing anything.

## Start here

| If you need to… | Read |
|---|---|
| Find the relevant code or distinguish similar watchface implementations | [Repository map](docs/REPOSITORY_MAP.md) |
| Understand component boundaries and data flow | [Architecture](docs/ARCHITECTURE.md) |
| Build, test, or regenerate artwork | [Development guide](docs/DEVELOPMENT.md) |
| See the last verified state and outstanding device work | [Current status](docs/CURRENT_STATUS.md) |
| Connect to, transfer files to, deploy, or inspect the TomTom | [Device operations](docs/DEVICE_OPERATIONS.md) |
| Keep device storage bounded and manage rollback files | [Device storage policy](docs/DEVICE_STORAGE.md) |
| Work on the embedded visual preview | [Preview guide](watchface-research/preview/README.md) and [visual handoff](watchface-research/preview/VISUAL_DESIGN_HANDOFF.md) |
| Run or change the desktop editor | [Face Studio guide](DOCS_FACE_STUDIO.md) |
| Deploy the host weather relay | [Relay guide](tomtom-relay/README.md) |
| Configure the Google Weather API service | [Weather service guide](weather-service/README.md) |
| Build the standalone native Windows Face Studio | [Native Studio guide](face-studio-native/README.md) |
| Review the requested feature scope before implementation | [Feature draft](FEATURES_FOR_REVIEW.md) |
| Give an AI coding agent repository context | [AI.md](AI.md), [CLAUDE.md](CLAUDE.md), or [Claude.md](Claude.md) |

## Main components

- `studio/` is the Python/PyQt desktop Face Studio and package tooling.
- `face-studio-native/` is the native C++17/Win32 Face Studio on the
  `native-face-studio` branch. It is a standalone Windows executable and is
  being reviewed separately from the Python reference app.
- `watchface-research/preview/` is the C89 Nano-X preview/gallery renderer.
  Its `live_watchface.c` is not the packaged-face runtime.
- `watchface-research/project/` contains OpenTom application, kernel, startup,
  and test sources used for device research.
- `tomtom-relay/` is an optional Rust service for the Linux host directly
  connected to the device.
- `weather-service/` is an optional Python service that calls Google's Weather
  API. Weather/location handling has explicit consent and no-store constraints.
- `TomTom 1 Project/` is a preserved device-data/reference tree, not the
  authoritative source for the preview renderer.

## Quick development checks

Run commands from the repository root:

```sh
python3 test_studio.py
python3 test_ttface_package.py
python3 test_gallery_package.py
python3 test_device_control.py
python3 test_weather_cache.py
python3 -m unittest watchface-research/project/tests/test_convert_icon.py
cargo test --manifest-path tomtom-relay/Cargo.toml
```

For the native Windows app and its host-portable core, use the build commands
in [`face-studio-native/README.md`](face-studio-native/README.md). Linux can
cross-compile the Windows `.exe` with MinGW-w64; that does not verify GUI
behavior on physical Windows.

The embedded renderer's exact ARM build additionally requires the OpenTom ARM
GCC 3.3.4 compiler, Nano-X headers/library, and matching kernel headers; see
[Development](docs/DEVELOPMENT.md). Do not treat a host-only test as proof of a
successful device build.

## Safety and scope

Device administration and file transfer are for a directly connected,
trusted USB network only. The device control and event protocols are not
general-purpose remote administration services. Never bridge them to Wi-Fi or
the public internet. Do not commit binaries, API credentials, service tokens,
GPS coordinates, cached weather payloads, or temporary device captures.

Before deployment, preserve a verified rollback copy on the host. A successful
Git push never proves that a binary is installed or working on the physical
TomTom; report source changes, builds, tests, and device checks separately.
