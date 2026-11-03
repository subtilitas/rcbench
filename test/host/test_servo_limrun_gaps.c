/*
 * The limit run riding through the output encoder's gaps (servo_gap.h).
 *
 * Under test: with AS5600 in use, a gap of 300ms rides and one of 301ms
 * ends the run as an error, SAFE asked while it searches, both when the
 * part answers again and against the clock; a second gap 9999ms after the
 * start of the one before ends it and one 10000ms after rides; a gap in the
 * programmer's session ends it with no SAFE asked, the bench SAFE already;
 * the report counts the gaps; the INA3221 silent while an encoder gap is
 * open ends the run at once as the meter's silence, not the encoder's; a
 * run without the encoder takes none of it.
 *
 * The rig is servo_limrun_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_limrun_rig.h"

/* The part stops answering at @p at. */
static void lost(uint32_t at)
{
    const servo_test_enc_t none = { .valid = false, .taken_ms = at };
    servo_limrun_encoder(&g.run, &none);
}

/* It answers again at @p at. */
static void back(uint32_t at)
{
    const servo_test_enc_t e = { .valid = true, .raw = 3000u,
                                 .taken_ms = at };
    servo_limrun_encoder(&g.run, &e);
}

/* A run on the PWM pin with the encoder in use, at its first search. */
static bool at_search(void)
{
    rig_init(0u, false);
    rig_pin();
    g.enc = true;
    return start() && run_to(SERVO_LIMRUN_PH_SEARCH, RUN_MS);
}

TEST_CASE(the_limit_run_rides_through_an_encoder_gap_of_300ms)
{
    CHECK(at_search());
    idle_for(1500u);
    const uint32_t t = g.now;
    lost(t);
    idle_for(300u);
    CHECK(servo_limrun_running(&g.run));
    back(t + 300u);
    CHECK(servo_limrun_running(&g.run));
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FOUND);
    CHECK_EQ(g.run.enc_gap.count, 1u);
    CHECK_EQ(g.run.enc_gap.longest_ms, 300u);
    CHECK(strstr(g.report, "AS5600 gaps: 1, the longest 300ms.") != NULL);
}

TEST_CASE(an_encoder_gap_of_301ms_ends_the_limit_run_as_an_error)
{
    for (int by_clock = 0; by_clock < 2; ++by_clock) {
        CHECK(at_search());
        idle_for(1500u);
        const unsigned disarms = g.disarms;
        const uint32_t t = g.now;
        lost(t);
        idle_for(300u);
        CHECK(servo_limrun_running(&g.run));
        if (by_clock) {
            idle_for(10u);              /* the next step, 310ms in */
        } else {
            back(t + 301u);
            idle_for(10u);
        }
        CHECK(!servo_limrun_running(&g.run));
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_TEST);
        CHECK_EQ(g.run.test_why, SERVO_TEST_AB_ENC_SILENT);
        CHECK(servo_limrun_why_error(g.run.why, g.run.test_why));
        CHECK_EQ(g.disarms, disarms + 1u);
        CHECK(g.off);
        CHECK(g.released);
        CHECK_EQ(g.run.end_ms - t, by_clock ? 310u : 301u);
        run_for(100u);
        CHECK(strstr(g.report, "AS5600 not answering") != NULL);
    }
}

TEST_CASE(a_second_encoder_gap_within_10s_ends_the_limit_run)
{
    for (uint32_t apart = 9999u; apart <= 10000u; ++apart) {
        CHECK(at_search());
        idle_for(500u);
        const uint32_t t = g.now;
        lost(t);
        idle_for(50u);
        back(t + 50u);
        idle_for(apart - 60u);
        CHECK(servo_limrun_running(&g.run));
        lost(t + apart);
        if (apart < SERVO_GAP_EVERY_MS) {
            CHECK(!servo_limrun_running(&g.run));
            CHECK_EQ(g.run.test_why, SERVO_TEST_AB_ENC_GAPS);
        } else {
            CHECK(servo_limrun_running(&g.run));
            back(t + apart + 10u);
            idle_for(500u);
            CHECK(servo_limrun_running(&g.run));
        }
    }
}

/* In the programmer's session the bench is SAFE already: the run ends, no
 * SAFE is asked, the output goes off. */
TEST_CASE(an_encoder_gap_in_a_session_ends_the_run_without_asking_safe)
{
    rig_init(0u, false);
    g.enc = true;
    g.prog_ms = 5000u;
    CHECK(start());
    CHECK(run_to(SERVO_LIMRUN_PH_CONNECT, RUN_MS));
    const unsigned disarms = g.disarms;
    const uint32_t t = g.now;
    lost(t);
    idle_for(320u);
    CHECK(!servo_limrun_running(&g.run));
    CHECK_EQ(g.run.test_why, SERVO_TEST_AB_ENC_SILENT);
    CHECK_EQ(g.disarms, disarms);
    CHECK(g.off);
}

/* The INA3221 feeds the run's current protections: its silence ends the
 * run at once, whatever gap of the encoder is open, and names the meter. */
TEST_CASE(the_meter_silent_in_an_encoder_gap_ends_the_run_at_once)
{
    CHECK(at_search());
    idle_for(1500u);
    lost(g.now);
    g.windows = false;
    const uint32_t last = g.run.last_win_ms;
    idle_for(400u);
    CHECK(!servo_limrun_running(&g.run));
    CHECK_EQ(g.run.test_why, SERVO_TEST_AB_WIN_STALE);
    CHECK(g.run.end_ms - last <= SERVO_LIMRUN_WIN_STALE_MS + 10u);
}

TEST_CASE(a_limit_run_without_the_encoder_has_no_gaps)
{
    rig_init(0u, false);
    rig_pin();
    g.enc = false;
    CHECK(start() && run_to(SERVO_LIMRUN_PH_SEARCH, RUN_MS));
    lost(g.now);
    idle_for(2000u);
    CHECK(servo_limrun_running(&g.run));
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FOUND);
    CHECK_EQ(g.run.enc_gap.count, 0u);
    CHECK(strstr(g.report, "AS5600 gaps") == NULL);
    servo_limrun_encoder(NULL, NULL);
    servo_limrun_encoder(&g.run, NULL);
}

int main(void)
{
    RUN(the_limit_run_rides_through_an_encoder_gap_of_300ms);
    RUN(an_encoder_gap_of_301ms_ends_the_limit_run_as_an_error);
    RUN(a_second_encoder_gap_within_10s_ends_the_limit_run);
    RUN(an_encoder_gap_in_a_session_ends_the_run_without_asking_safe);
    RUN(the_meter_silent_in_an_encoder_gap_ends_the_run_at_once);
    RUN(a_limit_run_without_the_encoder_has_no_gaps);
    return test_summary("servo_limrun_gaps");
}
