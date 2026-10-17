/*
 * The bench the stick programming suites run the engine on: an arm that
 * follows the ARM the engine asks for, a supply that switches when asked
 * and is read at a chosen rate, and the simulated ESC (electronic speed
 * controller) drawing the current.  rig_hand() gives a profile manual
 * steps of the case's own.  One tick is 1 ms.
 *
 * For test_esc_stick.c, test_esc_stick_rules.c, test_esc_stick_hand.c and
 * test_esc_stick_entry.c.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "greatest.h"

#include "esc_profile.h"
#include "esc_sim.h"
#include "esc_stick.h"

typedef struct {
    esc_stick_t        e;
    esc_sim_t          sim;
    esc_stick_timing_t t;
    const esc_profile_t *p;

    uint32_t now;
    bool     armed;
    bool     arm_refused;     /* the bench never arms                 */
    uint32_t stops;
    uint32_t pressed;         /* of the stops, those pressed          */
    bool     link;
    bool     supply_on;
    bool     supply_dead;     /* the output never comes on            */
    bool     supply_stuck;    /* the output never goes off            */
    bool     asked;           /* what the engine last asked           */
    uint32_t off_at;          /* when it asked the output off         */
    uint32_t off_lag_ms;      /* the module goes off this much later  */
    uint32_t moved_while_on;  /* stick moves with the module powered
                                 after the run asked it off           */
    bool     online;

    uint32_t read_iv;         /* a reading every this many ms         */
    uint32_t jitter;          /* plus up to this much                 */
    uint32_t next_read;
    uint32_t seq;
    uint32_t lcg;
    bool     readings_stop;
    int32_t  extra_ma;        /* added to every reading, on or off    */
    int32_t  off_ma;          /* what the module reads while off      */
    unsigned no_current;      /* the next readings carry no current   */
    uint32_t late_once_at;    /* one reading this late, at this time  */
    uint32_t late_once_by;
    uint32_t skew_ms;         /* readings stamped this far ahead      */
    uint32_t skip;            /* readings the supply took unseen      */
    float    on_pct[8];       /* the stick at each power-up           */
    unsigned on_n;
    bool     no_watch;        /* nobody taps DONE once the ESC has
                                 confirmed (before_power_off)         */
} rig_t;

static rig_t r;

static inline int item_index(const esc_profile_t *p, uint8_t number)
{
    for (unsigned i = 0; i < p->item_count; ++i) {
        if (p->items[i].number == number) {
            return (int)i;
        }
    }
    return -1;
}

static inline int value_index(const esc_profile_t *p, uint8_t item,
                              uint8_t number)
{
    const int i = item_index(p, item);
    if (i < 0) {
        return -1;
    }
    for (unsigned k = 0; k < p->items[i].value_count; ++k) {
        if (p->items[i].values[k].number == number) {
            return (int)k;
        }
    }
    return -1;
}

/* A change by the numbers the ESC sounds. */
static inline esc_stick_change_t change(uint8_t item, uint8_t value)
{
    const int i = item_index(r.p, item);
    const int v = value_index(r.p, item, value);
    esc_stick_change_t c = { (uint8_t)((i < 0) ? 255 : i),
                             (uint8_t)((v < 0) ? 255 : v) };
    return c;
}

/* The bench with its clock at @p t0. */
static inline void rig_at(const char *id, uint32_t t0)
{
    memset(&r, 0, sizeof(r));
    r.p = esc_profiles_find(id);
    esc_stick_timing_defaults(&r.t);
    if (r.p != NULL && r.p->entry_hold_ms != 0u) {
        r.t.entry_ms = r.p->entry_hold_ms;
    }
    esc_sim_cfg_t c;
    esc_sim_defaults(&c);
    c.wait_hand = true;         /* the menu starts at the pull */
    esc_sim_init(&r.sim, r.p, &c);
    r.now = t0;
    r.link = true;
    r.online = true;
    r.read_iv = 50u;
    r.lcg = 7u;
}

static inline void rig(const char *id)
{
    rig_at(id, 1000u);
}

static inline esc_stick_bench_t bench(void)
{
    esc_stick_bench_t b = { r.now, r.armed, r.stops, r.link, r.pressed };
    return b;
}

static inline bool start(const esc_stick_change_t *ch, size_t n)
{
    const esc_stick_bench_t b = bench();
    const char *why = NULL;
    const bool ok = esc_stick_start(&r.e, r.p, ch, n, &r.t, 7600u, 1000u,
                                    &b, &why);
    r.next_read = r.now;
    return ok;
}

/* One millisecond of bench. */
static inline void tick(void)
{
    const esc_stick_out_t *o = esc_stick_out(&r.e);
    if (!r.arm_refused) {
        r.armed = o->arm;
    }
    /* The request, and apart from it the module, which follows an OFF
     * off_lag_ms behind, as the PD mini follows a link exchange and its
     * own transaction. */
    if (r.asked && !o->supply_on) {
        r.off_at = r.now;
    }
    const bool was_on = r.supply_on;
    r.asked = o->supply_on;
    if (r.asked) {
        r.supply_on = !r.supply_dead;
    } else if (r.supply_on && !r.supply_stuck
               && r.now - r.off_at >= r.off_lag_ms) {
        r.supply_on = false;
    }
    static float last_pct = -2.0f;
    const float pct = r.armed ? o->throttle_pct : -1.0f;
    if (r.supply_on && !was_on && r.on_n < 8u) {
        r.on_pct[r.on_n++] = pct;
    }
    if (pct != last_pct && r.supply_on && !r.asked) {
        r.moved_while_on++;
    }
    last_pct = pct;
    const int32_t ma = esc_sim_step(&r.sim, r.now, r.supply_on, pct);
    if (!r.readings_stop && (int32_t)(r.now - r.next_read) >= 0) {
        r.seq += 1u + r.skip;
        esc_stick_sample_t s = {
            .seq = r.seq, .at_ms = r.now + r.skew_ms,
            .ma = (r.supply_on ? ma : r.off_ma) + r.extra_ma,
            .current_ok = r.no_current == 0u, .output = r.asked,
            .reported_on = r.supply_on, .online = r.online,
        };
        esc_stick_sample(&r.e, &s);
        if (r.no_current > 0u) {
            r.no_current--;
        }
        r.lcg = r.lcg * 1103515245u + 12345u;
        const uint32_t j = (r.jitter > 0u) ? (r.lcg >> 8) % (r.jitter + 1u)
                                           : 0u;
        r.next_read = r.now + r.read_iv + j;
        if (r.late_once_at != 0u && r.now >= r.late_once_at) {
            r.next_read += r.late_once_by;
            r.late_once_at = 0u;
        }
    }
    /* The operator watching the ESC confirm taps DONE as soon as it
     * counts: the run goes on as before the step existed. */
    if (!r.no_watch && r.e.phase == ESC_STICK_HAND_END
        && esc_stick_hand_ready(&r.e)) {
        (void)esc_stick_confirm(&r.e);
    }
    const esc_stick_bench_t b = bench();
    esc_stick_step(&r.e, &b);
    r.now++;
}

/* Until the run ends or @p ms pass. */
static inline void run_for(uint32_t ms)
{
    for (uint32_t i = 0; i < ms && esc_stick_running(&r.e); ++i) {
        tick();
    }
}

static inline void run_until_phase(esc_stick_phase_t ph, uint32_t ms)
{
    for (uint32_t i = 0; i < ms && esc_stick_running(&r.e)
                         && r.e.phase != ph; ++i) {
        tick();
    }
}

/* Until the run asks for the step that starts the menu. */
static inline void run_until_asked(uint32_t ms)
{
    for (uint32_t i = 0; i < ms && esc_stick_running(&r.e); ++i) {
        const esc_manual_t *m = esc_stick_hand(&r.e);
        if (m != NULL && m->when == ESC_MANUAL_BEFORE_MENU && r.e.hand_menu) {
            return;
        }
        tick();
    }
}

/* The person does it now -- the jumper pulled, the button pressed -- and,
 * with @p done, taps DONE once it counts. */
static inline void act(bool done)
{
    esc_sim_hand(&r.sim, r.now);
    if (done) {
        run_for(ESC_STICK_HAND_MIN_MS);
        (void)esc_stick_confirm(&r.e);
    }
}

/* The run ended where everything is safe: throttle at rest, supply off,
 * the arm let go. */
static inline void ended_safe(void)
{
    const esc_stick_out_t *o = esc_stick_out(&r.e);
    CHECK(!esc_stick_running(&r.e));
    CHECK(!o->arm);
    CHECK(!o->supply_on);
    CHECK(o->throttle_pct == ESC_STICK_PCT_MIN);
}

/* A copy of a profile of record with manual steps of the test's own. */
static esc_profile_t g_hand;
static esc_manual_t  g_hand_steps[ESC_MANUAL_MAX];

static inline void rig_hand(const char *id, const esc_manual_t *m, uint8_t n)
{
    rig(id);
    g_hand = *r.p;
    memcpy(g_hand_steps, m, n * sizeof(*m));
    g_hand.automatable = ESC_AUTO_ASSISTED;
    g_hand.manual = g_hand_steps;
    g_hand.manual_count = n;
    r.p = &g_hand;
    esc_sim_cfg_t c;
    esc_sim_defaults(&c);
    c.wait_hand = true;
    esc_sim_init(&r.sim, r.p, &c);
}

/* Until the run asks for the step before the supply goes off. */
static inline void run_until_end_step(uint32_t ms)
{
    r.no_watch = true;
    for (uint32_t i = 0; i < ms && esc_stick_running(&r.e)
                         && r.e.phase != ESC_STICK_HAND_END; ++i) {
        if (r.e.hand_menu) {
            act(false);                 /* the button pressed */
        }
        tick();
    }
}
