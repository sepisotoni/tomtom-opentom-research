# TomTom Face Studio - Desktop Authoring Guide & Interchange Contract

## 1. Overview

**TomTom Face Studio** is a desktop authoring tool for designing watch faces for the **TomTom ONE (v6 / Model ID 19)** hardware (Samsung S3C2412 ARM926EJ-S processor, 320x240 Nano-X framebuffer display).

It enables watch face creators to:
- Hand-draw pixel artwork on a 320x240 canvas with grid display and zoom.
- Import external PNG/JPG artwork, fit/crop/position it, and preview it at exact device scale.
- Arrange display elements (digital time, date, custom text, and status icons).
- Preview live time (1Hz clock simulation) and test device states (battery low, GPS fix, weather offline) via a built-in hardware simulator.
- Apply constrained declarative rules to control element visibility without writing C code or executing arbitrary scripts on the device.
- Validate and export self-contained `.ttface` watch face packages ready for Nano-X blitting.

---

## 2. Launching & Building the Studio

### Quick Start (Linux / Windows)

```bash
# Option A: Run directly using python3 & PyQt5
./run_studio.sh

# Option B: Run Python package directly
python3 -m studio.tomtom_face_studio
```

### Running Unit Tests

```bash
python3 test_studio.py
```

### Building Standalone Desktop Executable

```bash
python3 build_studio.py
```

* **Linux Output**: `dist/TomTomFaceStudio/TomTomFaceStudio` (Linux 64-bit ELF executable).
* **Windows Output**: The Python/PyInstaller build remains host-specific. For a native Windows `.exe`, use the separate C++ implementation in [`face-studio-native/`](face-studio-native/README.md); it can be cross-compiled from Linux with MinGW-w64.

---

## 3. Workflow & Usage

### 3.1 Hand Drawing Pixel Art
1. Select a tool from the **Hand Drawing Tools** panel (**Pencil**, **Eraser**, **Fill Bucket**, **Eyedropper**).
2. Choose a brush size (1px to 8px) and active color from the color swatches or color picker dialog.
3. Click or drag on the 320x240 canvas to draw.
4. Toggle **Show Pixel Grid** or change **Zoom** (1x to 4x) for precision pixel editing.
5. Use **Undo** / **Redo** to step through drawing history.

### 3.2 Importing Background Artwork
1. Under **Import Background Artwork**, click **Import PNG / JPG Artwork...**.
2. Select an image file.
3. Choose a fit mode:
   - `contain`: Scales image down to fit inside 320x240 while maintaining aspect ratio.
   - `cover`: Fills 320x240 completely, cropping edges if needed.
   - `stretch`: Stretches image to exactly 320x240.
   - `custom`: Places image at exact X/Y offsets.
4. The desktop studio automatically rasterizes and composites the artwork onto the canvas.

### 3.3 Adding & Arranging Display Elements
1. On the **Elements** tab, click:
   - `+ Time`: Adds digital time display (HH:MM or HH:MM:SS, 12h/24h formats).
   - `+ Date`: Adds formatted date text (e.g. "SUN, SEP 27").
   - `+ Text`: Adds custom text labels.
   - `+ Icon`: Adds 16x16 status icons (`battery_low`, `gps_fix`, `weather_unavailable`).
2. Adjust element properties in **Element Properties**:
   - `Pos X`, `Pos Y`: Top-left coordinate.
   - `Font Size`: Integer font height in pixels.
   - `Color`: Hex color picker.
   - `Rule Binding`: Bind visibility to a declarative signal (`None`, `battery_low`, `gps_fix`, `weather_unavailable`).

### 3.4 Live Preview & Device State Simulation
1. Switch to the **Simulator** tab.
2. Drag the **Battery Level** slider (0–100%):
   - Setting battery <= 20% triggers `battery_low` and automatically reveals low battery warning icons.
3. Toggle **GPS Satellite Fix Acquired**:
   - Toggles `gps_fix` signal state to test location pin visibility.
4. Toggle **Weather Telemetry Offline**:
   - Toggles `weather_unavailable` signal state to test offline weather warning icons.
5. Watch the live 1Hz preview clock tick in real-time.

### 3.5 Validating & Exporting `.ttface` Packages
1. Open the **Export** tab.
2. Enter `Face Name`, `Author`, and `Version`.
3. Click **Validate Project Schema**:
   - Verifies canvas resolution is integer 320x240.
   - Validates element field types (`x`, `y`, `width`, `height` must be integers; non-integer or boolean types are rejected).
   - Rejects out-of-bounds elements (`x < 0`, `y < 0`, `x + width > 320`, `y + height > 240`).
   - Rejects unsupported signals and operators.
   - Enforces manifest background file matching ZIP entry (`assets/bg.rgb565`).
4. Click **Export .ttface Package...** to select a location and generate the `.ttface` zip package.
5. Under **Formats Supported by This Face**, select only formats this face
   supports, then choose one of those as its default. The editor preview
   updates immediately. A face can support one, several, or (if it has no
   digital clock) no time formats.

### 3.6 Building a `.ttgallery` Collection
1. Click **Build .ttgallery Collection...**.
2. Optionally select other `.ttface` packages to include with the face currently
   open in the editor.
3. Save the collection. It contains `gallery.json`, validated nested face
   packages, 160x120 previews, and SHA-256 hashes.
4. To add or remove faces, build a new gallery with the desired set. The
   manifest describes the complete collection, not incremental changes.
   `plan_gallery_changes()` reports additions, changed packages, and removals
   when comparing an installed manifest to the replacement.

The format list is a capability declaration, not a request for the device to
invent a renderer. Only IDs in the shared renderer registry can be selected.
Adding a new format requires implementing and testing its renderer in the
shared desktop and TomTom C runtimes, then registering its ID. Existing faces
that do not declare it will remain unaffected.

### 3.7 Packaging the Original OpenTom Faces

`.ttface` manifests include a `renderer_id`. The default
`ttface.elements.v1` renderer retains the declarative background-and-elements
behavior. Five registered `opentom.builtin.*.v1` IDs package the original
Frost Outline, Hydro Aqua Wave, Solid Lavender, Vivid Sunset, and Real
Telemetry faces without flattening their dynamic C rendering into screenshots.
The device runtime recognizes those IDs when a package is staged in
`/mnt/sdcard/opentom/faces/active/` and the packaged face slot is selected.

Build individual packages and a validated `.ttgallery` collection with:

```bash
python3 watchface-research/package_legacy_faces.py
```

The output defaults to `~/Downloads/TomTom-Legacy-Faces/`. The gallery is an
archive format; device-side gallery installation and synchronization are
separate and are not implied by generating the package.

---

## 4. The `.ttface` Package Interchange Contract

A `.ttface` file is a versioned ZIP archive containing:

```text
package.ttface
├── manifest.json         # Package manifest & element logic definitions
└── assets/
    └── bg.rgb565         # Raw 320x240 16-bit RGB565 little-endian binary (153,600 bytes)
```

### Manifest Schema (`manifest.json`)

```json
{
  "manifest_version": 1,
  "face_name": "Cyberpunk Retro",
  "version": "1.0.0",
  "author": "TomTom Face Studio",
  "target_device": "TomTom ONE v6 (Model 19)",
  "canvas": {
    "width": 320,
    "height": 240
  },
  "display": {
    "formats": ["horizontal", "stacked"],
    "default_format": "horizontal"
  },
  "background": {
    "type": "raw_rgb565",
    "file": "assets/bg.rgb565",
    "color": "#0A0A1A"
  },
  "elements": [
    {
      "id": "main_clock",
      "type": "digital_time",
      "format": "HH:MM",
      "x": 65,
      "y": 25,
      "width": 190,
      "height": 55,
      "color": "#84EBFF",
      "font_size": 48,
      "is_12h": false,
      "rule": null
    },
    {
      "id": "bat_warning",
      "type": "status_icon",
      "icon_type": "battery_low",
      "x": 285,
      "y": 10,
      "width": 24,
      "height": 24,
      "color": "#FF4444",
      "rule": "battery_low"
    }
  ],
  "rules": [
    {
      "name": "battery_low",
      "signal": "battery_percent",
      "op": "<=",
      "value": 20
    }
  ]
}
```

### Hardware Resource & Memory Limits

| Resource | Value / Limit | Rationale |
|---|---|---|
| **Framebuffer Resolution** | 320 x 240 pixels | Native TomTom ONE LCD resolution |
| **Color Depth** | 16-bit RGB565 | Match Nano-X framebuffer format |
| **Background Raw Asset Size** | Exactly 153,600 bytes | `320 * 240 * 2` bytes (0 CPU decompression overhead) |
| **Max Elements per Face** | 16 elements | Maintain < 0.1% CPU target on 266 MHz S3C2412 |
| **Max Logic Rules** | 8 rules | Keep runtime state evaluation deterministic |

---

## 5. Summary of Feature Status

### Implemented & Tested
- [x] Strict `.ttface` contract: `manifest.json` background file path (`assets/bg.rgb565`) matches ZIP entry.
- [x] Strict validation: Rejects malformed field types (non-integer coordinates/dimensions), out-of-bounds elements, unsupported operators, and unsupported signals.
- [x] Fail-closed rule evaluation: Unknown or invalid rules return `False` and never silently display elements.
- [x] Hand-drawing toolkit: Pencil, Eraser, Fill Bucket, Eyedropper, Clear, Undo/Redo stack.
- [x] Background artwork import (PNG/JPG) with contain/cover/stretch/custom fitting modes.
- [x] Dynamic element placement: Digital time (12h/24h), Date, Custom Text, Status Icons.
- [x] Hardware state simulator for instant desktop preview.
- [x] Schema validator & `.ttface` zip package exporter.
- [x] Shared display-format registry and per-face supported/default format
  declarations with preview rendering.
- [x] Face-declared data add-ons registry (`data_requirements`) and union mapping in `.ttgallery` package manifests (`provider_capabilities`).
- [x] Bounded hourly weather forecast cache service with strict <= 4 KiB cache limit, stale marking (`is_stale`), UTC timestamping, and deterministic `FakeWeatherProvider`.
- [x] Privacy controls with location sharing disabled by default and zero raw GPS coordinate logging/retention.
- [x] `.ttgallery` builder with nested face validation, generated previews,
  checksums, and host-side integrity inspection.
- [x] Editor project save/load (`.ttproj` format).
- [x] C loader header interface ([`watchface-research/project/applications/src/tools/ttface_loader.h`](file:///home/sepisotoni/projects/tomtom-opentom-research/watchface-research/project/applications/src/tools/ttface_loader.h)).
- [x] Bundled GCC 3.3.4 warning-clean build verification (`-Wall -W -Werror`).
- [x] Complete Python and C test suites (`test_studio.py`, `test_weather_cache.py`, `ttface_loader_test.c`).
- [x] Sample project ([`samples/cyberpunk_retro.ttproj`](file:///home/sepisotoni/projects/tomtom-opentom-research/samples/cyberpunk_retro.ttproj)) & package ([`samples/cyberpunk_retro.ttface`](file:///home/sepisotoni/projects/tomtom-opentom-research/samples/cyberpunk_retro.ttface)).

---

## 6. Face-Declared Data Add-ons & Weather Cache Contract

### 6.1 Data Requirements Schema (`data_requirements`)

Faces declare required data field add-ons in `manifest.json`:

```json
{
  "manifest_version": 1,
  "face_name": "Cyberpunk Neon",
  "data_requirements": [
    "battery.percent",
    "gps.fix",
    "weather.current.temperature",
    "weather.current.condition"
  ]
}
```

#### Supported Field Registry (`SUPPORTED_DATA_FIELDS`)

| Field ID | Description | Category | Capability |
|---|---|---|---|
| `battery.percent` | Battery percentage level (0-100%) | `battery` | `device-telemetry-v1` |
| `gps.fix` | GPS fix status boolean | `gps` | `device-gps-v1` |
| `weather.current.temperature` | Current outdoor temperature (°C) | `weather` | `weather-provider-v1` |
| `weather.current.condition` | Weather condition summary string | `weather` | `weather-provider-v1` |
| `weather.hourly.temperature` | 24h hourly forecast temperatures | `weather` | `weather-provider-v1` |
| `weather.hourly.condition` | 24h hourly forecast conditions | `weather` | `weather-provider-v1` |
| `weather.hourly.precipitation_probability` | 24h hourly precipitation chance | `weather` | `weather-provider-v1` |
| `weather.daily.condition` | 7-day forecast daily condition summaries | `weather` | `weather-provider-v1` |

Faces that omit `data_requirements` default to `[]` for backward compatibility. Faces that request no weather fields do not trigger weather network fetching.

### 6.2 Weather Cache & Refresh Contract

- **Single Shared Cache**: One central `SharedWeatherService` serves the union of requested data fields across all faces.
- **Hourly Rate Limiter**: Refresh occurs no more often than once per hour (`min_refresh_interval_sec = 3600`).
- **Strict Size Bound**: Total serialized cache is strictly capped at **<= 4 KiB (4,096 bytes)**. Excessive forecast items are truncated or rejected upon serialization.
- **Offline & Stale Data**: When offline or when GPS fix is missing, expired forecast records are dropped and valid remaining cache data is marked with `"is_stale": true`.
- **Privacy & Coordinates**: Telemetry location sharing is disabled by default (`location_sharing_enabled = False`). No raw GPS coordinates are written to disk, stored in cache, logged, or included in exception text.

### 6.3 Hardware GPS Status & Device Findings

- **Device NMEA Status**: TTY `SAC1` NMEA feed on connected TomTom hardware was inspected; no active NMEA output was available and `/mnt/sdcard/opentom/bin/gltt` was missing on hardware.
- **Honest Fail-Closed Design**: The desktop app and weather service fail closed when no dated GPS fix is present. Simulated data in the editor is clearly labelled **[SIMULATED DEVICE DATA]** and is never presented as live device telemetry.

---

## 7. Remaining Device-Side Firmware Work (For TomTom Developer)

1. **Package Activation**: The bounded `ttface_unpack_archive()` helper has host tests, but it is not wired into startup or the watchface. Add an explicit, safe activation flow that validates the package and preserves rollback before extracting into the active-face directory.
2. **Runtime Validation**: The device renderer and manifest parser are still prototypes. Validate manifest schema, bounds, text, and rule behavior in C before relying on exported packages.
3. **Hardware Telemetry**: Replace prototype state values with verified battery and GPS readings. Weather data has no provider or runtime path yet; do not display simulated weather as live data.

The separate Nano-X artwork preview and Numerals Duo glyph sources are in
[`watchface-research/preview/`](watchface-research/preview/README.md). That
preview is not the package runtime and does not activate `.ttface` files.

The **Device** tab can select and read one of the nine built-in preview faces
on a TomTom connected directly over USB Ethernet. It communicates with the
small `tt-control` service on TCP port 18743. The service binds only to
`192.168.101.115`, accepts only `PING`, `STATUS`, and `SET_FACE <id>`, validates
the face ID, and atomically persists it to
`/mnt/sdcard/opentom/preview-gallery/current_face`. The running renderer polls
that file and applies the selection within about one second. The OpenTom
startup loop starts and supervises the service after configuring the USB
interface. This service provides no shell access; keep the USB network direct
and do not bridge or expose it to other networks.

Gallery activation, atomic package replacement/rollback, and installation of
arbitrary `.ttgallery` packages are not implemented; package export,
integrity inspection, and replacement planning remain host-side.

## Native Windows build (C++)

A native C++17/Win32 port of Face Studio lives in [`face-studio-native/`](face-studio-native/README.md). It reads and
writes the same `.ttface`, `.ttgallery` and `.ttproj` formats, applies the same validation, and ships face selection to
the TomTom over the fixed-command USB service (TCP 18743, `PING` / `STATUS` / `SET_FACE`). The Python Face Studio described
above stays in the repository as the behavioural reference until parity is verified; the Windows GitHub Actions workflow
builds the native `.exe` and publishes it as the `TomTomFaceStudio-win-x64` artifact.
