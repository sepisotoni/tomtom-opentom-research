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
- `project/applications/src/tools/Makefile`: app-specific build rules.
- `project/src/opentom_skel/start.sh`: persistent startup template.
- `project/src/opentom_skel/etc/nxmenu.cfg`: persistent menu template.
- `project/src/opentom_skel/bin/watchface_next`: helper that advances the
  running app by signal.
- `project/kernel/drivers/barcelona/gpio/gpio.c`: relevant GPIO/power-button
  source from the OpenTom checkout.
- `project/applications/src/tools/power_button.c`: current power-button helper.
- `project/kernel/drivers/char/s3c2410-rtc.c`: relevant RTC driver source.
- `project/kernel/.config`: build configuration excerpt's full source config.
- `context/HARDWARE_FACTS.md`: observed device values and config facts.
- `microwindows/`: Nano-X public headers, config, and client library.
- `baseline/watchface-current-arm`: ARM executable staged before the latest
  local source refactor; treat as baseline only, not as the current build.
- `opentom-license.txt`: project license notice.

## Findings to keep in mind

The kernel configuration has `CONFIG_RTC=m`, `CONFIG_S3C2410_RTC=y`, and
`CONFIG_S3C2410_RTC_SETTIMEOFDAY=y`, but
`CONFIG_S3C2410_RTC_GETTIMEOFDAY` is disabled. Therefore the hardware/driver
has RTC support, but that config alone does not establish that a valid
battery-backed clock is read into system time at boot. The physical RTC node,
backup-domain power source, and retained time need verification.

The power GPIO source currently reports a shutdown event after about
400–600 ms, and has a separate 10-second pre-PIC-reset path. This does not
mean the current system waits 10 seconds to shut down. Treat any change to
make ten seconds the only power-off action as a separate, hardware-sensitive
kernel behavior change.

Device metadata reported a GPS UART (`ttySAC1`, `gpstype=128`), but that is not
proof that a valid GPS time/fix is available to user space. Investigate the
actual GPS stream and timing before relying on it.

USB Ethernet at `192.168.101.115` was observed only while connected to a Linux
host. A router USB connector is not necessarily a USB Ethernet host; there is
no verified router internet, NTP, DNS, or remote-update setup yet.

The project startup currently uses `TZ=CEST-2`; replace that with a real
`Europe/Paris` timezone rule if the root filesystem includes zone data or
otherwise provide a tested daylight-saving-aware solution.

## Build in the full checkout

This handoff is intentionally compact and does not include the 211 MB
cross-toolchain or the complete sysroot. In a full LeddaZ/OpenTom checkout:

```sh
cd /path/to/LeddaZ-OpenTom
source get_cross_env.sh
make -B -C applications/src/tools watchface
```

Check warning output and confirm the result is a 32-bit ARM ELF. Do not install
the result, modify `ttsystem`, rebuild kernel modules, or reboot the device as
part of the Gemini task.
