# TomTom Face Preview

This directory holds the Nano-X preview app and the source assets it needs.
The preview is for testing face artwork on the TomTom display; it is separate
from the packaged-face runtime in `../project/applications/src/tools/` and
does not install or select a persistent face.

## Numerals Duo assets

- `generate_numerals_atlas.py` creates the rounded digit atlas from Bezier paths.
- `digit-atlas-numerals.svg` is the editable vector artwork.
- `digit-atlas-numerals.pgm` is the raster mask loaded by the preview.
- `digit-atlas.svg` / `.pgm` and `digit-atlas-solid.svg` / `.pgm` provide the
  other preview styles.
- `generate_font_atlas.py` rasterizes a local TTF/OTF into the same compact
  mask format. Roboto, Ubuntu, and Nunito fonts are kept in
  `~/Downloads/TomTom-Fonts/`; the font binaries are not copied into this repo.
- `digit-atlas-roboto.pgm`, `digit-atlas-ubuntu.pgm`, and
  `digit-atlas-nunito.pgm` back selectable font faces (styles 6, 7, and 8).
- The three font faces use a centered `HH` over `MM` layout, with an enlarged
  hour-only view at `:00`. A screen tap moves time to the right and reveals
  condition/temperature in the left panel through a 600 ms transition.
  Weather updates never open the panel automatically.
- `live_watchface.c` is the Nano-X preview application. Tap the screen or send
  `SIGUSR2` to animate the info panel; send `SIGUSR1` to cycle through local
  styles.
  Numerals Duo is style 5.
- `watchface.cfg` starts at Ubuntu and cycles only styles 5–8 (Numerals Duo,
  Roboto, Ubuntu, and Nunito). `cycle_start` and `cycle_count` can limit cycling
  to any contiguous range of styles available in the executable. Atlas paths
  are relative to `artwork_dir`.

For new icons and animation design, read
[`VISUAL_DESIGN_HANDOFF.md`](VISUAL_DESIGN_HANDOFF.md). It documents the C89 /
Nano-X constraints and the PNG-to-RGB565 sprite-header converter.
The `info_anim/` module is integrated into this renderer. A screen tap or
`SIGUSR2` opens or closes the info panel while the animation is being
evaluated; weather updates do not trigger it. The required Google attribution
is wrapped inside the info panel, and clock seconds are suppressed during the
transition to avoid drawing over the enlarged time. A test-only power-button
press also toggles the panel through the OS-side duration configuration
(`<=250 ms` quick action; `251-399 ms` no action). Face selection remains
available through Face Studio and `SIGUSR1`.
The running preview accepts a face index from the USB-only `tomtom-control`
service through `/mnt/sdcard/opentom/preview-gallery/current_face`; indices
0–8 select the built-in styles and the four font/rounded styles. The service
listens on `192.168.101.115:18743` and is supervised by the device startup
loop. `PING`, `STATUS`, and `SET_FACE <id>` manage persistent face selection;
`SET_WEATHER`, `WEATHER_STATUS`, and `CLEAR_WEATHER` manage only the current
weather summary in RAM. Face selection is written atomically and retained
across renderer restarts and device reboots. The service does not provide a
shell or general-purpose remote access.

### USB control service build and protocol

Build the service using the same OpenTom ARM GCC 3.3.4 toolchain used for the
watchface, then place the output at
`/mnt/sdcard/opentom/bin/tomtom-control` in the deployed TomTom filesystem:

```sh
arm-linux-gcc -Wall -W -Werror -O2 \
  -o tomtom-control \
  watchface-research/project/src/opentom_skel/bin/tomtom-control.c
```

The startup script assigns the USB address, then starts the daemon as
`bin/tomtom-control`; it checks the process at startup and periodically while
the UI is running, restarting an exited service. The daemon listens on
`0.0.0.0:18743` so both loopback and the USB interface work, but rejects
non-loopback peers outside `192.168.101.0/24`. It serves one short request per
TCP connection and bounds request size and receive time. Its plain-text
protocol is:

| Request | Response | Effect |
|---|---|---|
| `PING` | `OK TOMTOM_CONTROL 1` | Confirms the service is responsive |
| `STATUS` | `OK FACE <id>` | Reads the persisted built-in face ID |
| `SET_FACE <id>` | `OK FACE <id>` | Validates `0`–`8` and atomically saves the ID |
| `SET_WEATHER <condition> <C> <alert> <precip> <noteworthy>` | `OK WEATHER` | Updates the RAM-only summary |
| `WEATHER_STATUS` | `OK WEATHER ...` or `OK WEATHER NONE` | Reads the RAM-only summary |
| `CLEAR_WEATHER` | `OK WEATHER CLEARED` | Clears the RAM-only summary |

Invalid IDs and unsupported commands receive `ERR ...` responses. The daemon
has no shell, file-path, or arbitrary command interface. On the connected
TomTom it was observed using about **1.37 MiB RSS** while idle; its event loop
blocks in `select()` and does no periodic polling. TCP is not encrypted or
authenticated, so the USB network must not be bridged or exposed to another
network.

The preview keeps its 320x240 RGB565 logical canvas and draws it directly to
the Nano-X backbuffer at the device's native size. A separate scaled output
buffer is allocated only if the window size differs. Seconds update only their
small dirty regions; the base face is rebuilt on time-digit, face, battery, or
date changes.

Regenerate the rounded glyph atlas on a desktop with Pillow installed:

```sh
python3 watchface-research/preview/generate_numerals_atlas.py
```

Generate font masks from the TTFs in Downloads:

```sh
python3 watchface-research/preview/generate_font_atlas.py \
  ~/Downloads/TomTom-Fonts/Roboto-Variable.ttf \
  watchface-research/preview/digit-atlas-roboto.pgm \
  --weight 750 --width 85
python3 watchface-research/preview/generate_font_atlas.py \
  ~/Downloads/TomTom-Fonts/Ubuntu-Variable.ttf \
  watchface-research/preview/digit-atlas-ubuntu.pgm \
  --weight 700 --width 85
python3 watchface-research/preview/generate_font_atlas.py \
  ~/Downloads/TomTom-Fonts/Nunito-Variable.ttf \
  watchface-research/preview/digit-atlas-nunito.pgm \
  --weight 800 --width 88
```

Pass the optional atlases after the rounded atlas when launching the preview:

```sh
./live_watchface digit-atlas.pgm digit-atlas-solid.pgm \
  digit-atlas-numerals.pgm digit-atlas-roboto.pgm \
  digit-atlas-ubuntu.pgm digit-atlas-nunito.pgm
```

The device build needs the TomTom ARM GCC 3.3.4 toolchain, Nano-X headers and
library, and the matching Barcelona battery header. The toolchain and full
OpenTom sysroot are external build inputs, not vendored in this repository.
For this checkout, build the preview and its two companion daemons using the
corresponding local input paths:

```sh
ARM_GCC=~/projects/tomtom-opentomresearch/OpenTom/gcc-3.3.4_glibc-2.3.2/bin/arm-linux-gcc
NANOX=~/opentom-device-install/build/microwin
KERNEL=~/opentom-device-install/src/linux-s3c24xx

"$ARM_GCC" -Wall -W -Werror -O2 \
  -I"$NANOX/src/include" -I"$KERNEL/include" \
  live_watchface.c info_anim/info_anim.c \
  -L"$NANOX/src/lib" -lnano-X -lm \
  -o /tmp/watchface-weather
"$ARM_GCC" -Wall -W -Werror -O2 \
  ../project/src/opentom_skel/bin/tomtom-control.c \
  -o /tmp/tomtom-control
"$ARM_GCC" -Wall -W -Werror -O2 \
  ../project/src/opentom_skel/bin/weather-sync.c \
  -o /tmp/weather-sync
```

Use temporary or ignored build directories; do not commit generated device
executables. `start.sh` starts the GPS daemon before `weather-sync`, then
supervises both companion daemons. The worker accepts only checksum-valid RMC
sentences with an active GPS fix, sends coordinates to the host relay with
explicit location consent, and stores only the compact display summary in
device RAM. It makes at most one relay request per hour, including after an
error or a rate-limit response. The renderer reads that RAM summary every 15
seconds; this is a local cache refresh, not a weather-provider request. The
relay returns a `no-store` summary; the renderer suppresses ordinary
conditions and shows noteworthy precipitation, storms, or temperature
extremes when the panel is opened by touch. The current preview draws weather
glyphs with native
Nano-X shapes; the Material SVGs are references, not runtime assets. The
`MATERIAL_WEATHER_ICON_AUDIT.md` file inventories related upstream icon
references and licensing; selected SVGs and the Apache-2.0 license are kept
in `assets/material-weather/`.
