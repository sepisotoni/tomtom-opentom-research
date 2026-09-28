# info_anim - clock <-> info-panel animation

Separate module; `live_watchface.c` is untouched. Pure geometry + timing:
no Nano-X, no drawing, no malloc, no floats, C89. It reproduces the timings
of the React prototype (600 ms total: clock 0-500 ms, divider 100-600 ms,
panel fade 200-600 ms, date out 0-280 ms / in 320-600 ms).

| File | Purpose |
|---|---|
| `info_anim.h/.c` | the module |
| `info_anim_lut.h` | generated easing tables (`gen_ease_lut.py`) |
| `test_info_anim.c` | host tests (379 checks) + `dump` mode |
| `preview_filmstrip.py` | renders the module's real output with the digit atlas |

## Build / test on the PC

    gcc -std=c89 -pedantic -Wall -Wextra -Werror -O2 info_anim.c test_info_anim.c -o /tmp/t_ia && /tmp/t_ia
    python3 preview_filmstrip.py /tmp/t_ia filmstrip.png

## Integration sketch (in live_watchface.c)

1. `static InfoAnim anim;` -> `ia_init(&anim);` once. Add `info_anim.c` to the build.
2. Millisecond clock (wraps harmlessly, the module works modulo 2^32):

        static unsigned long now_ms(void)
        {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            return (unsigned long)tv.tv_sec * 1000UL + (unsigned long)(tv.tv_usec / 1000);
        }

3. Every loop: `t = now_ms(); ia_update(&anim, weather_display.available && weather_display.noteworthy, t); ia_tick(&anim, t);`
4. Wait time: `ia_active(&anim, t) ? 33 : FRAME_TIMEOUT_MS` for `GrGetNextEventTimeout`.
   While active, force `redraw_base = 1`. When idle the 1000 ms wake-up is unchanged.
5. `BUTTON_DOWN` -> `ia_tap(&anim, t)` (position is ignored; see below).
6. In `draw_frame`: `p = ia_progress(&anim, t)`; if `p > 0` take digit rects from
   `ia_digit_rect(ia_layout_for(kind, config_stacked, hour_only), i, p, &r)` instead of the fixed
   tables; divider height = `240 * ia_divider(p) / IA_ONE`; fade the weather content with
   `ia_panel_alpha(p)`; draw the date at `base_x + dx` with `ia_date()` (draw an instance only when
   its alpha > 0) and fade text with `ia_blend565(background, colour, alpha)`.
7. `kind`: `IA_KIND_NUMERALS` for Numerals Duo, `IA_KIND_FONT` for Roboto/Ubuntu/Nunito,
   else `IA_KIND_GENERIC`. `hour_only = local->tm_min == 0`.

## Behaviour notes

* One master progress value drives everything, so reversing mid-animation runs backwards
  from the current point: no restart, no jump.
* Auto mode has 30 s hysteresis (`IA_DWELL_MS`) so a flapping weather flag can't make the panel
  flap. A tap overrides instantly and lasts until the noteworthy flag next changes.
* A clock that jumps backwards (GPS time sync) is treated as "just started"; the animation stays
  in range.
* Layout tables copy the numbers in `draw_frame`; each row cites its source. Re-sync if those change.
* If `hour_only` flips (59 -> 00) *during* an animation, the layout switches at that frame (a snap).

## Not verified

Not compiled with the ARM GCC 3.3.4 toolchain and not run on a TomTom. Host checks: strict
`-std=c89 -pedantic`, ASan/UBSan, and a simulated 32-bit counter wrap (a real 32-bit build was not
possible on the authoring machine). Per-frame cost of a full `draw_frame` on the 266 MHz CPU is unmeasured.
