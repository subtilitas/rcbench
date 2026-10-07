/*
 * One servo move judged from its current (servo_move.h), sample by sample.
 *
 * Under test: the threshold from the idle noise; movement, arrival and
 * arrival at an end held harder than the servo moves, at the PD mini's 10
 * readings a second and at the coprocessor's 1 kHz, the latter against the
 * servo model; a reading before the command; the window with the meter's
 * lag, late and unseen; a clock before the command; the filter and its
 * delay; clipped samples, which decide only what their bound decides; a
 * run near the level that spreads wider than the band.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <string.h>

#include "greatest.h"

#include "servo_move.h"
#include "servo_sim.h"

/* A move at 10 readings a second, as the servo test runs it: time in ms,
 * from 0.10 A before the command to an end held at 0.10 A. */
static void pd_move(servo_move_t *m, float rise, float ref, float move_a)
{
    const servo_move_cfg_t c = {
        .cmd_t    = 1000u,
        .window_t = servo_move_window_ms(300u),
        .rise_a   = rise,
        .ref_a    = ref,
        .move_a   = move_a,
        .band_a   = SERVO_MOVE_BAND_A,
        .settle_n = 2u,
        .filter_n = 1u,
    };
    servo_move_begin(m, &c);
}

/* Readings every 100 ms from @p t0, one per value, until the move is over;
 * the state after the last one fed. */
static servo_move_state_t feed(servo_move_t *m, uint32_t t0, const float *a,
                               unsigned n)
{
    servo_move_state_t st = m->state;
    for (unsigned k = 0; k < n && !servo_move_over(m); ++k) {
        st = servo_move_sample(m, t0 + 100u * k, a[k], SERVO_MOVE_CLIP_NONE);
    }
    return st;
}

/* -------------------------------------------------------- the threshold */

TEST_CASE(the_threshold_is_three_noises_or_twenty_milliamps)
{
    CHECK_EQ(servo_move_noise_a(0u, 0.0f, 0.0f), 0.0f);
    /* Ten readings of 0.10 A: no noise, the smallest threshold. */
    CHECK_NEAR(servo_move_noise_a(10u, 1.0f, 0.1f), 0.0f, 1e-4f);
    CHECK_EQ(servo_move_threshold_a(0.0f), SERVO_MOVE_MIN_A);
    /* 0.09 and 0.11 A in turn: 0.010 A, a threshold of 0.030 A. */
    const float n = servo_move_noise_a(2u, 0.20f, 0.09f * 0.09f + 0.11f * 0.11f);
    CHECK_NEAR(n, 0.010f, 1e-5f);
    CHECK_NEAR(servo_move_threshold_a(n), 0.030f, 1e-5f);
    /* A variance below zero from rounding is no noise. */
    CHECK_EQ(servo_move_noise_a(1u, 1.0f, 0.5f), 0.0f);
    CHECK_EQ(servo_move_window_ms(0u), 3000u);
    CHECK_EQ(servo_move_window_ms(300u), 3300u);
}

/* ------------------------------------------------------------ at 10 Hz */

/* Up past the destination's level, then back within the band: arrived at
 * that reading.  The mean and the peak are of the readings before it. */
TEST_CASE(a_reading_back_at_the_level_after_passing_it_is_the_arrival)
{
    servo_move_t m;
    pd_move(&m, 0.10f, 0.10f, 0.020f);
    CHECK_EQ(m.state, SERVO_MOVE_WAITING);
    CHECK(!servo_move_over(&m));
    /* A reading before the command is not the move. */
    CHECK_EQ(servo_move_sample(&m, 999u, 0.90f, SERVO_MOVE_CLIP_NONE),
             SERVO_MOVE_WAITING);
    const float a[] = { 0.11f, 0.40f, 0.60f, 0.50f, 0.12f, 0.10f };
    CHECK_EQ(feed(&m, 1000u, a, 6u), SERVO_MOVE_ARRIVED);
    CHECK(servo_move_over(&m));
    CHECK(servo_move_arrived(&m));
    CHECK(servo_move_moved(&m));
    CHECK_EQ(m.moved_t, 1100u);
    CHECK_EQ(m.end_t, 1400u);
    CHECK_EQ(m.n, 4u);
    CHECK_NEAR(m.sum, 0.11f + 0.40f + 0.60f + 0.50f, 1e-5f);
    CHECK_NEAR(m.peak, 0.60f, 1e-6f);
    CHECK(!m.clipped);
    /* Over: nothing more changes it. */
    CHECK_EQ(servo_move_sample(&m, 1600u, 0.9f, SERVO_MOVE_CLIP_NONE),
             SERVO_MOVE_ARRIVED);
    CHECK_EQ(servo_move_tick(&m, 99999u), SERVO_MOVE_ARRIVED);
    CHECK_EQ(m.end_t, 1400u);
}

/* An end held at 0.50 A, harder than the servo moves at 0.30 A: never
 * passed, so two readings in a row within the band of it and of each
 * other are the arrival, at the first of them. */
TEST_CASE(an_end_held_harder_is_reached_at_two_settled_readings)
{
    servo_move_t m;
    pd_move(&m, 0.10f, 0.50f, 0.020f);
    const float a[] = { 0.30f, 0.30f, 0.48f, 0.51f };
    CHECK_EQ(feed(&m, 1000u, a, 4u), SERVO_MOVE_SETTLED);
    CHECK(servo_move_arrived(&m));
    CHECK_EQ(m.end_t, 1200u);
    CHECK_EQ(m.n, 3u);           /* the first of the two counts */

    /* Two near the level but further apart than the band: a current
     * climbing through it, not there yet. */
    pd_move(&m, 0.10f, 0.50f, 0.020f);
    const float b[] = { 0.30f, 0.455f, 0.51f, 0.50f };
    CHECK_EQ(feed(&m, 1000u, b, 4u), SERVO_MOVE_SETTLED);
    CHECK_EQ(m.end_t, 1200u);    /* 0.51 and 0.50, not 0.455 and 0.51 */
}

/* A burst above the level while the servo accelerates, then below it by
 * more than the band: handed to the settled rule, not arrived on the way
 * back up. */
TEST_CASE(an_acceleration_burst_hands_the_move_to_the_settled_rule)
{
    servo_move_t m;
    pd_move(&m, 0.10f, 0.50f, 0.020f);
    const float a[] = { 0.70f, 0.30f, 0.47f, 0.49f };
    CHECK_EQ(feed(&m, 1000u, a, 2u), SERVO_MOVE_MOVING);
    CHECK(!m.left);
    CHECK_EQ(feed(&m, 1200u, a + 2, 2u), SERVO_MOVE_SETTLED);
    CHECK_EQ(m.end_t, 1200u);
}

/* The peak is the highest valued sample, from the first one on: a move
 * whose samples all lie below 0 A reports the highest of them, not 0. */
TEST_CASE(the_peak_of_samples_below_zero_is_the_highest_of_them)
{
    servo_move_t m;
    pd_move(&m, 0.10f, 0.10f, 0.020f);
    CHECK_EQ(m.peak, 0.0f);
    const float a[] = { -0.30f, -0.20f, -0.25f };
    CHECK_EQ(feed(&m, 1000u, a, 3u), SERVO_MOVE_MOVING);
    CHECK_EQ(m.n, 3u);
    CHECK_NEAR(m.peak, -0.20f, 1e-6f);
}

/* Movement below the level before the command: a servo leaving an end it
 * pushed on. */
TEST_CASE(leaving_an_end_pushed_on_is_movement_downwards)
{
    servo_move_t m;
    pd_move(&m, 0.50f, 0.10f, 0.020f);
    const float a[] = { 0.50f, 0.45f, 0.30f, 0.11f, 0.10f };
    CHECK_EQ(feed(&m, 1000u, a, 2u), SERVO_MOVE_MOVING);
    CHECK_EQ(m.moved_t, 1100u);
    CHECK_EQ(feed(&m, 1200u, a + 2, 3u), SERVO_MOVE_ARRIVED);
    CHECK_EQ(m.end_t, 1300u);    /* above the level at 0.30 A, back at 0.11 */
}

/* No movement in the window: unseen.  Movement and no arrival: late.  The
 * window is SERVO_MOVE_TIMEOUT_MS plus the meter's lag, and a move that
 * arrives inside the lag is timed. */
TEST_CASE(the_window_ends_a_move_late_or_unseen)
{
    servo_move_t m;
    pd_move(&m, 0.10f, 0.10f, 0.020f);
    CHECK_EQ(servo_move_tick(&m, 1000u + 3299u), SERVO_MOVE_WAITING);
    CHECK_EQ(servo_move_tick(&m, 1000u + 3300u), SERVO_MOVE_UNSEEN);
    CHECK(servo_move_over(&m));
    CHECK(!servo_move_moved(&m));
    CHECK(!servo_move_arrived(&m));
    CHECK_EQ(m.end_t, 4300u);

    pd_move(&m, 0.10f, 0.10f, 0.020f);
    const float up[] = { 0.40f };
    CHECK_EQ(feed(&m, 1100u, up, 1u), SERVO_MOVE_MOVING);
    CHECK_EQ(servo_move_tick(&m, 4300u), SERVO_MOVE_LATE);
    CHECK(servo_move_moved(&m));
    CHECK(!servo_move_arrived(&m));

    /* Back at 4100 ms, after 3000 ms but inside the 300 ms lag: timed. */
    pd_move(&m, 0.10f, 0.10f, 0.020f);
    CHECK_EQ(feed(&m, 1100u, up, 1u), SERVO_MOVE_MOVING);
    CHECK_EQ(servo_move_tick(&m, 4100u), SERVO_MOVE_MOVING);
    const float back[] = { 0.10f };
    CHECK_EQ(feed(&m, 4100u, back, 1u), SERVO_MOVE_ARRIVED);
    CHECK_EQ(m.end_t - m.cfg.cmd_t, 3100u);
}

/* The coprocessor stamps the edge ahead of the time it is told: a clock
 * before the command decides nothing. */
TEST_CASE(a_clock_before_the_command_decides_nothing)
{
    servo_move_t m;
    pd_move(&m, 0.10f, 0.10f, 0.020f);
    CHECK_EQ(servo_move_tick(&m, 990u), SERVO_MOVE_WAITING);
    CHECK_EQ(servo_move_tick(&m, 0u), SERVO_MOVE_WAITING);
    /* A move never begun is not over. */
    servo_move_t idle;
    memset(&idle, 0, sizeof(idle));
    CHECK(!servo_move_over(&idle));
    CHECK_EQ(servo_move_sample(&idle, 0u, 1.0f, SERVO_MOVE_CLIP_NONE),
             SERVO_MOVE_IDLE);
    CHECK_EQ(servo_move_tick(&idle, 99999u), SERVO_MOVE_IDLE);
}

/* settle_n under 2 is 2; the filter is held to 1 to SERVO_MOVE_FILTER_MAX. */
TEST_CASE(the_configuration_is_held_to_its_range)
{
    servo_move_t m;
    servo_move_cfg_t c;
    memset(&c, 0, sizeof(c));
    servo_move_begin(&m, &c);
    CHECK_EQ(m.cfg.settle_n, 2u);
    CHECK_EQ(m.cfg.filter_n, 1u);
    c.filter_n = 99u;
    c.settle_n = 5u;
    servo_move_begin(&m, &c);
    CHECK_EQ(m.cfg.filter_n, SERVO_MOVE_FILTER_MAX);
    CHECK_EQ(m.cfg.settle_n, 5u);
}

/* Over more than two readings, a sample near the level that spreads the
 * run wider than the band starts the run again from itself. */
TEST_CASE(a_run_wider_than_the_band_starts_again)
{
    servo_move_t m;
    pd_move(&m, 0.10f, 0.50f, 0.020f);
    m.cfg.settle_n = 3u;
    const float a[] = { 0.30f, 0.46f, 0.50f, 0.515f, 0.51f, 0.505f };
    CHECK_EQ(feed(&m, 1000u, a, 6u), SERVO_MOVE_SETTLED);
    /* 0.46 to 0.515 spreads 0.055: the run starts again at 0.515. */
    CHECK_EQ(m.end_t, 1300u);
}

/* A current that stays between the threshold and the band above the level
 * is still on its way: the run near the level keeps counting, held at
 * 255, without settling. */
TEST_CASE(a_long_run_above_the_level_is_counted_to_its_limit)
{
    servo_move_t m;
    pd_move(&m, 0.10f, 0.10f, 0.020f);
    m.cfg.window_t = 100000u;
    for (uint32_t k = 0; k < 300u; ++k) {
        CHECK_EQ(servo_move_sample(&m, 1000u + k, 0.13f, SERVO_MOVE_CLIP_NONE),
                 SERVO_MOVE_MOVING);
    }
    CHECK(m.left);
    CHECK_EQ(m.run_n, 255u);
    CHECK_EQ(servo_move_sample(&m, 1300u, 0.11f, SERVO_MOVE_CLIP_NONE),
             SERVO_MOVE_ARRIVED);
}

/* ---------------------------------------------------------------- clips */

/* A clipped sample is at or past its bound: it is movement when the bound
 * is past the threshold, not yet there when the bound is above the level
 * by the threshold, never an arrival, and out of the mean and the peak. */
TEST_CASE(a_clipped_sample_decides_only_what_its_bound_decides)
{
    servo_move_t m;
    pd_move(&m, 0.10f, 0.10f, 0.020f);
    /* A bound of 0.11 A says nothing about movement. */
    CHECK_EQ(servo_move_sample(&m, 1000u, 0.11f, SERVO_MOVE_CLIP_HIGH),
             SERVO_MOVE_WAITING);
    CHECK(m.clipped);
    CHECK_EQ(servo_move_sample(&m, 1100u, 1.6376f, SERVO_MOVE_CLIP_HIGH),
             SERVO_MOVE_MOVING);
    CHECK(m.left);
    CHECK_EQ(m.n, 0u);
    CHECK_EQ(m.peak, 0.0f);
    CHECK_EQ(servo_move_sample(&m, 1200u, 0.40f, SERVO_MOVE_CLIP_NONE),
             SERVO_MOVE_MOVING);
    CHECK_EQ(servo_move_sample(&m, 1300u, 0.11f, SERVO_MOVE_CLIP_NONE),
             SERVO_MOVE_ARRIVED);
    CHECK_EQ(m.n, 1u);
    CHECK_NEAR(m.peak, 0.40f, 1e-6f);

    /* At the bottom of the range: movement down from 0.50 A, and back
     * below the level, which hands the move to the settled rule. */
    pd_move(&m, 0.50f, 0.10f, 0.020f);
    CHECK_EQ(servo_move_sample(&m, 1000u, 0.80f, SERVO_MOVE_CLIP_NONE),
             SERVO_MOVE_MOVING);
    CHECK(m.left);
    CHECK_EQ(servo_move_sample(&m, 1100u, -1.6376f, SERVO_MOVE_CLIP_LOW),
             SERVO_MOVE_MOVING);
    CHECK(!m.left);
    /* A low bound above the level: nothing. */
    pd_move(&m, 0.50f, 0.10f, 0.020f);
    CHECK_EQ(servo_move_sample(&m, 1000u, 0.49f, SERVO_MOVE_CLIP_LOW),
             SERVO_MOVE_WAITING);
    CHECK(!m.left);
}

/* ---------------------------------------------------------------- filter */

/* Four samples' mean: a step shows after (4 - 1) / 2 samples on average,
 * the samples before the command fill it, and a window holding clips at
 * both ends decides nothing. */
TEST_CASE(the_filter_averages_four_samples)
{
    servo_move_t m;
    servo_move_cfg_t c = {
        .cmd_t = 100u, .window_t = 30030u, .rise_a = 0.10f, .ref_a = 0.10f,
        .move_a = 0.30f, .band_a = 0.05f, .settle_n = 10u, .filter_n = 4u,
    };
    servo_move_begin(&m, &c);
    for (uint32_t t = 60u; t < 100u; t += 10u) {
        CHECK_EQ(servo_move_sample(&m, t, 0.10f, SERVO_MOVE_CLIP_NONE),
                 SERVO_MOVE_WAITING);
    }
    /* A step to 0.90 A at the command: 0.30, 0.50, 0.70 ... */
    CHECK_EQ(servo_move_sample(&m, 100u, 0.90f, SERVO_MOVE_CLIP_NONE),
             SERVO_MOVE_WAITING);
    CHECK_EQ(servo_move_sample(&m, 110u, 0.90f, SERVO_MOVE_CLIP_NONE),
             SERVO_MOVE_MOVING);
    CHECK_EQ(m.moved_t, 110u);
    CHECK_NEAR(m.sum, 0.30f + 0.50f, 1e-5f);

    /* Clips at both ends in one window: no value. */
    servo_move_begin(&m, &c);
    CHECK_EQ(servo_move_sample(&m, 100u, 1.6f, SERVO_MOVE_CLIP_HIGH),
             SERVO_MOVE_MOVING);
    CHECK_EQ(servo_move_sample(&m, 110u, -1.6f, SERVO_MOVE_CLIP_LOW),
             SERVO_MOVE_MOVING);
    CHECK(m.clipped);
    CHECK_EQ(m.run_n, 0u);
    /* Before the command, the same window only fills the filter. */
    servo_move_begin(&m, &c);
    CHECK_EQ(servo_move_sample(&m, 80u, 1.6f, SERVO_MOVE_CLIP_HIGH),
             SERVO_MOVE_WAITING);
    CHECK_EQ(servo_move_sample(&m, 90u, -1.6f, SERVO_MOVE_CLIP_LOW),
             SERVO_MOVE_WAITING);
    CHECK(!m.clipped);
}

/* Samples held from before the move began fill the filter as if fed then:
 * a step of 0.10 A at the command is a quarter of that against three
 * primed samples, under a 0.030 A threshold.  Unfiltered, or once over,
 * priming does nothing. */
TEST_CASE(primed_samples_fill_the_filter)
{
    servo_move_t m;
    servo_move_cfg_t c = {
        .cmd_t = 100u, .window_t = 30030u, .rise_a = 0.12f, .ref_a = 0.12f,
        .move_a = 0.030f, .band_a = 0.05f, .settle_n = 10u, .filter_n = 4u,
    };
    servo_move_begin(&m, &c);
    for (unsigned k = 0; k < 3u; ++k) {
        servo_move_prime(&m, 0.12f, SERVO_MOVE_CLIP_NONE);
    }
    CHECK_EQ(m.f_n, 3u);
    CHECK_EQ(servo_move_sample(&m, 100u, 0.22f, SERVO_MOVE_CLIP_NONE),
             SERVO_MOVE_WAITING);
    CHECK_NEAR(m.sum, 0.145f, 1e-5f);

    c.filter_n = 1u;
    servo_move_begin(&m, &c);
    servo_move_prime(&m, 0.12f, SERVO_MOVE_CLIP_NONE);
    CHECK_EQ(m.f_n, 0u);
    CHECK_EQ(servo_move_sample(&m, 100u, 0.22f, SERVO_MOVE_CLIP_NONE),
             SERVO_MOVE_MOVING);
    CHECK_EQ(servo_move_tick(&m, 100u + 30030u), SERVO_MOVE_LATE);
    servo_move_prime(&m, 0.12f, SERVO_MOVE_CLIP_NONE);
    CHECK_EQ(m.f_n, 0u);
}

/* ------------------------------------------------- against the servo model */

/* The modelled servo's move from 1100 to 1900 us takes 800 us at 1.2 us/ms:
 * 667 ms, drawing 0.95 A moving and 0.12 A holding, with 0.02 A of noise.
 * At 1 kHz, times in 0.1 ms, the four-sample filter and ten samples to
 * settle, the arrival comes within the filter's lag of the model's; at 10
 * readings a second, up to a reading later. */
static uint32_t model_move(uint32_t every_ms, uint32_t t_per_ms,
                           uint8_t filter_n, uint8_t settle_n,
                           servo_move_state_t *how)
{
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    sc.stop_lo_us = 1000u;
    sc.stop_hi_us = 2000u;
    servo_sim_t sim;
    servo_sim_init(&sim, &sc);
    sim.position_us = 1100.0f;
    (void)servo_sim_step(&sim, 1100u, 0u);
    servo_move_t m;
    const servo_move_cfg_t c = {
        .cmd_t    = 1000u * t_per_ms,
        .window_t = servo_move_window_ms(3u) * t_per_ms,
        .rise_a   = 0.12f,
        .ref_a    = 0.12f,
        .move_a   = servo_move_threshold_a(0.02f / 3.464f),
        .band_a   = SERVO_MOVE_BAND_A,
        .settle_n = settle_n,
        .filter_n = filter_n,
    };
    servo_move_begin(&m, &c);
    for (uint32_t ms = 1u; ms < 6000u && !servo_move_over(&m); ++ms) {
        const uint16_t cmd = (ms >= 1000u) ? 1900u : 1100u;
        const float a = servo_sim_step(&sim, cmd, ms);
        if (ms % every_ms == 0u) {
            servo_move_sample(&m, ms * t_per_ms, a, SERVO_MOVE_CLIP_NONE);
        }
        servo_move_tick(&m, ms * t_per_ms);
    }
    *how = m.state;
    CHECK_NEAR(m.sum / (float)m.n, 0.95f, 0.06f);
    return (m.end_t - m.cfg.cmd_t) / t_per_ms;
}

TEST_CASE(a_modelled_move_is_timed_within_the_filter_at_1_khz)
{
    servo_move_state_t how = SERVO_MOVE_IDLE;
    const uint32_t fast = model_move(1u, 10u, 4u, 10u, &how);
    CHECK_EQ(how, SERVO_MOVE_ARRIVED);
    CHECK(fast >= 667u);
    CHECK(fast <= 667u + 4u);
    const uint32_t slow = model_move(100u, 1u, 1u, 2u, &how);
    CHECK_EQ(how, SERVO_MOVE_ARRIVED);
    CHECK(slow >= 667u);
    CHECK(slow <= 667u + 100u);
    CHECK(slow > fast);
}

/* A servo commanded past its stop pushes there at more than it moves: the
 * stop is never passed, and ten samples within the band of its level are
 * the arrival, at the first of them. */
TEST_CASE(a_modelled_stop_is_reached_settled_at_1_khz)
{
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    sc.stop_lo_us = 1000u;
    sc.stop_hi_us = 1880u;
    sc.travel_a = 0.30f;
    sc.stall_a = 1.20f;
    sc.bind_us = 40u;
    servo_sim_t sim;
    servo_sim_init(&sim, &sc);
    sim.position_us = 1100.0f;
    (void)servo_sim_step(&sim, 1100u, 0u);
    /* 1900 us is 20 us past the stop: half way to stall, 0.66 A. */
    const float held = 0.12f + (1.20f - 0.12f) * 0.5f;
    servo_move_t m;
    const servo_move_cfg_t c = {
        .cmd_t = 10000u, .window_t = 30030u, .rise_a = 0.12f, .ref_a = held,
        .move_a = 0.030f, .band_a = SERVO_MOVE_BAND_A, .settle_n = 10u,
        .filter_n = 4u,
    };
    servo_move_begin(&m, &c);
    for (uint32_t ms = 1u; ms < 6000u && !servo_move_over(&m); ++ms) {
        const float a = servo_sim_step(&sim, (ms >= 1000u) ? 1900u : 1100u, ms);
        servo_move_sample(&m, ms * 10u, a, SERVO_MOVE_CLIP_NONE);
    }
    CHECK_EQ(m.state, SERVO_MOVE_SETTLED);
    /* 780 us at 1.2 us/ms: 650 ms, and the filter's lag. */
    const uint32_t ms = (m.end_t - m.cfg.cmd_t) / 10u;
    CHECK(ms >= 650u);
    CHECK(ms <= 650u + 4u);
}

int main(void)
{
    RUN(the_threshold_is_three_noises_or_twenty_milliamps);
    RUN(a_reading_back_at_the_level_after_passing_it_is_the_arrival);
    RUN(an_end_held_harder_is_reached_at_two_settled_readings);
    RUN(an_acceleration_burst_hands_the_move_to_the_settled_rule);
    RUN(the_peak_of_samples_below_zero_is_the_highest_of_them);
    RUN(leaving_an_end_pushed_on_is_movement_downwards);
    RUN(the_window_ends_a_move_late_or_unseen);
    RUN(a_clock_before_the_command_decides_nothing);
    RUN(the_configuration_is_held_to_its_range);
    RUN(a_run_wider_than_the_band_starts_again);
    RUN(a_long_run_above_the_level_is_counted_to_its_limit);
    RUN(a_clipped_sample_decides_only_what_its_bound_decides);
    RUN(the_filter_averages_four_samples);
    RUN(primed_samples_fill_the_filter);
    RUN(a_modelled_move_is_timed_within_the_filter_at_1_khz);
    RUN(a_modelled_stop_is_reached_settled_at_1_khz);
    return test_summary("servo_move");
}
