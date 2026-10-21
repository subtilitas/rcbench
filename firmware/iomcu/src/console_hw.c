/**
 * @file
 * @brief The IOMCU_CONSOLE build's console; see console_hw.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "console_hw.h"

#include <stdarg.h>

#include "pico/stdio.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "page_console.h"

/**
 * @brief Characters taken in one pass: a pasted line of
 * PAGE_CONSOLE_LINE_MAX takes 3 passes at most.
 */
#define CONSOLE_HW_KEYS_PER_PASS 64u

static page_console_t s_console;
/**
 * @brief The output of one pass, off the main loop's stack.
 */
static char s_out[PAGE_CONSOLE_OUT_LINE * 2u];

void console_hw_init(const link_dev_t *dev)
{
    page_console_init(&s_console, dev);
}

void console_hw_pass(void)
{
    for (unsigned i = 0; i < CONSOLE_HW_KEYS_PER_PASS; ++i) {
        const int c = getchar_timeout_us(0u);
        if (c < 0) {
            break;
        }
        page_console_key(&s_console, c);
    }
    page_console_poll(&s_console);
    /* Room as the transmit buffer has it now: a write of no more than that
     * returns without waiting for the host.  The room filled, a line cut
     * where it ends, so a line of any length leaves whatever
     * CFG_TUD_CDC_TX_BUFSIZE is.  Nothing else writes to the port in this
     * build after the boot (IOMCU_SAY in main.c), so nothing lands inside
     * a cut line. */
    const bool connected = stdio_usb_connected();
    const size_t avail = connected ? (size_t)tud_cdc_write_available() : 0u;
    const size_t room = (avail > sizeof(s_out)) ? sizeof(s_out) : avail;
    const size_t n = page_console_take_part(&s_console, s_out, room);
    if (n > 0u) {
        (void)stdio_put_string(s_out, (int)n, false, false);
    }
}

void console_hw_say(const char *fmt, ...)
{
    if (!stdio_usb_connected()) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    page_console_vsay(&s_console, fmt, ap);
    va_end(ap);
}
