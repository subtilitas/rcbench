/*
 * AUTO end points (servo_rudder_auto.h): the margin in the pulse length.
 *
 * Under test: the margin's pulse range from the servo's NEUTRAL N, each
 * side extended by the margin's % of its span from N, and PULSE LOWER and
 * PULSE UPPER on the KST's 0.8us step rounded toward the center, at N
 * 1500.0us, 1360.0us and 1640.0us and 0%, 10% and 50%; a NEUTRAL outside
 * 1360.0us to 1640.0us refused at the read; the pulse write's read back
 * checked against the read's NEUTRAL; the run writes them, drives 1000us and
 * 2000us only and holds both ends and the verify pass to the target; the
 * bench's PULSE MIN and PULSE MAX widen where narrower than the margin's
 * range, never narrow, and the report names it; a servo that holds the
 * margin's pulses already needs no pulse write and still widens; an
 * abort inside the pulse write and a pulse write read back otherwise
 * widen nothing; a Hitec family refuses a margin above 0 and writes no
 * pulses.
 *
 * The rig is servo_rauto_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_rauto_rig.h"

/* The run to its end, the outbox drained into the report. */
static void run_out(void)
{
    run_for(RAUTO_MS);
    drain();
}

/* LOWER 1000us - (N - 1000us) x p, UPPER 2000us + (2000us - N) x p: at N
 * 1500.0us 950 to 2050us at 10%, at N 1640.0us and 50% 680 to 2180us.
 * N 1501.3us gives 949.87us and 2049.87us, on the 0.8us step inward 950.4us
 * and 2049.6us, in whole us outward 950 and 2050us; N 0 is 1500.0us;
 * outside 1360.0us to 1640.0us N is held to the nearer bound. */
TEST_CASE(the_margin_sets_the_pulse_range_on_the_kst_step)
{
    static const struct {
        uint16_t n;
        uint8_t  pct;
        uint16_t us[2], dus[2];
    } k[] = {
        { 15000u, 0u,  { 1000u, 2000u }, { 10000u, 20000u } },
        { 15000u, 4u,  { 980u, 2020u },  { 9800u, 20200u } },
        { 15000u, 10u, { 950u, 2050u },  { 9504u, 20496u } },
        { 15000u, 50u, { 750u, 2250u },  { 7504u, 22496u } },
        { 15000u, 51u, { 750u, 2250u },  { 7504u, 22496u } },   /* as 50% */
        { 13600u, 0u,  { 1000u, 2000u }, { 10000u, 20000u } },
        { 13600u, 10u, { 964u, 2064u },  { 9640u, 20640u } },
        { 13600u, 50u, { 820u, 2320u },  { 8200u, 23200u } },
        { 16400u, 0u,  { 1000u, 2000u }, { 10000u, 20000u } },
        { 16400u, 10u, { 936u, 2036u },  { 9360u, 20360u } },
        { 16400u, 50u, { 680u, 2180u },  { 6800u, 21800u } },
        { 15013u, 10u, { 950u, 2050u },  { 9504u, 20496u } },
        { 0u,     10u, { 950u, 2050u },  { 9504u, 20496u } },
        { 12000u, 50u, { 820u, 2320u },  { 8200u, 23200u } },  /* as 1360 */
        { 20000u, 50u, { 680u, 2180u },  { 6800u, 21800u } },  /* as 1640 */
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        uint16_t us[2] = { 0u, 0u }, dus[2] = { 0u, 0u };
        servo_rauto_pulse_range(k[i].pct, k[i].n, us);
        servo_rauto_kst_pulses(k[i].pct, k[i].n, dus);
        CHECK_EQ(us[0], k[i].us[0]);
        CHECK_EQ(us[1], k[i].us[1]);
        CHECK_EQ(dus[0], k[i].dus[0]);
        CHECK_EQ(dus[1], k[i].dus[1]);
        /* On the 0.8us step, inside the margin's range. */
        CHECK_EQ(dus[0] % SERVO_RAUTO_KST_STEP_DUS, 0u);
        CHECK_EQ(dus[1] % SERVO_RAUTO_KST_STEP_DUS, 0u);
        CHECK(dus[0] >= 10u * us[0] && dus[1] <= 10u * us[1]);
        /* Inside PULSE LOWER's and PULSE UPPER's 500us to 2500us. */
        CHECK(dus[0] >= 5000u && dus[1] <= 25000u);
        /* Each side's span from N grows by the margin, within a step. */
        const float n = (float)(k[i].n == 0u ? 15000u
                                : k[i].n < 13600u ? 13600u
                                : k[i].n > 16400u ? 16400u : k[i].n);
        const float m = 1.0f + (float)(k[i].pct > 50u ? 50u : k[i].pct)
                                   / 100.0f;
        CHECK(fabsf((n - (float)dus[0]) - (n - 10000.0f) * m) < 8.0f);
        CHECK(fabsf(((float)dus[1] - n) - (20000.0f - n) * m) < 8.0f);
    }
    servo_rauto_pulse_range(10u, 15000u, NULL);
    servo_rauto_kst_pulses(10u, 15000u, NULL);
    CHECK(!servo_rauto_widen(NULL, NULL));
}

/* 10%: PULSE LOWER and PULSE UPPER 950.4us and 2049.6us, written before
 * any end pulse; the run drives 1000us, 2000us and the center only; the
 * goals and the verify pass are the target, 27.5°. */
TEST_CASE(at_10_percent_the_run_writes_the_pulses_and_aims_at_the_target)
{
    rig_init(0u);
    g.cfg.margin_pct = 10u;
    CHECK(start());
    CHECK_NEAR(g.run.goal_deg[0], 27.5f, 1e-4f);
    CHECK_NEAR(g.run.goal_deg[1], 27.5f, 1e-4f);
    run_out();
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK_EQ(g.pulse_ops, 1u);
    CHECK(g.pulses_before_drive);
    CHECK_EQ(g.pulses[1], 9504u);
    CHECK_EQ(g.pulses[2], 20496u);
    CHECK_EQ(g.cmd_lo, 1000u);
    CHECK_EQ(g.cmd_hi, 2000u);
    /* 150 counts give 30°; 27.5° takes 138. */
    CHECK_EQ(g.v[0], 138u);
    CHECK_EQ(g.v[1], 138u);
    /* The verify pass: each end within the tolerance of the target. */
    CHECK_EQ(g.run.vn, SERVO_RAUTO_VERIFY_N);
    for (unsigned k = 0; k < 2u; ++k) {
        CHECK(g.run.vwithin[k]);
        CHECK(fabsf(g.run.vdeg[k] - 27.5f) <= 0.5f);
    }
    CHECK(in_report("Targets:        1000us 27.5°, 2000us 27.5°, tolerance 0.5°"));
    CHECK(in_report("Margin:         10%: servo pulses 950.4-2049.6us; by the linear model 950us reaches 30.3°, 2050us 30.3°; not driven"));
    CHECK(in_report("Pulse write:    PULSE LOWER 900.0 -> 950.4us, PULSE UPPER 2100.0 -> 2049.6us"));
    CHECK(in_report("goal 27.50°  within"));
    CHECK(in_report("Bench pulses:   PULSE MIN/MAX 1000-2000 -> 950-2050us, for the session"));
}

/* 30° at both ends, target 30°, margin 10%: within at the first measures,
 * no value write; the old 33° goal would have written. */
TEST_CASE(a_servo_at_the_target_takes_no_value_write_at_any_margin)
{
    static const uint8_t m[] = { 0u, 10u, 50u };
    for (size_t i = 0; i < sizeof(m) / sizeof(m[0]); ++i) {
        rig_init(0u);
        g.cfg.target_deg[0] = g.cfg.target_deg[1] = 30.0f;
        g.cfg.margin_pct = m[i];
        CHECK(start());
        run_out();
        CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
        CHECK_EQ(g.run.writes[0] + g.run.writes[1], 0u);
        CHECK_EQ(g.v[0], 150u);
        CHECK_EQ(g.v[1], 150u);
    }
}

/* The bench's PULSE MIN and PULSE MAX against the margin's 950 to 2050us:
 * narrower widens, equal and wider stay, one side narrower widens that
 * side alone; at 0% nothing widens. */
TEST_CASE(the_bench_range_widens_only_where_narrower)
{
    static const struct {
        uint8_t  pct;
        uint16_t min, max;
        bool     widen;
        uint16_t lo, hi;
    } k[] = {
        { 10u, 1000u, 2000u, true,  950u, 2050u },
        { 10u, 950u,  2050u, false, 950u, 2050u },
        { 10u, 900u,  2100u, false, 900u, 2100u },
        { 10u, 900u,  2000u, true,  900u, 2050u },
        { 10u, 1000u, 2100u, true,  950u, 2100u },
        { 50u, 1000u, 2000u, true,  750u, 2250u },
        { 0u,  1000u, 2000u, false, 1000u, 2000u },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        rig_init(0u);
        g.cfg.margin_pct = k[i].pct;
        g.cfg.min_us = k[i].min;
        g.cfg.max_us = k[i].max;
        CHECK(start());
        /* Not before the servo holds the margin's pulses. */
        CHECK(!servo_rauto_widen(&g.run, NULL));
        run_out();
        CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
        uint16_t prof[2] = { 0u, 0u };
        CHECK_EQ(servo_rauto_widen(&g.run, prof), k[i].widen);
        CHECK_EQ(prof[0], k[i].lo);
        CHECK_EQ(prof[1], k[i].hi);
        CHECK_EQ(in_report("Bench pulses:"), k[i].widen);
    }
}

/* The servo holds 950.4 to 2049.6us at the read: no pulse write, and the
 * bench's 1000 to 2000us widen all the same. */
TEST_CASE(pulses_already_at_the_margin_need_no_write_and_still_widen)
{
    rig_init(0u);
    g.cfg.margin_pct = 10u;
    g.pulses[1] = 9504u;
    g.pulses[2] = 20496u;
    CHECK(start());
    run_out();
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK_EQ(g.pulse_ops, 0u);
    CHECK(!in_report("Pulse write:"));
    uint16_t prof[2];
    CHECK(servo_rauto_widen(&g.run, prof));
    CHECK_EQ(prof[0], 950u);
    CHECK_EQ(prof[1], 2050u);

    /* 1000.0 to 2000.0us at 10%: written, not taken as the margin's. */
    rig_init(0u);
    g.cfg.margin_pct = 10u;
    g.pulses[1] = 10000u;
    g.pulses[2] = 20000u;
    CHECK(start());
    run_out();
    CHECK_EQ(g.pulse_ops, 1u);
    CHECK_EQ(g.pulses[1], 9504u);
}

/* A STOP while the pulse write is on its way, and a pulse write read back
 * as 1000.0 to 2000.0us: the servo's pulses are not known to be the
 * margin's, and the bench's range stays; every output off. */
TEST_CASE(a_pulse_write_cut_off_or_read_back_otherwise_widens_nothing)
{
    rig_init(0u);
    g.cfg.margin_pct = 10u;
    CHECK(start());
    while (servo_rauto_running(&g.run) && g.op != OP_PULSES) {
        tick();
    }
    CHECK_EQ(g.op, OP_PULSES);
    servo_rauto_abort(&g.run, SERVO_TEST_AB_OPERATOR, g.now);
    run_out();
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_ABORTED);
    CHECK(ended_safe());
    CHECK(!servo_rauto_widen(&g.run, NULL));
    CHECK(servo_rauto_restore(&g.run));
    CHECK(!in_report("Bench pulses:"));

    rig_init(0u);
    g.cfg.margin_pct = 10u;
    g.pulses[1] = 10000u;
    g.pulses[2] = 20000u;
    g.pulses_lost = true;
    CHECK(start());
    run_out();
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_ABORTED);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_WRITE);
    CHECK(ended_safe());
    CHECK(!servo_rauto_widen(&g.run, NULL));
    CHECK_EQ(g.cmd_hi, RCENTER);        /* no end pulse driven */
}

/* HITEC D SERIES and HITEC CLASSIC: a margin above 0 is refused at the
 * start, 0 runs with no pulse write, the goals the targets, nothing to
 * widen, and the report names the lock. */
TEST_CASE(a_hitec_family_takes_margin_0_only_and_writes_no_pulses)
{
    static const servo_rauto_family_t f[] = { SERVO_RAUTO_FAM_HITEC_D,
                                              SERVO_RAUTO_FAM_HITEC_C };
    for (size_t i = 0; i < 2u; ++i) {
        rig_init(0u);
        g.cfg.family = (uint8_t)f[i];
        g.cfg.margin_pct = 10u;
        CHECK(!start());
        g.cfg.margin_pct = 1u;
        CHECK(!start());
        g.cfg.margin_pct = 0u;
        CHECK(start());
        CHECK_NEAR(g.run.goal_deg[0], 27.5f, 1e-4f);
        run_out();
        CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
        CHECK_EQ(g.pulse_ops, 0u);
        CHECK(!servo_rauto_widen(&g.run, NULL));
        CHECK(in_report("Margin:         0%: KST/Chaservo only, a Hitec servo has no pulse registers"));
        CHECK(!in_report("Bench pulses:"));
    }
}

/* NEUTRAL 1640.0us at 50%: each side extends by half its own span from
 * NEUTRAL, PULSE LOWER 680.0us and PULSE UPPER 2180.0us, and the bench's
 * PULSE MIN and PULSE MAX widen to 680-2180us; at 1360.0us and 10% 964.0us
 * and 2064.0us, the bench to 964-2064us.  The report names the NEUTRAL. */
TEST_CASE(an_off_center_neutral_scales_each_side_from_neutral)
{
    static const struct {
        uint16_t n;
        uint8_t  pct;
        uint16_t dus[2], prof[2];
    } k[] = {
        { 16400u, 50u, { 6800u, 21800u }, { 680u, 2180u } },
        { 13600u, 10u, { 9640u, 20640u }, { 964u, 2064u } },
        { 16400u, 10u, { 9360u, 20360u }, { 936u, 2036u } },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        rig_init(0u);
        g.cfg.margin_pct = k[i].pct;
        g.pulses[0] = k[i].n;
        CHECK(start());
        run_out();
        CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
        CHECK_EQ(g.pulse_ops, 1u);
        CHECK_EQ(g.pulses[1], k[i].dus[0]);
        CHECK_EQ(g.pulses[2], k[i].dus[1]);
        uint16_t prof[2] = { 0u, 0u };
        CHECK(servo_rauto_widen(&g.run, prof));
        CHECK_EQ(prof[0], k[i].prof[0]);
        CHECK_EQ(prof[1], k[i].prof[1]);
        CHECK(in_report("Margin:         ") && in_report("% from NEUTRAL "));
    }
    CHECK(in_report("Margin:         10% from NEUTRAL 1640.0us: servo pulses 936.0-2036.0us; by the linear model 936us reaches 30.3°, 2036us 30.3°; not driven"));
    CHECK(in_report("Bench pulses:   PULSE MIN/MAX 1000-2000 -> 936-2036us, for the session"));
}

/* NEUTRAL 1640.0us at 10%: a read back of the 1500.0us range, 950.4 to
 * 2049.6us, or of a NEUTRAL the write moved, ends the run as ABORTED with
 * WRITE, nothing widened. */
TEST_CASE(the_read_back_check_takes_the_reads_neutral)
{
    static const uint16_t back[2][3] = {
        { 16400u, 9504u, 20496u },     /* the 1500.0us range */
        { 15000u, 9360u, 20360u },     /* NEUTRAL moved */
    };
    for (size_t i = 0; i < 2u; ++i) {
        rig_init(0u);
        g.cfg.margin_pct = 10u;
        g.pulses[0] = 16400u;
        CHECK(start());
        while (servo_rauto_running(&g.run) && g.op != OP_PULSES) {
            tick();
        }
        CHECK_EQ(g.op, OP_PULSES);
        CHECK_EQ(g.want_p[0], 9360u);
        CHECK_EQ(g.want_p[1], 20360u);
        servo_rauto_pulsed(&g.run, true, back[i], g.now);
        CHECK_EQ(g.run.verdict, SERVO_RAUTO_ABORTED);
        CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_WRITE);
        run_out();
        CHECK(ended_safe());
        CHECK(!servo_rauto_widen(&g.run, NULL));
    }
}

/* A KST NEUTRAL outside 1360.0us to 1640.0us: FAIL at the read with
 * NEUTRAL OUTSIDE, LEAVE asked, no pulse write, nothing driven, nothing
 * widened; the bounds themselves run. */
TEST_CASE(a_neutral_outside_the_kst_range_fails_at_the_read)
{
    static const uint16_t bad[] = { 13599u, 16401u, 0u };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        rig_init(0u);
        g.cfg.margin_pct = 10u;
        g.pulses[0] = bad[i];
        CHECK(start());
        run_out();
        CHECK_EQ(g.run.verdict, SERVO_RAUTO_FAIL);
        CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_NEUTRAL);
        CHECK_EQ(servo_rauto_why_str(&g.run), SERVO_STR_RA_WHY_NEUTRAL);
        CHECK_EQ(g.pulse_ops, 0u);
        CHECK_EQ(g.cmd_hi, RCENTER);
        CHECK_EQ(g.leave_asks, 1u);   /* the port given back, as NO SERVO */
        CHECK(!servo_rauto_widen(&g.run, NULL));
        CHECK(in_report("NEUTRAL outside 1360-1640us"));
    }
    static const uint16_t edge[] = { 13600u, 16400u };
    for (size_t i = 0; i < 2u; ++i) {
        rig_init(0u);
        g.cfg.margin_pct = 0u;
        g.pulses[0] = edge[i];
        CHECK(start());
        run_out();
        CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    }
}

int main(void)
{
    RUN(the_margin_sets_the_pulse_range_on_the_kst_step);
    RUN(at_10_percent_the_run_writes_the_pulses_and_aims_at_the_target);
    RUN(a_servo_at_the_target_takes_no_value_write_at_any_margin);
    RUN(the_bench_range_widens_only_where_narrower);
    RUN(pulses_already_at_the_margin_need_no_write_and_still_widen);
    RUN(a_pulse_write_cut_off_or_read_back_otherwise_widens_nothing);
    RUN(a_hitec_family_takes_margin_0_only_and_writes_no_pulses);
    RUN(an_off_center_neutral_scales_each_side_from_neutral);
    RUN(the_read_back_check_takes_the_reads_neutral);
    RUN(a_neutral_outside_the_kst_range_fails_at_the_read);
    return test_summary("servo_rauto_margin");
}
