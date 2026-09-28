/* info_anim.c - see info_anim.h.  C89, integer math only. */
#include "info_anim.h"
#include "info_anim_lut.h"

/* ------------------------------------------------------------------ *
 * Timeline, in ms of the 600 ms master clock.  Mirrors the React
 * prototype's CSS transitions exactly:
 *   clock   0..500   cubic-bezier(.4,0,.2,1)
 *   divider 100..600 cubic-bezier(.4,0,.2,1)  (grows down from the top)
 *   panel   200..600 ease                     (opacity)
 *   date    leaves 0..280 to the right, arrives 320..600 from the left
 * ------------------------------------------------------------------ */
#define CLOCK_START    0
#define CLOCK_DUR      500
#define DIVIDER_START  100
#define DIVIDER_DUR    500
#define PANEL_START    200
#define PANEL_DUR      400
#define DATE_OUT_START 0
#define DATE_OUT_DUR   280
#define DATE_IN_START  320
#define DATE_IN_DUR    280
#define DATE_SLIDE_PX  80

/* ------------------------------------------------------------------ *
 * Layout tables.  Each row cites where the numbers come from in
 * live_watchface.c (draw_frame) so they can be re-synced if that changes.
 * All info-state ("to") rects are the existing weather layouts.
 * ------------------------------------------------------------------ */
#define NUM_INFO_4 \
    {{162,   1, 76, 118}, {240,   1, 76, 118}, \
     {162, 120, 76, 118}, {240, 120, 76, 118}}
#define FONT_INFO_4 \
    {{164,   1, 70, 118}, {238,   1, 70, 118}, \
     {164, 120, 70, 118}, {238, 120, 70, 118}}
#define INFO_2 \
    {{162, 55, 76, 130}, {240, 55, 76, 130}, {0, 0, 0, 0}, {0, 0, 0, 0}}

static const IaLayout layouts[] = {
    /* [0] generic, side-by-side: digit_x[] = 8,82,170,244; DIGIT_Y 42;
     *     DIGIT_W x DIGIT_H = 70x156 */
    { 4,
      {{  8, 42, 70, 156}, { 82, 42, 70, 156},
       {170, 42, 70, 156}, {244, 42, 70, 156}},
      NUM_INFO_4 },
    /* [1] generic, config_stacked: stacked_x[] = 112,173; y 34 / 126;
     *     DIGIT_W/2 x DIGIT_H/2 = 35x78 */
    { 4,
      {{112, 34, 35, 78}, {173, 34, 35, 78},
       {112, 126, 35, 78}, {173, 126, 35, 78}},
      NUM_INFO_4 },
    /* [2] Numerals Duo stacked: x 83+col*84, y 38 / 128, 70x82 */
    { 4,
      {{ 83,  38, 70, 82}, {167,  38, 70, 82},
       { 83, 128, 70, 82}, {167, 128, 70, 82}},
      NUM_INFO_4 },
    /* [3] font faces stacked: x 101+col*63, y 42+row*88, 55x88;
     *     info x 164+col*74, 70x118 */
    { 4,
      {{101,  42, 55, 88}, {164,  42, 55, 88},
       {101, 130, 55, 88}, {164, 130, 55, 88}},
      FONT_INFO_4 },
    /* [4] hour only, generic: hour_only_x[] = 86,160; DIGIT_Y 42; 70x156 */
    { 2,
      {{ 86, 42, 70, 156}, {160, 42, 70, 156}, {0, 0, 0, 0}, {0, 0, 0, 0}},
      INFO_2 },
    /* [5] hour only, Numerals Duo: x 45+i*120, y 35, 110x190 */
    { 2,
      {{ 45, 35, 110, 190}, {165, 35, 110, 190}, {0, 0, 0, 0}, {0, 0, 0, 0}},
      INFO_2 },
    /* [6] hour only, font faces: x 46+i*116, y 40, 112x176 */
    { 2,
      {{ 46, 40, 112, 176}, {162, 40, 112, 176}, {0, 0, 0, 0}, {0, 0, 0, 0}},
      INFO_2 }
};

const IaLayout *
ia_layout_for(int kind, int stacked, int hour_only)
{
    if (hour_only) {
        if (kind == IA_KIND_NUMERALS)
            return &layouts[5];
        if (kind == IA_KIND_FONT)
            return &layouts[6];
        return &layouts[4];
    }
    if (kind == IA_KIND_NUMERALS)
        return &layouts[2];
    if (kind == IA_KIND_FONT)
        return &layouts[3];
    return stacked ? &layouts[1] : &layouts[0];
}

/* ------------------------------ helpers ------------------------------ */

static int
clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* 0..IA_ONE -> eased 0..IA_ONE via a 33-entry table, linear in between */
static int
ease(const unsigned short *lut, int t)
{
    int i;
    int f;
    int diff;

    if (t <= 0)
        return 0;
    if (t >= IA_ONE)
        return IA_ONE;
    i = t >> 5;
    f = t & 31;
    diff = (int)lut[i + 1] - (int)lut[i];
    return (int)lut[i] + (diff * f + 16) / 32;
}

/* where a window [start, start+dur] ms is, at master-clock time pm ms,
 * as 0..IA_ONE (clamped) */
static int
window(int pm, int start_ms, int dur_ms)
{
    return clampi((pm - start_ms) * IA_ONE / dur_ms, 0, IA_ONE);
}

static int
master_ms(int p)
{
    return p * IA_TOTAL_MS / IA_ONE;
}

/* Milliseconds since t0.  Works modulo 2^32 on every platform (the device's
 * unsigned long is 32-bit, a PC's is 64-bit), so a wrapped counter still
 * gives a correct short interval.  A clock that went backwards counts as 0. */
static unsigned long
since(unsigned long now_ms, unsigned long t0)
{
    unsigned long d = (now_ms - t0) & 0xffffffffUL;

    return d > 0x7fffffffUL ? 0UL : d;
}

/* ---------------------------- state machine ---------------------------- */

void
ia_init(InfoAnim *a)
{
    a->p0 = 0;
    a->dir = 0;
    a->t0 = 0;
    a->open = 0;
    a->prev_noteworthy = 0;
    a->manual = 0;
    a->manual_open = 0;
    a->have_changed = 0;
    a->last_change = 0;
}

int
ia_progress(const InfoAnim *a, unsigned long now_ms)
{
    unsigned long d;
    int delta;

    if (a->dir == 0)
        return a->p0;
    d = since(now_ms, a->t0);
    if (d > (unsigned long)IA_TOTAL_MS)
        d = (unsigned long)IA_TOTAL_MS;
    delta = (int)d * IA_ONE / IA_TOTAL_MS;
    return clampi(a->p0 + a->dir * delta, 0, IA_ONE);
}

void
ia_request(InfoAnim *a, int open, unsigned long now_ms)
{
    int p;

    open = open != 0;
    if (open == a->open)
        return;
    p = ia_progress(a, now_ms);     /* continue from wherever we are */
    a->p0 = p;
    a->t0 = now_ms;
    a->dir = open ? 1 : -1;
    a->open = open;
    a->last_change = now_ms;
    a->have_changed = 1;
}

/* Collapse a finished animation to "settled" so long uptimes cannot wrap
 * the 32-bit millisecond counter into a fake restart. */
void
ia_tick(InfoAnim *a, unsigned long now_ms)
{
    int p;

    if (a->dir == 0)
        return;
    p = ia_progress(a, now_ms);
    if (p == (a->open ? IA_ONE : 0)) {
        a->p0 = p;
        a->dir = 0;
    }
}

int
ia_active(const InfoAnim *a, unsigned long now_ms)
{
    if (a->dir == 0)
        return 0;
    return ia_progress(a, now_ms) != (a->open ? IA_ONE : 0);
}

/* ------------------------------- policy ------------------------------- */

void
ia_update(InfoAnim *a, int noteworthy, unsigned long now_ms)
{
    int want;

    noteworthy = noteworthy != 0;
    if (noteworthy != a->prev_noteworthy) {
        a->prev_noteworthy = noteworthy;
        a->manual = 0;              /* fresh data cancels a manual override */
    }
    want = a->manual ? a->manual_open : noteworthy;
    if (want == a->open)
        return;
    /* hysteresis: automatic changes wait out the dwell time */
    if (!a->manual && a->have_changed &&
        since(now_ms, a->last_change) < (unsigned long)IA_DWELL_MS)
        return;
    ia_request(a, want, now_ms);
}

void
ia_tap(InfoAnim *a, unsigned long now_ms)
{
    a->manual = 1;
    a->manual_open = !a->open;
    ia_request(a, a->manual_open, now_ms);
}

/* ------------------------------ geometry ------------------------------ */

void
ia_digit_rect(const IaLayout *l, int i, int p, IaRect *out)
{
    int e = ease(ia_lut_material,
                 window(master_ms(p), CLOCK_START, CLOCK_DUR));
    int inv = IA_ONE - e;
    const IaRect *f = &l->from[i];
    const IaRect *t = &l->to[i];

    /* all coordinates are >= 0, so +512 >> 10 rounds to nearest */
    out->x = (short)((f->x * inv + t->x * e + 512) >> 10);
    out->y = (short)((f->y * inv + t->y * e + 512) >> 10);
    out->w = (short)((f->w * inv + t->w * e + 512) >> 10);
    out->h = (short)((f->h * inv + t->h * e + 512) >> 10);
}

int
ia_divider(int p)
{
    return ease(ia_lut_material,
                window(master_ms(p), DIVIDER_START, DIVIDER_DUR));
}

int
ia_panel_alpha(int p)
{
    return ease(ia_lut_css_ease,
                window(master_ms(p), PANEL_START, PANEL_DUR)) * 255 / IA_ONE;
}

void
ia_date(int p, int *center_dx, int *center_alpha,
        int *left_dx, int *left_alpha)
{
    int pm = master_ms(p);
    int t_out = window(pm, DATE_OUT_START, DATE_OUT_DUR);
    int t_in = window(pm, DATE_IN_START, DATE_IN_DUR);

    /* leaves to the right while fading out */
    *center_dx = DATE_SLIDE_PX * ease(ia_lut_material, t_out) / IA_ONE;
    *center_alpha = 255 - ease(ia_lut_css_ease, t_out) * 255 / IA_ONE;
    /* arrives from the left while fading in */
    *left_dx = -DATE_SLIDE_PX +
               DATE_SLIDE_PX * ease(ia_lut_material, t_in) / IA_ONE;
    *left_alpha = ease(ia_lut_css_ease, t_in) * 255 / IA_ONE;
}

unsigned short
ia_blend565(unsigned short bg, unsigned short fg, int alpha)
{
    int inv;
    int r;
    int g;
    int b;

    alpha = clampi(alpha, 0, 255);
    inv = 255 - alpha;
    r = ((bg >> 11) & 31) * inv + ((fg >> 11) & 31) * alpha;
    g = ((bg >> 5) & 63) * inv + ((fg >> 5) & 63) * alpha;
    b = (bg & 31) * inv + (fg & 31) * alpha;
    r = (r + 127) / 255;
    g = (g + 127) / 255;
    b = (b + 127) / 255;
    return (unsigned short)((r << 11) | (g << 5) | b);
}
