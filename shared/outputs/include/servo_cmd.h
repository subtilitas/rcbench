/**
 * @file
 * @brief A command of the SERVO screen as the panel's servo link takes it
 *        (servo_link.h), and what the link reports back: the frame rate's
 *        state, a pause let go of and where a sweep started.
 *
 * Below the screen, so the link module in shared/outputs takes the types
 * without the screen's header.  servo_screen.h includes it.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The kind of a command of the SERVO screen. */
typedef enum {
    SERVO_CMD_NONE = 0,   /**< no command */
    SERVO_CMD_POSITION,   /**< value is the pulse width in microseconds */
    SERVO_CMD_CENTRE,     /**< centre the surfaces */
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
     * Drive the surfaces through the curve in sweep_*, on the IOmcu.
     * Repeated while it runs, which keeps it running; any other command
     * stops it.
     */
    SERVO_CMD_SWEEP,
    /**
     * Stop a sweep and hold every surface where its output is, which only
     * the IOmcu knows exactly.  Repeated while held; any other
     * command ends it.
     */
    SERVO_CMD_HOLD,
} servo_cmd_kind_t;

/** @brief A command of the SERVO screen, as the servo link takes it. */
typedef struct {
    servo_cmd_kind_t kind;     /**< what the command asks */
    uint16_t         value_us; /**< pulse width in microseconds, for a position */
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
     * it to the IOmcu's SERVO page, which runs the PWM target at it:
     * every PWM output whose first channel is a surface, or from protocol
     * 4.19 the one programming port the target names; see
     * servo_screen_rate().
     */
    uint16_t         frame_hz;
    /** @brief SERVO_CMD_SWEEP's curve, as the SERVO page takes it
     *        (servo_sweep.h): the curve, thousandths of a cycle a second,
     *        command units either side of the centre and the hold at each end.
     *
     * Zero otherwise.
     */
    uint16_t         sweep_kind, sweep_mhz, sweep_span, sweep_dwell_ms;
    /** @brief This command ends a sweep the screen was running: a HOLD, a
     *        finger on the dial, CENTRE.
     *
     * The panel gives it way at once over sweep writes already on the wire or
     * queued.
     */
    bool             ends_sweep;
    /** @brief SERVO_CMD_SWEEP carrying on the sweep PAUSE held, from where it
     *        was held: the panel asks the IOmcu to resume it (protocol 4.6),
     *        and an older one, or one that refuses, starts the curve over.
     */
    bool             resume;
    /** @brief SERVO_CMD_HOLD: which pause it is, given back with its
     *        acknowledgement in servo_screen_sweep_held().
     *
     * Never 0.
     */
    uint16_t         pause_seq;
    /** @brief The pause this command derives from -- a resume of it, a repeat
     *        of that resume, a position said again under a changed profile --
     *        or 0: what the panel drops once it has let that pause go
     *        (servo_cmd_stale()).
     */
    uint16_t         from_pause;
    /** @brief SERVO_CMD_SWEEP: which sweep command it is, given back with the
     *        acknowledgement of a start in servo_screen_sweep_started().
     */
    uint16_t         start_seq;
} servo_cmd_t;

/** @brief What became of a frame rate the panel wrote to the SERVO page. */
typedef enum {
    SERVO_RATE_UNSENT = 0,  /**< not written yet: it goes with a position   */
    SERVO_RATE_IN_FORCE,    /**< the target the IOmcu holds runs at
                                 it (servo_in_force_note())                 */
    SERVO_RATE_REFUSED,     /**< a pulse longer than the frame, or a pin
                                 sharing a PWM slice with an output at
                                 another rate; the pins kept theirs         */
    SERVO_RATE_UNSUPPORTED, /**< the IOmcu has no SERVO page: protocol
                                 4.0 runs each slot at its binding's rate   */
    SERVO_RATE_IN_FORCE_SURFACES, /**< every PWM surface runs at it,
                                       whatever the target: protocol 4.1 to
                                       4.18                                 */
} servo_rate_state_t;

/**
 * @brief The state an acknowledged rate is shown in, by the IOmcu's protocol
 *        minor @p minor: SERVO_RATE_IN_FORCE from LINK_MINOR_SERVO_TARGET_RATE
 *        (4.19), where the rate reaches the PWM TARGET alone, and
 *        SERVO_RATE_IN_FORCE_SURFACES below it.
 *
 * @param minor IOmcu protocol minor version, as acknowledged.
 * @return SERVO_RATE_IN_FORCE for @p minor at 4.19 or above,
 *         SERVO_RATE_IN_FORCE_SURFACES below it. No other state is returned.
 */
servo_rate_state_t servo_rate_in_force(uint16_t minor);

/**
 * @brief Whether a command kept to be said again may still be said after the
 *        link has gone and come back.
 *
 * A sweep or a hold may not: both end with the link, on the screen and at the
 * far end, and said again they would start motion nobody asked for.  A position
 * is what the screen still shows.
 *
 * @param c The command to test. NULL is refused.
 * @return true for any kind other than a sweep or a hold, including NONE;
 *         false for a sweep, a hold and NULL.
 */
bool servo_cmd_survives_link_loss(const servo_cmd_t *c);

/**
 * @brief The pause a HOLD @p c ends when the panel lets it go: the pause its
 *        chain rests on (from_pause) when it was posted before an earlier
 *        pause's resume was acknowledged, and otherwise its own.
 *
 * What the panel records for servo_cmd_stale() and hands to
 * servo_screen_released().
 *
 * @param c The command to test. NULL is refused.
 * @return The pause sequence number the command rests on, from_pause when it
 *         is set and pause_seq otherwise. 0 for NULL.
 */
uint16_t servo_cmd_pause_root(const servo_cmd_t *c);

/** @brief A pause the panel has let go of, for servo_cmd_stale(). */
typedef struct {
    bool     on;
    uint16_t pause_seq;
} servo_pause_end_t;

/**
 * @brief Whether the drive command @p c (a position, a centre, a sweep or a
 *        hold) was asked during the pause @p e records as let go of, so is
 *        stale and is not to be sent: the screen has ended that pause, and the
 *        command would hold or move the servo somewhere it no longer shows.
 *
 * The screen asks nothing more of a pause it has left, and commands keep their
 * order, so the first drive command from after the pause retires @p e; a pause
 * number reused after 65535 more is then not mistaken for it.
 *
 * @param e The pause record. Cleared (on = false) when @p c is a drive command
 *          that is not stale. Not changed when @p c is stale or is not a drive
 *          command.
 * @param c The command to test.
 * @return true when @p c is stale and must not be sent, false otherwise,
 *         including when @p e or @p c is NULL or @p e is off.
 */
bool servo_cmd_stale(servo_pause_end_t *e, const servo_cmd_t *c);

/** @brief Where the IOmcu's output was when it started a sweep. */
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

#ifdef __cplusplus
}
#endif
