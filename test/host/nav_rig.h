/**
 * @file nav_rig.h
 * @brief The shell's cases as test_nav.c and test_nav_*.c share them: the canvas,
 * a touch and a tap, the way back to the overview, the tiles and the home
 * tag, STOP, and the openers of the screens' lists, keypads and panels.
 *
 * Only static inline helpers and the buffers and counters they use.
 *
 * Used by:
 * - test_nav.c
 * - test_nav_tiles.c
 * - test_nav_alert.c
 * - test_nav_mark.c
 * - test_nav_contact.c
 * - test_nav_drag.c
 * - test_nav_bubble.c
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "touch_feed.h"

#include "gfx.h"
#include "kst_panel.h"
#include "link_pages.h"
#include "motor_screen.h"
#include "out_bind.h"
#include "outputs_screen.h"
#include "overview_screen.h"
#include "picker_screen.h"
#include "esc_stick_panel.h"
#include "servo_screen.h"
#include "settings.h"
#include "settings_screen.h"
#include "splash_screen.h"
#include "supply_screen.h"
#include "ui_alert_slot.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_watermark.h"
#include "ui_widgets.h"
#include "ui_workbench.h"

#include "servo_spots.h"

#define W 800
#define H 480

static gfx_color_t *fb __attribute__((unused));
static gfx_canvas_t cv __attribute__((unused));

/** @brief Clears the canvas and resets the router and the screens the cases use. */
static inline void fresh(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb);
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    ui_router_init();
}

/** @brief Feeds the router one touch event of contact @p id at (@p x, @p y). */
static inline void touch(int x, int y, touch_event_type_t type, uint8_t id)
{
    touch_event_t e = { .type = type,
                        .point = { .id = id, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    ui_router_event(&e);
}

/** @brief Presses and releases contact 1 at (@p x, @p y). */
static inline void tap(int x, int y)
{
    touch(x, y, TOUCH_EVENT_DOWN, 1);
    touch(x, y, TOUCH_EVENT_UP, 1);
}

/** @brief Leave the splash the way the application does. */
static inline void to_overview(void)
{
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    ui_router_goto(SCREEN_OVERVIEW);
}

/** @brief The middle of SETUP's tile @p index, in panel coordinates. */
static inline void hub_point(int index, int *x, int *y)
{
    gfx_rect_t r = gfx_rect_make(0, 0, 0, 0);
    CHECK(settings_hub_slot(index, &r));
    *x = r.x + r.w / 2;
    *y = UI_BAND_H + r.y + r.h / 2;
}

/* ------------------------------------------------ one owner per contact */

/*
 * Geometry in panel coordinates, mirrored from the screens (their own
 * suites carry the screen-local values).
 * MOTOR & ESC: the throttle track x 72..485, y 384..423; + at x 492..547.
 * SERVO: the shaft; SPEED's track x 514..781, y 344..365.
 * SUPPLY: both tracks x 72..485; OUTPUT ON / OFF on the right rail.
 */
#define M_TRACK_Y  (336 + UI_BAND_H + 20)
#define M_PX_PCT   (100.0f / 413.0f)     /* throttle per px of travel */
#define UNDER_STOP_Y 44

/** @brief Takes the next motor command into @p c when given and returns whether one was queued. */
static inline bool motor_posted(motor_cmd_t *c)
{
    motor_cmd_t got = { .kind = MOTOR_CMD_NONE };
    const bool any = motor_screen_poll_cmd(&got);
    if (c != NULL) {
        *c = got;
    }
    return any;
}

/** @brief Takes every queued motor command. */
static inline void drain_motor(void)
{
    while (motor_posted(NULL)) { }
}

/** @brief Takes the next servo command; SERVO_CMD_NONE when none is queued. */
static inline servo_cmd_t servo_took(void)
{
    servo_cmd_t c = { .kind = SERVO_CMD_NONE };
    servo_screen_take(&c);
    return c;
}

/** @brief Takes every queued servo command. */
static inline void drain_servo(void)
{
    while (servo_took().kind != SERVO_CMD_NONE) { }
}

/** @brief Takes every queued supply command. */
static inline void drain_supply(void)
{
    supply_cmd_t c;
    while (supply_screen_poll_cmd(&c)) { }
}

/** @brief A bench screen on top, nothing on the glass, nothing waiting. */
static inline void on_screen(ui_screen_id_t id)
{
    settings_set_store(NULL);
    settings_init();
    fresh();
    to_overview();
    ui_router_goto(id);
    if (id == SCREEN_SUPPLY) {
        supply_screen_settings_loaded();
    }
    drain_motor();
    drain_servo();
    drain_supply();
    feed_reset();
}

/** @brief Whether the knob moves the throttle: it does not while a finger owns the
 * slider.  The turn is taken back. */
static inline bool knob_moves_the_throttle(void)
{
    motor_screen_knob_frame();
    motor_screen_knob(0.05f);
    const bool posted = motor_posted(NULL);
    if (posted) {
        motor_screen_knob_frame();
        motor_screen_knob(-0.05f);
        (void)motor_posted(NULL);
    }
    return posted;
}

/** @brief A point on the band, clear of STOP and of the home tag. */
static inline void check_plain_band(int x, int y)
{
    CHECK(y < UI_BAND_H);
    CHECK(!gfx_rect_contains(ui_band_stop_rect(), x, y));
    CHECK(!gfx_rect_contains(
              ui_home_tag_rect(ui_router_title(ui_router_current())), x, y));
}
