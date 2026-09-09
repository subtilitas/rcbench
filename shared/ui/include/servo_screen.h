/*
 * The servo bench: the outputs the binding marks as surfaces, commanded
 * together by dragging one horn.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "ui_screen.h"

typedef enum {
    SERVO_CMD_NONE = 0,
    SERVO_CMD_POSITION,   /**< value is the pulse width in microseconds */
    SERVO_CMD_CENTRE,
    /**
     * Let go of the output: the surfaces the bench drives return to centre,
     * which is where a surface rests.  The pins stay bound to what the
     * OUTPUTS screen bound them to; this screen commands channels and owns no
     * wiring of its own.
     */
    SERVO_CMD_RELEASE,
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
    /**
     * The endpoints the selected servo type has, carried with the command.
     *
     * The pulse means nothing without them: 760 us is the centre of a narrow
     * servo and below the bottom of a standard one, so a command clamped
     * against the wrong pair is a servo that does not move, or one driven
     * past its stops. Zero on either says the sender named no range.
     */
    uint16_t         min_us, max_us;
    /**
     * How fast the bench may move the output, in channel-span units a
     * second; zero is at once.
     *
     * The SPEED setting, as the far end takes it. A servo goes at its own
     * rate unless the bench ramps the command in front of it, so a speed
     * that only moved the drawing would be a control that does nothing to
     * the thing under test.
     */
    uint16_t         slew_per_s;
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

/**
 * Abandon a hold that is under way, because the bench has been stopped.
 *
 * Separate from servo_screen_set_armed() because a stop on a bench that was
 * not armed changes nothing about whether it is armed, and the gesture must
 * end all the same.
 */
void servo_screen_cancel_arm(void);

/** Commanded pulse width, for the application and for tests. */
uint16_t servo_screen_commanded(void);

/**
 * Set the commanded angle without a touch event.
 *
 * Restores the position at start-up, so a boot does not centre a surface the
 * operator has set.
 */
void servo_screen_set_commanded(float deg);
