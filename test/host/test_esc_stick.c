/*
 * Stick programming against the simulated ESC (electronic speed
 * controller): whole runs through the modelled bench of esc_stick_rig.h --
 * an arm that follows the ARM the engine asks for, a supply that switches
 * when asked and is read at a chosen rate, and the simulation drawing the
 * current -- and the limits to the readings a run takes.
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
#include "tick_wrap.h"

#include "esc_stick_rig.h"

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

/* Readings at the PD mini's 100ms with jitter up to the 200ms the default
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

/* Without an IOmcu the bench is modelled, and the run is allowed;
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

/* ------------------------------------------- limits to the reading */

/*
 * The supply is off at a current of ESC_STICK_OFF_MA and not 1mA above
 * it: a module that reads 20mA while off is taken as off and the run
 * stores its value; one that reads 21mA never is, and the run ends with
 * SUPPLY STAYS ON before anything is powered.
 */
TEST_CASE(the_supply_is_off_at_20_ma_and_not_at_21)
{
    rig("sunrise-pro");
    r.off_ma = ESC_STICK_OFF_MA;
    esc_stick_change_t c[1] = { change(2, 5) };
    CHECK(start(c, 1));
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 2), 5);

    rig("sunrise-pro");
    r.off_ma = ESC_STICK_OFF_MA + 1;
    CHECK(start(c, 1));
    run_for(240000u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_SUPPLY_ON);
    CHECK_EQ(r.e.entries, 0);
    CHECK_EQ(r.on_n, 0u);
    ended_safe();
}

/* Until the readings asked to come without a current have been given. */
static void run_out_no_current(void)
{
    while (r.no_current > 0u && esc_stick_running(&r.e)) {
        tick();
    }
}

/*
 * A reading without a current is a reading missed.  One spoils the group
 * it falls in and the run goes on; ESC_STICK_LATE_RUN less one in a row
 * still do; ESC_STICK_LATE_RUN in a row end the run at that reading, not
 * one later.
 */
TEST_CASE(a_reading_without_a_current_is_a_reading_missed)
{
    rig("hobbywing-flyfun-8item");
    esc_stick_change_t c[1] = { change(3, 2) };
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_ITEMS, 60000u);
    r.no_current = 1u;
    run_out_no_current();
    CHECK(esc_stick_running(&r.e));
    CHECK_EQ(r.e.late_run, 1u);
    run_for(400000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 3), 2);
    CHECK_EQ(r.sim.stores, 1u);

    rig("hobbywing-flyfun-8item");
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_ITEMS, 60000u);
    r.no_current = ESC_STICK_LATE_RUN - 1u;
    run_out_no_current();
    CHECK(esc_stick_running(&r.e));
    CHECK_EQ(r.e.late_run, ESC_STICK_LATE_RUN - 1u);
    uint32_t seq = r.seq;
    while (r.seq == seq && esc_stick_running(&r.e)) {
        tick();                                 /* one with a current */
    }
    CHECK_EQ(r.e.late_run, 0u);
    seq = r.seq;
    r.no_current = ESC_STICK_LATE_RUN + 2u;
    run_for(5000u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_RATE);
    CHECK_EQ(r.seq - seq, ESC_STICK_LATE_RUN);
    CHECK_EQ(r.sim.stores, 0u);
    ended_safe();
}

/* Readings a count shows the panel never saw end the run the same way: at
 * the ESC_STICK_LATE_RUN-th in a row. */
TEST_CASE(the_third_late_reading_in_a_row_ends_the_run)
{
    rig("hobbywing-flyfun-8item");
    esc_stick_change_t c[1] = { change(3, 2) };
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_ITEMS, 60000u);
    const uint32_t seq = r.seq;
    r.skip = 1u;                                /* the count steps by 2 */
    run_for(5000u);
    CHECK_EQ(r.e.reason, ESC_STICK_R_RATE);
    CHECK_EQ((r.seq - seq) / 2u, ESC_STICK_LATE_RUN);
    ended_safe();
}

/* How long a flyfun run takes from its start to its first reading in the
 * item menu. */
#define STALE_PROFILE "hobbywing-flyfun-8item"

static uint32_t ms_to_the_menu(void)
{
    rig_at(STALE_PROFILE, 0u);
    esc_stick_change_t c[1] = { change(3, 2) };
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_ITEMS, 60000u);
    CHECK_EQ(r.e.phase, ESC_STICK_ITEMS);
    return r.e.read_ms;
}

/*
 * Readings that stop in a powered phase end the run ESC_STICK_STALE_MS
 * after the last one: still running at 1000ms, ended at 1001ms, and
 * ended safe.
 */
static void readings_stop_in_the_menu(uint32_t t0)
{
    rig_at(STALE_PROFILE, t0);
    esc_stick_change_t c[1] = { change(3, 2) };
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_ITEMS, 60000u);
    CHECK_EQ(r.e.phase, ESC_STICK_ITEMS);
    const uint32_t last = r.e.read_ms;
    r.readings_stop = true;
    while (r.now - last <= ESC_STICK_STALE_MS) {
        tick();                     /* the step at r.now, then r.now + 1 */
    }
    CHECK(esc_stick_running(&r.e)); /* stepped at last + 1000 */
    tick();
    CHECK(!esc_stick_running(&r.e));
    CHECK_EQ(r.e.reason, ESC_STICK_R_STALE);
    CHECK_EQ(r.now - 1u - last, ESC_STICK_STALE_MS + 1u);
    ended_safe();
}

TEST_CASE(readings_that_stop_end_the_run_at_1001_ms_across_the_tick_wrap)
{
    /* The last reading 500ms before the wrap. */
    at_tick_0_and_before_the_wrap(readings_stop_in_the_menu,
                                  ms_to_the_menu() + 500u);
}

/*
 * The same with the supply off and a person asked to act at the ESC: no
 * reading for ESC_STICK_STALE_MS says the supply is still off, and the run
 * ends 1001ms after the last one with nothing powered.
 */
static void readings_stop_while_a_step_is_asked(uint32_t t0)
{
    static const esc_manual_t k[] = {
        { ESC_MANUAL_AT_POWER_UP, "Hold the button.", 3000u, NULL, false, false },
    };
    rig_hand("kontronik-jazz", k, 1);
    r.now = t0;
    esc_stick_change_t c[1] = { change(1, 3) };
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_HAND_OFF, 60000u);
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_OFF);
    const uint32_t seq = r.seq;
    while (r.seq == seq) {
        tick();                     /* to a reading taken in the phase */
    }
    const uint32_t last = r.e.read_ms;
    r.readings_stop = true;
    while (r.now - last <= ESC_STICK_STALE_MS) {
        tick();
    }
    CHECK(esc_stick_running(&r.e));
    CHECK_EQ(r.e.phase, ESC_STICK_HAND_OFF);
    tick();
    CHECK_EQ(r.e.reason, ESC_STICK_R_STALE);
    CHECK_EQ(r.now - 1u - last, ESC_STICK_STALE_MS + 1u);
    CHECK_EQ(r.e.entries, 0);
    CHECK_EQ(r.on_n, 0u);
    ended_safe();
}

TEST_CASE(readings_that_stop_before_the_power_end_the_run_at_1001_ms)
{
    /* How long the run takes to ask for the step, for a last reading
     * 500ms before the wrap. */
    static const esc_manual_t k[] = {
        { ESC_MANUAL_AT_POWER_UP, "Hold the button.", 3000u, NULL, false, false },
    };
    rig_hand("kontronik-jazz", k, 1);
    r.now = 0u;
    esc_stick_change_t c[1] = { change(1, 3) };
    CHECK(start(c, 1));
    run_until_phase(ESC_STICK_HAND_OFF, 60000u);
    at_tick_0_and_before_the_wrap(readings_stop_while_a_step_is_asked,
                                  r.now + 550u);
}

/* A whole run across the wrap stores what a run at tick 0 stores, in the
 * same time: every wait of the run is a difference of two times. */
static uint32_t g_run_ms;

static void a_whole_run_stores_its_value(uint32_t t0)
{
    rig_at("sunrise-pro", t0);
    esc_stick_change_t c[2] = { change(1, 3), change(2, 4) };
    CHECK(start(c, 2));
    run_for(240000u);
    CHECK_EQ(r.e.phase, ESC_STICK_DONE);
    CHECK_EQ(esc_sim_stored(&r.sim, 1), 3);
    CHECK_EQ(esc_sim_stored(&r.sim, 2), 4);
    CHECK_EQ(r.e.entries, 2);
    ended_safe();
    if (t0 == 0u) {
        g_run_ms = r.now;
    } else {
        CHECK_EQ(r.now - t0, g_run_ms);
        CHECK(r.now < t0);                      /* it did wrap */
    }
}

TEST_CASE(a_run_takes_the_same_time_across_the_tick_wrap)
{
    /* The wrap half way through the run at tick 0. */
    rig_at("sunrise-pro", 0u);
    esc_stick_change_t c[2] = { change(1, 3), change(2, 4) };
    CHECK(start(c, 2));
    run_for(240000u);
    at_tick_0_and_before_the_wrap(a_whole_run_stores_its_value, r.now / 2u);
}

int main(void)
{
    RUN(a_two_stage_short_long_menu_stores_what_was_asked);
    RUN(a_slow_jittery_supply_still_counts_right);
    RUN(noise_inside_the_hysteresis_does_not_count);
    RUN(a_one_stage_menu_of_repeated_groups_stores_its_value);
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
    RUN(every_reason_and_phase_has_its_words);
    RUN(the_red_light_is_for_ends_nobody_chose);
    RUN(a_stop_nobody_pressed_is_the_benchs_own);
    RUN(the_green_light_is_dark_while_the_supply_is_off);
    RUN(the_run_counts_every_pulse_the_detector_begins);
    RUN(the_green_light_shows_every_pulse_for_its_minimum);
    RUN(the_supply_is_off_at_20_ma_and_not_at_21);
    RUN(a_reading_without_a_current_is_a_reading_missed);
    RUN(the_third_late_reading_in_a_row_ends_the_run);
    RUN(readings_that_stop_end_the_run_at_1001_ms_across_the_tick_wrap);
    RUN(readings_that_stop_before_the_power_end_the_run_at_1001_ms);
    RUN(a_run_takes_the_same_time_across_the_tick_wrap);
    return test_summary("esc_stick");
}
