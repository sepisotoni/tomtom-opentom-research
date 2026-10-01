# Current project status

Last maintained: 2026-10-01. This page records verified facts and outstanding
work, not a guarantee that a currently disconnected device still has the same
state. Recheck hardware before deployment.

## Preview renderer

- Source of truth: `watchface-research/preview/live_watchface.c` plus
  `info_anim/info_anim.c` and the preview assets.
- Face IDs `5`–`8` are Numerals Duo, Roboto, Ubuntu, and Nunito.
- The font-face info card has a drawn date, battery at upper right, weather
  icon beside temperature, optional high/low and severity badge, and official
  Google Maps attribution. Display weather content only with required
  attribution.
- Placeholder notification packets replace (not overlay) weather content in
  the card. They fade during the final 1.5 seconds and are held only in RAM.
- The last preview ARM build used `-DWATCHFACE_DIRECT_FB`; it still renders
  with Nano-X and copies the completed RGB565 backbuffer to the framebuffer.
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

## Outstanding device storage work

The prior gallery directory held numerous dated `watchface.new.before-*`
executables. They were not copied off-device or deleted. A later check found
the TomTom unreachable from the Linux host (`No route to host`), so no cleanup
was completed in that session.

When the direct USB link is available again, inspect the SD card, archive and
verify older rollback executables on host storage outside Git, retain one
known-good device rollback, remove only exact verified obsolete files, and
recheck free space and runtime. Follow
[`DEVICE_STORAGE.md`](DEVICE_STORAGE.md). Never infer cleanup from a command
that ran while the target was unavailable.

## Recent local validation

The preview icon-converter suite passed four tests; animation tests passed 491
checks; the notification parser/timing test passed; the direct-framebuffer ARM
renderer build completed. These results apply to that local build/source state
and do not replace a fresh on-device verification.
