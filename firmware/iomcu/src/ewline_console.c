/**
 * @file
 * @brief The console's test of the signal wire's pin driver; see
 *        ewline_console.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ewline_console.h"

#include <string.h>

#include "hardware/gpio.h"
#include "hardware/timer.h"

#include "pico/time.h"

#include "console_hw.h"
#include "ew_line.h"
#include "esc_console.h"
#include "iomcu.h"
#include "page_console.h"

/** @brief What one step does. */
typedef enum {
    STEP_END = 0,
    STEP_LEVEL,  /**< the pad at @c level for @c ms                      */
    STEP_SEND,   /**< the frame @c frame sent, with its capture waited for */
    STEP_CUT,    /**< the frame @c frame, aborted @c ms after its start   */
    STEP_FEED,   /**< the frame @c frame, then the answer driven by override */
} step_kind_t;

/** @brief One step of a test. */
typedef struct {
    uint8_t  kind;   /**< a step_kind_t */
    uint8_t  level;  /**< an ew_line_t, for STEP_LEVEL */
    uint8_t  frame;  /**< an index into k_frames       */
    uint8_t  bad;    /**< 1 + a fed byte with a low stop bit, 0 none */
    uint16_t ms;     /**< how long, for STEP_LEVEL and STEP_CUT */
} step_t;

/** @brief A fixed frame. */
typedef struct {
    const uint8_t *bytes; /**< the bytes                                 */
    uint16_t n;           /**< how many                                  */
    uint16_t n_head;      /**< bytes before the pause                    */
    uint16_t max_rx;      /**< bytes to receive                          */
    uint8_t  rate;        /**< an ew_rate_t                              */
    uint8_t  pause_bits;  /**< idle bit times after the head             */
    uint8_t  rest_down;   /**< ew_line_rest_down() for this frame        */
    uint32_t first_us;    /**< the wait for the first byte               */
    uint32_t gap_us;      /**< the wait for each later byte              */
} frame_t;

/** @brief Protocol A's wait for a first byte, us. */
#define WAIT_FIRST_A_US 100000u
/** @brief Protocol A's wait between bytes, us. */
#define WAIT_GAP_A_US 2000u
/** @brief The text line's wait for a first byte, us. */
#define WAIT_FIRST_TEXT_US 200000u
/** @brief The text line's wait between bytes, us. */
#define WAIT_GAP_TEXT_US 20000u

/** @brief 10 levels a byte in one pattern: every bit an edge. */
static const uint8_t k_edges[] = { 0x55 };
/** @brief The first contact of protocol A. */
static const uint8_t k_contact[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0D,
    0x42, 0x4C, 0x48, 0x65, 0x6C, 0x69, 0xF4, 0x7D
};
/** @brief An announcement and its data, with no program after it. */
static const uint8_t k_announce[] = {
    0xFE, 0x00, 0x00, 0x04, 0x30, 0x2B, 0x01, 0x02, 0x03, 0x04, 0xA1, 0x0F
};
/** @brief A line of the text interface. */
static const uint8_t k_info[] = { 'i', 'n', 'f', 'o', '\n' };
/** @brief What the `rx` test drives onto its own pad. */
static const uint8_t k_answer[] = {
    0x34, 0x37, 0x31, 0x64, 0xE8, 0xB2, 0x06, 0x01, 0x30
};

/** @brief The frames, by the index a step names. */
enum { F_CONTACT, F_ANNOUNCE, F_INFO, F_MARK_A, F_MARK_TEXT, F_ASK_A, F_ASK_TEXT };
/** @brief The frames, by those indexes. */
static const frame_t k_frames[] = {
    [F_CONTACT] = { k_contact, sizeof(k_contact), 0u, sizeof(k_answer),
                    EW_RATE_A, 0u, 0u, WAIT_FIRST_A_US, WAIT_GAP_A_US },
    [F_ANNOUNCE] = { k_announce, sizeof(k_announce), 6u, 0u, EW_RATE_A,
                     EW_PAUSE_BITS_MAX, 0u, 0u, 0u },
    [F_INFO] = { k_info, sizeof(k_info), 0u, EW_RX_TEXT_MAX, EW_RATE_TEXT, 0u,
                 0u, WAIT_FIRST_TEXT_US, WAIT_GAP_TEXT_US },
    [F_MARK_A] = { k_edges, 1u, 0u, 0u, EW_RATE_A, 0u, 1u, 0u, 0u },
    [F_MARK_TEXT] = { k_edges, 1u, 0u, 0u, EW_RATE_TEXT, 0u, 1u, 0u, 0u },
    [F_ASK_A] = { k_edges, 1u, 0u, sizeof(k_answer), EW_RATE_A, 0u, 0u,
                  WAIT_FIRST_A_US, WAIT_GAP_A_US },
    [F_ASK_TEXT] = { k_edges, 1u, 0u, sizeof(k_answer), EW_RATE_TEXT, 0u, 0u,
                     WAIT_FIRST_TEXT_US, WAIT_GAP_TEXT_US },
};

/** @brief A step: the pad at @p l for @p t ms. */
#define LEVEL(l, t) { STEP_LEVEL, (uint8_t)(l), 0u, 0u, (t) }
/** @brief A step: the frame @p f and its capture. */
#define SEND(f) { STEP_SEND, 0u, (f), 0u, 0u }

/** @brief The transmit test: every pad state and both rates. */
static const step_t k_tx[] = {
    LEVEL(EW_LINE_PARK, 20u), LEVEL(EW_LINE_HIGH, 50u),
    SEND(F_CONTACT), SEND(F_CONTACT), SEND(F_CONTACT),
    LEVEL(EW_LINE_UP, 20u),
    SEND(F_ANNOUNCE), LEVEL(EW_LINE_UP, 20u),
    SEND(F_INFO), LEVEL(EW_LINE_UP, 20u),
    SEND(F_MARK_A), LEVEL(EW_LINE_UP, 20u),
    SEND(F_MARK_TEXT), LEVEL(EW_LINE_PARK, 20u),
    LEVEL(EW_LINE_LOW, 300u), LEVEL(EW_LINE_PARK, 20u),
    { STEP_END, 0u, 0u, 0u, 0u },
};
/** @brief The abort test: cut in a byte, then a transaction. */
static const step_t k_abort[] = {
    LEVEL(EW_LINE_HIGH, 5u),
    { STEP_CUT, 0u, F_CONTACT, 0u, 3u },
    LEVEL(EW_LINE_PARK, 20u), LEVEL(EW_LINE_HIGH, 5u),
    SEND(F_MARK_A), LEVEL(EW_LINE_PARK, 20u),
    { STEP_END, 0u, 0u, 0u, 0u },
};
/** @brief The receive test: the answer on the pad by override. */
static const step_t k_rx[] = {
    LEVEL(EW_LINE_HIGH, 5u),
    { STEP_FEED, 0u, F_ASK_A, 0u, 0u }, LEVEL(EW_LINE_UP, 5u),
    { STEP_FEED, 0u, F_ASK_A, 3u, 0u }, LEVEL(EW_LINE_UP, 5u),
    { STEP_FEED, 0u, F_ASK_TEXT, 0u, 0u }, LEVEL(EW_LINE_PARK, 5u),
    { STEP_END, 0u, 0u, 0u, 0u },
};

uint64_t s_console_pins;             /* see iomcu.h */

static ew_pin_line_t s_line;        /**< the line under test */
static const step_t *s_step;        /**< the step, NULL with no test running */
static uint32_t s_t0_us;            /**< the step's start */
static bool s_in_step;              /**< the step has begun */
static bool s_fed;                  /**< the step's answer has been driven */
static ew_xfer_t s_x;               /**< the running transaction */
static uint8_t s_rx[EW_RX_TEXT_MAX];    /**< what it received */
static uint32_t s_start_max;        /**< the longest start(), us */
static uint32_t s_poll_max;         /**< the longest poll(), us */
static uint32_t s_level_max;        /**< the longest line(), us */
static uint32_t s_abort_us;         /**< the abort(), us */

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
 * @brief End the test: the line closed, the times said.
 * @param why the line's word
 */
static void finish(const char *why)
{
    const unsigned pin = s_line.pin;

    ew_line_close(&s_line);
    s_console_pins = 0u;
    reserve_held();
    s_step = NULL;
    console_hw_say("ewline %s gpio=%u start_max=%luus poll_max=%luus "
                   "level_max=%luus abort=%luus\n",
                   why, pin, (unsigned long)s_start_max,
                   (unsigned long)s_poll_max, (unsigned long)s_level_max,
                   (unsigned long)s_abort_us);
}

/**
 * @brief A read of ESC_WIRE, as a link request reads it.
 * @param off the first register
 * @param n   registers
 * @param out takes them
 */
static void esc_io_read(uint8_t off, uint8_t n, uint16_t *out)
{
    esc_read(&s_state, off, n, out);
}

/**
 * @brief A write of ESC_WIRE, as a link request writes it.
 * @param off the first register
 * @param n   registers
 * @param in  their values
 * @return 0, or a link_nack_t
 */
static uint8_t esc_io_write(uint8_t off, uint8_t n, const uint16_t *in)
{
    return esc_write(&s_state, off, n, in);
}

/** @brief The `esc` command's way to the page. */
static const esc_console_io_t k_esc_io = {
    esc_io_read, esc_io_write, console_hw_say,
};

bool ewline_console_line(void *ctx, char **tok, unsigned n)
{
    const step_t *steps = NULL;
    uint32_t pin = NUM_BANK0_GPIOS;
    ew_line_err_t e;

    (void)ctx;
    if (n != 0u && strcmp(tok[0], "esc") == 0) {
        return esc_console_line(&k_esc_io, tok, n);
    }
    if (n == 0u || strcmp(tok[0], "ewline") != 0) {
        return false;
    }
    if (n == 3u && strcmp(tok[2], "tx") == 0) {
        steps = k_tx;
    } else if (n == 3u && strcmp(tok[2], "abort") == 0) {
        steps = k_abort;
    } else if (n == 3u && strcmp(tok[2], "rx") == 0) {
        steps = k_rx;
    }
    if (steps == NULL || !page_console_number(tok[1], &pin)
        || pin >= NUM_BANK0_GPIOS) {
        console_hw_say("err usage: ewline <gpio> tx|abort|rx\n");
        return true;
    }
    if (s_step != NULL) {
        console_hw_say("err ewline: a test runs\n");
        return true;
    }
    if (!pin_free((unsigned)pin)) {
        console_hw_say("err ewline: gpio %lu is held, or the bank is ARMED\n",
                       (unsigned long)pin);
        return true;
    }
    ew_line_init(&s_line);
    e = ew_line_open(&s_line, (unsigned)pin);
    if (e != EW_LINE_OK) {
        console_hw_say("err ewline: open %d\n", (int)e);
        return true;
    }
    /* From here on no page takes the pin; finish() gives it back. */
    s_console_pins = (uint64_t)1u << pin;
    reserve_held();
    console_hw_say("ewline open gpio=%lu bit_a=%luns bit_text=%luns\n",
                   (unsigned long)pin,
                   (unsigned long)ew_line_bit_ns(&s_line, EW_RATE_A),
                   (unsigned long)ew_line_bit_ns(&s_line, EW_RATE_TEXT));
    s_start_max = 0u;
    s_poll_max = 0u;
    s_level_max = 0u;
    s_abort_us = 0u;
    s_in_step = false;
    s_step = steps;
    return true;
}

/**
 * @brief Start a step's frame.
 * @param f the frame
 * @return false when the driver refused it
 */
static bool start_frame(const frame_t *f)
{
    const uint32_t t0 = time_us_32();
    bool ok;

    memset(&s_x, 0, sizeof(s_x));
    s_x.rate = f->rate;
    s_x.pause_bits = f->pause_bits;
    s_x.n_tx = f->n;
    s_x.n_head = f->n_head;
    s_x.max_rx = f->max_rx;
    s_x.first_us = f->first_us;
    s_x.gap_us = f->gap_us;
    s_x.tx = f->bytes;
    s_x.rx = f->max_rx != 0u ? s_rx : NULL;
    ew_line_rest_down(&s_line, f->rest_down != 0u);
    ok = ew_line_start(&s_line, &s_x);
    note(&s_start_max, t0);
    return ok;
}

/**
 * @brief Drive one byte onto the pad by the pin's overrides, blocking for its
 *        10 bit times.
 * @param value    the byte
 * @param stop     the stop bit's level
 * @param bit_ns   the bit time
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
 * @brief The answer on the pad, while the receiver waits: the pad's output
 *        enable and level by override, the same pad the machine reads.
 * @param bad    1 + the byte with a low stop bit, 0 none
 * @param bit_ns the bit time
 */
static void feed_answer(uint8_t bad, uint32_t bit_ns)
{
    const uint pin = s_line.pin;

    gpio_set_outover(pin, GPIO_OVERRIDE_HIGH);
    gpio_set_oeover(pin, GPIO_OVERRIDE_HIGH);
    for (uint32_t i = 0u; i < sizeof(k_answer); ++i) {
        feed_byte(k_answer[i], bad != i + 1u, bit_ns);
        /* The receiver waits for the idle level after a low stop bit. */
        if (bad == i + 1u) {
            gpio_set_outover(pin, GPIO_OVERRIDE_HIGH);
            busy_wait_us_32(2u * bit_ns / 1000u);
        }
    }
    gpio_set_outover(pin, GPIO_OVERRIDE_NORMAL);
    gpio_set_oeover(pin, GPIO_OVERRIDE_NORMAL);
}

/**
 * @brief Say what the `rx` test's capture holds.
 * @param c the capture
 */
static void say_capture(const ew_capture_t *c)
{
    const bool match = c->n == sizeof(k_answer)
                       && memcmp(s_rx, k_answer, sizeof(k_answer)) == 0;

    console_hw_say("ewline capture n=%u bad_stop=%u aborted=%d overflow=%d "
                   "match=%d\n",
                   c->n, c->bad_stop, c->aborted ? 1 : 0, c->overflow ? 1 : 0,
                   match ? 1 : 0);
}

/**
 * @brief A step's first pass.
 * @param st the step
 * @return false when the test ended
 */
static bool step_begin(const step_t *st)
{
    s_t0_us = time_us_32();
    s_fed = false;
    if (st->kind == STEP_LEVEL) {
        const uint32_t t0 = time_us_32();

        ew_line_level(&s_line, (ew_line_t)st->level);
        note(&s_level_max, t0);
        return true;
    }
    if (!start_frame(&k_frames[st->frame])) {
        finish("refused");
        return false;
    }
    /* After the start, which takes up to 216us: the frame's time counts from
     * here. */
    s_t0_us = time_us_32();
    return true;
}

void ewline_console_pass(void)
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
        if (!step_begin(st)) {
            return;
        }
        s_in_step = true;
        return;
    }
    const uint32_t elapsed = time_us_32() - s_t0_us;
    bool over = false;

    switch (st->kind) {
    case STEP_LEVEL:
        over = elapsed >= st->ms * 1000u;
        break;
    case STEP_CUT:
        if (elapsed >= st->ms * 1000u) {
            const uint32_t t0 = time_us_32();

            ew_line_abort(&s_line);
            note(&s_abort_us, t0);
            over = true;
        }
        break;
    default: {
        ew_capture_t c;
        const frame_t *f = &k_frames[st->frame];
        const uint32_t bit_ns = ew_line_bit_ns(&s_line, f->rate);

        /* The ask's 10 bit times, its lead and its release, then the answer. */
        if (st->kind == STEP_FEED && !s_fed) {
            if (elapsed < 12u * bit_ns / 1000u) {
                return;
            }
            s_fed = true;
            feed_answer(st->bad, bit_ns);
        }
        const uint32_t t0 = time_us_32();
        const bool ended = ew_line_poll(&s_line, &c);

        note(&s_poll_max, t0);
        if (ended) {
            if (st->kind == STEP_FEED) {
                say_capture(&c);
            }
            over = true;
        }
        break;
    }
    }
    if (over) {
        s_in_step = false;
        s_step = st + 1;
    }
}
