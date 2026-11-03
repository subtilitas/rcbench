/**
 * @file
 * @brief The reset of the panel's STOP: from a hold of STOP to the heartbeat
 *        sent again, with the bench SAFE throughout.
 *
 * STOP works as an emergency stop button.  One touch latches it
 * (arming_stop_pressed(): arming_held() true, the heartbeat withheld) and
 * nothing switches on while it stands: arming_request_arm_roles() takes no
 * arm, and the panel refuses every ON.  A hold of STOP for UI_HOLD_S (2s)
 * asks for the reset; this module decides when it is done:
 *
 *     hold of STOP completes     stop_latch_ask(): the reset waits
 *     the IOmcu confirms         STOP_LATCH_RESUME: arming_stop_release(),
 *     everything off, and at     heartbeat sent again, CLEAR written once,
 *     least STOP_LATCH_FLOOR_MS  the bench stays SAFE
 *     since the last STOP
 *     no confirmation within     STOP_LATCH_GIVE_UP: the latch stands, the
 *     STOP_LATCH_CONFIRM_MS      heartbeat stays withheld, the operator is
 *                                told why
 *     a STOP or an error while   the reset is withdrawn; the latch stands
 *     it waits
 *
 * "Everything off" is the caller's reading of the IOmcu's pages, made after
 * the reset was asked (STATUS not ARMED, the SUPPLY output off, the
 * PROG_SUPPLY output off) and of its own bank.  With no link there is no
 * IOmcu to ask: the panel's own bank and model are the reading, and the
 * IOmcu, if one is wired, has failed safe on link silence.
 *
 * The floor makes sure the IOmcu has seen the line go quiet: its monitor
 * doubts the line after HEARTBEAT_MAX_GAP_MS (150ms) and latches its arm
 * latch on that edge.  A heartbeat sent again before that edge would be one
 * the far end never stopped trusting.
 *
 * Nothing persists: a panel that restarts starts unlatched, with every
 * output off and the bench SAFE, as at any start.
 *
 * Pure C with the clock passed in.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The least time from the last STOP to the heartbeat sent again: 500ms. */
#define STOP_LATCH_FLOOR_MS 500u

/** @brief How long a reset waits for the IOmcu to confirm: 2000ms. */
#define STOP_LATCH_CONFIRM_MS 2000u

/** @brief How often the confirmation is read while a reset waits: 50ms. */
#define STOP_LATCH_ASK_MS 50u

/**
 * @brief After a reset, how long the band keeps LINK_FAULT_HEARTBEAT hidden:
 *        1000ms.
 *
 * The IOmcu trusts the line again after HEARTBEAT_GOOD_RUN good intervals,
 * 80 to 100ms, and the panel reads STATUS every 500ms; the bit read while
 * the line was withheld would otherwise show as FAULT 10 for up to 600ms.
 */
#define STOP_LATCH_GRACE_MS 1000u

/** @brief The reset's state. */
typedef struct {
    bool     waiting;      /**< a reset is asked and not yet done         */
    uint32_t pressed_seen; /**< arming_pressed_count() at the last look   */
    uint32_t stop_ms;      /**< when the last STOP was seen               */
    bool     stop_known;   /**< a STOP was seen since stop_latch_init()   */
    uint32_t ask_ms;       /**< when the reset was asked                  */
    bool     read_any;     /**< a confirmation was read in this wait      */
    uint32_t read_ms;      /**< when the last one was read                */
    bool     grace;        /**< grace_ms means something                  */
    uint32_t grace_ms;     /**< when the last reset was done              */
    uint32_t resets;       /**< resets done: CLEARs owed, one each        */
} stop_latch_t;

/** @brief What the caller does after stop_latch_step(). */
typedef enum {
    STOP_LATCH_NONE = 0,
    /**
     * @brief Confirmed: arming_stop_release(), the heartbeat goes again, and
     *        CLEAR is written once with the link up.  The bench stays SAFE.
     */
    STOP_LATCH_RESUME,
    /**
     * @brief No confirmation within STOP_LATCH_CONFIRM_MS: the latch and the
     *        withheld heartbeat stand, and the operator is told why.
     */
    STOP_LATCH_GIVE_UP,
} stop_latch_act_t;

/** @brief What the caller read for the confirmation, in one pass. */
typedef struct {
    bool bank_armed;   /**< this end's bank is armed                     */
    bool supply_on;    /**< the supply's output on, or an ON on its way  */
    bool prog_on;      /**< the ports' own 5V on, owed on, or an ON on its
                            way                                          */
    bool port_held;    /**< a programming port is held; stopped, it drives
                            nothing, and it does not count               */
    bool link_up;      /**< the link to the IOmcu answers                */
    bool far_read;     /**< every page below was read and answered       */
    bool far_armed;    /**< STATUS reads ARMED                           */
    bool far_supply;   /**< SUPPLY reads its output on                   */
    bool far_prog;     /**< PROG_SUPPLY reads its output on              */
} stop_latch_reading_t;

/**
 * @brief Whether a reading confirms everything off: nothing on at this end
 *        and, with the link up, every page read and reading off.  A held
 *        programming port is not an output; a session stopped under STOP
 *        stays open across the reset.
 * @param r The reading; NULL confirms nothing.
 * @return true when it confirms.
 */
bool stop_latch_all_off(const stop_latch_reading_t *r);

/**
 * @brief Start with no reset asked.
 * @param l       State; nothing for NULL.
 * @param pressed arming_pressed_count() now.
 */
void stop_latch_init(stop_latch_t *l, uint32_t pressed);

/**
 * @brief Follow the count of STOPs pressed, once a pass and before
 *        stop_latch_ask() and stop_latch_step().
 *
 * A count that moved is a STOP: the floor starts again from @p now_ms, a
 * reset that waits is withdrawn, and the grace of the last reset ends.
 * @param l       State; nothing for NULL.
 * @param pressed arming_pressed_count() now.
 * @param now_ms  Current time, in milliseconds.
 * @return true when a waiting reset was withdrawn.
 */
bool stop_latch_follow(stop_latch_t *l, uint32_t pressed, uint32_t now_ms);

/**
 * @brief The hold of STOP completed: ask for the reset.
 *
 * @p pressed is the count of STOPs pressed as the side that saw the hold
 * last had it.  A STOP pressed since, between the hold and this call, is
 * one the hold never saw: the request is refused, as one older than the
 * last STOP.  Call after stop_latch_follow() in the same pass.
 * @param l       State.
 * @param latched arming_stopped(): a stop stands.
 * @param pressed arming_pressed_count() as the hold's side last read it.
 * @param now_ms  Current time, in milliseconds.
 * @return true when the reset now waits; false with no stop standing, with
 *         one already waiting, for a request older than the last STOP, and
 *         for NULL.
 */
bool stop_latch_ask(stop_latch_t *l, bool latched, uint32_t pressed,
                    uint32_t now_ms);

/**
 * @brief An error while the latch stands: a reset that waits is withdrawn.
 * @param l State; nothing for NULL.
 * @return true when one was.
 */
bool stop_latch_error(stop_latch_t *l);

/**
 * @brief Whether a reset waits for the IOmcu's word.
 * @param l State.
 * @return true while one waits; false for NULL.
 */
bool stop_latch_waiting(const stop_latch_t *l);

/**
 * @brief Whether the caller reads the confirmation in this pass: a reset
 *        waits, and none was read in it yet or the last one is
 *        STOP_LATCH_ASK_MS old.  Marks the read as made.
 * @param l      State.
 * @param now_ms Current time, in milliseconds.
 * @return true when the caller reads now and passes the answer to
 *         stop_latch_step() in this pass.
 */
bool stop_latch_read_due(stop_latch_t *l, uint32_t now_ms);

/**
 * @brief One step of the reset.
 *
 *     no reset waits                        STOP_LATCH_NONE
 *     no stop stands any more               STOP_LATCH_NONE, withdrawn
 *     past STOP_LATCH_CONFIRM_MS from ask   STOP_LATCH_GIVE_UP
 *     @p confirmed, the floor reached       STOP_LATCH_RESUME
 *     otherwise                             STOP_LATCH_NONE
 *
 * The confirmation counts from the floor's millisecond on and up to the
 * bound's: one read at STOP_LATCH_CONFIRM_MS after the ask resumes, one
 * read a millisecond later is an exchange that came back late and resets
 * nothing.
 * @param l         State.
 * @param now_ms    Current time, in milliseconds.
 * @param latched   arming_stopped(): a stop stands.
 * @param confirmed The IOmcu, read in this pass after the ask, reports the
 *                  bench SAFE and every output off; with no link, the
 *                  panel's own bank and model do.
 * @return What the caller does.
 */
stop_latch_act_t stop_latch_step(stop_latch_t *l, uint32_t now_ms,
                                 bool latched, bool confirmed);

/**
 * @brief The STATUS fault bitmap the band shows: LINK_FAULT_HEARTBEAT taken
 *        out while a stop stands and for STOP_LATCH_GRACE_MS after a reset.
 *
 * The panel withholds the line itself then, and FAULT 10 would read as the
 * emergency stop or an open wire.  Other bits pass.
 * @param l       State; NULL hides nothing beyond @p latched.
 * @param now_ms  Current time, in milliseconds.
 * @param latched arming_stopped(): a stop stands.
 * @param faults  The bitmap as read.
 * @return The bitmap to show.
 */
uint16_t stop_latch_faults(const stop_latch_t *l, uint32_t now_ms,
                           bool latched, uint16_t faults);

/**
 * @brief Resets done since stop_latch_init(): one CLEAR each.
 * @param l State.
 * @return The count; 0 for NULL.
 */
uint32_t stop_latch_resets(const stop_latch_t *l);

#ifdef __cplusplus
}
#endif
