/*
 * The limit run's aborts and protections, against the modelled servo.
 *
 * Under test: every abort source at every phase, the output asked off and
 * RESTORE named once anything was written; the run's own timeouts; the
 * 3.0 A ceiling, a clipped sample, 1.0 A for 400 ms, a supply changed
 * while a search drives the servo and left alone while the programmer holds
 * the port; no window for 199, 200 and 201 ms, also across the 2^32 ms tick
 * wrap; windows without the rail voltage or a current; a whole run across
 * the wrap.
 *
 * The rig is servo_limrun_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_limrun_rig.h"

/* --- aborts at every phase ----------------------------------------------- */

/* Every phase, at each place it comes in a run: the read session, before
 * anything moves (with SERVO PULSES 1000-2000, its pulse write too); the first
 * search; the sessions and searches after the first write. */
typedef enum { AT_READ = 0, AT_PULSES, AT_SEARCH, AT_LATER } at_t;

static const struct {
    servo_limrun_phase_t ph;
    at_t at;
    bool wrote;
} k_phases[] = {
    { SERVO_LIMRUN_PH_SAFE,    AT_READ,   false },
    { SERVO_LIMRUN_PH_CONNECT, AT_READ,   false },
    { SERVO_LIMRUN_PH_LEAVE,   AT_READ,   false },
    { SERVO_LIMRUN_PH_ARM,     AT_READ,   false },
    { SERVO_LIMRUN_PH_WRITE,   AT_PULSES, false },
    { SERVO_LIMRUN_PH_LEAVE,   AT_PULSES, true },
    { SERVO_LIMRUN_PH_ARM,     AT_PULSES, true },
    { SERVO_LIMRUN_PH_SEARCH,  AT_SEARCH, false },
    { SERVO_LIMRUN_PH_SAFE,    AT_LATER,  true },
    { SERVO_LIMRUN_PH_CONNECT, AT_LATER,  true },
    { SERVO_LIMRUN_PH_WRITE,   AT_LATER,  true },
    { SERVO_LIMRUN_PH_LEAVE,   AT_LATER,  true },
    { SERVO_LIMRUN_PH_ARM,     AT_LATER,  true },
    { SERVO_LIMRUN_PH_PROBE,   AT_LATER,  true },
    { SERVO_LIMRUN_PH_CHECK,   AT_LATER,  true },
};
#define N_PHASES (sizeof(k_phases) / sizeof(k_phases[0]))

/* The run in phase @p ph at place @p at. */
static bool to_phase_at(servo_limrun_phase_t ph, at_t at)
{
    rig_init(0u, false);
    g.cfg.set_pulses = at == AT_PULSES;
    if (!start()) {
        return false;
    }
    if (at == AT_LATER && ph != SERVO_LIMRUN_PH_CHECK
        && ph != SERVO_LIMRUN_PH_PROBE) {
        /* The second session's phase: a value is written by then. */
        if (!run_to(SERVO_LIMRUN_PH_PROBE, RUN_MS)) {
            return false;
        }
    }
    if (!run_to(ph, RUN_MS)) {
        return false;
    }
    if (at == AT_READ || at == AT_PULSES) {
        return !g.run.searched;
    }
    return g.run.searched;
}

/* The phase after the first write, as most tests below want it. */
static bool to_phase(servo_limrun_phase_t ph)
{
    return to_phase_at(ph, ph == SERVO_LIMRUN_PH_SEARCH ? AT_SEARCH
                                                        : AT_LATER);
}

/* Each reason the caller has: in every phase the run ends ABORTED, asks
 * the output off and lets the servo go, once; RESTORE is named where a
 * value was written. */
TEST_CASE(every_caller_abort_ends_every_phase_with_the_output_off)
{
    static const servo_test_abort_t k_why[] = {
        SERVO_TEST_AB_STOP, SERVO_TEST_AB_DISARMED, SERVO_TEST_AB_LINK,
        SERVO_TEST_AB_LEFT, SERVO_TEST_AB_OPERATOR, SERVO_TEST_AB_SETTINGS,
        SERVO_TEST_AB_SUPPLY_LOST, SERVO_TEST_AB_SUPPLY_OFF,
        SERVO_TEST_AB_TRIPPED, SERVO_TEST_AB_INA_METER,
    };
    for (size_t p = 0; p < N_PHASES; ++p) {
        for (size_t w = 0; w < sizeof(k_why) / sizeof(k_why[0]); ++w) {
            if (!to_phase_at(k_phases[p].ph, k_phases[p].at)) {
                T_FAIL("phase %d at %d not reached", (int)k_phases[p].ph,
                       (int)k_phases[p].at);
                continue;
            }
            const bool wrote = servo_limrun_wrote(&g.run);
            servo_limrun_abort(&g.run, k_why[w], g.now);
            run_for(100u);
            CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
            CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_TEST);
            CHECK_EQ(g.run.test_why, k_why[w]);
            CHECK_EQ(g.offs, 1u);
            CHECK_EQ(g.releases, 1u);
            CHECK_EQ(g.ends, 1u);
            CHECK_EQ(wrote, k_phases[p].wrote);
            CHECK_EQ(servo_limrun_restore(&g.run), wrote);
            CHECK_EQ(strstr(g.report, "RESTORE") != NULL, wrote);
            if (k_phases[p].at == AT_READ || k_phases[p].at == AT_PULSES) {
                CHECK(!g.driven);
            }
            /* Nothing it is fed afterwards moves it. */
            servo_limrun_abort(&g.run, SERVO_TEST_AB_STOP, g.now);
            CHECK_EQ(g.run.test_why, k_why[w]);
        }
    }
}

/* The run's own ends: the meter's silence in every phase; the bench that
 * does not switch; the programmer that does not answer; a search past its
 * cap; the bench dropping out of ARMED during a search. */
TEST_CASE(the_runs_own_timeouts_end_it)
{
    for (size_t p = 0; p < N_PHASES; ++p) {
        CHECK(to_phase_at(k_phases[p].ph, k_phases[p].at));
        g.windows = false;
        run_for(1000u);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_TEST);
        CHECK_EQ(g.run.test_why, SERVO_TEST_AB_WIN_STALE);
        CHECK_EQ(g.offs, 1u);
    }

    /* SAFE not taken: at the read session, and before a later one. */
    rig_init(0u, false);
    g.bench_ms = 0xFFFFFFFFu;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_BENCH);
    CHECK(!g.driven);
    CHECK_EQ(g.connects, 0u);

    CHECK(to_phase(SERVO_LIMRUN_PH_SEARCH));
    run_for(1u);
    g.bench_ms = 0xFFFFFFFFu;
    CHECK(run_to(SERVO_LIMRUN_PH_SAFE, RUN_MS));
    run_for(SERVO_LIMRUN_RETURN_MS + SERVO_LIMRUN_BENCH_MS + 100u);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_BENCH);

    /* ARMED not taken: after the read session, and after a write. */
    for (int k = 0; k < 2; ++k) {
        CHECK(to_phase_at(SERVO_LIMRUN_PH_LEAVE, k == 0 ? AT_READ : AT_LATER));
        g.bench_ms = 0xFFFFFFFFu;
        run_for(RUN_MS);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_BENCH);
        CHECK_EQ(g.run.end_ms - g.run.phase_ms, SERVO_LIMRUN_BENCH_MS);
        CHECK_EQ(g.driven, k != 0);
    }

    for (int k = 0; k < 3; ++k) {
        const servo_limrun_phase_t ph = k == 0 ? SERVO_LIMRUN_PH_CONNECT
                                      : k == 1 ? SERVO_LIMRUN_PH_WRITE
                                               : SERVO_LIMRUN_PH_LEAVE;
        CHECK(to_phase(ph));
        g.prog_silent = true;
        run_for(SERVO_LIMRUN_PROG_MS + 1000u);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_PROG);
        CHECK_EQ(g.run.end_ms - g.run.phase_ms, SERVO_LIMRUN_PROG_MS);
    }

    /* A search past its cap: a servo too slow to arrive inside a window
     * keeps the search on its baseline. */
    rig_init(0u, false);
    rig_pin();
    CHECK(start());
    g.run.search_ms = g.now - SERVO_LIMRUN_SEARCH_MS + 50u;
    run_for(200u);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SEARCH_LONG);
    CHECK_EQ(g.offs, 1u);

    /* Out of ARMED in the middle of a search. */
    CHECK(to_phase(SERVO_LIMRUN_PH_PROBE));
    run_for(500u);
    g.live = false;
    run_for(100u);
    CHECK_EQ(g.run.test_why, SERVO_TEST_AB_DISARMED);
    CHECK_EQ(g.offs, 1u);
}

/* Before every session the servo goes back to the centre from the end the
 * search left it at, and SAFE is asked SERVO_LIMRUN_RETURN_MS later, not
 * sooner: a session never starts with the servo against an end.  A disarm
 * while it returns is the operator's and ends the run.  The read session
 * at the start holds PULSE CENTRE the same way. */
TEST_CASE(a_session_starts_with_the_servo_back_at_the_centre)
{
    for (int k = 0; k < 2; ++k) {
        rig_init(0u, false);
        CHECK(start());
        CHECK(run_to(SERVO_LIMRUN_PH_SEARCH, RUN_MS));
        CHECK(run_to(SERVO_LIMRUN_PH_SAFE, RUN_MS));
        const uint32_t at = g.run.phase_ms;
        /* The horn still out at the end the search reached. */
        CHECK(fabsf(g.servo.position_us - (float)CENTRE) > 200.0f);
        while (g.now - at < 20u) {
            tick();
        }
        CHECK_EQ(g.cmd, CENTRE);
        CHECK(!servo_limrun_switching(&g.run));
        if (k == 1) {
            g.live = false;
            run_for(100u);
            CHECK(!servo_limrun_running(&g.run));
            CHECK_EQ(g.run.test_why, SERVO_TEST_AB_DISARMED);
            CHECK_EQ(g.disarms, 1u);        /* the read session's */
            CHECK_EQ(g.offs, 1u);
            continue;
        }
        const unsigned disarms = g.disarms;
        while (g.now - at < SERVO_LIMRUN_RETURN_MS - 1u) {
            tick();
        }
        CHECK_EQ(g.disarms, disarms);
        while (g.now - at < SERVO_LIMRUN_RETURN_MS + 10u) {
            tick();
        }
        CHECK_EQ(g.disarms, disarms + 1u);
        CHECK(servo_limrun_switching(&g.run));
        CHECK(run_to(SERVO_LIMRUN_PH_CONNECT, 5000u));
    }

    /* The start: PULSE CENTRE commanded, SAFE asked RETURN_MS later. */
    rig_init(0u, false);
    CHECK(start());
    CHECK_EQ(g.run.phase, SERVO_LIMRUN_PH_SAFE);
    tick();
    while (g.now - g.t0 < SERVO_LIMRUN_RETURN_MS - 1u) {
        tick();
    }
    CHECK_EQ(g.cmd, CENTRE);
    CHECK_EQ(g.disarms, 0u);
    while (g.now - g.t0 < SERVO_LIMRUN_RETURN_MS + 10u) {
        tick();
    }
    CHECK_EQ(g.disarms, 1u);
    CHECK(!g.driven);
}

/* --- the protections ----------------------------------------------------- */

static servo_test_win_t win_at(uint32_t t, int16_t mean, int16_t max)
{
    servo_test_win_t w;
    memset(&w, 0, sizeof(w));
    w.current = true;
    w.voltage = true;
    w.mean_ma = mean;
    w.max_ma = max;
    w.min_ma = mean;
    w.mean_mv = 6000u;
    w.min_mv = 6000u;
    w.taken_ms = t;
    return w;
}

TEST_CASE(a_sample_at_3_a_ends_the_run_in_every_phase)
{
    for (size_t p = 0; p < N_PHASES; ++p) {
        const servo_limrun_phase_t ph = k_phases[p].ph;
        const at_t at = k_phases[p].at;
        CHECK(to_phase_at(ph, at));
        servo_test_win_t w = win_at(g.now, 200, 2999);
        servo_limrun_window(&g.run, &w);
        CHECK(servo_limrun_running(&g.run));
        w = win_at(g.now, 200, 3000);
        servo_limrun_window(&g.run, &w);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_HARD);
        run_for(20u);
        CHECK_EQ(g.offs, 1u);

        CHECK(to_phase_at(ph, at));
        w = win_at(g.now, 200, -3000);
        w.min_ma = -3000;
        servo_limrun_window(&g.run, &w);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_HARD);

        /* The reading's most negative value, -32768 mA, in mean, minimum
         * and maximum and not marked clipped: 32768 mA as a magnitude, not
         * a negative value under the ceiling. */
        CHECK(to_phase_at(ph, at));
        w = win_at(g.now, INT16_MIN, INT16_MIN);
        w.min_ma = INT16_MIN;
        servo_limrun_window(&g.run, &w);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_HARD);
        run_for(20u);
        CHECK_EQ(g.offs, 1u);

        CHECK(to_phase_at(ph, at));
        w = win_at(g.now, 200, 300);
        w.clipped = 1u;
        servo_limrun_window(&g.run, &w);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_CLIPPED);

        /* Above 1.0 A for 400 ms, in every phase too. */
        CHECK(to_phase_at(ph, at));
        const uint32_t t = g.now;
        for (uint32_t m = 1; m <= 8u; ++m) {
            w = win_at(t + 50u * m, 1001, 1100);
            servo_limrun_window(&g.run, &w);
        }
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_STALL);
        run_for(20u);
        CHECK_EQ(g.offs, 1u);
    }
}

/* Window means above 1.0 A: 399 ms of them run on, 400 ms end it, timed
 * from the start of the first window. */
TEST_CASE(above_1_a_for_400_ms_ends_the_run)
{
    for (int k = 0; k < 2; ++k) {
        rig_init(k == 0 ? 0u : 0u - 200u, false);
        CHECK(start());
        const uint32_t t = g.now + 50u;
        for (uint32_t m = 0; m < 7u; ++m) {
            servo_test_win_t w = win_at(t + 50u * m, 1001, 1100);
            servo_limrun_window(&g.run, &w);
        }
        /* 350 ms from the first window's start */
        CHECK(servo_limrun_running(&g.run));
        servo_test_win_t w = win_at(t + 349u, 1001, 1100);
        servo_limrun_window(&g.run, &w);
        CHECK(servo_limrun_running(&g.run));
        w = win_at(t + 350u, 1001, 1100);
        servo_limrun_window(&g.run, &w);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_STALL);
        CHECK_EQ(g.run.end_ms, t + 350u);

        /* 1000 mA is not above it; one window under resets the time. */
        rig_init(k == 0 ? 0u : 0u - 200u, false);
        CHECK(start());
        for (uint32_t m = 0; m < 20u; ++m) {
            w = win_at(g.now + 50u * (m + 1u),
                       (m % 7u == 6u) ? 999 : (m < 10u ? 1000 : 1001), 1100);
            servo_limrun_window(&g.run, &w);
        }
        CHECK(servo_limrun_running(&g.run));
    }
}

/* The supply held while a search drives the servo; the programmer's
 * session left to it. */
TEST_CASE(a_changed_supply_ends_a_search_but_not_a_session)
{
    /* The set point moved during the first search. */
    CHECK(to_phase(SERVO_LIMRUN_PH_SEARCH));
    run_for(500u);
    g.sup.set_v = 7.4f;
    run_for(500u);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SUPPLY_CHANGED);
    CHECK_EQ(g.offs, 1u);

    /* The rail 1.0 V low: runs on; 1.001 V low: ends. */
    CHECK(to_phase(SERVO_LIMRUN_PH_SEARCH));
    run_for(500u);
    g.rail_mv = 5000u;
    run_for(500u);
    CHECK(servo_limrun_running(&g.run));
    g.rail_mv = 4999u;
    run_for(500u);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SUPPLY_CHANGED);

    /* Output off, trip, gone: the caller's reasons, also by this path. */
    for (int k = 0; k < 3; ++k) {
        CHECK(to_phase(SERVO_LIMRUN_PH_SEARCH));
        run_for(500u);
        if (k == 0) {
            g.sup.output = false;
        } else if (k == 1) {
            g.sup.trip = 1u;
        } else {
            g.sup.online = false;
        }
        run_for(500u);
        CHECK_EQ(g.run.test_why, k == 0 ? SERVO_TEST_AB_SUPPLY_OFF
                                 : k == 1 ? SERVO_TEST_AB_TRIPPED
                                          : SERVO_TEST_AB_SUPPLY_LOST);
    }

    /* During a programmer's session -- the read session and a later one --
     * the rail dips and the set point is the session's: the run goes on,
     * and is held to the start's values again once a search drives. */
    for (int k = 0; k < 2; ++k) {
        CHECK(to_phase_at(SERVO_LIMRUN_PH_CONNECT, k == 0 ? AT_READ
                                                          : AT_LATER));
        g.rail_mv = 4200u;
        g.sup.set_v = 5.0f;
        run_for(300u);
        CHECK(servo_limrun_running(&g.run));
        g.rail_mv = 6000u;
        g.sup.set_v = 6.0f;
        CHECK(run_to(k == 0 ? SERVO_LIMRUN_PH_SEARCH : SERVO_LIMRUN_PH_PROBE,
                     RUN_MS));
        run_for(2000u);
        CHECK(servo_limrun_running(&g.run));
        g.sup.set_i = 2.0f;
        run_for(500u);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SUPPLY_CHANGED);
    }
}

/* Outside the programmer's sessions the supply stays the run's: in SAFE
 * before a session and while the bench goes back to ARMED, a supply that
 * goes, trips, switches off or changes its set point ends the run with the
 * output off.  Aborted while it waits for ARMED, it asks SAFE, so an ARM
 * not yet taken does not arm the bench after the end. */
TEST_CASE(the_supply_is_watched_in_safe_and_on_the_way_to_armed)
{
    static const struct {
        servo_limrun_phase_t ph;
        at_t at;
    } k_ph[] = {
        { SERVO_LIMRUN_PH_SAFE, AT_READ }, { SERVO_LIMRUN_PH_ARM, AT_READ },
        { SERVO_LIMRUN_PH_SAFE, AT_LATER }, { SERVO_LIMRUN_PH_ARM, AT_LATER },
    };
    for (size_t p = 0; p < 4u; ++p) {
        for (int k = 0; k < 5; ++k) {
            CHECK(to_phase_at(k_ph[p].ph, k_ph[p].at));
            /* The supply's first reading is its reference. */
            while (!g.run.sup_watch) {
                tick();
            }
            CHECK_EQ(g.run.phase, k_ph[p].ph);
            /* The bench takes its time: the phase lasts past a reading. */
            g.bench_ms = 2000u;
            const unsigned disarms = g.disarms;
            if (k == 0) {
                g.sup.output = false;
            } else if (k == 1) {
                g.sup.trip = 1u;
            } else if (k == 2) {
                g.sup.online = false;
            } else if (k == 3) {
                g.sup.set_v = 7.4f;
            } else {
                g.sup.set_i = 2.0f;
            }
            run_for(300u);
            CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
            if (k < 3) {
                CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_TEST);
                CHECK_EQ(g.run.test_why, k == 0 ? SERVO_TEST_AB_SUPPLY_OFF
                                       : k == 1 ? SERVO_TEST_AB_TRIPPED
                                                : SERVO_TEST_AB_SUPPLY_LOST);
            } else {
                CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SUPPLY_CHANGED);
            }
            CHECK_EQ(g.offs, 1u);
            CHECK_EQ(g.releases, 1u);
            const bool later = k_ph[p].at == AT_LATER;
            CHECK_EQ(servo_limrun_wrote(&g.run), later);
            CHECK_EQ(strstr(g.report, "RESTORE") != NULL, later);
            if (k_ph[p].ph == SERVO_LIMRUN_PH_ARM) {
                /* The bench ends SAFE, with the ARM asked before. */
                CHECK_EQ(g.disarms, disarms + 1u);
                idle_for(2100u);
                CHECK(!g.live);
                CHECK(g.safe);
            } else {
                /* Before SAFE is asked, as in a search: no SAFE asked. */
                CHECK_EQ(g.disarms, disarms);
            }
        }
    }
}

/* A supply that answers with its output off -- the rail fed from
 * elsewhere -- is watched as well: the run passes while it stays off, a
 * trip flag on it ends nothing, and its output switched on outside a
 * session ends the run as a changed supply.  Switched on inside a session,
 * the session runs on and the run ends once it is back. */
TEST_CASE(a_supply_off_at_the_start_that_switches_on_ends_the_run)
{
    rig_init(0u, false);
    g.sup.output = false;
    g.sup.trip = 1u;
    CHECK(start());
    run_for(RUN_MS);
    CHECK(g.run.sup_watch);
    CHECK(!g.run.sup_out);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);

    static const struct {
        servo_limrun_phase_t ph;
        at_t at;
    } k_ph[] = {
        { SERVO_LIMRUN_PH_SAFE, AT_READ },   { SERVO_LIMRUN_PH_SEARCH, AT_SEARCH },
        { SERVO_LIMRUN_PH_ARM, AT_LATER },   { SERVO_LIMRUN_PH_PROBE, AT_LATER },
        { SERVO_LIMRUN_PH_CONNECT, AT_READ }, { SERVO_LIMRUN_PH_CONNECT, AT_LATER },
    };
    for (size_t p = 0; p < sizeof(k_ph) / sizeof(k_ph[0]); ++p) {
        rig_init(0u, false);
        g.sup.output = false;
        CHECK(start());
        if (k_ph[p].at == AT_LATER && k_ph[p].ph != SERVO_LIMRUN_PH_PROBE) {
            CHECK(run_to(SERVO_LIMRUN_PH_PROBE, RUN_MS));
        }
        CHECK(run_to(k_ph[p].ph, RUN_MS));
        for (int k = 0; k < 1000 && !g.run.sup_watch; ++k) {
            tick();
        }
        CHECK(g.run.sup_watch);
        CHECK_EQ(g.run.phase, k_ph[p].ph);
        g.bench_ms = 2000u;
        g.sup.output = true;
        if (k_ph[p].ph == SERVO_LIMRUN_PH_CONNECT) {
            /* The session's: it runs on, and the run ends after it. */
            run_for(300u);
            CHECK(servo_limrun_running(&g.run));
        }
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SUPPLY_CHANGED);
        CHECK_EQ(g.offs, 1u);
        CHECK_EQ(g.releases, 1u);
    }
}

/* A supply that does not answer at the start: the run passes while it
 * stays away.  It answers later, its output off: the run ends before the
 * next CONNECT, which would switch it on beside the rail's feed.  It
 * answers later with its output on, in SAFE, a search or on the way to
 * ARMED: the run ends as a changed supply. */
TEST_CASE(a_supply_that_answers_late_ends_the_run)
{
    rig_init(0u, false);
    g.sup.online = false;
    CHECK(start());
    run_for(RUN_MS);
    CHECK(g.run.sup_watch);
    CHECK(!g.run.sup_online);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);

    rig_init(0u, false);
    g.sup.online = false;
    CHECK(start());
    CHECK(run_to(SERVO_LIMRUN_PH_SEARCH, RUN_MS));
    const unsigned c0 = g.connects;
    g.sup.online = true;
    g.sup.output = false;
    for (int k = 0; k < 100000 && servo_limrun_running(&g.run)
                    && g.connects == c0; ++k) {
        tick();
    }
    CHECK(!servo_limrun_running(&g.run));
    CHECK_EQ(g.connects, c0);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SUPPLY_CHANGED);
    CHECK_EQ(g.offs, 1u);

    static const servo_limrun_phase_t k_ph[] = {
        SERVO_LIMRUN_PH_SAFE, SERVO_LIMRUN_PH_SEARCH, SERVO_LIMRUN_PH_ARM,
    };
    for (size_t q = 0; q < 2u * sizeof(k_ph) / sizeof(k_ph[0]); ++q) {
        const size_t p = q / 2u;
        rig_init(0u, false);
        g.sup.online = false;
        CHECK(start());
        CHECK(run_to(k_ph[p], RUN_MS));
        for (int k = 0; k < 1000 && !g.run.sup_watch; ++k) {
            tick();
        }
        CHECK(g.run.sup_watch);
        CHECK_EQ(g.run.phase, k_ph[p]);
        g.bench_ms = 2000u;
        g.sup.online = true;
        g.sup.output = (q % 2u) != 0u;
        run_for(300u);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SUPPLY_CHANGED);
        CHECK_EQ(g.offs, 1u);
    }
}

/* Every search window holds 3.0 V on the rail, the first included: a rail
 * that goes off in the read session ends the run at the first search
 * window.  The search holds the centre for SERVO_LIMRUN_RETURN_MS first;
 * the run is followed past that, so a run that went on would have
 * stepped.  3000 mV searches on and steps. */
TEST_CASE(a_rail_under_3_v_in_any_search_window_ends_the_run)
{
    for (int k = 0; k < 3; ++k) {
        rig_init(0u, false);
        CHECK(start());
        CHECK(run_to(SERVO_LIMRUN_PH_ARM, RUN_MS));
        g.rail_mv = k == 0 ? 0u
                  : k == 1 ? (uint16_t)(SERVO_LIMRUN_RAIL_MIN_MV - 1u)
                           : (uint16_t)SERVO_LIMRUN_RAIL_MIN_MV;
        CHECK(run_to(SERVO_LIMRUN_PH_SEARCH, RUN_MS));
        run_for(SERVO_LIMRUN_RETURN_MS + 1000u);
        if (k < 2) {
            CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
            CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SUPPLY_CHANGED);
            /* At the first window: within one window of the search. */
            CHECK((uint32_t)(g.run.end_ms - g.run.search_ms)
                  <= SERVO_TEST_WIN_MS);
            CHECK(!g.driven);                   /* no step taken */
            CHECK_EQ(g.offs, 1u);
        } else {
            CHECK(servo_limrun_running(&g.run));
            CHECK(g.driven);
            CHECK_EQ(g.run.rail_mv, (uint16_t)SERVO_LIMRUN_RAIL_MIN_MV);
        }
    }
    /* A search already under way: the band of 1.0 V and the floor. */
    rig_init(0u, false);
    g.rail_mv = 3500u;
    CHECK(start());
    CHECK(run_to(SERVO_LIMRUN_PH_SEARCH, RUN_MS));
    run_for(1000u);
    CHECK(servo_limrun_running(&g.run));
    g.rail_mv = 2999u;
    run_for(500u);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SUPPLY_CHANGED);
}

/* The run aborted in the step after its ARM was asked, before the bench
 * took it: SAFE is asked behind it and the bench ends SAFE. */
TEST_CASE(an_abort_behind_the_arm_leaves_the_bench_safe)
{
    static const servo_test_abort_t k_why[] = {
        SERVO_TEST_AB_STOP, SERVO_TEST_AB_LINK, SERVO_TEST_AB_INA_METER,
        SERVO_TEST_AB_LEFT,
    };
    for (int at = 0; at < 2; ++at) {
        for (size_t w = 0; w < sizeof(k_why) / sizeof(k_why[0]); ++w) {
            CHECK(to_phase_at(SERVO_LIMRUN_PH_ARM, at ? AT_LATER : AT_READ));
            CHECK_EQ(g.bench_to, 1);
            const unsigned arms = g.arms;
            servo_limrun_abort(&g.run, k_why[w], g.now);
            servo_limrun_do_t d;
            servo_limrun_in_t in;
            memset(&in, 0, sizeof(in));
            servo_limrun_step(&g.run, g.now, &in, &d);
            CHECK(d.disarm);
            CHECK(!d.arm);
            CHECK(d.off);
            CHECK(d.release);
            take(&d);
            idle_for(500u);
            CHECK_EQ(g.arms, arms);
            CHECK(!g.live);
            CHECK(g.safe);
        }
    }
    /* Aborted in a search, ARMED stays: SAFE is the operator's there. */
    CHECK(to_phase(SERVO_LIMRUN_PH_PROBE));
    const unsigned disarms = g.disarms;
    servo_limrun_abort(&g.run, SERVO_TEST_AB_STOP, g.now);
    run_for(100u);
    CHECK_EQ(g.disarms, disarms);
}

/* --- the meter's silence ------------------------------------------------- */

/* The last window at t: the run goes on at t + 199 ms and ends at t + 200
 * ms (also when the step comes at t + 201), at tick 0 and with the wrap
 * between the window and the step; on the programming port, where the run
 * starts with the servo held at the centre for the read, and on the PWM
 * pin, where it starts with a search. */
TEST_CASE(no_window_for_200_ms_ends_the_run)
{
    static const uint32_t k_after[] = { 199u, 200u, 201u };
    static const uint32_t k_t0[] = { 0u, 0u - 100u, 0u - 200u, 0u - 199u };
    CHECK_EQ(SERVO_LIMRUN_WIN_STALE_MS, 200u);
    for (int pin = 0; pin < 2; ++pin) {
        for (size_t z = 0; z < sizeof(k_t0) / sizeof(k_t0[0]); ++z) {
            for (size_t a = 0; a < 3u; ++a) {
                rig_init(k_t0[z], false);
                if (pin) {
                    rig_pin();
                }
                CHECK(start());
                const uint32_t t = k_t0[z] + 50u;
                servo_test_win_t w = win_at(t, 120, 150);
                servo_limrun_window(&g.run, &w);
                servo_limrun_in_t in = { true, false, false, 0.0f };
                servo_limrun_do_t d;
                servo_limrun_step(&g.run, t + k_after[a], &in, &d);
                if (k_after[a] < 200u) {
                    CHECK(servo_limrun_running(&g.run));
                    CHECK(!d.off);
                } else {
                    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
                    CHECK_EQ(g.run.test_why, SERVO_TEST_AB_WIN_STALE);
                    CHECK(d.off && d.release);
                    CHECK_EQ(g.run.end_ms, t + k_after[a]);
                }
            }
        }
    }
    /* No window at all: 200 ms from the start. */
    rig_init(0u - 150u, false);
    CHECK(start());
    servo_limrun_in_t in = { true, false, false, 0.0f };
    servo_limrun_do_t d;
    servo_limrun_step(&g.run, g.now + 199u, &in, &d);
    CHECK(servo_limrun_running(&g.run));
    servo_limrun_step(&g.run, g.now + 200u, &in, &d);
    CHECK_EQ(g.run.test_why, SERVO_TEST_AB_WIN_STALE);
}

/* --- windows without a quantity ------------------------------------------ */

/* A search's window without the rail voltage ends the run before it moves
 * the search: at the run's first window, in a search, a probe and the
 * check.  A programmer's session reads no rail, so there it changes
 * nothing, and the rail's reference stays the first search window's. */
TEST_CASE(a_window_without_the_rail_voltage_ends_a_search)
{
    /* The run's first window, on the PWM pin: a search from the start. */
    rig_init(0u, false);
    rig_pin();
    g.no_volt = true;
    CHECK(start());
    run_for(1000u);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NO_VOLTAGE);
    CHECK_EQ(g.run.end_ms, g.t0 + SERVO_TEST_WIN_MS);
    CHECK(!g.run.have_rail);
    CHECK_EQ(g.offs, 1u);
    CHECK(!servo_limrun_wrote(&g.run));
    CHECK_EQ(servo_limrun_why_str(&g.run), SERVO_STR_L_WHY_NO_VOLTAGE);
    CHECK(strstr(g.report, "no rail voltage in a window") != NULL);

    /* On the programming port the first search's first window. */
    rig_init(0u, false);
    g.no_volt = true;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NO_VOLTAGE);
    CHECK(g.run.searched);
    CHECK_EQ(g.connects, 1u);

    /* SEARCH, PROBE and CHECK: the window ends the run and the search does
     * not take it.  After a write the values stay and RESTORE is offered. */
    static const servo_limrun_phase_t k_search[] = {
        SERVO_LIMRUN_PH_SEARCH, SERVO_LIMRUN_PH_PROBE, SERVO_LIMRUN_PH_CHECK,
    };
    for (size_t p = 0; p < 3u; ++p) {
        CHECK(to_phase(k_search[p]));
        run_for(2000u);
        CHECK(servo_limrun_running(&g.run));
        const uint16_t cmd = g.run.cmd_us;
        const uint16_t lf = g.run.lf.cmd_us;
        const uint32_t n = g.run.windows;
        servo_test_win_t w = win_at(g.now + 1u, 200, 250);
        w.voltage = false;
        servo_limrun_window(&g.run, &w);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NO_VOLTAGE);
        CHECK_EQ(g.run.end_ms, g.now + 1u);
        CHECK_EQ(g.run.cmd_us, cmd);
        CHECK_EQ(g.run.lf.cmd_us, lf);
        CHECK_EQ(g.run.windows, n + 1u);
        CHECK_EQ(servo_limrun_wrote(&g.run),
                 k_search[p] != SERVO_LIMRUN_PH_SEARCH);
        run_for(100u);
        CHECK_EQ(g.offs, 1u);
    }

    /* The programmer's phases: no rail read, the run goes on. */
    for (size_t p = 0; p < N_PHASES; ++p) {
        const servo_limrun_phase_t ph = k_phases[p].ph;
        if (ph == SERVO_LIMRUN_PH_SEARCH || ph == SERVO_LIMRUN_PH_PROBE
            || ph == SERVO_LIMRUN_PH_CHECK) {
            continue;
        }
        CHECK(to_phase_at(ph, k_phases[p].at));
        servo_test_win_t w = win_at(g.now + 1u, 20, 30);
        w.voltage = false;
        servo_limrun_window(&g.run, &w);
        CHECK(servo_limrun_running(&g.run));
        CHECK_EQ(g.run.last_win_ms, g.now + 1u);
    }

    /* The rail gone through a session and back for the next search: the
     * run goes on, held to the first search window's rail. */
    CHECK(to_phase(SERVO_LIMRUN_PH_CONNECT));
    g.no_volt = true;
    run_for(300u);
    CHECK(servo_limrun_running(&g.run));
    g.no_volt = false;
    CHECK(run_to(SERVO_LIMRUN_PH_PROBE, RUN_MS));
    run_for(2000u);
    CHECK(servo_limrun_running(&g.run));
    CHECK(g.run.have_rail);
    CHECK_EQ(g.run.rail_mv, 6000u);
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
}

/* The boundaries: after a window at t, one without the rail at t + 199 and
 * at t + 200 (before the step that would end the run for silence) ends it
 * for the rail; and the run's last window before it ends. */
TEST_CASE(a_window_without_the_rail_voltage_at_the_edges)
{
    static const uint32_t k_after[] = { 199u, 200u };
    static const uint32_t k_t0[] = { 0u, 0u - 120u };
    for (size_t z = 0; z < 2u; ++z) {
        for (size_t a = 0; a < 2u; ++a) {
            rig_init(k_t0[z], false);
            rig_pin();
            CHECK(start());
            const uint32_t t = k_t0[z] + 50u;
            servo_test_win_t w = win_at(t, 120, 150);
            servo_limrun_window(&g.run, &w);
            w = win_at(t + k_after[a], 120, 150);
            w.voltage = false;
            servo_limrun_window(&g.run, &w);
            servo_limrun_in_t in = { true, false, false, 0.0f };
            servo_limrun_do_t d;
            servo_limrun_step(&g.run, t + k_after[a], &in, &d);
            CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
            CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NO_VOLTAGE);
            CHECK_EQ(g.run.end_ms, t + k_after[a]);
        }
    }

    /* The window the run would end on, without the rail. */
    rig_init(0u, false);
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
    const uint32_t last = g.run.last_win_ms;
    rig_init(0u, false);
    CHECK(start());
    while (g.now != last - 1u && servo_limrun_running(&g.run)) {
        tick();
    }
    CHECK(servo_limrun_running(&g.run));
    g.no_volt = true;
    run_for(100u);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NO_VOLTAGE);
    CHECK_EQ(g.run.end_ms, last);
    CHECK(servo_limrun_wrote(&g.run));
}

/* A window without a current reading is no window: the protections cannot
 * read it, so the meter's silence runs on through it and ends the run 200
 * ms after the last window with a current, also with one at the edge. */
TEST_CASE(a_window_without_a_current_is_not_counted)
{
    rig_init(0u, false);
    CHECK(start());
    run_for(1000u);
    CHECK(servo_limrun_running(&g.run));
    const uint32_t last = g.run.last_win_ms;
    const uint32_t n = g.run.windows;
    g.no_cur = true;
    run_for(1000u);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.test_why, SERVO_TEST_AB_WIN_STALE);
    CHECK_EQ(g.run.end_ms, last + SERVO_LIMRUN_WIN_STALE_MS);
    CHECK_EQ(g.run.windows, n);
    CHECK_EQ(g.offs, 1u);

    rig_init(0u - 120u, false);
    CHECK(start());
    const uint32_t t = g.now + 50u;
    servo_test_win_t w = win_at(t, 120, 150);
    servo_limrun_window(&g.run, &w);
    servo_limrun_in_t in = { true, false, false, 0.0f };
    servo_limrun_do_t d;
    servo_limrun_step(&g.run, t + 199u, &in, &d);
    CHECK(servo_limrun_running(&g.run));
    w = win_at(t + 200u, 2999, 2999);
    w.current = false;
    servo_limrun_window(&g.run, &w);
    CHECK(servo_limrun_running(&g.run));
    servo_limrun_step(&g.run, t + 200u, &in, &d);
    CHECK_EQ(g.run.test_why, SERVO_TEST_AB_WIN_STALE);
    CHECK_EQ(g.run.end_ms, t + 200u);
}

/* --- the clock ----------------------------------------------------------- */

/* The same run from tick 0 and from 60 s before the wrap: the same values,
 * the same writes, the same duration; with SERVO PULSES 1000-2000 as well. */
TEST_CASE(a_run_across_the_tick_wrap_ends_as_at_tick_0)
{
    for (int set = 0; set < 2; ++set) {
        rig_init(0u, false);
        g.cfg.set_pulses = set != 0;
        CHECK(start());
        run_for(RUN_MS);
        const servo_limrun_t a = g.run;
        const uint16_t left = reg_get(KST_F_LEFT_ANGLE);
        const uint16_t right = reg_get(KST_F_RIGHT_ANGLE);

        rig_init(0u - 60000u, false);
        g.cfg.set_pulses = set != 0;
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
        CHECK_EQ(g.run.verdict, a.verdict);
        CHECK_EQ(g.run.writes, a.writes);
        CHECK_EQ(g.run.sessions, a.sessions);
        CHECK_EQ(reg_get(KST_F_LEFT_ANGLE), left);
        CHECK_EQ(reg_get(KST_F_RIGHT_ANGLE), right);
        CHECK_EQ(g.run.end_ms - g.run.start_ms, a.end_ms - a.start_ms);
        CHECK((int32_t)g.run.end_ms > 0);   /* it crossed the wrap */
    }
}

int main(void)
{
    RUN(every_caller_abort_ends_every_phase_with_the_output_off);
    RUN(the_runs_own_timeouts_end_it);
    RUN(a_session_starts_with_the_servo_back_at_the_centre);
    RUN(a_sample_at_3_a_ends_the_run_in_every_phase);
    RUN(above_1_a_for_400_ms_ends_the_run);
    RUN(a_changed_supply_ends_a_search_but_not_a_session);
    RUN(the_supply_is_watched_in_safe_and_on_the_way_to_armed);
    RUN(a_supply_off_at_the_start_that_switches_on_ends_the_run);
    RUN(a_supply_that_answers_late_ends_the_run);
    RUN(a_rail_under_3_v_in_any_search_window_ends_the_run);
    RUN(an_abort_behind_the_arm_leaves_the_bench_safe);
    RUN(no_window_for_200_ms_ends_the_run);
    RUN(a_window_without_the_rail_voltage_ends_a_search);
    RUN(a_window_without_the_rail_voltage_at_the_edges);
    RUN(a_window_without_a_current_is_not_counted);
    RUN(a_run_across_the_tick_wrap_ends_as_at_tick_0);
    return test_summary("servo_limrun_abort");
}
