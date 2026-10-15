/*
 * The touch injector on a Waveshare RP2350-Zero: the pins, the USB serial
 * port, the I2C (Inter-Integrated Circuit) target and the status LED around
 * the pure-C core in ../core.
 *
 *   GP4   SDA, I2C0 target at 0x14 once confirmed; an input before
 *   GP5   SCL, as GP4
 *   GP6   GT911 INT, reserved: an input, never driven
 *   GP7   panel RESET sense, reserved: an input
 *   GP16  WS2812 status LED, bytes in RGB order
 *
 * After every reset GP4 to GP7 are inputs with their pulls off and the I2C
 * block is not attached: the injector acknowledges nothing until the host
 * has run `wiring` and sent `confirm scl=GP5` (injector.h).  Core 1 does the
 * listening for `wiring`; core 0 runs everything else.  The watchdog resets
 * the chip when the main loop stalls for 200 ms, and a reset is LISTEN.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "hardware/watchdog.h"
#include "pico/i2c_slave.h"
#include "pico/multicore.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "injector.h"
#include "ws2812.pio.h"

#define PIN_SDA     4u
#define PIN_SCL     5u
#define PIN_INT     6u
#define PIN_RST     7u
#define PIN_LED     16u

/* pico-sdk 2.3.0 sends a USB string descriptor of at most 19 characters
 * (USBD_DESC_STR_MAX 20 in stdio_usb_descriptors.c) and cuts the rest; the
 * Pi's by-id port name is made of the product string. */
#define USB_STR_MAX 20u
_Static_assert(sizeof(USBD_PRODUCT) <= USB_STR_MAX,
               "USBD_PRODUCT is cut to 19 characters on the bus");

#define WATCHDOG_MS 200u
#define WIRING_US   500000u
/* Level changes the listening window keeps: 128 KiB of RAM.  The panel's
 * polling at 400 kHz makes about 10 000 in 500 ms (not measured). */
#define EDGE_MAX    16384u

static injector_t s_inj;
static bool       s_attached;

/* ------------------------------------------------------------ listening */

static inj_edge_t        s_edges[EDGE_MAX];
static volatile uint32_t s_edge_n;
static volatile uint32_t s_samples;
static volatile uint32_t s_window_us;
static volatile uint8_t  s_first;

static uint8_t lines(uint32_t gpio_in)
{
    return (uint8_t)((gpio_in >> PIN_SDA) & 0x3u);
}

/* From RAM, so a flash cache miss never stalls a sample.  The clock is
 * read once every 1024 samples. */
static void __not_in_flash_func(sample_window)(void)
{
    const uint32_t mask = (1u << PIN_SDA) | (1u << PIN_SCL);
    const uint32_t t0 = time_us_32();
    uint32_t last = sio_hw->gpio_in & mask;
    uint32_t i = 0;
    uint32_t n = 0;
    s_first = lines(last);
    for (;;) {
        const uint32_t s = sio_hw->gpio_in & mask;
        if (s != last) {
            if (n == EDGE_MAX) {
                break;
            }
            s_edges[n].at = i;
            s_edges[n].state = lines(s);
            ++n;
            last = s;
        }
        ++i;
        if ((i & 0x3FFu) == 0u && time_us_32() - t0 >= WIRING_US) {
            break;
        }
    }
    s_window_us = time_us_32() - t0;
    s_samples = i;
    s_edge_n = n;
}

static void core1_main(void)
{
    for (;;) {
        (void)multicore_fifo_pop_blocking();
        sample_window();
        multicore_fifo_push_blocking(1u);
    }
}

static void classify(inj_line_result_t *r)
{
    const uint32_t us = s_window_us ? s_window_us : 1u;
    const uint32_t ksps = (uint32_t)((uint64_t)s_samples * 1000u / us);
    inj_line_classify(s_first, s_edges, s_edge_n, s_samples, ksps, r);
}

/* ------------------------------------------------------------- the pins */

static void pins_listen(void)
{
    const uint pins[] = { PIN_SDA, PIN_SCL, PIN_INT, PIN_RST };
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_disable_pulls(pins[i]);
    }
}

/* ---------------------------------------------------------- the target */

static void __not_in_flash_func(i2c_event)(i2c_inst_t *i2c,
                                           i2c_slave_event_t ev)
{
    switch (ev) {
    case I2C_SLAVE_RECEIVE:
        while (i2c_get_read_available(i2c) > 0u) {
            gt911_emu_rx(&s_inj.emu, i2c_read_byte_raw(i2c));
        }
        break;
    case I2C_SLAVE_REQUEST:
        i2c_write_byte_raw(i2c, gt911_emu_tx(&s_inj.emu));
        break;
    case I2C_SLAVE_FINISH:
        gt911_emu_finish(&s_inj.emu);
        break;
    default:
        break;
    }
}

static void attach_target(void)
{
    /* The block's set-up and hold times for 400 kHz, then target mode,
     * then the pins: the lines reach the block only once it is a target. */
    (void)i2c_init(i2c0, 400000u);
    i2c_slave_init(i2c0, GT911_EMU_ADDR, i2c_event);
    irq_set_priority(I2C0_IRQ, 0);
    gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
    gpio_disable_pulls(PIN_SDA);
    gpio_disable_pulls(PIN_SCL);
    s_attached = true;
}

/* The core's state is shared with the I2C interrupt (injector.h). */
static void lock(void)
{
    if (s_attached) {
        irq_set_enabled(I2C0_IRQ, false);
    }
}

static void unlock(void)
{
    if (s_attached) {
        irq_set_enabled(I2C0_IRQ, true);
    }
}

/* -------------------------------------------------------------- the LED */

static PIO  s_pio = pio0;
static uint s_sm;

static void led_init(void)
{
    const uint offset = pio_add_program(s_pio, &ws2812_program);
    s_sm = (uint)pio_claim_unused_sm(s_pio, true);
    pio_gpio_init(s_pio, PIN_LED);
    pio_sm_set_consecutive_pindirs(s_pio, s_sm, PIN_LED, 1, true);
    pio_sm_config c = ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, PIN_LED);
    sm_config_set_out_shift(&c, false, true, 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / 8000000.0f);
    pio_sm_init(s_pio, s_sm, offset, &c);
    pio_sm_set_enabled(s_pio, s_sm, true);
}

static void led_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    /* The RP2350-Zero's LED takes its bytes red, green, blue: GRB order
     * shows the dim green of a polled target as red. */
    static uint32_t shown = 0xFFFFFFFFu;
    const uint32_t rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    if (rgb == shown || pio_sm_is_tx_fifo_full(s_pio, s_sm)) {
        return;
    }
    shown = rgb;
    pio_sm_put(s_pio, s_sm, rgb << 8u);
}

static void led_show(inj_led_t led, uint32_t now_ms)
{
    switch (led) {
    case INJ_LED_LISTEN:
        if ((now_ms / 500u) % 2u == 0u) {
            led_rgb(16, 16, 16);
        } else {
            led_rgb(0, 0, 0);
        }
        break;
    case INJ_LED_WIRING_BAD: led_rgb(24, 16, 0); break;
    case INJ_LED_POLLED:     led_rgb(0, 6, 0);   break;
    case INJ_LED_DOWN:       led_rgb(0, 0, 32);  break;
    case INJ_LED_FAILED:     led_rgb(32, 0, 0);  break;
    default:                 led_rgb(0, 0, 0);   break;
    }
}

/* ------------------------------------------------------------- the loop */

int main(void)
{
    pins_listen();
    stdio_init_all();
    led_init();
    injector_init(&s_inj);
    multicore_launch_core1(core1_main);
    watchdog_enable(WATCHDOG_MS, true);

    bool was_open = false;
    for (;;) {
        watchdog_update();
        const uint32_t now = to_ms_since_boot(get_absolute_time());

        char in[64];
        size_t n = 0;
        while (n < sizeof(in)) {
            const int c = getchar_timeout_us(0);
            if (c == PICO_ERROR_TIMEOUT) {
                break;
            }
            in[n++] = (char)c;
        }
        const bool open = stdio_usb_connected();

        lock();
        if (was_open && !open) {
            injector_host_closed(&s_inj);
        }
        if (n > 0u) {
            injector_input(&s_inj, in, n, now);
        }
        injector_poll(&s_inj, now);
        const bool start_wiring = injector_take_wiring(&s_inj);
        const bool attach = injector_take_attach(&s_inj);
        unlock();
        was_open = open;

        if (start_wiring) {
            multicore_fifo_push_blocking(1u);
        }
        if (multicore_fifo_rvalid()) {
            (void)multicore_fifo_pop_blocking();
            inj_line_result_t r;
            classify(&r);
            lock();
            injector_wiring_done(&s_inj, &r);
            unlock();
        }
        if (attach && !s_attached) {
            attach_target();
        }

        for (;;) {
            char line[INJ_REPLY_LEN];
            lock();
            const bool have = injector_take_reply(&s_inj, line, sizeof(line));
            unlock();
            if (!have) {
                break;
            }
            if (open) {
                puts(line);
            }
        }
        led_show(injector_led(&s_inj, now), now);
    }
}
