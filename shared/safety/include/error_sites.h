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
 *                             an error (supply_link_ev_error())
 *     error_sites_follow()    a counter of errors: the card's failed
 *                             writes, the programmers' session faults
 *     error_sites_cut()       an error on the supply's own output: a trip,
 *                             an ON taken across lost touch events; the
 *                             output off first, then the rule
 *     error_sites_reading()   the supply silent, or answering without a
 *                             reading for SUPPLY_LINK_BLIND_MS
 *
 * The act: DISARM requested when the bench is ARMED or a switch to ARMED
 * waits; the output off as error_stop() says; and to an IOmcu of link 4.31
 * (LINK_MINOR_PORT_CUT) that serves the SUPPLY page, with the PD (Power
 * Delivery) mini in use, the page's OFF whatever the panel reads of the
 * output (supply_link_cut()).
 *
 * error_card_rows_edge() and error_card_csv_edge() say when the logger
 * counts a card fault of a run.
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
    bool     port_holds;   /**< a programming port holds the supply        */
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
 * @brief An error on the supply's own output: the output off first,
 *        whoever holds it and with any IOmcu, then error_sites_stop() on
 *        the bench with the output off.
 *
 * @param b The bench; nothing to do for NULL.
 * @return The act.
 */
error_act_t error_sites_cut(const error_bench_t *b);

/**
 * @brief The supply's reading, judged with its output on: silent, or
 *        answering without a reading; error_sites_cut() for either.
 *
 * @param b      The bench.
 * @param online The supply answers (supply_state_t.online).
 * @param blind  The PD mini's reading count has stood for
 *               SUPPLY_LINK_BLIND_MS (supply_link_blind()); for the PD
 *               mini only.
 * @param act    Receives the act; nothing to do without a fault.
 * @return The fault found, which the caller names on the band;
 *         ERROR_READING_NONE with the output off, and for a NULL @p b or
 *         @p act.
 */
error_reading_t error_sites_reading(const error_bench_t *b, bool online,
                                    bool blind, error_act_t *act);

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

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_ERROR_SITES_H */
