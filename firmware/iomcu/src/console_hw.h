/**
 * @file
 * @brief The IOMCU_CONSOLE build: the bench console on the USB serial port.
 *
 * The bench console is shared/outputs/page_console.h, on the USB (Universal
 * Serial Bus) serial port. A bench tool, in no released image.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CONSOLE_HW_H
#define CONSOLE_HW_H

#include "link_dev.h"

/**
 * @brief Starts the console on the pages of @p dev.
 *
 * Call after link_dev_init().
 *
 * @param dev  The link device whose pages the console serves.
 */
void console_hw_init(const link_dev_t *dev);

/**
 * @brief Runs one console pass, once per pass of the main loop.
 *
 * A pass takes the characters that came, the report of a HITEC operation
 * that has ended, and as much output as the USB transmit buffer has room for
 * now. A line is cut where the room ends, and its rest goes first in the next
 * pass. A line of any length leaves. The pass never waits for the host: it
 * writes no more than the buffer has free, and a host that stops reading
 * leaves the output in the console's buffer.
 */
void console_hw_pass(void);

/**
 * @brief Writes a line to the USB serial port through the console's output.
 *
 * The line is formatted as printf() formats it. It leaves after the lines said
 * before it and never inside one, and saying it does not wait for the host.
 * It is dropped while no host has the port open, as printf() drops it.
 *
 * @param fmt  printf() format string.
 * @param ...  Arguments for @p fmt.
 */
void console_hw_say(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

#endif /* CONSOLE_HW_H */
