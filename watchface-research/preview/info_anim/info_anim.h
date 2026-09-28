/*
 * info_anim.h - clock <-> info-panel animation for the TomTom watchface.
 *
 * Pure geometry/timing.  No Nano-X, no drawing, no malloc, no floats, C89.
 * The caller asks "where is everything at this moment?" and draws it with
 * its own routines (draw_digit() already scales a digit to any rectangle).
 *
 * One master progress value runs 0..IA_ONE (0 = normal clock, IA_ONE = info
 * shown).  Every element derives its own position/opacity from it using its
 * own start time, duration and easing.  Because everything hangs off ONE
 * value, reversing mid-animation just runs it backwards from where it is:
 * no restart, no jump.
 */
#ifndef INFO_ANIM_H
#define INFO_ANIM_H

#define IA_ONE          1024   /* fixed-point 1.0 */
#define IA_TOTAL_MS     600    /* full open or close, ms */
#define IA_DWELL_MS     30000  /* auto-mode: min time between auto changes */
#define IA_MAX_DIGITS   4

/* Which face family the digits belong to (selects the layout tables). */
#define IA_KIND_GENERIC  0     /* outline/solid/aqua/lavender/sunset faces */
#define IA_KIND_NUMERALS 1     /* Numerals Duo (always stacked) */
#define IA_KIND_FONT     2     /* Roboto / Ubuntu / Nunito faces */

typedef struct {
    short x, y, w, h;
} IaRect;

typedef struct {
    int n;                          /* 4 = HH MM, 2 = HH only */
    IaRect from[IA_MAX_DIGITS];     /* progress 0    (normal clock) */
    IaRect to[IA_MAX_DIGITS];       /* progress IA_ONE (info shown) */
} IaLayout;

typedef struct {
    int p0;                 /* master progress at last retarget */
    int dir;                /* +1 opening, -1 closing, 0 settled */
    unsigned long t0;       /* ms stamp of last retarget */
    int open;               /* current target: 1 = info shown */
    /* auto/manual policy */
    int prev_noteworthy;
    int manual;             /* a tap overrides auto until data changes */
    int manual_open;
    int have_changed;
    unsigned long last_change;
} InfoAnim;

void ia_init(InfoAnim *a);

/* Time in ms is supplied by the caller (see README for gettimeofday). */
void ia_request(InfoAnim *a, int open, unsigned long now_ms);
void ia_tick(InfoAnim *a, unsigned long now_ms);    /* call once per loop */
int  ia_progress(const InfoAnim *a, unsigned long now_ms);
int  ia_active(const InfoAnim *a, unsigned long now_ms);

/* Policy.  ia_update() every loop with the current "noteworthy" flag;
 * ia_tap() on any touch (position is ignored - the screen is cracked). */
void ia_update(InfoAnim *a, int noteworthy, unsigned long now_ms);
void ia_tap(InfoAnim *a, unsigned long now_ms);

/* Layout selection.  stacked only matters for IA_KIND_GENERIC
 * (config_stacked); hour_only = local->tm_min == 0. */
const IaLayout *ia_layout_for(int kind, int stacked, int hour_only);

/* Per-frame geometry, all from the master progress p (0..IA_ONE). */
void ia_digit_rect(const IaLayout *l, int i, int p, IaRect *out);
int  ia_divider(int p);              /* 0..IA_ONE, fraction of divider drawn */
int  ia_panel_alpha(int p);          /* 0..255, weather panel opacity */
void ia_date(int p,
             int *center_dx, int *center_alpha,   /* date at its normal spot */
             int *left_dx, int *left_alpha);      /* date on the info side */

/* alpha 0..255; use to fade text/icons toward the face background. */
unsigned short ia_blend565(unsigned short bg, unsigned short fg, int alpha);

#endif
