/**
 * @file panel_console.h
 * @brief The RCBENCH_PANEL_CONSOLE build: the IOmcu's bench console on the
 *        panel's serial port, over the CONSOLE page (docs/BenchConsole.md).
 *
 * A debug tool, in no released image.  Built without the option every call
 * below is an empty inline function and panel_console.c is not compiled.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#if RCBENCH_PANEL_CONSOLE

/**
 * @brief The console's task: reads the serial port and prints what comes back.
 */
void panel_console_start(void);

/**
 * @brief Control task, once a pass the link is its own: the link's state and at
 *        most one exchange of the CONSOLE page.
 *
 * @param link_up The link is up.
 */
void panel_console_pass(bool link_up);

#else

/** @brief Nothing, built without the option. */
static inline void panel_console_start(void) {}

/** @brief Nothing, built without the option. @param link_up Unused. */
static inline void panel_console_pass(bool link_up) { (void)link_up; }

#endif
