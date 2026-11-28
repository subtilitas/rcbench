/**
 * @file
 * @brief The Guition JC3636K518C's encoder decoded as quadrature: see
 *        dk518_enc.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dk518_enc.h"

#include <stdio.h>
#include <string.h>

/** @brief The next pair clockwise: 11 > 01 > 00 > 10 > 11. */
static const uint8_t k_cw[4] = { 2u, 0u, 3u, 1u };

/**
 * @brief Start a trace at a pair.
 * @param t    the trace
 * @param pair where it starts
 */
static void trace_start(dk518_trace_t *t, uint8_t pair)
{
    memset(t, 0, sizeof *t);
    t->pair[0] = pair;
    t->n = 1u;
}

/**
 * @brief Add an edge to a trace; pairs past DK518_TRACE_MAX are counted
 *        only.
 * @param t    the trace
 * @param pair the pair after the edge
 */
static void trace_add(dk518_trace_t *t, uint8_t pair)
{
    if (t->n < DK518_TRACE_MAX) {
        t->pair[t->n++] = pair;
    }
    if (t->edges < UINT16_MAX) {
        ++t->edges;
    }
}

/**
 * @brief Count the sum as one detent, keep its trace and start the next.
 * @param e   the decode
 * @param now the time of the count
 * @return +1 or -1
 */
static int32_t count(dk518_enc_t *e, uint32_t now)
{
    const int32_t d = e->sum > 0 ? 1 : -1;
    e->last = e->run;
    e->last.detents = d;
    e->last.ms = now - e->first_ms;
    e->fresh = true;
    e->sum = 0;
    trace_start(&e->run, e->pair);
    return d;
}

void dk518_enc_init(dk518_enc_t *e)
{
    memset(e, 0, sizeof *e);
}

int32_t dk518_enc_edge(dk518_enc_t *e, uint8_t pair, uint32_t now)
{
    const uint8_t p = pair & 3u;
    if (!e->primed) {
        e->primed = true;
        e->pair = p;
        e->last_ms = now;
        trace_start(&e->run, p);
        return 0;
    }
    e->last_ms = now;
    if (p == e->pair) {
        return 0;
    }
    if (e->run.edges == 0u) {
        e->first_ms = now;
    }
    if (k_cw[e->pair] == p) {
        ++e->sum;
    } else if (k_cw[p] == e->pair) {
        --e->sum;
    }
    e->pair = p;
    e->quiet = false;
    trace_add(&e->run, p);
    if (e->home_ok && p == e->home && e->sum != 0) {
        return count(e, now);
    }
    return 0;
}

int32_t dk518_enc_poll(dk518_enc_t *e, uint32_t now)
{
    if (!e->primed || e->quiet || now - e->last_ms < DK518_REST_MS) {
        return 0;
    }
    e->quiet = true;
    if (!e->home_ok) {
        e->home_ok = true;
        e->home = e->pair;
    }
    if (e->sum != 0) {
        return count(e, now);
    }
    trace_start(&e->run, e->pair);
    return 0;
}

bool dk518_enc_last(dk518_enc_t *e, dk518_trace_t *out)
{
    if (!e->fresh) {
        return false;
    }
    e->fresh = false;
    *out = e->last;
    return true;
}

void dk518_enc_text(const dk518_trace_t *t, char *out, unsigned n)
{
    static const char *const k_pair[4] = { "00", "01", "10", "11" };
    if (out == NULL || n == 0u) {
        return;
    }
    const int32_t d = t->detents;
    int k = snprintf(out, n, "%s %ld, %u edges, %lums: ",
                     d < 0 ? "CCW" : "CW", (long)(d < 0 ? -d : d),
                     (unsigned)t->edges, (unsigned long)t->ms);
    for (unsigned i = 0; i < t->n && k >= 0 && (unsigned)k < n; ++i) {
        k += snprintf(out + k, n - (unsigned)k, "%s%s", i == 0u ? "" : ">",
                      k_pair[t->pair[i] & 3u]);
    }
}
