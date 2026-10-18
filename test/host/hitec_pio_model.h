/*
 * A model of the Hitec line's PIO (programmable input/output) state machine
 * and the helpers that drive it, for test_hitec_pio.c.
 *
 * assemble() reads protocols/hitec/rp2350/hitec_line.pio with an assembler
 * for the instructions it uses; run_model() steps the program one state
 * machine cycle at a time against the wire the test gives (ext_edge(),
 * ext_byte()).  test_hitec_pio.c says what the model covers and what it
 * leaves to the chip.  Include it after greatest.h, once per suite.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef HITEC_PIO_MODEL_H
#define HITEC_PIO_MODEL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hitec_pio.h"
#include "hitec_wire.h"

#define MAX_PROG 32u
#define IDLE_PC  HITEC_PIO_IDLE_PC
#define REL_PC   HITEC_PIO_RELEASE_PC  /* set pindirs, 0 after the bits */

/* ---------------------------------------------------------- the assembler */

static uint16_t prog[MAX_PROG];
static unsigned prog_len;
static unsigned wrap_target;
static unsigned wrap_at;

typedef struct {
    char name[16];
    unsigned addr;
} label_t;

static int reg_code(const char *t)
{
    if (!strcmp(t, "pins")) { return 0; }
    if (!strcmp(t, "x")) { return 1; }
    if (!strcmp(t, "y")) { return 2; }
    if (!strcmp(t, "pindirs")) { return 4; }
    return -1;
}

static int cond_code(const char *t)
{
    if (!strcmp(t, "x--")) { return 2; }
    if (!strcmp(t, "y--")) { return 4; }
    if (!strcmp(t, "pin")) { return 6; }
    if (!strcmp(t, "!osre")) { return 7; }
    return -1;
}

static bool assemble(const char *path)
{
    static char text[64][96];
    label_t labels[16];
    unsigned n_labels = 0;
    unsigned lines = 0;
    unsigned addr = 0;
    char buf[200];
    FILE *f = fopen(path, "r");

    if (f == NULL) {
        return false;
    }
    wrap_target = 0;
    wrap_at = 0;
    while (fgets(buf, sizeof(buf), f) != NULL) {
        char *c = strchr(buf, ';');
        char *s = buf;
        size_t len;

        if (c != NULL) {
            *c = '\0';
        }
        while (*s == ' ' || *s == '\t') {
            ++s;
        }
        len = strlen(s);
        while (len > 0u && strchr(" \t\r\n", s[len - 1u]) != NULL) {
            s[--len] = '\0';
        }
        if (len == 0u || !strncmp(s, ".program", 8)) {
            continue;
        }
        if (!strcmp(s, ".wrap_target")) {
            wrap_target = addr;
        } else if (!strcmp(s, ".wrap")) {
            wrap_at = addr - 1u;
        } else if (s[len - 1u] == ':') {
            if (n_labels == 16u || len > 15u) {
                fclose(f);
                return false;
            }
            s[len - 1u] = '\0';
            strcpy(labels[n_labels].name, s);
            labels[n_labels++].addr = addr;
        } else {
            if (lines == 64u || len >= sizeof(text[0]) || addr == MAX_PROG) {
                fclose(f);
                return false;
            }
            strcpy(text[lines++], s);
            ++addr;
        }
    }
    fclose(f);

    prog_len = 0;
    for (unsigned i = 0; i < lines; ++i) {
        char *tok[6];
        unsigned n = 0;
        unsigned delay = 0;
        unsigned w;

        for (char *p = strtok(text[i], " \t,"); p != NULL && n < 6u;
             p = strtok(NULL, " \t,")) {
            if (p[0] == '[') {
                delay = (unsigned)atoi(p + 1);
            } else {
                tok[n++] = p;
            }
        }
        if (n >= 2u && !strcmp(tok[0], "jmp")) {
            const int cond = n == 3u ? cond_code(tok[1]) : 0;
            int target = -1;

            for (unsigned j = 0; j < n_labels; ++j) {
                if (!strcmp(labels[j].name, tok[n - 1u])) {
                    target = (int)labels[j].addr;
                }
            }
            if (cond < 0 || target < 0) {
                return false;
            }
            w = ((unsigned)cond << 5) | (unsigned)target;
        } else if (n == 3u && !strcmp(tok[0], "mov")
                   && !strcmp(tok[1], "osr") && !strcmp(tok[2], "null")) {
            w = 0xA0E3u;                     /* MOV OSR, NULL */
        } else if (n == 3u && !strcmp(tok[0], "out")) {
            const int dest = !strcmp(tok[1], "null") ? 3 : reg_code(tok[1]);
            const unsigned bits = (unsigned)atoi(tok[2]);

            if (dest < 0 || bits == 0u || bits > 32u) {
                return false;
            }
            w = 0x6000u | ((unsigned)dest << 5) | (bits & 31u);
        } else if (n == 3u && !strcmp(tok[0], "in")) {
            const int src = reg_code(tok[1]);
            const unsigned bits = (unsigned)atoi(tok[2]);

            if (src < 0 || src > 1 || bits == 0u || bits > 32u) {
                return false;
            }
            w = 0x4000u | ((unsigned)src << 5) | (bits & 31u);
        } else if (n == 3u && !strcmp(tok[0], "set")) {
            const int dest = reg_code(tok[1]);

            if (dest < 0) {
                return false;
            }
            w = 0xE000u | ((unsigned)dest << 5)
                | ((unsigned)atoi(tok[2]) & 31u);
        } else if (n == 2u && !strcmp(tok[0], "push")
                   && !strcmp(tok[1], "noblock")) {
            w = 0x8000u;
        } else {
            return false;
        }
        prog[prog_len++] = (uint16_t)(w | (delay << 8));
    }
    return prog_len > 0u;
}

/* ------------------------------------------------------------- the model */

#define MAX_PAD  1200u
#define MAX_EXT  600u
#define MAX_RX   64u

typedef struct {
    uint64_t cyc;
    bool dir;          /* the pad drives */
    bool latch;
} pad_t;

typedef struct {
    double ns;         /* after the first release */
    bool level;
} ext_t;

typedef struct {
    /* what the test gives */
    uint32_t div256;
    uint32_t sys_hz;
    bool invert;              /* the D series' input override */
    uint32_t tx[HITEC_PIO_TX_WORDS];
    unsigned n_tx;
    ext_t ext[MAX_EXT];
    unsigned n_ext;
    bool ext0;                /* the released wire before the first change */
    unsigned rx_room;         /* words the receive channel takes */
    uint64_t abort_at;        /* 0: none; else the cycle of a restart */
    /* Set: due_end is the first cycle after the first push that
     * hitec_pio_answer_due() gives false for. */
    const hitec_pio_run_t *due_run;
    /* what the run leaves */
    pad_t pad[MAX_PAD];
    unsigned n_pad;
    uint32_t rx[MAX_RX];
    unsigned n_rx;
    unsigned dropped;         /* pushes into a full FIFO */
    uint64_t release;         /* the cycle of the first release */
    bool released;
    uint64_t end;             /* the cycle the machine stalled for a stream */
    bool finished;
    unsigned answers;         /* passes through the answer */
    unsigned pushes;
    uint64_t due_end;
    unsigned abort_pc;        /* where the machine was at the restart */
} model_t;

static uint64_t t_of(const model_t *m, uint64_t cyc)
{
    return cyc * m->div256;
}

static bool ext_level(const model_t *m, uint64_t t)
{
    bool level = m->ext0;

    if (!m->released || t < t_of(m, m->release)) {
        return level;
    }
    t -= t_of(m, m->release);
    const double ns = (double)t * 1e9 / ((double)m->sys_hz * 256.0);

    for (unsigned i = 0; i < m->n_ext && m->ext[i].ns <= ns; ++i) {
        level = m->ext[i].level;
    }
    return level;
}

/* The pad at time @p t: 1 driven high, 0 driven low, -1 an input. */
static int pad_at_t(const model_t *m, uint64_t t)
{
    int v = -1;

    for (unsigned i = 0; i < m->n_pad && t_of(m, m->pad[i].cyc) <= t; ++i) {
        v = m->pad[i].dir ? (m->pad[i].latch ? 1 : 0) : -1;
    }
    return v;
}

static int pad_at(const model_t *m, uint64_t cyc)
{
    return pad_at_t(m, t_of(m, cyc));
}

static bool wire_at_t(const model_t *m, uint64_t t)
{
    const int p = pad_at_t(m, t);

    return p >= 0 ? p == 1 : ext_level(m, t);
}

/* What the program reads at cycle @p cyc: the wire 2 system cycles before,
 * through the input override. */
static bool input_at(const model_t *m, uint64_t cyc)
{
    const uint64_t t = t_of(m, cyc);
    const bool w = wire_at_t(m, t >= 512u ? t - 512u : 0u);

    return m->invert ? !w : w;
}

static void run_model(model_t *m, uint64_t max_cycles)
{
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t isr = 0;
    uint32_t osr = 0;
    unsigned osr_count = 32;      /* empty */
    unsigned isr_count = 0;
    uint32_t txf[4];
    uint32_t rxf[4];
    unsigned n_txf = 0;
    unsigned n_rxf = 0;
    unsigned tx_at = 0;
    unsigned pc = IDLE_PC;
    unsigned delay = 0;
    bool dir = false;
    bool latch = false;

    m->n_pad = 0;
    m->n_rx = 0;
    m->dropped = 0;
    m->release = 0;
    m->released = false;
    m->end = 0;
    m->finished = false;
    m->answers = 0;
    m->pushes = 0;
    m->due_end = 0;
    m->pad[m->n_pad].cyc = 0;
    m->pad[m->n_pad].dir = false;
    m->pad[m->n_pad].latch = false;
    m->n_pad++;

    for (uint64_t cyc = 0; cyc < max_cycles; ++cyc) {
        const bool was_dir = dir;
        const bool was_latch = latch;

        if (m->abort_at != 0u && cyc == m->abort_at) {
            /* The driver's restart: released, FIFOs and the input side
             * cleared, the machine at the drain.  The output shift
             * register is left full of ones, the worst a restart can
             * leave: the drain has to empty it. */
            m->abort_pc = pc;
            dir = false;
            n_txf = 0;
            n_rxf = 0;
            tx_at = m->n_tx;
            osr = 0xFFFFFFFFu;
            osr_count = 0;
            isr = 0;
            isr_count = 0;
            delay = 0;
            pc = HITEC_PIO_DRAIN_PC;
            if (!m->released) {
                m->release = cyc;
                m->released = true;
            }
        }
        /* The channels: faster than a state machine cycle. */
        while (tx_at < m->n_tx && n_txf < 4u) {
            txf[n_txf++] = m->tx[tx_at++];
        }
        while (n_rxf > 0u && m->n_rx < m->rx_room) {
            m->rx[m->n_rx++] = rxf[0];
            memmove(&rxf[0], &rxf[1], (n_rxf - 1u) * sizeof(rxf[0]));
            --n_rxf;
        }

        if (m->due_run != NULL && m->pushes > 0u && m->due_end == 0u
            && !hitec_pio_answer_due(m->due_run, m->pushes, pc)) {
            m->due_end = cyc;
        }
        if (delay > 0u) {
            --delay;
        } else {
            const uint16_t w = prog[pc];
            unsigned next = pc == wrap_at ? wrap_target : pc + 1u;
            const unsigned arg = w & 31u;
            const unsigned sel = (w >> 5) & 7u;
            bool stalled = false;

            switch (w >> 13) {
            case 0: {                                           /* JMP */
                bool take = true;

                if (sel == 2u) {
                    take = x != 0u;
                    --x;
                } else if (sel == 4u) {
                    take = y != 0u;
                    --y;
                } else if (sel == 6u) {
                    take = input_at(m, cyc);
                } else if (sel == 7u) {
                    take = osr_count < 32u;
                }
                if (take) {
                    next = arg;
                }
                break;
            }
            case 2: {                                           /* IN */
                const unsigned bits = arg == 0u ? 32u : arg;
                const uint32_t v = sel == 0u ? (input_at(m, cyc) ? 1u : 0u)
                                             : x;
                const uint32_t data = bits == 32u ? v
                                                  : v & ((1u << bits) - 1u);

                isr = bits == 32u ? data
                                  : (isr >> bits) | (data << (32u - bits));
                isr_count = isr_count + bits > 32u ? 32u : isr_count + bits;
                break;
            }
            case 3: {                                           /* OUT */
                const unsigned bits = arg == 0u ? 32u : arg;
                uint32_t data;

                if (osr_count >= 32u) {
                    if (n_txf > 0u) {
                        osr = txf[0];
                        memmove(&txf[0], &txf[1],
                                (n_txf - 1u) * sizeof(txf[0]));
                        --n_txf;
                        osr_count = 0;
                    } else if (pc == IDLE_PC && tx_at == m->n_tx
                               && m->released) {
                        m->end = cyc;
                        m->finished = true;
                    }
                    stalled = true;
                    break;
                }
                data = bits == 32u ? osr : osr & ((1u << bits) - 1u);
                osr = bits == 32u ? 0u : osr >> bits;
                osr_count += bits;
                if (osr_count >= 32u && n_txf > 0u) {
                    osr = txf[0];
                    memmove(&txf[0], &txf[1], (n_txf - 1u) * sizeof(txf[0]));
                    --n_txf;
                    osr_count = 0;
                }
                if (sel == 3u) {
                    /* null: discarded */
                } else if (sel == 0u) {
                    latch = (data & 1u) != 0u;
                } else if (sel == 1u) {
                    x = data;
                } else if (sel == 2u) {
                    y = data;
                } else {
                    dir = (data & 1u) != 0u;
                }
                break;
            }
            case 5:                                    /* MOV OSR, NULL */
                osr = 0;
                osr_count = 0;
                break;
            case 4:                                             /* PUSH */
                m->pushes++;
                if (n_rxf < 4u) {
                    rxf[n_rxf++] = isr;
                } else {
                    m->dropped++;
                }
                isr = 0;
                isr_count = 0;
                break;
            default:                                            /* SET */
                if (sel == 2u) {
                    y = arg;
                } else if (sel == 4u) {
                    dir = (arg & 1u) != 0u;
                } else {
                    latch = (arg & 1u) != 0u;
                }
                if (pc == HITEC_PIO_ANSWER_PC) {
                    m->answers++;
                }
                if (pc == REL_PC && !m->released) {
                    m->release = cyc;
                    m->released = true;
                }
                break;
            }
            if (!stalled) {
                pc = next;
                delay = (w >> 8) & 31u;
            }
        }
        if ((dir != was_dir || latch != was_latch) && m->n_pad < MAX_PAD) {
            m->pad[m->n_pad].cyc = cyc;
            m->pad[m->n_pad].dir = dir;
            m->pad[m->n_pad].latch = latch;
            m->n_pad++;
        }
        if (m->finished && n_rxf == 0u) {
            return;
        }
    }
}

/* ---------------------------------------------------------------- helpers */

static model_t m;
static hitec_pio_clock_t clk;
static hitec_pio_run_t run;

/* @p cyc state machine cycles as ns. */
static double cyc_ns(uint64_t cyc)
{
    return (double)cyc * (double)clk.div256 / 256.0 * 1e9
           / (double)clk.sys_hz;
}

static double bit_ns(uint8_t family)
{
    return 1e9 / (double)hitec_bit_rate(family);
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

/* A byte as the servo sends it, its start bit @p at ns after the release,
 * @p bit ns a bit.  @p stop_level is the logical level of the stop bit. */
static void ext_byte(uint8_t family, double at, double bit, uint8_t value,
                     bool stop_level)
{
    const bool inv = family == HITEC_D;

    ext_edge(at, inv);                       /* the start bit: logical 0 */
    for (unsigned b = 0; b < 8u; ++b) {
        ext_edge(at + bit * (1 + b), (((value >> b) & 1u) != 0u) != inv);
    }
    ext_edge(at + bit * 9, stop_level != inv);
    if (!stop_level) {
        ext_edge(at + bit * 10, !inv);       /* idle again after a bit */
    }
}

/* Lay @p x out for the line and run it; false when no stream is made. */
static bool transact(uint32_t hz, uint8_t drive, const hitec_xfer_t *x,
                     unsigned rx_room, uint64_t abort_at)
{
    if (!hitec_pio_clock(hz, x->family, &clk)) {
        return false;
    }
    m.div256 = clk.div256;
    m.sys_hz = clk.sys_hz;
    m.invert = x->family == HITEC_D;
    m.n_tx = hitec_pio_tx(&clk, drive, x, m.tx, &run);
    if (m.n_tx == 0u) {
        return false;
    }
    m.rx_room = rx_room;
    m.abort_at = abort_at;
    run_model(&m, (uint64_t)(run.span_us + 100u) * 2000u);
    return true;
}

static void captured(hitec_capture_t *out, bool full)
{
    hitec_pio_capture(&clk, &run, m.rx, m.n_rx, full, false, false, false,
                      out);
}

/* The bytes a UART reads off the wire from cycle @p from on: idle to start,
 * mid-bit samples at 16 cycles a bit.  Returns the count; @p at gets the
 * cycle of each start edge. */
static unsigned wire_bytes(uint64_t from, uint64_t to, bool inv,
                           uint8_t *out, uint64_t *at, unsigned max)
{
    unsigned n = 0;
    bool prev = wire_at_t(&m, t_of(&m, from)) != inv;

    for (uint64_t c = from + 1u; c < to && n < max; ++c) {
        const bool now = wire_at_t(&m, t_of(&m, c)) != inv;

        if (prev && !now) {
            uint8_t v = 0;

            for (unsigned b = 0; b < 8u; ++b) {
                if (wire_at_t(&m, t_of(&m, c + 24u + 16u * b)) != inv) {
                    v |= (uint8_t)(1u << b);
                }
            }
            CHECK(wire_at_t(&m, t_of(&m, c + 152u)) != inv);
            out[n] = v;
            at[n] = c;
            ++n;
            c += 152u;
            prev = true;
            continue;
        }
        prev = now;
    }
    return n;
}

/* Every change of the pad between @p from and @p to lies on the 16-cycle
 * grid from @p origin. */
static void check_grid(uint64_t origin, uint64_t from, uint64_t to)
{
    for (unsigned i = 0; i < m.n_pad; ++i) {
        if (m.pad[i].cyc >= from && m.pad[i].cyc <= to) {
            CHECK_EQ((m.pad[i].cyc - origin) % 16u, 0u);
        }
    }
}

static bool pad_ever_high(uint64_t from)
{
    for (unsigned i = 0; i < m.n_pad; ++i) {
        if (m.pad[i].cyc >= from && m.pad[i].dir && m.pad[i].latch) {
            return true;
        }
    }
    return false;
}

/* The cycle of the first change of the pad to @p dir at or after @p from;
 * 0 for none. */
static uint64_t next_dir(uint64_t from, bool dir)
{
    for (unsigned i = 0; i < m.n_pad; ++i) {
        if (m.pad[i].cyc >= from && m.pad[i].dir == dir) {
            return m.pad[i].cyc;
        }
    }
    return 0;
}

/* A stamp within one cycle, the synchroniser and rounding of the edge. */
static void check_at(double got_ns, double want_ns)
{
    const double slack = cyc_ns(1) + 2e9 / (double)clk.sys_hz + 2.0;

    CHECK_NEAR(got_ns, want_ns, slack);
}


/* The D485HW's answer as the bench shows it: the line high until the servo
 * pulls it low, the answer, a byte 0x00 back to back, the line low for
 * 150 us, then released. */
static void d_answer(double first_ns, const uint8_t *a, unsigned n)
{
    const double bit = bit_ns(HITEC_D);
    double t = first_ns;

    ext_clear(true);
    ext_edge(300000.0, false);
    for (unsigned i = 0; i < n; ++i) {
        ext_byte(HITEC_D, t, bit, a[i], true);
        t += 10.0 * bit;
    }
    ext_byte(HITEC_D, t, bit, 0x00, true);
    ext_edge(t + 10.0 * bit + 150000.0, true);
}

/* A classic command and its reply, and a D read and its answer where the
 * clock gives the D series a divider, at a system clock of @p hz. */
static void check_clock_runs(uint32_t hz)
{
    hitec_xfer_t x;
    hitec_capture_t cap;
    hitec_reply_t r;
    uint8_t got[8] = { 0 };
    uint64_t at[8] = { 0 };
    static const uint8_t a[] = { 0x69, 0x00, 0x4E, 0x02, 0x01, 0x00, 0x51 };

    /* Classic: a command and its reply. */
    hitec_c_xfer(HITEC_C_READ_EEPROM, 0x13, 0x00, &x);
    memset(&m, 0, sizeof(m));
    ext_clear(true);
    ext_byte(HITEC_CLASSIC, 45000.0, bit_ns(HITEC_CLASSIC), 0xC2, true);
    ext_byte(HITEC_CLASSIC, 45000.0 + 516000.0, bit_ns(HITEC_CLASSIC), 0x2B,
             true);
    CHECK(transact(hz, HITEC_DRIVE_OPEN_DRAIN, &x, 2, 0));
    CHECK(clk.err_ppm <= 489u);
    CHECK_NEAR((double)hitec_pio_bit_ns(&clk), bit_ns(HITEC_CLASSIC),
               bit_ns(HITEC_CLASSIC) * 0.0005 + 1.0);
    CHECK_EQ(wire_bytes(0, m.release + 1u, false, got, at, 8), 4u);
    CHECK_EQ(got[1], 0x13);
    captured(&cap, true);
    CHECK_EQ(hitec_c_decode(&cap, HITEC_C_EXPECT_VALUE, &r), HITEC_RX_OK);
    CHECK_EQ(r.value, 0xC2);
    check_at(cap.byte[0].at_ns, 45000.0);

    /* D series, where the clock gives a divider. */
    if (!hitec_pio_clock(hz, HITEC_D, &clk)) {
        return;
    }
    hitec_d_read_xfer(0x00, 0x4E, &x);
    memset(&m, 0, sizeof(m));
    d_answer(6000000.0, a, sizeof(a));
    CHECK(transact(hz, HITEC_DRIVE_OPEN_DRAIN, &x, 8, 0));
    CHECK_EQ(wire_bytes(0, m.release - 16u, true, got, at, 8), 5u);
    CHECK_EQ(got[2], 0x4E);
    captured(&cap, true);
    CHECK_EQ(hitec_d_decode(&cap, 0x4E, &r), HITEC_RX_OK);
    CHECK_EQ(r.value, 1u);
    check_at(cap.byte[0].at_ns, 6000000.0);
}

/* The smallest clock @p family takes, found by bisection. */
static uint32_t lowest_clock(uint8_t family)
{
    uint32_t lo = 1;
    uint32_t hi = 20000000u;
    hitec_pio_clock_t c;

    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2u;

        if (hitec_pio_clock(mid, family, &c)) {
            hi = mid;
        } else {
            lo = mid + 1u;
        }
    }
    return lo;
}

#endif /* HITEC_PIO_MODEL_H */
