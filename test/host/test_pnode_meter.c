/*
 * The power node's readings on the panel (pnode_meter.h).
 *
 * Under test: windows to the bench's voltage, current and power, as the
 * mean of the windows since the last take; a reading stale after 300ms;
 * a window with every current sample clipped reading the INA228's full
 * scale at INFO's ADCRANGE and shunt, marked clipped; the run's peaks
 * from the windows and from the node's RUN extremes under the run's tag
 * only; the totals as differences of the 40-bit accumulators across the
 * CHARGE's wrap, from the last packet before the arm, with an epoch change and a
 * fall in ENERGY adding the new counts from 0, and none for a run without
 * a recent packet; the register scaling against ina228_calibrate() and an
 * INFO set-up it does not give refused; the dropout rule of servo_gap.h:
 * a gap of 299ms and 300ms rides and 301ms ends the run, a second gap
 * 9.9s after the first ends it and one 10.1s after rides, a run that
 * starts without a reading ends 301ms in, a run on WIRED keeps no rule,
 * and one error a run however long the gap; seq losses over WIN, ACC and
 * RUN only; BENCH's own figures moving to the ESC's.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "pnode_meter.h"

/** @brief The MATEK's set-up: 200µΩ at ADCRANGE 1. */
static rcnode_info_t matek(void)
{
    rcnode_info_t i;
    memset(&i, 0, sizeof i);
    i.hdr.type = RCNODE_T_INFO;
    i.proto = RCNODE_PROTO;
    i.role = RCNODE_ROLE_POWER;
    i.board = RCNODE_BOARD_C6_ZERO;
    i.i228_addr = 0x45u;
    i.adcrange = 1u;
    i.shunt_uohm = 200u;
    return i;
}

/** @brief A meter with the MATEK's calibration. */
static void meter(pnode_meter_t *m)
{
    pnode_meter_init(m);
    const rcnode_info_t i = matek();
    CHECK(pnode_meter_info(m, &i));
}

/** @brief The next STREAM seq of the node. */
static uint8_t s_seq;

/** @brief One window into the meter. */
static void win(pnode_meter_t *m, uint32_t now, uint8_t flags, int32_t i_mean,
                int32_t i_max, uint16_t v_mean, uint16_t v_min)
{
    rcnode_pwr_win_t w;
    memset(&w, 0, sizeof w);
    w.hdr.seq = s_seq++;
    w.flags = flags;
    w.n = 18u;
    w.i_mean = i_mean;
    w.i_max = i_max;
    w.v_mean = v_mean;
    w.v_min = v_min;
    uint8_t b[RCNODE_PKT_MAX];
    const size_t n = rcnode_enc_pwr_win(&w, b, sizeof b);
    CHECK_EQ(pnode_meter_packet(m, b, n, now), RCNODE_T_PWR_WIN);
}

/** @brief A plain reading: 10A mean, 12A peak, 22.2V mean, 22.0V low. */
static void good(pnode_meter_t *m, uint32_t now)
{
    win(m, now, RCNODE_WF_ONLINE | RCNODE_WF_PACK, 10000000, 12000000,
        11100u, 11000u);
}

/** @brief One accumulator packet into the meter. */
static void acc(pnode_meter_t *m, uint32_t now, uint8_t epoch, uint64_t q,
                uint64_t e, uint8_t flags)
{
    rcnode_pwr_acc_t a;
    memset(&a, 0, sizeof a);
    a.hdr.seq = s_seq++;
    a.flags = flags;
    a.epoch = epoch;
    a.charge = q & RCNODE_ACC_MASK;
    a.energy = e & RCNODE_ACC_MASK;
    uint8_t b[RCNODE_PKT_MAX];
    const size_t n = rcnode_enc_pwr_acc(&a, b, sizeof b);
    CHECK_EQ(pnode_meter_packet(m, b, n, now), RCNODE_T_PWR_ACC);
}

/** @brief Both _OK flags of an accumulator packet. */
#define BOTH (RCNODE_AF_CHARGE_OK | RCNODE_AF_ENERGY_OK | RCNODE_AF_ONLINE)

/** @brief The bench's state after a take. */
static bench_state_t bench_of(const pnode_reading_t *was,
                              const pnode_meter_t *m, uint32_t now)
{
    pnode_reading_t r;
    pnode_meter_reading(m, &r);
    bench_state_t b;
    memset(&b, 0, sizeof b);
    (void)pnode_reading_bench(was, &r, now, &b);
    return b;
}

TEST_CASE(windows_become_the_mean_of_the_windows_since_the_last_take)
{
    pnode_meter_t m;
    meter(&m);
    pnode_reading_t was;
    pnode_meter_reading(&m, &was);
    win(&m, 1000u, RCNODE_WF_ONLINE, 10000000, 11000000, 11000u, 10900u);
    win(&m, 1020u, RCNODE_WF_ONLINE, 20000000, 21000000, 12000u, 11900u);
    bench_state_t b = bench_of(&was, &m, 1025u);
    CHECK_NEAR(b.voltage, 23.0f, 1e-3f);    /* (11000 + 12000) / 2 x 2mV */
    CHECK_NEAR(b.current, 15.0f, 1e-3f);
    CHECK_NEAR(b.power, 345.0f, 0.05f);
    const uint16_t want = (uint16_t)(LINK_BN_SENSED | LINK_BN_VOLTAGE_OK
                                     | LINK_BN_CURRENT_OK);
    CHECK_EQ(b.flags, want);
    CHECK(b.from_node);
    CHECK(!b.clipped);
    /* No window since: the last window's figures. */
    pnode_meter_reading(&m, &was);
    b = bench_of(&was, &m, 1100u);
    CHECK_NEAR(b.current, 20.0f, 1e-3f);
    CHECK_NEAR(b.voltage, 24.0f, 1e-3f);
}

TEST_CASE(a_reading_is_stale_after_300ms_and_offline_windows_are_none)
{
    pnode_meter_t m;
    meter(&m);
    good(&m, 1000u);
    bench_state_t b = bench_of(NULL, &m, 1300u);
    CHECK((b.flags & LINK_BN_CURRENT_OK) != 0u);
    b = bench_of(NULL, &m, 1301u);
    CHECK_EQ(b.flags & (LINK_BN_CURRENT_OK | LINK_BN_VOLTAGE_OK), 0u);
    CHECK((b.flags & LINK_BN_SENSED) != 0u);
    CHECK_EQ(b.current, 0.0f);
    /* A window without the INA228 online is no reading. */
    const uint32_t n = m.r.win_n;
    win(&m, 1400u, 0u, 0, RCNODE_I_NONE, 0u, RCNODE_V_NONE);
    CHECK_EQ(m.r.win_n, n);
    CHECK_EQ(m.windows, 2u);
    b = bench_of(NULL, &m, 1401u);
    CHECK_EQ(b.flags & LINK_BN_CURRENT_OK, 0u);
}

TEST_CASE(every_sample_clipped_reads_the_range_end_marked_clipped)
{
    pnode_meter_t m;
    meter(&m);
    /* 40.96mV over 200µΩ: 204.8A at ADCRANGE 1. */
    CHECK_EQ(m.fs_ua, 204800000);
    (void)pnode_meter_step(&m, 900u, true, false, true);
    win(&m, 1000u, RCNODE_WF_ONLINE | RCNODE_WF_CLIP_HIGH, 0, RCNODE_I_NONE,
        11000u, 10800u);
    bench_state_t b = bench_of(NULL, &m, 1001u);
    CHECK_NEAR(b.current, 204.8f, 1e-3f);
    CHECK_NEAR(b.current_max, 204.8f, 1e-3f);
    CHECK(b.clipped);
    win(&m, 1020u, RCNODE_WF_ONLINE | RCNODE_WF_CLIP_LOW, 0, RCNODE_I_NONE,
        11000u, 10800u);
    b = bench_of(NULL, &m, 1021u);
    CHECK_NEAR(b.current, -204.8f, 1e-3f);
    /* Some samples clipped: the mean of the rest, still a bound. */
    win(&m, 1040u, RCNODE_WF_ONLINE | RCNODE_WF_CLIP_HIGH, 150000000,
        190000000, 11000u, 10800u);
    b = bench_of(NULL, &m, 1041u);
    CHECK_NEAR(b.current, 150.0f, 1e-3f);
    CHECK(b.clipped);
    /* The run keeps its clip and its peak at the end. */
    good(&m, 1060u);
    b = bench_of(NULL, &m, 1061u);
    CHECK(b.clipped);
    CHECK_NEAR(b.current_max, 204.8f, 1e-3f);
    /* A clipped window counts as a current at ADCRANGE 0 too: 163.84mV
     * over 1000µΩ is 163.84A. */
    rcnode_info_t i = matek();
    i.adcrange = 0u;
    i.shunt_uohm = 1000u;
    CHECK(pnode_meter_info(&m, &i));
    CHECK_EQ(m.fs_ua, 163840000);
}

TEST_CASE(the_scaling_is_the_drivers_and_a_setup_it_does_not_give_is_refused)
{
    pnode_meter_t m;
    pnode_meter_init(&m);
    rcnode_info_t i = matek();
    CHECK(pnode_meter_info(&m, &i));
    ina228_cal_t want;
    CHECK_EQ(ina228_calibrate(200u, 204800u, &want), INA228_SETUP_OK);
    CHECK_EQ(m.cal.adcrange, want.adcrange);
    CHECK_EQ(m.cal.shunt_cal, want.shunt_cal);
    CHECK_EQ(m.cal.shunt_uohm, want.shunt_uohm);
    /* One ENERGY step is 20mJ and one CHARGE step 390.625µC. */
    CHECK_EQ(ina228_energy_mj(&m.cal, 1000u), 20000u);
    CHECK_EQ(ina228_charge_uc(&m.cal, 1024u), 400000);
    /* 200µΩ at ADCRANGE 0 is 819.2A, past no limit, but the driver picks
     * ADCRANGE 0 for it: taken.  A shunt of 50µΩ at ADCRANGE 0 is 3276.8A,
     * past INA228_FS_MA_LIMIT: refused. */
    i.adcrange = 0u;
    CHECK(pnode_meter_info(&m, &i));
    i.shunt_uohm = 50u;
    CHECK(!pnode_meter_info(&m, &i));
    CHECK(!m.cal_ok);
    i = matek();
    i.shunt_uohm = 0u;
    CHECK(!pnode_meter_info(&m, &i));
    i = matek();
    i.adcrange = 2u;
    CHECK(!pnode_meter_info(&m, &i));
    i = matek();
    i.role = RCNODE_ROLE_BALANCE;
    CHECK(!pnode_meter_info(&m, &i));
    /* Without a calibration no packet is taken. */
    uint8_t b[RCNODE_PKT_MAX];
    rcnode_pwr_win_t w;
    memset(&w, 0, sizeof w);
    const size_t n = rcnode_enc_pwr_win(&w, b, sizeof b);
    CHECK_EQ(pnode_meter_packet(&m, b, n, 1000u), 0u);
    CHECK(!pnode_meter_info(NULL, &i));
    CHECK(!pnode_meter_info(&m, NULL));
}

TEST_CASE(the_peaks_are_the_runs_and_the_nodes_extremes_under_its_tag)
{
    pnode_meter_t m;
    meter(&m);
    good(&m, 900u);
    CHECK(pnode_meter_step(&m, 1000u, true, true, true));
    CHECK_EQ(m.tag, 1u);
    CHECK(!pnode_meter_step(&m, 1001u, true, true, true));
    win(&m, 1010u, RCNODE_WF_ONLINE, 30000000, 40000000, 10000u, 9500u);
    good(&m, 1030u);
    bench_state_t b = bench_of(NULL, &m, 1031u);
    CHECK_NEAR(b.current_max, 40.0f, 1e-3f);
    CHECK_NEAR(b.voltage_min, 19.0f, 1e-3f);
    CHECK_NEAR(b.power_max, 600.0f, 0.01f);   /* 20V x 30A */
    /* The node's extremes under this tag hold a window the radio lost. */
    rcnode_pwr_run_t x;
    memset(&x, 0, sizeof x);
    x.hdr.seq = s_seq++;
    x.tag = 1u;
    x.i_max = 55000000;
    x.v_min = 9000u;
    uint8_t buf[RCNODE_PKT_MAX];
    size_t n = rcnode_enc_pwr_run(&x, buf, sizeof buf);
    CHECK_EQ(pnode_meter_packet(&m, buf, n, 1040u), RCNODE_T_PWR_RUN);
    b = bench_of(NULL, &m, 1041u);
    CHECK_NEAR(b.current_max, 55.0f, 1e-3f);
    CHECK_NEAR(b.voltage_min, 18.0f, 1e-3f);
    /* Another tag's are the last run's: not taken. */
    x.tag = 7u;
    x.i_max = 99000000;
    x.hdr.seq = s_seq++;
    n = rcnode_enc_pwr_run(&x, buf, sizeof buf);
    (void)pnode_meter_packet(&m, buf, n, 1050u);
    b = bench_of(NULL, &m, 1051u);
    CHECK_NEAR(b.current_max, 55.0f, 1e-3f);
    /* The next run starts the peaks again under tag 2. */
    (void)pnode_meter_step(&m, 1100u, false, true, true);
    CHECK(pnode_meter_step(&m, 1200u, true, true, true));
    CHECK_EQ(m.tag, 2u);
    good(&m, 1210u);
    b = bench_of(NULL, &m, 1211u);
    CHECK_NEAR(b.current_max, 12.0f, 1e-3f);
    CHECK_NEAR(b.voltage_min, 22.0f, 1e-3f);
    CHECK_NEAR(b.power_max, 222.0f, 0.01f);  /* 22.2V x 10A */
    /* The tag runs 1 to 255 and never 0. */
    m.tag = 255u;
    (void)pnode_meter_step(&m, 1300u, false, true, true);
    (void)pnode_meter_step(&m, 1310u, true, true, true);
    CHECK_EQ(m.tag, 1u);
}

TEST_CASE(the_totals_are_differences_of_the_accumulators_since_the_arm)
{
    pnode_meter_t m;
    meter(&m);
    /* Before the arm, CHARGE at -10 in 40 bits: its difference crosses
     * the two's complement wrap.  ENERGY never wraps on a bench: a fall is
     * a restart (rcnode_acc_restarted()). */
    acc(&m, 900u, 1u, RCNODE_ACC_MASK - 9u, 1000u, BOTH);
    good(&m, 950u);
    (void)pnode_meter_step(&m, 1000u, true, false, true);
    bench_state_t b = bench_of(NULL, &m, 1001u);
    CHECK((b.flags & LINK_BN_TOTALS_OK) != 0u);
    CHECK_EQ(b.charge_mah, 0.0f);
    /* +9216 CHARGE steps (-10 to 9206) and +180000 ENERGY steps. */
    acc(&m, 1400u, 1u, 9206u, 181000u, BOTH);
    CHECK_EQ(m.q_steps, 9216);
    CHECK_EQ(m.e_steps, 180000);
    CHECK_EQ(m.restarts, 0u);
    b = bench_of(NULL, &m, 1401u);
    /* 9216 x 390.625µC = 3.6C = 1mAh; 180000 x 20mJ = 3600J = 1Wh. */
    CHECK_NEAR(b.charge_mah, 1.0f, 1e-4f);
    CHECK_NEAR(b.energy_wh, 1.0f, 1e-4f);
    /* A packet without both _OK flags is skipped, not a zero. */
    acc(&m, 1900u, 1u, 0u, 0u, RCNODE_AF_CHARGE_OK);
    CHECK_EQ(m.q_steps, 9216);
    /* Current back into the pack: the charge falls, the energy rises. */
    acc(&m, 2400u, 1u, 9206u - 4608u, 181000u + 90000u, BOTH);
    b = bench_of(NULL, &m, 2401u);
    CHECK_NEAR(b.charge_mah, 0.5f, 1e-4f);
    CHECK_NEAR(b.energy_wh, 1.5f, 1e-4f);
    /* Totals older than 1500ms lose TOTALS_OK. */
    b = bench_of(NULL, &m, 2400u + PNODE_ACC_STALE_MS);
    CHECK((b.flags & LINK_BN_TOTALS_OK) != 0u);
    b = bench_of(NULL, &m, 2401u + PNODE_ACC_STALE_MS);
    CHECK_EQ(b.flags & LINK_BN_TOTALS_OK, 0u);
    CHECK_EQ(b.charge_mah, 0.0f);
}

TEST_CASE(a_restart_adds_the_new_counts_from_zero)
{
    pnode_meter_t m;
    meter(&m);
    acc(&m, 900u, 1u, 1000u, 50000u, BOTH);
    (void)pnode_meter_step(&m, 1000u, true, false, true);
    acc(&m, 1400u, 1u, 10216u, 230000u, BOTH);
    CHECK_EQ(m.q_steps, 9216);
    /* A new epoch: the part was set up again and counts from 0. */
    acc(&m, 1900u, 2u, 100u, 2000u, BOTH);
    CHECK_EQ(m.restarts, 1u);
    CHECK_EQ(m.q_steps, 9316);
    CHECK_EQ(m.e_steps, 182000);
    /* A fall in ENERGY under the same epoch: a reset the node missed. */
    acc(&m, 2400u, 2u, 50u, 1000u, BOTH);
    CHECK_EQ(m.restarts, 2u);
    CHECK_EQ(m.q_steps, 9366);
    CHECK_EQ(m.e_steps, 183000);
}

TEST_CASE(a_run_without_a_recent_packet_has_no_node_totals)
{
    pnode_meter_t m;
    meter(&m);
    (void)pnode_meter_step(&m, 1000u, true, false, true);
    acc(&m, 1400u, 1u, 100u, 100u, BOTH);
    acc(&m, 1900u, 1u, 200u, 200u, BOTH);
    bench_state_t b = bench_of(NULL, &m, 1901u);
    CHECK_EQ(b.flags & LINK_BN_TOTALS_OK, 0u);
    /* A packet 501ms before the arm is too old to start from; one 500ms
     * before starts the run. */
    CHECK_EQ(PNODE_ACC_BASE_MS, 500u);
    (void)pnode_meter_step(&m, 2000u, false, false, true);
    (void)pnode_meter_step(&m, 1900u + 501u, true, false, true);
    CHECK(!m.r.totals_ok);
    (void)pnode_meter_step(&m, 4000u, false, false, true);
    acc(&m, 4100u, 1u, 300u, 300u, BOTH);
    (void)pnode_meter_step(&m, 4100u + 500u, true, false, true);
    CHECK(m.r.totals_ok);
}

/** @brief A run on NODE with a window every 20ms from @p from to @p to. */
static void windows(pnode_meter_t *m, uint32_t from, uint32_t to)
{
    for (uint32_t t = from; t <= to; ++t) {
        if ((t - from) % 20u == 0u) {
            good(m, t);
        }
        (void)pnode_meter_step(m, t, true, true, true);
    }
}

/** @brief Steps without a window from @p from to @p to. */
static void silence(pnode_meter_t *m, uint32_t from, uint32_t to)
{
    for (uint32_t t = from; t <= to; ++t) {
        (void)pnode_meter_step(m, t, true, true, true);
    }
}

TEST_CASE(a_gap_of_299ms_and_300ms_rides_and_301ms_ends_the_run)
{
    static const uint32_t lens[] = { 299u, 300u, 301u };
    for (unsigned k = 0; k < 3u; ++k) {
        pnode_meter_t m;
        meter(&m);
        good(&m, 990u);
        windows(&m, 1000u, 2000u);
        /* The last window at 2000; the next at 2000 + len. */
        silence(&m, 2001u, 2000u + lens[k] - 1u);
        CHECK(m.gap.open);
        CHECK_EQ(m.gap.from_ms, 2000u);
        good(&m, 2000u + lens[k]);
        (void)pnode_meter_step(&m, 2000u + lens[k], true, true, true);
        CHECK_EQ(m.errors, (lens[k] > SERVO_GAP_MAX_MS) ? 1u : 0u);
        CHECK(!m.gap.open);
    }
    /* Against the clock: no window at all after 2000 ends the run at
     * 2301, once, however long the silence. */
    pnode_meter_t m;
    meter(&m);
    good(&m, 990u);
    windows(&m, 1000u, 2000u);
    silence(&m, 2001u, 2300u);
    CHECK_EQ(m.errors, 0u);
    silence(&m, 2301u, 2301u);
    CHECK_EQ(m.errors, 1u);
    silence(&m, 2302u, 5000u);
    CHECK_EQ(m.errors, 1u);
}

TEST_CASE(a_second_gap_9_9s_after_the_first_ends_the_run_and_10_1s_rides)
{
    static const uint32_t apart[] = { 9900u, 10100u };
    for (unsigned k = 0; k < 2u; ++k) {
        pnode_meter_t m;
        meter(&m);
        good(&m, 990u);
        windows(&m, 1000u, 2000u);
        silence(&m, 2001u, 2199u);          /* 200ms: rides */
        windows(&m, 2200u, 2000u + apart[k]);
        CHECK_EQ(m.errors, 0u);
        silence(&m, 2001u + apart[k], 2000u + apart[k] + 150u);
        good(&m, 2000u + apart[k] + 151u);
        (void)pnode_meter_step(&m, 2000u + apart[k] + 151u, true, true, true);
        CHECK_EQ(m.errors, (k == 0u) ? 1u : 0u);
        CHECK_EQ(m.gap.count, 2u);
    }
}

TEST_CASE(a_run_without_a_reading_ends_301ms_in_and_wired_keeps_no_rule)
{
    pnode_meter_t m;
    meter(&m);
    silence(&m, 1000u, 1300u);
    CHECK_EQ(m.errors, 0u);
    CHECK_EQ(m.gap.from_ms, 1000u);
    silence(&m, 1301u, 1301u);
    CHECK_EQ(m.errors, 1u);
    /* The next run counts again. */
    (void)pnode_meter_step(&m, 2000u, false, true, true);
    silence(&m, 3000u, 3301u);
    CHECK_EQ(m.errors, 2u);
    /* WIRED: the node's silence ends nothing. */
    pnode_meter_t w;
    meter(&w);
    for (uint32_t t = 1000u; t < 5000u; ++t) {
        (void)pnode_meter_step(&w, t, true, false, true);
    }
    CHECK_EQ(w.errors, 0u);
    /* SAFE: neither. */
    for (uint32_t t = 1000u; t < 5000u; ++t) {
        (void)pnode_meter_step(&w, t, false, true, true);
    }
    CHECK_EQ(w.errors, 0u);
    /* NODE chosen inside a run: the rule starts then, not at the arm. */
    pnode_meter_t n;
    meter(&n);
    good(&n, 990u);
    (void)pnode_meter_step(&n, 1000u, true, false, true);
    silence(&n, 5000u, 5000u);
    CHECK_EQ(n.gap.from_ms, 5000u);
    silence(&n, 5001u, 5300u);
    CHECK_EQ(n.errors, 0u);
    silence(&n, 5301u, 5301u);
    CHECK_EQ(n.errors, 1u);
}

TEST_CASE(seq_losses_count_over_win_acc_and_run_and_not_status)
{
    pnode_meter_t m;
    meter(&m);
    s_seq = 10u;
    good(&m, 1000u);
    good(&m, 1020u);
    s_seq = 15u;                            /* 3 lost */
    good(&m, 1040u);
    CHECK_EQ(m.lost, 3u);
    rcnode_status_t st;
    memset(&st, 0, sizeof st);
    st.hdr.seq = 200u;
    st.vbat = 3900u;
    st.flags = RCNODE_ST_SENSOR | RCNODE_ST_PACK;
    uint8_t b[RCNODE_PKT_MAX];
    size_t n = rcnode_enc_status(&st, b, sizeof b);
    CHECK_EQ(pnode_meter_packet(&m, b, n, 1050u), RCNODE_T_STATUS);
    CHECK(m.have_status);
    CHECK_EQ(m.status.vbat, 3900u);
    good(&m, 1060u);
    CHECK_EQ(m.lost, 3u);
    /* An accumulator packet counts in the seq: one skipped before it. */
    ++s_seq;
    acc(&m, 1065u, 1u, 0u, 0u, BOTH);
    CHECK_EQ(m.lost, 4u);
    /* A refused packet is counted, not taken. */
    b[0] = 0x7Fu;
    CHECK_EQ(pnode_meter_packet(&m, b, n, 1070u), 0u);
    CHECK_EQ(m.bad, 1u);
    /* A new link counts from its first packet. */
    pnode_meter_link(&m);
    CHECK(!m.cal_ok);
    const rcnode_info_t i = matek();
    CHECK(pnode_meter_info(&m, &i));
    s_seq = 100u;
    good(&m, 1100u);
    CHECK_EQ(m.lost, 4u);
}

TEST_CASE(benchs_own_figures_move_to_the_escs)
{
    pnode_meter_t m;
    meter(&m);
    good(&m, 1000u);
    pnode_reading_t r;
    pnode_meter_reading(&m, &r);
    bench_state_t b;
    memset(&b, 0, sizeof b);
    b.flags = LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK;
    b.voltage = 21.5f;
    b.current = 9.0f;
    CHECK(pnode_reading_bench(NULL, &r, 1001u, &b));
    float v = 0.0f;
    float i = 0.0f;
    CHECK(bench_state_esc_voltage(&b, &v));
    CHECK(bench_state_esc_current(&b, &i));
    CHECK_NEAR(v, 21.5f, 1e-4f);
    CHECK_NEAR(i, 9.0f, 1e-4f);
    CHECK(bench_state_ina_current(&b, &i));
    CHECK_NEAR(i, 10.0f, 1e-4f);
    /* The wired INA228's ESC figures stay as they were. */
    memset(&b, 0, sizeof b);
    b.flags = LINK_BN_SENSED | LINK_BN_CURRENT_OK;
    b.esc_current = 7.0f;
    b.esc_ok = LINK_SN_ESC_CURRENT_OK;
    (void)pnode_reading_bench(NULL, &r, 1001u, &b);
    CHECK(bench_state_esc_current(&b, &i));
    CHECK_NEAR(i, 7.0f, 1e-4f);
    /* NULL does nothing. */
    CHECK(!pnode_reading_bench(NULL, NULL, 0u, &b));
    CHECK(!pnode_reading_bench(NULL, &r, 0u, NULL));
    pnode_meter_reading(NULL, &r);
    CHECK(!r.have_win);
    CHECK(!pnode_meter_step(NULL, 0u, true, true, true));
    CHECK_EQ(pnode_meter_packet(NULL, NULL, 0u, 0u), 0u);
}

TEST_CASE(a_gap_is_dated_from_the_last_reading_and_not_before_the_run)
{
    /* A reading 50ms before the arm: the run's dropout starts at the arm,
     * not at the reading, and ends the run 301ms after the arm. */
    pnode_meter_t m;
    meter(&m);
    good(&m, 950u);
    silence(&m, 1000u, 1300u);
    CHECK_EQ(m.gap.from_ms, 1000u);
    CHECK_EQ(m.errors, 0u);
    silence(&m, 1301u, 1301u);
    CHECK_EQ(m.errors, 1u);
    /* 100ms without a reading at a step is no dropout; 101ms is one, and a
     * second one within 10s of the first ends the run. */
    pnode_meter_t n;
    meter(&n);
    good(&n, 990u);
    windows(&n, 1000u, 2000u);
    silence(&n, 2001u, 2199u);
    windows(&n, 2200u, 3000u);
    CHECK_EQ(n.gap.count, 1u);
    silence(&n, 3001u, 3100u);
    windows(&n, 3101u, 3501u);
    CHECK_EQ(n.gap.count, 1u);
    CHECK_EQ(n.errors, 0u);
    silence(&n, 3502u, 3602u);
    windows(&n, 3603u, 3700u);
    CHECK_EQ(n.gap.count, 2u);
    CHECK_EQ(n.errors, 1u);
}

TEST_CASE(a_servo_run_does_not_end_on_a_node_gap_and_a_motor_run_does)
{
    /* SERVO or OUTPUTS armed: ARMED and NODE, no reading for 1s, no
     * error; the throttle's run on the same silence ends at 301ms. */
    pnode_meter_t s;
    meter(&s);
    good(&s, 990u);
    for (uint32_t t = 1000u; t <= 2000u; ++t) {
        (void)pnode_meter_step(&s, t, true, true, false);
    }
    CHECK_EQ(s.errors, 0u);
    CHECK(!s.gap.open);
    pnode_meter_t m;
    meter(&m);
    good(&m, 990u);
    for (uint32_t t = 1000u; t <= 1290u; ++t) {
        (void)pnode_meter_step(&m, t, true, true, true);
    }
    CHECK_EQ(m.errors, 0u);
    for (uint32_t t = 1291u; t <= 1400u; ++t) {
        (void)pnode_meter_step(&m, t, true, true, true);
    }
    CHECK_EQ(m.errors, 1u);
}

TEST_CASE(an_arm_on_node_needs_the_node_on_and_a_reading_within_100ms)
{
    pnode_meter_t m;
    meter(&m);
    pnode_reading_t r;
    pnode_meter_reading(&m, &r);
    /* WIRED: never refused for the node. */
    CHECK(!pnode_arm_refused(false, false, NULL, 1000u));
    CHECK(!pnode_arm_refused(false, true, &r, 1000u));
    /* NODE, nothing streaming: refused, ON or OFF. */
    CHECK(pnode_arm_refused(true, true, &r, 1000u));
    CHECK(pnode_arm_refused(true, false, &r, 1000u));
    CHECK(pnode_arm_refused(true, true, NULL, 1000u));
    /* The node streams: released within 100ms of a reading. */
    good(&m, 1000u);
    pnode_meter_reading(&m, &r);
    CHECK(!pnode_arm_refused(true, true, &r, 1000u));
    CHECK(!pnode_arm_refused(true, true, &r, 1100u));
    CHECK(pnode_arm_refused(true, true, &r, 1101u));
    /* POWER NODE OFF refuses whatever the copy says. */
    CHECK(pnode_arm_refused(true, false, &r, 1000u));
    /* A window without the INA228 online is no reading. */
    pnode_meter_t n;
    meter(&n);
    win(&n, 1000u, 0u, 0, RCNODE_I_NONE, 0u, RCNODE_V_NONE);
    pnode_meter_reading(&n, &r);
    CHECK(pnode_arm_refused(true, true, &r, 1000u));
}

/** @brief One RUN packet under @p tag. */
static void run_pkt(pnode_meter_t *m, uint32_t now, uint8_t tag)
{
    rcnode_pwr_run_t x;
    memset(&x, 0, sizeof x);
    x.hdr.seq = s_seq++;
    x.tag = tag;
    x.i_max = RCNODE_I_NONE;
    x.v_min = RCNODE_V_NONE;
    uint8_t b[RCNODE_PKT_MAX];
    const size_t n = rcnode_enc_pwr_run(&x, b, sizeof b);
    CHECK_EQ(pnode_meter_packet(m, b, n, now), RCNODE_T_PWR_RUN);
}

TEST_CASE(a_node_that_started_again_inside_a_run_asks_for_run_again)
{
    pnode_meter_t m;
    meter(&m);
    good(&m, 990u);
    CHECK(pnode_meter_step(&m, 1000u, true, true, true));
    run_pkt(&m, 1100u, m.tag);
    CHECK(!m.tag_wrong);
    /* The link comes back to a node that kept the tag: nothing owed. */
    pnode_meter_link(&m);
    const rcnode_info_t i = matek();
    CHECK(pnode_meter_info(&m, &i));
    run_pkt(&m, 1200u, m.tag);
    CHECK(!m.tag_wrong);
    /* It comes back to one that started again (tag 0): owed. */
    pnode_meter_link(&m);
    CHECK(pnode_meter_info(&m, &i));
    run_pkt(&m, 1300u, 0u);
    CHECK(m.tag_wrong);
    /* Only the first RUN after the link-up decides. */
    m.tag_wrong = false;
    run_pkt(&m, 1400u, 0u);
    CHECK(!m.tag_wrong);
    /* SAFE: nothing owed, and a link-up outside a run checks nothing. */
    (void)pnode_meter_step(&m, 1500u, false, true, true);
    pnode_meter_link(&m);
    CHECK(!m.tag_check);
}

int main(void)
{
    RUN(windows_become_the_mean_of_the_windows_since_the_last_take);
    RUN(a_reading_is_stale_after_300ms_and_offline_windows_are_none);
    RUN(every_sample_clipped_reads_the_range_end_marked_clipped);
    RUN(the_scaling_is_the_drivers_and_a_setup_it_does_not_give_is_refused);
    RUN(the_peaks_are_the_runs_and_the_nodes_extremes_under_its_tag);
    RUN(the_totals_are_differences_of_the_accumulators_since_the_arm);
    RUN(a_restart_adds_the_new_counts_from_zero);
    RUN(a_run_without_a_recent_packet_has_no_node_totals);
    RUN(a_gap_of_299ms_and_300ms_rides_and_301ms_ends_the_run);
    RUN(a_second_gap_9_9s_after_the_first_ends_the_run_and_10_1s_rides);
    RUN(a_run_without_a_reading_ends_301ms_in_and_wired_keeps_no_rule);
    RUN(a_gap_is_dated_from_the_last_reading_and_not_before_the_run);
    RUN(seq_losses_count_over_win_acc_and_run_and_not_status);
    RUN(a_servo_run_does_not_end_on_a_node_gap_and_a_motor_run_does);
    RUN(an_arm_on_node_needs_the_node_on_and_a_reading_within_100ms);
    RUN(a_node_that_started_again_inside_a_run_asks_for_run_again);
    RUN(benchs_own_figures_move_to_the_escs);
    return test_summary("pnode_meter");
}
