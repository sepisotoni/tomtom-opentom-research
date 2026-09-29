# info_anim - clock <-> info-panel animation

This C89 geometry/timing module is integrated into `../live_watchface.c` for
the Ubuntu, Roboto, Nunito, Numerals Duo, and generic preview faces. It
contains no Nano-X calls, drawing, heap allocation, or floating-point math.
The renderer supplies the geometry and fades the weather panel and dates.
Animation timing: 600 ms total (clock 0-500 ms, divider 100-600 ms, panel fade
200-600 ms, date out 0-280 ms / in 320-600 ms).

| File | Purpose |
|---|---|
| `info_anim.h/.c` | Geometry and animation state |
| `info_anim_lut.h` | Generated easing tables |
| `test_info_anim.c` | Host tests (379 checks) and `dump` mode |
| `preview_filmstrip.py` | Filmstrip using animation output and digit atlases |

## Build / test on the PC

From this directory:

```sh
gcc -std=c89 -pedantic -Wall -Wextra -Werror -O2 \
  info_anim.c test_info_anim.c -o /tmp/t_ia
/tmp/t_ia
python3 preview_filmstrip.py /tmp/t_ia /tmp/info-animation.png
```

The filmstrip includes an Ubuntu atlas row. It is a layout aid, not evidence
of Nano-X rendering or on-device performance.

## Integrated behavior

- `live_watchface.c` initializes one `InfoAnim` state; weather updates do not
  trigger animation while the interaction is being evaluated.
- The renderer uses `ia_layout_for()` and `ia_digit_rect()` for the transition
  from the normal clock to the weather-panel layout.
- The event timeout drops to 33 ms only while the 600 ms transition is active;
  the normal idle timeout remains one second.
- `SIGUSR2` toggles the info panel; screen taps cycle the configured face range
  and close an open panel. Face selection is also available through Face Studio
  and the current-face file.
- When conditions arrive from the GPS weather relay, the Ubuntu face shows
  the condition icon, temperature, precipitation/alert and required subdued
  Google attribution when the panel is opened. Attribution is wrapped within
  the info panel so it cannot intrude into the clock area.
- Weather state remains RAM-only; coordinates and forecasts are not written
  to persistent storage.

The device renderer build links both `live_watchface.c` and
`info_anim/info_anim.c`. The exact ARM GCC 3.3.4 and Nano-X build command is in
the parent `README.md`. CPU and battery cost during the brief animation
should be measured on the physical device.

## Animation module behavior

One master fixed-point progress value drives all geometry and opacity. A
reversal mid-animation continues from the current position without a jump.
The module handles short millisecond-counter wrap and backwards clock steps.
If `hour_only` changes at the minute boundary while an animation is active,
the selected layout changes on that frame.
