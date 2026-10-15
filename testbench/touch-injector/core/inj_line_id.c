/*
 * SPDX-License-Identifier: MIT
 */

#include "inj_line_id.h"

#include <string.h>

#define BOTH (INJ_LINE_GP4 | INJ_LINE_GP5)
#define NONE 0u

/* A level change waiting to outlast the glitch filter. */
typedef struct {
    bool     pending;
    uint32_t at;
    uint8_t  level;
} pend_t;

typedef struct {
    uint32_t idle;          /* samples of both-high that make idle       */
    uint8_t  s;             /* filtered state                           */
    uint32_t high_since;    /* when s last became BOTH                  */
    uint8_t  rise_line;     /* the line whose rise made it BOTH         */
    bool     in_tx;
    uint32_t start_at;
    uint8_t  start_line;    /* NONE when both fell at once              */
    uint32_t rise_at[2];    /* each line's last rise                    */
    bool     rose[2];
    uint32_t tx;
    uint32_t named[2];      /* transactions naming GP4, GP5 as SDA      */
    uint32_t edges[2];
} cls_t;

static void end_tx(cls_t *c)
{
    const uint8_t stop_line = c->rise_line;
    c->in_tx = false;
    ++c->tx;
    if (c->start_line != NONE && c->start_line == stop_line) {
        ++c->named[c->start_line == INJ_LINE_GP4 ? 0 : 1];
    }
}

/* One filtered level change of @p line to @p level at @p t. */
static void apply(cls_t *c, uint32_t t, uint8_t line, uint8_t level)
{
    const uint8_t before = c->s;
    const uint8_t after = level ? (uint8_t)(before | line)
                                : (uint8_t)(before & ~line);
    if (after == before) {
        return;
    }
    const int k = line == INJ_LINE_GP4 ? 0 : 1;
    ++c->edges[k];
    c->s = after;

    if (!level) {
        if (before == BOTH) {
            /* Out of both-high: a START when the bus was idle, otherwise
             * a repeated START or a gap inside one transaction. */
            if (t - c->high_since >= c->idle) {
                if (c->in_tx) {
                    end_tx(c);
                }
                c->in_tx = true;
                c->start_at = t;
                c->start_line = line;
            }
        } else if (c->in_tx && t == c->start_at
                   && before == (uint8_t)(BOTH & ~c->start_line)) {
            /* Both fell on one sample: the START names no line. */
            c->start_line = NONE;
        }
        return;
    }
    c->rise_at[k] = t;
    c->rose[k] = true;
    if (after == BOTH) {
        c->high_since = t;
        /* Both rose on one sample: the STOP names no line. */
        c->rise_line = (c->rose[1 - k] && c->rise_at[1 - k] == t) ? NONE
                                                                   : line;
    }
}

/* Emit the pending changes due by @p t, earliest first. */
static void flush(cls_t *c, pend_t p[2], uint32_t t)
{
    for (;;) {
        int pick = -1;
        for (int i = 0; i < 2; ++i) {
            if (p[i].pending && p[i].at + INJ_LINE_GLITCH <= t
                && (pick < 0 || p[i].at < p[pick].at)) {
                pick = i;
            }
        }
        if (pick < 0) {
            return;
        }
        p[pick].pending = false;
        apply(c, p[pick].at, pick == 0 ? INJ_LINE_GP4 : INJ_LINE_GP5,
              p[pick].level);
    }
}

void inj_line_classify(uint8_t first, const inj_edge_t *edges, uint32_t n,
                       uint32_t samples, uint32_t rate_ksps,
                       inj_line_result_t *out)
{
    cls_t c;
    memset(&c, 0, sizeof(c));
    c.idle = (uint32_t)(((uint64_t)INJ_LINE_IDLE_NS * rate_ksps + 999999u)
                        / 1000000u);
    if (c.idle == 0u) {
        c.idle = 1;
    }
    c.s = (uint8_t)(first & BOTH);
    c.high_since = 0;
    c.rise_line = NONE;

    pend_t p[2];
    memset(p, 0, sizeof(p));
    /* The raw level of each line, glitches included. */
    uint8_t raw = c.s;

    for (uint32_t i = 0; edges != NULL && i < n; ++i) {
        const uint32_t t = edges[i].at;
        const uint8_t st = (uint8_t)(edges[i].state & BOTH);
        for (int k = 0; k < 2; ++k) {
            const uint8_t line = k == 0 ? INJ_LINE_GP4 : INJ_LINE_GP5;
            if (((raw ^ st) & line) == 0u) {
                continue;
            }
            /* Every change that has outlasted the filter by now. */
            flush(&c, p, t);
            if (p[k].pending) {
                /* Back within the glitch time: neither change happened. */
                p[k].pending = false;
            } else {
                p[k] = (pend_t){ .pending = true, .at = t,
                                 .level = (st & line) ? 1u : 0u };
            }
        }
        raw = st;
    }
    flush(&c, p, samples);
    /* An idle span that reaches the end of the window closes the last
     * transaction. */
    if (c.in_tx && c.s == BOTH && samples - c.high_since >= c.idle) {
        end_tx(&c);
    }

    memset(out, 0, sizeof(*out));
    out->tx = c.tx;
    out->edges4 = c.edges[0];
    out->edges5 = c.edges[1];
    out->end_state = c.s;
    out->rate_ksps = rate_ksps;
    const uint32_t major = c.named[0] >= c.named[1] ? c.named[0] : c.named[1];
    out->agree_pct = c.tx ? (uint32_t)((uint64_t)major * 100u / c.tx) : 0u;

    if (c.tx >= INJ_LINE_MIN_TX && out->agree_pct >= INJ_LINE_MIN_AGREE_PCT) {
        out->verdict = c.named[0] >= c.named[1] ? INJ_WIRING_OK
                                                : INJ_WIRING_SWAPPED;
    } else if ((c.edges[0] == 0u && (c.s & INJ_LINE_GP4) == 0u)
               || (c.edges[1] == 0u && (c.s & INJ_LINE_GP5) == 0u)) {
        out->verdict = INJ_WIRING_LOW;
    } else if (c.edges[0] == 0u && c.edges[1] == 0u) {
        out->verdict = INJ_WIRING_IDLE;
    } else {
        out->verdict = INJ_WIRING_NOISE;
    }
}

const char *inj_wiring_name(inj_wiring_t v)
{
    switch (v) {
    case INJ_WIRING_OK:      return "ok";
    case INJ_WIRING_SWAPPED: return "swapped";
    case INJ_WIRING_IDLE:    return "idle";
    case INJ_WIRING_LOW:     return "low";
    case INJ_WIRING_NOISE:   return "noise";
    default:                 return "none";
    }
}
