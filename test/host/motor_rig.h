/**
 * @file motor_rig.h
 * @brief The bench screen's cases as test_motor.c and test_motor_*.c share them.
 *
 * Only static inline helpers and the buffers and counters they use.
 *
 * Used by:
 * - test_motor.c
 * - test_motor_draw.c
 * - test_motor_throttle.c
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "motor_spots.h"
#include "touch_feed.h"

#include "motor_screen.h"
#include "settings.h"
#include "telemetry_sim.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"

#define W UI_PANEL_W
#define H UI_PANEL_H

static gfx_color_t *fb __attribute__((unused));
static gfx_canvas_t cv __attribute__((unused));
static const ui_screen_t *scr __attribute__((unused));

/** @brief Clears the canvas and resets the bench screen and its queued commands. */
static inline void fresh(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb);
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    scr = motor_screen();
    scr->reset();
    motor_cmd_t junk;
    while (motor_screen_poll_cmd(&junk)) { }
}

/** @brief Screen coordinates: the router strips the band before the screen sees an
 * event, so these are already screen-local. */
static inline void ev(int x, int y, touch_event_type_t t, uint8_t id)
{
    const touch_event_t e = { .type = t,
                              .point = { .id = id, .x = (int16_t)x,
                                         .y = (int16_t)y, .strength = 40 } };
    scr->event(&e);
}
/** @brief Presses and releases contact 1 at (@p x, @p y). */
static inline void tap(int x, int y) { ev(x, y, TOUCH_EVENT_DOWN, 1);
                                ev(x, y, TOUCH_EVENT_UP, 1); }

/*
 * Geometry from motor_spots.h, in screen-local coordinates: the router
 * strips the band before a screen sees an event.  The test names say what
 * they aim at.
 */
#define BTN_H    UI_BTN_H
#define TRACK_Y  MO_TRACK_Y
/* ARM and RESET PEAKS live in the CONTROL card on the right rail, ARM
 * at its foot, where SERVO has its ARM. */
#define ARM_X    MO_ARM_X
#define ARM_Y    MO_ARM_Y
/* One percentage point at each end of the track. */
#define DOWN_X   MO_DOWN_X
#define UP_X     MO_UP_X
/* The track itself: x 98..520, so half of it is half the travel. */
#define TRACK_X  MO_TRACK_X

/* Mirrored from motor_live_int.h: arming is a hold, not a press. */
#define ARM_HOLD_S  2.0f
#define TICK_S      0.05f
/* Enough steps to pass the hold, derived rather than counted, so changing
 * either constant does not silently stop these tests from arming. */
#define HOLD_TICKS  ((int)(ARM_HOLD_S / TICK_S) + 5)

/** @brief Ticks the screen @p steps times at TICK_S. */
static inline void tick_for(int steps)
{
    for (int i = 0; i < steps; ++i) {
        scr->tick(TICK_S);
    }
}

/** @brief Hold ARM long enough to arm, and let go. */
static inline void hold_arm(void)
{
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
}

/**
 * @brief RGB565 components, for comparing a button's fill without pinning it.
 * Scaled to 8 bits, because red carries 5 bits and green 6 and the raw
 * fields are not comparable with each other.
 */
static inline int red_of(gfx_color_t c)   { return (int)(((c >> 11) & 0x1f) << 3); }
/** @brief The 8-bit green of an RGB565 pixel. */
static inline int green_of(gfx_color_t c) { return (int)(((c >> 5) & 0x3f) << 2); }
/** @brief The pixel inside the ARM button, clear of its corner, hairline and label. */
static inline gfx_color_t arm_px(void)
{
    /* Inside the ARM button, clear of its rounded corner, its hairline and
     * the centred label. */
    return fb[(size_t)(ARM_Y + 8) * W + (ARM_X - 90)];
}

/** @brief Takes every queued motor command and returns the last; MOTOR_CMD_NONE when none was queued. */
static inline motor_cmd_t last_cmd(void)
{
    motor_cmd_t c = { MOTOR_CMD_NONE, 0.0f };
    while (motor_screen_poll_cmd(&c)) { }
    return c;
}
#define TGT_TAP_X MO_TARGET_X
#define TGT_TAP_Y MO_TARGET_Y
