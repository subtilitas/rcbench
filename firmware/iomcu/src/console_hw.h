/*
 * The IOMCU_CONSOLE build: the bench console (shared/outputs/page_console.h)
 * on the USB (Universal Serial Bus) serial port.  A bench tool, in no
 * released image.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CONSOLE_HW_H
#define CONSOLE_HW_H

#include "link_dev.h"

/** The console on the pages of @p dev.  After link_dev_init(). */
void console_hw_init(const link_dev_t *dev);

/** Once a pass: the characters that came, the report of a HITEC operation
 *  that has ended, and the whole lines the transmit buffer has room for. */
void console_hw_pass(void);

#endif /* CONSOLE_HW_H */
