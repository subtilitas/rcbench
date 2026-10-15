/*
 * The Feetech line's PIO (programmable input/output) program, run on the
 * host (protocols/feetech/rp2350/ft_line.pio), with the arithmetic that
 * feeds it and reads it (protocols/feetech/feetech_pio.c).
 *
 * The program is assembled here from its source by a small assembler for
 * the instructions it uses, and run by a cycle-counting model of the state
 * machine: JMP always, on X--, Y-- and the pin; PULL block; OUT to the
 * pins and X with the output shift register shifting right; IN from the
 * pin, Y and NULL with the input shift register shifting right and
 * autopush at 9 bits; WAIT on the pin;
 * SET of the pins, the pin directions and Y; delays; the wrap.  A DMA
 * (direct memory access) channel fills the 4-word transmit FIFO and another
 * drains the 4-word receive FIFO.  The pin is read two cycles after it
 * moves, as the input synchroniser delays it.  The assembled words are held
 * to fixtures/ft_line_words.txt, which the firmware build holds pioasm's
 * output to: the model runs what the chip runs.
 *
 * The wire is the pad's latch while the pad drives, the servo's level while
 * the servo drives, and high otherwise: the pull-up.
 *
 * Under test:
 *  - the divider of every Feetech rate at 150 MHz and 125 MHz, and the
 *    rates and clocks refused;
 *  - the frame: 8 cycles a bit, bit 0 first, a stop bit of 8 cycles at the
 *    end and of 10 between bytes, the pad high 10 cycles before the first
 *    start bit and an input from the end of the last stop bit; the time
 *    feetech_pio_tx_us() gives for it;
 *  - the reply: bytes of a servo whose rate is off by -3 % to +3 %, at
 *    every phase against the state machine's cycle, come back as sent; a
 *    reply that starts at once after the release; a stop bit low is a
 *    framing error and the bytes after the line rose come back.
 *
 * Not tested here, because the host has no PIO: that the chip's input
 * synchroniser, autopush, WAIT and DMA handshakes behave as this model
 * says; the pad, the pull-up and the servo's levels.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "feetech_pio.h"
#include "feetech_wire.h"

#define MAX_PROG 32u
#define SYNC     2u

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

            if (dest < 0 || dest == 4 || bits == 0u || bits > 32u) {
                return false;
            }
            w = 0x6000u | ((unsigned)dest << 5) | (bits & 31u);
        } else if (n == 3u && !strcmp(tok[0], "in")) {
            const int src = !strcmp(tok[1], "null") ? 3 : reg_code(tok[1]);

            if (src < 0 || src == 1 || src == 4) {
                return false;
            }
            w = 0x4000u | ((unsigned)src << 5)
                | ((unsigned)atoi(tok[2]) & 31u);
        } else if (n == 4u && !strcmp(tok[0], "wait")
                   && !strcmp(tok[2], "pin")) {
            w = 0x2000u | (((unsigned)atoi(tok[1]) & 1u) << 7) | (1u << 5)
                | ((unsigned)atoi(tok[3]) & 31u);
        } else if (n == 2u && !strcmp(tok[0], "pull")
                   && !strcmp(tok[1], "block")) {
            w = 0x80A0u;
        } else if (n == 3u && !strcmp(tok[0], "set")) {
            const int dest = reg_code(tok[1]);

            if (dest < 0) {
                return false;
            }
            w = 0xE000u | ((unsigned)dest << 5)
                | ((unsigned)atoi(tok[2]) & 31u);
        } else {
            return false;
        }
        prog[prog_len++] = (uint16_t)(w | (delay << 8));
    }
    return prog_len > 0u;
}

/* ------------------------------------------------------------- the model */

#define MAX_DRIVE 2000u
#define MAX_EXT   1200u
#define MAX_RX    64u

typedef struct {
    uint64_t at;     /* cycles from the release of the pad */
    bool level;
} ext_t;

typedef struct {
    uint64_t cyc;
    bool dir;
    bool latch;
} drive_t;

typedef struct {
    /* what the test gives */
    uint32_t tx[FEETECH_PIO_TX_WORDS];
    unsigned n_tx;
    ext_t ext[MAX_EXT];   /* the servo's level changes; high before */
    unsigned n_ext;
    /* what the run leaves */
    drive_t drive[MAX_DRIVE];
    unsigned n_drive;
    uint32_t rx[MAX_RX];
    unsigned n_rx;
    uint64_t release;     /* the cycle the pad became an input */
    bool released;
} model_t;

static bool ext_level(const model_t *m, uint64_t cyc)
{
    bool level = true;

    if (!m->released || cyc < m->release) {
        return true;
    }
    for (unsigned i = 0; i < m->n_ext && m->ext[i].at <= cyc - m->release;
         ++i) {
        level = m->ext[i].level;
    }
    return level;
}

static void run_model(model_t *m, uint64_t max_cycles)
{
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t isr = 0;
    unsigned isr_count = 0;
    uint32_t osr = 0;
    uint32_t txf[4];
    uint32_t rxf[4];
    unsigned n_txf = 0;
    unsigned n_rxf = 0;
    unsigned tx_at = 0;
    unsigned pc = 0;
    unsigned delay = 0;
    bool dir = false;
    bool latch = false;
    bool wire[SYNC + 1u];

    for (unsigned i = 0; i <= SYNC; ++i) {
        wire[i] = true;
    }
    m->n_drive = 0;
    m->n_rx = 0;
    m->release = 0;
    m->released = false;

    for (uint64_t cyc = 0; cyc < max_cycles; ++cyc) {
        const bool was_dir = dir;
        const bool was_latch = latch;
        /* The pin as the state machine sees it: the wire SYNC cycles ago. */
        const bool pin = wire[cyc % (SYNC + 1u)];

        if (tx_at < m->n_tx && n_txf < 4u) {
            txf[n_txf++] = m->tx[tx_at++];
        }
        if (n_rxf > 0u && m->n_rx < MAX_RX) {
            m->rx[m->n_rx++] = rxf[0];
            memmove(&rxf[0], &rxf[1], (n_rxf - 1u) * sizeof(rxf[0]));
            --n_rxf;
        }

        if (delay > 0u) {
            --delay;
        } else {
            bool stalled = false;
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
            case 1:                                             /* WAIT */
                if (pin != (((w >> 7) & 1u) != 0u)) {
                    stalled = true;
                }
                break;
            case 2: {                                           /* IN */
                const bool bit = sel == 0u ? pin
                                 : sel == 2u ? (y & 1u) != 0u : false;

                isr = (isr >> 1) | (bit ? 0x80000000u : 0u);
                if (++isr_count >= 9u) {
                    if (n_rxf < 4u) {
                        rxf[n_rxf++] = isr;
                        isr = 0;
                        isr_count = 0;
                    } else {
                        stalled = true;
                    }
                }
                break;
            }
            case 3: {                                           /* OUT */
                const unsigned bits = arg == 0u ? 32u : arg;
                const uint32_t data = bits == 32u
                                          ? osr : osr & ((1u << bits) - 1u);

                osr = bits == 32u ? 0u : osr >> bits;
                if (sel == 0u) {
                    latch = (data & 1u) != 0u;
                } else {
                    x = data;
                }
                break;
            }
            case 4:                                             /* PULL */
                if (n_txf == 0u) {
                    stalled = true;
                } else {
                    osr = txf[0];
                    memmove(&txf[0], &txf[1], (n_txf - 1u) * sizeof(txf[0]));
                    --n_txf;
                }
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

        if (was_dir && !dir && !m->released) {
            m->release = cyc;
            m->released = true;
        }
        if ((dir != was_dir || latch != was_latch || cyc == 0u)
            && m->n_drive < MAX_DRIVE) {
            m->drive[m->n_drive].cyc = cyc;
            m->drive[m->n_drive].dir = dir;
            m->drive[m->n_drive].latch = latch;
            m->n_drive++;
        }
        wire[(cyc + SYNC) % (SYNC + 1u)] = dir ? latch : ext_level(m, cyc);
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

/* The servo's bytes, 8N1 at a bit of @p bit_x1000 / 1000 cycles, the first
 * start bit @p start_x1000 / 1000 cycles after the release.  A byte whose
 * index is @p broken has its stop bit low and the line low 3 bits more,
 * then high 2 bits. */
static void servo_sends(const uint8_t *bytes, unsigned n, uint64_t bit_x1000,
                        uint64_t start_x1000, unsigned broken)
{
    uint64_t t = start_x1000;

    m.n_ext = 0;
    for (unsigned i = 0; i < n; ++i) {
        bool level = true;

        for (unsigned b = 0; b < 10u; ++b) {
            bool want;

            if (b == 0u) {
                want = false;
            } else if (b == 9u) {
                want = i != broken;
            } else {
                want = ((bytes[i] >> (b - 1u)) & 1u) != 0u;
            }
            if (want != level && m.n_ext < MAX_EXT) {
                m.ext[m.n_ext].at = (t + b * bit_x1000 + 500u) / 1000u;
                m.ext[m.n_ext].level = want;
                m.n_ext++;
                level = want;
            }
        }
        t += 10u * bit_x1000;
        if (i == broken) {
            t += 3u * bit_x1000;
        }
        if (!level && m.n_ext < MAX_EXT) {
            m.ext[m.n_ext].at = (t + 500u) / 1000u;
            m.ext[m.n_ext].level = true;
            m.n_ext++;
            /* 2 bits high before the next start bit. */
            t += 2u * bit_x1000;
        }
    }
}

/* Send @p n bytes, then let the servo answer; run until all is in. */
static void transact(const uint8_t *frame, unsigned n)
{
    m.n_tx = feetech_pio_tx(frame, n, m.tx);
    run_model(&m, 20000u + (uint64_t)n * 100u + 30u * 8u * 20u);
}

/* The level the pad drives over cycles [a, a + len). */
static bool pad_holds(uint64_t a, uint64_t len, int want)
{
    for (uint64_t c = a; c < a + len; ++c) {
        if (pad_at(&m, c) != want) {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ cases */

TEST_CASE(the_words_are_the_ones_the_firmware_build_checks)
{
    FILE *f = fopen(FT_LINE_WORDS, "r");
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
    CHECK_EQ(prog_len, 22u);
    CHECK_EQ(wrap_target, 13u);
    CHECK_EQ(wrap_at, 21u);
}

TEST_CASE(every_feetech_rate_has_a_divider_within_a_hundredth_of_a_percent)
{
    static const uint32_t clocks[] = { 150000000u, 125000000u };
    static const uint32_t rates[] = { 1000000u, 500000u, 250000u, 128000u,
                                      115200u, 76800u, 57600u, 38400u };
    feetech_pio_clock_t c;

    for (unsigned i = 0; i < 2u; ++i) {
        for (unsigned j = 0; j < 8u; ++j) {
            CHECK(feetech_pio_clock(clocks[i], rates[j], &c));
            CHECK(c.error_ppm <= 100u);
            CHECK_EQ(c.baud, rates[j]);
        }
    }
    /* 150 MHz / 8 Mbit/s = 18.75: 18 and 192 256ths, exact. */
    CHECK(feetech_pio_clock(150000000u, 1000000u, &c));
    CHECK_EQ(c.div_int, 18);
    CHECK_EQ(c.div_frac, 192);
    CHECK_EQ(c.error_ppm, 0u);
    /* 125 MHz / (8 x 38400) = 406.9: 406 and 231 256ths. */
    CHECK(feetech_pio_clock(125000000u, 38400u, &c));
    CHECK_EQ(c.div_int, 406);
    CHECK_EQ(c.div_frac, 231);
}

TEST_CASE(a_rate_the_clock_cannot_divide_to_is_refused)
{
    feetech_pio_clock_t c;

    CHECK(!feetech_pio_clock(0, 1000000u, &c));
    CHECK(!feetech_pio_clock(150000000u, 0, &c));
    CHECK(!feetech_pio_clock(150000000u, 1000000u, NULL));
    /* Divider 1 at 18.75 Mbaud; above it the divider is below 1. */
    CHECK(feetech_pio_clock(150000000u, 18750000u, &c));
    CHECK_EQ(c.div_int, 1);
    CHECK_EQ(c.div_frac, 0);
    CHECK(!feetech_pio_clock(150000000u, 19000000u, &c));
    /* Divider 65535 or less: 287 baud at 150 MHz is 65331. */
    CHECK(feetech_pio_clock(150000000u, 287u, &c));
    CHECK(!feetech_pio_clock(150000000u, 280u, &c));
    /* Near divider 1 the 256ths are coarsest: under 0.2 % still. */
    CHECK(feetech_pio_clock(12000000u, 1490000u, &c));
    CHECK(c.error_ppm < 2000u);
}

TEST_CASE(the_word_stream_is_the_count_and_one_word_a_byte)
{
    uint8_t b[FEETECH_FRAME_MAX + 1u];
    uint32_t w[FEETECH_PIO_TX_WORDS + 1u];
    uint8_t got = 0;

    for (unsigned i = 0; i < sizeof(b); ++i) {
        b[i] = (uint8_t)(0xA0u + i);
    }
    CHECK_EQ(feetech_pio_tx(b, 1, w), 2u);
    CHECK_EQ(w[0], 0u);
    CHECK_EQ(w[1], 0xA0u);
    CHECK_EQ(feetech_pio_tx(b, FEETECH_FRAME_MAX, w), FEETECH_PIO_TX_WORDS);
    CHECK_EQ(w[0], FEETECH_FRAME_MAX - 1u);
    CHECK_EQ(feetech_pio_tx(b, FEETECH_FRAME_MAX + 1u, w), 0u);
    CHECK_EQ(feetech_pio_tx(b, 0, w), 0u);
    CHECK_EQ(feetech_pio_tx(NULL, 1, w), 0u);
    CHECK_EQ(feetech_pio_tx(b, 1, NULL), 0u);
    /* A received word: the byte in bits 23 to 30, the stop bit in 31. */
    CHECK(feetech_pio_rx(0x80000000u | (0x5Au << 23), &got));
    CHECK_EQ(got, 0x5A);
    CHECK(!feetech_pio_rx(0x5Au << 23, &got));
    CHECK_EQ(got, 0x5A);
    CHECK(feetech_pio_rx(0x80000000u, NULL));
}

TEST_CASE(a_frame_is_8n1_at_8_cycles_a_bit_and_the_pad_follows_the_rule)
{
    static const uint8_t frame[] = { 0xFF, 0xFF, 0x01, 0x02, 0x01, 0xFB };
    const unsigned n = sizeof(frame);
    uint64_t start = 0;
    feetech_pio_clock_t c;

    memset(&m, 0, sizeof(m));
    CHECK(assemble(FT_LINE_PIO));
    transact(frame, n);
    /* An input until the program drives high, then high for 10 cycles. */
    CHECK_EQ(pad_at(&m, 0), -1);
    for (uint64_t cyc = 0; cyc < 200u; ++cyc) {
        if (pad_at(&m, cyc) == 0) {
            start = cyc;
            break;
        }
    }
    CHECK_EQ(start, FEETECH_PIO_LEAD_CYCLES);
    CHECK(pad_holds(start - FEETECH_PIO_HIGH_CYCLES,
                    FEETECH_PIO_HIGH_CYCLES, 1));
    CHECK_EQ(pad_at(&m, start - FEETECH_PIO_HIGH_CYCLES - 1u), -1);
    for (unsigned i = 0; i < n; ++i) {
        const uint64_t at = start + (uint64_t)i
                            * (10u * FEETECH_PIO_CYCLES_PER_BIT
                               + FEETECH_PIO_GAP_CYCLES);

        CHECK(pad_holds(at, 8, 0));
        for (unsigned b = 0; b < 8u; ++b) {
            CHECK(pad_holds(at + 8u * (b + 1u), 8,
                            (int)((frame[i] >> b) & 1u)));
        }
        CHECK(pad_holds(at + 72u, i + 1u < n ? 10u : 8u, 1));
    }
    /* Released at the end of the last stop bit, to the cycle. */
    CHECK(m.released);
    CHECK_EQ(m.release, start + (uint64_t)n * 80u
                            + (uint64_t)(n - 1u) * FEETECH_PIO_GAP_CYCLES);
    CHECK_EQ(pad_at(&m, m.release), -1);
    for (unsigned i = 0; i < m.n_drive; ++i) {
        if (m.drive[i].cyc >= m.release) {
            CHECK(!m.drive[i].dir);
        }
    }
    /* No reply: no word. */
    CHECK_EQ(m.n_rx, 0u);
    /* The time the driver gives the frame: 63 us at 1 Mbaud on 150 MHz,
     * which is the release cycle times 18.75 / 150 MHz. */
    CHECK(feetech_pio_clock(150000000u, 1000000u, &c));
    CHECK_EQ(feetech_pio_tx_us(&c, n), 63u);
    CHECK_EQ(m.release * 125u, 63000u);
    CHECK_EQ(feetech_pio_tx_us(&c, 0), 0u);
    CHECK_EQ(feetech_pio_tx_us(NULL, 1), 0u);
}

TEST_CASE(the_longest_frame_goes_out_whole)
{
    uint8_t frame[FEETECH_FRAME_MAX];

    for (unsigned i = 0; i < sizeof(frame); ++i) {
        frame[i] = (uint8_t)(i * 37u);
    }
    memset(&m, 0, sizeof(m));
    CHECK(assemble(FT_LINE_PIO));
    transact(frame, sizeof(frame));
    CHECK(m.released);
    CHECK_EQ(m.release, FEETECH_PIO_LEAD_CYCLES + 64u * 80u + 63u * 2u);
    for (unsigned i = 0; i < sizeof(frame); ++i) {
        const uint64_t at = FEETECH_PIO_LEAD_CYCLES + (uint64_t)i * 82u;

        for (unsigned b = 0; b < 8u; ++b) {
            CHECK_EQ(pad_at(&m, at + 8u * (b + 1u) + 4u),
                     (int)((frame[i] >> b) & 1u));
        }
    }
}

/* A reply at @p err_ppm off the rate and @p phase thousandths of a cycle
 * late comes back byte for byte. */
static void reply_comes_back(int32_t err_ppm, unsigned phase)
{
    static const uint8_t ping[] = { 0xFF, 0xFF, 0x01, 0x02, 0x01, 0xFB };
    static const uint8_t reply[] = { 0xFF, 0xFF, 0x01, 0x04, 0x00, 0x18,
                                     0x05, 0xDD, 0x00, 0x55, 0xAA, 0x80 };
    const uint64_t bit = (uint64_t)((int64_t)8000 * (1000000 + err_ppm)
                                    / 1000000);

    memset(&m, 0, sizeof(m));
    servo_sends(reply, sizeof(reply), bit, 30000u + phase, 0xFFu);
    transact(ping, sizeof(ping));
    CHECK_EQ(m.n_rx, sizeof(reply));
    for (unsigned i = 0; i < m.n_rx && i < sizeof(reply); ++i) {
        uint8_t b = 0;

        CHECK(feetech_pio_rx(m.rx[i], &b));
        CHECK_EQ(b, reply[i]);
    }
}

TEST_CASE(a_reply_3_percent_off_the_rate_at_any_phase_comes_back)
{
    CHECK(assemble(FT_LINE_PIO));
    for (unsigned phase = 0; phase < 1000u; phase += 125u) {
        reply_comes_back(0, phase);
        reply_comes_back(30000, phase);
        reply_comes_back(-30000, phase);
    }
}

TEST_CASE(a_reply_that_starts_at_the_release_comes_back)
{
    static const uint8_t ping[] = { 0xFF, 0xFF, 0x01, 0x02, 0x01, 0xFB };
    static const uint8_t reply[] = { 0xFF, 0xFF, 0x01, 0x02, 0x00, 0xFC };
    uint8_t b = 0;

    CHECK(assemble(FT_LINE_PIO));
    memset(&m, 0, sizeof(m));
    servo_sends(reply, sizeof(reply), 8000u, 0, 0xFFu);
    transact(ping, sizeof(ping));
    CHECK_EQ(m.n_rx, sizeof(reply));
    for (unsigned i = 0; i < m.n_rx && i < sizeof(reply); ++i) {
        CHECK(feetech_pio_rx(m.rx[i], &b));
        CHECK_EQ(b, reply[i]);
    }
}

TEST_CASE(a_stop_bit_low_is_framing_and_the_bytes_after_it_come_back)
{
    static const uint8_t ping[] = { 0xFF, 0xFF, 0x01, 0x02, 0x01, 0xFB };
    static const uint8_t reply[] = { 0x12, 0x00, 0x34, 0x56 };
    uint8_t b = 0;

    CHECK(assemble(FT_LINE_PIO));
    memset(&m, 0, sizeof(m));
    /* The second byte's stop bit is low, and the line stays low 3 bits
     * longer: a break. */
    servo_sends(reply, sizeof(reply), 8000u, 20000u, 1u);
    transact(ping, sizeof(ping));
    CHECK_EQ(m.n_rx, 4u);
    CHECK(feetech_pio_rx(m.rx[0], &b));
    CHECK_EQ(b, 0x12);
    CHECK(!feetech_pio_rx(m.rx[1], &b));
    CHECK_EQ(b, 0x00);
    CHECK(feetech_pio_rx(m.rx[2], &b));
    CHECK_EQ(b, 0x34);
    CHECK(feetech_pio_rx(m.rx[3], &b));
    CHECK_EQ(b, 0x56);
}

int main(void)
{
    CHECK(assemble(FT_LINE_PIO));
    RUN(the_words_are_the_ones_the_firmware_build_checks);
    RUN(every_feetech_rate_has_a_divider_within_a_hundredth_of_a_percent);
    RUN(a_rate_the_clock_cannot_divide_to_is_refused);
    RUN(the_word_stream_is_the_count_and_one_word_a_byte);
    RUN(a_frame_is_8n1_at_8_cycles_a_bit_and_the_pad_follows_the_rule);
    RUN(the_longest_frame_goes_out_whole);
    RUN(a_reply_3_percent_off_the_rate_at_any_phase_comes_back);
    RUN(a_reply_that_starts_at_the_release_comes_back);
    RUN(a_stop_bit_low_is_framing_and_the_bytes_after_it_come_back);
    return test_summary("feetech_pio");
}
