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
 * - demo_mode_is_off_by_default_and_the_mark_is_drawn
 * - demo_mode_hides_the_mark_and_keeps_the_sim_chip
 * - demo_mode_switched_back_off_draws_the_mark_on_the_next_frame
 * - a_stored_demo_mode_loads
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

/* ---------------------------------------------------------------- demo mode */

#define BUF_PX ((size_t)W * H)

static float s_demo_stored[SETTING_COUNT];
static bool  s_demo_has_stored;

/** @brief The memory store's load. */
static bool demo_load(float *values, int count)
{
    if (!s_demo_has_stored) {
        return false;
    }
    memcpy(values, s_demo_stored, (size_t)count * sizeof(float));
    return true;
}

/** @brief The memory store's save. */
static bool demo_save(const float *values, int count)
{
    memcpy(s_demo_stored, values, (size_t)count * sizeof(float));
    s_demo_has_stored = true;
    return true;
}

static const settings_store_t k_demo_store = { demo_load, demo_save, NULL, NULL };

/**
 * @brief A simulated bench on the overview with the band's SIM chip, settings
 *        at their defaults, and two framebuffers of its own, as the panel
 *        alternates between two.
 * @param bufs Takes the two framebuffers; the caller frees them.
 * @param cvs  Takes their canvases.
 * @return Whether the buffers were allocated.
 */
static bool demo_rig(gfx_color_t *bufs[2], gfx_canvas_t cvs[2])
{
    settings_set_store(NULL);
    settings_init();
    fresh();
    to_overview();
    ui_bench_status_t st = *ui_router_status();
    st.simulated = true;
    st.mode      = "SIM";
    ui_router_set_status(&st);
    for (int i = 0; i < 2; ++i) {
        bufs[i] = calloc(BUF_PX, sizeof(gfx_color_t));
        if (bufs[i] == NULL) {
            T_FAIL("out of memory");
            return false;
        }
        gfx_canvas_init(&cvs[i], bufs[i], W, H, W);
    }
    return true;
}

/** @brief Draws both framebuffers, buffer 0 then 1, without clearing them. */
static void demo_frame(gfx_canvas_t cvs[2])
{
    ui_router_render(&cvs[0], 0);
    ui_router_render(&cvs[1], 1);
}

/** @brief Pixels that differ between two framebuffers. */
static long demo_diff(const gfx_color_t *a, const gfx_color_t *b)
{
    long n = 0;
    for (size_t i = 0; i < BUF_PX; ++i) {
        n += (a[i] != b[i]);
    }
    return n;
}

/** @brief Pixels of the band's mode chip that differ between two buffers. */
static long demo_chip_diff(const gfx_color_t *a, const gfx_color_t *b,
                           bool *has_ink)
{
    gfx_rect_t r;
    long n = 0;
    *has_ink = false;
    if (!ui_band_chip_rect(ui_router_status(), UI_BAND_CHIP_MODE, &r)) {
        T_FAIL("the SIM chip has no rectangle");
        return -1;
    }
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            const size_t at = (size_t)y * W + (size_t)x;
            n += (a[at] != b[at]);
            *has_ink = *has_ink || (a[at] != a[(size_t)r.y * W + (size_t)r.x]);
        }
    }
    return n;
}

TEST_CASE(demo_mode_is_off_by_default_and_the_mark_is_drawn)
{
    gfx_color_t *bufs[2] = { NULL, NULL };
    gfx_canvas_t cvs[2];
    if (!demo_rig(bufs, cvs)) {
        free(bufs[0]);
        return;
    }
    const setting_def_t *d = settings_def(SET_DEMO_MODE);
    CHECK(d != NULL);
    CHECK_STR_EQ(d->key, "demo_mode");
    CHECK_EQ(d->type, SET_TYPE_BOOL);
    CHECK_EQ(d->cat, SET_CAT_APP);
    CHECK(!settings_get_bool(SET_DEMO_MODE));
    CHECK_STR_EQ(settings_label(SET_DEMO_MODE), "Demo mode");

    gfx_color_t *blank = calloc(BUF_PX, sizeof(gfx_color_t));
    REQUIRE_ALLOC(blank);
    gfx_canvas_t bc;
    gfx_canvas_init(&bc, blank, W, H, W);
    ui_watermark(&bc);
    long lit = 0;
    for (size_t i = 0; i < BUF_PX; ++i) {
        lit += (blank[i] != 0);
    }
    CHECK(lit > 3000);              /* the mark alone: thousands of pixels */

    demo_frame(cvs);
    CHECK_EQ(demo_diff(bufs[0], bufs[1]), 0);
    free(blank);
    free(bufs[0]);
    free(bufs[1]);
}

TEST_CASE(demo_mode_hides_the_mark_and_keeps_the_sim_chip)
{
    gfx_color_t *bufs[2] = { NULL, NULL };
    gfx_canvas_t cvs[2];
    if (!demo_rig(bufs, cvs)) {
        free(bufs[0]);
        return;
    }
    demo_frame(cvs);
    gfx_color_t *marked = malloc(BUF_PX * sizeof(gfx_color_t));
    REQUIRE_ALLOC(marked);
    memcpy(marked, bufs[0], BUF_PX * sizeof(gfx_color_t));

    settings_set(SET_DEMO_MODE, 1.0f);
    demo_frame(cvs);

    /* The mark alone, on a blank canvas: every pixel it owns is gone. */
    gfx_color_t *blank = calloc(BUF_PX, sizeof(gfx_color_t));
    REQUIRE_ALLOC(blank);
    gfx_canvas_t bc;
    gfx_canvas_init(&bc, blank, W, H, W);
    ui_watermark(&bc);
    long still = 0;
    for (size_t i = 0; i < BUF_PX; ++i) {
        still += (blank[i] != 0);
    }
    CHECK_EQ(still, 0);                 /* ui_watermark() draws nothing */

    /* Both buffers changed in the mark's pixels and are the same screen as
     * a bench that is not simulated draws. */
    CHECK(demo_diff(marked, bufs[0]) > 3000);
    CHECK_EQ(demo_diff(bufs[0], bufs[1]), 0);
    gfx_color_t *plain = calloc(BUF_PX, sizeof(gfx_color_t));
    REQUIRE_ALLOC(plain);
    gfx_canvas_t pc;
    gfx_canvas_init(&pc, plain, W, H, W);
    ui_bench_status_t real = *ui_router_status();
    real.simulated = false;
    ui_router_set_status(&real);
    ui_router_render(&pc, 0);
    real.simulated = true;
    ui_router_set_status(&real);
    CHECK_EQ(demo_diff(plain, bufs[0]), 0);
    free(plain);

    /* The band still says SIM: the chip is drawn, with ink in it. */
    bool ink = false;
    (void)demo_chip_diff(bufs[0], bufs[1], &ink);
    CHECK(ink);
    const ui_bench_status_t *st = ui_router_status();
    CHECK(st->simulated);
    CHECK_STR_EQ(st->mode, "SIM");

    free(blank);
    free(marked);
    free(bufs[0]);
    free(bufs[1]);
    settings_set(SET_DEMO_MODE, 0.0f);
}

TEST_CASE(demo_mode_switched_back_off_draws_the_mark_on_the_next_frame)
{
    gfx_color_t *bufs[2] = { NULL, NULL };
    gfx_canvas_t cvs[2];
    if (!demo_rig(bufs, cvs)) {
        free(bufs[0]);
        return;
    }
    demo_frame(cvs);
    gfx_color_t *marked = malloc(BUF_PX * sizeof(gfx_color_t));
    REQUIRE_ALLOC(marked);
    memcpy(marked, bufs[0], BUF_PX * sizeof(gfx_color_t));

    settings_set(SET_DEMO_MODE, 1.0f);
    demo_frame(cvs);
    gfx_color_t *hidden = malloc(BUF_PX * sizeof(gfx_color_t));
    REQUIRE_ALLOC(hidden);
    memcpy(hidden, bufs[0], BUF_PX * sizeof(gfx_color_t));

    /* The buffers were never cleared: a pixel of the mark left behind would
     * differ from the same screen on a bench that is not simulated, drawn
     * from nothing. */
    gfx_color_t *scratch = calloc(BUF_PX, sizeof(gfx_color_t));
    REQUIRE_ALLOC(scratch);
    gfx_canvas_t sc;
    gfx_canvas_init(&sc, scratch, W, H, W);
    ui_bench_status_t real = *ui_router_status();
    real.simulated = false;
    ui_router_set_status(&real);
    ui_router_render(&sc, 0);
    real.simulated = true;
    ui_router_set_status(&real);
    CHECK_EQ(demo_diff(hidden, scratch), 0);

    settings_set(SET_DEMO_MODE, 0.0f);
    demo_frame(cvs);
    CHECK_EQ(demo_diff(marked, bufs[0]), 0);
    CHECK_EQ(demo_diff(marked, bufs[1]), 0);

    /* And ON again, with the mark's cached stencil already built. */
    settings_set(SET_DEMO_MODE, 1.0f);
    demo_frame(cvs);
    CHECK_EQ(demo_diff(hidden, bufs[0]), 0);
    CHECK_EQ(demo_diff(hidden, bufs[1]), 0);

    settings_set(SET_DEMO_MODE, 0.0f);
    free(scratch);
    free(hidden);
    free(marked);
    free(bufs[0]);
    free(bufs[1]);
}

TEST_CASE(a_stored_demo_mode_loads)
{
    memset(s_demo_stored, 0, sizeof(s_demo_stored));
    s_demo_stored[SET_DEMO_MODE] = 1.0f;
    s_demo_has_stored = true;
    settings_set_store(&k_demo_store);
    settings_init();
    CHECK(settings_get_bool(SET_DEMO_MODE));

    /* The first frame of a start with it stored ON carries no mark. */
    fresh();
    to_overview();
    ui_bench_status_t st = *ui_router_status();
    st.simulated = true;
    ui_router_set_status(&st);
    ui_router_render(&cv, 0);
    gfx_color_t *loaded = malloc(BUF_PX * sizeof(gfx_color_t));
    REQUIRE_ALLOC(loaded);
    memcpy(loaded, fb, BUF_PX * sizeof(gfx_color_t));
    st.simulated = false;
    ui_router_set_status(&st);
    ui_router_render(&cv, 0);
    CHECK_EQ(demo_diff(loaded, fb), 0);
    free(loaded);

    /* A save writes it with the other PANEL rows and a load reads it back. */
    settings_set(SET_DEMO_MODE, 0.0f);
    CHECK(settings_save());
    CHECK_EQ(s_demo_stored[SET_DEMO_MODE], 0.0f);
    settings_set(SET_DEMO_MODE, 1.0f);
    CHECK(settings_save());
    CHECK_EQ(s_demo_stored[SET_DEMO_MODE], 1.0f);
    settings_set_store(NULL);
    settings_init();
    CHECK(!settings_get_bool(SET_DEMO_MODE));

    /* RESET CATEGORY on PANEL gives OFF. */
    settings_set(SET_DEMO_MODE, 1.0f);
    settings_reset(SET_CAT_APP);
    CHECK(!settings_get_bool(SET_DEMO_MODE));
}

int main(void)
{
    RUN(the_simulation_mark_covers_the_whole_screen);
    RUN(the_simulation_mark_lets_the_screen_through);
    RUN(invalidating_the_router_repaints_every_screen);
    RUN(the_simulation_mark_follows_the_canvas_it_is_given);
    RUN(switching_the_mark_invalidates_the_cached_chrome);
    RUN(the_menu_marks_what_is_not_fitted);
    RUN(demo_mode_is_off_by_default_and_the_mark_is_drawn);
    RUN(demo_mode_hides_the_mark_and_keeps_the_sim_chip);
    RUN(demo_mode_switched_back_off_draws_the_mark_on_the_next_frame);
    RUN(a_stored_demo_mode_loads);
    return test_summary("nav_mark");
}
