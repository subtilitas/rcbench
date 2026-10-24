/*
 * ESC STICK under test: a framebuffer, the session's page set
 * (esc_stick_pages) behind a screen's hooks as MOTOR & ESC drives it, the
 * session reset and opened from PROGRAM's row, a tap on a body coordinate,
 * and the layout constants of esc_stick_int.h mirrored for the hit
 * rectangles.  For test_esc_stick_panel.c and test_esc_stick_panel_*.c; test_motor_program.c drives the
 * session through MOTOR & ESC itself.
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

#include "esc_stick_panel.h"
#include "gfx.h"
#include "prog_registry.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_screen.h"
#include "ui_theme.h"
#include "touch_feed.h"

#include "stick_spots.h"

#define W UI_PANEL_W
#define H UI_PANEL_H

static gfx_color_t *fb;
static gfx_canvas_t cv;
static const ui_screen_t *scr;

/* The layout's spots (stick_spots.h) under the names the rig's tests
 * use. */
#define SP_RIGHT   ST_RIGHT
#define BACK_X     ST_BACK_X
#define BACK_Y     ST_CRUMB_CY
#define ROW_CX     ST_PROW_X
#define STEP_DN_X  ST_STEP_DN_X
#define STEP_UP_X  ST_STEP_UP_X
#define STEP_CY(i) ST_STEP_CY(i)
#define READ_X     ST_READ_X
#define WRITE_X    ST_WRITE_X
#define BTN_CY     ST_BTN_CY
#define PAGE_UP_X  ST_PAGE_UP_X
#define PAGE_DN_X  ST_PAGE_DN_X
#define PAGE_CY    ST_PAGE_CY

/* The page set behind the hooks the rig's tests call: a screen whose reset
 * starts the session over, whose leave and enter are the host's, whose
 * home tag climbs the session and is false once it is closed, and whose
 * render paints the whole canvas on a revision not yet drawn: the output
 * strip's row is MOTOR & ESC's, and stays clear here. */
static uint32_t s_rig_drawn[2] = { UINT32_MAX, UINT32_MAX };
static unsigned s_rig_mask;

static inline void stick_invalidate(void)
{
    s_rig_mask = 0u;
    s_rig_drawn[0] = UINT32_MAX;
    s_rig_drawn[1] = UINT32_MAX;
}

static inline void rig_reset(void)
{
    esc_stick_pages.init((gfx_rect_t){ 0, PROG_FULL_Y, W,
                                       (int16_t)(H - UI_BAND_H - PROG_FULL_Y) });
    stick_invalidate();
}

static inline void rig_enter(void) { esc_stick_pages.entry(); }
static inline void rig_leave(void) { esc_stick_pages.leave(); }
static inline void rig_tick(float dt) { esc_stick_pages.tick(dt); }
static inline void rig_cancel(void) { esc_stick_pages.cancel(); }

static inline void rig_event(const touch_event_t *e)
{
    esc_stick_pages.event(e);
}

static inline bool rig_back(void)
{
    if (!esc_stick_pages.is_open()) {
        return false;
    }
    esc_stick_pages.back();
    return true;
}

static inline void rig_render(gfx_canvas_t *c, int buffer_index)
{
    const int buf = buffer_index & 1;
    if ((s_rig_mask & (1u << buf)) == 0u) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        s_rig_mask |= 1u << buf;
    }
    if (s_rig_drawn[buf] == esc_stick_pages.revision()) {
        return;
    }
    s_rig_drawn[buf] = esc_stick_pages.revision();
    gfx_clear(c, ui_theme_color(UI_C_BG));
    esc_stick_pages.render(c);
}

static const ui_screen_t k_rig_screen = {
    .title  = "MOTOR & ESC",
    .reset  = rig_reset,
    .enter  = rig_enter,
    .leave  = rig_leave,
    .tick   = rig_tick,
    .event  = rig_event,
    .cancel = rig_cancel,
    .render = rig_render,
    .back   = rig_back,
};

/* PROGRAM's STICK MENU row tapped: the session on the makers. */
static inline void open_stick(void)
{
    esc_stick_pages.open();
}

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
    scr = &k_rig_screen;
    scr->reset();
    esc_stick_panel_set_throttle(true, 0x01u);    /* channel 0 a throttle */
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
