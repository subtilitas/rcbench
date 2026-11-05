/**
 * @file
 * @brief MOVE SERVO on the programmer for servos by Hitec: the horn of a
 *        D series servo moved to the position being edited, inside the held
 *        session, while the bench is SAFE.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this module works with.
 *
 * The loop runs on the panel as session operations of the programmer
 * (hitec_prog.h), with no IOmcu change: each pass is a hitec_session_move(),
 * a write of register 0x1E and a read of register 0x0C, judged by
 * hitec_live.h.  It never saves: a value reaches the servo's memory only by
 * WRITE n VALUES.
 *
 * The switch goes on only after the operator's 2s hold (the caller's), and
 * only with
 *
 *   - the port held and idle, the servo in programming mode and read, no
 *     STOP latched, the port not stopped, the bench SAFE;
 *   - a D series servo whose stored positions ascend (0xB2 < 0xC2 < 0xB0);
 *   - DIRECTION CW, in the servo and in the draft: the CCW mapping is not
 *     measured;
 *   - no READ ALL owed, no record of a write standing, no request of the
 *     card out and no run holding the programmer;
 *   - a PD mini answering the page with its output on and read: a STOP has
 *     to switch the servo's supply off, and the current judge needs its
 *     reading.  The rbmod carrier's fixed 5V has no reading, so MOVE SERVO
 *     is refused there (HITEC_PROG_MSG_MOVE_NEED_PD).
 *
 * What runs:
 *
 *   1. START: a read of 0x0C and of 0x1E, 0x50, 0x52, 0x56, which the full
 *      read does not hold.  The session's image then is the start's set.
 *      A position further than HITEC_LIVE_LAG_FREE outside the reach ends
 *      the loop with nothing moved (HITEC_PROG_MSG_MOVE_OUTSIDE).
 *   2. RUN: passes.  The commanded position is the draft's value of the
 *      followed parameter (NEUTRAL, LEFT END or RIGHT END), clamped to the
 *      reach and slew limited (hitec_live_d_command()); each pass is judged
 *      (hitec_live_judge()) with the PD mini's current and, while the
 *      SERVO screen shows one, the AS5600 angle.  The stall level is
 *      HITEC_LIVE_STALL_PCT of the session's current limit.
 *   3. BACK, on a bind: one move to hitec_live_back(), the bind's message
 *      said; the switch reads OFF from here.
 *   4. CENTRE: 0x1E = 3000, the reset value.
 *   5. REBOOT: 0x46 = 1, HITEC_D_REBOOT_WAIT_MS, and the read of the full
 *      set (hitec_session_run_plan()): the servo starts again with its
 *      stored values.
 *   6. CHECK: 0x1E, 0x50, 0x52, 0x56 read again.  Every register of the
 *      loop's set (0x1E, 0x50, 0x52, 0x54, 0x56, 0xB0, 0xB2, 0xC2) and every
 *      register a plan writes (hitec_writable()) is held against the start:
 *      equal says HITEC_PROG_MSG_MOVE_DONE (where no bind spoke), a
 *      difference or a register not read says HITEC_PROG_MSG_MOVE_CHANGED,
 *      switches ENABLE WRITE TO SERVO off and owes READ ALL.
 *
 * MOVE OFF (hitec_prog_move_stop()) during START ends the loop at the
 * start's end, with nothing moved; during RUN the pass that runs ends
 * first, then 4 to 6.  It is asked by the caller at a tap on ON, a tab
 * change, CLOSE, the home tag and a screen change.  The other operations
 * of the programmer are refused while the loop or its end runs
 * (HITEC_PROG_MOVING): HITEC_PROG_MSG_BUSY.
 *
 * An ABORT during START ends the loop with nothing moved; during RUN and
 * BACK the end, 4 to 6, follows it; during the end it is a hard end.
 *
 * A hard end, with no 4 to 6: a STOP latched at the panel, a port the
 * IOmcu stopped (a switch to SAFE at the bench), the supply's output read
 * off or not read, an AS5600 reading that was there at the start and is
 * gone, a session fault, a cut or lost step, an ABORT during the end.  It
 * switches ENABLE WRITE TO SERVO off and owes READ ALL (must_read, with
 * hitec_move_t.owed saying the loop asks for it): what the servo's RAM
 * holds is not known.  Every hard end but an ABORT is an error: the
 * programmer counts a session fault at the gate (hitec_prog_gate_faults()),
 * and the panel turns every output off (error_stop.h), the servo's supply
 * and with it what its RAM holds included; a session fault is counted
 * once.
 *
 * A bind is no error: the loop moves the horn back and ends as MOVE OFF
 * does.  The PD mini's own current limit and its trip stay the hard
 * ceiling.
 *
 * Edits: while the switch reads ON the draft's NEUTRAL, LEFT END and RIGHT
 * END take edits (hitec_prog_move_edit()), and each edit makes its
 * parameter the followed one.  The 2s hold is the consent CHECK THIS CHANGE
 * asks for: HITEC_RULE_END and HITEC_RULE_CENTRE are confirmed for those
 * edits.  Every hard rule stays: the encoding and the order left end <
 * neutral < right end.
 *
 * Not measured (README.md of protocols/hitec, "Live loop"): the pass's
 * period, the servo's answer to targets below 400 and above 5600, the
 * thresholds of the judge on a blocked horn, the stall current of a
 * D485HW, whether a reboot drops a RAM write, and whether a D series servo
 * holds its target with no frames.
 *
 * Pure C.  One task, the programmer's.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hitec_prog.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The step of the D series positions on the panel's -/+ keys,
 *         counts (0.11deg). */
#define HITEC_MOVE_STEP 8u

/** @brief Where the loop is. */
typedef enum {
    HITEC_MOVE_OFF = 0,  /**< no loop                                    */
    HITEC_MOVE_START,    /**< the start's read                           */
    HITEC_MOVE_RUN,      /**< the passes                                 */
    HITEC_MOVE_BACK,     /**< a bind: the move back                      */
    HITEC_MOVE_CENTRE,   /**< the end: 0x1E = 3000                       */
    HITEC_MOVE_REBOOT,   /**< the end: the reboot and the full read      */
    HITEC_MOVE_CHECK,    /**< the end: the loop's set read and compared  */
} hitec_move_phase_t;

/** @brief What the screen shows of a loop. */
typedef struct {
    uint8_t  phase;      /**< hitec_move_phase_t                         */
    uint8_t  follow;     /**< the hitec_param_t followed                 */
    bool     has_pos;    /**< pos is a reading of this loop              */
    uint16_t pos;        /**< 0x0C, counts                               */
    bool     has_ma;     /**< ma is the PD mini's reading                */
    uint16_t ma;         /**< the servo supply's current, mA             */
    bool     has_angle;  /**< angle is the AS5600's reading              */
    uint16_t angle;      /**< 0 to 4095 a turn                           */
    bool     clamped;    /**< the followed value lies outside the reach  */
    uint16_t lo;         /**< the reach: the position of target 0        */
    uint16_t hi;         /**< the position of target 6000                */
} hitec_move_view_t;

/**
 * @brief Why MOVE SERVO is not taken now.
 *
 * @param p The programmer.
 * @return HITEC_PROG_MSG_NONE when it is; otherwise the refusal: the
 *         state's (hpg_held_idle()), NOT_IN_MODE, NOT_READ, BUSY (a run, a
 *         request of the card), MUST_READ, RECORD_HELD, MOVE_NO_MAP,
 *         MOVE_NEED_DIR, MOVE_NEED_PD.
 */
hitec_prog_msg_id_t hitec_prog_move_gate(const hitec_prog_t *p);

/**
 * @brief MOVE SERVO on: the gate, then the start's read.  The followed
 *        parameter is NEUTRAL.
 *
 * @param p The programmer.
 * @return HITEC_PROG_MSG_NONE when it starts, else the refusal, said.
 */
hitec_prog_msg_id_t hitec_prog_move_start(hitec_prog_t *p);

/**
 * @brief MOVE SERVO off: the end after the pass that runs.  Nothing for a
 *        loop that ends already or none.
 *
 * @param p The programmer.
 */
void hitec_prog_move_stop(hitec_prog_t *p);

/**
 * @brief Where the loop is.
 *
 * @param p The programmer.
 * @return hitec_move_phase_t.
 */
hitec_move_phase_t hitec_prog_move_phase(const hitec_prog_t *p);

/**
 * @brief Whether the switch reads ON: START or RUN, no MOVE OFF asked.
 *
 * @param p The programmer.
 * @return true while it does.
 */
bool hitec_prog_move_on(const hitec_prog_t *p);

/**
 * @brief Whether parameter @p id takes an edit through
 *        hitec_prog_move_edit() now: NEUTRAL, LEFT END or RIGHT END of the
 *        D series while the switch reads ON.
 *
 * @param p  The programmer.
 * @param id hitec_param_t.
 * @return true when it does.
 */
bool hitec_prog_move_live(const hitec_prog_t *p, uint8_t id);

/**
 * @brief Edit a position the loop follows: hitec_prog_edit() with the hold's
 *        consent for HITEC_RULE_END and HITEC_RULE_CENTRE.  An edit taken
 *        makes @p id the followed parameter.
 *
 * @param p     The programmer.
 * @param id    HITEC_P_D_NEUTRAL, HITEC_P_D_LEFT_END or HITEC_P_D_RIGHT_END.
 * @param value The position, counts.
 * @return HITEC_PROG_EDIT_OK, or HITEC_PROG_EDIT_REFUSED with its message
 *         (BUSY where hitec_prog_move_live() is false).
 */
hitec_prog_edit_t hitec_prog_move_edit(hitec_prog_t *p, uint8_t id,
                                       int32_t value);

/**
 * @brief The loop as the screen shows it.
 *
 * @param p   The programmer.
 * @param out Receives it.
 * @return false with no loop (out->phase HITEC_MOVE_OFF).
 */
bool hitec_prog_move_view(const hitec_prog_t *p, hitec_move_view_t *out);

/**
 * @brief The stall level of the next or running loop: HITEC_LIVE_STALL_PCT
 *        of the session's current limit.
 *
 * @param p The programmer.
 * @return mA.
 */
uint16_t hitec_prog_move_stall_ma(const hitec_prog_t *p);

#ifdef __cplusplus
}
#endif
