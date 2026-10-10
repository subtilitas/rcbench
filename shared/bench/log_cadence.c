/*
 * SPDX-License-Identifier: MIT
 */

#include "log_cadence.h"

void log_cadence_init(log_cadence_t *c, uint32_t now_ms)
{
    c->model_ms = now_ms;
    log_cadence_run_start(c, now_ms);
}

bool log_cadence_model_due(log_cadence_t *c, uint32_t now_ms, bool link_up,
                           float *step_s)
{
    *step_s = 0.0f;
    uint32_t since = (uint32_t)(now_ms - c->model_ms);
    if (since < LOG_CADENCE_MODEL_MS) {
        return false;
    }
    c->model_ms = now_ms;
    if (link_up) {
        return false;
    }
    if (since > LOG_CADENCE_MODEL_MAX_MS) {
        since = LOG_CADENCE_MODEL_MAX_MS;
    }
    *step_s = (float)since / 1000.0f;
    return true;
}

void log_cadence_run_start(log_cadence_t *c, uint32_t now_ms)
{
    c->seen_ms = now_ms;
    c->run_ms  = 0u;
    c->sent    = 0u;
    c->lost    = 0u;
}

bool log_cadence_bench_run(bool bench_kind, bool servo_test)
{
    return bench_kind && !servo_test;
}

bool log_cadence_row(log_cadence_t *c, uint32_t now_ms, bool new_sample,
                     bool bench_run, float *t_s)
{
    /* Summed in 64 bits from differences of the 32-bit tick: neither the
     * tick's wrap nor a run longer than 2^32 ms folds the time back. */
    c->run_ms += (uint32_t)(now_ms - c->seen_ms);
    c->seen_ms = now_ms;
    if (!new_sample || !bench_run) {
        return false;
    }
    *t_s = (float)((double)c->run_ms / 1000.0);
    return true;
}

unsigned log_cadence_rows(unsigned windows)
{
    return (windows > 0u) ? windows : 1u;
}

void log_cadence_posted(log_cadence_t *c, bool taken)
{
    if (taken) {
        ++c->sent;
    } else {
        ++c->lost;
    }
}

uint32_t log_cadence_sent(const log_cadence_t *c)
{
    return c->sent;
}

uint32_t log_cadence_lost(const log_cadence_t *c)
{
    return c->lost;
}
