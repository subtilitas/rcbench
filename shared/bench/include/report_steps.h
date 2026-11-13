/**
 * @file
 * @brief The IOmcu's 3s console report, one line per main loop pass.
 *
 * Pure: the snapshot, the room check and the output are the caller's
 * functions (report_io_t).  Rules:
 *
 * - Due on the first pass, then each time REPORT_PERIOD_MS have passed
 *   since the last due time, wrap-safe.
 * - With no host connected at the due time the report is skipped whole:
 *   no snapshot, no line.
 * - Otherwise the snapshot is taken once, at the due pass, and the lines
 *   leave one per pass in their order.  A line is formatted only when the
 *   transport has room for REPORT_LINE_MAX bytes; without it the pass does
 *   nothing and the line waits.  Lines that do not apply are passed over in
 *   the same pass at no cost.
 * - A report still unfinished at the next due time is abandoned and counted
 *   in skipped; the new one starts with a fresh snapshot.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "report_lines.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Time from one report's start to the next, in ms. */
#define REPORT_PERIOD_MS 3000u

/** @brief The transport and the source of the snapshot. */
typedef struct {
    /** @brief Whether a host takes the lines now. */
    bool (*host)(void *ctx);
    /** @brief Bytes the transport takes now without waiting. */
    size_t (*room)(void *ctx);
    /** @brief Fills the snapshot from the pages, at the due pass. */
    void (*snap)(void *ctx, report_snap_t *s);
    /** @brief Hands one line of @p n bytes to the transport. */
    void (*put)(void *ctx, const char *line, size_t n);
    void *ctx; /**< handed to each function */
} report_io_t;

/** @brief The report's state. */
typedef struct {
    bool          started; /**< a report has been due                    */
    bool          running; /**< a report has lines left                  */
    uint32_t      last_ms; /**< the last due time, ms                    */
    unsigned      next;    /**< the next line, a report_line_t            */
    uint32_t      skipped; /**< reports abandoned unfinished, modulo 2^32 */
    report_snap_t snap;    /**< the snapshot of the running report       */
    char          line[REPORT_LINE_MAX]; /**< the line being handed out  */
} report_steps_t;

/**
 * @brief Clears the state: the first pass is due.
 *
 * @param r  The state.
 */
void report_steps_init(report_steps_t *r);

/**
 * @brief One pass: starts a due report, and hands out at most one line.
 *
 * @param r       The state.
 * @param io      The transport and the snapshot's source.
 * @param now_ms  The pass's clock, ms.
 * @return true when a line was handed to the transport.
 */
bool report_steps_pass(report_steps_t *r, const report_io_t *io,
                       uint32_t now_ms);

#ifdef __cplusplus
}
#endif
