/*
 * The KST line's PIO (programmable input/output) program, run on the host
 * (protocols/kst/rp2350/kst_line.pio), with the arithmetic that feeds it
 * and reads it (protocols/kst/kst_pio.c).
 *
 * The program is assembled here from its source by a small assembler for
 * the instructions it uses, and run by a cycle-counting model of the state
 * machine: JMP always, on X--, Y-- and the pin; OUT to the pins, X, Y and
 * the ISR with autopull at 32 bits; MOV between X, Y and the ISR; PUSH
 * noblock; SET of the pins and the pin directions; delays; the wrap.  A DMA
 * (direct memory access) channel fills the 4-word transmit FIFO and another
 * drains the 4-word receive FIFO into a buffer of a given length.  The pin
 * is read two cycles after it moves, as the input synchroniser delays it.
 * The assembled words are held to fixtures/kst_line_words.txt, which the
 * firmware build holds pioasm's output to: the model runs what the chip
 * runs.
 *
 * The wire is the pad's latch while the pad drives, and the servo's level
 * while it does not; with neither it is low, as a pull-down holds it.
 *
 * Under test:
 *  - the frame: every half-cell of a read, a write and the sync burst is
 *    3810 cycles at 150 MHz and every edge lies on that grid; at 125, 133,
 *    48 and 12 MHz the half-cell is within 0.2 % of 25.40 us;
 *  - the pad: driven from the start of the stream, released 50 us after
 *    the last edge, driven low again at the end of the window or 165 us
 *    after the last rising edge, never driven high after the last edge;
 *  - the samples: 4 cycles apart on every path of the capture;
 *  - the stamps: the edges of a reply as kst_sim.h shapes it come back
 *    within one tick, at every phase of the 4-cycle sample, and decode;
 *  - a line that is high at the release, one that stays high, one that
 *    moves more often than the buffer holds, a stamp no count gives;
 *  - the clock: refused below 8 MHz and where the half-cell is off by more
 *    than 0.2 %; the stream's layout for 1, 31, 32, 63 and 127 half-cells
 *    and the arguments it refuses.
 *
 * Not tested here, because the host has no PIO: that the chip's input
 * synchroniser, autopull, JMP PIN, delay and DMA handshakes behave as this
 * model says; the pad, the pull-down and the servo's edges.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "kst_pio.h"
#include "kst_sim.h"
#include "kst_wire.h"

#define MAX_PROG 32u
#define SYNC     KST_PIO_SYNC_CYCLES

/* ---------------------------------------------------------- the assembler */

static uint16_t prog[MAX_PROG];
static unsigned prog_len;
static unsigned wrap_target;
static unsigned wrap_at;

typedef struct {
    char name[16];
    unsigned addr;
} label_t;

static int reg_code(const char *t, bool set_dest)
{
    if (!strcmp(t, "pins")) { return 0; }
    if (!strcmp(t, "x")) { return 1; }
    if (!strcmp(t, "y")) { return 2; }
    if (!strcmp(t, "pindirs")) { return 4; }
    if (!strcmp(t, "isr") && !set_dest) { return 6; }
    return -1;
}

static int cond_code(const char *t)
{
    if (!strcmp(t, "x--")) { return 2; }
    if (!strcmp(t, "y--")) { return 4; }
    if (!strcmp(t, "pin")) { return 6; }
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
    /* Pass 1: strip comments, take directives and labels, keep the
     * instructions. */
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

    /* Pass 2: encode. */
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
            const char *name = tok[n - 1u];
            int target = -1;

            for (unsigned j = 0; j < n_labels; ++j) {
                if (!strcmp(labels[j].name, name)) {
                    target = (int)labels[j].addr;
                }
            }
            if (cond < 0 || target < 0) {
                return false;
            }
            w = ((unsigned)cond << 5) | (unsigned)target;
        } else if (n == 3u && !strcmp(tok[0], "out")) {
            const int dest = reg_code(tok[1], false);
            const unsigned bits = (unsigned)atoi(tok[2]);

            if (dest < 0 || dest == 4 || bits == 0u || bits > 32u) {
                return false;
            }
            w = 0x6000u | ((unsigned)dest << 5) | (bits & 31u);
        } else if (n == 3u && !strcmp(tok[0], "mov")) {
            const int dest = reg_code(tok[1], false);
            const int src = reg_code(tok[2], false);

            if (dest < 1 || dest == 4 || src < 1 || src == 4) {
                return false;
            }
            w = 0xA000u | ((unsigned)dest << 5) | (unsigned)src;
        } else if (n == 3u && !strcmp(tok[0], "set")) {
            const int dest = reg_code(tok[1], true);

            if (dest != 0 && dest != 4) {
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

typedef struct {
    uint64_t at;     /* cycles from the frame's last edge */
    bool level;
} ext_t;

typedef struct {
    uint64_t cyc;
    bool dir;        /* the pad drives */
    bool latch;
} drive_t;

#define MAX_DRIVE 400u
#define MAX_EXT   200u
#define MAX_RX    64u

typedef struct {
    /* what the test gives */
    uint32_t tx[KST_PIO_TX_WORDS];
    unsigned n_tx;
    ext_t ext[MAX_EXT];
    unsigned n_ext;
    bool ext0;            /* the servo's level before its first change */
    unsigned rx_room;     /* words the receive channel takes */
    /* what the run leaves */
    drive_t drive[MAX_DRIVE];
    unsigned n_drive;
    uint32_t rx[MAX_RX];
    unsigned n_rx;
    unsigned dropped;     /* pushes into a full FIFO */
    uint64_t e0;          /* the frame's last edge */
    uint64_t release;     /* the cycle the pad became an input */
    uint64_t redrive;     /* the cycle it drove again */
    uint64_t first_sample;
    uint64_t samples;
    uint64_t bad_gaps;    /* samples not 4 cycles after the one before */
    uint64_t end;         /* the cycle the machine stalled for a stream */
    bool finished;
} model_t;

static bool ext_level(const model_t *m, uint64_t cyc)
{
    bool level = m->ext0;

    if (cyc < m->e0) {
        return level;
    }
    for (unsigned i = 0; i < m->n_ext && m->ext[i].at <= cyc - m->e0; ++i) {
        level = m->ext[i].level;
    }
    return level;
}

static void run_model(model_t *m, uint64_t max_cycles)
{
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t isr = 0;
    uint32_t osr = 0;
    unsigned osr_count = 32;      /* empty */
    uint32_t txf[4];
    uint32_t rxf[4];
    unsigned n_txf = 0;
    unsigned n_rxf = 0;
    unsigned tx_at = 0;
    unsigned pc = 0;
    unsigned delay = 0;
    bool dir = false;
    bool latch = false;
    bool wire[4] = { false, false, false, false };
    uint64_t last_fall = 0;
    uint64_t last_sample = 0;
    bool released = false;
    bool in_capture = false;

    m->n_drive = 0;
    m->n_rx = 0;
    m->dropped = 0;
    m->e0 = ~(uint64_t)0;
    m->release = 0;
    m->redrive = 0;
    m->first_sample = 0;
    m->samples = 0;
    m->bad_gaps = 0;
    m->finished = false;

    for (uint64_t cyc = 0; cyc < max_cycles; ++cyc) {
        const bool was_dir = dir;
        const bool was_latch = latch;
        bool stalled = false;

        /* The channels: one word a cycle each way. */
        if (tx_at < m->n_tx && n_txf < 4u) {
            txf[n_txf++] = m->tx[tx_at++];
        }
        if (n_rxf > 0u && m->n_rx < m->rx_room) {
            m->rx[m->n_rx++] = rxf[0];
            memmove(&rxf[0], &rxf[1], (n_rxf - 1u) * sizeof(rxf[0]));
            --n_rxf;
        }

        if (delay > 0u) {
            --delay;
        } else {
            const uint16_t w = prog[pc];
            unsigned next = pc == wrap_at ? wrap_target : pc + 1u;
            const unsigned arg = w & 31u;
            const unsigned sel = (w >> 5) & 7u;

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
                    take = wire[(cyc - SYNC) % 4u];
                    if (m->samples == 0u) {
                        m->first_sample = cyc;
                    } else if (cyc - last_sample != KST_PIO_TICK_CYCLES) {
                        m->bad_gaps++;
                    }
                    last_sample = cyc;
                    m->samples++;
                }
                if (take) {
                    next = arg;
                }
                break;
            }
            case 3: {                                           /* OUT */
                const unsigned bits = arg == 0u ? 32u : arg;
                uint32_t data;

                if (osr_count >= 32u) {
                    /* Empty: this cycle pulls, or waits for a word. */
                    if (n_txf > 0u) {
                        osr = txf[0];
                        memmove(&txf[0], &txf[1],
                                (n_txf - 1u) * sizeof(txf[0]));
                        --n_txf;
                        osr_count = 0;
                    } else if (pc == 1u && tx_at == m->n_tx && released) {
                        m->end = cyc;
                        m->finished = true;
                    }
                    stalled = true;
                    break;
                }
                data = bits == 32u ? osr : osr >> (32u - bits);
                osr = bits == 32u ? 0u : osr << bits;
                osr_count += bits;
                if (osr_count >= 32u && n_txf > 0u) {
                    osr = txf[0];
                    memmove(&txf[0], &txf[1], (n_txf - 1u) * sizeof(txf[0]));
                    --n_txf;
                    osr_count = 0;
                }
                if (sel == 0u) {
                    latch = (data & 1u) != 0u;
                } else if (sel == 1u) {
                    x = data;
                } else if (sel == 2u) {
                    y = data;
                } else {
                    isr = data;
                }
                break;
            }
            case 4:                                             /* PUSH */
                if (n_rxf < 4u) {
                    rxf[n_rxf++] = isr;
                } else {
                    m->dropped++;
                }
                isr = 0;
                break;
            case 5: {                                           /* MOV */
                const unsigned src = w & 7u;
                const uint32_t v = src == 1u ? x : src == 2u ? y : isr;

                if (sel == 1u) {
                    x = v;
                } else if (sel == 2u) {
                    y = v;
                } else {
                    isr = v;
                }
                break;
            }
            default:                                            /* SET */
                if (sel == 0u) {
                    latch = (arg & 1u) != 0u;
                } else {
                    dir = (arg & 1u) != 0u;
                }
                break;
            }
            if (!stalled) {
                pc = next;
                delay = (w >> 8) & 31u;
            }
        }

        if (was_latch && !latch) {
            last_fall = cyc;
        }
        if (was_dir && !dir) {
            m->e0 = last_fall;
            m->release = cyc;
            released = true;
            in_capture = true;
        }
        if (!was_dir && dir && in_capture) {
            m->redrive = cyc;
            in_capture = false;
        }
        if ((dir != was_dir || latch != was_latch || cyc == 0u)
            && m->n_drive < MAX_DRIVE) {
            m->drive[m->n_drive].cyc = cyc;
            m->drive[m->n_drive].dir = dir;
            m->drive[m->n_drive].latch = latch;
            m->n_drive++;
        }
        wire[cyc % 4u] = dir ? latch : ext_level(m, cyc);
        if (m->finished && n_rxf == 0u) {
            return;
        }
    }
}

/* The pad at @p cyc: 1 driven high, 0 driven low, -1 an input. */
static int pad_at(const model_t *m, uint64_t cyc)
{
    int v = -1;

    for (unsigned i = 0; i < m->n_drive && m->drive[i].cyc <= cyc; ++i) {
        v = m->drive[i].dir ? (m->drive[i].latch ? 1 : 0) : -1;
    }
    return v;
}

/* ---------------------------------------------------------------- helpers */

static model_t m;
static kst_pio_clock_t clk;
static kst_pio_run_t run;

static uint64_t ns_cycles(uint32_t hz, uint64_t ns)
{
    return (ns * hz + 500000000u) / 1000000000u;
}

/* Lay out @p frame for @p hz and run it with the servo's level changes in
 * @p cap, taken as times after the frame's last edge. */
static bool transact(uint32_t hz, const kst_frame_t *frame,
                     uint32_t window_ns, const kst_capture_t *cap,
                     unsigned rx_room)
{
    bool level = cap != NULL && cap->level0 != 0u;

    memset(&m, 0, sizeof(m));
    if (!kst_pio_clock(hz, &clk)) {
        return false;
    }
    m.n_tx = kst_pio_tx(&clk, frame, window_ns, m.tx, &run);
    if (m.n_tx == 0u) {
        return false;
    }
    m.ext0 = level;
    for (unsigned i = 0; cap != NULL && i < cap->n_edges; ++i) {
        level = !level;
        m.ext[m.n_ext].at = ns_cycles(hz, cap->edge_ns[i]);
        m.ext[m.n_ext].level = level;
        m.n_ext++;
    }
    m.rx_room = rx_room;
    run_model(&m, ns_cycles(hz, (uint64_t)run.lead_ns + window_ns) + 100000u);
    return m.finished;
}

static void captured(kst_capture_t *out)
{
    kst_pio_capture(&clk, &run, m.rx, m.n_rx,
                    m.dropped != 0u || m.n_rx >= KST_PIO_RX_WORDS, out);
}

/* The frame on the pad: every half-cell @p half cycles, every level as the
 * frame gives it, the edges on the grid. */
static void check_frame_on_pad(const kst_frame_t *f, uint32_t half)
{
    uint64_t first = 0;
    bool found = false;

    for (unsigned i = 0; i < m.n_drive; ++i) {
        if (m.drive[i].dir && m.drive[i].latch) {
            first = m.drive[i].cyc;
            found = true;
            break;
        }
    }
    CHECK(found);
    /* The pad drives from the first cycle and is low until the frame. */
    CHECK_EQ(pad_at(&m, 0), 0);
    CHECK_EQ(pad_at(&m, first - 1u), 0);
    for (unsigned i = 0; i < f->n_half; ++i) {
        const int want = kst_frame_level(f, i) ? 1 : 0;

        CHECK_EQ(pad_at(&m, first + (uint64_t)i * half), want);
        CHECK_EQ(pad_at(&m, first + (uint64_t)i * half + half - 1u), want);
    }
    CHECK_EQ(m.e0, first + (uint64_t)f->n_half * half);
    for (unsigned i = 0; i < m.n_drive; ++i) {
        if (m.drive[i].cyc >= first && m.drive[i].cyc <= m.e0) {
            CHECK_EQ((m.drive[i].cyc - first) % half, 0u);
        }
        /* After the last edge the pad never drives high. */
        if (m.drive[i].cyc >= m.e0) {
            CHECK(!(m.drive[i].dir && m.drive[i].latch));
        }
    }
    /* The time the driver waits before it looks for the end: the stream's
     * 3 counts and its pulls take up to 8 cycles more. */
    CHECK_NEAR((double)run.lead_ns,
               (double)m.e0 * 1e9 / (double)clk.sys_hz,
               8e9 / (double)clk.sys_hz);
}

static void check_reply_edges(const kst_capture_t *sent,
                              const kst_capture_t *got)
{
    const uint32_t tick = kst_pio_tick_ns(&clk);
    /* The model moves the line on a cycle: half a cycle, and 8 ns for the
     * rounding of both conversions. */
    const uint32_t slack = 500000000u / clk.sys_hz + 8u;

    CHECK_EQ(got->overflow, 0);
    CHECK_EQ(got->level0, sent->level0);
    CHECK_EQ(got->n_edges, sent->n_edges);
    CHECK_EQ(got->window_ns, sent->window_ns);
    for (unsigned i = 0; i < sent->n_edges && i < got->n_edges; ++i) {
        /* Stamped at the first sample at or after the edge. */
        CHECK(got->edge_ns[i] + slack >= sent->edge_ns[i]);
        CHECK(got->edge_ns[i] <= sent->edge_ns[i] + tick + slack);
    }
}

/* ------------------------------------------------------------------ cases */

TEST_CASE(the_words_are_the_ones_the_firmware_build_checks)
{
    FILE *f = fopen(KST_LINE_WORDS, "r");
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
    CHECK_EQ(prog_len, 24u);
    CHECK_EQ(wrap_target, 0u);
    CHECK_EQ(wrap_at, 23u);
}

TEST_CASE(a_read_frame_is_27_bits_of_3810_cycles_and_the_pad_follows_the_rule)
{
    kst_frame_t f;
    kst_capture_t cap;
    kst_reply_t r;

    CHECK(kst_frame_read(0x0A, &f));
    CHECK(transact(150000000u, &f, KST_READ_WINDOW_NS, NULL,
                   KST_PIO_RX_WORDS));
    CHECK_EQ(clk.half_cycles, KST_HALF_CELL_CYCLES_150MHZ);
    CHECK_EQ(kst_pio_half_cell_ns(&clk), KST_HALF_CELL_NS);
    CHECK_EQ(kst_pio_tick_ns(&clk), 27u);
    check_frame_on_pad(&f, 3810u);
    /* Released 50 us after the last edge, to the cycle. */
    CHECK_EQ(m.release - m.e0, 7500u);
    CHECK_EQ(pad_at(&m, m.release - 1u), 0);
    CHECK_EQ(pad_at(&m, m.release), -1);
    CHECK_EQ(m.first_sample - m.release, KST_PIO_SAMPLE_LEAD);
    CHECK_EQ(m.bad_gaps, 0u);
    /* No reply: low again at the end of the window, not before it and
     * within 1 us after it. */
    CHECK(m.redrive - m.e0 >= 225000u);
    CHECK(m.redrive - m.e0 < 225150u);
    CHECK_EQ(pad_at(&m, m.redrive), 0);
    CHECK_EQ(m.n_rx, 0u);
    captured(&cap);
    CHECK_EQ(cap.n_edges, 0);
    CHECK_EQ(cap.window_ns, KST_READ_WINDOW_NS);
    CHECK_EQ(kst_reply_decode(&cap, KST_EXPECT_READ, &r), KST_RX_NO_REPLY);
}

TEST_CASE(a_write_frame_and_the_sync_burst_keep_the_same_half_cell)
{
    kst_frame_t f;

    CHECK(kst_frame_write(0x1D, 0xA5, &f));
    CHECK(transact(150000000u, &f, KST_WRITE_WINDOW_NS, NULL,
                   KST_PIO_RX_WORDS));
    check_frame_on_pad(&f, 3810u);
    CHECK_EQ(m.release - m.e0, 7500u);
    CHECK_EQ(m.bad_gaps, 0u);
    /* 8 ms of window. */
    CHECK(m.redrive - m.e0 >= 1200000u);
    CHECK(m.redrive - m.e0 < 1200150u);

    kst_frame_sync(&f);
    CHECK_EQ(f.n_half, KST_FRAME_MAX_HALF_CELLS);
    CHECK(transact(150000000u, &f, KST_READ_WINDOW_NS, NULL,
                   KST_PIO_RX_WORDS));
    /* 127 half-cells and the low one fill 4 words: no half-cell in front. */
    CHECK_EQ(m.n_tx, KST_PIO_TX_WORDS);
    check_frame_on_pad(&f, 3810u);
    CHECK_EQ(m.release - m.e0, 7500u);
}

TEST_CASE(a_reply_comes_back_edge_for_edge_and_decodes)
{
    static const uint8_t values[] = { 0x00, 0xFF, 0x55, 0xAA, 0x26, 0x80,
                                      0x01 };
    kst_frame_t f;

    CHECK(kst_frame_read(0x05, &f));
    for (unsigned i = 0; i < sizeof(values); ++i) {
        const sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS + i * 7u);
        kst_capture_t sent;
        kst_capture_t got;
        kst_reply_t r;
        uint64_t last_rise = 0;
        uint64_t last_change = 0;

        sim_reply(&sent, KST_READ_WINDOW_NS, 3, values[i], &shape);
        CHECK(transact(150000000u, &f, KST_READ_WINDOW_NS, &sent,
                       KST_PIO_RX_WORDS));
        CHECK_EQ(m.bad_gaps, 0u);
        CHECK_EQ(m.dropped, 0u);
        captured(&got);
        check_reply_edges(&sent, &got);
        CHECK_EQ(kst_reply_decode(&got, KST_EXPECT_READ, &r), KST_RX_OK);
        CHECK_EQ(r.value, values[i]);
        CHECK_NEAR((double)r.delay_ns, (double)shape.delay_ns, 40.0);
        CHECK_NEAR((double)r.half_cell_ns, (double)SIM_HALF_NS, 10.0);
        /* Low again 165 us after the last rising edge, which is 60 us or
         * more after the reply's last level change, and never high. */
        for (unsigned k = 0; k < m.n_ext; ++k) {
            last_change = m.ext[k].at;
            if (m.ext[k].level) {
                last_rise = m.ext[k].at;
            }
        }
        CHECK(m.redrive - m.e0 >= last_rise + 24750u);
        CHECK(m.redrive - m.e0 < last_rise + 24750u + 20u);
        CHECK(m.redrive - m.e0 >= last_change + 9000u);
        CHECK_EQ(pad_at(&m, m.redrive), 0);
    }
}

TEST_CASE(a_reply_to_a_write_and_a_sync_acknowledge_decode)
{
    kst_frame_t f;
    kst_capture_t sent;
    kst_capture_t got;
    kst_reply_t r;
    sim_shape_t shape = sim_shape(SIM_WRITE_DELAY_NS);

    CHECK(kst_frame_write(0x03, 0x14, &f));
    sim_reply(&sent, KST_WRITE_WINDOW_NS, 3, 0x14, &shape);
    CHECK(transact(150000000u, &f, KST_WRITE_WINDOW_NS, &sent,
                   KST_PIO_RX_WORDS));
    captured(&got);
    check_reply_edges(&sent, &got);
    CHECK_EQ(kst_reply_decode(&got, KST_EXPECT_WRITE, &r), KST_RX_OK);
    CHECK_EQ(r.value, 0x14);

    kst_frame_sync(&f);
    shape = sim_shape(SIM_READ_DELAY_NS);
    sim_reply(&sent, KST_READ_WINDOW_NS, 2, 0x00, &shape);
    CHECK(transact(150000000u, &f, KST_READ_WINDOW_NS, &sent,
                   KST_PIO_RX_WORDS));
    captured(&got);
    check_reply_edges(&sent, &got);
    CHECK_EQ(kst_reply_decode(&got, KST_EXPECT_SYNC, &r), KST_RX_OK);
}

TEST_CASE(an_edge_at_any_phase_of_the_sample_is_stamped_within_one_tick)
{
    kst_frame_t f;

    CHECK(kst_frame_read(0x01, &f));
    /* 150 MHz: 20 ns is 3 cycles, so 0 to 100 ns walks every phase. */
    for (uint32_t shift = 0; shift <= 100u; shift += 20u) {
        const sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS + shift);
        kst_capture_t sent;
        kst_capture_t got;

        sim_reply(&sent, KST_READ_WINDOW_NS, 3, 0x3C, &shape);
        CHECK(transact(150000000u, &f, KST_READ_WINDOW_NS, &sent,
                       KST_PIO_RX_WORDS));
        CHECK_EQ(m.bad_gaps, 0u);
        captured(&got);
        check_reply_edges(&sent, &got);
    }
}

TEST_CASE(a_glitch_of_1_us_is_two_edges_and_the_decoder_removes_it)
{
    kst_frame_t f;
    kst_capture_t sent;
    kst_capture_t got;
    kst_reply_t r;
    sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);

    CHECK(kst_frame_read(0x01, &f));
    /* Inside the low half of a cell. */
    shape.glitch_at_ns = shape.delay_ns + 3u * SIM_HALF_NS + 8000u;
    shape.glitch_ns = 1000u;
    sim_reply(&sent, KST_READ_WINDOW_NS, 3, 0xFF, &shape);
    CHECK(transact(150000000u, &f, KST_READ_WINDOW_NS, &sent,
                   KST_PIO_RX_WORDS));
    CHECK_EQ(m.bad_gaps, 0u);
    captured(&got);
    check_reply_edges(&sent, &got);
    CHECK_EQ(kst_reply_decode(&got, KST_EXPECT_READ, &r), KST_RX_OK);
    CHECK_EQ(r.value, 0xFF);
    CHECK_EQ(r.glitches, 1);
}

TEST_CASE(a_line_high_at_the_release_sets_the_start_level)
{
    kst_frame_t f;
    kst_capture_t sent;
    kst_capture_t got;
    kst_reply_t r;

    CHECK(kst_frame_read(0x01, &f));
    /* High before the release, low 100 us after the last edge. */
    sim_cap_clear(&sent, KST_READ_WINDOW_NS);
    sent.level0 = 1;
    sim_cap_edge(&sent, 100000u);
    CHECK(transact(150000000u, &f, KST_READ_WINDOW_NS, &sent,
                   KST_PIO_RX_WORDS));
    CHECK_EQ(m.n_rx, 2u);
    CHECK_EQ(m.rx[0], run.window_ticks);
    captured(&got);
    check_reply_edges(&sent, &got);
    CHECK_EQ(kst_reply_decode(&got, KST_EXPECT_READ, &r),
             KST_RX_START_LEVEL);
    /* Low again 165 us after the first sample, the only rise seen. */
    CHECK(m.redrive - m.e0 >= 7500u + 24750u);
    CHECK(m.redrive - m.e0 < 7500u + 24750u + 20u);
}

TEST_CASE(a_line_that_stays_high_is_driven_low_after_the_quiet_time)
{
    kst_frame_t f;
    kst_capture_t sent;
    kst_capture_t got;

    CHECK(kst_frame_read(0x01, &f));
    sim_cap_clear(&sent, KST_READ_WINDOW_NS);
    sim_cap_edge(&sent, 500000u);
    CHECK(transact(150000000u, &f, KST_READ_WINDOW_NS, &sent,
                   KST_PIO_RX_WORDS));
    CHECK_EQ(m.bad_gaps, 0u);
    captured(&got);
    CHECK_EQ(got.n_edges, 1);
    CHECK_NEAR((double)got.edge_ns[0], 500000.0, 35.0);
    CHECK(m.redrive - m.e0 >= 75000u + 24750u);
    CHECK(m.redrive - m.e0 < 75000u + 24750u + 20u);
    CHECK_EQ(pad_at(&m, m.redrive), 0);
    CHECK(m.finished);
}

TEST_CASE(an_edge_before_the_reply_ends_the_capture_early)
{
    kst_frame_t f;
    kst_capture_t sent;
    kst_capture_t got;
    kst_reply_t r;
    const sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);

    CHECK(kst_frame_read(0x01, &f));
    /* A 10 us pulse 100 us after the last edge: the quiet time runs out
     * 265 us after the last edge, before the reply at 438 us. */
    sim_reply(&sent, KST_READ_WINDOW_NS, 3, 0x12, &shape);
    sim_cap_edge(&sent, 100000u);
    sim_cap_edge(&sent, 110000u);
    CHECK(transact(150000000u, &f, KST_READ_WINDOW_NS, &sent,
                   KST_PIO_RX_WORDS));
    CHECK(m.redrive - m.e0 < 40000u);
    captured(&got);
    CHECK_EQ(got.n_edges, 2);
    CHECK(kst_reply_decode(&got, KST_EXPECT_READ, &r) != KST_RX_OK);
    /* The pad is low from there on, whatever the servo does. */
    CHECK_EQ(pad_at(&m, m.redrive + 100000u), 0);
}

TEST_CASE(more_edges_than_the_buffer_holds_is_an_overflow)
{
    kst_frame_t f;
    kst_capture_t sent;
    kst_capture_t got;
    kst_reply_t r;

    CHECK(kst_frame_read(0x01, &f));
    /* 48 edges fit. */
    sim_cap_clear(&sent, KST_READ_WINDOW_NS);
    for (unsigned i = 0; i < 48u; ++i) {
        sim_cap_edge(&sent, 200000u + i * 10000u);
    }
    CHECK(transact(150000000u, &f, KST_READ_WINDOW_NS, &sent,
                   KST_PIO_RX_WORDS));
    captured(&got);
    check_reply_edges(&sent, &got);

    /* 60 do not: the channel takes 50 stamps and the rest is dropped. */
    memset(&m, 0, sizeof(m));
    m.n_tx = kst_pio_tx(&clk, &f, KST_READ_WINDOW_NS, m.tx, &run);
    for (unsigned i = 0; i < 60u; ++i) {
        m.ext[i].at = ns_cycles(150000000u, 200000u + i * 10000u);
        m.ext[i].level = (i % 2u) == 0u;
    }
    m.n_ext = 60;
    m.rx_room = KST_PIO_RX_WORDS;
    run_model(&m, 2000000u);
    CHECK(m.finished);
    CHECK_EQ(m.n_rx, KST_PIO_RX_WORDS);
    CHECK_EQ(m.bad_gaps, 0u);
    captured(&got);
    CHECK_EQ(got.overflow, 1);
    CHECK_EQ(kst_reply_decode(&got, KST_EXPECT_READ, &r), KST_RX_OVERFLOW);

    /* 49 edges after a high start: 50 stamps, one edge too many. */
    {
        uint32_t stamps[KST_PIO_RX_WORDS];

        stamps[0] = run.window_ticks;
        for (unsigned i = 1; i < KST_PIO_RX_WORDS; ++i) {
            stamps[i] = clk.quiet_ticks - 10u;
        }
        kst_pio_capture(&clk, &run, stamps, KST_PIO_RX_WORDS - 1u, false,
                        &got);
        CHECK_EQ(got.overflow, 0);
        CHECK_EQ(got.level0, 1);
        CHECK_EQ(got.n_edges, 48);
        kst_pio_capture(&clk, &run, stamps, KST_PIO_RX_WORDS, false, &got);
        CHECK_EQ(got.overflow, 1);
        /* The driver's own mark is kept. */
        kst_pio_capture(&clk, &run, stamps, 3, true, &got);
        CHECK_EQ(got.overflow, 1);
        CHECK_EQ(got.n_edges, 2);
        kst_pio_capture(&clk, &run, stamps, 0, true, &got);
        CHECK_EQ(got.overflow, 1);
        CHECK_EQ(got.n_edges, 0);
    }
}

TEST_CASE(a_stamp_no_count_gives_is_an_overflow_and_not_a_time)
{
    kst_frame_t f;
    kst_capture_t got;
    uint32_t stamps[3];

    CHECK(kst_frame_read(0x01, &f));
    CHECK(kst_pio_clock(150000000u, &clk));
    CHECK(kst_pio_tx(&clk, &f, KST_READ_WINDOW_NS, m.tx, &run) != 0u);
    stamps[0] = run.window_ticks + 1u;
    kst_pio_capture(&clk, &run, stamps, 1, false, &got);
    CHECK_EQ(got.overflow, 1);
    CHECK_EQ(got.n_edges, 0);
    stamps[0] = run.window_ticks - 100u;
    stamps[1] = clk.quiet_ticks + 1u;
    kst_pio_capture(&clk, &run, stamps, 2, false, &got);
    CHECK_EQ(got.overflow, 1);
    CHECK_EQ(got.n_edges, 1);
    /* The limits themselves are times: a rise at the last sample of the
     * window, a fall at the sample after it, a rise at the last sample of
     * the quiet time. */
    stamps[0] = 0;
    stamps[1] = clk.quiet_ticks;
    stamps[2] = 0;
    kst_pio_capture(&clk, &run, stamps, 3, false, &got);
    CHECK_EQ(got.overflow, 0);
    CHECK_EQ(got.n_edges, 3);
    CHECK(got.edge_ns[0] <= KST_READ_WINDOW_NS);
    CHECK(got.edge_ns[0] + 60u > KST_READ_WINDOW_NS);
    CHECK_NEAR((double)(got.edge_ns[1] - got.edge_ns[0]), 26.7, 1.0);
    CHECK_NEAR((double)(got.edge_ns[2] - got.edge_ns[0]),
               (double)KST_PIO_QUIET_NS, 60.0);
}

static void check_clock_runs(uint32_t hz)
{
    kst_frame_t f;
    kst_capture_t sent;
    kst_capture_t got;
    kst_reply_t r;
    const sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);
    double half_ns;

    CHECK(kst_frame_read(0x11, &f));
    sim_reply(&sent, KST_READ_WINDOW_NS, 3, 0x96, &shape);
    CHECK(transact(hz, &f, KST_READ_WINDOW_NS, &sent, KST_PIO_RX_WORDS));
    check_frame_on_pad(&f, clk.half_cycles);
    half_ns = (double)clk.half_cycles * 1e9 / (double)hz;
    CHECK_NEAR(half_ns, 25400.0, 50.8);
    CHECK_NEAR((double)kst_pio_half_cell_ns(&clk), half_ns, 0.51);
    CHECK_NEAR((double)(m.release - m.e0) * 1e9 / (double)hz, 50000.0,
               1e9 / (double)hz);
    CHECK_EQ(m.bad_gaps, 0u);
    captured(&got);
    check_reply_edges(&sent, &got);
    CHECK_EQ(kst_reply_decode(&got, KST_EXPECT_READ, &r), KST_RX_OK);
    CHECK_EQ(r.value, 0x96);
}

TEST_CASE(other_system_clocks_give_the_half_cell_within_0_2_percent)
{
    check_clock_runs(125000000u);
    CHECK_EQ(clk.half_cycles, 3175u);
    check_clock_runs(133000000u);
    check_clock_runs(48000000u);
    check_clock_runs(12000000u);
    CHECK_EQ(kst_pio_tick_ns(&clk), 334u);
    check_clock_runs(200000000u);
}

TEST_CASE(a_clock_too_slow_or_off_the_half_cell_is_refused)
{
    kst_pio_clock_t c;

    memset(&c, 0x5A, sizeof(c));
    CHECK(!kst_pio_clock(0u, &c));
    CHECK(!kst_pio_clock(150000000u, NULL));
    /* A tick of 500 ns is 8 MHz. */
    CHECK(!kst_pio_clock(7999999u, &c));
    CHECK(!kst_pio_clock(5000000u, &c));
    CHECK(kst_pio_clock(8000000u, &c));
    CHECK_EQ(kst_pio_tick_ns(&c), 500u);
    /* 8.01 MHz gives 203 cycles, 25.343 us: 0.22 % short. */
    CHECK(!kst_pio_clock(8010000u, &c));
    /* 8.1 MHz gives 206 cycles, 25.432 us: 0.13 % long. */
    CHECK(kst_pio_clock(8100000u, &c));
    CHECK_EQ(c.half_cycles, 206u);
    /* A refused clock leaves the counts as they were. */
    CHECK(!kst_pio_clock(8010000u, &c));
    CHECK_EQ(c.half_cycles, 206u);
    /* From 9.9 MHz up every clock passes. */
    for (uint32_t hz = 9900000u; hz < 10100000u; hz += 997u) {
        CHECK(kst_pio_clock(hz, &c));
    }
    CHECK(kst_pio_clock(0xFFFFFFFFu, &c));
}

TEST_CASE(the_stream_ends_its_levels_on_a_word_boundary)
{
    static const uint8_t halves[] = { 1, 30, 31, 32, 62, 63, 64, 126, 127 };
    kst_frame_t f;
    uint32_t words[KST_PIO_TX_WORDS + 1u];

    CHECK(kst_pio_clock(150000000u, &clk));
    for (unsigned i = 0; i < sizeof(halves); ++i) {
        const unsigned total = halves[i] + 1u;
        const unsigned level_words = (total + 31u) / 32u;
        unsigned n;

        memset(&f, 0, sizeof(f));
        f.n_half = halves[i];
        memset(f.level, 0xFF, sizeof(f.level));
        words[KST_PIO_TX_WORDS] = 0xDEADBEEFu;
        n = kst_pio_tx(&clk, &f, KST_READ_WINDOW_NS, words, &run);
        CHECK_EQ(n, 5u + level_words);
        CHECK_EQ(words[KST_PIO_TX_WORDS], 0xDEADBEEFu);
        CHECK_EQ(words[0], 3806u);
        CHECK_EQ(words[1], level_words * 32u - 1u);
        /* The last level bit is the low half-cell. */
        CHECK_EQ(words[1u + level_words] & 1u, 0u);
        CHECK_EQ(words[1u + level_words] & 2u, 2u);
        CHECK_EQ(words[n - 3u], 7500u - 3810u - 2u);
        CHECK_EQ(words[n - 2u], run.window_ticks);
        CHECK_EQ(words[n - 1u], 6187u);
        CHECK_EQ(run.lead_ns,
                 (level_words * 32u - 1u) * KST_HALF_CELL_NS);
        /* It runs, with that many half-cells high in a row. */
        CHECK(transact(150000000u, &f, KST_READ_WINDOW_NS, NULL,
                       KST_PIO_RX_WORDS));
        check_frame_on_pad(&f, 3810u);
    }
}

TEST_CASE(a_frame_or_a_window_the_program_cannot_run_gives_no_stream)
{
    kst_frame_t f;
    uint32_t words[KST_PIO_TX_WORDS];

    CHECK(kst_pio_clock(150000000u, &clk));
    CHECK(kst_frame_read(0x01, &f));
    CHECK_EQ(kst_pio_tx(NULL, &f, KST_READ_WINDOW_NS, words, &run), 0u);
    CHECK_EQ(kst_pio_tx(&clk, NULL, KST_READ_WINDOW_NS, words, &run), 0u);
    CHECK_EQ(kst_pio_tx(&clk, &f, KST_READ_WINDOW_NS, NULL, &run), 0u);
    CHECK_EQ(kst_pio_tx(&clk, &f, KST_READ_WINDOW_NS, words, NULL), 0u);
    /* A window that ends at the first sample has no sample in it. */
    CHECK_EQ(kst_pio_tx(&clk, &f, KST_RELEASE_NS, words, &run), 0u);
    CHECK_EQ(kst_pio_tx(&clk, &f, 0u, words, &run), 0u);
    CHECK(kst_pio_tx(&clk, &f, KST_RELEASE_NS + 100u, words, &run) != 0u);
    CHECK(run.window_ticks <= 3u);
    f.n_half = 0;
    CHECK_EQ(kst_pio_tx(&clk, &f, KST_READ_WINDOW_NS, words, &run), 0u);
    f.n_half = KST_FRAME_MAX_HALF_CELLS + 1u;
    CHECK_EQ(kst_pio_tx(&clk, &f, KST_READ_WINDOW_NS, words, &run), 0u);
}

TEST_CASE(a_stalled_channel_drops_stamps_and_the_pad_still_ends_low)
{
    kst_frame_t f;
    kst_capture_t sent;
    kst_capture_t got;
    const sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);

    CHECK(kst_frame_read(0x01, &f));
    sim_reply(&sent, KST_READ_WINDOW_NS, 3, 0x55, &shape);
    /* The channel takes 3 stamps; the FIFO holds 4 more. */
    CHECK(kst_pio_clock(150000000u, &clk));
    memset(&m, 0, sizeof(m));
    m.n_tx = kst_pio_tx(&clk, &f, KST_READ_WINDOW_NS, m.tx, &run);
    for (unsigned i = 0; i < sent.n_edges; ++i) {
        m.ext[i].at = ns_cycles(150000000u, sent.edge_ns[i]);
        m.ext[i].level = (i % 2u) == 0u;
    }
    m.n_ext = sent.n_edges;
    m.rx_room = 3;
    run_model(&m, 1000000u);
    CHECK_EQ(m.n_rx, 3u);
    CHECK(m.dropped > 0u);
    CHECK_EQ(m.bad_gaps, 0u);
    CHECK(m.redrive != 0u);
    CHECK_EQ(pad_at(&m, m.redrive), 0);
    captured(&got);
    CHECK_EQ(got.overflow, 1);
}

int main(void)
{
    if (!assemble(KST_LINE_PIO)) {
        printf("FAIL  cannot assemble %s\n", KST_LINE_PIO);
        return 1;
    }
    RUN(the_words_are_the_ones_the_firmware_build_checks);
    RUN(a_read_frame_is_27_bits_of_3810_cycles_and_the_pad_follows_the_rule);
    RUN(a_write_frame_and_the_sync_burst_keep_the_same_half_cell);
    RUN(a_reply_comes_back_edge_for_edge_and_decodes);
    RUN(a_reply_to_a_write_and_a_sync_acknowledge_decode);
    RUN(an_edge_at_any_phase_of_the_sample_is_stamped_within_one_tick);
    RUN(a_glitch_of_1_us_is_two_edges_and_the_decoder_removes_it);
    RUN(a_line_high_at_the_release_sets_the_start_level);
    RUN(a_line_that_stays_high_is_driven_low_after_the_quiet_time);
    RUN(an_edge_before_the_reply_ends_the_capture_early);
    RUN(more_edges_than_the_buffer_holds_is_an_overflow);
    RUN(a_stamp_no_count_gives_is_an_overflow_and_not_a_time);
    RUN(other_system_clocks_give_the_half_cell_within_0_2_percent);
    RUN(a_clock_too_slow_or_off_the_half_cell_is_refused);
    RUN(the_stream_ends_its_levels_on_a_word_boundary);
    RUN(a_frame_or_a_window_the_program_cannot_run_gives_no_stream);
    RUN(a_stalled_channel_drops_stamps_and_the_pad_still_ends_low);
    return test_summary("kst_pio");
}
