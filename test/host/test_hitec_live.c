/*
 * The live loop's core (protocols/hitec/hitec_live.c): the D series
 * target mapping, the pass's command, its judge and FIND STOP.
 *
 * Under test:
 *  - the mapping at targets 0, 400, 3000, 5600 and 6000 on the D485HW's
 *    stored ends, back and forth, the clamp at 0 and at the 16383 edge,
 *    the stored ends taken from an image;
 *  - the reach, the pulse width of a target and the 1000us share;
 *  - the slew at 149, 150 and 151 counts, the command clamped to the reach,
 *    a start 50 and 51 counts outside the reach;
 *  - the lag at 50, 150 and 151 counts, over 2 passes, a row broken, a
 *    command moving inward;
 *  - the current at the stall level for 399, 400 and 401ms, a reading
 *    below it, a pass with no reading, across the 32-bit ms wrap;
 *  - the AS5600 rule at half the change, below it, round the turn, with
 *    no angle, at a commanded change of 299 and 300 hundredths of a
 *    degree, a command that turns back;
 *  - the move back after a bind on both sides and near the neutral;
 *  - FIND STOP to a block, to the reach edge, on the current, its abort,
 *    its 40s cap and the cap across the wrap.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_reg_fix.h"

#include "hitec_live.h"

/* The judge's times are the limit run's. */
_Static_assert(HITEC_LIVE_STALL_MS == 400u, "SERVO_LIMRUN_STALL_MS");
_Static_assert(HITEC_LIVE_FIND_MS == 40000u, "SERVO_LIMRUN_SEARCH_MS");

/* The D485HW's stored ends. */
static const hitec_live_ends_t k_ends = { 3351u, 8192u, 13003u };

/* One pass's readings. */
static hitec_live_obs_t obs(uint32_t now, uint16_t p, uint16_t pos)
{
    hitec_live_obs_t o;

    memset(&o, 0, sizeof(o));
    o.now_ms = now;
    o.p = p;
    o.pos = pos;
    return o;
}

static hitec_live_obs_t obs_ma(uint32_t now, uint16_t p, uint16_t ma)
{
    hitec_live_obs_t o = obs(now, p, p);

    o.ma = ma;
    o.has_ma = 1;
    return o;
}

static hitec_live_obs_t obs_enc(uint32_t now, uint16_t p, uint16_t angle)
{
    hitec_live_obs_t o = obs(now, p, p);

    o.angle = angle;
    o.has_angle = 1;
    return o;
}

/* Judge one pass. */
static hitec_live_verdict_t jd(hitec_live_judge_t *j, hitec_live_obs_t o)
{
    return hitec_live_judge(j, &o);
}

/* Hand one pass to a search. */
static uint8_t fp(hitec_live_find_t *f, hitec_live_obs_t o)
{
    return hitec_live_find_pass(f, &o);
}

/* ------------------------------------------------------------- mapping */

TEST_CASE(the_mapping_at_0_400_3000_5600_and_6000)
{
    static const struct {
        uint16_t t;
        uint16_t pos;
    } k[] = {
        { 0, 2606 }, { 400, 3351 }, { 3000, 8192 }, { 5600, 13003 },
        { 6000, 13743 }, { 2700, 7633 }, { 3300, 8747 },
    };

    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        CHECK_EQ(hitec_live_d_pos(&k_ends, k[i].t), k[i].pos);
        CHECK_EQ(hitec_live_d_target(&k_ends, k[i].pos), k[i].t);
    }
    /* Above 6000 the target is 6000; beyond the reach the target stops at
     * 0 and 6000. */
    CHECK_EQ(hitec_live_d_pos(&k_ends, 6001u), 13743u);
    CHECK_EQ(hitec_live_d_pos(&k_ends, 65535u), 13743u);
    CHECK_EQ(hitec_live_d_target(&k_ends, 0u), 0u);
    CHECK_EQ(hitec_live_d_target(&k_ends, 2605u), 0u);
    CHECK_EQ(hitec_live_d_target(&k_ends, 13744u), 6000u);
    CHECK_EQ(hitec_live_d_target(&k_ends, 16383u), 6000u);
    CHECK_EQ(hitec_live_d_target(&k_ends, 65535u), 6000u);
    /* With about 1.86 counts a target, every target maps back to itself. */
    for (uint16_t t = 0; t <= HITEC_D_TARGET_MAX; ++t) {
        if (hitec_live_d_target(&k_ends, hitec_live_d_pos(&k_ends, t)) != t) {
            T_FAIL("target %u does not map back", t);
            break;
        }
    }
}

TEST_CASE(the_mapping_clamps_at_0_and_at_the_16383_edge)
{
    const hitec_live_ends_t wide = { 1000u, 8192u, 16000u };
    const hitec_live_ends_t top = { 3351u, 8192u, 16383u };
    uint16_t lo;
    uint16_t hi;

    /* 8192 - 3000 x 7192 / 2600 = -106: 0. */
    CHECK_EQ(hitec_live_d_pos(&wide, 0u), 0u);
    CHECK_EQ(hitec_live_d_pos(&wide, 400u), 1000u);
    /* 8192 + 3000 x 7808 / 2600 = 17201: 16383. */
    CHECK_EQ(hitec_live_d_pos(&wide, 6000u), 16383u);
    CHECK_EQ(hitec_live_d_pos(&wide, 5600u), 16000u);
    CHECK_EQ(hitec_live_d_target(&wide, 0u), 38u);
    CHECK_EQ(hitec_live_d_target(&wide, 16383u), 5728u);
    hitec_live_d_reach(&wide, &lo, &hi);
    CHECK_EQ(lo, 0u);
    CHECK_EQ(hi, 16383u);
    /* An end point at the edge: 5600 and 6000 both give 16383. */
    CHECK_EQ(hitec_live_d_pos(&top, 5600u), 16383u);
    CHECK_EQ(hitec_live_d_pos(&top, 6000u), 16383u);
    CHECK_EQ(hitec_live_d_target(&top, 16383u), 5600u);
}

TEST_CASE(positions_that_do_not_ascend_map_to_nothing)
{
    static const hitec_live_ends_t bad[] = {
        { 8192u, 8192u, 13003u }, { 3351u, 13003u, 13003u },
        { 9000u, 8192u, 13003u }, { 3351u, 8192u, 16384u },
    };
    hitec_live_cmd_t cmd;
    hitec_live_find_t f;

    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK_EQ(hitec_live_d_pos(&bad[i], 400u), 0u);
        CHECK_EQ(hitec_live_d_target(&bad[i], 3351u), 3000u);
        CHECK(!hitec_live_d_command(&bad[i], 8192u, 3351u, &cmd));
        CHECK(!hitec_live_find_start(&f, &bad[i], HITEC_FIND_LEFT, 1000u, 0));
    }
    CHECK_EQ(hitec_live_d_pos(NULL, 400u), 0u);
    CHECK_EQ(hitec_live_d_target(NULL, 3351u), 3000u);
    CHECK(!hitec_live_d_command(NULL, 8192u, 3351u, &cmd));
    CHECK(!hitec_live_d_command(&k_ends, 8192u, 3351u, NULL));
}

TEST_CASE(the_stored_ends_come_from_a_cw_image_only)
{
    hitec_image_t img;
    hitec_live_ends_t e;
    const uint16_t *d = fix_d485();

    hitec_image_init(&img, HITEC_D);
    for (unsigned a = 0; a < 128u; ++a) {
        CHECK(hitec_image_put_reg(&img, (uint8_t)(a * 2u), d[a]));
    }
    CHECK(hitec_live_d_ends(&img, &e));
    CHECK_EQ(e.left, 3351u);
    CHECK_EQ(e.mid, 8192u);
    CHECK_EQ(e.right, 13003u);

    /* CCW: the mapping is not measured. */
    CHECK(hitec_image_put_reg(&img, 0x5Eu, 1u));
    CHECK(!hitec_live_d_ends(&img, &e));
    CHECK(hitec_image_put_reg(&img, 0x5Eu, 0u));
    CHECK(hitec_live_d_ends(&img, &e));
    /* Out of order, beyond 16383. */
    CHECK(hitec_image_put_reg(&img, 0xB2u, 8192u));
    CHECK(!hitec_live_d_ends(&img, &e));
    CHECK(hitec_image_put_reg(&img, 0xB2u, 3351u));
    CHECK(hitec_image_put_reg(&img, 0xB0u, 16384u));
    CHECK(!hitec_live_d_ends(&img, &e));
    CHECK(hitec_image_put_reg(&img, 0xB0u, 13003u));
    /* Each of the four not known. */
    {
        static const uint8_t regs[] = { 0xB2u, 0xC2u, 0xB0u, 0x5Eu };

        for (unsigned i = 0; i < sizeof(regs); ++i) {
            hitec_image_t one = img;

            CHECK(hitec_image_forget(&one, regs[i]));
            CHECK(!hitec_live_d_ends(&one, &e));
        }
    }
    CHECK(!hitec_live_d_ends(NULL, &e));
    CHECK(!hitec_live_d_ends(&img, NULL));
    hitec_image_init(&img, HITEC_CLASSIC);
    CHECK(!hitec_live_d_ends(&img, &e));
}

TEST_CASE(the_reach_the_pulse_width_and_the_1000us_share)
{
    uint16_t lo;
    uint16_t hi;

    hitec_live_d_reach(&k_ends, &lo, &hi);
    CHECK_EQ(lo, 2606u);
    CHECK_EQ(hi, 13743u);

    CHECK_EQ(hitec_live_d_us_target(850u), 400u);
    CHECK_EQ(hitec_live_d_us_target(1500u), 3000u);
    CHECK_EQ(hitec_live_d_us_target(2150u), 5600u);
    CHECK_EQ(hitec_live_d_us_target(750u), 0u);
    CHECK_EQ(hitec_live_d_us_target(0u), 0u);
    CHECK_EQ(hitec_live_d_us_target(2250u), 6000u);
    CHECK_EQ(hitec_live_d_us_target(2251u), 6000u);
    CHECK_EQ(hitec_live_d_us_target(65535u), 6000u);

    /* (3000 - 1000) / 2600 = 76.9%. */
    CHECK_EQ(hitec_live_d_share_pm(1000u), 769u);
    CHECK_EQ(hitec_live_d_share_pm(2000u), 769u);
    CHECK_EQ(hitec_live_d_share_pm(850u), 1000u);
    CHECK_EQ(hitec_live_d_share_pm(2150u), 1000u);
    CHECK_EQ(hitec_live_d_share_pm(1500u), 0u);
    CHECK_EQ(hitec_live_d_share_pm(750u), 1154u);
}

/* ---------------------------------------------------------------- pass */

TEST_CASE(the_slew_at_149_150_and_151)
{
    CHECK_EQ(hitec_live_slew(8192u, 8341u), 8341u);
    CHECK_EQ(hitec_live_slew(8192u, 8342u), 8342u);
    CHECK_EQ(hitec_live_slew(8192u, 8343u), 8342u);
    CHECK_EQ(hitec_live_slew(8192u, 8043u), 8043u);
    CHECK_EQ(hitec_live_slew(8192u, 8042u), 8042u);
    CHECK_EQ(hitec_live_slew(8192u, 8041u), 8042u);
    CHECK_EQ(hitec_live_slew(8192u, 8192u), 8192u);
    CHECK_EQ(hitec_live_slew(0u, 16383u), 150u);
    CHECK_EQ(hitec_live_slew(16383u, 0u), 16233u);
    CHECK_EQ(hitec_live_slew(100u, 0u), 0u);
}

TEST_CASE(the_command_steps_150_and_stops_at_the_reach)
{
    hitec_live_cmd_t c;
    uint16_t p = 3351u;
    unsigned passes = 0;

    /* The keypad from 3351 to 6000 (S-L3): 150 a pass, no clamp. */
    while (p != 6000u && passes < 100u) {
        uint16_t was = p;

        CHECK(hitec_live_d_command(&k_ends, p, 6000u, &c));
        CHECK_EQ(c.clamped, 0u);
        CHECK(c.p - was <= 150);
        CHECK_EQ(c.target, hitec_live_d_target(&k_ends, c.p));
        p = c.p;
        ++passes;
    }
    CHECK_EQ(passes, 18u);  /* 2649 counts: 17 x 150 and 99 */

    /* Beyond the reach: the command runs to its edge and stays. */
    CHECK(hitec_live_d_command(&k_ends, 2700u, 1000u, &c));
    CHECK_EQ(c.clamped, 1u);
    CHECK_EQ(c.p, 2606u);
    CHECK_EQ(c.target, 0u);
    CHECK_EQ(c.lo, 2606u);
    CHECK_EQ(c.hi, 13743u);
    CHECK(hitec_live_d_command(&k_ends, 2606u, 2605u, &c));
    CHECK_EQ(c.clamped, 1u);
    CHECK_EQ(c.p, 2606u);
    CHECK(hitec_live_d_command(&k_ends, 2606u, 2606u, &c));
    CHECK_EQ(c.clamped, 0u);
    CHECK(hitec_live_d_command(&k_ends, 13500u, 16383u, &c));
    CHECK_EQ(c.clamped, 1u);
    CHECK_EQ(c.p, 13650u);
    CHECK_EQ(c.target, 5950u);
    CHECK(hitec_live_d_command(&k_ends, 13650u, 16383u, &c));
    CHECK_EQ(c.p, 13743u);
    CHECK_EQ(c.target, 6000u);
}

TEST_CASE(a_start_outside_the_reach_counts_as_its_edge_up_to_50)
{
    hitec_live_cmd_t c;

    /* 50 below the reach: from the edge, 150 on. */
    CHECK(hitec_live_d_command(&k_ends, 2556u, 8192u, &c));
    CHECK_EQ(c.p, 2756u);
    CHECK_EQ(c.target, hitec_live_d_target(&k_ends, 2756u));
    CHECK(hitec_live_d_command(&k_ends, 2556u, 2000u, &c));
    CHECK_EQ(c.p, 2606u);
    CHECK_EQ(c.target, 0u);
    /* 51 below: refused, as far below as 0. */
    CHECK(!hitec_live_d_command(&k_ends, 2555u, 8192u, &c));
    CHECK(!hitec_live_d_command(&k_ends, 2000u, 8192u, &c));
    CHECK(!hitec_live_d_command(&k_ends, 0u, 8192u, &c));
    /* Above: 50 taken, 51 refused, 65535 refused. */
    CHECK(hitec_live_d_command(&k_ends, 13793u, 8192u, &c));
    CHECK_EQ(c.p, 13593u);
    CHECK(!hitec_live_d_command(&k_ends, 13794u, 8192u, &c));
    CHECK(!hitec_live_d_command(&k_ends, 65535u, 8192u, &c));
}

/* --------------------------------------------------------------- judge */

TEST_CASE(the_lag_at_50_150_and_151_binds_on_2_passes)
{
    hitec_live_judge_t j;
    uint32_t t = 0;

    hitec_live_judge_init(&j, 8192u, 5000u, 1000u);
    CHECK_EQ(j.last_free, 5000u);
    CHECK_EQ(hitec_live_judge(NULL, &(hitec_live_obs_t){ 0 }),
             HITEC_LIVE_FOLLOWING);
    CHECK_EQ(hitec_live_judge(&j, NULL), HITEC_LIVE_FOLLOWING);
    hitec_live_judge_init(NULL, 8192u, 5000u, 1000u);

    CHECK_EQ(jd(&j, obs(t += 110u, 5000u, 5050u)),
             HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs(t += 110u, 4950u, 4900u)),
             HITEC_LIVE_FREE);
    CHECK_EQ(j.last_free, 4900u);  /* the position read */
    CHECK_EQ(jd(&j, obs(t += 110u, 4900u, 4951u)),
             HITEC_LIVE_FOLLOWING);
    CHECK_EQ(j.last_free, 4900u);
    /* 150 on any number of passes is not a bind. */
    for (unsigned i = 0; i < 5u; ++i) {
        CHECK_EQ(jd(&j, obs(t += 110u, 4900u, 5050u)),
                 HITEC_LIVE_FOLLOWING);
    }
    /* 151 once is not; twice in a row is. */
    CHECK_EQ(jd(&j, obs(t += 110u, 4900u, 5051u)),
             HITEC_LIVE_FOLLOWING);
    CHECK_EQ(jd(&j, obs(t += 110u, 4863u, 5014u)),
             HITEC_LIVE_BOUND_LAG);
    CHECK_EQ(j.last_free, 4900u);
    CHECK_EQ(hitec_live_back(&j), 4975u);
}

TEST_CASE(a_row_broken_or_a_command_moving_inward_does_not_bind)
{
    hitec_live_judge_t j;
    uint32_t t = 0;

    hitec_live_judge_init(&j, 8192u, 11000u, 1000u);
    CHECK_EQ(jd(&j, obs(t += 110u, 11000u, 10849u)),
             HITEC_LIVE_FOLLOWING);
    CHECK_EQ(jd(&j, obs(t += 110u, 11000u, 10850u)),
             HITEC_LIVE_FOLLOWING);
    CHECK_EQ(jd(&j, obs(t += 110u, 11000u, 10849u)),
             HITEC_LIVE_FOLLOWING);
    /* Inward: the command goes back toward the neutral while the horn
     * trails. */
    CHECK_EQ(jd(&j, obs(t += 110u, 10850u, 11001u)),
             HITEC_LIVE_FOLLOWING);
    CHECK_EQ(jd(&j, obs(t += 110u, 10700u, 10851u)),
             HITEC_LIVE_FOLLOWING);
    /* Still again, over 150 twice: a bind. */
    CHECK_EQ(jd(&j, obs(t += 110u, 10700u, 10851u)),
             HITEC_LIVE_FOLLOWING);
    CHECK_EQ(jd(&j, obs(t += 110u, 10700u, 10851u)),
             HITEC_LIVE_BOUND_LAG);
    /* No free pass: the start is the last free position. */
    CHECK_EQ(j.last_free, 11000u);
    CHECK_EQ(hitec_live_back(&j), 10925u);
}

TEST_CASE(the_move_back_goes_75_toward_the_neutral_and_not_past_it)
{
    hitec_live_judge_t j;

    hitec_live_judge_init(&j, 8192u, 2229u, 1000u);
    CHECK_EQ(hitec_live_back(&j), 2304u);
    hitec_live_judge_init(&j, 8192u, 13743u, 1000u);
    CHECK_EQ(hitec_live_back(&j), 13668u);
    hitec_live_judge_init(&j, 8192u, 8117u, 1000u);
    CHECK_EQ(hitec_live_back(&j), 8192u);
    hitec_live_judge_init(&j, 8192u, 8116u, 1000u);
    CHECK_EQ(hitec_live_back(&j), 8191u);
    hitec_live_judge_init(&j, 8192u, 8267u, 1000u);
    CHECK_EQ(hitec_live_back(&j), 8192u);
    hitec_live_judge_init(&j, 8192u, 8192u, 1000u);
    CHECK_EQ(hitec_live_back(&j), 8192u);
    CHECK_EQ(hitec_live_back(NULL), 0u);
}

TEST_CASE(the_current_at_the_stall_level_binds_at_400ms)
{
    static const uint32_t t0s[] = { 1000u, 0xFFFFFF00u };
    hitec_live_judge_t j;

    CHECK_EQ(hitec_live_stall_ma(1000u), 800u);
    CHECK_EQ(hitec_live_stall_ma(500u), 400u);
    CHECK_EQ(hitec_live_stall_ma(65535u), 52428u);

    for (unsigned i = 0; i < 2u; ++i) {
        const uint32_t t0 = t0s[i];

        hitec_live_judge_init(&j, 8192u, 8192u, 1000u);
        CHECK_EQ(jd(&j, obs_ma(t0, 8192u, 800u)),
                 HITEC_LIVE_FREE);
        CHECK_EQ(jd(&j, obs_ma(t0 + 399u, 8192u, 900u)),
                 HITEC_LIVE_FREE);
        CHECK_EQ(jd(&j, obs_ma(t0 + 400u, 8192u, 800u)),
                 HITEC_LIVE_BOUND_CURRENT);
        CHECK_EQ(jd(&j, obs_ma(t0 + 401u, 8192u, 800u)),
                 HITEC_LIVE_BOUND_CURRENT);

        /* 799 is under the level; a reading under it starts again. */
        hitec_live_judge_init(&j, 8192u, 8192u, 1000u);
        CHECK_EQ(jd(&j, obs_ma(t0, 8192u, 799u)),
                 HITEC_LIVE_FREE);
        CHECK_EQ(jd(&j, obs_ma(t0 + 100u, 8192u, 800u)),
                 HITEC_LIVE_FREE);
        CHECK_EQ(jd(&j, obs_ma(t0 + 300u, 8192u, 799u)),
                 HITEC_LIVE_FREE);
        CHECK_EQ(jd(&j, obs_ma(t0 + 400u, 8192u, 800u)),
                 HITEC_LIVE_FREE);
        CHECK_EQ(jd(&j, obs_ma(t0 + 799u, 8192u, 800u)),
                 HITEC_LIVE_FREE);
        CHECK_EQ(jd(&j, obs_ma(t0 + 800u, 8192u, 800u)),
                 HITEC_LIVE_BOUND_CURRENT);

        /* A pass with no reading leaves the run standing. */
        hitec_live_judge_init(&j, 8192u, 8192u, 1000u);
        CHECK_EQ(jd(&j, obs_ma(t0, 8192u, 1000u)),
                 HITEC_LIVE_FREE);
        CHECK_EQ(jd(&j, obs(t0 + 399u, 8192u, 8192u)),
                 HITEC_LIVE_FREE);
        CHECK_EQ(jd(&j, obs(t0 + 400u, 8192u, 8192u)),
                 HITEC_LIVE_BOUND_CURRENT);
        /* With no run, no reading binds nothing. */
        hitec_live_judge_init(&j, 8192u, 8192u, 1000u);
        CHECK_EQ(jd(&j, obs(t0 + 5000u, 8192u, 8192u)),
                 HITEC_LIVE_FREE);
    }
}

TEST_CASE(the_as5600_rule_binds_below_half_of_a_3deg_change)
{
    static const struct {
        uint16_t a0;
        uint16_t a1;
        hitec_live_verdict_t v;
    } k[] = {
        /* 300 counts commanded: 402 hundredths of a degree; half is 201,
         * 22.9 AS5600 counts. */
        { 2048u, 2048u, HITEC_LIVE_BOUND_ENC },
        { 2048u, 2070u, HITEC_LIVE_BOUND_ENC },  /* 193 */
        { 2048u, 2071u, HITEC_LIVE_FREE },       /* 202 */
        { 2048u, 2025u, HITEC_LIVE_FREE },       /* the other way */
        { 2048u, 2026u, HITEC_LIVE_BOUND_ENC },
        { 4090u, 17u, HITEC_LIVE_FREE },         /* round the turn: 23 */
        { 10u, 4083u, HITEC_LIVE_FREE },         /* 23 the other way */
        { 10u, 4084u, HITEC_LIVE_BOUND_ENC },    /* 22 */
    };
    hitec_live_judge_t j;

    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        hitec_live_judge_init(&j, 8192u, 8192u, 1000u);
        CHECK_EQ(jd(&j, obs_enc(0u, 8192u, k[i].a0)),
                 HITEC_LIVE_FREE);
        CHECK_EQ(jd(&j, obs_enc(110u, 8342u, k[i].a0)),
                 HITEC_LIVE_FREE);
        /* 150 counts before this pass: 201 hundredths, not judged. */
        CHECK_EQ(jd(&j, obs_enc(220u, 8492u, k[i].a0)),
                 HITEC_LIVE_FREE);
        /* 300 before this pass: judged, the horn had one pass. */
        CHECK_EQ(jd(&j, obs_enc(330u, 8642u, k[i].a1)),
                 k[i].v);
    }

    /* 223 counts are 299 hundredths of a degree, not judged; 224 are 300,
     * judged. */
    hitec_live_judge_init(&j, 8192u, 8192u, 1000u);
    CHECK_EQ(jd(&j, obs_enc(0u, 8192u, 2048u)), HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs_enc(110u, 8415u, 2048u)), HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs_enc(220u, 8415u, 2048u)), HITEC_LIVE_FREE);
    hitec_live_judge_init(&j, 8192u, 8192u, 1000u);
    CHECK_EQ(jd(&j, obs_enc(0u, 8192u, 2048u)), HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs_enc(110u, 8416u, 2048u)), HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs_enc(220u, 8416u, 2048u)), HITEC_LIVE_BOUND_ENC);

    /* A command that turns back starts a new reference: the horn follows
     * 8192, 8342, 8417, 8267 exactly. */
    hitec_live_judge_init(&j, 8192u, 8192u, 1000u);
    CHECK_EQ(jd(&j, obs_enc(0u, 8192u, 2048u)), HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs_enc(110u, 8342u, 2070u)), HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs_enc(220u, 8417u, 2082u)), HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs_enc(330u, 8267u, 2059u)), HITEC_LIVE_FREE);
    CHECK_EQ(j.enc_p, 8267u);
    CHECK_EQ(j.enc_a, 2059u);
    /* The same the other way round. */
    hitec_live_judge_init(&j, 8192u, 8192u, 1000u);
    CHECK_EQ(jd(&j, obs_enc(0u, 8192u, 2048u)), HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs_enc(110u, 8042u, 2026u)), HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs_enc(220u, 7967u, 2014u)), HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs_enc(330u, 8117u, 2037u)), HITEC_LIVE_FREE);
    CHECK_EQ(j.enc_p, 8117u);
    /* From there on, a blocked horn still binds. */
    CHECK_EQ(jd(&j, obs_enc(440u, 8267u, 2037u)), HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs_enc(550u, 8417u, 2037u)), HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs_enc(660u, 8567u, 2037u)), HITEC_LIVE_BOUND_ENC);

    /* A command that stands still after moving keeps the reference: a
     * blocked horn binds on the still pass, both ways. */
    hitec_live_judge_init(&j, 8192u, 8192u, 1000u);
    CHECK_EQ(jd(&j, obs_enc(0u, 8192u, 2048u)), HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs_enc(110u, 8042u, 2048u)), HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs_enc(220u, 7892u, 2048u)), HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs_enc(330u, 7892u, 2048u)), HITEC_LIVE_BOUND_ENC);
    hitec_live_judge_init(&j, 8192u, 8192u, 1000u);
    CHECK_EQ(jd(&j, obs_enc(0u, 8192u, 2048u)), HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs_enc(110u, 8342u, 2048u)), HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs_enc(220u, 8492u, 2048u)), HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs_enc(330u, 8492u, 2048u)), HITEC_LIVE_BOUND_ENC);

    /* A pass with no angle drops the reference. */
    hitec_live_judge_init(&j, 8192u, 8192u, 1000u);
    CHECK_EQ(jd(&j, obs_enc(0u, 8192u, 2048u)), HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs(110u, 8342u, 8342u)), HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs_enc(220u, 8492u, 2048u)),
             HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs_enc(330u, 8642u, 2048u)),
             HITEC_LIVE_FREE);
    CHECK_EQ(jd(&j, obs_enc(440u, 8792u, 2048u)),
             HITEC_LIVE_FREE);
    /* 300 from the new reference at 8492, before this pass: judged. */
    CHECK_EQ(jd(&j, obs_enc(550u, 8942u, 2048u)),
             HITEC_LIVE_BOUND_ENC);

    /* A judgement passed moves the reference to that pass. */
    hitec_live_judge_init(&j, 8192u, 8192u, 1000u);
    (void)jd(&j, obs_enc(0u, 8192u, 2048u));
    (void)jd(&j, obs_enc(110u, 8342u, 2048u));
    (void)jd(&j, obs_enc(220u, 8492u, 2060u));
    CHECK_EQ(jd(&j, obs_enc(330u, 8642u, 2080u)),
             HITEC_LIVE_FREE);
    CHECK_EQ(j.enc_p, 8642u);
    CHECK_EQ(j.enc_a, 2080u);
    CHECK_EQ(jd(&j, obs_enc(440u, 8792u, 2080u)),
             HITEC_LIVE_FREE);
}

TEST_CASE(a_bind_names_lag_before_current_before_the_as5600)
{
    hitec_live_judge_t j;
    hitec_live_obs_t o;

    hitec_live_judge_init(&j, 8192u, 8192u, 1000u);
    o = obs_enc(0u, 8192u, 2048u);
    o.has_ma = 1;
    o.ma = 1000u;
    CHECK_EQ(jd(&j, o), HITEC_LIVE_FREE);
    o = obs_enc(200u, 8492u, 2048u);
    o.pos = 8192u;
    o.has_ma = 1;
    o.ma = 1000u;
    CHECK_EQ(jd(&j, o), HITEC_LIVE_FOLLOWING);
    o.now_ms = 400u;
    o.p = 8642u;
    CHECK_EQ(jd(&j, o), HITEC_LIVE_BOUND_LAG);
    o.pos = o.p;
    o.now_ms = 500u;
    CHECK_EQ(jd(&j, o), HITEC_LIVE_BOUND_CURRENT);
    o.has_ma = 0;
    o.p = 8800u;
    o.pos = o.p;
    hitec_live_judge_init(&j, 8192u, 8192u, 1000u);
    (void)jd(&j, obs_enc(0u, 8192u, 2048u));
    (void)jd(&j, obs_enc(110u, 8642u, 2048u));
    CHECK_EQ(jd(&j, o), HITEC_LIVE_BOUND_ENC);
}

/* ----------------------------------------------------------- FIND STOP */

/* A horn blocked at @p block: it reaches no position past it. */
static uint16_t blocked(uint8_t side, uint16_t block, uint16_t p)
{
    if (side == HITEC_FIND_LEFT) {
        return p < block ? block : p;
    }
    return p > block ? block : p;
}

/* Run a search with passes @p pass_ms apart; the result's state. */
static uint8_t find_run(hitec_live_find_t *f, uint8_t side, uint16_t block,
                        uint32_t t0, uint32_t pass_ms, unsigned *passes)
{
    uint8_t st = HITEC_FIND_RUN;
    uint32_t t = t0;

    CHECK(hitec_live_find_start(f, &k_ends, side, 1000u, t0));
    CHECK_EQ(f->p, 8192u);
    *passes = 0;
    while (st == HITEC_FIND_RUN && *passes < 2000u) {
        t += pass_ms;
        st = fp(f, obs(t, 0u, blocked(side, block, f->p)));
        ++*passes;
    }
    return st;
}

TEST_CASE(find_stop_finds_a_block_and_sets_the_end_75_inside)
{
    hitec_live_find_t f;
    unsigned passes = 0;

    /* A block at about -45deg (L0.4, S-L6). */
    CHECK_EQ(find_run(&f, HITEC_FIND_LEFT, 4840u, 0u, 110u, &passes),
             HITEC_FIND_FOUND);
    CHECK_EQ(f.why, HITEC_LIVE_BOUND_LAG);
    /* The last command within 50 of the block is 8192 - 91 x 37 = 4825;
     * the horn read 4840 there. */
    CHECK_EQ(f.stop, 4840u);
    CHECK_EQ(f.end, 4915u);
    CHECK(passes * 110u < HITEC_LIVE_FIND_MS);
    /* It stays found. */
    CHECK_EQ(fp(&f, obs(0u, 0u, 0u)), HITEC_FIND_FOUND);
    hitec_live_find_abort(&f);
    CHECK_EQ(f.state, HITEC_FIND_FOUND);

    CHECK_EQ(find_run(&f, HITEC_FIND_RIGHT, 11000u, 0u, 110u, &passes),
             HITEC_FIND_FOUND);
    CHECK_EQ(f.stop, 11000u);
    CHECK_EQ(f.end, 10925u);
}

TEST_CASE(find_stop_reaches_the_edge_with_no_stop)
{
    hitec_live_find_t f;
    unsigned passes = 0;

    CHECK_EQ(find_run(&f, HITEC_FIND_LEFT, 0u, 0u, 110u, &passes),
             HITEC_FIND_NONE);
    CHECK_EQ(f.stop, 2606u);
    CHECK_EQ(f.p, 2606u);
    /* 151 steps of 37 to the edge, 2 passes at each position and at the
     * edge. */
    CHECK_EQ(passes, 2u * 152u);

    CHECK_EQ(find_run(&f, HITEC_FIND_RIGHT, 16383u, 0u, 110u, &passes),
             HITEC_FIND_NONE);
    CHECK_EQ(f.stop, 13743u);
}

TEST_CASE(find_stop_binds_on_the_current)
{
    hitec_live_find_t f;
    uint32_t t = 0;
    uint8_t st = HITEC_FIND_RUN;
    unsigned n = 0;

    CHECK(hitec_live_find_start(&f, &k_ends, HITEC_FIND_RIGHT, 500u, t));
    while (st == HITEC_FIND_RUN && n < 1000u) {
        /* 400mA, the stall level of 500mA, from 9000 on. */
        const uint16_t ma = f.p >= 9000u ? 400u : 100u;

        t += 110u;
        st = fp(&f, obs_ma(t, f.p, ma));
        ++n;
    }
    CHECK_EQ(st, HITEC_FIND_FOUND);
    CHECK_EQ(f.why, HITEC_LIVE_BOUND_CURRENT);
    CHECK(f.stop >= 9000u);
    CHECK_EQ(f.end, f.stop - 75u);
}

TEST_CASE(find_stop_aborts)
{
    hitec_live_find_t f;
    const hitec_live_obs_t none = obs(0u, 0u, 0u);
    uint16_t p;

    CHECK(hitec_live_find_start(&f, &k_ends, HITEC_FIND_LEFT, 1000u, 0u));
    for (unsigned i = 0; i < 5u; ++i) {
        CHECK_EQ(fp(&f, obs(110u * (i + 1u), 0u, f.p)),
                 HITEC_FIND_RUN);
    }
    p = f.p;
    CHECK(p < 8192u);
    hitec_live_find_abort(&f);
    CHECK_EQ(f.state, HITEC_FIND_ABORTED);
    CHECK_EQ(fp(&f, obs(660u, 0u, f.p)),
             HITEC_FIND_ABORTED);
    CHECK_EQ(f.p, p);
    hitec_live_find_abort(NULL);
    CHECK_EQ(hitec_live_find_pass(NULL, &none), HITEC_FIND_ABORTED);
    CHECK_EQ(hitec_live_find_pass(&f, NULL), HITEC_FIND_ABORTED);
    CHECK(!hitec_live_find_start(NULL, &k_ends, HITEC_FIND_LEFT, 1000u, 0u));
    CHECK(!hitec_live_find_start(&f, &k_ends, 2u, 1000u, 0u));
}

TEST_CASE(find_stop_ends_at_its_40s_cap_across_the_wrap)
{
    static const uint32_t t0s[] = { 0u, 0xFFFFF000u };
    hitec_live_find_t f;
    unsigned passes = 0;

    for (unsigned i = 0; i < 2u; ++i) {
        const uint32_t t0 = t0s[i];

        CHECK(hitec_live_find_start(&f, &k_ends, HITEC_FIND_LEFT, 1000u, t0));
        CHECK_EQ(fp(&f, obs(t0 + 39999u, 0u, f.p)),
                 HITEC_FIND_RUN);
        CHECK_EQ(fp(&f, obs(t0 + 40000u, 0u, f.p)),
                 HITEC_FIND_TIMEOUT);
        CHECK_EQ(fp(&f, obs(t0 + 40001u, 0u, f.p)),
                 HITEC_FIND_TIMEOUT);
    }
    /* At 140ms a pass the edge is 42.6s away: the cap ends it. */
    CHECK_EQ(find_run(&f, HITEC_FIND_LEFT, 0u, 0xFFFF0000u, 140u, &passes),
             HITEC_FIND_TIMEOUT);
    CHECK_EQ(passes, 286u);  /* 40040ms */
    CHECK(f.p > 2606u);
}

int main(void)
{
    RUN(the_mapping_at_0_400_3000_5600_and_6000);
    RUN(the_mapping_clamps_at_0_and_at_the_16383_edge);
    RUN(positions_that_do_not_ascend_map_to_nothing);
    RUN(the_stored_ends_come_from_a_cw_image_only);
    RUN(the_reach_the_pulse_width_and_the_1000us_share);
    RUN(the_slew_at_149_150_and_151);
    RUN(the_command_steps_150_and_stops_at_the_reach);
    RUN(a_start_outside_the_reach_counts_as_its_edge_up_to_50);
    RUN(the_lag_at_50_150_and_151_binds_on_2_passes);
    RUN(a_row_broken_or_a_command_moving_inward_does_not_bind);
    RUN(the_move_back_goes_75_toward_the_neutral_and_not_past_it);
    RUN(the_current_at_the_stall_level_binds_at_400ms);
    RUN(the_as5600_rule_binds_below_half_of_a_3deg_change);
    RUN(a_bind_names_lag_before_current_before_the_as5600);
    RUN(find_stop_finds_a_block_and_sets_the_end_75_inside);
    RUN(find_stop_reaches_the_edge_with_no_stop);
    RUN(find_stop_binds_on_the_current);
    RUN(find_stop_aborts);
    RUN(find_stop_ends_at_its_40s_cap_across_the_wrap);
    return test_summary("hitec_live");
}
