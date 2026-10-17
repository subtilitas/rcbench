/*
 * The limit run's report and words, against the modelled servo.
 *
 * Under test: the report, line by line; a start refused and the calls
 * that take NULL; the words of every reason and phase; the share, the
 * overset and the final value as arithmetic.
 *
 * The rig is servo_limrun_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_limrun_rig.h"

/* --- the report ---------------------------------------------------------- */

TEST_CASE(the_report_holds_the_run)
{
    rig_init(0u, false);
    g.enc = true;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
    char want[128];
    CHECK(strstr(g.report, "RCBENCH SERVO LIMIT REPORT\n") == g.report);
    CHECK(strstr(g.report, "PASS") != NULL);
    CHECK(strstr(g.report, "aileron left") != NULL);
    CHECK(strstr(g.report, "PROG PORT GP13") != NULL);
    CHECK(strstr(g.report, "INA3221") != NULL);
    CHECK(strstr(g.report, "1.8") != NULL);
    CHECK(strstr(g.report, "3.0 A") != NULL);
    CHECK(strstr(g.report, "Pulse range:    centre 1500 us, 800-2200 us")
          != NULL);
    CHECK(strstr(g.report, "Searched:       from 1500 us out to 900 us and "
                           "2100 us") != NULL);
    CHECK(strstr(g.report, "Servo pulses:   NEUTRAL 1500.2 us, PULSE LOWER "
                           "900.0 us, PULSE UPPER 2100.0 us") != NULL);
    CHECK(strstr(g.report, "Pulse write") == NULL);
    snprintf(want, sizeof(want), "LEFT ANGLE:     150 before, %u after",
             (unsigned)reg_get(KST_F_LEFT_ANGLE));
    CHECK(strstr(g.report, want) != NULL);
    snprintf(want, sizeof(want), "RIGHT ANGLE:    150 before, %u after",
             (unsigned)reg_get(KST_F_RIGHT_ANGLE));
    CHECK(strstr(g.report, want) != NULL);
    snprintf(want, sizeof(want), "overset %u, share", g.run.v1[0]);
    CHECK(strstr(g.report, want) != NULL);
    snprintf(want, sizeof(want), "final %u", g.run.v2[1]);
    CHECK(strstr(g.report, want) != NULL);
    CHECK(strstr(g.report, "overset +3 %") != NULL);
    snprintf(want, sizeof(want), "values written %u, writes sent %u",
             g.run.writes, g.run.write_ops);
    CHECK(strstr(g.report, want) != NULL);
    CHECK(strstr(g.report, "KST00001.KST") != NULL);
    CHECK(strstr(g.report, "AS5600") != NULL || strstr(g.report, "deg") != NULL);
    CHECK(strstr(g.report, "unit") != NULL);
    CHECK(strstr(g.report, "linear") != NULL);
    CHECK(strstr(g.report, "bisect") == NULL);
    CHECK(strstr(g.report, "RESTORE") == NULL);
    /* Every line fits the line buffer and the report ends once. */
    CHECK_EQ(g.ends, 1u);
    CHECK(g.lines > 20u);
    for (unsigned k = 0; k < 200u; ++k) {
        char buf[SERVO_TEST_LINE_MAX];
        if (!servo_limrun_report_line(&g.run, k, buf, sizeof(buf))) {
            CHECK_EQ(k, g.lines);
            break;
        }
        CHECK(strlen(buf) < sizeof(buf) - 1u);
    }

    /* Search only on the PWM pin: no programming lines. */
    rig_init(0u, false);
    rig_pin();
    CHECK(start());
    run_for(RUN_MS);
    CHECK(strstr(g.report, "search only") != NULL);
    CHECK(strstr(g.report, "Not run: search only.") != NULL);
    CHECK(strstr(g.report, "LEFT ANGLE:") == NULL);
}

TEST_CASE(a_start_with_its_centre_outside_min_and_max_is_refused)
{
    rig_init(0u, false);
    g.cfg.centre_us = MAX_US;
    CHECK(!start());
    CHECK(!servo_limrun_running(&g.run));
    g.cfg.centre_us = CENTRE;
    CHECK(start());
    CHECK(!start());                    /* one at a time */
    servo_limrun_step(NULL, 0u, NULL, NULL);
    servo_limrun_window(NULL, NULL);
    CHECK(!servo_limrun_running(NULL));
    CHECK(servo_limrun_drained(NULL));
}

/* Every reason and every phase has words of its own, and a reason fits the
 * box's line of 31 characters.  No servo read: no values to give. */
TEST_CASE(every_reason_and_phase_has_its_words)
{
    servo_limrun_t r;
    servo_limrun_init(&r);
    CHECK(!servo_limrun_original(&r, NULL, NULL));
    CHECK(!servo_limrun_original(NULL, NULL, NULL));
    CHECK(!servo_limrun_restore(NULL));
    CHECK_EQ(servo_limrun_why_str(NULL), SERVO_STR_AB_NONE);
    servo_str_t seen[SERVO_LIMRUN_WHY_COUNT];
    for (int w = SERVO_LIMRUN_WHY_HARD; w < SERVO_LIMRUN_WHY_COUNT; ++w) {
        r.why = (servo_limrun_why_t)w;
        const servo_str_t id = servo_limrun_why_str(&r);
        seen[w] = id;
        CHECK(id != SERVO_STR_AB_NONE);
        CHECK(strlen(servo_str(id)) > 0u);
        CHECK(strlen(servo_str(id)) <= 31u);
        for (int k = SERVO_LIMRUN_WHY_HARD; k < w; ++k) {
            CHECK(seen[k] != id);
        }
    }
    r.why = SERVO_LIMRUN_WHY_TEST;
    r.test_why = SERVO_TEST_AB_STOP;
    CHECK_EQ(servo_limrun_why_str(&r), servo_test_abort_str(SERVO_TEST_AB_STOP));
    r.why = SERVO_LIMRUN_WHY_COUNT;
    CHECK_EQ(servo_limrun_why_str(&r), SERVO_STR_AB_NONE);
    for (int p = SERVO_LIMRUN_PH_SEARCH; p <= SERVO_LIMRUN_PH_CHECK; ++p) {
        const servo_str_t id = servo_limrun_phase_str((servo_limrun_phase_t)p);
        CHECK(id != SERVO_STR_PHASE_NONE);
        CHECK(strlen(servo_str(id)) > 0u);
        for (int k = SERVO_LIMRUN_PH_SEARCH; k < p; ++k) {
            CHECK(servo_limrun_phase_str((servo_limrun_phase_t)k) != id);
        }
    }
    CHECK_EQ(servo_limrun_phase_str(SERVO_LIMRUN_PH_NONE),
             SERVO_STR_PHASE_NONE);
    CHECK(servo_limrun_verdict_str(SERVO_LIMRUN_FOUND)
          != servo_limrun_verdict_str(SERVO_LIMRUN_FREE));
}

/* The arithmetic: the share from NEUTRAL, the overset rounded up and the
 * final value down, both within the smallest value and v0. */
TEST_CASE(the_share_the_overset_and_the_final_value)
{
    servo_limrun_servo_t sv;
    memset(&sv, 0, sizeof(sv));
    sv.pulse_dus[SERVO_LIMRUN_P_NEUTRAL] = 15002u;
    sv.pulse_dus[SERVO_LIMRUN_P_LOWER]   = 9000u;
    sv.pulse_dus[SERVO_LIMRUN_P_UPPER]   = 21000u;
    CHECK_EQ(servo_limrun_span(&sv, SERVO_LIMRUN_LO), 6002);
    CHECK_EQ(servo_limrun_span(&sv, SERVO_LIMRUN_HI), 5998);
    CHECK_EQ(servo_limrun_dist(&sv, SERVO_LIMRUN_LO, 1000u), 5002);
    CHECK_EQ(servo_limrun_dist(&sv, SERVO_LIMRUN_HI, 2000u), 4998);
    CHECK_EQ(servo_limrun_dist(&sv, SERVO_LIMRUN_LO, 1500u), 2);
    CHECK_EQ(servo_limrun_dist(&sv, SERVO_LIMRUN_HI, 1500u), -2);

    CHECK_EQ(servo_limrun_overset(150u, 5002, 6002, 1u), 129u);
    CHECK_EQ(servo_limrun_scaled(129u, 5802, 6002, 1u), 124u);
    /* Exact products: 100 x 0.5 x 1.03 = 51.5 up to 52; 100 x 0.5 = 50. */
    CHECK_EQ(servo_limrun_overset(100u, 3000, 6000, 1u), 52u);
    CHECK_EQ(servo_limrun_scaled(100u, 3000, 6000, 1u), 50u);
    /* At most v0, at least the smallest value. */
    CHECK_EQ(servo_limrun_overset(150u, 6000, 6000, 1u), 150u);
    CHECK_EQ(servo_limrun_overset(150u, 100, 6000, 40u), 40u);
    CHECK_EQ(servo_limrun_scaled(150u, 100, 6000, 40u), 40u);
    CHECK_EQ(servo_limrun_scaled(150u, 7000, 6000, 1u), 150u);
    /* No share: the smallest value, never above v0. */
    CHECK_EQ(servo_limrun_overset(150u, 0, 6000, 1u), 1u);
    CHECK_EQ(servo_limrun_overset(150u, 3000, 0, 1u), 1u);
    CHECK_EQ(servo_limrun_scaled(150u, -5, 6000, 1u), 1u);
    CHECK_EQ(servo_limrun_scaled(20u, -5, 6000, 40u), 20u);
    CHECK_EQ(servo_limrun_overset(255u, 6000, 6000, 1u), 255u);
}

int main(void)
{
    RUN(the_report_holds_the_run);
    RUN(a_start_with_its_centre_outside_min_and_max_is_refused);
    RUN(every_reason_and_phase_has_its_words);
    RUN(the_share_the_overset_and_the_final_value);
    return test_summary("servo_limrun_report");
}
