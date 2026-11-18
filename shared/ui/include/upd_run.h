/**
 * @file upd_run.h
 * @brief The update run on the screens: UPDATES opens by itself once per
 *        start while a run stands after a restart, the band names what
 *        holds that opening back, and the run ends.
 *
 * An update run stands after the panel's install and after the C6 radio's
 * update (fw_run.h keeps the fact across the restart; the panel's
 * fw_ota_run_active() reads it).  upd_run_drive() is called once a frame
 * on the render side, before the IOmcu's question is served, so that
 * UPDATES comes first when both are due.
 *
 * UPDATES opens by itself once per start, on OVERVIEW (past the splash),
 * with the bench not unsafe (SAFE, no arm waiting, the supply off), the
 * panel's radio wholly off and no panel install under way: the IOmcu
 * question's conditions.  While one of the last three holds it back, the
 * band names it, once, and again when the reason changes.  On another
 * screen it waits without a line.
 *
 * The run ends, and the caller erases the run note, when:
 * - the operator leaves UPDATES by the home tag (upd_screen_take_left()):
 *   "not now";
 * - no step blocks any more (upd_wizard.h), once every target has had its
 *   first report (upd_run_feed_t.known): with no IOmcu link and no
 *   wireless device published yet, a zeroed view passes every step.
 * Neither ends it while a restart is due (upd_run_feed_t.restart_due):
 * the C6 update notes the run for the start after it, and an end then
 * would erase that note.  A screen forced over UPDATES (PANEL UPDATE, the
 * IOmcu's progress, the bus-fault screen) ends nothing.
 *
 * A rollback and the last opening are the run note's own ends
 * (fw_run_settle()): the start after them finds no run.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_UPD_RUN_H
#define RCBENCH_UPD_RUN_H

#include <stdbool.h>
#include <stdint.h>

#include "ui_text.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What holds the run's opening of UPDATES back, in this order. */
typedef enum {
    UPD_RUN_WAIT_NONE = 0, /**< nothing: it opens on OVERVIEW             */
    UPD_RUN_WAIT_SAFE,     /**< ARMED, an arm waiting, the supply on       */
    UPD_RUN_WAIT_RADIO,    /**< the panel's radio is not wholly off        */
    UPD_RUN_WAIT_INSTALL,  /**< a panel download or install is under way   */
} upd_run_wait_t;

/** @brief What the application knows once a frame. */
typedef struct {
    bool run;         /**< an update run stands (fw_ota_run_active())     */
    bool unsafe;      /**< ARMED, an arm waiting, the supply on or coming */
    bool radio_up;    /**< the panel's radio is not wholly off            */
    bool installing;  /**< a panel download or install is under way       */
    bool known;       /**< every target has had its first report: the
                           wireless devices' rows are published           */
    bool restart_due; /**< an update runs that ends in a restart, or the
                           restart is due (the C6's)                      */
} upd_run_feed_t;

/** @brief What the driver keeps between frames; zeroed at the start. */
typedef struct {
    bool    opened; /**< UPDATES opened by itself in this start        */
    uint8_t said;   /**< the upd_run_wait_t the band said last, 0 none */
} upd_run_t;

/** @brief What upd_run_drive() decided about the run. */
typedef enum {
    UPD_RUN_GOES_ON = 0, /**< no run, or it stands                        */
    UPD_RUN_END_LEFT,    /**< the home tag left UPDATES: end it           */
    UPD_RUN_END_DONE,    /**< no step blocks: end it                      */
} upd_run_end_t;

/**
 * @brief What holds the opening back now.
 * @param f the feed; NULL is UPD_RUN_WAIT_NONE
 * @return the first of unsafe, radio and install that holds
 */
upd_run_wait_t upd_run_wait(const upd_run_feed_t *f);

/**
 * @brief The band's line for a wait.
 * @param w the wait
 * @return its text ID; TX_ALERT_RUN_WAIT_SAFE for UPD_RUN_WAIT_NONE
 */
ui_text_id_t upd_run_wait_text(upd_run_wait_t w);

/**
 * @brief Once a frame: drain the home tag's latch, decide the run's end,
 *        open UPDATES or say on the band what holds it back.
 *
 * Without a run it only drains the latch.  An end opens nothing; the
 * caller ends the run (fw_ota_run_end()), and the next frames find none.
 *
 * @param r the driver
 * @param f what the application knows
 * @return the end, or UPD_RUN_GOES_ON
 */
upd_run_end_t upd_run_drive(upd_run_t *r, const upd_run_feed_t *f);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_UPD_RUN_H */
