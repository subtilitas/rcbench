/**
 * @file test_nav_mark.c
 * @brief The shell's simulation mark and the cached chrome: what it covers, what
 * it lets through, and what repaints when it switches.
 *
 * Cases:
 * - the_simulation_mark_covers_the_whole_screen
 * - the_simulation_mark_lets_the_screen_through
 * - invalidating_the_router_repaints_every_screen
 * - the_simulation_mark_follows_the_canvas_it_is_given
 * - switching_the_mark_invalidates_the_cached_chrome
 * - the_menu_marks_what_is_not_fitted
 *
 * SPDX-License-Identifier: MIT
 */

#include "nav_rig.h"

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
    REQUIRE_ALLOC(plain);
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
    REQUIRE_ALLOC(plain);
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
    REQUIRE_ALLOC(first);
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
 * The menu says what the bench can do, not what it has screens for.  The
 * marks derive from the capability bits the IOmcu reports, not from a
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

    /* SUPPLY is marked whatever the IOmcu reports -- until SETUP
     * enables the PD mini, and again once it is disabled. */
    overview_screen_set_supply_real(true);
    ui_router_render(&cv, 0);
    int real = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] == ui_theme_color(UI_C_PANEL_SUNK)) { ++real; }
    }
    if (real >= full) {
        T_FAIL("the PD mini enabled left SUPPLY marked: %d then %d",
               full, real);
    }
    overview_screen_set_supply_real(false);
    ui_router_render(&cv, 0);
    int again = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] == ui_theme_color(UI_C_PANEL_SUNK)) { ++again; }
    }
    CHECK_EQ(again, full);
}

int main(void)
{
    RUN(the_simulation_mark_covers_the_whole_screen);
    RUN(the_simulation_mark_lets_the_screen_through);
    RUN(invalidating_the_router_repaints_every_screen);
    RUN(the_simulation_mark_follows_the_canvas_it_is_given);
    RUN(switching_the_mark_invalidates_the_cached_chrome);
    RUN(the_menu_marks_what_is_not_fitted);
    return test_summary("nav_mark");
}
