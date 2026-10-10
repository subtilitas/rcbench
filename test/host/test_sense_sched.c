/*
 * The sensor bus's sampling schedule (sense_sched.h), against the modelled
 * INA228 and INA3221 of fake_ina.h and a clock the suite moves 1 ms a
 * tick.
 *
 * Under test: the read rates, counted at the parts -- CH1 1000 Hz, the
 * INA228's current and voltage 500 Hz each, the rest 50 Hz or 25 Hz, CH2
 * and CH3 at 1000 Hz while the pair runs, and every tick of the rotation
 * inside 1 ms of bus time; CH1's ring of four windows after 1, 4, 5 and 6
 * of them, across window 65535, with the numbers a late tick skips, and
 * its clipped samples counted at the end of the range; a part that reset
 * itself found by the read-back of its set-up within one rotation,
 * emptied, counted once, set up again after SENSE_RETRY_MS and across the
 * millisecond count's wrap, and one corrupted read-back no reset; the
 * 50 ms windows, their numbers and
 * their figures, a clipped sample kept out of them, across the 32-bit
 * millisecond wrap, and a sample read after a probe that crosses a window
 * boundary kept in the window after it; the run's peaks, power
 * only from the voltage read just before, and the INA228's totals since
 * the arm, ended by one failed read; a part missing, lost and back; a
 * stuck bus recovered; the move capture on CH1 against the servo model,
 * arrived, settled, late, unseen, clipped and lost, its filter seeded with
 * the samples taken while armed, and across the 0.1 ms
 * count's wrap; the level before the command taken from CH1's 50 ms
 * before the edge, and a capture with none lost; its states the link's.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "as5600.h"
#include "fake_ina.h"
#include "link_pages.h"
#include "sense_sched.h"
#include "servo_sim.h"

#define I228_ADDR  0x45u        /* the MATEK I2C-INA-BM               */
#define I228_UOHM  200u
#define I228_MA    204800u
#define I3221_ADDR 0x40u        /* the DAOKAI module, R100 shunts      */
#define I3221_UOHM 100000u

static fake_bus_t    fb;
static fake_part_t  *i228;
static fake_part_t  *i3221;
static sense_sched_t s;
static uint64_t      g_us;
static unsigned      g_recoveries;

static uint64_t now_us(void *ctx)
{
    (void)ctx;
    return g_us;
}

static void recover(void *ctx)
{
    (void)ctx;
    ++g_recoveries;
}

/* Bus time on the suite's clock: each transaction takes g_xfer_us, and
 * one to the INA228 g_xfer_228_us more, 0 for none; from g_switch_us on,
 * CH1 draws g_switch_a and the INA228 g_switch_228_a. */
static uint64_t g_xfer_us;
static uint64_t g_xfer_228_us;
static uint64_t g_switch_us;
static double   g_switch_a;
static double   g_switch_228_a;

static void spend(uint8_t addr)
{
    g_us += g_xfer_us + ((addr == I228_ADDR) ? g_xfer_228_us : 0u);
    if (g_switch_us != 0u && g_us >= g_switch_us) {
        i3221->amps[0] = g_switch_a;
        i228->amps[0]  = g_switch_228_a;
    }
}

static sense_err_t timed_read(void *ctx, uint8_t addr, uint8_t reg,
                              uint8_t *buf, size_t n)
{
    spend(addr);
    return fake_read(ctx, addr, reg, buf, n);
}

static sense_err_t timed_write(void *ctx, uint8_t addr, uint8_t reg,
                               const uint8_t *buf, size_t n)
{
    spend(addr);
    return fake_write(ctx, addr, reg, buf, n);
}

/* Both parts on the bus, as @p channels says for the INA3221; the clock
 * at 10 s. */
static void rig(uint8_t channels, bool with_recover)
{
    sense_bus_t scratch;
    fake_bus_init(&fb, &scratch);
    i228  = fake_add(&fb, FAKE_INA228, I228_ADDR, 200e-6);
    i3221 = fake_add(&fb, FAKE_INA3221, I3221_ADDR, 0.1);
    i228->volts[0] = 16.8;
    i3221->volts[0] = 6.0;
    i3221->volts[1] = 5.9;
    i3221->volts[2] = 5.8;
    const sense_sched_io_t io = { { timed_read, timed_write, &fb }, now_us,
                                  with_recover ? recover : NULL, NULL };
    const sense_sched_cfg_t cfg = {
        .ina228_en = true, .ina228_addr = I228_ADDR,
        .ina228_shunt_uohm = I228_UOHM, .ina228_max_ma = I228_MA,
        .ina3221_en = true, .ina3221_addr = I3221_ADDR,
        .ina3221_shunt_uohm = I3221_UOHM, .ina3221_channels = channels,
    };
    g_us = 10000000u;
    g_recoveries = 0u;
    g_xfer_us = 0u;
    g_xfer_228_us = 0u;
    g_switch_us = 0u;
    sense_sched_init(&s, &io, &cfg);
}

/* One tick, and the next 1 ms after this one began, however long its
 * transactions took. */
static void tick(void)
{
    const uint64_t at = g_us;
    sense_sched_tick(&s);
    g_us = at + 1000u;
}

static void ticks(unsigned n)
{
    for (unsigned k = 0; k < n; ++k) {
        tick();
    }
}

static void zero_counts(void)
{
    memset(i228->reads, 0, sizeof(i228->reads));
    memset(i3221->reads, 0, sizeof(i3221->reads));
}

/* ---------------------------------------------------------------- rates */

TEST_CASE(each_register_is_read_at_its_rate)
{
    rig(1u, true);
    tick();                     /* the probes */
    CHECK_EQ(ina228_state(&s.i228), SENSE_PART_ONLINE);
    CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_ONLINE);
    zero_counts();
    ticks(1000u);
    CHECK_EQ(i3221->reads[INA3221_SHUNT1], 1000u);
    CHECK_EQ(i3221->reads[INA3221_BUS1], 50u);
    CHECK_EQ(i3221->reads[INA3221_SHUNT1 + 2], 0u);   /* CH2: not enabled */
    CHECK_EQ(i3221->reads[INA3221_BUS1 + 4], 0u);     /* CH3 */
    /* Mask/Enable shares its slot with Configuration, DIETEMP its slot
     * with ADC_CONFIG: 25 Hz each. */
    CHECK_EQ(i3221->reads[INA3221_MASK_ENABLE], 25u);
    CHECK_EQ(i3221->reads[INA3221_CONFIG], 25u);
    CHECK_EQ(i228->reads[INA228_CURRENT], 500u);
    CHECK_EQ(i228->reads[INA228_VBUS], 500u);
    CHECK_EQ(i228->reads[INA228_DIETEMP], 25u);
    CHECK_EQ(i228->reads[INA228_ADC_CONFIG], 25u);
    CHECK_EQ(i228->reads[INA228_DIAG_ALRT], 50u);
    CHECK_EQ(i228->reads[INA228_ENERGY], 50u);
    CHECK_EQ(i228->reads[INA228_CHARGE], 50u);
    CHECK_EQ(fb.bad_width, 0u);
    CHECK(s.have_temp && s.have_diag && s.have_flags);
    CHECK_EQ(s.temp_mdegc, 0);
    CHECK_EQ(s.diag, 0x0001u);           /* MEMSTAT: the trim checksum good */

    /* All three channels: CH2 and CH3 at 50 Hz, and at 1000 Hz while the
     * pair runs, when the rotation leaves their slots out. */
    rig(7u, true);
    tick();
    zero_counts();
    ticks(1000u);
    for (unsigned ch = 0; ch < 3u; ++ch) {
        CHECK_EQ(i3221->reads[INA3221_SHUNT1 + 2u * ch], ch == 0u ? 1000u : 50u);
        CHECK_EQ(i3221->reads[INA3221_BUS1 + 2u * ch], 50u);
    }
    sense_sched_fast_pair(&s, true);
    zero_counts();
    ticks(1000u);
    for (unsigned ch = 0; ch < 3u; ++ch) {
        CHECK_EQ(i3221->reads[INA3221_SHUNT1 + 2u * ch], 1000u);
    }
    CHECK_EQ(i228->reads[INA228_CURRENT], 500u);
    sense_sched_fast_pair(&s, false);
    zero_counts();
    ticks(1000u);
    CHECK_EQ(i3221->reads[INA3221_SHUNT1 + 2u], 50u);
}

/* -------------------------------------------------------------- windows */

/* 50 ms windows numbered from the first tick: CH1's 50 samples, the
 * INA228's 25 currents and 25 voltages, 2 or 3 bus voltages a channel. */
TEST_CASE(a_window_holds_fifty_milliseconds)
{
    rig(7u, true);
    sense_window_t w;
    CHECK(!sense_sched_window(&s, SENSE_SRC_CH1, &w));
    i3221->amps[0] = 0.5;
    i3221->amps[1] = 0.2;
    i228->amps[0]  = 10.0;
    ticks(50u);
    CHECK(!sense_sched_window(&s, SENSE_SRC_CH1, &w));
    /* The next window, a ramp; its first tick closes window 0. */
    for (unsigned k = 0; k < 50u; ++k) {
        i3221->amps[0] = 0.1 + 0.01 * (double)k;
        tick();
    }
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK_EQ(w.number, 0u);
    CHECK_EQ(w.n_i, 50u);
    CHECK_EQ(w.i_mean_ua, 500000);
    CHECK_EQ(w.i_min_ua, 500000);
    CHECK_EQ(w.i_max_ua, 500000);
    CHECK_EQ(w.n_v, 3u);
    CHECK_EQ(w.v_mean_uv, 6000000);
    CHECK_EQ(w.v_min_uv, 6000000);
    CHECK(!w.clip_hi && !w.clip_lo);
    CHECK(sense_sched_window(&s, SENSE_SRC_CH2, &w));
    CHECK_EQ(w.n_i, 3u);
    CHECK_EQ(w.i_mean_ua, 200000);
    CHECK_EQ(w.v_min_uv, 5904000);       /* 5.9 V in 8 mV steps */
    CHECK(sense_sched_window(&s, SENSE_SRC_INA228, &w));
    CHECK_EQ(w.n_i, 25u);
    CHECK_EQ(w.n_v, 25u);
    CHECK_NEAR((double)w.i_mean_ua, 10.0e6, 400.0);
    CHECK_NEAR((double)w.v_mean_uv, 16.8e6, 200.0);
    CHECK(!sense_sched_window(&s, SENSE_SRC_COUNT, &w));

    /* The ramp's window: mean, lowest and highest. */
    tick();
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK_EQ(w.number, 1u);
    CHECK_EQ(w.n_i, 50u);
    CHECK_EQ(w.i_min_ua, 100000);
    CHECK_EQ(w.i_max_ua, 590000);
    CHECK_EQ(w.i_mean_ua, 345000);
    CHECK_EQ(w.n_v, 2u);

    /* A tick 120 ms late, at 221 ms: window 2 closes, window 3 is
     * missed, and the next to close is window 4. */
    g_us += 120000u;
    tick();
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK_EQ(w.number, 2u);
    ticks(50u);
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK_EQ(w.number, 4u);
}

/* A reading at the end of its range carries no value: the window keeps
 * the rest and says which end was reached. */
TEST_CASE(a_clipped_reading_is_kept_out_of_the_window)
{
    rig(7u, true);
    i3221->amps[0] = 2.0;       /* past the R100's 1.638 A */
    i3221->amps[1] = -2.0;
    i228->amps[0]  = 300.0;     /* past the MATEK's 204.8 A */
    ticks(50u);
    /* The next window half clipped; its first tick closes window 0. */
    for (unsigned k = 0; k < 50u; ++k) {
        i3221->amps[0] = (k < 25u) ? 2.0 : 1.0;
        tick();
    }
    sense_window_t w;
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK(w.clip_hi);
    CHECK(!w.clip_lo);
    CHECK_EQ(w.n_i, 0u);
    CHECK_EQ(w.i_mean_ua, 0);
    CHECK(sense_sched_window(&s, SENSE_SRC_CH2, &w));
    CHECK(w.clip_lo);
    CHECK(sense_sched_window(&s, SENSE_SRC_INA228, &w));
    CHECK(w.clip_hi);
    CHECK_EQ(w.n_i, 0u);
    CHECK_EQ(w.n_v, 25u);

    /* Half the window clipped: the other half is its figures. */
    tick();
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK(w.clip_hi);
    CHECK_EQ(w.n_i, 25u);
    CHECK_EQ(w.i_mean_ua, 1000000);
}

/* ------------------------------------------------------------- the run */

/* From the arm: the INA228's lowest voltage, highest current and highest
 * power, which a new arm starts again. */
TEST_CASE(the_run_keeps_its_peaks_from_the_arm)
{
    rig(1u, true);
    i228->amps[0] = 50.0;
    i228->volts[0] = 20.0;
    ticks(10u);
    sense_sched_arm(&s);
    CHECK(!s.run.have_i && !s.run.have_v && !s.run.have_p);
    i228->amps[0] = 10.0;
    i228->volts[0] = 16.0;
    ticks(10u);
    /* A peak: the pack sags to 15 V, then 30 A for 4 ms, 450 W.  A
     * current is multiplied by the voltage read 1 ms before it, so a step
     * in both at once would pair 30 A with 16 V. */
    i228->volts[0] = 15.0;
    ticks(2u);
    i228->amps[0] = 30.0;
    ticks(4u);
    i228->amps[0] = 5.0;
    ticks(2u);
    i228->volts[0] = 16.5;
    ticks(10u);
    const sense_run_t *r = &s.run;
    CHECK(r->have_i && r->have_v && r->have_p);
    CHECK_NEAR((double)r->i_max_ua, 30.0e6, 400.0);
    CHECK_NEAR((double)r->v_min_uv, 15.0e6, 200.0);
    CHECK_NEAR((double)r->p_max_uw, 450.0e6, 10000.0);
    CHECK(!r->i_clipped);

    i228->amps[0] = 300.0;
    ticks(2u);
    CHECK(s.run.i_clipped);
    CHECK_NEAR((double)r->i_max_ua, 30.0e6, 400.0);

    sense_sched_arm(&s);
    i228->amps[0] = 1.0;
    ticks(4u);
    CHECK_NEAR((double)s.run.i_max_ua, 1.0e6, 400.0);
    CHECK(!s.run.i_clipped);
}

/* ENERGY and CHARGE cleared on the tick after the arm and this run's
 * while the INA228 answers; a part lost, or not there at the arm, leaves
 * the run without them. */
TEST_CASE(the_totals_are_the_runs_while_the_part_answers)
{
    rig(1u, true);
    tick();
    i228->energy = 12345u;
    i228->charge = 999u;
    sense_sched_arm(&s);
    CHECK(s.run.clear_owed);
    CHECK(!s.run.totals_ok);
    tick();
    CHECK(!s.run.clear_owed);
    CHECK(s.run.totals_ok);
    CHECK_EQ(i228->energy, 0u);       /* RSTACC reached the part */
    CHECK_EQ(i228->charge, 0u);
    i228->energy = 1000u;                       /* 20 mJ a step */
    i228->charge = 0xFFFFFFFC18u;               /* -1000 steps */
    ticks(20u);
    CHECK_EQ(s.run.energy_mj, ina228_energy_mj(&s.i228.cal, 1000u));
    CHECK_EQ(s.run.energy_mj, 20000u);
    CHECK_EQ(s.run.charge_uc, -390625);         /* 390.625 µC a step */
    CHECK(s.run.totals_ok);

    /* The part stops answering: offline after 3 failures, the totals are
     * no longer the run's, and stay so when it is back. */
    i228->present = false;
    ticks(10u);
    CHECK_EQ(ina228_state(&s.i228), SENSE_PART_OFFLINE);
    CHECK(!s.run.totals_ok);
    i228->present = true;
    ticks(1100u);
    CHECK_EQ(ina228_state(&s.i228), SENSE_PART_ONLINE);
    CHECK(!s.run.totals_ok);

    /* The clear's write fails once: repeated on the next tick. */
    sense_sched_arm(&s);
    fb.fail_with = SENSE_NACK;
    fb.fail_count = 1u;
    tick();
    CHECK(s.run.clear_owed);
    CHECK(!s.run.totals_ok);
    tick();
    CHECK(s.run.totals_ok);

    /* Not there at the arm: no totals for the run. */
    i228->present = false;
    ticks(10u);
    sense_sched_arm(&s);
    tick();
    CHECK(!s.run.clear_owed);
    CHECK(!s.run.totals_ok);
    i228->present = true;
    ticks(1100u);
    CHECK(!s.run.totals_ok);
}

/* ----------------------------------------------------- missing and stuck */

/* A part that does not answer is probed every second and never read; one
 * lost mid-run goes offline after three failed reads, its windows empty,
 * and comes back at the next probe. */
TEST_CASE(a_missing_part_is_not_read_and_a_lost_one_comes_back)
{
    rig(1u, true);
    i3221->present = false;
    ticks(51u);
    CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_ABSENT);
    CHECK_EQ(i3221->reads[INA3221_SHUNT1], 0u);
    sense_window_t w;
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK_EQ(w.n_i, 0u);
    CHECK_EQ(w.n_v, 0u);
    CHECK(sense_sched_window(&s, SENSE_SRC_INA228, &w));
    CHECK_EQ(w.n_i, 25u);

    i3221->present = true;
    ticks(1000u);
    CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_ONLINE);
    i3221->present = false;
    ticks(3u);
    CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_OFFLINE);
    i3221->present = true;
    ticks(997u);
    CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_OFFLINE);
    ticks(3u);
    CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_ONLINE);
}

/* SDA held low: nothing more is sent, a recovery runs at once and every
 * 100 ms, and the first transaction after one that goes through frees
 * the bus.  The parts' failed reads take them offline meanwhile, so that
 * transaction is the next probe, up to a second on; it brings them back.
 * Without a recover callback the recovery is still counted. */
TEST_CASE(a_stuck_bus_is_recovered)
{
    rig(1u, true);
    ticks(5u);
    fb.low = true;
    tick();
    CHECK(s.bus.stuck);
    const unsigned sent = fb.transactions;
    ticks(250u);
    CHECK(fb.transactions - sent <= 3u);         /* one test a recovery */
    CHECK_EQ(g_recoveries, 3u);                  /* at 0, 100 and 200 ms */
    CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_OFFLINE);
    fb.low = false;
    ticks(100u);
    CHECK(s.bus.stuck);                          /* nothing has tested it */
    ticks(1000u);
    CHECK(!s.bus.stuck);
    CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_ONLINE);
    CHECK_EQ(ina228_state(&s.i228), SENSE_PART_ONLINE);

    rig(1u, false);
    fb.low = true;
    ticks(2u);
    CHECK(s.bus.stuck);
    CHECK_EQ(s.bus.recoveries, 1u);
    CHECK_EQ(g_recoveries, 0u);
}

/* A part not enabled, or set up wrong, is never addressed. */
TEST_CASE(a_part_not_enabled_is_never_addressed)
{
    rig(1u, true);
    const sense_sched_io_t io = s.io;
    sense_sched_cfg_t cfg = s.cfg;
    cfg.ina228_en = false;
    cfg.ina3221_addr = 0x44u;               /* outside 0x40 to 0x43 */
    sense_sched_init(&s, &io, &cfg);
    CHECK_EQ(s.i3221_setup, INA3221_SETUP_BAD_ADDR);
    CHECK_EQ(s.i228_setup, INA228_SETUP_OK);
    ticks(1000u);
    CHECK_EQ(fb.transactions, 0u);
    sense_window_t w;
    CHECK(sense_sched_window(&s, SENSE_SRC_INA228, &w));
    CHECK_EQ(w.n_i, 0u);
    sense_sched_arm(&s);
    tick();
    CHECK(!s.run.totals_ok);
    const sense_cap_arm_t lv = { 120000, 120000, 30000, 50000 };
    CHECK(!sense_sched_cap_arm(&s, &lv));
}

/* ---------------------------------------------------------- the capture */

static servo_sim_t g_servo;
static uint16_t    g_cmd;

/* The modelled servo on CH1: 0.12 A holding, 0.95 A moving, 1.2 us/ms. */
static void servo_rig(float travel_a)
{
    rig(1u, true);
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    sc.stop_lo_us = 1000u;
    sc.stop_hi_us = 2000u;
    sc.travel_a = travel_a;
    servo_sim_init(&g_servo, &sc);
    g_servo.position_us = 1100.0f;
    g_cmd = 1100u;
}

/* One tick with the servo's current on CH1. */
static void servo_tick(void)
{
    i3221->amps[0] = servo_sim_step(&g_servo, g_cmd, (uint32_t)(g_us / 1000u));
    tick();
}

static void servo_ticks(unsigned n)
{
    for (unsigned k = 0; k < n; ++k) {
        servo_tick();
    }
}

static const sense_cap_arm_t k_levels = { 120000, 120000, 30000, 50000 };

/* Armed, then the edge stamped 0.5 ms ahead, then the command reaches the
 * servo at that edge: the move is timed from the edge to within the
 * filter's lag of the model's 667 ms. */
TEST_CASE(a_capture_times_a_move_from_the_edge)
{
    servo_rig(0.95f);
    servo_ticks(100u);
    CHECK_EQ(s.cap.state, SENSE_CAP_IDLE);
    sense_sched_cap_edge(&s, g_us);          /* not armed: nothing */
    CHECK_EQ(s.cap.state, SENSE_CAP_IDLE);
    CHECK(sense_sched_cap_arm(&s, &k_levels));
    CHECK_EQ(s.cap.state, SENSE_CAP_ARMED);
    servo_ticks(5u);
    CHECK_EQ(s.cap.state, SENSE_CAP_ARMED);
    const uint64_t edge = g_us + 500u;
    sense_sched_cap_edge(&s, edge);
    CHECK_EQ(s.cap.state, SENSE_CAP_WAITING);
    CHECK_EQ(s.cap.edge_t, (uint32_t)(edge / 100u));
    servo_tick();                            /* before the edge */
    g_cmd = 1900u;
    servo_ticks(5u);
    CHECK_EQ(s.cap.state, SENSE_CAP_MOVING);
    for (unsigned k = 0; k < 1000u && s.cap.state == SENSE_CAP_MOVING; ++k) {
        servo_tick();
    }
    CHECK_EQ(s.cap.state, SENSE_CAP_ARRIVED);
    CHECK_EQ(s.cap.seq, 1u);
    /* Movement within the filter of the edge; arrival 667 ms and the
     * filter's lag after it. */
    CHECK(s.cap.move_t <= 30u);
    CHECK(s.cap.arrive_t >= 6670u);
    CHECK(s.cap.arrive_t <= 6670u + 40u);
    CHECK_NEAR((double)s.cap.peak_ua, 950000.0, 12000.0);
    CHECK_NEAR((double)s.cap.mean_ua, 950000.0, 30000.0);
    CHECK(s.cap.samples >= 660u);
    CHECK(!s.cap.clipped);

    /* An edge after the capture is over changes nothing; a disarm keeps
     * the count. */
    sense_sched_cap_edge(&s, g_us);
    CHECK_EQ(s.cap.state, SENSE_CAP_ARRIVED);
    sense_sched_cap_disarm(&s);
    CHECK_EQ(s.cap.state, SENSE_CAP_IDLE);
    CHECK_EQ(s.cap.seq, 1u);
}

/* A servo moving at 2 A: past the R100's 1.638 A, every moving sample is
 * clipped.  The clip is movement and is not yet there; the arrival is
 * timed, and peak and mean say they lack the clipped samples. */
TEST_CASE(a_capture_through_clipped_samples_is_timed)
{
    servo_rig(2.0f);
    servo_ticks(10u);
    CHECK(sense_sched_cap_arm(&s, &k_levels));
    sense_sched_cap_edge(&s, g_us);
    g_cmd = 1900u;
    for (unsigned k = 0; k < 1000u && s.cap.state <= SENSE_CAP_MOVING; ++k) {
        servo_tick();
    }
    CHECK_EQ(s.cap.state, SENSE_CAP_ARRIVED);
    CHECK(s.cap.clipped);
    CHECK(s.cap.arrive_t >= 6670u);
    CHECK(s.cap.arrive_t <= 6670u + 40u);
    CHECK(s.cap.samples < 10u);

    /* At the bottom of the range: movement down from 0.50 A. */
    rig(1u, true);
    tick();
    const sense_cap_arm_t down = { 500000, 100000, 30000, 50000 };
    CHECK(sense_sched_cap_arm(&s, &down));
    sense_sched_cap_edge(&s, g_us);
    i3221->amps[0] = -2.0;
    ticks(2u);
    CHECK_EQ(s.cap.state, SENSE_CAP_MOVING);
}

/* An end pushed on at more than the servo moves: settled. */
TEST_CASE(a_capture_at_a_stop_settles)
{
    servo_rig(0.30f);
    g_servo.cfg.stop_hi_us = 1880u;
    g_servo.cfg.stall_a = 1.20f;
    g_servo.cfg.bind_us = 40u;
    servo_ticks(10u);
    const sense_cap_arm_t at_stop = { 120000, 660000, 30000, 50000 };
    CHECK(sense_sched_cap_arm(&s, &at_stop));
    sense_sched_cap_edge(&s, g_us);
    g_cmd = 1900u;
    for (unsigned k = 0; k < 1000u && s.cap.state <= SENSE_CAP_MOVING; ++k) {
        servo_tick();
    }
    CHECK_EQ(s.cap.state, SENSE_CAP_SETTLED);
    CHECK(s.cap.arrive_t >= 6500u);
    CHECK(s.cap.arrive_t <= 6500u + 40u);
}

/* No arrival in 3005 ms, 3000 ms and the 5 ms lag: late with movement,
 * unseen without. */
TEST_CASE(a_capture_ends_late_or_unseen)
{
    rig(1u, true);
    i3221->amps[0] = 0.12;
    tick();
    CHECK(sense_sched_cap_arm(&s, &k_levels));
    sense_sched_cap_edge(&s, g_us);
    ticks(3004u);
    CHECK_EQ(s.cap.state, SENSE_CAP_WAITING);
    ticks(2u);
    CHECK_EQ(s.cap.state, SENSE_CAP_UNSEEN);
    CHECK_EQ(s.cap.seq, 1u);
    CHECK_EQ(s.cap.move_t, 0u);
    CHECK_EQ(s.cap.arrive_t, 0u);

    CHECK(sense_sched_cap_arm(&s, &k_levels));
    CHECK_EQ(s.cap.seq, 1u);
    sense_sched_cap_edge(&s, g_us);
    ticks(10u);
    i3221->amps[0] = 0.9;
    ticks(2996u);
    CHECK_EQ(s.cap.state, SENSE_CAP_LATE);
    CHECK_EQ(s.cap.seq, 2u);
    CHECK(s.cap.move_t > 0u);
    CHECK_EQ(s.cap.arrive_t, 0u);
}

/* The INA3221 stops answering: the capture ends lost, armed or under way.
 * A capture is refused with the part offline or CH1 not enabled. */
TEST_CASE(a_capture_is_lost_with_its_part)
{
    rig(1u, true);
    tick();
    CHECK(sense_sched_cap_arm(&s, &k_levels));
    sense_sched_cap_edge(&s, g_us);
    tick();
    i3221->present = false;
    ticks(3u);
    CHECK_EQ(s.cap.state, SENSE_CAP_LOST);
    CHECK_EQ(s.cap.seq, 1u);
    CHECK(!sense_sched_cap_arm(&s, &k_levels));

    rig(1u, true);
    tick();
    CHECK(sense_sched_cap_arm(&s, &k_levels));
    i3221->present = false;
    ticks(3u);
    CHECK_EQ(s.cap.state, SENSE_CAP_LOST);

    rig(6u, true);                           /* CH2 and CH3 only */
    tick();
    CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_ONLINE);
    CHECK(!sense_sched_cap_arm(&s, &k_levels));
    CHECK_EQ(s.cap.state, SENSE_CAP_IDLE);
}

/* --------------------------------------------- totals and power, strictly */

/* One failed INA228 read after the clear, the part still online: the
 * totals are no longer the run's, and stay so until the next arm. */
TEST_CASE(one_failed_ina228_read_ends_the_runs_totals)
{
    rig(1u, true);
    ticks(3u);
    sense_sched_arm(&s);
    tick();
    CHECK(s.run.totals_ok);
    if ((s.ticks & 1u) != 0u) {
        tick();                         /* to a tick that reads CURRENT */
    }
    fb.fail_with = SENSE_NACK;
    fb.fail_at = fb.transactions + 2u;  /* CH1, then the INA228 */
    tick();
    CHECK_EQ(ina228_state(&s.i228), SENSE_PART_ONLINE);
    CHECK(!s.run.totals_ok);
    ticks(100u);
    CHECK(!s.run.totals_ok);
    sense_sched_arm(&s);
    tick();
    CHECK(s.run.totals_ok);
}

/* A failed VBUS read leaves the current after it without a power: a
 * voltage from before the failure is not paired with it. */
TEST_CASE(a_failed_voltage_read_leaves_the_next_current_without_power)
{
    rig(1u, true);
    i228->amps[0] = 10.0;
    i228->volts[0] = 16.0;
    ticks(3u);
    if ((s.ticks & 1u) == 0u) {
        tick();                         /* the next tick reads VBUS */
    }
    sense_sched_arm(&s);
    fb.fail_with = SENSE_NACK;
    fb.fail_at = fb.transactions + 4u;  /* the clear's 2 writes, CH1, VBUS */
    tick();
    CHECK(!s.have_vbus);
    tick();                             /* CURRENT: no voltage to pair */
    CHECK(s.run.have_i);
    CHECK(!s.run.have_p);
    ticks(2u);                          /* VBUS, then CURRENT */
    CHECK(s.run.have_p);
    CHECK_NEAR((double)s.run.p_max_uw, 160.0e6, 10000.0);
}

/* --------------------------------------------------------- the wrap */

/* The 32-bit millisecond count wraps after 49.7 days: the windows run on,
 * numbered one after another, and the parts stay online.  A capture across
 * the 0.1 ms count's wrap, after 4.97 days, is timed as any other. */
TEST_CASE(windows_and_captures_run_on_across_the_wraps)
{
    rig(1u, true);
    g_us = ((uint64_t)1u << 32) * 1000u - 120000u;   /* 120 ms before */
    i3221->amps[0] = 0.5;
    ticks(51u);
    sense_window_t w;
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK_EQ(w.number, 0u);
    for (unsigned n = 1u; n <= 5u; ++n) {
        ticks(50u);
        CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
        CHECK_EQ(w.number, n);
        CHECK_EQ(w.n_i, 50u);
    }
    CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_ONLINE);
    CHECK_EQ(ina228_state(&s.i228), SENSE_PART_ONLINE);

    rig(1u, true);
    g_us = ((uint64_t)1u << 32) * 100u - 200000u;    /* 200 ms before */
    i3221->amps[0] = 0.12;
    ticks(10u);
    const sense_cap_arm_t lv = { 120000, 120000, 30000, 50000 };
    CHECK(sense_sched_cap_arm(&s, &lv));
    ticks(5u);
    sense_sched_cap_edge(&s, g_us);
    i3221->amps[0] = 0.9;
    ticks(500u);
    i3221->amps[0] = 0.12;
    ticks(20u);
    CHECK_EQ(s.cap.state, SENSE_CAP_ARRIVED);
    CHECK(s.cap.arrive_t >= 5000u);
    CHECK(s.cap.arrive_t <= 5040u);
}

/* A probe of a part back from offline can cross a window boundary: the
 * INA228's 10 transactions take about 1.1 ms.  The window is rolled again
 * after it, so the CH1 sample read past the boundary is the next
 * window's. */
TEST_CASE(a_sample_after_a_probe_across_a_boundary_is_the_next_windows)
{
    rig(1u, true);
    i3221->amps[0] = 0.5;
    ticks(5u);
    i228->present = false;
    ticks(5u);
    CHECK_EQ(ina228_state(&s.i228), SENSE_PART_OFFLINE);
    i228->present = true;
    /* The next boundary, and the probe due 1 ms before it. */
    const uint64_t b_ms = s.t0_ms + SENSE_WINDOW_MS * (s.win + 1u);
    s.i228.part.probe_at = (uint32_t)(b_ms - 1u);
    while (g_us / 1000u < b_ms - 1u) {
        tick();
    }
    const uint16_t n = (uint16_t)s.win;
    g_xfer_us   = 110u;                 /* a 16-bit read at 400 kHz */
    g_switch_us = b_ms * 1000u;
    g_switch_a  = 1.0;
    tick();
    g_xfer_us = 0u;
    CHECK_EQ(ina228_state(&s.i228), SENSE_PART_ONLINE);
    sense_window_t w;
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK_EQ(w.number, n);
    CHECK_EQ(w.i_max_ua, 500000);
    ticks(51u);                         /* past the next window's end */
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK_EQ(w.number, (uint16_t)(n + 1u));
    CHECK_EQ(w.i_max_ua, 1000000);
}

/* A tick's reads take up to 690 µs and can straddle a window's end: CH1,
 * read before it, counts in the window that ends; the INA228, read after
 * it, in the next. */
TEST_CASE(a_read_batch_across_a_boundary_splits_between_the_windows)
{
    rig(1u, true);
    i3221->amps[0] = 0.5;
    i228->amps[0]  = 10.0;
    ticks(10u);
    const uint64_t b_ms = s.t0_ms + SENSE_WINDOW_MS * (s.win + 1u);
    while (g_us / 1000u < b_ms - 3u) {
        tick();
    }
    if ((s.ticks & 1u) != 0u) {
        tick();                         /* to a tick that reads CURRENT */
    }
    const uint16_t n = (uint16_t)s.win;
    g_us = b_ms * 1000u - 500u;         /* CH1 done at -200 µs, the INA228
                                           at +100 µs */
    g_xfer_us = 300u;
    i3221->amps[0] = 0.7;
    i228->amps[0]  = 20.0;
    tick();
    g_xfer_us = 0u;
    i3221->amps[0] = 0.5;
    i228->amps[0]  = 10.0;
    sense_window_t w;
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK_EQ(w.number, n);
    CHECK_EQ(w.i_max_ua, 700000);
    CHECK(sense_sched_window(&s, SENSE_SRC_INA228, &w));
    CHECK_NEAR((double)w.i_max_ua, 10.0e6, 400.0);
    ticks(51u);                         /* past the next window's end */
    CHECK(sense_sched_window(&s, SENSE_SRC_INA228, &w));
    CHECK_EQ(w.number, (uint16_t)(n + 1u));
    CHECK_NEAR((double)w.i_max_ua, 20.0e6, 400.0);
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK_EQ(w.i_max_ua, 500000);
}

/* A move from 0.12 A at 0.9 A that drops back @p drop_ms after the edge:
 * the filter shows the drop whole 3 samples later.  The capture's state,
 * and its arrival in 0.1 ms. */
static sense_cap_state_t arrival_after(unsigned drop_ms, uint32_t *arrive_t)
{
    rig(1u, true);
    i3221->amps[0] = 0.12;
    ticks(5u);
    CHECK(sense_sched_cap_arm(&s, &k_levels));
    ticks(5u);
    sense_sched_cap_edge(&s, g_us);
    i3221->amps[0] = 0.9;
    ticks(drop_ms);
    i3221->amps[0] = 0.12;
    ticks(10u);
    *arrive_t = s.cap.arrive_t;
    return s.cap.state;
}

/* The window is 3005 ms: an arrival 1 ms before it is timed, one exactly
 * at it or 1 ms past it is late -- the deadline is checked before the
 * sample is judged. */
TEST_CASE(an_arrival_at_the_deadline_is_late)
{
    uint32_t t = 0u;
    CHECK_EQ(arrival_after(3001u, &t), SENSE_CAP_ARRIVED);
    CHECK_EQ(t, 30040u);
    CHECK_EQ(arrival_after(3002u, &t), SENSE_CAP_LATE);
    CHECK_EQ(t, 0u);
    CHECK_EQ(arrival_after(3003u, &t), SENSE_CAP_LATE);
}

/* The modelled move with the INA228's reads taking @p xfer_us each after
 * CH1's: its times. */
static void timed_move(uint64_t xfer_us, uint32_t *move_t, uint32_t *arrive_t)
{
    servo_rig(0.95f);
    g_xfer_228_us = xfer_us;
    servo_ticks(10u);
    CHECK(sense_sched_cap_arm(&s, &k_levels));
    servo_ticks(5u);
    sense_sched_cap_edge(&s, g_us + 500u);
    g_cmd = 1900u;
    for (unsigned k = 0; k < 1000u && s.cap.state <= SENSE_CAP_MOVING; ++k) {
        servo_tick();
    }
    CHECK_EQ(s.cap.state, SENSE_CAP_ARRIVED);
    *move_t   = s.cap.move_t;
    *arrive_t = s.cap.arrive_t;
}

/* CH1 is stamped when its own read is done: INA228 reads after it that
 * take 400 µs each move no capture time. */
TEST_CASE(slow_reads_after_ch1_do_not_move_the_capture)
{
    uint32_t m0 = 0u;
    uint32_t a0 = 0u;
    uint32_t m1 = 0u;
    uint32_t a1 = 0u;
    timed_move(0u, &m0, &a0);
    timed_move(400u, &m1, &a1);
    CHECK_EQ(m1, m0);
    CHECK_EQ(a1, a0);
    CHECK(a0 >= 6670u);
}

/* ---------------------------------------------------- the seeded filter */

/* The CH1 samples taken while armed fill the filter at the edge: a single
 * sample 0.10 A above the level is a quarter of that, under the 0.030 A
 * threshold, and starts no move.  Without them it would be the whole
 * 0.10 A. */
TEST_CASE(a_one_sample_transient_at_the_edge_starts_no_move)
{
    rig(1u, true);
    i3221->amps[0] = 0.12;
    ticks(5u);
    const sense_cap_arm_t lv = { 120000, 120000, 30000, 50000 };
    CHECK(sense_sched_cap_arm(&s, &lv));
    ticks(10u);
    CHECK_EQ(s.cap.pre_n, SENSE_CAP_FILTER_N);
    sense_sched_cap_edge(&s, g_us);
    i3221->amps[0] = 0.22;
    tick();
    i3221->amps[0] = 0.12;
    tick();
    CHECK_EQ(s.cap.state, SENSE_CAP_WAITING);
    ticks(3010u);
    CHECK_EQ(s.cap.state, SENSE_CAP_UNSEEN);

    /* Armed with no sample before the edge: the transient is a move. */
    CHECK(sense_sched_cap_arm(&s, &lv));
    sense_sched_cap_edge(&s, g_us);
    i3221->amps[0] = 0.22;
    tick();
    CHECK_EQ(s.cap.state, SENSE_CAP_MOVING);
}

/* ------------------------------------- the level before the command */

static const sense_cap_arm_t k_auto = { SENSE_CAP_RISE_AUTO, 120000, 30000,
                                        50000 };

/* Armed with the level left to CH1: it is the 0.12 A the servo held
 * before the edge, and the move is timed as with the level given. */
TEST_CASE(a_capture_takes_the_level_before_the_command_from_ch1)
{
    servo_rig(0.95f);
    servo_ticks(100u);
    CHECK(sense_sched_cap_arm(&s, &k_auto));
    servo_ticks(5u);
    const uint64_t edge = g_us + 500u;
    sense_sched_cap_edge(&s, edge);
    CHECK(s.cap.rise_owed);
    servo_tick();                            /* before the edge */
    CHECK(s.cap.rise_owed);
    g_cmd = 1900u;
    servo_tick();                            /* the first past it */
    CHECK(!s.cap.rise_owed);
    /* The model holds at about 0.12 A; the INA3221 reads it in 0.4 mA
     * steps. */
    CHECK_NEAR((double)s.cap.arm.rise_ua, 120000.0, 1200.0);
    CHECK_NEAR((double)s.cap.mv.cfg.rise_a, s.cap.arm.rise_ua * 1e-6, 1e-6);
    for (unsigned k = 0; k < 1000u && !servo_move_over(&s.cap.mv); ++k) {
        servo_tick();
    }
    CHECK_EQ(s.cap.state, SENSE_CAP_ARRIVED);
    CHECK(s.cap.move_t <= 30u);
    CHECK(s.cap.arrive_t >= 6670u);
    CHECK(s.cap.arrive_t <= 6670u + 40u);
}

/* The level is the 50 ms before the edge: older samples are not in it,
 * samples taken between the call and an edge that lies ahead are, and a
 * clipped sample counts at the end of the range. */
TEST_CASE(the_level_before_the_command_is_the_50_ms_before_the_edge)
{
    rig(1u, true);
    i3221->amps[0] = 0.5;
    ticks(100u);
    i3221->amps[0] = 0.2;
    ticks(40u);
    CHECK(sense_sched_cap_arm(&s, &k_auto));
    const uint64_t edge = g_us + 10000u;     /* 10 ms ahead */
    sense_sched_cap_edge(&s, edge);
    i3221->amps[0] = 0.3;
    ticks(10u);                              /* up to the edge */
    CHECK(s.cap.rise_owed);
    tick();                                  /* at the edge */
    CHECK(!s.cap.rise_owed);
    /* 40 samples at 0.2 A and 10 at 0.3 A. */
    CHECK_EQ(s.cap.arm.rise_ua, 220000);

    rig(1u, true);
    i3221->amps[0] = 2.0;                    /* past 1.638 A: clipped */
    ticks(60u);
    CHECK(sense_sched_cap_arm(&s, &k_auto));
    sense_sched_cap_edge(&s, g_us);
    tick();
    CHECK_EQ(s.cap.arm.rise_ua, s.ch_clip_ua);
    CHECK_EQ(s.cap.state, SENSE_CAP_WAITING);
}

/* No CH1 sample in the 50 ms before the edge: the capture ends lost, at
 * the first sample past it.  A level given at the arm needs none. */
TEST_CASE(a_capture_with_no_ch1_level_before_the_edge_is_lost)
{
    rig(1u, true);
    tick();                                  /* probe, the first sample */
    CHECK(sense_sched_cap_arm(&s, &k_auto));
    sense_sched_cap_edge(&s, g_us - 2000u);  /* before every sample */
    CHECK_EQ(s.cap.state, SENSE_CAP_WAITING);
    tick();
    CHECK_EQ(s.cap.state, SENSE_CAP_LOST);
    CHECK_EQ(s.cap.seq, 1u);

    rig(1u, true);
    i3221->amps[0] = 0.12;                   /* k_levels' level */
    tick();
    CHECK(sense_sched_cap_arm(&s, &k_levels));
    sense_sched_cap_edge(&s, g_us - 2000u);
    tick();
    CHECK_EQ(s.cap.state, SENSE_CAP_WAITING);
}

/* Armed and never given its edge -- nothing rendered the frame -- the
 * capture ends lost at SENSE_CAP_EDGE_WAIT_MS, not armed for ever; an
 * edge in time keeps it. */
TEST_CASE(a_capture_with_no_edge_ends_lost)
{
    rig(1u, true);
    i3221->amps[0] = 0.12;
    tick();
    /* Armed at the next tick's time: that tick is 0 ms after the arm, so
     * the last one still armed is 2999 ms after it. */
    CHECK(sense_sched_cap_arm(&s, &k_levels));
    ticks(SENSE_CAP_EDGE_WAIT_MS);
    CHECK_EQ(s.cap.state, SENSE_CAP_ARMED);
    tick();
    CHECK_EQ(s.cap.state, SENSE_CAP_LOST);
    CHECK_EQ(s.cap.seq, 1u);

    CHECK(sense_sched_cap_arm(&s, &k_levels));
    ticks(SENSE_CAP_EDGE_WAIT_MS - 10u);
    sense_sched_cap_edge(&s, g_us);
    ticks(20u);
    CHECK_EQ(s.cap.state, SENSE_CAP_WAITING);
}

/* The capture's states are SERVO_SENSE's CAP_STATE values. */
TEST_CASE(the_capture_states_are_the_links)
{
    CHECK_EQ((int)SENSE_CAP_IDLE, (int)LINK_CAP_IDLE);
    CHECK_EQ((int)SENSE_CAP_ARMED, (int)LINK_CAP_ARMED);
    CHECK_EQ((int)SENSE_CAP_WAITING, (int)LINK_CAP_WAIT_MOVE);
    CHECK_EQ((int)SENSE_CAP_MOVING, (int)LINK_CAP_MOVING);
    CHECK_EQ((int)SENSE_CAP_ARRIVED, (int)LINK_CAP_ARRIVED);
    CHECK_EQ((int)SENSE_CAP_SETTLED, (int)LINK_CAP_AT_STOP);
    CHECK_EQ((int)SENSE_CAP_LATE, (int)LINK_CAP_LATE);
    CHECK_EQ((int)SENSE_CAP_UNSEEN, (int)LINK_CAP_UNSEEN);
    CHECK_EQ((int)SENSE_CAP_LOST, (int)LINK_CAP_LOST);
}

/* ---------------------------------------------------------- the ring */

/* Window @p n of a run: CH1 draws 0.1 A times n + 1 through its 50 ticks. */
static void fill_window(unsigned n)
{
    i3221->amps[0] = 0.1 * (double)(n + 1u);
    ticks(50u);
}

/* CH1's last four complete windows, newest first, each with its figures:
 * after 1, 4, 5 and 6 windows. */
TEST_CASE(the_ring_keeps_ch1s_last_four_windows)
{
    rig(1u, true);
    sense_ring_win_t r;
    sense_window_t w;
    for (unsigned k = 0; k < SENSE_WIN_RING; ++k) {
        CHECK(!sense_sched_ring(&s, k, &r));
    }
    fill_window(0u);
    CHECK(!sense_sched_ring(&s, 0u, &r));        /* none has closed */

    /* One window: the newest, and nothing behind it. */
    fill_window(1u);
    CHECK(sense_sched_ring(&s, 0u, &r));
    CHECK(r.closed);
    CHECK_EQ(r.n_i, 50u);
    CHECK_EQ(r.n_clip, 0u);
    CHECK_EQ(r.n_v, 3u);
    CHECK_EQ(r.i_mean_ua, 100000);
    CHECK_EQ(r.i_min_ua, 100000);
    CHECK_EQ(r.i_max_ua, 100000);
    CHECK_EQ(r.v_mean_uv, 6000000);
    CHECK_EQ(r.v_min_uv, 6000000);
    CHECK(!r.clip_hi && !r.clip_lo);
    for (unsigned k = 1u; k < SENSE_WIN_RING; ++k) {
        CHECK(!sense_sched_ring(&s, k, &r));
        CHECK(!r.closed);
        CHECK_EQ(r.n_i, 0u);
    }
    CHECK(!sense_sched_ring(&s, SENSE_WIN_RING, &r));

    /* Four: windows 3, 2, 1, 0. */
    fill_window(2u);
    fill_window(3u);
    fill_window(4u);
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK_EQ(w.number, 3u);
    for (unsigned k = 0; k < SENSE_WIN_RING; ++k) {
        CHECK(sense_sched_ring(&s, k, &r));
        CHECK_EQ(r.i_mean_ua, 100000 * (int32_t)(4u - k));
        CHECK_EQ(r.n_i, 50u);
    }
    /* The newest entry is the window SERVO_SENSE shows. */
    CHECK(sense_sched_ring(&s, 0u, &r));
    CHECK_EQ(r.i_mean_ua, w.i_mean_ua);
    CHECK_EQ(r.i_max_ua, w.i_max_ua);
    CHECK_EQ(r.v_mean_uv, w.v_mean_uv);
    CHECK_EQ(r.v_min_uv, w.v_min_uv);
    CHECK_EQ(r.n_v, w.n_v);

    /* Five: window 0 has left the ring.  Six: window 1 has. */
    fill_window(5u);
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK_EQ(w.number, 4u);
    for (unsigned k = 0; k < SENSE_WIN_RING; ++k) {
        CHECK(sense_sched_ring(&s, k, &r));
        CHECK_EQ(r.i_mean_ua, 100000 * (int32_t)(5u - k));
    }
    fill_window(6u);
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK_EQ(w.number, 5u);
    for (unsigned k = 0; k < SENSE_WIN_RING; ++k) {
        CHECK(sense_sched_ring(&s, k, &r));
        CHECK_EQ(r.i_mean_ua, 100000 * (int32_t)(6u - k));
    }
}

/* The window number is 16 bits on the link: the ring runs on across
 * 65535 to 0, an entry still the window numbered k before the newest. */
TEST_CASE(the_ring_runs_on_across_window_65535)
{
    rig(1u, true);
    fill_window(0u);
    /* A tick late by 65532 windows: window 0 closes, and the next one
     * filled is number 65533. */
    g_us += (uint64_t)65532u * SENSE_WINDOW_MS * 1000u;
    fill_window(1u);
    fill_window(2u);                             /* 65534; closes 65533 */
    sense_window_t w;
    sense_ring_win_t r;
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK_EQ(w.number, 65533u);
    CHECK(sense_sched_ring(&s, 0u, &r));
    CHECK_EQ(r.i_mean_ua, 200000);
    /* Window 0 lies 65533 numbers back, and the three before 65533 never
     * were. */
    for (unsigned k = 1u; k < SENSE_WIN_RING; ++k) {
        CHECK(!sense_sched_ring(&s, k, &r));
    }
    fill_window(3u);                             /* 65535 */
    fill_window(4u);                             /* 65536: number 0 */
    fill_window(5u);                             /* 65537: number 1 */
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK_EQ(w.number, 0u);
    for (unsigned k = 0; k < SENSE_WIN_RING; ++k) {
        CHECK(sense_sched_ring(&s, k, &r));      /* 0, 65535, 65534, 65533 */
        CHECK_EQ(r.i_mean_ua, 100000 * (int32_t)(5u - k));
    }
    fill_window(6u);
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK_EQ(w.number, 1u);
    for (unsigned k = 0; k < SENSE_WIN_RING; ++k) {
        CHECK(sense_sched_ring(&s, k, &r));      /* 1, 0, 65535, 65534 */
        CHECK_EQ(r.i_mean_ua, 100000 * (int32_t)(6u - k));
    }
}

/* Ticks at 0 to 99 ms, then the tick due at 100 ms comes @p late_ms late
 * and the ticks after it 1 ms apart, up to the tick at @p until_ms. */
static void late_tick(unsigned late_ms, unsigned until_ms)
{
    rig(1u, true);
    i3221->amps[0] = 0.5;
    ticks(100u);
    g_us += (uint64_t)late_ms * 1000u;
    ticks(until_ms - 100u - late_ms + 1u);
}

/* A tick late by less than a window skips no number; one late by a window
 * or more leaves out the windows that saw no tick, and the ring's places
 * for their numbers read not closed. */
TEST_CASE(a_late_tick_skips_numbers_and_the_ring_says_which)
{
    sense_window_t w;
    sense_ring_win_t r;
    /* 49 ms: the tick lands at 149 ms, in window 2.  Windows 0, 1 and 2
     * all close; window 2 holds the one late sample. */
    late_tick(49u, 150u);
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK_EQ(w.number, 2u);
    CHECK(sense_sched_ring(&s, 0u, &r));
    CHECK_EQ(r.n_i, 1u);
    CHECK(sense_sched_ring(&s, 1u, &r));
    CHECK_EQ(r.n_i, 50u);
    CHECK(sense_sched_ring(&s, 2u, &r));
    CHECK_EQ(r.n_i, 50u);
    CHECK(!sense_sched_ring(&s, 3u, &r));

    /* 50 ms: the tick lands at 150 ms, in window 3.  No tick fell in
     * window 2: its number is skipped. */
    late_tick(50u, 200u);
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK_EQ(w.number, 3u);
    CHECK(sense_sched_ring(&s, 0u, &r));
    CHECK_EQ(r.n_i, 50u);
    CHECK(!sense_sched_ring(&s, 1u, &r));        /* window 2 */
    CHECK(!r.closed);
    CHECK_EQ(r.n_i, 0u);
    CHECK_EQ(r.i_mean_ua, 0);
    CHECK(sense_sched_ring(&s, 2u, &r));         /* window 1 */
    CHECK_EQ(r.n_i, 50u);
    CHECK(sense_sched_ring(&s, 3u, &r));         /* window 0 */

    /* 51 ms: the same, window 3 one sample short. */
    late_tick(51u, 200u);
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK_EQ(w.number, 3u);
    CHECK(sense_sched_ring(&s, 0u, &r));
    CHECK_EQ(r.n_i, 49u);
    CHECK(!sense_sched_ring(&s, 1u, &r));
    CHECK(sense_sched_ring(&s, 2u, &r));
    CHECK(sense_sched_ring(&s, 3u, &r));

    /* 5 windows: the tick lands at 350 ms, in window 7.  Window 1 closes
     * then, with windows 2 to 6 skipped: at first the ring is windows 1
     * and 0, and once window 7 closes it alone. */
    late_tick(250u, 350u);
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK_EQ(w.number, 1u);
    CHECK(sense_sched_ring(&s, 0u, &r));
    CHECK(sense_sched_ring(&s, 1u, &r));
    CHECK(!sense_sched_ring(&s, 2u, &r));
    late_tick(250u, 400u);
    CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
    CHECK_EQ(w.number, 7u);
    CHECK(sense_sched_ring(&s, 0u, &r));
    CHECK_EQ(r.n_i, 50u);
    for (unsigned k = 1u; k < SENSE_WIN_RING; ++k) {
        CHECK(!sense_sched_ring(&s, k, &r));     /* windows 6, 5, 4 */
        CHECK(!r.closed);
    }
}

/* A clipped sample is the reading it is: in a ring entry it counts at the
 * end of the range, 1.638 A on the 0.1 Ω shunt, and the entry says how
 * many of its samples did.  sense_sched_window() keeps them out. */
TEST_CASE(a_ring_entry_counts_its_clipped_samples)
{
    static const unsigned k_clipped[] = { 0u, 1u, 44u, 45u, 50u };
    for (unsigned c = 0; c < sizeof(k_clipped) / sizeof(k_clipped[0]); ++c) {
        const unsigned n = k_clipped[c];
        rig(1u, true);
        for (unsigned k = 0; k < 50u; ++k) {
            i3221->amps[0] = (k < n) ? 2.0 : 1.0;
            tick();
        }
        i3221->amps[0] = 1.0;
        tick();                                  /* closes window 0 */
        sense_ring_win_t r;
        CHECK(sense_sched_ring(&s, 0u, &r));
        CHECK_EQ(r.n_i, 50u);
        CHECK_EQ(r.n_clip, n);
        CHECK_EQ(r.clip_hi, n > 0u);
        CHECK(!r.clip_lo);
        CHECK_EQ(r.i_max_ua, (n > 0u) ? 1638000 : 1000000);
        CHECK_EQ(r.i_min_ua, (n == 50u) ? 1638000 : 1000000);
        CHECK_EQ(r.i_mean_ua,
                 (int32_t)(((int64_t)n * 1638000
                            + (int64_t)(50u - n) * 1000000) / 50));
        sense_window_t w;
        CHECK(sense_sched_window(&s, SENSE_SRC_CH1, &w));
        CHECK_EQ(w.n_i, 50u - n);
        CHECK_EQ(w.i_mean_ua, (n == 50u) ? 0 : 1000000);
        CHECK_EQ(w.clip_hi, n > 0u);
    }

    /* The bottom of the range, and both ends in one window. */
    rig(1u, true);
    for (unsigned k = 0; k < 50u; ++k) {
        i3221->amps[0] = (k < 3u) ? -2.0 : -1.0;
        tick();
    }
    for (unsigned k = 0; k < 50u; ++k) {
        i3221->amps[0] = (k == 7u) ? 2.0 : (k == 9u) ? -2.0 : 0.2;
        tick();
    }
    tick();
    sense_ring_win_t r;
    CHECK(sense_sched_ring(&s, 1u, &r));
    CHECK_EQ(r.n_i, 50u);
    CHECK_EQ(r.n_clip, 3u);
    CHECK(r.clip_lo && !r.clip_hi);
    CHECK_EQ(r.i_min_ua, -1638400);
    CHECK_EQ(r.i_max_ua, -1000000);
    CHECK_EQ(r.i_mean_ua, (3 * -1638400 + 47 * -1000000) / 50);
    CHECK(sense_sched_ring(&s, 0u, &r));
    CHECK_EQ(r.n_clip, 2u);
    CHECK(r.clip_lo && r.clip_hi);
    CHECK_EQ(r.i_max_ua, 1638000);
    CHECK_EQ(r.i_min_ua, -1638400);
    CHECK_EQ(r.i_mean_ua, (1638000 - 1638400 + 48 * 200000) / 50);

    /* A window of nothing but the bottom. */
    rig(1u, true);
    i3221->amps[0] = -2.0;
    ticks(51u);
    CHECK(sense_sched_ring(&s, 0u, &r));
    CHECK_EQ(r.n_clip, 50u);
    CHECK_EQ(r.i_max_ua, -1638400);
    CHECK_EQ(r.i_min_ua, -1638400);
    CHECK_EQ(r.i_mean_ua, -1638400);
}

/* ------------------------------------------------- a part that reset */

/* One rotation: a slot every second tick, 40 ms. */
#define ROTATION_TICKS (SENSE_ROTATION * 2u)

/* Ticks until the INA3221 leaves online, @p limit at most; how many. */
static unsigned ticks_to_3221_offline(unsigned limit)
{
    unsigned n = 0u;
    while (n < limit && ina3221_state(&s.i3221) == SENSE_PART_ONLINE) {
        tick();
        ++n;
    }
    return n;
}

static unsigned ticks_to_228_offline(unsigned limit)
{
    unsigned n = 0u;
    while (n < limit && ina228_state(&s.i228) == SENSE_PART_ONLINE) {
        tick();
        ++n;
    }
    return n;
}

/* An INA3221 that resets itself answers every read, on its power-on
 * Configuration.  The read-back finds it within one rotation, 40 ms,
 * wherever in the rotation the reset falls, and counts it once. */
TEST_CASE(an_ina3221_reset_is_found_within_one_rotation)
{
    for (unsigned phase = 0; phase < ROTATION_TICKS; ++phase) {
        rig(1u, true);
        i3221->amps[0] = 0.5;
        ticks(200u + phase);
        CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_ONLINE);
        CHECK_EQ(s.i3221_resets, 0u);
        fake_reset3221(i3221);
        CHECK_EQ(i3221->reg[INA3221_CONFIG], INA3221_CONFIG_RESET);
        const unsigned n = ticks_to_3221_offline(100u);
        CHECK(n <= ROTATION_TICKS);
        CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_OFFLINE);
        CHECK_EQ(s.i3221_resets, 1u);
        CHECK_EQ(s.i228_resets, 0u);
        CHECK_EQ(ina228_state(&s.i228), SENSE_PART_ONLINE);
        CHECK_EQ(s.bus.errors, 0u);              /* every read answered */
    }
}

/* What a found reset does, from a clock at @p start_us: the window being
 * filled is emptied, a capture under way ends lost, the reset is counted
 * once, nothing is sent to the part for SENSE_RETRY_MS, and then it is
 * set up again and read. */
static void reset_then_back(uint64_t start_us)
{
    rig(1u, true);
    g_us = start_us;
    i3221->amps[0] = 0.12;
    ticks(203u);
    CHECK(sense_sched_cap_arm(&s, &k_levels));
    sense_sched_cap_edge(&s, g_us);
    ticks(2u);
    CHECK_EQ(s.cap.state, SENSE_CAP_WAITING);
    fake_reset3221(i3221);
    CHECK(ticks_to_3221_offline(100u) <= ROTATION_TICKS);
    CHECK_EQ(s.i3221_resets, 1u);
    CHECK_EQ(s.cap.state, SENSE_CAP_LOST);
    CHECK_EQ(s.cap.seq, 1u);
    CHECK(!sense_sched_cap_arm(&s, &k_levels));
    /* The window being filled holds nothing, of CH1 or its voltage. */
    CHECK_EQ(s.acc[SENSE_SRC_CH1].n_i, 0u);
    CHECK_EQ(s.acc[SENSE_SRC_CH1].n_v, 0u);
    const uint64_t found_in = s.win;
    const unsigned config_reads = i3221->reads[INA3221_CONFIG];
    const unsigned ch1_reads = i3221->reads[INA3221_SHUNT1];

    /* The window it was found in closes empty. */
    sense_ring_win_t r;
    ticks(50u);
    CHECK(s.ring_at >= found_in);
    CHECK(sense_sched_ring(&s, (unsigned)(s.ring_at - found_in), &r));
    CHECK_EQ(r.n_i, 0u);
    CHECK_EQ(r.n_v, 0u);
    /* 999 ms after the tick that found it: still offline, nothing sent
     * to it, and its Configuration still the reset value. */
    ticks(949u);
    CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_OFFLINE);
    CHECK_EQ(i3221->reads[INA3221_CONFIG], config_reads);
    CHECK_EQ(i3221->reads[INA3221_SHUNT1], ch1_reads);
    CHECK_EQ(i3221->reg[INA3221_CONFIG], INA3221_CONFIG_RESET);
    for (unsigned k = 0; k < SENSE_WIN_RING; ++k) {
        CHECK(sense_sched_ring(&s, k, &r));      /* closed, and empty */
        CHECK_EQ(r.n_i, 0u);
        CHECK_EQ(r.n_v, 0u);
    }
    /* 1000 ms: probed, set up again and read in that tick. */
    tick();
    CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_ONLINE);
    CHECK_EQ(i3221->reg[INA3221_CONFIG], INA3221_CONFIG_BENCH_CH1);
    CHECK_EQ(i3221->reads[INA3221_SHUNT1], ch1_reads + 1u);
    /* 1001 ms, and on: online, the reset counted once, windows whole. */
    tick();
    CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_ONLINE);
    ticks(150u);
    CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_ONLINE);
    CHECK_EQ(s.i3221_resets, 1u);
    CHECK(sense_sched_ring(&s, 0u, &r));
    CHECK_EQ(r.n_i, 50u);
    CHECK_EQ(r.i_mean_ua, 120000);
    CHECK(sense_sched_cap_arm(&s, &k_levels));
}

TEST_CASE(a_reset_ina3221_is_emptied_counted_and_set_up_again)
{
    reset_then_back(10000000u);
    /* The retry across the 32-bit millisecond count's wrap: the reset
     * found about 500 ms before it. */
    reset_then_back(((uint64_t)1u << 32) * 1000u - 750000u);
    /* And the reset itself across it. */
    reset_then_back(((uint64_t)1u << 32) * 1000u - 210000u);
}

/* The counts are 8 bits each on the link and run on from 255 to 0. */
TEST_CASE(the_reset_counts_run_on_across_255)
{
    rig(1u, true);
    ticks(100u);
    s.i3221_resets = 254u;
    s.i228_resets  = 255u;
    fake_reset3221(i3221);
    CHECK(ticks_to_3221_offline(100u) <= ROTATION_TICKS);
    CHECK_EQ(s.i3221_resets, 255u);
    CHECK_EQ(s.i228_resets, 255u);
    ticks(1100u);
    CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_ONLINE);
    fake_reset3221(i3221);
    CHECK(ticks_to_3221_offline(100u) <= ROTATION_TICKS);
    CHECK_EQ(s.i3221_resets, 0u);
    fake_reset228(i228);
    CHECK(ticks_to_228_offline(100u) <= ROTATION_TICKS);
    CHECK_EQ(s.i228_resets, 0u);
    CHECK_EQ(s.i3221_resets, 0u);
}

/* One read of a set-up register that comes back wrong is read again at
 * once and is no reset; two in a row are. */
TEST_CASE(one_corrupted_read_back_is_no_reset)
{
    rig(1u, true);
    i3221->amps[0] = 0.5;
    ticks(100u);
    unsigned before = i3221->reads[INA3221_CONFIG];
    i3221->glitch_reg   = INA3221_CONFIG;
    i3221->glitch_value = INA3221_CONFIG_RESET;
    i3221->glitch_n     = 1u;
    ticks(ROTATION_TICKS);
    CHECK_EQ(i3221->glitch_n, 0u);
    CHECK_EQ(i3221->reads[INA3221_CONFIG], before + 2u);
    CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_ONLINE);
    CHECK_EQ(s.i3221_resets, 0u);
    i3221->glitch_n = 2u;
    ticks(ROTATION_TICKS);
    CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_OFFLINE);
    CHECK_EQ(s.i3221_resets, 1u);

    rig(1u, true);
    i228->amps[0] = 10.0;
    ticks(3u);
    sense_sched_arm(&s);
    ticks(100u);
    before = i228->reads[INA228_ADC_CONFIG];
    i228->glitch_reg   = INA228_ADC_CONFIG;
    i228->glitch_value = INA228_ADC_RESET;
    i228->glitch_n     = 1u;
    ticks(ROTATION_TICKS);
    CHECK_EQ(i228->reads[INA228_ADC_CONFIG], before + 2u);
    CHECK_EQ(ina228_state(&s.i228), SENSE_PART_ONLINE);
    CHECK_EQ(s.i228_resets, 0u);
    CHECK(s.run.totals_ok);
    i228->glitch_n = 2u;
    ticks(ROTATION_TICKS);
    CHECK_EQ(ina228_state(&s.i228), SENSE_PART_OFFLINE);
    CHECK_EQ(s.i228_resets, 1u);
    CHECK(!s.run.totals_ok);

    /* A read-back that fails on the wire is a failed transaction and no
     * reset: the part stays online, and the INA228's totals end. */
    rig(1u, true);
    ticks(3u);
    sense_sched_arm(&s);
    ticks(100u);
    CHECK(s.run.totals_ok);
    while ((s.ticks & 1u) != 0u || s.rot % SENSE_ROTATION != 11u) {
        tick();                                  /* to ADC_CONFIG's slot */
    }
    fb.fail_with = SENSE_NACK;
    fb.fail_at = fb.transactions + 3u;           /* CH1, CURRENT, the item */
    tick();
    CHECK_EQ(s.bus.errors, 1u);
    CHECK_EQ(ina228_state(&s.i228), SENSE_PART_ONLINE);
    CHECK_EQ(s.i228_resets, 0u);
    CHECK(!s.run.totals_ok);
    while ((s.ticks & 1u) != 0u || s.rot % SENSE_ROTATION != 19u) {
        tick();                                  /* to Configuration's */
    }
    fb.fail_at = fb.transactions + 3u;
    tick();
    CHECK_EQ(s.bus.errors, 2u);
    CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_ONLINE);
    CHECK_EQ(s.i3221_resets, 0u);
}

/* An INA228 that resets itself runs at ADCRANGE 0: with the MATEK set-up
 * at ADCRANGE 1 its CURRENT reads a quarter.  The read-back of ADC_CONFIG
 * finds it within one rotation: offline, its window emptied, the totals no
 * longer the run's, and after SENSE_RETRY_MS set up again and reading the
 * whole current.  No window shows the quarter. */
static void i228_reset_then_back(uint64_t start_us)
{
    rig(1u, true);
    g_us = start_us;
    i228->amps[0] = 100.0;
    ticks(3u);
    sense_sched_arm(&s);
    ticks(197u);                                 /* to a window's start */
    sense_window_t w;
    CHECK(sense_sched_window(&s, SENSE_SRC_INA228, &w));
    CHECK_NEAR((double)w.i_mean_ua, 100.0e6, 1.0e6);
    CHECK(s.run.totals_ok);
    CHECK((i228->reg[INA228_CONFIG] & INA228_CONFIG_ADCRANGE) != 0u);

    fake_reset228(i228);
    const unsigned n = ticks_to_228_offline(100u);
    CHECK(n <= ROTATION_TICKS);
    CHECK_EQ(ina228_state(&s.i228), SENSE_PART_OFFLINE);
    CHECK_EQ(s.i228_resets, 1u);
    CHECK_EQ(s.i3221_resets, 0u);
    CHECK(!s.run.totals_ok);
    CHECK(!s.have_vbus);
    CHECK_EQ(s.acc[SENSE_SRC_INA228].n_i, 0u);
    CHECK_EQ(s.acc[SENSE_SRC_INA228].n_v, 0u);
    CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_ONLINE);

    /* Every window from here to the part's return and past it: empty, or
     * the whole current. */
    unsigned whole = 0u;
    uint16_t last = w.number;
    for (unsigned k = 1u; k <= 1300u; ++k) {     /* ms since it was found */
        if (k == 1000u) {                        /* 999 ms have passed */
            CHECK_EQ(ina228_state(&s.i228), SENSE_PART_OFFLINE);
            CHECK_EQ(i228->reg[INA228_ADC_CONFIG], INA228_ADC_RESET);
        }
        tick();
        if (k == 1000u) {
            CHECK_EQ(ina228_state(&s.i228), SENSE_PART_ONLINE);
            CHECK_EQ(i228->reg[INA228_ADC_CONFIG], INA228_ADC_BENCH);
            CHECK((i228->reg[INA228_CONFIG] & INA228_CONFIG_ADCRANGE) != 0u);
        }
        CHECK(sense_sched_window(&s, SENSE_SRC_INA228, &w));
        if (w.number != last) {
            last = w.number;
            if (w.n_i > 0u) {
                CHECK_NEAR((double)w.i_mean_ua, 100.0e6, 1.0e6);
                ++whole;
            }
        }
    }
    CHECK(whole >= 4u);
    CHECK_EQ(ina228_state(&s.i228), SENSE_PART_ONLINE);
    CHECK_EQ(s.i228_resets, 1u);
    /* The totals are a run's again from the next arm. */
    CHECK(!s.run.totals_ok);
    sense_sched_arm(&s);
    tick();
    CHECK(s.run.totals_ok);
}

TEST_CASE(a_reset_ina228_is_found_and_set_up_again)
{
    i228_reset_then_back(10000000u);
    i228_reset_then_back(((uint64_t)1u << 32) * 1000u - 750000u);

    /* Wherever in the rotation the reset falls. */
    for (unsigned phase = 0; phase < ROTATION_TICKS; ++phase) {
        rig(1u, true);
        ticks(200u + phase);
        fake_reset228(i228);
        CHECK(ticks_to_228_offline(100u) <= ROTATION_TICKS);
        CHECK_EQ(s.i228_resets, 1u);
        CHECK_EQ(s.bus.errors, 0u);
    }

    /* A set-up at ADCRANGE 0, 300 A on 200 µΩ: CONFIG reads back as
     * written after a reset, and ADC_CONFIG tells. */
    sense_bus_t scratch;
    fake_bus_init(&fb, &scratch);
    i228  = fake_add(&fb, FAKE_INA228, I228_ADDR, 200e-6);
    i3221 = fake_add(&fb, FAKE_INA3221, I3221_ADDR, 0.1);
    const sense_sched_io_t io = { { timed_read, timed_write, &fb }, now_us,
                                  NULL, NULL };
    const sense_sched_cfg_t cfg = {
        .ina228_en = true, .ina228_addr = I228_ADDR,
        .ina228_shunt_uohm = I228_UOHM, .ina228_max_ma = 300000u,
    };
    g_us = 10000000u;
    sense_sched_init(&s, &io, &cfg);
    ticks(100u);
    CHECK_EQ(ina228_state(&s.i228), SENSE_PART_ONLINE);
    CHECK_EQ(i228->reg[INA228_CONFIG] & INA228_CONFIG_ADCRANGE, 0u);
    fake_reset228(i228);
    CHECK(ticks_to_228_offline(100u) <= ROTATION_TICKS);
    CHECK_EQ(s.i228_resets, 1u);
}

/* Between the reset and the read-back that finds it the INA3221 converts
 * on its power-on set-up: one CH1 result every 6.6 ms, read up to seven
 * times over.  The values are right; the 1 ms samples repeat. */
TEST_CASE(a_reset_ina3221_repeats_its_samples_until_it_is_found)
{
    rig(1u, true);
    i3221->clock_us = &g_us;
    g_xfer_us = 120u;                            /* a transaction's time */
    /* A ramp of 0.4 mA, one step of the shunt register, a tick. */
    unsigned distinct = 0u;
    int32_t last = -1;
    for (unsigned k = 0; k < 100u; ++k) {
        i3221->amps[0] = 0.1 + 0.0004 * (double)k;
        tick();
        const int32_t ua = s.ch1[(s.ch1_head + SENSE_CH1_HISTORY - 1u)
                                 % SENSE_CH1_HISTORY].ua;
        distinct += (ua != last);
        last = ua;
    }
    CHECK_EQ(distinct, 100u);                    /* 280 µs a result */

    while (s.rot % SENSE_ROTATION != 0u) {
        tick();                                  /* the read-back is done */
    }
    fake_reset3221(i3221);
    distinct = 0u;
    unsigned n = 0u;
    while (ina3221_state(&s.i3221) == SENSE_PART_ONLINE && n < 100u) {
        i3221->amps[0] = 0.2 + 0.0004 * (double)n;
        tick();
        const int32_t ua = s.ch1[(s.ch1_head + SENSE_CH1_HISTORY - 1u)
                                 % SENSE_CH1_HISTORY].ua;
        distinct += (ua != last);
        last = ua;
        ++n;
    }
    CHECK(n >= 38u && n <= ROTATION_TICKS);
    CHECK(distinct >= 5u && distinct <= 7u);     /* 40 ms / 6.6 ms */
}

/* -------------------------------------------------------- bus time */

/* The worst tick's reads, in clocks, over @p n ticks: all three parts. */
static unsigned worst_tick_clocks(fake_part_t *enc, unsigned n, bool even_only)
{
    unsigned worst = 0u;
    for (unsigned k = 0; k < n; ++k) {
        const bool even = (s.ticks & 1u) == 0u;
        const uint64_t c0 = i228->clocks + i3221->clocks
                            + ((enc != NULL) ? enc->clocks : 0u);
        tick();
        const unsigned c = (unsigned)(i228->clocks + i3221->clocks
                                      + ((enc != NULL) ? enc->clocks : 0u)
                                      - c0);
        if (c > worst && (even || !even_only)) {
            worst = c;
        }
    }
    return worst;
}

/* Every tick of the 20-slot rotation fits the 1 ms tick at 400 kHz, 2.5 µs
 * a clock: with all three channels, the pair at 1000 Hz and the encoder.
 * The read-backs take the slots of DIETEMP and Mask/Enable and are as
 * long: an even tick stays at 690 µs, and the one that reads a set-up
 * register twice is 742.5 µs. */
TEST_CASE(every_tick_of_the_rotation_fits_a_millisecond)
{
    sense_bus_t scratch;
    fake_bus_init(&fb, &scratch);
    i228  = fake_add(&fb, FAKE_INA228, I228_ADDR, 200e-6);
    i3221 = fake_add(&fb, FAKE_INA3221, I3221_ADDR, 0.1);
    fake_part_t *enc = fake_add(&fb, FAKE_AS5600, AS5600_ADDR, 0.0);
    const sense_sched_io_t io = { { timed_read, timed_write, &fb }, now_us,
                                  NULL, NULL };
    const sense_sched_cfg_t cfg = {
        .ina228_en = true, .ina228_addr = I228_ADDR,
        .ina228_shunt_uohm = I228_UOHM, .ina228_max_ma = I228_MA,
        .ina3221_en = true, .ina3221_addr = I3221_ADDR,
        .ina3221_shunt_uohm = I3221_UOHM, .ina3221_channels = 7u,
        .as5600_en = true,
    };
    g_us = 10000000u;
    g_xfer_us = 0u;
    g_xfer_228_us = 0u;
    g_switch_us = 0u;
    sense_sched_init(&s, &io, &cfg);
    ticks(2u);                                   /* the probes */
    CHECK_EQ(ina228_state(&s.i228), SENSE_PART_ONLINE);
    CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_ONLINE);
    CHECK_EQ(as5600_state(&s.enc.dev), SENSE_PART_ONLINE);

    /* The pair off: CH1 (48 clocks), the INA228's slot (57) and the
     * longest item, ENERGY or CHARGE (75): 180 clocks, 450 µs. */
    CHECK_EQ(worst_tick_clocks(enc, 1000u, true), 48u + 57u + 75u);
    /* The pair on.  Even: 3 x 48 + 57 + 75 = 276 clocks, 690 µs.  Odd,
     * with the encoder's field slot: 3 x 48 + 57 + 135 = 336, 840 µs. */
    sense_sched_fast_pair(&s, true);
    CHECK_EQ(worst_tick_clocks(enc, 1000u, true), 276u);
    CHECK_EQ(worst_tick_clocks(enc, 1000u, false), 336u);
    CHECK(336u * 25u < 10000u);                  /* 2.5 µs a clock: 1 ms */

    /* Both read-backs read twice in every rotation: 3 x 48 + 57 + 2 x 48
     * = 297 clocks, 742.5 µs, under the odd tick's 840 µs. */
    unsigned worst = 0u;
    for (unsigned k = 0; k < 50u; ++k) {
        i3221->glitch_reg   = INA3221_CONFIG;
        i3221->glitch_value = INA3221_CONFIG_RESET;
        i3221->glitch_n     = 1u;
        i228->glitch_reg    = INA228_ADC_CONFIG;
        i228->glitch_value  = INA228_ADC_RESET;
        i228->glitch_n      = 1u;
        const unsigned c = worst_tick_clocks(enc, ROTATION_TICKS, true);
        if (c > worst) {
            worst = c;
        }
    }
    CHECK_EQ(worst, 297u);
    CHECK_EQ(s.i228_resets, 0u);
    CHECK_EQ(s.i3221_resets, 0u);
    CHECK_EQ(fb.bad_width, 0u);
}

int main(void)
{
    RUN(each_register_is_read_at_its_rate);
    RUN(a_window_holds_fifty_milliseconds);
    RUN(a_clipped_reading_is_kept_out_of_the_window);
    RUN(the_run_keeps_its_peaks_from_the_arm);
    RUN(the_totals_are_the_runs_while_the_part_answers);
    RUN(a_missing_part_is_not_read_and_a_lost_one_comes_back);
    RUN(a_stuck_bus_is_recovered);
    RUN(a_part_not_enabled_is_never_addressed);
    RUN(a_capture_times_a_move_from_the_edge);
    RUN(a_capture_through_clipped_samples_is_timed);
    RUN(a_capture_at_a_stop_settles);
    RUN(a_capture_ends_late_or_unseen);
    RUN(a_capture_is_lost_with_its_part);
    RUN(one_failed_ina228_read_ends_the_runs_totals);
    RUN(a_failed_voltage_read_leaves_the_next_current_without_power);
    RUN(windows_and_captures_run_on_across_the_wraps);
    RUN(a_sample_after_a_probe_across_a_boundary_is_the_next_windows);
    RUN(a_read_batch_across_a_boundary_splits_between_the_windows);
    RUN(an_arrival_at_the_deadline_is_late);
    RUN(slow_reads_after_ch1_do_not_move_the_capture);
    RUN(a_one_sample_transient_at_the_edge_starts_no_move);
    RUN(a_capture_takes_the_level_before_the_command_from_ch1);
    RUN(the_level_before_the_command_is_the_50_ms_before_the_edge);
    RUN(a_capture_with_no_ch1_level_before_the_edge_is_lost);
    RUN(a_capture_with_no_edge_ends_lost);
    RUN(the_capture_states_are_the_links);
    RUN(the_ring_keeps_ch1s_last_four_windows);
    RUN(the_ring_runs_on_across_window_65535);
    RUN(a_late_tick_skips_numbers_and_the_ring_says_which);
    RUN(a_ring_entry_counts_its_clipped_samples);
    RUN(an_ina3221_reset_is_found_within_one_rotation);
    RUN(a_reset_ina3221_is_emptied_counted_and_set_up_again);
    RUN(the_reset_counts_run_on_across_255);
    RUN(one_corrupted_read_back_is_no_reset);
    RUN(a_reset_ina228_is_found_and_set_up_again);
    RUN(a_reset_ina3221_repeats_its_samples_until_it_is_found);
    RUN(every_tick_of_the_rotation_fits_a_millisecond);
    return test_summary("sense_sched");
}
