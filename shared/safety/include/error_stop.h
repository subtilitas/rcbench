/**
 * @file
 * @brief The panel's rule for an error: every output off and the bench SAFE.
 *
 * An error is a fault the panel finds that ends what the bench drives: the
 * supply tripped, sagged, stopped answering, gave no reading, was switched
 * off by the IOmcu or by the PD mini's own protection, refused an ON or
 * would not switch; an INA3221 channel whose window passed its current
 * limit; a card write that failed during a run.  The operator's own OFF,
 * DISARM and edits are no error.
 *
 * A programming session that ends an operation in a fault of the servo or
 * the wire is an error as well: a KST operation that ends with
 * kst_link_outcome_fault(), a Hitec one with hitec_prog_ses_fault().
 *
 * What an error asks:
 *
 *     the bench ARMED, or a switch to ARMED waiting   DISARM: the bench SAFE
 *     the supply's output on or asked on               the output off
 *     a programming port holds the supply              the port's cut
 *                                                      (port_cut_t)
 *
 * A SAFE bench is not disarmed again: a disarm withholds the heartbeat while
 * it stands, and on a SAFE bench that would end a programming port's
 * session, its write journal included, for no output it drives.
 *
 * The port's cut switches the supply off outside a write to the servo's
 * memory: at once when none runs, when the write ends otherwise, and
 * ERROR_PORT_CUT_MS after the error at the latest.  A cut in the middle of
 * a write is the harm the wait avoids.  The cut stands until the port lets
 * go of the supply with the output off: a port that switches the supply on
 * again in that time (a KST power cycle's ON, a Hitec POWER) is switched
 * off again, and the panel sends the HITEC page no step once the cut has
 * acted (port_cut_blocks()).  The IOmcu switches the supply off on its own
 * when the heartbeat or the link goes (safety_gate.h), whoever holds it.
 *
 * A counter of errors raised on another task (error_stop_take()) is read
 * here as an edge: one act per change, whatever the difference, so a
 * counter that wraps through 0 still acts.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_ERROR_STOP_H
#define RCBENCH_ERROR_STOP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The longest the port's cut waits for a write to the servo's memory
 *        to end, counted from the error, in ms.
 *
 * The longest writes the code allows, without a fault:
 *
 *   - KST: its planning, at most 2,161 slices of 892us at 150MHz
 *     (docs/Performance.md, computed), 1.93s; the read of 32 registers in
 *     3 passes before and after and KST_PLAN_MAX_STEPS (33) writes with 2
 *     read-backs each, 258 read frames of 5.9ms and 33 write frames of
 *     12.4ms with their reply windows and gaps, 1.93s; and
 *     KST_LINK_POLL_MS (50ms) for the panel to see the end
 *     (test_kst_link_cut).
 *   - Hitec, at 32ms a step across the link at the panel's 5ms pass and a
 *     16ms render frame on the host loopback (test_hitec_prog_cut): a
 *     classic write of HITEC_PLAN_MAX (56) steps and its read-back of 36
 *     cells, 2.9s; a D series write of every register of the full read
 *     with its save, its 1000ms reboot wait and its read-back, 3.5s.
 *
 * test_kst_link_cut and test_hitec_prog_cut hold each under
 * ERROR_PORT_CUT_MS / 2.  A write with retries, a slower render frame or a
 * slower link takes longer and is cut at the bound.  None is measured on
 * hardware.
 */
#define ERROR_PORT_CUT_MS 10000u

/** @brief What an error asks of the bench. */
typedef struct {
    bool disarm;      /**< the bench to SAFE                        */
    bool supply_off;  /**< the supply's output off                  */
    bool port_cut;    /**< the port's cut (port_cut_ask())          */
} error_stop_t;

/**
 * @brief The act for one error.
 *
 * @param live       The bench is ARMED, or a switch to ARMED waits.
 * @param supply_on  The supply's output is on, or an ON is asked.
 * @param port_holds A programming port holds the supply.
 * @return disarm when @p live; supply_off when @p supply_on and not
 *         @p port_holds; port_cut when @p port_holds.
 */
error_stop_t error_stop(bool live, bool supply_on, bool port_holds);

/** @brief An error counter read on the task that acts. */
typedef struct {
    uint32_t seen;    /**< the count at the last look              */
} error_watch_t;

/**
 * @brief Start watching from @p count.
 *
 * @param w     Watch state.
 * @param count The counter now.
 */
void error_watch_init(error_watch_t *w, uint32_t count);

/**
 * @brief One look at the counter: the act for the errors counted since the
 *        last look, or nothing.
 *
 * @param w          Watch state; @c seen moves to @p count.
 * @param count      The counter now.
 * @param live       As error_stop().
 * @param supply_on  As error_stop().
 * @param port_holds As error_stop().
 * @return error_stop()'s act when @p count differs from the last look;
 *         neither part otherwise, and for a NULL @p w.
 */
error_stop_t error_stop_take(error_watch_t *w, uint32_t count, bool live,
                             bool supply_on, bool port_holds);

/** @brief The cut a programming port's supply owes after an error. */
typedef struct {
    bool     armed;   /**< an error asked it and it stands          */
    bool     acted;   /**< it has switched the supply off once      */
    uint32_t t0;      /**< when the first error asked it, ms        */
} port_cut_t;

/**
 * @brief An error while a port holds the supply: the cut stands from now,
 *        or from the error that asked it first while it stands.
 *
 * @param c      Cut state; nothing is done for NULL.
 * @param now_ms The clock, ms.
 */
void port_cut_ask(port_cut_t *c, uint32_t now_ms);

/**
 * @brief One look at a standing cut, every pass of the task that switches
 *        the supply.
 *
 * The cut ends, with nothing to do, once @p holds and @p supply_on are both
 * false.  Otherwise it switches the supply off whenever @p supply_on and no
 * write runs, and while one runs from ERROR_PORT_CUT_MS after port_cut_ask()
 * on, measured across the clock's wrap at 2^32ms.  Once it has switched the
 * supply off, no write is waited for again.
 *
 * @param c         Cut state.
 * @param holds     A programming port holds the supply.
 * @param writing   A write to the servo's memory runs.
 * @param supply_on The supply's output is on, or an ON is asked.
 * @param now_ms    The clock, ms.
 * @return true when the supply is to be switched off now; false for a NULL
 *         @p c, a cut that does not stand and while the write is waited for.
 */
bool port_cut_step(port_cut_t *c, bool holds, bool writing, bool supply_on,
                   uint32_t now_ms);

/**
 * @brief Whether the panel sends the HITEC page no step: the cut stands and
 *        has acted.
 *
 * @param c Cut state.
 * @return true from the first switch-off of a cut until it ends; false for
 *         NULL.
 */
bool port_cut_blocks(const port_cut_t *c);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_ERROR_STOP_H */
