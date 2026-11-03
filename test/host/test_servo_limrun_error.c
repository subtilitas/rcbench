/*
 * The limit run's errors switch an ARMED bench to SAFE.
 *
 * Under test: a run ended on an error of the bench -- a sample at the 3.0A
 * ceiling, a clipped sample, 1.0A for 400ms, a window without the rail
 * voltage, the supply lost or tripped, the meter gone -- while it searches,
 * probes or checks asks SAFE once, beside the output off and the release;
 * the operator's ends and STOP do not, nor does an error while the
 * programmer holds the port, where the bench is SAFE already.  Every
 * reason is judged by servo_limrun_why_error().
 *
 * The rig is servo_limrun_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_limrun_rig.h"

/* A window taken at @p t with mean and maximum current in mA, 6.000V. */
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

/* The run at PROBE, after its first write: a search on an ARMED bench. */
static bool at_probe(void)
{
    rig_init(0u, false);
    return start() && run_to(SERVO_LIMRUN_PH_PROBE, RUN_MS);
}

/* The run at its first search, nothing written. */
static bool at_search(void)
{
    rig_init(0u, false);
    return start() && run_to(SERVO_LIMRUN_PH_SEARCH, RUN_MS)
           && !servo_limrun_wrote(&g.run);
}

/* A caller's reason in a search: SAFE asked for an error only. */
TEST_CASE(an_error_in_a_search_asks_safe_and_the_operators_end_does_not)
{
    static const struct {
        servo_test_abort_t why;
        bool error;
    } k[] = {
        { SERVO_TEST_AB_STOP,        false },
        { SERVO_TEST_AB_DISARMED,    false },
        { SERVO_TEST_AB_OPERATOR,    false },
        { SERVO_TEST_AB_SETTINGS,    false },
        { SERVO_TEST_AB_SUPPLY_OFF,  false },
        { SERVO_TEST_AB_TOUCH,       false },
        { SERVO_TEST_AB_SUPPLY_LOST, true  },
        { SERVO_TEST_AB_TRIPPED,     true  },
        { SERVO_TEST_AB_INA_METER,   true  },
        { SERVO_TEST_AB_WIN_STALE,   true  },
    };
    for (int first = 0; first < 2; ++first) {
        for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
            CHECK(first ? at_search() : at_probe());
            const unsigned disarms = g.disarms;
            servo_limrun_abort(&g.run, k[i].why, g.now);
            servo_limrun_do_t d;
            servo_limrun_in_t in;
            memset(&in, 0, sizeof(in));
            servo_limrun_step(&g.run, g.now, &in, &d);
            CHECK_EQ(d.disarm, k[i].error);
            CHECK(d.off);
            CHECK(d.release);
            take(&d);
            run_for(100u);
            CHECK_EQ(g.disarms, disarms + (k[i].error ? 1u : 0u));
            CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
        }
    }
}

/* The run's own error ends in a search: each asks SAFE once. */
TEST_CASE(the_runs_own_errors_in_a_search_ask_safe)
{
    for (int kind = 0; kind < 4; ++kind) {
        CHECK(at_probe());
        const unsigned disarms = g.disarms;
        servo_test_win_t w;
        if (kind == 0) {                        /* the ceiling, at 3000mA */
            w = win_at(g.now, 200, 3000);
            servo_limrun_window(&g.run, &w);
            CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_HARD);
        } else if (kind == 1) {                 /* a clipped sample */
            w = win_at(g.now, 200, 300);
            w.clipped = 1u;
            servo_limrun_window(&g.run, &w);
            CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_CLIPPED);
        } else if (kind == 2) {                 /* above 1.0A for 400ms */
            const uint32_t t = g.now;
            for (uint32_t m = 1; m <= 8u; ++m) {
                w = win_at(t + 50u * m, 1001, 1100);
                servo_limrun_window(&g.run, &w);
            }
            CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_STALL);
        } else {                                /* the meter silent */
            servo_limrun_in_t in = { true, false, false, 0.0f };
            servo_limrun_do_t d;
            servo_limrun_step(&g.run, g.now + 250u, &in, &d);
            CHECK_EQ(g.run.test_why, SERVO_TEST_AB_WIN_STALE);
            CHECK(d.disarm);
            take(&d);
        }
        run_for(100u);
        CHECK_EQ(g.disarms, disarms + 1u);
        CHECK_EQ(g.offs, 1u);
        idle_for(500u);
        CHECK(!g.live);
    }
}

/* At 2999mA the run goes on and asks nothing: the ceiling's edge. */
TEST_CASE(under_the_ceiling_nothing_is_asked)
{
    CHECK(at_probe());
    const unsigned disarms = g.disarms;
    const servo_test_win_t w = win_at(g.now, 200, 2999);
    servo_limrun_window(&g.run, &w);
    CHECK(servo_limrun_running(&g.run));
    servo_limrun_do_t d;
    servo_limrun_in_t in;
    memset(&in, 0, sizeof(in));
    servo_limrun_step(&g.run, g.now, &in, &d);
    CHECK(!d.disarm);
    CHECK_EQ(g.disarms, disarms);
}

/* An error while the programmer holds the port, in CONNECT or WRITE: the
 * bench is SAFE there, and the run asks no second SAFE. */
TEST_CASE(an_error_in_a_session_asks_no_safe)
{
    static const servo_limrun_phase_t k_ph[] = {
        SERVO_LIMRUN_PH_CONNECT, SERVO_LIMRUN_PH_WRITE,
    };
    for (size_t p = 0; p < sizeof(k_ph) / sizeof(k_ph[0]); ++p) {
        rig_init(0u, false);
        CHECK(start());
        CHECK(run_to(SERVO_LIMRUN_PH_PROBE, RUN_MS));
        CHECK(run_to(k_ph[p], RUN_MS));
        servo_limrun_abort(&g.run, SERVO_TEST_AB_TRIPPED, g.now);
        servo_limrun_do_t d;
        servo_limrun_in_t in;
        memset(&in, 0, sizeof(in));
        servo_limrun_step(&g.run, g.now, &in, &d);
        CHECK(!d.disarm);
        CHECK(d.off);
    }
}

/* The rule, reason by reason. */
TEST_CASE(which_reasons_are_errors)
{
    CHECK(servo_limrun_why_error(SERVO_LIMRUN_WHY_HARD, SERVO_TEST_AB_NONE));
    CHECK(servo_limrun_why_error(SERVO_LIMRUN_WHY_CLIPPED,
                                 SERVO_TEST_AB_NONE));
    CHECK(servo_limrun_why_error(SERVO_LIMRUN_WHY_STALL, SERVO_TEST_AB_NONE));
    CHECK(servo_limrun_why_error(SERVO_LIMRUN_WHY_NO_VOLTAGE,
                                 SERVO_TEST_AB_NONE));
    CHECK(servo_limrun_why_error(SERVO_LIMRUN_WHY_TEST,
                                 SERVO_TEST_AB_INA_SILENT));
    CHECK(!servo_limrun_why_error(SERVO_LIMRUN_WHY_TEST, SERVO_TEST_AB_STOP));
    CHECK(!servo_limrun_why_error(SERVO_LIMRUN_WHY_PROG, SERVO_TEST_AB_NONE));
    CHECK(!servo_limrun_why_error(SERVO_LIMRUN_WHY_WRITE,
                                  SERVO_TEST_AB_NONE));
    CHECK(!servo_limrun_why_error(SERVO_LIMRUN_WHY_SEARCH_LONG,
                                  SERVO_TEST_AB_NONE));
    CHECK(!servo_limrun_why_error(SERVO_LIMRUN_WHY_COUNT,
                                  SERVO_TEST_AB_NONE));
    for (int w = 0; w <= (int)SERVO_TEST_AB_COUNT; ++w) {
        const servo_test_abort_t why = (servo_test_abort_t)w;
        const bool error = why == SERVO_TEST_AB_SUPPLY_LOST
            || why == SERVO_TEST_AB_TRIPPED || why == SERVO_TEST_AB_STALE
            || why == SERVO_TEST_AB_NOT_ON || why == SERVO_TEST_AB_SET_NOT_TAKEN
            || why == SERVO_TEST_AB_CAP || why == SERVO_TEST_AB_STALL
            || why == SERVO_TEST_AB_CC || why == SERVO_TEST_AB_WIN_STALE
            || why == SERVO_TEST_AB_INA_RESET || why == SERVO_TEST_AB_INA_SILENT
            || why == SERVO_TEST_AB_INA_NO_WINDOW
            || why == SERVO_TEST_AB_INA_SETUP || why == SERVO_TEST_AB_INA_METER;
        CHECK_EQ(servo_test_abort_error(why), error);
    }
}

int main(void)
{
    RUN(an_error_in_a_search_asks_safe_and_the_operators_end_does_not);
    RUN(the_runs_own_errors_in_a_search_ask_safe);
    RUN(under_the_ceiling_nothing_is_asked);
    RUN(an_error_in_a_session_asks_no_safe);
    RUN(which_reasons_are_errors);
    return test_summary("servo_limrun_error");
}
