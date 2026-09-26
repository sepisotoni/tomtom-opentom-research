# Gemini task: native OpenTom watch faces, time, and long-press shutdown

You have the uploaded `tomtom-opentomresearch` repository. Work inside
`watchface-research/project/`, following the file paths already laid out
there. Read `watchface-research/README.md` and
`watchface-research/context/HARDWARE_FACTS.md` first; inspect the supplied C
sources and `reference/apple-watch-reference.png`.

## Output must fit this device

This is an embedded TomTom ONE v6 running Linux, not a web browser or a modern
desktop. Implement the display as native C using the supplied Nano-X/
Microwindows API and old ARM GCC toolchain conventions. Do not generate HTML,
CSS, JavaScript, React, web pages, browser mockups, or other files unsuitable
for this framebuffer device. Keep changes limited to relevant C, shell,
configuration, and concise documentation files.

## Watch display

- Design a clean, colorful, readable set of clock faces inspired by the image:
  stacked large digital hour/minute digits, tasteful background/color, no
  enclosing box.
- Provide separate modular faces/pages so time, status, and later navigation
  information can be extended without rewriting a monolithic renderer.
- A tap anywhere on the screen cycles faces.
- Avoid flicker and waste: repaint the full display only on face changes or
  true expose/resize events; otherwise redraw only the digits/text/ring
  segments that changed. Keep idle CPU use low.
- Build with the repository's ARM GCC 3.3.4 / Nano-X environment where
  available. Check C compatibility with that compiler, warning-clean output
  where possible, and a 32-bit ARM executable. Do not claim device validation
  without testing on the physical device.

## Correct time across reboot

The owner believes the device's battery should keep time, but verify instead
of assuming. The supplied running-build config excerpt shows:

- `CONFIG_RTC=m`
- `CONFIG_S3C2410_RTC=y`
- `CONFIG_S3C2410_RTC_SETTIMEOFDAY=y`
- `CONFIG_S3C2410_RTC_GETTIMEOFDAY` is disabled.

Investigate the supplied RTC driver and startup behavior: determine whether
this build can read/write the RTC, whether `/dev/rtc` is actually created, and
whether the RTC backup domain remains powered by the device battery when the
device is shut down. A driver option or battery presence alone is not proof
that the clock persists. Propose a safe test: set a known time, shut down,
wait, power on, and compare RTC/system time. Do not reboot or operate the
physical device as part of your response.

The hardware metadata observed on this model reports GPS at `ttySAC1` and
`gpstype=128`, but do not assume a valid GPS fix or that its NMEA stream is
available to user space. Check existing GPS utilities/protocol. If no working
RTC read or GPS time source is verified, explain that after power loss the
correct time cannot be recovered without a network/GPS source. Any network
time sync must be optional, bounded, non-blocking during boot, and only run
when a genuine routed network is available. Use the `Europe/Paris` timezone
(including daylight-saving rules), not a permanently hardcoded UTC offset.

## Power button: ten-second hold to turn off

The owner specifically wants the device to power off only after the power
button is held continuously for 10 seconds. Research the supplied
`kernel/drivers/barcelona/gpio/gpio.c` and
`applications/src/tools/power_button.c` before proposing an implementation.
The current source has a 400–600 ms `GPIO_SHUTDOWN_TIMEOUT`, while
`GPIO_PREPIC_TIMEOUT` is 10 seconds; do not confuse these independent paths.
The current startup invokes the `power_button` utility with suspend commands.

Design the desired behavior deliberately: short press must not turn the unit
off; face navigation may remain on touchscreen taps; a sustained 10-second
press is the only intentional power-off action. Account for low-battery,
suspend, hardware-PIC reset, and charger-connected behavior. If this requires
a kernel GPIO-driver change, identify it clearly and explain rebuild/brick
risk; do not silently patch kernel code, change the shipped image, or claim
the new behavior works without hardware testing. Return a small patch and a
test plan rather than applying it to the physical TomTom.

## Network and update limits

The currently observed `192.168.101.115` interface is the TomTom's USB gadget
Ethernet address while tethered to a Linux host. A router USB socket is not
automatically a USB Ethernet host, and the TomTom does not yet have verified
router/WAN routing, DNS, NTP, or remote update service. Do not promise
always-on internet or automatic updates until the required network hardware,
routing, and secure update mechanism are proven.

## Report

Return:

1. Changed file paths and a focused patch.
2. Build/check commands and actual results.
3. Evidence-backed RTC/GPS findings and remaining physical tests.
4. Whether a 10-second-only power-off can be achieved safely, which source
   path must change, and risks.
5. Explicitly say no device-side installation/reboot was performed.

Do not change `ttsystem`, USB kernel modules, or the physical device. Keep
power-button work as a proposed source patch until it can be tested safely.
