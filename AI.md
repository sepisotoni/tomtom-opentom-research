# AI contributor entrypoint

Use this page to navigate the repository. It is not a substitute for the
task-specific source, tests, and detailed operations documents.

## Choose the correct subsystem

- Desktop authoring, package schema, canvas, export, or gallery:
  `studio/`, root `test_*.py`, and [`DOCS_FACE_STUDIO.md`](DOCS_FACE_STUDIO.md).
- Native Windows Face Studio:
  `face-studio-native/` and [`face-studio-native/README.md`](face-studio-native/README.md).
  This C++17/Win32 app is distinct from the Python reference implementation.
- Feature planning for the PC app or TomTom display:
  [`FEATURES_FOR_REVIEW.md`](FEATURES_FOR_REVIEW.md). A real extended monitor
  is not implemented by the current Face Studio executable.
- Live graphical face preview, fonts, weather card, notification animation:
  `watchface-research/preview/live_watchface.c`,
  `watchface-research/preview/info_anim/`, and
  [`watchface-research/preview/README.md`](watchface-research/preview/README.md).
- Packaged OpenTom face, boot scripts, power button, or kernel research:
  `watchface-research/project/`; begin at
  [`watchface-research/README.md`](watchface-research/README.md).
- Host-to-device USB service or retained assets:
  `tomtom-relay/`.
- Google Weather API or Render configuration:
  `weather-service/`.
- Existing device-data/reference snapshot:
  `TomTom 1 Project/`. Do not mistake it for current application source.

## Before changing code

1. Read [`docs/REPOSITORY_MAP.md`](docs/REPOSITORY_MAP.md) and the nearest
   subsystem README.
2. Confirm which executable or service owns the behavior. In particular,
   `live_watchface.c` and the packaged runtime in
   `watchface-research/project/applications/src/tools/` are separate.
   The Python and native Face Studio apps are separate implementations too;
   use the native app's own build/tests when changing `face-studio-native/`.
3. Inspect current Git changes and preserve unrelated work.
4. For device tasks, read [`docs/DEVICE_OPERATIONS.md`](docs/DEVICE_OPERATIONS.md)
   and [`docs/DEVICE_STORAGE.md`](docs/DEVICE_STORAGE.md) before connecting,
   transferring, deleting, or restarting anything.
5. Run the narrow tests for the touched code and `git diff --check`. Report
   only checks actually run.

## Hard boundaries

- Keep embedded C compatible with the legacy ANSI C89 ARM GCC 3.3.4 toolchain.
  Avoid dynamic allocation in render loops and use existing bounded helpers.
- Do not change location consent or persist Google Weather API content.
  Weather data is transient; credentials belong in local environment files,
  never in source control.
- Preserve Google Maps attribution wherever its weather content is displayed.
- Keep the device's unauthenticated control/event services on the isolated USB
  link. Telnet is unencrypted and is not a safe general network interface.
- Never silently substitute fake data, bypass validation, or claim successful
  device deployment based only on a build or source-level test.
- Do not delete unknown device files. Archive required rollback binaries to
  the host, verify the copies, then retain only the approved on-device rollback
  described in the storage policy.

## Detailed references

- [Architecture and protocols](docs/ARCHITECTURE.md)
- [Current project/device status](docs/CURRENT_STATUS.md)
- [Build and validation](docs/DEVELOPMENT.md)
- [Device access and deployment](docs/DEVICE_OPERATIONS.md)
- [Device storage/rollback policy](docs/DEVICE_STORAGE.md)
- [Claude-specific entrypoint](Claude.md) and the established
  [Claude Code instructions](CLAUDE.md)
