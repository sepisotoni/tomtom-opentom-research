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
* `/mnt/sdcard/opentom/screenshot-v1.raw` is **307,200 bytes**, but docs/DEVICE_OPERATIONS.md says a 320x240 RGB565 capture
  is 153,600. 307,200 = 320x240x4 or 320x480x2, so the real framebuffer geometry / depth / channel order is **unverified**
  (needs `fbset -s` on the device). The display receiver already requires exactly 320x240 RGB565 and checks `/dev/fb0`.
* Audio: `espeakdsp` opens `/dev/dsp`, so an OSS device may exist, but the kernel `.config` shipped in the dump enables no
  `CONFIG_SOUND`. Whether `/dev/dsp` exists and plays on this unit is **unverified**.

## Feature map

| Wish | Exists today | Missing | Who |
| --- | --- | --- | --- |
| Switch faces | TCP 18743 `SET_FACE <0-8>`, `STATUS` | nothing | done |
| Notifications / webhook | UDP 45872 `OT1\|N\|ttl\|text` | nothing (one message slot) | done |
| Mirror a window / screen to the TomTom | display receiver TCP 18745 + Studio capture | `DISPLAY_START/STOP` + clock-restore watchdog | GPT, then app |
| Windows extended monitor | IDD driver + control tool | driver compiled and tried on Windows | display agent, you |
| **Screenshot of the TomTom screen** | `/dev/fb` is readable | read-only capture endpoint (e.g. `SCREENSHOT` -> one raw frame) after confirming `fbset -s` | GPT, then app (save as PNG) |
| **Picture as face background** | face renderer + PGM atlases | a `background=` key (RGB565 320x240) loaded by the renderer; a way to put the file on the SD card | GPT; the app already converts any image to RGB565 |
| **Brightness / time format / layout / cycling / default face** | sysfs + `watchface.cfg` | fixed commands to read and change them, with validation and a safe reload | GPT, then a Settings tab |
| **Stream PC audio to the TomTom** | maybe `/dev/dsp` (unverified) | confirm audio works; a PCM receiver on the device; PC side uses WASAPI loopback capture | GPT first (feasibility), then app |
| Upload files to the device | relay serves assets to the device over HTTP (device pulls) | decide push vs pull; size/hash checks | GPT + app |
| Battery / GPS / uptime readout | `nx_bat_monitor`, `gltt` | read-only `INFO` command | GPT, then app |

## Questions for [GPT-TOMTOM] that decide the plan

1. On the running device: `ls -l /dev/dsp /dev/mixer /dev/fb*`, `cat /proc/devices`, `dmesg | grep -i -E "audio|sound|i2s|dsp"`,
   `fbset -s`. Is there working PCM output, and at what rates / channels / bit depth (try a short 16-bit mono tone)?
2. Is `/dev/fb` readable by `tomtom-control`, and what exactly is in `screenshot-v1.raw`?
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
