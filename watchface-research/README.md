# TomTom OpenTom watch-face handoff

This directory is the focused work area for Gemini. It contains the current
watch-face source, Nano-X API/build context, device/kernel evidence, and the
visual reference. Keep new work under `watchface-research/project/` using the
existing project-relative paths.

## Target and build

- Device: TomTom ONE v6, model ID 19.
- Reported CPU: Samsung S3C2412; 320x240 framebuffer, Nano-X/Microwindows.
- OpenTom source: LeddaZ/OpenTom at commit
  `ad2646cfd48dd49f2ecfe785077a245ecb7457de`.
- Toolchain: bundled `gcc-3.3.4_glibc-2.3.2`, compiler `arm-linux-gcc`.
- Visual reference: `reference/apple-watch-reference.png`.
- Read `GEMINI_TASK.md` for scope, constraints, power-button requirement, and
  requested response.

Only native embedded-compatible C, shell, configuration, and concise
documentation belong in the implementation. Do not create HTML/CSS/JavaScript
or browser-only mockups.

## Contents

- `project/applications/src/tools/watchface.c`: current watch application.
- `preview/`: isolated Nano-X face preview and rounded Numerals Duo artwork;
  not the packaged-face runtime or the persistent startup face.
- `project/applications/src/tools/ttface_loader.h` and
  `ttface_package.c`: opt-in package parsing/rendering and bounded ZIP
  extraction under development. Extraction is not yet wired into startup.
- `project/applications/src/tools/Makefile`: app-specific build rules.
- `project/src/opentom_skel/start.sh`: persistent startup template.
- `project/src/opentom_skel/etc/nxmenu.cfg`: persistent menu template.
- `project/src/opentom_skel/bin/watchface_next`: helper that advances the
  running app by signal.
- `project/kernel/drivers/barcelona/gpio/gpio.c`: relevant GPIO/power-button
  source from the OpenTom checkout; the duration event is generic and leaves
  press-to-action policy to the user-space helper.
- `project/kernel/include/barcelona/Barc_Gpio.h`: matching button-duration
  ioctl and event structure.
- `project/applications/src/tools/power_button.c`: current power-button helper.
- `project/applications/src/tools/power_button_policy.h`: user-space
  duration-to-action policy, covered by `project/tests/power_button_duration_test.c`.
- `project/src/opentom_skel/etc/power-button.cfg`: editable thresholds and
  action paths; the current mapping is <=250 ms to toggle the panel,
  251–399 ms ignored, and >=400 ms to suspend. Send `SIGHUP` to the
  duration-aware daemon after editing to reload without rebooting.
- `project/kernel/drivers/char/s3c2410-rtc.c`: relevant RTC driver source.
- `project/kernel/.config`: build configuration excerpt's full source config.
- `context/HARDWARE_FACTS.md`: observed device values and config facts.
- `microwindows/`: Nano-X public headers, config, and client library.
- `baseline/watchface-current-arm`: ARM executable staged before the latest
  local source refactor; treat as baseline only, not as the current build.
- `opentom-license.txt`: project license notice.

The preview uses source files and editable/raster glyph atlases; generated
device executables and screenshots are kept out of this source directory.
The separate weather companion/relay path now supplies a compact RAM-only
summary to the preview renderer from a checksum-valid GPS fix, or from the
configured Tzaneen fallback location after 60 seconds without a fix.
See `preview/VISUAL_DESIGN_HANDOFF.md` for the icon/animation asset workflow
and constraints before making visual changes.

## Findings to keep in mind

The kernel configuration has `CONFIG_RTC=m`, `CONFIG_S3C2410_RTC=y`, and
`CONFIG_S3C2410_RTC_SETTIMEOFDAY=y`, but
`CONFIG_S3C2410_RTC_GETTIMEOFDAY` is disabled. Therefore the hardware/driver
has RTC support, but that config alone does not establish that a valid
battery-backed clock is read into system time at boot. The physical RTC node,
backup-domain power source, and retained time need verification.

The duration-aware GPIO source samples only the power input at 50 Hz and
reports a completed press with its measured milliseconds through
`IOR_BUTTON_EVENT`. The kernel makes no gesture/action choice; the OS-side
configuration does. Other GPIO status timing and the independent 10-second
pre-PIC reset path are preserved. The event duration is quantized to about
20 ms and should be calibrated on the device before adding more gestures.

Device metadata reported a GPS UART (`ttySAC1`, `gpstype=128`), but that is not
proof that a valid GPS time/fix is available to user space. Investigate the
actual GPS stream and timing before relying on it.

USB Ethernet at `192.168.101.115` was observed only while connected to a Linux
host. A router USB connector is not necessarily a USB Ethernet host; there is
no verified router internet, NTP, DNS, or remote-update setup yet.

The project startup applies the weather service's current location offset when
available and otherwise uses a daylight-saving-aware Paris timezone rule.

## Build in the full checkout

This handoff is intentionally compact and does not include the 211 MB
cross-toolchain or the complete sysroot. In a full LeddaZ/OpenTom checkout:

```sh
cd /path/to/LeddaZ-OpenTom
source get_cross_env.sh
make -B -C applications/src/tools watchface
```

Check warning output and confirm the result is a 32-bit ARM ELF. The GPIO
duration-interface change requires rebuilding the complete matching
`ttsystem`, not loading a generic kernel module. Keep the previous image and
startup files as rollback copies before installing or rebooting.
