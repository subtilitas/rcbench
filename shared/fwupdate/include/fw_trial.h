/**
 * @file fw_trial.h
 * @brief When a new build's first start is kept: the steps the start
 *        waits for, and the bound on the wait.
 *
 * The bootloader starts a new build once on trial (pending verify).  A
 * reset while it still is, by a panic, a watchdog, a restart or a power
 * cut, starts the build before it.  This model says when the trial is
 * settled in the build's favour: once every step in FW_TRIAL_ALL is seen,
 * or, FW_TRIAL_BOUND_MS after the start, once the screen loop is ticking.
 * Either way only on a quiet bench, because keeping the build writes
 * otadata and a flash write stalls both cores.
 *
 * The steps:
 * - FW_TRIAL_CARD: the card was tried (mounted, or found missing or
 *   unreadable) and the start's card reads ran;
 * - FW_TRIAL_SETTINGS: the settings are loaded and handed on;
 * - FW_TRIAL_CONTROL: the control task finished one pass of its loop;
 * - FW_TRIAL_LOGGER: the logger task finished one pass of its loop;
 * - FW_TRIAL_FRAME: a whole frame reached the display;
 * - FW_TRIAL_TICK: the screen loop ticked after that frame.  A tick seen
 *   before FW_TRIAL_FRAME counts for nothing.
 *
 * No step waits for the IOmcu: a panel without one is kept the same way.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 * The panel's calls are firmware/panel/main/panel_trial.c.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_FW_TRIAL_H
#define RCBENCH_FW_TRIAL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The card was tried and the start's card reads ran. */
#define FW_TRIAL_CARD     0x01u
/** @brief The settings are loaded. */
#define FW_TRIAL_SETTINGS 0x02u
/** @brief The control task finished one pass. */
#define FW_TRIAL_CONTROL  0x04u
/** @brief The logger task finished one pass. */
#define FW_TRIAL_LOGGER   0x08u
/** @brief A whole frame reached the display. */
#define FW_TRIAL_FRAME    0x10u
/** @brief The screen loop ticked after FW_TRIAL_FRAME. */
#define FW_TRIAL_TICK     0x20u
/** @brief Every step: the start as a whole works. */
#define FW_TRIAL_ALL      0x3Fu

/**
 * @brief How long after the start a trial waits for every step: 30000ms.  Past
 * it, FW_TRIAL_TICK alone keeps the build.
 */
#define FW_TRIAL_BOUND_MS 30000u

/** @brief What fw_trial_poll() decides. */
typedef enum {
    FW_TRIAL_OPEN = 0,   /**< still on trial: wait                        */
    FW_TRIAL_KEEP,       /**< every step seen: keep the build now          */
    FW_TRIAL_KEEP_BOUND, /**< the bound passed and the screen loop ticks   */
    FW_TRIAL_SETTLED,    /**< settled before: nothing more to do           */
} fw_trial_verdict_t;

/** @brief One trial: the steps seen and whether it is settled. */
typedef struct {
    uint32_t start_ms; /**< the start the bound counts from               */
    uint32_t done;     /**< FW_TRIAL_* steps seen                          */
    bool     bound;    /**< FW_TRIAL_BOUND_MS has passed; latched          */
    bool     settled;  /**< kept, or settled by fw_trial_settle()          */
} fw_trial_t;

/**
 * @brief Start a trial with no step seen.
 * @param t        The trial.
 * @param start_ms The millisecond count the bound counts from.
 */
void fw_trial_init(fw_trial_t *t, uint32_t start_ms);

/**
 * @brief Record steps as seen.  FW_TRIAL_TICK counts only once
 *        FW_TRIAL_FRAME is seen, before this call or in it; bits outside
 *        FW_TRIAL_ALL are ignored.
 * @param t     The trial.
 * @param steps One or more FW_TRIAL_* steps.
 */
void fw_trial_mark(fw_trial_t *t, uint32_t steps);

/**
 * @brief Settle the trial outside this model, as the start without touch
 *        does; every later poll says FW_TRIAL_SETTLED.
 * @param t The trial.
 */
void fw_trial_settle(fw_trial_t *t);

/**
 * @brief Decide at @p now_ms.  FW_TRIAL_KEEP and FW_TRIAL_KEEP_BOUND
 *        settle the trial: each is returned once.
 * @param t      The trial.
 * @param now_ms The millisecond count; it may wrap past 2^32.
 * @param quiet  Whether the bench can take a flash write now.
 * @return FW_TRIAL_KEEP once every step is seen on a quiet bench;
 *         FW_TRIAL_KEEP_BOUND once FW_TRIAL_BOUND_MS has passed since
 *         start_ms with FW_TRIAL_TICK seen on a quiet bench; otherwise
 *         FW_TRIAL_OPEN, or FW_TRIAL_SETTLED after a settle.
 */
fw_trial_verdict_t fw_trial_poll(fw_trial_t *t, uint32_t now_ms, bool quiet);

/**
 * @brief The steps not seen yet.
 * @param t The trial.
 * @return The FW_TRIAL_* bits of FW_TRIAL_ALL not marked.
 */
uint32_t fw_trial_missing(const fw_trial_t *t);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FW_TRIAL_H */
