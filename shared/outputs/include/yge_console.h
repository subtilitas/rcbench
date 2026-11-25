/**
 * @file yge_console.h
 * @brief The bench console's `yge` command: the YGE_PROG page's status and its
 *        operations, in the same frames a panel sends.  A build option only
 *        (IOMCU_CONSOLE), for a station with no panel screen for it.
 *
 * @verbatim
 *   yge                         a line: state, op, result, refusal, flags, line,
 *                               GPIO, port, session result, guard refusal,
 *                               entries, reads, type code, firmware, time
 *   yge open port <n>           OPEN
 *   yge enter mv=<mV> ma=<mA> [boot=<ms>]
 *                               ENTER, SWITCHED; no default voltage or current;
 *                               boot defaults to 5000ms
 *   yge enter hand [boot=<ms>]  ENTER, BY HAND; no supply is switched
 *   yge done|read|leave|stop|abort|clear|close
 * @endverbatim
 *
 * `done` is HAND_DONE.  No command writes to an ESC: a write comes from a
 * panel, with its draft.  As with every page, the page needs a heartbeat and a
 * link that is not silent, so a running panel beside the console
 * (page_console.h).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_YGE_CONSOLE_H
#define RCBENCH_YGE_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The way to the page and to the console's output. */
typedef struct {
    /** @brief Read YGE_PROG. */
    void (*read)(uint8_t off, uint8_t n, uint16_t *out);
    /** @brief Write YGE_PROG; 0 or a link_nack_t. */
    uint8_t (*write)(uint8_t off, uint8_t n, const uint16_t *in);
    /** @brief One line out, printf style. */
    void (*say)(const char *fmt, ...);
} yge_console_io_t;

/**
 * @brief A console line that starts with `yge`.
 * @param io  the page and the output
 * @param tok the line's tokens
 * @param n   how many, 1 or more
 * @return true: the line was this command's
 */
bool yge_console_line(const yge_console_io_t *io, char **tok, unsigned n);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_YGE_CONSOLE_H */
