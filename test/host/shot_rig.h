/**
 * @file shot_rig.h
 * @brief The screenshot's suites' rig: the application's side (ui_shot_io_t),
 *        a router on the menu, a hold in the gap and a tap on a popup's key.
 *
 * Only static inline helpers and the counters they use.
 *
 * Used by:
 * - test_shot.c
 * - test_shot_popup.c
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "nav_rig.h"
#include "supply_spots.h"

#include <stddef.h>

#define REQUIRE(c)                                                            \
    do {                                                                      \
        if (!(c)) {                                                           \
            T_FAIL("required %s", #c);                                        \
            return;                                                           \
        }                                                                     \
    } while (0)

#include "balance_screen.h"
#include "busfault_screen.h"
#include "shot_file.h"
#include "shot_gate.h"
#include "fm_name.h"
#include "ui_shot.h"
#include "ui_textkey.h"

/* ------------------------------------------------- the application's side */

static int           g_allocs __attribute__((unused));
static int           g_releases __attribute__((unused));
static bool          g_alloc_fails __attribute__((unused));
static ui_shot_job_t g_job __attribute__((unused));
static char          g_base[80] __attribute__((unused));
static const gfx_color_t *g_job_px __attribute__((unused));
static bool          g_answered __attribute__((unused));
static fm_err_t      g_err __attribute__((unused));
static unsigned      g_number __attribute__((unused));
static int           g_cancels __attribute__((unused));
static int           g_copies __attribute__((unused));
static bool          g_start_fails __attribute__((unused));

static inline void *io_alloc(size_t n, void *ctx)
{
    (void)ctx;
    if (g_alloc_fails) {
        return NULL;
    }
    ++g_allocs;
    return malloc(n);
}

static inline void io_release(void *p, void *ctx)
{
    (void)ctx;
    ++g_releases;
    free(p);
}

static inline bool io_start(ui_shot_job_t job, const gfx_color_t *px, const char *base,
                     void *ctx)
{
    (void)ctx;
    if (g_start_fails) {
        return false;
    }
    g_job = job;
    g_job_px = px;
    g_answered = false;
    snprintf(g_base, sizeof(g_base), "%s", (base != NULL) ? base : "");
    return true;
}

static inline bool io_poll(fm_err_t *err, unsigned *number, void *ctx)
{
    (void)ctx;
    if (!g_answered) {
        return false;
    }
    g_answered = false;
    *err = g_err;
    *number = g_number;
    return true;
}

static inline int io_percent(void *ctx)
{
    (void)ctx;
    return 42;
}

static inline void io_cancel(void *ctx)
{
    (void)ctx;
    ++g_cancels;
}

static inline uint32_t io_now(void *ctx)
{
    (void)ctx;
    return 0u;
}

static inline void io_copied(uint32_t us, void *ctx)
{
    (void)ctx;
    (void)us;
    ++g_copies;
}

static const ui_shot_io_t k_io __attribute__((unused)) = {
    .alloc = io_alloc, .release = io_release, .start = io_start,
    .poll = io_poll, .percent = io_percent, .cancel = io_cancel,
    .now_us = io_now, .copied = io_copied, .ctx = NULL,
};

/* ------------------------------------------------------------- the rig */

/** @brief What the band shows in the cases: LINK, BENCH, SAFE, no clock. */
static inline ui_bench_status_t status(void)
{
    ui_bench_status_t st = { 0 };
    st.link_up = true;
    st.mode = "BENCH";
    st.mcu_temp_c = NAN;
    return st;
}

/** @brief A router on the menu, the bench idle unless the case says. */
static inline void start(void)
{
    fresh();
    ui_shot_bind(&k_io);
    ui_router_goto(SCREEN_OVERVIEW);
    const ui_bench_status_t st = status();
    ui_router_set_status(&st);
    g_allocs = g_releases = g_cancels = g_copies = 0;
    g_alloc_fails = g_start_fails = false;
    g_job = UI_SHOT_JOB_NONE;
    g_answered = false;
    g_err = FM_OK;
    g_number = 1u;
    ui_router_shot_set_idle(true);
}

/** @brief One frame's render into the panel's canvas. */
static inline void frame(void)
{
    ui_router_render(&cv, 0);
}

/** @brief Time passes in steps of 100ms and then @p ms. */
static inline void wait_ms(int ms)
{
    while (ms > 100) {
        ui_router_tick(0.1f);
        ms -= 100;
    }
    if (ms > 0) {
        ui_router_tick((float)ms / 1000.0f);
    }
}

/** @brief The band's empty part's middle, x. */
static inline int gap_x(void)
{
    gfx_rect_t g;
    if (!ui_router_shot_gap(&g)) {
        return -1;
    }
    return g.x + g.w / 2;
}

/** @brief A hold of contact 1 in the gap, 1000ms, and the frame after it. */
static inline void hold_shot(void)
{
    touch(gap_x(), 30, TOUCH_EVENT_DOWN, 1);
    wait_ms(1000);
    frame();
}

/** @brief A hold, its release and a tick: a picture on the popup. */
static inline void open_popup(void)
{
    hold_shot();
    touch(gap_x(), 30, TOUCH_EVENT_UP, 1);
    ui_router_tick(0.016f);
}

/** @brief A tap on a key of the popup's keyboard. */
static inline void tap_key(int key)
{
    const gfx_rect_t r = ui_shot_key_rect(key);
    tap(r.x + r.w / 2, r.y + r.h / 2 + UI_BAND_H);
}

