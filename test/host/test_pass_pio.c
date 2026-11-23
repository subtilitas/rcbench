/*
 * The pass-through's PIO (programmable input/output) programs, run on the
 * host (protocols/passthru/rp2350/pass.pio).
 *
 * The programs are assembled here from their source by a small assembler
 * for the instructions they use, and run by a cycle-counting model of two
 * state machines in one block at GPIO base 16: MOV between the pins, X, Y,
 * the ISR and null (inverted as well); JMP always and on X != Y; PUSH
 * noblock into a 4-word FIFO that a DMA (direct memory access) model
 * drains, stamping each state with the microsecond timer.  A pin is read
 * two cycles after it moves, as the input synchroniser delays it; a
 * written output reaches the pad one cycle after the instruction.  The
 * assembled words are held to fixtures/pass_mirror_words.txt and
 * fixtures/pass_capture_words.txt, which the firmware build holds pioasm's
 * output to: the model runs what the chip runs.
 *
 * Under test:
 *  - MIRROR: every edge of every input reaches its output 3 cycles later
 *    (20ns at 150MHz), widths kept to the cycle, 1-cycle pulses included;
 *  - CAPTURE: the first state pushed is the levels at the start; every
 *    change after it pushed once, 2 to 8 cycles after the edge; two
 *    states in a row never equal; edges closer than the push path end in
 *    the right state; a stalled DMA drops states and sets RXSTALL, one
 *    that keeps up drops none at 227,680 events/s;
 *  - both together, through the ring and the decoder: 9 receiver signals
 *    at 560Hz and at 40Hz, each with its own width from 400us to 2500us,
 *    come out as IN_US and OUT_US within 1us of the widths sent, with the
 *    frame period, no missed frame and no glitch.
 *
 * Not tested here, because the host has no PIO: that the chip's
 * synchroniser, pad and DMA chaining behave as this model says, and the
 * delay of the carrier's input buffer and output resistor.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "pass_decode.h"
#include "pass_ring.h"

#define SYNC     2u          /* the input synchroniser, cycles */
#define CPU_MHZ  150u
#define MAX_PROG 32u
#define PINS     18u
#define PIN_ALL  0x3FFFFu
#define IN_BITS  0x2AAAAu    /* odd bits: the inputs */
#define OUT_BITS 0x15555u    /* even bits: the outputs */

/* ---------------------------------------------------------- the assembler */

typedef struct {
    uint16_t w[MAX_PROG];
    unsigned len;
    unsigned wrap_target;
    unsigned wrap;
    unsigned sample;          /* the public label `sample`, if any */
} prog_t;

static unsigned mov_code(const char *t, bool *inv)
{
    *inv = false;
    if (t[0] == '~' || t[0] == '!') {
        *inv = true;
        ++t;
    }
    if (!strcmp(t, "pins")) { return 0; }
    if (!strcmp(t, "x")) { return 1; }
    if (!strcmp(t, "y")) { return 2; }
    if (!strcmp(t, "null")) { return 3; }
    if (!strcmp(t, "isr")) { return 6; }
    if (!strcmp(t, "osr")) { return 7; }
    return 99;
}

/* Split @p line at spaces and commas into at most 6 tokens. */
static size_t split(char *line, char *tok[6])
{
    static char none[] = "";
    for (size_t i = 0; i < 6u; ++i) {
        tok[i] = none;
    }
    size_t n = 0;
    for (char *s = strtok(line, " \t,\r\n"); s != NULL && n < 6u;
         s = strtok(NULL, " \t,\r\n")) {
        tok[n++] = s;
    }
    return n;
}

/* The program @p name of @p path. */
static bool assemble(const char *path, const char *name, prog_t *p)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return false;
    }
    static char text[64][160];
    size_t lines = 0;
    bool in = false;
    char buf[160];
    while (fgets(buf, sizeof(buf), f) != NULL && lines < 64u) {
        char *c = strchr(buf, ';');
        if (c != NULL) {
            *c = '\0';
        }
        char *s = buf;
        while (*s == ' ' || *s == '\t') {
            ++s;
        }
        if (!strncmp(s, ".program", 8)) {
            char tmp[160];
            snprintf(tmp, sizeof(tmp), "%s", s + 8);
            char *t[6];
            split(tmp, t);
            in = !strcmp(t[0], name);
            continue;
        }
        if (in && s[0] != '\0' && s[0] != '\n' && s[0] != '\r') {
            snprintf(text[lines++], sizeof(text[0]), "%s", s);
        }
    }
    fclose(f);

    struct { char n[24]; unsigned a; } lab[16] = {0};
    size_t nl = 0;
    memset(p, 0, sizeof(*p));
    /* Pass 1: labels, addresses and the wrap. */
    unsigned addr = 0;
    for (size_t i = 0; i < lines; ++i) {
        char tmp[160];
        snprintf(tmp, sizeof(tmp), "%s", text[i]);
        char *t[6];
        const size_t n = split(tmp, t);
        size_t k = (n > 0u && !strcmp(t[0], "public")) ? 1u : 0u;
        if (n > k && !strcmp(t[k], ".wrap_target")) {
            p->wrap_target = addr;
            continue;
        }
        if (n > k && !strcmp(t[k], ".wrap")) {
            p->wrap = addr - 1u;
            continue;
        }
        if (n > k && t[k][strlen(t[k]) - 1u] == ':') {
            t[k][strlen(t[k]) - 1u] = '\0';
            snprintf(lab[nl].n, sizeof(lab[nl].n), "%s", t[k]);
            lab[nl++].a = addr;
            if (!strcmp(t[k], "sample")) {
                p->sample = addr;
            }
            ++k;
        }
        if (n > k) {
            ++addr;
        }
    }
    /* Pass 2: the words. */
    for (size_t i = 0; i < lines; ++i) {
        char tmp[160];
        snprintf(tmp, sizeof(tmp), "%s", text[i]);
        char *t[6];
        const size_t n = split(tmp, t);
        size_t k = (n > 0u && !strcmp(t[0], "public")) ? 1u : 0u;
        if (n > k && t[k][0] == '.') {
            continue;
        }
        if (n > k && t[k][strlen(t[k]) - 1u] == ':') {
            ++k;
        }
        if (n <= k) {
            continue;
        }
        uint16_t w;
        if (!strcmp(t[k], "mov")) {
            bool inv_d, inv_s;
            const unsigned dst = mov_code(t[k + 1u], &inv_d);
            const unsigned src = mov_code(t[k + 2u], &inv_s);
            if (dst > 7u || src > 7u || inv_d) {
                return false;
            }
            w = (uint16_t)(0xA000u | (dst << 5) | (inv_s ? 0x08u : 0u) | src);
        } else if (!strcmp(t[k], "jmp")) {
            unsigned cond = 0;
            const char *target = t[k + 1u];
            if (!strcmp(t[k + 1u], "x!=y")) {
                cond = 5;
                target = t[k + 2u];
            }
            unsigned a = 99;
            for (size_t j = 0; j < nl; ++j) {
                if (!strcmp(lab[j].n, target)) {
                    a = lab[j].a;
                }
            }
            if (a > 31u) {
                return false;
            }
            w = (uint16_t)((cond << 5) | a);
        } else if (!strcmp(t[k], "push")) {
            const bool block = !strcmp(t[k + 1u], "block");
            w = (uint16_t)(0x8000u | (block ? 0x20u : 0u));
        } else {
            return false;
        }
        p->w[p->len++] = w;
    }
    return p->len > 0u;
}

static bool same_as_fixture(const prog_t *p, const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return false;
    }
    unsigned v;
    unsigned n = 0;
    bool ok = true;
    while (fscanf(f, "%x", &v) == 1) {
        if (n >= p->len || p->w[n] != (uint16_t)v) {
            ok = false;
        }
        ++n;
    }
    fclose(f);
    return ok && n == p->len;
}

/* ------------------------------------------------------------- the model */

typedef struct {
    const prog_t *p;
    unsigned pc;
    uint32_t x, y, isr;
    unsigned in_base, in_count;     /* relative to GPIO 16 */
    unsigned out_base, out_count;
} sm_t;

/* One instruction at a cycle; @p seen the pins as the synchroniser gives
 * them.  Returns true when it pushed, the word in *word; *out and *wrote
 * receive a MOV PINS. */
static bool step(sm_t *s, uint32_t seen, uint32_t *word, uint32_t *out,
                 bool *wrote)
{
    const uint16_t w = s->p->w[s->pc];
    unsigned next = (s->pc == s->p->wrap) ? s->p->wrap_target : s->pc + 1u;
    bool pushed = false;
    *wrote = false;
    const uint32_t inmask = (s->in_count >= 32u) ? 0xFFFFFFFFu
                            : ((1u << s->in_count) - 1u);
    const uint32_t pins = (seen >> s->in_base) & inmask;
    switch (w >> 13) {
    case 0: {
        const unsigned cond = (w >> 5) & 7u;
        if (cond == 0u || (cond == 5u && s->x != s->y)) {
            next = w & 31u;
        }
        break;
    }
    case 4:
        *word = s->isr;
        s->isr = 0u;
        pushed = true;
        break;
    case 5: {
        const unsigned dst = (w >> 5) & 7u;
        const unsigned src = w & 7u;
        uint32_t v = (src == 0u) ? pins : (src == 1u) ? s->x
                     : (src == 2u) ? s->y : (src == 6u) ? s->isr : 0u;
        if ((w & 0x18u) == 0x08u) {
            v = ~v;
        }
        if (dst == 0u) {
            const uint32_t m = ((1u << s->out_count) - 1u) << s->out_base;
            *out = (v << s->out_base) & m;
            *wrote = true;
        } else if (dst == 1u) { s->x = v; }
        else if (dst == 2u) { s->y = v; }
        else if (dst == 6u) { s->isr = v; }
        break;
    }
    default:
        break;
    }
    s->pc = next;
    return pushed;
}

static prog_t mirror_p, capture_p;

/* Input edges: at cycle t, input pin bits set to level. */
typedef struct { uint64_t t; uint32_t bits; bool high; } edge_t;

#define MAX_EDGES 200000u
static edge_t   edges[MAX_EDGES];
static size_t   nedges;

/* What the run records. */
#define MAX_WORDS 200000u
static uint32_t words[MAX_WORDS];
static uint64_t word_cyc[MAX_WORDS];       /* the sample that saw it */
static size_t   nwords;
static unsigned rxstall;
/* Output pad changes. */
static uint64_t out_cyc[MAX_EDGES];
static uint32_t out_lvl[MAX_EDGES];
static size_t   nout;

/* The ring and decoder the end-to-end runs feed. */
static uint32_t   r_states[PASS_RING_EVENTS];
static uint32_t   r_stamps[PASS_RING_EVENTS];
static uint32_t   r_head;
static pass_ring_t ring;
static pass_dec_t  dec;

typedef struct {
    bool     mirror;          /* run MIRROR */
    bool     capture;         /* run CAPTURE */
    unsigned dma_lat;         /* cycles a DMA event takes; 0 never */
    uint32_t start_level;     /* inputs high at the start */
    bool     decode;          /* feed the ring and the decoder */
    uint64_t drain_every;     /* cycles between drains */
} run_t;

static int cmp_edge(const void *a, const void *b)
{
    const edge_t *x = a;
    const edge_t *y = b;
    return (x->t > y->t) - (x->t < y->t);
}

static void run(uint64_t cycles, const run_t *rc)
{
    qsort(edges, nedges, sizeof(edges[0]), cmp_edge);
    nwords = nout = 0;
    rxstall = 0;
    r_head = 0;
    memset(r_states, 0, sizeof(r_states));
    memset(r_stamps, 0, sizeof(r_stamps));
    pass_ring_init(&ring);
    pass_dec_init(&dec, 0u);

    sm_t m = { &mirror_p, 0, 0, 0, 0, 1u, 17u, 0u, 17u };
    sm_t c = { &capture_p, capture_p.sample, 0, 0xFFFFFFFFu, 0, 0u, PINS,
               0u, 0u };
    uint32_t in = rc->start_level & IN_BITS;
    uint32_t outreg = 0u;                  /* MIRROR's output register */
    uint32_t pad_out = 0u;                 /* what the even pads drive */
    /* Raw levels, newest last: the lines stood at their start levels
     * before the run. */
    uint32_t hist[SYNC + 1u];
    for (unsigned i = 0; i <= SYNC; ++i) {
        hist[i] = in;
    }
    uint32_t fifo[4];
    unsigned nf = 0;
    uint64_t dma_free = 0;
    size_t ei = 0;
    for (uint64_t cyc = 0; cyc < cycles; ++cyc) {
        while (ei < nedges && edges[ei].t <= cyc) {
            in = edges[ei].high ? (in | edges[ei].bits)
                                : (in & ~edges[ei].bits);
            ++ei;
        }
        /* The pads now: outputs as MIRROR drove them last cycle. */
        const uint32_t raw = (in & IN_BITS) | (pad_out & OUT_BITS);
        for (unsigned i = 0; i < SYNC; ++i) {
            hist[i] = hist[i + 1u];
        }
        hist[SYNC] = raw;
        const uint32_t seen = hist[0];
        if (rc->mirror) {
            uint32_t o;
            bool wrote;
            uint32_t dummy;
            (void)step(&m, seen, &dummy, &o, &wrote);
            if (wrote) {
                outreg = o;
            }
        }
        if (rc->capture) {
            uint32_t w = 0u, o;
            bool wrote;
            if (step(&c, seen, &w, &o, &wrote)) {
                if (nf < 4u) {
                    fifo[nf++] = w;
                } else {
                    ++rxstall;
                }
            }
        }
        /* The DMA: one state and its stamp per dma_lat cycles. */
        if (nf > 0u && rc->dma_lat != 0u && cyc >= dma_free) {
            const uint32_t wv = fifo[0];
            memmove(&fifo[0], &fifo[1], (nf - 1u) * sizeof(fifo[0]));
            --nf;
            if (nwords < MAX_WORDS) {
                words[nwords] = wv;
                word_cyc[nwords] = cyc;
                ++nwords;
            }
            if (rc->decode) {
                const uint32_t i = r_head & (PASS_RING_EVENTS - 1u);
                r_states[i] = wv;
                r_stamps[i] = (uint32_t)((cyc + rc->dma_lat) / CPU_MHZ);
                ++r_head;
            }
            dma_free = cyc + rc->dma_lat;
        }
        if (rc->decode && cyc % rc->drain_every == rc->drain_every - 1u) {
            (void)pass_ring_drain(&ring, r_states, r_stamps, r_head,
                                  (uint32_t)(cyc / CPU_MHZ), &dec);
        }
        /* MIRROR's write reaches the pads at the next cycle; only the even
         * pins are outputs. */
        const uint32_t next_out = outreg & OUT_BITS;
        if (next_out != pad_out && nout < MAX_EDGES) {
            out_cyc[nout] = cyc + 1u;
            out_lvl[nout] = next_out;
            ++nout;
        }
        pad_out = next_out;
    }
}

static void edge(uint64_t t, uint32_t bits, bool high)
{
    if (nedges < MAX_EDGES) {
        edges[nedges].t = t;
        edges[nedges].bits = bits;
        edges[nedges].high = high;
        ++nedges;
    }
}

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t)(rng >> 16);
}

static bool load(void)
{
    return assemble(PASS_PIO, "pass_mirror", &mirror_p)
           && assemble(PASS_PIO, "pass_capture", &capture_p);
}

/* ------------------------------------------------------------------ cases */

TEST_CASE(the_programs_assemble_to_the_words_pioasm_gives)
{
    CHECK(load());
    CHECK(same_as_fixture(&mirror_p, PASS_MIRROR_WORDS));
    CHECK(same_as_fixture(&capture_p, PASS_CAPTURE_WORDS));
    CHECK_EQ(mirror_p.len, 1u);
    CHECK_EQ(mirror_p.wrap_target, 0u);
    CHECK_EQ(mirror_p.wrap, 0u);
    CHECK_EQ(capture_p.len, 5u);
    CHECK_EQ(capture_p.sample, 3u);
    CHECK_EQ(capture_p.wrap_target, 3u);
    CHECK_EQ(capture_p.wrap, 4u);
}

TEST_CASE(mirror_puts_every_input_edge_on_its_output_3_cycles_later)
{
    CHECK(load());
    nedges = 0;
    uint64_t t = 50;
    uint32_t lvl = 0u;
    /* Random edges on random inputs, 1 to 40 cycles apart: 1-cycle
     * pulses among them. */
    for (unsigned i = 0; i < 20000u; ++i) {
        const unsigned k = rnd() % PASS_CHANNELS;
        const uint32_t bit = 1u << PASS_IN_BIT(k);
        const bool high = (lvl & bit) == 0u;
        lvl ^= bit;
        edge(t, bit, high);
        t += 1u + rnd() % 40u;
    }
    const run_t rc = { true, false, 0u, 0u, false, 1u };
    run(t + 20u, &rc);
    /* Replay the inputs: each output change is the inputs' state 3 cycles
     * before, shifted one pin down. */
    uint32_t in = 0u;
    size_t ei = 0;
    size_t oi = 0;
    uint32_t out_now = 0u;
    for (uint64_t cyc = 0; cyc < t + 20u; ++cyc) {
        while (oi < nout && out_cyc[oi] <= cyc) {
            out_now = out_lvl[oi++];
        }
        if (cyc >= 3u) {
            /* The inputs as they stood at cyc - 3. */
            while (ei < nedges && edges[ei].t <= cyc - 3u) {
                in = edges[ei].high ? (in | edges[ei].bits)
                                    : (in & ~edges[ei].bits);
                ++ei;
            }
            if (out_now != ((in >> 1) & OUT_BITS)) {
                T_FAIL("cycle %llu: out %05x, inputs 3 cycles before %05x",
                       (unsigned long long)cyc, out_now, in);
                break;
            }
        }
    }
    CHECK(nout > 15000u);
}

TEST_CASE(capture_pushes_the_start_and_every_change_once)
{
    CHECK(load());
    nedges = 0;
    uint64_t t = 100;
    uint32_t lvl = (1u << PASS_IN_BIT(2)) | (1u << PASS_IN_BIT(7));
    const uint32_t start = lvl;
    uint32_t expect[3000];
    uint64_t at[3000];
    size_t ne = 0;
    expect[ne] = start;
    at[ne++] = 0;
    /* Edges at least 8 cycles apart: each one is its own state. */
    for (unsigned i = 0; i < 2999u; ++i) {
        const unsigned k = rnd() % PASS_CHANNELS;
        const uint32_t bit = 1u << PASS_IN_BIT(k);
        const bool high = (lvl & bit) == 0u;
        lvl ^= bit;
        edge(t, bit, high);
        expect[ne] = lvl;
        at[ne++] = t;
        t += 8u + rnd() % 30u;
    }
    const run_t rc = { false, true, 4u, start, false, 1u };
    run(t + 40u, &rc);
    CHECK_EQ(rxstall, 0u);
    CHECK_EQ(nwords, ne);
    for (size_t i = 0; i < nwords && i < ne; ++i) {
        if (words[i] != expect[i]) {
            T_FAIL("word %zu: %05x, want %05x", i, words[i], expect[i]);
            break;
        }
        /* Seen 2 to 8 cycles after the edge (the DMA takes it at once in
         * this run, 1 to 4 cycles after the push). */
        if (i > 0u) {
            const uint64_t lag = word_cyc[i] - at[i];
            if (lag < SYNC || lag > 8u + 4u) {
                T_FAIL("word %zu: %llu cycles after its edge", i,
                       (unsigned long long)lag);
                break;
            }
        }
    }
}

TEST_CASE(edges_inside_the_push_path_end_in_the_right_state)
{
    CHECK(load());
    for (unsigned gap = 1; gap < 8u; ++gap) {
        nedges = 0;
        /* Input 0 rises, input 1 rises gap cycles later, both fall 200
         * cycles on. */
        edge(100u, 1u << PASS_IN_BIT(0), true);
        edge(100u + gap, 1u << PASS_IN_BIT(1), true);
        edge(300u, (1u << PASS_IN_BIT(0)) | (1u << PASS_IN_BIT(1)), false);
        const run_t rc = { false, true, 4u, 0u, false, 1u };
        run(400u, &rc);
        CHECK(nwords >= 3u);
        /* Never the same state twice in a row. */
        for (size_t i = 1; i < nwords; ++i) {
            CHECK(words[i] != words[i - 1u]);
        }
        /* Both high before the fall, and low at the end. */
        bool both = false;
        for (size_t i = 0; i < nwords; ++i) {
            if (words[i] == ((1u << PASS_IN_BIT(0)) | (1u << PASS_IN_BIT(1)))) {
                both = true;
            }
        }
        CHECK(both);
        CHECK_EQ(words[nwords - 1u], 0u);
    }
}

TEST_CASE(a_stalled_dma_drops_states_and_one_that_keeps_up_drops_none)
{
    CHECK(load());
    nedges = 0;
    for (unsigned i = 0; i < 12u; ++i) {
        edge(100u + i * 50u, 1u << PASS_IN_BIT(0), (i & 1u) == 0u);
    }
    const run_t stall = { false, true, 0u, 0u, false, 1u };
    run(1000u, &stall);
    CHECK(rxstall > 0u);
    CHECK_EQ(nwords, 0u);
    /* 227,680 events a second is one every 659 cycles; edges every 650
     * cycles, a DMA event 16 cycles. */
    nedges = 0;
    for (unsigned i = 0; i < 4000u; ++i) {
        edge(100u + i * 650u, 1u << PASS_IN_BIT(i % 9u), (i / 9u) % 2u == 0u);
    }
    const run_t keep = { false, true, 16u, 0u, false, 1u };
    run(100u + 4000u * 650u + 100u, &keep);
    CHECK_EQ(rxstall, 0u);
    CHECK_EQ(nwords, 4001u);
}

/* 9 receiver signals of @p hz, input k @p w0 + k * @p dw us wide, all
 * rising together; MIRROR on, the capture into the ring, drained every 1ms.
 * Returns true when every figure is within 1us. */
static bool through(unsigned hz, uint32_t w0, uint32_t dw, unsigned frames)
{
    nedges = 0;
    const uint64_t period = (uint64_t)CPU_MHZ * 1000000u / hz;
    uint32_t all = 0u;
    for (unsigned k = 0; k < PASS_CHANNELS; ++k) {
        all |= 1u << PASS_IN_BIT(k);
    }
    for (unsigned f = 0; f < frames; ++f) {
        const uint64_t t = 1000u + f * period;
        edge(t, all, true);
        for (unsigned k = 0; k < PASS_CHANNELS; ++k) {
            edge(t + (uint64_t)(w0 + dw * k) * CPU_MHZ, 1u << PASS_IN_BIT(k),
                 false);
        }
    }
    const run_t rc = { true, true, 16u, 0u, true, CPU_MHZ * 1000u };
    run(1000u + frames * period + 2u * CPU_MHZ * 1000u, &rc);
    bool ok = rxstall == 0u;
    const uint32_t per_us = (uint32_t)(period / CPU_MHZ);
    for (unsigned k = 0; k < PASS_CHANNELS; ++k) {
        const pass_pin_t *in = &dec.pin[PASS_IN_BIT(k)];
        const pass_pin_t *out = &dec.pin[PASS_OUT_BIT(k)];
        const uint32_t w = w0 + dw * k;
        if (in->width_us + 1u < w || in->width_us > w + 1u
            || out->width_us + 1u < w || out->width_us > w + 1u
            || in->period_us + 1u < per_us || in->period_us > per_us + 1u
            || in->missed != 0u || in->glitches != 0u || out->glitches != 0u
            || in->frames != frames || out->frames != frames) {
            printf("      %uHz input %u: in %u out %u period %u missed %u "
                   "glitches %u frames %u/%u\n", hz, k,
                   (unsigned)in->width_us, (unsigned)out->width_us,
                   (unsigned)in->period_us, (unsigned)in->missed,
                   (unsigned)in->glitches, (unsigned)in->frames,
                   (unsigned)out->frames);
            ok = false;
        }
    }
    return ok;
}

TEST_CASE(nine_signals_at_560hz_and_40hz_come_out_within_1us)
{
    CHECK(load());
    /* 560Hz: 1786us frames, widths 400us to 1520us. */
    CHECK(through(560u, 400u, 140u, 8u));
    /* 40Hz: 25ms frames, widths 400us to 2500us. */
    CHECK(through(40u, 400u, 262u, 4u));
}

int main(void)
{
    RUN(the_programs_assemble_to_the_words_pioasm_gives);
    RUN(mirror_puts_every_input_edge_on_its_output_3_cycles_later);
    RUN(capture_pushes_the_start_and_every_change_once);
    RUN(edges_inside_the_push_path_end_in_the_right_state);
    RUN(a_stalled_dma_drops_states_and_one_that_keeps_up_drops_none);
    RUN(nine_signals_at_560hz_and_40hz_come_out_within_1us);
    return test_summary("pass_pio");
}
