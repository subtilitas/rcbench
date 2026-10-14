/*
 * The coprocessor's firmware update, as the panel offers and shows it.
 *
 * Two views.  The question: which version the coprocessor runs, which one
 * this panel carries, whether that one is older, CANCEL and START.  START
 * takes the 2 s hold (UI_HOLD_S), the ARM gesture.  The progress: a full
 * screen without the band, like the bus-fault screen, with the step under
 * way and the blocks sent.  The bench cannot be armed while it shows: the
 * coprocessor refuses ARM for the whole update session.
 *
 * The screen decides nothing about the update.  The application offers it,
 * takes START or CANCEL from the latches, reports the step, and moves the
 * router away when the update has ended.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_COPROC_UPDATE_SCREEN_H
#define RCBENCH_COPROC_UPDATE_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CU_VIEW_QUESTION = 0,
    CU_VIEW_PROGRESS,
} cu_view_t;

/** The step the progress view names. */
typedef enum {
    CU_STEP_PREPARE = 0,  /**< hashing the image before the first block */
    CU_STEP_SEND,         /**< blocks going out                          */
    CU_STEP_VERIFY,       /**< the coprocessor hashes what it wrote      */
    CU_STEP_RESTART,      /**< it restarts into the new slot             */
    CU_STEP_BUY,          /**< and this panel keeps the new image        */
} cu_step_t;

/** A firmware version: major, minor, patch. */
typedef struct {
    uint16_t major, minor, patch;
} cu_version_t;

/**
 * Whether @p carried is older than @p running: the question then says so,
 * and START still takes it after the hold.
 */
bool coproc_update_older(cu_version_t running, cu_version_t carried);

/**
 * Offer the update: the question view, with the version the coprocessor
 * runs and the one this panel carries.  Clears both latches.
 */
void coproc_update_screen_offer(cu_version_t running, cu_version_t carried);

/** The step under way and the blocks committed of @p blocks. */
void coproc_update_screen_progress(cu_step_t step, uint16_t block,
                                   uint16_t blocks);

cu_view_t coproc_update_screen_view(void);

/** True once after START's hold completed; the view is then PROGRESS. */
bool coproc_update_screen_take_start(void);

/** True once after CANCEL, or after the question was left another way. */
bool coproc_update_screen_take_cancel(void);

void coproc_update_screen_invalidate(void);

const ui_screen_t *coproc_update_screen(void);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_COPROC_UPDATE_SCREEN_H */
