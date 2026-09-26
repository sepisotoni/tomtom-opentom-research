/*
 * OpenTom Watchface Suite for TomTom ONE (v6) / Samsung S3C2412
 * Target: Nano-X / Microwindows on 320x240 LCD Framebuffer
 * Language: Pure ANSI C (C89/C90 standard, ARM GCC 3.3.4 compatible)
 * Memory: < 100 KB RAM (zero dynamic mallocs; glyphs reside in ROM .rodata)
 * CPU: < 0.1% idle (dirty-region partial updates only; event-driven loop)
 *
 * Layouts:
 *  - HORIZONTAL (Side-by-Side: HH : MM) [Default]
 *  - STACKED (Top-and-Bottom: HH over MM)
 *
 * Sizing & On-The-Hour Scaling:
 *  - Regular Side-by-Side: 52px x 88px digits (Scale 2), perfectly proportioned.
 *  - On-The-Hour: Expands to GIANT HERO SIZE (78px x 132px, Scale 3), centered prominently!
 *
 * 5 Modular Faces (Pitch black background, vivid numerals, no enclosing box):
 *  0: Frost Outline   (Luminous vector-stroke cyan outline on pitch black - User favorite)
 *  1: Hydro Aqua Wave (Electric hydro cyan & marine blue with dynamic wave divider)
 *  2: Solid Lavender  (Radiant soft lavender bold numerals, Apple Watch ref left)
 *  3: Vivid Sunset    (Solar tangerine hours & electric rose minutes)
 *  4: Real Telemetry  (Live OS metrics fetched from /proc/uptime, loadavg, meminfo, Paris DST)
 *
 * Tap anywhere on 320x240 screen or send SIGUSR1 to cycle faces.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <unistd.h>

#include "nano-X.h"

#define SCREEN_W 320
#define SCREEN_H 240

#define GLYPH_W  26
#define GLYPH_H  44

#define SCALE_NORMAL 2
#define DIGIT_W      (GLYPH_W * SCALE_NORMAL) /* 52 px */
#define DIGIT_H      (GLYPH_H * SCALE_NORMAL) /* 88 px */
#define DIGIT_GAP    6                        /* 6 px gap */

#define SCALE_HERO   4
#define HERO_W       (GLYPH_W * SCALE_HERO)   /* 104 px */
#define HERO_H       (GLYPH_H * SCALE_HERO)   /* 176 px */
#define HERO_GAP     10                       /* 10 px gap */

#define LAYOUT_HORIZONTAL 0
#define LAYOUT_STACKED    1

#define FACE_FROST_OUTLINE  0
#define FACE_HYDRO_AQUA     1
#define FACE_SOLID_LAVENDER 2
#define FACE_VIVID_SUNSET   3
#define FACE_REAL_TELEMETRY 4
#define FACE_COUNT          5

/* Precomputed vector bitmasks for digits 0-9 (26x44 grid in 32-bit words) */
static const unsigned long digit_glyph_solid[10][44] = {
  /* Digit 0 */
  { 0x003ff00UL, 0x01fffe0UL, 0x07ffff8UL, 0x0fffffcUL, 
    0x0fffffcUL, 0x1fffffeUL, 0x1fffffeUL, 0x1ff87feUL, 
    0x3fe01ffUL, 0x3fc00ffUL, 0x3fc00ffUL, 0x3f8007fUL, 
    0x3f8007fUL, 0x3f8007fUL, 0x3f8007fUL, 0x3f8007fUL, 
    0x3f8007fUL, 0x3f8007fUL, 0x3f8007fUL, 0x3f8007fUL, 
    0x3f8007fUL, 0x3f8007fUL, 0x3f8007fUL, 0x3f8007fUL, 
    0x3f8007fUL, 0x3f8007fUL, 0x3f8007fUL, 0x3f8007fUL, 
    0x3f8007fUL, 0x3f8007fUL, 0x3f8007fUL, 0x3f8007fUL, 
    0x3f8007fUL, 0x3fc00ffUL, 0x3fc00ffUL, 0x3fe01ffUL, 
    0x1ff87feUL, 0x1fffffeUL, 0x1fffffeUL, 0x0fffffcUL, 
    0x0fffffcUL, 0x07ffff8UL, 0x01fffe0UL, 0x003ff00UL, },
  /* Digit 1 */
  { 0x000ffe0UL, 0x001ffe0UL, 0x007ffe0UL, 0x00fffe0UL, 
    0x01ffffeUL, 0x03ffffeUL, 0x03ffffeUL, 0x07ffffeUL, 
    0x0fffffeUL, 0x0fffffeUL, 0x0ffeffeUL, 0x0ffcffeUL, 
    0x0ffcffeUL, 0x0ff8ffeUL, 0x0ff8ffeUL, 0x01c0ffeUL, 
    0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 
    0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 
    0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 
    0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 
    0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 
    0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 
    0x0000ffeUL, 0x00007fcUL, 0x00007fcUL, 0x00001f0UL, },
  /* Digit 2 */
  { 0x003ff00UL, 0x01fffe0UL, 0x07ffff8UL, 0x0fffffcUL, 
    0x0fffffcUL, 0x1fffffeUL, 0x1fffffeUL, 0x1ff87feUL, 
    0x3fe01ffUL, 0x3fc00ffUL, 0x3fc00ffUL, 0x3f8007fUL, 
    0x000007fUL, 0x000007fUL, 0x00000ffUL, 0x00001ffUL, 
    0x00003feUL, 0x00007fcUL, 0x0000ff8UL, 0x0001ff0UL, 
    0x0003fe0UL, 0x0007fc0UL, 0x000ff80UL, 0x001ff00UL, 
    0x003fe00UL, 0x007fc00UL, 0x00ff800UL, 0x01ff000UL, 
    0x03fe000UL, 0x07fc000UL, 0x0ff8000UL, 0x1ff0000UL, 
    0x3fe0000UL, 0x7fc0000UL, 0xff80000UL, 0x1ff0000UL, 
    0x3fffffeUL, 0x3fffffeUL, 0x3fffffeUL, 0x3fffffeUL, 
    0x3fffffeUL, 0x3fffffeUL, 0x1fffffcUL, 0x0fffffcUL, },
  /* Digit 3 */
  { 0x003ff00UL, 0x01fffe0UL, 0x07ffff8UL, 0x0fffffcUL, 
    0x0fffffcUL, 0x1fffffeUL, 0x1fffffeUL, 0x1ff87feUL, 
    0x3fe01ffUL, 0x3fc00ffUL, 0x00000ffUL, 0x000007fUL, 
    0x000007fUL, 0x000007fUL, 0x00000ffUL, 0x00001ffUL, 
    0x00007feUL, 0x0001ffcUL, 0x000fff8UL, 0x001fffeUL, 
    0x001fffeUL, 0x000fff8UL, 0x0001ffcUL, 0x00007feUL, 
    0x00001ffUL, 0x00000ffUL, 0x000007fUL, 0x000007fUL, 
    0x000007fUL, 0x00000ffUL, 0x3fc00ffUL, 0x3fe01ffUL, 
    0x1ff87feUL, 0x1fffffeUL, 0x1fffffeUL, 0x0fffffcUL, 
    0x0fffffcUL, 0x07ffff8UL, 0x01fffe0UL, 0x003ff00UL, 
    0x0000000UL, 0x0000000UL, 0x0000000UL, 0x0000000UL, },
  /* Digit 4 */
  { 0x3f80ffeUL, 0x3f80ffeUL, 0x3f80ffeUL, 0x3f80ffeUL, 
    0x3f80ffeUL, 0x3f80ffeUL, 0x3f80ffeUL, 0x3f80ffeUL, 
    0x3f80ffeUL, 0x3f80ffeUL, 0x3f80ffeUL, 0x3f80ffeUL, 
    0x3f80ffeUL, 0x3f80ffeUL, 0x3f80ffeUL, 0x3f80ffeUL, 
    0x3f80ffeUL, 0x3f80ffeUL, 0x3f80ffeUL, 0x3f80ffeUL, 
    0x3f80ffeUL, 0x3f80ffeUL, 0x3ffffffUL, 0x3ffffffUL, 
    0x3ffffffUL, 0x3ffffffUL, 0x3ffffffUL, 0x3ffffffUL, 
    0x3ffffffUL, 0x3ffffffUL, 0x0000ffeUL, 0x0000ffeUL, 
    0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 
    0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 
    0x0000ffeUL, 0x00007fcUL, 0x00007fcUL, 0x00001f0UL, },
  /* Digit 5 */
  { 0x3fffffeUL, 0x3fffffeUL, 0x3fffffeUL, 0x3fffffeUL, 
    0x3fffffeUL, 0x3fffffeUL, 0x3f80000UL, 0x3f80000UL, 
    0x3f80000UL, 0x3f80000UL, 0x3f80000UL, 0x3f80000UL, 
    0x3f80000UL, 0x3f80000UL, 0x3f80000UL, 0x3f80000UL, 
    0x3f80000UL, 0x3f80000UL, 0x3ffff80UL, 0x3ffffc0UL, 
    0x3ffffe0UL, 0x3fffff0UL, 0x3f807f8UL, 0x00001fcUL, 
    0x00000feUL, 0x00000ffUL, 0x000007fUL, 0x000007fUL, 
    0x000007fUL, 0x000007fUL, 0x00000ffUL, 0x00000feUL, 
    0x3fc01fcUL, 0x3fe03f8UL, 0x1fffff0UL, 0x1ffffe0UL, 
    0x0ffffc0UL, 0x07fff80UL, 0x01ffe00UL, 0x007f800UL, 
    0x0000000UL, 0x0000000UL, 0x0000000UL, 0x0000000UL, },
  /* Digit 6 */
  { 0x003ff00UL, 0x01fffe0UL, 0x07ffff8UL, 0x0fffffcUL, 
    0x0fffffcUL, 0x1fffffeUL, 0x1fffffeUL, 0x1ff8000UL, 
    0x3fe0000UL, 0x3fc0000UL, 0x3f80000UL, 0x3f80000UL, 
    0x3f80000UL, 0x3f80000UL, 0x3f80000UL, 0x3f80000UL, 
    0x3f80000UL, 0x3f80000UL, 0x3f80000UL, 0x3f80000UL, 
    0x3f80000UL, 0x3fffffeUL, 0x3fffffeUL, 0x3fffffeUL, 
    0x3fe01ffUL, 0x3fc00ffUL, 0x3f8007fUL, 0x3f8007fUL, 
    0x3f8007fUL, 0x3f8007fUL, 0x3f8007fUL, 0x3f8007fUL, 
    0x3f8007fUL, 0x3fc00ffUL, 0x3fc00ffUL, 0x3fe01ffUL, 
    0x1ff87feUL, 0x1fffffeUL, 0x1fffffeUL, 0x0fffffcUL, 
    0x0fffffcUL, 0x07ffff8UL, 0x01fffe0UL, 0x003ff00UL, },
  /* Digit 7 */
  { 0x3fffffeUL, 0x3fffffeUL, 0x3fffffeUL, 0x3fffffeUL, 
    0x3fffffeUL, 0x3fffffeUL, 0x0000ffeUL, 0x0000ffeUL, 
    0x0001ffcUL, 0x0001ffcUL, 0x0003ff8UL, 0x0003ff8UL, 
    0x0007ff0UL, 0x0007ff0UL, 0x000ffe0UL, 0x000ffe0UL, 
    0x001ffc0UL, 0x001ffc0UL, 0x003ff80UL, 0x003ff80UL, 
    0x007ff00UL, 0x007ff00UL, 0x00ffe00UL, 0x00ffe00UL, 
    0x01ffc00UL, 0x01ffc00UL, 0x03ff800UL, 0x03ff800UL, 
    0x07ff000UL, 0x07ff000UL, 0x0ffe000UL, 0x0ffe000UL, 
    0x1ffc000UL, 0x1ffc000UL, 0x3ff8000UL, 0x3ff8000UL, 
    0x7ff0000UL, 0x7ff0000UL, 0xffe0000UL, 0xffe0000UL, 
    0x1ffc000UL, 0x0ffc000UL, 0x07fc000UL, 0x01f8000UL, },
  /* Digit 8 */
  { 0x003fe00UL, 0x01fff80UL, 0x07fffe0UL, 0x0ffffffUL, 
    0x0ffffffUL, 0x1ffffffUL, 0x1f801ffUL, 0x1f801ffUL, 
    0x3f000ffUL, 0x3f000ffUL, 0x3f000ffUL, 0x3f000ffUL, 
    0x3f000ffUL, 0x3f000ffUL, 0x1f801ffUL, 0x1f801ffUL, 
    0x1ffffffUL, 0x0ffffffUL, 0x07fffe0UL, 0x07ffff8UL, 
    0x0ffffffUL, 0x1ffffffUL, 0x1ffffffUL, 0x1f800ffUL, 
    0x3f0007fUL, 0x3f0007fUL, 0x3f0007fUL, 0x3f0007fUL, 
    0x3f0007fUL, 0x3f0007fUL, 0x3f0007fUL, 0x3f0007fUL, 
    0x3f0007fUL, 0x3f0007fUL, 0x3f0007fUL, 0x1f800ffUL, 
    0x1ffffffUL, 0x1ffffffUL, 0x0ffffffUL, 0x0ffffffUL, 
    0x07ffff8UL, 0x01fffe0UL, 0x007ff00UL, 0x0000000UL, },
  /* Digit 9 */
  { 0x003ff00UL, 0x01fffe0UL, 0x07ffff8UL, 0x0fffffcUL, 
    0x0fffffcUL, 0x1fffffeUL, 0x1fffffeUL, 0x1ff87feUL, 
    0x3fe01ffUL, 0x3fc00ffUL, 0x3f8007fUL, 0x3f8007fUL, 
    0x3f8007fUL, 0x3f8007fUL, 0x3f8007fUL, 0x3f8007fUL, 
    0x3f8007fUL, 0x3fc00ffUL, 0x3fe01ffUL, 0x1ff87feUL, 
    0x1fffffeUL, 0x1fffffeUL, 0x0fffffeUL, 0x07ffffeUL, 
    0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 
    0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 
    0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 0x0000ffeUL, 
    0x3f80ffeUL, 0x3fffffeUL, 0x3fffffeUL, 0x3fffffeUL, 
    0x1fffffeUL, 0x07ffffcUL, 0x01fffe0UL, 0x003ff00UL, },
};

static const unsigned long digit_glyph_outline[10][44] = {
  /* Digit 0 (Outline) */
  { 0x003ff00UL, 0x01fffe0UL, 0x07c0038UL, 0x0f8001cUL, 
    0x0f0000cUL, 0x1e00006UL, 0x1c07c06UL, 0x180fe06UL, 
    0x301fe03UL, 0x303ff03UL, 0x307ff03UL, 0x307ff03UL, 
    0x3000003UL, 0x3000003UL, 0x3000003UL, 0x3000003UL, 
    0x3000003UL, 0x3000003UL, 0x3000003UL, 0x3000003UL, 
    0x3000003UL, 0x3000003UL, 0x3000003UL, 0x3000003UL, 
    0x3000003UL, 0x3000003UL, 0x3000003UL, 0x3000003UL, 
    0x3000003UL, 0x3000003UL, 0x3000003UL, 0x3000003UL, 
    0x307ff03UL, 0x303ff03UL, 0x301fe03UL, 0x300fe03UL, 
    0x1807c06UL, 0x1c00006UL, 0x1e00006UL, 0x0f0000cUL, 
    0x0f8001cUL, 0x07c0038UL, 0x01fffe0UL, 0x003ff00UL, },
  /* Digit 1 (Outline) */
  { 0x000ffe0UL, 0x001ffe0UL, 0x0070020UL, 0x00e0020UL, 
    0x01c0022UL, 0x0380022UL, 0x0300022UL, 0x0600022UL, 
    0x0c00022UL, 0x0801022UL, 0x0803022UL, 0x0803022UL, 
    0x0807022UL, 0x0807022UL, 0x0807022UL, 0x01c0022UL, 
    0x0000022UL, 0x0000022UL, 0x0000022UL, 0x0000022UL, 
    0x0000022UL, 0x0000022UL, 0x0000022UL, 0x0000022UL, 
    0x0000022UL, 0x0000022UL, 0x0000022UL, 0x0000022UL, 
    0x0000022UL, 0x0000022UL, 0x0000022UL, 0x0000022UL, 
    0x0000022UL, 0x0000022UL, 0x0000022UL, 0x0000022UL, 
    0x0000022UL, 0x0000022UL, 0x0000022UL, 0x0000022UL, 
    0x0000022UL, 0x00007fcUL, 0x00007fcUL, 0x00001f0UL, },
  /* Digit 2 (Outline) */
  { 0x003ff00UL, 0x01fffe0UL, 0x07c0038UL, 0x0f8001cUL, 
    0x0f0000cUL, 0x1e00006UL, 0x1c07c06UL, 0x180fe06UL, 
    0x301fe03UL, 0x303ff03UL, 0x303ff03UL, 0x307ff03UL, 
    0x0000003UL, 0x0000003UL, 0x0000007UL, 0x000000fUL, 
    0x000001eUL, 0x000003cUL, 0x0000078UL, 0x00000f0UL, 
    0x00001e0UL, 0x00003c0UL, 0x0000780UL, 0x0000f00UL, 
    0x0001e00UL, 0x0003c00UL, 0x0007800UL, 0x000f000UL, 
    0x001e000UL, 0x003c000UL, 0x0078000UL, 0x00f0000UL, 
    0x01e0000UL, 0x03c0000UL, 0x0780000UL, 0x0f00000UL, 
    0x3e00002UL, 0x2000002UL, 0x2000002UL, 0x2000002UL, 
    0x2000002UL, 0x3fffffeUL, 0x1fffffcUL, 0x0fffffcUL, },
  /* Digit 3 (Outline) */
  { 0x003ff00UL, 0x01fffe0UL, 0x07c0038UL, 0x0f8001cUL, 
    0x0f0000cUL, 0x1e00006UL, 0x1c07c06UL, 0x180fe06UL, 
    0x301fe03UL, 0x303ff03UL, 0x0000003UL, 0x0000003UL, 
    0x0000003UL, 0x0000003UL, 0x0000007UL, 0x000000fUL, 
    0x000001eUL, 0x000003cUL, 0x000ff78UL, 0x00100f0UL, 
    0x00100f0UL, 0x000ff78UL, 0x000003cUL, 0x000001eUL, 
    0x000000fUL, 0x0000007UL, 0x0000003UL, 0x0000003UL, 
    0x0000003UL, 0x0000007UL, 0x303ff03UL, 0x301fe03UL, 
    0x180fe06UL, 0x1c07c06UL, 0x1e00006UL, 0x0f0000cUL, 
    0x0f8001cUL, 0x07c0038UL, 0x01fffe0UL, 0x003ff00UL, 
    0x0000000UL, 0x0000000UL, 0x0000000UL, 0x0000000UL, },
  /* Digit 4 (Outline) */
  { 0x3f80ffeUL, 0x2080002UL, 0x2080002UL, 0x2080002UL, 
    0x2080002UL, 0x2080002UL, 0x2080002UL, 0x2080002UL, 
    0x2080002UL, 0x2080002UL, 0x2080002UL, 0x2080002UL, 
    0x2080002UL, 0x2080002UL, 0x2080002UL, 0x2080002UL, 
    0x2080002UL, 0x2080002UL, 0x2080002UL, 0x2080002UL, 
    0x2080002UL, 0x2080002UL, 0x20ffffeUL, 0x2000002UL, 
    0x2000002UL, 0x2000002UL, 0x2000002UL, 0x2000002UL, 
    0x2000002UL, 0x3fffffeUL, 0x0000002UL, 0x0000002UL, 
    0x0000002UL, 0x0000002UL, 0x0000002UL, 0x0000002UL, 
    0x0000002UL, 0x0000002UL, 0x0000002UL, 0x0000002UL, 
    0x0000002UL, 0x00007fcUL, 0x00007fcUL, 0x00001f0UL, },
  /* Digit 5 (Outline) */
  { 0x3fffffeUL, 0x2000002UL, 0x2000002UL, 0x2000002UL, 
    0x2000002UL, 0x2000002UL, 0x2080000UL, 0x2080000UL, 
    0x2080000UL, 0x2080000UL, 0x2080000UL, 0x2080000UL, 
    0x2080000UL, 0x2080000UL, 0x2080000UL, 0x2080000UL, 
    0x2080000UL, 0x2080000UL, 0x20ffff8UL, 0x200000cUL, 
    0x2000006UL, 0x2000002UL, 0x20807f8UL, 0x00001fcUL, 
    0x00000feUL, 0x000007fUL, 0x000000fUL, 0x000000fUL, 
    0x000000fUL, 0x000000fUL, 0x000007fUL, 0x00000feUL, 
    0x3fc01fcUL, 0x3fe03f8UL, 0x1f00000UL, 0x1f00000UL, 
    0x0f00000UL, 0x0780000UL, 0x01fe000UL, 0x007f800UL, 
    0x0000000UL, 0x0000000UL, 0x0000000UL, 0x0000000UL, },
  /* Digit 6 (Outline) */
  { 0x003ff00UL, 0x01fffe0UL, 0x07c0038UL, 0x0f8001cUL, 
    0x0f0000cUL, 0x1e00006UL, 0x1c00006UL, 0x1800000UL, 
    0x3000000UL, 0x3000000UL, 0x3000000UL, 0x3000000UL, 
    0x3000000UL, 0x3000000UL, 0x3000000UL, 0x3000000UL, 
    0x3000000UL, 0x3000000UL, 0x3000000UL, 0x3000000UL, 
    0x3000000UL, 0x3ffffffUL, 0x3000003UL, 0x3000003UL, 
    0x301fe03UL, 0x303ff03UL, 0x307ff03UL, 0x307ff03UL, 
    0x3000003UL, 0x3000003UL, 0x3000003UL, 0x3000003UL, 
    0x3000003UL, 0x303ff03UL, 0x303ff03UL, 0x301fe03UL, 
    0x1807c06UL, 0x1c00006UL, 0x1e00006UL, 0x0f0000cUL, 
    0x0f8001cUL, 0x07c0038UL, 0x01fffe0UL, 0x003ff00UL, },
  /* Digit 7 (Outline) */
  { 0x3fffffeUL, 0x2000002UL, 0x2000002UL, 0x2000002UL, 
    0x2000002UL, 0x2000002UL, 0x0000002UL, 0x0000002UL, 
    0x0001004UL, 0x0001004UL, 0x0002008UL, 0x0002008UL, 
    0x0004010UL, 0x0004010UL, 0x0008020UL, 0x0008020UL, 
    0x0010040UL, 0x0010040UL, 0x0020080UL, 0x0020080UL, 
    0x0040100UL, 0x0040100UL, 0x0080200UL, 0x0080200UL, 
    0x0100400UL, 0x0100400UL, 0x0200800UL, 0x0200800UL, 
    0x0401000UL, 0x0401000UL, 0x0802000UL, 0x0802000UL, 
    0x1004000UL, 0x1004000UL, 0x2008000UL, 0x2008000UL, 
    0x4010000UL, 0x4010000UL, 0x8020000UL, 0x8020000UL, 
    0x1ffc000UL, 0x0ffc000UL, 0x07fc000UL, 0x01f8000UL, },
  /* Digit 8 (Outline) */
  { 0x003fe00UL, 0x01fff80UL, 0x07c03e0UL, 0x0f801ffUL, 
    0x0f000ffUL, 0x1e000ffUL, 0x1c000ffUL, 0x18001ffUL, 
    0x30000ffUL, 0x30000ffUL, 0x30000ffUL, 0x30000ffUL, 
    0x30000ffUL, 0x30000ffUL, 0x18001ffUL, 0x1c000ffUL, 
    0x1e000ffUL, 0x0f801ffUL, 0x07c03e0UL, 0x07ffff8UL, 
    0x0f801ffUL, 0x1e000ffUL, 0x1c000ffUL, 0x18000ffUL, 
    0x300007fUL, 0x300007fUL, 0x300007fUL, 0x300007fUL, 
    0x300007fUL, 0x300007fUL, 0x300007fUL, 0x300007fUL, 
    0x300007fUL, 0x300007fUL, 0x300007fUL, 0x18000ffUL, 
    0x1c000ffUL, 0x1e000ffUL, 0x0f801ffUL, 0x0f801ffUL, 
    0x07ffff8UL, 0x01fffe0UL, 0x007ff00UL, 0x0000000UL, },
  /* Digit 9 (Outline) */
  { 0x003ff00UL, 0x01fffe0UL, 0x07c0038UL, 0x0f8001cUL, 
    0x0f0000cUL, 0x1e00006UL, 0x1c07c06UL, 0x180fe06UL, 
    0x301fe03UL, 0x303ff03UL, 0x307ff03UL, 0x307ff03UL, 
    0x307ff03UL, 0x307ff03UL, 0x307ff03UL, 0x307ff03UL, 
    0x307ff03UL, 0x303ff03UL, 0x301fe03UL, 0x180fe06UL, 
    0x1c07c06UL, 0x1e00006UL, 0x0f00006UL, 0x0780006UL, 
    0x0000006UL, 0x0000006UL, 0x0000006UL, 0x0000006UL, 
    0x0000006UL, 0x0000006UL, 0x0000006UL, 0x0000006UL, 
    0x0000006UL, 0x0000006UL, 0x0000006UL, 0x0000006UL, 
    0x3f80006UL, 0x2000006UL, 0x2000006UL, 0x2000006UL, 
    0x1e00006UL, 0x07c003cUL, 0x01fffe0UL, 0x003ff00UL, },
};

/* Forward declaration of plug-in overlay callback */
typedef void (*OverlayDrawHook)(const struct tm *local, int full);

/* Modular WatchFace definition */
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

/* Configuration state */
static int cfg_layout = LAYOUT_HORIZONTAL; /* 0 = side-by-side HH:MM; 1 = stacked */
static int cfg_12hour = 1;                 /* 1 = 12-hour AM/PM; 0 = 24-hour military */
static int cfg_show_ampm = 1;              /* 1 = show AM/PM indicator; 0 = hide */

/* Nano-X State */
static GR_WINDOW_ID window;
static GR_GC_ID gc;
static GR_FONT_ID detail_font;
static volatile sig_atomic_t face_change_requested;
static int current_face_idx = FACE_FROST_OUTLINE;

static GR_SIZE screen_width = SCREEN_W;
static GR_SIZE screen_height = SCREEN_H;

/* Cached time state for partial redraws */
static int last_display_hour = -1;
static int last_minute = -1;
static int last_second = -1;
static int last_is_pm = -1;

/* Signal handler for face rotation */
static void
request_next_face(int signal_number)
{
	(void)signal_number;
	face_change_requested = 1;
}

/* Load optional configuration file from disk */
static void
load_configuration(void)
{
	FILE *fp;
	char line[128];

	fp = fopen("/mnt/sdcard/opentom/etc/watchface.cfg", "r");
	if (!fp)
		fp = fopen("etc/watchface.cfg", "r");
	if (!fp)
		fp = fopen("/etc/watchface.cfg", "r");

	if (!fp)
		return;

	while (fgets(line, sizeof(line), fp)) {
		char key[64], val[64];
		if (line[0] == '#' || line[0] == ';' || line[0] == '\n' || line[0] == '\r')
			continue;
		if (sscanf(line, "%63[^=]=%63s", key, val) == 2) {
			if (strcmp(key, "layout") == 0) {
				if (strcmp(val, "horizontal") == 0)
					cfg_layout = LAYOUT_HORIZONTAL;
				else if (strcmp(val, "stacked") == 0)
					cfg_layout = LAYOUT_STACKED;
			} else if (strcmp(key, "time_format") == 0) {
				if (strcmp(val, "12") == 0)
					cfg_12hour = 1;
				else if (strcmp(val, "24") == 0)
					cfg_12hour = 0;
			} else if (strcmp(key, "show_ampm") == 0) {
				cfg_show_ampm = atoi(val);
			} else if (strcmp(key, "default_face") == 0) {
				int f = atoi(val);
				if (f >= 0 && f < FACE_COUNT)
					current_face_idx = f;
			}
		}
	}
	fclose(fp);
}

/* Fast rasterizer: draws a digit using run-length spans with arbitrary scale factor */
static void
draw_digit_scaled(int x_origin, int y_origin, int digit, int outline, GR_COLOR color, int scale)
{
	int y, x;
	const unsigned long (*glyphs)[44] = outline ? digit_glyph_outline : digit_glyph_solid;

	if (digit < 0 || digit > 9 || scale <= 0)
		return;

	GrSetGCForeground(gc, color);

	for (y = 0; y < GLYPH_H; y++) {
		unsigned long row = glyphs[digit][y];
		if (!row)
			continue;

		x = 0;
		while (x < GLYPH_W) {
			int bit_idx = GLYPH_W - 1 - x;
			if ((row >> bit_idx) & 1UL) {
				int start_x = x;
				int span_len = 0;
				while (x < GLYPH_W && ((row >> (GLYPH_W - 1 - x)) & 1UL)) {
					span_len++;
					x++;
				}
				GrFillRect(window, gc,
					   x_origin + start_x * scale,
					   y_origin + y * scale,
					   span_len * scale,
					   scale);
			} else {
				x++;
			}
		}
	}
}

/* Draws centered single-line text using detail font */
static void
draw_centered_text(const char *text, GR_COORD baseline_y, GR_COLOR color)
{
	GR_SIZE width, height, base;

	GrSetGCForeground(gc, color);
	GrSetGCFont(gc, detail_font);
	GrGetGCTextSize(gc, (void *)text, -1, GR_TFBASELINE, &width, &height, &base);
	GrText(window, gc, (screen_width - width) / 2, baseline_y, (void *)text, -1, GR_TFBASELINE);
}

/* Renders AM/PM indicator pill at top right */
static void
draw_ampm_indicator(int is_pm, GR_COLOR color, int top_y)
{
	const char *str = is_pm ? "PM" : "AM";
	int x = screen_width - 34;

	GrSetGCForeground(gc, GR_RGB(0, 0, 0));
	GrFillRect(window, gc, x - 2, top_y - 12, 32, 16);

	GrSetGCForeground(gc, color);
	GrSetGCFont(gc, detail_font);
	GrText(window, gc, x, top_y, (void *)str, 2, GR_TFBASELINE);
}

/* Draws proportional glowing colon dots between hours and minutes */
static void
draw_colon_dots(int center_x, int base_y, GR_COLOR color)
{
	GrSetGCForeground(gc, color);
	/* Upper dot */
	GrFillRect(window, gc, center_x - 3, base_y + 24, 7, 7);
	/* Lower dot */
	GrFillRect(window, gc, center_x - 3, base_y + 56, 7, 7);
}

/* Renders the dynamic hydro wave line */
static void
draw_hydro_wave(int second, int full_redraw, int y_pos)
{
	int x;
	double phase = (double)(second % 60) * 0.18;

	if (full_redraw) {
		GrSetGCForeground(gc, GR_RGB(0, 0, 0));
		GrFillRect(window, gc, 36, y_pos - 6, 248, 12);
	}

	GrSetGCForeground(gc, GR_RGB(0, 245, 255));
	for (x = 40; x <= 280; x += 2) {
		int wy = y_pos + (int)(sin((double)x * 0.08 + phase) * 3.5);
		GrPoint(window, gc, x, wy);
		GrPoint(window, gc, x, wy + 1);
	}
}

/* Real Telemetry Page: Fetches genuine Linux metrics (Zero mock data) */
static void
draw_telemetry_overlay(const struct tm *local, int full_redraw)
{
	char time_str[48];
	char utc_str[32];
	char uptime_str[64];
	char load_str[64];
	char mem_str[64];
	time_t now_val;
	struct tm *utc_tm;
	FILE *fp;
	double uptime_sec = 0.0;
	double l1 = 0.0, l5 = 0.0, l15 = 0.0;
	long mem_total = 0, mem_free = 0;
	int is_pm = (local->tm_hour >= 12);
	int display_h = cfg_12hour ? (local->tm_hour % 12 ? local->tm_hour % 12 : 12) : local->tm_hour;

	if (full_redraw) {
		GrSetGCForeground(gc, GR_RGB(0, 0, 0));
		GrFillRect(window, gc, 0, 0, screen_width, screen_height);

		draw_centered_text("LIVE SYSTEM TELEMETRY", 24, GR_RGB(96, 165, 250));
		draw_centered_text("TOMTOM ONE v6  (S3C2412 ARM)", 44, GR_RGB(220, 228, 240));
	}

	/* Fetch real uptime from /proc/uptime */
	fp = fopen("/proc/uptime", "r");
	if (fp) {
		if (fscanf(fp, "%lf", &uptime_sec) != 1)
			uptime_sec = 0.0;
		fclose(fp);
	}
	{
		int days = (int)uptime_sec / 86400;
		int hrs = ((int)uptime_sec / 3600) % 24;
		int mins = ((int)uptime_sec / 60) % 60;
		int secs = (int)uptime_sec % 60;
		snprintf(uptime_str, sizeof(uptime_str), "UPTIME: %dd %02dh %02dm %02ds", days, hrs, mins, secs);
	}

	/* Fetch real load averages from /proc/loadavg */
	fp = fopen("/proc/loadavg", "r");
	if (fp) {
		if (fscanf(fp, "%lf %lf %lf", &l1, &l5, &l15) != 3) {
			l1 = l5 = l15 = 0.0;
		}
		fclose(fp);
	}
	snprintf(load_str, sizeof(load_str), "CPU LOAD: %.2f  %.2f  %.2f", l1, l5, l15);

	/* Fetch real memory info from /proc/meminfo */
	fp = fopen("/proc/meminfo", "r");
	if (fp) {
		char line[128];
		while (fgets(line, sizeof(line), fp)) {
			if (sscanf(line, "MemTotal: %ld kB", &mem_total) == 1) continue;
			if (sscanf(line, "MemFree: %ld kB", &mem_free) == 1) continue;
		}
		fclose(fp);
	}
	snprintf(mem_str, sizeof(mem_str), "RAM: %ld MB free / %ld MB total", mem_free / 1024, mem_total / 1024);

	/* Format Europe/Paris DST vs UTC */
	now_val = time(NULL);
	utc_tm = gmtime(&now_val);
	if (cfg_12hour) {
		snprintf(time_str, sizeof(time_str), "PARIS TIME: %02d:%02d:%02d %s (%Z)",
			display_h, local->tm_min, local->tm_sec, is_pm ? "PM" : "AM");
	} else {
		strftime(time_str, sizeof(time_str), "PARIS TIME: %H:%M:%S (%Z)", local);
	}

	if (utc_tm)
		strftime(utc_str, sizeof(utc_str), "UTC CLOCK:  %H:%M:%S", utc_tm);
	else
		snprintf(utc_str, sizeof(utc_str), "UTC CLOCK:  unavailable");

	/* Clear text area on black background */
	GrSetGCForeground(gc, GR_RGB(0, 0, 0));
	GrFillRect(window, gc, 20, 60, 280, 130);

	draw_centered_text(time_str, 78, GR_RGB(240, 246, 255));
	draw_centered_text(utc_str, 98, GR_RGB(160, 185, 215));
	draw_centered_text(uptime_str, 120, GR_RGB(110, 240, 180));
	draw_centered_text(load_str, 142, GR_RGB(255, 200, 90));
	draw_centered_text(mem_str, 164, GR_RGB(140, 215, 255));
	draw_centered_text("TAP SCREEN TO CYCLE WATCH FACES", 216, GR_RGB(110, 130, 160));
}

/* Master modular Watch Face array (Expandable table) */
static const WatchFace watch_faces[FACE_COUNT] = {
	/* Face 0: Frost Outline (User favorite #1) */
	{
		"Frost Outline",
		GR_RGB(0, 0, 0),         /* Pitch black night-friendly background */
		GR_RGB(130, 235, 255),   /* Luminous icy cyan outline */
		GR_RGB(130, 235, 255),   /* Matching minute outline */
		GR_RGB(100, 160, 200),   /* Subtle date stamp */
		1,                       /* Outline = TRUE */
		0,                       /* No wave */
		NULL                     /* Standard overlay */
	},
	/* Face 1: Hydro Aqua Wave (User favorite #2) */
	{
		"Hydro Aqua Wave",
		GR_RGB(0, 0, 0),         /* Pitch black background */
		GR_RGB(0, 245, 255),     /* Electric hydro cyan */
		GR_RGB(20, 180, 255),    /* Marine wave blue */
		GR_RGB(60, 200, 230),    /* Date stamp */
		0,                       /* Solid = TRUE */
		1,                       /* Has dynamic hydro wave */
		NULL
	},
	/* Face 2: Solid Lavender (Apple Watch Reference Left) */
	{
		"Solid Lavender",
		GR_RGB(0, 0, 0),         /* Pitch black background */
		GR_RGB(232, 218, 242),   /* Soft vivid lavender */
		GR_RGB(218, 196, 236),   /* Harmonious lilac */
		GR_RGB(160, 145, 185),   /* Date stamp */
		0,
		0,
		NULL
	},
	/* Face 3: Vivid Sunset */
	{
		"Vivid Sunset",
		GR_RGB(0, 0, 0),         /* Pitch black background */
		GR_RGB(255, 115, 65),    /* Solar tangerine */
		GR_RGB(255, 125, 215),   /* Electric rose magenta */
		GR_RGB(200, 110, 140),   /* Date stamp */
		0,
		0,
		NULL
	},
	/* Face 4: Real Telemetry (Zero mock data, pulls live OS data) */
	{
		"Real Telemetry",
		GR_RGB(0, 0, 0),         /* Pitch black background */
		GR_RGB(96, 165, 250),
		GR_RGB(147, 197, 253),
		GR_RGB(148, 163, 184),
		0,
		0,
		draw_telemetry_overlay   /* Calls real live telemetry renderer */
	}
};

/* Renders side-by-side horizontal layout with enlarged digits and giant on-the-hour size */
static void
render_horizontal_face(const struct tm *local, int full_redraw)
{
	const WatchFace *face = &watch_faces[current_face_idx];
	int raw_hour = local->tm_hour;
	int is_pm = (raw_hour >= 12);
	int display_hour;
	int min = local->tm_min;
	int sec = local->tm_sec;
	int is_on_the_hour = (min == 0);

	/* Coordinates for regular side-by-side (Scale 2, 52x88) */
	int base_y = 76;
	int h_pair_w = 2 * DIGIT_W + DIGIT_GAP; /* 110 px */
	int m_pair_w = 2 * DIGIT_W + DIGIT_GAP; /* 110 px */
	int colon_w = 24;
	int total_w = h_pair_w + colon_w + m_pair_w; /* 244 px */
	int h_x1 = (screen_width - total_w) / 2;     /* 38 px */
	int colon_cx = h_x1 + h_pair_w + (colon_w / 2); /* 160 px */
	int m_x1 = colon_cx + (colon_w / 2);         /* 172 px */

	/* Coordinates for EXPANDED ON-THE-HOUR HERO SIZE (Scale 4, 104x176) */
	int hero_pair_w = 2 * HERO_W + HERO_GAP;    /* 218 px */
	int hero_x1 = (screen_width - hero_pair_w) / 2; /* 51 px centered! */
	int hero_y = 38;                            /* Centered vertically on 240px */

	if (cfg_12hour) {
		display_hour = raw_hour % 12;
		if (display_hour == 0)
			display_hour = 12;
	} else {
		display_hour = raw_hour;
	}

	if (full_redraw || (is_on_the_hour && last_minute != 0) || (!is_on_the_hour && last_minute == 0)) {
		char date_str[32];

		/* Clean screen with pitch black */
		GrSetGCForeground(gc, face->bg_color);
		GrFillRect(window, gc, 0, 0, screen_width, screen_height);

		/* Top subtle date stamp centered at y = 28 */
		strftime(date_str, sizeof(date_str), "%a  %d %b", local);
		draw_centered_text(date_str, 28, face->date_color);

		/* AM/PM indicator badge (if enabled) */
		if (cfg_12hour && cfg_show_ampm)
			draw_ampm_indicator(is_pm, face->date_color, 28);

		if (is_on_the_hour) {
			/* EXPANDED ON-THE-HOUR: Giant 78x132 hero digits, centered prominently! */
			draw_digit_scaled(hero_x1, hero_y, display_hour / 10, face->is_outline, face->hour_color, SCALE_HERO);
			draw_digit_scaled(hero_x1 + HERO_W + HERO_GAP, hero_y, display_hour % 10, face->is_outline, face->hour_color, SCALE_HERO);
		} else {
			/* REGULAR MINUTES: Hours, glowing colon dots, and Minutes */
			draw_digit_scaled(h_x1, base_y, display_hour / 10, face->is_outline, face->hour_color, SCALE_NORMAL);
			draw_digit_scaled(h_x1 + DIGIT_W + DIGIT_GAP, base_y, display_hour % 10, face->is_outline, face->hour_color, SCALE_NORMAL);
			draw_colon_dots(colon_cx, base_y, face->hour_color);
			draw_digit_scaled(m_x1, base_y, min / 10, face->is_outline, face->min_color, SCALE_NORMAL);
			draw_digit_scaled(m_x1 + DIGIT_W + DIGIT_GAP, base_y, min % 10, face->is_outline, face->min_color, SCALE_NORMAL);
		}
		last_display_hour = display_hour;
		last_minute = min;

		/* Dynamic hydro wave accent underneath */
		if (face->has_hydro_wave)
			draw_hydro_wave(sec, 1, is_on_the_hour ? 202 : 182);

		last_second = sec;
		last_is_pm = is_pm;
	} else {
		/* Partial dirty redraw */
		if (!is_on_the_hour) {
			if (display_hour != last_display_hour) {
				GrSetGCForeground(gc, face->bg_color);
				GrFillRect(window, gc, h_x1, base_y, h_pair_w, DIGIT_H);
				draw_digit_scaled(h_x1, base_y, display_hour / 10, face->is_outline, face->hour_color, SCALE_NORMAL);
				draw_digit_scaled(h_x1 + DIGIT_W + DIGIT_GAP, base_y, display_hour % 10, face->is_outline, face->hour_color, SCALE_NORMAL);
				last_display_hour = display_hour;
			}

			if (min != last_minute) {
				GrSetGCForeground(gc, face->bg_color);
				GrFillRect(window, gc, m_x1, base_y, m_pair_w, DIGIT_H);
				draw_digit_scaled(m_x1, base_y, min / 10, face->is_outline, face->min_color, SCALE_NORMAL);
				draw_digit_scaled(m_x1 + DIGIT_W + DIGIT_GAP, base_y, min % 10, face->is_outline, face->min_color, SCALE_NORMAL);
				last_minute = min;
			}
		} else {
			if (display_hour != last_display_hour) {
				GrSetGCForeground(gc, face->bg_color);
				GrFillRect(window, gc, hero_x1, hero_y, hero_pair_w, HERO_H);
				draw_digit_scaled(hero_x1, hero_y, display_hour / 10, face->is_outline, face->hour_color, SCALE_HERO);
				draw_digit_scaled(hero_x1 + HERO_W + HERO_GAP, hero_y, display_hour % 10, face->is_outline, face->hour_color, SCALE_HERO);
				last_display_hour = display_hour;
			}
		}

		if (cfg_12hour && cfg_show_ampm && (is_pm != last_is_pm)) {
			draw_ampm_indicator(is_pm, face->date_color, 28);
			last_is_pm = is_pm;
		}

		if (face->has_hydro_wave)
			draw_hydro_wave(sec, 0, is_on_the_hour ? 202 : 182);

		last_second = sec;
	}
}

/* Renders stacked layout */
static void
render_stacked_face(const struct tm *local, int full_redraw)
{
	const WatchFace *face = &watch_faces[current_face_idx];
	int raw_hour = local->tm_hour;
	int is_pm = (raw_hour >= 12);
	int display_hour;
	int min = local->tm_min;
	int sec = local->tm_sec;
	int is_on_the_hour = (min == 0);

	int pair_w = 2 * DIGIT_W + 10;
	int x1 = (screen_width - pair_w) / 2;
	int hour_y = is_on_the_hour ? 56 : 18;
	int min_y = hour_y + DIGIT_H + 12;

	/* On the hour: expanded hero size */
	int hero_pair_w = 2 * HERO_W + HERO_GAP;
	int hero_x1 = (screen_width - hero_pair_w) / 2;
	int hero_y = 38;

	if (cfg_12hour) {
		display_hour = raw_hour % 12;
		if (display_hour == 0)
			display_hour = 12;
	} else {
		display_hour = raw_hour;
	}

	if (full_redraw || (is_on_the_hour && last_minute != 0) || (!is_on_the_hour && last_minute == 0)) {
		char date_str[32];

		GrSetGCForeground(gc, face->bg_color);
		GrFillRect(window, gc, 0, 0, screen_width, screen_height);

		strftime(date_str, sizeof(date_str), "%a  %d %b", local);
		draw_centered_text(date_str, 20, face->date_color);

		if (cfg_12hour && cfg_show_ampm)
			draw_ampm_indicator(is_pm, face->date_color, 20);

		if (is_on_the_hour) {
			draw_digit_scaled(hero_x1, hero_y, display_hour / 10, face->is_outline, face->hour_color, SCALE_HERO);
			draw_digit_scaled(hero_x1 + HERO_W + HERO_GAP, hero_y, display_hour % 10, face->is_outline, face->hour_color, SCALE_HERO);
		} else {
			draw_digit_scaled(x1, hour_y, display_hour / 10, face->is_outline, face->hour_color, SCALE_NORMAL);
			draw_digit_scaled(x1 + DIGIT_W + 10, hour_y, display_hour % 10, face->is_outline, face->hour_color, SCALE_NORMAL);
			draw_digit_scaled(x1, min_y, min / 10, face->is_outline, face->min_color, SCALE_NORMAL);
			draw_digit_scaled(x1 + DIGIT_W + 10, min_y, min % 10, face->is_outline, face->min_color, SCALE_NORMAL);
			if (face->has_hydro_wave)
				draw_hydro_wave(sec, 1, 112);
		}
		last_display_hour = display_hour;
		last_minute = min;

		last_second = sec;
		last_is_pm = is_pm;
	} else {
		if (is_on_the_hour) {
			if (display_hour != last_display_hour) {
				GrSetGCForeground(gc, face->bg_color);
				GrFillRect(window, gc, hero_x1, hero_y, hero_pair_w, HERO_H);
				draw_digit_scaled(hero_x1, hero_y, display_hour / 10, face->is_outline, face->hour_color, SCALE_HERO);
				draw_digit_scaled(hero_x1 + HERO_W + HERO_GAP, hero_y, display_hour % 10, face->is_outline, face->hour_color, SCALE_HERO);
				last_display_hour = display_hour;
			}
		} else {
			if (display_hour != last_display_hour) {
				GrSetGCForeground(gc, face->bg_color);
				GrFillRect(window, gc, x1, hour_y, pair_w, DIGIT_H);
				draw_digit_scaled(x1, hour_y, display_hour / 10, face->is_outline, face->hour_color, SCALE_NORMAL);
				draw_digit_scaled(x1 + DIGIT_W + 10, hour_y, display_hour % 10, face->is_outline, face->hour_color, SCALE_NORMAL);
				last_display_hour = display_hour;
			}

			if (min != last_minute) {
				GrSetGCForeground(gc, face->bg_color);
				GrFillRect(window, gc, x1, min_y, pair_w, DIGIT_H);
				draw_digit_scaled(x1, min_y, min / 10, face->is_outline, face->min_color, SCALE_NORMAL);
				draw_digit_scaled(x1 + DIGIT_W + 10, min_y, min % 10, face->is_outline, face->min_color, SCALE_NORMAL);
				last_minute = min;
			}

			if (face->has_hydro_wave)
				draw_hydro_wave(sec, 0, 112);
		}

		if (cfg_12hour && cfg_show_ampm && (is_pm != last_is_pm)) {
			draw_ampm_indicator(is_pm, face->date_color, 20);
			last_is_pm = is_pm;
		}

		last_second = sec;
	}
}

/* Master face render router */
static void
render_face(const struct tm *local, int full_redraw)
{
	const WatchFace *face = &watch_faces[current_face_idx];

	if (face->custom_overlay != NULL) {
		face->custom_overlay(local, full_redraw);
		last_display_hour = local->tm_hour;
		last_minute = local->tm_min;
		last_second = local->tm_sec;
		last_is_pm = (local->tm_hour >= 12);
		return;
	}

	if (cfg_layout == LAYOUT_HORIZONTAL) {
		render_horizontal_face(local, full_redraw);
	} else {
		render_stacked_face(local, full_redraw);
	}
}

int
main(int argc, char *argv[])
{
	GR_EVENT event;
	int i;

	/* Load default file configuration */
	load_configuration();

	/* Parse optional command line flags */
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-12") == 0) {
			cfg_12hour = 1;
		} else if (strcmp(argv[i], "-24") == 0) {
			cfg_12hour = 0;
		} else if (strcmp(argv[i], "-horizontal") == 0) {
			cfg_layout = LAYOUT_HORIZONTAL;
		} else if (strcmp(argv[i], "-stacked") == 0) {
			cfg_layout = LAYOUT_STACKED;
		} else if (strcmp(argv[i], "-ampm") == 0) {
			cfg_show_ampm = 1;
		} else if (strcmp(argv[i], "-no-ampm") == 0) {
			cfg_show_ampm = 0;
		}
	}

	if (GrOpen() < 0) {
		fprintf(stderr, "Cannot connect to Nano-X graphics server\n");
		return 1;
	}

	signal(SIGUSR1, request_next_face);

	window = GrNewWindowEx(GR_WM_PROPS_NODECORATE |
			       GR_WM_PROPS_NOAUTOMOVE |
			       GR_WM_PROPS_NOAUTORESIZE |
			       GR_WM_PROPS_NORESIZE,
			       "TomTom Watchfaces",
			       GR_ROOT_WINDOW_ID,
			       0, 0, SCREEN_W, SCREEN_H, GR_RGB(0, 0, 0));

	gc = GrNewGC();
	detail_font = GrCreateFontEx(GR_FONT_SYSTEM_FIXED, 12, 0, NULL);

	if (window == 0 || gc == 0 || detail_font == 0) {
		fprintf(stderr, "Error creating Nano-X graphics resources\n");
		GrClose();
		return 1;
	}

	{
		GR_WINDOW_INFO win_info;
		GrGetWindowInfo(window, &win_info);
		if (win_info.width > 0 && win_info.height > 0) {
			screen_width = win_info.width;
			screen_height = win_info.height;
		}
	}

	GrSelectEvents(window, GR_EVENT_MASK_EXPOSURE |
			       GR_EVENT_MASK_BUTTON_DOWN |
			       GR_EVENT_MASK_CLOSE_REQ);
	GrMapWindow(window);

	/* Main event-driven loop: 1 Hz timeout for clock ticking */
	for (;;) {
		time_t now_time;
		struct tm *local_tm;
		int force_full = 0;

		GrGetNextEventTimeout(&event, 1000L);

		if (event.type == GR_EVENT_TYPE_CLOSE_REQ) {
			GrClose();
			return 0;
		}

		/* Tap anywhere on touchscreen cycles watch face */
		if (event.type == GR_EVENT_TYPE_BUTTON_DOWN) {
			current_face_idx = (current_face_idx + 1) % FACE_COUNT;
			force_full = 1;
		}

		if (event.type == GR_EVENT_TYPE_EXPOSURE) {
			GR_WINDOW_INFO info;
			GrGetWindowInfo(window, &info);
			if (info.width > 0 && info.height > 0) {
				screen_width = info.width;
				screen_height = info.height;
			}
			force_full = 1;
		}

		if (face_change_requested) {
			face_change_requested = 0;
			current_face_idx = (current_face_idx + 1) % FACE_COUNT;
			force_full = 1;
		}

		if (event.type == GR_EVENT_TYPE_EXPOSURE && !force_full)
			continue;

		now_time = time(NULL);
		local_tm = localtime(&now_time);
		if (!local_tm)
			continue;

		if (last_second < 0 || force_full) {
			render_face(local_tm, 1);
		} else if (local_tm->tm_sec != last_second ||
			   local_tm->tm_min != last_minute ||
			   local_tm->tm_hour != (cfg_12hour ? (last_display_hour % 12) : last_display_hour)) {
			render_face(local_tm, 0);
		}
	}
}
