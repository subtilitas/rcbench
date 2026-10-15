/*
 * The Dynamixel line's PIO (programmable input/output) program, run on the
 * host (protocols/dynamixel/rp2350/dxl_line.pio), with the arithmetic that
 * feeds it (protocols/dynamixel/dxl_pio.c).  ROBOTIS and Dynamixel are
 * names or trademarks of their owner, used here only to say which servos
 * the program is for.
 *
 * The program is assembled here from its source by a small assembler for
 * the instructions it uses, and run by a cycle-counting model of the state
 * machine: JMP always, on X--, Y-- and the pin; WAIT on the pin; SET of the
 * pins, the pin directions and X; PULL block; OUT to the pins and Y,
 * shifting right; IN from the pins, shifting right; PUSH noblock; delays;
 * the wrap.  A DMA (direct memory access) channel fills the 4-word
 * transmit FIFO, one byte repeated in the 4 lanes of a word, and another
 * drains the 4-word receive FIFO.  The pin is read two cycles after it
 * moves, as the input synchroniser delays it.  The assembled words are held
 * to fixtures/dxl_line_words.txt, which the firmware build holds pioasm's
 * output to: the model runs what the chip runs.
 *
 * The wire is the pad's latch while the pad drives, and the servo's level
 * while it does not; with neither it is high, as the pull-up holds it.
 * Time on the wire is kept in 1/1000 of a state machine cycle, so a sender
 * 2 % off the rate and at any phase of the cycle is modelled.
 *
 * Under test: the words; the transmit timing (lead-in, 8 cycles a bit, the
 * stop bit, the release); the receive of bytes alone and back to back, at
 * every phase, at -2 % and +2 % of the rate, and where it stops working;
 * a byte with a low stop bit and a long break; a full receive FIFO; the
 * divider at 150, 125, 48 and 12 MHz for the 8 rates; the stream layout.
 *
 * Not tested here, because the host has no PIO: that the chip's input
 * synchroniser, WAIT, JMP PIN, delays and DMA handshakes behave as this
 * model says; the fractional divider's jitter; the pad and the pull-up.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "dxl_pio.h"

#define MAX_PROG 32u
#define BIT      DXL_PIO_CYCLES_PER_BIT

/* ---------------------------------------------------------- the assembler */

static uint16_t prog[MAX_PROG];
static unsigned prog_len;
static unsigned wrap_target;
static unsigned wrap_at;
static int off_tx = -1;
static int off_rx = -1;

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
            bool pub = false;

            s[len - 1u] = '\0';
            if (!strncmp(s, "public ", 7)) {
                s += 7;
                pub = true;
            }
            if (n_labels == 16u || strlen(s) > 15u) {
                fclose(f);
                return false;
            }
            strcpy(labels[n_labels].name, s);
            labels[n_labels++].addr = addr;
            if (pub && !strcmp(s, "send")) {
                off_tx = (int)addr;
            }
            if (pub && !strcmp(s, "listen")) {
                off_rx = (int)addr;
            }
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
        } else if (n == 4u && !strcmp(tok[0], "wait")
                   && !strcmp(tok[2], "pin")) {
            w = 0x2000u | (((unsigned)atoi(tok[1]) & 1u) << 7) | (1u << 5)
                | ((unsigned)atoi(tok[3]) & 31u);
        } else if (n == 3u && !strcmp(tok[0], "in")
                   && !strcmp(tok[1], "pins")) {
            w = 0x4000u | ((unsigned)atoi(tok[2]) & 31u);
        } else if (n == 3u && !strcmp(tok[0], "out")) {
            const int dest = reg_code(tok[1]);
            const unsigned bits = (unsigned)atoi(tok[2]);

            if (dest < 0 || dest == 4 || bits == 0u || bits > 32u) {
                return false;
            }
            w = 0x6000u | ((unsigned)dest << 5) | (bits & 31u);
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

#define MAX_DRIVE 4096u
#define MAX_EXT   64u
#define MAX_RX    64u

typedef struct {
    uint8_t byte;
    bool    broken;      /* a low stop bit */
    uint64_t start_mc;   /* start bit's falling edge, 1/1000 cycle */
    uint32_t bit_mc;     /* one bit, 1/1000 cycle */
    uint32_t low_bits;   /* a break: low this many bits instead of a byte */
} ext_t;

typedef struct {
    uint64_t cyc;
    bool dir;
    bool latch;
} drive_t;

typedef struct {
    /* what the test gives */
    uint32_t tx[DXL_PIO_TX_MAX + 1u];
    unsigned n_tx;
    ext_t ext[MAX_EXT];
    unsigned n_ext;
    unsigned rx_room;      /* bytes the receive channel takes */
    bool start_at_rx;      /* enter at `listen` instead of `send` */
    /* what the run leaves */
    drive_t drive[MAX_DRIVE];
    unsigned n_drive;
    uint8_t rx[MAX_RX];
    uint64_t rx_at[MAX_RX];
    unsigned n_rx;
    unsigned dropped;      /* pushes into a full FIFO */
    uint64_t release;      /* the cycle the pad became an input */
} model_t;

static bool ext_level(const model_t *m, uint64_t cyc)
{
    const uint64_t t = cyc * 1000u;

    for (unsigned i = 0; i < m->n_ext; ++i) {
        const ext_t *e = &m->ext[i];
        const uint32_t bits = e->low_bits != 0u ? e->low_bits : 10u;

        if (t < e->start_mc || t >= e->start_mc + (uint64_t)bits * e->bit_mc) {
            continue;
        }
        if (e->low_bits != 0u) {
            return false;
        }
        const uint64_t k = (t - e->start_mc) / e->bit_mc;

        if (k == 0u) {
            return false;
        }
        if (k == 9u) {
            return !e->broken;
        }
        return ((e->byte >> (k - 1u)) & 1u) != 0u;
    }
    return true;
}

static void run_model(model_t *m, uint64_t cycles)
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
    unsigned pc = m->start_at_rx ? (unsigned)off_rx : (unsigned)off_tx;
    unsigned delay = 0;
    bool dir = false;
    bool latch = false;
    bool wire[3] = { true, true, true };

    m->n_drive = 0;
    m->n_rx = 0;
    m->dropped = 0;
    m->release = 0;

    for (uint64_t cyc = 0; cyc < cycles; ++cyc) {
        const bool was_dir = dir;
        const bool was_latch = latch;
        /* The pin as the program sees it: two cycles old. */
        const bool pin = wire[cyc % 3u];

        /* The channels: one word a cycle each way. */
        if (tx_at < m->n_tx && n_txf < 4u) {
            txf[n_txf++] = m->tx[tx_at++];
        }
        if (n_rxf > 0u && m->n_rx < m->rx_room && m->n_rx < MAX_RX) {
            m->rx[m->n_rx] = (uint8_t)(rxf[0] >> 24);
            m->rx_at[m->n_rx++] = cyc;
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
                stalled = pin != (((w >> 7) & 1u) != 0u);
                break;
            case 2:                                             /* IN */
                isr = (isr >> 1) | ((pin ? 1u : 0u) << 31);
                break;
            case 3: {                                           /* OUT */
                const unsigned bits = arg == 0u ? 32u : arg;
                const uint32_t data = bits == 32u ? osr
                                                  : osr & ((1u << bits) - 1u);

                osr = bits == 32u ? 0u : osr >> bits;
                if (sel == 0u) {
                    latch = (data & 1u) != 0u;
                } else {
                    y = data;
                }
                break;
            }
            case 4:
                if ((w & 0x80u) != 0u) {                        /* PULL */
                    if (n_txf == 0u) {
                        stalled = true;
                        break;
                    }
                    osr = txf[0];
                    memmove(&txf[0], &txf[1], (n_txf - 1u) * sizeof(txf[0]));
                    --n_txf;
                } else if (n_rxf < 4u) {                        /* PUSH */
                    rxf[n_rxf++] = isr;
                    isr = 0;
                } else {
                    m->dropped++;
                    isr = 0;
                }
                break;
            default:                                            /* SET */
                if (sel == 0u) {
                    latch = (arg & 1u) != 0u;
                } else if (sel == 1u) {
                    x = arg;
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
        if ((dir != was_dir || latch != was_latch || cyc == 0u)
            && m->n_drive < MAX_DRIVE) {
            m->drive[m->n_drive].cyc = cyc;
            m->drive[m->n_drive].dir = dir;
            m->drive[m->n_drive].latch = latch;
            m->n_drive++;
        }
        /* The wire this cycle, read two cycles later. */
        wire[(cyc + 2u) % 3u] = dir ? latch : ext_level(m, cyc);
    }
}

/* ---------------------------------------------------------------- helpers */

static model_t m;

/* Load @p n bytes as the driver streams them. */
static void load_packet(const uint8_t *p, size_t n)
{
    uint8_t s[DXL_PIO_TX_MAX + 1u];
    const size_t k = dxl_pio_tx(p, n, s, sizeof(s));

    memset(&m, 0, sizeof(m));
    for (size_t i = 0; i < k; ++i) {
        m.tx[i] = s[i] * 0x01010101u;
    }
    m.n_tx = (unsigned)k;
    m.rx_room = MAX_RX;
}

/* The pad at @p cyc: 1 driven high, 0 driven low, -1 an input. */
static int pad_at(uint64_t cyc)
{
    int v = -1;

    for (unsigned i = 0; i < m.n_drive && m.drive[i].cyc <= cyc; ++i) {
        v = m.drive[i].dir ? (m.drive[i].latch ? 1 : 0) : -1;
    }
    return v;
}

/* Decode what the pad sent, check every bit is 8 cycles, every stop bit
 * between bytes 11 and the last 9 then released.  Returns the bytes. */
static size_t decode_pad(uint8_t *out, size_t cap, uint64_t *lead)
{
    uint64_t drive_on = 0;
    uint64_t fall = 0;
    size_t n = 0;

    /* The drive starts high. */
    for (unsigned i = 0; i < m.n_drive; ++i) {
        if (m.drive[i].dir) {
            drive_on = m.drive[i].cyc;
            CHECK(m.drive[i].latch);
            break;
        }
    }
    for (fall = drive_on; pad_at(fall) == 1; ++fall) {
    }
    CHECK_EQ(pad_at(fall), 0);
    *lead = fall - drive_on;
    for (;;) {
        uint8_t b = 0;

        /* The start bit, 8 cycles low. */
        for (unsigned c = 0; c < BIT; ++c) {
            if (pad_at(fall + c) != 0) {
                T_FAIL("start bit of byte %zu at cycle %u", n, c);
            }
        }
        for (unsigned k = 0; k < 8u; ++k) {
            const uint64_t at = fall + BIT * (k + 1u);
            const int v = pad_at(at);

            for (unsigned c = 1; c < BIT; ++c) {
                if (pad_at(at + c) != v) {
                    T_FAIL("bit %u of byte %zu moves within its time", k, n);
                }
            }
            b = (uint8_t)(b | ((v == 1 ? 1u : 0u) << k));
        }
        if (n < cap) {
            out[n] = b;
        }
        ++n;
        const uint64_t stop = fall + 9u * BIT;
        uint64_t c = stop;

        while (pad_at(c) == 1) {
            ++c;
        }
        if (pad_at(c) == -1) {
            CHECK_EQ(c - stop, 9u);
            CHECK_EQ(c, m.release);
            return n;
        }
        CHECK_EQ(c - stop, 11u);
        fall = c;
        if (n > 300u) {
            T_FAIL("no release");
            return n;
        }
    }
}

/* Bytes @p b back to back from @p start_mc, @p gap_bits idle between. */
static void put_bytes(const uint8_t *b, size_t n, uint64_t start_mc,
                      uint32_t bit_mc, uint32_t gap_bits)
{
    for (size_t i = 0; i < n && m.n_ext < MAX_EXT; ++i) {
        ext_t *e = &m.ext[m.n_ext++];

        e->byte = b[i];
        e->start_mc = start_mc + (uint64_t)i * (10u + gap_bits) * bit_mc;
        e->bit_mc = bit_mc;
    }
}

/* The receive of @p n bytes at @p bit_mc from phase @p phase_mc: true when
 * every byte comes back. */
static bool receive_ok(const uint8_t *b, size_t n, uint32_t bit_mc,
                       uint32_t phase_mc, uint32_t gap_bits)
{
    memset(&m, 0, sizeof(m));
    m.start_at_rx = true;
    m.rx_room = MAX_RX;
    put_bytes(b, n, 50000u + phase_mc, bit_mc, gap_bits);
    run_model(&m, 60u + (uint64_t)(n * (10u + gap_bits) + 4u) * bit_mc / 1000u);
    if (m.n_rx != n || m.dropped != 0u) {
        return false;
    }
    return memcmp(m.rx, b, n) == 0;
}

/* ------------------------------------------------------------------ cases */

TEST_CASE(the_words_are_the_ones_the_firmware_build_checks)
{
    FILE *f = fopen(DXL_LINE_WORDS, "r");
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
    CHECK_EQ(off_tx, 0);
    CHECK_EQ(off_rx, 14);
    CHECK_EQ(wrap_target, 14u);
    CHECK_EQ(wrap_at, 21u);
}

TEST_CASE(a_packet_goes_out_8_cycles_a_bit_after_a_lead_of_idle_high)
{
    const uint8_t p[] = { 0xFF, 0xFF, 0xFD, 0x00, 0x01, 0x03, 0x00, 0x01,
                          0x19, 0x4E, 0x55, 0xAA, 0x00, 0x80 };
    uint8_t got[32];
    uint64_t lead = 0;

    load_packet(p, sizeof(p));
    run_model(&m, 200u + sizeof(p) * 83u + 100u);
    /* The pad never drives low before the first start bit. */
    for (uint64_t c = 0; c < 158u; ++c) {
        if (pad_at(c) == 0) {
            T_FAIL("driven low at cycle %u", (unsigned)c);
        }
    }
    const size_t n = decode_pad(got, sizeof(got), &lead);

    CHECK_EQ(n, sizeof(p));
    CHECK(memcmp(got, p, sizeof(p)) == 0);
    /* 158 cycles, 19.75 bit times, within the DXL_PIO_LEAD_BITS. */
    CHECK_EQ(lead, 158u);
    CHECK(lead <= DXL_PIO_LEAD_BITS * BIT);
    /* No echo: nothing the pad sent comes back. */
    CHECK_EQ(m.n_rx, 0u);
}

TEST_CASE(one_byte_and_the_longest_stream_go_out_whole)
{
    static uint8_t p[DXL_PIO_TX_MAX];
    static uint8_t got[DXL_PIO_TX_MAX + 8u];
    uint64_t lead = 0;

    p[0] = 0x5A;
    load_packet(p, 1);
    run_model(&m, 400);
    CHECK_EQ(decode_pad(got, sizeof(got), &lead), 1u);
    CHECK_EQ(got[0], 0x5Au);

    for (size_t i = 0; i < sizeof(p); ++i) {
        p[i] = (uint8_t)(i * 37u + 11u);
    }
    load_packet(p, sizeof(p));
    run_model(&m, 200u + sizeof(p) * 83u + 100u);
    CHECK_EQ(decode_pad(got, sizeof(got), &lead), DXL_PIO_TX_MAX);
    CHECK(memcmp(got, p, sizeof(p)) == 0);
    /* 158 + 256 x 83 - 2: the last stop bit is 9 cycles, not 11. */
    CHECK_EQ(m.release, 1u + 158u + 256u * 83u - 2u);
}

TEST_CASE(a_reply_after_the_release_comes_back_byte_for_byte)
{
    const uint8_t p[] = { 0xFF, 0xFF, 0xFD, 0x00, 0x01, 0x03, 0x00, 0x01,
                          0x19, 0x4E };
    const uint8_t r[] = { 0xFF, 0xFF, 0xFD, 0x00, 0x01, 0x07, 0x00, 0x55,
                          0x00, 0x06, 0x04, 0x26, 0x65, 0x5D };

    load_packet(p, sizeof(p));
    run_model(&m, 1200);
    const uint64_t rel = m.release;

    CHECK(rel > 0u);
    /* The reply 2 bit times after the release, back to back. */
    load_packet(p, sizeof(p));
    put_bytes(r, sizeof(r), (rel + 2u * BIT) * 1000u + 333u, BIT * 1000u, 0);
    run_model(&m, rel + 20u + (sizeof(r) + 1u) * 10u * BIT);
    CHECK_EQ(m.n_rx, sizeof(r));
    CHECK(memcmp(m.rx, r, sizeof(r)) == 0);
    CHECK_EQ(m.dropped, 0u);
    /* A byte is in 10 cycles after its stop bit's middle at the latest. */
    const uint64_t last_stop_mid = (rel + 2u * BIT) + (sizeof(r) - 1u) * 10u
                                   * BIT + 9u * BIT + BIT / 2u;
    CHECK(m.rx_at[sizeof(r) - 1u] <= last_stop_mid + 10u);
}

TEST_CASE(every_phase_and_2_percent_either_way_receive_back_to_back)
{
    uint8_t b[24];

    for (size_t i = 0; i < sizeof(b); ++i) {
        b[i] = (uint8_t)(i * 73u + 0x35u);
    }
    b[3] = 0x00;
    b[4] = 0xFF;
    b[5] = 0x80;
    b[6] = 0x01;
    static const uint32_t rates[] = { 7840, 8000, 8160 };

    for (size_t r = 0; r < 3u; ++r) {
        for (uint32_t ph = 0; ph < 1000u; ph += 125u) {
            for (uint32_t gap = 0; gap < 2u; ++gap) {
                if (!receive_ok(b, sizeof(b), rates[r], ph * 8u, gap)) {
                    T_FAIL("bit %u mc, phase %u, gap %u: %u of %zu bytes",
                           (unsigned)rates[r], (unsigned)ph, (unsigned)gap,
                           m.n_rx, sizeof(b));
                }
            }
        }
    }
}

/* Where the receive stops working: a sender fast by 3 % back to back
 * loses bytes; with 1 bit time between bytes 4 % fast and slow still
 * pass.  The servo's clock error is not published. */
TEST_CASE(the_rate_window_ends_beyond_2_percent)
{
    uint8_t b[16];

    for (size_t i = 0; i < sizeof(b); ++i) {
        b[i] = (uint8_t)(0xFF - i * 9u);
    }
    bool all = true;

    for (uint32_t ph = 0; ph < 8000u; ph += 500u) {
        all = all && receive_ok(b, sizeof(b), 7760, ph, 0);
    }
    CHECK(!all);
    for (uint32_t ph = 0; ph < 8000u; ph += 500u) {
        CHECK(receive_ok(b, sizeof(b), 7680, ph, 1));
        CHECK(receive_ok(b, sizeof(b), 8320, ph, 1));
    }
}

TEST_CASE(a_low_stop_bit_drops_the_byte_and_a_break_gives_nothing)
{
    memset(&m, 0, sizeof(m));
    m.start_at_rx = true;
    m.rx_room = MAX_RX;
    const uint8_t b[3] = { 0x12, 0x34, 0x56 };

    put_bytes(b, 3, 40000, 8000, 2);
    m.ext[1].broken = true;
    run_model(&m, 40u + 3u * 12u * 8u + 40u);
    CHECK_EQ(m.n_rx, 2u);
    CHECK_EQ(m.rx[0], 0x12u);
    CHECK_EQ(m.rx[1], 0x56u);

    /* A break of 40 bit times, then a byte. */
    memset(&m, 0, sizeof(m));
    m.start_at_rx = true;
    m.rx_room = MAX_RX;
    m.ext[0].start_mc = 40000;
    m.ext[0].bit_mc = 8000;
    m.ext[0].low_bits = 40;
    m.n_ext = 1;
    const uint8_t c = 0xA7;

    put_bytes(&c, 1, 40000u + 41u * 8000u, 8000, 0);
    run_model(&m, 40u + 55u * 8u);
    CHECK_EQ(m.n_rx, 1u);
    CHECK_EQ(m.rx[0], 0xA7u);
}

TEST_CASE(a_full_receive_fifo_drops_and_does_not_stall)
{
    uint8_t b[10];

    for (size_t i = 0; i < sizeof(b); ++i) {
        b[i] = (uint8_t)i;
    }
    memset(&m, 0, sizeof(m));
    m.start_at_rx = true;
    m.rx_room = 2;
    put_bytes(b, sizeof(b), 40000, 8000, 0);
    run_model(&m, 40u + 11u * 80u);
    /* 2 taken by the channel, 4 in the FIFO, 4 dropped. */
    CHECK_EQ(m.n_rx, 2u);
    CHECK_EQ(m.dropped, 4u);
}

/* --------------------------------------------------------- the arithmetic */

typedef struct {
    uint32_t hz;
    uint32_t baud;
    uint32_t div_int;
    uint8_t div_frac;
    int32_t error_ppm;
    uint32_t actual;
    uint32_t jitter_ppm;
    bool ok;
} div_case_t;

static const div_case_t k_div[] = {
    { 150000000, 9600, 1953, 32, 0, 9600, 64, true },
    { 150000000, 57600, 325, 133, 4, 57600, 384, true },
    { 150000000, 115200, 162, 195, -8, 115199, 768, true },
    { 150000000, 1000000, 18, 192, 0, 1000000, 6667, true },
    { 150000000, 2000000, 9, 96, 0, 2000000, 13334, true },
    { 150000000, 3000000, 6, 64, 0, 3000000, 20000, true },
    { 150000000, 4000000, 4, 176, 0, 4000000, 26667, true },
    { 150000000, 4500000, 4, 43, -312, 4498594, 30000, true },
    { 125000000, 3000000, 5, 53, 250, 3000750, 24000, true },
    { 125000000, 4500000, 3, 121, -125, 4499438, 36000, true },
    { 48000000, 9600, 625, 0, 0, 9600, 0, true },
    { 48000000, 4500000, 1, 85, 978, 4504399, 93750, true },
    { 12000000, 1000000, 1, 128, 0, 1000000, 83334, true },
    /* A divider below 1 is refused. */
    { 12000000, 2000000, 0, 0, 0, 0, 0, false },
    { 12000000, 4500000, 0, 0, 0, 0, 0, false },
};

TEST_CASE(the_divider_for_every_rate_and_clock)
{
    for (size_t i = 0; i < sizeof(k_div) / sizeof(k_div[0]); ++i) {
        const div_case_t *k = &k_div[i];
        dxl_pio_div_t d;
        const bool ok = dxl_pio_divider(k->hz, k->baud, &d);

        if (ok != k->ok) {
            T_FAIL("%u Hz, %u bit/s: %d", (unsigned)k->hz, (unsigned)k->baud,
                   (int)ok);
            continue;
        }
        if (!ok) {
            continue;
        }
        CHECK_EQ(d.div_int, k->div_int);
        CHECK_EQ(d.div_frac, k->div_frac);
        CHECK_EQ(d.error_ppm, k->error_ppm);
        CHECK_EQ(d.actual, k->actual);
        CHECK_EQ(d.jitter_ppm, k->jitter_ppm);
        CHECK(d.error_ppm <= DXL_PIO_ERROR_MAX_PPM);
        CHECK(d.error_ppm >= -DXL_PIO_ERROR_MAX_PPM);
    }
}

TEST_CASE(the_divider_refuses_zero_and_rates_too_slow)
{
    dxl_pio_div_t d;

    CHECK(!dxl_pio_divider(0, 57600, &d));
    CHECK(!dxl_pio_divider(150000000, 0, &d));
    CHECK(!dxl_pio_divider(150000000, 57600, NULL));
    /* 150 MHz / 8 / 65536 = 286.1 bit/s is the slowest. */
    CHECK(dxl_pio_divider(150000000, 287, &d));
    CHECK(!dxl_pio_divider(150000000, 285, &d));
    /* The divider 65536 exactly, at 8 x 65536 x 300 Hz. */
    CHECK(dxl_pio_divider(8u * 65536u * 300u, 300, &d));
    CHECK_EQ(d.div_int, 65536u);
    CHECK_EQ(d.div_frac, 0u);
    /* The rounding keeps the mean error under 1/512 of a divider of 1:
     * 0.2 %, far inside the 2 % limit, at every clock and rate. */
    for (uint32_t hz = 12000000; hz <= 200000000; hz += 1000003u) {
        for (uint32_t baud = 9600; baud <= 4500000; baud = baud * 3u / 2u) {
            if (dxl_pio_divider(hz, baud, &d)) {
                CHECK(d.error_ppm <= 1954 && d.error_ppm >= -1954);
            }
        }
    }
}

TEST_CASE(the_stream_is_the_count_less_1_then_the_bytes)
{
    uint8_t p[DXL_PIO_TX_MAX + 1u];
    uint8_t s[DXL_PIO_TX_MAX + 2u];

    for (size_t i = 0; i < sizeof(p); ++i) {
        p[i] = (uint8_t)~i;
    }
    CHECK_EQ(dxl_pio_tx(p, 1, s, sizeof(s)), 2u);
    CHECK_EQ(s[0], 0u);
    CHECK_EQ(s[1], p[0]);
    CHECK_EQ(dxl_pio_tx(p, DXL_PIO_TX_MAX, s, sizeof(s)), DXL_PIO_TX_MAX + 1u);
    CHECK_EQ(s[0], 255u);
    CHECK(memcmp(&s[1], p, DXL_PIO_TX_MAX) == 0);
    CHECK_EQ(dxl_pio_tx(p, DXL_PIO_TX_MAX + 1u, s, sizeof(s)), 0u);
    CHECK_EQ(dxl_pio_tx(p, 0, s, sizeof(s)), 0u);
    CHECK_EQ(dxl_pio_tx(p, 10, s, 10), 0u);
    CHECK_EQ(dxl_pio_tx(p, 10, s, 11), 11u);
    CHECK_EQ(dxl_pio_tx(NULL, 10, s, sizeof(s)), 0u);
    CHECK_EQ(dxl_pio_tx(p, 10, NULL, sizeof(s)), 0u);
}

int main(void)
{
    if (!assemble(DXL_LINE_PIO)) {
        printf("FAIL  cannot assemble %s\n", DXL_LINE_PIO);
        return 1;
    }
    RUN(the_words_are_the_ones_the_firmware_build_checks);
    RUN(a_packet_goes_out_8_cycles_a_bit_after_a_lead_of_idle_high);
    RUN(one_byte_and_the_longest_stream_go_out_whole);
    RUN(a_reply_after_the_release_comes_back_byte_for_byte);
    RUN(every_phase_and_2_percent_either_way_receive_back_to_back);
    RUN(the_rate_window_ends_beyond_2_percent);
    RUN(a_low_stop_bit_drops_the_byte_and_a_break_gives_nothing);
    RUN(a_full_receive_fifo_drops_and_does_not_stall);
    RUN(the_divider_for_every_rate_and_clock);
    RUN(the_divider_refuses_zero_and_rates_too_slow);
    RUN(the_stream_is_the_count_less_1_then_the_bytes);
    return test_summary("dxl_pio");
}
