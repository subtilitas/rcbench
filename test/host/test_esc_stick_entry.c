/*
 * Stick programming: the power-up.  The position the stick enters each
 * change from, the entry time a value waits, the stick moves after a
 * selection, and the supply read off before the stick moves.
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

#include "esc_stick_rig.h"

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

int main(void)
{
    RUN(one_change_per_entry_cycles_the_power);
    RUN(the_stick_rests_and_stores_where_the_profile_says);
    RUN(the_supply_goes_off_before_the_stick_moves);
    RUN(the_stick_waits_for_the_supplys_own_off);
    RUN(the_signal_is_in_place_before_the_power);
    RUN(the_simulation_enters_only_from_the_entry_position);
    RUN(a_car_mode_is_powered_up_from_the_middle);
    RUN(each_power_up_enters_from_the_position_of_its_change);
    RUN(a_value_waits_its_own_entry_time);
    RUN(sun_plus_powers_up_at_the_back_but_its_car_mode);
    RUN(the_stick_stays_at_min_until_the_supply_reads_off);
    RUN(a_live_reading_after_the_entry_move_ends_the_run_at_once);
    RUN(a_value_makes_its_moves_after_the_selection);
    RUN(a_car_mode_selects_at_full_and_stores_at_the_brake);
    return test_summary("esc_stick_entry");
}
