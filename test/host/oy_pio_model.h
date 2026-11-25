/*
 * A model of the PIO (programmable input/output) state machine of programming
 * for YGE ESCs, for test_openyge_pio.c.
 *
 * assemble() reads protocols/openyge/rp2350/oy_line.pio with an assembler for
 * the instructions it uses; model_run() steps the program one state machine
 * cycle at a time against the wire the test gives (ext_edge(), ext_byte() in
 * the suite).  test_openyge_pio.c says what the model covers and what it
 * leaves to the chip.  Include it after greatest.h, once per suite.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef OY_PIO_MODEL_H
#define OY_PIO_MODEL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openyge_pio.h"

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

/* A source or destination of IN, OUT, SET and MOV; -1 when unknown. */
static int reg_code(const char *t)
{
    static const char *const names[] = { "pins", "x", "y", "null", "pindirs",
                                         "status" };

    for (int i = 0; i < 6; ++i) {
        if (strcmp(t, names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static int cond_code(const char *t)
{
    if (strcmp(t, "!x") == 0) { return 1; }
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
    if (n == 1u && strcmp(tok[0], "nop") == 0) {
        return 0xA042;                          /* mov y, y */
    }
    if (n == 1u && strcmp(tok[0], "pull") == 0) {
        return 0x80A0;
    }
    if (n == 2u && strcmp(tok[0], "pull") == 0
        && strcmp(tok[1], "ifempty") == 0) {
        return 0x80E0;
    }
    if (n == 2u && strcmp(tok[0], "push") == 0
        && strcmp(tok[1], "noblock") == 0) {
        return 0x8000;
    }
    if (n == 3u && strcmp(tok[0], "mov") == 0 && strcmp(tok[1], "x") == 0
        && strcmp(tok[2], "status") == 0) {
        return 0xA025;
    }
    if (n == 3u) {
        const int r = reg_code(tok[1]);
        const unsigned arg = (unsigned)atoi(tok[2]);

        if (r < 0 || r > 4 || arg > 32u) {
            return -1;
        }
        if (strcmp(tok[0], "out") == 0) {
            return (long)(0x6000u | ((unsigned)r << 5) | (arg & 31u));
        }
        if (strcmp(tok[0], "in") == 0 && r < 4) {
            return (long)(0x4000u | ((unsigned)r << 5) | (arg & 31u));
        }
        if (strcmp(tok[0], "set") == 0 && r != 3) {
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

#define MAX_PAD  8000u
#define MAX_EXT  4000u
#define MAX_MARK 8u

typedef struct {
    uint64_t cyc;
    bool dir;          /* the pad drives */
    bool latch;
} pad_t;

typedef struct {
    double ns;         /* after cycle 0 */
    bool level;
} ext_t;

typedef struct {
    /* what the test gives */
    uint32_t div256;
    uint32_t sys_hz;
    uint32_t tx[OY_PIO_TX_WORDS + 1u];
    unsigned n_tx;
    uint64_t tx_at;         /* the cycle the transmit channel starts */
    ext_t ext[MAX_EXT];
    unsigned n_ext;
    bool ext0;              /* the released wire before the first change */
    unsigned rx_room;       /* entries the receive channel takes */
    /* what the run leaves */
    pad_t pad[MAX_PAD];
    unsigned n_pad;
    uint16_t rx[OY_PIO_RX_ENTRIES];   /* the channel reads the upper half */
    uint64_t rx_at[OY_PIO_RX_ENTRIES];
    unsigned n_rx;
    unsigned dropped;       /* words a full receive FIFO did not take */
    uint64_t send[MAX_MARK];      /* cycles of the `pull` at OY_PIO_SEND_PC */
    unsigned n_send;
    uint64_t release[MAX_MARK];   /* cycles of the `set pindirs, 0` */
    unsigned n_release;
} model_t;

static uint64_t t_of(const model_t *m, uint64_t cyc)
{
    return cyc * m->div256;
}

static bool ext_level(const model_t *m, uint64_t t, unsigned *cursor)
{
    const double ns = (double)t * 1e9 / ((double)m->sys_hz * 256.0);

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
    unsigned cursor = 0;
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t isr = 0;
    uint32_t osr = 0;
    unsigned osr_count = 32;
    uint32_t txf[4];
    uint32_t rxf[4];
    unsigned n_txf = 0;
    unsigned n_rxf = 0;
    unsigned tx_next = 0;
    unsigned pc = OY_PIO_WAIT_LO_PC;
    unsigned delay = 0;
    bool dir = false;
    bool latch = true;

    m->n_pad = 0;
    m->n_rx = 0;
    m->dropped = 0;
    m->n_send = 0;
    m->n_release = 0;

    for (uint64_t cyc = 0; cyc < max_cycles; ++cyc) {
        const bool was_dir = dir;
        const bool was_latch = latch;

        /* The channels: faster than a state machine cycle. */
        while (cyc >= m->tx_at && tx_next < m->n_tx && n_txf < 4u) {
            txf[n_txf++] = m->tx[tx_next++];
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

                if (sel == 1u) {
                    take = x == 0u;
                } else if (sel == 2u) {
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

                isr = (isr >> arg) | ((v & ((1u << arg) - 1u)) << (32u - arg));
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
                } else if (sel == 4u) {
                    dir = (data & 1u) != 0u;
                }
                break;
            }
            case 4:
                if ((w & 0x80u) == 0u) {                        /* PUSH */
                    if (n_rxf == 4u) {
                        ++m->dropped;
                    } else {
                        rxf[n_rxf++] = isr;
                    }
                    isr = 0;
                    break;
                }
                /* PULL */
                if ((w & 0x40u) != 0u && osr_count < 32u) {
                    break;
                }
                if (pc == OY_PIO_SEND_PC && m->n_send < MAX_MARK) {
                    m->send[m->n_send++] = cyc;
                }
                if (n_txf == 0u) {
                    stalled = true;
                    break;
                }
                osr = txf[0];
                memmove(&txf[0], &txf[1], (n_txf - 1u) * sizeof(txf[0]));
                --n_txf;
                osr_count = 0;
                break;
            case 5:                                             /* MOV */
                if ((w & 7u) == 5u && sel == 1u) {
                    x = n_txf < 1u ? 0xFFFFFFFFu : 0u;      /* x <- status */
                }
                break;                                      /* y <- y: nop */
            default:                                            /* SET */
                if (sel == 2u) {
                    y = arg;
                } else if (sel == 1u) {
                    x = arg;
                } else if (sel == 4u) {
                    dir = (arg & 1u) != 0u;
                } else {
                    latch = (arg & 1u) != 0u;
                }
                if (pc == OY_PIO_RELEASE_PC && m->n_release < MAX_MARK) {
                    m->release[m->n_release++] = cyc;
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
    }
}

#endif /* OY_PIO_MODEL_H */
