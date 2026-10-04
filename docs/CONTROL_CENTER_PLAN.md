# TomTom Control Center - feature plan (grounded in the device dump)

The Windows app now opens as a **control center** (Device / Display / Live tabs). The face designer (canvas, elements,
simulator, `.ttface` / `.ttgallery`) is still there under **View > Face designer**, but is no longer the front door.
This file maps each wished-for feature to what the device already has (from a read-only dump of the device's files)
and who builds what. Facts below come from files in the dump; anything marked **unverified** has not been observed
on the running device. Device-side work belongs to [GPT-TOMTOM]; the app agent only routes to it.

The dump itself is NOT in the repo (it contains user data); only conclusions are recorded here.

## What the dump shows

* Stock TomTom system (`ttsystem`, kernel 2.6.13-LeddaZ, ARM) with the OpenTom add-ons in `/mnt/sdcard/opentom`
  (Nano-X, `start.sh` supervisor loop, `tomtom-control`, `weather-sync`, `power_button`, the watchface renderer).
* Present on the device but deliberately NOT used by the Studio: `telnetd`, `ftpd`, `dropbear` (ssh), a `vnc` binary.
  They are unauthenticated or shell-level; the Studio keeps to fixed-command services.
* Faces are rendered by the watchface binary from `watchface.cfg` keys (`layout`, `time_format`, `show_ampm`,
  `default_face`, `cycle_start`, `cycle_count`, `artwork_dir`, per-face digit atlases). There is **no background-image key**.
* Settings the OS already exposes: backlight `/sys/class/backlight/s3c/brightness` (+`max_brightness`), CPU governor
  `/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor`, suspend `/sys/power/state`, `power-button.cfg`,
  `weather_timezone_offset_minutes`.
* `/mnt/sdcard/opentom/screenshot-v1.raw` is **307,200 bytes**. Decoded as little-endian RGB565 it is **two stacked 320x240
  pages**: the first is a clean picture of the watchface, the second is all black. That fits a 320x480 virtual
  framebuffer (double buffering), not a 320x240x4 image (that decode is garbage). Which page is on screen depends on the
  framebuffer's y-offset at capture time, so a device-side `SCREENSHOT` should report it (`FBIOGET_VSCREENINFO`). Derived
  from this one sample file; unverified on the running device. The Studio's Pictures tab already opens such files
  (`TomTomFaceStudio.exe --capture <file>` also works) and saves them as PNG.
* Audio: `espeakdsp` opens `/dev/dsp`, so an OSS device may exist, but the kernel `.config` shipped in the dump enables no
  `CONFIG_SOUND`. Whether `/dev/dsp` exists and plays on this unit is **unverified**.

## Feature map

| Wish | Exists today | Missing | Who |
| --- | --- | --- | --- |
| Switch faces | TCP 18743 `SET_FACE <0-8>`, `STATUS` | nothing | done |
| Notifications / webhook | UDP 45872 `OT1\|N\|ttl\|text` | nothing (one message slot) | done |
| Mirror a window / screen to the TomTom | display receiver TCP 18745 + Studio capture | `DISPLAY_START/STOP` + clock-restore watchdog | GPT, then app |
| Windows extended monitor | IDD driver + control tool | driver compiled and tried on Windows | display agent, you |
| **Screenshot of the TomTom screen** | raw file viewer + PNG export in the Studio (Pictures tab) | read-only capture command that returns the *visible* 320x240 page (and its y-offset) | GPT, then the app fetches it |
| **Picture as face background** | Studio prepares any picture as 320x240 RGB565 (Pictures tab: fill / fit / stretch, `.rgb565` + PNG export) | a `background=` key loaded by the renderer; a way to put the file on the SD card | GPT; then a "Send to TomTom" button |
| **Brightness / time format / layout / cycling / default face** | sysfs + `watchface.cfg` | fixed commands to read and change them, with validation and a safe reload | GPT, then a Settings tab |
| **Stream PC audio to the TomTom** | maybe `/dev/dsp` (unverified) | confirm audio works; a PCM receiver on the device; PC side uses WASAPI loopback capture | GPT first (feasibility), then app |
| Upload files to the device | relay serves assets to the device over HTTP (device pulls) | decide push vs pull; size/hash checks | GPT + app |
| Battery / GPS / uptime readout | `nx_bat_monitor`, `gltt` | read-only `INFO` command | GPT, then app |

## Questions for [GPT-TOMTOM] that decide the plan

1. On the running device: `ls -l /dev/dsp /dev/mixer /dev/fb*`, `cat /proc/devices`, `dmesg | grep -i -E "audio|sound|i2s|dsp"`,
   `fbset -s`. Is there working PCM output, and at what rates / channels / bit depth (try a short 16-bit mono tone)?
2. Is `/dev/fb` readable by `tomtom-control`? `screenshot-v1.raw` decodes as two 320x240 RGB565 pages (see above): confirm the framebuffer is 320x480 virtual and say how to find the visible page.
3. Can the renderer take a background file (RGB565, 320x240) without a rebuild of every face? If it needs renderer changes,
   propose the `watchface.cfg` key and a safe hot-reload.
4. Which settings are safe to expose, with limits (brightness range, governor values, which `watchface.cfg` keys)?
5. Preferred transport for bulk data (screenshots, backgrounds, audio): keep to USB-only, token-less, fixed commands; no shell.

## Reaching the TomTom when it is plugged into another computer

The device accepts control connections only from `192.168.101.0/24` (it answers `ERR USB_ONLY` otherwise), and the Studio
refuses non-private addresses. If the TomTom is cabled to a Linux box instead of the Windows PC:

* **Face control works today through an SSH tunnel** from Windows to the Linux box, for example
  `ssh -N -L 28743:192.168.101.115:18743 user@linux-box`, then in the Device tab use host `127.0.0.1`, port `28743`.
  Nothing is exposed to the LAN, and the tunnel is authenticated and encrypted.
* Notifications (UDP), screen mirroring (TCP 18745, host hard-coded) and anything new need a small **authenticated bridge**
  on the Linux box (token required, fixed commands only, bound to a trusted LAN address), the same model as the existing
  `tomtom-relay`. That is device-side/Linux work for [GPT-TOMTOM]; the Studio will then get a "connect via bridge" option.
* Do not port-forward or NAT the raw device ports to the LAN: they are plain text and unauthenticated.

## Owner clarification (2026-10-04): access and video path

The owner prefers always-on SSH using the device's existing credentials, does
not want TFTP, and would like Studio to start/stop video display remotely.
This is a preference, not an installed configuration. The live device check
found the Dropbear executable in `/mnt/sdcard/opentom/bin/dropbear`, but no
Dropbear process was running. `telnetd` was running, and the observed root
Telnet login reached a shell without a password prompt; no verified SSH
credential or root-password state was established. Do not claim SSH is
currently available or reuse an unverified/blank root credential.

Recommended network split:

1. Keep the TomTom-facing raw interfaces on the direct USB subnet only.
   Retain TCP `18745` and the existing `TTDP` binary frame format between the
   Linux bridge and device; there is no technical need to change the device
   receiver to port `10000`.
2. The Studio sends a bounded JSON command such as
   `POST /v1/display/session` with `{"action":"start"}` or `{"action":"stop"}`
   to the Linux host over HTTPS. HTTP headers carry transport metadata
   (`Content-Type`, authorization); they are not a place to encode display
   commands or frame data. The bridge maps only fixed allowlisted actions to
   the narrow USB control service.
3. Send video as a separate binary WebSocket stream (`wss://.../v1/display/frames`)
   or equivalent bounded binary upload, not as JSON/base64 or HTTP headers.
   The host validates the session and forwards the existing fixed-size frames
   over USB to TCP `18745`. A 320x240 RGB565 stream at 10 fps is about
   1.54 MB/s before transport overhead.
4. For remote Internet use, require an authenticated encrypted path to the
   Linux host (prefer a private VPN such as the owner's existing trusted
   network/VPN, or a public HTTPS endpoint with strong per-client
   authentication, rate limits, and request bounds). HTTPS encrypts traffic
   but does not authenticate a client by itself. Do not expose the device's
   unauthenticated ports or permit an unauthenticated Internet relay. This
   host-side identity does not require adding a token to the isolated USB
   device protocol.
5. The device display supervisor must own Nano-X and the receiver
   exclusively. `start`/`stop` are idempotent; explicit stop or a receiver
   silence timeout stops the receiver and restores the clock. The Windows IDD
   agent reports a 1-second keepalive, so a proposed frame timeout is 4
   seconds (above 2.5 seconds of expected gap). This is a design proposal,
   not an implemented or physically tested watchdog.

The present physical-button configuration maps a quick press (up to 250 ms)
to `watchface-toggle-info` and a long press (at least 400 ms) to suspend;
the 250–400 ms interval has no action. The touchscreen is exposed as
`event0`, but the owner reports unreliable screen taps. A future media view
could map safe button gestures to previous/play/next, but should preserve the
long-press suspend behavior and be validated against real button events before
changing policy.

YouTube Shorts should not be implemented as native YouTube playback on this
TomTom yet. A plausible prototype is for Windows to decode/capture an
owner-selected PC window/video and stream downscaled frames (and optionally
PCM audio) through the authenticated host bridge. The TomTom is only 320x240;
the current receiver tops out at 10 fps and has no video decoder. This would
show the PC's video on the device, not make the device independently browse
YouTube. The device audio test accepted S16_LE stereo at 22,050 Hz, but
audible speaker output was not confirmed. On-device short-video fetching,
decoding, and YouTube navigation remain unverified and out of scope until
hardware/input and resource tests support them.

No SSH, bridge listener, media player, or new display-session service was
enabled by this clarification.
