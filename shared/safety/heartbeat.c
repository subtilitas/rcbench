/*
 * SPDX-License-Identifier: MIT
 */

#include "heartbeat.h"

#include <string.h>

/*
 * Every comparison is an unsigned difference against a timeout, never a
 * comparison of two timestamps.  A 32-bit millisecond counter wraps after
 * 49.7 days, and `now < then` is wrong for about 1 ms either side of the
 * wrap; the subtraction wraps with the counter and stays correct.
 */
static inline bool elapsed_at_least(uint32_t now, uint32_t then, uint32_t ms)
{
    return (uint32_t)(now - then) >= ms;
}

/* --------------------------------------------------------------- generator */

void heartbeat_gen_init(heartbeat_gen_t *g)
{
    if (g != NULL) {
        memset(g, 0, sizeof(*g));
    }
}

bool heartbeat_gen_step(heartbeat_gen_t *g, uint32_t now_ms, bool alive)
{
    if (g == NULL) {
        return false;
    }

    if (!alive) {
        /*
         * Low at once, and back to not-started.  Not-started makes recovery
         * wait a full period rather than emit the fragment of one left over
         * from before the stop; a short first interval is rejected by the
         * monitor's floor.  The clock is taken on the first step after
         * recovery, because `alive` may be withdrawn for a long time and the
         * period runs from the moment of recovery.
         */
        g->level   = false;
        g->started = false;
        return false;
    }

    if (!g->started) {
        g->started = true;
        g->last_ms = now_ms;
        return g->level;   /* still low; the first edge is one period away */
    }

    if (elapsed_at_least(now_ms, g->last_ms, HEARTBEAT_PERIOD_MS)) {
        g->level   = !g->level;
        g->last_ms = now_ms;
    }
    return g->level;
}

/* ----------------------------------------------------------------- monitor */

void heartbeat_mon_init(heartbeat_mon_t *m)
{
    if (m != NULL) {
        memset(m, 0, sizeof(*m));
    }
}

void heartbeat_mon_edge(heartbeat_mon_t *m, uint32_t now_ms)
{
    if (m == NULL) {
        return;
    }

    if (!m->have_edge) {
        /* One edge is not an interval, and intervals are what is judged.
         * Take the timestamp and wait for the next. */
        m->have_edge    = true;
        m->last_edge_ms = now_ms;
        return;
    }

    const uint32_t gap = (uint32_t)(now_ms - m->last_edge_ms);
    m->last_edge_ms = now_ms;

    if (gap < HEARTBEAT_MIN_GAP_MS) {
        /*
         * Too fast to be a render loop: the case the monostable cannot see,
         * because whatever does this retriggers it and it holds the outputs
         * enabled throughout.
         *
         * The run is reset rather than decremented, and the line is dropped
         * immediately if it was up.  Noise keeps no credit earned before it
         * started.
         */
        ++m->rejected_fast;
        m->good_run = 0;
        m->alive    = false;
        return;
    }
    if (gap > HEARTBEAT_MAX_GAP_MS) {
        ++m->rejected_slow;
        m->good_run = 0;
        m->alive    = false;
        return;
    }

    if (m->good_run < HEARTBEAT_GOOD_RUN) {
        ++m->good_run;
    }
    if (m->good_run >= HEARTBEAT_GOOD_RUN) {
        m->alive = true;
    }
}

bool heartbeat_mon_alive(heartbeat_mon_t *m, uint32_t now_ms)
{
    if (m == NULL) {
        return false;
    }

    /*
     * Silence is checked here rather than in the edge handler: a quiet line
     * raises no edges to handle.  This is the only path that catches an
     * unplugged cable.
     */
    if (!m->have_edge
        || elapsed_at_least(now_ms, m->last_edge_ms, HEARTBEAT_MAX_GAP_MS)) {
        if (m->alive || m->good_run != 0) {
            ++m->rejected_slow;
        }
        m->good_run = 0;
        m->alive    = false;
    }
    return m->alive;
}
