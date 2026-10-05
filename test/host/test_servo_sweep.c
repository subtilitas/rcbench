/*
 * The sweep: a servo driven through a square, a sine or a triangle.
 *
 * Under test: the curve's shape at its quarters, the dwell standing it still
 * at each end, the movement count ending it at the centre, a clock that
 * wraps, and a sweep hours long keeping its period to the microsecond.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>

#include "greatest.h"

#include "servo_sweep.h"

static uint16_t at(sweep_t *w, uint32_t t)
{
    uint16_t c = 0;
    (void)sweep_step(w, w->start_ms + t, &c);
    return c;
}

static sweep_t make(sweep_kind_t kind, uint16_t mhz, uint16_t amp,
                    uint16_t dwell, uint16_t moves)
{
    sweep_t w;
    const sweep_cfg_t cfg = { kind, mhz, amp, dwell, moves };
    CHECK(sweep_start(&w, &cfg, 1000u));
    return w;
}

TEST_CASE(a_configuration_outside_its_ranges_is_refused)
{
    const sweep_cfg_t bad[] = {
        { SWEEP_OFF,        1000u, 400u,    0u, 0u },
        { SWEEP_KIND_COUNT, 1000u, 400u,    0u, 0u },
        { SWEEP_SINE,  SWEEP_MHZ_MIN - 1u, 400u, 0u, 0u },
        { SWEEP_SINE,  SWEEP_MHZ_MAX + 1u, 400u, 0u, 0u },
        { SWEEP_SINE,       1000u, SWEEP_AMPLITUDE_MAX + 1u, 0u, 0u },
        { SWEEP_SINE,       1000u, 400u, SWEEP_DWELL_MAX_MS + 1u, 0u },
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        sweep_t w;
        w.running = true;
        CHECK(!sweep_cfg_valid(&bad[i]));
        CHECK(!sweep_start(&w, &bad[i], 0u));
        CHECK(!w.running);
        uint16_t c = 0;
        CHECK(!sweep_step(&w, 10u, &c));
        CHECK_EQ(c, SWEEP_CENTRE);
    }
    CHECK(!sweep_cfg_valid(NULL));
    CHECK(!sweep_start(NULL, &bad[0], 0u));
}

/* A sine starts at the centre, so a sweep begins without a jump. */
TEST_CASE(a_sine_is_at_its_ends_at_the_quarters)
{
    sweep_t w = make(SWEEP_SINE, 1000u, 400u, 0u, 0u);   /* 1 Hz */
    CHECK_EQ(at(&w, 0u),   500u);
    CHECK_EQ(at(&w, 250u), 900u);
    CHECK_EQ(at(&w, 500u), 500u);
    CHECK_EQ(at(&w, 750u), 100u);
    CHECK_EQ(at(&w, 1000u), 500u);
    CHECK(at(&w, 125u) > 770u && at(&w, 125u) < 785u);   /* 500 + 400 sin 45 */
}

TEST_CASE(a_triangle_moves_at_one_speed)
{
    sweep_t w = make(SWEEP_TRIANGLE, 1000u, 400u, 0u, 0u);
    CHECK_EQ(at(&w, 0u),   500u);
    CHECK_EQ(at(&w, 125u), 700u);
    CHECK_EQ(at(&w, 250u), 900u);
    CHECK_EQ(at(&w, 500u), 500u);
    CHECK_EQ(at(&w, 625u), 300u);
    CHECK_EQ(at(&w, 875u), 300u);
}

TEST_CASE(a_square_jumps_at_the_start_and_the_half)
{
    sweep_t w = make(SWEEP_SQUARE, 1000u, 400u, 0u, 0u);
    CHECK_EQ(at(&w, 0u),   900u);
    CHECK_EQ(at(&w, 499u), 900u);
    CHECK_EQ(at(&w, 500u), 100u);
    CHECK_EQ(at(&w, 999u), 100u);
    CHECK_EQ(at(&w, 1000u), 900u);
}

/* The dwell stands the curve still at each end and lengthens the cycle by
 * twice itself; a square holds each end half a period plus the dwell. */
TEST_CASE(the_dwell_holds_each_end)
{
    sweep_t w = make(SWEEP_SINE, 1000u, 400u, 200u, 0u);
    CHECK_EQ(at(&w, 250u),  900u);
    CHECK_EQ(at(&w, 449u),  900u);
    CHECK_EQ(at(&w, 700u),  500u);
    CHECK_EQ(at(&w, 950u),  100u);
    CHECK_EQ(at(&w, 1149u), 100u);
    CHECK_EQ(at(&w, 1400u), 500u);                 /* the cycle is 1.4 s */
    CHECK_EQ(at(&w, 1650u), 900u);

    sweep_t sq = make(SWEEP_SQUARE, 1000u, 400u, 200u, 0u);
    CHECK_EQ(at(&sq, 699u), 900u);
    CHECK_EQ(at(&sq, 700u), 100u);
    CHECK_EQ(at(&sq, 1399u), 100u);
    CHECK_EQ(at(&sq, 1400u), 900u);
}

/* Each end reached is a movement; with a count the sweep ends after the last
 * end's dwell, at the centre. */
TEST_CASE(a_movement_count_ends_the_sweep_at_the_centre)
{
    sweep_t w = make(SWEEP_SINE, 1000u, 400u, 0u, 3u);
    CHECK_EQ(sweep_moves(&w, w.start_ms + 249u), 0u);
    CHECK_EQ(sweep_moves(&w, w.start_ms + 250u), 1u);
    CHECK_EQ(sweep_moves(&w, w.start_ms + 750u), 2u);
    uint16_t c = 0;
    CHECK(sweep_step(&w, w.start_ms + 1249u, &c));
    CHECK(c > 890u);
    CHECK(!sweep_step(&w, w.start_ms + 1250u, &c));
    CHECK_EQ(c, SWEEP_CENTRE);
    CHECK(!w.running);
    CHECK_EQ(sweep_moves(&w, w.start_ms + 5000u), 3u);

    sweep_t d = make(SWEEP_TRIANGLE, 1000u, 400u, 100u, 2u);
    CHECK(sweep_step(&d, d.start_ms + 949u, &c));
    CHECK_EQ(c, 100u);                             /* holding the second end */
    CHECK(!sweep_step(&d, d.start_ms + 950u, &c));
    CHECK_EQ(c, SWEEP_CENTRE);
}

TEST_CASE(a_stopped_sweep_rests_at_the_centre)
{
    sweep_t w = make(SWEEP_SQUARE, 1000u, 400u, 0u, 0u);
    sweep_stop(&w);
    uint16_t c = 0;
    CHECK(!sweep_step(&w, w.start_ms + 10u, &c));
    CHECK_EQ(c, SWEEP_CENTRE);
    sweep_stop(NULL);
    CHECK(!sweep_step(NULL, 0u, &c));
    CHECK_EQ(sweep_moves(NULL, 0u), 0u);
}

TEST_CASE(the_clock_may_wrap)
{
    sweep_t w;
    const sweep_cfg_t cfg = { SWEEP_SINE, 1000u, 400u, 0u, 0u };
    CHECK(sweep_start(&w, &cfg, UINT32_MAX - 100u));
    uint16_t c = 0;
    CHECK(sweep_step(&w, 149u, &c));               /* 250 ms after the start */
    CHECK_EQ(c, 900u);
}

/* Ten hours in, the curve is where it was in the first cycle: the period is
 * kept in integer microseconds. */
TEST_CASE(a_long_sweep_keeps_its_period)
{
    sweep_t w = make(SWEEP_SINE, 1000u, 400u, 0u, 0u);
    const uint32_t ten_hours = 10u * 3600u * 1000u;
    CHECK_EQ(at(&w, ten_hours + 250u), 900u);
    CHECK_EQ(at(&w, ten_hours + 750u), 100u);
    CHECK_EQ(sweep_moves(&w, w.start_ms + ten_hours + 250u),
             2u * 36000u + 1u);
}

int main(void)
{
    RUN(a_configuration_outside_its_ranges_is_refused);
    RUN(a_sine_is_at_its_ends_at_the_quarters);
    RUN(a_triangle_moves_at_one_speed);
    RUN(a_square_jumps_at_the_start_and_the_half);
    RUN(the_dwell_holds_each_end);
    RUN(a_movement_count_ends_the_sweep_at_the_centre);
    RUN(a_stopped_sweep_rests_at_the_centre);
    RUN(the_clock_may_wrap);
    RUN(a_long_sweep_keeps_its_period);
    return test_summary("servo_sweep");
}
