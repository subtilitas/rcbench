/*
 * The limit run, against a modelled servo whose travel its registers set.
 *
 * Under test: the read first and the search from NEUTRAL to the servo's own
 * pulse limits, never past them; the bench's range refused; SERVO PULSES
 * at 1000-2000 and its write before anything moves, and the share from the
 * pulses written; the two writes a side, the overset from the first bound
 * pulse and the final value, with REVERSION off and on, on LEFT ANGLE and
 * on RIGHT ANGLE; the low stop from 920 to 1440us in 3us steps, every
 * run PASS; the overset left out near the pulse limit; a servo free
 * after the overset; a square-law servo that still binds (FAIL, RESTORE);
 * the smallest value; a servo that never binds; the write cap; a write
 * refused, and one lost on the line; the programmer's other failures.
 *
 * The rig is servo_limrun_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_limrun_rig.h"

/* Where a linear servo's horn stands at full travel for value @p v on
 * @p side, us: NEUTRAL 1500.2us, 4us a count. */
static float full_us(int side, uint16_t v)
{
    const float n = (float)NEUTRAL_DUS / 10.0f;
    return side == SERVO_LIMRUN_HI ? n + 4.0f * (float)v
                                   : n - 4.0f * (float)v;
}

/* --- the read first ------------------------------------------------------ */

/* Nothing drives the servo before the read session is over: CONNECT, the
 * read and LEAVE come first, at PULSE CENTRE. */
static void read_before_drive(void)
{
    CHECK(g.driven);
    CHECK_EQ(g.connects_at_drive, 1u);
    CHECK_EQ(g.leaves_at_drive, 1u);
}

/* --- convergence --------------------------------------------------------- */

static void converges(bool reversion)
{
    rig_init(0u, reversion);
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.state, SERVO_LIMRUN_DONE);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NONE);
    read_before_drive();
    /* Searched from NEUTRAL out to PULSE LOWER and PULSE UPPER, never past
     * them: the final check, free, reaches both exactly. */
    CHECK_EQ(g.run.ctr_us, 1500u);
    CHECK_EQ(g.run.end_us[SERVO_LIMRUN_LO], 900u);
    CHECK_EQ(g.run.end_us[SERVO_LIMRUN_HI], 2100u);
    CHECK_EQ(g.cmd_lo, 900u);
    CHECK_EQ(g.cmd_hi, 2100u);
    /* Which register limits which side: found, and as the model has it. */
    const int lo_reg = reversion ? SERVO_LIMRUN_RIGHT : SERVO_LIMRUN_LEFT;
    CHECK_EQ(g.run.reg_side[lo_reg], SERVO_LIMRUN_LO);
    CHECK_EQ(g.run.reg_side[1 - lo_reg], SERVO_LIMRUN_HI);
    const uint16_t left = reg_get(KST_F_LEFT_ANGLE);
    const uint16_t right = reg_get(KST_F_RIGHT_ANGLE);
    CHECK_EQ(g.run.cur[0], left);
    CHECK_EQ(g.run.cur[1], right);
    const uint16_t lo_v = reversion ? right : left;
    const uint16_t hi_v = reversion ? left : right;
    /* Two writes a side: the overset from the share at the first search's
     * first bound pulse, the final value from the share the overset's
     * search found free. */
    for (int s = 0; s < 2; ++s) {
        const int32_t span = s == SERVO_LIMRUN_LO ? 6002 : 5998;
        CHECK(g.run.first[s].bound);
        CHECK_EQ(g.run.d0_dus[s],
                 servo_limrun_dist(&g.run.servo, (uint8_t)s,
                                   g.run.first[s].free_us));
        CHECK_EQ(g.run.db_dus[s],
                 servo_limrun_dist(&g.run.servo, (uint8_t)s,
                                   g.run.first[s].bind_us));
        CHECK_EQ(g.run.db_dus[s], g.run.d0_dus[s] + 100);
        CHECK_EQ(servo_limrun_span(&g.run.servo, (uint8_t)s), span);
        CHECK_EQ(g.run.v1[s],
                 servo_limrun_overset(150u, g.run.db_dus[s], span, 1u));
        CHECK(g.run.v1[s] < 150u);
        CHECK(g.run.d1_dus[s] > g.run.d0_dus[s]);
        CHECK_EQ(g.run.v2[s],
                 servo_limrun_scaled(g.run.v1[s], g.run.d1_dus[s], span, 1u));
    }
    CHECK_EQ(lo_v, g.run.v2[SERVO_LIMRUN_LO]);
    CHECK_EQ(hi_v, g.run.v2[SERVO_LIMRUN_HI]);
    /* The stops at 1150 and 1880us: 350.2us is 87.55 counts, 379.8us
     * 94.95; the values land inside them, within the search's 10us. */
    CHECK_EQ(lo_v, 86u);
    CHECK_EQ(hi_v, 94u);
    CHECK(full_us(SERVO_LIMRUN_LO, lo_v) > 1150.0f);
    CHECK(full_us(SERVO_LIMRUN_HI, hi_v) < 1880.0f);
    /* Writes: counted as the programmer saw them, under the cap.  REVERSION
     * off: LEFT ANGLE's overset finds the mapping, then both registers in
     * one write, then RIGHT ANGLE's final value.  On: the first write went
     * to the other side's register and comes back, two values more. */
    CHECK_EQ(g.run.writes, g.values);
    CHECK_EQ(g.run.write_ops, g.write_ops);
    CHECK_EQ(g.run.writes, reversion ? 6u : 4u);
    CHECK_EQ(g.run.write_ops, reversion ? 4u : 3u);
    CHECK(g.run.writes <= SERVO_LIMRUN_WRITES_MAX);
    CHECK_EQ(g.pulse_ops, 0u);
    CHECK_EQ(g.connects, g.run.sessions);
    CHECK_EQ(g.leaves, g.connects);
    /* The read session writes nothing: one write a session after it. */
    CHECK_EQ(g.run.sessions, g.run.write_ops + 1u);
    /* Ended well: the servo let go, the output left on. */
    CHECK_EQ(g.offs, 0u);
    CHECK_EQ(g.releases, 1u);
    CHECK(g.run.after[0].done && !g.run.after[0].bound);
    CHECK(g.run.after[1].done && !g.run.after[1].bound);
    CHECK(!servo_limrun_restore(&g.run));
    CHECK_EQ(g.opens, 1u);
    CHECK_EQ(g.ends, 1u);
    CHECK(strstr(g.report, "RCBENCH SERVO LIMIT REPORT") != NULL);
    uint16_t was_l = 0u, was_r = 0u;
    CHECK(servo_limrun_original(&g.run, &was_l, &was_r));
    CHECK_EQ(was_l, 150u);
    CHECK_EQ(was_r, 150u);
    CHECK(servo_limrun_original(&g.run, NULL, NULL));
}

TEST_CASE(both_sides_converge_with_reversion_off)
{
    converges(false);
}

TEST_CASE(both_sides_converge_with_reversion_on)
{
    converges(true);
}

/* servo_limrun.h's worked example: the low stop at 1000us, the first
 * search free at 1000us and bound at 990us.  The share at the bound
 * pulse 510.2 / 600.2, v1 = ceil(150 x 0.850 x 1.03) = 132; the overset's
 * search is free to 930us, v2 = floor(132 x 570.2 / 600.2) = 125. */
TEST_CASE(the_worked_example)
{
    for (int rev = 0; rev < 2; ++rev) {
        rig_init(0u, rev != 0);
        g.servo.cfg.stop_lo_us = 1000u;
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
        CHECK_EQ(g.run.first[SERVO_LIMRUN_LO].free_us, 1000u);
        CHECK_EQ(g.run.first[SERVO_LIMRUN_LO].bind_us, 990u);
        CHECK_EQ(g.run.d0_dus[SERVO_LIMRUN_LO], 5002);
        CHECK_EQ(g.run.db_dus[SERVO_LIMRUN_LO], 5102);
        CHECK_EQ(g.run.v1[SERVO_LIMRUN_LO], 132u);
        CHECK_EQ(g.run.d1_dus[SERVO_LIMRUN_LO], 5702);
        CHECK_EQ(g.run.v2[SERVO_LIMRUN_LO], 125u);
        CHECK_EQ(reg_get(rev ? KST_F_RIGHT_ANGLE : KST_F_LEFT_ANGLE), 125u);
        CHECK(full_us(SERVO_LIMRUN_LO, 125u) > 1000.0f);
        CHECK(full_us(SERVO_LIMRUN_LO, 126u) < 1000.0f);
        CHECK(strstr(g.report, "share free 0.833, bound 0.850; overset 132, "
                               "share 0.950; final 125") != NULL);
    }
}

/* Near the pulse limit the overset is left out and the first search
 * counts as its: with the bound pulse's share above about 0.97 the overset
 * would not lie below v0; a little further in the stop would move less
 * than 20us, a move a search cannot tell from none.  One write then,
 * floor(v0 x f0), f0 the share the first search found free. */
TEST_CASE(the_overset_is_left_out_near_the_pulse_limit)
{
    static const struct {
        uint16_t stop_lo, free_us;
        int32_t d0, db;
        uint16_t overset, v2;
    } k[] = {
        { 915u, 920u, 5802, 5902, 150u, 145u }, /* v1 not below v0     */
        { 940u, 940u, 5602, 5702, 147u, 140u }, /* the move 11.4us    */
    };
    for (size_t i = 0; i < 2u; ++i) {
        rig_init(0u, false);
        g.servo.cfg.stop_lo_us = k[i].stop_lo;
        g.servo.cfg.stop_hi_us = 2150u;     /* the high side free */
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
        CHECK_EQ(g.run.first[SERVO_LIMRUN_LO].free_us, k[i].free_us);
        CHECK_EQ(g.run.d0_dus[SERVO_LIMRUN_LO], k[i].d0);
        CHECK_EQ(g.run.db_dus[SERVO_LIMRUN_LO], k[i].db);
        CHECK_EQ(servo_limrun_overset(150u, k[i].db, 6002, 1u), k[i].overset);
        CHECK_EQ(g.run.v1[SERVO_LIMRUN_LO], 0u);
        CHECK_EQ(g.run.d1_dus[SERVO_LIMRUN_LO], 0);
        CHECK_EQ(g.run.v2[SERVO_LIMRUN_LO], k[i].v2);
        CHECK_EQ(reg_get(KST_F_LEFT_ANGLE), k[i].v2);
        CHECK_EQ(reg_get(KST_F_RIGHT_ANGLE), 150u);
        CHECK_EQ(g.run.writes, 1u);
        CHECK(strstr(g.report, "overset -, share -") != NULL);
    }
    /* Further in, the overset is written: bound at 950us, the stop's
     * estimate, the last free pulse at 960us, moves 540.2us x 8 / 142 =
     * 30.4us. */
    CHECK_EQ(servo_limrun_overset(150u, 5502, 6002, 1u), 142u);
}

/* The low stop from 920 to 1440us in 3us steps -- 580us to 60us from
 * NEUTRAL -- with the servo's pulses at 900 and 2100us and with SET
 * 1000-2000 (stops from 1010us): every run passes, the low end's full
 * travel inside the stop.  The overset from the first bound pulse keeps
 * the stop inside the pulse limit by 3% of the share. */
TEST_CASE(every_stop_position_passes)
{
    for (int set = 0; set < 2; ++set) {
        unsigned runs = 0u;

        for (uint16_t stop = set ? 1010u : 920u; stop <= 1440u; stop += 3u) {
            rig_init(0u, false);
            g.cfg.set_pulses = set != 0;
            g.servo.cfg.stop_lo_us = stop;
            g.servo.cfg.stop_hi_us = 2150u;
            const float depth = (float)g.servo.cfg.bind_us * 0.15f
                                / (g.servo.cfg.stall_a - g.servo.cfg.free_a)
                                + 0.5f;
            CHECK(start());
            run_for(RUN_MS);
            ++runs;
            if (g.run.verdict != SERVO_LIMRUN_PASS) {
                T_FAIL("stop %u us, SET %d: verdict %d why %d", stop, set,
                       (int)g.run.verdict, (int)g.run.why);
                continue;
            }
            /* Inside the stop by the model's own resolution: a push of
             * up to 45us x 0.15A / 2.28A = 3.0us draws less than the
             * +0.15A a bind needs, and the command rounds to 0.5us. */
            CHECK(sim_horn_us(g.reg, set ? 1000u : 900u, g.upc, false)
                  > (float)stop - depth);
            CHECK(g.run.writes <= SERVO_LIMRUN_WRITES_MAX);
        }
        CHECK_EQ(runs, set ? 144u : 174u);
    }
}

/* Free after the overset: the stop the overset was to keep in reach is not
 * there -- the servo's travel is not linear in the register.  FAIL, no
 * final value, RESTORE offered. */
TEST_CASE(free_after_the_overset_fails)
{
    for (int rev = 0; rev < 2; ++rev) {
        rig_init(0u, rev != 0);
        g.shrink = 0.75f;           /* 91 counts: 273us, short of 350 */
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NO_STOP);
        CHECK_EQ(servo_limrun_why_str(&g.run), SERVO_STR_L_WHY_NO_STOP);
        CHECK_EQ(g.run.v2[SERVO_LIMRUN_LO], 0u);
        CHECK(servo_limrun_wrote(&g.run));
        CHECK(servo_limrun_restore(&g.run));
        CHECK_EQ(g.offs, 0u);
        CHECK(strstr(g.report, "no stop after the overset write") != NULL);
        CHECK(strstr(g.report, "RESTORE") != NULL);
    }
}

/* A square-law servo: the share the search measures is not the share of
 * the register.  The final value still binds: FAIL at the check, no write
 * after it, RESTORE offered. */
TEST_CASE(a_servo_that_is_not_linear_fails_at_the_check)
{
    for (int rev = 0; rev < 2; ++rev) {
        rig_init(0u, rev != 0);
        g.square = true;
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_STILL_BINDS);
        CHECK(g.run.after[SERVO_LIMRUN_LO].bound
              || g.run.after[SERVO_LIMRUN_HI].bound);
        /* Two writes a side and the mapping's, no correction after. */
        CHECK_EQ(g.run.write_ops, rev ? 4u : 3u);
        CHECK(servo_limrun_restore(&g.run));
        CHECK_EQ(g.offs, 0u);
        CHECK(strstr(g.report, "an end binds after programming") != NULL);
        CHECK(strstr(g.report, "RESTORE") != NULL);
    }
}

/* One side binds: the other register keeps the servo's own value, also
 * where the mapping lowered it first (REVERSION on: LEFT ANGLE limits the
 * long pulses, the bound side is the short one).  The free side's search
 * runs to its pulse limit and no further. */
TEST_CASE(one_side_bound_leaves_the_other_register_as_it_was)
{
    for (int rev = 0; rev < 2; ++rev) {
        rig_init(0u, rev != 0);
        g.servo.cfg.stop_hi_us = 2150u;     /* past PULSE UPPER */
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
        const uint16_t keep = 150u;
        if (rev == 0) {
            CHECK_EQ(reg_get(KST_F_RIGHT_ANGLE), keep);
            CHECK(reg_get(KST_F_LEFT_ANGLE) < keep);
            CHECK_EQ(g.run.map_tries, 1u);
        } else {
            CHECK_EQ(reg_get(KST_F_LEFT_ANGLE), keep);
            CHECK(reg_get(KST_F_RIGHT_ANGLE) < keep);
            /* LEFT ANGLE tried first, and it moved nothing. */
            CHECK_EQ(g.run.map_tries, 2u);
        }
        CHECK(!g.run.first[SERVO_LIMRUN_HI].bound);
        CHECK_EQ(g.run.first[SERVO_LIMRUN_HI].limit_us, 2100u);
        CHECK(!g.run.after[SERVO_LIMRUN_HI].bound);
        CHECK_EQ(g.cmd_hi, 2100u);
        CHECK(strstr(g.report, "free to 2100 us") != NULL);
    }
}

/* The smallest value the hard rules take: the overset and the final value
 * stop there, and a side that still binds fails as that, RESTORE named. */
TEST_CASE(a_servo_bound_at_the_smallest_value_fails_clearly)
{
    rig_init(0u, false);
    g.lo_raw = 100u;            /* 100 counts: 400us, past both stops */
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SMALLEST);
    CHECK_EQ(g.smallest, 100u);
    CHECK(g.run.writes <= SERVO_LIMRUN_WRITES_MAX);
    CHECK_EQ(g.offs, 0u);
    CHECK(servo_limrun_restore(&g.run));
    CHECK(strstr(g.report, "binds at the smallest angle") != NULL);
    CHECK(strstr(g.report, "RESTORE") != NULL);
}

/* LEFT ANGLE at the smallest value already: the mapping starts on RIGHT
 * ANGLE; with both there and the end still bound, FAIL without a write. */
TEST_CASE(registers_at_the_smallest_value_already)
{
    rig_init(0u, false);
    g.lo_raw = 100u;
    reg_set(KST_F_LEFT_ANGLE, 100u);
    reg_set(KST_F_RIGHT_ANGLE, 100u);
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_SMALLEST);
    CHECK_EQ(g.write_ops, 0u);
    CHECK_EQ(g.connects, 1u);
    CHECK_EQ(g.leave_asks, 1u);     /* the read session's */
    CHECK(!servo_limrun_restore(&g.run));

    rig_init(0u, true);             /* LEFT at 60 limits the long pulses */
    g.lo_raw = 60u;
    reg_set(KST_F_LEFT_ANGLE, 60u);
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
    CHECK_EQ(g.run.map_tries, 2u);
    CHECK_EQ(g.run.reg_side[SERVO_LIMRUN_RIGHT], SERVO_LIMRUN_LO);
    CHECK_EQ(reg_get(KST_F_LEFT_ANGLE), 60u);
    CHECK_EQ(reg_get(KST_F_RIGHT_ANGLE), 86u);
}

/* Neither end binds out to the pulse limits: FREE, nothing written, the
 * read session the only one. */
TEST_CASE(a_servo_that_never_binds_is_not_programmed)
{
    rig_init(0u, false);
    g.servo.cfg.stop_lo_us = 850u;
    g.servo.cfg.stop_hi_us = 2150u;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FREE);
    CHECK_EQ(g.connects, 1u);
    CHECK_EQ(g.write_ops, 0u);
    CHECK_EQ(g.disarms, 1u);
    CHECK_EQ(g.cmd_lo, 900u);
    CHECK_EQ(g.cmd_hi, 2100u);
    CHECK(g.run.first[0].done && !g.run.first[0].bound);
    CHECK(g.run.first[1].done && !g.run.first[1].bound);
    CHECK(strstr(g.report, "neither end binds") != NULL);
    CHECK(strstr(g.report, "RESTORE") == NULL);
}

/* A free end not past NEUTRAL: the share is 0 or less, FAIL before any
 * write.  The search starts at NEUTRAL rounded to the microsecond and
 * takes three free positions for its baseline, so a real search does not
 * end there; the test moves NEUTRAL past the high end found (white box). */
TEST_CASE(a_free_end_not_past_neutral_fails_before_a_write)
{
    rig_init(0u, false);
    CHECK(start());
    CHECK(run_to(SERVO_LIMRUN_PH_SEARCH, RUN_MS));
    g.run.pulse_now[SERVO_LIMRUN_P_NEUTRAL] = 19000u;
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_BEFORE_NEUTRAL);
    CHECK_EQ(g.write_ops, 0u);
    CHECK(!servo_limrun_restore(&g.run));
    CHECK(strstr(g.report, "free end not past NEUTRAL") != NULL);
}

/* The INA3221's shunt fitted the other way round: the windows read
 * negative, a bound servo more negative.  The search reads the magnitude,
 * as the protections do, and finds both ends; the run programs them. */
TEST_CASE(a_reversed_shunt_finds_the_bound_ends)
{
    for (int prog = 0; prog < 2; ++prog) {
        rig_init(0u, false);
        g.neg = true;
        g.cfg.program = prog != 0;
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, prog ? SERVO_LIMRUN_PASS : SERVO_LIMRUN_FOUND);
        CHECK(g.run.first[SERVO_LIMRUN_LO].bound);
        CHECK(g.run.first[SERVO_LIMRUN_HI].bound);
        CHECK(g.run.first[SERVO_LIMRUN_LO].baseline_a > 0.0f);
        if (prog) {
            CHECK(full_us(SERVO_LIMRUN_LO, reg_get(KST_F_LEFT_ANGLE))
                  > 1150.0f);
            CHECK(full_us(SERVO_LIMRUN_HI, reg_get(KST_F_RIGHT_ANGLE))
                  < 1880.0f);
        }
    }
}

/* Search only on the programming port: the read, then the search out to
 * the servo's pulse limits; the endpoints, nothing written. */
TEST_CASE(search_only_reports_the_endpoints)
{
    rig_init(0u, false);
    g.cfg.program = false;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FOUND);
    CHECK_EQ(g.connects, 1u);
    CHECK_EQ(g.write_ops, 0u);
    read_before_drive();
    CHECK(g.run.first[0].bound && g.run.first[1].bound);
    CHECK(g.run.first[0].limit_us > 1150u && g.run.first[0].limit_us < 1200u);
    CHECK(g.run.first[1].limit_us < 1880u && g.run.first[1].limit_us > 1830u);
    CHECK(strstr(g.report, "search only") != NULL);
    CHECK(strstr(g.report, "Searched:       from 1500 us out to 900 us and "
                           "2100 us") != NULL);
    /* Nothing sent, so no backup: the line is left out. */
    CHECK(strstr(g.report, "Writes:         values written 0, writes "
                           "sent 0") != NULL);
    CHECK(strstr(g.report, "Backup:") == NULL);
}

/* The PWM pin: no read, no session, the search out to the bench's PULSE
 * MIN and PULSE MAX, past where a servo's own pulse limits would lie. */
TEST_CASE(the_pwm_pin_searches_out_to_min_and_max)
{
    rig_init(0u, false);
    rig_pin();
    g.servo.cfg.stop_lo_us = 850u;
    g.servo.cfg.stop_hi_us = 2150u;
    CHECK(start());
    CHECK_EQ(g.run.phase, SERVO_LIMRUN_PH_SEARCH);
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FREE);
    CHECK_EQ(g.connects, 0u);
    CHECK_EQ(g.disarms, 0u);
    CHECK_EQ(g.cmd_lo, MIN_US);
    CHECK_EQ(g.cmd_hi, MAX_US);
    CHECK(strstr(g.report, "Searched:       from 1500 us out to 800 us and "
                           "2200 us") != NULL);
    CHECK(strstr(g.report, "Servo pulses") == NULL);

    /* Bound ends: FOUND, the bench never switched. */
    rig_init(0u, false);
    rig_pin();
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FOUND);
    CHECK_EQ(g.connects, 0u);
    CHECK_EQ(g.disarms, 0u);

    /* Programming and the pulse write need the read: refused. */
    rig_init(0u, false);
    rig_pin();
    g.cfg.program = true;
    CHECK(!start());
    g.cfg.program = false;
    g.cfg.set_pulses = true;
    CHECK(!start());
    CHECK(!servo_limrun_running(&g.run));
}

/* --- the servo's pulses -------------------------------------------------- */

/* The bench's PULSE MIN to PULSE MAX narrower than the servo's PULSE LOWER
 * to PULSE UPPER, on either side: refused after the read, the bench left
 * SAFE, nothing driven, both ranges named. */
TEST_CASE(a_bench_range_narrower_than_the_servos_is_refused)
{
    for (int side = 0; side < 2; ++side) {
        rig_init(0u, side != 0);
        if (side == 0) {
            g.cfg.min_us = 950u;
        } else {
            g.cfg.max_us = 2050u;
        }
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_RANGE);
        CHECK(!g.driven);
        CHECK_EQ(g.connects, 1u);
        CHECK_EQ(g.write_ops, 0u);
        CHECK_EQ(g.leave_asks, 1u);
        CHECK_EQ(g.arms, 0u);
        CHECK(g.safe);
        CHECK(!g.live);
        CHECK_EQ(g.offs, 0u);
        CHECK(!servo_limrun_restore(&g.run));
        char want[96];
        snprintf(want, sizeof(want),
                 "servo pulses 900.0-2100.0 us outside PULSE MIN/MAX "
                 "%u-%u us", g.cfg.min_us, g.cfg.max_us);
        CHECK(strstr(g.report, want) != NULL);
        CHECK(strstr(g.report, "servo pulses outside MIN/MAX") != NULL);
        /* Nothing searched: no line says where. */
        CHECK(strstr(g.report, "Searched:") == NULL);
    }
    /* Equal to them is inside. */
    rig_init(0u, false);
    g.cfg.min_us = 900u;
    g.cfg.max_us = 2100u;
    g.cfg.program = false;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FOUND);
}

/* NEUTRAL not between PULSE LOWER and PULSE UPPER at the read: no search. */
TEST_CASE(a_neutral_outside_the_pulse_limits_is_refused)
{
    rig_init(0u, false);
    rig_pulses(15500, 21000);
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NEUTRAL);
    CHECK(!g.driven);
    CHECK_EQ(g.write_ops, 0u);
    CHECK(strstr(g.report, "NEUTRAL outside LOWER/UPPER") != NULL);
}

/* SERVO PULSES at 1000-2000: PULSE LOWER and PULSE UPPER written in the
 * read session before anything moves, and read back; the search runs out
 * to them; the angles are programmed after it as without it, the share
 * from the pulses written.  REVERSION on, the most a run writes: the two
 * pulses, two a side and the mapping's two. */
TEST_CASE(set_1000_2000_writes_the_pulses_before_the_search)
{
    for (int rev = 0; rev < 2; ++rev) {
        rig_init(0u, rev != 0);
        g.cfg.set_pulses = true;
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
        read_before_drive();
        CHECK_EQ(g.pulse_ops_at_drive, 1u);
        CHECK_EQ(g.pulse_ops, 1u);
        uint16_t p[3];
        pulses_of(p);
        CHECK_EQ(p[0], NEUTRAL_DUS);
        CHECK_EQ(p[1], 10000u);
        CHECK_EQ(p[2], 20000u);
        CHECK_EQ(g.run.end_us[SERVO_LIMRUN_LO], 1000u);
        CHECK_EQ(g.run.end_us[SERVO_LIMRUN_HI], 2000u);
        /* The share from the pulses written, not those read: 1000.0 and
         * 2000.0us, spans of 500.2 and 499.8us. */
        for (int s = 0; s < 2; ++s) {
            const int32_t span = s == SERVO_LIMRUN_LO ? 5002 : 4998;
            const int32_t n = NEUTRAL_DUS;
            const int32_t b = (int32_t)g.run.first[s].bind_us * 10;
            CHECK_EQ(g.run.db_dus[s], s == SERVO_LIMRUN_LO ? n - b : b - n);
            CHECK_EQ(g.run.v1[s],
                     servo_limrun_overset(150u, g.run.db_dus[s], span, 1u));
            CHECK_EQ(g.run.v2[s],
                     servo_limrun_scaled(g.run.v1[s], g.run.d1_dus[s], span,
                                         1u));
        }
        CHECK_EQ(g.cmd_lo, 1000u);
        CHECK_EQ(g.cmd_hi, 2000u);
        CHECK_EQ(g.run.writes, rev ? 8u : 6u);
        CHECK(g.run.writes <= SERVO_LIMRUN_WRITES_MAX);
        CHECK_EQ(g.run.writes, g.values);
        CHECK(full_us(SERVO_LIMRUN_LO, reg_get(rev ? KST_F_RIGHT_ANGLE
                                                   : KST_F_LEFT_ANGLE))
              > 1150.0f);
        CHECK(strstr(g.report, "Pulse write:    PULSE LOWER 900.0 -> 1000.0 "
                               "us, PULSE UPPER 2100.0 -> 2000.0 us") != NULL);
        /* The values before the run, for RESTORE. */
        CHECK_EQ(g.run.servo.pulse_dus[SERVO_LIMRUN_P_LOWER], LOWER_DUS);
        CHECK_EQ(g.run.servo.pulse_dus[SERVO_LIMRUN_P_UPPER], UPPER_DUS);
    }
}

/* At 1000.0 and 2000.0 already: no pulse write. */
TEST_CASE(set_1000_2000_on_a_servo_there_already_writes_nothing)
{
    rig_init(0u, false);
    rig_pulses(10000, 20000);
    g.cfg.set_pulses = true;
    g.cfg.program = false;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FOUND);
    CHECK_EQ(g.pulse_ops, 0u);
    CHECK_EQ(g.write_ops, 0u);
    CHECK_EQ(g.run.writes, 0u);
    CHECK(strstr(g.report, "Pulse write:    PULSE LOWER 1000.0 -> 1000.0 us")
          != NULL);
}

/* Search only with SERVO PULSES 1000-2000: the pulses are the only write. */
TEST_CASE(search_only_with_set_1000_2000_writes_only_the_pulses)
{
    rig_init(0u, false);
    g.cfg.set_pulses = true;
    g.cfg.program = false;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FOUND);
    CHECK_EQ(g.write_ops, 1u);
    CHECK_EQ(g.pulse_ops, 1u);
    CHECK_EQ(g.run.writes, 2u);
    CHECK_EQ(reg_get(KST_F_LEFT_ANGLE), 150u);
    CHECK_EQ(reg_get(KST_F_RIGHT_ANGLE), 150u);
    CHECK_EQ(g.connects, 1u);
    CHECK(strstr(g.report, "search only; PULSE LOWER") != NULL);
    CHECK(!servo_limrun_restore(&g.run));
}

/* NEUTRAL outside 1000-2000 after the write: FAIL before anything moves,
 * RESTORE offered for the pulses written. */
TEST_CASE(set_1000_2000_with_neutral_outside_fails_before_the_search)
{
    rig_init(0u, false);
    uint16_t raw = 0u;
    CHECK(kst_field_raw_from_display(KST_F_NEUTRAL, 20500, &raw));
    reg_set(KST_F_NEUTRAL, raw);
    g.cfg.set_pulses = true;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NEUTRAL);
    CHECK(!g.driven);
    CHECK_EQ(g.pulse_ops, 1u);
    CHECK_EQ(g.leave_asks, 1u);
    CHECK(servo_limrun_wrote(&g.run));
    CHECK(servo_limrun_restore(&g.run));
    CHECK(g.safe);
    CHECK(strstr(g.report, "RESTORE") != NULL);
}

/* The pulse write refused, or lost on the line: ABORTED before anything
 * moves. */
TEST_CASE(a_pulse_write_refused_or_lost_aborts_before_the_search)
{
    rig_init(0u, false);
    g.cfg.set_pulses = true;
    g.refuse_write = 1u;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_WRITE);
    CHECK(!g.driven);
    CHECK(!servo_limrun_wrote(&g.run));
    CHECK(servo_limrun_restore(&g.run));     /* sent: it may have landed */
    CHECK(strstr(g.report, "RESTORE") != NULL);

    rig_init(0u, false);
    g.cfg.set_pulses = true;
    g.lose_write = 1u;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_OTHER_VALUES);
    CHECK(!g.driven);
}

/* The read fails, or the programmer does not answer it: the run ends
 * before anything is driven. */
TEST_CASE(a_read_that_fails_ends_the_run_before_a_drive)
{
    rig_init(0u, false);
    g.not_kst = true;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_NOT_KST);
    CHECK_EQ(g.write_ops, 0u);
    CHECK_EQ(g.offs, 0u);
    CHECK(!g.driven);
    CHECK(strstr(g.report, "no KST or Chaservo servo read") != NULL);

    rig_init(0u, false);
    CHECK(start());
    CHECK(run_to(SERVO_LIMRUN_PH_CONNECT, RUN_MS));
    g.op = OP_NONE;
    servo_limrun_refused(&g.run, g.now);
    run_for(100u);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_PROG);
    CHECK(!g.driven);

    rig_init(0u, false);
    g.prog_silent = true;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_PROG);
    CHECK(!g.driven);
    CHECK_EQ(g.offs, 1u);
}

/* --- the write cap ------------------------------------------------------- */

/* Told it has written 7 values once the first side is mapped, the run
 * needs 2 more for its next write: FAIL at the cap, never past it.  The
 * cap is reached exactly by REVERSION on with SERVO PULSES 1000-2000
 * (set_1000_2000_writes_the_pulses_before_the_search). */
TEST_CASE(the_write_cap_is_never_passed)
{
    rig_init(0u, false);
    CHECK(start());
    CHECK(run_to(SERVO_LIMRUN_PH_PROBE, RUN_MS));
    g.run.writes = SERVO_LIMRUN_WRITES_MAX - 1u;
    g.values = g.run.writes;
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_FAIL);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_WRITE_CAP);
    CHECK_EQ(g.run.writes, SERVO_LIMRUN_WRITES_MAX - 1u);
    CHECK_EQ(g.write_ops, 1u);
    CHECK(servo_limrun_restore(&g.run));
    CHECK(strstr(g.report, "write cap of 8 values reached") != NULL);

    /* Room for exactly the 2: written up to the cap, then the next value
     * fails. */
    rig_init(0u, false);
    CHECK(start());
    CHECK(run_to(SERVO_LIMRUN_PH_PROBE, RUN_MS));
    g.run.writes = SERVO_LIMRUN_WRITES_MAX - 2u;
    g.values = g.run.writes;
    run_for(RUN_MS);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_WRITE_CAP);
    CHECK_EQ(g.run.writes, SERVO_LIMRUN_WRITES_MAX);
    CHECK_EQ(g.write_ops, 2u);

    /* One value more of room: the run ends at the cap, PASS. */
    rig_init(0u, false);
    CHECK(start());
    CHECK(run_to(SERVO_LIMRUN_PH_PROBE, RUN_MS));
    g.run.writes = SERVO_LIMRUN_WRITES_MAX - 3u;
    g.values = g.run.writes;
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_PASS);
    CHECK_EQ(g.run.writes, SERVO_LIMRUN_WRITES_MAX);
}

/* --- the programmer's failures ------------------------------------------- */

TEST_CASE(a_refused_write_aborts_and_offers_restore)
{
    rig_init(0u, false);
    g.refuse_write = 2u;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_WRITE);
    CHECK_EQ(g.offs, 1u);
    CHECK(servo_limrun_wrote(&g.run));
    CHECK(servo_limrun_restore(&g.run));
    CHECK(strstr(g.report, "a write refused or lost") != NULL);
    CHECK(strstr(g.report, "RESTORE") != NULL);

    /* The first write fails once sent: no value is known written, but
     * part of it may have landed, so RESTORE is offered, and the
     * programmer holds the values before the run. */
    rig_init(0u, false);
    g.refuse_write = 1u;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_WRITE);
    CHECK(!servo_limrun_wrote(&g.run));
    CHECK(servo_limrun_restore(&g.run));
    CHECK_EQ(g.run.write_ops, 1u);
    CHECK(strstr(g.report, "Writes:         values written 0, writes "
                           "sent 1") != NULL);
    CHECK(strstr(g.report, "Backup:") != NULL);
    CHECK(strstr(g.report, "RESTORE") != NULL);

    /* The first write refused before it is sent: nothing written, no
     * backup, no RESTORE line. */
    rig_init(0u, false);
    CHECK(start());
    CHECK(run_to(SERVO_LIMRUN_PH_WRITE, RUN_MS));
    g.op = OP_NONE;
    servo_limrun_refused(&g.run, g.now);
    run_for(RUN_MS);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_WRITE);
    CHECK_EQ(g.run.write_ops, 0u);
    CHECK(!servo_limrun_restore(&g.run));
    CHECK(strstr(g.report, "Backup:") == NULL);
    CHECK(strstr(g.report, "RESTORE") == NULL);
}

/* A write the line lost: the read back holds the old values. */
TEST_CASE(a_write_lost_on_the_line_aborts)
{
    rig_init(0u, false);
    g.lose_write = 3u;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_OTHER_VALUES);
    CHECK_EQ(g.offs, 1u);
    CHECK_EQ(g.run.write_ops, 3u);
    CHECK(strstr(g.report, "RESTORE") != NULL);
}

/* The servo changed between sessions. */
TEST_CASE(other_values_at_a_later_connect_abort)
{
    rig_init(0u, false);
    CHECK(start());
    CHECK(run_to(SERVO_LIMRUN_PH_PROBE, RUN_MS));
    reg_set(KST_F_RIGHT_ANGLE, 120u);
    run_for(RUN_MS);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_OTHER_VALUES);
}

/* A later session reads the same two values from another servo, or from
 * one set otherwise: another type, REVERSION switched, another range,
 * another PULSE LOWER.  The run ends before it writes again, and RESTORE
 * is offered. */
TEST_CASE(another_servo_at_a_later_connect_aborts)
{
    for (int k = 0; k < 5; ++k) {
        rig_init(0u, false);
        g.change_at = 3u;          /* the second writing session */
        g.change_kind = k;
        CHECK(start());
        run_for(RUN_MS);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
        CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_OTHER_VALUES);
        CHECK_EQ(g.connects, 3u);
        CHECK_EQ(g.write_ops, 1u);
        CHECK_EQ(g.offs, 1u);
        CHECK(strstr(g.report, "RESTORE") != NULL);
    }
}

TEST_CASE(a_failed_leave_aborts)
{
    rig_init(0u, false);
    g.leave_fails = true;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_PROG);
    CHECK_EQ(g.offs, 1u);
    /* The read session's LEAVE: nothing written, nothing driven. */
    CHECK_EQ(g.connects, 1u);
    CHECK_EQ(g.write_ops, 0u);
    CHECK(!g.driven);
    CHECK(!servo_limrun_wrote(&g.run));
    CHECK(strstr(g.report, "RESTORE") == NULL);

    /* The LEAVE after the first write: the value stays, RESTORE named. */
    rig_init(0u, false);
    g.leave_fail_at = 2u;
    CHECK(start());
    run_for(RUN_MS);
    CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
    CHECK_EQ(g.run.why, SERVO_LIMRUN_WHY_PROG);
    CHECK_EQ(g.write_ops, 1u);
    CHECK(servo_limrun_wrote(&g.run));
    CHECK(strstr(g.report, "RESTORE") != NULL);
}

/* A CONNECT, write or LEAVE the programmer does not take ends the run. */
TEST_CASE(a_step_the_programmer_refuses_aborts)
{
    static const struct {
        servo_limrun_phase_t ph;
        servo_limrun_why_t why;
    } k[] = {
        { SERVO_LIMRUN_PH_CONNECT, SERVO_LIMRUN_WHY_PROG },
        { SERVO_LIMRUN_PH_WRITE,   SERVO_LIMRUN_WHY_WRITE },
        { SERVO_LIMRUN_PH_LEAVE,   SERVO_LIMRUN_WHY_PROG },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        rig_init(0u, false);
        CHECK(start());
        CHECK(run_to(k[i].ph, RUN_MS));
        g.op = OP_NONE;
        servo_limrun_refused(&g.run, g.now);
        CHECK_EQ(g.run.verdict, SERVO_LIMRUN_ABORTED);
        CHECK_EQ(g.run.why, k[i].why);
        run_for(100u);
        CHECK_EQ(g.offs, 1u);
    }
    /* Outside the programmer's phases it changes nothing. */
    rig_init(0u, false);
    rig_pin();
    CHECK(start());
    run_for(1000u);
    servo_limrun_refused(&g.run, g.now);
    CHECK(servo_limrun_running(&g.run));
}

int main(void)
{
    RUN(both_sides_converge_with_reversion_off);
    RUN(both_sides_converge_with_reversion_on);
    RUN(the_worked_example);
    RUN(the_overset_is_left_out_near_the_pulse_limit);
    RUN(every_stop_position_passes);
    RUN(free_after_the_overset_fails);
    RUN(a_servo_that_is_not_linear_fails_at_the_check);
    RUN(one_side_bound_leaves_the_other_register_as_it_was);
    RUN(a_servo_bound_at_the_smallest_value_fails_clearly);
    RUN(registers_at_the_smallest_value_already);
    RUN(a_servo_that_never_binds_is_not_programmed);
    RUN(a_free_end_not_past_neutral_fails_before_a_write);
    RUN(a_reversed_shunt_finds_the_bound_ends);
    RUN(search_only_reports_the_endpoints);
    RUN(the_pwm_pin_searches_out_to_min_and_max);
    RUN(a_bench_range_narrower_than_the_servos_is_refused);
    RUN(a_neutral_outside_the_pulse_limits_is_refused);
    RUN(set_1000_2000_writes_the_pulses_before_the_search);
    RUN(set_1000_2000_on_a_servo_there_already_writes_nothing);
    RUN(search_only_with_set_1000_2000_writes_only_the_pulses);
    RUN(set_1000_2000_with_neutral_outside_fails_before_the_search);
    RUN(a_pulse_write_refused_or_lost_aborts_before_the_search);
    RUN(a_read_that_fails_ends_the_run_before_a_drive);
    RUN(the_write_cap_is_never_passed);
    RUN(a_refused_write_aborts_and_offers_restore);
    RUN(a_write_lost_on_the_line_aborts);
    RUN(other_values_at_a_later_connect_abort);
    RUN(another_servo_at_a_later_connect_aborts);
    RUN(a_failed_leave_aborts);
    RUN(a_step_the_programmer_refuses_aborts);
    return test_summary("servo_limrun");
}
