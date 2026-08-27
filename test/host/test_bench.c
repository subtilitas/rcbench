/*
 * The bench's model: what the numbers mean on the wire, the rules for asking
 * a motor to spin, and a simulator that has to be honest about being one.
 */
#include <math.h>
#include <string.h>

#include "greatest.h"

#include "bench_state.h"
#include "link_pages.h"
#include "telemetry_sim.h"
#include "throttle.h"

/* ----------------------------------------------------------- the wire form */

/* Every field, at both ends of its range, through the fixed point and back.
 * A scale that disagrees with the wire is a wrong reading rather than a
 * crash, which is the kind that ships. */
TEST_CASE(every_field_survives_the_round_trip)
{
    bench_state_t in;
    memset(&in, 0, sizeof(in));
    in.voltage     = 24.31f;
    in.current     = 68.14f;
    in.power       = 1656.0f;
    in.rpm         = 13581.0f;
    in.temp_esc    = 46.3f;
    in.temp_motor  = 58.9f;
    in.charge_mah  = 1843.0f;
    in.energy_wh   = 44.7f;
    in.voltage_min = 20.71f;
    in.current_max = 71.0f;
    in.power_max   = 1701.0f;
    in.rpm_max     = 13900.0f;
    in.flags       = LINK_BN_VOLTAGE_OK | LINK_BN_SIMULATED;

    uint16_t regs[LINK_BN_COUNT];
    bench_state_to_regs(&in, regs);

    bench_state_t out;
    memset(&out, 0, sizeof(out));
    bench_state_from_regs(&out, regs, 0, LINK_BN_COUNT);

    CHECK_NEAR(out.voltage,     in.voltage,     0.01f);
    CHECK_NEAR(out.current,     in.current,     0.01f);
    CHECK_NEAR(out.power,       in.power,       1.0f);
    CHECK_NEAR(out.rpm,         in.rpm,         1.0f);
    CHECK_NEAR(out.temp_esc,    in.temp_esc,    0.1f);
    CHECK_NEAR(out.temp_motor,  in.temp_motor,  0.1f);
    CHECK_NEAR(out.charge_mah,  in.charge_mah,  1.0f);
    CHECK_NEAR(out.energy_wh,   in.energy_wh,   0.1f);
    CHECK_NEAR(out.voltage_min, in.voltage_min, 0.01f);
    CHECK_NEAR(out.current_max, in.current_max, 0.01f);
    CHECK_EQ(out.flags, in.flags);
    CHECK(out.valid);
}

/* Temperature is the only signed field, and a cold bench is a real reading. */
TEST_CASE(temperature_survives_going_below_zero)
{
    bench_state_t in;
    memset(&in, 0, sizeof(in));
    in.temp_esc   = -12.4f;
    in.temp_motor = -0.1f;

    uint16_t regs[LINK_BN_COUNT];
    bench_state_to_regs(&in, regs);
    bench_state_t out;
    memset(&out, 0, sizeof(out));
    bench_state_from_regs(&out, regs, 0, LINK_BN_COUNT);

    CHECK_NEAR(out.temp_esc, -12.4f, 0.1f);
    CHECK_NEAR(out.temp_motor, -0.1f, 0.1f);
}

/* A value past what sixteen bits hold has to clamp, not wrap: a 700 A reading
 * that wrapped to 44 A is a reading somebody would believe. */
TEST_CASE(an_out_of_range_value_clamps_rather_than_wraps)
{
    bench_state_t in;
    memset(&in, 0, sizeof(in));
    in.voltage = 900.0f;      /* past 655.35 V */
    in.current = 700.0f;      /* past 655.35 A */
    in.temp_esc = 5000.0f;    /* past 3276.7 C */
    in.rpm = -50.0f;

    uint16_t regs[LINK_BN_COUNT];
    bench_state_to_regs(&in, regs);
    bench_state_t out;
    memset(&out, 0, sizeof(out));
    bench_state_from_regs(&out, regs, 0, LINK_BN_COUNT);

    CHECK(out.voltage > 600.0f);
    CHECK(out.current > 600.0f);
    CHECK(out.temp_esc > 3000.0f);
    CHECK_EQ(out.rpm, 0.0f);
}

/*
 * A short read fills what arrived and leaves the rest alone, so a host that
 * polls four registers at 20 Hz and the whole page once a second gets a
 * coherent state either way.
 */
TEST_CASE(a_partial_page_leaves_the_rest_alone)
{
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    b.rpm_max = 9999.0f;
    b.energy_wh = 12.5f;

    const uint16_t live[4] = { 2431, 6814, 1656, 13581 };
    bench_state_from_regs(&b, live, LINK_BN_VOLTAGE_CV, 4);

    CHECK_NEAR(b.voltage, 24.31f, 0.01f);
    CHECK_NEAR(b.rpm, 13581.0f, 1.0f);
    CHECK_EQ(b.rpm_max, 9999.0f);      /* untouched */
    CHECK_NEAR(b.energy_wh, 12.5f, 0.01f);
}

/* The sag floor resets to the reading, not to zero -- a floor of zero volts
 * would read as a pack that had collapsed. */
TEST_CASE(resetting_peaks_does_not_invent_a_collapsed_pack)
{
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    b.voltage = 24.0f; b.voltage_min = 19.0f;
    b.current = 3.0f;  b.current_max = 70.0f;

    bench_state_reset_peaks(&b);
    CHECK_EQ(b.voltage_min, 24.0f);
    CHECK_EQ(b.current_max, 3.0f);
    CHECK_EQ(b.voltage, 24.0f);   /* the live reading is not disturbed */
}

/* ------------------------------------------------------------- the throttle */

TEST_CASE(throttle_set_while_disarmed_is_remembered_and_not_emitted)
{
    throttle_t t;
    throttle_init(&t, NULL, 0);
    throttle_set(&t, 60.0f, 0);

    CHECK_EQ(t.command, 60.0f);
    for (int i = 0; i < 100; ++i) {
        CHECK_EQ(throttle_step(&t, 0.05f), 0.0f);
    }

    throttle_arm(&t, true, 0);
    CHECK(throttle_step(&t, 0.05f) > 0.0f);
    CHECK_EQ(t.command, 60.0f);   /* arming did not lose the slider */
}

TEST_CASE(the_ramp_limits_the_way_up_and_not_the_way_down)
{
    throttle_t t;
    throttle_init(&t, NULL, 0);
    throttle_set_rate(&t, 50.0f);   /* 50 %/s */
    throttle_arm(&t, true, 0);
    throttle_set(&t, 100.0f, 0);

    /* One second of ramp gets halfway, not all the way. */
    const float after_1s = throttle_step(&t, 1.0f);
    CHECK_NEAR(after_1s, 50.0f, 0.5f);

    /* Down is immediate: a slew limit on stopping helps nobody. */
    throttle_set(&t, 0.0f, 0);
    CHECK_EQ(throttle_step(&t, 0.001f), 0.0f);
}

TEST_CASE(disarming_drops_to_idle_with_no_ramp)
{
    throttle_t t;
    throttle_init(&t, NULL, 0);
    throttle_arm(&t, true, 0);
    throttle_set(&t, 80.0f, 0);
    for (int i = 0; i < 40; ++i) { throttle_step(&t, 0.05f); }
    CHECK(t.actual > 50.0f);

    throttle_arm(&t, false, 0);
    CHECK_EQ(t.actual, 0.0f);
    CHECK_EQ(throttle_step(&t, 1.0f), 0.0f);
}

/* It refuses to hold a throttle nobody is watching. */
TEST_CASE(silence_from_the_commander_is_overdue)
{
    throttle_t t;
    throttle_init(&t, NULL, 1000);
    CHECK(!throttle_overdue(&t, 1000 + 499));
    CHECK(throttle_overdue(&t, 1000 + 500));

    throttle_keepalive(&t, 2000);
    CHECK(!throttle_overdue(&t, 2400));
}

/* Wrap-safe, like both link watchdogs: the deadline is sampled before the
 * turnover, which is where a naive comparison fires a whole interval early. */
TEST_CASE(the_command_timeout_survives_the_millisecond_wrap)
{
    throttle_t t;
    const uint32_t near_wrap = (uint32_t)0u - 250u;
    throttle_init(&t, NULL, near_wrap);
    CHECK(near_wrap + 500u < near_wrap);

    CHECK(!throttle_overdue(&t, near_wrap + 125u));
    CHECK(!throttle_overdue(&t, near_wrap + 499u));
    CHECK(throttle_overdue(&t, near_wrap + 500u));
}

/* ------------------------------------------------------------ the simulator */

/* It must declare itself.  A modelled number that does not is the entire
 * risk of having a simulator at all. */
TEST_CASE(the_simulator_flags_everything_it_produces)
{
    telemetry_sim_t s;
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    telemetry_sim_init(&s, NULL);
    telemetry_sim_step(&s, 50.0f, 0.05f, &b);

    CHECK(b.flags & LINK_BN_SIMULATED);
    CHECK(bench_state_simulated(&b));

    /* And it survives the wire, so a remote fake declares itself too. */
    uint16_t regs[LINK_BN_COUNT];
    bench_state_to_regs(&b, regs);
    bench_state_t out;
    memset(&out, 0, sizeof(out));
    bench_state_from_regs(&out, regs, 0, LINK_BN_COUNT);
    CHECK(bench_state_simulated(&out));
}

/*
 * The three behaviours a bench exists to show, which a flattering simulator
 * would hide: the bus sags under load, current grows faster than throttle,
 * and rpm follows the sagging bus rather than the stick.
 */
TEST_CASE(the_model_sags_under_load)
{
    telemetry_sim_t s;
    bench_state_t idle, loaded;
    memset(&idle, 0, sizeof(idle));
    memset(&loaded, 0, sizeof(loaded));

    telemetry_sim_init(&s, NULL);
    telemetry_sim_step(&s, 0.0f, 0.05f, &idle);
    const float open_v = idle.voltage;

    telemetry_sim_init(&s, NULL);
    for (int i = 0; i < 40; ++i) {
        telemetry_sim_step(&s, 100.0f, 0.05f, &loaded);
    }
    CHECK(loaded.voltage < open_v - 1.0f);
    CHECK(loaded.current > 40.0f);
}

TEST_CASE(current_grows_faster_than_throttle)
{
    telemetry_sim_t a, b;
    bench_state_t half, full;
    memset(&half, 0, sizeof(half));
    memset(&full, 0, sizeof(full));

    telemetry_sim_init(&a, NULL);
    telemetry_sim_step(&a, 50.0f, 0.05f, &half);
    telemetry_sim_init(&b, NULL);
    telemetry_sim_step(&b, 100.0f, 0.05f, &full);

    /* Doubling the throttle must more than double the current. */
    CHECK(full.current > half.current * 2.5f);
}

TEST_CASE(rpm_lags_a_step_rather_than_following_it)
{
    telemetry_sim_t s;
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    telemetry_sim_init(&s, NULL);

    telemetry_sim_step(&s, 100.0f, 0.02f, &b);
    const float first = b.rpm;
    for (int i = 0; i < 100; ++i) {
        telemetry_sim_step(&s, 100.0f, 0.02f, &b);
    }
    CHECK(first < b.rpm * 0.3f);   /* nowhere near, one tick in */
    CHECK(b.rpm > 8000.0f);        /* and it does get there */
}

/* The peaks are accumulated by whoever produces the numbers, so the seam
 * behaves the same from either side. */
TEST_CASE(the_simulator_accumulates_peaks_like_the_coprocessor_would)
{
    telemetry_sim_t s;
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    telemetry_sim_init(&s, NULL);

    for (int i = 0; i < 40; ++i) { telemetry_sim_step(&s, 100.0f, 0.05f, &b); }
    const float peak_a = b.current_max;
    const float sag_v  = b.voltage_min;
    CHECK(peak_a > 40.0f);

    /*
     * The sag is compared after the load comes off, not during it.  Under
     * load the floor *is* the present reading, so an earlier version of this
     * case asserted sag_v < voltage while they were necessarily equal.
     */
    for (int i = 0; i < 40; ++i) { telemetry_sim_step(&s, 0.0f, 0.05f, &b); }
    CHECK(b.current < 5.0f);
    CHECK(sag_v < b.voltage);          /* it went lower than it sits now */
    CHECK_EQ(b.current_max, peak_a);   /* and both are held */
    CHECK_EQ(b.voltage_min, sag_v);
}

TEST_CASE(charge_and_energy_only_accumulate)
{
    telemetry_sim_t s;
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    telemetry_sim_init(&s, NULL);

    float last_mah = 0.0f, last_wh = 0.0f;
    for (int i = 0; i < 200; ++i) {
        telemetry_sim_step(&s, (i % 2) ? 80.0f : 10.0f, 0.05f, &b);
        CHECK(b.charge_mah >= last_mah);
        CHECK(b.energy_wh >= last_wh);
        last_mah = b.charge_mah;
        last_wh  = b.energy_wh;
    }
    CHECK(last_mah > 0.0f);
}

int main(void)
{
    RUN(every_field_survives_the_round_trip);
    RUN(temperature_survives_going_below_zero);
    RUN(an_out_of_range_value_clamps_rather_than_wraps);
    RUN(a_partial_page_leaves_the_rest_alone);
    RUN(resetting_peaks_does_not_invent_a_collapsed_pack);
    RUN(throttle_set_while_disarmed_is_remembered_and_not_emitted);
    RUN(the_ramp_limits_the_way_up_and_not_the_way_down);
    RUN(disarming_drops_to_idle_with_no_ramp);
    RUN(silence_from_the_commander_is_overdue);
    RUN(the_command_timeout_survives_the_millisecond_wrap);
    RUN(the_simulator_flags_everything_it_produces);
    RUN(the_model_sags_under_load);
    RUN(current_grows_faster_than_throttle);
    RUN(rpm_lags_a_step_rather_than_following_it);
    RUN(the_simulator_accumulates_peaks_like_the_coprocessor_would);
    RUN(charge_and_energy_only_accumulate);
    return test_summary("bench");
}
