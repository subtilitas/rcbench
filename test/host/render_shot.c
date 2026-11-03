/**
 * @file render_shot.c
 * @brief The screenshot popup's views for render_screen.c: the popup, a save
 *        under way and a failed save, over the menu.
 *
 * SPDX-License-Identifier: MIT
 */
#include "render_shot.h"

#include <stdlib.h>
#include <string.h>

#include "ui_screen.h"
#include "ui_shot.h"
#include "ui_textkey.h"

/** @brief The answer the fake card task gives; true once. */
static bool s_answer;
static fm_err_t s_err;

static void *io_alloc(size_t n, void *ctx)
{
    (void)ctx;
    return malloc(n);
}

static void io_release(void *p, void *ctx)
{
    (void)ctx;
    free(p);
}

static bool io_start(ui_shot_job_t job, const gfx_color_t *px, const char *base,
                     void *ctx)
{
    (void)job;
    (void)px;
    (void)base;
    (void)ctx;
    return true;
}

static bool io_poll(fm_err_t *err, unsigned *number, void *ctx)
{
    (void)ctx;
    if (!s_answer) {
        return false;
    }
    s_answer = false;
    *err = s_err;
    *number = 7u;
    return true;
}

static int io_percent(void *ctx)
{
    (void)ctx;
    return 42;
}

static void io_cancel(void *ctx)
{
    (void)ctx;
}

static const ui_shot_io_t k_io = {
    .alloc = io_alloc, .release = io_release, .start = io_start,
    .poll = io_poll, .percent = io_percent, .cancel = io_cancel,
    .now_us = NULL, .copied = NULL, .ctx = NULL,
};

bool render_shot_view(const char *view)
{
    return strncmp(view, "shot-", 5) == 0;
}

/**
 * @brief One event of contact 1.
 * @param type  its type
 * @param x     panel x
 * @param y     panel y
 */
static void touch(touch_event_type_t type, int x, int y)
{
    touch_event_t e = { .type = type,
                        .point = { .id = 1u, .x = (int16_t)x, .y = (int16_t)y,
                                   .strength = 40 } };
    ui_router_event(&e);
}

void render_shot_pose(gfx_canvas_t *c, const char *view)
{
    ui_shot_bind(&k_io);
    ui_router_render(c, 0);
    gfx_rect_t gap;
    if (!ui_router_shot_gap(&gap)) {
        return;
    }
    const int x = gap.x + gap.w / 2;
    touch(TOUCH_EVENT_DOWN, x, 30);
    ui_router_shot_set_idle(true);
    for (int i = 0; i < 10; ++i) {
        ui_router_tick(0.1f);
    }
    ui_router_render(c, 0);
    touch(TOUCH_EVENT_UP, x, 30);
    ui_router_tick(0.016f);
    s_answer = true;
    s_err = FM_OK;
    ui_router_tick(0.016f);     /* the number the card gave */
    if (strcmp(view, "shot-popup") == 0) {
        return;
    }
    const gfx_rect_t ok = ui_shot_key_rect(UI_TK_OK);
    const int kx = ok.x + ok.w / 2;
    const int ky = ok.y + ok.h / 2 + UI_BAND_H;
    touch(TOUCH_EVENT_DOWN, kx, ky);
    touch(TOUCH_EVENT_UP, kx, ky);
    ui_router_tick(0.016f);
    if (strcmp(view, "shot-full") == 0) {
        s_answer = true;
        s_err = FM_ERR_FULL;
        ui_router_tick(0.016f);
    }
}
