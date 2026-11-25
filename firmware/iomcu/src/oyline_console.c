/**
 * @file
 * @brief The console's test of the telemetry wire's pin driver of programming
 *        for YGE ESCs; see oyline_console.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "oyline_console.h"

#include <string.h>

#include "hardware/gpio.h"
#include "hardware/timer.h"

#include "pico/time.h"

#include "console_hw.h"
#include "iomcu.h"
#include "iomcu_rsn_pio.h"
#include "openyge.h"
#include "oy_line.h"
#include "page_console.h"
#include "yge_console.h"

/** @brief What one step does. */
typedef enum {
    STEP_END = 0,
    STEP_PARK,    /**< the line parked for @c ms                           */
    STEP_LISTEN,  /**< the line listening for @c ms, the bytes counted     */
    STEP_SEND,    /**< the request handed over, the step ends at its release */
    STEP_CUT,     /**< the request handed over, parked @c ms after         */
    STEP_FEED,    /**< the request driven onto the pad by override, then
                       its bytes taken and checked                         */
} step_kind_t;

/** @brief One step of a test. */
typedef struct {
    uint8_t  kind;   /**< a step_kind_t                                     */
    uint8_t  bad;    /**< STEP_FEED: 1 + a byte with a low stop bit, 0 none */
    uint16_t ms;     /**< how long; for STEP_CUT in 0.1ms                   */
} step_t;

/** @brief The device a request addresses: 1, as Rotorflight does (K6). */
#define OY_DEVICE 1u
/** @brief The longest a send step waits for its release, us. */
#define SEND_LIMIT_US 5000u

/** @brief A step: parked for @p t ms. */
#define PARK(t) { STEP_PARK, 0u, (t) }
/** @brief A step: listening for @p t ms. */
#define LISTEN(t) { STEP_LISTEN, 0u, (t) }
/** @brief A step: the request, to its release. */
#define SEND { STEP_SEND, 0u, 0u }
/** @brief The end of a test. */
#define END { STEP_END, 0u, 0u }

/** @brief The transmit test: every line state, 3 requests 40ms apart. */
static const step_t k_tx[] = {
    PARK(20u), LISTEN(20u),
    SEND, LISTEN(40u), SEND, LISTEN(40u), SEND, LISTEN(40u),
    PARK(20u), END,
};
/** @brief The receive test: a request fed back, then one with a low stop bit
 *         in byte 5. */
static const step_t k_rx[] = {
    LISTEN(5u), { STEP_FEED, 0u, 0u }, LISTEN(5u),
    { STEP_FEED, 5u, 0u }, LISTEN(5u), PARK(5u), END,
};
/** @brief The cut test: parked 0.5ms into a request of 1.04ms. */
static const step_t k_cut[] = {
    LISTEN(5u), { STEP_CUT, 0u, 5u }, PARK(20u), LISTEN(5u), SEND, LISTEN(5u),
    PARK(20u), END,
};

static oy_pin_line_t s_line;        /**< the line under test */
static const step_t *s_step;        /**< the step, NULL with no test running */
static uint64_t s_pin_bit;          /**< this test's bit in s_console_pins */
static uint32_t s_t0_us;            /**< the step's start */
static bool s_in_step;              /**< the step has begun */
static uint8_t s_seq;               /**< the sequence number of the next request */
static uint8_t s_req[OPENYGE_HDR_V3 + OPENYGE_CONTROL_BYTES
                     + OPENYGE_CRC_BYTES]; /**< the request */
static uint32_t s_req_n;            /**< its bytes */
static uint32_t s_rx_bytes;         /**< bytes taken while listening */
static uint32_t s_tx_max;           /**< the longest oy_line_tx(), us */
static uint32_t s_rx_max;           /**< the longest oy_line_rx(), us */
static uint32_t s_park_us;          /**< the longest oy_line_park(), us */
static uint32_t s_out_max;          /**< the longest hand-over to release, us */

/**
 * @brief Note the longest call of a kind.
 * @param max the longest so far
 * @param t0  the call's start, us
 */
static void note(uint32_t *max, uint32_t t0)
{
    const uint32_t d = time_us_32() - t0;

    if (d > *max) {
        *max = d;
    }
}

/**
 * @brief Whether the test may take a pin: the bank SAFE, nothing reserved, no
 *        slot and no driver on it, and it is no output.
 * @param pin the GPIO
 * @return true when it may
 */
static bool pin_free(unsigned pin)
{
    const gpio_function_t fn = gpio_get_function(pin);

    reserve_held();
    if (s_outputs.armed || !outputs_pin_available(&s_outputs, (uint8_t)pin)
        || (fn != GPIO_FUNC_SIO && fn != GPIO_FUNC_NULL)
        || gpio_is_dir_out(pin)) {
        return false;
    }
    for (unsigned i = 0u; i < OUT_MAX_SLOTS; ++i) {
        if (s_outputs.slot[i].driver != OUT_DRIVER_NONE
            && s_outputs.slot[i].pin == pin) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Take what the line received and count it.
 * @return the bytes taken
 */
static uint32_t drain(void)
{
    uint8_t buf[32];
    uint32_t total = 0u;
    size_t n;

    do {
        const uint32_t t0 = time_us_32();

        n = oy_line_rx(&s_line, buf, sizeof(buf));
        note(&s_rx_max, t0);
        total += (uint32_t)n;
    } while (n != 0u);
    s_rx_bytes += total;
    return total;
}

/**
 * @brief End the test: the line closed, the counts and times said.
 * @param why the line's word
 */
static void finish(const char *why)
{
    const unsigned pin = s_line.pin;

    if (s_line.listening != 0u) {
        (void)drain();
    }
    oy_line_close(&s_line);
    place_line_gone();
    s_console_pins &= ~s_pin_bit;
    s_pin_bit = 0u;
    reserve_held();
    s_step = NULL;
    console_hw_say("oyline %s gpio=%u sent=%lu dropped=%lu rx=%lu framing=%lu "
                   "full=%lu tx_max=%luus rx_max=%luus park=%luus "
                   "out_max=%luus\n",
                   why, pin, (unsigned long)s_line.sent,
                   (unsigned long)s_line.tx_dropped,
                   (unsigned long)s_rx_bytes,
                   (unsigned long)s_line.rx.framing,
                   (unsigned long)s_line.rx.full, (unsigned long)s_tx_max,
                   (unsigned long)s_rx_max, (unsigned long)s_park_us,
                   (unsigned long)s_out_max);
}

/**
 * @brief A read of YGE_PROG, as a link request reads it.
 * @param off the first register
 * @param n   registers
 * @param out takes them
 */
static void yge_io_read(uint8_t off, uint8_t n, uint16_t *out)
{
    yge_read(&s_state, off, n, out);
}

/**
 * @brief A write of YGE_PROG, as a link request writes it.
 * @param off the first register
 * @param n   registers
 * @param in  their values
 * @return 0, or a link_nack_t
 */
static uint8_t yge_io_write(uint8_t off, uint8_t n, const uint16_t *in)
{
    return yge_write(&s_state, off, n, in);
}

/** @brief The `yge` command's way to the page. */
static const yge_console_io_t k_yge_io = {
    yge_io_read, yge_io_write, console_hw_say,
};

bool oyline_console_line(void *ctx, char **tok, unsigned n)
{
    const step_t *steps = NULL;
    uint32_t pin = NUM_BANK0_GPIOS;
    oy_line_err_t e;

    (void)ctx;
    if (n != 0u && strcmp(tok[0], "yge") == 0) {
        return yge_console_line(&k_yge_io, tok, n);
    }
    if (n == 0u || strcmp(tok[0], "oyline") != 0) {
        return false;
    }
    if (n == 3u && strcmp(tok[2], "tx") == 0) {
        steps = k_tx;
    } else if (n == 3u && strcmp(tok[2], "rx") == 0) {
        steps = k_rx;
    } else if (n == 3u && strcmp(tok[2], "cut") == 0) {
        steps = k_cut;
    }
    if (steps == NULL || !page_console_number(tok[1], &pin)
        || pin >= NUM_BANK0_GPIOS) {
        console_hw_say("err usage: oyline <gpio> tx|rx|cut\n");
        return true;
    }
    if (s_step != NULL || s_console_pins != 0u) {
        console_hw_say("err oyline: a test runs\n");
        return true;
    }
    if (!pin_free((unsigned)pin)) {
        console_hw_say("err oyline: gpio %lu is held, or the bank is ARMED\n",
                       (unsigned long)pin);
        return true;
    }
    oy_line_init(&s_line);
    /* On board 3 into a block that holds no output (iomcu_rsn_pio.h). */
    if (!place_begin(RSN_PIO_LINE, (uint8_t)pin)) {
        console_hw_say("err oyline: no PIO block free\n");
        return true;
    }
    e = oy_line_open(&s_line, (unsigned)pin);
    place_end(e == OY_LINE_OK);
    if (e != OY_LINE_OK) {
        console_hw_say("err oyline: open %d\n", (int)e);
        return true;
    }
    /* From here on no page takes the pin; finish() gives it back. */
    s_pin_bit = (uint64_t)1u << pin;
    s_console_pins |= s_pin_bit;
    reserve_held();
    console_hw_say("oyline open gpio=%lu bit=%luns div=%lu+%lu/256\n",
                   (unsigned long)pin,
                   (unsigned long)oy_line_bit_ns(&s_line),
                   (unsigned long)(s_line.clock.div256 >> 8),
                   (unsigned long)(s_line.clock.div256 & 0xFFu));
    s_seq = 1u;
    s_rx_bytes = 0u;
    s_tx_max = 0u;
    s_rx_max = 0u;
    s_park_us = 0u;
    s_out_max = 0u;
    s_in_step = false;
    s_step = steps;
    return true;
}

/**
 * @brief Build the next request and hand it over.
 * @return false when the driver refused it
 */
static bool send_request(void)
{
    const uint32_t t0 = time_us_32();
    bool ok;

    s_req_n = (uint32_t)openyge_build_telemetry_request(s_req, sizeof(s_req),
                                                        OY_DEVICE, s_seq++);
    ok = oy_line_tx(&s_line, s_req, s_req_n);
    note(&s_tx_max, t0);
    return ok;
}

/**
 * @brief Drive one byte onto the pad by the pin's overrides, blocking for its
 *        10 bit times.
 * @param value  the byte
 * @param stop   the stop bit's level
 * @param bit_ns the bit time
 */
static void feed_byte(uint8_t value, bool stop, uint32_t bit_ns)
{
    const uint pin = s_line.pin;
    const absolute_time_t t0 = get_absolute_time();
    const uint32_t bits = ((uint32_t)value << 1) | (stop ? 0x200u : 0u);

    for (uint32_t b = 0u; b < 10u; ++b) {
        gpio_set_outover(pin, ((bits >> b) & 1u) != 0u ? GPIO_OVERRIDE_HIGH
                                                       : GPIO_OVERRIDE_LOW);
        busy_wait_until(delayed_by_us(t0, ((b + 1u) * bit_ns + 500u) / 1000u));
    }
}

/**
 * @brief A request on the pad while the receiver runs, the same pad the
 *        machine reads; then its bytes taken and checked.
 * @param bad 1 + the byte with a low stop bit, 0 none
 */
static void feed(uint8_t bad)
{
    const uint pin = s_line.pin;
    const uint32_t bit_ns = oy_line_bit_ns(&s_line);
    const uint32_t framing0 = s_line.rx.framing;
    openyge_decoder_t dec;
    openyge_frame_t fr;
    uint8_t got[32];
    uint32_t n = 0u;
    bool framed = false;
    size_t k;

    s_req_n = (uint32_t)openyge_build_telemetry_request(s_req, sizeof(s_req),
                                                        OY_DEVICE, s_seq++);
    gpio_set_outover(pin, GPIO_OVERRIDE_HIGH);
    gpio_set_oeover(pin, GPIO_OVERRIDE_HIGH);
    for (uint32_t i = 0u; i < s_req_n; ++i) {
        feed_byte(s_req[i], bad != i + 1u, bit_ns);
        /* The receiver waits for the idle level after a low stop bit. */
        if (bad == i + 1u) {
            gpio_set_outover(pin, GPIO_OVERRIDE_HIGH);
            busy_wait_us_32(2u * bit_ns / 1000u);
        }
    }
    gpio_set_outover(pin, GPIO_OVERRIDE_NORMAL);
    gpio_set_oeover(pin, GPIO_OVERRIDE_NORMAL);
    /* The last byte's push is 1 bit time after its stop bit's middle. */
    busy_wait_us_32(2u * bit_ns / 1000u);
    do {
        const uint32_t t0 = time_us_32();

        k = oy_line_rx(&s_line, got + n, sizeof(got) - n);
        note(&s_rx_max, t0);
        n += (uint32_t)k;
    } while (k != 0u && n < sizeof(got));
    s_rx_bytes += n;
    openyge_decoder_reset(&dec);
    for (uint32_t i = 0u; i < n; ++i) {
        framed = openyge_decode_byte(&dec, got[i], &fr) || framed;
    }
    console_hw_say("oyline feed n=%lu framing=%lu match=%d crc_ok=%d\n",
                   (unsigned long)n,
                   (unsigned long)(s_line.rx.framing - framing0),
                   (n == s_req_n && memcmp(got, s_req, s_req_n) == 0) ? 1 : 0,
                   framed ? 1 : 0);
}

/**
 * @brief A step's first pass.
 * @param st the step
 * @return false when the test ended
 */
static bool step_begin(const step_t *st)
{
    s_t0_us = time_us_32();
    switch (st->kind) {
    case STEP_PARK: {
        const uint32_t t0 = time_us_32();

        oy_line_park(&s_line);
        note(&s_park_us, t0);
        break;
    }
    case STEP_LISTEN:
        oy_line_listen(&s_line);
        break;
    case STEP_FEED:
        feed(st->bad);
        break;
    default:
        if (!send_request()) {
            finish("refused");
            return false;
        }
        s_t0_us = time_us_32();
        break;
    }
    return true;
}

void oyline_console_pass(void)
{
    if (s_step == NULL) {
        return;
    }
    /* An arming goes first, whatever step this is. */
    if (s_outputs.armed) {
        finish("armed");
        return;
    }
    const step_t *st = s_step;

    if (st->kind == STEP_END) {
        finish("done");
        return;
    }
    if (!s_in_step) {
        if (step_begin(st)) {
            s_in_step = true;
        }
        return;
    }
    const uint32_t elapsed = time_us_32() - s_t0_us;
    bool over = false;

    switch (st->kind) {
    case STEP_PARK:
        over = elapsed >= st->ms * 1000u;
        break;
    case STEP_LISTEN:
        (void)drain();
        over = elapsed >= st->ms * 1000u;
        break;
    case STEP_CUT:
        if (elapsed >= st->ms * 100u) {
            const uint32_t t0 = time_us_32();

            oy_line_park(&s_line);
            note(&s_park_us, t0);
            over = true;
        }
        break;
    case STEP_SEND:
        /* The release: the stream out and the machine off the frame's way
         * out.  A line held low ends the test. */
        if (oy_line_sent(&s_line)) {
            if (elapsed > s_out_max) {
                s_out_max = elapsed;
            }
            over = true;
        } else if (elapsed >= SEND_LIMIT_US) {
            finish("stuck");
            return;
        }
        break;
    default:
        over = true;
        break;
    }
    if (over) {
        s_in_step = false;
        s_step = st + 1;
    }
}
