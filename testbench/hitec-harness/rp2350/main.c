/*
 * The Hitec bench harness on an RP2350: protocols/hitec's pin driver on the
 * pin a USB (Universal Serial Bus) serial command names, with the command
 * lines and reports of ../core.
 *
 * After every reset no pin is touched: every GPIO stays as the boot ROM
 * leaves it, an input, until `open`.  `close`, a closed serial port and a
 * watchdog reset (1 s without a pass of the main loop) leave the line pin
 * an input with the family's pull and no override, and the marker pin an
 * input.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "hitec_harness.h"
#include "hitec_line.h"

/* pico-sdk 2.3.0 sends a USB string descriptor of at most 19 characters
 * and cuts the rest; the Pi's by-id port name is made of the product
 * string. */
#define USB_STR_MAX 20u
_Static_assert(sizeof(USBD_PRODUCT) <= USB_STR_MAX,
               "USBD_PRODUCT is cut to 19 characters on the bus");

#define WATCHDOG_MS 1000u

static hitec_line_t s_line;
static hh_line_t    s_state;
static hh_cmd_t     s_open;      /* the open command of the open line */
static hh_cmd_t     s_run;       /* the transaction under way */
static bool         s_busy;
static uint32_t     s_t0_us;

static void emit(void *ctx, const char *line)
{
    (void)ctx;
    puts(line);
}

static void reply(const char *line)
{
    emit(NULL, line);
}

static const char *line_err_name(hitec_line_err_t e)
{
    switch (e) {
    case HITEC_LINE_OK:        return "OK";
    case HITEC_LINE_ERR_ARG:   return "ARG";
    case HITEC_LINE_ERR_CLOCK: return "CLOCK";
    case HITEC_LINE_ERR_PIO:   return "PIO";
    case HITEC_LINE_ERR_DMA:   return "DMA";
    default:                   return "?";
    }
}

static void mark(bool on)
{
    if (s_state.open && s_open.mark != HH_NO_PIN) {
        gpio_put(s_open.mark, on);
    }
}

static void line_close(void)
{
    if (s_busy) {
        hitec_line_abort(&s_line);
        s_busy = false;
    }
    hitec_line_close(&s_line);
    if (s_state.open && s_open.mark != HH_NO_PIN) {
        gpio_deinit(s_open.mark);
    }
    s_state.open = false;
}

static void do_open(const hh_cmd_t *c)
{
    char out[HH_OUT_MAX];
    hitec_line_err_t e;

    line_close();
    e = hitec_line_open(&s_line, c->pin, c->family, c->drive);
    if (e != HITEC_LINE_OK) {
        (void)snprintf(out, sizeof(out), "err open code=%s",
                       line_err_name(e));
        reply(out);
        return;
    }
    s_open = *c;
    s_state.open = true;
    s_state.family = c->family;
    if (c->mark != HH_NO_PIN) {
        gpio_init(c->mark);
        gpio_put(c->mark, false);
        gpio_set_dir(c->mark, GPIO_OUT);
    }
    (void)snprintf(out, sizeof(out),
                   "ok open family=%s pin=%u drive=%s bit_ns=%lu div256=%lu "
                   "err_ppm=%lu sys_hz=%lu mark=%d",
                   c->family == HITEC_D ? "d" : "classic", (unsigned)c->pin,
                   c->drive == HITEC_DRIVE_PUSH_PULL ? "pp" : "od",
                   (unsigned long)hitec_line_bit_ns(&s_line),
                   (unsigned long)s_line.clock.div256,
                   (unsigned long)s_line.clock.err_ppm,
                   (unsigned long)s_line.clock.sys_hz,
                   c->mark == HH_NO_PIN ? -1 : (int)c->mark);
    reply(out);
}

static void do_status(void)
{
    char out[HH_OUT_MAX];

    (void)snprintf(out, sizeof(out),
                   "ok status open=%u busy=%u family=%s pin=%d drive=%s "
                   "mark=%d",
                   s_state.open ? 1u : 0u, s_busy ? 1u : 0u,
                   !s_state.open ? "-"
                       : s_state.family == HITEC_D ? "d" : "classic",
                   s_state.open ? (int)s_open.pin : -1,
                   !s_state.open ? "-"
                       : s_open.drive == HITEC_DRIVE_PUSH_PULL ? "pp" : "od",
                   s_state.open && s_open.mark != HH_NO_PIN
                       ? (int)s_open.mark : -1);
    reply(out);
}

static void do_xfer(const hh_cmd_t *c)
{
    s_run = *c;
    if (!hitec_line_start(&s_line, &s_run.xfer)) {
        reply("err start the driver refused it (a level of 1 needs pp)");
        return;
    }
    s_t0_us = time_us_32();
    mark(true);
    s_busy = true;
    hh_report_start(&s_run, &s_line.run, emit, NULL);
}

static void run_line(const char *text)
{
    char err[HH_OUT_MAX];
    hh_cmd_t c;

    if (!hh_parse(text, &s_state, &c, err, sizeof(err))) {
        reply(err);
        return;
    }
    if (s_busy && c.verb != HH_ABORT && c.verb != HH_STATUS) {
        reply("err busy");
        return;
    }
    switch (c.verb) {
    case HH_HELLO: {
        char out[HH_OUT_MAX];

        (void)snprintf(out, sizeof(out),
                       "ok hello rcbench-hitec-harness proto=%d sys_hz=%lu",
                       HH_PROTOCOL, (unsigned long)clock_get_hz(clk_sys));
        reply(out);
        break;
    }
    case HH_HELP:
        hh_report_help(emit, NULL);
        break;
    case HH_STATUS:
        do_status();
        break;
    case HH_OPEN:
        do_open(&c);
        break;
    case HH_CLOSE:
        line_close();
        reply("ok close");
        break;
    case HH_ABORT:
        if (!s_busy) {
            reply("err idle");
        } else {
            /* The capture with aborted=1 follows from the poll. */
            hitec_line_abort(&s_line);
            reply("ok abort");
        }
        break;
    default:
        do_xfer(&c);
        break;
    }
}

int main(void)
{
    static char text[HH_LINE_MAX + 2u];
    size_t n = 0;
    bool overlong = false;
    bool was_open = false;

    stdio_init_all();
    hitec_line_init(&s_line);
    watchdog_enable(WATCHDOG_MS, true);

    for (;;) {
        watchdog_update();

        if (s_busy) {
            hitec_capture_t cap;

            if (hitec_line_poll(&s_line, &cap)) {
                const uint32_t t_us = time_us_32() - s_t0_us;

                mark(false);
                s_busy = false;
                hh_report_capture(&s_run, &cap, t_us, emit, NULL);
            }
        }

        const bool host = stdio_usb_connected();
        if (was_open && !host) {
            line_close();       /* nobody reads: the pin goes back */
        }
        was_open = host;

        for (;;) {
            const int ch = getchar_timeout_us(0);

            if (ch == PICO_ERROR_TIMEOUT) {
                break;
            }
            if (ch == '\n') {
                text[n] = '\0';
                if (overlong) {
                    reply("err line too long");
                } else {
                    run_line(text);
                }
                n = 0;
                overlong = false;
                break;          /* one command a pass: the poll comes first */
            }
            if (n < HH_LINE_MAX) {
                text[n++] = (char)ch;
            } else {
                overlong = true;
            }
        }
    }
}
