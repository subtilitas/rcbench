/*
 * Bit timing, for the two controllers this link has.
 *
 * The value of testing this is that wrong timing does not fail cleanly: a node
 * a fraction of a percent off works on a short bench cable with one other node
 * and starts logging errors when the bus gets longer, colder or busier. Every
 * number below is checkable by hand from the datasheet, which is the point --
 * these are the values that would otherwise be copied from an application note
 * and believed.
 */
#include <string.h>

#include "greatest.h"

#include "can_timing.h"

/* The bit rate a solution actually produces, recomputed from its parts rather
 * than read back from the field that claims it. */
static uint32_t achieved(const can_timing_limits_t *lim, const can_timing_t *t)
{
    return lim->clock_hz / ((uint32_t)t->div * t->tq);
}

TEST_CASE(a_16_mhz_mcp2515_reaches_one_megabit)
{
    can_timing_limits_t lim;
    can_timing_limits_mcp2515(&lim, 16000000u);
    can_timing_t t;
    CHECK(can_timing_solve(&lim, 1000000u, CAN_SAMPLE_POINT_DEFAULT, &t));

    /* 16 MHz / (2 x 8) = 1 Mbit/s: the smallest divisor and the fewest quanta
     * the part allows, which is exactly why 8 MHz cannot do it. */
    CHECK_EQ(t.div, 2);
    CHECK_EQ(t.tq, 8);
    CHECK_EQ(1u + t.tseg1 + t.tseg2, t.tq);
    CHECK_EQ(achieved(&lim, &t), 1000000u);
    CHECK_EQ(t.bitrate_hz, 1000000u);
}

/*
 * The finding worth having before anything is soldered.  MCP2515 divides its
 * crystal by two before the prescaler even starts, and a bit needs at least
 * eight quanta -- so an 8 MHz part tops out at 500 kbit/s and no register
 * value will change that.
 */
TEST_CASE(an_8_mhz_mcp2515_cannot_do_one_megabit_at_all)
{
    can_timing_limits_t lim;
    can_timing_limits_mcp2515(&lim, 8000000u);
    can_timing_t t;

    CHECK_EQ(can_timing_solve(&lim, 1000000u, CAN_SAMPLE_POINT_DEFAULT, &t),
             false);
    CHECK_EQ(can_timing_solve(&lim, 800000u, CAN_SAMPLE_POINT_DEFAULT, &t),
             false);

    /* 500 kbit/s is the ceiling, and it is reachable. */
    CHECK(can_timing_solve(&lim, 500000u, CAN_SAMPLE_POINT_DEFAULT, &t));
    CHECK_EQ(achieved(&lim, &t), 500000u);
    CHECK_EQ(can_timing_max_bitrate(&lim), 500000u);

    /* Against 1 Mbit/s on the 16 MHz part. */
    can_timing_limits_t hi;
    can_timing_limits_mcp2515(&hi, 16000000u);
    CHECK_EQ(can_timing_max_bitrate(&hi), 1000000u);
}

/*
 * The module's crystal, pinned as a test because it is the number the whole
 * bandwidth budget rests on.
 *
 * It is 16 MHz, and that was not taken on trust: the vendor's shipping driver
 * carries a table of CNF triples for ten standard rates, and decoding those
 * back into divisor and quanta gives the advertised rate at 16 MHz and at no
 * other crystal. Ten independent confirmations of one number.
 *
 * If a future module arrives with 8 MHz, this case fails and says so, which is
 * the right moment to find out rather than after the bus is wired.
 */
TEST_CASE(the_module_reaches_every_standard_rate_on_its_16_mhz_crystal)
{
    can_timing_limits_t lim;
    can_timing_limits_mcp2515(&lim, 16000000u);
    const uint32_t standard[] = { 5000u, 10000u, 20000u, 50000u, 100000u,
                                  125000u, 250000u, 500000u, 800000u,
                                  1000000u };
    for (size_t i = 0; i < sizeof(standard) / sizeof(standard[0]); ++i) {
        can_timing_t t;
        uint8_t cnf[3];
        if (!can_timing_solve(&lim, standard[i], CAN_SAMPLE_POINT_DEFAULT, &t)
            || !mcp2515_encode_timing(&t, cnf)) {
            T_FAIL("%u bit/s is out of reach on a 16 MHz crystal",
                   (unsigned)standard[i]);
            continue;
        }
        if (achieved(&lim, &t) != standard[i]) {
            T_FAIL("%u bit/s came out as %u", (unsigned)standard[i],
                   (unsigned)achieved(&lim, &t));
        }
    }

    /* And the one the link will actually use, in full. */
    can_timing_t t;
    CHECK(can_timing_solve(&lim, 1000000u, CAN_SAMPLE_POINT_DEFAULT, &t));
    CHECK_EQ(t.div, 2);
    CHECK_EQ(t.tq, 8);
    CHECK_EQ(t.tseg1, 5);
    CHECK_EQ(t.tseg2, 2);
    /*
     * 75%, not the 87.5% asked for: eight quanta is the fewest a bit may have,
     * so one quantum is an eighth of the bit and nothing lands closer. The
     * vendor's own table puts this bit at 62.5% -- also legal, and a whole
     * quantum earlier than it needs to be.
     */
    CHECK_EQ(t.sample_permille, 750);
}

TEST_CASE(the_panels_twai_reaches_every_rate_the_coprocessor_can)
{
    can_timing_limits_t lim;
    can_timing_limits_twai(&lim);
    const uint32_t rates[] = { 125000u, 250000u, 500000u, 800000u, 1000000u };
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
        can_timing_t t;
        if (!can_timing_solve(&lim, rates[i], CAN_SAMPLE_POINT_DEFAULT, &t)) {
            T_FAIL("TWAI cannot do %u bit/s", (unsigned)rates[i]);
            continue;
        }
        if (achieved(&lim, &t) != rates[i]) {
            T_FAIL("%u bit/s came out as %u", (unsigned)rates[i],
                   (unsigned)achieved(&lim, &t));
        }
    }
}

/*
 * Exact, not close.  A rate that cannot be hit on the nose is reported as
 * impossible rather than approximated, because two nodes that disagree by a
 * percent agree on short frames and fall out on long ones -- which is a fault
 * that appears only once the bus is busy.
 */
TEST_CASE(a_rate_that_cannot_be_hit_exactly_is_refused)
{
    can_timing_limits_t lim;
    can_timing_limits_mcp2515(&lim, 16000000u);
    can_timing_t t;

    /* 16 MHz will not divide into 999 kbit/s by any integer path. */
    CHECK_EQ(can_timing_solve(&lim, 999000u, CAN_SAMPLE_POINT_DEFAULT, &t),
             false);
    CHECK_EQ(can_timing_solve(&lim, 333333u, CAN_SAMPLE_POINT_DEFAULT, &t),
             false);

    /* And every rate it does accept is exact, across the whole usable range. */
    for (uint32_t rate = 10000u; rate <= 1000000u; rate += 10000u) {
        if (can_timing_solve(&lim, rate, CAN_SAMPLE_POINT_DEFAULT, &t)) {
            if (achieved(&lim, &t) != rate) {
                T_FAIL("%u bit/s solved to %u", (unsigned)rate,
                       (unsigned)achieved(&lim, &t));
            }
        }
    }
}

/* The sample point lands near where it was asked to, and the segments are
 * always a legal split of the quanta. */
TEST_CASE(the_sample_point_lands_near_the_target_and_the_segments_are_legal)
{
    can_timing_limits_t lim;
    can_timing_limits_mcp2515(&lim, 16000000u);
    const uint32_t rates[] = { 125000u, 250000u, 500000u, 1000000u };
    const uint16_t targets[] = { 700u, 750u, 875u };

    for (size_t r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r) {
        for (size_t s = 0; s < sizeof(targets) / sizeof(targets[0]); ++s) {
            can_timing_t t;
            if (!can_timing_solve(&lim, rates[r], targets[s], &t)) {
                T_FAIL("no solution for %u bit/s", (unsigned)rates[r]);
                continue;
            }
            CHECK_EQ(1u + t.tseg1 + t.tseg2, t.tq);
            CHECK(t.tseg1 >= lim.tseg1_min && t.tseg1 <= lim.tseg1_max);
            CHECK(t.tseg2 >= lim.tseg2_min && t.tseg2 <= lim.tseg2_max);
            CHECK(t.tq >= lim.tq_min && t.tq <= lim.tq_max);
            /*
             * Within a tenth of the bit. At eight quanta one quantum IS 125
             * permille, so nothing closer is available and demanding it would
             * be demanding the impossible rather than the correct.
             */
            const int32_t err = (int32_t)t.sample_permille - (int32_t)targets[s];
            if (err > 130 || err < -130) {
                T_FAIL("%u bit/s target %u gave %u", (unsigned)rates[r],
                       targets[s], t.sample_permille);
            }
        }
    }
}

/*
 * The solver claims the *closest* achievable sample point, so assert exactly
 * that: no other legal split of the same quanta is nearer the target.
 *
 * Checking the reported value against a tolerance does not test this. An
 * earlier version did, and an error function that measured the sample point
 * from the wrong end of the sync quantum survived it -- because the reported
 * figure was computed separately and stayed right while the *choice* it drove
 * quietly went wrong. Only a mid-range target moves the winner, which is why
 * this sweeps them.
 */
TEST_CASE(no_legal_split_sits_closer_to_the_target)
{
    can_timing_limits_t lim;
    can_timing_limits_mcp2515(&lim, 16000000u);

    for (uint32_t rate = 50000u; rate <= 1000000u; rate += 50000u) {
        for (uint16_t target = 400; target <= 900; target += 25) {
            can_timing_t t;
            if (!can_timing_solve(&lim, rate, target, &t)) {
                continue;
            }
            const int32_t chosen =
                (int32_t)t.sample_permille - (int32_t)target;
            const uint32_t chosen_err =
                (uint32_t)(chosen < 0 ? -chosen : chosen);

            for (uint8_t t1 = lim.tseg1_min; t1 <= lim.tseg1_max; ++t1) {
                if ((uint32_t)t1 + 1u >= t.tq) {
                    break;
                }
                const uint8_t t2 = (uint8_t)(t.tq - 1u - t1);
                if (t2 < lim.tseg2_min || t2 > lim.tseg2_max) {
                    continue;
                }
                const int32_t sp =
                    (int32_t)(((uint32_t)(1u + t1) * 1000u) / t.tq);
                const int32_t d = sp - (int32_t)target;
                const uint32_t err = (uint32_t)(d < 0 ? -d : d);
                if (err < chosen_err) {
                    T_FAIL("%u bit/s target %u: chose tseg1 %u (%u permille) "
                           "but tseg1 %u gives %d",
                           (unsigned)rate, target, t.tseg1,
                           t.sample_permille, t1, sp);
                }
            }
        }
    }
}

/*
 * The resynchronisation jump width may never exceed phase 2.  If it did, a
 * resynchronisation could shorten a bit past its own sample point -- the
 * controller would be sampling a bit it had already finished.
 */
TEST_CASE(the_jump_width_never_exceeds_phase_two)
{
    can_timing_limits_t lim;
    can_timing_limits_mcp2515(&lim, 16000000u);
    for (uint32_t rate = 10000u; rate <= 1000000u; rate += 5000u) {
        can_timing_t t;
        if (!can_timing_solve(&lim, rate, CAN_SAMPLE_POINT_DEFAULT, &t)) {
            continue;
        }
        if (t.sjw > t.tseg2) {
            T_FAIL("%u bit/s: sjw %u exceeds tseg2 %u", (unsigned)rate,
                   t.sjw, t.tseg2);
        }
        if (t.sjw < 1 || t.sjw > lim.sjw_max) {
            T_FAIL("%u bit/s: sjw %u out of range", (unsigned)rate, t.sjw);
        }
    }
}

/* ------------------------------------------------------ register encoding */

/*
 * The one worked example, checkable by hand against the datasheet: 16 MHz,
 * 500 kbit/s, sixteen quanta a bit.
 *
 *   div 2  -> BRP 0
 *   tq 16, tseg1 13, tseg2 2  -> sample point 14/16 = 875 permille
 *   phase1 7, propagation 6
 *
 *   CNF1 = SJW-1 << 6 | BRP        = 01 000000 = 0x40   (SJW 2)
 *   CNF2 = BTLMODE | PS1-1 << 3 | PRSEG-1 = 1 0 110 101 = 0xB5
 *   CNF3 = PS2-1                   = 0x01
 */
TEST_CASE(the_registers_come_out_as_the_datasheet_says_they_should)
{
    can_timing_limits_t lim;
    can_timing_limits_mcp2515(&lim, 16000000u);
    can_timing_t t;
    CHECK(can_timing_solve(&lim, 500000u, CAN_SAMPLE_POINT_DEFAULT, &t));
    CHECK_EQ(t.div, 2);
    CHECK_EQ(t.tq, 16);
    CHECK_EQ(t.tseg1, 13);
    CHECK_EQ(t.tseg2, 2);
    CHECK_EQ(t.sample_permille, 875);

    uint8_t cnf[3];
    CHECK(mcp2515_encode_timing(&t, cnf));
    CHECK_EQ(cnf[0], 0x40);   /* SJW 2, BRP 0 */
    CHECK_EQ(cnf[1], 0xB5);   /* BTLMODE, PS1 7, PRSEG 6 */
    CHECK_EQ(cnf[2], 0x01);   /* PS2 2 */
}

/* Every solution the solver produces must be expressible; a segmentation the
 * encoder cannot render is a bug in one of the two and not a valid state. */
TEST_CASE(every_solution_encodes)
{
    const uint32_t crystals[] = { 8000000u, 16000000u, 20000000u };
    for (size_t c = 0; c < sizeof(crystals) / sizeof(crystals[0]); ++c) {
        can_timing_limits_t lim;
        can_timing_limits_mcp2515(&lim, crystals[c]);
        for (uint32_t rate = 10000u; rate <= 1000000u; rate += 5000u) {
            can_timing_t t;
            if (!can_timing_solve(&lim, rate, CAN_SAMPLE_POINT_DEFAULT, &t)) {
                continue;
            }
            uint8_t cnf[3];
            if (!mcp2515_encode_timing(&t, cnf)) {
                T_FAIL("%u Hz crystal, %u bit/s: solved but would not encode",
                       (unsigned)crystals[c], (unsigned)rate);
                continue;
            }
            /* And the registers decode back to the same segmentation. */
            const uint8_t brp    = cnf[0] & 0x3Fu;
            const uint8_t sjw    = (uint8_t)((cnf[0] >> 6) + 1u);
            const uint8_t prseg  = (uint8_t)((cnf[1] & 0x07u) + 1u);
            const uint8_t phseg1 = (uint8_t)(((cnf[1] >> 3) & 0x07u) + 1u);
            const uint8_t phseg2 = (uint8_t)((cnf[2] & 0x07u) + 1u);
            CHECK_EQ((brp + 1u) * 2u, t.div);
            CHECK_EQ(sjw, t.sjw);
            CHECK_EQ(prseg + phseg1, t.tseg1);
            CHECK_EQ(phseg2, t.tseg2);
            CHECK_EQ(cnf[1] & 0x80u, 0x80u);   /* BTLMODE always set */
        }
    }
}

TEST_CASE(a_segmentation_the_part_cannot_express_is_refused)
{
    can_timing_t t;
    uint8_t cnf[3];
    memset(&t, 0, sizeof(t));
    t.div = 2; t.tseg1 = 13; t.tseg2 = 2; t.sjw = 2; t.tq = 16;

    t.tseg1 = 17;                     /* no legal split into two 1..8 halves */
    CHECK_EQ(mcp2515_encode_timing(&t, cnf), false);
    t.tseg1 = 13;

    t.div = 3;                        /* the divisor is always even */
    CHECK_EQ(mcp2515_encode_timing(&t, cnf), false);
    t.div = 130;                      /* BRP past 63 */
    CHECK_EQ(mcp2515_encode_timing(&t, cnf), false);
    t.div = 2;

    t.tseg2 = 1;                      /* phase 2 of one is forbidden */
    CHECK_EQ(mcp2515_encode_timing(&t, cnf), false);
    t.tseg2 = 2;

    t.sjw = 5;                        /* jump width is two bits wide */
    CHECK_EQ(mcp2515_encode_timing(&t, cnf), false);
}

TEST_CASE(null_arguments_are_refused_rather_than_dereferenced)
{
    can_timing_limits_t lim;
    can_timing_t t;
    uint8_t cnf[3];
    can_timing_limits_mcp2515(NULL, 16000000u);
    can_timing_limits_twai(NULL);
    can_timing_limits_mcp2515(&lim, 16000000u);
    CHECK_EQ(can_timing_solve(NULL, 500000u, 875, &t), false);
    CHECK_EQ(can_timing_solve(&lim, 500000u, 875, NULL), false);
    CHECK_EQ(can_timing_solve(&lim, 0, 875, &t), false);
    CHECK_EQ(can_timing_max_bitrate(NULL), 0);
    CHECK_EQ(mcp2515_encode_timing(NULL, cnf), false);
    CHECK_EQ(mcp2515_encode_timing(&t, NULL), false);

    /* A clock of zero is a configuration mistake, not a division by zero. */
    memset(&lim, 0, sizeof(lim));
    CHECK_EQ(can_timing_solve(&lim, 500000u, 875, &t), false);
    CHECK_EQ(can_timing_max_bitrate(&lim), 0);
}

int main(void)
{
    RUN(a_16_mhz_mcp2515_reaches_one_megabit);
    RUN(an_8_mhz_mcp2515_cannot_do_one_megabit_at_all);
    RUN(the_module_reaches_every_standard_rate_on_its_16_mhz_crystal);
    RUN(the_panels_twai_reaches_every_rate_the_coprocessor_can);
    RUN(a_rate_that_cannot_be_hit_exactly_is_refused);
    RUN(the_sample_point_lands_near_the_target_and_the_segments_are_legal);
    RUN(no_legal_split_sits_closer_to_the_target);
    RUN(the_jump_width_never_exceeds_phase_two);
    RUN(the_registers_come_out_as_the_datasheet_says_they_should);
    RUN(every_solution_encodes);
    RUN(a_segmentation_the_part_cannot_express_is_refused);
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    return test_summary("can_timing");
}
