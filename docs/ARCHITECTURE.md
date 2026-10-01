# Architecture and protocols

## Components and data flow

```text
Face Studio (Python/PyQt or native C++/Win32) ── .ttface / artwork authoring
          │
          └── TCP fixed-command client (optional face selection)
                                      │
TomTom ONE v6 ── Nano-X gallery renderer / packaged runtime (separate apps)
      │
      ├── USB TCP 18743: fixed-command control + RAM weather summary
      ├── USB UDP 45872: short-lived notification placeholder events
      ├── USB TCP 18744: weather PNG icon proxy from host relay
      ├── USB TCP 18745: experimental bounded RGB565 display-frame receiver
      └── optional GPS → weather-sync → host relay → Render → Google Weather API
```

The network ports are implementation defaults, not permission to expose the
services to a general LAN. The device and relay assume a trusted, directly
connected USB Ethernet path.

## Display runtimes

### Desktop authoring applications

`studio/` is the Python/PyQt reference app. `face-studio-native/` is a
standalone C++17/Win32 implementation with its own UI, core library, tests,
and build. The native app currently offers constrained device face selection
over the existing USB control protocol; it does not stream desktop pixels or
create a Windows virtual/extended monitor.

### Preview gallery

`watchface-research/preview/live_watchface.c` renders a 320x240 logical canvas
with Nano-X. Its direct-framebuffer build still uses Nano-X for drawing and
input: the completed RGB565 image is read from the Nano-X backbuffer and copied
to the mapped framebuffer. This is a presentation-path choice, not a text
terminal, GPU renderer, or elimination of software rasterization.

The gallery executable is separate from the packaged runtime in
`watchface-research/project/applications/src/tools/watchface.c`. Face Studio's
gallery/package code is a third, desktop-side system. Do not use a successful
test of one as proof of another.

The gallery uses:

- `watchface.cfg` for default/cycling layout and atlas paths.
- `current_face` for persisted face selection, atomically replaced by the
  control service.
- `info_anim/` for the integer-timed panel transition.
- `notification_event.h` for notification parsing, expiry, and fade timing.
- PGM digit atlases and generated RGB565/mask headers for artwork.

The font-face information card has its own 138x230 virtual coordinate surface.
`WeatherCardLayout` maps local child coordinates onto the card's animated
origin and ratio-derived dimensions. The date and battery use named card
anchors; weather, notification, alert, decoration, icon, and attribution
drawing use the same transform. New card contents should claim a bounded local
slot and must not draw at screen-global positions, so the entire card can move
or resize as one layout.

### Face Studio and `.ttface`

Face Studio edits and validates declarative projects on the host. Its package
format has dedicated Python validation and tests. Package parsing/extraction
work in the OpenTom C runtime is separate and may not be connected to the
startup path; verify the current integration before describing `.ttface` as
installed or bootable on the physical device.

## Device-side services

### `tomtom-control` — TCP 18743

The C daemon is started/supervised by `project/src/opentom_skel/start.sh`. It
accepts one bounded text command per TCP connection. Face selection is
persisted; weather summary fields are RAM-only.

| Command | Effect |
|---|---|
| `PING` | Service liveness |
| `STATUS` | Return current persisted face ID |
| `SET_FACE <0..8>` | Persist a validated face ID |
| `SET_WEATHER ...` | Update transient summary fields |
| `WEATHER_STATUS` | Return transient summary or `NONE` |
| `CLEAR_WEATHER` | Clear transient summary |

The request/response details are documented in
[`watchface-research/preview/README.md`](../watchface-research/preview/README.md).
This is a narrow application protocol, not a shell. The daemon is unauthenticated
and must remain on USB.

### Experimental display frames — TCP 18745

`watchface-research/project/src/opentom_skel/bin/tomtom-display-receiver.c`
is a standalone, not-yet-deployed framebuffer receiver. It accepts a fixed-size
320x240 RGB565 little-endian frame protocol from the USB subnet, checks the
framebuffer mode before mapping `/dev/fb0`, and rate-limits frames to 10 per
second. It uses a separate port from `tomtom-control`; do not add display data
to that command protocol.

The receiver takes over the physical framebuffer and must not run concurrently
with the gallery renderer. It is not started or supervised by `start.sh`, is
not an installed feature, and has not yet been tested on the device. The PC
side still needs an IDD frame source and bounded transport implementation.
Protocol definitions and host tests are in
`tomtom-display-protocol.[ch]` and
`watchface-research/project/tests/test_display_protocol.c`.
The exact field layout and receiver safety limitations are documented in
[`DISPLAY_STREAM.md`](DISPLAY_STREAM.md).

### Placeholder notification — UDP 45872

The renderer accepts packets from the USB subnet only. Packet shape:

```text
OT1|N|<seconds>|<printable ASCII message>
```

TTL is 1–60 seconds and text is limited to 32 printable ASCII characters.
Notifications temporarily replace weather content; they do not cross-fade over
it, and fade during the final 1.5 seconds (or the full shorter TTL). The
notification lives only in RAM. Parser/timing tests are in
`preview/notification_event_test.c`.

### Weather icon proxy — TCP 18744

The renderer requests a validated Google icon key from the host relay only when
the key changes. The relay proxies a PNG from Google's fixed static host. The
image is bounded and decoded in RAM; it is not written to the SD card. The
device-side connection is to the USB host at port 18744.

## Weather data flow and privacy

1. `weather-sync.c` reads a valid GPS RMC fix when available; otherwise it
   waits the configured fallback delay and may use the configured fallback
   location.
2. The client sends rounded coordinates only with explicit location consent
   to the host relay's `/v1/weather/display`.
3. `tomtom-relay` forwards the request to the Render service over HTTPS.
4. `weather-service/app.py` calls Google Weather API with a server-side key.
5. The response is bounded/no-store; device summary is supplied to the control
   daemon in RAM. The renderer refreshes the local summary periodically.

Weather-provider forecast data and coordinates must not be persisted or logged.
The only hourly cooldown is an in-memory keyed digest at the weather service.
Attribution is required near displayed Google weather content; the current
visual preview uses the official Maps logo asset. Review current upstream
policy before changing this.

Read each service README before altering credentials, consent, caching,
retention, routing, or public deployment.
