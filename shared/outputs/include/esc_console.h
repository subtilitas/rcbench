/**
 * @file esc_console.h
 * @brief The bench console's `esc` command: the ESC_WIRE page's status and its
 *        operations, in the same frames a panel sends.  A build option only
 *        (IOMCU_CONSOLE), for a station with no panel screen for it.
 *
 * @verbatim
 *   esc                         a line: state, op, result, refusal, flags, line,
 *                               GPIO, session result, firmware, layout, why
 *   esc open slot|port <n>      OPEN
 *   esc enter mv=<mV> ma=<mA> [hold=<ms>] [off=<ms>]
 *                               ENTER, SWITCHED; no default voltage or current;
 *                               hold and off default to 3000ms
 *   esc enter hand [hold=<ms>]  ENTER, BY HAND; no supply is switched
 *   esc done|read|leave|stop|abort|clear|close
 * @endverbatim
 *
 * No command writes a page: a write comes from a panel, with its draft.  As with
 * every page, the page needs a heartbeat and a link that is not silent, so a
 * running panel beside the console (page_console.h).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_ESC_CONSOLE_H
#define RCBENCH_ESC_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The way to the page and to the console's output. */
typedef struct {
    /** @brief Read ESC_WIRE. */
    void (*read)(uint8_t off, uint8_t n, uint16_t *out);
    /** @brief Write ESC_WIRE; 0 or a link_nack_t. */
    uint8_t (*write)(uint8_t off, uint8_t n, const uint16_t *in);
    /** @brief One line out, printf style. */
    void (*say)(const char *fmt, ...);
} esc_console_io_t;

/**
 * @brief A console line that starts with `esc`.
 * @param io  the page and the output
 * @param tok the line's tokens
 * @param n   how many, 1 or more
 * @return true: the line was this command's
 */
bool esc_console_line(const esc_console_io_t *io, char **tok, unsigned n);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_ESC_CONSOLE_H */
