/**
 * @file fw_trial.c
 * @brief When a new build's first start is kept: see fw_trial.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "fw_trial.h"

#include <string.h>

void fw_trial_init(fw_trial_t *t, uint32_t start_ms)
{
    memset(t, 0, sizeof(*t));
    t->start_ms = start_ms;
}

void fw_trial_mark(fw_trial_t *t, uint32_t steps)
{
    steps &= FW_TRIAL_ALL;
    /* The frame first: a tick that came before any frame reached the
     * display says nothing about a display that works. */
    t->done |= steps & ~FW_TRIAL_TICK;
    if ((steps & FW_TRIAL_TICK) != 0u && (t->done & FW_TRIAL_FRAME) != 0u) {
        t->done |= FW_TRIAL_TICK;
    }
}

void fw_trial_settle(fw_trial_t *t)
{
    t->settled = true;
}

fw_trial_verdict_t fw_trial_poll(fw_trial_t *t, uint32_t now_ms, bool quiet)
{
    if (t->settled) {
        return FW_TRIAL_SETTLED;
    }
    /* Latched: a count that wraps past 2^32 later does not reopen it. */
    if ((uint32_t)(now_ms - t->start_ms) >= FW_TRIAL_BOUND_MS) {
        t->bound = true;
    }
    if (!quiet) {
        return FW_TRIAL_OPEN;
    }
    if (t->done == FW_TRIAL_ALL) {
        t->settled = true;
        return FW_TRIAL_KEEP;
    }
    if (t->bound && (t->done & FW_TRIAL_TICK) != 0u) {
        t->settled = true;
        return FW_TRIAL_KEEP_BOUND;
    }
    return FW_TRIAL_OPEN;
}

uint32_t fw_trial_missing(const fw_trial_t *t)
{
    return FW_TRIAL_ALL & ~t->done;
}
