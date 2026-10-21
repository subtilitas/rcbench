/**
 * @file
 * @brief The IOMCU_CONSOLE build: the bench console
 * (shared/outputs/page_console.h) on the USB (Universal Serial Bus) serial
 * port.
 *
 * A bench tool, in no
 * released image.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CONSOLE_HW_H
#define CONSOLE_HW_H

#include "link_dev.h"

/**
 * @brief The console on the pages of @p dev.
 *
 * After link_dev_init().
 *
 * @param dev  The link device whose pages the console serves.
 */
void console_hw_init(const link_dev_t *dev);

/**
 * @brief Once a pass: the characters that came, the report of a HITEC
 * operation that has ended, and as much output as the USB transmit buffer
 * has room for now, a line cut where the room ends and its rest first in the
 * next pass.
 *
 * A line of any length leaves.  Never waits for the host: a pass
 * writes no more than the buffer has free, and a host that stops reading
 * leaves the output in the console's buffer.
 */
void console_hw_pass(void);

/**
 * @brief A line for the USB serial port, formatted as printf() formats,
 * through the console's output: it leaves after the lines said before it and
 * never inside one, and saying it does not wait for the host.
 *
 * Dropped while no
 * host has the port open, as printf() drops it.
 *
 * @param fmt  printf() format string.
 * @param ...  Arguments for the format string.
 */
void console_hw_say(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

#endif /* CONSOLE_HW_H */
