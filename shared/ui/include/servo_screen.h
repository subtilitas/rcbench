/*
 * The servo bench: one output, commanded by dragging its horn.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "ui_screen.h"

typedef enum {
    SERVO_CMD_NONE = 0,
    SERVO_CMD_POSITION,   /**< value is the pulse width in microseconds */
    SERVO_CMD_CENTRE,
    SERVO_CMD_RELEASE,    /**< stop driving the output                  */
    SERVO_CMD_ARM,        /**< the two-second hold, as on MOTOR & ESC    */
    /**
     * Let go of the output and disarm.
     *
     * One command rather than two, because leaving the screen has to do
     * both and a screen that can only post one of them would leave a pin
     * bound behind a bench it had just disarmed.
     */
    SERVO_CMD_DISARM,
} servo_cmd_kind_t;

typedef struct {
    servo_cmd_kind_t kind;
    uint16_t         value_us;
} servo_cmd_t;

/** Drop the cached chrome, so the next frame repaints it. */
void servo_invalidate(void);

const ui_screen_t *servo_screen(void);

/** Take the pending command, if any.  Cleared by reading. */
bool servo_screen_take(servo_cmd_t *out);

/** What the output is actually doing, from the bench or from the model. */
void servo_screen_feedback(uint16_t position_us, float current_a, bool valid);

/**
 * What the bench is, so the button can say it.
 *
 * The screen does not decide whether it is armed: arming is asked of the
 * policy and the answer comes back from the bench, the same way MOTOR & ESC
 * learns it.
 */
void servo_screen_set_armed(bool armed);

/** Commanded pulse width, for the application and for tests. */
uint16_t servo_screen_commanded(void);

/**
 * Set the commanded angle without a touch event.
 *
 * Restores the position at start-up, so a boot does not centre a surface the
 * operator has set.
 */
void servo_screen_set_commanded(float deg);
