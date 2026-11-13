/**
 * @file
 * @brief The IOmcu's 3s console report, one line per pass; see
 *        report_steps.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "report_steps.h"

#include <string.h>

void report_steps_init(report_steps_t *r)
{
    memset(r, 0, sizeof(*r));
}

/**
 * @brief Starts the report when it is due: the last one abandoned, then a
 *        snapshot when a host takes the lines.
 *
 * @param r       The state.
 * @param io      The transport and the snapshot's source.
 * @param now_ms  The pass's clock, ms.
 */
static void start_when_due(report_steps_t *r, const report_io_t *io,
                           uint32_t now_ms)
{
    if (r->started && (uint32_t)(now_ms - r->last_ms) < REPORT_PERIOD_MS) {
        return;
    }
    r->started = true;
    r->last_ms = now_ms;
    if (r->running) {
        ++r->skipped;
        r->running = false;
    }
    if (!io->host(io->ctx)) {
        return;
    }
    io->snap(io->ctx, &r->snap);
    r->running = true;
    r->next    = 0u;
}

bool report_steps_pass(report_steps_t *r, const report_io_t *io,
                       uint32_t now_ms)
{
    start_when_due(r, io, now_ms);
    if (!r->running) {
        return false;
    }
    if (io->room(io->ctx) < REPORT_LINE_MAX) {
        return false;
    }
    while (r->next < (unsigned)REPORT_LINES) {
        const size_t n = report_line(&r->snap, r->next, r->line,
                                     sizeof(r->line));
        ++r->next;
        if (n > 0u) {
            io->put(io->ctx, r->line, n);
            if (r->next >= (unsigned)REPORT_LINES) {
                r->running = false;
            }
            return true;
        }
    }
    r->running = false;
    return false;
}
