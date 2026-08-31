/*
 * What the battery screen decides: the verdict, and that the spread it is
 * based on is the widest gap rather than a distance from some nominal.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "battery_screen.h"
#include "ui_screen.h"
#include "ui_theme.h"

#define W 800
#define H 480

static gfx_color_t *fb;
static gfx_canvas_t cv;
static const ui_screen_t *scr;

static void fresh(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    scr = battery_screen();
    scr->reset();
}

static int count_of(gfx_color_t c)
{
    int n = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] == c) { ++n; }
    }
    return n;
}

/* A pack of @p cells, all at @p base except the last, which is @p sag volts
 * lower.  A weak cell is the case this screen exists for. */
static void pack(int cells, float base, float sag)
{
    battery_state_t b;
    memset(&b, 0, sizeof(b));
    b.cells = cells;
    for (int i = 0; i < cells; ++i) {
        b.volts[i] = base;
        b.milliohms[i] = 2.0f;
    }
    b.volts[cells - 1] = base - sag;
    b.capacity_mah = 5000.0f;
    b.drawn_mah = 1000.0f;
    b.valid = true;
    battery_screen_set(&b);
}

/*
 * The spread is the widest gap between any two cells, not a departure from
 * a nominal.  A pack that is flat but even is a discharged pack; a pack that
 * is full but uneven is a broken one, and only the second is this screen's
 * business.
 */
TEST_CASE(spread_is_between_cells_not_from_nominal)
{
    fresh();
    pack(6, 3.80f, 0.0f);
    CHECK(battery_screen_spread_mv() < 1.0f);

    /* Every cell low together: still no spread. */
    pack(6, 3.20f, 0.0f);
    CHECK(battery_screen_spread_mv() < 1.0f);

    /* One cell down: that is a spread, at any state of charge. */
    pack(6, 3.20f, 0.040f);
    CHECK_NEAR(battery_screen_spread_mv(), 40.0f, 0.5f);
}

/* Three verdicts, and each has to look different. */
TEST_CASE(the_verdict_follows_the_spread)
{
    fresh();
    pack(6, 3.80f, 0.005f);
    scr->render(&cv, 0);
    CHECK(count_of(ui_theme_color(UI_C_OK)) > 0);
    CHECK_EQ(count_of(ui_theme_color(UI_C_DANGER)), 0);

    pack(6, 3.80f, 0.040f);
    scr->render(&cv, 0);
    CHECK(count_of(ui_theme_color(UI_C_WARN)) > 0);
    CHECK_EQ(count_of(ui_theme_color(UI_C_OK)), 0);

    pack(6, 3.80f, 0.090f);
    scr->render(&cv, 0);
    CHECK(count_of(ui_theme_color(UI_C_DANGER)) > 0);
    CHECK_EQ(count_of(ui_theme_color(UI_C_OK)), 0);
}

/* Nothing connected is its own state, not a pack of zeroes. */
TEST_CASE(no_pack_is_not_a_flat_pack)
{
    fresh();
    scr->render(&cv, 0);
    CHECK_EQ(count_of(ui_theme_color(UI_C_DANGER)), 0);
    CHECK_EQ(count_of(ui_theme_color(UI_C_OK)), 0);
    CHECK(count_of(ui_theme_color(UI_C_TEXT_FAINT)) > 0);
}

/*
 * The scale follows the pack.  A fixed one draws a healthy pack as flat lines
 * and a sick one as flat lines with a stub, which is the same picture -- so a
 * small spread and a large one must not render alike.
 */
TEST_CASE(the_scale_follows_the_pack)
{
    fresh();
    pack(6, 3.80f, 0.010f);
    scr->render(&cv, 0);
    gfx_color_t *small = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(small, fb, (size_t)W * H * sizeof(gfx_color_t));

    pack(6, 3.80f, 0.020f);
    scr->render(&cv, 0);
    int differ = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] != small[i]) { ++differ; }
    }
    /* The bars are the same *shape* -- one cell down, five level -- so if the
     * scale did not move, doubling the sag would change almost nothing. */
    if (differ < 200) {
        T_FAIL("doubling the spread changed %d pixels", differ);
    }
    free(small);
}


/*
 * One to fourteen cells was the promise, and the bar geometry is derived from
 * the count: a 14S pack has a third of the width per cell that a 4S does, and
 * the clamps that keep a bar drawable are the kind of arithmetic that is fine
 * at six and wrong at fourteen.
 */
TEST_CASE(every_cell_count_draws)
{
    for (int n = 1; n <= BATTERY_CELLS_MAX; ++n) {
        fresh();
        pack(n, 3.80f, (n > 1) ? 0.030f : 0.0f);
        scr->render(&cv, 0);

        int lit = 0;
        for (int i = 0; i < W * H; ++i) {
            if (fb[i] != ui_theme_color(UI_C_BG)) { ++lit; }
        }
        if (lit < 15000) {
            T_FAIL("%dS drew only %d pixels", n, lit);
        }
        /*
         * And nothing escapes the card it is drawn in.  The gutter is only
         * the eight pixels between the two cards -- 501 to 507 -- and not a
         * pixel more: the right card starts at 508 and is entitled to be
         * there, which is what this check first accused it of.
         */
        int stray = 0;
        for (int y = 0; y < H; ++y) {
            for (int x = 501; x < 508; ++x) {
                if (fb[(size_t)y * W + x] != ui_theme_color(UI_C_BG)) {
                    ++stray;
                }
            }
        }
        if (stray > 0) {
            T_FAIL("%dS put %d pixels in the gutter", n, stray);
        }
    }
}

int main(void)
{
    RUN(spread_is_between_cells_not_from_nominal);
    RUN(the_verdict_follows_the_spread);
    RUN(no_pack_is_not_a_flat_pack);
    RUN(the_scale_follows_the_pack);
    RUN(every_cell_count_draws);
    return test_summary("battery");
}
