/*
 * The bench model: the fixed-point wire form of bench_state, and a simulator
 * that declares its output as simulated.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <string.h>

#include "greatest.h"

#include "bench_state.h"
#include "link_pages.h"
#include "telemetry_sim.h"

/* ----------------------------------------------------------- the wire form */

/* Every field through the fixed-point form and back.  A scale that disagrees
 * with the wire is a wrong reading rather than a crash. */
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

/* A value past what 16 bits hold clamps rather than wraps: 700 A wrapped
 * would read as about 44 A. */
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

/*
 * The case that made this a function rather than four comparisons: a run that
 * starts before any voltage has arrived.
 *
 * The extended-telemetry frames an ESC sends carry voltage a few times a
 * second while the bench samples at fifty, so the reset that opens a run
 * usually happens with the field still empty.  A floor seeded from that empty
 * field is zero, and no real reading is ever below it, so the whole run
 * reports a pack that collapsed to nothing.
 */
TEST_CASE(a_run_that_starts_before_the_first_voltage_still_finds_its_floor)
{
    bench_state_t b;
    memset(&b, 0, sizeof(b));

    /* Armed, and nothing has answered yet. */
    bench_state_reset_peaks(&b);
    CHECK(!b.sag_seeded);
    bench_state_track_peaks(&b);
    CHECK_EQ(b.voltage_min, 0.0f);   /* still nothing to say */

    /* The first voltage arrives.  It is the floor, not a reading above it. */
    b.voltage = 24.0f;
    b.flags  |= (uint16_t)LINK_BN_VOLTAGE_OK;
    bench_state_track_peaks(&b);
    CHECK_EQ(b.voltage_min, 24.0f);
    CHECK(b.sag_seeded);

    /* And it sags from there. */
    b.voltage = 21.5f;
    bench_state_track_peaks(&b);
    CHECK_EQ(b.voltage_min, 21.5f);

    b.voltage = 23.0f;
    bench_state_track_peaks(&b);
    CHECK_EQ(b.voltage_min, 21.5f);   /* a recovery is not a new floor */
}

/*
 * A field with no valid bit is not a measurement of zero.  Tracking one would
 * put a current peak of zero beside a sag floor of zero and call both of them
 * results.
 */
TEST_CASE(peaks_ignore_the_fields_nothing_answered_for)
{
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    b.voltage = 24.0f;
    b.flags   = (uint16_t)LINK_BN_VOLTAGE_OK;
    bench_state_reset_peaks(&b);

    /* Current never answered, so its peak stays where the reset left it even
     * though the live field reads high. */
    b.current = 55.0f;
    bench_state_track_peaks(&b);
    CHECK_EQ(b.current_max, 0.0f);

    b.flags |= (uint16_t)LINK_BN_CURRENT_OK;
    bench_state_track_peaks(&b);
    CHECK_EQ(b.current_max, 55.0f);

    /* And the voltage floor was a measurement from the start, because the
     * reset happened with a valid reading in hand. */
    CHECK_EQ(b.voltage_min, 24.0f);

    /*
     * Power and speed carry no valid bit of their own.  Power is a product
     * that stays at zero unless both halves arrived, and speed is empty when
     * no reply carried one, so a zero cannot raise either peak.
     */
    b.power = 1320.0f;
    b.rpm   = 24500.0f;
    bench_state_track_peaks(&b);
    CHECK_EQ(b.power_max, 1320.0f);
    CHECK_EQ(b.rpm_max, 24500.0f);

    b.power = 900.0f;
    b.rpm   = 10000.0f;
    bench_state_track_peaks(&b);
    CHECK_EQ(b.power_max, 1320.0f);   /* a peak is not the live reading */
    CHECK_EQ(b.rpm_max, 24500.0f);

    bench_state_track_peaks(NULL);    /* refused rather than dereferenced */
}

/* ------------------------------------------------------------ the simulator */

/* Every value the simulator produces carries LINK_BN_SIMULATED. */
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
 * Three behaviours of the model: the bus sags under load, current grows
 * faster than throttle, and rpm (revolutions per minute) lags a throttle
 * step.
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
     * The sag is compared after the load comes off.  Under load the floor
     * equals the present reading, so sag_v < voltage holds only afterwards.
     */
    for (int i = 0; i < 40; ++i) { telemetry_sim_step(&s, 0.0f, 0.05f, &b); }
    CHECK(b.current < 5.0f);
    CHECK(sag_v < b.voltage);          /* lower than the present reading */
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

/* ------------------------------------------- the run's charge and energy */

static bench_state_t measured(float v, float a, uint16_t flags)
{
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    b.voltage = v;
    b.current = a;
    b.power   = v * a;
    b.flags   = flags;
    return b;
}

TEST_CASE(charge_and_energy_count_what_was_measured_while_driving)
{
    bench_totals_t t;
    bench_totals_reset(&t);
    const bench_state_t b = measured(16.0f, 36.0f,
                                     LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK);
    for (int i = 0; i < 100; ++i) {          /* 100 s at 36 A and 576 W */
        bench_totals_count(&t, &b, 1.0f, true);
    }
    CHECK_NEAR(t.mah, 1000.0f, 0.5f);
    CHECK_NEAR(t.wh, 16.0f, 0.01f);
    CHECK_EQ(t.counted, BENCH_COUNTED_CHARGE | BENCH_COUNTED_ENERGY);
}

TEST_CASE(nothing_is_counted_while_disarmed_or_unmeasured)
{
    bench_totals_t t;
    bench_totals_reset(&t);
    bench_state_t b = measured(16.0f, 36.0f,
                               LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK);
    bench_totals_count(&t, &b, 1.0f, false);
    CHECK_EQ(t.mah, 0.0f);
    CHECK_EQ(t.counted, 0u);

    /* A voltage with no current counts neither; a current with no voltage
     * counts charge and not energy. */
    b = measured(16.0f, 36.0f, LINK_BN_VOLTAGE_OK);
    bench_totals_count(&t, &b, 1.0f, true);
    CHECK_EQ(t.mah, 0.0f);
    CHECK_EQ(t.counted, 0u);
    b = measured(16.0f, 36.0f, LINK_BN_CURRENT_OK);
    bench_totals_count(&t, &b, 1.0f, true);
    CHECK_NEAR(t.mah, 10.0f, 0.01f);
    CHECK_EQ(t.wh, 0.0f);
    CHECK_EQ(t.counted, BENCH_COUNTED_CHARGE);
}

TEST_CASE(one_count_runs_through_a_change_of_source)
{
    /*
     * The coprocessor's readings for 10 s, the panel's model for 2 s while
     * the link is down, then the coprocessor again for 3 s.  Each source
     * writes its own charge over the field -- the coprocessor its empty
     * register -- and the count is shown over it every time.  It never goes
     * back, and the model's seconds are in it.
     */
    bench_totals_t t;
    bench_totals_reset(&t);
    float last = 0.0f;
    for (int i = 0; i < 15; ++i) {
        bench_state_t b = measured(16.0f, 36.0f,
                                   LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK);
        b.charge_mah = (i >= 10 && i < 12) ? 3.0f : 0.0f;  /* each source's own */
        bench_totals_count(&t, &b, 1.0f, true);
        bench_totals_show(&t, &b);
        CHECK(b.charge_mah >= last);
        last = b.charge_mah;
        CHECK_EQ(b.counted, BENCH_COUNTED_CHARGE | BENCH_COUNTED_ENERGY);
    }
    CHECK_NEAR(last, 150.0f, 0.1f);
}

TEST_CASE(the_totals_outlast_the_run_and_reset_at_the_next)
{
    bench_totals_t t;
    bench_totals_reset(&t);
    const bench_state_t b = measured(16.0f, 36.0f,
                                     LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK);
    bench_totals_count(&t, &b, 1.0f, true);
    /* Disarmed, a source that reports nothing at all: the totals stand. */
    bench_state_t empty;
    memset(&empty, 0, sizeof(empty));
    bench_totals_count(&t, &empty, 1.0f, false);
    bench_totals_show(&t, &empty);
    CHECK_NEAR(empty.charge_mah, 10.0f, 0.01f);
    CHECK_EQ(empty.counted, BENCH_COUNTED_CHARGE | BENCH_COUNTED_ENERGY);

    bench_totals_reset(&t);
    bench_totals_show(&t, &empty);
    CHECK_EQ(empty.charge_mah, 0.0f);
    CHECK_EQ(empty.counted, 0u);
}

TEST_CASE(a_stalled_loop_counts_at_most_one_step)
{
    bench_totals_t t;
    bench_totals_reset(&t);
    const bench_state_t b = measured(16.0f, 36.0f,
                                     LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK);
    bench_totals_count(&t, &b, 60.0f, true);     /* a minute's stall */
    CHECK_NEAR(t.mah, 10.0f, 0.01f);             /* one second's worth */
    bench_totals_count(&t, &b, -1.0f, true);
    CHECK_NEAR(t.mah, 10.0f, 0.01f);
    bench_totals_count(NULL, &b, 1.0f, true);
    bench_totals_count(&t, NULL, 1.0f, true);
    bench_totals_show(NULL, NULL);
    bench_totals_reset(NULL);
}

/*
 * The INA228 counts while BENCH says its totals are the run's: its charge
 * and energy are the count and nothing the panel saw is added.  When the
 * bit goes the count carries on from the part's last total, and never goes
 * back.
 */
TEST_CASE(the_ina228_counts_while_its_totals_are_the_runs)
{
    bench_totals_t t;
    bench_totals_reset(&t);
    const uint16_t sensed = (uint16_t)(LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK
                                       | LINK_BN_SENSED | LINK_BN_TOTALS_OK);
    /* The run's first sample can be the last run's page: its totals are
     * not taken, and the panel counts that sample itself. */
    bench_state_t b = measured(16.0f, 36.0f, sensed);
    b.charge_mah = 900.0f;
    b.energy_wh  = 14.0f;
    bench_totals_count(&t, &b, 0.05f, true);
    bench_totals_show(&t, &b);
    CHECK_NEAR(b.charge_mah, 0.5f, 0.001f);
    bench_totals_count(&t, &b, 0.05f, true);
    CHECK_NEAR(t.mah, 1.0f, 0.001f);

    b = measured(16.0f, 36.0f, sensed);
    b.charge_mah = 120.0f;
    b.energy_wh  = 1.9f;
    bench_totals_count(&t, &b, 1.0f, true);
    bench_totals_show(&t, &b);
    CHECK_NEAR(b.charge_mah, 120.0f, 0.001f);
    CHECK_NEAR(b.energy_wh, 1.9f, 0.001f);
    CHECK_EQ(b.counted, BENCH_COUNTED_CHARGE | BENCH_COUNTED_ENERGY);

    /* Not while disarmed: the totals stand where the run left them. */
    b = measured(16.0f, 36.0f, sensed);
    b.charge_mah = 130.0f;
    bench_totals_count(&t, &b, 1.0f, false);
    CHECK_NEAR(t.mah, 120.0f, 0.001f);

    /* The part stopped answering: BENCH's fields empty, the bit gone, and
     * the count goes on from 120 mAh with nothing to add. */
    b = measured(0.0f, 0.0f, LINK_BN_SENSED);
    bench_totals_count(&t, &b, 1.0f, true);
    bench_totals_show(&t, &b);
    CHECK_NEAR(b.charge_mah, 120.0f, 0.001f);
    CHECK_EQ(b.counted, BENCH_COUNTED_CHARGE | BENCH_COUNTED_ENERGY);
}

/*
 * The coprocessor's edge into driving: the last run's totals leave the
 * BENCH page at once, so a poll before its next 50 Hz sample reads no
 * totals rather than the last run's marked as this one's.  The live
 * readings, the peaks and every other flag stay.
 */
TEST_CASE(a_run_start_takes_the_last_runs_totals_off_the_page)
{
    bench_state_t b = measured(24.0f, 40.0f,
                               LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK
                               | LINK_BN_SENSED | LINK_BN_TOTALS_OK);
    b.charge_mah  = 1840.0f;
    b.energy_wh   = 44.7f;
    b.voltage_min = 21.5f;
    b.current_max = 96.0f;
    uint16_t regs[LINK_BN_COUNT];
    bench_state_to_regs(&b, regs);
    uint16_t before[LINK_BN_COUNT];
    memcpy(before, regs, sizeof(before));

    bench_state_run_starts(&b, regs);
    CHECK_EQ(regs[LINK_BN_CHARGE_MAH], 0u);
    CHECK_EQ(regs[LINK_BN_ENERGY_DWH], 0u);
    CHECK_EQ(regs[LINK_BN_FLAGS] & LINK_BN_TOTALS_OK, 0u);
    CHECK_EQ(regs[LINK_BN_FLAGS],
             before[LINK_BN_FLAGS] & (uint16_t)~LINK_BN_TOTALS_OK);
    for (unsigned i = 0u; i < LINK_BN_COUNT; ++i) {
        if (i != LINK_BN_CHARGE_MAH && i != LINK_BN_ENERGY_DWH
            && i != LINK_BN_FLAGS) {
            CHECK_EQ(regs[i], before[i]);
        }
    }
    CHECK_EQ(b.charge_mah, 0.0f);
    CHECK_EQ(b.energy_wh, 0.0f);
    CHECK_EQ(b.flags & LINK_BN_TOTALS_OK, 0u);
    CHECK_NEAR(b.voltage, 24.0f, 0.001f);

    /* A panel reading the page now counts its own, from 0. */
    bench_state_t panel;
    memset(&panel, 0, sizeof(panel));
    bench_state_from_regs(&panel, regs, 0u, LINK_BN_COUNT);
    bench_totals_t t;
    bench_totals_reset(&t);
    t.run_s = BENCH_TOTALS_SETTLE_S;      /* past the panel's own guard */
    bench_totals_count(&t, &panel, 0.05f, true);
    CHECK(t.mah < 1.0f);

    bench_state_run_starts(NULL, regs);
    bench_state_run_starts(&b, NULL);
}

TEST_CASE(the_finer_totals_replace_benchs_where_they_agree)
{
    const uint16_t sensed = (uint16_t)(LINK_BN_SENSED | LINK_BN_TOTALS_OK);
    bench_state_t b = measured(0.0f, 0.0f, sensed);
    b.charge_mah = 123.0f;          /* BENCH's 1 mAh and 0.1 Wh steps */
    b.energy_wh  = 2.9f;
    bench_state_fine_totals(&b, 12274, 287u);
    CHECK_NEAR(b.charge_mah, 122.74f, 0.001f);
    CHECK_NEAR(b.energy_wh, 2.87f, 0.001f);

    /* A read from another moment -- before the arm, after a stall --
     * disagrees by more than a step and is not taken. */
    b.charge_mah = 123.0f;
    b.energy_wh  = 2.9f;
    bench_state_fine_totals(&b, 51200, 1904u);
    CHECK_EQ(b.charge_mah, 123.0f);
    CHECK_NEAR(b.energy_wh, 2.9f, 0.001f);

    /* A run that gave back more than it took: BENCH holds 0, SENSE the
     * signed total. */
    b.charge_mah = 0.0f;
    bench_state_fine_totals(&b, -350, 0u);
    CHECK_NEAR(b.charge_mah, -3.5f, 0.001f);

    /* Without TOTALS_OK nothing changes. */
    b = measured(0.0f, 0.0f, LINK_BN_SENSED);
    b.charge_mah = 7.0f;
    bench_state_fine_totals(&b, 700, 0u);
    CHECK_EQ(b.charge_mah, 7.0f);
    bench_state_fine_totals(NULL, 0, 0u);
}

TEST_CASE(the_escs_figures_come_from_whichever_page_carries_them)
{
    float v = 0.0f;
    float a = 0.0f;
    /* No INA228: BENCH's numbers are the ESC's. */
    bench_state_t b = measured(24.0f, 30.0f,
                               LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK);
    CHECK(bench_state_esc_voltage(&b, &v));
    CHECK(bench_state_esc_current(&b, &a));
    CHECK_NEAR(v, 24.0f, 0.001f);
    CHECK_NEAR(a, 30.0f, 0.001f);
    CHECK(!bench_state_ina_voltage(&b, &v));
    CHECK(!bench_state_ina_current(&b, &a));

    /* The INA228 as the source: BENCH's are its own, SENSE's the ESC's. */
    b.flags |= (uint16_t)LINK_BN_SENSED;
    bench_state_set_esc(&b, true, 24.4f, false, 99.0f, true);
    CHECK(b.clipped);
    CHECK(bench_state_esc_voltage(&b, &v));
    CHECK_NEAR(v, 24.4f, 0.001f);
    CHECK(!bench_state_esc_current(&b, &a));
    CHECK(bench_state_ina_voltage(&b, &v));
    CHECK(bench_state_ina_current(&b, &a));
    CHECK_NEAR(v, 24.0f, 0.001f);
    CHECK_NEAR(a, 30.0f, 0.001f);
    CHECK_EQ(b.esc_current, 0.0f);

    /* An INA228 field nothing answered for is not one. */
    b.flags = (uint16_t)LINK_BN_SENSED;
    CHECK(!bench_state_ina_voltage(&b, &v));
    CHECK(!bench_state_esc_voltage(NULL, &v));
    CHECK(!bench_state_esc_current(&b, NULL));
    bench_state_set_esc(NULL, false, 0.0f, false, 0.0f, false);
}

int main(void)
{
    RUN(every_field_survives_the_round_trip);
    RUN(temperature_survives_going_below_zero);
    RUN(an_out_of_range_value_clamps_rather_than_wraps);
    RUN(a_partial_page_leaves_the_rest_alone);
    RUN(resetting_peaks_does_not_invent_a_collapsed_pack);
    RUN(a_run_that_starts_before_the_first_voltage_still_finds_its_floor);
    RUN(peaks_ignore_the_fields_nothing_answered_for);
    RUN(the_simulator_flags_everything_it_produces);
    RUN(the_model_sags_under_load);
    RUN(current_grows_faster_than_throttle);
    RUN(rpm_lags_a_step_rather_than_following_it);
    RUN(the_simulator_accumulates_peaks_like_the_coprocessor_would);
    RUN(charge_and_energy_only_accumulate);
    RUN(charge_and_energy_count_what_was_measured_while_driving);
    RUN(nothing_is_counted_while_disarmed_or_unmeasured);
    RUN(one_count_runs_through_a_change_of_source);
    RUN(the_totals_outlast_the_run_and_reset_at_the_next);
    RUN(a_stalled_loop_counts_at_most_one_step);
    RUN(the_ina228_counts_while_its_totals_are_the_runs);
    RUN(a_run_start_takes_the_last_runs_totals_off_the_page);
    RUN(the_finer_totals_replace_benchs_where_they_agree);
    RUN(the_escs_figures_come_from_whichever_page_carries_them);
    return test_summary("bench");
}
