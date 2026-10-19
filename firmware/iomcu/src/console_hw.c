/*
 * The IOMCU_CONSOLE build's console; see console_hw.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "console_hw.h"

#include "pico/stdio.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "page_console.h"

/* Characters taken in one pass: a pasted line of PAGE_CONSOLE_LINE_MAX
 * takes 3 passes at most. */
#define CONSOLE_HW_KEYS_PER_PASS 64u

static page_console_t s_console;
/* The lines of one pass, off the main loop's stack. */
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
    /* Room as the transmit buffer has it now: a write of no more than
     * that returns without waiting for the host. */
    char *buf = s_out;
    const bool connected = stdio_usb_connected();
    size_t room = connected ? (size_t)tud_cdc_write_available() : 0u;
    if (room > sizeof(s_out)) {
        room = sizeof(s_out);
    }
    const size_t n = page_console_take(&s_console, buf, room);
    if (n > 0u) {
        (void)stdio_put_string(buf, (int)n, false, false);
    }
}
