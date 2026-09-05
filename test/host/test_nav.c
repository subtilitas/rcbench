/*
 * The shell: the band, the router, and the two safety properties they carry.
 *
 * STOP has to work from every screen that can have something armed behind it,
 * and no screen may draw over it.  The second is enforced by handing screens a
 * sub-canvas, and this file checks the enforcement.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "gfx.h"
#include "overview_screen.h"
#include "splash_screen.h"
#include "stub_screen.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_theme.h"
#include "ui_watermark.h"
#include "ui_widgets.h"

#define W 800
#define H 480

static gfx_color_t *fb;
static gfx_canvas_t cv;

static void fresh(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    ui_router_init();
}

static void touch(int x, int y, touch_event_type_t type, uint8_t id)
{
    touch_event_t e = { .type = type,
                        .point = { .id = id, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    ui_router_event(&e);
}

static void tap(int x, int y)
{
    touch(x, y, TOUCH_EVENT_DOWN, 1);
    touch(x, y, TOUCH_EVENT_UP, 1);
}

/** Leave the splash the way the application does. */
static void to_overview(void)
{
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    ui_router_goto(SCREEN_OVERVIEW);
}

TEST_CASE(the_router_starts_on_the_splash)
{
    fresh();
    CHECK_EQ(ui_router_current(), SCREEN_SPLASH);
}

/* The splash has nothing to stop and nowhere to go back to, so it owns the
 * whole panel.  Everything else carries the band. */
TEST_CASE(the_splash_has_no_band_and_everything_else_does)
{
    fresh();
    ui_router_render(&cv, 0);
    /* Nothing in the band's rows should look like a STOP button on the
     * splash: the screen painted the whole canvas itself. */
    const gfx_rect_t stop = ui_band_stop_rect();
    const gfx_color_t danger = ui_theme_color(UI_C_DANGER);
    bool found = false;
    for (int y = stop.y; y < stop.y + stop.h && !found; ++y) {
        for (int x = stop.x; x < stop.x + stop.w; ++x) {
            if (fb[(size_t)y * W + x] == danger) { found = true; break; }
        }
    }
    CHECK(!found);

    to_overview();
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);
    found = false;
    for (int y = stop.y; y < stop.y + stop.h && !found; ++y) {
        for (int x = stop.x; x < stop.x + stop.w; ++x) {
            if (fb[(size_t)y * W + x] == danger) { found = true; break; }
        }
    }
    CHECK(found);
}

/*
 * The property the sub-canvas exists for: after a full render, the band is
 * still there.  Every screen begins by clearing its canvas, so a screen handed
 * the panel instead of a window into it erases STOP.
 *
 * The check looks for the band's own pixels.  Comparing two renders of the
 * band region is not sufficient: without the sub-canvas both are wiped
 * identically.
 */
TEST_CASE(no_screen_can_draw_over_the_band)
{
    fresh();
    to_overview();

    const gfx_rect_t stop = ui_band_stop_rect();
    const gfx_color_t danger = ui_theme_color(UI_C_DANGER);

    for (int id = SCREEN_OVERVIEW; id < SCREEN_COUNT; ++id) {
        ui_router_goto((ui_screen_id_t)id);
        memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
        ui_router_render(&cv, 0);

        int lit = 0;
        for (int y = stop.y; y < stop.y + stop.h; ++y) {
            for (int x = stop.x; x < stop.x + stop.w; ++x) {
                if (fb[(size_t)y * W + x] == danger) {
                    ++lit;
                }
            }
        }
        if (lit < 200) {
            T_FAIL("screen %d left only %d STOP pixels standing", id, lit);
        }
    }
}

/* STOP is latched rather than dispatched, so the loop that owns the heartbeat
 * drains it: a stop stops the line as well as sending the command. */
TEST_CASE(stop_latches_and_clears_when_read)
{
    fresh();
    to_overview();
    CHECK(!ui_router_take_stop());

    const gfx_rect_t r = ui_band_stop_rect();
    tap(r.x + r.w / 2, r.y + r.h / 2);
    CHECK(ui_router_take_stop());
    CHECK(!ui_router_take_stop());   /* read once, gone */
}

/* Every screen that can have something armed behind it. */
TEST_CASE(stop_works_from_every_screen_that_has_a_band)
{
    fresh();
    to_overview();
    const gfx_rect_t r = ui_band_stop_rect();

    for (int id = SCREEN_OVERVIEW; id < SCREEN_COUNT; ++id) {
        ui_router_goto((ui_screen_id_t)id);
        (void)ui_router_take_stop();
        tap(r.x + r.w / 2, r.y + r.h / 2);
        if (!ui_router_take_stop()) {
            T_FAIL("STOP did nothing on screen %d", id);
        }
    }
}

/* A press that begins on STOP and slides off is not a stop, and neither is it
 * a tap on whatever it slid onto. */
TEST_CASE(a_press_that_slides_off_stop_does_nothing)
{
    fresh();
    to_overview();
    ui_router_goto(SCREEN_MOTOR);
    const gfx_rect_t r = ui_band_stop_rect();

    touch(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_DOWN, 1);
    touch(20, 300, TOUCH_EVENT_MOVE, 1);
    touch(20, 300, TOUCH_EVENT_UP, 1);
    CHECK(!ui_router_take_stop());
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
}

TEST_CASE(the_home_tag_returns_to_the_overview)
{
    fresh();
    to_overview();
    ui_router_goto(SCREEN_ANALYSER);
    CHECK_EQ(ui_router_current(), SCREEN_ANALYSER);
    tap(UI_TAG_X + 20, UI_TAG_Y + UI_TAG_H / 2);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/* The overview is where the home tag would take you, so it does not draw one:
 * an identity mark rather than a control. */
TEST_CASE(the_overview_has_no_home_tag)
{
    fresh();
    to_overview();
    tap(UI_TAG_X + 20, UI_TAG_Y + UI_TAG_H / 2);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/* A second finger, or a palm, must not steal the release the first is waiting
 * for; otherwise the throttle drag latches to whatever moves next. */
TEST_CASE(a_second_contact_cannot_steal_the_release)
{
    fresh();
    to_overview();
    const gfx_rect_t r = ui_band_stop_rect();

    touch(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_DOWN, 1);
    touch(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_UP, 2);   /* not ours */
    CHECK(!ui_router_take_stop());
    touch(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_UP, 1);
    CHECK(ui_router_take_stop());
}

/*
 * The two doors on the Setup screen. They are the only way to either
 * outputs view, so a tile that stopped navigating would leave a screen
 * nobody can reach -- and the geometry here is what says the second door
 * did not land on top of RESET CATEGORY.
 */
TEST_CASE(the_setup_screen_opens_both_views_of_the_outputs)
{
    /* Matching settings_screen.c: two doors of 42 in the left column. */
    const int door_x = 16 + 208 / 2;
    const int outputs_y = UI_BAND_H + 248 + 42 / 2;
    const int picker_y  = UI_BAND_H + 248 + 42 + 4 + 42 / 2;

    fresh();
    ui_router_goto(SCREEN_SETUP);
    tap(door_x, outputs_y);
    CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);

    ui_router_goto(SCREEN_SETUP);
    tap(door_x, picker_y);
    CHECK_EQ(ui_router_current(), SCREEN_PICKER);

    /* And a press that slides off a door is not a tap on it. */
    ui_router_goto(SCREEN_SETUP);
    touch(door_x, picker_y, TOUCH_EVENT_DOWN, 1);
    touch(door_x + 300, picker_y, TOUCH_EVENT_MOVE, 1);
    touch(door_x + 300, picker_y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
}

/* Tiles navigate, and a press that slid off its tile is not a tap on it. */
TEST_CASE(a_tile_navigates_and_a_slip_does_not)
{
    fresh();
    to_overview();

    /* First tile: top-left of the body, which is UI_BAND_H down the panel. */
    tap(110, UI_BAND_H + 90);
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);

    ui_router_goto(SCREEN_OVERVIEW);
    touch(110, UI_BAND_H + 90, TOUCH_EVENT_DOWN, 1);
    touch(310, UI_BAND_H + 90, TOUCH_EVENT_MOVE, 1);
    touch(310, UI_BAND_H + 90, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/* Every stub says what it will do, and either names a blocker or says plainly
 * that nothing is blocking it.  A screen that renders nothing is a screen
 * nobody notices is empty. */
TEST_CASE(every_stub_renders_something_and_says_something)
{
    fresh();
    to_overview();
    for (int id = SCREEN_MOTOR; id < SCREEN_COUNT; ++id) {
        ui_router_goto((ui_screen_id_t)id);
        memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
        ui_router_render(&cv, 0);

        int lit = 0;
        for (int y = UI_BAND_H; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                if (fb[(size_t)y * W + x] != ui_theme_color(UI_C_BG)) {
                    ++lit;
                }
            }
        }
        if (lit < 2000) {
            T_FAIL("screen %d drew only %d pixels", id, lit);
        }
        CHECK(ui_router_title((ui_screen_id_t)id)[0] != '\0');
    }
}

/* The alert survives a change of screen, because it describes the bench and
 * not the screen. */
TEST_CASE(an_alert_survives_navigation)
{
    fresh();
    to_overview();
    ui_router_set_alert("touch controller stopped answering");
    ui_router_goto(SCREEN_LOGS);
    CHECK(ui_router_alert() != NULL);
    ui_router_set_alert(NULL);
    CHECK(ui_router_alert() == NULL);
}

/* The band's other states: armed, and a fault badge.  Rendered here rather
 * than only on a faulted bench. */
TEST_CASE(the_band_shows_what_is_wrong)
{
    fresh();
    to_overview();

    const ui_bench_status_t bad = {
        .link_up = false, .armed = true, .faults = 0x11,
        .run_seconds = 0, .mode = NULL,
    };
    ui_router_set_status(&bad);
    ui_router_invalidate();
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);

    /* Armed and faulted both paint in the danger and warning colours, and
     * neither is the background. */
    int danger = 0, warn = 0;
    for (int y = 0; y < UI_BAND_H; ++y) {
        for (int x = 0; x < W; ++x) {
            const gfx_color_t p = fb[(size_t)y * W + x];
            if (p == ui_theme_color(UI_C_DANGER)) { ++danger; }
            if (p == ui_theme_color(UI_C_WARN))   { ++warn; }
        }
    }
    CHECK(danger > 400);   /* STOP, and the ARMED badge */
    CHECK(warn > 100);     /* the fault chip */

    CHECK_EQ(ui_router_status()->faults, 0x11);
}

/* The splash holds until every step has answered, then hands over.  A tap
 * skips the hold, but only once there is something to have read. */
TEST_CASE(the_splash_holds_then_hands_over)
{
    fresh();
    CHECK(!splash_screen_done());

    ui_router_tick(5.0f);
    CHECK(!splash_screen_done());        /* nothing has answered yet */

    tap(400, 300);
    CHECK(!splash_screen_done());        /* and a tap does not skip that */

    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "ok");
    }
    ui_router_render(&cv, 0);
    CHECK(!splash_screen_done());        /* answered, but not yet held */

    ui_router_tick(2.0f);
    CHECK(splash_screen_done());
}

TEST_CASE(a_tap_skips_the_splash_hold)
{
    fresh();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_WARN, "");
    }
    CHECK(!splash_screen_done());
    tap(400, 300);
    CHECK(splash_screen_done());
}

/* A failure is still reported: it renders, and it renders in its own colour,
 * so it can be read on the way past rather than stranding anyone. */
TEST_CASE(a_failed_step_is_drawn_in_its_own_colour)
{
    fresh();
    splash_screen_set(SPLASH_STEP_LINK, SPLASH_FAIL, "no answer");
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);

    int danger = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] == ui_theme_color(UI_C_DANGER)) { ++danger; }
    }
    CHECK(danger > 0);
}

/* The alert is drawn across the bottom, over controls that are not working
 * anyway rather than over the numbers. */
TEST_CASE(the_alert_band_renders_at_the_bottom)
{
    fresh();
    to_overview();
    ui_router_set_alert("touch controller stopped answering");
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);

    int danger_bottom = 0;
    for (int y = H - 34; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (fb[(size_t)y * W + x] == ui_theme_color(UI_C_DANGER)) {
                ++danger_bottom;
            }
        }
    }
    CHECK(danger_bottom > 5000);
    ui_router_set_alert(NULL);
}

/* ------------------------------------------------------- the simulation mark */

/*
 * The bench runs without hardware, and the risk is a modelled number being
 * photographed and quoted as a measured one.  The mark is drawn over
 * everything, on every screen, and no screen can opt out.
 */
TEST_CASE(the_simulation_mark_covers_the_whole_screen)
{
    fresh();
    to_overview();

    ui_bench_status_t st = *ui_router_status();
    st.simulated = false;
    ui_router_set_status(&st);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);
    gfx_color_t *plain = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(plain, fb, (size_t)W * H * sizeof(gfx_color_t));

    st.simulated = true;
    ui_router_set_status(&st);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);

    /*
     * The property is anti-crop: no horizontal strip and no vertical strip
     * of the screen is free of the mark, so no crop of a photograph loses
     * it.
     *
     * Quadrant counts are not asserted: a single diagonal word leaves a
     * corner quadrant with about 51 marked pixels.
     *
     * Ink inside the status band is not asserted either.  The router draws
     * the mark last, over the whole canvas, clipped to nothing; but the top
     * 48 rows are a corner of the diagonal, and their count depends on the
     * shape of the letters rather than on the policy.
     */
    const int strips = 5;
    for (int i = 0; i < strips; ++i) {
        int rows = 0, cols = 0;
        for (int y = i * H / strips; y < (i + 1) * H / strips; ++y) {
            for (int x = 0; x < W; ++x) {
                if (fb[(size_t)y * W + x] != plain[(size_t)y * W + x]) {
                    ++rows;
                }
            }
        }
        for (int x = i * W / strips; x < (i + 1) * W / strips; ++x) {
            for (int y = 0; y < H; ++y) {
                if (fb[(size_t)y * W + x] != plain[(size_t)y * W + x]) {
                    ++cols;
                }
            }
        }
        if (rows < 100) {
            T_FAIL("horizontal strip %d has only %d marked pixels", i, rows);
        }
        if (cols < 100) {
            T_FAIL("vertical strip %d has only %d marked pixels", i, cols);
        }
    }

    free(plain);
}

/*
 * The numbers underneath stay readable.
 *
 * The mark is a stencil, not a blend.  It is laid over screens that repaint
 * only what changed, and a blend applied to pixels already carrying the mark
 * compounds: 15% over 15% is 28%, then 39%, until solid.  Readability comes
 * from sparseness, which is what this measures: a marked pixel's neighbours
 * are mostly unmarked, so the content underneath reads through the gaps.  A
 * solid fill scores close to eight.
 */
TEST_CASE(the_simulation_mark_lets_the_screen_through)
{
    fresh();
    to_overview();
    ui_bench_status_t st = *ui_router_status();

    st.simulated = false;
    ui_router_set_status(&st);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);
    gfx_color_t *plain = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(plain, fb, (size_t)W * H * sizeof(gfx_color_t));

    st.simulated = true;
    ui_router_set_status(&st);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);

    const gfx_color_t ink = ui_theme_color(UI_C_TEXT);
    int changed = 0, not_ink = 0, marked_neighbours = 0;
    for (int y = 1; y < H - 1; ++y) {
        for (int x = 1; x < W - 1; ++x) {
            const size_t at = (size_t)y * W + (size_t)x;
            if (fb[at] == plain[at]) {
                continue;
            }
            ++changed;
            if (fb[at] != ink) {
                ++not_ink;
            }
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    const size_t n = (size_t)(y + dy) * W + (size_t)(x + dx);
                    if (fb[n] != plain[n]) {
                        ++marked_neighbours;
                    }
                }
            }
        }
    }
    CHECK(changed > 2000);
    /* A stencil, so a marked pixel is the ink colour and nothing between. */
    CHECK_EQ(not_ink, 0);
    if (marked_neighbours > changed * 4) {
        T_FAIL("marked pixels average %.1f marked neighbours of 8, which is "
               "closer to a fill than to a watermark",
               (double)marked_neighbours / (double)changed);
    }
    free(plain);
}

/*
 * Every screen caches its chrome per framebuffer, so ui_router_invalidate()
 * has to reach every screen.  Five of them had no hook at all, which left a
 * theme change or a change of the SIMULATED flag showing stale chrome, and
 * cost nothing measurable, so nothing caught it.  Scribbling over a settled
 * canvas and invalidating has to restore exactly what a fresh render draws.
 */
TEST_CASE(invalidating_the_router_repaints_every_screen)
{
    const size_t bytes = (size_t)W * H * sizeof(gfx_color_t);
    gfx_color_t *settled = malloc(bytes);
    CHECK(settled != NULL);
    if (settled == NULL) {
        return;
    }

    for (int id = 0; id < SCREEN_COUNT; ++id) {
        /*
         * Both framebuffers, because the caches are per buffer: a hook that
         * cleared one bit and not the other would pass a check of buffer 0
         * and leave the panel alternating between a repainted frame and a
         * stale one, which reads as flicker.
         */
        for (int buf = 0; buf < 2; ++buf) {
            fresh();
            ui_router_goto((ui_screen_id_t)id);
            ui_router_render(&cv, 0);
            ui_router_render(&cv, 1);
            ui_router_render(&cv, buf);
            memcpy(settled, fb, bytes);

            /* Something else owned the buffer in between. */
            memset(fb, 0x5a, bytes);
            ui_router_invalidate();
            ui_router_render(&cv, buf);

            if (memcmp(settled, fb, bytes) != 0) {
                long differ = 0;
                for (long i = 0; i < (long)W * H; ++i) {
                    if (settled[i] != fb[i]) {
                        ++differ;
                    }
                }
                T_FAIL("screen %d buffer %d keeps %ld px of stale chrome "
                       "after ui_router_invalidate()", id, buf, differ);
            }
        }
    }
    free(settled);
}

/*
 * The mark's stencil is cached, so a canvas whose geometry or ink differs
 * from the cached one has to rebuild it rather than replay the wrong points,
 * and a canvas covering more points than the table holds falls back to
 * scanning.  A 1600x960 canvas covers about four times the panel's 3,439.
 */
TEST_CASE(the_simulation_mark_follows_the_canvas_it_is_given)
{
    enum { BW = 1600, BH = 960 };
    gfx_color_t *big = calloc((size_t)BW * BH, sizeof(gfx_color_t));
    CHECK(big != NULL);
    if (big == NULL) {
        return;
    }
    gfx_canvas_t bc;
    gfx_canvas_init(&bc, big, BW, BH, 0);

    ui_watermark_invalidate();
    ui_watermark(&bc);
    int wide = 0;
    for (long i = 0; i < (long)BW * BH; ++i) {
        if (big[i] != 0) {
            ++wide;
        }
    }
    CHECK(wide > 3439);

    /* Back to a panel-sized canvas: the cache rebuilds rather than replaying
     * points from the larger one, which would land outside this canvas. */
    gfx_color_t *small = calloc((size_t)W * H, sizeof(gfx_color_t));
    CHECK(small != NULL);
    if (small == NULL) {
        free(big);
        return;
    }
    gfx_canvas_t sc;
    gfx_canvas_init(&sc, small, W, H, 0);
    ui_watermark(&sc);
    int narrow = 0;
    for (long i = 0; i < (long)W * H; ++i) {
        if (small[i] != 0) {
            ++narrow;
        }
    }
    CHECK(narrow > 0);
    CHECK(narrow < wide);

    /* And drawing it again is still idempotent through the cache. */
    gfx_color_t *once = malloc((size_t)W * H * sizeof(gfx_color_t));
    if (once != NULL) {
        memcpy(once, small, (size_t)W * H * sizeof(gfx_color_t));
        ui_watermark(&sc);
        CHECK_EQ(memcmp(once, small,
                        (size_t)W * H * sizeof(gfx_color_t)), 0);
        free(once);
    }
    free(small);
    free(big);
}

/*
 * Screens cache their chrome per framebuffer.  Switching the mark changes
 * pixels they believe they have already drawn correctly, so the router has to
 * invalidate them; otherwise the mark appears on one buffer and not the
 * other, which on a panel that alternates between two is flicker.
 *
 * Both buffers are painted before the flag flips.  Flipping first draws each
 * buffer for the first time anyway, and the case then passes without the
 * invalidation.
 */
TEST_CASE(switching_the_mark_invalidates_the_cached_chrome)
{
    fresh();
    to_overview();
    ui_bench_status_t st = *ui_router_status();

    st.simulated = false;
    ui_router_set_status(&st);
    ui_router_render(&cv, 0);
    ui_router_render(&cv, 1);   /* both caches warm and unmarked */

    st.simulated = true;
    ui_router_set_status(&st);

    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);
    gfx_color_t *first = malloc((size_t)W * H * sizeof(gfx_color_t));
    memcpy(first, fb, (size_t)W * H * sizeof(gfx_color_t));

    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 1);

    /* Both buffers must be the marked screen, not one of each. */
    CHECK_EQ(memcmp(first, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* And they must actually be drawn, not left as the memset. */
    int lit = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] != 0) { ++lit; }
    }
    CHECK(lit > 100000);
    free(first);
}

/*
 * The stub copy is drawn with gfx_text, which clips at the canvas edge rather
 * than wrapping.  A line that is too long is cut mid-word, so every line is
 * held to the width.
 */
TEST_CASE(no_line_of_stub_copy_runs_off_the_screen)
{
    fresh();
    for (int id = SCREEN_MOTOR; id < SCREEN_COUNT; ++id) {
        const char *const *lines = stub_copy_lines((ui_screen_id_t)id);
        if (lines == NULL) {
            continue;
        }
        for (int i = 0; i < 4 && lines[i] != NULL; ++i) {
            const int w = gfx_text_width(&gfx_font_8x16, lines[i], 1);
            if (w > STUB_COPY_MAX_W) {
                T_FAIL("screen %d line %d is %d px, over %d",
                       id, i, w, STUB_COPY_MAX_W);
            }
        }
        const char *blocker = stub_copy_blocker((ui_screen_id_t)id);
        if (blocker != NULL) {
            const int w = gfx_text_width(&gfx_font_8x16, blocker, 1);
            if (w > STUB_COPY_MAX_W) {
                T_FAIL("screen %d blocker is %d px, over %d",
                       id, w, STUB_COPY_MAX_W);
            }
        }
    }
}


/*
 * The menu says what the bench can do, not what it has screens for.  The
 * marks derive from the capability bits the coprocessor reports, not from a
 * preference: a capability corrects itself when the part is fitted, and until
 * then the screen says its numbers are modelled.
 */
TEST_CASE(the_menu_marks_what_is_not_fitted)
{
    fresh();
    to_overview();

    ui_bench_status_t st = *ui_router_status();
    st.capabilities = 0;                 /* nothing soldered on */
    ui_router_set_status(&st);
    ui_router_render(&cv, 0);
    int bare = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] == ui_theme_color(UI_C_PANEL_SUNK)) { ++bare; }
    }

    /* Everything fitted: the badges that said MODELLED go away, so there is
     * strictly less of the badge colour on the screen. */
    st.capabilities = 0xFFFFu;
    ui_router_set_status(&st);
    ui_router_render(&cv, 0);
    int full = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] == ui_theme_color(UI_C_PANEL_SUNK)) { ++full; }
    }

    if (full >= bare) {
        T_FAIL("fitting the hardware did not remove any badges: %d then %d",
               bare, full);
    }
    /* But the two that have no screen still say so. */
    CHECK(full > 0);
}

int main(void)
{
    RUN(the_router_starts_on_the_splash);
    RUN(the_splash_has_no_band_and_everything_else_does);
    RUN(no_screen_can_draw_over_the_band);
    RUN(stop_latches_and_clears_when_read);
    RUN(stop_works_from_every_screen_that_has_a_band);
    RUN(a_press_that_slides_off_stop_does_nothing);
    RUN(the_home_tag_returns_to_the_overview);
    RUN(the_overview_has_no_home_tag);
    RUN(a_second_contact_cannot_steal_the_release);
    RUN(the_setup_screen_opens_both_views_of_the_outputs);
    RUN(a_tile_navigates_and_a_slip_does_not);
    RUN(every_stub_renders_something_and_says_something);
    RUN(no_line_of_stub_copy_runs_off_the_screen);
    RUN(an_alert_survives_navigation);
    RUN(the_band_shows_what_is_wrong);
    RUN(the_splash_holds_then_hands_over);
    RUN(a_tap_skips_the_splash_hold);
    RUN(a_failed_step_is_drawn_in_its_own_colour);
    RUN(the_alert_band_renders_at_the_bottom);
    RUN(the_simulation_mark_covers_the_whole_screen);
    RUN(the_simulation_mark_lets_the_screen_through);
    RUN(invalidating_the_router_repaints_every_screen);
    RUN(the_simulation_mark_follows_the_canvas_it_is_given);
    RUN(switching_the_mark_invalidates_the_cached_chrome);
    RUN(the_menu_marks_what_is_not_fitted);
    return test_summary("nav");
}
