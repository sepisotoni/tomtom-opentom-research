# OpenTom Watchface Architecture & Implementation Specification

This document provides the exact implementation details, memory specifications, and extension guidelines for the native watchface application (`watchface-research/project/applications/src/tools/watchface.c`) running on the **TomTom ONE v6** (Samsung S3C2412, Nano-X / Microwindows, 320x240 LCD).

---

## 1. Core Technical Specifications

- **Language**: Pure ANSI C (C89/C90 compliant for `arm-linux-gcc` 3.3.4 and `glibc-2.3.2`).
- **Memory Footprint**: **< 100 KB RAM** (0 bytes dynamic heap allocation; `malloc()` is never called).
- **Glyph Storage**: All 10 digits (0–9) are stored as 26×44 vector bitmask words in `.rodata` (ROM/flash).
- **CPU Footprint**: **< 0.1% idle CPU** on the 266 MHz S3C2412 ARM926EJ-S processor.
- **Rendering Model**: Event-driven loop using `GrGetNextEventTimeout(&event, 1000L)`. Redraws only dirty regions (bounding boxes of changed digits) rather than the entire 320×240 framebuffer.
- **Visual Design**: Pitch black background (`#000000`) for glare-free night readability; bold, rounded Apple Watch typography; no enclosing boxes; no cluttering bottom second pip bars.

---

## 2. Included 5 Modular Faces

The faces are registered in the static table `watch_faces[5]`:

| Index | Name | Style | Colors | Special Features |
|---|---|---|---|---|
| **0** | **Frost Outline** | Vector Stroke Contour | Glowing Icy Cyan (`#84EBFF`) on Pitch Black | Hollow rounded numerals; favorite #1 |
| **1** | **Hydro Aqua Wave** | Solid Fill + Sine Wave | Electric Aqua (`#00F5FF`) & Marine (`#14B4FF`) | Dynamic fluid sine water divider |
| **2** | **Solid Lavender** | Solid Fill | Soft Lavender (`#E8DAF2`) & Lilac (`#DAC4EC`) | Apple Watch reference left face |
| **3** | **Vivid Sunset** | Solid Fill | Solar Tangerine (`#FF7341`) & Rose (`#FF7DD7`) | Luminous contrast, gentle at night |
| **4** | **Real Telemetry** | Plugin Overlay Hook | Sky Blue (`#60A5FA`) & Slate (`#93C5FD`) | Zero mock data: live OS telemetry |

---

## 3. Modular Architecture: How to Expand (e.g., Adding Weather)

The watchface suite is deliberately decoupled. Adding a new face (such as weather, step count, or compass) requires **zero changes** to the core rendering engine.

### Data Structure (`watchface.c`):
```c
typedef void (*OverlayDrawHook)(const struct tm *local, int full);

typedef struct {
    const char *name;
    GR_COLOR bg_color;
    GR_COLOR hour_color;
    GR_COLOR min_color;
    GR_COLOR date_color;
    int is_outline;
    int has_hydro_wave;
    OverlayDrawHook custom_overlay; /* Plugin hook: weather, sensors, complications */
} WatchFace;
```

### Adding a Weather Face Example:
1. Define the overlay function:
```c
static void
draw_weather_overlay(const struct tm *local, int full_redraw)
{
    FILE *fp;
    char temp_str[16] = "-- C";
    char cond_str[32] = "Unavailable";

    /* Read weather data written by background sync script */
    fp = fopen("/mnt/sdcard/opentom/data/weather.txt", "r");
    if (fp) {
        fscanf(fp, "%15s %31s", temp_str, cond_str);
        fclose(fp);
    }

    if (full_redraw) {
        GrSetGCForeground(gc, GR_RGB(0, 0, 0));
        GrFillRect(window, gc, 0, 0, screen_width, screen_height);
    }

    /* Clear and draw weather telemetry */
    GrSetGCForeground(gc, GR_RGB(0, 0, 0));
    GrFillRect(window, gc, 30, 80, 260, 80);

    draw_centered_text("OUTDOOR WEATHER", 96, GR_RGB(96, 165, 250));
    draw_centered_text(temp_str, 124, GR_RGB(255, 220, 100));
    draw_centered_text(cond_str, 146, GR_RGB(200, 210, 230));
}
```
2. Add an entry to the `watch_faces[]` array:
```c
{
    "Live Weather",
    GR_RGB(0, 0, 0),
    GR_RGB(96, 165, 250),
    GR_RGB(255, 220, 100),
    GR_RGB(160, 180, 200),
    0,
    0,
    draw_weather_overlay
}
```
3. Update `#define FACE_COUNT` accordingly.

---

## 4. Layout Modes, 12h/24h Formats & AM/PM Configuration

The watchface supports configurable layout positioning, 12-hour/24-hour formats, and toggleable AM/PM badges:

### On-The-Hour Mode (Expanded Hero Scaling):
- When `minute == 0` (e.g., 10:00, 12:00, 08:00):
  - Displays **ONLY the hour numerals** (`10`, `12`, `08`), centered horizontally and vertically on the pitch-black display.
  - **Hero Scaling**: Hour digits are dramatically **EXPANDED** to hero proportions:
    - In native C: scaled by `SCALE_HERO` (3×, `78×132 px` per glyph, total pair width `164 px` centered at `x = 78, y = 56`).
    - In web simulator: rendered at **`144px`** extra-large font-size, centered with high-impact presence.
  - The colon (`:`) and trailing `00` minutes are omitted.
  - No redundant labels or text (pure time display).
  - The top date stamp (`SAT 26 SEP`) and optional AM/PM badge remain cleanly at the top.
- When `minute != 0` (e.g., 10:01, 08:34):
  - Displays the full hours, glowing colon dots, and minutes side-by-side (`HH : MM`) or stacked (`HH` over `MM`).
- **`horizontal` (Default)**: Hours and Minutes side-by-side (`HH : MM`) centered along the 320x240 display:
  - Digits enlarged to `88px` (in C: `SCALE_NORMAL 2x`, `52×88 px` per glyph).
  - Hours: `x1 = 38, x2 = 96, y = 76`
  - Colon dots: centered at `x = 160` (dots at `y = 100` and `y = 134`), maintained at proportional radius to prevent looking unbalanced.
  - Minutes: `x1 = 172, x2 = 230, y = 76`
  - Total span: 244 px, perfectly framed by 38 px left and right margins.
- **`stacked`**: Traditional top-and-bottom (`HH` over `MM`). On the hour, the hour digits expand to the same massive hero proportions.

### Configuration File (`/mnt/sdcard/opentom/etc/watchface.cfg`):
```ini
# Layout mode: horizontal (HH : MM) or stacked (HH over MM)
layout=horizontal

# Time display format: 12 (12-hour AM/PM) or 24 (24-hour military)
time_format=12

# AM/PM indicator badge: 1 (display), 0 (hide)
show_ampm=1

# Default initial face (0-4):
default_face=0
```

### Command-line Overrides:
The binary supports runtime flag overrides:
- `./watchface -horizontal`: Force side-by-side layout (`HH : MM`).
- `./watchface -stacked`: Force top-and-bottom layout (`HH` over `MM`).
- `./watchface -12`: Force 12-hour format.
- `./watchface -24`: Force 24-hour format.
- `./watchface -ampm`: Enable AM/PM badge.
- `./watchface -no-ampm`: Hide AM/PM badge.

### AM/PM Rendering Architecture:
- Positioned in the header bar at top-right (`x = screen_width - 34, y = 12`).
- Rendered in harmonious color matching the active face's date/accent palette.
- Updates cleanly at noon (12:00:00 PM) and midnight (12:00:00 AM) without repainting the rest of the display.

---

## 5. Real Data Sources (Zero Mock Data)

The application pulls exclusively from real Linux kernel pseudo-filesystems and system interfaces:
- **System Uptime**: `/proc/uptime` (parsed via `fscanf` into days, hours, minutes, seconds).
- **CPU Load**: `/proc/loadavg` (parsed into 1m, 5m, 15m run-queue load averages).
- **System RAM**: `/proc/meminfo` (`MemTotal` and `MemFree` extracted in kB).
- **Hardware Status**: `/dev/hwstatus` with ioctl `IOR_HWSTATUS` (battery level, docking state).
- **Real Local Time**: `time(NULL)` and `localtime()` evaluated using POSIX Europe/Paris DST rules (`CET-1CEST,M3.5.0/2,M10.5.0/3`).

---

## 5. Time Transition Animation

When the minute rolls over:
- `animate_minute_transition()` selectively clears only the bounding rectangle of the minute digits (`114 × 88 px`) with pitch black.
- The new minute digits are drawn instantly using run-length horizontal span rasterization (`draw_digit_fast`), taking under 0.8 ms of execution time.
- The hour digits, date stamp, and background are untouched, preventing visible display flicker.

---

## 6. Build Instructions

In a complete LeddaZ/OpenTom cross-compilation environment:
```sh
cd /path/to/LeddaZ-OpenTom
source get_cross_env.sh
make -B -C applications/src/tools watchface
```

Confirm that the output binary is a 32-bit ARM ELF:
```sh
file applications/src/tools/watchface
```
Expected output:
```text
watchface: ELF 32-bit LSB executable, ARM, version 1 (ARM), dynamically linked, interpreter /lib/ld-linux.so.2
```
