/**
 * @file files_host_io.h
 * @brief The FILES screen's card task on the host: a files_io_t over a card
 *        in memory (fm_fake.h), running each request through fm_req_run()
 *        at once or when the test says so, with the bench the gate reads,
 *        CANCEL and the progress under the test's hand.
 *
 * Used by test_files_*.c and render_screen.c.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_FILES_HOST_IO_H
#define RCBENCH_FILES_HOST_IO_H

#include <stdbool.h>
#include <stdint.h>

#include "card_state.h"
#include "card_tree.h"
#include "files_screen.h"
#include "fm_fake.h"
#include "fm_ops.h"
#include "fm_req.h"
#include "log_take.h"

/** @brief The card task, its card and the bench it answers with. */
typedef struct {
    fk_card_t card;            /**< the card                              */
    fm_io_t io;                /**< the card's table, fk_io()             */
    bool defer;                /**< hold a request until fh_run()         */
    fm_req_t *held;            /**< the request handed over, not run      */
    bool fail_submit;          /**< submit() refuses: the task did not start */
    bool armed;                /**< the bench is ARMED                    */
    bool link_up;              /**< the IOmcu link is up                  */
    log_marks_t marks;         /**< the logger's open files               */
    log_marks_t seen;          /**< marks as bench() last copied them: the
                                    screen's snapshot, as on the panel   */
    char run_dir[CARD_DIR_MAX];  /**< the open run's folder               */
    char test_dir[CARD_DIR_MAX]; /**< the open test's folder              */
    const char *volume;        /**< the card's name, NULL for none        */
    card_state_t state;        /**< what the slot holds with no card      */
    bool cancel;               /**< CANCEL was asked                      */
    uint64_t done;             /**< progress() answers these              */
    uint64_t total;            /**< and these                             */
    bool fixed_progress;       /**< progress() answers done and total as
                                    set, not as the job ran              */
    unsigned submits;          /**< requests taken                        */
    unsigned looks;            /**< requests that looked at the slot      */
    unsigned cancels;          /**< cancel() calls                        */
    fm_req_kind_t last_kind;   /**< the kind of the last request taken    */
    int arm_after_steps;       /**< ARMED from this job step on; -1 never */
    int steps;                 /**< job steps the bench was asked for     */
} fh_t;

/**
 * @brief An empty card in the slot, mounted, a SAFE bench with the link
 *        up and nothing open, requests run at once.
 * @param h  the task
 */
void fh_init(fh_t *h);

/**
 * @brief Free the card.
 * @param h  the task
 */
void fh_free(fh_t *h);

/**
 * @brief The screen's io over @p h.
 * @param h  the task
 * @return the io
 */
files_io_t fh_files_io(fh_t *h);

/**
 * @brief Run the request held (with @p h->defer) to its end, as the task
 *        does.
 * @param h  the task
 * @return true when one was held
 */
bool fh_run(fh_t *h);

#endif
