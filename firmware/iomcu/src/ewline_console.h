/**
 * @file
 * @brief The IOMCU_CONSOLE build's test of the signal wire's pin driver with
 *        no ESC (electronic speed controller) attached: `ewline <gpio> tx`,
 *        `ewline <gpio> abort`, `ewline <gpio> rx` (docs/BenchConsole.md).
 *
 * A bench tool for a logic analyser.  It sends fixed bytes: no throttle signal,
 * no erase, no program.  It takes a pin only while the bank is SAFE and nothing
 * holds the pin, reserves it against every page until it ends, and closes the
 * line at once when the bank arms.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EWLINE_CONSOLE_H
#define EWLINE_CONSOLE_H

#include <stdbool.h>

/**
 * @brief A console line that starts with `ewline`.
 * @param ctx unused
 * @param tok the line's tokens
 * @param n   how many
 * @return true when the line was this test's
 */
bool ewline_console_line(void *ctx, char **tok, unsigned n);

/**
 * @brief One pass: the test's next step.  At most 6ms in the `rx` test's feed,
 *        else under 200us; this build only.
 */
void ewline_console_pass(void);

#endif /* EWLINE_CONSOLE_H */
