/*
 * The limit run on a Hitec servo (servo_limrun.h, "Servo families"),
 * against a modelled servo whose travel its end points set.
 *
 * Under test: a D series run lowers LEFT END toward NEUTRAL in counts, a
 * classic run writes the cell of the bound side, by direction; the run's
 * pulses are the family's model; SERVO PULSES is refused for a Hitec
 * family; a read with no Hitec servo ends NOT_HITEC; the report names the
 * family's registers; the mapping is found on a CCW classic servo; RESTORE
 * is offered after a FAIL with the values before the run; the bench's
 * range is held to the model's pulses; the 32-bit ms wrap inside a run.
 *
 * The rig is hitec_limrun_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_limrun_rig.h"

#include "servo_str.h"

/* Only the left side binds: the right end's travel short of its stop. */
static void left_only(void)
{
    h.hs.value[1] = 2000u;            /* 290us of horn: free before 1880 */
    h_wire(h.wire);
    memcpy(h.wire0, h.wire, sizeof(h.wire0));
}

TEST_CASE(a_d_series_run_lowers_0xb2_toward_0xc2_for_a_bound_left_side)
{
    h_init_d(0u, true);
    left_only();
    CHECK(h_start());
    h_run_for(H_RUN_MS);
    CHECK_EQ(h.run.verdict, SERVO_LIMRUN_PASS);
    /* The search ran out to the model's full-travel pulses: the final check,
     * free, reaches both. */
    CHECK_EQ(h.run.end_us[SERVO_LIMRUN_LO], 850u);
    CHECK_EQ(h.run.end_us[SERVO_LIMRUN_HI], 2150u);
    CHECK_EQ(h.cmd_lo, 850u);
    CHECK_EQ(h.cmd_hi, 2150u);
    CHECK_EQ(h.connects_at_drive, 1u);
    /* LEFT END limits the short pulses, as the model has it. */
    CHECK_EQ(h.run.reg_side[SERVO_LIMRUN_LEFT], SERVO_LIMRUN_LO);
    /* Travel units in, counts on the wire: 0xB2 moved up toward 0xC2, 0xB0
     * stayed where it was. */
    CHECK(h.hs.value[0] < 4470u);
    CHECK_EQ(h.hs.value[1], 2000u);
    CHECK_EQ(h.wire[0], (uint16_t)(8192u - h.hs.value[0]));
    CHECK(h.wire[0] > h.wire0[0] && h.wire[0] < 8192u);
    CHECK_EQ(h.wire[1], h.wire0[1]);
    CHECK_EQ(h.run.cur[0], h.hs.value[0]);
    /* The horn at full travel now stops short of the left stop at 1150us. */
    CHECK(1500.0f - h.upc * (float)h.hs.value[0] > 1150.0f);
    /* Nothing asked for SERVO PULSES; each write a session of its own. */
    CHECK(h.write_ops >= 1u && h.write_ops <= 3u);
    CHECK_EQ(h.leaves, h.connects);
}

TEST_CASE(a_classic_run_writes_the_cell_of_the_bound_side)
{
    /* A CW servo: left is cell 0x14, right 0x13.  Only the right side
     * binds. */
    h_init_c(0u, true);
    h.hs.value[0] = 100u;             /* 325us: free before 1150 */
    h_wire(h.wire);
    memcpy(h.wire0, h.wire, sizeof(h.wire0));
    CHECK(h_start());
    h_run_for(H_RUN_MS);
    CHECK_EQ(h.run.verdict, SERVO_LIMRUN_PASS);
    CHECK_EQ(h.run.end_us[SERVO_LIMRUN_LO], 900u);
    CHECK_EQ(h.run.end_us[SERVO_LIMRUN_HI], 2100u);
    CHECK_EQ(h.run.reg_side[SERVO_LIMRUN_RIGHT], SERVO_LIMRUN_HI);
    CHECK_EQ(h.hs.value[0], 100u);
    CHECK(h.hs.value[1] < 200u && h.hs.value[1] > 0u);
    /* A classic travel is the raw cell: the wire holds it as it is, 8 bits. */
    CHECK_EQ(h.wire[1], h.hs.value[1]);
    CHECK(h.wire[1] <= 255u);
    CHECK_EQ(h.wire[0], h.wire0[0]);
    CHECK(1500.0f + h.upc * (float)h.hs.value[1] < 1880.0f);
}

TEST_CASE(set_pulses_is_refused_with_a_hitec_family)
{
    h_init_d(0u, true);
    h.cfg.set_pulses = true;
    CHECK(!h_start());
    CHECK(!servo_limrun_running(&h.run));
    h.cfg.family = SERVO_LIMRUN_FAM_HITEC_C;
    CHECK(!h_start());
    /* KST takes it; a family past the enum takes nothing. */
    h.cfg.family = SERVO_LIMRUN_FAM_KST;
    CHECK(h_start());
    servo_limrun_init(&h.run);
    h.cfg.set_pulses = false;
    h.cfg.family = SERVO_LIMRUN_FAM_COUNT;
    CHECK(!h_start());
    h.cfg.family = SERVO_LIMRUN_FAM_HITEC_C;
    CHECK(h_start());
}

TEST_CASE(no_servo_read_ends_not_hitec)
{
    /* No servo answers the first CONNECT. */
    h_init_d(0u, true);
    h.no_servo = true;
    CHECK(h_start());
    h_run_for(H_RUN_MS);
    CHECK_EQ(h.run.verdict, SERVO_LIMRUN_FAIL);
    CHECK_EQ(h.run.why, SERVO_LIMRUN_WHY_NOT_HITEC);
    CHECK_EQ(servo_limrun_why_str(&h.run), SERVO_STR_L_WHY_NOT_HITEC);
    CHECK_STR_EQ(servo_str(SERVO_STR_L_WHY_NOT_HITEC), "no Hitec servo read");
    CHECK(!h.driven);
    CHECK(!servo_limrun_wrote(&h.run));
    /* A FAIL is a run's outcome, not an error: the bench stays SAFE, the
     * servo let go, the port asked back. */
    CHECK(h.safe);
    CHECK_EQ(h.op, H_OP_LEAVE);
    /* A read whose end points do not edit is no Hitec servo the run takes;
     * the same on a classic servo. */
    h_init_c(0u, false);
    h.not_editable = true;
    CHECK(h_start());
    h_run_for(H_RUN_MS);
    CHECK_EQ(h.run.why, SERVO_LIMRUN_WHY_NOT_HITEC);
    CHECK(h_says("no Hitec servo read"));
    /* KST keeps its own reason. */
    h_init_d(0u, true);
    h.cfg.family = SERVO_LIMRUN_FAM_KST;
    h.no_servo = true;
    CHECK(h_start());
    h_run_for(H_RUN_MS);
    CHECK_EQ(h.run.why, SERVO_LIMRUN_WHY_NOT_KST);
}

TEST_CASE(the_report_names_left_end_0xb2_and_cell_0x14_by_family)
{
    CHECK_STR_EQ(servo_limrun_reg_name(SERVO_LIMRUN_FAM_KST, SERVO_LIMRUN_LEFT),
                 "LEFT ANGLE");
    CHECK_STR_EQ(servo_limrun_reg_name(SERVO_LIMRUN_FAM_KST,
                                       SERVO_LIMRUN_RIGHT | SERVO_LIMRUN_REG_CCW),
                 "RIGHT ANGLE");
    CHECK_STR_EQ(servo_limrun_reg_name(SERVO_LIMRUN_FAM_HITEC_D,
                                       SERVO_LIMRUN_LEFT | SERVO_LIMRUN_REG_CCW),
                 "LEFT END (0xB2)");
    CHECK_STR_EQ(servo_limrun_reg_name(SERVO_LIMRUN_FAM_HITEC_D,
                                       SERVO_LIMRUN_RIGHT),
                 "RIGHT END (0xB0)");
    CHECK_STR_EQ(servo_limrun_reg_name(SERVO_LIMRUN_FAM_HITEC_C,
                                       SERVO_LIMRUN_LEFT),
                 "LEFT END (CELL 0x14)");
    CHECK_STR_EQ(servo_limrun_reg_name(SERVO_LIMRUN_FAM_HITEC_C,
                                       SERVO_LIMRUN_RIGHT),
                 "RIGHT END (CELL 0x13)");
    CHECK_STR_EQ(servo_limrun_reg_name(SERVO_LIMRUN_FAM_HITEC_C,
                                       SERVO_LIMRUN_LEFT | SERVO_LIMRUN_REG_CCW),
                 "LEFT END (CELL 0x13)");
    CHECK_STR_EQ(servo_limrun_reg_name(SERVO_LIMRUN_FAM_HITEC_C,
                                       SERVO_LIMRUN_RIGHT | SERVO_LIMRUN_REG_CCW),
                 "RIGHT END (CELL 0x14)");
    CHECK_STR_EQ(servo_limrun_reg_name(7u, SERVO_LIMRUN_RIGHT), "RIGHT ANGLE");

    h_init_d(0u, true);
    CHECK(h_start());
    h_run_for(H_RUN_MS);
    CHECK_EQ(h.run.verdict, SERVO_LIMRUN_PASS);
    CHECK(h_says("PROGRAMMING (LEFT END and RIGHT END, travel units)"));
    CHECK(h_says("Servo:          Hitec D SERIES, DIRECTION CW"));
    CHECK(h_says("Pulse model:    NEUTRAL 1500.0us, full travel at 850.0us "
                 "and 2150.0us, not measured"));
    CHECK(h_says("LEFT END (0xB2): 4470 before, "));
    CHECK(h_says("RIGHT END (0xB0): 4470 before, "));
    CHECK(h_says("Unit: counts from NEUTRAL 0xC2"));
    CHECK(h_says("Backup:         HITEC001, the values before the run"));
    CHECK(!h_says("LEFT ANGLE"));
    CHECK(!h_says("20k_sel"));

    h_init_c(0u, true);
    CHECK(h_start());
    h_run_for(H_RUN_MS);
    CHECK(h_says("Servo:          Hitec CLASSIC, DIRECTION CW"));
    CHECK(h_says("full travel at 900.0us and 2100.0us"));
    CHECK(h_says("LEFT END (CELL 0x14): 200 before, "));
    CHECK(h_says("RIGHT END (CELL 0x13): 200 before, "));
    CHECK(h_says("Unit: the raw cell, its unit not known"));

    h_init_c(0u, false);
    h.left_lo = false;
    CHECK(h_start());
    h_run_for(H_RUN_MS);
    CHECK(h_says("DIRECTION CCW"));
    CHECK(h_says("LEFT END (CELL 0x13): "));
    CHECK(h_says("RIGHT END (CELL 0x14): "));
}

TEST_CASE(the_mapping_search_finds_the_register_on_a_ccw_classic_servo)
{
    /* CCW: the left travel, cell 0x13, limits the long pulses.  The run's
     * first write goes to LEFT END for the first bound side, the short one,
     * finds the end stayed, and swaps. */
    h_init_c(0u, false);
    h.left_lo = false;
    h.hs.value[0] = 100u;             /* the long side free: 325us */
    h_wire(h.wire);
    memcpy(h.wire0, h.wire, sizeof(h.wire0));
    CHECK(h_start());
    h_run_for(H_RUN_MS);
    CHECK_EQ(h.run.verdict, SERVO_LIMRUN_PASS);
    CHECK_EQ(h.run.reg_side[SERVO_LIMRUN_RIGHT], SERVO_LIMRUN_LO);
    CHECK_EQ(h.run.reg_side[SERVO_LIMRUN_LEFT], SERVO_LIMRUN_HI);
    CHECK_EQ(h.run.map_tries, 2u);
    /* LEFT END went back to its own value; RIGHT END took the low side's. */
    CHECK_EQ(h.hs.value[0], 100u);
    CHECK(h.hs.value[1] < 200u);
    CHECK(h.run.writes <= SERVO_LIMRUN_WRITES_MAX);
}

TEST_CASE(restore_is_offered_after_a_fail_with_the_values_before_the_run)
{
    /* A servo whose travel is not linear in the pulse: the final value from
     * the linear model still binds at the check. */
    h_init_d(0u, true);
    left_only();
    h.square = true;
    CHECK(h_start());
    h_run_for(H_RUN_MS);
    CHECK_EQ(h.run.verdict, SERVO_LIMRUN_FAIL);
    CHECK_EQ(h.run.why, SERVO_LIMRUN_WHY_STILL_BINDS);
    CHECK(servo_limrun_restore(&h.run));
    uint16_t l = 0u, r = 0u;
    CHECK(servo_limrun_original(&h.run, &l, &r));
    CHECK_EQ(l, 4470u);
    CHECK_EQ(r, 2000u);
    /* Never below the travel the hard rules take. */
    CHECK(h.hs.value[0] >= h.hs.lo);
    CHECK(h_says("Restore:        load the backup file on FILES of the Hitec "
                 "programmer and write it with WRITE n VALUES"));
    CHECK(!h_says("RESTORE on the programming page"));
}

TEST_CASE(a_bench_range_short_of_the_pulse_model_is_refused_before_a_drive)
{
    /* STANDARD PWM, 1000 to 2000us: the D series model's 850 and 2150us lie
     * outside.  1 under and 1 past the model's pulses fail; exactly at them
     * the run goes on. */
    static const uint16_t k[][2] = { { 851u, 2200u }, { 800u, 2149u },
                                     { 1000u, 2000u } };
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        h_init_d(0u, true);
        h.cfg.min_us = k[i][0];
        h.cfg.max_us = k[i][1];
        CHECK(h_start());
        h_run_for(H_RUN_MS);
        CHECK_EQ(h.run.verdict, SERVO_LIMRUN_FAIL);
        CHECK_EQ(h.run.why, SERVO_LIMRUN_WHY_RANGE);
        CHECK(!h.driven);
        CHECK_EQ(h.write_ops, 0u);
    }
    h_init_d(0u, true);
    h.cfg.min_us = 850u;
    h.cfg.max_us = 2150u;
    CHECK(h_start());
    h_run_for(H_RUN_MS);
    CHECK_EQ(h.run.verdict, SERVO_LIMRUN_PASS);
}

TEST_CASE(a_run_across_the_32_bit_ms_wrap_passes)
{
    h_init_d(0xFFFFFFFFu - 9000u, true);
    CHECK(h_start());
    h_run_for(H_RUN_MS);
    CHECK(h.now < 0x80000000u);       /* the clock wrapped inside the run */
    CHECK_EQ(h.run.verdict, SERVO_LIMRUN_PASS);
    CHECK(h.hs.value[0] < 4470u && h.hs.value[1] < 4470u);
}

/* A write the programmer never answers: the run waits SERVO_LIMRUN_PROG_MS
 * for it, the D series' save and reboot inside that, and not a 1ms
 * longer; it then ends ABORTED with the output off. */
TEST_CASE(a_write_never_answered_ends_at_the_programmer_cap)
{
    h_init_d(0u, true);
    CHECK(h_start());
    for (uint32_t k = 0; k < H_RUN_MS && h.op != H_OP_WRITE; ++k) {
        h_tick();
    }
    CHECK_EQ(h.op, H_OP_WRITE);
    CHECK_EQ(h.run.phase, SERVO_LIMRUN_PH_WRITE);
    const uint32_t at = h.run.phase_ms;
    h.prog_ms = 0xFFFFFFFFu;            /* the answer never comes */
    while (h.now - at < SERVO_LIMRUN_PROG_MS - 10u) {
        h_tick();
    }
    CHECK(servo_limrun_running(&h.run));
    CHECK_EQ(h.run.phase, SERVO_LIMRUN_PH_WRITE);
    while (h.now - at < SERVO_LIMRUN_PROG_MS + 10u) {
        h_tick();
    }
    CHECK(!servo_limrun_running(&h.run));
    CHECK_EQ(h.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(h.run.why, SERVO_LIMRUN_WHY_PROG);
    CHECK_EQ(h.offs, 1u);
}

TEST_CASE(the_family_pulses_are_the_hitec_model_and_none_for_kst)
{
    uint16_t p[3] = { 1u, 2u, 3u };
    CHECK(!servo_limrun_family_pulses(SERVO_LIMRUN_FAM_KST, p));
    CHECK_EQ(p[0], 1u);
    CHECK(!servo_limrun_family_pulses(SERVO_LIMRUN_FAM_COUNT, p));
    CHECK(servo_limrun_family_pulses(SERVO_LIMRUN_FAM_HITEC_D, p));
    CHECK_EQ(p[SERVO_LIMRUN_P_NEUTRAL], 15000u);
    CHECK_EQ(p[SERVO_LIMRUN_P_LOWER], 8500u);
    CHECK_EQ(p[SERVO_LIMRUN_P_UPPER], 21500u);
    CHECK(servo_limrun_family_pulses(SERVO_LIMRUN_FAM_HITEC_C, p));
    CHECK_EQ(p[SERVO_LIMRUN_P_LOWER], 9000u);
    CHECK_EQ(p[SERVO_LIMRUN_P_UPPER], 21000u);
}

int main(void)
{
    RUN(a_d_series_run_lowers_0xb2_toward_0xc2_for_a_bound_left_side);
    RUN(a_classic_run_writes_the_cell_of_the_bound_side);
    RUN(set_pulses_is_refused_with_a_hitec_family);
    RUN(no_servo_read_ends_not_hitec);
    RUN(the_report_names_left_end_0xb2_and_cell_0x14_by_family);
    RUN(the_mapping_search_finds_the_register_on_a_ccw_classic_servo);
    RUN(restore_is_offered_after_a_fail_with_the_values_before_the_run);
    RUN(a_bench_range_short_of_the_pulse_model_is_refused_before_a_drive);
    RUN(a_run_across_the_32_bit_ms_wrap_passes);
    RUN(a_write_never_answered_ends_at_the_programmer_cap);
    RUN(the_family_pulses_are_the_hitec_model_and_none_for_kst);
    return test_summary("servo_limrun_hitec");
}
