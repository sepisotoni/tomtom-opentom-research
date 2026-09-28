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
  hour-only view at `:00`. If real weather data becomes available, the time
  moves into the right half and condition/temperature appear on the left.
  Date, battery, and seconds remain ordinary details and do not trigger that
  split. The preview currently has no weather provider, so it stays centered.
- `live_watchface.c` is the Nano-X preview application. Tap the screen or send
  `SIGUSR1` to cycle through the local styles; Numerals Duo is style 5.
- `watchface.cfg` starts at Ubuntu and cycles only styles 5–8 (Numerals Duo,
  Roboto, Ubuntu, and Nunito). `cycle_start` and `cycle_count` can limit cycling
  to any contiguous range of styles available in the executable. Atlas paths
  are relative to `artwork_dir`.
The running preview accepts a face index from the USB-only `tt-control`
service through `/mnt/sdcard/opentom/preview-gallery/current_face`; indices
0–8 select the built-in styles and the four font/rounded styles. The service
listens on `192.168.101.115:18743`, implements only `PING`, `STATUS`, and
`SET_FACE <id>`, and is supervised by the device startup loop. Face selection
is written atomically and retained across renderer restarts and device
reboots. It does not provide a shell or general-purpose remote access.

### USB control service build and protocol

Build the service using the same OpenTom ARM GCC 3.3.4 toolchain used for the
watchface, then place the output at
`opentom_skel/bin/tt-control` in the deployed TomTom filesystem:

```sh
arm-linux-gcc -Wall -W -Werror -O2 \
  -o tt-control \
  watchface-research/project/src/opentom_skel/bin/tomtom-control.c
```

The startup script requires the binary as
`/mnt/sdcard/opentom/bin/tt-control` and starts it through `bin/tomtom-control`
after assigning the USB address. It checks the process at startup and
periodically while the UI is running, restarting an exited service. The daemon
listens only on `192.168.101.115:18743`, serves one short request per TCP
connection, rejects peers outside `192.168.101.0/24`, and bounds request size
and receive time. Its plain-text protocol is:

| Request | Response | Effect |
|---|---|---|
| `PING` | `OK TOMTOM_CONTROL 1` | Confirms the service is responsive |
| `STATUS` | `OK FACE <id>` | Reads the persisted built-in face ID |
| `SET_FACE <id>` | `OK FACE <id>` | Validates `0`–`8` and atomically saves the ID |

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
Compile against those inputs and write build products to a temporary or
ignored build directory; do not commit device executables or deploy them as
part of an artwork change.

Weather is not connected to a data source in this preview. Its weather state
remains unavailable until the separate telemetry/data-source work is designed
and implemented. Do not treat this visual preview as evidence of working
weather data or package installation. The `MATERIAL_WEATHER_ICON_AUDIT.md`
file inventories the related upstream icons and notes which weather conditions
are not represented in Google's set. Selected source SVGs and the Apache-2.0
license are kept in `assets/material-weather/`; the device preview does not yet
render those SVGs or receive forecast data.
