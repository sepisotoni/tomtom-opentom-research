# Current project status

Last maintained: 2026-10-01. This page records verified facts and outstanding
work, not a guarantee that a currently disconnected device still has the same
state. Recheck hardware before deployment.

## Preview renderer

- Source of truth: `watchface-research/preview/live_watchface.c` plus
  `info_anim/info_anim.c` and the preview assets.
- Font-face info card now uses a virtual 138x230 local coordinate surface;
  date, battery, and content elements move/scale through its common layout
  mapping. The battery has a reserved upper-right slot relative to the card.
- Face IDs `5`–`8` are Numerals Duo, Roboto, Ubuntu, and Nunito.
- The font-face info card has a drawn date, battery at upper right, weather
  icon beside temperature, optional high/low and severity badge, and official
  Google Maps attribution. Display weather content only with required
  attribution.
- Placeholder notification packets replace (not overlay) weather content in
  the card. They fade during the final 1.5 seconds and are held only in RAM.
- The currently installed preview ARM build uses `-DWATCHFACE_DIRECT_FB`; it
  still renders with Nano-X and copies the completed RGB565 backbuffer to the
  framebuffer.
- Weather summaries and icons are transient. The relay/service flow requires
  explicit location consent and must not persist location or provider data.

## Last recorded TomTom deployment

Verified on the physical device during the prior USB session:

- persisted face: `8` (Ubuntu);
- renderer path:
  `/mnt/sdcard/opentom/preview-gallery/modern-20260928/watchface.new`;
- direct framebuffer log:
  `DIRECT_FB=320x240 RGB565 stride=640 offset=0,0`;
- status response: `OK FACE 8`;
- transient weather response at that time: `OK WEATHER 4 12 3 75 1`;
- compact official Maps logo build size: 62,735 bytes;
- captured display:
  `~/Downloads/TomTom-weather-compact-map-logo-20261001T0349.png`;
- pre-install rollback:
  `watchface.new.before-compact-maps-logo-20261001T034838`.

These are historical observations; the weather response is not current weather.
No GPS coordinates, API tokens, or raw weather payloads belong in this status
file.

## Device storage cleanup (2026-10-01)

The TomTom USB Ethernet gadget appeared as `enx9ec66eaa84f0`, but its current
USB port did not match the checked-in persistent network profile. Rather than
installing a mismatched permanent profile, the host brought up that verified
USB interface and added a temporary `/32` route to the TomTom.

- Archived all **38** dated `watchface.new.before-*` binaries to
  `/home/sepisotoni/TomTomBackups/20261001-device-cleanup/`, outside Git.
- Verified every archived filename and byte size against the live device FTP
  inventory; saved per-file SHA-256 values in `SHA256SUMS.tsv`.
- Removed the 37 obsolete device backups using explicit file paths, keeping
  only `watchface.new.before-compact-maps-logo-20261001T034838` as the rollback.
- Stopped the temporary USB-only read-only FTP server and removed its device
  log.
- Verified `watchface.new` remains running (PID 326), `OK FACE 8`, and the
  renderer log still reports `DIRECT_FB=320x240 RGB565 stride=640 offset=0,0`.
- SD free space increased from 448,196 KiB to 449,980 KiB.

The archive contains the original dated backups even though only one rollback
remains on the device. The temporary host USB address is not persistent; if the
device becomes unreachable again, rediscover the USB interface and route rather
than using the possibly mismatched systemd network profile.

## Dynamic card preview (2026-10-01)

- Deployed the card-local layout build to the gallery renderer and restarted it.
- Face 8 was selected; the renderer remained on direct 320x240 RGB565 output.
- Populated the RAM-only weather display with a visual sample (rain, 12°C,
  placeholders for extremes) because `WEATHER_STATUS` was `NONE`; this is not
  live weather. The state is volatile and can be cleared with `CLEAR_WEATHER`.
- Device capture:
  `~/Downloads/TomTom-dynamic-info-card-sample-20261001T1412.png`.
- The immediately pre-change 62,735-byte renderer was archived on the host as
  `~/TomTomBackups/20261001-device-cleanup/watchface.new.before-dynamic-card-20261001T1412`;
  the existing compact-logo rollback remains on the TomTom.
- Follow-up verification capture:
  `~/Downloads/TomTom-dynamic-info-card-reopened-20261001T1425.png`.
  The 14:22 and 14:23 captures were made while the info panel was closed; they
  were not evidence of a renderer regression. Reopening it with `SIGUSR2`
  restored the visible date, battery, weather icon/data, and card-local layout.
  The card is left open on the device for visual review.
- Preview weather remains fabricated, volatile sample data, not live weather.

## Recent local validation

The preview icon-converter suite passed four tests; animation tests passed 491
checks; the notification parser/timing test passed; the direct-framebuffer ARM
renderer build completed. No Python test suites were run for the dynamic-card
change, per preference. The latest on-device capture above verifies the
card-local layout with the info panel open.

## Native Windows Face Studio branch

- Work branch: `native-face-studio`.
- Local MinGW-w64 Release build:
  `build/native-face-studio-windows/TomTomFaceStudio.exe` (474,112 bytes).
- The executable is a Windows x64 PE and imports only OS DLLs; its GUI was not
  launched on physical Windows during this build.
- The portable C++ core was built natively and its CTest suite passed.
- The checked-in app currently covers face authoring/package workflows and
  basic USB face selection. Windows desktop streaming/extended-monitor support
  and a PC notification sender are not present in this native app yet. See
  [`FEATURES_FOR_REVIEW.md`](../FEATURES_FOR_REVIEW.md).
- The owner confirmed that “extend as a display” means a true Windows second
  monitor, not a mirror. This needs a WDK-built/test-signed IDD, a separate
  Windows-to-USB frame transport, and a TomTom framebuffer receiver; none is
  implemented by the current preview `.exe`.
- On the owner PC, the Microsoft IDD sample compiled its UMDF driver DLL and
  app and generated a catalog. Its staged x64 INF passed `InfVerif /u` with
  exit code 0. The source template did not pass manual verification because
  its `$ARCH$` and `DriverVer` placeholders are intended to be stamped during
  build. This validates the basic sample toolchain only; no driver has been
  installed and no TomTom-specific transport/receiver exists yet.
