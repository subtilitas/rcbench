/*
 * The signal wire's PIO (programmable input/output) program, run on the host
 * (protocols/escwire/rp2350/ew_line.pio), with the arithmetic that feeds it and
 * reads it (protocols/escwire/ew_pio.c).
 *
 * The program is assembled from its source by a small assembler for the
 * instructions it uses, and run by a model of the state machine that steps one
 * state machine cycle at a time: JMP always, on X--, Y-- and the pin; OUT to the
 * pins, X and Y; PULL with and without IFEMPTY; MOV X, OSR; IN from the pins, Y
 * and NULL with autopush at 9 bits, shifting right; SET of the pins, the pin
 * directions and Y; delays; the wrap.  A channel fills the 4-word transmit FIFO
 * and another drains the 4-word receive FIFO, its upper half, into a buffer of
 * a given length.  A cycle lasts the divider's system clock cycles, fraction
 * included, and the pin is read 2 system clock cycles after it moves.  The
 * assembled words are held to fixtures/ew_line_words.txt, which the firmware
 * build holds pioasm's output to: the model runs what the chip runs.
 *
 * Under test:
 *  - the dividers at 150MHz, other clocks, refusals; the wait counts; the
 *    stream's layout at its limits;
 *  - the transmit: 16 cycles a bit, 52us and 26.04us, driven high first, the
 *    frame on the wire, no gap between bytes, a pause, the release 16 cycles
 *    after the last stop bit's edge;
 *  - the receive: at every phase, a sender 4% slow or fast with no gaps, 2 idle
 *    bits between bytes, the longest replies; each wait with a byte just inside
 *    and just outside it;
 *  - framing errors: a low stop bit, a line held low, a line low at the release,
 *    a low too short for a start bit;
 *  - an echo: own bytes are not received, an ESC (electronic speed controller)
 *    repeating them is, and the core refuses the answer;
 *  - an abort in a byte, then a clean transaction; more bytes than the channel
 *    takes; the clock's wrap; the time against ew_xfer_span_us().
 *
 * Not tested here, because the host has no PIO: that the chip's input
 * synchroniser, PULL, autopush, JMP PIN, the fractional divider, the restart of
 * a state machine, the channel's read of a FIFO word's upper half and the DMA
 * (direct memory access) handshakes behave as this model says; the pad, the
 * pulls and an ESC's levels.  The console test covers some of that on a chip
 * (docs/BenchConsole.md).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "ew_a.h"
#include "ew_pio.h"
#include "ew_wire.h"

#include "ew_pio_model.h"

#define SYS_HZ 150000000u

/* The first contact of protocol A: a frame of 17B to send. */
static const uint8_t k_contact[17] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0D,
    0x42, 0x4C, 0x48, 0x65, 0x6C, 0x69, 0xF4, 0x7D
};

static model_t m;
static ew_pio_clock_t clk;
static ew_pio_run_t run;
static uint8_t rx_bytes[EW_PIO_RX_WORDS];

/* @p cyc state machine cycles as ns. */
static double cyc_ns(uint64_t cyc)
{
    return (double)cyc * (double)clk.div256 / 256.0 * 1e9 / (double)clk.sys_hz;
}

static double tick_ns(void)
{
    return cyc_ns(EW_PIO_TICK_CYCLES);
}

static void ext_clear(bool level0)
{
    m.n_ext = 0;
    m.ext0 = level0;
}

/* A change of the released wire to @p level, @p ns after the release. */
static void ext_edge(double ns, bool level)
{
    m.ext[m.n_ext].ns = ns;
    m.ext[m.n_ext].level = level;
    m.n_ext++;
}

/* A byte, its start bit @p at ns after the release, @p bit ns a bit. */
static void ext_byte(double at, double bit, uint8_t value, bool stop)
{
    ext_edge(at, false);
    for (unsigned b = 0; b < 8u; ++b) {
        ext_edge(at + bit * (1 + b), ((value >> b) & 1u) != 0u);
    }
    ext_edge(at + bit * 9, stop);
    if (!stop) {
        ext_edge(at + bit * 10, true);
    }
}

static ew_xfer_t xfer_of(uint8_t rate, const uint8_t *tx, uint16_t n_tx,
                         uint16_t max_rx, uint32_t first_us, uint32_t gap_us)
{
    ew_xfer_t x;

    memset(&x, 0, sizeof(x));
    x.rate = rate;
    x.n_tx = n_tx;
    x.max_rx = max_rx;
    x.first_us = first_us;
    x.gap_us = gap_us;
    x.tx = tx;
    x.rx = max_rx != 0u ? rx_bytes : NULL;
    return x;
}

/* The stream of @p x into the model, ready to run.  ext stays as set. */
static bool load(const ew_xfer_t *x)
{
    if (!ew_pio_clock(SYS_HZ, x->rate, &clk)) {
        return false;
    }
    m.div256 = clk.div256;
    m.sys_hz = clk.sys_hz;
    m.abort_at = 0;
    m.resume_at = 0;
    m.rx_room = x->max_rx;
    m.n_tx = ew_pio_stream(&clk, x, m.tx, &run);
    return m.n_tx != 0u;
}

/* The capture of what the model's channel wrote. */
static void capture(ew_capture_t *c)
{
    ew_pio_capture(&run, m.rx, m.n_rx, m.stalled, false, c);
}

/* The bytes the pad drove between its first low and the release, read mid-bit;
 * every level change on the 16-cycle grid.  Returns how many. */
static unsigned sent_bytes(uint8_t *out, unsigned room, uint64_t *first_low)
{
    uint64_t at = 0;
    unsigned n = 0;

    for (unsigned i = 0; i < m.n_pad; ++i) {
        if (m.pad[i].dir && !m.pad[i].latch) {
            at = m.pad[i].cyc;
            break;
        }
    }
    *first_low = at;
    for (unsigned i = 0; i < m.n_pad; ++i) {
        if (m.pad[i].cyc > at && m.pad[i].cyc < m.release) {
            CHECK_EQ((m.pad[i].cyc - at) % EW_PIO_CYCLES_PER_BIT, 0u);
        }
    }
    while (at + 8u < m.release && n < room) {
        unsigned v = 0;

        CHECK_EQ(pad_at(&m, at + 8u), 0);      /* the start bit */
        for (unsigned b = 0; b < 8u; ++b) {
            const int p = pad_at(&m, at + 24u + 16u * b);

            v |= (p == 1 ? 1u : 0u) << b;
        }
        CHECK_EQ(pad_at(&m, at + 152u), 1);    /* the stop bit */
        out[n++] = (uint8_t)v;
        at += 160u;
        /* A pause: high bit times up to the next start bit. */
        while (at + 8u < m.release && pad_at(&m, at + 8u) == 1) {
            at += 16u;
        }
    }
    return n;
}

/* ------------------------------------------------------------------ cases */

TEST_CASE(the_words_are_the_ones_the_firmware_build_checks)
{
    FILE *f = fopen(EW_LINE_WORDS, "r");
    char line[32];
    unsigned n = 0;

    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        if (line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        CHECK(n < prog_len);
        if (n < prog_len) {
            CHECK_EQ((unsigned)strtoul(line, NULL, 16), prog[n]);
        }
        ++n;
    }
    fclose(f);
    CHECK_EQ(n, prog_len);
    CHECK_EQ(prog_len, EW_PIO_PROGRAM_WORDS);
    CHECK(prog_len < 32u);
    CHECK_EQ(wrap_target, 0u);
    CHECK_EQ(wrap_at, EW_PIO_PROGRAM_WORDS - 1u);
    /* The instructions the driver and the model name by address. */
    CHECK_EQ(prog[EW_PIO_IDLE_PC], 0x80A0u);          /* pull */
    CHECK_EQ(prog[EW_PIO_RELEASE_PC], 0xE080u);       /* set pindirs, 0 */
    /* A jmp pin, whatever the target. */
    CHECK_EQ(prog[EW_PIO_WAIT_HI_PC] & 0xFFE0u, 0x00C0u);
    CHECK_EQ(prog[EW_PIO_WAIT_LO_PC] & 0xFFE0u, 0x00C0u);
}

TEST_CASE(the_dividers_at_150mhz_are_exact)
{
    ew_pio_clock_t c;

    CHECK(ew_pio_clock(SYS_HZ, EW_RATE_A, &c));
    CHECK_EQ(c.div256, (487u << 8) + 128u);
    CHECK_EQ(c.err_ppm, 0u);
    CHECK_EQ(ew_pio_bit_ns(&c), EW_BIT_A_NS);
    CHECK(ew_pio_clock(SYS_HZ, EW_RATE_TEXT, &c));
    CHECK_EQ(c.div256, (244u << 8) + 36u);
    CHECK_EQ(c.err_ppm, 0u);
    CHECK_EQ(ew_pio_bit_ns(&c), EW_BIT_TEXT_NS);
}

TEST_CASE(other_clocks_and_the_refusals)
{
    ew_pio_clock_t c;

    /* 125MHz: 52us is exact, 38400baud rounds within 0.05%. */
    CHECK(ew_pio_clock(125000000u, EW_RATE_A, &c));
    CHECK_EQ(c.err_ppm, 0u);
    CHECK(ew_pio_clock(125000000u, EW_RATE_TEXT, &c));
    CHECK(c.err_ppm <= EW_PIO_MAX_ERROR_PPM);
    CHECK_NEAR((double)ew_pio_bit_ns(&c), 26042.0, 1.0);
    /* The top of the range of the clock's type. */
    CHECK(ew_pio_clock(0xFFFFFFFFu, EW_RATE_A, &c));
    CHECK(ew_pio_clock(0xFFFFFFFFu, EW_RATE_TEXT, &c));
    /* A divider of 4 is the least: 4 x 16 cycles x 19230.77 bits per s. */
    CHECK(ew_pio_clock(1230770u, EW_RATE_A, &c));
    CHECK(!ew_pio_clock(1230000u, EW_RATE_A, &c));
    CHECK(!ew_pio_clock(2450000u, EW_RATE_TEXT, &c));
    CHECK(!ew_pio_clock(0u, EW_RATE_A, &c));
    CHECK(!ew_pio_clock(SYS_HZ, EW_RATES, &c));
    CHECK(!ew_pio_clock(SYS_HZ, EW_RATE_A, NULL));
}

TEST_CASE(the_wait_counts_round_up_and_do_not_overflow)
{
    ew_pio_clock_t c;

    CHECK(ew_pio_clock(SYS_HZ, EW_RATE_A, &c));
    /* 6.5us a tick. */
    CHECK_EQ(ew_pio_ticks(&c, 13u), 2u);
    CHECK_EQ(ew_pio_ticks(&c, 14u), 3u);
    CHECK_EQ(ew_pio_ticks(&c, 0u), 0u);
    /* 4s at the top of the clock's type, 64-bit intermediates. */
    CHECK(ew_pio_clock(0xFFFFFFFFu, EW_RATE_TEXT, &c));
    CHECK_NEAR((double)ew_pio_ticks(&c, EW_FIRST_US_MAX),
               4.0 / (2.0 / (16.0 * 38400.0)), 8.0);
}

TEST_CASE(the_stream_at_its_limits)
{
    static uint8_t tx[EW_TX_MAX];
    static uint32_t words[EW_PIO_TX_WORDS + 1u];
    ew_pio_run_t r;
    ew_pio_clock_t c;
    ew_xfer_t x = xfer_of(EW_RATE_A, tx, EW_TX_MAX, 0u, 0u, 0u);
    uint32_t n;

    CHECK(ew_pio_clock(SYS_HZ, EW_RATE_A, &c));
    memset(tx, 0x55, sizeof(tx));
    x.n_head = 6u;
    x.pause_bits = EW_PAUSE_BITS_MAX;
    words[EW_PIO_TX_WORDS] = 0xA5A5A5A5u;
    n = ew_pio_stream(&c, &x, words, &r);
    CHECK_EQ(n, EW_PIO_TX_WORDS);
    CHECK_EQ(words[EW_PIO_TX_WORDS], 0xA5A5A5A5u);    /* nothing past it */
    CHECK_EQ(words[0], EW_TX_MAX * 10u + EW_PAUSE_BITS_MAX - 1u);
    CHECK_EQ(words[1], 0u);
    CHECK_EQ(words[n - 1u], 0u);
    CHECK_EQ(r.span_us, ew_xfer_span_us(&x));

    /* One byte, no receive: count, first, 1 word of bits, gap. */
    x = xfer_of(EW_RATE_A, tx, 1u, 0u, 0u, 0u);
    tx[0] = 0xA5;
    CHECK_EQ(ew_pio_stream(&c, &x, words, &r), 4u);
    CHECK_EQ(words[0], 9u);
    CHECK_EQ(words[2], 0x34Au | 0x200u);

    /* A receive: first wait 6.5us a tick, later 4 ticks more. */
    x = xfer_of(EW_RATE_A, tx, 1u, 9u, 100000u, 2000u);
    CHECK_EQ(ew_pio_stream(&c, &x, words, &r), 4u);
    CHECK_EQ(words[1], 15385u);
    CHECK_EQ(words[3], 308u + EW_PIO_HALF_BIT_TICKS);

    /* A bit crossing a word: 3 bytes are 30 bits, 4 are 40. */
    x = xfer_of(EW_RATE_A, tx, 4u, 0u, 0u, 0u);
    memset(tx, 0xFF, 4u);
    CHECK_EQ(ew_pio_stream(&c, &x, words, &r), 5u);
    CHECK_EQ(words[2], 0xBFEFFBFEu);
    CHECK_EQ(words[3] & 0xFFu, 0xFFu);

    /* The longest reply at each rate, and one past it. */
    x = xfer_of(EW_RATE_TEXT, tx, 1u, EW_RX_TEXT_MAX, 200000u, 20000u);
    CHECK_EQ(ew_pio_stream(&c, &x, words, &r), 4u);
    x.max_rx = EW_RX_TEXT_MAX + 1u;
    CHECK_EQ(ew_pio_stream(&c, &x, words, &r), 0u);
    x = xfer_of(EW_RATE_A, tx, 1u, EW_RX_MAX + 1u, 100000u, 2000u);
    CHECK_EQ(ew_pio_stream(&c, &x, words, &r), 0u);

    /* What ew_xfer_valid() refuses, and no arguments. */
    x.n_tx = 0u;
    CHECK_EQ(ew_pio_stream(&c, &x, words, &r), 0u);
    x.n_tx = 1u;
    CHECK_EQ(ew_pio_stream(NULL, &x, words, &r), 0u);
    CHECK_EQ(ew_pio_stream(&c, &x, NULL, &r), 0u);
    CHECK_EQ(ew_pio_stream(&c, &x, words, NULL), 0u);
}

TEST_CASE(the_first_contact_goes_out_at_52us_a_bit)
{
    ew_xfer_t x = xfer_of(EW_RATE_A, k_contact, 17u, 9u, 100000u, 2000u);
    uint8_t got[32];
    uint64_t first;

    ext_clear(true);
    CHECK(load(&x));
    model_run(&m, 200000u);
    CHECK(m.finished);
    /* 3.25us a cycle, 52us a bit, within nothing at 150MHz. */
    CHECK_NEAR(cyc_ns(EW_PIO_CYCLES_PER_BIT), 52000.0, 0.01);
    /* Driven high before the first bit, and the first low after that. */
    CHECK_EQ(m.pad[0].dir, true);
    CHECK_EQ(m.pad[0].latch, true);
    CHECK_EQ(sent_bytes(got, 32u, &first), 17u);
    CHECK(memcmp(got, k_contact, 17u) == 0);
    /* The frame back to back: 170 bits, and the release 1 bit after the last
     * stop bit's edge, to the cycle. */
    CHECK_EQ(m.release - first, 170u * EW_PIO_CYCLES_PER_BIT);
    CHECK_EQ(pad_at(&m, m.release - 1u), 1);
    CHECK_EQ(pad_at(&m, m.release), -1);
    /* An input from there on, and no bytes of its own. */
    CHECK_EQ(m.n_pad > 0u ? m.pad[m.n_pad - 1u].dir : true, false);
    CHECK_EQ(m.n_rx, 0u);
}

TEST_CASE(a_text_line_goes_out_at_38400baud)
{
    static const uint8_t line[] = { 'i', 'n', 'f', 'o', '\n' };
    ew_xfer_t x = xfer_of(EW_RATE_TEXT, line, 5u, 0u, 0u, 0u);
    uint8_t got[8];
    uint64_t first;

    ext_clear(true);
    CHECK(load(&x));
    model_run(&m, 20000u);
    CHECK(m.finished);
    CHECK_NEAR(cyc_ns(EW_PIO_CYCLES_PER_BIT), 1e9 / 38400.0, 0.01);
    CHECK_EQ(sent_bytes(got, 8u, &first), 5u);
    CHECK(memcmp(got, line, 5u) == 0);
    CHECK_EQ(m.release - first, 50u * EW_PIO_CYCLES_PER_BIT);
}

TEST_CASE(a_pause_is_idle_bit_times_after_the_head)
{
    static const uint8_t frame[] = { 0xFE, 0x00, 0x00, 0x04, 0x30, 0x2B,
                                     0x01, 0x02, 0x03, 0x04, 0xA1, 0x0F };
    ew_xfer_t x = xfer_of(EW_RATE_A, frame, 12u, 1u, 80000u, 2000u);
    uint8_t got[16];
    uint64_t first;

    x.n_head = 6u;
    x.pause_bits = 2u;
    ext_clear(true);
    CHECK(load(&x));
    model_run(&m, 100000u);
    CHECK(m.finished);
    CHECK_EQ(sent_bytes(got, 16u, &first), 12u);
    CHECK(memcmp(got, frame, 12u) == 0);
    /* The pause held high, 104us, then the data's start bit. */
    CHECK_EQ(pad_at(&m, first + 60u * 16u), 1);
    CHECK_EQ(pad_at(&m, first + 61u * 16u), 1);
    CHECK_EQ(pad_at(&m, first + 62u * 16u + 8u), 0);
    CHECK_EQ(m.release - first, 122u * EW_PIO_CYCLES_PER_BIT);
}

TEST_CASE(an_answer_comes_back_at_every_phase)
{
    static const uint8_t ask[] = { 0xFF };
    const uint8_t answer[] = { 0x34, 0x37, 0x31, 0x64, 0xE8, 0xB2, 0x06, 0x01,
                               0x30 };

    /* The start bit anywhere within one tick, in 1/8 of a cycle. */
    for (unsigned phase = 0; phase < 24u; ++phase) {
        ew_xfer_t x = xfer_of(EW_RATE_A, ask, 1u, 9u, 100000u, 2000u);
        ew_capture_t c;

        ext_clear(true);
        for (unsigned i = 0; i < 9u; ++i) {
            ext_byte(5000.0 + phase * 3250.0 / 8.0 + i * 520000.0, 52000.0,
                     answer[i], true);
        }
        CHECK(load(&x));
        model_run(&m, 3000u);
        CHECK_EQ(m.n_rx, 9u);
        capture(&c);
        CHECK_EQ(c.n, 9u);
        CHECK_EQ(c.bad_stop, 0u);
        CHECK(!c.aborted);
        CHECK(!c.overflow);
        CHECK(memcmp(rx_bytes, answer, 9u) == 0);
        CHECK_EQ(ew_a_contact_reply(&c, rx_bytes), EW_ANS_OK);
    }
}

/* @p n bytes with no gap at @p bit ns a bit: every byte right, every stop high,
 * for every phase in 1/4 of a cycle. */
static void long_reply_holds(uint8_t rate, unsigned n, double bit,
                             double nominal)
{
    static const uint8_t ask[] = { 0x03 };

    for (unsigned phase = 0; phase < 8u; ++phase) {
        ew_xfer_t x = xfer_of(rate, ask, 1u, (uint16_t)n, 100000u, 2000u);
        ew_capture_t c;

        ext_clear(true);
        for (unsigned i = 0; i < n; ++i) {
            ext_byte(nominal + phase * nominal / 64.0 + i * bit * 10.0, bit,
                     (uint8_t)(i * 37u), true);
        }
        CHECK(load(&x));
        model_run(&m, 64u + n * 170u);
        capture(&c);
        CHECK_EQ(c.n, n);
        CHECK_EQ(c.bad_stop, 0u);
        for (unsigned i = 0; i < c.n; ++i) {
            CHECK_EQ(rx_bytes[i], (uint8_t)(i * 37u));
        }
    }
}

TEST_CASE(a_sender_4_percent_off_with_no_gaps)
{
    long_reply_holds(EW_RATE_A, EW_RX_MAX, 52000.0 * 0.96, 52000.0);
    long_reply_holds(EW_RATE_A, EW_RX_MAX, 52000.0, 52000.0);
    long_reply_holds(EW_RATE_A, EW_RX_MAX, 52000.0 * 1.04, 52000.0);
    long_reply_holds(EW_RATE_TEXT, 200u, 26042.0 * 0.96, 26042.0);
    long_reply_holds(EW_RATE_TEXT, 200u, 26042.0 * 1.04, 26042.0);
}

TEST_CASE(the_longest_text_reply_fits)
{
    long_reply_holds(EW_RATE_TEXT, EW_RX_TEXT_MAX, 26042.0, 26042.0);
}

TEST_CASE(two_idle_bits_between_bytes)
{
    static const uint8_t ask[] = { 0x03 };
    ew_xfer_t x = xfer_of(EW_RATE_A, ask, 1u, 112u, 80000u, 2000u);
    ew_capture_t c;

    ext_clear(true);
    /* 12 bit times a byte, the first 2 bit times after the host's last. */
    for (unsigned i = 0; i < 112u; ++i) {
        ext_byte(104000.0 + i * 624000.0, 52000.0, (uint8_t)(i + 1u), true);
    }
    CHECK(load(&x));
    model_run(&m, 30000u);
    capture(&c);
    CHECK_EQ(c.n, 112u);
    CHECK_EQ(c.bad_stop, 0u);
    CHECK_EQ(rx_bytes[111], 112u);
}

TEST_CASE(silence_ends_at_the_first_wait)
{
    static const uint8_t ask[] = { 0xFF };
    ew_xfer_t x = xfer_of(EW_RATE_A, ask, 1u, 1u, 4000u, 2000u);
    ew_capture_t c;

    ext_clear(true);
    CHECK(load(&x));
    model_run(&m, 3000u);
    CHECK(m.finished);
    CHECK_EQ(m.n_rx, 0u);
    CHECK(cyc_ns(m.end - m.release) >= 4000000.0);
    CHECK(cyc_ns(m.end - m.release) <= 4000000.0 + 3.0 * tick_ns());
    capture(&c);
    CHECK_EQ(c.n, 0u);
    CHECK_EQ(ew_a_one(&c, rx_bytes), EW_ANS_SILENT);
}

TEST_CASE(a_byte_just_inside_and_just_outside_the_first_wait)
{
    static const uint8_t ask[] = { 0xFF };
    ew_xfer_t x = xfer_of(EW_RATE_A, ask, 1u, 1u, 4000u, 2000u);

    ext_clear(true);
    ext_byte(4000000.0, 52000.0, 0x30, true);
    CHECK(load(&x));
    model_run(&m, 3000u);
    CHECK_EQ(m.n_rx, 1u);
    CHECK_EQ(m.rx[0] >> 7 & 0xFFu, 0x30u);

    ext_clear(true);
    ext_byte(4000000.0 + 3.0 * tick_ns(), 52000.0, 0x30, true);
    CHECK(load(&x));
    model_run(&m, 3000u);
    CHECK_EQ(m.n_rx, 0u);
}

TEST_CASE(a_byte_just_inside_and_just_outside_the_later_wait)
{
    static const uint8_t ask[] = { 0x03 };
    const struct {
        uint8_t rate;
        double bit;
        uint32_t gap_us;
    } rows[] = {
        { EW_RATE_A, 52000.0, 2000u },
        { EW_RATE_TEXT, 1e9 / 38400.0, 20000u },
    };

    for (unsigned r = 0; r < 2u; ++r) {
        ew_xfer_t x = xfer_of(rows[r].rate, ask, 1u, 2u, 80000u, rows[r].gap_us);
        const double end = 1000000.0 + rows[r].bit * 10.0;
        const double gap = rows[r].gap_us * 1000.0;

        ext_clear(true);
        ext_byte(1000000.0, rows[r].bit, 0x11, true);
        ext_byte(end + gap, rows[r].bit, 0x22, true);
        CHECK(load(&x));
        model_run(&m, 60000u);
        CHECK_EQ(m.n_rx, 2u);

        ext_clear(true);
        ext_byte(1000000.0, rows[r].bit, 0x11, true);
        ext_byte(end + gap + 3.0 * tick_ns(), rows[r].bit, 0x22, true);
        CHECK(load(&x));
        model_run(&m, 60000u);
        CHECK(m.finished);
        CHECK_EQ(m.n_rx, 1u);
    }
}

TEST_CASE(a_low_stop_bit_is_marked_and_the_next_byte_counts)
{
    static const uint8_t ask[] = { 0x03 };
    ew_xfer_t x = xfer_of(EW_RATE_A, ask, 1u, 4u, 80000u, 2000u);
    ew_capture_t c;

    ext_clear(true);
    ext_byte(100000.0, 52000.0, 0x41, true);
    ext_byte(800000.0, 52000.0, 0x42, false);
    ext_byte(1500000.0, 52000.0, 0x43, false);
    ext_byte(2200000.0, 52000.0, 0x44, true);
    CHECK(load(&x));
    model_run(&m, 3000u);
    capture(&c);
    CHECK_EQ(c.n, 4u);
    CHECK_EQ(c.bad_stop, 2u);     /* 1 + the first one's index */
    CHECK_EQ(rx_bytes[0], 0x41u);
    CHECK_EQ(rx_bytes[1], 0x42u);
    CHECK_EQ(rx_bytes[3], 0x44u);
    CHECK_EQ(ew_a_read_reply(&c, rx_bytes, 1u), EW_ANS_FRAMING);
}

TEST_CASE(a_low_stop_bit_and_2_idle_bits_lose_no_byte)
{
    static const uint8_t ask[] = { 0x03 };
    ew_xfer_t x = xfer_of(EW_RATE_A, ask, 1u, 3u, 80000u, 2000u);
    ew_capture_t c;

    /* As the console test drives it: the next start bit 2 bit times after the
     * low stop bit's end. */
    ext_clear(true);
    ext_byte(100000.0, 52000.0, 0x31, true);
    ext_byte(620000.0, 52000.0, 0x64, false);
    ext_byte(620000.0 + 12.0 * 52000.0, 52000.0, 0xE8, true);
    CHECK(load(&x));
    model_run(&m, 3000u);
    capture(&c);
    CHECK_EQ(c.n, 3u);
    CHECK_EQ(c.bad_stop, 2u);
    CHECK_EQ(rx_bytes[1], 0x64u);
    CHECK_EQ(rx_bytes[2], 0xE8u);
}

TEST_CASE(a_line_held_low_is_one_byte)
{
    static const uint8_t ask[] = { 0x03 };
    ew_xfer_t x = xfer_of(EW_RATE_A, ask, 1u, 8u, 80000u, 2000u);
    ew_capture_t c;

    ext_clear(true);
    ext_edge(100000.0, false);
    CHECK(load(&x));
    model_run(&m, 3000u);
    CHECK(m.finished);
    capture(&c);
    CHECK_EQ(c.n, 1u);
    CHECK_EQ(c.bad_stop, 1u);
    CHECK_EQ(rx_bytes[0], 0x00u);
}

TEST_CASE(a_line_low_at_the_release_is_no_byte)
{
    static const uint8_t ask[] = { 0x03 };
    ew_xfer_t x = xfer_of(EW_RATE_A, ask, 1u, 8u, 4000u, 2000u);

    ext_clear(false);
    CHECK(load(&x));
    model_run(&m, 3000u);
    CHECK(m.finished);
    CHECK_EQ(m.n_rx, 0u);
    CHECK(cyc_ns(m.end - m.release) >= 4000000.0);
}

TEST_CASE(a_low_too_short_for_a_start_bit_is_no_byte)
{
    static const uint8_t ask[] = { 0x03 };
    ew_xfer_t x = xfer_of(EW_RATE_A, ask, 1u, 1u, 4000u, 2000u);

    ext_clear(true);
    /* A quarter of a bit: gone before the sample in mid start bit. */
    ext_edge(100000.0, false);
    ext_edge(113000.0, true);
    CHECK(load(&x));
    model_run(&m, 3000u);
    CHECK(m.finished);
    CHECK_EQ(m.n_rx, 0u);
    /* And a real byte after it still lands. */
    ext_byte(300000.0, 52000.0, 0x30, true);
    CHECK(load(&x));
    model_run(&m, 3000u);
    CHECK_EQ(m.n_rx, 1u);
    CHECK_EQ(m.rx[0] >> 7 & 0xFFu, 0x30u);
}

TEST_CASE(a_line_that_falls_after_the_answer_makes_a_false_byte)
{
    static const uint8_t ask[] = { 0x03 };
    ew_xfer_t x = xfer_of(EW_RATE_A, ask, 1u, 2u, 80000u, 2000u);
    ew_capture_t c;

    /* A load holds the line low once the sender lets go: a low with a low stop
     * bit, which the capture marks and the core refuses. */
    ext_clear(true);
    ext_byte(100000.0, 52000.0, 0x30, true);
    ext_edge(100000.0 + 520000.0 + 26000.0, false);
    CHECK(load(&x));
    model_run(&m, 3000u);
    capture(&c);
    CHECK_EQ(c.n, 2u);
    CHECK_EQ(c.bad_stop, 2u);
    CHECK_EQ(rx_bytes[1], 0x00u);
    CHECK_EQ(ew_a_one(&c, rx_bytes), EW_ANS_FRAMING);

    /* A transaction that asks for just the answer has ended before it. */
    x = xfer_of(EW_RATE_A, ask, 1u, 1u, 80000u, 2000u);
    CHECK(load(&x));
    model_run(&m, 3000u);
    capture(&c);
    CHECK_EQ(c.n, 1u);
    CHECK_EQ(c.bad_stop, 0u);
    CHECK_EQ(ew_a_one(&c, rx_bytes), EW_ANS_OK);
}

TEST_CASE(own_bytes_are_not_received)
{
    static const uint8_t frame[] = { 0xFF, 0x00, 0x1A, 0x00, 0x3B, 0x74 };
    ew_xfer_t x = xfer_of(EW_RATE_A, frame, 6u, 1u, 4000u, 2000u);
    ew_capture_t c;

    /* The bytes on the wire while the pad drives are the pad's; the receiver
     * starts at the release. */
    ext_clear(true);
    CHECK(load(&x));
    model_run(&m, 5000u);
    CHECK(m.finished);
    capture(&c);
    CHECK_EQ(c.n, 0u);
    CHECK_EQ(ew_a_one(&c, rx_bytes), EW_ANS_SILENT);
}

TEST_CASE(an_echo_after_the_release_is_received_and_refused)
{
    static const uint8_t frame[] = { 0xFF, 0x00, 0x1A, 0x00, 0x3B, 0x74 };
    ew_xfer_t x = xfer_of(EW_RATE_A, frame, 6u, 1u, 4000u, 2000u);
    ew_capture_t c;

    ext_clear(true);
    for (unsigned i = 0; i < 6u; ++i) {
        ext_byte(52000.0 + i * 520000.0, 52000.0, frame[i], true);
    }
    CHECK(load(&x));
    model_run(&m, 3000u);
    capture(&c);
    /* One byte asked: the echo's first, and it is no answer 0x30. */
    CHECK_EQ(c.n, 1u);
    CHECK_EQ(rx_bytes[0], 0xFFu);
    CHECK(ew_a_one(&c, rx_bytes) != EW_ANS_OK);

    /* A first contact asks 9 bytes: the first 9 of an echo are no `471`. */
    x = xfer_of(EW_RATE_A, k_contact, 17u, 9u, 100000u, 2000u);
    ext_clear(true);
    for (unsigned i = 0; i < 17u; ++i) {
        ext_byte(52000.0 + i * 520000.0, 52000.0, k_contact[i], true);
    }
    CHECK(load(&x));
    model_run(&m, 6000u);
    capture(&c);
    CHECK_EQ(c.n, 9u);
    CHECK(ew_a_contact_reply(&c, rx_bytes) != EW_ANS_OK);
}

TEST_CASE(an_abort_in_a_byte_releases_and_the_next_transaction_is_clean)
{
    static const uint8_t frame[] = { 0xFF, 0x00, 0x1A, 0x00, 0x3B, 0x74 };
    static const uint8_t next[] = { 0xA5 };
    ew_xfer_t x = xfer_of(EW_RATE_A, frame, 6u, 0u, 0u, 0u);
    ew_xfer_t y = xfer_of(EW_RATE_A, next, 1u, 0u, 0u, 0u);
    ew_pio_run_t second;
    uint8_t got[2];
    uint64_t first;

    /* A cut in the third byte, at any cycle of a bit: the pad is an input in
     * that cycle, whatever bit it was driving. */
    for (unsigned at = 0; at < 16u; ++at) {
        const uint64_t cut = 340u + at;

        ext_clear(true);
        CHECK(load(&x));
        m.abort_at = cut;
        m.resume_at = m.n_tx;
        m.n_tx += ew_pio_stream(&clk, &y, &m.tx[m.n_tx], &second);
        model_run(&m, 2000u);
        CHECK(m.finished);
        CHECK_EQ(pad_at(&m, cut), -1);
        /* The next one: what the cut left in the shift registers plays no
         * part, one byte, 10 bits. */
        {
            unsigned last_cut = 0;

            for (unsigned i = 0; i < m.n_pad; ++i) {
                if (m.pad[i].cyc <= cut) {
                    last_cut = i;
                }
            }
            memmove(&m.pad[0], &m.pad[last_cut + 1u],
                    (m.n_pad - last_cut - 1u) * sizeof(m.pad[0]));
            m.n_pad -= last_cut + 1u;
            m.release = m.pad[m.n_pad - 1u].cyc;
        }
        CHECK_EQ(sent_bytes(got, 2u, &first), 1u);
        CHECK_EQ(got[0], 0xA5u);
        CHECK_EQ(m.release - first, 160u);
    }
}

TEST_CASE(more_bytes_than_the_channel_takes)
{
    static const uint8_t ask[] = { 0x03 };
    ew_xfer_t x = xfer_of(EW_RATE_A, ask, 1u, 3u, 80000u, 2000u);
    ew_capture_t c;

    ext_clear(true);
    for (unsigned i = 0; i < 12u; ++i) {
        ext_byte(52000.0 + i * 520000.0, 52000.0, (uint8_t)(0x10u + i), true);
    }
    CHECK(load(&x));
    model_run(&m, 3000u);
    /* The channel ends at its count; the machine fills the FIFO and stalls,
     * with the pad an input.  A whole capture all the same. */
    CHECK_EQ(m.n_rx, 3u);
    CHECK(m.stalled);
    CHECK_EQ(pad_at(&m, 2999u), -1);
    capture(&c);
    CHECK_EQ(c.n, 3u);
    CHECK(!c.overflow);
    CHECK_EQ(rx_bytes[2], 0x12u);
}

TEST_CASE(the_capture_at_its_limits)
{
    static const uint16_t words[3] = {
        (uint16_t)(0x8000u | (0x55u << 7)), /* stop high */
        (uint16_t)(0xAAu << 7),             /* stop low */
        (uint16_t)(0x8000u | (0xFFu << 7)),
    };
    uint8_t out[3];
    ew_pio_run_t r;
    ew_capture_t c;

    memset(&r, 0, sizeof(r));
    r.max_rx = 2u;
    r.rx = out;
    /* The count held to what the transaction asked. */
    ew_pio_capture(&r, words, 3u, false, false, &c);
    CHECK_EQ(c.n, 2u);
    CHECK_EQ(out[0], 0x55u);
    CHECK_EQ(out[1], 0xAAu);
    CHECK_EQ(c.bad_stop, 2u);
    CHECK(!c.overflow);
    CHECK(!c.aborted);
    /* A stall short of the count lost bytes; at the count it did not. */
    ew_pio_capture(&r, words, 1u, true, false, &c);
    CHECK(c.overflow);
    CHECK_EQ(c.bad_stop, 0u);
    ew_pio_capture(&r, words, 2u, true, false, &c);
    CHECK(!c.overflow);
    /* The driver's cut. */
    ew_pio_capture(&r, words, 1u, false, true, &c);
    CHECK(c.aborted);
    CHECK_EQ(ew_a_one(&c, out), EW_ANS_ABORTED);
    /* None at all, with no place for any. */
    r.max_rx = 0u;
    r.rx = NULL;
    ew_pio_capture(&r, NULL, 0u, false, false, &c);
    CHECK_EQ(c.n, 0u);
}

TEST_CASE(late_across_the_clocks_wrap)
{
    ew_pio_run_t r;

    memset(&r, 0, sizeof(r));
    r.span_us = 1000u;
    /* 1000 + 1000 grace, from 16us before the wrap. */
    CHECK(!ew_pio_late(&r, 1984u, 0xFFFFFFF0u));
    CHECK(ew_pio_late(&r, 1985u, 0xFFFFFFF0u));
    CHECK(!ew_pio_late(&r, 100u, 100u));
    /* A span at the top of its type does not wrap the limit. */
    r.span_us = 0xFFFFFFFFu;
    CHECK(!ew_pio_late(&r, 0xFFFFFFFFu, 0u));
}

TEST_CASE(a_transaction_stays_inside_its_time)
{
    static const uint8_t frame[4] = { 0xFF, 0x00, 0x1A, 0x00 };
    const struct {
        uint8_t rate;
        uint16_t n_tx;
        uint16_t max_rx;
        uint32_t first_us;
        uint32_t gap_us;
        unsigned answers;
    } rows[] = {
        { EW_RATE_A, 4u, 0u, 0u, 0u, 0u },
        { EW_RATE_A, 4u, 1u, 4000u, 2000u, 0u },
        { EW_RATE_A, 4u, 8u, 80000u, 2000u, 7u },
        { EW_RATE_TEXT, 4u, 8u, 200000u, 20000u, 7u },
        { EW_RATE_TEXT, 4u, 8u, 1u, 1u, 0u },
    };

    for (unsigned r = 0; r < sizeof(rows) / sizeof(rows[0]); ++r) {
        ew_xfer_t x = xfer_of(rows[r].rate, frame, rows[r].n_tx, rows[r].max_rx,
                              rows[r].first_us, rows[r].gap_us);
        const double bit = rows[r].rate == EW_RATE_A ? 52000.0 : 1e9 / 38400.0;

        ext_clear(true);
        for (unsigned i = 0; i < rows[r].answers; ++i) {
            /* Each at the end of the last wait: the slowest a sender can be. */
            ext_byte(rows[r].first_us * 1000.0
                         + i * (rows[r].gap_us * 1000.0 + bit * 10.0),
                     bit, 0x5A, true);
        }
        CHECK(load(&x));
        model_run(&m, 40000000u);
        CHECK(m.finished);
        CHECK_EQ(m.n_rx, rows[r].answers);
        if (rows[r].max_rx != 0u) {
            CHECK(cyc_ns(m.end) <= run.span_us * 1000.0);
        } else {
            /* No wait at all: the release and the way round the program, 14
             * cycles past the bits. */
            CHECK(cyc_ns(m.end) <= run.span_us * 1000.0 + cyc_ns(14u));
        }
    }
    CHECK(EW_PIO_GRACE_US < 5000u);
}

int main(void)
{
    if (!assemble(EW_LINE_PIO)) {
        printf("cannot assemble %s\n", EW_LINE_PIO);
        return 1;
    }
    RUN(the_words_are_the_ones_the_firmware_build_checks);
    RUN(the_dividers_at_150mhz_are_exact);
    RUN(other_clocks_and_the_refusals);
    RUN(the_wait_counts_round_up_and_do_not_overflow);
    RUN(the_stream_at_its_limits);
    RUN(the_first_contact_goes_out_at_52us_a_bit);
    RUN(a_text_line_goes_out_at_38400baud);
    RUN(a_pause_is_idle_bit_times_after_the_head);
    RUN(an_answer_comes_back_at_every_phase);
    RUN(a_sender_4_percent_off_with_no_gaps);
    RUN(the_longest_text_reply_fits);
    RUN(two_idle_bits_between_bytes);
    RUN(silence_ends_at_the_first_wait);
    RUN(a_byte_just_inside_and_just_outside_the_first_wait);
    RUN(a_byte_just_inside_and_just_outside_the_later_wait);
    RUN(a_low_stop_bit_is_marked_and_the_next_byte_counts);
    RUN(a_low_stop_bit_and_2_idle_bits_lose_no_byte);
    RUN(a_line_held_low_is_one_byte);
    RUN(a_line_low_at_the_release_is_no_byte);
    RUN(a_low_too_short_for_a_start_bit_is_no_byte);
    RUN(a_line_that_falls_after_the_answer_makes_a_false_byte);
    RUN(own_bytes_are_not_received);
    RUN(an_echo_after_the_release_is_received_and_refused);
    RUN(an_abort_in_a_byte_releases_and_the_next_transaction_is_clean);
    RUN(more_bytes_than_the_channel_takes);
    RUN(the_capture_at_its_limits);
    RUN(late_across_the_clocks_wrap);
    RUN(a_transaction_stays_inside_its_time);
    return test_summary("ew_pio");
}
