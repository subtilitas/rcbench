/**
 * @file
 * @brief When the bench is allowed to be armed, and what the panel must tell
 *        the IOmcu about it.
 *
 * The policy only: no touch driver, no link, no task.  The panel's control
 * loop feeds it what happened and acts on what it returns, and the host suite
 * can then hold the rules that decide whether something spins.
 *
 * The rules, each of which has cost this project a defect:
 *
 *   - A stop latches.  Only an explicit arm clears it.
 *   - The heartbeat is suppressed while a stop is latched, and the
 *     IOmcu refuses to arm while the heartbeat is not trusted.  An arm
 *     therefore clears the latch FIRST and waits for the line to settle
 *     before the write, or the two conditions deadlock each other.
 *   - A stop during that wait wins.  The arm is abandoned, not queued.
 *   - With a far end to ask, the wait ends when the far end reports the
 *     line trusted and not when a fixed time is up: the monitor needs five
 *     edges, the fifth is 100 to 125ms after the latch clears at a 5ms
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
 *   - An arm covers the roles it was asked for.  Asking for other roles
 *     while armed passes through a disarm: ARMING_ACT_DISARM first, then
 *     ARMING_ACT_ARM for the new roles after the settle, and a new run.
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
 * @brief How long the touch controller may go without answering before the
 *        bench is disarmed and refused an arm.
 */
#define ARMING_TOUCH_DEAD_MS 500u

/**
 * @brief How long past the settle the panel waits for the far end to report the
 *        line trusted before it gives the arm up: 200ms, 300ms from the hold
 *        with the 100ms settle.
 *
 * The monitor needs five edges.  An edge is emitted on the first pass at or
 * after HEARTBEAT_PERIOD_MS (20ms) from the last, so five take 100ms at a
 * 5ms pass, 135ms at 9ms and 300ms at a pass of 30ms, six times the
 * control task's period.
 */
#define ARMING_LINE_WAIT_MS 200u

/**
 * @brief What an exchange with the far end that ended unanswered means; see
 *        arming_exchange_unanswered().
 */
typedef enum {
    /**
     * @brief The link was already down: a probe nobody answered.
     *
     * Nothing.
     */
    ARMING_QUIET_NOTHING = 0,
    /**
     * @brief The link is gone.
     *
     * The caller sends nothing more on it and takes it down at its next poll.
     */
    ARMING_QUIET_LINK_DOWN,
    /**
     * @brief The link is gone under an armed bench or a waiting arm: a stop is
     *        latched as well, and the caller disarms its bank and zeroes the
     *        command now.
     */
    ARMING_QUIET_STOP,
} arming_quiet_t;

/** @brief What the caller must put on the link, if anything, after a step. */
typedef enum {
    ARMING_ACT_NONE = 0,
    ARMING_ACT_DISARM,   /**< write ARM = 0                          */
    ARMING_ACT_ARM,      /**< write CLEAR, then ARM = arming_roles() */
    /**
     * @brief The far end did not come to trust the line within the bound: the
     *        arm is given up, nothing is written, the operator is told.
     */
    ARMING_ACT_GIVE_UP,
} arming_action_t;

typedef struct {
    bool     armed;
    bool     stopped;         /**< the latch                              */
    /**
     * @brief How many times the bench has been stopped, by any means: a STOP,
     *        the far end, or touch that stopped answering.
     *
     * The latch is a level and says only that a stop is in force; a screen
     * needs the event. A second STOP during a hold that began after the
     * first one leaves the latch exactly as it was, and the hold would
     * otherwise complete and clear it. Touch dying is not a latch at all,
     * and it invalidates a gesture just as much.
     */
    uint32_t stops;
    /**
     * @brief Of those, the ones an operator pressed: counted in the same call
     *        as the stop, so a reader that sees a stop sees its press with it.
     *
     * What is left -- touch dead or lost, the far end -- is the bench's own.
     */
    uint32_t pressed;
    bool     touch_was_dead;  /**< so touch dying is counted once, not per pass */
    /**
     * @brief A disarm owed to something the policy saw between steps.
     *
     * Touch can die and answer again inside one blocking link exchange, and
     * the bank must not survive that: by the time the policy next runs the
     * controller is healthy, so nothing in the state would say the outage
     * happened, and the heartbeat need not have been withheld for long
     * enough for the far end to fail safe either.
     */
    bool     disarm_pending;
    bool     arming;          /**< an arm is waiting for the line         */
    /**
     * @brief The roles of the arm in force or waiting, the caller's bits; see
     *        arming_request_arm_roles().
     */
    uint8_t  roles;
    /**
     * @brief A request for other roles found the bench armed: the next step
     *        disarms before the arm for them.
     */
    bool     switching;
    uint32_t settle_ms;       /**< how long the line is given             */
    uint32_t settle_until_ms;           /**< the instant the settle ends, ms */
    /**
     * @brief How long past the settle an arm waits for the far end to report
     *        the line trusted; 0 when there is no far end to ask and the settle
     *        alone decides.
     *
     * See arming_set_line_wait().
     */
    uint32_t line_wait_ms;
    bool     line_trusted;    /**< reported for the arm that is waiting   */
    bool     line_nobody;     /**< no far end in this pass; see
                                   arming_line_nobody()                   */
    /**
     * @brief The deadline of the arm arming_step() last handed out, kept for
     *        the handshake that writes it: settle plus line wait, the instant
     *        the wait for the line ends at.
     *
     * `handshake_bounded` is false for an arm that asked no far end.  See
     * arming_handshake_open().
     */
    uint32_t handshake_until_ms;
    bool     handshake_bounded;
    uint32_t last_touch_ms;
    bool     in_run;          /**< a run is being timed                   */
    uint32_t run_start_ms;    /**< its start; any tick, 0 included; read
                                   only while in_run                      */
    uint32_t run_seconds;     /**< held after the run ends                */
} arming_t;

/**
 * @brief @p settle_ms is how long the heartbeat is given to become trustworthy
 *        before an arm is written: HEARTBEAT_GOOD_RUN intervals, plus one.
 *
 * @param a Policy state to initialise.
 * @param now_ms Current time, in milliseconds.
 * @param settle_ms Time the heartbeat is given to become trusted, in
 *        milliseconds.
 */
void arming_init(arming_t *a, uint32_t now_ms, uint32_t settle_ms);

/**
 * @brief Make an arm wait for the far end's word on the line, for at most @p
 *        wait_ms past the settle.  0 turns the wait off.
 *
 * From the settle to the bound arming_line_wanted() is true on every pass
 * until arming_line_report() has said the line is trusted; arming_step()
 * then returns ARMING_ACT_ARM in that pass.  At the bound without a yes,
 * and past the bound whatever was reported, it returns ARMING_ACT_GIVE_UP:
 * the report is an exchange that can come back late, and a yes that does
 * arms nothing.
 *
 * @param a Policy state.
 * @param wait_ms Bound past the settle, in milliseconds; 0 turns the wait off.
 */
void arming_set_line_wait(arming_t *a, uint32_t wait_ms);

/**
 * @brief Whether the caller is to ask the far end about the line in this pass.
 *
 * @param a Policy state.
 * @param now_ms Current time, in milliseconds.
 * @return true when the caller is to ask the far end in this pass.
 */
bool arming_line_wanted(const arming_t *a, uint32_t now_ms);

/**
 * @brief What the far end said: @p trusted when its monitor believes the line.
 *
 * A question nobody answered is reported as false.  With no far end the caller
 * reports true.  Ignored unless an arm is waiting.
 *
 * @param a Policy state.
 * @param trusted true when the far end's monitor believes the line.
 */
void arming_line_report(arming_t *a, bool trusted);

/**
 * @brief There is no far end to ask in this pass: the next arming_step() lets
 *        the settle alone decide, bound or no bound.
 *
 * Nothing can be stale where nothing was asked, and with no link the pass
 * itself can be a second late behind an identity probe nobody answers.  Holds
 * for that one step; a caller with no link says so before every step.
 *
 * @param a Policy state.
 */
void arming_line_nobody(arming_t *a);

/**
 * @brief The touch controller answered.
 *
 * @param a Policy state.
 * @param now_ms Current time, in milliseconds.
 */
void arming_touch_seen(arming_t *a, uint32_t now_ms);

/**
 * @brief True once touch has been silent for ARMING_TOUCH_DEAD_MS.
 *
 * @param a Policy state.
 * @param now_ms Current time, in milliseconds.
 * @return true when touch has been silent for ARMING_TOUCH_DEAD_MS.
 */
bool arming_touch_dead(const arming_t *a, uint32_t now_ms);

/**
 * @brief Whether the stop latch is set: a STOP, a dead touch, or the far end.
 *
 * A screen asks so a gesture already under way can be abandoned.
 *
 * @param a Policy state.
 * @return true when the stop latch is set.
 */
bool arming_stopped(const arming_t *a);

/**
 * @brief How many stops have been applied.
 *
 * Changes on every stop, latched or not, so a caller can act on the event
 * rather than on the level.
 *
 * @param a Policy state.
 * @return The number of stops applied since arming_init().
 */
uint32_t arming_stop_count(const arming_t *a);

/**
 * @brief A STOP an operator pressed: arming_stop(), counted as pressed too.
 *
 * @param a Policy state.
 */
void arming_stop_pressed(arming_t *a);

/**
 * @brief How many of the stops were pressed (arming_stop_pressed()); never more
 *        than arming_stop_count() has counted since arming_init().
 *
 * @param a Policy state.
 * @return The number of stops pressed by an operator.
 */
uint32_t arming_pressed_count(const arming_t *a);

/**
 * @brief Sample the touch controller's health, and act on the edge where it
 *        dies.
 *
 * Called by whatever judges touch, as often as it judges it, and by
 * arming_step(). The two are not the same caller and need not run at the
 * same rate: a link exchange can wait a second while touch is still being
 * pumped, and a controller that died and recovered inside that wait would
 * leave nothing for the policy to see afterwards.
 *
 * @param a Policy state.
 * @param now_ms Current time, in milliseconds.
 */
void arming_touch_poll(arming_t *a, uint32_t now_ms);

/**
 * @brief STOP.
 *
 * Latches; abandons an arm that is waiting for the line.
 *
 * @param a Policy state.
 */
void arming_stop(arming_t *a);

/** @brief Every role: what arming_request_arm() asks for. */
#define ARMING_ROLES_ALL 0xFFu

/**
 * @brief The operator asked to arm every role: arming_request_arm_roles() with
 *        ARMING_ROLES_ALL.
 *
 * @param a Policy state.
 * @param now_ms Current time, in milliseconds.
 */
void arming_request_arm(arming_t *a, uint32_t now_ms);

/**
 * @brief The operator asked to arm the outputs of @p roles, a non-zero set of
 *        the caller's role bits; 0 is ignored.
 *
 * Ignored while touch is dead.
 *
 * Asked while armed for other roles, the bench is not re-armed in place: the
 * next arming_step() returns ARMING_ACT_DISARM, and the arm for @p roles
 * follows the settle as any arm does.  The outputs of the old roles have
 * stopped before those of the new ones start, and the run clock starts a
 * new run.  A STOP in between wins, as it does over any arm that waits.
 *
 * @param a Policy state.
 * @param now_ms Current time, in milliseconds.
 * @param roles Non-zero set of the caller's role bits; 0 is ignored.
 */
void arming_request_arm_roles(arming_t *a, uint32_t now_ms, uint8_t roles);

/**
 * @brief The roles of the arm in force or waiting: 0 before any request.
 *
 * @param a Policy state.
 * @return The roles of the arm in force or waiting.
 */
uint8_t arming_roles(const arming_t *a);

/**
 * @brief The operator asked to disarm.
 *
 * @param a Policy state.
 */
void arming_request_disarm(arming_t *a);

/**
 * @brief The IOmcu refused the arm.
 *
 * The latch stays clear: a heartbeat that is running is the truth about a loop
 * that is running, and the operator can ask again.
 *
 * @param a Policy state.
 */
void arming_refused(arming_t *a);

/**
 * @brief Whether the arm arming_step() handed out may still write its next
 *        frame, asked before CLEAR (@p cleared false) and before the frame that
 *        arms (@p cleared true, the CLEAR acknowledged).
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
 *     line, within HEARTBEAT_MAX_GAP_MS (150ms).
 *
 * Also false, with nothing changed, when no arm is in hand.  Always true
 * for an arm that asked no far end.  What it cannot bound is the frame that
 * arms itself: one acknowledged late has armed the far end, at rest.
 *
 * @param a Policy state.
 * @param now_ms Current time, in milliseconds.
 * @param cleared true once CLEAR has been acknowledged.
 * @return true up to and at the deadline; false once it has passed or no arm is
 *         in hand.
 */
bool arming_handshake_open(arming_t *a, uint32_t now_ms, bool cleared);

/**
 * @brief The CLEAR or the frame that arms did not come back acknowledged.
 *
 * @p answered: the far end answered and refused, which is arming_refused().
 * Not answered, the exchange has waited out its timeout and the link has
 * gone quiet under an arm: a stop, as arming_link_lost() makes one.  The far
 * end may have taken the frame and lost only the acknowledgement, so it may
 * be armed; the stop withholds the heartbeat from it.
 *
 * Returns true when it stopped, with arming_link_lost()'s duty for the
 * caller.
 *
 * @param a Policy state.
 * @param answered true when the far end answered with a refusal.
 * @return true when the arm was stopped.
 */
bool arming_write_failed(arming_t *a, bool answered);

/**
 * @brief Where the panel is in writing the arm arming_step() handed out with
 *        ARMING_ACT_ARM; see arming_handshake_step().
 */
typedef enum {
        /** @brief Just handed out: nothing done for it yet. */
    ARMING_HS_HANDED = 0,
        /** @brief The command is zero and the surfaces are asked to centre. */
    ARMING_HS_PREPARED,
    /**
     * @brief CLEAR came back acknowledged.
     *
     * A value outside this enum takes this point's rules.
     */
    ARMING_HS_CLEARED,
    /**
     * @brief The frame that arms came back acknowledged: the far end is armed.
     */
    ARMING_HS_ARMED,
} arming_hs_at_t;

/** @brief What the panel learned for the arm since the last point. */
typedef struct {
    bool link_up;       /**< the control loop holds the link as up        */
    bool port_taken;    /**< the servo programmer has a port or an
                             operation under way                           */
    bool release_owed;  /**< a servo release the far end has not taken    */
    /**
     * @brief The rate the surfaces run at is known.
     *
     * Asking can take an exchange; read only at ARMING_HS_PREPARED with the
     * link up and no release owed, and the caller need not ask otherwise.
     */
    bool rate_settled;
    bool disarm_asked;  /**< a disarm is posted and not yet delivered     */
    /**
     * @brief The throttle's pulse endpoints are not known to be in force at the
     *        far end: owed (an edit, a binding or a link-up not yet paid, or a
     *        write nobody answered) or blocked by the last answer (refused, or
     *        another pair stored); see endpoints_block().
     *
     * The frame that arms carries no CHAN_CFG.
     */
    bool endpoints_blocked;
} arming_hs_facts_t;

/**
 * @brief What the panel does next with the arm, after arming_handshake_step().
 */
typedef enum {
    /**
     * @brief Go on: at ARMING_HS_HANDED prepare, at ARMING_HS_PREPARED write
     *        CLEAR, at ARMING_HS_CLEARED write the frame that arms.
     */
    ARMING_HS_GO = 0,
    /**
     * @brief No link: arm this end's bank for arming_roles(), nothing written.
     */
    ARMING_HS_LOCAL,
    /**
     * @brief The servo programmer holds the port: given up as arming_refused()
     *        gives one up, nothing written.
     */
    ARMING_HS_PROG,
    /**
     * @brief A servo release the far end has not taken: the arm fails as a
     *        write that did not go through, nothing written.
     *
     * The policy is unchanged;  the caller ends the arm as
     * arming_write_failed() or arming_link_lost() does, by whether the link
     * went quiet.
     */
    ARMING_HS_SERVO_HELD,
    /**
     * @brief The surfaces' rate is not known; the same as ARMING_HS_SERVO_HELD.
     */
    ARMING_HS_SERVO_RATE,
    /**
     * @brief The throttle's pulse endpoints are not in force at the far end.
     *
     * At  ARMING_HS_PREPARED the same as ARMING_HS_SERVO_HELD; at
     * ARMING_HS_CLEARED given up as arming_refused() gives one up, and the
     * frame that arms is not written; at ARMING_HS_ARMED the arm is taken back
     * as arming_request_disarm() takes one back, and the caller writes the
     * disarm at once.
     */
    ARMING_HS_ENDPOINTS,
    /**
     * @brief Past the arm's deadline: given up as arming_handshake_open() gives
     *        it up, before CLEAR as a refusal and after it as a stop.
     */
    ARMING_HS_CLOSED,
    /**
     * @brief A stop or a disarm landed while CLEAR was on the wire: given up as
     *        arming_refused() gives one up.
     *
     * The operator asked for it.
     */
    ARMING_HS_WITHDRAWN,
} arming_hs_t;

/**
 * @brief The next step of writing the arm arming_step() handed out, at @p at
 *        with what the panel learned in @p f.
 *
 *     HANDED     port taken                      ARMING_HS_PROG
 *                otherwise                       ARMING_HS_GO
 *     PREPARED   link down                       ARMING_HS_LOCAL
 *                release owed                    ARMING_HS_SERVO_HELD
 *                rate not settled                ARMING_HS_SERVO_RATE
 *                endpoints blocked               ARMING_HS_ENDPOINTS
 *                deadline passed                 ARMING_HS_CLOSED
 *                otherwise                       ARMING_HS_GO, write CLEAR
 *     CLEARED    stopped, or a disarm asked      ARMING_HS_WITHDRAWN
 *                endpoints blocked               ARMING_HS_ENDPOINTS
 *                deadline passed                 ARMING_HS_CLOSED
 *                otherwise                       ARMING_HS_GO, write ARM
 *     ARMED      endpoints blocked               ARMING_HS_ENDPOINTS
 *                otherwise                       ARMING_HS_GO
 *
 * The verdicts that end the arm change the policy as their comments say;
 * ARMING_HS_GO and ARMING_HS_LOCAL change nothing.  The CLEAR and the frame
 * that arms can still fail on the wire; arming_write_failed() and the
 * caller's own rule for a link gone quiet decide those.
 *
 * @param a Policy state.
 * @param at Point the arm has reached.
 * @param f What the panel learned for the arm.
 * @param now_ms Current time, in milliseconds.
 * @return What the panel does next with the arm.
 */
arming_hs_t arming_handshake_step(arming_t *a, arming_hs_at_t at,
                                  const arming_hs_facts_t *f,
                                  uint32_t now_ms);

/**
 * @brief Whether a stop has been applied that the caller has not let go for
 *        yet: true once for every change of arming_stop_count() from @p
 *        *served, which it then takes.
 *
 * Every stop counts, latched or not, armed or not, and two stops between calls
 * are let go for once.
 *
 * @param a Policy state.
 * @param served Stop count last let go; updated when true is returned.
 * @return true when a stop applied since @p served is owed to the caller.
 */
bool arming_stop_unserved(const arming_t *a, uint32_t *served);

/**
 * @brief The IOmcu disarmed us: a NACK on a control write, or a failsafe.
 *
 * @param a Policy state.
 */
void arming_stop_from_far_end(arming_t *a);

/**
 * @brief An exchange with the far end ended with no answer: it waited out its
 *        timeout, or its frame did not reach the wire.
 *
 * A refusal is an answer and is not reported here.
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
 *
 * @param a Policy state.
 * @param link_up true when the link was up when the exchange was sent.
 * @param bank_armed true when this end's bank is armed.
 * @return What the caller does about the unanswered exchange.
 */
arming_quiet_t arming_exchange_unanswered(arming_t *a, bool link_up,
                                          bool bank_armed);

/**
 * @brief Whether a link going quiet now would be a stop: this end's bank is
 *        armed (@p bank_armed), the policy is armed, or an arm is waiting.
 *
 * What arming_link_lost() acts on; a caller asks it to decide whether an
 * unanswered exchange is worth taking the link down for at once.
 *
 * @param a Policy state.
 * @param bank_armed true when this end's bank is armed.
 * @return true when a link going quiet now would be a stop.
 */
bool arming_link_needed(const arming_t *a, bool bank_armed);

/**
 * @brief The far end stopped answering.
 *
 * @p bank_armed is whether this end's bank is armed.  An armed bench and an arm
 * that is waiting are stopped as arming_stop_from_far_end() stops them; a
 * disarmed bench is left as it is.
 *
 * Returns true when it stopped: the caller then disarms its bank and zeroes
 * the command, because the policy's own disarm is gated on `armed`, which
 * the stop has already cleared.
 *
 * @param a Policy state.
 * @param bank_armed true when this end's bank is armed.
 * @return true when it stopped, and the caller disarms its bank.
 */
bool arming_link_lost(arming_t *a, bool bank_armed);

/**
 * @brief A far end started answering.
 *
 * A bank armed with no link -- the simulator's -- is stopped the same way: the
 * far end was never asked to arm, and the ARM written at every poll would ask
 * it now.  An arm still waiting is left to complete: it writes CLEAR and the
 * frame that arms, as an arm made with the link up does.
 *
 * Returns true when it stopped, with the same duty for the caller.
 *
 * @param a Policy state.
 * @param bank_armed true when this end's bank is armed.
 * @return true when it stopped, and the caller disarms its bank.
 */
bool arming_link_found(arming_t *a, bool bank_armed);

/**
 * @brief Advance, and say what the link owes the IOmcu.
 *
 * @param a Policy state.
 * @param now_ms Current time, in milliseconds.
 * @return The action the caller must put on the link.
 */
arming_action_t arming_step(arming_t *a, uint32_t now_ms);

/**
 * @brief Whether the heartbeat may edge.
 *
 * @param a Policy state.
 * @param now_ms Current time, in milliseconds.
 * @return true when the heartbeat may edge.
 */
bool arming_heartbeat(const arming_t *a, uint32_t now_ms);

/**
 * @brief Seconds of the current run, or of the last one once it has ended.
 *
 * @param a Policy state.
 * @return Seconds of the current run, or of the last one.
 */
uint32_t arming_run_seconds(const arming_t *a);

#ifdef __cplusplus
}
#endif
