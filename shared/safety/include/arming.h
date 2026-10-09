/*
 * When the bench is allowed to be armed, and what the panel must tell the
 * coprocessor about it.
 *
 * The policy only: no touch driver, no link, no task.  The panel's control
 * loop feeds it what happened and acts on what it returns, and the host suite
 * can then hold the rules that decide whether something spins.
 *
 * The rules, each of which has cost this project a defect:
 *
 *   - A stop latches.  Only an explicit arm clears it.
 *   - The heartbeat is suppressed while a stop is latched, and the
 *     coprocessor refuses to arm while the heartbeat is not trusted.  An arm
 *     therefore clears the latch FIRST and waits for the line to settle
 *     before the write, or the two conditions deadlock each other.
 *   - A stop during that wait wins.  The arm is abandoned, not queued.
 *   - With a far end to ask, the wait ends when the far end reports the
 *     line trusted and not when a fixed time is up: the monitor needs five
 *     edges, the fifth is 100 to 125 ms after the latch clears at a 5 ms
 *     pass, and later on a slower one.  The wait is bounded; an arm the far
 *     end never trusts is given up before CLEAR is written.
 *   - A link that goes quiet while the bench is armed, or while an arm is
 *     waiting, is a stop.  The far end may have restarted, and an ARM
 *     written again when it answers would arm it with no hand on the panel.
 *     A bank armed with no link is stopped when a far end appears, for the
 *     same reason.
 *   - Touch that has stopped answering disarms and refuses to arm: the panel
 *     is the only place a STOP button exists.
 *   - The run clock times a run, not the panel.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * How long the touch controller may go without answering before the bench is
 * disarmed and refused an arm.
 */
#define ARMING_TOUCH_DEAD_MS 500u

/**
 * How long past the settle the panel waits for the far end to report the
 * line trusted before it gives the arm up: 200 ms, 300 ms from the hold
 * with the 100 ms settle.
 *
 * The monitor needs five edges.  An edge is emitted on the first pass at or
 * after HEARTBEAT_PERIOD_MS (20 ms) from the last, so five take 100 ms at a
 * 5 ms pass, 135 ms at 9 ms and 300 ms at a pass of 30 ms, six times the
 * control task's period.
 */
#define ARMING_LINE_WAIT_MS 200u

/**
 * What an exchange with the far end that ended unanswered means; see
 * arming_exchange_unanswered().
 */
typedef enum {
    /** The link was already down: a probe nobody answered.  Nothing. */
    ARMING_QUIET_NOTHING = 0,
    /** The link is gone.  The caller sends nothing more on it and takes it
     *  down at its next poll. */
    ARMING_QUIET_LINK_DOWN,
    /** The link is gone under an armed bench or a waiting arm: a stop is
     *  latched as well, and the caller disarms its bank and zeroes the
     *  command now. */
    ARMING_QUIET_STOP,
} arming_quiet_t;

/** What the caller must put on the link, if anything, after a step. */
typedef enum {
    ARMING_ACT_NONE = 0,
    ARMING_ACT_DISARM,   /**< write ARM = 0                          */
    ARMING_ACT_ARM,      /**< write CLEAR, then ARM = 1              */
    /** The far end did not come to trust the line within the bound: the arm
     *  is given up, nothing is written, the operator is told. */
    ARMING_ACT_GIVE_UP,
} arming_action_t;

typedef struct {
    bool     armed;
    bool     stopped;         /**< the latch                              */
    /**
     * How many times the bench has been stopped, by any means: a STOP, the
     * far end, or touch that stopped answering.
     *
     * The latch is a level and says only that a stop is in force; a screen
     * needs the event. A second STOP during a hold that began after the
     * first one leaves the latch exactly as it was, and the hold would
     * otherwise complete and clear it. Touch dying is not a latch at all,
     * and it invalidates a gesture just as much.
     */
    uint32_t stops;
    /**
     * Of those, the ones an operator pressed: counted in the same call as
     * the stop, so a reader that sees a stop sees its press with it.  What
     * is left -- touch dead or lost, the far end -- is the bench's own.
     */
    uint32_t pressed;
    bool     touch_was_dead;  /**< so touch dying is counted once, not per pass */
    /**
     * A disarm owed to something the policy saw between steps.
     *
     * Touch can die and answer again inside one blocking link exchange, and
     * the bank must not survive that: by the time the policy next runs the
     * controller is healthy, so nothing in the state would say the outage
     * happened, and the heartbeat need not have been withheld for long
     * enough for the far end to fail safe either.
     */
    bool     disarm_pending;
    bool     arming;          /**< an arm is waiting for the line         */
    uint32_t settle_ms;       /**< how long the line is given             */
    uint32_t settle_until_ms;
    /**
     * How long past the settle an arm waits for the far end to report the
     * line trusted; 0 when there is no far end to ask and the settle alone
     * decides.  See arming_set_line_wait().
     */
    uint32_t line_wait_ms;
    bool     line_trusted;    /**< reported for the arm that is waiting   */
    bool     line_nobody;     /**< no far end in this pass; see
                                   arming_line_nobody()                   */
    /**
     * The deadline of the arm arming_step() last handed out, kept for the
     * handshake that writes it: settle plus line wait, the instant the wait
     * for the line ends at.  `handshake_bounded` is false for an arm that
     * asked no far end.  See arming_handshake_open().
     */
    uint32_t handshake_until_ms;
    bool     handshake_bounded;
    uint32_t last_touch_ms;
    uint32_t run_start_ms;    /**< 0 when not in a run                    */
    uint32_t run_seconds;     /**< held after the run ends                */
} arming_t;

/**
 * @p settle_ms is how long the heartbeat is given to become trustworthy
 * before an arm is written: HEARTBEAT_GOOD_RUN intervals, plus one.
 */
void arming_init(arming_t *a, uint32_t now_ms, uint32_t settle_ms);

/**
 * Make an arm wait for the far end's word on the line, for at most
 * @p wait_ms past the settle.  0 turns the wait off.
 *
 * From the settle to the bound arming_line_wanted() is true on every pass
 * until arming_line_report() has said the line is trusted; arming_step()
 * then returns ARMING_ACT_ARM in that pass.  At the bound without a yes,
 * and past the bound whatever was reported, it returns ARMING_ACT_GIVE_UP:
 * the report is an exchange that can come back late, and a yes that does
 * arms nothing.
 */
void arming_set_line_wait(arming_t *a, uint32_t wait_ms);

/** Whether the caller is to ask the far end about the line in this pass. */
bool arming_line_wanted(const arming_t *a, uint32_t now_ms);

/**
 * What the far end said: @p trusted when its monitor believes the line.  A
 * question nobody answered is reported as false.  With no far end the
 * caller reports true.  Ignored unless an arm is waiting.
 */
void arming_line_report(arming_t *a, bool trusted);

/**
 * There is no far end to ask in this pass: the next arming_step() lets the
 * settle alone decide, bound or no bound.  Nothing can be stale where
 * nothing was asked, and with no link the pass itself can be a second late
 * behind an identity probe nobody answers.  Holds for that one step; a
 * caller with no link says so before every step.
 */
void arming_line_nobody(arming_t *a);

/** The touch controller answered. */
void arming_touch_seen(arming_t *a, uint32_t now_ms);

/** True once touch has been silent for ARMING_TOUCH_DEAD_MS. */
bool arming_touch_dead(const arming_t *a, uint32_t now_ms);

/** Whether the stop latch is set: a STOP, a dead touch, or the far end.
 *  A screen asks so a gesture already under way can be abandoned. */
bool arming_stopped(const arming_t *a);

/** How many stops have been applied. Changes on every stop, latched or not,
 *  so a caller can act on the event rather than on the level. */
uint32_t arming_stop_count(const arming_t *a);

/** A STOP an operator pressed: arming_stop(), counted as pressed too. */
void arming_stop_pressed(arming_t *a);

/** How many of the stops were pressed (arming_stop_pressed()); never more
 *  than arming_stop_count() has counted since arming_init(). */
uint32_t arming_pressed_count(const arming_t *a);

/**
 * Sample the touch controller's health, and act on the edge where it dies.
 *
 * Called by whatever judges touch, as often as it judges it, and by
 * arming_step(). The two are not the same caller and need not run at the
 * same rate: a link exchange can wait a second while touch is still being
 * pumped, and a controller that died and recovered inside that wait would
 * leave nothing for the policy to see afterwards.
 */
void arming_touch_poll(arming_t *a, uint32_t now_ms);

/** STOP. Latches; abandons an arm that is waiting for the line. */
void arming_stop(arming_t *a);

/** The operator asked to arm. Ignored while touch is dead. */
void arming_request_arm(arming_t *a, uint32_t now_ms);

/** The operator asked to disarm. */
void arming_request_disarm(arming_t *a);

/**
 * The coprocessor refused the arm. The latch stays clear: a heartbeat that
 * is running is the truth about a loop that is running, and the operator can
 * ask again.
 */
void arming_refused(arming_t *a);

/**
 * Whether the arm arming_step() handed out may still write its next frame,
 * asked before CLEAR (@p cleared false) and before the frame that arms
 * (@p cleared true, the CLEAR acknowledged).
 *
 * The arm's deadline is the one the wait for the line ends at, settle plus
 * line wait from the hold.  The exchanges of the handshake are answered in
 * about a millisecond each and can be answered up to the exchange timeout
 * late; the deadline is what keeps a slow one from arming the bench that
 * much after the hold.  True up to and at the deadline.  Past it the arm is
 * given up and false is returned; the caller writes nothing further and
 * tells the operator, who holds ARM again:
 *
 *   - before CLEAR, as arming_refused() gives one up.  Nothing was written
 *     and the far end's latch stands;
 *   - after CLEAR, as a stop from the far end.  The CLEAR released the far
 *     end's latch and no frame can set it again, so the stop withholds the
 *     heartbeat, and the far end sets its latch when it stops trusting the
 *     line, within HEARTBEAT_MAX_GAP_MS (150 ms).
 *
 * Also false, with nothing changed, when no arm is in hand.  Always true
 * for an arm that asked no far end.  What it cannot bound is the frame that
 * arms itself: one acknowledged late has armed the far end, at rest.
 */
bool arming_handshake_open(arming_t *a, uint32_t now_ms, bool cleared);

/**
 * The CLEAR or the frame that arms did not come back acknowledged.
 *
 * @p answered: the far end answered and refused, which is arming_refused().
 * Not answered, the exchange has waited out its timeout and the link has
 * gone quiet under an arm: a stop, as arming_link_lost() makes one.  The far
 * end may have taken the frame and lost only the acknowledgement, so it may
 * be armed; the stop withholds the heartbeat from it.
 *
 * Returns true when it stopped, with arming_link_lost()'s duty for the
 * caller.
 */
bool arming_write_failed(arming_t *a, bool answered);

/** The coprocessor disarmed us: a NACK on a control write, or a failsafe. */
void arming_stop_from_far_end(arming_t *a);

/**
 * An exchange with the far end ended with no answer: it waited out its
 * timeout, or its frame did not reach the wire.  A refusal is an answer and
 * is not reported here.
 *
 * The one decision for every exchange, whatever it carried and wherever in
 * a pass it was sent.  @p link_up is whether the caller held the link as up
 * when it sent it; @p bank_armed whether its bank is armed.
 *
 *     link down                      ARMING_QUIET_NOTHING
 *     link up, disarmed              ARMING_QUIET_LINK_DOWN
 *     link up, armed or arming       ARMING_QUIET_STOP, the stop latched as
 *                                    arming_link_lost() latches it
 *
 * With the link down the exchange was a probe, and a bank armed with no far
 * end -- the simulated bench -- runs on through probes nobody answers.
 */
arming_quiet_t arming_exchange_unanswered(arming_t *a, bool link_up,
                                          bool bank_armed);

/**
 * Whether a link going quiet now would be a stop: this end's bank is armed
 * (@p bank_armed), the policy is armed, or an arm is waiting.  What
 * arming_link_lost() acts on; a caller asks it to decide whether an
 * unanswered exchange is worth taking the link down for at once.
 */
bool arming_link_needed(const arming_t *a, bool bank_armed);

/**
 * The far end stopped answering.  @p bank_armed is whether this end's bank
 * is armed.  An armed bench and an arm that is waiting are stopped as
 * arming_stop_from_far_end() stops them; a disarmed bench is left as it is.
 *
 * Returns true when it stopped: the caller then disarms its bank and zeroes
 * the command, because the policy's own disarm is gated on `armed`, which
 * the stop has already cleared.
 */
bool arming_link_lost(arming_t *a, bool bank_armed);

/**
 * A far end started answering.  A bank armed with no link -- the
 * simulator's -- is stopped the same way: the far end was never asked to
 * arm, and the ARM written at every poll would ask it now.  An arm still
 * waiting is left to complete: it writes CLEAR and the frame that arms, as
 * an arm made with the link up does.
 *
 * Returns true when it stopped, with the same duty for the caller.
 */
bool arming_link_found(arming_t *a, bool bank_armed);

/** Advance, and say what the link owes the coprocessor. */
arming_action_t arming_step(arming_t *a, uint32_t now_ms);

/** Whether the heartbeat may edge. */
bool arming_heartbeat(const arming_t *a, uint32_t now_ms);

/** Seconds of the current run, or of the last one once it has ended. */
uint32_t arming_run_seconds(const arming_t *a);

#ifdef __cplusplus
}
#endif
