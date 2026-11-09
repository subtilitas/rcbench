/*
 * AUTO end points (servo_rudder_auto.h): which register moves which side,
 * and writes held to their effect.
 *
 * Under test: a Hitec D series servo set to CW has its registers known
 * from the read, LEFT END at 1000us; a side within the tolerance at its
 * first measure, then the other side's writes that do not move it end the
 * run after 2 writes with NO_EFFECT, the register of the side's name
 * written (Hitec) or both tried (KST), every output off and RESTORE
 * offered; run 2's shape with the IMU's offset writes the 2000us side's
 * register and passes; a register found on a measure an offset fooled
 * ends 2 writes later; a D series servo set to CCW finds its registers on
 * the first write; writes that move a known side the wrong way end after
 * 2; a run whose writes move both sides still passes on both families;
 * and the bound of 50% around the model's first estimate, with the bench's
 * run 2 numbers and on a servo that moves 30% of each write.
 *
 * The rig is servo_rauto_rig.h: 150 counts give 30°, the goals 27.5°, the
 * tolerance 0.5°.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_rauto_rig.h"

#include "hitec_prog_run.h"

/* Both sides 2.5° past the goal, the 1000us side written first: one write
 * a side and PASS on both families; the Hitec D series run knows its
 * registers before the first write. */
TEST_CASE(both_sides_written_once_pass_on_kst_and_hitec_d)
{
    for (int fam = 0; fam < 2; ++fam) {
        rig_init(0u);
        g.cfg.family = fam == 0 ? SERVO_RAUTO_FAM_KST : SERVO_RAUTO_FAM_HITEC_D;
        CHECK(start());
        CHECK(run_to(SERVO_RAUTO_PH_MOVE, RAUTO_MS));
        CHECK_EQ(g.run.reg_side[0], fam == 0 ? -1 : 0);
        CHECK_EQ(g.run.reg_side[1], fam == 0 ? -1 : 1);
        run_for(RAUTO_MS);
        CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
        CHECK_EQ(g.run.writes[0], 1u);
        CHECK_EQ(g.run.writes[1], 1u);
        CHECK_EQ(g.v[0], 138u);
        CHECK_EQ(g.v[1], 138u);
        CHECK_EQ(g.run.reg_side[0], 0);
        CHECK_EQ(g.run.reg_side[1], 1);
        CHECK(in_report(fam == 0 ? "moves the 1000us side"
                                 : "LEFT END (0xB2): 8042 before, 8054 after "
                                   "(counts); moves the 1000us side"));
        CHECK(ended_safe());
    }
}

/* The 1000us side at 27.6°, within at its first measure; the 2000us side
 * at 30° and no write moves it.  Hitec D series, CW: RIGHT END is the
 * 2000us side's from the read; its two writes leave the side where it was
 * and the run ends NO_EFFECT, LEFT END untouched, every output off and
 * RESTORE offered. */
TEST_CASE(hitec_d_writes_that_do_not_move_the_2000us_side_end_after_two)
{
    rig_init(0u);
    g.cfg.family = SERVO_RAUTO_FAM_HITEC_D;
    g.v[0] = 138u;
    g.frozen[1] = true;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_FAIL);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_NO_EFFECT);
    CHECK_EQ(servo_rauto_why_str(&g.run), SERVO_STR_RA_WHY_NO_EFFECT_HI);
    CHECK_EQ(g.run.reg_side[0], 0);
    CHECK_EQ(g.run.reg_side[1], 1);
    CHECK_EQ(g.run.writes[0], 0u);
    CHECK_EQ(g.run.writes[1], SERVO_RAUTO_NO_EFFECT_WRITES);
    CHECK_EQ(g.v[0], 138u);
    CHECK_EQ(g.v[1], 127u);
    /* 1000us once, then 2000us before and after each write. */
    CHECK_EQ(g.run.step_n, 4u);
    CHECK(g.run.steps[0].within);
    CHECK_EQ(g.run.steps[3].value, 127u);
    CHECK(ended_safe());
    CHECK(servo_rauto_restore(&g.run));
    CHECK(in_report("Result:         FAIL - writes do not move the 2000us side"));
    CHECK(in_report("Restore:"));
}

/* The same servo on KST, whose registers are not known: RIGHT ANGLE is
 * tried for the 2000us side, the side stays, RIGHT ANGLE goes back and
 * LEFT ANGLE is tried, the side stays again: NO_EFFECT after 2 writes,
 * nothing found. */
TEST_CASE(kst_writes_that_do_not_move_the_2000us_side_end_after_two)
{
    rig_init(0u);
    g.v[0] = 138u;
    g.frozen[1] = true;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_FAIL);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_NO_EFFECT);
    CHECK_EQ(g.run.side, 1u);
    CHECK_EQ(g.run.reg_side[0], -1);
    CHECK_EQ(g.run.reg_side[1], -1);
    CHECK_EQ(g.run.writes[1], 2u);
    CHECK_EQ(g.run.steps[2].value, 138u);
    CHECK_EQ(g.run.steps[3].value, 127u);
    CHECK_EQ(g.v[1], 150u);
    CHECK_EQ(g.v[0], 127u);
    CHECK(ended_safe());
    CHECK(servo_rauto_restore(&g.run));
}

/* The bench's run 2 as it was measured: the 1000us side within at its
 * first measure, the 2000us side 2.5° past the goal, and an offset of
 * 1.5° toward the goal on the 2000us side from the first write on, as the
 * IMU's drift gave.  The 2000us side's register is the second one: RIGHT
 * END from the read (Hitec D series, CW), RIGHT ANGLE tried first (KST).
 * Its writes land the side within, the first register untouched. */
TEST_CASE(run_2_writes_the_2000us_side_register_and_passes)
{
    for (int fam = 0; fam < 2; ++fam) {
        rig_init(0u);
        g.cfg.family = fam == 0 ? SERVO_RAUTO_FAM_KST : SERVO_RAUTO_FAM_HITEC_D;
        g.v[0] = 138u;
        g.drift[1] = -1.5f;
        g.drift_from = 1u;
        CHECK(start());
        run_for(RAUTO_MS);
        CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
        CHECK_EQ(g.run.reg_side[0], 0);
        CHECK_EQ(g.run.reg_side[1], 1);
        CHECK_EQ(g.run.writes[0], 0u);
        CHECK(g.run.writes[1] >= 1u);
        CHECK_EQ(g.v[0], 138u);
        CHECK(fabsf(g.run.measured_deg[1] - 27.5f) <= 0.5f);
        CHECK(!servo_rauto_restore(&g.run));
    }
}

/* The bench's run 2 on a servo whose registers are found: LEFT ANGLE moves
 * the 2000us side.  RIGHT ANGLE is tried first and an offset of 1.5°
 * toward the goal appears with that write, as an IMU's drift does: the
 * measure reads it as moved and takes RIGHT ANGLE for the 2000us side.
 * Its next 2 writes move nothing there, and the run ends NO_EFFECT after
 * 3 writes in place of 8. */
TEST_CASE(a_register_found_on_a_drifted_measure_ends_two_writes_later)
{
    rig_init(0u);
    g.map[0] = 1;
    g.map[1] = 0;
    g.v[0] = 150u;
    g.v[1] = 138u;
    g.drift[1] = -1.5f;
    g.drift_from = 1u;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_FAIL);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_NO_EFFECT);
    CHECK_EQ(g.run.reg_side[1], 1);
    CHECK_EQ(g.run.writes[0], 0u);
    CHECK_EQ(g.run.writes[1], 1u + SERVO_RAUTO_NO_EFFECT_WRITES);
    /* The 2000us side's own register never written. */
    CHECK_EQ(g.v[0], 150u);
    CHECK(ended_safe());
    CHECK(servo_rauto_restore(&g.run));
}

/* A Hitec D series servo set to CCW: the registers are not known, the
 * first write of the 1000us side goes to RIGHT END and finds it. */
TEST_CASE(hitec_d_ccw_finds_its_registers_on_the_first_write)
{
    rig_init(0u);
    g.cfg.family = SERVO_RAUTO_FAM_HITEC_D;
    g.reversion = true;
    g.map[0] = 1;
    g.map[1] = 0;
    CHECK(start());
    CHECK(run_to(SERVO_RAUTO_PH_MOVE, RAUTO_MS));
    CHECK_EQ(g.run.reg_side[0], -1);
    CHECK_EQ(g.run.reg_side[1], -1);
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_PASS);
    CHECK_EQ(g.run.reg_side[0], 1);
    CHECK_EQ(g.run.reg_side[1], 0);
    CHECK_EQ(g.run.writes[0], 1u);
    CHECK_EQ(g.run.writes[1], 1u);
}

/* Writes that move a known side away from the goal: the first one alone
 * can be noise, the second ends the run, on the side written. */
TEST_CASE(writes_that_move_a_known_side_the_wrong_way_end_after_two)
{
    rig_init(0u);
    g.cfg.family = SERVO_RAUTO_FAM_HITEC_D;
    g.resp = -1.0f;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_FAIL);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_NO_EFFECT);
    CHECK_EQ(servo_rauto_why_str(&g.run), SERVO_STR_RA_WHY_NO_EFFECT_LO);
    CHECK_EQ(g.run.writes[0], SERVO_RAUTO_NO_EFFECT_WRITES);
    CHECK_EQ(g.run.writes[1], 0u);
    CHECK(ended_safe());
    CHECK(servo_rauto_restore(&g.run));
}

/* The bound on a write with the bench's run 2 numbers: LEFT END 4726 at
 * the read, the side's first measure 50.02°, a 49.5° goal (which the run's
 * measures fit with a 0.5° tolerance): the estimate 4676.9, the bound
 * 2339 to 7015.  The run's last write, 6627, lies inside it: the 2-write
 * effect check, not the bound, stops that run. */
TEST_CASE(the_bound_with_the_run_2_numbers)
{
    CHECK(servo_rauto_in_cap(4726u, 49.5f, 50.02f, 4677u));
    CHECK(servo_rauto_in_cap(4726u, 49.5f, 50.02f, 6627u));
    CHECK(servo_rauto_in_cap(4726u, 49.5f, 50.02f, 7015u));
    CHECK(!servo_rauto_in_cap(4726u, 49.5f, 50.02f, 7016u));
    CHECK(servo_rauto_in_cap(4726u, 49.5f, 50.02f, 2339u));
    CHECK(!servo_rauto_in_cap(4726u, 49.5f, 50.02f, 2338u));
    CHECK(!servo_rauto_in_cap(4726u, 49.5f, 0.0f, 4726u));
    CHECK(!servo_rauto_in_cap(4726u, 49.5f, -1.0f, 4726u));
}

/* A servo whose writes move the side 30% of the linear change, each more
 * than the effect check asks: 150 counts give 30°, the goal 49.5° (45°
 * and 10%), the estimate 247.5 counts and the bound 124 to 371.  The
 * writes go 248, 342, and the third would be 400: FAIL CAP before it, the
 * register at 342, every output off and RESTORE offered. */
TEST_CASE(a_write_past_50_percent_of_the_estimate_ends_the_run)
{
    rig_init(0u);
    g.cfg.family = SERVO_RAUTO_FAM_HITEC_D;
    g.cfg.target_deg[0] = 45.0f;
    g.cfg.target_deg[1] = 45.0f;
    g.hi = 400u;
    g.resp = 0.3f;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_FAIL);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_CAP);
    CHECK_EQ(servo_rauto_why_str(&g.run), SERVO_STR_RA_WHY_CAP);
    CHECK_EQ(g.run.writes[0], 2u);
    CHECK_EQ(g.v[0], 342u);
    CHECK(ended_safe());
    CHECK(servo_rauto_restore(&g.run));
    CHECK(in_report("FAIL - end point too far from its estimate"));
}

/* The units the programming pages show: the bench's D485HW at NEUTRAL
 * 8192 holds LEFT END 4841 and RIGHT END 4808 counts from NEUTRAL, which
 * the HITEC page shows as 3351 and 13000 counts; the run seam's
 * hitec_run_to_reg() writes the same.  KST and a classic Hitec servo show
 * the raw value itself. */
TEST_CASE(end_points_show_in_the_programming_pages_units)
{
    servo_rauto_t r;
    servo_rauto_init(&r);
    r.cfg.family = SERVO_RAUTO_FAM_HITEC_D;
    r.servo.mid = 8192u;
    CHECK_EQ(servo_rauto_reg_of(&r, 0, 4841u), 3351u);
    CHECK_EQ(servo_rauto_reg_of(&r, 1, 4808u), 13000u);
    CHECK_EQ(servo_rauto_unit(&r), SERVO_STR_RA_R_COUNTS);
    hitec_run_servo_t h;
    memset(&h, 0, sizeof(h));
    h.series = (uint8_t)HITEC_D;
    h.mid = 8192u;
    for (uint16_t t = 1u; t < 8192u; t = (uint16_t)(t * 3u)) {
        CHECK_EQ(servo_rauto_reg_of(&r, 0, t), hitec_run_to_reg(&h, 0u, t));
        CHECK_EQ(servo_rauto_reg_of(&r, 1, t), hitec_run_to_reg(&h, 1u, t));
    }
    r.cfg.family = SERVO_RAUTO_FAM_KST;
    CHECK_EQ(servo_rauto_reg_of(&r, 0, 150u), 150u);
    CHECK_EQ(servo_rauto_unit(&r), SERVO_STR_RA_R_RAW);
    r.cfg.family = SERVO_RAUTO_FAM_HITEC_C;
    CHECK_EQ(servo_rauto_reg_of(&r, 1, 194u), 194u);
    CHECK_EQ(servo_rauto_unit(&r), SERVO_STR_RA_R_RAW);
    CHECK_EQ(servo_rauto_reg_of(NULL, 0, 77u), 77u);
    CHECK_EQ(servo_rauto_unit(NULL), SERVO_STR_RA_R_RAW);
    /* A D series servo read without NEUTRAL is no servo to the run. */
    rig_init(0u);
    g.cfg.family = SERVO_RAUTO_FAM_HITEC_D;
    g.no_mid = true;
    CHECK(start());
    run_for(RAUTO_MS);
    CHECK_EQ(g.run.verdict, SERVO_RAUTO_FAIL);
    CHECK_EQ(g.run.why, SERVO_RAUTO_WHY_NO_SERVO);
    CHECK_EQ(g.write_ops, 0u);
}

int main(void)
{
    RUN(end_points_show_in_the_programming_pages_units);
    RUN(the_bound_with_the_run_2_numbers);
    RUN(a_write_past_50_percent_of_the_estimate_ends_the_run);
    RUN(both_sides_written_once_pass_on_kst_and_hitec_d);
    RUN(run_2_writes_the_2000us_side_register_and_passes);
    RUN(hitec_d_writes_that_do_not_move_the_2000us_side_end_after_two);
    RUN(kst_writes_that_do_not_move_the_2000us_side_end_after_two);
    RUN(a_register_found_on_a_drifted_measure_ends_two_writes_later);
    RUN(hitec_d_ccw_finds_its_registers_on_the_first_write);
    RUN(writes_that_move_a_known_side_the_wrong_way_end_after_two);
    return test_summary("servo_rauto_effect");
}
