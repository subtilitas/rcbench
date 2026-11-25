/*
 * The PIO (programmable input/output) program of programming for YGE ESCs,
 * run on the host (protocols/openyge/rp2350/oy_line.pio), with the arithmetic
 * that feeds it and reads it (protocols/openyge/openyge_pio.c).
 *
 * The program is assembled from its source by a small assembler for the
 * instructions it uses, and run by a model of the state machine that steps one
 * state machine cycle at a time: JMP always, on !X, X--, Y-- and the pin; OUT
 * to the pins and Y; PULL with and without IFEMPTY; PUSH NOBLOCK, which a full
 * receive FIFO drops; MOV X, STATUS with the status all ones while the transmit
 * FIFO is empty; NOP; IN from the pins, Y and NULL, shifting right; SET of the
 * pins, the pin directions and Y; delays; the wrap.  A channel fills the
 * 4-word transmit FIFO from a given cycle and another drains the 4-word
 * receive FIFO, its upper half, into OY_PIO_RX_ENTRIES entries.  A cycle lasts
 * the divider's system clock cycles, fraction included, and the pin is read 2
 * system clock cycles after it moves.  The assembled words are held to
 * fixtures/oy_line_words.txt, which the firmware build holds pioasm's output
 * to: the model runs what the chip runs.
 *
 * Under test (PLAN row 4):
 *  - the divider: 81 + 97/256 at 150MHz, other clocks, refusals;
 *  - the stream's layout at its limits, the span, the late check across the
 *    clock's wrap;
 *  - the transmit: 16 cycles a bit, a telemetry request on the wire with its
 *    CRC (cyclic redundancy check), the release 16 cycles after the last stop
 *    bit's edge, within 1 cycle of the stop bit's end; no echo;
 *  - the receive: at every phase, a sender 3% and 4% slow or fast with no
 *    gaps, a reply right after the release;
 *  - framing errors: a low stop bit counted, a line held low, a low too short
 *    for a start bit;
 *  - the buffer full at 256B: the words after it dropped, counted once, and
 *    the channel started again;
 *  - a stream handed over while a byte comes in waits for that byte.
 *
 * Not tested here, because the host has no PIO: that the chip's input
 * synchroniser, PULL, PUSH NOBLOCK, MOV STATUS, JMP PIN, the fractional
 * divider, the channel's read of a FIFO word's upper half and the DMA (direct
 * memory access) handshakes behave as this model says; the pad, the pulls and
 * an ESC's levels.  Station step H1 covers those on a chip
 * (docs/OpenYGE.md section 9.2).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "openyge.h"
#include "openyge_pio.h"

#include "oy_pio_model.h"

#define SYS_HZ 150000000u
/** The nominal bit time at 115200 baud, ns. */
#define BIT_NS (1e9 / 115200.0)

static model_t m;
static oy_pio_clock_t clk;

/* @p cyc state machine cycles as ns. */
static double cyc_ns(uint64_t cyc)
{
    return (double)cyc * (double)clk.div256 / 256.0 * 1e9 / (double)clk.sys_hz;
}

static void ext_clear(void)
{
    m.n_ext = 0;
    m.ext0 = true;              /* the pad's pull-up */
}

/* A change of the released wire to @p level, @p ns after cycle 0. */
static void ext_edge(double ns, bool level)
{
    m.ext[m.n_ext].ns = ns;
    m.ext[m.n_ext].level = level;
    m.n_ext++;
}

/* A byte, its start bit @p at ns after cycle 0, @p bit ns a bit. */
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

/* The model at 150MHz: no stream, the receive channel 256 entries. */
static void reset(void)
{
    CHECK(oy_pio_clock(SYS_HZ, &clk));
    m.div256 = clk.div256;
    m.sys_hz = clk.sys_hz;
    m.n_tx = 0;
    m.tx_at = 0;
    m.rx_room = OY_PIO_RX_ENTRIES;
    ext_clear();
}

/* A frame handed over at cycle @p at. */
static void load(const uint8_t *frame, size_t n, uint64_t at)
{
    m.n_tx = oy_pio_stream(frame, n, m.tx);
    m.tx_at = at;
    CHECK(m.n_tx != 0u);
}

/* The bytes the model's channel wrote, through oy_pio_take(). */
static size_t take_all(oy_pio_rx_t *r, uint8_t *out, size_t room)
{
    oy_pio_rx_reset(r);
    return oy_pio_take(r, m.rx, m.n_rx, out, room);
}

/* The bytes the pad drove from its first low, read mid-bit, until the
 * release; every level change on the 16-cycle grid.  Returns how many and
 * the cycle the last stop bit began. */
static unsigned sent_bytes(uint8_t *out, unsigned room, uint64_t *last_stop)
{
    uint64_t at = 0;
    unsigned n = 0;
    const uint64_t rel = m.n_release > 0u ? m.release[0] : 0u;

    for (unsigned i = 0; i < m.n_pad; ++i) {
        if (m.pad[i].dir && !m.pad[i].latch) {
            at = m.pad[i].cyc;
            break;
        }
    }
    for (unsigned i = 0; i < m.n_pad; ++i) {
        if (m.pad[i].cyc > at && m.pad[i].cyc < rel) {
            CHECK_EQ((m.pad[i].cyc - at) % OY_PIO_CYCLES_PER_BIT, 0u);
        }
    }
    *last_stop = 0;
    while (at + 8u < rel && n < room) {
        unsigned v = 0;

        CHECK_EQ(pad_at(&m, at + 8u), 0);      /* the start bit */
        for (unsigned b = 0; b < 8u; ++b) {
            const int p = pad_at(&m, at + 24u + 16u * b);

            v |= (p == 1 ? 1u : 0u) << b;
        }
        CHECK_EQ(pad_at(&m, at + 152u), 1);    /* the stop bit */
        out[n++] = (uint8_t)v;
        *last_stop = at + 144u;
        at += 160u;
    }
    return n;
}

/* ------------------------------------------------------------------ cases */

TEST_CASE(the_words_are_the_ones_the_firmware_build_checks)
{
    FILE *f = fopen(OY_LINE_WORDS, "r");
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
    CHECK_EQ(prog_len, OY_PIO_PROGRAM_WORDS);
    CHECK(prog_len < 32u);
    CHECK_EQ(wrap_target, OY_PIO_WAIT_LO_PC);
    CHECK_EQ(wrap_at, OY_PIO_PROGRAM_WORDS - 1u);
    /* The instructions the driver and the model name by address. */
    CHECK_EQ(prog[OY_PIO_IDLE_HI_PC], 0xA025u);       /* mov x, status */
    CHECK_EQ(prog[OY_PIO_WAIT_LO_PC] & 0xFFE0u, 0x00C0u);  /* jmp pin */
    CHECK_EQ(prog[OY_PIO_SEND_PC], 0x80A0u);          /* pull */
    CHECK_EQ(prog[OY_PIO_RELEASE_PC], 0xE080u);       /* set pindirs, 0 */
    CHECK_EQ(prog[OY_PIO_STOP_HIGH_PC], 0x4041u);     /* in y, 1 */
}

TEST_CASE(the_divider_at_150mhz_is_81_and_97_256)
{
    oy_pio_clock_t c;

    CHECK(oy_pio_clock(SYS_HZ, &c));
    CHECK_EQ(c.div256, (81u << 8) + 97u);
    CHECK_EQ(c.sys_hz, SYS_HZ);
    /* 81.37890625 against 81.38020833: 16ppm, 0.002%. */
    CHECK_EQ(c.err_ppm, 16u);
    CHECK_EQ(oy_pio_bit_ns(&c), 8680u);
    CHECK_NEAR((double)oy_pio_bit_ns(&c), BIT_NS, BIT_NS * 0.005);
}

TEST_CASE(other_clocks_and_the_refusals)
{
    oy_pio_clock_t c;
    oy_pio_clock_t kept = { 1u, 2u, 3u };

    CHECK(oy_pio_clock(125000000u, &c));
    CHECK_EQ(c.div256, 17361u);
    CHECK(c.err_ppm <= OY_PIO_MAX_ERROR_PPM);
    CHECK_NEAR((double)oy_pio_bit_ns(&c), BIT_NS, 1.0);
    /* The top of the range of the clock's type. */
    CHECK(oy_pio_clock(0xFFFFFFFFu, &c));
    CHECK_NEAR((double)oy_pio_bit_ns(&c), BIT_NS, 1.0);
    /* A divider of 4 is the least: 4 x 16 cycles x 115200 bits per s. */
    CHECK(oy_pio_clock(7372800u, &c));
    CHECK_EQ(c.div256, 1024u);
    CHECK_EQ(c.err_ppm, 0u);
    CHECK(!oy_pio_clock(7340000u, &kept));
    CHECK_EQ(kept.div256, 2u);              /* left as it was */
    CHECK(!oy_pio_clock(0u, &c));
    CHECK(!oy_pio_clock(SYS_HZ, NULL));
}

TEST_CASE(the_stream_at_its_limits)
{
    static uint8_t tx[OY_PIO_TX_MAX + 1u];
    static uint32_t words[OY_PIO_TX_WORDS + 1u];

    memset(tx, 0x55, sizeof(tx));
    words[OY_PIO_TX_WORDS] = 0xA5A5A5A5u;
    CHECK_EQ(oy_pio_stream(tx, OY_PIO_TX_MAX, words), OY_PIO_TX_WORDS);
    CHECK_EQ(OY_PIO_TX_WORDS, 45u);
    CHECK_EQ(words[OY_PIO_TX_WORDS], 0xA5A5A5A5u);  /* nothing past it */
    CHECK_EQ(words[0], OY_PIO_TX_MAX * 10u - 1u);
    CHECK_EQ(oy_pio_stream(tx, OY_PIO_TX_MAX + 1u, words), 0u);

    /* One byte: the count and 1 word of bits. */
    tx[0] = 0xA5;
    CHECK_EQ(oy_pio_stream(tx, 1u, words), 2u);
    CHECK_EQ(words[0], 9u);
    CHECK_EQ(words[1], 0x34Au | 0x200u);

    /* A bit crossing a word: 3 bytes are 30 bits, 4 are 40. */
    memset(tx, 0xFF, 4u);
    CHECK_EQ(oy_pio_stream(tx, 4u, words), 3u);
    CHECK_EQ(words[1], 0xBFEFFBFEu);
    CHECK_EQ(words[2] & 0xFFu, 0xFFu);

    /* No bytes, no arguments. */
    CHECK_EQ(oy_pio_stream(tx, 0u, words), 0u);
    CHECK_EQ(oy_pio_stream(NULL, 1u, words), 0u);
    CHECK_EQ(oy_pio_stream(tx, 1u, NULL), 0u);
}

TEST_CASE(a_request_goes_out_and_the_pad_lets_go_at_the_stop_bits_end)
{
    uint8_t req[16];
    uint8_t got[16] = { 0 };
    uint64_t last_stop;
    openyge_decoder_t dec;
    openyge_frame_t fr;
    bool framed = false;
    const size_t n = openyge_build_telemetry_request(req, sizeof(req), 1u, 7u);

    memset(&fr, 0, sizeof(fr));
    CHECK_EQ(n, 12u);
    reset();
    load(req, n, 40u);
    model_run(&m, 40u + 12u * 160u + 400u);
    CHECK_EQ(m.n_send, 1u);
    CHECK_EQ(m.n_release, 1u);
    CHECK_EQ(sent_bytes(got, sizeof(got), &last_stop), 12u);
    CHECK(memcmp(got, req, 12u) == 0);
    /* 16 cycles a bit: 8680ns. */
    CHECK_NEAR(cyc_ns(OY_PIO_CYCLES_PER_BIT), 8680.4, 0.1);
    /* The release 16 cycles after the last stop bit's edge: at its end, within
     * 1 cycle; an input from there on. */
    CHECK(m.release[0] >= last_stop + OY_PIO_CYCLES_PER_BIT);
    CHECK(m.release[0] <= last_stop + OY_PIO_CYCLES_PER_BIT + 1u);
    CHECK_EQ(pad_at(&m, m.release[0] - 1u), 1);
    CHECK_EQ(pad_at(&m, m.release[0]), -1);
    CHECK_EQ(pad_at(&m, 40u + 12u * 160u + 399u), -1);
    /* Driven high before the first start bit, never driven before the
     * hand-over. */
    CHECK_EQ(pad_at(&m, 39u), -1);
    CHECK(m.pad[0].dir);
    CHECK(m.pad[0].latch);
    /* The frame decodes with a valid CRC. */
    openyge_decoder_reset(&dec);
    for (size_t i = 0; i < n; ++i) {
        framed = openyge_decode_byte(&dec, got[i], &fr) || framed;
    }
    CHECK(framed);
    CHECK_EQ(fr.hdr.type, (uint8_t)OPENYGE_FT_TELE_REQ);
    /* Its own bytes are not received. */
    CHECK_EQ(m.n_rx, 0u);
}

TEST_CASE(the_release_at_the_limits_of_the_frame)
{
    static uint8_t frame[OY_PIO_TX_MAX];
    static uint8_t got[OY_PIO_TX_MAX];
    const size_t sizes[] = { 1u, 2u, 3u, 4u, OY_PIO_TX_MAX - 1u, OY_PIO_TX_MAX };

    for (size_t i = 0; i < OY_PIO_TX_MAX; ++i) {
        frame[i] = (uint8_t)(i * 29u + 3u);
    }
    for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        uint64_t last_stop;
        const uint64_t span = (uint64_t)sizes[s] * 160u + 300u;

        reset();
        load(frame, sizes[s], 0u);
        model_run(&m, span);
        CHECK_EQ(sent_bytes(got, OY_PIO_TX_MAX, &last_stop), sizes[s]);
        CHECK(memcmp(got, frame, sizes[s]) == 0);
        CHECK_EQ(m.n_release, 1u);
        CHECK_EQ(m.release[0], last_stop + OY_PIO_CYCLES_PER_BIT);
        CHECK_EQ(pad_at(&m, span - 1u), -1);
        CHECK_EQ(m.n_rx, 0u);
        /* Inside the span the driver allows. */
        CHECK(cyc_ns(m.release[0]) <= oy_pio_span_us(&clk, sizes[s]) * 1000.0);
    }
}

TEST_CASE(a_reply_right_after_the_release_is_received)
{
    static const uint8_t ask[] = { 0xA5, 0x02 };
    static const uint8_t reply[] = { 0xA5, 0x03, 0x00, 0x22, 0x01, 0x81, 0x55 };
    uint8_t got[sizeof(reply)] = { 0 };
    oy_pio_rx_t r;
    double rel;

    /* A dry run for the release's time, then the reply one bit after it. */
    reset();
    load(ask, sizeof(ask), 10u);
    model_run(&m, 600u);
    CHECK_EQ(m.n_release, 1u);
    rel = cyc_ns(m.release[0]);
    for (unsigned i = 0; i < sizeof(reply); ++i) {
        ext_byte(rel + BIT_NS + i * 10.0 * BIT_NS, BIT_NS, reply[i], true);
    }
    model_run(&m, 600u + sizeof(reply) * 170u);
    CHECK_EQ(m.n_rx, sizeof(reply));
    CHECK_EQ(take_all(&r, got, sizeof(got)), sizeof(reply));
    CHECK(memcmp(got, reply, sizeof(reply)) == 0);
    CHECK_EQ(r.framing, 0u);
}

TEST_CASE(a_byte_at_every_phase)
{
    /* The start bit anywhere within 3 cycles, in 1/8 of a cycle. */
    for (unsigned phase = 0; phase < 24u; ++phase) {
        oy_pio_rx_t r;
        uint8_t got[2] = { 0 };

        reset();
        ext_byte(2000.0 + phase * cyc_ns(1u) / 8.0, BIT_NS, 0x5Bu, true);
        ext_byte(2000.0 + phase * cyc_ns(1u) / 8.0 + 10.0 * BIT_NS, BIT_NS,
                 0xC4u, true);
        model_run(&m, 600u);
        CHECK_EQ(m.n_rx, 2u);
        CHECK_EQ(take_all(&r, got, sizeof(got)), 2u);
        CHECK_EQ(got[0], 0x5Bu);
        CHECK_EQ(got[1], 0xC4u);
        CHECK_EQ(r.framing, 0u);
    }
}

/* @p n bytes with no gap at @p factor times the bit time: every byte right,
 * every stop high, for every phase in 1/4 of a cycle. */
static void long_reply_holds(unsigned n, double factor)
{
    static uint8_t got[OY_PIO_RX_ENTRIES];
    const double bit = BIT_NS * factor;

    for (unsigned phase = 0; phase < 12u; ++phase) {
        oy_pio_rx_t r;

        reset();
        for (unsigned i = 0; i < n; ++i) {
            ext_byte(1000.0 + phase * cyc_ns(1u) / 4.0 + i * bit * 10.0, bit,
                     (uint8_t)(i * 37u + 1u), true);
        }
        model_run(&m, 64u + (uint64_t)n * 170u);
        CHECK_EQ(m.n_rx, n);
        CHECK_EQ(take_all(&r, got, sizeof(got)), n);
        CHECK_EQ(r.framing, 0u);
        for (unsigned i = 0; i < n && i < m.n_rx; ++i) {
            CHECK_EQ(got[i], (uint8_t)(i * 37u + 1u));
        }
    }
}

TEST_CASE(a_byte_at_3_percent_off)
{
    long_reply_holds(1u, 0.97);
    long_reply_holds(1u, 1.03);
    long_reply_holds(OPENYGE_MAX_FRAME, 0.97);
    long_reply_holds(OPENYGE_MAX_FRAME, 1.0);
    long_reply_holds(OPENYGE_MAX_FRAME, 1.03);
}

TEST_CASE(a_sender_4_percent_off_with_no_gaps)
{
    long_reply_holds(OPENYGE_MAX_FRAME, 0.96);
    long_reply_holds(OPENYGE_MAX_FRAME, 1.04);
}

TEST_CASE(a_sender_5_percent_fast_with_no_gaps_is_out_of_reach)
{
    /* Past the limit the header gives: the start bits fall behind the
     * receiver's look and a byte is lost or wrong.  The suite fails if the
     * limit it documents stops being a limit. */
    oy_pio_rx_t r;
    static uint8_t got[OPENYGE_MAX_FRAME];
    bool all_right = true;

    reset();
    for (unsigned i = 0; i < OPENYGE_MAX_FRAME; ++i) {
        ext_byte(1000.0 + i * BIT_NS * 0.95 * 10.0, BIT_NS * 0.95,
                 (uint8_t)(i * 37u + 1u), true);
    }
    model_run(&m, 64u + OPENYGE_MAX_FRAME * 170u);
    (void)take_all(&r, got, sizeof(got));
    all_right = m.n_rx == OPENYGE_MAX_FRAME && r.framing == 0u;
    for (unsigned i = 0; all_right && i < m.n_rx; ++i) {
        all_right = got[i] == (uint8_t)(i * 37u + 1u);
    }
    CHECK(!all_right);
}

TEST_CASE(a_low_stop_bit_is_counted_and_the_next_byte_counts)
{
    oy_pio_rx_t r;
    uint8_t got[3] = { 0 };

    reset();
    ext_byte(1000.0, BIT_NS, 0x3Cu, false);
    /* 2 idle bits after the low stop bit, then 2 bytes. */
    ext_byte(1000.0 + 12.0 * BIT_NS, BIT_NS, 0x81u, true);
    ext_byte(1000.0 + 22.0 * BIT_NS, BIT_NS, 0x7Eu, true);
    model_run(&m, 700u);
    CHECK_EQ(m.n_rx, 3u);
    CHECK_EQ(take_all(&r, got, sizeof(got)), 3u);
    CHECK_EQ(got[0], 0x3Cu);
    CHECK_EQ(got[1], 0x81u);
    CHECK_EQ(got[2], 0x7Eu);
    CHECK_EQ(r.framing, 1u);
    CHECK_EQ(r.bytes, 3u);
    CHECK_EQ(m.rx[0] >> OY_PIO_RX_STOP_SHIFT, 0u);
    CHECK_EQ(m.rx[1] >> OY_PIO_RX_STOP_SHIFT, 1u);
}

TEST_CASE(a_line_held_low_is_one_byte)
{
    oy_pio_rx_t r;
    uint8_t got[2] = { 0 };

    reset();
    ext_edge(1000.0, false);
    ext_edge(1000.0 + 40.0 * BIT_NS, true);
    model_run(&m, 900u);
    CHECK_EQ(m.n_rx, 1u);
    CHECK_EQ(take_all(&r, got, sizeof(got)), 1u);
    CHECK_EQ(got[0], 0x00u);
    CHECK_EQ(r.framing, 1u);
}

TEST_CASE(a_low_too_short_for_a_start_bit_is_no_byte)
{
    /* Up to 7 cycles low: the sample 8 cycles on finds it high. */
    for (unsigned len = 1u; len <= 7u; ++len) {
        reset();
        ext_edge(1000.0, false);
        ext_edge(1000.0 + cyc_ns(len), true);
        model_run(&m, 400u);
        CHECK_EQ(m.n_rx, 0u);
    }
    /* A whole start bit with the line high after it: 0xFF. */
    reset();
    ext_edge(1000.0, false);
    ext_edge(1000.0 + BIT_NS, true);
    model_run(&m, 400u);
    CHECK_EQ(m.n_rx, 1u);
    CHECK_EQ((m.rx[0] >> OY_PIO_RX_DATA_SHIFT) & 0xFFu, 0xFFu);
}

TEST_CASE(the_buffer_is_full_at_256_bytes)
{
    static uint8_t got[OY_PIO_RX_ENTRIES + 8u];
    const unsigned n = OY_PIO_RX_ENTRIES + 14u;
    oy_pio_rx_t r;
    size_t total = 0;
    bool drop = false;

    reset();
    for (unsigned i = 0; i < n; ++i) {
        ext_byte(1000.0 + i * 10.0 * BIT_NS, BIT_NS, (uint8_t)i, true);
    }
    model_run(&m, 64u + (uint64_t)n * 170u);
    /* The channel stops at 256 entries, the FIFO holds 4 more, the machine
     * drops the rest and goes on. */
    CHECK_EQ(m.n_rx, OY_PIO_RX_ENTRIES);
    CHECK_EQ(m.dropped, n - OY_PIO_RX_ENTRIES - 4u);
    /* Taken 32 at a time, as the session takes them. */
    oy_pio_rx_reset(&r);
    for (unsigned call = 0; call < 9u; ++call) {
        total += oy_pio_take(&r, m.rx, m.n_rx, got + total, 32u);
    }
    CHECK_EQ(total, OY_PIO_RX_ENTRIES);
    CHECK(r.filled);
    CHECK_EQ(r.full, 1u);
    CHECK_EQ(r.bytes, OY_PIO_RX_ENTRIES);
    for (unsigned i = 0; i < OY_PIO_RX_ENTRIES; ++i) {
        CHECK_EQ(got[i], (uint8_t)i);
    }
    /* Started again at entry 0, the FIFO's words dropped, counted once. */
    CHECK_EQ(oy_pio_rearm(&r, m.n_rx, &drop), 0u);
    CHECK(drop);
    CHECK_EQ(r.full, 1u);
    CHECK(!r.filled);
    CHECK_EQ(r.rd, 0u);
    /* One entry short of full: no full, no drop. */
    reset();
    for (unsigned i = 0; i < OY_PIO_RX_ENTRIES - 1u; ++i) {
        ext_byte(1000.0 + i * 10.0 * BIT_NS, BIT_NS, (uint8_t)i, true);
    }
    model_run(&m, 64u + (uint64_t)OY_PIO_RX_ENTRIES * 170u);
    CHECK_EQ(m.n_rx, OY_PIO_RX_ENTRIES - 1u);
    CHECK_EQ(m.dropped, 0u);
    CHECK_EQ(take_all(&r, got, sizeof(got)), OY_PIO_RX_ENTRIES - 1u);
    CHECK(!r.filled);
    CHECK_EQ(r.full, 0u);
    CHECK_EQ(oy_pio_rearm(&r, m.n_rx, &drop), 0u);
    CHECK(!drop);
}

TEST_CASE(take_and_rearm_at_their_limits)
{
    uint16_t ent[OY_PIO_RX_ENTRIES];
    uint8_t buf[OY_PIO_RX_ENTRIES];
    oy_pio_rx_t r;
    bool drop = true;

    for (unsigned i = 0; i < OY_PIO_RX_ENTRIES; ++i) {
        ent[i] = (uint16_t)((1u << OY_PIO_RX_STOP_SHIFT)
                            | ((i & 0xFFu) << OY_PIO_RX_DATA_SHIFT));
    }
    ent[3] = (uint16_t)(0x42u << OY_PIO_RX_DATA_SHIFT);   /* stop low */
    oy_pio_rx_reset(&r);
    /* No room takes nothing. */
    CHECK_EQ(oy_pio_take(&r, ent, 5u, buf, 0u), 0u);
    CHECK_EQ(r.rd, 0u);
    /* Part, then the rest. */
    CHECK_EQ(oy_pio_take(&r, ent, 5u, buf, 2u), 2u);
    CHECK_EQ(oy_pio_take(&r, ent, 5u, buf, 32u), 3u);
    CHECK_EQ(buf[1], 0x42u);
    CHECK_EQ(r.framing, 1u);
    CHECK_EQ(r.bytes, 5u);
    CHECK_EQ(oy_pio_take(&r, ent, 5u, buf, 32u), 0u);
    /* Entries came in while the channel stopped: it goes on at the next
     * entry, nothing handed over twice. */
    CHECK_EQ(oy_pio_rearm(&r, 7u, &drop), 7u);
    CHECK(!drop);
    CHECK_EQ(r.rd, 5u);
    CHECK_EQ(oy_pio_take(&r, ent, 7u, buf, 32u), 2u);
    CHECK_EQ(oy_pio_rearm(&r, 7u, &drop), 0u);
    CHECK_EQ(r.rd, 0u);
    CHECK_EQ(r.bytes, 7u);              /* the counts go on */
    /* A count past the buffer is held to it. */
    oy_pio_rx_reset(&r);
    CHECK_EQ(oy_pio_take(&r, ent, OY_PIO_RX_ENTRIES + 9u, buf, sizeof(buf)),
             OY_PIO_RX_ENTRIES);
    CHECK_EQ(r.full, 1u);
    /* Full with entries still to hand over: stays stopped, counted once. */
    oy_pio_rx_reset(&r);
    CHECK_EQ(oy_pio_take(&r, ent, OY_PIO_RX_ENTRIES, buf, 10u), 10u);
    CHECK_EQ(oy_pio_rearm(&r, OY_PIO_RX_ENTRIES, &drop),
             OY_PIO_RX_ENTRIES);
    CHECK(!drop);
    CHECK_EQ(oy_pio_take(&r, ent, OY_PIO_RX_ENTRIES, buf, sizeof(buf)),
             OY_PIO_RX_ENTRIES - 10u);
    CHECK_EQ(r.full, 1u);
    CHECK_EQ(oy_pio_rearm(&r, OY_PIO_RX_ENTRIES + 3u, &drop), 0u);
    CHECK(drop);
    CHECK_EQ(r.full, 1u);
    /* Full found first at the rearm, never by a take: counted there. */
    oy_pio_rx_reset(&r);
    r.rd = OY_PIO_RX_ENTRIES;
    CHECK_EQ(oy_pio_rearm(&r, OY_PIO_RX_ENTRIES, &drop), 0u);
    CHECK(drop);
    CHECK_EQ(r.full, 1u);
}

TEST_CASE(a_stream_handed_over_in_a_byte_waits_for_its_end)
{
    static const uint8_t ask[] = { 0x00 };
    oy_pio_rx_t r;
    uint8_t got[2] = { 0 };
    uint8_t sent[2] = { 0 };
    uint64_t last_stop;
    const double start = 1000.0;
    uint64_t first_drive = 0;

    reset();
    ext_byte(start, BIT_NS, 0x96u, true);
    /* Handed over 3 bits into the byte. */
    load(ask, sizeof(ask), (uint64_t)((start + 3.0 * BIT_NS) / cyc_ns(1u)));
    model_run(&m, 900u);
    CHECK_EQ(m.n_rx, 1u);
    CHECK_EQ(take_all(&r, got, sizeof(got)), 1u);
    CHECK_EQ(got[0], 0x96u);
    CHECK_EQ(r.framing, 0u);
    CHECK_EQ(m.n_pad > 0u, true);
    first_drive = m.pad[0].cyc;
    /* Not driven before the byte's stop bit was sampled, and driven high:
     * the stop bit's own level. */
    CHECK(cyc_ns(first_drive) > start + 9.5 * BIT_NS);
    CHECK(m.pad[0].latch);
    CHECK_EQ(sent_bytes(sent, sizeof(sent), &last_stop), 1u);
    CHECK_EQ(sent[0], 0x00u);
    /* The start bit after the byte's stop bit has ended. */
    CHECK(cyc_ns(last_stop - 144u) >= start + 10.0 * BIT_NS);
    /* Inside the span from the hand-over. */
    CHECK(cyc_ns(m.release[0] - m.tx_at) <= oy_pio_span_us(&clk, 1u) * 1000.0);
}

TEST_CASE(two_frames_one_after_the_other)
{
    static const uint8_t a[] = { 0x11, 0x22 };
    uint8_t sent[4] = { 0 };
    uint64_t last_stop;

    /* Both streams in the FIFO path at once: two sends, two releases, the
     * receiver between them. */
    reset();
    m.n_tx = oy_pio_stream(a, sizeof(a), m.tx);
    m.n_tx += oy_pio_stream(a, sizeof(a), m.tx + m.n_tx);
    model_run(&m, 1000u);
    CHECK_EQ(m.n_send, 2u);
    CHECK_EQ(m.n_release, 2u);
    CHECK(m.send[1] > m.release[0]);
    CHECK_EQ(sent_bytes(sent, sizeof(sent), &last_stop), 2u);
    CHECK_EQ(m.n_rx, 0u);
}

TEST_CASE(span_sending_and_late)
{
    /* 12 bytes at 150MHz: 166 + 2 + 1920 cycles of 542.5ns, rounded up. */
    CHECK(oy_pio_clock(SYS_HZ, &clk));
    CHECK_EQ(oy_pio_span_us(&clk, 12u), 1133u);
    CHECK_EQ(oy_pio_span_us(&clk, 0u), 92u);
    CHECK(oy_pio_span_us(&clk, OY_PIO_TX_MAX) > 140u * 10u * 8680u / 1000u);
    /* 1000 + 1000 grace, from 16us before the wrap. */
    CHECK(!oy_pio_late(1000u, 1984u, 0xFFFFFFF0u));
    CHECK(oy_pio_late(1000u, 1985u, 0xFFFFFFF0u));
    CHECK(!oy_pio_late(1000u, 100u, 100u));
    /* A span at the top of its type does not wrap the limit. */
    CHECK(!oy_pio_late(0xFFFFFFFFu, 0xFFFFFFFFu, 0u));
    /* The frame's way out, by address. */
    CHECK(!oy_pio_sending(OY_PIO_SEND_PC - 1u));
    CHECK(oy_pio_sending(OY_PIO_SEND_PC));
    CHECK(oy_pio_sending(OY_PIO_RELEASE_PC));
    CHECK(!oy_pio_sending(OY_PIO_RELEASE_PC + 1u));
    CHECK(!oy_pio_sending(OY_PIO_WAIT_LO_PC));
}

int main(void)
{
    if (!assemble(OY_LINE_PIO)) {
        printf("cannot assemble %s\n", OY_LINE_PIO);
        return 1;
    }
    RUN(the_words_are_the_ones_the_firmware_build_checks);
    RUN(the_divider_at_150mhz_is_81_and_97_256);
    RUN(other_clocks_and_the_refusals);
    RUN(the_stream_at_its_limits);
    RUN(a_request_goes_out_and_the_pad_lets_go_at_the_stop_bits_end);
    RUN(the_release_at_the_limits_of_the_frame);
    RUN(a_reply_right_after_the_release_is_received);
    RUN(a_byte_at_every_phase);
    RUN(a_byte_at_3_percent_off);
    RUN(a_sender_4_percent_off_with_no_gaps);
    RUN(a_sender_5_percent_fast_with_no_gaps_is_out_of_reach);
    RUN(a_low_stop_bit_is_counted_and_the_next_byte_counts);
    RUN(a_line_held_low_is_one_byte);
    RUN(a_low_too_short_for_a_start_bit_is_no_byte);
    RUN(the_buffer_is_full_at_256_bytes);
    RUN(take_and_rearm_at_their_limits);
    RUN(a_stream_handed_over_in_a_byte_waits_for_its_end);
    RUN(two_frames_one_after_the_other);
    RUN(span_sending_and_late);
    return test_summary("openyge_pio");
}
