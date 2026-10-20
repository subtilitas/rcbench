/*
 * The precharge-and-sample receiver's core (protocols/hitec/hitec_pre.c):
 * the cycle arithmetic, the sampler's PIO program, the decoder and the
 * run, without a pin.
 *
 * A wire model gives the level a D series servo leaves on the line at any
 * time: low at rest, high for a start bit and a data 0, low for a data 1
 * and the stop bit.  sample_wire() reads it the way the sampler does,
 * sample k at k * period + charge + wait cycles less the input
 * synchroniser's 2, and packs the samples as the state machine pushes
 * them: shifting left, 32 to a word.
 *
 * Under test:
 *  - the counts at 150 MHz and at every clock the arithmetic takes, and
 *    the clocks it refuses;
 *  - the program, stepped instruction by instruction: the precharge, the
 *    release, the sample and the period;
 *  - a 7-byte answer and its trailing byte at 7.99 samples a bit, the
 *    servo's bit time 4 % short and long, and the answer's times;
 *  - a start bit on the first sample and on the window's last, a byte
 *    across the window's end, a framing error, no answer, a line stuck
 *    high from the start and after a byte, a glitch;
 *  - the 30 ms window and the one above it, a sampler that stops short,
 *    words fed one at a time against all at once;
 *  - the run's plans, and an abort in every phase.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <string.h>

#include "greatest.h"

#include "hitec_pio.h"
#include "hitec_pre.h"
#include "hitec_wire.h"

/* ------------------------------------------------------------ the wire */

#define MAX_FRAMES 24u

typedef struct {
    double   at_ns;     /* the start bit's leading edge */
    double   bit_ns;
    uint8_t  value;
    uint8_t  stop_high; /* 1: the stop bit is high, a framing error */
} frame_t;

static frame_t  frames[MAX_FRAMES];
static unsigned n_frames;
static double   stuck_from_ns;   /* the line high from here on; <0 never */
static double   glitch_from_ns;  /* high for glitch_ns; <0 none */
static double   glitch_ns;

static void wire_clear(void)
{
    n_frames = 0;
    stuck_from_ns = -1.0;
    glitch_from_ns = -1.0;
    glitch_ns = 0.0;
}

static void wire_byte(double at_ns, double bit_ns, uint8_t value,
                      bool stop_high)
{
    frames[n_frames].at_ns = at_ns;
    frames[n_frames].bit_ns = bit_ns;
    frames[n_frames].value = value;
    frames[n_frames].stop_high = stop_high ? 1u : 0u;
    n_frames++;
}

/* @p n bytes back to back from @p at_ns. */
static void wire_bytes(double at_ns, double bit_ns, const uint8_t *v,
                       unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        wire_byte(at_ns + i * 10.0 * bit_ns, bit_ns, v[i], false);
    }
}

/* 1 high. */
static unsigned wire_at(double ns)
{
    if (stuck_from_ns >= 0.0 && ns >= stuck_from_ns) {
        return 1u;
    }
    if (glitch_from_ns >= 0.0 && ns >= glitch_from_ns
        && ns < glitch_from_ns + glitch_ns) {
        return 1u;
    }
    for (unsigned i = 0; i < n_frames; ++i) {
        const frame_t *f = &frames[i];
        const double b = (ns - f->at_ns) / f->bit_ns;

        if (b < 0.0 || b >= 10.0) {
            continue;
        }
        if (b < 1.0) {
            return 1u;                                  /* start */
        }
        if (b < 9.0) {
            return ((f->value >> (unsigned)(b - 1.0)) & 1u) ? 0u : 1u;
        }
        return f->stop_high;                            /* stop */
    }
    return 0u;
}

static uint32_t words[HITEC_PRE_WORDS];
static hitec_pre_timing_t T;
static const double D_BIT_NS = 1e9 / 115200.0;

/* The sampler's words for @p n_words words, from its start. */
static void sample_wire(const hitec_pre_timing_t *t, uint32_t n_words)
{
    memset(words, 0, sizeof(words));
    for (uint32_t k = 0; k < n_words * 32u; ++k) {
        const double cyc = (double)k * t->period + t->charge + t->wait
                           - HITEC_PIO_SYNC_SYS_CYCLES;
        const double ns = cyc * 1e9 / t->sys_hz;

        if (wire_at(ns) != 0u) {
            words[k >> 5] |= 1u << (31u - (k & 31u));
        }
    }
}

/* A LISTEN of @p window_us for @p max_rx bytes, run on the words the wire
 * gives, all at once. */
static hitec_pre_run_t run;
static hitec_capture_t cap;

static void listen_run(uint32_t window_us, uint8_t max_rx)
{
    hitec_xfer_t x;

    CHECK(hitec_listen_xfer(HITEC_D, window_us, max_rx, false, 0, &x));
    CHECK_EQ(hitec_pre_begin(&run, &T, &x), HITEC_PRE_PLAN_LISTEN);
    sample_wire(&T, run.words);
    CHECK(hitec_pre_step(&run, &T, words, run.words, true));
    hitec_pre_capture(&run, &T, 0, &cap);
}

static const uint8_t k_answer[8] = { 0x69, 0x00, 0x32, 0x02, 0x10, 0x27,
                                     0x6B, 0x00 };

/* ---------------------------------------------------------------- cases */

TEST_CASE(the_counts_at_150_mhz_are_the_bench_period_and_the_middle_of_the_window)
{
    CHECK(hitec_pre_timing(150000000u, &T));
    CHECK_EQ(T.period, 163u);
    CHECK_EQ(T.charge, 15u);
    CHECK_EQ(T.wait, 17u);
    CHECK_EQ(hitec_pre_cycles_ns(&T, T.period), 1087u);
    CHECK_EQ(hitec_pre_cycles_ns(&T, T.charge), 100u);
    CHECK_EQ(hitec_pre_cycles_ns(&T, T.wait), 113u);
    /* 7.99 samples a bit. */
    CHECK(T.spb_q16 >= (uint32_t)(7.985 * 65536.0)
          && T.spb_q16 <= (uint32_t)(7.995 * 65536.0));
    CHECK_EQ(T.mid[0], 3u);
    CHECK_EQ(T.mid[1], 11u);
    CHECK_EQ(T.mid[9], 75u);
    /* 25 ms, the read's window: 23 006 samples. */
    CHECK_EQ(hitec_pre_window_samples(&T, 25000u), 23006u);
    CHECK_EQ(hitec_pre_window_samples(&T, 0u), 0u);
}

TEST_CASE(every_clock_the_arithmetic_takes_stays_in_the_measured_window)
{
    hitec_pre_timing_t t;
    uint16_t prog[HITEC_PRE_PROGRAM_MAX];

    for (uint32_t hz = HITEC_PRE_MIN_HZ; hz <= HITEC_PRE_MAX_HZ;
         hz += 1000000u) {
        CHECK(hitec_pre_timing(hz, &t));
        const uint32_t c = hitec_pre_cycles_ns(&t, t.charge);
        const uint32_t w = hitec_pre_cycles_ns(&t, t.wait);
        const double spb = t.spb_q16 / 65536.0;

        CHECK(c >= HITEC_PRE_CHARGE_MIN_NS && c <= HITEC_PRE_CHARGE_MAX_NS);
        CHECK(w >= HITEC_PRE_SAMPLE_MIN_NS && w <= HITEC_PRE_SAMPLE_MAX_NS);
        CHECK(spb > 7.8 && spb < 8.2);
        CHECK(hitec_pre_program(&t, prog, HITEC_PRE_PROGRAM_MAX) > 0u);
        /* The longest window with every byte fits the buffer. */
        CHECK(hitec_pre_words(&t, HITEC_PRE_WINDOW_MAX_US, HITEC_RX_MAX)
              > 0u);
        CHECK_EQ(hitec_pre_words(&t, HITEC_PRE_WINDOW_MAX_US + 1u, 1u), 0u);
    }
    /* Both ends and one past each. */
    CHECK(hitec_pre_timing(HITEC_PRE_MIN_HZ, &t));
    CHECK(hitec_pre_timing(HITEC_PRE_MAX_HZ, &t));
    t.period = 77u;
    CHECK(!hitec_pre_timing(HITEC_PRE_MIN_HZ - 1u, &t));
    CHECK(!hitec_pre_timing(HITEC_PRE_MAX_HZ + 1u, &t));
    CHECK(!hitec_pre_timing(0u, &t));
    CHECK_EQ(t.period, 77u);   /* left as it was */
    CHECK(!hitec_pre_timing(150000000u, NULL));
}

/* Step the program one instruction at a time: the cycles the pad is
 * driven, where the sample falls and the period. */
static void check_program(uint32_t hz)
{
    hitec_pre_timing_t t;
    uint16_t prog[HITEC_PRE_PROGRAM_MAX];
    unsigned n;
    uint32_t cyc = 0;
    uint32_t drive_on = 0xFFFFFFFFu;
    uint32_t drive_off = 0xFFFFFFFFu;
    uint32_t sample = 0xFFFFFFFFu;
    unsigned samples = 0;

    CHECK(hitec_pre_timing(hz, &t));
    n = hitec_pre_program(&t, prog, HITEC_PRE_PROGRAM_MAX);
    CHECK(n >= 3u && n <= HITEC_PRE_PROGRAM_MAX);
    for (unsigned i = 0; i < n; ++i) {
        const uint16_t w = prog[i];
        const unsigned op = w >> 13;
        const unsigned delay = (w >> 8) & 0x1Fu;

        if (op == 7u) {                       /* SET */
            CHECK_EQ((w >> 5) & 7u, 4u);      /* pindirs */
            if ((w & 0x1Fu) == 1u) {
                drive_on = cyc;
            } else {
                CHECK_EQ(w & 0x1Fu, 0u);
                drive_off = cyc;
            }
        } else if (op == 2u) {                /* IN pins, 1 */
            CHECK_EQ(w & 0xFFu, 0x01u);
            sample = cyc;
            samples++;
        } else {
            CHECK_EQ(w & 0xE0FFu, 0xA042u);   /* mov y, y */
        }
        cyc += 1u + delay;
    }
    CHECK_EQ(drive_on, 0u);
    CHECK_EQ(drive_off, t.charge);
    CHECK_EQ(sample, t.charge + t.wait);
    CHECK_EQ(samples, 1u);
    CHECK_EQ(cyc, t.period);
    /* One word short refuses. */
    CHECK_EQ(hitec_pre_program(&t, prog, n - 1u), 0u);
    CHECK_EQ(hitec_pre_program(&t, prog, n), n);
}

TEST_CASE(the_program_precharges_releases_samples_once_and_keeps_the_period)
{
    check_program(150000000u);
    check_program(HITEC_PRE_MIN_HZ);
    check_program(HITEC_PRE_MAX_HZ);
    check_program(125000000u);
    check_program(200000000u);
    CHECK(hitec_pre_timing(150000000u, &T));
    {
        uint16_t prog[HITEC_PRE_PROGRAM_MAX];

        /* The bench's words: set pindirs, 1 [14], set pindirs, 0 [16],
         * in pins, 1 [31], 3 nops of 32 and one of 3. */
        CHECK_EQ(hitec_pre_program(&T, prog, HITEC_PRE_PROGRAM_MAX), 7u);
        CHECK_EQ(prog[0], 0xEE81u);
        CHECK_EQ(prog[1], 0xF080u);
        CHECK_EQ(prog[2], 0x5F01u);
        CHECK_EQ(prog[3], 0xBF42u);
        CHECK_EQ(prog[6], 0xA242u);
        CHECK_EQ(hitec_pre_program(NULL, prog, 7u), 0u);
        CHECK_EQ(hitec_pre_program(&T, NULL, 7u), 0u);
    }
}

TEST_CASE(a_seven_byte_answer_decodes_at_7_99_samples_a_bit)
{
    hitec_reply_t r;

    CHECK(hitec_pre_timing(150000000u, &T));
    /* The answer of register 0x32 5.9 ms into the window, its trailing
     * 0x00 behind it, as the D485HW sends them. */
    wire_clear();
    wire_bytes(5.9e6, D_BIT_NS, k_answer, 8u);
    listen_run(HITEC_D_WINDOW_US, HITEC_D_READ_RX);
    CHECK_EQ(cap.n, 8u);
    CHECK_EQ(cap.full, 1u);
    CHECK_EQ(cap.overflow, 0u);
    CHECK_EQ(cap.aborted, 0u);
    CHECK_EQ(cap.window_ns, 25000000u);
    for (unsigned i = 0; i < 8u; ++i) {
        CHECK_EQ(cap.byte[i].value, k_answer[i]);
        CHECK_EQ(cap.byte[i].framing, 0u);
        /* Within half a sample period of the edge. */
        CHECK(fabs((double)cap.byte[i].at_ns - (5.9e6 + i * 10.0 * D_BIT_NS))
              <= 544.0);
    }
    CHECK_EQ(run.dec.end, HITEC_PRE_END_FULL);
    /* The capture is the PULL-UP receiver's: hitec_d_decode() takes it,
     * the times taken from the end of the request as a SEND gives them. */
    CHECK_EQ(hitec_d_decode(&cap, 0x32u, &r), HITEC_RX_OK);
    CHECK_EQ(r.value, 0x2710u);
}

TEST_CASE(the_servo_bit_time_4_percent_short_or_long_decodes)
{
    static const double scale[] = { 0.96, 0.98, 1.0, 1.02, 1.04 };

    CHECK(hitec_pre_timing(150000000u, &T));
    for (unsigned s = 0; s < sizeof(scale) / sizeof(scale[0]); ++s) {
        /* At every phase of the edge against the samples. */
        for (unsigned ph = 0; ph < 8u; ++ph) {
            wire_clear();
            wire_bytes(3.0e6 + ph * 136.0, D_BIT_NS * scale[s], k_answer, 8u);
            listen_run(10000u, 8u);
            CHECK_EQ(cap.n, 8u);
            for (unsigned i = 0; i < 8u; ++i) {
                CHECK_EQ(cap.byte[i].value, k_answer[i]);
                CHECK_EQ(cap.byte[i].framing, 0u);
            }
        }
    }
}

TEST_CASE(a_start_bit_on_the_first_sample_is_not_taken)
{
    CHECK(hitec_pre_timing(150000000u, &T));
    /* 0xFF: the start bit high, then low to the end of the frame.  The
     * receiver waits for the rest level first, as the PULL-UP receiver
     * does, and the line never leaves it again. */
    wire_clear();
    wire_byte(-100.0, D_BIT_NS, 0xFF, false);
    listen_run(2000u, 1u);
    CHECK_EQ(cap.n, 0u);
    CHECK_EQ(run.dec.end, HITEC_PRE_END_WINDOW);
    /* The same byte one sample later is taken. */
    wire_clear();
    wire_byte(1100.0, D_BIT_NS, 0xFF, false);
    listen_run(2000u, 1u);
    CHECK_EQ(cap.n, 1u);
    CHECK_EQ(cap.byte[0].value, 0xFFu);
}

TEST_CASE(a_start_bit_on_the_last_sample_of_the_window_is_taken_whole)
{
    uint32_t w;
    double last_ns;

    CHECK(hitec_pre_timing(150000000u, &T));
    w = hitec_pre_window_samples(&T, 1000u);
    /* Sample w - 1 is the window's last; the edge just before it. */
    last_ns = ((double)(w - 1u) * T.period + T.charge + T.wait - 2.0) * 1e9
              / T.sys_hz;
    wire_clear();
    wire_byte(last_ns - 300.0, D_BIT_NS, 0xA5, false);
    listen_run(1000u, 1u);
    CHECK_EQ(cap.n, 1u);
    CHECK_EQ(cap.byte[0].value, 0xA5u);
    CHECK_EQ(cap.byte[0].framing, 0u);
    CHECK_EQ(run.dec.start[0], w - 1u);
    CHECK_EQ(cap.overflow, 0u);
    /* One sample later the window is over. */
    wire_clear();
    wire_byte(last_ns + 1086.0 - 300.0, D_BIT_NS, 0xA5, false);
    listen_run(1000u, 1u);
    CHECK_EQ(cap.n, 0u);
    CHECK_EQ(run.dec.end, HITEC_PRE_END_WINDOW);
}

TEST_CASE(a_byte_across_the_window_end_and_the_bytes_behind_it)
{
    CHECK(hitec_pre_timing(150000000u, &T));
    /* 8 bytes from 0.5 ms into a 0.6 ms window, 0.69 ms long: the bytes
     * do not count against the window, so the waiting has 0.1 ms left
     * when the last byte ends, past the window's end in time. */
    wire_clear();
    wire_bytes(0.5e6, D_BIT_NS, k_answer, 8u);
    listen_run(600u, 8u);
    CHECK_EQ(cap.n, 8u);
    for (unsigned i = 0; i < 8u; ++i) {
        CHECK_EQ(cap.byte[i].value, k_answer[i]);
    }
    CHECK_EQ(cap.overflow, 0u);
    /* A gap after byte 2 longer than the window has left: the rest are
     * not taken. */
    wire_clear();
    wire_bytes(0.4e6, D_BIT_NS, k_answer, 3u);
    wire_bytes(0.4e6 + 30.0 * D_BIT_NS + 150e3, D_BIT_NS, k_answer + 3, 5u);
    listen_run(500u, 8u);
    CHECK_EQ(cap.n, 3u);
    CHECK_EQ(run.dec.end, HITEC_PRE_END_WINDOW);
}

TEST_CASE(a_stop_bit_read_high_is_a_framing_error)
{
    hitec_reply_t r;

    CHECK(hitec_pre_timing(150000000u, &T));
    wire_clear();
    for (unsigned i = 0; i < 7u; ++i) {
        wire_byte(6.0e6 + i * 10.0 * D_BIT_NS, D_BIT_NS, k_answer[i],
                  i == 6u);
    }
    listen_run(HITEC_D_WINDOW_US, 7u);
    CHECK_EQ(cap.n, 7u);
    for (unsigned i = 0; i < 7u; ++i) {
        CHECK_EQ(cap.byte[i].framing, i == 6u ? 1u : 0u);
        CHECK_EQ(cap.byte[i].value, k_answer[i]);
    }
    CHECK_EQ(run.dec.framing, 1u << 6);
    CHECK_EQ(hitec_d_decode(&cap, 0x32u, &r), HITEC_RX_ERR_FRAMING);
}

TEST_CASE(no_answer_ends_with_the_window_and_decodes_silent)
{
    hitec_reply_t r;

    CHECK(hitec_pre_timing(150000000u, &T));
    wire_clear();
    listen_run(HITEC_D_WINDOW_US, HITEC_D_READ_RX);
    CHECK_EQ(cap.n, 0u);
    CHECK_EQ(cap.full, 0u);
    CHECK_EQ(cap.overflow, 0u);
    CHECK_EQ(run.dec.end, HITEC_PRE_END_WINDOW);
    CHECK_EQ(run.dec.waited, run.dec.window);
    CHECK_EQ(hitec_d_decode(&cap, 0x32u, &r), HITEC_RX_ERR_SILENT);
}

TEST_CASE(a_line_stuck_high_gives_no_byte_or_one_framing_byte)
{
    hitec_reply_t r;

    CHECK(hitec_pre_timing(150000000u, &T));
    /* High from the start: the line never rests. */
    wire_clear();
    stuck_from_ns = 0.0;
    listen_run(HITEC_D_WINDOW_US, HITEC_D_READ_RX);
    CHECK_EQ(cap.n, 0u);
    CHECK_EQ(run.dec.end, HITEC_PRE_END_WINDOW);
    CHECK_EQ(hitec_d_decode(&cap, 0x32u, &r), HITEC_RX_ERR_SILENT);
    /* High from the middle of byte 2: byte 2 is 0x00 with a framing
     * error, and nothing after it. */
    wire_clear();
    wire_bytes(6.0e6, D_BIT_NS, k_answer, 7u);
    stuck_from_ns = 6.0e6 + 21.0 * D_BIT_NS;
    listen_run(HITEC_D_WINDOW_US, HITEC_D_READ_RX);
    CHECK_EQ(cap.n, 3u);
    CHECK_EQ(cap.byte[0].value, 0x69u);
    CHECK_EQ(cap.byte[1].value, 0x00u);
    CHECK_EQ(cap.byte[2].framing, 1u);
    CHECK_EQ(hitec_d_decode(&cap, 0x32u, &r), HITEC_RX_ERR_LENGTH);
}

TEST_CASE(a_glitch_shorter_than_half_a_bit_is_not_a_start_bit)
{
    CHECK(hitec_pre_timing(150000000u, &T));
    wire_clear();
    glitch_from_ns = 1.0e6;
    glitch_ns = 2500.0;    /* 2 to 3 samples, under half a bit */
    wire_bytes(2.0e6, D_BIT_NS, k_answer, 8u);
    listen_run(5000u, 8u);
    CHECK_EQ(cap.n, 8u);
    CHECK_EQ(cap.byte[0].value, 0x69u);
}

TEST_CASE(the_30_ms_window_fits_and_the_next_microsecond_does_not)
{
    hitec_xfer_t x;
    hitec_pre_run_t r;

    CHECK(hitec_pre_timing(150000000u, &T));
    CHECK(hitec_pre_words(&T, HITEC_PRE_WINDOW_MAX_US, HITEC_RX_MAX) > 0u);
    CHECK(hitec_pre_words(&T, HITEC_PRE_WINDOW_MAX_US, HITEC_RX_MAX)
          <= HITEC_PRE_WORDS);
    CHECK_EQ(hitec_pre_words(&T, HITEC_PRE_WINDOW_MAX_US + 1u, 1u), 0u);
    CHECK_EQ(hitec_pre_words(NULL, 1000u, 1u), 0u);
    /* An answer at 29.9 ms of a 30 ms window, 16 bytes. */
    wire_clear();
    {
        uint8_t v[16];

        for (unsigned i = 0; i < 16u; ++i) {
            v[i] = (uint8_t)(0x11u * i);
        }
        wire_bytes(29.9e6, D_BIT_NS, v, 16u);
        listen_run(HITEC_PRE_WINDOW_MAX_US, HITEC_RX_MAX);
        CHECK_EQ(cap.n, 16u);
        CHECK_EQ(cap.overflow, 0u);
        for (unsigned i = 0; i < 16u; ++i) {
            CHECK_EQ(cap.byte[i].value, v[i]);
        }
    }
    CHECK(hitec_listen_xfer(HITEC_D, HITEC_PRE_WINDOW_MAX_US + 1u, 1u, false,
                            0, &x));
    CHECK_EQ(hitec_pre_begin(&r, &T, &x), HITEC_PRE_PLAN_REFUSE);
}

TEST_CASE(a_sampler_that_stops_short_keeps_the_whole_bytes_and_overflows)
{
    uint32_t got;

    CHECK(hitec_pre_timing(150000000u, &T));
    wire_clear();
    wire_bytes(1.0e6, D_BIT_NS, k_answer, 8u);
    {
        hitec_xfer_t x;

        CHECK(hitec_listen_xfer(HITEC_D, 5000u, 8u, false, 0, &x));
        CHECK_EQ(hitec_pre_begin(&run, &T, &x), HITEC_PRE_PLAN_LISTEN);
    }
    sample_wire(&T, run.words);
    /* The words up to the middle of byte 3's data. */
    got = (uint32_t)((1.0e6 + 35.0 * D_BIT_NS) / 1086.67) / 32u;
    CHECK(!hitec_pre_step(&run, &T, words, got, false));
    CHECK_EQ(run.phase, HITEC_PRE_SAMPLE);
    CHECK(hitec_pre_step(&run, &T, words, got, true));
    hitec_pre_capture(&run, &T, 0, &cap);
    CHECK_EQ(cap.n, 3u);
    CHECK_EQ(cap.overflow, 1u);
    CHECK_EQ(cap.aborted, 0u);
    CHECK_EQ(run.dec.end, HITEC_PRE_END_SHORT);
    /* A run that ends with its window is not overflowed. */
    wire_clear();
    listen_run(1000u, 1u);
    CHECK_EQ(cap.overflow, 0u);
}

TEST_CASE(words_fed_one_at_a_time_decode_as_all_at_once)
{
    CHECK(hitec_pre_timing(150000000u, &T));
    for (unsigned trial = 0; trial < 40u; ++trial) {
        hitec_capture_t once;
        hitec_xfer_t x;

        wire_clear();
        wire_bytes(2.0e6 + trial * 3711.0, D_BIT_NS * (0.97 + trial * 0.0015),
                   k_answer, 8u);
        if (trial % 5u == 0u) {
            glitch_from_ns = 1.5e6 + trial * 100.0;
            glitch_ns = 1500.0;
        }
        listen_run(8000u, 8u);
        once = cap;
        CHECK(hitec_listen_xfer(HITEC_D, 8000u, 8u, false, 0, &x));
        CHECK_EQ(hitec_pre_begin(&run, &T, &x), HITEC_PRE_PLAN_LISTEN);
        for (uint32_t n = 0; n <= run.words; ++n) {
            if (hitec_pre_step(&run, &T, words, n, n == run.words)) {
                break;
            }
        }
        CHECK_EQ(run.phase, HITEC_PRE_DONE);
        hitec_pre_capture(&run, &T, 0, &cap);
        CHECK(memcmp(&once, &cap, sizeof(cap)) == 0);
        CHECK_EQ(cap.n, 8u);
    }
}

TEST_CASE(the_plans_of_each_transaction)
{
    hitec_xfer_t x;
    hitec_xfer_t tx;
    hitec_pre_run_t r;

    CHECK(hitec_pre_timing(150000000u, &T));
    /* A read: the request on the line, then the sampler. */
    hitec_d_read_xfer(0x00, 0x32, &x);
    CHECK_EQ(hitec_pre_begin(&r, &T, &x), HITEC_PRE_PLAN_SEND);
    CHECK_EQ(r.phase, HITEC_PRE_TX);
    CHECK_EQ(r.window_ns, 25000000u);
    CHECK_EQ(r.words, hitec_pre_words(&T, 25000u, 8u));
    hitec_pre_tx_xfer(&x, &tx);
    CHECK_EQ(tx.window_us, 0u);
    CHECK_EQ(tx.max_rx, 0u);
    CHECK_EQ(tx.n_tx, 5u);
    CHECK_EQ(tx.lead_us, x.lead_us);
    CHECK(hitec_xfer_valid(&tx));
    /* A step before the request has ended changes nothing. */
    CHECK(!hitec_pre_step(&r, &T, words, 0u, true));
    CHECK_EQ(r.phase, HITEC_PRE_TX);
    hitec_pre_tx_done(&r);
    CHECK_EQ(r.phase, HITEC_PRE_SAMPLE);
    hitec_pre_tx_done(&r);
    CHECK_EQ(r.phase, HITEC_PRE_SAMPLE);
    /* A write and a LEVEL: the line alone. */
    hitec_d_write_xfer(0x00, 0x32, 0x1234, &x);
    CHECK_EQ(hitec_pre_begin(&r, &T, &x), HITEC_PRE_PLAN_LINE);
    CHECK_EQ(r.phase, HITEC_PRE_IDLE);
    CHECK(hitec_level_xfer(HITEC_D, 0, 100u, &x));
    CHECK_EQ(hitec_pre_begin(&r, &T, &x), HITEC_PRE_PLAN_LINE);
    /* A listen, with and without an answer. */
    CHECK(hitec_listen_xfer(HITEC_D, 1000u, 1u, false, 0, &x));
    CHECK_EQ(hitec_pre_begin(&r, &T, &x), HITEC_PRE_PLAN_LISTEN);
    CHECK_EQ(r.phase, HITEC_PRE_SAMPLE);
    CHECK(hitec_listen_xfer(HITEC_D, 1000u, 1u, true, 0x2B, &x));
    CHECK_EQ(hitec_pre_begin(&r, &T, &x), HITEC_PRE_PLAN_REFUSE);
    /* Classic, not valid, NULL. */
    hitec_c_xfer(0x61, 0x12, 0x00, &x);
    CHECK_EQ(hitec_pre_begin(&r, &T, &x), HITEC_PRE_PLAN_REFUSE);
    hitec_d_read_xfer(0x00, 0x32, &x);
    x.max_rx = 0;
    CHECK_EQ(hitec_pre_begin(&r, &T, &x), HITEC_PRE_PLAN_REFUSE);
    hitec_d_read_xfer(0x00, 0x32, &x);
    CHECK_EQ(hitec_pre_begin(&r, NULL, &x), HITEC_PRE_PLAN_REFUSE);
    CHECK_EQ(hitec_pre_begin(&r, &T, NULL), HITEC_PRE_PLAN_REFUSE);
    CHECK_EQ(hitec_pre_begin(NULL, &T, &x), HITEC_PRE_PLAN_REFUSE);
}

TEST_CASE(an_abort_in_every_phase_keeps_the_whole_bytes)
{
    hitec_xfer_t x;
    hitec_pre_run_t r;
    uint32_t mid_byte;

    CHECK(hitec_pre_timing(150000000u, &T));
    wire_clear();
    wire_bytes(1.0e6, D_BIT_NS, k_answer, 8u);
    hitec_d_read_xfer(0x00, 0x32, &x);

    /* Idle: nothing happens. */
    memset(&r, 0, sizeof(r));
    hitec_pre_abort(&r, &T, words, 0u);
    CHECK_EQ(r.phase, HITEC_PRE_IDLE);
    CHECK_EQ(r.aborted, 0u);
    hitec_pre_lost(&r);
    CHECK_EQ(r.phase, HITEC_PRE_IDLE);

    /* The request on the line: no byte. */
    CHECK_EQ(hitec_pre_begin(&r, &T, &x), HITEC_PRE_PLAN_SEND);
    hitec_pre_abort(&r, &T, words, 0u);
    CHECK_EQ(r.phase, HITEC_PRE_DONE);
    hitec_pre_capture(&r, &T, 0, &cap);
    CHECK_EQ(cap.aborted, 1u);
    CHECK_EQ(cap.n, 0u);
    CHECK_EQ(cap.overflow, 0u);

    /* Sampling, before the first byte. */
    CHECK_EQ(hitec_pre_begin(&r, &T, &x), HITEC_PRE_PLAN_SEND);
    hitec_pre_tx_done(&r);
    sample_wire(&T, r.words);
    CHECK(!hitec_pre_step(&r, &T, words, 10u, false));
    hitec_pre_abort(&r, &T, words, 20u);
    hitec_pre_capture(&r, &T, 0, &cap);
    CHECK_EQ(cap.aborted, 1u);
    CHECK_EQ(cap.n, 0u);

    /* In the middle of byte 3: 3 bytes whole. */
    mid_byte = (uint32_t)((1.0e6 + 35.0 * D_BIT_NS) / 1086.67) / 32u;
    CHECK_EQ(hitec_pre_begin(&r, &T, &x), HITEC_PRE_PLAN_SEND);
    hitec_pre_tx_done(&r);
    hitec_pre_abort(&r, &T, words, mid_byte);
    hitec_pre_capture(&r, &T, 0, &cap);
    CHECK_EQ(cap.aborted, 1u);
    CHECK_EQ(cap.overflow, 0u);
    CHECK_EQ(cap.n, 3u);
    CHECK_EQ(cap.byte[2].value, k_answer[2]);

    /* After every byte but before the window ends. */
    CHECK_EQ(hitec_pre_begin(&r, &T, &x), HITEC_PRE_PLAN_SEND);
    hitec_pre_tx_done(&r);
    hitec_pre_abort(&r, &T, words, r.words);
    hitec_pre_capture(&r, &T, 0, &cap);
    CHECK_EQ(cap.n, 8u);
    CHECK_EQ(cap.aborted, 1u);
    CHECK_EQ(cap.full, 1u);   /* the 8th ended the decoder before the cut */

    /* Done: an abort and a loss change nothing. */
    {
        const hitec_pre_run_t before = r;

        hitec_pre_abort(&r, &T, words, 0u);
        CHECK(memcmp(&before, &r, sizeof(r)) == 0);
    }
    /* A sampler that stalled or ran past its time. */
    CHECK_EQ(hitec_pre_begin(&r, &T, &x), HITEC_PRE_PLAN_SEND);
    hitec_pre_tx_done(&r);
    (void)hitec_pre_step(&r, &T, words, 40u, false);
    hitec_pre_lost(&r);
    hitec_pre_capture(&r, &T, 0, &cap);
    CHECK_EQ(cap.overflow, 1u);
    CHECK_EQ(cap.aborted, 0u);
    CHECK_EQ(r.phase, HITEC_PRE_DONE);
    /* NULLs. */
    hitec_pre_abort(NULL, &T, words, 0u);
    hitec_pre_lost(NULL);
    hitec_pre_tx_done(NULL);
    CHECK(!hitec_pre_step(NULL, &T, words, 0u, true));
    hitec_pre_capture(NULL, &T, 0, &cap);
    CHECK_EQ(cap.n, 0u);
    CHECK_EQ(cap.family, HITEC_D);
}

TEST_CASE(the_offset_moves_the_times_and_holds_them_at_zero)
{
    CHECK(hitec_pre_timing(150000000u, &T));
    wire_clear();
    wire_bytes(6.0e6, D_BIT_NS, k_answer, 8u);
    listen_run(HITEC_D_WINDOW_US, 8u);
    {
        const uint32_t at0 = cap.byte[0].at_ns;

        hitec_pre_capture(&run, &T, 700000, &cap);
        CHECK_EQ(cap.byte[0].at_ns, at0 + 700000u);
        hitec_pre_capture(&run, &T, -(int32_t)at0 - 5, &cap);
        CHECK_EQ(cap.byte[0].at_ns, 0u);
        CHECK(cap.byte[1].at_ns > 0u);
    }
}

int main(void)
{
    RUN(the_counts_at_150_mhz_are_the_bench_period_and_the_middle_of_the_window);
    RUN(every_clock_the_arithmetic_takes_stays_in_the_measured_window);
    RUN(the_program_precharges_releases_samples_once_and_keeps_the_period);
    RUN(a_seven_byte_answer_decodes_at_7_99_samples_a_bit);
    RUN(the_servo_bit_time_4_percent_short_or_long_decodes);
    RUN(a_start_bit_on_the_first_sample_is_not_taken);
    RUN(a_start_bit_on_the_last_sample_of_the_window_is_taken_whole);
    RUN(a_byte_across_the_window_end_and_the_bytes_behind_it);
    RUN(a_stop_bit_read_high_is_a_framing_error);
    RUN(no_answer_ends_with_the_window_and_decodes_silent);
    RUN(a_line_stuck_high_gives_no_byte_or_one_framing_byte);
    RUN(a_glitch_shorter_than_half_a_bit_is_not_a_start_bit);
    RUN(the_30_ms_window_fits_and_the_next_microsecond_does_not);
    RUN(a_sampler_that_stops_short_keeps_the_whole_bytes_and_overflows);
    RUN(words_fed_one_at_a_time_decode_as_all_at_once);
    RUN(the_plans_of_each_transaction);
    RUN(an_abort_in_every_phase_keeps_the_whole_bytes);
    RUN(the_offset_moves_the_times_and_holds_them_at_zero);
    return test_summary("hitec_pre");
}
