/* Host-side tests for info_anim.c.  Build/run: see Makefile / README. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "info_anim.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, msg) do { \
    ++checks; \
    if (!(cond)) { ++failures; printf("FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

static int
same(const IaRect *a, const IaRect *b)
{
    return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h;
}

static void
test_layout_endpoints(void)
{
    int kind, stacked, hour, i;

    for (kind = 0; kind <= IA_KIND_ROBOTO; ++kind)
        for (stacked = 0; stacked <= 1; ++stacked)
            for (hour = 0; hour <= 1; ++hour) {
                const IaLayout *l = ia_layout_for(kind, stacked, hour);
                CHECK(l != NULL, "layout exists");
                CHECK(l->n == (hour ? 2 : 4), "digit count matches hour_only");
                for (i = 0; i < l->n; ++i) {
                    IaRect r;
                    ia_digit_rect(l, i, 0, &r);
                    CHECK(same(&r, &l->from[i]), "p=0 is the normal clock");
                    ia_digit_rect(l, i, IA_ONE, &r);
                    CHECK(same(&r, &l->to[i]), "p=1024 is the info layout");
                    CHECK(l->to[i].x + l->to[i].w <= 320 &&
                          l->to[i].y + l->to[i].h <= 240, "info rect on screen");
                    CHECK(l->from[i].x + l->from[i].w <= 320 &&
                          l->from[i].y + l->from[i].h <= 240,
                          "normal rect on screen");
                }
            }
}

static void
test_roboto_side_by_side_layout(void)
{
    const IaLayout *l = ia_layout_for(IA_KIND_ROBOTO, 0, 0);
    const IaLayout *font = ia_layout_for(IA_KIND_FONT, 0, 0);
    int i;

    CHECK(l->n == 4, "Roboto side-by-side keeps all four digits");
    CHECK(l->from[0].y == l->from[1].y &&
          l->from[2].y == l->from[3].y,
          "Roboto starts as two horizontal digit pairs");
    CHECK(l->from[0].x + l->from[1].w < l->from[2].x,
          "Roboto leaves a centered gap for the colon");
    CHECK(l->to[0].y == l->to[1].y &&
          l->to[2].y == l->to[3].y &&
          l->to[0].y < l->to[2].y,
          "Roboto info endpoint is stacked");
    CHECK((l->to[0].x + l->to[3].x + l->to[3].w) / 2 == 240,
          "Roboto expanded digits are centered in the right half");
    for (i = 0; i < l->n; ++i) {
        CHECK(l->to[i].x >= 160,
              "Roboto info digits remain on the right half");
        CHECK(l->to[i].x + l->to[i].w <= 320,
              "Roboto info digits fit inside the screen");
    }
    l = ia_layout_for(IA_KIND_ROBOTO, 0, 1);
    CHECK(l->n == 2, "Roboto hour-only info shows two digits");
    CHECK((l->to[0].x + l->to[1].x + l->to[1].w) / 2 == 240,
          "Roboto hour-only digits are centered in the right half");
    for (i = 0; i < l->n; ++i)
        CHECK(l->to[i].x >= 160 &&
              l->to[i].x + l->to[i].w <= 320,
              "Roboto hour-only digits fit in the right half");
    CHECK(font->from[0].y < font->from[2].y,
          "Ubuntu and Nunito retain their existing stacked layout");
    l = ia_layout_for(IA_KIND_ROBOTO, 0, 0);
    for (i = 0; i < l->n; ++i) {
        IaRect r;
        ia_digit_rect(l, i, 0, &r);
        CHECK(same(&r, &l->from[i]), "Roboto starts side-by-side at p=0");
        ia_digit_rect(l, i, IA_ONE, &r);
        CHECK(same(&r, &l->to[i]), "Roboto settles stacked at p=1");
    }
}

static void
test_monotone_and_no_overshoot(void)
{
    int kind, stacked, hour, i, p;

    for (kind = 0; kind <= IA_KIND_ROBOTO; ++kind)
        for (stacked = 0; stacked <= 1; ++stacked)
            for (hour = 0; hour <= 1; ++hour) {
                const IaLayout *l = ia_layout_for(kind, stacked, hour);
                for (i = 0; i < l->n; ++i) {
                    IaRect prev, cur;
                    int dx = l->to[i].x - l->from[i].x;
                    int ok = 1;
                    ia_digit_rect(l, i, 0, &prev);
                    for (p = 1; p <= IA_ONE; ++p) {
                        ia_digit_rect(l, i, p, &cur);
                        if ((dx >= 0 && cur.x < prev.x) ||
                            (dx < 0 && cur.x > prev.x))
                            ok = 0;
                        prev = cur;
                    }
                    CHECK(ok, "x never moves backwards / overshoots");
                }
            }
}

static void
test_timeline_endpoints(void)
{
    int cd, ca, ld, la;

    ia_date(0, &cd, &ca, &ld, &la);
    CHECK(cd == 0 && ca == 255, "date starts centered, opaque");
    CHECK(la == 0 && ld == -80, "left date starts hidden at -80");
    CHECK(ia_divider(0) == 0 && ia_panel_alpha(0) == 0, "no divider/panel at 0");
    ia_date(IA_ONE, &cd, &ca, &ld, &la);
    CHECK(cd == 80 && ca == 0, "center date fully gone at the end");
    CHECK(ld == 0 && la == 255, "left date settled, opaque");
    CHECK(ia_divider(IA_ONE) == IA_ONE, "divider full");
    CHECK(ia_panel_alpha(IA_ONE) == 255, "panel opaque");
    /* stagger: at 100 ms the divider has not started, panel at 200 ms not */
    CHECK(ia_divider(100 * IA_ONE / IA_TOTAL_MS) == 0, "divider waits 100 ms");
    CHECK(ia_panel_alpha(200 * IA_ONE / IA_TOTAL_MS) == 0, "panel waits 200 ms");
    /* date handoff: gone by 280, new one not before 320 */
    ia_date(300 * IA_ONE / IA_TOTAL_MS, &cd, &ca, &ld, &la);
    CHECK(ca == 0 && la == 0, "date is fully out before the new one arrives");
}

static void
test_open_close_and_reversal(void)
{
    InfoAnim a;
    int before, after, p;
    unsigned long t;

    ia_init(&a);
    CHECK(ia_progress(&a, 1000) == 0 && !ia_active(&a, 1000), "starts settled");
    ia_request(&a, 1, 1000);
    CHECK(ia_progress(&a, 1000) == 0, "opening starts at 0");
    CHECK(ia_active(&a, 1200), "active mid-way");
    CHECK(ia_progress(&a, 1000 + IA_TOTAL_MS) == IA_ONE, "fully open at 600 ms");
    CHECK(!ia_active(&a, 1000 + IA_TOTAL_MS), "not active when done");

    /* reverse at 200 ms: no jump, then runs back down */
    ia_init(&a);
    ia_request(&a, 1, 1000);
    before = ia_progress(&a, 1200);
    ia_request(&a, 0, 1200);
    after = ia_progress(&a, 1200);
    CHECK(before == after, "reversal does not jump");
    CHECK(before > 0 && before < IA_ONE, "was mid-animation");
    p = ia_progress(&a, 1250);
    CHECK(p < before, "progress now decreases");
    CHECK(ia_progress(&a, 1200 + IA_TOTAL_MS) == 0, "returns to 0");
    /* closing from p takes proportionally less time (no restart) */
    CHECK(ia_progress(&a, 1200 + before * IA_TOTAL_MS / IA_ONE + 2) == 0,
          "close from mid-way is shorter, not a full 600 ms");

    /* settled state survives a huge time jump (32-bit ms wrap) */
    ia_init(&a);
    ia_request(&a, 1, 1000);
    t = 1000 + IA_TOTAL_MS + 5;
    {
        unsigned long tick;

        for (tick = 1100; tick < t; tick += 100)
            ia_tick(&a, tick);
    }
    ia_tick(&a, t);
    CHECK(a.dir == 0, "tick settles a finished animation");
    CHECK(ia_progress(&a, t + 0x7ffffff0UL) == IA_ONE, "stays open after ~25 days");

    /* clock jumping backwards (GPS time sync) must not break anything */
    ia_init(&a);
    ia_request(&a, 1, 5000);
    p = ia_progress(&a, 4000);
    CHECK(p >= 0 && p <= IA_ONE, "progress stays in range on backwards clock");
    CHECK(p == 0, "backwards clock behaves as 'just started'");
    /* 32-bit millisecond counter wrapping in the middle of an animation */
    ia_init(&a);
    ia_request(&a, 1, 0xffffff00UL);
    p = ia_progress(&a, 0x00000100UL);          /* 512 ms later, wrapped */
    CHECK(p == 512 * IA_ONE / IA_TOTAL_MS, "animation survives counter wrap");
    /* repeated toggles never leave range */
    ia_init(&a);
    for (t = 0; t < 5000; t += 37) {
        if ((t / 37) % 3 == 0)
            ia_tap(&a, t);
        p = ia_progress(&a, t);
        CHECK(p >= 0 && p <= IA_ONE, "fuzzed taps stay in range");
    }
}

static void
test_policy(void)
{
    InfoAnim a;

    /* noteworthy opens immediately the first time */
    ia_init(&a);
    ia_update(&a, 1, 1000);
    CHECK(a.open == 1, "noteworthy opens the panel");
    /* flapping is held off by the dwell time */
    ia_update(&a, 0, 1000 + 10000);
    CHECK(a.open == 1, "does not close within 30 s of opening");
    ia_update(&a, 0, 1000 + IA_DWELL_MS + 1);
    CHECK(a.open == 0, "closes after the dwell time");
    ia_update(&a, 1, 1000 + IA_DWELL_MS + 2000);
    CHECK(a.open == 0, "re-open also waits");
    ia_update(&a, 1, 1000 + 2 * IA_DWELL_MS + 2);
    CHECK(a.open == 1, "re-opens after the dwell");

    /* a tap works instantly and survives while data is unchanged */
    ia_init(&a);
    ia_update(&a, 0, 100);
    ia_tap(&a, 200);
    CHECK(a.open == 1, "tap opens with nothing noteworthy");
    ia_update(&a, 0, 300);
    CHECK(a.open == 1, "manual open is not auto-closed");
    ia_tap(&a, 400);
    CHECK(a.open == 0, "second tap closes");
    ia_tap(&a, 450);
    CHECK(a.open == 1, "rapid taps toggle again");
    /* new data clears the manual override */
    ia_update(&a, 1, 500);
    ia_update(&a, 0, 500 + 2 * IA_DWELL_MS);
    CHECK(a.open == 0, "fresh data returns control to auto mode");
}

static void
test_stall_clamp_and_colon(void)
{
    InfoAnim a;
    int p_before;
    int p_after;
    int p_without_clamp;
    int previous;
    int p;
    int valid;

    CHECK(ia_colon_alpha(0) == 255, "colon visible when closed");
    CHECK(ia_colon_alpha(200 * IA_ONE / IA_TOTAL_MS) == 0,
          "colon fades by 200 ms");
    CHECK(ia_colon_alpha(IA_ONE) == 0, "colon hidden when open");
    previous = 255;
    valid = 1;
    for (p = 0; p <= IA_ONE; ++p) {
        int alpha = ia_colon_alpha(p);
        if (alpha > previous || alpha < 0 || alpha > 255)
            valid = 0;
        previous = alpha;
    }
    CHECK(valid, "colon alpha decreases monotonically");

    ia_init(&a);
    ia_request(&a, 1, 1000);
    ia_tick(&a, 1050);
    p_before = ia_progress(&a, 1050);
    ia_tick(&a, 1400);
    p_after = ia_progress(&a, 1400);
    p_without_clamp = 400 * IA_ONE / IA_TOTAL_MS;
    CHECK(p_after - p_before <= (IA_MAX_STEP_MS + 1) * IA_ONE /
                                     IA_TOTAL_MS,
          "stall advances by at most the clamp interval");
    CHECK(p_after < p_without_clamp, "stall pauses part of the timeline");
    CHECK(p_after > p_before, "animation continues after a stall");

    ia_init(&a);
    ia_request(&a, 1, 5000);
    ia_tick(&a, 5033);
    ia_tick(&a, 5066);
    CHECK(ia_progress(&a, 5066) == 66 * IA_ONE / IA_TOTAL_MS,
          "normal frame gaps are not clamped");

    ia_init(&a);
    ia_request(&a, 1, 0);
    ia_tick(&a, 300);
    CHECK(ia_active(&a, 300), "stall does not finish the animation early");
    ia_tick(&a, 330);
    ia_tick(&a, 430);
    ia_tick(&a, 530);
    ia_tick(&a, 630);
    ia_tick(&a, 730);
    ia_tick(&a, 830);
    ia_tick(&a, 930);
    CHECK(!ia_active(&a, 930) && ia_progress(&a, 930) == IA_ONE,
          "clamped animation completes after its extended timeline");
}

static void
test_blend(void)
{
    CHECK(ia_blend565(0x0000, 0xffff, 0) == 0x0000, "alpha 0 = background");
    CHECK(ia_blend565(0x0000, 0xffff, 255) == 0xffff, "alpha 255 = foreground");
    CHECK(ia_blend565(0x1234, 0x1234, 128) == 0x1234, "same colour is stable");
    CHECK(ia_blend565(0x0000, 0xf800, 128) >> 11 == 16, "half red");
}

static void
report_speed(void)
{
    const IaLayout *l = ia_layout_for(IA_KIND_NUMERALS, 1, 0);
    IaRect prev, cur;
    int p, i, worst = 0;
    /* largest movement between two frames 33 ms apart */
    int step = 33 * IA_ONE / IA_TOTAL_MS;

    for (i = 0; i < l->n; ++i) {
        ia_digit_rect(l, i, 0, &prev);
        for (p = step; p <= IA_ONE + step; p += step) {
            int d;
            ia_digit_rect(l, i, p > IA_ONE ? IA_ONE : p, &cur);
            d = abs(cur.x - prev.x);
            if (abs(cur.y - prev.y) > d) d = abs(cur.y - prev.y);
            if (d > worst) worst = d;
            prev = cur;
        }
    }
    printf("info: numerals stacked, largest jump per 33 ms frame = %d px\n", worst);
}

static int
dump(int kind, int stacked, int hour)
{
    const IaLayout *l = ia_layout_for(kind, stacked, hour);
    int t, i;

    printf("t,n,cd,ca,ld,la,div,panel");
    for (i = 0; i < l->n; ++i)
        printf(",x%d,y%d,w%d,h%d", i, i, i, i);
    printf("\n");
    for (t = 0; t <= IA_TOTAL_MS; t += 50) {
        int p = t * IA_ONE / IA_TOTAL_MS;
        int cd, ca, ld, la;
        ia_date(p, &cd, &ca, &ld, &la);
        printf("%d,%d,%d,%d,%d,%d,%d,%d", t, l->n, cd, ca, ld, la,
               ia_divider(p), ia_panel_alpha(p));
        for (i = 0; i < l->n; ++i) {
            IaRect r;
            ia_digit_rect(l, i, p, &r);
            printf(",%d,%d,%d,%d", r.x, r.y, r.w, r.h);
        }
        printf("\n");
    }
    return 0;
}

int
main(int argc, char **argv)
{
    if (argc == 5 && strcmp(argv[1], "dump") == 0)
        return dump(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));
    test_layout_endpoints();
    test_roboto_side_by_side_layout();
    test_monotone_and_no_overshoot();
    test_timeline_endpoints();
    test_open_close_and_reversal();
    test_policy();
    test_stall_clamp_and_colon();
    test_blend();
    report_speed();
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
