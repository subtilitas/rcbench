/*
 * The servo bench: the outputs the binding marks as surfaces, commanded
 * together by dragging one horn.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "servo_test.h"
#include "supply.h"
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
    /**
     * Drive the surfaces through the curve in sweep_*, on the coprocessor.
     * Repeated while it runs, which keeps it running; any other command
     * stops it.
     */
    SERVO_CMD_SWEEP,
    /**
     * Stop a sweep and hold every surface where its output is, which only
     * the coprocessor knows exactly.  Repeated while held; any other
     * command ends it.
     */
    SERVO_CMD_HOLD,
} servo_cmd_kind_t;

typedef struct {
    servo_cmd_kind_t kind;
    uint16_t         value_us;
    /**
     * The range the channel is configured with, carried with the command:
     * centred on the servo's PULSE CENTRE and reaching the further of its
     * PULSE MIN and MAX, so its midpoint -- where the far end rests a
     * surface -- is the centre.  value_us stays within MIN..MAX.
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
    /**
     * The frame rate the profile in force runs at, in Hz.  The panel writes
     * it to the coprocessor's SERVO page, which runs every PWM output whose
     * first channel is a surface at it; see servo_screen_rate().
     */
    uint16_t         frame_hz;
    /** SERVO_CMD_SWEEP's curve, as the SERVO page takes it (servo_sweep.h):
     *  the curve, thousandths of a cycle a second, command units either
     *  side of the centre and the hold at each end.  Zero otherwise. */
    uint16_t         sweep_kind, sweep_mhz, sweep_span, sweep_dwell_ms;
    /** This command ends a sweep the screen was running: a HOLD, a finger
     *  on the dial, CENTRE.  The panel gives it way at once over sweep
     *  writes already on the wire or queued. */
    bool             ends_sweep;
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

/** The frame rate and the type in force, for the application and tests. */
uint16_t servo_screen_frame_hz(void);
const char *servo_screen_type_name(void);

/** One sample of the supply that feeds the servo, for the live power plot
 *  on the right card. */
void servo_screen_supply(const supply_state_t *s);

/** What became of a frame rate the panel wrote to the SERVO page. */
typedef enum {
    SERVO_RATE_UNSENT = 0,  /**< not written yet: it goes with a position   */
    SERVO_RATE_IN_FORCE,    /**< every PWM surface runs at it               */
    SERVO_RATE_REFUSED,     /**< a surface shares a PWM slice with an output
                                 at another rate; the pins kept theirs      */
    SERVO_RATE_UNSUPPORTED, /**< the coprocessor has no SERVO page: protocol
                                 4.0 runs each slot at its binding's rate   */
} servo_rate_state_t;

/**
 * What the panel last heard about the frame rate @p hz.  Called every frame;
 * a state about a rate other than the one the screen shows reads as
 * SERVO_RATE_UNSENT for it.
 */
void servo_screen_rate(servo_rate_state_t st, uint16_t hz);

/** Whether the coprocessor can sweep: protocol 4.2 or later.  SWEEP is
 *  offered only then. */
void servo_screen_set_sweep(bool able);

/** Whether a sweep is running, for the application and tests. */
bool servo_screen_sweeping(void);

/** Whether a sweep is paused: PAUSE tapped, the hold in force, nothing
 *  else commanded since.  For tests. */
bool servo_screen_paused(void);

/** The panel let go of what the screen was holding -- a HOLD the far end
 *  had already ended -- and released the surfaces to their centre. */
void servo_screen_released(void);

/** Where the coprocessor's output was when it started a sweep. */
typedef enum {
    SERVO_SWEEP_FROM_REST,    /**< where it was held before the sweep      */
    SERVO_SWEEP_FROM_HERE,    /**< carrying on: a changed curve            */
    SERVO_SWEEP_FROM_FROZEN,  /**< where it froze when the sweep went
                                   unrepeated, @p frozen_ago_ms ago        */
} servo_sweep_from_t;

/**
 * The coprocessor started the sweep @p age_ms ago, its output starting from
 * @p from: the horn is drawn along its curve from then, and without feedback
 * from where that output was.
 */
void servo_screen_sweep_started(uint32_t age_ms, servo_sweep_from_t from,
                                uint32_t frozen_ago_ms);

/**
 * Set the commanded angle without a touch event.
 *
 * Restores the position at start-up, so a boot does not centre a surface the
 * operator has set.
 */
void servo_screen_set_commanded(float deg);

/* ------------------------------------------------- the automatic test */

/*
 * START TEST on the TEST page runs servo_test.h's automatic test from this
 * screen: a two-second hold, or HV SERVOS ONLY held for two seconds when a
 * step is above 6.0 V.  It needs an armed bench and a supply that answers,
 * and it commands the servo and SUPPLY's set points and output itself.
 * Progress and the result are shown on the left card.
 */

/**
 * The panel's millisecond clock, every frame before the tick: the clock the
 * supply's readings are stamped with (supply_state_t.taken_ms), so a travel
 * time is measured on one clock.  Without it the screen's own is used.
 */
void servo_screen_clock(uint32_t now_ms);

/** Whether the link to the coprocessor is up; a run ends when it goes. */
void servo_screen_set_link(bool up);

/** Whether a run is under way, for the application and tests. */
bool servo_screen_testing(void);

/**
 * The run's next item for the card, without taking it (servo_test_peek()),
 * and taking it once it is on its way.  The application drains these every
 * frame, whichever screen is up.
 */
servo_test_out_t servo_screen_test_peek(const char **text);
void servo_screen_test_pop(void);

/**
 * The number of the files the run's items went to -- BENCHnnn.CSV and,
 * with REPORT, BENCHnnn.TXT -- or -1 when the card took none; and whether
 * the card took the .TXT whole.
 */
void servo_screen_test_files(int number, bool report);

/**
 * Every frame, whichever screen is up: a run's end is seen, and SUPPLY's
 * set points go back to what they were before it once its OFF has gone, a
 * sample taken after that shows the output off, no ON is on its way and
 * OUTPUT ON is not being held, here or on SUPPLY -- unless they were set
 * since it ended.
 */
void servo_screen_service(void);
