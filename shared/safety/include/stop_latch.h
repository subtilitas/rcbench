/**
 * @file
 * @brief The reset of the panel's STOP as one state machine: from a hold of
 *        STOP to the heartbeat sent again and CLEAR written, with the bench
 *        SAFE throughout.
 *
 * STOP works as an emergency stop button.  One touch latches it
 * (arming_stop_pressed(): arming_held() true, the heartbeat withheld) and
 * nothing switches on while it stands: arming_request_arm_roles() takes no
 * arm, and the panel refuses every ON.  A hold of STOP for UI_HOLD_S (2s)
 * on one unbroken contact asks for the reset.  The machine:
 *
 *     UNLATCHED      a STOP                          -> LATCHED
 *     LATCHED        a hold that spans the latest    -> HOLD_DONE
 *                    STOP and contact break
 *     HOLD_DONE      STOP_LATCH_FRESH_MS after the   -> ASKING
 *                    hold: the first read issued
 *     ASKING         the reading confirms all off,   -> CONFIRMED
 *                    STOP_LATCH_FLOOR_MS after the
 *                    STOP was seen
 *     ASKING         no confirmation within          -> LATCHED, GIVE_UP
 *                    STOP_LATCH_CONFIRM_MS
 *     CONFIRMED      io.resume(): the heartbeat      -> RESUMED
 *                    goes again
 *     RESUMED        io.clear() once, link up        -> CLEAR_WRITTEN
 *                    no link: nothing to write       -> UNLATCHED, DONE
 *     CLEAR_WRITTEN                                  -> UNLATCHED, DONE
 *
 * A STOP from any state goes back to LATCHED.  A contact break
 * (stop_latch_in_t.breaks) and an error (stop_latch_error()) go back to
 * LATCHED from HOLD_DONE, ASKING and CONFIRMED.  stop_latch_run() makes one
 * transition; after each exchange in it, io.refresh() reads the inputs
 * again, and a STOP that landed in the exchange sends the machine back
 * before the reading counts.
 *
 * The hold: its press is a STOP as well, so a hold the reset takes ends
 * STOP_LATCH_HOLD_SEEN_MS or more after the latest STOP and the latest
 * contact break.  A STOP or a break after the press is one the hold did not
 * span, and the request is refused.  HOLD_DONE then waits
 * STOP_LATCH_FRESH_MS, the time a controller that went silent in the last
 * moment of the hold takes to count as a break.
 *
 * "All off" is this end (its bank, the supply's output, the ports' 5V, or
 * an ON of either on its way) and, with the link up, the IOmcu's pages read
 * after the hold: STATUS not ARMED, SUPPLY's output off where the IOmcu
 * serves that page, PROG_SUPPLY's output off where it serves that one.
 * With no link there is no IOmcu to ask: the panel's own reading is the
 * confirmation, and an IOmcu that is wired has failed safe on link silence.
 *
 * The floor: the IOmcu's monitor doubts the line after HEARTBEAT_MAX_GAP_MS
 * (150ms) and sets its arm latch on that edge.  A heartbeat sent again
 * before 500ms would be one the far end may never have stopped trusting.
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

/**
 * @brief The least time from the latest STOP or contact break to the end of
 *        a hold the reset takes: 1900ms.
 *
 * The hold runs UI_HOLD_S (2000ms) from its own press, a STOP the control
 * task applies at the press; the 100ms spare covers the pass that sees it.
 */
#define STOP_LATCH_HOLD_SEEN_MS 1900u

/**
 * @brief How long the touch controller may go unanswered before it counts
 *        as a contact break: 100ms, 10 polls of 10ms.  HOLD_DONE waits this
 *        long before the first read.
 */
#define STOP_LATCH_FRESH_MS 100u

/** @brief The least time from the STOP, as the machine saw it, to CONFIRMED:
 *         500ms. */
#define STOP_LATCH_FLOOR_MS 500u

/** @brief How long ASKING waits for the confirmation: 2000ms from the first
 *         read. */
#define STOP_LATCH_CONFIRM_MS 2000u

/** @brief How often the confirmation is read in ASKING: 50ms. */
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

/** @brief Where the reset stands. */
typedef enum {
    STOP_LATCH_UNLATCHED = 0, /**< no operator's STOP stands             */
    STOP_LATCH_LATCHED,       /**< a STOP stands, no reset under way     */
    STOP_LATCH_HOLD_DONE,     /**< a hold completed; the read is due     */
    STOP_LATCH_ASKING,        /**< the read is issued, no confirmation   */
    STOP_LATCH_CONFIRMED,     /**< all off, the floor passed             */
    STOP_LATCH_RESUMED,       /**< the heartbeat goes again              */
    STOP_LATCH_CLEAR_WRITTEN, /**< CLEAR written once                    */
} stop_latch_state_t;

/** @brief What the caller does after stop_latch_run(). */
typedef enum {
    STOP_LATCH_NONE = 0,
    /** @brief No confirmation within STOP_LATCH_CONFIRM_MS: the latch and the
     *         withheld heartbeat stand, and the operator is told why. */
    STOP_LATCH_GIVE_UP,
    /** @brief The reset is done: UNLATCHED, the bench SAFE. */
    STOP_LATCH_DONE,
} stop_latch_act_t;

/** @brief The control task's inputs, read once a pass and again after every
 *         exchange (stop_latch_io_t.refresh). */
typedef struct {
    uint32_t now_ms;      /**< current time, ms                            */
    uint32_t pressed;     /**< arming_pressed_count(): operator's STOPs    */
    uint32_t press_ms;    /**< when the latest of them was applied, ms     */
    uint32_t breaks;      /**< contact breaks: the touch controller
                               unanswered for STOP_LATCH_FRESH_MS          */
    uint32_t break_ms;    /**< when the latest was counted, ms             */
    bool     held;        /**< arming_held(): an operator's STOP stands    */
    bool     hold_done;   /**< a completed hold's request, taken this pass */
    uint32_t hold_ms;     /**< when that hold completed, ms                */
    bool     link_up;     /**< the link to the IOmcu answers               */
    bool     local_on;    /**< something is on, or an ON is on its way, at
                               this end                                    */
    bool     supply_page; /**< the IOmcu serves LINK_PAGE_SUPPLY           */
    bool     prog_page;   /**< the IOmcu serves LINK_PAGE_PROG_SUPPLY      */
} stop_latch_in_t;

/** @brief What the machine does through the caller. */
typedef struct {
    void *ctx; /**< passed back to each call */
    /**
     * @brief Read one register of the IOmcu.
     * @return true when the read was answered with the value in @p out
     */
    bool (*read)(void *ctx, uint8_t page, uint8_t reg, uint16_t *out);
    /** @brief The heartbeat goes again: arming_stop_release(). */
    void (*resume)(void *ctx);
    /** @brief Write CLEAR once; its answer changes nothing. */
    void (*clear)(void *ctx);
    /** @brief Read the inputs again after an exchange; hold_done and
     *         hold_ms stay as they were. */
    void (*refresh)(void *ctx, stop_latch_in_t *in);
} stop_latch_io_t;

/** @brief The machine's state. */
typedef struct {
    stop_latch_state_t state; /**< where the reset stands                 */
    uint32_t pressed_seen; /**< stop_latch_in_t.pressed at the last look  */
    uint32_t breaks_seen;  /**< stop_latch_in_t.breaks at the last look   */
    uint32_t seen_ms;      /**< when the machine saw the latest STOP      */
    uint32_t event_ms;     /**< when the latest STOP or break happened    */
    bool     event_known;  /**< a STOP or a break was seen since init     */
    uint32_t ask_ms;       /**< when ASKING began                         */
    uint32_t hold_ms;      /**< when the hold the reset runs on completed */
    bool     read_any;     /**< a reading was made in this reset          */
    uint32_t read_ms;      /**< when the last one was made                */
    bool     read_off;     /**< the last reading confirmed all off        */
    bool     grace;        /**< grace_ms means something                  */
    uint32_t grace_ms;     /**< when the last reset was done              */
    uint32_t resets;       /**< resets done                               */
    uint32_t refused;      /**< holds refused                             */
} stop_latch_t;

/**
 * @brief Start: LATCHED when @p in says a STOP stands, else UNLATCHED.  A
 *        panel that starts has no STOP standing.
 * @param l  State; nothing for NULL.
 * @param in The inputs now; NULL starts unlatched with zero counts.
 */
void stop_latch_init(stop_latch_t *l, const stop_latch_in_t *in);

/**
 * @brief One transition of the machine, once a pass of the control task
 *        after its stops are served.
 * @param l  State.
 * @param in The pass's inputs; io.refresh() updates them.
 * @param io What the machine does through the caller; a NULL member is
 *           skipped, and a NULL read answers nothing.
 * @return What the caller does; STOP_LATCH_NONE for NULL arguments.
 */
stop_latch_act_t stop_latch_run(stop_latch_t *l, stop_latch_in_t *in,
                                const stop_latch_io_t *io);

/**
 * @brief An error: a reset that is not yet resumed goes back to LATCHED.
 * @param l State; nothing for NULL.
 * @return true when one went back.
 */
bool stop_latch_error(stop_latch_t *l);

/**
 * @brief Where the reset stands.
 * @param l State.
 * @return The state; STOP_LATCH_UNLATCHED for NULL.
 */
stop_latch_state_t stop_latch_state(const stop_latch_t *l);

/**
 * @brief The STATUS fault bitmap the band shows: LINK_FAULT_HEARTBEAT taken
 *        out while an operator's STOP stands, while the reset finishes, and
 *        for STOP_LATCH_GRACE_MS after it.
 *
 * The panel withholds the line itself then, and FAULT 10 would read as the
 * emergency stop or an open wire.  Other bits pass.
 * @param l       State; NULL hides nothing beyond @p latched.
 * @param now_ms  Current time, in milliseconds.
 * @param latched arming_held(): an operator's STOP stands.
 * @param faults  The bitmap as read.
 * @return The bitmap to show.
 */
uint16_t stop_latch_faults(const stop_latch_t *l, uint32_t now_ms,
                           bool latched, uint16_t faults);

/**
 * @brief Resets done since stop_latch_init().
 * @param l State.
 * @return The count; 0 for NULL.
 */
uint32_t stop_latch_resets(const stop_latch_t *l);

/**
 * @brief Holds refused since stop_latch_init(): a STOP or a contact break
 *        the hold did not span.
 * @param l State.
 * @return The count; 0 for NULL.
 */
uint32_t stop_latch_refused(const stop_latch_t *l);

#ifdef __cplusplus
}
#endif
