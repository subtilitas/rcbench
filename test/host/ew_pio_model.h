/*
 * A model of the signal wire's PIO (programmable input/output) state machine,
 * for test_ew_pio.c.
 *
 * assemble() reads protocols/escwire/rp2350/ew_line.pio with an assembler for
 * the instructions it uses; model_run() steps the program one state machine
 * cycle at a time against the wire the test gives (ext_edge(), ext_byte()).
 * test_ew_pio.c says what the model covers and what it leaves to the chip.
 * Include it after greatest.h, once per suite.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EW_PIO_MODEL_H
#define EW_PIO_MODEL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ew_pio.h"

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
    static const char *const names[] = { "pins", "x", "y", "null", "pindirs" };

    for (int i = 0; i < 5; ++i) {
        if (strcmp(t, names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static int cond_code(const char *t)
{
    if (strcmp(t, "x--") == 0) { return 2; }
    if (strcmp(t, "y--") == 0) { return 4; }
    if (strcmp(t, "pin") == 0) { return 6; }
    return -1;
}

/* One instruction; -1 when the assembler does not know it. */
static long encode(char **tok, unsigned n, const label_t *labels,
                   unsigned n_labels)
{
    if (n >= 2u && strcmp(tok[0], "jmp") == 0) {
        const int cond = n == 3u ? cond_code(tok[1]) : 0;

        for (unsigned j = 0; cond >= 0 && j < n_labels; ++j) {
            if (strcmp(labels[j].name, tok[n - 1u]) == 0) {
                return (long)(((unsigned)cond << 5) | labels[j].addr);
            }
        }
        return -1;
    }
    if (n == 1u && strcmp(tok[0], "pull") == 0) {
        return 0x80A0;
    }
    if (n == 2u && strcmp(tok[0], "pull") == 0
        && strcmp(tok[1], "ifempty") == 0) {
        return 0x80E0;
    }
    if (n == 3u && strcmp(tok[0], "mov") == 0 && strcmp(tok[1], "x") == 0
        && strcmp(tok[2], "osr") == 0) {
        return 0xA027;
    }
    if (n == 3u) {
        const int r = reg_code(tok[1]);
        const unsigned arg = (unsigned)atoi(tok[2]);

        if (r < 0 || arg > 32u) {
            return -1;
        }
        if (strcmp(tok[0], "out") == 0) {
            return (long)(0x6000u | ((unsigned)r << 5) | (arg & 31u));
        }
        if (strcmp(tok[0], "in") == 0) {
            return (long)(0x4000u | ((unsigned)r << 5) | (arg & 31u));
        }
        if (strcmp(tok[0], "set") == 0) {
            return (long)(0xE000u | ((unsigned)r << 5) | (arg & 31u));
        }
    }
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
        if (len == 0u || strncmp(s, ".program", 8) == 0) {
            continue;
        }
        if (strcmp(s, ".wrap_target") == 0) {
            wrap_target = addr;
        } else if (strcmp(s, ".wrap") == 0) {
            wrap_at = addr - 1u;
        } else if (s[len - 1u] == ':') {
            s[len - 1u] = '\0';
            strcpy(labels[n_labels].name, s);
            labels[n_labels++].addr = addr;
        } else {
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

        for (char *p = strtok(text[i], " \t,"); p != NULL && n < 6u;
             p = strtok(NULL, " \t,")) {
            if (p[0] == '[') {
                delay = (unsigned)atoi(p + 1);
            } else {
                tok[n++] = p;
            }
        }
        const long w = encode(tok, n, labels, n_labels);

        if (w < 0) {
            return false;
        }
        prog[prog_len++] = (uint16_t)((unsigned)w | (delay << 8));
    }
    return prog_len > 0u;
}

/* ------------------------------------------------------------- the model */

#define MAX_PAD  16000u
#define MAX_EXT  20000u

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
    uint32_t tx[2u * EW_PIO_TX_WORDS];
    unsigned n_tx;
    ext_t ext[MAX_EXT];
    unsigned n_ext;
    bool ext0;              /* the released wire before the first change */
    unsigned rx_room;       /* words the receive channel takes */
    uint64_t abort_at;      /* 0: none; else the cycle of a restart */
    unsigned resume_at;     /* the word of the stream after that restart */
    /* what the run leaves */
    pad_t pad[MAX_PAD];
    unsigned n_pad;
    uint16_t rx[EW_PIO_RX_WORDS];   /* the channel reads the upper half */
    unsigned n_rx;
    uint64_t rx_at[EW_PIO_RX_WORDS];
    bool stalled;           /* found the receive FIFO full */
    uint64_t release;       /* the cycle of the first release */
    bool released;
    uint64_t end;           /* the cycle it came to rest */
    bool finished;
} model_t;

static uint64_t t_of(const model_t *m, uint64_t cyc)
{
    return cyc * m->div256;
}

static bool ext_level(const model_t *m, uint64_t t, unsigned *cursor)
{
    if (!m->released || t < t_of(m, m->release)) {
        return m->ext0;
    }
    const double ns = (double)(t - t_of(m, m->release)) * 1e9
                      / ((double)m->sys_hz * 256.0);

    while (*cursor < m->n_ext && m->ext[*cursor].ns <= ns) {
        ++*cursor;
    }
    return *cursor == 0u ? m->ext0 : m->ext[*cursor - 1u].level;
}

/* The pad at cycle @p cyc: 1 driven high, 0 driven low, -1 an input. */
static int pad_at(const model_t *m, uint64_t cyc)
{
    int v = -1;

    for (unsigned i = 0; i < m->n_pad && m->pad[i].cyc <= cyc; ++i) {
        v = m->pad[i].dir ? (m->pad[i].latch ? 1 : 0) : -1;
    }
    return v;
}

static void model_run(model_t *m, uint64_t max_cycles)
{
    static unsigned cursor;
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t isr = 0;
    uint32_t osr = 0;
    unsigned osr_count = 32;
    unsigned isr_count = 0;
    uint32_t txf[4];
    uint32_t rxf[4];
    unsigned n_txf = 0;
    unsigned n_rxf = 0;
    unsigned tx_at = 0;
    unsigned pc = EW_PIO_IDLE_PC;
    unsigned delay = 0;
    bool dir = false;
    bool latch = true;
    unsigned tx_end = m->abort_at != 0u ? m->resume_at : m->n_tx;

    cursor = 0;
    m->n_pad = 0;
    m->n_rx = 0;
    m->stalled = false;
    m->release = 0;
    m->released = false;
    m->end = 0;
    m->finished = false;

    for (uint64_t cyc = 0; cyc < max_cycles; ++cyc) {
        const bool was_dir = dir;
        const bool was_latch = latch;

        if (m->abort_at != 0u && cyc == m->abort_at) {
            /* The driver's restart: released, FIFOs and shift counters
             * cleared, X and Y as they were, the output shift register holding
             * whatever it held, and the machine on its first `pull`. */
            dir = false;
            n_txf = 0;
            n_rxf = 0;
            tx_at = m->resume_at;
            tx_end = m->n_tx;
            osr_count = 32;
            isr = 0;
            isr_count = 0;
            delay = 0;
            pc = EW_PIO_IDLE_PC;
            if (!m->released) {
                m->release = cyc;
                m->released = true;
            }
        }
        /* The channels: faster than a state machine cycle. */
        while (tx_at < tx_end && n_txf < 4u) {
            txf[n_txf++] = m->tx[tx_at++];
        }
        while (n_rxf > 0u && m->n_rx < m->rx_room) {
            m->rx_at[m->n_rx] = cyc;
            m->rx[m->n_rx++] = (uint16_t)(rxf[0] >> 16);
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
            const uint64_t t_in = t_of(m, cyc) - (cyc > 0u ? 512u : 0u);
            /* Read 2 system cycles late: the input synchroniser. */
            const bool wire = dir ? latch : ext_level(m, t_in, &cursor);
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
                    take = wire;
                }
                if (take) {
                    next = arg;
                }
                break;
            }
            case 2: {                                           /* IN */
                const uint32_t v = sel == 0u ? (wire ? 1u : 0u)
                                   : sel == 1u ? x
                                   : sel == 2u ? y
                                               : 0u;

                if (isr_count + arg >= 9u && n_rxf == 4u) {
                    m->stalled = true;
                    stalled = true;
                    break;
                }
                isr = (isr >> arg) | ((v & ((1u << arg) - 1u)) << (32u - arg));
                isr_count += arg;
                if (isr_count >= 9u) {
                    rxf[n_rxf++] = isr;
                    isr = 0;
                    isr_count = 0;
                }
                break;
            }
            case 3: {                                           /* OUT */
                const unsigned bits = arg == 0u ? 32u : arg;
                const uint32_t data = bits == 32u
                                          ? osr
                                          : osr & ((1u << bits) - 1u);

                osr = bits == 32u ? 0u : osr >> bits;
                osr_count = osr_count + bits > 32u ? 32u : osr_count + bits;
                if (sel == 0u) {
                    latch = (data & 1u) != 0u;
                } else if (sel == 1u) {
                    x = data;
                } else if (sel == 2u) {
                    y = data;
                }
                break;
            }
            case 4:                                             /* PULL */
                if ((w & 0x40u) != 0u && osr_count < 32u) {
                    break;
                }
                if (n_txf == 0u) {
                    if (pc == EW_PIO_IDLE_PC && tx_at == m->n_tx && m->released
                        && n_rxf == 0u) {
                        m->end = cyc;
                        m->finished = true;
                    }
                    stalled = true;
                    break;
                }
                osr = txf[0];
                memmove(&txf[0], &txf[1], (n_txf - 1u) * sizeof(txf[0]));
                --n_txf;
                osr_count = 0;
                break;
            case 5:                                             /* MOV X, OSR */
                x = osr;
                break;
            default:                                            /* SET */
                if (sel == 2u) {
                    y = arg;
                } else if (sel == 4u) {
                    dir = (arg & 1u) != 0u;
                } else {
                    latch = (arg & 1u) != 0u;
                }
                if (pc == EW_PIO_RELEASE_PC && !m->released) {
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
        if (dir != was_dir || latch != was_latch) {
            if (m->n_pad < MAX_PAD) {
                m->pad[m->n_pad].cyc = cyc;
                m->pad[m->n_pad].dir = dir;
                m->pad[m->n_pad].latch = latch;
                m->n_pad++;
            }
        }
        if (m->finished) {
            return;
        }
    }
}

#endif /* EW_PIO_MODEL_H */
