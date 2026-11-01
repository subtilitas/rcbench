/**
 * @file
 * @brief The IOMCU_CONSOLE build: the bench console
 * (shared/outputs/page_console.h) on the USB (Universal Serial Bus)
 * serial port.
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
 * @brief The CONSOLE page's read (link_pages_console.h).
 *
 * @param ctx  Unused.
 * @param off  First register.
 * @param n    Registers.
 * @param out  Takes them.
 */
void console_hw_page_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out);

/**
 * @brief The CONSOLE page's write.
 *
 * @param ctx  Unused.
 * @param off  First register.
 * @param n    Registers.
 * @param in   Their values.
 * @return 0, or a link_nack_t.
 */
uint8_t console_hw_page_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in);

/**
 * @brief A line for the USB serial port, formatted as printf() formats,
 * through the console's output: it leaves after the lines said before it and
 * never inside one, and saying it does not wait for the host.
 *
 * Held until a USB host or a panel on the CONSOLE page takes it, as far as the
 * console's buffer holds it.
 *
 * @param fmt  printf() format string.
 * @param ...  Arguments for the format string.
 */
void console_hw_say(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

#endif /* CONSOLE_HW_H */
