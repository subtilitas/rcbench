/*
 * The EX Bus line's PIO (programmable input/output) program, run on the
 * host (protocols/exbus/rp2350/exbus_line.pio), with the arithmetic that
 * feeds it and reads it (protocols/exbus/exbus_pio.c).
 *
 * The program is assembled here from its source by a small assembler for
 * the instructions it uses, and run by a cycle-counting model of the state
 * machine: JMP always, on X--, Y-- and the pin; PULL block; OUT to the
 * pins; IN from the pins and X, shifting right; PUSH noblock; MOV from the
 * ISR and the OSR to X and the ISR; SET of the pins, the pin directions and
 * Y;
 * delays; the wrap.  A DMA (direct memory access) channel fills the 4-word
 * transmit FIFO and another drains the 4-word receive FIFO into a buffer of
 * a given length.  The pin is read two cycles after it moves, as the input
 * synchroniser delays it.  The assembled words are held to
 * fixtures/exbus_line_words.txt, which the firmware build holds pioasm's
 * output to: the model runs what the chip runs.
 *
 * The wire is the pad's latch while the pad drives, and the device's level
 * while it does not; a device that drives nothing leaves it high, as the
 * pull-up holds it.
 *
 * Under test:
 *  - the transmission: every bit of a channel packet and a request is 8
 *    cycles at the level 8N1 gives, least significant bit first, at both
 *    rates; a transmit channel that feeds slowly only lengthens stop bits;
 *  - the release: right after the last stop bit, for 2 * window + 3
 *    cycles without a reply and until 160 cycles (20 bits) after the last
 *    stop bit of a reply, the pad not driven while it lasts and driven high
 *    after it;
 *  - the reception: the specification's reply byte for byte at both rates,
 *    at every phase of the 2-cycle tick, from a device up to 3 % off the
 *    rate; each byte's start within 1 us, or 2 us off the rate; a reply
 *    after the window is not received, one that starts at its end is
 *    received whole; a gap of 19 bits is bridged, one of 20 ends the
 *    release; a low stop bit; more bytes than the buffer holds;
 *  - the arithmetic: the divider at 150, 133, 125, 48 and 12 MHz, the
 *    refusals, the window count at its limits, the stream's layout, the
 *    stamps no run gives.
 *
 * Not tested here, because the host has no PIO: that the chip's input
 * synchroniser, delays, PULL stall and DMA handshakes behave as this model
 * says; the pad, the pull-up and a device's edges.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "exbus_pio.h"
#include "exbus_wire.h"

#define MAX_PROG 32u
#define SYNC     EXBUS_PIO_SYNC_CYCLES

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
    if (!strcmp(t, "isr")) { return 6; }
    if (!strcmp(t, "osr")) { return 7; }
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
            const int dest = reg_code(tok[1]);
            const unsigned bits = (unsigned)atoi(tok[2]);

            /* `out pins` only: destination 0. */
            if (dest != 0 || bits == 0u || bits > 32u) {
                return false;
            }
            w = 0x6000u | (bits & 31u);
        } else if (n == 3u && !strcmp(tok[0], "in")) {
            const int src = reg_code(tok[1]);
            const unsigned bits = (unsigned)atoi(tok[2]);

            if ((src != 0 && src != 1) || bits == 0u || bits > 32u) {
                return false;
            }
            w = 0x4000u | ((unsigned)src << 5) | (bits & 31u);
        } else if (n == 3u && !strcmp(tok[0], "mov")) {
            const int dest = reg_code(tok[1]);
            const int src = reg_code(tok[2]);

            if ((dest != 1 && dest != 6) || (src != 1 && src < 6)) {
                return false;
            }
            w = 0xA000u | ((unsigned)dest << 5) | (unsigned)src;
        } else if (n == 3u && !strcmp(tok[0], "set")) {
            const int dest = reg_code(tok[1]);

            if (dest < 0 || dest == 6 || dest == 7) {
                return false;
            }
            w = 0xE000u | ((unsigned)dest << 5)
                | ((unsigned)atoi(tok[2]) & 31u);
        } else if (n == 2u && !strcmp(tok[0], "push")
                   && !strcmp(tok[1], "noblock")) {
            w = 0x8000u;
        } else if (n == 2u && !strcmp(tok[0], "pull")
                   && !strcmp(tok[1], "block")) {
            w = 0x80A0u;
        } else {
            return false;
        }
        prog[prog_len++] = (uint16_t)(w | (delay << 8));
    }
    return prog_len > 0u;
}

/* ------------------------------------------------------------- the model */

typedef struct {
    uint64_t at;     /* cycles after the release */
    bool level;
} ext_t;

typedef struct {
    uint64_t cyc;
    bool dir;        /* the pad drives */
    bool latch;
} drive_t;

#define MAX_DRIVE 2048u
#define MAX_EXT   1024u
#define MAX_RX    64u

typedef struct {
    /* what the test gives */
    uint32_t tx[EXBUS_PIO_TX_WORDS];
    unsigned n_tx;
    unsigned tx_gap;      /* cycles between two words of the channel */
    ext_t ext[MAX_EXT];   /* the device's levels, in order */
    unsigned n_ext;
    unsigned rx_room;     /* words the receive channel takes */
    /* what the run leaves */
    drive_t drive[MAX_DRIVE];
    unsigned n_drive;
    uint32_t rx[MAX_RX];
    unsigned n_rx;
    unsigned left;        /* words left in the receive FIFO */
    unsigned dropped;     /* pushes into a full FIFO */
    uint64_t release;     /* the cycle the pad became an input */
    uint64_t redrive;     /* the cycle it drove again */
    bool released;
    bool redriven;
    uint64_t end;         /* the cycle the machine stalled for a stream */
    bool finished;
} model_t;

static void run_model(model_t *m, uint64_t max_cycles)
{
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t isr = 0;
    uint32_t osr = 0;
    uint32_t txf[4];
    uint32_t rxf[4];
    unsigned n_txf = 0;
    unsigned n_rxf = 0;
    unsigned tx_at = 0;
    uint64_t tx_next = 0;
    unsigned ext_at = 0;
    bool ext_level = true;
    unsigned pc = 0;
    unsigned delay = 0;
    /* The open leaves the pad driving its latch, high. */
    bool dir = true;
    bool latch = true;
    bool wire[4] = { true, true, true, true };

    m->n_drive = 0;
    m->n_rx = 0;
    m->left = 0;
    m->dropped = 0;
    m->release = 0;
    m->redrive = 0;
    m->released = false;
    m->redriven = false;
    m->finished = false;

    for (uint64_t cyc = 0; cyc < max_cycles; ++cyc) {
        const bool was_dir = dir;
        const bool was_latch = latch;
        const bool pin = wire[(cyc + 4u - SYNC) % 4u];
        /* cppcheck-suppress variableScope
         * set inside the decode switch and read after it */
        bool stalled = false;

        /* The channels: one word a cycle each way, or one every tx_gap. */
        if (tx_at < m->n_tx && n_txf < 4u && cyc >= tx_next) {
            txf[n_txf++] = m->tx[tx_at++];
            tx_next = cyc + (m->tx_gap > 0u ? m->tx_gap : 1u);
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
                    take = pin;
                }
                if (take) {
                    next = arg;
                }
                break;
            }
            case 2: {                                           /* IN */
                const unsigned bits = arg == 0u ? 32u : arg;
                const uint32_t mask = bits == 32u ? 0xFFFFFFFFu
                                                  : (1u << bits) - 1u;
                const uint32_t data = (sel == 0u ? (pin ? 1u : 0u) : x) & mask;

                isr = bits == 32u ? data
                                  : (isr >> bits) | (data << (32u - bits));
                break;
            }
            case 3:                                             /* OUT */
                latch = (osr & 1u) != 0u;
                osr >>= 1;
                break;
            case 4:
                if ((w & 0x80u) != 0u) {                        /* PULL */
                    if (n_txf == 0u) {
                        if (pc == 1u && tx_at == m->n_tx && m->released) {
                            m->end = cyc;
                            m->finished = true;
                        }
                        stalled = true;
                        break;
                    }
                    osr = txf[0];
                    memmove(&txf[0], &txf[1], (n_txf - 1u) * sizeof(txf[0]));
                    --n_txf;
                } else {                                        /* PUSH */
                    if (n_rxf < 4u) {
                        rxf[n_rxf++] = isr;
                    } else {
                        m->dropped++;
                    }
                    isr = 0;
                }
                break;
            case 5: {                                           /* MOV */
                const unsigned src = w & 7u;
                const uint32_t v = src == 1u ? x : src == 6u ? isr : osr;

                if (sel == 1u) {
                    x = v;
                } else {
                    isr = v;
                }
                break;
            }
            default:                                            /* SET */
                if (sel == 0u) {
                    latch = (arg & 1u) != 0u;
                } else if (sel == 1u) {
                    x = arg;
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

        if (was_dir && !dir && !m->released) {
            m->release = cyc;
            m->released = true;
        }
        if (!was_dir && dir && m->released && !m->redriven) {
            m->redrive = cyc;
            m->redriven = true;
        }
        if ((dir != was_dir || latch != was_latch || cyc == 0u)
            && m->n_drive < MAX_DRIVE) {
            m->drive[m->n_drive].cyc = cyc;
            m->drive[m->n_drive].dir = dir;
            m->drive[m->n_drive].latch = latch;
            m->n_drive++;
        }
        if (m->released) {
            while (ext_at < m->n_ext
                   && m->ext[ext_at].at <= cyc - m->release) {
                ext_level = m->ext[ext_at++].level;
            }
        }
        wire[cyc % 4u] = dir ? latch : ext_level;
        if (m->finished && (n_rxf == 0u || m->n_rx >= m->rx_room)) {
            m->left = n_rxf;
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
static exbus_pio_clock_t clk;
static exbus_pio_run_t run;
static uint8_t tx_bytes[EXBUS_TX_MAX];
static unsigned n_tx_bytes;

/* The device sends @p n bytes from @p start cycles after the release, a
 * bit every @p bit cycles, @p gap bits of idle between bytes.  A byte whose
 * index is @p low_stop has a low stop bit.  Returns the end of the last
 * stop bit, in cycles after the release. */
static double device_sends(const uint8_t *bytes, unsigned n, double start,
                           double bit, unsigned gap, int low_stop)
{
    double t = start;

    for (unsigned i = 0; i < n; ++i) {
        for (unsigned b = 0; b < 10u && m.n_ext < MAX_EXT; ++b) {
            bool level;

            if (b == 0u) {
                level = false;
            } else if (b == 9u) {
                level = (int)i != low_stop;
            } else {
                level = ((bytes[i] >> (b - 1u)) & 1u) != 0u;
            }
            m.ext[m.n_ext].at = (uint64_t)(t + b * bit + 0.5);
            m.ext[m.n_ext].level = level;
            m.n_ext++;
        }
        t += (10u + gap) * bit;
    }
    /* Idle after the last byte. */
    if (m.n_ext < MAX_EXT) {
        m.ext[m.n_ext].at = (uint64_t)(t + 0.5);
        m.ext[m.n_ext].level = true;
        m.n_ext++;
    }
    return t - gap * bit;
}

/* The channel packet of @p channels at 1500 us and a request, Packet_ID 8,
 * as the bench sends them. */
static void cycle_bytes(unsigned channels)
{
    uint16_t ch[EXBUS_MAX_CHANNELS];

    for (unsigned i = 0; i < EXBUS_MAX_CHANNELS; ++i) {
        ch[i] = (uint16_t)(12000u + 8u * i);
    }
    n_tx_bytes = (unsigned)exbus_build_channels(ch, channels, 8, false,
                                                tx_bytes, NULL);
    n_tx_bytes += (unsigned)exbus_build_request(8, tx_bytes + n_tx_bytes);
}

/* Lay the cycle out for @p baud at 150 MHz with @p window_us; the device's
 * levels are set by the case after this and before go(). */
static bool prepare(uint32_t baud, uint32_t window_us)
{
    memset(&m, 0, sizeof(m));
    if (!exbus_pio_clock(150000000u, baud, &clk)) {
        return false;
    }
    m.n_tx = exbus_pio_tx(&clk, tx_bytes, n_tx_bytes, window_us, m.tx, &run);
    m.rx_room = EXBUS_PIO_RX_WORDS;
    return m.n_tx != 0u;
}

static bool go(void)
{
    run_model(&m, (uint64_t)n_tx_bytes * 200u + 2u * run.window_ticks
                  + 100u * EXBUS_PIO_BYTE_CYCLES + 100000u);
    return m.finished;
}

static void captured(exbus_capture_t *out)
{
    exbus_pio_capture(&clk, &run, m.rx, m.n_rx,
                      m.dropped != 0u || m.left != 0u, out);
}

/* Cycles of @p us at the clock. */
static double us_cycles(double us)
{
    return us * 1e6 / (double)clk.cycle_ps;
}

/* The transmission on the pad: 8N1 from tx_bytes, 8 cycles a bit, the
 * release right after the last stop bit.  Stop bits may be longer when
 * @p slack. */
static void check_tx_on_pad(bool slack)
{
    uint64_t t = 0;
    bool found = false;

    for (unsigned i = 0; i < m.n_drive; ++i) {
        if (m.drive[i].dir && !m.drive[i].latch) {
            t = m.drive[i].cyc;
            found = true;
            break;
        }
    }
    CHECK(found);
    /* High from the open to the first start bit. */
    CHECK_EQ(pad_at(&m, 0), 1);
    CHECK_EQ(pad_at(&m, t - 1u), 1);
    for (unsigned i = 0; i < n_tx_bytes; ++i) {
        for (unsigned b = 0; b < 10u; ++b) {
            const int want = b == 0u ? 0
                             : b == 9u ? 1
                             : (int)((tx_bytes[i] >> (b - 1u)) & 1u);

            CHECK_EQ(pad_at(&m, t + 8u * b), want);
            CHECK_EQ(pad_at(&m, t + 8u * b + 7u), want);
        }
        t += 80u;
        /* A longer stop bit: the next start bit is where the pad falls. */
        while (slack && i + 1u < n_tx_bytes && pad_at(&m, t) == 1) {
            ++t;
        }
    }
    CHECK(m.released);
    CHECK_EQ(m.release, t);
}

/* The release without a reply: an input from the cycle after the last
 * stop bit for 2 * window + 3 cycles, then driven high. */
static void check_release_common(void)
{
    CHECK(m.redriven);
    CHECK_EQ(pad_at(&m, m.release), -1);
    CHECK_EQ(pad_at(&m, m.redrive - 1u), -1);
    CHECK_EQ(pad_at(&m, m.redrive), 1);
    CHECK(m.finished);
    /* The pad changes twice in the release: off and on. */
    for (unsigned i = 0; i < m.n_drive; ++i) {
        if (m.drive[i].cyc > m.release && m.drive[i].cyc < m.redrive) {
            CHECK(false);
        }
    }
}

static void check_release(void)
{
    check_release_common();
    CHECK_EQ(m.redrive - m.release, 2u * (uint64_t)run.window_ticks + 3u);
}

/* The release with a reply whose last stop bit ends @p end cycles after
 * the release starts: 160 to 161 cycles (20 bits) later, or within @p tol
 * cycles of that from a device off the rate, as the capture says to the
 * microsecond. */
static void check_release_after(double end, const exbus_capture_t *c,
                                double tol)
{
    const double len = (double)(m.redrive - m.release);

    check_release_common();
    CHECK(len >= end + 159.5 - tol);
    CHECK(len <= end + 161.5 + tol);
    CHECK_NEAR((double)c->release_us, len * (double)clk.cycle_ps / 1e6, 1.0);
}

/* The capture against the @p n bytes the device sent from @p start cycles,
 * a bit every @p bit cycles, with starts within @p tol_us. */
static void check_bytes(const exbus_capture_t *c, const uint8_t *sent,
                        unsigned n, double start, double bit, double tol_us)
{
    CHECK_EQ(c->overflow, 0);
    CHECK_EQ(c->n, n);
    for (unsigned i = 0; i < n && i < c->n; ++i) {
        const double edge = (double)(uint64_t)(start + 10u * i * bit + 0.5);
        const double edge_us = edge * (double)clk.cycle_ps / 1e6;

        CHECK_EQ(c->bytes[i].value, sent[i]);
        CHECK_EQ(c->bytes[i].flags, 0);
        CHECK_NEAR((double)c->bytes[i].at_us, edge_us, tol_us);
    }
}

static unsigned hex_to_bytes(const char *hex, uint8_t *out, unsigned room)
{
    unsigned n = 0;
    unsigned v = 0;
    unsigned digits = 0;

    for (const char *p = hex; *p != '\0'; ++p) {
        const char c = *p;
        unsigned d;

        if (c >= '0' && c <= '9') {
            d = (unsigned)(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            d = (unsigned)(c - 'A' + 10);
        } else {
            continue;
        }
        v = (v << 4) | d;
        if (++digits == 2u) {
            if (n < room) {
                out[n] = (uint8_t)v;
            }
            ++n;
            v = 0;
            digits = 0;
        }
    }
    return n;
}

#define SPEC_REPLY                                                           \
    "3B 01 20 08 3A 18 9F 56 00 A4 51 55 EE 11 30 20 21 00 40 34 A3 28 00"   \
    " 41 00 00 51 18 00 09 91 D6"

/* ------------------------------------------------------------------ cases */

TEST_CASE(the_words_are_the_ones_the_firmware_build_checks)
{
    FILE *f = fopen(EXBUS_LINE_WORDS, "r");
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
    CHECK_EQ(prog_len, 23u);
    CHECK_EQ(wrap_target, 0u);
    CHECK_EQ(wrap_at, 22u);
}

TEST_CASE(a_cycle_goes_out_as_8n1_and_the_line_is_released_for_the_window)
{
    exbus_capture_t c;

    cycle_bytes(16);
    CHECK_EQ(n_tx_bytes, 48u);
    CHECK(prepare(125000, 4000));
    /* 1 us a cycle: 4000 us is 1999 ticks, 4001 cycles. */
    CHECK_EQ(clk.cycle_ps, 1000000u);
    CHECK_EQ(run.window_ticks, 1999u);
    CHECK(go());
    check_tx_on_pad(false);
    check_release();
    CHECK_EQ(m.n_rx, 0u);
    captured(&c);
    CHECK_EQ(c.n, 0);
    CHECK_EQ(c.overflow, 0);
    CHECK_EQ(c.release_us, 4001);
    /* The machine waits for the next stream where the driver looks. */
    CHECK_EQ(m.end, m.redrive + 1u);
}

TEST_CASE(at_250_kbaud_a_bit_is_8_cycles_of_0_5_us)
{
    exbus_capture_t c;

    cycle_bytes(24);
    CHECK_EQ(n_tx_bytes, 64u);
    CHECK(prepare(250000, 4000));
    CHECK_EQ(clk.cycle_ps, 500000u);
    CHECK_EQ(run.window_ticks, 3999u);
    CHECK(go());
    check_tx_on_pad(false);
    check_release();
    captured(&c);
    CHECK_EQ(c.release_us, 4001);
}

TEST_CASE(a_slow_transmit_channel_only_lengthens_stop_bits)
{
    cycle_bytes(8);
    CHECK(prepare(125000, 4000));
    m.tx_gap = 100;
    CHECK(go());
    check_tx_on_pad(true);
    check_release();
}

TEST_CASE(the_specification_reply_comes_back_byte_for_byte_at_both_rates)
{
    static const uint32_t rates[2] = { 125000u, 250000u };
    uint8_t reply[64];
    const unsigned n = hex_to_bytes(SPEC_REPLY, reply, sizeof(reply));

    cycle_bytes(16);
    for (unsigned r = 0; r < 2u; ++r) {
        exbus_capture_t c;
        exbus_reply_t rep;
        uint8_t got[EXBUS_RX_MAX] = { 0 };
        double end;

        CHECK(prepare(rates[r], 4000));
        end = device_sends(reply, n, us_cycles(300), 8.0, 0, -1);
        CHECK(go());
        check_tx_on_pad(false);
        captured(&c);
        check_release_after(end, &c, 0);
        check_bytes(&c, reply, n, us_cycles(300), 8.0, 1.0);
        for (unsigned i = 0; i < c.n; ++i) {
            got[i] = c.bytes[i].value;
        }
        CHECK_EQ(exbus_reply_parse(got, c.n, 8, &rep), EXBUS_RX_OK);
        CHECK_EQ(rep.block_len, 24);
        /* Within the longest release 32 bytes can give. */
        CHECK(c.release_us
              <= exbus_pio_release_us(&clk, run.window_ticks, n));
    }
}

/* A byte takes the receive loop 78 cycles from the sample that saw its
 * start bit back to the watch loop: a device up to 80 / 78 - 1 = 2.5 %
 * fast loses no time over a reply.  A slow device is bounded by the stop
 * bit sample, 3 % holds. */
TEST_CASE(every_phase_and_a_device_2_5_percent_fast_to_3_percent_slow_decode)
{
    static const double err[5] = { -0.025, -0.02, 0.0, 0.02, 0.03 };
    static const uint8_t pattern[12] = { 0x00, 0xFF, 0x55, 0xAA, 0x3B, 0x01,
                                         0x80, 0x7F, 0xFE, 0x0F, 0xF0, 0x00 };

    cycle_bytes(4);
    for (unsigned e = 0; e < 5u; ++e) {
        for (unsigned phase = 0; phase < 16u; ++phase) {
            exbus_capture_t c;
            const double bit = 8.0 * (1.0 + err[e]);
            const double start = 200.0 + phase * 0.5;
            double end;

            CHECK(prepare(125000, 4000));
            end = device_sends(pattern, 12, start, bit, 0, -1);
            CHECK(go());
            captured(&c);
            /* The quiet time is the line's cycles, the stop bit the
             * device's: 3 % of 10 bits is 2.4 cycles. */
            check_release_after(end, &c, 2.5);
            check_bytes(&c, pattern, 12, start, bit,
                        err[e] == 0.0 ? 1.0 : 2.0);
        }
    }
}

TEST_CASE(gaps_between_bytes_shift_the_stamps_and_not_the_bytes)
{
    static const uint8_t b[4] = { 0x3B, 0x01, 0x08, 0x06 };
    exbus_capture_t c;
    double end;

    cycle_bytes(4);
    CHECK(prepare(250000, 4000));
    end = device_sends(b, 4, 1001, 8.0, 3, -1);
    CHECK(go());
    captured(&c);
    check_release_after(end, &c, 0);
    CHECK_EQ(c.n, 4);
    for (unsigned i = 0; i < 4u && i < c.n; ++i) {
        CHECK_EQ(c.bytes[i].value, b[i]);
        /* 13 bits of 4 us a byte, from 500.5 us. */
        CHECK_NEAR((double)c.bytes[i].at_us, 500.5 + 52.0 * i, 1.0);
    }
}

TEST_CASE(a_gap_of_19_bits_is_bridged_and_one_of_20_ends_the_release)
{
    static const uint8_t b[3] = { 0x3B, 0x01, 0x08 };
    exbus_capture_t c;
    double end;

    cycle_bytes(4);
    CHECK(prepare(125000, 4000));
    end = device_sends(b, 3, 500, 8.0, 19, -1);
    CHECK(go());
    captured(&c);
    CHECK_EQ(c.n, 3);
    check_release_after(end, &c, 0);
    /* 29 bits of 8 us a byte, from 500 us. */
    CHECK_NEAR((double)c.bytes[2].at_us, 500.0 + 2.0 * 232.0, 1.0);
    /* 20 bits: the release ends before the second byte, and the pad drives
     * high against it. */
    CHECK(prepare(125000, 4000));
    end = device_sends(b, 1, 500, 8.0, 0, -1);
    m.n_ext = 0;
    (void)device_sends(b, 3, 500, 8.0, 20, -1);
    CHECK(go());
    captured(&c);
    CHECK_EQ(c.n, 1);
    CHECK_EQ(c.bytes[0].value, 0x3B);
    check_release_after(end, &c, 0);
}

TEST_CASE(a_reply_after_the_window_is_not_received)
{
    uint8_t reply[64];
    const unsigned n = hex_to_bytes(SPEC_REPLY, reply, sizeof(reply));
    exbus_capture_t c;

    cycle_bytes(16);
    CHECK(prepare(125000, 4000));
    /* The release ends 4001 cycles after it starts. */
    (void)device_sends(reply, n, 4010, 8.0, 0, -1);
    CHECK(go());
    check_release();
    captured(&c);
    CHECK_EQ(c.n, 0);
}

TEST_CASE(a_reply_that_starts_at_the_end_of_the_window_is_received_whole)
{
    uint8_t reply[64];
    const unsigned n = hex_to_bytes(SPEC_REPLY, reply, sizeof(reply));
    exbus_capture_t c;
    double end;

    cycle_bytes(16);
    CHECK(prepare(125000, 4000));
    /* Its start bit is seen by the last sample of the window, at 3999. */
    end = device_sends(reply, n, 3997, 8.0, 0, -1);
    CHECK(go());
    captured(&c);
    check_release_after(end, &c, 0);
    check_bytes(&c, reply, n, 3997, 8.0, 1.0);
    /* The scheduler takes a start up to window_us as in the window. */
    CHECK(c.bytes[0].at_us <= 4000u);
    /* One cycle later it is not seen. */
    CHECK(prepare(125000, 4000));
    (void)device_sends(reply, n, 3998, 8.0, 0, -1);
    CHECK(go());
    captured(&c);
    CHECK_EQ(c.n, 0);
}

TEST_CASE(a_low_stop_bit_marks_its_byte)
{
    static const uint8_t b[3] = { 0x3B, 0x01, 0x20 };
    exbus_capture_t c;

    cycle_bytes(4);
    CHECK(prepare(125000, 4000));
    (void)device_sends(b, 3, 500, 8.0, 2, 1);
    CHECK(go());
    captured(&c);
    CHECK(c.n >= 2u);
    CHECK_EQ(c.bytes[0].value, 0x3B);
    CHECK_EQ(c.bytes[0].flags, 0);
    CHECK_EQ(c.bytes[1].value, 0x01);
    CHECK_EQ(c.bytes[1].flags, EXBUS_RXB_FRAMING);
    CHECK(m.redriven);
}

TEST_CASE(more_bytes_than_the_buffer_holds_is_an_overflow)
{
    uint8_t many[60];
    exbus_capture_t c;
    double end;

    for (unsigned i = 0; i < 60u; ++i) {
        many[i] = (uint8_t)(i * 7u);
    }
    cycle_bytes(16);
    CHECK(prepare(125000, 4000));
    end = device_sends(many, 60, 200, 8.0, 0, -1);
    CHECK(go());
    /* Every byte keeps the line released, kept or not. */
    check_release_common();
    CHECK(m.redrive - m.release >= (uint64_t)end + 159u);
    CHECK_EQ(m.n_rx, EXBUS_PIO_RX_WORDS);
    captured(&c);
    CHECK_EQ(c.overflow, 1);
    CHECK_EQ(c.n, EXBUS_RX_MAX);
    for (unsigned i = 0; i < c.n; ++i) {
        CHECK_EQ(c.bytes[i].value, many[i]);
    }
    /* A channel that takes 10 words leaves the rest in the FIFO or drops
     * it: lost. */
    CHECK(prepare(125000, 4000));
    (void)device_sends(many, 20, 200, 8.0, 0, -1);
    m.rx_room = 10;
    CHECK(go());
    CHECK_EQ(m.n_rx, 10u);
    CHECK(m.left != 0u || m.dropped != 0u);
    captured(&c);
    CHECK_EQ(c.overflow, 1);
    CHECK_EQ(c.n, 10);
}

TEST_CASE(the_divider_at_common_system_clocks)
{
    static const struct {
        uint32_t hz;
        uint32_t baud;
        uint32_t div256;
    } want[] = {
        { 150000000u, 125000u, 150u << 8 },
        { 150000000u, 250000u, 75u << 8 },
        { 133000000u, 125000u, 133u << 8 },
        { 133000000u, 250000u, (66u << 8) | 0x80u },
        { 125000000u, 250000u, (62u << 8) | 0x80u },
        { 48000000u, 125000u, 48u << 8 },
        { 12000000u, 250000u, 6u << 8 },
        { 2000000u, 250000u, 1u << 8 },
    };

    for (unsigned i = 0; i < sizeof(want) / sizeof(want[0]); ++i) {
        CHECK(exbus_pio_clock(want[i].hz, want[i].baud, &clk));
        CHECK_EQ(clk.div256, want[i].div256);
        CHECK_EQ(clk.error_ppm, 0u);
        CHECK_EQ(clk.baud, want[i].baud);
    }
    /* A rate the divider does not hit. */
    CHECK(exbus_pio_clock(100000000u, 115200u, &clk));
    CHECK_EQ(clk.div256, 27778u);
    CHECK(clk.error_ppm <= 10u);
    /* The worst a divider of about 1 gives: half a step of 1/256. */
    CHECK(exbus_pio_clock(2003984u, 250000u, &clk));
    CHECK_EQ(clk.div256, 257u);
    CHECK(clk.error_ppm < 1954u);
    /* 1 cycle at 150 MHz and 125 kbaud is 1 us. */
    CHECK(exbus_pio_clock(150000000u, 125000u, &clk));
    CHECK_EQ(exbus_pio_cycles_ns(&clk, 3), 3000u);
    CHECK_EQ(exbus_pio_cycles_ns(NULL, 3), 0u);
}

TEST_CASE(a_divider_out_of_range_is_refused)
{
    CHECK(!exbus_pio_clock(150000000u, 0, &clk));
    CHECK_EQ(clk.div256, 0u);
    CHECK(!exbus_pio_clock(0, 125000u, &clk));
    CHECK(!exbus_pio_clock(150000000u, 125000u, NULL));
    /* Below 1: 1 MHz cannot give 8 cycles a bit at 250 kbaud. */
    CHECK(!exbus_pio_clock(1000000u, 250000u, &clk));
    CHECK(!exbus_pio_clock(1990000u, 250000u, &clk));
    /* Above 65535. */
    CHECK(!exbus_pio_clock(150000000u, 286u, &clk));
    CHECK(exbus_pio_clock(150000000u, 287u, &clk));
}

TEST_CASE(the_window_count_and_the_longest_release)
{
    CHECK(exbus_pio_clock(150000000u, 125000u, &clk));
    CHECK_EQ(exbus_pio_window_ticks(&clk, 4000), 1999u);
    CHECK_EQ(exbus_pio_window_ticks(&clk, 4001), 1999u);
    CHECK_EQ(exbus_pio_window_ticks(&clk, 4002), 2000u);
    CHECK_EQ(exbus_pio_release_us(&clk, 1999, 0), 4001u);
    CHECK_EQ(exbus_pio_release_us(&clk, 2000, 0), 4003u);
    /* A byte adds at most 78 cycles and a gap 2 cycles short of 160. */
    CHECK_EQ(EXBUS_PIO_QUIET_TICKS, 80u);
    CHECK_EQ(EXBUS_PIO_BYTE_MAX_CYCLES, 236u);
    CHECK_EQ(exbus_pio_release_us(&clk, 1999, 32), 4001u + 236u * 32u);
    CHECK_EQ(exbus_pio_release_us(NULL, 1999, 0), 0u);
    /* Too short for one tick, and the largest count. */
    CHECK_EQ(exbus_pio_window_ticks(&clk, 0), 0u);
    CHECK_EQ(exbus_pio_window_ticks(&clk, 3), 0u);
    CHECK_EQ(exbus_pio_window_ticks(&clk, 4), 1u);
    CHECK_EQ(exbus_pio_window_ticks(&clk, 2u * EXBUS_PIO_WINDOW_MAX + 3u),
             EXBUS_PIO_WINDOW_MAX);
    CHECK_EQ(exbus_pio_window_ticks(&clk, 2u * EXBUS_PIO_WINDOW_MAX + 4u),
             0u);
    CHECK_EQ(exbus_pio_window_ticks(&clk, 0xFFFFFFFFu), 0u);
    CHECK_EQ(exbus_pio_window_ticks(NULL, 4000), 0u);
    /* 250 kbaud: 0.5 us a cycle. */
    CHECK(exbus_pio_clock(150000000u, 250000u, &clk));
    CHECK_EQ(exbus_pio_window_ticks(&clk, 4000), 3999u);
    CHECK_EQ(exbus_pio_release_us(&clk, 3999, 0), 4001u);
    CHECK_EQ(exbus_pio_release_us(&clk, 3999, 1), 4119u);
}

TEST_CASE(the_stream_is_count_window_bytes)
{
    uint32_t words[EXBUS_PIO_TX_WORDS + 1u];
    uint8_t b[EXBUS_TX_MAX + 1u];

    memset(b, 0x5A, sizeof(b));
    CHECK(exbus_pio_clock(150000000u, 125000u, &clk));
    CHECK_EQ(exbus_pio_tx(&clk, b, 1, 4000, words, &run), 3u);
    CHECK_EQ(words[0], 0u);
    CHECK_EQ(words[1], 1999u);
    /* The byte, and the quiet time the program reloads after a byte. */
    CHECK_EQ(words[2], 0x505Au);
    CHECK_EQ(run.tx_bytes, 1u);
    CHECK_EQ(exbus_pio_tx(&clk, b, EXBUS_TX_MAX, 4000, words, &run),
             EXBUS_PIO_TX_WORDS);
    CHECK_EQ(words[0], 63u);
    /* Refused: no bytes, too many, no window, a missing argument. */
    CHECK_EQ(exbus_pio_tx(&clk, b, 0, 4000, words, &run), 0u);
    CHECK_EQ(exbus_pio_tx(&clk, b, EXBUS_TX_MAX + 1u, 4000, words, &run), 0u);
    CHECK_EQ(exbus_pio_tx(&clk, b, 8, 0, words, &run), 0u);
    CHECK_EQ(exbus_pio_tx(NULL, b, 8, 4000, words, &run), 0u);
    CHECK_EQ(exbus_pio_tx(&clk, NULL, 8, 4000, words, &run), 0u);
    CHECK_EQ(exbus_pio_tx(&clk, b, 8, 4000, NULL, &run), 0u);
    CHECK_EQ(exbus_pio_tx(&clk, b, 8, 4000, words, NULL), 0u);
}

/* A received word: count, byte, stop bit high. */
#define RX_WORD(x, byte) \
    ((uint32_t)(x) | ((uint32_t)(byte) << 23) | 0x80000000u)

TEST_CASE(a_stamp_no_run_gives_is_an_overflow)
{
    exbus_capture_t c;
    uint32_t w[EXBUS_PIO_RX_WORDS];

    CHECK(exbus_pio_clock(150000000u, 125000u, &clk));
    run.window_ticks = 100;
    run.tx_bytes = 8;
    /* The first byte seen at the first sample, 1 cycle after the release;
     * the second at the first sample after it, then one 9 ticks later. */
    w[0] = RX_WORD(100, 0x3B);
    w[1] = RX_WORD(79, 0x01);
    w[2] = RX_WORD(70, 0x20) & 0x7FFFFFFFu;
    exbus_pio_capture(&clk, &run, w, 3, false, &c);
    CHECK_EQ(c.overflow, 0);
    CHECK_EQ(c.n, 3);
    CHECK_EQ(c.bytes[0].value, 0x3B);
    CHECK_EQ(c.bytes[0].at_us, 0);
    CHECK_EQ(c.bytes[1].value, 0x01);
    /* 1 + 78 cycles, less 3. */
    CHECK_EQ(c.bytes[1].at_us, 76);
    /* 79 + 78 + 2 * 9, less 3. */
    CHECK_EQ(c.bytes[2].at_us, 172);
    CHECK_EQ(c.bytes[2].flags, EXBUS_RXB_FRAMING);
    /* The release: 175 + 78 + 160. */
    CHECK_EQ(c.release_us, 413);
    /* The first above the window. */
    w[0] = RX_WORD(101, 0);
    exbus_pio_capture(&clk, &run, w, 1, false, &c);
    CHECK_EQ(c.overflow, 1);
    CHECK_EQ(c.n, 0);
    CHECK_EQ(c.release_us, 203);
    /* A later one at the quiet time. */
    w[0] = RX_WORD(90, 0);
    w[1] = RX_WORD(80, 0);
    exbus_pio_capture(&clk, &run, w, 2, false, &c);
    CHECK_EQ(c.overflow, 1);
    CHECK_EQ(c.n, 1);
    /* 49 words: one more than kept. */
    w[0] = RX_WORD(100, 0);
    for (unsigned i = 1; i < EXBUS_PIO_RX_WORDS; ++i) {
        w[i] = RX_WORD(79, i);
    }
    exbus_pio_capture(&clk, &run, w, EXBUS_PIO_RX_WORDS - 1u, false, &c);
    CHECK_EQ(c.overflow, 0);
    CHECK_EQ(c.n, EXBUS_RX_MAX);
    exbus_pio_capture(&clk, &run, w, EXBUS_PIO_RX_WORDS, false, &c);
    CHECK_EQ(c.overflow, 1);
    CHECK_EQ(c.n, EXBUS_RX_MAX);
    /* Lost, and the arguments. */
    exbus_pio_capture(&clk, &run, w, 1, true, &c);
    CHECK_EQ(c.overflow, 1);
    CHECK_EQ(c.n, 1);
    exbus_pio_capture(&clk, &run, NULL, 1, false, &c);
    CHECK_EQ(c.overflow, 1);
    exbus_pio_capture(NULL, &run, w, 1, false, &c);
    CHECK_EQ(c.overflow, 1);
    exbus_pio_capture(&clk, NULL, w, 1, false, &c);
    CHECK_EQ(c.overflow, 1);
    exbus_pio_capture(&clk, &run, w, 1, false, NULL);
    exbus_pio_capture(&clk, &run, NULL, 0, false, &c);
    CHECK_EQ(c.overflow, 0);
    CHECK_EQ(c.n, 0);
}

int main(void)
{
    if (!assemble(EXBUS_LINE_PIO)) {
        printf("FAIL  cannot assemble %s\n", EXBUS_LINE_PIO);
        return 1;
    }
    RUN(the_words_are_the_ones_the_firmware_build_checks);
    RUN(a_cycle_goes_out_as_8n1_and_the_line_is_released_for_the_window);
    RUN(at_250_kbaud_a_bit_is_8_cycles_of_0_5_us);
    RUN(a_slow_transmit_channel_only_lengthens_stop_bits);
    RUN(the_specification_reply_comes_back_byte_for_byte_at_both_rates);
    RUN(every_phase_and_a_device_2_5_percent_fast_to_3_percent_slow_decode);
    RUN(gaps_between_bytes_shift_the_stamps_and_not_the_bytes);
    RUN(a_gap_of_19_bits_is_bridged_and_one_of_20_ends_the_release);
    RUN(a_reply_after_the_window_is_not_received);
    RUN(a_reply_that_starts_at_the_end_of_the_window_is_received_whole);
    RUN(a_low_stop_bit_marks_its_byte);
    RUN(more_bytes_than_the_buffer_holds_is_an_overflow);
    RUN(the_divider_at_common_system_clocks);
    RUN(a_divider_out_of_range_is_refused);
    RUN(the_window_count_and_the_longest_release);
    RUN(the_stream_is_count_window_bytes);
    RUN(a_stamp_no_run_gives_is_an_overflow);
    return test_summary("exbus_pio");
}
