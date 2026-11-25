/**
 * @file
 * @brief The IOMCU_CONSOLE build's test of the telemetry wire's pin driver of
 *        programming for YGE ESCs (electronic speed controllers), with no ESC
 *        attached: `oyline <gpio> tx`, `oyline <gpio> rx`, `oyline <gpio> cut`
 *        (docs/BenchConsole.md).
 *
 * A bench tool for a logic analyser.  It sends one fixed frame, a telemetry
 * request (`TELE_REQ`, 12B): no write.  It takes a pin only while the bank is
 * SAFE and nothing holds the pin, reserves it against every page until it
 * ends, and closes the line at once when the bank arms.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef OYLINE_CONSOLE_H
#define OYLINE_CONSOLE_H

#include <stdbool.h>

/**
 * @brief A console line that starts with `oyline`.
 * @param ctx unused
 * @param tok the line's tokens
 * @param n   how many
 * @return true when the line was this test's
 */
bool oyline_console_line(void *ctx, char **tok, unsigned n);

/**
 * @brief One pass: the test's next step.  About 1.1ms in a feed step of the
 *        `rx` test, 12 bytes driven by override, else short; this build only.
 */
void oyline_console_pass(void);

#endif /* OYLINE_CONSOLE_H */
