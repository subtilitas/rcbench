/**
 * @file
 * @brief Where the panel's control task finds an error, and what it does
 *        there: error_stop()'s rule turned into the steps the control task
 *        carries out.
 *
 * The control task (firmware/panel/main/panel_supply.c) fills an
 * error_bench_t with the bench as it is, calls the function for the site,
 * and carries out the error_act_t it returns, in the order of its fields:
 *
 *     error_sites_stop()      an error found elsewhere: an INA3221 channel
 *                             over its limit, a SUPPLY page event that is
 *                             an error (supply_link_ev_error()), an ON
 *                             taken across lost touch events
 *     error_sites_follow()    a counter of errors: the card's failed
 *                             writes, the programmers' session faults
 *     error_sites_cut()       the panel's own supply trip, CURRENT TRIP or
 *                             VOLTAGE TRIP: the output off first, whoever
 *                             holds it and with any IOmcu, then the rule
 *     error_sites_reading()   the supply silent, or answering without a
 *                             reading for SUPPLY_LINK_BLIND_MS
 *
 * The act: DISARM requested when the bench is ARMED or a switch to ARMED
 * waits; the output off as error_stop() says; and to an IOmcu of link 4.31
 * (LINK_MINOR_PORT_CUT) that serves the SUPPLY page, with the PD (Power
 * Delivery) mini in use, the page's OFF whatever the panel reads of the
 * output (supply_link_cut()).
 *
 * A programming port holds the PD mini's output only, never the panel's
 * model.  With an IOmcu older than 4.31 a port that holds the supply keeps
 * it through every error here but the trip (error_sites_keeps()).  A SUPPLY
 * page event whose output is off at the far end already -- the module's
 * protection, a sagging input, an ON the page let go -- is followed off
 * here whoever holds it; an ON the page refused is not, when the port keeps
 * the supply (error_sites_page_keeps()).
 *
 * error_card_rows_edge(), error_card_csv_edge() and error_card_close_edge()
 * say when the logger counts a card fault of a run.
 *
 * Values in, values out: no call through a pointer, so the stack check
 * (tools/stack_check.py) follows the control task's chain through here.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_ERROR_SITES_H
#define RCBENCH_ERROR_SITES_H

#include <stdbool.h>
#include <stdint.h>

#include "error_stop.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The bench as an error finds it. */
typedef struct {
    bool     live;         /**< ARMED, or a switch to ARMED waits          */
    bool     supply_on;    /**< the output on, or an ON asked              */
    bool     port_holds;   /**< a programming port holds the supply; taken
                                for the PD mini only                      */
    bool     supply_real;  /**< the PD mini is the supply, not the model   */
    bool     supply_page;  /**< the IOmcu serves the SUPPLY page           */
    uint16_t far_minor;    /**< the IOmcu's link protocol minor            */
} error_bench_t;

/** @brief What the control task does for an error, in this order. */
typedef struct {
    bool off_first;   /**< the supply's output off before the rest      */
    bool disarm;      /**< DISARM requested: the bench to SAFE          */
    bool supply_off;  /**< the supply's output off                      */
    bool cut;         /**< the SUPPLY page's OFF, written whatever the
                           panel reads (supply_link_cut())              */
} error_act_t;

/** @brief Which reading fault error_sites_reading() found. */
typedef enum {
    ERROR_READING_NONE = 0,   /**< no fault                                */
    ERROR_READING_SILENT,     /**< the supply stopped answering            */
    ERROR_READING_BLIND,      /**< it answers with no reading              */
} error_reading_t;

/**
 * @brief Whether the IOmcu holds the panel's OFF for a programming port's
 *        write, and an error therefore switches the supply off whoever
 *        holds it (error_stop.h).
 *
 * @param supply_page The IOmcu serves the SUPPLY page.
 * @param far_minor   Its link protocol minor.
 * @return true for @p supply_page at LINK_MINOR_PORT_CUT (4.31) or later.
 */
bool error_port_defers(bool supply_page, uint16_t far_minor);

/**
 * @brief An error: error_stop()'s act on the bench as it is now.
 *
 * @param b The bench; nothing to do for NULL.
 * @return The act.
 */
error_act_t error_sites_stop(const error_bench_t *b);

/**
 * @brief One look at a counter of errors (error_stop_take()): the act once
 *        per change.
 *
 * @param watch The counter's watch; nothing to do for NULL.
 * @param count The counter now.
 * @param b     The bench; nothing to do for NULL.
 * @return The act; nothing to do when @p count has not changed.
 */
error_act_t error_sites_follow(error_watch_t *watch, uint32_t count,
                               const error_bench_t *b);

/**
 * @brief Whether an error leaves the supply to a programming port: the
 *        output on or asked on, a port holding it, the PD mini in use, and
 *        an IOmcu older than 4.31 or without the SUPPLY page, which would
 *        cut a write to the servo's memory at the panel's OFF.
 *
 * @param b The bench; false for NULL.
 * @return true when the panel writes no OFF for the error.
 */
bool error_sites_keeps(const error_bench_t *b);

/**
 * @brief The panel's own supply trip, CURRENT TRIP or VOLTAGE TRIP: the
 *        output off first, whoever holds it and with any IOmcu, then
 *        error_sites_stop() on the bench with the output off.
 *
 * @param b The bench; nothing to do for NULL.
 * @return The act.
 */
error_act_t error_sites_cut(const error_bench_t *b);

/**
 * @brief The supply's reading, judged with its output on: silent, or
 *        answering without a reading; error_sites_stop() for either.
 *
 * A fault the port's supply keeps (error_sites_keeps()) stands from pass to
 * pass: it is acted on and named at its first pass, and again once it is
 * another fault or the port no longer keeps the supply.
 *
 * @param b      The bench.
 * @param online The supply answers (supply_state_t.online).
 * @param blind  The PD mini's reading count has stood for
 *               SUPPLY_LINK_BLIND_MS (supply_link_blind()); for the PD
 *               mini only.
 * @param told   The fault of the last pass, ERROR_READING_NONE at the
 *               start; receives this pass's.
 * @param act    Receives the act; nothing to do without a fault.
 * @return The fault acted on, which the caller names on the band;
 *         ERROR_READING_NONE with the output off, for a fault that stands
 *         and keeps, and for a NULL @p b, @p told or @p act.
 */
error_reading_t error_sites_reading(const error_bench_t *b, bool online,
                                    bool blind, error_reading_t *told,
                                    error_act_t *act);

/**
 * @brief Whether a SUPPLY page event that ends the panel's ON leaves the
 *        page's command to a programming port instead of the panel's OFF
 *        (supply_link_keep()).
 *
 * @param b       The bench.
 * @param far_off The output is off at the far end already: the module's
 *                protection (SUPPLY_LINK_EV_TRIPPED), the IOmcu's cut for a
 *                sagging input (SUPPLY_LINK_EV_SAGGED), or an ON the page
 *                let go with no port holding it (SUPPLY_LINK_EV_ON_LOST).
 * @return true for an ON refused (SUPPLY_LINK_EV_ON_REFUSED) alone that
 *         error_sites_keeps() leaves to the port, judged with the output
 *         on whatever the panel reads of it: the port's ON on the page may
 *         not have reached the panel yet; false for NULL.
 */
bool error_sites_page_keeps(const error_bench_t *b, bool far_off);

/**
 * @brief Whether the run log's writes failed in this pass: a card fault
 *        counted once a run, at the failure's edge.
 *
 * @param open       A run's file is open.
 * @param was_failed The writer had failed before the pass.
 * @param failed     The writer has failed after it.
 * @return true at the edge.
 */
bool error_card_rows_edge(bool open, bool was_failed, bool failed);

/**
 * @brief Whether a failed line of the automatic test is the run's card
 *        fault: the first failure of the run, in its CSV (comma-separated
 *        values).  A report written after the run is not one.
 *
 * @param bad           The line's write or commit failed.
 * @param failed_before A write of the run failed before this line.
 * @param txt           The line is the report's.
 * @return true for the run's card fault.
 */
bool error_card_csv_edge(bool bad, bool failed_before, bool txt);

/**
 * @brief Whether a failed final commit or close of a run's file is the
 *        run's card fault: of the run log, or of the automatic test's CSV,
 *        and the run's first failure.  A report written after the run is
 *        not one.
 *
 * @param bad           The commit or the close failed.
 * @param failed_before A write of the run failed before: counted then.
 * @param report        The file is the report written after the run.
 * @return true for the run's card fault.
 */
bool error_card_close_edge(bool bad, bool failed_before, bool report);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_ERROR_SITES_H */
