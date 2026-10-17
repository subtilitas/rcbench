/*
 * The programmer screen under test: a framebuffer, the screen reset to its
 * start, a tap on a screen coordinate, and the layout constants of
 * programmer_screen.c mirrored for the hit rectangles of its first three
 * levels.  Shared by test_programmer.c and test_esc_stick_panel.c.
 *
 * For the host tests.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "gfx.h"
#include "programmer_screen.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_screen.h"
#include "ui_theme.h"
#include "touch_feed.h"

#define W 800
#define H 480

static gfx_color_t *fb;
static gfx_canvas_t cv;
static const ui_screen_t *scr;

/* Mirrored from programmer_screen.c; if the layout moves these move with it. */
#define TILE_CX(i) (18 + (i) * 260 + 122)
#define TILE_CY    180
#define BACK_X     66
#define BACK_Y     27
#define ROW_CX     400
#define ROW_CY(n)  (64 + (n) * 68 + 29)
#define CONNECT_X  698
#define CONNECT_Y  70
#define STEP_DN_X  653
#define STEP_UP_X  765
#define STEP_CY(i) (132 + (i) * 30 + 10)
#define READ_X     516
#define WRITE_X    698
#define BTN_CY     407
#define PAGE_UP_X  723
#define PAGE_DN_X  761
#define PAGE_CY    116

static inline void fresh(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb);
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    settings_init();
    supply_screen()->reset();
    const supply_caps_t caps = SUPPLY_CAPS_PPS_DEFAULT;
    supply_screen_set_caps(&caps);
    supply_screen_settings_loaded();
    scr = programmer_screen();
    scr->reset();
    programmer_screen_set_throttle(true, 0x01u);    /* channel 0 a throttle */
}

static inline void tap(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    scr->event(&e);
    e.type = TOUCH_EVENT_UP;
    scr->event(&e);
}

static inline int lit(void)
{
    int n = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] != ui_theme_color(UI_C_BG)) { ++n; }
    }
    return n;
}

/* ESC (electronic speed controller) then BLHeli_S then CONNECT: the whole
 * descent. */
static inline void descend_to_blheli(void)
{
    tap(TILE_CX(0), TILE_CY);
    tap(ROW_CX, ROW_CY(0));
    tap(CONNECT_X, CONNECT_Y);
}
