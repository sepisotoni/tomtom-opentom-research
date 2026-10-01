# TomTom Face Studio — native Windows x64 (C++17 / Win32)

A native port of the Python/PyQt Face Studio (`studio/`). It is a single small
`.exe` with **no runtime to install**: plain C++17, Win32 + GDI for the UI, and the
Windows Imaging Component (part of Windows) to decode imported PNG/JPEG/BMP artwork.
There is no Qt, .NET, Python, PyInstaller or third-party library — JSON, ZIP/deflate,
PNG, SHA-256 and the rest are implemented in `src/core/`.

The Python version in `studio/` is untouched and remains the behavioural reference
until feature parity has been verified on real hardware.

## What it does

| Area | Native implementation |
| --- | --- |
| Canvas editor | 320×240 canvas, pencil / eraser / flood fill / eyedropper, brush size, colour picker + swatches, 30-step undo/redo, 1–4× zoom, grid, scrolling |
| Image import | PNG / JPEG / BMP (WIC) with `contain`, `cover`, `stretch`, `custom` fit and Lanczos resampling with alpha compositing |
| Elements & rules | `digital_time`, `date`, `text`, `status_icon` editor with X/Y/size/colour/rule, on-canvas selection outline, rule-driven visibility |
| Simulator | Battery slider, GPS / weather / charging toggles, live 1 Hz PC clock or manual time — always labelled **SIMULATED** |
| `.ttface` | Validation with the same messages as the reference, export, inspection |
| `.ttgallery` | Build (current face + optional existing `.ttface` files), inspect with full checksum / nested-package / preview verification, change planning |
| `.ttproj` | Save / open editor projects (same JSON + embedded PNG format) |
| Device tab | Read current face, apply a face, PING — over the fixed-command USB service (see below) |

Formats are the existing ones, not new ones. `tests/parity_check.py` proves it: for a
350-project corpus the C++ validator returns the **same error list** as
`validate_face_project()`; a `.ttface` written by C++ has a `manifest.json` and
`bg.rgb565` that are **byte-identical** to the Python export; galleries written by
either implementation are accepted by the other, and both reject the same tampering
(flipped byte, missing preview/manifest, `../` paths, unlisted files, duplicate IDs).

## Device control and security

The Device tab speaks to the service in
`watchface-research/project/src/opentom_skel/bin/tomtom-control.c` on TCP **18743** at
the TomTom's USB address (default `192.168.101.115`; host side is typically
`192.168.101.114`). It supports exactly `PING\n`, `STATUS\n` and `SET_FACE <id>\n`
and nothing else is ever sent.

* Faces 0–8: Blue Outline, Aqua Wave, Lavender, Sunset, Weather Preview, Numerals Duo, Roboto, Ubuntu, Nunito.
* **No shell, no Telnet fallback, no listener.** The app only makes outbound connections.
* The protocol is **plain text and unauthenticated**. Use a direct USB link only; never
  bridge, route or expose it to Wi-Fi, other networks or the internet. The UI says so.
  As a guard the app refuses public addresses: only literal IPv4 addresses in
  10/8, 172.16/12, 192.168/16, 169.254/16 and 127/8 are accepted (no host names, so no DNS).
* Connect timeout 3 s (Windows needs ~2 s to report a refused connection); the whole send + reply exchange is capped at 3 s (a trickled
  reply cannot extend it). Replies are capped at 64 bytes and must match
  `OK TOMTOM_CONTROL 1` / `OK FACE <single digit 0–8>` / `ERR <A-Z0-9_ ≤32>` exactly —
  leading zeros, signs, CR, control or non-ASCII bytes, trailing data and out-of-range
  IDs are rejected. `SET_FACE` must be acknowledged with the *requested* face.
* Network calls run on a worker thread; the UI never blocks and buttons are disabled while a call is in flight.

## Build

Requirements: Windows 10/11, Visual Studio 2022 (Desktop C++ workload) and CMake ≥ 3.20.

```bat
cmake -S face-studio-native -B build -A x64
cmake --build build --config Release
build\Release\facestudio_tests.exe
build\Release\TomTomFaceStudio.exe
```

Open the folder in Visual Studio ("Open a local folder") to build without the command line.
Release builds use `/MT` (static CRT, so no VC++ redistributable), `/O1`, `/GL`+`/LTCG`,
`/OPT:REF`, `/OPT:ICF`.

Cross-compiling from Linux (this is how the port was developed and smoke-tested):

```sh
sudo apt-get install cmake ninja-build g++-mingw-w64-x86-64-posix
cmake -S face-studio-native -B build-mingw -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=face-studio-native/cmake/mingw-w64-x86_64.cmake
cmake --build build-mingw
```

The Windows workflow `.github/workflows/face-studio-native.yml` builds with MSVC, runs the
tests, prints the size / imported DLLs / idle memory to the job summary and uploads
`TomTomFaceStudio.exe` as the **`TomTomFaceStudio-win-x64`** workflow artifact. No binaries are committed.

## Releases

Pushing a version tag builds the app and publishes it as a GitHub Release automatically
(`.github/workflows/face-studio-release.yml`):

```sh
git tag v1.2.0
git push origin v1.2.0        # stable release
git tag v1.2.0-rc1 && git push origin v1.2.0-rc1   # marked as a pre-release
```

* The tag must look like `vMAJOR.MINOR.PATCH` (optionally `-suffix`) and the tagged commit must already be on `main`.
* The workflow builds with MSVC, runs `facestudio_tests.exe`, stamps the version into the exe's file properties, and attaches
  `TomTomFaceStudio-<tag>-win-x64.exe` plus `SHA256SUMS.txt`. Release notes are generated from the commits since the previous release.
* Run it by hand from the Actions tab ("Release - Native Face Studio") to try it out: a manual run creates a **draft** release by default.
* The exe is not code-signed, so SmartScreen may warn on first run.

## Tests

| Suite | Command |
| --- | --- |
| Native unit tests (37: codecs, JSON, validation, rules, canvas, packages, gallery, device protocol with a fake TCP server) | `facestudio_tests` |
| C++ ↔ Python parity | `python3 face-studio-native/tests/parity_check.py <path-to-ttface_tool>` |
| Python reference regression (47) | `python3 -m unittest test_studio test_gallery_package test_device_control test_ttface_package test_weather_cache` |

The device tests cover: reply parsing, every invalid face ID, malformed / oversized /
trailing / non-ASCII replies, early close, read timeout, trickle (slowloris) timeout,
connection refused, connect timeout, exact bytes on the wire for `PING` / `STATUS` /
`SET_FACE 0..8`, and that invalid IDs or non-USB hosts never open a connection.

## Footprint

Measured on the GitHub Actions `windows-latest` runner (MSVC, Release, `/MT`, `/O1`, LTCG), run
[36858597138](https://github.com/sepisotoni/tomtom-opentom-research/actions/runs/36858597138):

* `TomTomFaceStudio.exe`: **500,736 bytes (489 KiB)**; the uploaded artifact zip is 252,590 bytes.
* Imports only OS DLLs: `USER32`, `GDI32`, `COMCTL32`, `COMDLG32`, `ole32`, `WS2_32`, `KERNEL32` — no VC++ redistributable, no runtime to install.
* Idle, 8 s after launch with the window open and the 1 Hz live preview running: **working set 19.1 MiB, private bytes 3.8 MiB**.
* The MinGW-w64 cross build used during development is 474,112 bytes (463 KiB).
* The only footprint that is not ours: JPEG/PNG/BMP decoding goes through the OS Windows Imaging Component (reached via COM, so it adds no import and no code to the exe).

## Not ported / differences (deliberate, please review)

* **Weather cache and location sharing** (`studio/core/weather.py`, `weather-service/`) are not part of this port;
  the editor still exports `data_requirements` exactly as before.
* Previews use an embedded 5×7 bitmap font scaled to the requested size, not DejaVu — layout is approximate and
  gallery preview PNGs are valid 160×120 PNGs but **not byte-identical** to the Python ones (all other gallery files are).
* Integers outside 64 bits keep their exact digits for validation messages but are otherwise treated as out of range.
* Verified under Wine 9 and in unit tests; a run on physical Windows is still owed (the CI artifact is the vehicle).

## CPU model: unresolved

The owner reports S3C2443 / ARM926EJ-S; some repo material says S3C2412. Repository evidence is mixed and nothing here
changes any firmware/kernel assumption or claims a CPU:

* `watchface-research/README.md:11` and `project/RTC_AND_GPS_INVESTIGATION.md` say Samsung **S3C2412**.
* `project/kernel/.config` sets `CONFIG_CPU_S3C2443=y` (plus `CONFIG_CPU_ARM926T=y`, `CONFIG_ARCH_S3C2410=y`, `CONFIG_MACH_TOMTOMGO=y`).
* `project/kernel/drivers/char/s3c2410-rtc.c` handles `GOCPU_S3C2443` and `GOCPU_S3C2412` together; `drivers/barcelona/gpio/gpio.c` carries `S3C2412_*` clock constants.
* `context/HARDWARE_FACTS.md` records `cputype: 4` without naming the SoC.

Both parts are ARM926EJ-S cores, so nothing in the desktop app depends on the distinction.
