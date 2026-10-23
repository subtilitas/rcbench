/*
 * Screen rotation 180° in the flush (gfx_turn.h), with a fake copy engine
 * in place of the ESP32-P4's PPA (pixel-processing accelerator).
 *
 * Held here:
 * - a switch to 180° draws into the logical buffers from the next frame,
 *   asks the UI for one whole repaint and copies the whole picture for two
 *   flips, then only what each frame drew;
 * - the shown flag, which the touch map turns by, changes when a frame is
 *   presented and never in the flush before it;
 * - a copy that never completes takes the flush's 50ms and no more; the
 *   frame then goes out as drawn, unturned, the shown flag NORMAL with it,
 *   the alert due once, and 180° stays off until the next start, the
 *   engine not called again;
 * - a slow engine that spends the 50ms over several copies, and one that
 *   refuses a block, fall back the same way;
 * - no engine at the start and 180° asked for: NORMAL, the alert once;
 * - a fake PPA shaped like the driver, which ends a job by calling its
 *   done callback with the job's user_data: the callback receives the
 *   display's state and wakes the wait; an engine that hands it NULL, as
 *   a job built without its user_data does, is caught, counted, and the
 *   flush falls back instead of the callback dereferencing NULL;
 * - the screens drawn through the panel's loop, switched to 180° and back
 *   while they run: every turned frame buffer is its logical picture
 *   turned pixel for pixel, and every logical picture is the picture of a
 *   run that never turned, in both buffers; a run whose engine stops part
 *   way falls back and still shows that picture.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "gfx.h"
#include "gfx_turn.h"
#include "link_pages.h"
#include "motor_screen.h"
#include "servo_screen.h"
#include "settings.h"
#include "supply.h"
#include "supply_screen.h"
#include "telemetry_sim.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_theme.h"

#include "servo_spots.h"

#define W UI_PANEL_W
#define H UI_PANEL_H
/** @brief A small picture for the state cases. */
#define SW 64
#define SH 48

/** @brief What the fake engine does with a copy. */
typedef enum {
    ENG_COPY,    /**< copies, turned, at cost_us a copy                    */
    ENG_NEVER,   /**< never completes: waits its whole budget              */
    ENG_REFUSE,  /**< refuses the block                                    */
} eng_mode_t;

/** @brief The fake engine and its clock. */
typedef struct {
    eng_mode_t mode;      /**< what a copy does                            */
    uint32_t now;         /**< the clock, us                               */
    uint32_t cost_us;     /**< a copy's time when it copies                */
    int calls;            /**< copies asked for                            */
    uint32_t budgets[16]; /**< the budgets the first 16 were given         */
    int width;            /**< the picture's width                         */
    int height;           /**< its height                                  */
} engine_t;

/** @brief The fake engine's copy: the CPU (central processing unit) turns
 *         the block, as the PPA would. */
static gfx_turn_copy_t eng_copy(void *ctx, const gfx_color_t *src,
                                gfx_color_t *dst, gfx_rect_t r,
                                uint32_t budget_us)
{
    engine_t *e = ctx;
    if (e->calls < 16) {
        e->budgets[e->calls] = budget_us;
    }
    ++e->calls;
    if (e->mode == ENG_NEVER) {
        e->now += budget_us;
        return GFX_TURN_COPY_TIMEOUT;
    }
    if (e->mode == ENG_REFUSE) {
        return GFX_TURN_COPY_ERROR;
    }
    if (e->cost_us > budget_us) {
        e->now += budget_us;
        return GFX_TURN_COPY_TIMEOUT;
    }
    e->now += e->cost_us;
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            dst[(size_t)(e->height - 1 - y) * (size_t)e->width
                + (size_t)(e->width - 1 - x)] =
                src[(size_t)y * (size_t)e->width + (size_t)x];
        }
    }
    return GFX_TURN_COPY_DONE;
}

/** @brief The fake engine's clock. */
static uint32_t eng_now(void *ctx)
{
    return ((engine_t *)ctx)->now;
}

/** @brief A fake engine for a @p w x @p h picture, copying in 1ms. */
static engine_t engine(int w, int h)
{
    engine_t e = { .mode = ENG_COPY, .cost_us = 1000u, .width = w,
                   .height = h };
    return e;
}

/** @brief The ops over @p e. */
static gfx_turn_ops_t ops_of(engine_t *e)
{
    const gfx_turn_ops_t o = { eng_copy, eng_now, e };
    return o;
}

/** @brief Whether @p fb is @p logical turned by 180°, every pixel. */
static bool is_turned(const gfx_color_t *logical, const gfx_color_t *fb,
                      int w, int h)
{
    const size_t n = (size_t)w * (size_t)h;
    for (size_t i = 0; i < n; ++i) {
        if (fb[n - 1u - i] != logical[i]) {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------ the state */

static gfx_color_t s_lg[2][SW * SH];
static gfx_color_t s_fb[2][SW * SH];

/** @brief Draw something new into @p c: a box at a place frame @p i
 *         picks. */
static void scribble(gfx_canvas_t *c, int i)
{
    gfx_fill_rect(c, (i * 7) % (SW - 8), (i * 5) % (SH - 6), 8, 6,
                  (gfx_color_t)(0x1111u * (unsigned)(i + 1)));
}

/**
 * @brief One frame the panel's way on the small picture: draw, flush,
 *        present, flip, take the request.
 * @param t the state
 * @param e the engine
 * @param b the buffer's index
 * @param i the frame's number
 * @param want the setting
 * @return what the flush returned
 */
static gfx_turn_show_t frame(gfx_turn_t *t, engine_t *e, int b, int i,
                             bool want)
{
    gfx_canvas_t c;
    const bool turned = gfx_turn_drawing(t);
    gfx_canvas_init(&c, turned ? s_lg[b] : s_fb[b], SW, SH, SW);
    if (gfx_turn_take_repaint(t)) {
        gfx_fill_rect(&c, 0, 0, SW, SH, 0x0841u);
    }
    gfx_turn_begin(t, &c);
    scribble(&c, i);
    const gfx_turn_ops_t o = ops_of(e);
    const gfx_turn_show_t show = gfx_turn_flush(t, s_lg[b], s_fb[b], &o);
    (void)gfx_turn_presented(t, show);
    gfx_turn_request(t, want);
    gfx_turn_next(t);
    return show;
}

TEST_CASE(a_fresh_state_is_normal)
{
    gfx_turn_t t;
    gfx_turn_init(&t, SW, SH);
    CHECK(!gfx_turn_drawing(&t));
    CHECK(!gfx_turn_shown());
    CHECK(!gfx_turn_take_repaint(&t));
    CHECK(!gfx_turn_take_alert(&t));
    engine_t e = engine(SW, SH);
    CHECK_EQ(frame(&t, &e, 0, 0, false), GFX_TURN_SHOW_NORMAL);
    CHECK_EQ(e.calls, 0);
}

TEST_CASE(a_switch_copies_the_whole_picture_twice_then_what_changed)
{
    gfx_turn_t t;
    gfx_turn_init(&t, SW, SH);
    engine_t e = engine(SW, SH);
    (void)frame(&t, &e, 0, 0, true);    /* drawn NORMAL; 180° taken after */
    CHECK(gfx_turn_drawing(&t));
    CHECK(!gfx_turn_shown());
    CHECK(t.repaint);
    for (int i = 1; i <= 6; ++i) {
        const int b = i & 1;
        CHECK_EQ(frame(&t, &e, b, i, true), GFX_TURN_SHOW_TURNED);
        CHECK(is_turned(s_lg[b], s_fb[b], SW, SH));
        CHECK(gfx_turn_shown());
        if (i <= (int)GFX_TURN_FULL_FLIPS) {
            CHECK_EQ(t.pixels, (uint32_t)(SW * SH));
        } else {
            /* One 8 x 6 box in one or two bands: a band's rows at most. */
            CHECK(t.rects >= 1u && t.rects <= 2u);
            CHECK(t.pixels <= 2u * 16u * 8u);
        }
    }
    CHECK(!gfx_turn_take_repaint(&t));   /* frame() took the one there was */
}

TEST_CASE(the_shown_flag_changes_on_presenting_not_in_the_flush)
{
    gfx_turn_t t;
    gfx_turn_init(&t, SW, SH);
    engine_t e = engine(SW, SH);
    gfx_turn_request(&t, true);
    gfx_turn_next(&t);
    gfx_canvas_t c;
    gfx_canvas_init(&c, s_lg[1], SW, SH, SW);
    gfx_turn_begin(&t, &c);
    scribble(&c, 3);
    const gfx_turn_ops_t o = ops_of(&e);
    const gfx_turn_show_t show = gfx_turn_flush(&t, s_lg[1], s_fb[1], &o);
    CHECK_EQ(show, GFX_TURN_SHOW_TURNED);
    CHECK(!gfx_turn_shown());           /* not on the glass yet */
    CHECK(gfx_turn_presented(&t, show));
    CHECK(gfx_turn_shown());
    CHECK(!gfx_turn_presented(&t, show));   /* no change the second time */
    /* And back: drawn NORMAL from the next frame, the flag at its present. */
    gfx_turn_request(&t, false);
    gfx_turn_next(&t);
    CHECK(!gfx_turn_drawing(&t));
    CHECK(gfx_turn_take_repaint(&t));
    CHECK(gfx_turn_shown());
    CHECK(gfx_turn_presented(&t, GFX_TURN_SHOW_NORMAL));
    CHECK(!gfx_turn_shown());
}

/** @brief Turn @p t on and run frames until the whole-picture copies are
 *         done. */
static void turned_and_settled(gfx_turn_t *t, engine_t *e)
{
    gfx_turn_init(t, SW, SH);
    for (int i = 0; i < 4; ++i) {
        (void)frame(t, e, i & 1, i, true);
    }
    (void)gfx_turn_take_repaint(t);
    CHECK(gfx_turn_shown());
}

TEST_CASE(a_copy_that_never_completes_falls_back_within_the_budget)
{
    gfx_turn_t t;
    engine_t e = engine(SW, SH);
    turned_and_settled(&t, &e);
    e.mode = ENG_NEVER;
    e.calls = 0;
    const uint32_t start = e.now;
    gfx_canvas_t c;
    gfx_canvas_init(&c, s_lg[0], SW, SH, SW);
    gfx_turn_begin(&t, &c);
    scribble(&c, 11);
    const gfx_turn_ops_t o = ops_of(&e);
    const gfx_turn_show_t show = gfx_turn_flush(&t, s_lg[0], s_fb[0], &o);
    CHECK_EQ(show, GFX_TURN_SHOW_FALLBACK);
    CHECK_EQ(e.calls, 1);
    CHECK_EQ(e.now - start, GFX_TURN_BUDGET_US);
    CHECK_EQ(t.why, GFX_TURN_FAIL_TIMEOUT);
    /* The frame as drawn, unturned. */
    CHECK(memcmp(s_lg[0], s_fb[0], sizeof(s_lg[0])) == 0);
    /* Picture and touch NORMAL together, at the frame's present. */
    CHECK(gfx_turn_shown());
    CHECK(gfx_turn_presented(&t, show));
    CHECK(!gfx_turn_shown());
    CHECK(gfx_turn_take_repaint(&t));
    gfx_turn_request(&t, true);
    gfx_turn_next(&t);
    CHECK(gfx_turn_take_alert(&t));
    CHECK(!gfx_turn_take_alert(&t));
    /* 180° stays off; the engine is not asked again. */
    e.calls = 0;
    for (int i = 0; i < 6; ++i) {
        CHECK_EQ(frame(&t, &e, i & 1, 20 + i, (i & 2) != 0),
                 GFX_TURN_SHOW_NORMAL);
    }
    CHECK_EQ(e.calls, 0);
    CHECK(!gfx_turn_shown());
    CHECK(!gfx_turn_take_alert(&t));
    CHECK(t.failed);
}

TEST_CASE(a_slow_engine_spends_the_budget_over_several_copies)
{
    gfx_turn_t t;
    engine_t e = engine(SW, SH);
    turned_and_settled(&t, &e);
    e.cost_us = 20000u;
    e.calls = 0;
    gfx_canvas_t c;
    gfx_canvas_init(&c, s_lg[0], SW, SH, SW);
    gfx_turn_begin(&t, &c);
    /* Three boxes in three bands far apart: three copies. */
    gfx_fill_rect(&c, 0, 0, 4, 4, 0xF800u);
    gfx_fill_rect(&c, 60, 20, 4, 4, 0x07E0u);
    gfx_fill_rect(&c, 0, 40, 4, 4, 0x001Fu);
    const gfx_turn_ops_t o = ops_of(&e);
    CHECK_EQ(gfx_turn_flush(&t, s_lg[0], s_fb[0], &o),
             GFX_TURN_SHOW_FALLBACK);
    CHECK_EQ(e.calls, 3);
    CHECK_EQ(e.budgets[0], GFX_TURN_BUDGET_US);
    CHECK_EQ(e.budgets[1], GFX_TURN_BUDGET_US - 20000u);
    CHECK_EQ(e.budgets[2], GFX_TURN_BUDGET_US - 40000u);
    CHECK(t.copy_us <= GFX_TURN_BUDGET_US);
    CHECK_EQ(t.why, GFX_TURN_FAIL_TIMEOUT);
    CHECK(memcmp(s_lg[0], s_fb[0], sizeof(s_lg[0])) == 0);
}

TEST_CASE(a_refused_block_falls_back)
{
    gfx_turn_t t;
    engine_t e = engine(SW, SH);
    turned_and_settled(&t, &e);
    e.mode = ENG_REFUSE;
    CHECK_EQ(frame(&t, &e, 0, 30, true), GFX_TURN_SHOW_FALLBACK);
    CHECK_EQ(t.why, GFX_TURN_FAIL_ERROR);
    CHECK(!gfx_turn_shown());
    CHECK(memcmp(s_lg[0], s_fb[0], sizeof(s_lg[0])) == 0);
    CHECK(gfx_turn_take_alert(&t));
}

TEST_CASE(no_engine_at_the_start_stays_normal_with_one_alert)
{
    gfx_turn_t t;
    gfx_turn_init(&t, SW, SH);
    gfx_turn_fail(&t, GFX_TURN_FAIL_ENGINE);
    engine_t e = engine(SW, SH);
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(frame(&t, &e, i & 1, i, true), GFX_TURN_SHOW_NORMAL);
    }
    CHECK_EQ(e.calls, 0);
    CHECK(!gfx_turn_drawing(&t));
    CHECK(!gfx_turn_shown());
    CHECK(gfx_turn_take_alert(&t));
    CHECK(!gfx_turn_take_alert(&t));
    CHECK_EQ(t.why, GFX_TURN_FAIL_ENGINE);
    /* No alert when NORMAL is what is asked for. */
    gfx_turn_init(&t, SW, SH);
    gfx_turn_fail(&t, GFX_TURN_FAIL_ENGINE);
    (void)frame(&t, &e, 0, 0, false);
    CHECK(!gfx_turn_take_alert(&t));
}

TEST_CASE(a_flush_without_ops_presents_normal_and_falls_back)
{
    gfx_turn_t t;
    engine_t e = engine(SW, SH);
    turned_and_settled(&t, &e);
    CHECK_EQ(gfx_turn_flush(&t, s_lg[0], s_fb[0], NULL),
             GFX_TURN_SHOW_NORMAL);
    CHECK(t.failed);
    CHECK_EQ(t.why, GFX_TURN_FAIL_ENGINE);
    CHECK(gfx_damage_sink == NULL);
}

/* Every call takes a NULL state and does nothing with it. */
TEST_CASE(a_null_state_does_nothing)
{
    gfx_turn_init(NULL, SW, SH);
    gfx_turn_request(NULL, true);
    gfx_turn_fail(NULL, GFX_TURN_FAIL_ERROR);
    gfx_turn_begin(NULL, NULL);
    gfx_turn_next(NULL);
    CHECK(!gfx_turn_drawing(NULL));
    CHECK(!gfx_turn_take_repaint(NULL));
    CHECK(!gfx_turn_take_alert(NULL));
    CHECK_EQ(gfx_turn_flush(NULL, s_lg[0], s_fb[0], NULL),
             GFX_TURN_SHOW_NORMAL);
    CHECK(gfx_damage_sink == NULL);
    /* GFX_TURN_FAIL_NONE is no failure. */
    gfx_turn_t t;
    gfx_turn_init(&t, SW, SH);
    gfx_turn_fail(&t, GFX_TURN_FAIL_NONE);
    CHECK(!t.failed);
}

/* ------------------------------------------- the engine's done callback */

/** @brief The display's state as the done callback needs it: a fake
 *         semaphore. */
typedef struct {
    int given;               /**< tokens the done callback gave           */
    int taken;               /**< tokens the wait took                    */
} fake_disp_t;

/** @brief How the fake driver ends a job. */
typedef enum {
    DRV_DONE,        /**< done callback with the job's user_data          */
    DRV_DROPS_CTX,   /**< done callback with NULL, as a job whose
                          user_data was left unset                        */
    DRV_NEVER,       /**< no done callback                                */
    DRV_REFUSE,      /**< the submit fails                                */
} drv_mode_t;

/** @brief A fake PPA driver: ends a job in its "interrupt", the done
 *         callback. */
typedef struct {
    drv_mode_t mode;            /**< how it ends a job                    */
    int submits;                /**< jobs submitted                       */
    void *last_ctx;             /**< what the done callback got last      */
    fake_disp_t *disp;          /**< the state the wait reads             */
} fake_drv_t;

/**
 * @brief The done callback, as the panel's on_ppa_done(): the context
 *        first, then the semaphore given.
 * @param user_data what the driver passed
 * @return true when it woke the wait
 */
static bool fake_on_done(void *user_data)
{
    fake_disp_t *st = gfx_turn_done_ctx(user_data);
    if (st == NULL) {
        return false;
    }
    ++st->given;
    return true;
}

/** @brief The fake driver's submit: copies the block turned, then ends it
 *         as its mode says. */
static bool fake_submit(void *engine, const gfx_turn_job_t *job)
{
    fake_drv_t *d = engine;
    ++d->submits;
    if (d->mode == DRV_REFUSE) {
        return false;
    }
    for (int y = 0; y < job->src.h; ++y) {
        for (int x = 0; x < job->src.w; ++x) {
            job->out[(size_t)(job->dst_y + job->src.h - 1 - y) * job->width
                     + (size_t)(job->dst_x + job->src.w - 1 - x)] =
                job->in[(size_t)(job->src.y + y) * job->width
                        + (size_t)(job->src.x + x)];
        }
    }
    if (d->mode == DRV_DONE) {
        d->last_ctx = job->user_data;
        (void)fake_on_done(job->user_data);
    } else if (d->mode == DRV_DROPS_CTX) {
        d->last_ctx = NULL;
        (void)fake_on_done(NULL);
    }
    return true;
}

/** @brief The fake wait: a token the done callback gave. */
static bool fake_wait(void *engine, uint32_t budget_us)
{
    (void)budget_us;
    fake_drv_t *d = engine;
    if (d->disp->given > d->disp->taken) {
        ++d->disp->taken;
        return true;
    }
    return false;
}

/** @brief The engine over @p d, its done context @p disp. */
static gfx_turn_engine_t fake_engine(fake_drv_t *d, fake_disp_t *disp)
{
    d->disp = disp;
    const gfx_turn_engine_t e = { fake_submit, fake_wait, d, disp };
    return e;
}

/** @brief The engine a frame's copies go through. */
static gfx_turn_engine_t s_eng;

/** @brief gfx_turn_ops_t's copy over s_eng, as the panel's ppa_copy(). */
static gfx_turn_copy_t eng_job_copy(void *ctx, const gfx_color_t *src,
                                    gfx_color_t *dst, gfx_rect_t r,
                                    uint32_t budget_us)
{
    (void)ctx;
    return gfx_turn_engine_copy(&s_eng, src, dst, r, SW, SH, budget_us);
}

TEST_CASE(the_done_callback_receives_the_display_state)
{
    fake_drv_t d = { .mode = DRV_DONE };
    fake_disp_t disp = { 0, 0 };
    s_eng = fake_engine(&d, &disp);
    for (size_t i = 0; i < (size_t)SW * SH; ++i) {
        s_lg[0][i] = (gfx_color_t)(i * 7u);
    }
    const gfx_rect_t r = gfx_rect_make(3, 5, 10, 7);
    CHECK_EQ(gfx_turn_engine_copy(&s_eng, s_lg[0], s_fb[0], r, SW, SH,
                                  GFX_TURN_BUDGET_US),
             GFX_TURN_COPY_DONE);
    CHECK(d.last_ctx == &disp);
    CHECK_EQ(disp.given, 1);
    CHECK_EQ(disp.taken, 1);
    /* The block landed turned. */
    CHECK_EQ(s_fb[0][(size_t)(SH - 1 - 5) * SW + (size_t)(SW - 1 - 3)],
             s_lg[0][(size_t)5 * SW + 3u]);
    CHECK_EQ(s_fb[0][(size_t)(SH - 1 - 11) * SW + (size_t)(SW - 1 - 12)],
             s_lg[0][(size_t)11 * SW + 12u]);
}

TEST_CASE(a_done_callback_without_its_context_is_caught)
{
    gfx_turn_t t;
    engine_t clock = engine(SW, SH);
    turned_and_settled(&t, &clock);
    fake_drv_t d = { .mode = DRV_DROPS_CTX };
    fake_disp_t disp = { 0, 0 };
    s_eng = fake_engine(&d, &disp);
    const uint32_t stray = gfx_turn_stray_dones();
    const gfx_turn_ops_t o = { eng_job_copy, eng_now, &clock };
    gfx_canvas_t c;
    gfx_canvas_init(&c, s_lg[0], SW, SH, SW);
    gfx_turn_begin(&t, &c);
    scribble(&c, 41);
    /* The callback got NULL, counted it and gave nothing: the wait times
     * out and the frame goes out as drawn. */
    CHECK_EQ(gfx_turn_flush(&t, s_lg[0], s_fb[0], &o),
             GFX_TURN_SHOW_FALLBACK);
    CHECK_EQ(gfx_turn_stray_dones(), stray + 1u);
    CHECK_EQ(disp.given, 0);
    CHECK_EQ(t.why, GFX_TURN_FAIL_TIMEOUT);
    CHECK(memcmp(s_lg[0], s_fb[0], sizeof(s_lg[0])) == 0);
    (void)gfx_turn_presented(&t, GFX_TURN_SHOW_FALLBACK);
    CHECK(!gfx_turn_shown());
}

TEST_CASE(an_engine_without_a_done_context_takes_no_job)
{
    fake_drv_t d = { .mode = DRV_DONE };
    fake_disp_t disp = { 0, 0 };
    gfx_turn_engine_t e = fake_engine(&d, &disp);
    e.done_ctx = NULL;
    const gfx_rect_t r = gfx_rect_make(0, 0, 4, 4);
    CHECK_EQ(gfx_turn_engine_copy(&e, s_lg[0], s_fb[0], r, SW, SH, 1000u),
             GFX_TURN_COPY_ERROR);
    CHECK_EQ(d.submits, 0);
    CHECK_EQ(gfx_turn_engine_copy(NULL, s_lg[0], s_fb[0], r, SW, SH, 1000u),
             GFX_TURN_COPY_ERROR);
    e = fake_engine(&d, &disp);
    CHECK_EQ(gfx_turn_engine_copy(&e, NULL, s_fb[0], r, SW, SH, 1000u),
             GFX_TURN_COPY_ERROR);
    /* Refused and never ended. */
    fake_drv_t refuse = { .mode = DRV_REFUSE };
    const gfx_turn_engine_t er = fake_engine(&refuse, &disp);
    CHECK_EQ(gfx_turn_engine_copy(&er, s_lg[0], s_fb[0], r, SW, SH, 1000u),
             GFX_TURN_COPY_ERROR);
    fake_drv_t never = { .mode = DRV_NEVER };
    const gfx_turn_engine_t en = fake_engine(&never, &disp);
    CHECK_EQ(gfx_turn_engine_copy(&en, s_lg[0], s_fb[0], r, SW, SH, 1000u),
             GFX_TURN_COPY_TIMEOUT);
    CHECK(gfx_turn_done_ctx(&disp) == &disp);
}

/* Frames through the job layer and the fake driver: turned, every frame
 * buffer its picture turned. */
TEST_CASE(frames_through_the_driver_shape_turn)
{
    gfx_turn_t t;
    engine_t clock = engine(SW, SH);
    gfx_turn_init(&t, SW, SH);
    fake_drv_t d = { .mode = DRV_DONE };
    fake_disp_t disp = { 0, 0 };
    s_eng = fake_engine(&d, &disp);
    const gfx_turn_ops_t o = { eng_job_copy, eng_now, &clock };
    for (int i = 0; i < 8; ++i) {
        const int b = i & 1;
        gfx_canvas_t c;
        gfx_canvas_init(&c, gfx_turn_drawing(&t) ? s_lg[b] : s_fb[b], SW, SH,
                        SW);
        if (gfx_turn_take_repaint(&t)) {
            gfx_fill_rect(&c, 0, 0, SW, SH, 0x2104u);
        }
        gfx_turn_begin(&t, &c);
        scribble(&c, 60 + i);
        const gfx_turn_show_t show = gfx_turn_flush(&t, s_lg[b], s_fb[b], &o);
        if (i >= 1) {
            CHECK_EQ(show, GFX_TURN_SHOW_TURNED);
            CHECK(is_turned(s_lg[b], s_fb[b], SW, SH));
        }
        (void)gfx_turn_presented(&t, show);
        gfx_turn_request(&t, true);
        gfx_turn_next(&t);
    }
    CHECK_EQ(disp.given, disp.taken);
    CHECK(d.submits >= 7);
    CHECK(d.last_ctx == &disp);
}

/* ------------------------------------------------- the screens, turned */

static gfx_color_t *s_logical[2];
static gfx_color_t *s_frame[2];
/** @brief The frames measured. */
#define FRAMES 150
/** @brief A run's logical pictures, frame by frame: [0] never turned. */
static uint64_t s_pic[2][FRAMES];

/** @brief A picture as a 64-bit FNV-1a hash of its 64-bit words. */
static uint64_t picture_hash(const gfx_color_t *px)
{
    uint64_t h = 1469598103934665603ull;
    const size_t words = (size_t)W * H * sizeof(gfx_color_t) / 8u;
    for (size_t i = 0; i < words; ++i) {
        uint64_t v;
        memcpy(&v, (const uint8_t *)px + i * 8u, sizeof(v));
        h = (h ^ v) * 1099511628211ull;
    }
    return h;
}

/** @brief A touch at (@p x, @p y) of the panel, through the router. */
static void touch_at(int x, int y, touch_event_type_t type)
{
    const touch_event_t ev = {
        .type = type,
        .point = { .id = 1, .x = (int16_t)x, .y = (int16_t)y,
                   .strength = 40 },
    };
    ui_router_event(&ev);
}

/** @brief One run: a screen, ARMED or not, and where it goes. */
typedef struct {
    ui_screen_id_t screen;   /**< the screen at the start             */
    bool armed;              /**< the bench ARMED                     */
    bool sweep;              /**< SWEEP tapped at frame 10            */
    bool band;               /**< a FAULT chip and STOP pressed       */
    ui_screen_id_t then;     /**< the screen from frame 70 on         */
} run_t;

/** @brief The panel loop's feed for frame @p i of @p r. */
static void feed(const run_t *r, int i, uint32_t ms)
{
    static telemetry_sim_t sim;
    static bench_state_t bench;
    static supply_sim_t sup_sim;
    static supply_state_t sup;
    if (i == 0) {
        memset(&bench, 0, sizeof(bench));
        memset(&sup, 0, sizeof(sup));
        telemetry_sim_init(&sim, NULL);
        supply_sim_init(&sup_sim);
    }
    const bool motor = r->screen == SCREEN_MOTOR;
    servo_screen_clock(ms);
    servo_screen_set_armed(r->armed && !motor);
    servo_screen_set_bench_armed(r->armed);
    motor_screen_set_armed(r->armed && motor);
    servo_screen_rate(SERVO_RATE_IN_FORCE, 50u);
    servo_screen_set_drive(1u, 12u, true);
    servo_screen_set_sweep(true);
    servo_screen_set_surfaces(true);
    servo_screen_set_pins(1ull << 12, true);
    servo_screen_set_link(true);
    servo_screen_source(SERVO_SOURCE_MODEL, 0u);
    if (i % 3 == 0) {
        telemetry_sim_step(&sim, (r->armed && motor) ? 30.0f : 0.0f, 0.05f,
                           &bench);
        motor_screen_push(&bench);
    }
    if (i % 6 == 0) {
        supply_sim_step(&sup_sim, 0.1f, &sup);
        supply_screen_push(&sup);
        servo_screen_supply_at(&sup, SERVO_SOURCE_MODEL, 0u);
    }
    if (r->sweep && i == 10) {
        touch_at(SV_SWEEP_X, UI_BAND_H + SV_BTN_Y, TOUCH_EVENT_DOWN);
        touch_at(SV_SWEEP_X, UI_BAND_H + SV_BTN_Y, TOUCH_EVENT_UP);
    }
    if (r->band && (i == 40 || i == 50)) {
        const gfx_rect_t stop = ui_band_stop_rect();
        const int sx = stop.x + stop.w / 2, sy = stop.y + stop.h / 2;
        if (i == 40) {
            touch_at(sx, sy, TOUCH_EVENT_DOWN);
        } else {
            touch_at(sx, H / 2, TOUCH_EVENT_MOVE);
            touch_at(sx, H / 2, TOUCH_EVENT_UP);
        }
    }
    if (i == 70 && r->then != r->screen) {
        ui_router_goto(r->then);
    }
    servo_cmd_t cmd;
    while (servo_screen_take(&cmd)) {
        if (cmd.kind == SERVO_CMD_SWEEP) {
            servo_screen_sweep_started(cmd.start_seq, 0u,
                                       SERVO_SWEEP_FROM_REST, 0u);
        }
    }
    motor_cmd_t mc;
    while (motor_screen_poll_cmd(&mc)) { }
    (void)ui_router_take_stop();
    const ui_bench_status_t st = {
        .link_up      = true,
        .armed        = r->armed,
        .armed_roles  = (uint8_t)(!r->armed ? 0u
                                  : motor ? UI_ARMED_MOTOR : UI_ARMED_SERVO),
        .run_seconds  = r->armed ? ms / 1000u : 0u,
        .faults       = (r->band && i >= 30 && i < 90) ? 1u : 0u,
        .mode         = "BENCH",
        .capabilities = LINK_CAP_SERVO_PWM,
        .mcu_temp_c   = NAN,
    };
    ui_router_set_status(&st);
}

/** @brief The setting over a run that switches: 180° from frame 20 to 79
 *         and from 100 on. */
static bool switching(int i)
{
    return (i >= 20 && i < 80) || i >= 100;
}

/**
 * @brief Run @p r through the panel's loop with the fake engine.
 * @param r the run
 * @param want the setting at each frame, or NULL for NORMAL throughout
 * @param stop_at the frame the engine stops completing at, or -1
 * @param pic where the logical pictures go
 * @return the frames whose frame buffer is not what it should hold
 */
static int run(const run_t *r, bool (*want)(int), int stop_at, uint64_t *pic)
{
    for (int b = 0; b < 2; ++b) {
        memset(s_logical[b], 0, (size_t)W * H * sizeof(gfx_color_t));
        memset(s_frame[b], 0, (size_t)W * H * sizeof(gfx_color_t));
    }
    ui_theme_set(UI_THEME_DARK);
    settings_set_store(NULL);
    settings_init();
    ui_router_init();
    supply_screen_settings_loaded();
    ui_router_goto(r->screen);
    gfx_turn_t t;
    gfx_turn_init(&t, W, H);
    engine_t e = engine(W, H);
    const gfx_turn_ops_t o = ops_of(&e);
    int bad = 0;
    unsigned long px_sum = 0ul, px_most = 0ul, rects = 0ul, turned_frames = 0ul;
    for (int i = 0; i < FRAMES; ++i) {
        const uint32_t ms = 1000u + (uint32_t)i * 1000u / 60u;
        feed(r, i, ms);
        ui_router_tick(1.0f / 60.0f);
        if (gfx_turn_take_repaint(&t)) {
            ui_router_invalidate();
        }
        if (i == stop_at) {
            e.mode = ENG_NEVER;
        }
        const int b = i & 1;
        const bool turned = gfx_turn_drawing(&t);
        gfx_canvas_t c;
        gfx_canvas_init(&c, turned ? s_logical[b] : s_frame[b], W, H, W);
        gfx_turn_begin(&t, &c);
        ui_router_render(&c, b);
        const bool whole = t.full_left > 0u;
        const gfx_turn_show_t show = gfx_turn_flush(&t, s_logical[b],
                                                    s_frame[b], &o);
        (void)gfx_turn_presented(&t, show);
        if (show == GFX_TURN_SHOW_TURNED && !whole) {
            px_sum += t.pixels;
            px_most = (t.pixels > px_most) ? t.pixels : px_most;
            rects += t.rects;
            ++turned_frames;
        }
        if (show == GFX_TURN_SHOW_TURNED
            && !is_turned(s_logical[b], s_frame[b], W, H)) {
            printf("    screen %d frame %d: not its picture turned\n",
                   (int)r->screen, i);
            ++bad;
        }
        if (show == GFX_TURN_SHOW_FALLBACK
            && memcmp(s_logical[b], s_frame[b],
                      (size_t)W * H * sizeof(gfx_color_t)) != 0) {
            printf("    screen %d frame %d: fallback not as drawn\n",
                   (int)r->screen, i);
            ++bad;
        }
        if (gfx_turn_shown() != (show == GFX_TURN_SHOW_TURNED)) {
            ++bad;
        }
        pic[i] = picture_hash(turned ? s_logical[b] : s_frame[b]);
        gfx_turn_request(&t, want != NULL && want(i));
        gfx_turn_next(&t);
    }
    if (want != NULL && stop_at < 0 && turned_frames > 0ul) {
        /* For the record: what a turned frame copies, the two whole
         * copies after each switch left out. */
        printf("    screen %d: %lu turned frames, %lu px copied a frame, at "
               "most %lu, %.1f blocks\n", (int)r->screen, turned_frames,
               px_sum / turned_frames, px_most,
               (double)rects / (double)turned_frames);
    }
    if (stop_at >= 0 && stop_at < FRAMES && !t.failed) {
        printf("    screen %d: no fallback\n", (int)r->screen);
        ++bad;
    }
    return bad;
}

/** @brief The runs the screens are held to. */
static const run_t k_runs[] = {
    { SCREEN_SERVO, true, true, false, SCREEN_SERVO },
    { SCREEN_SERVO, true, false, true, SCREEN_OVERVIEW },
    { SCREEN_OVERVIEW, true, false, true, SCREEN_SETUP },
    { SCREEN_MOTOR, true, false, false, SCREEN_MOTOR },
    { SCREEN_SETUP, false, false, false, SCREEN_SERVO },
};

/** @brief Allocate the panel-sized buffers once. */
static bool buffers(void)
{
    for (int b = 0; b < 2; ++b) {
        if (s_logical[b] == NULL) {
            s_logical[b] = calloc((size_t)W * H, sizeof(gfx_color_t));
            s_frame[b] = calloc((size_t)W * H, sizeof(gfx_color_t));
        }
        if (s_logical[b] == NULL || s_frame[b] == NULL) {
            return false;
        }
    }
    return true;
}

/* Switched to 180° and back while the screens run: each turned frame is
 * its picture turned, and each picture is the one a run that never turned
 * shows. */
TEST_CASE(the_screens_turn_and_come_back_pixel_for_pixel)
{
    REQUIRE_ALLOC(buffers() ? s_logical[0] : NULL);
    for (size_t k = 0; k < sizeof(k_runs) / sizeof(k_runs[0]); ++k) {
        CHECK_EQ(run(&k_runs[k], NULL, -1, s_pic[0]), 0);
        CHECK_EQ(run(&k_runs[k], switching, -1, s_pic[1]), 0);
        for (int i = 0; i < FRAMES; ++i) {
            if (s_pic[0][i] != s_pic[1][i]) {
                T_FAIL("screen %d: frame %d differs from the run that "
                       "never turned", (int)k_runs[k].screen, i);
                break;
            }
        }
    }
}

/* An engine that stops completing part way through a turned run: that
 * frame goes out as drawn, the rest NORMAL, the pictures unchanged. */
TEST_CASE(the_screens_fall_back_when_the_engine_stops)
{
    REQUIRE_ALLOC(buffers() ? s_logical[0] : NULL);
    const run_t *r = &k_runs[0];
    CHECK_EQ(run(r, NULL, -1, s_pic[0]), 0);
    CHECK_EQ(run(r, switching, 55, s_pic[1]), 0);
    for (int i = 0; i < FRAMES; ++i) {
        if (s_pic[0][i] != s_pic[1][i]) {
            T_FAIL("frame %d differs from the run that never turned", i);
            break;
        }
    }
    CHECK(!gfx_turn_shown());
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    RUN(a_fresh_state_is_normal);
    RUN(a_switch_copies_the_whole_picture_twice_then_what_changed);
    RUN(the_shown_flag_changes_on_presenting_not_in_the_flush);
    RUN(a_copy_that_never_completes_falls_back_within_the_budget);
    RUN(a_slow_engine_spends_the_budget_over_several_copies);
    RUN(a_refused_block_falls_back);
    RUN(no_engine_at_the_start_stays_normal_with_one_alert);
    RUN(a_flush_without_ops_presents_normal_and_falls_back);
    RUN(a_null_state_does_nothing);
    RUN(the_done_callback_receives_the_display_state);
    RUN(a_done_callback_without_its_context_is_caught);
    RUN(an_engine_without_a_done_context_takes_no_job);
    RUN(frames_through_the_driver_shape_turn);
    RUN(the_screens_turn_and_come_back_pixel_for_pixel);
    RUN(the_screens_fall_back_when_the_engine_stops);
    for (int b = 0; b < 2; ++b) {
        free(s_logical[b]);
        free(s_frame[b]);
    }
    return test_summary("gfx_turn");
}
