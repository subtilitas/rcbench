/*
 * The phase tap's PIO (programmable input/output) program, run on the host
 * (firmware/iomcu/src/tone_cap.pio).
 *
 * The program is assembled here from its source by a small assembler for
 * the instructions it uses, and run by a cycle-counting model of the state
 * machine: JMP on X--, Y--, the pin; IN from X, Y, null; SET; MOV to X, Y
 * and the ISR from null and the OSR; PUSH noblock into a 4-word FIFO that a
 * DMA channel drains.  The pin is read two cycles after it moves, as the
 * input synchroniser delays it.  The assembled words are held to
 * fixtures/tone_cap_words.txt, which the firmware build holds pioasm's
 * output to: the model runs what the chip runs.
 *
 * Under test:
 *  - the count: every decrement of X lies exactly 4 cycles after the one
 *    before, on every path, over streams of every shape, so X keeps time
 *    whatever the line does and over its wrap;
 *  - the words equal those of tone_holdoff_edge() (shared/sense/tone.c)
 *    fed with the edges the program sampled, at the ticks it stamped,
 *    including the lows at and beside the hold-off and edges that arrive
 *    during the push and swallow paths;
 *  - a carrier of 8 to 144 kHz at 10, 50 and 90 % duty, with jitter, comes
 *    out as the rule says: the hold-off swallows every carrier low above
 *    125 kHz and keeps the tone's own;
 *  - against the rule on ideal ticks (the edge's cycle over 4), no stamp is
 *    more than 3 ticks away, and a low of 4 ticks more or less than the
 *    hold-off is decided as the rule decides it;
 *  - FIFO overflow: a stalled DMA drops words and RXSTALL is set; a DMA
 *    that keeps up drops none at 247,000 edges/s;
 *  - the start: a line low gives nothing, a line high at the start a rise.
 *
 * Not tested here, because the host has no PIO: that the chip's input
 * synchroniser, JMP PIN, delay and DMA handshakes behave as this model
 * says (the 2-cycle input delay is the datasheet's); the pad, the
 * zener and the edges a real ESC makes.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "edge_ring.h"
#include "tone.h"
#include "tone_svc.h"

#define CPU_HZ   150000000ull
#define TICK     TONE_SVC_TICK_HZ
#define HOLD     300u                 /* 8 us in ticks */
#define M31      0x7FFFFFFFu
#define SYNC     2u                   /* the input synchroniser, cycles */
#define MAX_PROG 32u

/* ---------------------------------------------------------- the assembler */

typedef struct {
    char     name[24];
    unsigned addr;
} label_t;

static uint16_t prog[MAX_PROG];
static size_t   prog_len;
static unsigned low_addr;

static bool is_cond(const char *t)
{
    return !strcmp(t, "x--") || !strcmp(t, "y--") || !strcmp(t, "pin")
           || !strcmp(t, "!x") || !strcmp(t, "!y") || !strcmp(t, "x!=y")
           || !strcmp(t, "!osre");
}

static unsigned cond_code(const char *t)
{
    if (!strcmp(t, "!x")) { return 1; }
    if (!strcmp(t, "x--")) { return 2; }
    if (!strcmp(t, "!y")) { return 3; }
    if (!strcmp(t, "y--")) { return 4; }
    if (!strcmp(t, "x!=y")) { return 5; }
    if (!strcmp(t, "pin")) { return 6; }
    return 7;
}

static unsigned src_code(const char *t)
{
    if (!strcmp(t, "pins")) { return 0; }
    if (!strcmp(t, "x")) { return 1; }
    if (!strcmp(t, "y")) { return 2; }
    if (!strcmp(t, "null")) { return 3; }
    if (!strcmp(t, "isr")) { return 6; }
    if (!strcmp(t, "osr")) { return 7; }
    return 99;
}

static unsigned mov_dst(const char *t)
{
    if (!strcmp(t, "x")) { return 1; }
    if (!strcmp(t, "y")) { return 2; }
    if (!strcmp(t, "isr")) { return 6; }
    if (!strcmp(t, "osr")) { return 7; }
    return 99;
}

/* Split @p line into tokens at spaces and commas; a "[n]" is the delay. */
static size_t tokens(char *line, char *tok[8], unsigned *delay)
{
    size_t n = 0;
    *delay = 0;
    for (char *p = strtok(line, " \t,\r\n"); p != NULL && n < 8;
         p = strtok(NULL, " \t,\r\n")) {
        if (p[0] == '[') {
            *delay = (unsigned)atoi(p + 1);
        } else {
            tok[n++] = p;
        }
    }
    return n;
}

static bool assemble(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return false;
    }
    static char text[200][160];
    size_t lines = 0;
    bool in_prog = false;
    char buf[256];
    while (fgets(buf, sizeof(buf), f) != NULL && lines < 200u) {
        char *c = strchr(buf, ';');
        if (c != NULL) {
            *c = '\0';
        }
        char *s = buf;
        while (*s == ' ' || *s == '\t') {
            ++s;
        }
        if (s[0] == '%') {
            break;
        }
        if (!strncmp(s, ".program", 8)) {
            in_prog = true;
            continue;
        }
        if (!in_prog || s[0] == '\0' || s[0] == '\n' || s[0] == '\r') {
            continue;
        }
        strncpy(text[lines], s, sizeof(text[0]) - 1u);
        text[lines][sizeof(text[0]) - 1u] = '\0';
        ++lines;
    }
    fclose(f);

    /* Pass 1: labels and instruction addresses. */
    label_t labels[32];
    size_t nl = 0;
    unsigned addr = 0;
    for (size_t i = 0; i < lines; ++i) {
        char tmp[160];
        strcpy(tmp, text[i]);
        char *tok[8];
        unsigned d;
        size_t n = tokens(tmp, tok, &d);
        size_t k = 0;
        if (n > 0 && !strcmp(tok[0], "public")) {
            k = 1;
        }
        if (n > k && tok[k][strlen(tok[k]) - 1u] == ':') {
            tok[k][strlen(tok[k]) - 1u] = '\0';
            strcpy(labels[nl].name, tok[k]);
            labels[nl].addr = addr;
            if (!strcmp(tok[k], "low")) {
                low_addr = addr;
            }
            ++nl;
            ++k;
        }
        if (n > k) {
            ++addr;
        }
    }

    /* Pass 2: encode. */
    prog_len = 0;
    for (size_t i = 0; i < lines; ++i) {
        char tmp[160];
        strcpy(tmp, text[i]);
        char *tok[8];
        unsigned d;
        size_t n = tokens(tmp, tok, &d);
        size_t k = 0;
        if (n > 0 && !strcmp(tok[0], "public")) {
            k = 1;
        }
        if (n > k && tok[k][strlen(tok[k]) - 1u] == ':') {
            ++k;
        }
        if (n <= k) {
            continue;
        }
        const char *m = tok[k];
        uint16_t w = 0;
        if (!strcmp(m, "jmp")) {
            unsigned cond = 0;
            const char *lab = tok[k + 1u];
            if (is_cond(tok[k + 1u])) {
                cond = cond_code(tok[k + 1u]);
                lab = tok[k + 2u];
            }
            unsigned target = 99;
            for (size_t j = 0; j < nl; ++j) {
                if (!strcmp(labels[j].name, lab)) {
                    target = labels[j].addr;
                }
            }
            w = (uint16_t)(0x0000u | (d << 8) | (cond << 5) | target);
        } else if (!strcmp(m, "in")) {
            const unsigned cnt = (unsigned)atoi(tok[k + 2u]);
            w = (uint16_t)(0x4000u | (d << 8) | (src_code(tok[k + 1u]) << 5)
                           | (cnt & 31u));
        } else if (!strcmp(m, "push")) {
            const bool block = (n > k + 1u && !strcmp(tok[k + 1u], "block"));
            w = (uint16_t)(0x8000u | (d << 8) | (block ? 0x20u : 0u));
        } else if (!strcmp(m, "mov")) {
            w = (uint16_t)(0xA000u | (d << 8) | (mov_dst(tok[k + 1u]) << 5)
                           | src_code(tok[k + 2u]));
        } else if (!strcmp(m, "nop")) {
            w = (uint16_t)(0xA042u | (d << 8));
        } else if (!strcmp(m, "set")) {
            const unsigned dest = !strcmp(tok[k + 1u], "x") ? 1u : 2u;
            w = (uint16_t)(0xE000u | (d << 8) | (dest << 5)
                           | ((unsigned)atoi(tok[k + 2u]) & 31u));
        } else {
            return false;
        }
        prog[prog_len++] = w;
    }
    return true;
}

/* ------------------------------------------------------------- the model */

typedef struct { uint64_t t; bool level; } raw_t;       /* t in cycles */
typedef struct { uint64_t cyc; bool level; uint32_t x; } sample_t;
typedef struct { uint64_t ready; uint32_t word; } fifo_t;

#define MAX_LOG 600000u

/* Only the samples where the level changed are kept: the oracle needs no
 * more, and a run is millions of samples. */
static sample_t samples[MAX_LOG];
static size_t   nsamples;
static uint64_t nxdec;          /* decrements of X                      */
static uint64_t bad_gap;        /* ... not 4 cycles after the one before */
static uint64_t first_bad;
static uint32_t words[MAX_LOG];
static size_t   nwords;
static unsigned rxstall;
static uint64_t cycles_run;
static uint32_t x_end;

typedef struct {
    uint32_t x0;
    uint32_t hold;          /* ticks; the OSR holds it less 2 */
    unsigned dma_lat;       /* cycles from push to the ring; 0: never */
    bool     line_high_at_start;
} run_cfg_t;

static void run_model(const raw_t *e, size_t ne, uint64_t total,
                      const run_cfg_t *rc)
{
    nsamples = nwords = 0;
    nxdec = bad_gap = first_bad = 0;
    uint64_t last_dec = 0;
    bool last_level = false;
    rxstall = 0;
    uint32_t x = rc->x0, y = 0, isr = 0, osr = rc->hold - 2u;
    unsigned pc = low_addr;
    fifo_t fifo[4];
    unsigned nf = 0;
    size_t ei = 0;
    bool pin = rc->line_high_at_start;
    uint64_t cyc = 0;
    /* Pin edges are at cycles; the pin read at cycle c is the level at c - 2. */
    while (cyc < total) {
        /* The DMA moves what is ready. */
        while (nf > 0u && rc->dma_lat != 0u && fifo[0].ready <= cyc) {
            if (nwords < MAX_LOG) {
                words[nwords++] = fifo[0].word;
            }
            memmove(&fifo[0], &fifo[1], (nf - 1u) * sizeof(fifo[0]));
            --nf;
        }
        const uint16_t w = prog[pc];
        const unsigned delay = (w >> 8) & 31u;
        unsigned next = pc + 1u;
        if (next >= prog_len) {
            next = 0;
        }
        /* The pin as the instruction at cyc sees it. */
        while (ei < ne && e[ei].t + SYNC <= cyc) {
            pin = e[ei].level;
            ++ei;
        }
        switch (w >> 13) {
        case 0: {                                   /* JMP */
            const unsigned cond = (w >> 5) & 7u;
            bool take = false;
            switch (cond) {
            case 0: take = true; break;
            case 1: take = (x == 0u); break;
            case 2:
                take = (x != 0u);
                --x;
                if (nxdec != 0u && cyc - last_dec != 4u) {
                    if (bad_gap++ == 0u) { first_bad = cyc; }
                }
                last_dec = cyc;
                ++nxdec;
                break;
            case 3: take = (y == 0u); break;
            case 4: take = (y != 0u); --y; break;
            case 5: take = (x != y); break;
            case 6:
                take = pin;
                if (pin != last_level && nsamples < MAX_LOG) {
                    last_level = pin;
                    samples[nsamples].cyc = cyc;
                    samples[nsamples].level = pin;
                    samples[nsamples].x = x;
                    ++nsamples;
                }
                break;
            default: take = (osr != 0u); break;
            }
            if (take) {
                next = w & 31u;
            }
            break;
        }
        case 2: {                                   /* IN */
            unsigned cnt = w & 31u;
            if (cnt == 0u) { cnt = 32u; }
            const unsigned src = (w >> 5) & 7u;
            const uint32_t v = (src == 1u) ? x : (src == 2u) ? y : 0u;
            const uint32_t mask = (cnt == 32u) ? 0xFFFFFFFFu
                                               : ((1u << cnt) - 1u);
            isr = (cnt == 32u) ? v : ((isr << cnt) | (v & mask));
            break;
        }
        case 4:                                     /* PUSH noblock */
            if (nf < 4u) {
                fifo[nf].ready = cyc + rc->dma_lat;
                fifo[nf].word = isr;
                ++nf;
            } else {
                ++rxstall;
            }
            isr = 0;
            break;
        case 5: {                                   /* MOV */
            const unsigned dst = (w >> 5) & 7u;
            const unsigned src = w & 7u;
            const uint32_t v = (src == 1u) ? x : (src == 2u) ? y
                               : (src == 7u) ? osr : (src == 6u) ? isr : 0u;
            if (dst == 1u) { x = v; }
            else if (dst == 2u) { y = v; }
            else if (dst == 6u) { isr = v; }
            else if (dst == 7u) { osr = v; }
            break;
        }
        case 7:                                     /* SET */
            if (((w >> 5) & 7u) == 2u) { y = w & 31u; }
            else if (((w >> 5) & 7u) == 1u) { x = w & 31u; }
            break;
        default:
            break;
        }
        cyc += 1u + delay;
        pc = next;
    }
    cycles_run = cyc;
    x_end = x;
}

/* ----------------------------------------------------------- the streams */

#define MAX_RAW 400000u
static raw_t raw[MAX_RAW];
static size_t nraw;

static void edge(uint64_t t, bool level)
{
    if (nraw < MAX_RAW) {
        raw[nraw].t = t;
        raw[nraw].level = level;
        ++nraw;
    }
}

static uint64_t us(double v)
{
    return (uint64_t)(v * 150.0 + 0.5);
}

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 16);
}

/* A beep: @p bursts bursts at @p tone_hz, each @p on_us long, chopped by a
 * carrier of @p car_khz at @p duty percent (0: not chopped), from @p t0
 * cycles.  Every edge gets up to @p jit cycles of jitter, kept in order. */
static uint64_t beep(uint64_t t0, unsigned bursts, double tone_hz,
                     double on_us, double car_khz, unsigned duty,
                     unsigned jit)
{
    uint64_t last = t0;
    for (unsigned b = 0; b < bursts; ++b) {
        const uint64_t s = t0 + (uint64_t)((double)b * (double)CPU_HZ
                                           / tone_hz);
        if (car_khz == 0.0) {
            uint64_t a = s + (jit ? rnd() % (jit + 1u) : 0u);
            uint64_t z = s + us(on_us) + (jit ? rnd() % (jit + 1u) : 0u);
            if (a <= last) { a = last + 1u; }
            if (z <= a) { z = a + 1u; }
            edge(a, true);
            edge(z, false);
            last = z;
        } else {
            const double c = 1000.0 / car_khz;           /* us */
            for (double k = 0; k + c <= on_us + 1e-9; k += c) {
                uint64_t a = s + us(k) + (jit ? rnd() % (jit + 1u) : 0u);
                uint64_t z = s + us(k + c * (double)duty / 100.0)
                             + (jit ? rnd() % (jit + 1u) : 0u);
                if (a <= last) { a = last + 1u; }
                if (z <= a) { z = a + 1u; }
                edge(a, true);
                edge(z, false);
                last = z;
            }
        }
    }
    return last;
}

/* ------------------------------------------------------------- the oracle */

#define MAX_EXP 200000u
static uint32_t expected[MAX_EXP];
static size_t   nexp;

/* The words tone_holdoff_edge() gives for the edges the program sampled, at
 * the ticks it stamped them. */
static void oracle(uint32_t x0, uint32_t hold)
{
    tone_holdoff_t h;
    tone_holdoff_init(&h, TICK, 0u);
    h.hold = hold;
    bool last = false;
    nexp = 0;
    const uint64_t now = (uint64_t)((x0 - x_end) & M31);
    for (size_t i = 0; i < nsamples; ++i) {
        const uint64_t t = (uint64_t)((x0 - samples[i].x) & M31);
        if (samples[i].level != last) {
            last = samples[i].level;
            tone_edge_t o[2];
            const size_t n = tone_holdoff_edge(&h, t, last, o);
            for (size_t k = 0; k < n && nexp < MAX_EXP; ++k) {
                expected[nexp++] = edge_word(
                    (x0 - (uint32_t)o[k].t) & M31, o[k].level);
            }
        }
    }
    tone_edge_t o[1];
    if (tone_holdoff_advance(&h, now, o) == 1u && nexp < MAX_EXP) {
        expected[nexp++] = edge_word((x0 - (uint32_t)o[0].t) & M31,
                                     o[0].level);
    }
}

static bool same_words(void)
{
    if (nexp != nwords) {
        printf("      words: expected %zu, got %zu\n", nexp, nwords);
        return false;
    }
    for (size_t i = 0; i < nwords; ++i) {
        if (expected[i] != words[i]) {
            printf("      word %zu: expected %08x, got %08x\n", i,
                   expected[i], words[i]);
            return false;
        }
    }
    return true;
}

/* Every decrement 4 cycles after the one before. */
static bool decrements_even(void)
{
    if (bad_gap != 0u) {
        printf("      %llu decrements not 4 cycles after the one before, "
               "the first at cycle %llu\n", (unsigned long long)bad_gap,
               (unsigned long long)first_bad);
        return false;
    }
    return true;
}

static run_cfg_t cfg_default(void)
{
    run_cfg_t c = { .x0 = 0u, .hold = HOLD, .dma_lat = 12u,
                    .line_high_at_start = false };
    return c;
}

/* Run the stream in raw[] and hold it to both checks. */
static bool run_and_check(uint64_t total, const run_cfg_t *rc)
{
    run_model(raw, nraw, total, rc);
    oracle(rc->x0, rc->hold);
    const bool ok = decrements_even() && same_words();
    /* And the count has not drifted from the clock: one decrement a 4
     * cycles, over the whole run. */
    const uint64_t expect = cycles_run / 4u;
    if (nxdec + 1u < expect || nxdec > expect + 1u) {
        printf("      %llu decrements in %llu cycles\n",
               (unsigned long long)nxdec, (unsigned long long)cycles_run);
        return false;
    }
    return ok;
}

/* ------------------------------------------------------------------ cases */

TEST_CASE(the_program_assembles_to_the_words_pioasm_gives)
{
    CHECK(assemble(TONE_CAP_PIO));
    FILE *f = fopen(TONE_CAP_WORDS, "r");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    unsigned v;
    size_t n = 0;
    while (fscanf(f, "%x", &v) == 1) {
        CHECK(n < prog_len);
        if (n < prog_len) {
            CHECK_EQ(prog[n], (uint16_t)v);
        }
        ++n;
    }
    fclose(f);
    CHECK_EQ(n, prog_len);
    CHECK_EQ(prog_len, 22u);
    CHECK(prog_len <= 32u);
    CHECK_EQ(low_addr, 16u);
}

TEST_CASE(a_line_that_never_moves_gives_no_word_and_x_keeps_time)
{
    CHECK(assemble(TONE_CAP_PIO));
    nraw = 0;
    run_cfg_t c = cfg_default();
    CHECK(run_and_check(2000000u, &c));
    CHECK_EQ(nwords, 0u);
    CHECK(nxdec >= 499990u);
}

TEST_CASE(x_counts_through_its_wrap_and_the_words_stay_right)
{
    CHECK(assemble(TONE_CAP_PIO));
    /* X starts 100 ticks from 0: the first decrement into 0xFFFFFFFF
     * is not a jump, and nothing may care. */
    nraw = 0;
    edge(us(3.0), true);
    edge(us(20.0), false);
    edge(us(60.0), true);
    edge(us(70.0), false);
    edge(us(200.0), true);
    run_cfg_t c = cfg_default();
    c.x0 = 100u;
    CHECK(run_and_check(us(400.0), &c));
    /* Three rises and two falls: the 10 us pulse at 60 us follows a 40 us
     * low and leaves a 130 us one, both longer than 8 us. */
    CHECK_EQ(nwords, 5u);
}

TEST_CASE(the_start_is_a_low_line_and_a_line_already_high_is_a_rise)
{
    CHECK(assemble(TONE_CAP_PIO));
    nraw = 0;
    run_cfg_t c = cfg_default();
    c.line_high_at_start = true;
    edge(0, true);
    edge(us(50.0), false);
    CHECK(run_and_check(us(100.0), &c));
    CHECK_EQ(nwords, 2u);
    CHECK(edge_word_level(words[0]));
    CHECK(!edge_word_level(words[1]));
    /* The rise stamp is the first sample: one or two ticks in. */
    CHECK(((0u - edge_word_count(words[0])) & M31) <= 3u);
}

TEST_CASE(a_low_shorter_than_the_hold_off_is_swallowed_and_a_longer_one_kept)
{
    CHECK(assemble(TONE_CAP_PIO));
    run_cfg_t c = cfg_default();
    /* A pulse, then lows of 4 ticks (16 cycles) less and more than the
     * hold-off, 300 ticks = 1200 cycles, then a pulse. */
    for (unsigned dl = 0; dl < 2u; ++dl) {
        nraw = 0;
        const uint64_t low = (dl == 0u) ? 1184u : 1216u;
        edge(1000, true);
        edge(2000, false);
        edge(2000 + low, true);
        edge(4000 + low, false);
        edge(4000 + low + 20000u, true);
        CHECK(run_and_check(60000u, &c));
        /* dl 0: the low is swallowed and the two pulses are one: a rise,
         * the fall after both, a rise; dl 1: kept, two pulses. */
        CHECK_EQ(nwords, dl == 0u ? 3u : 5u);
    }
}

/* The hold-off's threshold, found by scanning the low's length one cycle at
 * a time over the four phases of the 4-cycle grid: a low of the rule's 300
 * ticks (1200 cycles, 8 us) is always kept and one of 1196 cycles never. */
TEST_CASE(the_hold_off_threshold_is_the_rules_to_within_a_tick)
{
    CHECK(assemble(TONE_CAP_PIO));
    run_cfg_t c = cfg_default();
    for (unsigned phase = 0; phase < 4u; ++phase) {
        unsigned first_kept = 0;
        for (unsigned low = 1180u; low <= 1220u; ++low) {
            nraw = 0;
            edge(1000u + phase, true);
            edge(3000u + phase, false);
            edge(3000u + phase + low, true);
            edge(6000u + phase + low, false);
            edge(40000u, true);
            CHECK(run_and_check(80000u, &c));
            const bool kept = (nwords == 5u);
            if (kept && first_kept == 0u) {
                first_kept = low;
            }
            if (!kept && first_kept != 0u) {
                T_FAIL("phase %u: low %u swallowed after %u was kept", phase,
                       low, first_kept);
            }
        }
        /* Kept from 300 ticks (1200 cycles) down to 3 cycles less, by
         * where the fall and the rise sit on the sampling grid: 1200,
         * 1199, 1198 and 1197 for the four phases. */
        if (first_kept != 1200u - phase) {
            T_FAIL("phase %u: first low kept is %u cycles", phase,
                   first_kept);
        }
    }
}

TEST_CASE(edges_in_the_push_and_swallow_paths_are_seen_by_the_next_sample)
{
    CHECK(assemble(TONE_CAP_PIO));
    run_cfg_t c = cfg_default();
    /* A fall at every cycle from just before a rise's push to well after
     * it, and a rise at every cycle through the hold-off's last samples:
     * the words are the rule's on the samples taken, and X never skips. */
    for (unsigned d = 0; d < 40u; ++d) {
        for (unsigned phase = 0; phase < 4u; ++phase) {
            nraw = 0;
            edge(2000u + phase, true);
            edge(2000u + phase + d, false);
            edge(2000u + phase + d + 20u, true);
            edge(2000u + phase + d + 30u, false);
            edge(2000u + phase + d + 1190u + (d % 4u), true);
            edge(2000u + phase + d + 1500u, false);
            CHECK(run_and_check(20000u, &c));
        }
    }
}

TEST_CASE(a_carrier_comes_out_as_the_hold_off_rule_says)
{
    CHECK(assemble(TONE_CAP_PIO));
    run_cfg_t c = cfg_default();
    static const double car[] = { 8.0, 12.0, 16.0, 24.0, 32.0, 48.0, 62.5,
                                  100.0, 125.0, 144.0 };
    static const unsigned duty[] = { 10u, 50u, 90u };
    for (size_t i = 0; i < sizeof(car) / sizeof(car[0]); ++i) {
        for (size_t j = 0; j < 3u; ++j) {
            for (unsigned jit = 0; jit <= 6u; jit += 6u) {
                nraw = 0;
                /* 20 bursts of 2.5 kHz, 120 us long, chopped. */
                const uint64_t end = beep(1500u, 20u, 2500.0, 120.0, car[i],
                                          duty[j], jit);
                if (!run_and_check(end + 6000u, &c)) {
                    T_FAIL("carrier %.1f kHz, duty %u, jitter %u", car[i],
                           duty[j], jit);
                }
            }
        }
    }
}

TEST_CASE(a_144_khz_carrier_is_two_words_a_burst)
{
    CHECK(assemble(TONE_CAP_PIO));
    run_cfg_t c = cfg_default();
    for (unsigned duty = 10; duty <= 90u; duty += 40u) {
        nraw = 0;
        const uint64_t end = beep(1500u, 30u, 3000.0, 100.0, 144.0, duty, 3u);
        CHECK(run_and_check(end + 6000u, &c));
        CHECK_EQ(nwords, 60u);
    }
}

TEST_CASE(an_unchopped_tone_is_two_words_a_burst_at_the_edges_ticks)
{
    CHECK(assemble(TONE_CAP_PIO));
    run_cfg_t c = cfg_default();
    nraw = 0;
    const uint64_t end = beep(1500u, 50u, 1000.0, 200.0, 0.0, 0u, 0u);
    CHECK(run_and_check(end + 6000u, &c));
    CHECK_EQ(nwords, 100u);
    /* Against ideal ticks, the edge's cycle over 4: within 3 ticks. */
    for (size_t i = 0; i < nwords; ++i) {
        const int64_t tick = (int64_t)((0u - edge_word_count(words[i])) & M31);
        const int64_t ideal = (int64_t)(raw[i].t / 4u);
        if (tick - ideal < -1 || tick - ideal > 3) {
            T_FAIL("word %zu: tick %lld, ideal %lld", i, (long long)tick,
                   (long long)ideal);
        }
        CHECK_EQ(edge_word_level(words[i]), raw[i].level);
    }
    /* A beep of 1 kHz is 37500 ticks a period: 50 periods hold the rate to
     * a tick in 1.9 million. */
    const int64_t span = (int64_t)(((0u - edge_word_count(words[98])) & M31))
                         - (int64_t)(((0u - edge_word_count(words[0])) & M31));
    CHECK_EQ(span, 49 * 37500);
}

TEST_CASE(random_streams_agree_with_the_rule_whatever_the_edges_do)
{
    CHECK(assemble(TONE_CAP_PIO));
    run_cfg_t c = cfg_default();
    for (unsigned round = 0; round < 40u; ++round) {
        nraw = 0;
        uint64_t t = 500u + rnd() % 50u;
        bool level = true;
        for (unsigned i = 0; i < 4000u; ++i) {
            edge(t, level);
            level = !level;
            switch (rnd() % 6u) {
            case 0: t += 1u + rnd() % 12u; break;          /* glitch */
            case 1: t += 1170u + rnd() % 60u; break;       /* the hold-off */
            case 2: t += 40u + rnd() % 400u; break;
            case 3: t += 1100u + rnd() % 300u; break;
            case 4: t += 5u + rnd() % 50u; break;
            default: t += 1u + rnd() % 3000u; break;
            }
        }
        c.x0 = (round % 3u == 0u) ? 40u : 0u;
        if (!run_and_check(t + 20000u, &c)) {
            T_FAIL("round %u", round);
        }
    }
}

TEST_CASE(a_dma_that_keeps_up_drops_no_word_at_247000_edges_a_second)
{
    CHECK(assemble(TONE_CAP_PIO));
    run_cfg_t c = cfg_default();
    /* Pulses and lows of 4 us: the lows are under the hold-off, so use
     * lows of 8.1 us, the fastest the program reports: 2 words per
     * 8.1 us + 0.1 us pulse. */
    nraw = 0;
    uint64_t t = 1000u;
    for (unsigned i = 0; i < 20000u; ++i) {
        edge(t, true);
        edge(t + 15u, false);
        t += 15u + 1215u;
    }
    CHECK(run_and_check(t + 20000u, &c));
    CHECK_EQ(rxstall, 0u);
    CHECK(nwords >= 39990u);
}

TEST_CASE(a_dma_that_stalls_drops_words_and_sets_rxstall)
{
    CHECK(assemble(TONE_CAP_PIO));
    run_cfg_t c = cfg_default();
    c.dma_lat = 0u;                       /* never drains */
    nraw = 0;
    for (unsigned i = 0; i < 12u; ++i) {
        edge(2000u + i * 3000u, true);
        edge(2000u + i * 3000u + 1500u, false);
    }
    run_model(raw, nraw, 60000u, &c);
    CHECK(rxstall > 0u);
    CHECK_EQ(nwords, 0u);
    /* The count does not care. */
    CHECK(decrements_even());
}

int main(void)
{
    RUN(the_program_assembles_to_the_words_pioasm_gives);
    RUN(a_line_that_never_moves_gives_no_word_and_x_keeps_time);
    RUN(x_counts_through_its_wrap_and_the_words_stay_right);
    RUN(the_start_is_a_low_line_and_a_line_already_high_is_a_rise);
    RUN(a_low_shorter_than_the_hold_off_is_swallowed_and_a_longer_one_kept);
    RUN(the_hold_off_threshold_is_the_rules_to_within_a_tick);
    RUN(edges_in_the_push_and_swallow_paths_are_seen_by_the_next_sample);
    RUN(a_carrier_comes_out_as_the_hold_off_rule_says);
    RUN(a_144_khz_carrier_is_two_words_a_burst);
    RUN(an_unchopped_tone_is_two_words_a_burst_at_the_edges_ticks);
    RUN(random_streams_agree_with_the_rule_whatever_the_edges_do);
    RUN(a_dma_that_keeps_up_drops_no_word_at_247000_edges_a_second);
    RUN(a_dma_that_stalls_drops_words_and_sets_rxstall);
    return test_summary("tone_pio");
}
