/**
 * @file
 * @brief The panel's rule for an error: every output off and the bench SAFE.
 *
 * An error is a fault the panel finds that ends what the bench drives: the
 * supply tripped, sagged, stopped answering, gave no reading, was switched
 * off by the IOmcu or by the PD mini's own protection, refused an ON or
 * would not switch; an INA3221 channel whose window passed its current
 * limit; a card write that failed during a run.  A programming session
 * whose operation ends in a fault of the servo or the wire is an error as
 * well: a KST outcome kst_link_outcome_fault() names, a Hitec result
 * hitec_prog_ses_fault() names.  The operator's own OFF, DISARM and edits
 * are no error.
 *
 * What an error asks:
 *
 *     the bench ARMED, or a switch to ARMED waiting   DISARM: the bench SAFE
 *     an IOmcu of link 4.31 or later                   the output off, on
 *                                                      or not, a port's
 *                                                      included; the IOmcu
 *                                                      holds it for a write
 *                                                      that runs
 *     an older IOmcu, the output on or asked on        the output off
 *     an older IOmcu, a programming port holds         the output left to
 *     the supply                                       the port's session
 *
 * A SAFE bench is not disarmed again: a disarm withholds the heartbeat while
 * it stands, and on a SAFE bench that would end a programming port's
 * session, its write journal included, for no output it drives.
 *
 * A cut in the middle of a write to the servo's memory is the harm the
 * rule must not do.  Only the IOmcu sees a write exactly: from link 4.31
 * (LINK_MINOR_PORT_CUT) its SUPPLY page holds the panel's OFF of a held
 * supply until the port's write ends, SUPPLY_PAGE_WRITE_WAIT_MS (10000ms)
 * at most, and refuses the port's ON after it until the port lets go
 * (supply_page.h).  The panel asks the OFF then on every error, whether
 * it reads the output on or not and whether it reads a port holding the
 * supply or not: a port in a power cycle has the output off and is about
 * to switch it on, and what the panel reads of a port is one poll old.  An
 * older IOmcu switches the supply off at the OFF, write or not: there a
 * port that holds the supply keeps it.  The IOmcu switches the supply off
 * on its own when the heartbeat or the link goes (safety_gate.h), whoever
 * holds it.
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

/** @brief What an error asks of the bench. */
typedef struct {
    bool disarm;      /**< the bench to SAFE                        */
    bool supply_off;  /**< the supply's output off                  */
} error_stop_t;

/**
 * @brief The act for one error.
 *
 * @param live        The bench is ARMED, or a switch to ARMED waits.
 * @param supply_on   The supply's output is on, or an ON is asked.
 * @param port_holds  A programming port holds the supply.
 * @param port_defers The IOmcu holds an OFF for a port's write (link 4.31,
 *                    LINK_MINOR_PORT_CUT).
 * @return disarm when @p live; supply_off always with @p port_defers, and
 *         without it when @p supply_on and not @p port_holds.
 */
error_stop_t error_stop(bool live, bool supply_on, bool port_holds,
                        bool port_defers);

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
 * @param w           Watch state; @c seen moves to @p count.
 * @param count       The counter now.
 * @param live        As error_stop().
 * @param supply_on   As error_stop().
 * @param port_holds  As error_stop().
 * @param port_defers As error_stop().
 * @return error_stop()'s act when @p count differs from the last look;
 *         neither part otherwise, and for a NULL @p w.
 */
error_stop_t error_stop_take(error_watch_t *w, uint32_t count, bool live,
                             bool supply_on, bool port_holds,
                             bool port_defers);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_ERROR_STOP_H */
