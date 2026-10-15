/*
 * The S.BUS line's PIO (programmable input/output) program, run on the host
 * (protocols/sbus/rp2350/sbus_line.pio), with the arithmetic that feeds it
 * and reads it (protocols/sbus/sbus_pio.c).
 *
 * The program is assembled here from its source by a small assembler for
 * the instructions it uses, and run by a cycle-counting model of the state
 * machine: JMP always, on !X, X--, Y-- and the pin; OUT to the pins and X
 * with autopull at 32 bits, shifting right; IN from the pins and X,
 * shifting right; PUSH noblock; SET of the pins, the pin directions and Y;
 * delays; the wrap.  A DMA (direct memory access) channel fills the 4-word
 * transmit FIFO and another drains the 4-word receive FIFO into a buffer of
 * a given length, one word a cycle each.  The assembled words are held to
 * fixtures/sbus_line_words.txt, which the firmware build holds pioasm's
 * output to: the model runs what the chip runs.
 *
 * The model sees the line as the program does, through the inverting
 * overrides: idle 1, start bit 0.  While the pad drives, the line is its
 * latch.  While it does not, the line is the sensors' UART (universal
 * asynchronous receiver-transmitter) bytes, timed in ns on their own bit
 * time, and 1 between them, as the pull-down holds the wire low.  The pin
 * is read 13 ns late, 2 system clocks of the input synchroniser at
 * 150 MHz.
 *
 * Under test:
 *  - the frame: 25 bytes of 12 bits, 8 cycles a bit, behind 20 idle bits,
 *    at 150 MHz each bit 10.000 us; the pad never drives a 0 after it;
 *  - the pad: released 2 cycles after the last stop bit, first sample 1
 *    cycle later, samples 2 cycles apart while idle, 8 cycles apart in a
 *    byte; driven again 5 + 2 x window cycles after the last stop bit, 92
 *    more for every byte;
 *  - the bytes: 8 slots of 3 bytes at 660 and 700 us spacing, at every
 *    phase of the sample and with the sensor's bit time 2 % off either
 *    way, come back with their data, parity, stop bit and a start time
 *    within one cycle, and decode to 8 taken slots;
 *  - a bad parity bit and a bad stop bit, back-to-back bytes, a byte at the
 *    end of the window, a stalled receive channel, a stamp no sequence of
 *    samples gives;
 *  - the clock: 150, 125, 133, 48 and 12 MHz exact, every clock from
 *    1.5625 MHz up within 0.1 %, refused below 800 kHz and off by more;
 *  - the stream's layout for 1, 8 and 25 bytes and the arguments it
 *    refuses.
 *
 * Not tested here, because the host has no PIO: that the chip's input
 * synchroniser, autopull, JMP PIN, delay, overrides and DMA handshakes
 * behave as this model says; the pad, the pull-down and the sensors' edges.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "sbus.h"
#include "sbus2.h"
#include "sbus_pio.h"

#define MAX_PROG 32u

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
    if (!strcmp(t, "!x")) { return 1; }
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
        } else if (n == 3u && (!strcmp(tok[0], "out")
                               || !strcmp(tok[0], "in"))) {
            const int reg = reg_code(tok[1]);
            const unsigned bits = (unsigned)atoi(tok[2]);

            if (reg < 0 || reg > 1 || bits == 0u || bits > 32u) {
                return false;
            }
            w = (tok[0][0] == 'o' ? 0x6000u : 0x4000u)
                | ((unsigned)reg << 5) | (bits & 31u);
        } else if (n == 3u && !strcmp(tok[0], "set")) {
            const int dest = reg_code(tok[1]);

            if (dest != 0 && dest != 2 && dest != 4) {
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

#define SYNC_NS   13u
#define MAX_EXT   64u
#define MAX_RX    64u
#define MAX_DRIVE 800u
#define MAX_IN    400u

/* One byte a sensor sends: start bit at at_ns after the last stop bit. */
typedef struct {
    uint64_t at_ns;
    uint8_t data;
    uint32_t bit_ps;      /* its bit time */
    bool bad_parity;
    bool bad_stop;        /* the first stop bit at the start level */
} ext_t;

typedef struct {
    uint64_t cyc;
    bool dir;
    bool latch;
} drive_t;

typedef struct {
    /* what the test gives */
    uint32_t tx[SBUS_PIO_TX_WORDS];
    unsigned n_tx;
    ext_t ext[MAX_EXT];
    unsigned n_ext;
    unsigned rx_room;
    uint32_t cycle_ps;
    /* what the run leaves */
    drive_t drive[MAX_DRIVE];
    unsigned n_drive;
    uint32_t rx[MAX_RX];
    unsigned n_rx;
    unsigned dropped;
    uint64_t e;              /* end of the last stop bit sent */
    uint64_t release;
    uint64_t redrive;
    uint64_t first_sample;
    uint64_t idle_samples;
    uint64_t bad_gaps;       /* idle samples not 2 cycles after the last */
    uint64_t in_cyc[MAX_IN]; /* every `in pins` */
    unsigned n_in;
    uint64_t collisions;     /* cycles the pad drove over a sensor's 0 */
    bool finished;
} model_t;

/* The level a sensor puts on the line at @p ns after the last stop bit:
 * 0 or 1 inside a byte, -1 outside every byte. */
static int ext_level(const model_t *m, int64_t ns)
{
    for (unsigned i = 0; i < m->n_ext; ++i) {
        const ext_t *b = &m->ext[i];
        const int64_t into_ps = (ns - (int64_t)b->at_ns) * 1000;

        if (into_ps < 0 || into_ps >= (int64_t)b->bit_ps * 12) {
            continue;
        }
        const unsigned k = (unsigned)(into_ps / b->bit_ps);
        const unsigned p = sbus_pio_parity(b->data) ^ (b->bad_parity ? 1u : 0u);

        if (k == 0u) {
            return 0;
        }
        if (k <= 8u) {
            return (b->data >> (k - 1u)) & 1u;
        }
        if (k == 9u) {
            return (int)p;
        }
        if (k == 10u) {
            return b->bad_stop ? 0 : 1;
        }
        return 1;
    }
    return -1;
}

static uint64_t cyc_ns(const model_t *m, uint64_t cyc)
{
    return cyc * m->cycle_ps / 1000u;
}

/* The line as the program reads it at cycle @p cyc. */
static bool line_at(const model_t *m, uint64_t cyc, bool dir, bool latch)
{
    if (dir || m->e == 0u) {
        return latch;
    }
    const int64_t ns = (int64_t)cyc_ns(m, cyc) - SYNC_NS
                       - (int64_t)cyc_ns(m, m->e);
    const int v = ext_level(m, ns);

    return v != 0;
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
    unsigned pc = 1;              /* stalled on the first `out`, as between
                                   * transactions */
    unsigned delay = 0;
    bool dir = true;              /* as the driver leaves it */
    bool latch = true;
    uint64_t last_idle = 0;

    m->n_drive = 0;
    m->n_rx = 0;
    m->dropped = 0;
    m->e = 0;
    m->release = 0;
    m->redrive = 0;
    m->first_sample = 0;
    m->idle_samples = 0;
    m->bad_gaps = 0;
    m->n_in = 0;
    m->collisions = 0;
    m->finished = false;
    m->drive[m->n_drive].cyc = 0;
    m->drive[m->n_drive].dir = dir;
    m->drive[m->n_drive].latch = latch;
    m->n_drive++;

    for (uint64_t cyc = 0; cyc < max_cycles; ++cyc) {
        const bool was_dir = dir;
        const bool was_latch = latch;
        const bool pin = line_at(m, cyc, dir, latch);

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
            bool stalled = false;

            switch (w >> 13) {
            case 0: {                                           /* JMP */
                bool take = true;

                if (sel == 1u) {
                    take = x == 0u;
                } else if (sel == 2u) {
                    take = x != 0u;
                    --x;
                } else if (sel == 4u) {
                    take = y != 0u;
                    --y;
                } else if (sel == 6u) {
                    take = pin;
                    if (m->idle_samples == 0u) {
                        m->first_sample = cyc;
                    } else if (cyc - last_idle != SBUS_PIO_SAMPLE_CYCLES) {
                        m->bad_gaps++;
                    }
                    last_idle = cyc;
                    m->idle_samples++;
                    if (!take) {
                        /* A byte: the next sample is 94 cycles on; count
                         * the gap from there. */
                        last_idle = cyc + SBUS_PIO_BYTE_CYCLES
                                    - SBUS_PIO_SAMPLE_CYCLES;
                    }
                }
                if (sel == 2u && !take && pc == 3u) {
                    /* The last bit's loop test: its 8 cycles end next. */
                    m->e = cyc + 1u;
                }
                if (take) {
                    next = arg;
                }
                break;
            }
            case 2: {                                           /* IN */
                const unsigned bits = arg == 0u ? 32u : arg;
                const uint32_t mask = bits == 32u ? ~0u : (1u << bits) - 1u;
                const uint32_t data = (sel == 0u ? (pin ? 1u : 0u) : x)
                                      & mask;

                isr = bits == 32u ? data
                                  : (isr >> bits) | (data << (32u - bits));
                if (sel == 0u && m->n_in < MAX_IN) {
                    m->in_cyc[m->n_in++] = cyc;
                }
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
                    } else if (pc == 1u && tx_at == m->n_tx && m->e != 0u) {
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
                if (sel == 0u) {
                    latch = (data & 1u) != 0u;
                } else {
                    x = data;
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
            default:                                            /* SET */
                if (sel == 0u) {
                    latch = (arg & 1u) != 0u;
                } else if (sel == 2u) {
                    y = arg;
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

        if (was_dir && !dir) {
            m->release = cyc;
        }
        if (!was_dir && dir && m->release != 0u && m->redrive == 0u) {
            m->redrive = cyc;
        }
        if ((dir != was_dir || latch != was_latch) && m->n_drive < MAX_DRIVE) {
            m->drive[m->n_drive].cyc = cyc;
            m->drive[m->n_drive].dir = dir;
            m->drive[m->n_drive].latch = latch;
            m->n_drive++;
        }
        if (dir && m->e != 0u) {
            const int64_t ns = (int64_t)cyc_ns(m, cyc)
                               - (int64_t)cyc_ns(m, m->e);

            if (ext_level(m, ns) == 0) {
                m->collisions++;
            }
        }
        if (m->finished && n_rxf == 0u) {
            return;
        }
    }
}

/* The pad at @p cyc: 1 driven to 1, 0 driven to 0, -1 an input. */
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
static sbus_pio_clock_t clk;
static sbus_pio_run_t run;

/* A frame of 16 channels at mid travel with footer @p footer. */
static void frame_bytes(uint8_t footer, uint8_t *out)
{
    sbus_frame_t f;

    memset(&f, 0, sizeof(f));
    for (unsigned i = 0; i < SBUS_CHANNELS; ++i) {
        f.channel[i] = (uint16_t)(SBUS_RAW_1000US + 100u * i);
    }
    sbus_encode(&f, footer, out);
}

/* Lay out the frame and a window for @p hz; the sensors' bytes go in
 * m.ext before the call. */
static bool transact(uint32_t hz, const uint8_t *bytes, unsigned n,
                     uint32_t window_us, unsigned rx_room)
{
    if (!sbus_pio_clock(hz, &clk)) {
        return false;
    }
    m.n_tx = sbus_pio_tx(&clk, bytes, n, window_us, m.tx, &run);
    if (m.n_tx == 0u) {
        return false;
    }
    m.cycle_ps = clk.cycle_ps;
    m.rx_room = rx_room;
    run_model(&m, (uint64_t)n * 100u + 400u + 2u * run.window_samples
                  + 94u * MAX_EXT + 1000u);
    return m.finished;
}

static void captured(sbus2_capture_t *out)
{
    sbus_pio_capture(&clk, &run, m.rx, m.n_rx,
                     m.dropped != 0u || m.n_rx >= SBUS_PIO_RX_WORDS, out);
}

/* The 3 bytes of slot @p slot's record, the first at @p at_ns. */
static void add_record(unsigned slot, uint8_t d0, uint8_t d1, uint64_t at_ns,
                       uint32_t bit_ps)
{
    const uint8_t b[3] = { sbus2_slot_id(slot), d0, d1 };

    for (unsigned k = 0; k < 3u; ++k) {
        ext_t *e = &m.ext[m.n_ext++];

        memset(e, 0, sizeof(*e));
        e->at_ns = at_ns + (uint64_t)k * 12u * bit_ps / 1000u;
        e->data = b[k];
        e->bit_ps = bit_ps;
    }
}

static void check_frame_on_pad(const uint8_t *bytes, unsigned n)
{
    const unsigned frame_bits = n * SBUS_PIO_BYTE_BITS;
    const unsigned lead = (32u - frame_bits % 32u) % 32u;
    const uint64_t first = 2u + (uint64_t)lead * SBUS_PIO_BIT_CYCLES;

    /* Driving its idle 1 from the start to the first start bit. */
    CHECK_EQ(pad_at(&m, 0), 1);
    CHECK_EQ(pad_at(&m, first - 1u), 1);
    for (unsigned b = 0; b < n; ++b) {
        const uint32_t cell = ((uint32_t)bytes[b] << 1)
                              | ((uint32_t)sbus_pio_parity(bytes[b]) << 9)
                              | (3u << 10);

        for (unsigned k = 0; k < SBUS_PIO_BYTE_BITS; ++k) {
            const uint64_t at = first + (uint64_t)(b * SBUS_PIO_BYTE_BITS + k)
                                        * SBUS_PIO_BIT_CYCLES;
            const int want = (int)((cell >> k) & 1u);

            CHECK_EQ(pad_at(&m, at), want);
            CHECK_EQ(pad_at(&m, at + SBUS_PIO_BIT_CYCLES - 1u), want);
        }
    }
    CHECK_EQ(m.e, first + (uint64_t)frame_bits * SBUS_PIO_BIT_CYCLES);
    CHECK_EQ(run.lead_ns, (uint32_t)((m.e * clk.cycle_ps + 500u) / 1000u));
    /* After the frame the pad never drives a 0. */
    for (unsigned i = 0; i < m.n_drive; ++i) {
        if (m.drive[i].cyc >= m.e) {
            CHECK(!(m.drive[i].dir && !m.drive[i].latch));
        }
    }
}

/* Every byte the sensors sent, in order, with its data, its bits and its
 * start within one cycle and the synchroniser's 13 ns. */
static void check_bytes(const sbus2_capture_t *cap)
{
    const int64_t slack = (int64_t)(clk.cycle_ps / 1000u) + SYNC_NS + 2;

    CHECK_EQ(cap->overflow, 0);
    CHECK_EQ(cap->n, m.n_ext);
    for (unsigned i = 0; i < m.n_ext && i < cap->n; ++i) {
        const int64_t err = (int64_t)cap->byte[i].t_ns
                            - (int64_t)m.ext[i].at_ns;

        CHECK_EQ(cap->byte[i].data, m.ext[i].data);
        CHECK_EQ(cap->byte[i].parity_ok, !m.ext[i].bad_parity);
        CHECK_EQ(cap->byte[i].stop_ok, !m.ext[i].bad_stop);
        CHECK(err <= slack);
        CHECK(err >= -slack);
    }
}

/* ------------------------------------------------------------------ cases */

TEST_CASE(the_words_are_the_ones_the_firmware_build_checks)
{
    FILE *f = fopen(SBUS_LINE_WORDS, "r");
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
    CHECK_EQ(prog_len, 14u);
    CHECK_EQ(wrap_target, 0u);
    CHECK_EQ(wrap_at, 13u);
}

TEST_CASE(an_sbus_frame_is_300_bits_of_8_cycles_and_keeps_the_pad)
{
    uint8_t bytes[SBUS_FRAME_BYTES];
    sbus2_capture_t cap;

    memset(&m, 0, sizeof(m));
    frame_bytes(SBUS_FOOTER, bytes);
    CHECK(transact(150000000u, bytes, SBUS_FRAME_BYTES, 0u, SBUS_PIO_RX_WORDS));
    CHECK_EQ(clk.div_int, 187u);
    CHECK_EQ(clk.div_frac, 128u);
    CHECK_EQ(clk.cycle_ps, 1250000u);
    check_frame_on_pad(bytes, SBUS_FRAME_BYTES);
    /* 2 cycles to the first idle bit, 20 idle bits, 300 frame bits:
     * 3,202.5 us. */
    CHECK_EQ(run.lead_ns, 3202500u);
    /* No window: never released, no sample. */
    CHECK_EQ(m.release, 0u);
    CHECK_EQ(m.idle_samples, 0u);
    CHECK_EQ(m.n_rx, 0u);
    captured(&cap);
    CHECK_EQ(cap.n, 0);
    CHECK_EQ(cap.overflow, 0);
}

TEST_CASE(an_empty_window_releases_samples_every_2_cycles_and_redrives)
{
    uint8_t bytes[SBUS_FRAME_BYTES];
    sbus2_capture_t cap;

    memset(&m, 0, sizeof(m));
    frame_bytes(SBUS2_FOOTER_0, bytes);
    CHECK(transact(150000000u, bytes, SBUS_FRAME_BYTES, SBUS2_WINDOW_US,
                   SBUS_PIO_RX_WORDS));
    check_frame_on_pad(bytes, SBUS_FRAME_BYTES);
    /* 8000 us of 2.5 us samples. */
    CHECK_EQ(run.window_samples, 3200u);
    CHECK_EQ(m.release - m.e, SBUS_PIO_RELEASE_CYCLES);
    CHECK_EQ(pad_at(&m, m.release - 1u), 1);
    CHECK_EQ(pad_at(&m, m.release), -1);
    CHECK_EQ(m.first_sample - m.e, SBUS_PIO_FIRST_SAMPLE);
    CHECK_EQ(m.idle_samples, run.window_samples + 1u);
    CHECK_EQ(m.bad_gaps, 0u);
    CHECK_EQ(m.redrive - m.e, 5u + 2u * run.window_samples);
    CHECK_EQ(pad_at(&m, m.redrive), 1);
    CHECK_EQ(m.collisions, 0u);
    CHECK_EQ(m.n_rx, 0u);
    captured(&cap);
    CHECK_EQ(cap.n, 0);
    CHECK_EQ(cap.overflow, 0);
}

TEST_CASE(eight_slots_at_660_and_700_us_come_back_at_every_phase)
{
    static const uint32_t spacing[2] = { 660u, 700u };
    static const uint32_t bit_ps[3] = { 9800000u, 10000000u, 10200000u };
    uint8_t bytes[SBUS_FRAME_BYTES];

    frame_bytes(SBUS2_FOOTER_2, bytes);
    for (unsigned s = 0; s < 2u; ++s) {
        for (unsigned b = 0; b < 3u; ++b) {
            for (unsigned ph = 0; ph < 10u; ++ph) {
                sbus2_capture_t cap;
                sbus2_window_t win;

                memset(&m, 0, sizeof(m));
                for (unsigned k = 0; k < SBUS2_GROUP_SLOTS; ++k) {
                    add_record(16u + k, (uint8_t)(0x10u * k + ph),
                               (uint8_t)(0xA5u ^ k),
                               (uint64_t)(SBUS2_SLOT0_US + spacing[s] * k)
                               * 1000u + 125u * ph + 7u * k,
                               bit_ps[b]);
                }
                CHECK(transact(150000000u, bytes, SBUS_FRAME_BYTES,
                               SBUS2_WINDOW_US, SBUS_PIO_RX_WORDS));
                CHECK_EQ(m.n_rx, 24u);
                CHECK_EQ(m.bad_gaps, 0u);
                CHECK_EQ(m.collisions, 0u);
                CHECK_EQ(m.n_in, 240u);
                /* Within a byte, 10 samples 8 cycles apart. */
                for (unsigned i = 0; i + 1u < m.n_in; ++i) {
                    if (i % 10u != 9u) {
                        CHECK_EQ(m.in_cyc[i + 1u] - m.in_cyc[i], 8u);
                    }
                }
                CHECK_EQ(m.redrive - m.e, 5u + 2u * run.window_samples
                                          + 92u * 24u);
                captured(&cap);
                check_bytes(&cap);
                CHECK(sbus2_window_decode(NULL, SBUS2_FOOTER_2, &cap, &win));
                CHECK_EQ(win.taken, 8u);
                CHECK_EQ(win.group, 2u);
                for (unsigned k = 0; k < SBUS2_GROUP_SLOTS; ++k) {
                    CHECK(win.slot[k].taken);
                    CHECK_EQ(win.slot[k].data[0], 0x10u * k + ph);
                    CHECK_EQ(win.slot[k].data[1], 0xA5u ^ k);
                }
            }
        }
    }
}

TEST_CASE(other_system_clocks_read_the_same_slots)
{
    static const uint32_t hz[4] = { 125000000u, 133000000u, 48000000u,
                                    12000000u };
    uint8_t bytes[SBUS_FRAME_BYTES];

    frame_bytes(SBUS2_FOOTER_1, bytes);
    for (unsigned i = 0; i < 4u; ++i) {
        sbus2_capture_t cap;
        sbus2_window_t win;

        memset(&m, 0, sizeof(m));
        for (unsigned k = 0; k < SBUS2_GROUP_SLOTS; ++k) {
            add_record(8u + k, (uint8_t)k, (uint8_t)(0xF0u + k),
                       (uint64_t)(SBUS2_SLOT0_US + SBUS2_SLOT_US * k) * 1000u
                       + 333u * k, 10000000u);
        }
        CHECK(transact(hz[i], bytes, SBUS_FRAME_BYTES, SBUS2_WINDOW_US,
                       SBUS_PIO_RX_WORDS));
        CHECK_EQ(clk.cycle_ps, 1250000u);
        check_frame_on_pad(bytes, SBUS_FRAME_BYTES);
        captured(&cap);
        check_bytes(&cap);
        CHECK(sbus2_window_decode(NULL, SBUS2_FOOTER_1, &cap, &win));
        CHECK_EQ(win.taken, 8u);
    }
}

TEST_CASE(a_bad_parity_or_stop_bit_is_marked_on_its_byte)
{
    uint8_t bytes[SBUS_FRAME_BYTES];
    sbus2_capture_t cap;
    sbus2_window_t win;

    memset(&m, 0, sizeof(m));
    frame_bytes(SBUS2_FOOTER_0, bytes);
    add_record(0u, 0x00u, 0x33u, 2000000u, 10000000u);
    add_record(1u, 0x81u, 0x80u, 2660000u, 10000000u);
    add_record(2u, 0x12u, 0x34u, 3320000u, 10000000u);
    m.ext[1].bad_parity = true;
    m.ext[5].bad_stop = true;
    CHECK(transact(150000000u, bytes, SBUS_FRAME_BYTES, SBUS2_WINDOW_US,
                   SBUS_PIO_RX_WORDS));
    captured(&cap);
    check_bytes(&cap);
    CHECK(sbus2_window_decode(NULL, SBUS2_FOOTER_0, &cap, &win));
    CHECK_EQ(win.bad_byte, 2u);
    CHECK_EQ(win.taken, 1u);
    CHECK(win.slot[2].taken);
}

TEST_CASE(back_to_back_bytes_with_a_fast_sensor_are_all_read)
{
    uint8_t bytes[SBUS_FRAME_BYTES];
    sbus2_capture_t cap;

    /* 24 bytes with no gap at a bit time 2 % short: each starts 117.6 us
     * after the last, 0.1 us after the program samples again. */
    frame_bytes(SBUS2_FOOTER_3, bytes);
    for (unsigned ph = 0; ph < 4u; ++ph) {
        memset(&m, 0, sizeof(m));
        for (unsigned i = 0; i < 24u; ++i) {
            ext_t *e = &m.ext[m.n_ext++];

            e->at_ns = 2000000u + 400u * ph + (uint64_t)i * 117600u;
            e->data = (uint8_t)(0x5Au + 37u * i);
            e->bit_ps = 9800000u;
        }
        CHECK(transact(150000000u, bytes, SBUS_FRAME_BYTES, SBUS2_WINDOW_US,
                       SBUS_PIO_RX_WORDS));
        CHECK_EQ(m.collisions, 0u);
        captured(&cap);
        check_bytes(&cap);
    }
}

TEST_CASE(a_byte_at_the_end_of_the_window_is_read_whole)
{
    uint8_t bytes[SBUS_FRAME_BYTES];
    sbus2_capture_t cap;

    /* The last idle sample is at 3 + 2 x 3200 cycles, 8003.75 us; a byte
     * that starts just before it is read to its first stop bit, and the
     * pad drives again in its second stop bit, at the level the pad drives. */
    memset(&m, 0, sizeof(m));
    frame_bytes(SBUS2_FOOTER_0, bytes);
    m.ext[0].at_ns = 8002000u;
    m.ext[0].data = 0xC3u;
    m.ext[0].bit_ps = 10000000u;
    m.n_ext = 1;
    CHECK(transact(150000000u, bytes, SBUS_FRAME_BYTES, SBUS2_WINDOW_US,
                   SBUS_PIO_RX_WORDS));
    captured(&cap);
    check_bytes(&cap);
    CHECK_EQ(m.collisions, 0u);
    CHECK(cyc_ns(&m, m.redrive - m.e) >= 8002000u + 110000u);
    CHECK_EQ(m.redrive - m.e, 5u + 2u * run.window_samples + 92u);

    /* One that starts after it is not read, and the pad drives over its
     * start bit. */
    memset(&m, 0, sizeof(m));
    m.ext[0].at_ns = 8006000u;
    m.ext[0].data = 0xC3u;
    m.ext[0].bit_ps = 10000000u;
    m.n_ext = 1;
    CHECK(transact(150000000u, bytes, SBUS_FRAME_BYTES, SBUS2_WINDOW_US,
                   SBUS_PIO_RX_WORDS));
    CHECK_EQ(m.n_rx, 0u);
    CHECK(m.collisions > 0u);
}

TEST_CASE(a_stalled_receive_channel_drops_bytes_and_is_an_overflow)
{
    uint8_t bytes[SBUS_FRAME_BYTES];
    sbus2_capture_t cap;

    memset(&m, 0, sizeof(m));
    frame_bytes(SBUS2_FOOTER_0, bytes);
    for (unsigned k = 0; k < SBUS2_GROUP_SLOTS; ++k) {
        add_record(k, 1u, 2u, (uint64_t)(2000u + 660u * k) * 1000u,
                   10000000u);
    }
    CHECK(transact(150000000u, bytes, SBUS_FRAME_BYTES, SBUS2_WINDOW_US, 3u));
    CHECK_EQ(m.n_rx, 3u);
    CHECK(m.dropped > 0u);
    CHECK_EQ(pad_at(&m, m.redrive), 1);
    captured(&cap);
    CHECK_EQ(cap.overflow, 1);
    CHECK_EQ(cap.n, 3u);
}

TEST_CASE(the_capture_refuses_counters_no_samples_give)
{
    sbus2_capture_t cap;
    uint32_t w[SBUS2_CAPTURE_MAX + 1u];

    CHECK(sbus_pio_clock(150000000u, &clk));
    memset(&run, 0, sizeof(run));
    run.window_samples = 3200u;
    run.window_us = SBUS2_WINDOW_US;

    /* d = 800 samples before the first byte: sample 3 + 1600 cycles, the
     * edge 1 cycle before it, 1602 cycles: 2002.5 us. */
    w[0] = 0x6Bu | (1u << 9) | ((3200u - 800u) << 10);  /* parity bit 0 */
    w[1] = (1u << 9) | ((3200u - 801u) << 10);         /* data 0x00 */
    sbus_pio_capture(&clk, &run, w, 2u, false, &cap);
    CHECK_EQ(cap.overflow, 0);
    CHECK_EQ(cap.n, 2u);
    CHECK_EQ(cap.byte[0].t_ns, 2002500u);
    CHECK_EQ(cap.byte[0].data, 0x6Bu);
    CHECK_EQ(cap.byte[0].parity_ok, 0);    /* 0x6B has 5 ones */
    CHECK_EQ(cap.byte[0].stop_ok, 1);
    /* One sample and one byte later: 2 + 92 cycles. */
    CHECK_EQ(cap.byte[1].t_ns, 2002500u + 117500u);
    CHECK_EQ(cap.byte[1].parity_ok, 1);

    /* A counter above the window, then one that does not count down. */
    w[0] = 3201u << 10;
    sbus_pio_capture(&clk, &run, w, 1u, false, &cap);
    CHECK_EQ(cap.overflow, 1);
    CHECK_EQ(cap.n, 0);
    w[0] = 100u << 10;
    w[1] = 100u << 10;
    w[2] = 50u << 10;
    sbus_pio_capture(&clk, &run, w, 3u, false, &cap);
    CHECK_EQ(cap.overflow, 1);
    CHECK_EQ(cap.n, 1u);

    /* Lost words, too many words, no words. */
    w[0] = 100u << 10;
    sbus_pio_capture(&clk, &run, w, 1u, true, &cap);
    CHECK_EQ(cap.overflow, 1);
    CHECK_EQ(cap.n, 1u);
    for (unsigned i = 0; i <= SBUS2_CAPTURE_MAX; ++i) {
        w[i] = (3000u - i) << 10;
    }
    sbus_pio_capture(&clk, &run, w, SBUS2_CAPTURE_MAX + 1u, false, &cap);
    CHECK_EQ(cap.overflow, 1);
    CHECK_EQ(cap.n, SBUS2_CAPTURE_MAX);
    sbus_pio_capture(&clk, &run, NULL, 0u, false, &cap);
    CHECK_EQ(cap.overflow, 0);
    CHECK_EQ(cap.n, 0);
    sbus_pio_capture(&clk, &run, NULL, 1u, false, &cap);
    CHECK_EQ(cap.overflow, 1);
    sbus_pio_capture(NULL, &run, w, 1u, false, &cap);
    CHECK_EQ(cap.overflow, 1);
    sbus_pio_capture(&clk, NULL, w, 1u, false, &cap);
    CHECK_EQ(cap.overflow, 1);
    sbus_pio_capture(&clk, &run, w, 1u, false, NULL);
}

TEST_CASE(the_clock_is_exact_at_the_usual_rates_and_refused_off_them)
{
    static const struct {
        uint32_t hz;
        uint16_t i;
        uint8_t f;
    } exact[6] = {
        { 150000000u, 187u, 128u }, { 125000000u, 156u, 64u },
        { 133000000u, 166u, 64u },  { 48000000u, 60u, 0u },
        { 12000000u, 15u, 0u },     { 1562500u, 1u, 244u },
    };

    for (unsigned i = 0; i < 6u; ++i) {
        CHECK(sbus_pio_clock(exact[i].hz, &clk));
        CHECK_EQ(clk.div_int, exact[i].i);
        CHECK_EQ(clk.div_frac, exact[i].f);
        CHECK_EQ(clk.cycle_ps, 1250000u);
        CHECK_EQ(clk.sys_hz, exact[i].hz);
    }
    /* 800 kHz divides by 1. */
    CHECK(sbus_pio_clock(800000u, &clk));
    CHECK_EQ(clk.div_int, 1u);
    CHECK_EQ(clk.div_frac, 0u);
    /* Below it no divider; just above it the divider stays 1 and the bit
     * is 0.2 % short. */
    CHECK(!sbus_pio_clock(799999u, &clk));
    CHECK(!sbus_pio_clock(0u, &clk));
    CHECK(!sbus_pio_clock(801560u, &clk));
    CHECK(!sbus_pio_clock(150000000u, NULL));

    /* Every clock from 1.5625 MHz to 300 MHz is within 0.1 %. */
    for (uint32_t hz = 1562500u; hz < 300000000u; hz += 99991u) {
        const bool ok = sbus_pio_clock(hz, &clk);

        CHECK(ok);
        if (ok) {
            const int64_t err = (int64_t)clk.cycle_ps * 8 - 10000000;

            CHECK(err <= 10000);
            CHECK(err >= -10000);
        }
    }
    CHECK(sbus_pio_clock(4294967295u, &clk));
}

TEST_CASE(the_stream_ends_its_bits_on_a_word_boundary)
{
    static const struct {
        unsigned n;
        unsigned words;
        unsigned lead;
    } lay[3] = { { 1u, 3u, 20u }, { 8u, 5u, 0u }, { 25u, 12u, 20u } };
    uint8_t bytes[SBUS_PIO_MAX_BYTES];
    uint32_t w[SBUS_PIO_TX_WORDS];

    CHECK(sbus_pio_clock(150000000u, &clk));
    for (unsigned i = 0; i < SBUS_PIO_MAX_BYTES; ++i) {
        bytes[i] = (uint8_t)(0x0Fu + 29u * i);
    }
    for (unsigned c = 0; c < 3u; ++c) {
        const unsigned n = lay[c].n;
        const unsigned total = lay[c].lead + 12u * n;

        CHECK_EQ(sbus_pio_tx(&clk, bytes, n, 1000u, w, &run), lay[c].words);
        CHECK_EQ(total % 32u, 0u);
        CHECK_EQ(w[0], total - 1u);
        CHECK_EQ(w[lay[c].words - 1u], 400u);
        CHECK_EQ(run.window_samples, 400u);
        CHECK_EQ(run.window_us, 1000u);
        CHECK_EQ(run.lead_ns, 2500u + 10000u * total);
        for (unsigned k = 0; k < total; ++k) {
            const unsigned bit = (w[1u + k / 32u] >> (k % 32u)) & 1u;

            if (k < lay[c].lead) {
                CHECK_EQ(bit, 1u);
            } else {
                const unsigned b = (k - lay[c].lead) / 12u;
                const unsigned j = (k - lay[c].lead) % 12u;
                const uint32_t cell = ((uint32_t)bytes[b] << 1)
                                      | (sbus_pio_parity(bytes[b]) << 9)
                                      | (3u << 10);

                CHECK_EQ(bit, (cell >> j) & 1u);
            }
        }
    }
    /* A window that is not whole samples rounds up. */
    CHECK_EQ(sbus_pio_tx(&clk, bytes, 1u, 1u, w, &run), 3u);
    CHECK_EQ(run.window_samples, 1u);
    CHECK_EQ(sbus_pio_tx(&clk, bytes, 1u, 0u, w, &run), 3u);
    CHECK_EQ(w[2], 0u);
    /* The longest window: 2^22 - 1 samples, 10,485,757.5 us. */
    CHECK_EQ(sbus_pio_tx(&clk, bytes, 1u, 10485757u, w, &run), 3u);
    CHECK_EQ(run.window_samples, SBUS_PIO_MAX_SAMPLES);
    CHECK_EQ(sbus_pio_tx(&clk, bytes, 1u, 10485758u, w, &run), 0u);
}

TEST_CASE(a_stream_the_program_cannot_run_is_refused)
{
    uint8_t bytes[SBUS_PIO_MAX_BYTES + 1u];
    uint32_t w[SBUS_PIO_TX_WORDS];
    sbus_pio_clock_t zero;

    memset(bytes, 0, sizeof(bytes));
    memset(&zero, 0, sizeof(zero));
    CHECK(sbus_pio_clock(150000000u, &clk));
    CHECK_EQ(sbus_pio_tx(&clk, bytes, 0u, 0u, w, &run), 0u);
    CHECK_EQ(sbus_pio_tx(&clk, bytes, SBUS_PIO_MAX_BYTES + 1u, 0u, w, &run),
             0u);
    CHECK_EQ(sbus_pio_tx(&clk, NULL, 1u, 0u, w, &run), 0u);
    CHECK_EQ(sbus_pio_tx(&clk, bytes, 1u, 0u, NULL, &run), 0u);
    CHECK_EQ(sbus_pio_tx(&clk, bytes, 1u, 0u, w, NULL), 0u);
    CHECK_EQ(sbus_pio_tx(NULL, bytes, 1u, 0u, w, &run), 0u);
    CHECK_EQ(sbus_pio_tx(&zero, bytes, 1u, 0u, w, &run), 0u);
}

TEST_CASE(parity_is_even_over_the_8_data_bits)
{
    CHECK_EQ(sbus_pio_parity(0x00u), 0u);
    CHECK_EQ(sbus_pio_parity(0x01u), 1u);
    CHECK_EQ(sbus_pio_parity(0x0Fu), 0u);
    CHECK_EQ(sbus_pio_parity(0x07u), 1u);
    CHECK_EQ(sbus_pio_parity(0x80u), 1u);
    CHECK_EQ(sbus_pio_parity(0xFFu), 0u);
    CHECK_EQ(sbus_pio_parity(0xFEu), 1u);
}

int main(void)
{
    if (!assemble(SBUS_LINE_PIO)) {
        printf("FAIL  cannot assemble %s\n", SBUS_LINE_PIO);
        return 1;
    }
    RUN(the_words_are_the_ones_the_firmware_build_checks);
    RUN(an_sbus_frame_is_300_bits_of_8_cycles_and_keeps_the_pad);
    RUN(an_empty_window_releases_samples_every_2_cycles_and_redrives);
    RUN(eight_slots_at_660_and_700_us_come_back_at_every_phase);
    RUN(other_system_clocks_read_the_same_slots);
    RUN(a_bad_parity_or_stop_bit_is_marked_on_its_byte);
    RUN(back_to_back_bytes_with_a_fast_sensor_are_all_read);
    RUN(a_byte_at_the_end_of_the_window_is_read_whole);
    RUN(a_stalled_receive_channel_drops_bytes_and_is_an_overflow);
    RUN(the_capture_refuses_counters_no_samples_give);
    RUN(the_clock_is_exact_at_the_usual_rates_and_refused_off_them);
    RUN(the_stream_ends_its_bits_on_a_word_boundary);
    RUN(a_stream_the_program_cannot_run_is_refused);
    RUN(parity_is_even_over_the_8_data_bits);
    return test_summary("sbus_pio");
}
