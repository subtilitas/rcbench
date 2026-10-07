/*
 * Stick programming against the simulated ESC (electronic speed
 * controller): the beep detector on its own, then whole runs through a
 * modelled bench -- an arm that follows the ARM the engine asks for, a
 * supply that switches when asked and is read at a chosen rate, and the
 * simulation drawing the current.
 *
 * The cases that matter most are the ones where a count could go wrong: a
 * beep that never shows in the current must not become a selection of the
 * number below it, and a menu that does not match the profile must time
 * out rather than select something.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "esc_profile.h"
#include "esc_sim.h"
#include "esc_stick.h"

/* ------------------------------------------------------------ the rig */

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
    uint32_t late_once_at;    /* one reading this late, at this time  */
    uint32_t late_once_by;
    uint32_t skew_ms;         /* readings stamped this far ahead      */
    uint32_t skip;            /* readings the supply took unseen      */
    float    on_pct[8];       /* the stick at each power-up           */
    unsigned on_n;
} rig_t;

static rig_t r;

static int item_index(const esc_profile_t *p, uint8_t number)
{
    for (unsigned i = 0; i < p->item_count; ++i) {
        if (p->items[i].number == number) {
            return (int)i;
        }
    }
    return -1;
}

static int value_index(const esc_profile_t *p, uint8_t item, uint8_t number)
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
static esc_stick_change_t change(uint8_t item, uint8_t value)
{
    const int i = item_index(r.p, item);
    const int v = value_index(r.p, item, value);
    esc_stick_change_t c = { (uint8_t)((i < 0) ? 255 : i),
                             (uint8_t)((v < 0) ? 255 : v) };
    return c;
}

static void rig(const char *id)
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
    r.now = 1000u;
    r.link = true;
    r.online = true;
    r.read_iv = 50u;
    r.lcg = 7u;
}

static esc_stick_bench_t bench(void)
{
    esc_stick_bench_t b = { r.now, r.armed, r.stops, r.link, r.pressed };
    return b;
}

static bool start(const esc_stick_change_t *ch, size_t n)
{
    const esc_stick_bench_t b = bench();
    const char *why = NULL;
    const bool ok = esc_stick_start(&r.e, r.p, ch, n, &r.t, 7600u, 1000u,
                                    &b, &why);
    r.next_read = r.now;
    return ok;
}

/* One millisecond of bench. */
static void tick(void)
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
            .ma = (r.supply_on ? ma : 0) + r.extra_ma,
            .current_ok = true, .output = r.asked,
            .reported_on = r.supply_on, .online = r.online,
        };
        esc_stick_sample(&r.e, &s);
        r.lcg = r.lcg * 1103515245u + 12345u;
        const uint32_t j = (r.jitter > 0u) ? (r.lcg >> 8) % (r.jitter + 1u)
                                           : 0u;
        r.next_read = r.now + r.read_iv + j;
        if (r.late_once_at != 0u && r.now >= r.late_once_at) {
            r.next_read += r.late_once_by;
            r.late_once_at = 0u;
        }
    }
    const esc_stick_bench_t b = bench();
    esc_stick_step(&r.e, &b);
    r.now++;
}

/* Until the run ends or @p ms pass. */
static void run_for(uint32_t ms)
{
    for (uint32_t i = 0; i < ms && esc_stick_running(&r.e); ++i) {
        tick();
    }
}

static void run_until_phase(esc_stick_phase_t ph, uint32_t ms)
{
    for (uint32_t i = 0; i < ms && esc_stick_running(&r.e)
                         && r.e.phase != ph; ++i) {
        tick();
    }
}

/* Until the run asks for the step that starts the menu. */
static void run_until_asked(uint32_t ms)
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
static void act(bool done)
{
    esc_sim_hand(&r.sim, r.now);
    if (done) {
        run_for(ESC_STICK_HAND_MIN_MS);
        (void)esc_stick_confirm(&r.e);
    }
}

/* The run ended where everything is safe: throttle at rest, supply off,
 * the arm let go. */
static void ended_safe(void)
{
    const esc_stick_out_t *o = esc_stick_out(&r.e);
    CHECK(!esc_stick_running(&r.e));
    CHECK(!o->arm);
    CHECK(!o->supply_on);
    CHECK(o->throttle_pct == ESC_STICK_PCT_MIN);
}

/* ------------------------------------------------------- the detector */

static esc_det_t det;

/* The time the last det_fresh() left the detector at. */
static uint32_t g_at;

static void det_fresh(uint8_t les)
{
    esc_stick_timing_t t;
    esc_stick_timing_defaults(&t);
    esc_det_init(&det, &t, les);
    /* The floor at 150 mA, then counting, after the quiet that has to come
     * first. */
    (void)esc_det_reading(&det, 0u, 150, false);
    esc_det_count(&det);
    g_at = 0u;
    for (int i = 0; i < 15; ++i) {
        g_at += 50u;
        (void)esc_det_reading(&det, g_at, 150, false);
    }
}

/* Readings every @p iv ms from @p *at: @p n of them at @p ma. */
static esc_det_event_t feed(uint32_t *at, uint32_t iv, int n, int32_t ma)
{
    esc_det_event_t last = ESC_DET_NONE;
    for (int i = 0; i < n; ++i) {
        *at += iv;
        const esc_det_event_t ev = esc_det_reading(&det, *at, ma, false);
        if (ev != ESC_DET_NONE) {
            last = ev;
        }
    }
    return last;
}

TEST_CASE(beeps_make_a_group_that_silence_ends)
{
    det_fresh(0u);
    uint32_t at = g_at;
    for (int b = 0; b < 3; ++b) {
        (void)feed(&at, 50u, 5, 750);      /* 250 ms beep */
        CHECK_EQ(feed(&at, 50u, 1, 150), ESC_DET_PULSE);
        (void)feed(&at, 50u, 4, 150);      /* the rest of a 250 ms gap */
    }
    CHECK_EQ(feed(&at, 50u, 20, 150), ESC_DET_GROUP);
    CHECK_EQ(det.count, 3);
    CHECK(det.valid);
}

TEST_CASE(long_beeps_count_by_the_profiles_measure)
{
    det_fresh(5u);
    uint32_t at = g_at;
    (void)feed(&at, 50u, 16, 750);         /* 800 ms: long */
    (void)feed(&at, 50u, 5, 150);
    (void)feed(&at, 50u, 5, 750);          /* short */
    (void)feed(&at, 50u, 5, 150);
    (void)feed(&at, 50u, 5, 750);          /* short */
    CHECK_EQ(feed(&at, 50u, 20, 150), ESC_DET_GROUP);
    CHECK_EQ(det.count, 7);
    CHECK(det.valid);
}

/* A long beep in a menu that sounds none, or after a short one, is not the
 * menu speaking. */
TEST_CASE(a_long_beep_where_none_belongs_spoils_the_group)
{
    det_fresh(0u);
    uint32_t at = g_at;
    (void)feed(&at, 50u, 16, 750);
    CHECK_EQ(feed(&at, 50u, 20, 150), ESC_DET_GROUP);
    CHECK(!det.valid);

    det_fresh(5u);
    at = g_at;
    (void)feed(&at, 50u, 5, 750);
    (void)feed(&at, 50u, 5, 150);
    (void)feed(&at, 50u, 16, 750);
    CHECK_EQ(feed(&at, 50u, 20, 150), ESC_DET_GROUP);
    CHECK(!det.valid);
}

TEST_CASE(pulses_and_gaps_outside_their_lengths_spoil_the_group)
{
    /* One reading of a pulse where a beep must hold four. */
    det_fresh(0u);
    uint32_t at = g_at;
    (void)feed(&at, 50u, 2, 150);
    (void)feed(&at, 50u, 1, 750);
    CHECK_EQ(feed(&at, 50u, 20, 150), ESC_DET_GROUP);
    CHECK(!det.valid);

    /* Longer than any beep. */
    det_fresh(5u);
    at = g_at;
    (void)feed(&at, 50u, 40, 750);
    CHECK_EQ(feed(&at, 50u, 20, 150), ESC_DET_GROUP);
    CHECK(!det.valid);

    /* A gap of one reading between two beeps. */
    det_fresh(0u);
    at = g_at;
    (void)feed(&at, 50u, 5, 750);
    (void)feed(&at, 50u, 1, 150);
    (void)feed(&at, 50u, 5, 750);
    CHECK_EQ(feed(&at, 50u, 20, 150), ESC_DET_GROUP);
    CHECK(!det.valid);
}

/* Between the two thresholds the state holds: no chatter on a reading that
 * sits near the edge. */
TEST_CASE(hysteresis_holds_a_beep_through_a_dip)
{
    det_fresh(0u);
    uint32_t at = g_at;
    (void)feed(&at, 50u, 2, 750);
    /* 150 + 100 - 40 = 210 is the release: 230 is still a beep. */
    CHECK_EQ(feed(&at, 50u, 1, 230), ESC_DET_NONE);
    (void)feed(&at, 50u, 2, 750);
    CHECK_EQ(feed(&at, 50u, 1, 150), ESC_DET_PULSE);
    CHECK_EQ(feed(&at, 50u, 20, 150), ESC_DET_GROUP);
    CHECK_EQ(det.count, 1);
    CHECK(det.valid);
}

TEST_CASE(a_current_that_stays_high_is_reported_once)
{
    det_fresh(0u);
    uint32_t at = g_at;
    int stuck = 0;
    for (int i = 0; i < 200; ++i) {
        at += 50u;
        if (esc_det_reading(&det, at, 900, false) == ESC_DET_STUCK) {
            ++stuck;
        }
    }
    CHECK_EQ(stuck, 1);
}

/* The floor while the ESC starts is the lowest current seen, so a tone in
 * the entry does not lift it. */
TEST_CASE(the_floor_is_the_lowest_current_before_counting)
{
    esc_stick_timing_t t;
    esc_stick_timing_defaults(&t);
    esc_det_init(&det, &t, 0u);
    (void)esc_det_reading(&det, 0u, 700, false);
    (void)esc_det_reading(&det, 50u, 160, false);
    (void)esc_det_reading(&det, 100u, 900, false);
    CHECK_EQ(esc_det_floor_ma(&det), 160);
    CHECK(esc_det_reading(&det, 150u, 900, false) == ESC_DET_NONE);
    CHECK(!det.group_open);
}

/* ------------------------------------------------------ what can run */

TEST_CASE(the_profiles_the_engine_runs_are_the_counted_menus)
{
    unsigned two = 0, one = 0;
    for (size_t i = 0; i < esc_profiles_count(); ++i) {
        const esc_profile_t *p = esc_profiles_at(i);
        const char *why = NULL;
        switch (esc_stick_kind(p, &why)) {
        case ESC_STICK_KIND_TWO_STAGE: ++two; break;
        case ESC_STICK_KIND_ONE_STAGE: ++one; break;
        default:
            if (why == NULL || why[0] == '\0') {
                T_FAIL("%s refused without a reason", p->id);
            }
            break;
        }
    }
    /* docs/StickProgramming.md gives these counts. */
    CHECK_EQ(two, 13);
    CHECK_EQ(one, 11);

    const char *why = NULL;
    CHECK_EQ(esc_stick_kind(esc_profiles_find("castle-phoenix-edge"), &why),
             ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "yes/no menu");
    CHECK_EQ(esc_stick_kind(esc_profiles_find("graupner-brushless-control-t"),
                            &why), ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "needs a person at the ESC");
    /* A jumper pulled during the menu has no moment the run can know. */
    CHECK_EQ(esc_stick_kind(esc_profiles_find("kontronik-mini20"), &why),
             ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "manual step");
    /* A switch thrown at power-up with the stick at MAX: no hand at a
     * powered ESC whose stick is not at MIN. */
    CHECK_EQ(esc_stick_kind(esc_profiles_find("turnigy-aquastar"), &why),
             ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "manual step");
    CHECK_EQ(esc_stick_kind(esc_profiles_find("kontronik-jazz"), &why),
             ESC_STICK_KIND_ONE_STAGE);
    CHECK_EQ(esc_stick_kind(esc_profiles_find("robbe-roxxy-bl-smart-control"),
                            &why), ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "item and value, one move");
    CHECK_EQ(esc_stick_kind(NULL, &why), ESC_STICK_KIND_NONE);
    /* The YGE menus rest at minimum after a maximum entry whose length no
     * manual gives: the move is not guessed. */
    CHECK_EQ(esc_stick_kind(esc_profiles_find("yge-mode-setup-5"), &why),
             ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "rest move, no entry time");
    CHECK_EQ(esc_stick_listen(esc_profiles_find("yge-mode-setup-5")),
             ESC_THR_MIN);
    /* The Silver Series toggles its brake; it sounds no menu of values. */
    CHECK_EQ(esc_stick_kind(
                 esc_profiles_find("greatplanes-electrifly-silver-series"),
                 &why), ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "menu of its own kind");
    CHECK_EQ(esc_stick_listen(esc_profiles_find("hobbywing-flyfun-8item")),
             ESC_THR_MAX);
}

TEST_CASE(a_run_that_cannot_work_is_refused_before_it_starts)
{
    rig("hobbywing-flyfun-8item");
    const char *why = NULL;
    esc_stick_change_t c[2] = { change(1, 2), change(1, 1) };
    CHECK(!esc_stick_check(r.p, c, 2, &r.t, &why));
    CHECK_STR_EQ(why, "one change per item");
    CHECK(!esc_stick_check(r.p, c, 0, &r.t, &why));
    CHECK(!esc_stick_check(r.p, NULL, 1, &r.t, &why));
    CHECK(!esc_stick_check(r.p, c, 1, NULL, &why));
    esc_stick_change_t bad = { 200u, 0u };
    CHECK(!esc_stick_check(r.p, &bad, 1, &r.t, &why));
    CHECK_STR_EQ(why, "no such item");
    bad.item = 0u;
    bad.value = 200u;
    CHECK(!esc_stick_check(r.p, &bad, 1, &r.t, &why));
    CHECK_STR_EQ(why, "no such value");
    esc_stick_change_t many[ESC_STICK_MAX_CHANGES + 1u];
    memset(many, 0, sizeof(many));
    CHECK(!esc_stick_check(r.p, many, ESC_STICK_MAX_CHANGES + 1u, &r.t,
                           &why));

    /* Every timing rule, one at a time. */
    static const struct { size_t off; uint32_t v; const char *why; } k[] = {
        { offsetof(esc_stick_timing_t, beep_min_ms), 0u,
          "BEEP MIN and GAP MIN above 0" },
        { offsetof(esc_stick_timing_t, long_ms), 200u, "LONG above BEEP MIN" },
        { offsetof(esc_stick_timing_t, long_max_ms), 500u,
          "LONG MAX above LONG" },
        { offsetof(esc_stick_timing_t, group_gap_ms), 200u,
          "GROUP GAP above GAP MIN" },
        { offsetof(esc_stick_timing_t, hysteresis_ma), 100u,
          "THRESHOLD above HYSTERESIS" },
        { offsetof(esc_stick_timing_t, entry_ms), 500u,
          "ENTRY above 500 ms" },
        /* The item has to be moved on within 3000 ms of its tone. */
        { offsetof(esc_stick_timing_t, group_gap_ms), 2800u,
          "GROUP GAP misses the select window" },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        esc_stick_timing_t t = r.t;
        memcpy((char *)&t + k[i].off, &k[i].v, sizeof(uint32_t));
        if (esc_stick_check(r.p, c, 1, &t, &why)) {
            T_FAIL("timing case %u accepted", (unsigned)i);
        } else {
            CHECK_STR_EQ(why, k[i].why);
        }
    }
    CHECK(esc_stick_check(r.p, c, 1, &r.t, &why));

    /* A value numbered 0 is never sounded. */
    rig("eflite-pro-sbec-7menu");
    CHECK_EQ(esc_stick_kind(r.p, NULL), ESC_STICK_KIND_NONE);

    /* And a refused start leaves the run idle. */
    rig("hobbywing-flyfun-8item");
    esc_stick_change_t dup[2] = { change(1, 2), change(1, 1) };
    CHECK(!start(dup, 2));
    CHECK(!esc_stick_running(&r.e));
}

TEST_CASE(the_voltage_is_the_lowest_cell_count_the_family_states)
{
    CHECK_EQ(esc_stick_profile_mv(esc_profiles_find("hobbywing-flyfun-8item")),
             7600u);
    CHECK_EQ(esc_stick_profile_mv(NULL), 0u);
    esc_model_t m[2] = { { "a", 0, 0, false, 0, 0 }, { "b", 6, 6, true, 0, 0 } };
    esc_profile_t p;
    memset(&p, 0, sizeof(p));
    p.models = m;
    p.model_count = 1u;
    CHECK_EQ(esc_stick_profile_mv(&p), 0u);
    p.model_count = 2u;
    CHECK_EQ(esc_stick_profile_mv(&p), 7200u);   /* six NiMH cells */
}

/* ------------------------------------------------------- whole runs */

TEST_CASE(a_two_stage_short_long_menu_stores_what_was_asked)
{
    rig("hobbywing-flyfun-8item");
    /* Item 5 sounds as one long beep; item 2 as two short ones. */
    esc_stick_change_t c[2] = { change(5, 2), change(2, 2) };
    CHECK(start(c, 2));
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_stick_done_count(&r.e), 2u);
    CHECK_EQ(esc_sim_stored(&r.sim, 5), 2);
    CHECK_EQ(esc_sim_stored(&r.sim, 2), 2);
    CHECK_EQ(r.sim.stores, 2u);
    CHECK_EQ(r.e.entries, 1);
    ended_safe();
}

/* Readings at the PD mini's 100 ms with jitter up to the 200 ms the default
 * timing allows. */
TEST_CASE(a_slow_jittery_supply_still_counts_right)
{
    rig("dualsky-xcontroller");
    r.read_iv = 100u;
    r.jitter = 90u;
    esc_stick_change_t c[1] = { change(3, 2) };
    CHECK(start(c, 1));
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 3), 2);
    CHECK_EQ(r.sim.stores, 1u);
}

TEST_CASE(noise_inside_the_hysteresis_does_not_count)
{
    rig("hobbywing-flyfun-8item");
    r.sim.c.noise_ma = 30u;
    esc_stick_change_t c[1] = { change(3, 2) };
    CHECK(start(c, 1));
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 3), 2);
    CHECK_EQ(r.sim.stores, 1u);
}

TEST_CASE(a_one_stage_menu_of_repeated_groups_stores_its_value)
{
    rig("sunrise-pro");
    esc_stick_change_t c[1] = { change(2, 5) };
    CHECK(start(c, 1));
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 2), 5);
    CHECK_EQ(r.sim.stores, 1u);
}

/* One change per power-up: the run switches the supply off and enters
 * again for the next. */
TEST_CASE(one_change_per_entry_cycles_the_power)
{
    rig("sunrise-pro");
    esc_stick_change_t c[2] = { change(1, 3), change(2, 4) };
    CHECK(start(c, 2));
    run_until_phase(ESC_STICK_CYCLE, 240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_CYCLE);
    CHECK(!esc_stick_out(&r.e)->supply_on);
    CHECK(esc_stick_out(&r.e)->arm);
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(r.e.entries, 2);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);
    CHECK_EQ(esc_sim_stored(&r.sim, 2), 4);
}

/* yge-hv with an entry time, as a card profile would give it once one is
 * measured. */
static esc_profile_t g_yge;

static void rig_yge(uint32_t hold_ms)
{
    rig("yge-hv");
    g_yge = *r.p;
    g_yge.entry_hold_ms = hold_ms;
    r.p = &g_yge;
    r.t.entry_ms = hold_ms;
    esc_sim_cfg_t c;
    esc_sim_defaults(&c);
    esc_sim_init(&r.sim, r.p, &c);
}

/* YGE with a known entry: power up at maximum, the stick rests at minimum
 * while the modes sound, maximum selects, and the move back to minimum
 * stores -- before the supply goes off. */
TEST_CASE(the_stick_rests_and_stores_where_the_profile_says)
{
    rig_yge(6000u);
    CHECK_EQ(esc_stick_kind(r.p, NULL), ESC_STICK_KIND_ONE_STAGE);
    esc_stick_change_t c[1] = { change(1, 4) };
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_VALUES, 60000u);
    CHECK(esc_stick_out(&r.e)->throttle_pct == ESC_STICK_PCT_MIN);
    run_until_phase(ESC_STICK_STORE, 240000u);
    CHECK(esc_stick_out(&r.e)->throttle_pct == ESC_STICK_PCT_MAX);
    CHECK_EQ(r.sim.stores, 0u);               /* selected, not yet stored */
    run_until_phase(ESC_STICK_OFF, 60000u);
    CHECK(esc_stick_out(&r.e)->throttle_pct == ESC_STICK_PCT_MIN);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 4);
    run_for(60000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);

    /* Without the store move the simulated ESC keeps nothing: what the
     * profile says is what it needs. */
    rig_yge(6000u);
    g_yge.store_throttle = ESC_THR_NONE;
    esc_sim_init(&r.sim, r.p, NULL);
    g_yge.store_throttle = ESC_THR_MIN;
    CHECK(start(c, 1));
    r.e.p = r.p;
    run_for(240000u);
    CHECK_EQ(r.sim.stores, 1u);
}

/* A run that ends as planned switches the supply off with the stick where
 * it stored, and moves it only once the output reads off. */
TEST_CASE(the_supply_goes_off_before_the_stick_moves)
{
    rig("sunrise-pro");
    esc_stick_change_t c[1] = { change(2, 5) };
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_OFF, 240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_OFF);
    const esc_stick_out_t *o = esc_stick_out(&r.e);
    CHECK(!o->supply_on);
    CHECK(o->throttle_pct == ESC_STICK_PCT_MIN);   /* sunrise selects at MIN */
    CHECK(o->arm);
    run_for(5000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);

    /* Two-stage, selecting at MAX: MAX holds until the output is off. */
    rig("hobbywing-flyfun-8item");
    esc_stick_change_t h[1] = { change(3, 2) };
    CHECK(start(h, 1));
    run_until_phase(ESC_STICK_OFF, 240000u);
    CHECK(o == esc_stick_out(&r.e));
    o = esc_stick_out(&r.e);
    CHECK(!o->supply_on);
    CHECK(o->throttle_pct == ESC_STICK_PCT_MAX);
    run_for(5000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK(o->throttle_pct == ESC_STICK_PCT_MIN);

    /* A supply that does not go off ends the run: at the end, and between
     * two changes. */
    rig("hobbywing-flyfun-8item");
    CHECK(start(h, 1));
    run_until_phase(ESC_STICK_OFF, 240000u);
    r.supply_stuck = true;
    run_for(10000u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_SUPPLY_ON);
    ended_safe();

    rig("sunrise-pro");
    esc_stick_change_t two[2] = { change(1, 3), change(2, 4) };
    CHECK(start(two, 2));
    run_until_phase(ESC_STICK_CYCLE, 240000u);
    r.supply_stuck = true;
    run_for(10000u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_SUPPLY_ON);
    CHECK_EQ(r.e.entries, 1);
    ended_safe();
}

/*
 * The panel's OFF is a request: the module goes off a link exchange and a
 * module transaction later.  A power cycle and a planned end wait for the
 * supply's own report, in readings taken after the request, with the
 * current down; the stick never moves while the module is still on.  The
 * reviewers' proof: sunrise-pro with two changes and the module 300 ms
 * behind moved the stick at full power.
 */
TEST_CASE(the_stick_waits_for_the_supplys_own_off)
{
    rig("sunrise-pro");
    r.off_lag_ms = 300u;
    esc_stick_change_t two[2] = { change(1, 3), change(2, 4) };
    CHECK(start(two, 2));
    run_for(400000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(r.e.entries, 2);
    CHECK_EQ(r.moved_while_on, 0u);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);
    CHECK_EQ(esc_sim_stored(&r.sim, 2), 4);

    /* A planned end does not report DONE, nor disarm, before the module
     * reads off. */
    rig("hobbywing-flyfun-8item");
    r.off_lag_ms = 1500u;
    esc_stick_change_t h[1] = { change(3, 2) };
    CHECK(start(h, 1));
    run_until_phase(ESC_STICK_OFF, 240000u);
    for (int i = 0; i < 1400; ++i) {
        tick();
    }
    CHECK_EQ(r.e.phase, ESC_STICK_OFF);
    CHECK(esc_stick_out(&r.e)->arm);
    CHECK(r.supply_on);
    run_for(5000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(r.moved_while_on, 0u);

    /* Reported off with the current still up is not off. */
    rig("hobbywing-flyfun-8item");
    CHECK(start(h, 1));
    run_until_phase(ESC_STICK_OFF, 240000u);
    r.extra_ma = 200;                /* the module reads off at once */
    tick();
    const uint32_t seq = r.seq;
    for (int i = 0; i < 5000 && esc_stick_running(&r.e); ++i) {
        tick();
    }
    CHECK(r.seq != seq);
    CHECK_EQ(r.e.reason, ESC_STICK_R_SUPPLY_ON);
    ended_safe();

    /* A module that never goes off ends the run, and the stick stays. */
    rig("sunrise-pro");
    r.supply_stuck = true;
    CHECK(start(two, 2));
    run_for(400000u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_SUPPLY_ON);
    CHECK_EQ(r.e.entries, 1);
}

/* An item with one value -- a rule written as a value, "N beeps = N
 * cells" -- is nothing to choose, and is not offered. */
TEST_CASE(an_item_with_one_value_is_not_offered)
{
    rig("hobbywing-flyfun-hv-9item");
    const int i = item_index(r.p, 7);
    CHECK(i >= 0);
    CHECK_STR_EQ(esc_stick_not_offered(&r.p->items[i]),
                 "one value: nothing to choose");
    CHECK_STR_EQ(esc_stick_not_offered(&r.p->items[i + 1]),
                 "reset and exit are actions, not settings");
    CHECK(esc_stick_not_offered(&r.p->items[0]) == NULL);
    CHECK_STR_EQ(esc_stick_not_offered(NULL), "no such item");
    esc_stick_change_t c[1] = { change(7, 1) };
    const char *why = NULL;
    CHECK(!esc_stick_check(r.p, c, 1, &r.t, &why));
    CHECK_STR_EQ(why, "one value: nothing to choose");
}

/* Reset and exit are actions: the ESC acts on the select move and sounds
 * no values, so they are not offered as changes. */
TEST_CASE(reset_and_exit_are_not_changes)
{
    rig("hobbywing-flyfun-8item");
    const char *why = NULL;
    esc_stick_change_t c[2] = { change(3, 2), change(7, 1) };
    CHECK(esc_stick_is_action(&r.p->items[6]));
    CHECK(esc_stick_is_action(&r.p->items[7]));
    CHECK(!esc_stick_is_action(&r.p->items[0]));
    CHECK(!esc_stick_is_action(NULL));
    CHECK(!esc_stick_check(r.p, c, 2, &r.t, &why));
    CHECK_STR_EQ(why, "reset and exit are actions, not settings");
    c[1] = change(8, 1);
    CHECK(!esc_stick_check(r.p, c, 2, &r.t, &why));
}

/* The simulated ESC follows the data for actions: exit leaves the menu,
 * reset clears what was stored. */
TEST_CASE(the_simulation_acts_on_reset_and_exit)
{
    const esc_profile_t *p = esc_profiles_find("hobbywing-flyfun-8item");
    for (int k = 0; k < 2; ++k) {
        esc_sim_t s;
        esc_sim_init(&s, p, NULL);
        s.stored[3] = 2u;
        const uint8_t want = (k == 0) ? 7u : 8u;   /* reset, exit */
        float pct = 100.0f;
        bool moved = false;
        for (uint32_t t = 0; t < 120000u; t += 5u) {
            (void)esc_sim_step(&s, t, true, pct);
            if (!moved && s.mode == ESC_SIM_ITEMS && s.ended == want
                && s.seg == 3u /* the pause after it */) {
                pct = 0.0f;
                moved = true;
            }
        }
        CHECK(moved);
        if (k == 0) {
            CHECK_EQ(s.resets, 1u);
            CHECK_EQ(esc_sim_stored(&s, 3), 0);
            CHECK_EQ(s.mode, ESC_SIM_ITEMS);
        } else {
            CHECK_EQ(s.mode, ESC_SIM_IDLE);
            CHECK_EQ(esc_sim_stored(&s, 3), 2);
        }
    }
}

/* The reviewers' traces: listening that began in the middle of a group, a
 * beep then lost, and in a menu that repeats its groups a lost beep that
 * makes a "2" read as one more "1".  Each stored the wrong value. */
TEST_CASE(a_partial_first_group_and_a_lost_beep_store_nothing_wrong)
{
    static const struct {
        const char *id;
        uint8_t item, value;
        int32_t drop;
        uint32_t entry;     /* the engine's ENTRY, ms */
    } k[] = {
        { "dualsky-xcontroller", 2, 1, 2, 9000u },
        { "sunrise-pro", 1, 1, 5, 10500u },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        rig(k[i].id);
        r.sim.c.drop_group = k[i].drop;
        r.t.entry_ms = k[i].entry;
        esc_stick_change_t c[1] = { change(k[i].item, k[i].value) };
        CHECK(start(c, 1));
        run_for(400000u);
        const uint8_t got = esc_sim_stored(&r.sim, k[i].item);
        if (r.sim.stores > 0u && got != k[i].value) {
            T_FAIL("%s stored item %u = %u, wanted %u", k[i].id,
                   (unsigned)k[i].item, (unsigned)got, (unsigned)k[i].value);
        }
        CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    }
}

/*
 * A sweep of the ESC's entry against the engine's, with one beep lost in
 * each of the first groups in turn: no run stores a value other than the
 * one asked for.  Before the order rule asked for three groups in a row
 * and a quiet line first, 68 of a wider sweep did.
 */
TEST_CASE(a_sweep_of_lost_beeps_and_entry_times_stores_nothing_wrong)
{
    static const struct { const char *id; uint8_t item, value; } k[] = {
        { "dualsky-xcontroller", 3, 1 },
        { "sunrise-pro", 1, 1 },
        { "sunrise-pro", 2, 5 },
        { "ztw-gecko", 2, 1 },
    };
    unsigned runs = 0, wrong = 0, done = 0;
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        for (int32_t drop = -1; drop < 10; ++drop) {
            for (int32_t dl = -2000; dl <= 2000; dl += 1000) {
                rig(k[i].id);
                const int32_t ent = (int32_t)((r.p->entry_hold_ms != 0u)
                                                  ? r.p->entry_hold_ms
                                                  : 5000u) + dl;
                if (ent < 600) {
                    continue;
                }
                r.sim.c.drop_group = drop;
                r.sim.c.entry_ms = (uint32_t)ent;
                esc_stick_change_t c[1] = { change(k[i].item, k[i].value) };
                if (!start(c, 1)) {
                    continue;
                }
                run_for(400000u);
                ++runs;
                done += (r.e.phase == ESC_STICK_DONE) ? 1u : 0u;
                const uint8_t got = esc_sim_stored(&r.sim, k[i].item);
                if (r.sim.stores > 0u && got != k[i].value) {
                    ++wrong;
                    T_FAIL("%s drop %d entry %d stored %u, wanted %u",
                           k[i].id, (int)drop, (int)ent, (unsigned)got,
                           (unsigned)k[i].value);
                }
            }
        }
    }
    CHECK_EQ(wrong, 0u);
    CHECK(runs > 150u);
    /* And losing a beep costs a loop, not the run. */
    if (done * 10u < runs * 9u) {
        T_FAIL("%u of %u runs done", done, runs);
    }
}

/* A link that came up after the start and went is lost as well. */
TEST_CASE(a_link_that_comes_up_and_goes_is_lost)
{
    rig("hobbywing-flyfun-8item");
    r.link = false;
    esc_stick_change_t c[1] = { change(3, 2) };
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_ENTRY, 10000u);
    r.link = true;
    run_for(100u);
    CHECK(esc_stick_running(&r.e));
    r.link = false;
    run_for(100u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_LINK);
    ended_safe();
}

TEST_CASE(a_two_stage_menu_whose_groups_repeat_stores_its_value)
{
    rig("ztw-gecko");
    esc_stick_change_t c[1] = { change(2, 3) };
    CHECK(start(c, 1));
    run_for(400000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 2), 3);
    CHECK_EQ(r.sim.stores, 1u);
}

/*
 * The case the sequence check exists for.  DualSky's brake has values 1 and
 * 2.  Item 1 is selected on the ninth group (the first heard is never acted
 * on, so it is the loop's second pass); the value loop then sounds 1, 2, 1,
 * 2 ... and group 10 -- a "2" -- loses its last beep, so it reads as "1".
 * Acted on, it would store 2.  The check passes it, and the next loop
 * stores 1.
 */
TEST_CASE(a_missed_beep_does_not_select_the_number_below)
{
    rig("dualsky-xcontroller");
    r.sim.c.drop_group = 10;
    esc_stick_change_t c[1] = { change(1, 1) };
    CHECK(start(c, 1));
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 1);
    CHECK_EQ(r.sim.stores, 1u);
}

/* An ESC whose menu is one item longer than the profile's: item 1 follows
 * item 9, which the profile does not have, so it is never trusted and the
 * run times out having selected nothing. */
TEST_CASE(a_menu_that_does_not_match_the_profile_times_out)
{
    rig("dualsky-xcontroller");
    r.sim.c.extra = 1u;
    r.t.timeout_ms = 40000u;
    esc_stick_change_t c[1] = { change(1, 2) };
    CHECK(start(c, 1));
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_ABORTED);
    CHECK_EQ(r.e.reason, ESC_STICK_R_TIMEOUT);
    CHECK_EQ(r.sim.stores, 0u);
    ended_safe();
}

TEST_CASE(an_esc_that_never_beeps_ends_the_run)
{
    rig("hobbywing-flyfun-8item");
    r.sim.c.mute = true;
    esc_stick_change_t c[1] = { change(1, 2) };
    CHECK(start(c, 1));
    run_for(240000u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_NO_BEEPS);
    ended_safe();
}

TEST_CASE(abort_stop_and_disarm_end_the_run_safe)
{
    static const esc_stick_reason_t want[] = {
        ESC_STICK_R_USER, ESC_STICK_R_STOP, ESC_STICK_R_BENCH_STOP,
        ESC_STICK_R_DISARMED, ESC_STICK_R_LINK, ESC_STICK_R_SUPPLY_OFF,
        ESC_STICK_R_SUPPLY_LOST, ESC_STICK_R_STALE, ESC_STICK_R_LEFT,
    };
    for (size_t k = 0; k < sizeof(want) / sizeof(want[0]); ++k) {
        rig("hobbywing-flyfun-8item");
        esc_stick_change_t c[1] = { change(3, 2) };
        CHECK(start(c, 1));
        run_until_phase(ESC_STICK_ITEMS, 60000u);
        CHECK_EQ(r.e.phase, ESC_STICK_ITEMS);
        CHECK(esc_stick_out(&r.e)->supply_on);
        switch (want[k]) {
        case ESC_STICK_R_USER:  esc_stick_abort(&r.e, ESC_STICK_R_USER); break;
        case ESC_STICK_R_LEFT:  esc_stick_abort(&r.e, ESC_STICK_R_LEFT); break;
        case ESC_STICK_R_STOP:  r.stops++; r.pressed++;                  break;
        case ESC_STICK_R_BENCH_STOP: r.stops++;                          break;
        case ESC_STICK_R_DISARMED: r.arm_refused = true; r.armed = false; break;
        case ESC_STICK_R_LINK:  r.link = false;                          break;
        case ESC_STICK_R_SUPPLY_OFF: r.supply_dead = true;               break;
        case ESC_STICK_R_SUPPLY_LOST: r.online = false;                  break;
        case ESC_STICK_R_STALE: r.readings_stop = true;                  break;
        default: break;
        }
        run_for(5000u);
        if (r.e.reason != want[k]) {
            T_FAIL("case %u ended %s", (unsigned)k,
                   esc_stick_reason_text(r.e.reason));
        }
        ended_safe();
        CHECK_STR_EQ(esc_stick_phase_text(r.e.phase), "ABORTED");
        /* And a second abort changes nothing about how it ended. */
        esc_stick_abort(&r.e, ESC_STICK_R_USER);
        CHECK_EQ(r.e.reason, want[k]);
    }
}

/* Without a coprocessor the bench is modelled, and the run is allowed;
 * only a link that was there and went is a reason to stop. */
TEST_CASE(a_link_that_was_never_up_is_not_lost)
{
    rig("hobbywing-flyfun-8item");
    r.link = false;
    esc_stick_change_t c[1] = { change(1, 2) };
    CHECK(start(c, 1));
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
}

TEST_CASE(readings_too_far_apart_end_the_run)
{
    rig("hobbywing-flyfun-8item");
    r.read_iv = 300u;
    esc_stick_change_t c[1] = { change(1, 2) };
    CHECK(start(c, 1));
    run_for(60000u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_RATE);
    CHECK_EQ(r.sim.stores, 0u);
    ended_safe();
}

/* One reading late spoils the group it falls in, and only that: the next
 * loop is used. */
TEST_CASE(one_late_reading_passes_a_group_and_the_run_goes_on)
{
    rig("hobbywing-flyfun-8item");
    r.late_once_at = 1000u + 1000u + 7000u + 9000u;
    r.late_once_by = 400u;
    esc_stick_change_t c[1] = { change(3, 2) };
    CHECK(start(c, 1));
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 3), 2);
}

/* A reading count that steps by more than one is a reading missed: once
 * spoils a group, every time ends the run.  And a count that stops is
 * readings that stop, however often the page is read. */
TEST_CASE(readings_the_panel_never_saw_are_late)
{
    rig("hobbywing-flyfun-8item");
    esc_stick_change_t c[1] = { change(3, 2) };
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_ITEMS, 60000u);
    r.skip = 1u;
    run_for(1000u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_RATE);
    ended_safe();

    rig("hobbywing-flyfun-8item");
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_ITEMS, 60000u);
    r.skip = 1u;
    const uint32_t before = r.seq;
    while (r.seq == before && esc_stick_running(&r.e)) {
        tick();
    }
    r.skip = 0u;
    run_for(400000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 3), 2);

    /* The count stops: samples go on, the same reading in each. */
    rig("hobbywing-flyfun-8item");
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_ITEMS, 60000u);
    const uint32_t frozen = r.seq;
    const uint32_t taken = r.now;
    for (int i = 0; i < 2000 && esc_stick_running(&r.e); ++i) {
        esc_stick_sample_t x = { .seq = frozen, .at_ms = taken, .ma = 150,
                                 .current_ok = true, .output = true,
                                 .reported_on = true,
                                 .online = true };
        esc_stick_sample(&r.e, &x);
        const esc_stick_bench_t b = bench();
        esc_stick_step(&r.e, &b);
        r.now++;
    }
    CHECK_EQ(r.e.reason, ESC_STICK_R_STALE);
}

/* A reading stamped a little after the step's own time is not old. */
TEST_CASE(a_reading_ahead_of_the_step_is_not_stale)
{
    rig("hobbywing-flyfun-8item");
    r.skew_ms = 30u;
    esc_stick_change_t c[1] = { change(1, 2) };
    CHECK(start(c, 1));
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
}

TEST_CASE(a_bench_that_will_not_arm_or_power_ends_the_run)
{
    rig("hobbywing-flyfun-8item");
    r.arm_refused = true;
    esc_stick_change_t c[1] = { change(1, 2) };
    CHECK(start(c, 1));
    run_for(10000u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_NOT_ARMED);
    ended_safe();

    rig("hobbywing-flyfun-8item");
    r.supply_dead = true;
    CHECK(start(c, 1));
    run_for(10000u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_NO_POWER);
    ended_safe();
}

TEST_CASE(a_current_that_stays_high_ends_the_run)
{
    rig("hobbywing-flyfun-8item");
    esc_stick_change_t c[1] = { change(1, 2) };
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_ITEMS, 60000u);
    r.extra_ma = 2000;
    run_for(20000u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_HIGH);
    ended_safe();
}

/* The run's order: arm first with the throttle at rest and the supply off,
 * then the entry position, and only then power. */
TEST_CASE(the_signal_is_in_place_before_the_power)
{
    rig("hobbywing-flyfun-8item");
    esc_stick_change_t c[1] = { change(1, 2) };
    CHECK(start(c, 1));
    const esc_stick_out_t *o = esc_stick_out(&r.e);
    CHECK_EQ(r.e.phase, ESC_STICK_ARMING);
    CHECK(o->arm && !o->supply_on && o->throttle_pct == ESC_STICK_PCT_MIN);
    tick();
    CHECK_EQ(r.e.phase, ESC_STICK_SIGNAL);
    /* Armed, at MIN until the supply reads off; then the entry position,
     * held ESC_STICK_SIGNAL_MS before the power. */
    CHECK(o->throttle_pct == ESC_STICK_PCT_MIN && !o->supply_on);
    for (int i = 0; i < 5000 && !r.e.sig_moved; ++i) {
        tick();
    }
    CHECK(r.e.off_seen);
    CHECK(o->throttle_pct == ESC_STICK_PCT_MAX && !o->supply_on);
    const uint32_t moved = r.now;
    run_until_phase(ESC_STICK_POWER, 5000u);
    CHECK(r.now - moved >= ESC_STICK_SIGNAL_MS);
    CHECK(o->supply_on);
    CHECK_EQ(o->supply_mv, 7600u);
    CHECK_EQ(o->supply_ma, 1000u);
    CHECK_EQ(esc_stick_beeps(&r.e), 0u);
    CHECK(esc_stick_beeps(NULL) == 0u);
    CHECK(esc_stick_out(NULL) == NULL);
    /* A sample or a step with nothing to act on does nothing. */
    esc_stick_sample(&r.e, NULL);
    esc_stick_step(&r.e, NULL);
    esc_stick_t idle;
    memset(&idle, 0, sizeof(idle));
    esc_stick_step(&idle, NULL);
    CHECK_EQ(idle.phase, ESC_STICK_IDLE);
}

/* Every reason and phase has words for the screen. */
TEST_CASE(every_reason_and_phase_has_its_words)
{
    for (int i = ESC_STICK_R_STOP; i <= ESC_STICK_R_LEFT; ++i) {
        const char *t = esc_stick_reason_text((esc_stick_reason_t)i);
        if (t == NULL || t[0] == '\0' || strcmp(t, "?") == 0) {
            T_FAIL("reason %d has no words", i);
        }
    }
    for (int i = ESC_STICK_IDLE; i <= ESC_STICK_ABORTED; ++i) {
        const char *t = esc_stick_phase_text((esc_stick_phase_t)i);
        if (t == NULL || t[0] == '\0' || strcmp(t, "?") == 0) {
            T_FAIL("phase %d has no words", i);
        }
    }
    CHECK_STR_EQ(esc_stick_reason_text(ESC_STICK_R_NONE), "");
    CHECK_EQ(esc_stick_read_max_ms(NULL), 0u);
    esc_stick_timing_defaults(NULL);
}

/* The red light's table: every reason decided, the operator's ends and DONE
 * dark, everything not as expected lit. */
TEST_CASE(the_red_light_is_for_ends_nobody_chose)
{
    static const struct {
        esc_stick_reason_t r;
        bool               fault;
    } k[] = {
        { ESC_STICK_R_NONE,        false },
        { ESC_STICK_R_STOP,        false },
        { ESC_STICK_R_BENCH_STOP,  true },
        { ESC_STICK_R_USER,        false },
        { ESC_STICK_R_LEFT,        false },
        { ESC_STICK_R_DISARMED,    true },
        { ESC_STICK_R_LINK,        true },
        { ESC_STICK_R_SUPPLY_OFF,  true },
        { ESC_STICK_R_SUPPLY_LOST, true },
        { ESC_STICK_R_STALE,       true },
        { ESC_STICK_R_RATE,        true },
        { ESC_STICK_R_NOT_ARMED,   true },
        { ESC_STICK_R_NO_POWER,    true },
        { ESC_STICK_R_SUPPLY_ON,   true },
        { ESC_STICK_R_NO_BEEPS,    true },
        { ESC_STICK_R_HIGH,        true },
        { ESC_STICK_R_TIMEOUT,     true },
        { ESC_STICK_R_HAND,        true },
        { ESC_STICK_R_TOUCH,       true },
    };
    /* The table covers the list. */
    CHECK_EQ(sizeof(k) / sizeof(k[0]), (size_t)ESC_STICK_R_LEFT + 1u);
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        if (esc_stick_reason_is_fault(k[i].r) != k[i].fault) {
            T_FAIL("%s is decided wrong", esc_stick_reason_text(k[i].r));
        }
    }
}

/* A stop is STOP only when every stop in the span was pressed: a stop the
 * bench raised is never hidden by a press that came with it. */
TEST_CASE(a_stop_nobody_pressed_is_the_benchs_own)
{
    CHECK_EQ(esc_stick_stop_reason(1u, 1u), ESC_STICK_R_STOP);
    CHECK_EQ(esc_stick_stop_reason(2u, 2u), ESC_STICK_R_STOP);
    CHECK_EQ(esc_stick_stop_reason(1u, 0u), ESC_STICK_R_BENCH_STOP);
    CHECK_EQ(esc_stick_stop_reason(2u, 1u), ESC_STICK_R_BENCH_STOP);
    /* Counts that wrapped between the start and the stop. */
    rig("hobbywing-flyfun-8item");
    r.stops = UINT32_MAX;
    r.pressed = UINT32_MAX;
    esc_stick_change_t c[1] = { change(3, 2) };
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_ITEMS, 60000u);
    r.stops++;
    r.pressed++;
    run_for(100u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_STOP);
    CHECK(!esc_stick_reason_is_fault(r.e.reason));
    rig("hobbywing-flyfun-8item");
    r.stops = 5u;
    r.pressed = 5u;
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_ITEMS, 60000u);
    r.stops += 2u;                  /* a press and touch lost with it */
    r.pressed += 1u;
    run_for(100u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_BENCH_STOP);
    CHECK(esc_stick_reason_is_fault(r.e.reason));
    ended_safe();
}

/* Green only while the supply is on for the menu.  A pulse the detector
 * holds when STORE switches the supply off -- the ESC's own tone answering
 * a selection -- does not keep it lit through POWER OFF, POWER CYCLE and the
 * next POWER ON.  Swept over STORE, so the off lands in every part of a
 * tone, on three menus. */
TEST_CASE(the_green_light_is_dark_while_the_supply_is_off)
{
    static const char *const ids[] = {
        "sunrise-pro", "hobbywing-flyfun-8item", "dualsky-xcontroller",
    };
    unsigned lit_on = 0u;
    for (size_t k = 0; k < sizeof(ids) / sizeof(ids[0]); ++k) {
        for (uint32_t st = 0u; st <= 4000u; st += 250u) {
            rig(ids[k]);
            r.t.store_ms = st;
            esc_stick_change_t c[2];
            size_t n = 1u;
            if (k == 0u) {
                c[0] = change(1, 3);
                c[1] = change(2, 4);
                n = 2u;
            } else {
                c[0] = change(3, 2);
            }
            CHECK(start(c, n));
            esc_stick_light_t l;
            esc_stick_light_reset(&l, &r.e);
            for (uint32_t i = 0; i < 400000u && esc_stick_running(&r.e);
                 ++i) {
                tick();
                const bool g = esc_stick_light_green(&l, &r.e, r.now);
                const esc_stick_phase_t ph = r.e.phase;
                const bool off = ph == ESC_STICK_OFF
                                 || ph == ESC_STICK_CYCLE
                                 || ph == ESC_STICK_POWER
                                 || ph == ESC_STICK_SIGNAL
                                 || ph == ESC_STICK_ARMING;
                if (g && off) {
                    T_FAIL("%s STORE %u ms: green in %s", ids[k],
                           (unsigned)st, esc_stick_phase_text(ph));
                    break;
                }
                if (off) {
                    CHECK_EQ(esc_stick_beeps(&r.e), 0u);
                }
                lit_on += g ? 1u : 0u;
            }
        }
    }
    CHECK(lit_on > 1000u);
}

/* Every rise of the detector is counted as a pulse, on every power-up. */
TEST_CASE(the_run_counts_every_pulse_the_detector_begins)
{
    rig("hobbywing-flyfun-8item");
    esc_stick_change_t c[1] = { change(3, 2) };
    CHECK(start(c, 1));
    uint32_t rises = 0u;
    bool was = false;
    for (uint32_t i = 0; i < 240000u && esc_stick_running(&r.e); ++i) {
        tick();
        if (r.e.det.high && !was) {
            ++rises;
        }
        was = r.e.det.high;
        if (r.e.pulses != rises && r.e.pulses != rises + 1u) {
            T_FAIL("%u pulses counted for %u rises seen", r.e.pulses, rises);
        }
    }
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK(r.e.pulses > 10u);
    CHECK(r.e.pulses >= rises);
}

/* Green follows the detector, and a pulse shorter than a look still shows
 * for ESC_STICK_BEEP_LIGHT_MS. */
TEST_CASE(the_green_light_shows_every_pulse_for_its_minimum)
{
    static esc_stick_t e;
    esc_stick_light_t l;
    memset(&e, 0, sizeof(e));
    esc_stick_light_reset(&l, &e);
    CHECK(!esc_stick_light_green(&l, &e, 0u));          /* idle: dark */
    e.phase = ESC_STICK_ITEMS;
    CHECK(!esc_stick_light_green(&l, &e, 10u));
    /* A pulse under way: on for as long as it lasts. */
    e.det.high = true;
    e.pulses = 1u;
    CHECK(esc_stick_light_green(&l, &e, 20u));
    CHECK(esc_stick_light_green(&l, &e, 1000u));
    e.det.high = false;
    CHECK(!esc_stick_light_green(&l, &e, 1001u));
    /* A pulse that began and ended between two looks: on from the look
     * that sees it, for the minimum, then off. */
    e.pulses = 2u;
    CHECK(esc_stick_light_green(&l, &e, 2000u));
    CHECK(esc_stick_light_green(&l, &e,
                                2000u + ESC_STICK_BEEP_LIGHT_MS - 1u));
    CHECK(!esc_stick_light_green(&l, &e, 2000u + ESC_STICK_BEEP_LIGHT_MS));
    /* A pulse still high past the minimum stays on. */
    e.pulses = 3u;
    e.det.high = true;
    CHECK(esc_stick_light_green(&l, &e, 3000u));
    CHECK(esc_stick_light_green(&l, &e, 3500u));
    /* The run over: dark, and what it counted does not light the next. */
    e.phase = ESC_STICK_ABORTED;
    CHECK(!esc_stick_light_green(&l, &e, 3501u));
    e.phase = ESC_STICK_ITEMS;
    e.det.high = false;
    CHECK(!esc_stick_light_green(&l, &e, 3502u));
    /* A look stamped before the pulse's own look counts as no time. */
    e.pulses = 4u;
    CHECK(esc_stick_light_green(&l, &e, 5000u));
    CHECK(esc_stick_light_green(&l, &e, 4990u));
    esc_stick_light_reset(NULL, &e);
    CHECK(!esc_stick_light_green(NULL, &e, 0u));
    esc_stick_light_reset(&l, NULL);
    CHECK_EQ(l.pulses, 0u);
}

/* The simulation on its own: the stick away from the entry position at
 * power-on, or moved off it during the entry, gives no menu. */
TEST_CASE(the_simulation_enters_only_from_the_entry_position)
{
    const esc_profile_t *p = esc_profiles_find("hobbywing-flyfun-8item");
    esc_sim_t s;
    esc_sim_init(&s, p, NULL);
    int beeps = 0;
    for (uint32_t t = 0; t < 20000u; t += 5u) {
        if (esc_sim_step(&s, t, true, 0.0f) > 300) {
            ++beeps;
        }
    }
    CHECK_EQ(beeps, 0);
    CHECK_EQ(s.mode, ESC_SIM_IDLE);

    esc_sim_init(&s, p, NULL);
    (void)esc_sim_step(&s, 0u, true, 100.0f);
    (void)esc_sim_step(&s, 100u, true, 0.0f);
    CHECK_EQ(s.mode, ESC_SIM_IDLE);
    (void)esc_sim_step(&s, 200u, false, 0.0f);
    CHECK_EQ(s.mode, ESC_SIM_OFF);
    CHECK_EQ(esc_sim_step(NULL, 0u, true, 0.0f), 0);
    CHECK_EQ(esc_sim_stored(NULL, 1), 0);
}

/* ------------------------------------------------------ manual steps */

/* A copy of a profile of record with manual steps of the test's own. */
static esc_profile_t g_hand;
static esc_manual_t  g_hand_steps[ESC_MANUAL_MAX];

static void rig_hand(const char *id, const esc_manual_t *m, uint8_t n)
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

/* Kontronik JAZZ: the jumper is on before the warning is held, the run
 * powers up and waits the 2 s entry, then asks for the pull and listens
 * from that moment, powered and at MIN.  The ESC is silent until the pull;
 * SILENCE does not end the run meanwhile.  No DONE: the menu heard in
 * order takes the step as done, and the mode is stored. */
TEST_CASE(a_jumper_pulled_after_the_entry_is_waited_for)
{
    rig("kontronik-jazz");
    CHECK_EQ(esc_stick_kind(r.p, NULL), ESC_STICK_KIND_ONE_STAGE);
    esc_stick_change_t c[1] = { change(1, 3) };
    CHECK(start(c, 1));
    CHECK(esc_stick_hand(&r.e) == NULL);
    run_until_asked(60000u);
    const esc_manual_t *m = esc_stick_hand(&r.e);
    CHECK(m != NULL);
    if (m != NULL) {
        CHECK_EQ(m->when, ESC_MANUAL_BEFORE_MENU);
    }
    CHECK_EQ(r.e.phase, ESC_STICK_VALUES);        /* listening already */
    CHECK(r.e.hand_menu);
    CHECK(esc_stick_out(&r.e)->supply_on);
    CHECK(esc_stick_out(&r.e)->arm);
    CHECK(esc_stick_out(&r.e)->throttle_pct == ESC_STICK_PCT_MIN);
    CHECK_EQ(r.e.entries, 1);
    /* A tap within the first second is not the operator's answer. */
    CHECK(!esc_stick_hand_ready(&r.e));
    CHECK(!esc_stick_confirm(&r.e));
    CHECK_EQ(esc_stick_hand_left_ms(&r.e), ESC_STICK_HAND_WAIT_MS);
    run_for(r.t.silence_ms + 1000u);
    CHECK(esc_stick_running(&r.e));
    CHECK(r.e.hand_menu);
    CHECK_EQ(r.e.groups, 0u);
    act(false);
    for (uint32_t i = 0; i < 20000u && r.e.hand_menu; ++i) {
        tick();
    }
    CHECK(!r.e.hand_menu);
    CHECK(r.e.hand_done);
    CHECK(esc_stick_hand(&r.e) == NULL);
    CHECK(!esc_stick_confirm(&r.e));
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);
    ended_safe();
}

/* The pull starts the series at once, and DONE comes when the operator is
 * back at the screen, here 4 s later.  Counting from the pull, mode 3 is
 * acted on in the first loop -- groups 1, 2, 3 in order -- not a whole
 * loop later, as it would be counting from DONE.  A partial first group is
 * never acted on: the quiet-first and order rules hold from the prompt. */
TEST_CASE(a_pull_starts_the_menu_and_no_group_is_lost)
{
    rig("kontronik-jazz");
    esc_stick_change_t c[1] = { change(1, 3) };
    CHECK(start(c, 1));
    run_until_asked(60000u);
    run_for(300u);
    act(false);
    const uint32_t pulled = r.now;
    run_for(4000u);
    (void)esc_stick_confirm(&r.e);            /* back at the screen */
    run_until_phase(ESC_STICK_STORE, 60000u);
    CHECK_EQ(r.e.phase, ESC_STICK_STORE);
    /* The answer (1.25 s), the pause, then 1, 2 and 3 at about 1.75 s
     * each: under 9 s.  One loop of nine modes more is over 15 s. */
    if (r.now - pulled > 9000u) {
        T_FAIL("mode 3 acted on %u ms after the pull",
               (unsigned)(r.now - pulled));
    }
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);

    /* The ESC already sounding when the run asks: no group counted until
     * GROUP GAP of quiet, and the order rule needs three in a row. */
    rig("kontronik-jazz");
    c[0] = change(1, 2);
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_ENTRY, 60000u);
    act(false);                               /* pulled early */
    run_until_asked(60000u);
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 2);
}

/* Two before_menu steps: the first waits in HAND_ON for DONE, the second,
 * marked starts_menu, is listened through. */
TEST_CASE(an_earlier_step_still_waits_for_done)
{
    static const esc_manual_t k[] = {
        { ESC_MANUAL_BEFORE_MENU, "Check the LED.", 0u },
        { ESC_MANUAL_BEFORE_MENU, "Pull the jumper.", 0u, NULL, true },
    };
    rig_hand("kontronik-jazz", k, 2);
    esc_stick_change_t c[1] = { change(1, 3) };
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_HAND_ON, 60000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_ON);
    CHECK(!r.e.hand_menu);
    CHECK_EQ(r.e.hand, 0u);
    run_for(5000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_ON);      /* waits for DONE */
    CHECK(esc_stick_confirm(&r.e));
    tick();
    CHECK(r.e.hand_menu);
    CHECK_EQ(r.e.hand, 1u);
    CHECK_EQ(r.e.phase, ESC_STICK_VALUES);
    act(false);
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);

    /* A menu that rests elsewhere: the stick would move under the hand, so
     * the last step waits for DONE too. */
    rig_hand("kontronik-jazz", &k[1], 1);
    g_hand.listen_throttle = ESC_THR_MID;
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_HAND_ON, 60000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_ON);
    CHECK(!r.e.hand_menu);
    CHECK(esc_stick_out(&r.e)->throttle_pct == ESC_STICK_PCT_MIN);

    /* A last step the profile does not mark as starting the menu waits for
     * DONE as any other: a card profile's own step, not the series. */
    rig_hand("kontronik-jazz", &k[0], 1);
    CHECK(!g_hand.manual[0].starts_menu);
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_HAND_ON, 60000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_ON);
    CHECK(!r.e.hand_menu);
    act(false);
    run_for(5000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_ON);      /* still waits */
    CHECK_EQ(r.e.groups, 0u);
    CHECK(esc_stick_confirm(&r.e));
    tick();
    CHECK_EQ(r.e.phase, ESC_STICK_VALUES);
    CHECK(!r.e.hand_menu);
    CHECK(esc_stick_hand(&r.e) == NULL);
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);
}

/* No DONE: the run ends after ESC_STICK_HAND_WAIT_MS, everything off, with
 * the red light lit. */
TEST_CASE(a_step_never_confirmed_ends_the_run)
{
    rig("kontronik-jazz");
    esc_stick_change_t c[1] = { change(1, 3) };
    CHECK(start(c, 1));
    run_until_asked(60000u);
    run_for(ESC_STICK_HAND_WAIT_MS - 2u);
    CHECK(r.e.hand_menu);                   /* the ESC waits, silent */
    CHECK(esc_stick_hand_left_ms(&r.e) <= 2u);
    run_for(10u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_HAND);
    CHECK(esc_stick_reason_is_fault(r.e.reason));
    CHECK_STR_EQ(esc_stick_reason_text(r.e.reason), "NOT CONFIRMED");
    ended_safe();
    CHECK(esc_stick_hand(&r.e) == NULL);
    CHECK_EQ(esc_stick_hand_left_ms(&r.e), 0u);
    CHECK(!esc_stick_confirm(&r.e));
}

/* Until the run asks for the step before the supply goes off. */
static void run_until_end_step(uint32_t ms)
{
    for (uint32_t i = 0; i < ms && esc_stick_running(&r.e)
                         && r.e.phase != ESC_STICK_HAND_END; ++i) {
        if (r.e.hand_menu) {
            act(false);                 /* the button pressed */
        }
        tick();
    }
}

/*
 * Kontronik KONTROL-X: the supply off before the ESC has confirmed its
 * mode locks the ESC (Kontronik_Kontrol-X_Kolibri-X.pdf p.4, 8 flashes).
 * After the store the run holds the ESC powered, the stick where the store
 * left it, until DONE; then the supply goes off and the run ends as any
 * other.  No DONE in ESC_STICK_HAND_WAIT_MS: the run ends safe and says
 * the ESC may be locked; so does any other end while the step is asked.
 */
TEST_CASE(the_supply_stays_on_until_the_esc_has_confirmed)
{
    rig("kontronik-kontrol-x");
    CHECK_EQ(esc_profile_manual_count(r.p, ESC_MANUAL_BEFORE_POWER_OFF), 1u);
    esc_stick_change_t c[1] = { change(1, 2) };
    CHECK(start(c, 1));
    run_until_end_step(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_END);
    CHECK_EQ(esc_stick_done_count(&r.e), 1u);
    const esc_manual_t *m = esc_stick_hand(&r.e);
    CHECK(m != NULL);
    if (m != NULL) {
        CHECK_EQ(m->when, ESC_MANUAL_BEFORE_POWER_OFF);
    }
    CHECK(!r.e.hand_menu);
    CHECK(r.e.lock_risk);
    CHECK(esc_stick_out(&r.e)->supply_on);
    CHECK(esc_stick_out(&r.e)->arm);
    CHECK(esc_stick_out(&r.e)->throttle_pct == ESC_STICK_PCT_MAX);
    CHECK_STR_EQ(esc_stick_phase_text(r.e.phase), "WAITING FOR THE ESC");
    run_for(30000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_END);     /* powered, waiting */
    CHECK(esc_stick_out(&r.e)->supply_on);
    CHECK(esc_stick_out(&r.e)->throttle_pct == ESC_STICK_PCT_MAX);
    CHECK(esc_stick_confirm(&r.e));
    tick();
    CHECK_EQ(r.e.phase, ESC_STICK_OFF);
    CHECK(!esc_stick_out(&r.e)->supply_on);
    CHECK(!r.e.lock_risk);
    run_for(20000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK(!esc_stick_lock_risk(&r.e));
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 2);

    /* Mode 3 goes to full reverse after full forward: the step is asked
     * with the stick where the last move left it. */
    rig("kontronik-kontrol-x");
    c[0] = change(1, 3);
    CHECK(start(c, 1));
    run_until_end_step(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_END);
    CHECK(esc_stick_out(&r.e)->throttle_pct == ESC_STICK_PCT_MIN);
    run_for(ESC_STICK_HAND_MIN_MS);
    CHECK(esc_stick_confirm(&r.e));
    run_for(20000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);

    /* No DONE: NOT CONFIRMED, everything off, and the ESC may be locked. */
    rig("kontronik-kontrol-x");
    c[0] = change(1, 2);
    CHECK(start(c, 1));
    run_until_end_step(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_END);
    CHECK(!esc_stick_lock_risk(&r.e));           /* not while it runs */
    run_for(ESC_STICK_HAND_WAIT_MS - 2u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_END);
    run_for(10u);
    CHECK_EQ(r.e.phase, ESC_STICK_ABORTED);
    CHECK_EQ(r.e.reason, ESC_STICK_R_HAND);
    ended_safe();
    CHECK(esc_stick_lock_risk(&r.e));

    /* STOP while the step is asked: the same. */
    rig("kontronik-kontrol-x");
    CHECK(start(c, 1));
    run_until_end_step(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_END);
    r.stops++;
    r.pressed++;
    tick();
    CHECK_EQ(r.e.reason, ESC_STICK_R_STOP);
    ended_safe();
    CHECK(esc_stick_lock_risk(&r.e));

    /* An end before the step, or after its DONE, is no such end. */
    rig("kontronik-kontrol-x");
    CHECK(start(c, 1));
    run_until_asked(60000u);
    esc_stick_abort(&r.e, ESC_STICK_R_USER);
    CHECK(!esc_stick_lock_risk(&r.e));

    /* A profile with two such steps asks for both, in order. */
    static const esc_manual_t k[] = {
        { ESC_MANUAL_BEFORE_MENU, "Pull the jumper.", 0u, NULL, true },
        { ESC_MANUAL_BEFORE_POWER_OFF, "Watch the LED.", 0u, NULL, false },
        { ESC_MANUAL_BEFORE_POWER_OFF, "Listen for the tones.", 0u, NULL,
          false },
    };
    rig_hand("kontronik-jazz", k, 3);
    esc_stick_change_t j[1] = { change(1, 3) };
    CHECK(start(j, 1));
    run_until_end_step(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_END);
    CHECK_EQ(r.e.hand, 1u);
    run_for(ESC_STICK_HAND_MIN_MS);
    CHECK(esc_stick_confirm(&r.e));
    tick();
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_END);
    CHECK_EQ(r.e.hand, 2u);
    CHECK(esc_stick_out(&r.e)->supply_on);
    CHECK(!esc_stick_confirm(&r.e));            /* too soon after the last */
    run_for(ESC_STICK_HAND_MIN_MS);
    CHECK(esc_stick_confirm(&r.e));
    run_for(20000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);
}

/* While a step is waited for, every end a run has still ends it: ABORT,
 * STOP, the bench's own stop, a disarm, the supply and the link. */
TEST_CASE(stop_abort_and_the_supply_end_a_run_waiting_for_a_step)
{
    static const esc_stick_reason_t want[] = {
        ESC_STICK_R_USER, ESC_STICK_R_STOP, ESC_STICK_R_BENCH_STOP,
        ESC_STICK_R_DISARMED, ESC_STICK_R_LINK, ESC_STICK_R_SUPPLY_OFF,
        ESC_STICK_R_SUPPLY_LOST, ESC_STICK_R_STALE,
    };
    for (size_t k = 0; k < sizeof(want) / sizeof(want[0]); ++k) {
        rig("kontronik-jazz");
        esc_stick_change_t c[1] = { change(1, 3) };
        CHECK(start(c, 1));
        run_until_asked(60000u);
        run_for(ESC_STICK_HAND_MIN_MS);
        CHECK(r.e.hand_menu);
        /* A DONE taken in the same frame as the end: the end wins, and
         * the menu never starts. */
        CHECK(esc_stick_confirm(&r.e));
        switch (want[k]) {
        case ESC_STICK_R_USER:  esc_stick_abort(&r.e, ESC_STICK_R_USER); break;
        case ESC_STICK_R_STOP:  r.stops++; r.pressed++;                  break;
        case ESC_STICK_R_BENCH_STOP: r.stops++;                          break;
        case ESC_STICK_R_DISARMED: r.arm_refused = true; r.armed = false; break;
        case ESC_STICK_R_LINK:  r.link = false;                          break;
        case ESC_STICK_R_SUPPLY_OFF: r.supply_dead = true;               break;
        case ESC_STICK_R_SUPPLY_LOST: r.online = false;                  break;
        case ESC_STICK_R_STALE: r.readings_stop = true;                  break;
        default: break;
        }
        run_for(5000u);
        if (r.e.reason != want[k]) {
            T_FAIL("case %u ended %s", (unsigned)k,
                   esc_stick_reason_text(r.e.reason));
        }
        CHECK_EQ(r.e.groups, 0u);
        ended_safe();
    }
}

/* A button held while the supply comes on: the run stops before the power
 * with the supply off and the stick at the entry, and DONE switches it on.
 * A stop before the DONE is acted on leaves the supply off. */
TEST_CASE(a_step_at_power_up_is_asked_with_the_supply_off)
{
    static const esc_manual_t k[] = {
        { ESC_MANUAL_AT_POWER_UP, "Hold the button.", 3000u },
    };
    rig_hand("kontronik-jazz", k, 1);
    CHECK_EQ(esc_stick_kind(r.p, NULL), ESC_STICK_KIND_ONE_STAGE);
    esc_stick_change_t c[1] = { change(1, 3) };
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_HAND_OFF, 60000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_OFF);
    CHECK(!esc_stick_out(&r.e)->supply_on);
    CHECK(!r.supply_on);
    CHECK(esc_stick_out(&r.e)->arm);
    CHECK(esc_stick_out(&r.e)->throttle_pct == ESC_STICK_PCT_MIN);
    CHECK_EQ(r.e.entries, 0);
    run_for(ESC_STICK_HAND_MIN_MS);
    CHECK(esc_stick_confirm(&r.e));
    tick();
    CHECK_EQ(r.e.phase, ESC_STICK_POWER);
    CHECK(esc_stick_out(&r.e)->supply_on);
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);

    /* DONE, and a STOP before the next step: the supply never comes on. */
    rig_hand("kontronik-jazz", k, 1);
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_HAND_OFF, 60000u);
    run_for(ESC_STICK_HAND_MIN_MS);
    CHECK(esc_stick_confirm(&r.e));
    r.stops++;
    r.pressed++;
    tick();
    CHECK_EQ(r.e.reason, ESC_STICK_R_STOP);
    CHECK(!esc_stick_out(&r.e)->supply_on);
    ended_safe();
}

/* A button held 3 s while the supply comes on, and an entry of 2 s: the
 * pull asked after the entry waits for the hold to end, not the entry. */
TEST_CASE(the_entry_lasts_at_least_the_hold_at_power_up)
{
    static const esc_manual_t k[] = {
        { ESC_MANUAL_AT_POWER_UP, "Hold the button.", 3000u },
        { ESC_MANUAL_BEFORE_MENU, "Pull the jumper.", 0u, NULL, true },
    };
    rig_hand("kontronik-jazz", k, 2);
    CHECK_EQ(r.t.entry_ms, 2000u);
    esc_stick_change_t c[1] = { change(1, 3) };
    CHECK(start(c, 1));
    CHECK_EQ(esc_stick_entry_ms(&r.e), 3000u);
    run_until_phase(ESC_STICK_HAND_OFF, 60000u);
    run_for(ESC_STICK_HAND_MIN_MS);
    CHECK(esc_stick_confirm(&r.e));
    run_until_phase(ESC_STICK_ENTRY, 10000u);
    CHECK_EQ(r.e.phase, ESC_STICK_ENTRY);
    const uint32_t on = r.e.on_ms;
    run_until_asked(10000u);
    CHECK(r.e.hand_menu);
    CHECK(r.e.hand_ms - on >= 3000u);
    CHECK(r.e.hand_ms - on < 3010u);
    /* Without the hold, the entry's own 2 s. */
    static const esc_manual_t pull[] = {
        { ESC_MANUAL_BEFORE_MENU, "Pull the jumper.", 0u, NULL, true },
    };
    rig_hand("kontronik-jazz", pull, 1);
    CHECK(start(c, 1));
    CHECK_EQ(esc_stick_entry_ms(&r.e), 2000u);
    CHECK_EQ(esc_stick_entry_ms(NULL), 0u);
}

/* One change per power-up and a jumper fitted before each: the warning
 * covers the first power-up, and the run asks before every later one, with
 * the supply off and seen off. */
TEST_CASE(a_step_before_power_is_asked_before_every_later_power_up)
{
    static const esc_manual_t k[] = {
        { ESC_MANUAL_BEFORE_POWER, "Fit the jumper.", 0u },
        { ESC_MANUAL_AFTER_PROGRAMMING, "Pull the jumper.", 0u },
    };
    rig_hand("sunrise-pro", k, 2);
    CHECK_EQ(esc_stick_kind(r.p, NULL), ESC_STICK_KIND_ONE_STAGE);
    esc_stick_change_t c[2] = { change(1, 3), change(2, 4) };
    CHECK(start(c, 2));
    run_until_phase(ESC_STICK_ENTRY, 60000u);
    CHECK_EQ(r.e.phase, ESC_STICK_ENTRY);         /* first: not asked */
    run_until_phase(ESC_STICK_HAND_OFF, 240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_OFF);
    CHECK_EQ(r.e.entries, 1);
    CHECK(!esc_stick_out(&r.e)->supply_on);
    CHECK(!r.supply_on);
    const esc_manual_t *m = esc_stick_hand(&r.e);
    CHECK(m != NULL);
    if (m != NULL) {
        CHECK_STR_EQ(m->action, "Fit the jumper.");
    }
    /* The supply has been off at least OFF TIME by now. */
    run_for(ESC_STICK_HAND_MIN_MS);
    CHECK(esc_stick_confirm(&r.e));
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(r.e.entries, 2);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);
    CHECK_EQ(esc_sim_stored(&r.sim, 2), 4);
}

/* What the run cannot wait for is refused before it starts. */
TEST_CASE(a_step_the_run_cannot_wait_for_is_refused)
{
    const char *why = NULL;
    static const esc_manual_t during[] = {
        { ESC_MANUAL_DURING_MENU, "Pull the jumper.", 0u },
    };
    rig_hand("kontronik-jazz", during, 1);
    CHECK_EQ(esc_stick_kind(r.p, &why), ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "manual step");
    /* A hand at a powered ESC with the stick at MAX: sunrise-pro enters
     * at full throttle. */
    static const esc_manual_t menu[] = {
        { ESC_MANUAL_BEFORE_MENU, "Press the button.", 0u },
    };
    rig_hand("sunrise-pro", menu, 1);
    CHECK_EQ(esc_stick_kind(r.p, &why), ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "manual step");
    static const esc_manual_t at_power[] = {
        { ESC_MANUAL_AT_POWER_UP, "Hold the button.", 0u },
    };
    rig_hand("sunrise-pro", at_power, 1);
    CHECK_EQ(esc_stick_kind(r.p, &why), ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "manual step");
    /* Before the power and after the run nobody reaches a powered ESC. */
    static const esc_manual_t outside[] = {
        { ESC_MANUAL_BEFORE_POWER, "Fit the jumper.", 0u },
        { ESC_MANUAL_AFTER_PROGRAMMING, "Pull the jumper.", 0u },
    };
    rig_hand("sunrise-pro", outside, 2);
    CHECK_EQ(esc_stick_kind(r.p, &why), ESC_STICK_KIND_ONE_STAGE);
    /* Assisted with no steps: a person at the ESC the run cannot ask. */
    rig_hand("kontronik-jazz", outside, 0);
    CHECK_EQ(esc_stick_kind(r.p, &why), ESC_STICK_KIND_NONE);
    CHECK_STR_EQ(why, "needs a person at the ESC");
}

/* A car mode the manual programs from the middle is not stored from the
 * profile's brake position. */
/* A Kontronik car mode is programmed with the stick at motor-off in the
 * middle: the run powers it up there, and the simulated ESC, which counts
 * a value stored from another position as misplaced, stores it from MID. */
TEST_CASE(a_car_mode_is_powered_up_from_the_middle)
{
    rig("kontronik-beat");
    esc_stick_change_t c[1] = { change(1, 6) };
    CHECK_EQ(esc_stick_change_entry(r.p, &c[0]), ESC_THR_MID);
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_SIGNAL, 10000u);
    for (int i = 0; i < 5000 && !r.e.sig_moved; ++i) {
        tick();
    }
    CHECK(esc_stick_out(&r.e)->throttle_pct == ESC_STICK_PCT_MID);
    CHECK(!esc_stick_out(&r.e)->supply_on);
    run_until_asked(60000u);
    /* The jumper is pulled with the stick at MID, the manual's motor-off. */
    CHECK(esc_stick_out(&r.e)->throttle_pct == ESC_STICK_PCT_MID);
    act(true);
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(r.on_n, 1u);
    CHECK(r.on_pct[0] == ESC_STICK_PCT_MID);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 6);
    CHECK_EQ(r.sim.stored_from[1], ESC_THR_MID);
    CHECK_EQ(r.sim.misplaced, 0u);

    /* A mode programmed from the back still powers up at MIN. */
    rig("kontronik-beat");
    c[0] = change(1, 3);
    CHECK_EQ(esc_stick_change_entry(r.p, &c[0]), ESC_THR_MIN);
    CHECK(start(c, 1));
    run_until_asked(60000u);
    act(true);
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK(r.on_pct[0] == ESC_STICK_PCT_MIN);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);
    CHECK_EQ(r.sim.misplaced, 0u);
    CHECK_EQ(esc_stick_change_entry(NULL, &c[0]), ESC_THR_MIN);
}

/* SUN PLUS waits 5 s for modes 4 to 6 and 2 s for the rest: the button
 * is asked for when the mode's own wait is over, not the scheme's. */
TEST_CASE(a_value_waits_its_own_entry_time)
{
    static const struct { uint8_t mode; uint32_t ms; } k[] = {
        { 4, 5000u }, { 2, 2000u }, { 6, 5000u },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        rig("kontronik-sun-plus");
        esc_stick_change_t c[1] = { change(1, k[i].mode) };
        CHECK_EQ(esc_stick_change_entry_ms(r.p, &c[0], &r.t), k[i].ms);
        CHECK(start(c, 1));
        run_until_phase(ESC_STICK_ENTRY, 10000u);
        CHECK_EQ(esc_stick_entry_ms(&r.e), k[i].ms);
        const uint32_t on = r.e.on_ms;
        run_until_asked(20000u);
        CHECK(r.e.hand_menu);
        if (r.e.hand_ms - on < k[i].ms || r.e.hand_ms - on > k[i].ms + 5u) {
            T_FAIL("mode %u asked after %u ms, want %u", k[i].mode,
                   (unsigned)(r.e.hand_ms - on), (unsigned)k[i].ms);
        }
        act(true);
        run_for(240000u);
        CHECK_EQ(r.e.phase, ESC_STICK_DONE);
        CHECK_EQ(esc_sim_stored(&r.sim, 1), k[i].mode);
    }
    CHECK_EQ(esc_stick_change_entry_ms(NULL, NULL, &r.t), r.t.entry_ms);
    CHECK_EQ(esc_stick_change_entry_ms(NULL, NULL, NULL), 0u);
}

/* SUN PLUS: the manual's neutral position is the back (mode 4, "neutral
 * position (back position)", p.12 EN; mode 5's two-position switch, p.13
 * EN), so every mode powers up at MIN but mode 6, the car mode, whose
 * brake lies below its neutral: MID. */
TEST_CASE(sun_plus_powers_up_at_the_back_but_its_car_mode)
{
    rig("kontronik-sun-plus");
    for (uint8_t m = 1; m <= 9; ++m) {
        const esc_stick_change_t c = change(1, m);
        if (c.value == 255) {
            continue;           /* modes 7 and 8 are not in the list */
        }
        const esc_throttle_t want = (m == 6) ? ESC_THR_MID : ESC_THR_MIN;
        if (esc_stick_change_entry(r.p, &c) != want) {
            T_FAIL("mode %u powers up at %d", m,
                   (int)esc_stick_change_entry(r.p, &c));
        }
    }
    esc_stick_change_t c[1] = { change(1, 5) };
    CHECK(start(c, 1));
    run_until_asked(20000u);
    CHECK(r.e.hand_menu);
    CHECK(r.on_n >= 1u && r.on_pct[0] == ESC_STICK_PCT_MIN);
    act(true);
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 5);
}

/* A two-stage power-up takes one entry time for every change; a value
 * whose time is under the settle time cannot have a floor. */
TEST_CASE(an_entry_time_that_cannot_work_is_refused)
{
    const char *why = NULL;
    rig("hobbywing-flyfun-8item");
    static esc_profile_t two;
    static esc_item_t two_items[2];
    static esc_value_t two_values[2];
    two = *r.p;
    memcpy(two_items, r.p->items, sizeof(two_items));
    memcpy(two_values, r.p->items[1].values, sizeof(two_values));
    two_values[1].entry_hold_ms = 9000u;
    two_items[1].values = two_values;
    two.items = two_items;
    two.item_count = 2;
    esc_stick_change_t c[2] = { { 0u, 1u }, { 1u, 1u } };
    CHECK(!esc_stick_check(&two, c, 2, &r.t, &why));
    CHECK_STR_EQ(why, "changes need different entry times");
    CHECK(esc_stick_check(&two, &c[1], 1, &r.t, &why));
    two_values[1].entry_hold_ms = 400u;
    CHECK(!esc_stick_check(&two, &c[1], 1, &r.t, &why));
    CHECK_STR_EQ(why, "ENTRY above 500 ms");
}

/* Values asking 2 s and 3 s, with a button held 5 s at power-up: the run
 * waits 5 s for either, so they share a power-up.  The check compares what
 * the run waits, not what the values ask. */
TEST_CASE(shared_entry_times_compare_what_the_run_waits)
{
    const char *why = NULL;
    rig("hobbywing-flyfun-8item");
    static esc_profile_t two;
    static esc_item_t two_items[2];
    static esc_value_t v0[2], v1[2];
    static const esc_manual_t hold[] = {
        { ESC_MANUAL_AT_POWER_UP, "Hold SET.", 5000u },
    };
    two = *r.p;
    memcpy(two_items, r.p->items, sizeof(two_items));
    memcpy(v0, r.p->items[0].values, sizeof(v0));
    memcpy(v1, r.p->items[1].values, sizeof(v1));
    v0[1].entry_hold_ms = 2000u;
    v1[1].entry_hold_ms = 3000u;
    two_items[0].values = v0;
    two_items[1].values = v1;
    two.items = two_items;
    two.item_count = 2;
    /* A hand at a powered ESC is asked for at MIN. */
    two.entry_throttle = ESC_THR_MIN;
    two.select_throttle = ESC_THR_MAX;
    two.value_select_throttle = ESC_THR_MID;
    two.listen_throttle = ESC_THR_NONE;
    two.automatable = ESC_AUTO_ASSISTED;
    two.manual = hold;
    two.manual_count = 1;
    CHECK_EQ(esc_stick_kind(&two, NULL), ESC_STICK_KIND_TWO_STAGE);
    esc_stick_change_t c[2] = { { 0u, 1u }, { 1u, 1u } };
    CHECK_EQ(esc_stick_change_entry_ms(&two, &c[0], &r.t), 5000u);
    CHECK_EQ(esc_stick_change_entry_ms(&two, &c[1], &r.t), 5000u);
    CHECK(esc_stick_check(&two, c, 2, &r.t, &why));
    /* A hold shorter than one of them: 3 s against 2.5 s, refused. */
    static const esc_manual_t short_hold[] = {
        { ESC_MANUAL_AT_POWER_UP, "Hold SET.", 2500u },
    };
    two.manual = short_hold;
    CHECK(!esc_stick_check(&two, c, 2, &r.t, &why));
    CHECK_STR_EQ(why, "changes need different entry times");
}

/* PIX mode 2: full throttle selects, then the stick goes to the brake
 * and the ESC answers before the mode is stored.  The simulated ESC keeps
 * nothing without that move, so a run that skipped it would fail here. */
TEST_CASE(a_value_makes_its_moves_after_the_selection)
{
    rig("kontronik-pix");
    esc_stick_change_t c[1] = { change(1, 2) };
    CHECK(start(c, 1));
    run_until_asked(60000u);
    act(true);
    run_until_phase(ESC_STICK_STORE, 240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_STORE);
    CHECK(esc_stick_out(&r.e)->throttle_pct == ESC_STICK_PCT_MAX);
    CHECK_EQ(esc_stick_store_move(&r.e, 0u), ESC_THR_MIN);
    CHECK_EQ(esc_stick_store_move(&r.e, 1u), ESC_THR_NONE);
    CHECK_EQ(r.sim.stores, 0u);                /* selected, not stored */
    run_for(r.t.store_ms + 5u);
    CHECK_EQ(r.e.store_step, 1u);
    CHECK(esc_stick_out(&r.e)->throttle_pct == ESC_STICK_PCT_MIN);
    CHECK(esc_stick_out(&r.e)->supply_on);    /* the ESC answers first */
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 2);
    CHECK_EQ(r.sim.stores, 1u);

    /* A mode with no move after it powers off from full throttle. */
    rig("kontronik-pix");
    c[0] = change(1, 3);
    CHECK(start(c, 1));
    run_until_asked(60000u);
    act(true);
    run_until_phase(ESC_STICK_STORE, 240000u);
    CHECK_EQ(esc_stick_store_move(&r.e, 0u), ESC_THR_NONE);
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);
    CHECK_EQ(esc_stick_store_move(NULL, 0u), ESC_THR_NONE);

    /* The same run with the move left out of the engine's profile: the ESC
     * still waits for it, and nothing is stored. */
    rig("kontronik-pix");
    static esc_profile_t bare;
    static esc_item_t bare_item;
    static esc_value_t bare_values[5];
    bare = *r.p;
    bare_item = r.p->items[0];
    memcpy(bare_values, r.p->items[0].values, sizeof(bare_values));
    bare_values[1].after_count = 0u;
    bare_item.values = bare_values;
    bare.items = &bare_item;
    r.p = &bare;
    c[0] = change(1, 2);
    CHECK(start(c, 1));
    run_until_asked(60000u);
    act(true);
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(r.sim.stores, 0u);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 0);
}

/* A car mode from the middle: power-up at MID, full throttle selects, the
 * brake stores. */
TEST_CASE(a_car_mode_selects_at_full_and_stores_at_the_brake)
{
    rig("kontronik-jazz");
    esc_stick_change_t c[1] = { change(1, 6) };
    CHECK(start(c, 1));
    run_until_asked(60000u);
    act(true);
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK(r.on_pct[0] == ESC_STICK_PCT_MID);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 6);
    CHECK_EQ(r.sim.misplaced, 0u);
    /* Two stages: no moves after a value. */
    rig("hobbywing-flyfun-8item");
    static esc_profile_t two;
    static esc_item_t two_items[1];
    static esc_value_t two_values[2];
    two = *r.p;
    two_items[0] = r.p->items[0];
    memcpy(two_values, r.p->items[0].values, sizeof(two_values));
    two_values[1].after_count = 1u;
    two_values[1].after[0] = ESC_THR_MID;
    two_items[0].values = two_values;
    two.items = two_items;
    two.item_count = 1;
    const char *why = NULL;
    esc_stick_change_t d[1] = { { 0u, 1u } };
    CHECK(!esc_stick_check(&two, d, 1, &r.t, &why));
    CHECK_STR_EQ(why, "moves after the value, two stages");

    /* A move to where the stick already is -- the select position, or
     * the move before -- is no move, and refused. */
    rig("kontronik-pix");
    static esc_profile_t pix;
    static esc_item_t pix_item;
    static esc_value_t pix_values[5];
    pix = *r.p;
    pix_item = r.p->items[0];
    memcpy(pix_values, r.p->items[0].values, sizeof(pix_values));
    pix_item.values = pix_values;
    pix.items = &pix_item;
    esc_stick_change_t p2[1] = { { 0u, 1u } };   /* mode 2 */
    CHECK(esc_stick_check(&pix, p2, 1, &r.t, &why));
    pix_values[1].after[0] = ESC_THR_MAX;        /* the select position */
    CHECK(!esc_stick_check(&pix, p2, 1, &r.t, &why));
    CHECK_STR_EQ(why, "a move after the value makes no move");
    pix_values[1].after[0] = ESC_THR_MIN;
    pix_values[1].after[1] = ESC_THR_MIN;        /* the same twice */
    pix_values[1].after_count = 2u;
    CHECK(!esc_stick_check(&pix, p2, 1, &r.t, &why));
    CHECK_STR_EQ(why, "a move after the value makes no move");
    pix_values[1].after[1] = ESC_THR_MID;
    CHECK(esc_stick_check(&pix, p2, 1, &r.t, &why));
    /* Every value of record moves. */
    for (size_t i = 0; i < esc_profiles_count(); ++i) {
        const esc_profile_t *q = esc_profiles_at(i);
        for (unsigned it = 0; it < q->item_count; ++it) {
            for (unsigned v = 0; v < q->items[it].value_count; ++v) {
                const esc_value_t *x = &q->items[it].values[v];
                esc_throttle_t at = (q->store_throttle != ESC_THR_NONE)
                                        ? q->store_throttle
                                        : q->select_throttle;
                for (unsigned k = 0; k < x->after_count; ++k) {
                    if (x->after[k] == at) {
                        T_FAIL("%s value %u: no move", q->id, x->number);
                    }
                    at = x->after[k];
                }
            }
        }
    }
}

/* sunrise-pro with value 5 of item 2 programmed from the middle. */
static esc_profile_t g_mid;
static esc_item_t    g_mid_items[2];
static esc_value_t   g_mid_values[2];

static void rig_mid(void)
{
    rig("sunrise-pro");
    g_mid = *r.p;
    memcpy(g_mid_items, r.p->items, sizeof(g_mid_items));
    memcpy(g_mid_values, r.p->items[1].values, sizeof(g_mid_values));
    g_mid_values[1].entry_throttle = ESC_THR_MID;
    g_mid_items[1].values = g_mid_values;
    g_mid.items = g_mid_items;
    r.p = &g_mid;
    esc_sim_cfg_t cfg;
    esc_sim_defaults(&cfg);
    esc_sim_init(&r.sim, r.p, &cfg);
}

/* One change a power-up, each from its own position: the first at the
 * profile's MAX, the second at MID.  The stick moves only once the supply
 * reads off, however late the module follows the OFF. */
TEST_CASE(each_power_up_enters_from_the_position_of_its_change)
{
    rig_mid();
    r.off_lag_ms = 400u;
    esc_stick_change_t c[2] = { change(1, 3), change(2, 5) };
    CHECK(start(c, 2));
    run_for(480000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(r.e.entries, 2);
    CHECK_EQ(r.on_n, 2u);
    CHECK(r.on_pct[0] == ESC_STICK_PCT_MAX);
    CHECK(r.on_pct[1] == ESC_STICK_PCT_MID);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);
    CHECK_EQ(esc_sim_stored(&r.sim, 2), 5);
    CHECK_EQ(r.sim.stored_from[2], ESC_THR_MID);
    CHECK_EQ(r.sim.misplaced, 0u);
    CHECK_EQ(r.moved_while_on, 0u);

    /* The other order: MID first, then MAX. */
    rig_mid();
    r.off_lag_ms = 400u;
    esc_stick_change_t d[2] = { change(2, 5), change(1, 2) };
    CHECK(start(d, 2));
    run_for(480000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK(r.on_pct[0] == ESC_STICK_PCT_MID);
    CHECK(r.on_pct[1] == ESC_STICK_PCT_MAX);
    CHECK_EQ(r.sim.misplaced, 0u);
    CHECK_EQ(r.moved_while_on, 0u);

    /* A supply that never reads off: the stick never leaves the store
     * position for the next entry, and the run ends SUPPLY STAYS ON. */
    rig_mid();
    r.supply_stuck = true;
    CHECK(start(c, 2));
    run_for(480000u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_SUPPLY_ON);
    CHECK_EQ(r.on_n, 1u);
    CHECK_EQ(r.moved_while_on, 0u);
}

/* What one power-up cannot take is refused, not reordered or guessed. */
TEST_CASE(a_power_up_position_that_cannot_work_is_refused)
{
    const char *why = NULL;
    /* Two stages: the ESC picks the order, so the changes share one. */
    rig("hobbywing-flyfun-8item");
    static esc_profile_t two;
    static esc_item_t two_items[2];
    static esc_value_t two_values[2];
    two = *r.p;
    memcpy(two_items, r.p->items, sizeof(two_items));
    memcpy(two_values, r.p->items[1].values, sizeof(two_values));
    two_values[1].entry_throttle = ESC_THR_MID;
    two_items[1].values = two_values;
    two.items = two_items;
    two.item_count = 2;
    esc_stick_change_t c[2] = { { 0u, 1u }, { 1u, 1u } };
    CHECK(!esc_stick_check(&two, c, 2, &r.t, &why));
    CHECK_STR_EQ(why, "changes need different power-up positions");
    c[0] = (esc_stick_change_t){ 1u, 1u };
    CHECK(esc_stick_check(&two, c, 1, &r.t, &why));
    /* The rest there is the select move: hobbywing selects at MIN. */
    two_values[1].entry_throttle = ESC_THR_MIN;
    CHECK(!esc_stick_check(&two, c, 1, &r.t, &why));
    CHECK_STR_EQ(why, "select move is the rest");

    /* A hand at a powered ESC with the stick at MAX. */
    rig("kontronik-jazz");
    static esc_profile_t jazz;
    static esc_item_t jazz_item;
    static esc_value_t jazz_values[9];
    jazz = *r.p;
    jazz_item = r.p->items[0];
    memcpy(jazz_values, r.p->items[0].values,
           r.p->items[0].value_count * sizeof(jazz_values[0]));
    jazz_values[2].entry_throttle = ESC_THR_MAX;
    jazz_item.values = jazz_values;
    jazz.items = &jazz_item;
    esc_stick_change_t m[1] = { { 0u, 2u } };
    CHECK(!esc_stick_check(&jazz, m, 1, &r.t, &why));
    CHECK_STR_EQ(why, "select move is the rest");
    jazz.select_throttle = ESC_THR_MID;
    CHECK(!esc_stick_check(&jazz, m, 1, &r.t, &why));
    CHECK_STR_EQ(why, "manual step");
    /* A named rest away from a value's position needs the entry time. */
    jazz.listen_throttle = ESC_THR_MIN;
    jazz.entry_hold_ms = 0u;
    jazz_values[2].entry_throttle = ESC_THR_MID;
    jazz.select_throttle = ESC_THR_MAX;
    CHECK(!esc_stick_check(&jazz, m, 1, &r.t, &why));
    CHECK_STR_EQ(why, "rest move, no entry time");
}

/* A button to hold while the supply comes on: the step at an unpowered
 * ESC. */
static const esc_manual_t k_hold_button[] = {
    { ESC_MANUAL_AT_POWER_UP, "Hold the button.", 0u },
};

/* A run started with the module's output still on: nothing is powered or
 * asked of a person until the supply reads off; a module that stays on
 * ends the run with SUPPLY STAYS ON, the step never shown. */
TEST_CASE(a_run_started_with_the_output_on_asks_nothing)
{
    rig_hand("kontronik-jazz", k_hold_button, 1);
    r.supply_on = true;                 /* live before the run */
    r.supply_stuck = true;
    esc_stick_change_t c[1] = { change(1, 3) };
    CHECK(start(c, 1));
    bool asked = false, powered = false;
    for (int i = 0; i < 10000 && esc_stick_running(&r.e); ++i) {
        tick();
        asked = asked || esc_stick_hand(&r.e) != NULL;
        powered = powered || esc_stick_out(&r.e)->supply_on;
    }
    CHECK(!asked);
    CHECK(!powered);
    CHECK_EQ(r.e.reason, ESC_STICK_R_SUPPLY_ON);
    CHECK(esc_stick_reason_is_fault(r.e.reason));
    ended_safe();

    /* The same module going off 600 ms into the run: the step is asked
     * only once it has read off for ESC_STICK_OFF_SETTLE_MS. */
    rig_hand("kontronik-jazz", k_hold_button, 1);
    r.supply_on = true;
    r.off_at = r.now;
    r.off_lag_ms = 600u;
    CHECK(start(c, 1));
    uint32_t off_at = 0u;
    for (int i = 0; i < 10000 && esc_stick_running(&r.e)
                    && esc_stick_hand(&r.e) == NULL; ++i) {
        tick();
        if (!r.supply_on && off_at == 0u) {
            off_at = r.now;
        }
    }
    CHECK(esc_stick_hand(&r.e) != NULL);
    CHECK(off_at != 0u);
    CHECK(r.now - off_at >= ESC_STICK_OFF_SETTLE_MS);
    CHECK(!r.supply_on);
}

/* While a step at an unpowered ESC is asked, a reading that shows the
 * output on, or the current up, ends the run at once; readings that stop
 * end it too. */
TEST_CASE(a_supply_that_comes_on_during_the_step_ends_the_run)
{
    esc_stick_change_t c[1] = { change(1, 3) };
    for (int k = 0; k < 3; ++k) {
        rig_hand("kontronik-jazz", k_hold_button, 1);
        CHECK(start(c, 1));
        run_until_phase(ESC_STICK_HAND_OFF, 60000u);
        CHECK_EQ(r.e.phase, ESC_STICK_HAND_OFF);
        CHECK(esc_stick_hand(&r.e) != NULL);
        if (k == 0) {
            r.supply_on = true;         /* the module, by itself */
            r.supply_stuck = true;
        } else if (k == 1) {
            r.extra_ma = 200;           /* current through the ESC */
        } else {
            r.readings_stop = true;
        }
        const uint32_t at = r.now;
        run_for(5000u);
        if (k < 2) {
            CHECK_EQ(r.e.reason, ESC_STICK_R_SUPPLY_ON);
            CHECK(r.e.phase_ms - at <= r.read_iv + 2u);   /* the next one */
        } else {
            CHECK_EQ(r.e.reason, ESC_STICK_R_STALE);
        }
        CHECK(esc_stick_hand(&r.e) == NULL);
        ended_safe();
    }

    /* Before a later power-up: a module on again after the run saw it off
     * ends the run before the jumper is asked for. */
    static const esc_manual_t fit[] = {
        { ESC_MANUAL_BEFORE_POWER, "Fit the jumper.", 0u },
    };
    rig_hand("sunrise-pro", fit, 1);
    esc_stick_change_t two[2] = { change(1, 3), change(2, 4) };
    CHECK(start(two, 2));
    run_until_phase(ESC_STICK_CYCLE, 240000u);
    for (int i = 0; i < 20000 && esc_stick_running(&r.e)
                    && !r.e.cycle_moved; ++i) {
        tick();
    }
    CHECK(r.e.cycle_moved);
    r.supply_on = true;
    r.supply_stuck = true;
    bool asked = false;
    for (int i = 0; i < 20000 && esc_stick_running(&r.e); ++i) {
        tick();
        asked = asked || esc_stick_hand(&r.e) != NULL;
    }
    CHECK(!asked);
    CHECK_EQ(r.e.reason, ESC_STICK_R_SUPPLY_ON);
}

/* Run for @p ms; the time the module is powered while the stick, armed
 * and not asked on, is away from MIN. */
static uint32_t powered_away_from_min(uint32_t ms)
{
    uint32_t n = 0u;
    for (uint32_t i = 0; i < ms && esc_stick_running(&r.e); ++i) {
        tick();
        if (r.supply_on && !esc_stick_out(&r.e)->supply_on && r.armed
            && esc_stick_out(&r.e)->throttle_pct > ESC_STICK_PCT_MIN) {
            ++n;
        }
    }
    return n;
}

/* A module live from the start: a MID entry (the car mode) and a MAX one
 * keep the stick at MIN until the run ends with SUPPLY STAYS ON. */
TEST_CASE(the_stick_stays_at_min_until_the_supply_reads_off)
{
    static const char *const ids[] = { "kontronik-beat", "sunrise-pro" };
    for (size_t k = 0; k < 2; ++k) {
        rig(ids[k]);
        esc_stick_change_t c[1] = { (k == 0) ? change(1, 6) : change(1, 3) };
        CHECK(esc_stick_change_entry(r.p, &c[0]) != ESC_THR_MIN);
        r.supply_on = true;
        r.supply_stuck = true;
        CHECK(start(c, 1));
        CHECK_EQ(powered_away_from_min(10000u), 0u);
        CHECK_EQ(r.e.reason, ESC_STICK_R_SUPPLY_ON);
        ended_safe();
    }
}

/* The module coming on by itself after the stick went to the entry: the
 * run ends on the next reading, in SIGNAL and in a power cycle alike. */
TEST_CASE(a_live_reading_after_the_entry_move_ends_the_run_at_once)
{
    rig("sunrise-pro");
    esc_stick_change_t c[1] = { change(1, 3) };
    CHECK(start(c, 1));
    for (int i = 0; i < 10000 && !r.e.sig_moved; ++i) {
        tick();
    }
    CHECK_EQ(r.e.phase, ESC_STICK_SIGNAL);
    CHECK(r.e.sig_moved);
    r.supply_on = true;
    r.supply_stuck = true;
    CHECK(powered_away_from_min(10000u) <= r.read_iv + 2u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_SUPPLY_ON);
    ended_safe();

    rig("sunrise-pro");
    esc_stick_change_t two[2] = { change(1, 3), change(2, 4) };
    CHECK(start(two, 2));
    run_until_phase(ESC_STICK_CYCLE, 240000u);
    for (int i = 0; i < 20000 && esc_stick_running(&r.e)
                    && !r.e.cycle_moved; ++i) {
        tick();
    }
    CHECK(r.e.cycle_moved);
    run_for(100u);
    r.supply_on = true;
    r.supply_stuck = true;
    CHECK(powered_away_from_min(10000u) <= r.read_iv + 2u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_SUPPLY_ON);
    ended_safe();
}

/* Readings that stop after the supply read off: no step is asked, and
 * nothing powered, on a reading older than ESC_STICK_STALE_MS. */
TEST_CASE(no_step_is_asked_on_an_old_reading)
{
    static const esc_manual_t fit[] = {
        { ESC_MANUAL_BEFORE_POWER, "Fit the jumper.", 0u, NULL, false },
    };
    rig_hand("sunrise-pro", fit, 1);
    esc_stick_change_t two[2] = { change(1, 3), change(2, 4) };
    CHECK(start(two, 2));
    run_until_phase(ESC_STICK_CYCLE, 240000u);
    for (int i = 0; i < 20000 && esc_stick_running(&r.e) && !r.e.off_seen;
         ++i) {
        tick();
    }
    CHECK(r.e.off_seen);
    r.readings_stop = true;
    bool asked = false, powered = false;
    for (int i = 0; i < 20000 && esc_stick_running(&r.e); ++i) {
        tick();
        asked = asked || esc_stick_hand(&r.e) != NULL;
        powered = powered || esc_stick_out(&r.e)->supply_on;
    }
    CHECK(!asked);
    CHECK(!powered);
    CHECK_EQ(r.e.reason, ESC_STICK_R_STALE);
    ended_safe();
}

int main(void)
{
    RUN(beeps_make_a_group_that_silence_ends);
    RUN(long_beeps_count_by_the_profiles_measure);
    RUN(a_long_beep_where_none_belongs_spoils_the_group);
    RUN(pulses_and_gaps_outside_their_lengths_spoil_the_group);
    RUN(hysteresis_holds_a_beep_through_a_dip);
    RUN(a_current_that_stays_high_is_reported_once);
    RUN(the_floor_is_the_lowest_current_before_counting);
    RUN(the_profiles_the_engine_runs_are_the_counted_menus);
    RUN(a_run_that_cannot_work_is_refused_before_it_starts);
    RUN(the_voltage_is_the_lowest_cell_count_the_family_states);
    RUN(a_two_stage_short_long_menu_stores_what_was_asked);
    RUN(a_slow_jittery_supply_still_counts_right);
    RUN(noise_inside_the_hysteresis_does_not_count);
    RUN(a_one_stage_menu_of_repeated_groups_stores_its_value);
    RUN(one_change_per_entry_cycles_the_power);
    RUN(the_stick_rests_and_stores_where_the_profile_says);
    RUN(the_supply_goes_off_before_the_stick_moves);
    RUN(reset_and_exit_are_not_changes);
    RUN(the_stick_waits_for_the_supplys_own_off);
    RUN(an_item_with_one_value_is_not_offered);
    RUN(the_simulation_acts_on_reset_and_exit);
    RUN(a_partial_first_group_and_a_lost_beep_store_nothing_wrong);
    RUN(a_sweep_of_lost_beeps_and_entry_times_stores_nothing_wrong);
    RUN(a_link_that_comes_up_and_goes_is_lost);
    RUN(a_two_stage_menu_whose_groups_repeat_stores_its_value);
    RUN(a_missed_beep_does_not_select_the_number_below);
    RUN(a_menu_that_does_not_match_the_profile_times_out);
    RUN(an_esc_that_never_beeps_ends_the_run);
    RUN(abort_stop_and_disarm_end_the_run_safe);
    RUN(a_link_that_was_never_up_is_not_lost);
    RUN(readings_too_far_apart_end_the_run);
    RUN(one_late_reading_passes_a_group_and_the_run_goes_on);
    RUN(readings_the_panel_never_saw_are_late);
    RUN(a_reading_ahead_of_the_step_is_not_stale);
    RUN(a_bench_that_will_not_arm_or_power_ends_the_run);
    RUN(a_current_that_stays_high_ends_the_run);
    RUN(the_signal_is_in_place_before_the_power);
    RUN(every_reason_and_phase_has_its_words);
    RUN(the_red_light_is_for_ends_nobody_chose);
    RUN(a_stop_nobody_pressed_is_the_benchs_own);
    RUN(the_green_light_is_dark_while_the_supply_is_off);
    RUN(the_run_counts_every_pulse_the_detector_begins);
    RUN(the_green_light_shows_every_pulse_for_its_minimum);
    RUN(the_simulation_enters_only_from_the_entry_position);
    RUN(a_jumper_pulled_after_the_entry_is_waited_for);
    RUN(a_pull_starts_the_menu_and_no_group_is_lost);
    RUN(an_earlier_step_still_waits_for_done);
    RUN(a_step_never_confirmed_ends_the_run);
    RUN(the_supply_stays_on_until_the_esc_has_confirmed);
    RUN(stop_abort_and_the_supply_end_a_run_waiting_for_a_step);
    RUN(a_step_at_power_up_is_asked_with_the_supply_off);
    RUN(the_entry_lasts_at_least_the_hold_at_power_up);
    RUN(a_step_before_power_is_asked_before_every_later_power_up);
    RUN(a_step_the_run_cannot_wait_for_is_refused);
    RUN(a_car_mode_is_powered_up_from_the_middle);
    RUN(each_power_up_enters_from_the_position_of_its_change);
    RUN(a_power_up_position_that_cannot_work_is_refused);
    RUN(a_value_waits_its_own_entry_time);
    RUN(sun_plus_powers_up_at_the_back_but_its_car_mode);
    RUN(an_entry_time_that_cannot_work_is_refused);
    RUN(shared_entry_times_compare_what_the_run_waits);
    RUN(a_run_started_with_the_output_on_asks_nothing);
    RUN(a_supply_that_comes_on_during_the_step_ends_the_run);
    RUN(the_stick_stays_at_min_until_the_supply_reads_off);
    RUN(a_live_reading_after_the_entry_move_ends_the_run_at_once);
    RUN(no_step_is_asked_on_an_old_reading);
    RUN(a_value_makes_its_moves_after_the_selection);
    RUN(a_car_mode_selects_at_full_and_stores_at_the_brake);
    return test_summary("esc_stick");
}
