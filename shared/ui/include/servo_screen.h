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
    /** SERVO_CMD_SWEEP carrying on the sweep PAUSE held, from where it was
     *  held: the panel asks the coprocessor to resume it (protocol 4.6),
     *  and an older one, or one that refuses, starts the curve over. */
    bool             resume;
    /** SERVO_CMD_HOLD: which pause it is, given back with its
     *  acknowledgement in servo_screen_sweep_held().  Never 0. */
    uint16_t         pause_seq;
    /** The pause this command derives from -- a resume of it, a repeat of
     *  that resume, a position said again under a changed profile -- or 0:
     *  what the panel drops once it has let that pause go
     *  (servo_cmd_stale()). */
    uint16_t         from_pause;
    /** SERVO_CMD_SWEEP: which sweep command it is, given back with the
     *  acknowledgement of a start in servo_screen_sweep_started(). */
    uint16_t         start_seq;
} servo_cmd_t;

/** Drop the cached chrome, so the next frame repaints it. */
void servo_invalidate(void);

const ui_screen_t *servo_screen(void);

/**
 * Move the horn by @p span_fraction of its travel (-travel to +travel),
 * from the rotary knob (knob.h).  Relative to the commanded angle.  Nothing
 * happens for a zero fraction, while a finger is on the dial, while the
 * settings panel is open, or while a sweep (running or paused) or a test run owns the horn: the
 * knob does not take the horn from them as a finger does.  It posts the
 * position command a touch does and never arms.  It posts only into an empty
 * slot or over a position: any other pending command (a release, an arm, a
 * disarm) stays and the knob's motion is dropped.
 */
void servo_screen_knob(float span_fraction);

/**
 * Start of a frame, before its touch events: forget that a finger owned the
 * dial or that the settings, a sweep or a test owned the horn.  The knob
 * moves nothing in a frame in which a finger owned the dial at any point,
 * including a press and release inside it, or in which the settings were
 * open, a sweep or a test ran at any point, even if it ended before the
 * turn is applied.
 */
void servo_screen_knob_frame(void);

/**
 * Withdraw the knob's position command if it is still waiting to be taken:
 * the commanded angle returns to its value from before the knob, and a
 * position that was pending before the knob moved it is pending again with
 * that angle, and the output is held or idle as it was before the knob.  A
 * command posted since, or already taken, is left alone.
 */
void servo_screen_knob_cancel(void);

/** Take the pending command, if any.  Cleared by reading. */
bool servo_screen_take(servo_cmd_t *out);

/**
 * A reading of the output encoder (an AS5600 on the horn shaft), each one
 * once, in the order they arrive.  With AS5600 on in SETUP the MEASURED row
 * shows its angle from the centre count, and a run reads it for the angle
 * results in its CSV and report.  A reading that is not valid shows "---".
 */
void servo_screen_encoder(const servo_test_enc_t *e);

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

/** The pulse width the solid arm is drawn at: measured, or without
 *  feedback the drawing's estimate of the output.  For tests. */
uint16_t servo_screen_drawn(void);

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

/** Whether a surface is bound, as the panel last read the binding: SWEEP
 *  is offered only then, since a sweep of nothing never starts. */
void servo_screen_set_surfaces(bool any);

/** Whether the coprocessor holds the output encoder enabled, as SENSE's
 *  ENABLE register was last read or written.  The SENSE set-up is written
 *  only while the bank is disarmed: a run started with the AS5600 setting on
 *  and this false has no angles, and takes no angle columns. */
void servo_screen_set_enc_held(bool held);

/** A sweep command found no surface to sweep and was not sent: the screen
 *  stops waiting for its start and ends the sweep. */
void servo_screen_sweep_refused(void);

/**
 * Whether a command kept to be said again may still be said after the link
 * has gone and come back.  A sweep or a hold may not: both end with the
 * link, on the screen and at the far end, and said again they would start
 * motion nobody asked for.  A position is what the screen still shows.
 */
bool servo_cmd_survives_link_loss(const servo_cmd_t *c);

/**
 * The pause a HOLD @p c ends when the panel lets it go: the pause its chain
 * rests on (from_pause) when it was posted before an earlier pause's resume
 * was acknowledged, and otherwise its own.  What the panel records for
 * servo_cmd_stale() and hands to servo_screen_released().
 */
uint16_t servo_cmd_pause_root(const servo_cmd_t *c);

/** A pause the panel has let go of, for servo_cmd_stale(). */
typedef struct {
    bool     on;
    uint16_t pause_seq;
} servo_pause_end_t;

/**
 * Whether the drive command @p c (a position, a centre, a sweep or a hold)
 * was asked during the pause @p e records as let go of, so is stale and is
 * not to be sent: the screen has ended that pause, and the command would
 * hold or move the servo somewhere it no longer shows.  The screen asks
 * nothing more of a pause it has left, and commands keep their order, so
 * the first drive command from after the pause retires @p e; a pause
 * number reused after 65535 more is then not mistaken for it.
 */
bool servo_cmd_stale(servo_pause_end_t *e, const servo_cmd_t *c);

/** Whether a sweep is running, for the application and tests. */
bool servo_screen_sweeping(void);

/** Whether a sweep is paused: PAUSE tapped, the hold in force, nothing
 *  else commanded since.  For tests. */
bool servo_screen_paused(void);

/**
 * The panel let go of pause @p pause_seq -- a HOLD the far end had already
 * ended, or one answered late -- released the surfaces to their centre and
 * dropped the commands asked during that pause.  Taken only while the
 * screen's commands still derive from that pause; one that has moved on
 * since was sent, and the surfaces follow it.
 */
void servo_screen_released(uint16_t pause_seq);

/** Where the coprocessor's output was when it started a sweep. */
typedef enum {
    SERVO_SWEEP_FROM_REST,    /**< where it was held before the sweep      */
    SERVO_SWEEP_FROM_HERE,    /**< carrying on: a changed curve            */
    SERVO_SWEEP_FROM_FROZEN,  /**< where it froze when the sweep went
                                   unrepeated, @p frozen_ago_ms ago        */
    SERVO_SWEEP_RESUMED,      /**< where it was held: the paused sweep
                                   carried on from its phase; @p age_ms is
                                   the age of the curve's phase 0, moved on
                                   by the hold (servo_phase_resumed())     */
} servo_sweep_from_t;

/**
 * The coprocessor started the sweep with its curve's phase 0 @p age_ms ago,
 * its output starting from @p from.  Until this, a sweep asked for is not
 * drawn: the far end is not known to move.  From it the horn is drawn along
 * the curve, and without feedback worked on from where that output was
 * when the far end began to move.  @p since_ms is, for
 * SERVO_SWEEP_FROM_FROZEN, how long ago the far end froze, and for
 * SERVO_SWEEP_RESUMED how long ago the resume was acknowledged; for a
 * resume @p age_ms is the panel's timing of the far end's phase 0, moved on
 * by the hold, whatever this screen took the pause's phase to be.
 * @p start_seq is the sweep command whose write it acknowledges
 * (servo_cmd_t): a start or resume waited for is ended only by its own.
 */
void servo_screen_sweep_started(uint16_t start_seq, uint32_t age_ms,
                                servo_sweep_from_t from, uint32_t since_ms);

/**
 * The coprocessor took the HOLD of pause @p pause_seq (servo_cmd_t) and kept
 * its curve @p kept_ms in (servo_phase_held()).  The curve ran on until
 * then, so the paused phase drawn here is set to it, not to the tap's.
 * Nothing unless that pause still stands: one resumed before this arrives
 * is rebased by its SERVO_SWEEP_RESUMED, and another pause's is not this
 * one's.
 */
void servo_screen_sweep_held(uint16_t pause_seq, uint32_t kept_ms);

/** How far the sweep's curve clock is from its phase 0, paused or not.
 *  For tests, which stand in for the panel's timing. */
uint32_t servo_screen_curve_ms(void);

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
