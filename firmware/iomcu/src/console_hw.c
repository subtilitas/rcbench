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

#include "ewline_console.h"
#include "console_tunnel.h"
#include "page_console.h"

/**
 * @brief Characters taken in one pass: a pasted line of
 * PAGE_CONSOLE_LINE_MAX takes 3 passes at most.
 */
#define CONSOLE_HW_KEYS_PER_PASS 64u

static page_console_t s_console;
/** @brief The console's bytes over the link (the CONSOLE page). */
static console_tunnel_t s_tunnel;
/**
 * @brief The output of one pass, off the main loop's stack.
 */
static char s_out[PAGE_CONSOLE_OUT_LINE * 2u];

void console_hw_init(const link_dev_t *dev)
{
    page_console_init(&s_console, dev);
    page_console_set_ext(&s_console, ewline_console_line, NULL);
    console_tunnel_init(&s_tunnel, &s_console);
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
    ewline_console_pass();
    /* Room as the transmit buffer has it now: a write of no more than that
     * returns without waiting for the host.  The room filled, a line cut
     * where it ends, so a line of any length leaves whatever
     * CFG_TUD_CDC_TX_BUFSIZE is.  Nothing else writes to the port in this
     * build after the boot (IOMCU_SAY in main.c), so nothing lands inside
     * a cut line.
     *
     * With a panel reading the CONSOLE page, the output also fills its window
     * and leaves at the window's pace.  With no USB host and no panel nothing
     * leaves, and the buffer holds a start's first lines for whoever comes. */
    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());

    console_tunnel_pass(&s_tunnel, now_ms);

    const bool tunnel = console_tunnel_active(&s_tunnel, now_ms);
    const bool connected = stdio_usb_connected();
    size_t room = connected ? (size_t)tud_cdc_write_available()
                            : (tunnel ? sizeof(s_out) : 0u);

    if (room > sizeof(s_out)) {
        room = sizeof(s_out);
    }
    if (tunnel && room > console_tunnel_room(&s_tunnel)) {
        room = console_tunnel_room(&s_tunnel);
    }
    const size_t n = page_console_take_part(&s_console, s_out, room);

    if (n > 0u) {
        if (connected) {
            (void)stdio_put_string(s_out, (int)n, false, false);
        }
        if (tunnel) {
            console_tunnel_put(&s_tunnel, s_out, n);
        }
    }
}

void console_hw_page_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    console_tunnel_read(&s_tunnel, off, n, out);
}

uint8_t console_hw_page_write(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    (void)ctx;
    return console_tunnel_write(&s_tunnel, off, n, in);
}

void console_hw_say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    page_console_vsay(&s_console, fmt, ap);
    va_end(ap);
}
