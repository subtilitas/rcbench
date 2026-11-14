/*
 * The sensor nodes' protocol (protocols/rcnode).
 *
 * The failures this is about: a field written at the wrong offset or width,
 * a value at the end of its range that does not come back as written, a
 * wrap of seq, t_node or a 40-bit accumulator read as a jump, a window that
 * counts a clipped sample as a current, and an advertising report taken
 * for a node that is not one.  Expected bytes are written out by hand from
 * the layout tables in rcnode.h, not produced with the module's encoders.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "rcnode.h"

#include <stdint.h>
#include <string.h>

/** Whether @p n bytes of @p got equal @p want; prints the first difference. */
static int same_bytes(const uint8_t *got, const uint8_t *want, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (got[i] != want[i]) {
            printf("      byte %zu: got 0x%02X, want 0x%02X\n", i, got[i], want[i]);
            return 0;
        }
    }
    return 1;
}

/** A sample of @p i_ua and @p v_uv that is not clipped. */
static rcnode_sample_t sample(int32_t i_ua, int32_t v_uv)
{
    rcnode_sample_t s = { true, 0, i_ua, v_uv };
    return s;
}

TEST_CASE(the_uuids_carry_rcbench_and_the_id_lsb_first)
{
    static const uint8_t svc[16] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x00, 0x68, 0x63, 0x6E, 0x65, 0x62, 0x63, 0x72,
    };
    uint8_t u[16];
    rcnode_uuid(RCNODE_ID_SERVICE, u);
    CHECK(same_bytes(u, svc, 16));
    rcnode_uuid(RCNODE_ID_STREAM, u);
    CHECK_EQ(u[6], 0x01);
    CHECK_EQ(u[7], 0x01);
    rcnode_uuid(RCNODE_ID_INFO, u);
    CHECK_EQ(u[6], 0x04);
    CHECK_EQ(u[7], 0x01);
    CHECK(same_bytes(u + 8, svc + 8, 8));
    rcnode_uuid(RCNODE_ID_STATUS, NULL);          /* does nothing */
}

TEST_CASE(a_power_window_at_its_limits_has_its_bytes_and_comes_back)
{
    rcnode_pwr_win_t w;
    memset(&w, 0, sizeof w);
    w.hdr.seq    = 255u;
    w.hdr.t_node = 0xFFFFFFFFu;
    w.flags  = 0xFFu;
    w.n      = 255u;
    w.i_mean = INT32_MAX;
    w.i_max  = INT32_MIN;
    w.v_mean = 0xFFFFu;
    w.v_min  = 0x0102u;
    static const uint8_t want[20] = {
        0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0x7F, 0x00, 0x00, 0x00, 0x80,
        0xFF, 0xFF, 0x02, 0x01,
    };
    uint8_t b[24];
    CHECK_EQ(rcnode_enc_pwr_win(&w, b, sizeof b), 20);
    CHECK(same_bytes(b, want, 20));

    rcnode_pkt_t p;
    CHECK_EQ(rcnode_decode(b, 20, &p), RCNODE_OK);
    CHECK_EQ(p.hdr.type, RCNODE_T_PWR_WIN);
    CHECK_EQ(p.u.win.hdr.seq, 255);
    CHECK_EQ(p.u.win.hdr.t_node, 0xFFFFFFFFu);
    CHECK_EQ(p.u.win.n, 255);
    CHECK_EQ(p.u.win.i_mean, INT32_MAX);
    CHECK_EQ(p.u.win.i_max, INT32_MIN);
    CHECK_EQ(p.u.win.v_mean, 0xFFFF);
    CHECK_EQ(p.u.win.v_min, 0x0102);

    w.i_mean = -1;
    w.i_max  = -204800000;             /* the MATEK's -204.8A */
    CHECK_EQ(rcnode_enc_pwr_win(&w, b, sizeof b), 20);
    CHECK_EQ(rcnode_decode(b, 20, &p), RCNODE_OK);
    CHECK_EQ(p.u.win.i_mean, -1);
    CHECK_EQ(p.u.win.i_max, -204800000);
    CHECK_EQ(rcnode_enc_pwr_win(&w, b, 19), 0);
}

TEST_CASE(the_accumulators_travel_as_40_bits_at_their_ends)
{
    rcnode_pwr_acc_t a;
    memset(&a, 0, sizeof a);
    a.hdr.seq = 7u;
    a.hdr.t_node = 0x01020304u;
    a.flags  = 0x3Fu;
    a.epoch  = 255u;
    a.charge = 0xFFFFFFFFFFull;        /* -1 */
    a.energy = 0x123456789ABCull;      /* bits past 40 are cut */
    a.temp   = -32768;
    static const uint8_t want[20] = {
        0x02, 0x07, 0x04, 0x03, 0x02, 0x01, 0x3F, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xBC, 0x9A, 0x78,
        0x56, 0x34, 0x00, 0x80,
    };
    uint8_t b[20];
    CHECK_EQ(rcnode_enc_pwr_acc(&a, b, sizeof b), 20);
    CHECK(same_bytes(b, want, 20));

    rcnode_pkt_t p;
    CHECK_EQ(rcnode_decode(b, 20, &p), RCNODE_OK);
    CHECK_EQ(p.u.acc.epoch, 255);
    CHECK_EQ(p.u.acc.charge, 0xFFFFFFFFFFll);
    CHECK_EQ(rcnode_sext40(p.u.acc.charge), -1);
    CHECK_EQ(p.u.acc.energy, 0x3456789ABCll);
    CHECK_EQ(p.u.acc.temp, -32768);

    a.charge = 0x8000000000ull;        /* -2^39 */
    a.energy = 0xFFFFFFFFFFull;
    a.temp   = 32767;
    CHECK_EQ(rcnode_enc_pwr_acc(&a, b, sizeof b), 20);
    CHECK_EQ(rcnode_decode(b, 20, &p), RCNODE_OK);
    CHECK_EQ(rcnode_sext40(p.u.acc.charge), -549755813888ll);
    CHECK_EQ(p.u.acc.energy, 0xFFFFFFFFFFll);
    CHECK_EQ(p.u.acc.temp, 32767);
}

TEST_CASE(the_run_extremes_keep_their_no_value_marks)
{
    rcnode_pwr_run_t r;
    memset(&r, 0, sizeof r);
    r.tag = 0xA5u;
    r.flags = 0x07u;
    r.i_max = RCNODE_I_NONE;
    r.v_min = RCNODE_V_NONE;
    r.windows = UINT32_MAX;
    static const uint8_t want[18] = {
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA5, 0x07,
        0x00, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF,
    };
    uint8_t b[18];
    CHECK_EQ(rcnode_enc_pwr_run(&r, b, sizeof b), 18);
    CHECK(same_bytes(b, want, 18));
    rcnode_pkt_t p;
    CHECK_EQ(rcnode_decode(b, 18, &p), RCNODE_OK);
    CHECK_EQ(p.u.run.tag, 0xA5);
    CHECK_EQ(p.u.run.i_max, RCNODE_I_NONE);
    CHECK_EQ(p.u.run.v_min, RCNODE_V_NONE);
    CHECK_EQ(p.u.run.windows, UINT32_MAX);
}

TEST_CASE(a_balance_report_takes_phase_3599_and_quality_100_and_refuses_past)
{
    rcnode_bal_report_t r;
    memset(&r, 0, sizeof r);
    r.hdr.seq = 1u;
    r.flags = 0x8FFFu;                 /* every vibe flag and COUNTS */
    r.quality = 100u;
    r.revs = 255u;
    r.rpm = 65535u;
    r.spread_pm = 0x1234u;
    r.mag_1x = 412u;
    r.phase_1x = 3599u;
    r.mag_2x = 65535u;
    r.mag_blade = 120u;
    r.rms = 530u;
    static const uint8_t want[24] = {
        0x20, 0x01, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x8F,
        100,  255,  0xFF, 0xFF, 0x34, 0x12, 0x9C, 0x01,
        0x0F, 0x0E, 0xFF, 0xFF, 0x78, 0x00, 0x12, 0x02,
    };
    uint8_t b[24];
    CHECK_EQ(rcnode_enc_bal_report(&r, b, sizeof b), 24);
    CHECK(same_bytes(b, want, 24));
    rcnode_pkt_t p;
    CHECK_EQ(rcnode_decode(b, 24, &p), RCNODE_OK);
    CHECK_EQ(p.u.bal.flags, 0x8FFF);
    CHECK_EQ(p.u.bal.revs, 255);
    CHECK_EQ(p.u.bal.spread_pm, 0x1234);
    CHECK_EQ(p.u.bal.phase_1x, 3599);
    CHECK_EQ(p.u.bal.mag_2x, 65535);
    CHECK_EQ(p.u.bal.mag_blade, 120);
    CHECK_EQ(p.u.bal.rms, 530);
    CHECK_EQ(p.u.bal.rpm, 65535);

    uint8_t bad[24];
    memcpy(bad, b, 24);
    bad[16] = 0x10;                    /* phase 3600 */
    CHECK_EQ(rcnode_decode(bad, 24, &p), RCNODE_ERR_RANGE);
    memcpy(bad, b, 24);
    bad[8] = 101u;
    CHECK_EQ(rcnode_decode(bad, 24, &p), RCNODE_ERR_RANGE);
    CHECK_EQ(rcnode_enc_bal_report(&r, b, 23), 0);
}

TEST_CASE(the_encoder_wraps_the_phase_and_refuses_a_quality_over_100)
{
    rcnode_bal_report_t r;
    memset(&r, 0, sizeof r);
    r.quality = 100u;
    r.phase_1x = 3600u;                /* 360.0° is 0 */
    uint8_t b[24];
    rcnode_pkt_t p;
    CHECK_EQ(rcnode_enc_bal_report(&r, b, sizeof b), 24);
    CHECK_EQ(rcnode_decode(b, 24, &p), RCNODE_OK);
    CHECK_EQ(p.u.bal.phase_1x, 0);
    r.phase_1x = 65535u;               /* 65535 mod 3600 = 735 */
    CHECK_EQ(rcnode_enc_bal_report(&r, b, sizeof b), 24);
    CHECK_EQ(rcnode_decode(b, 24, &p), RCNODE_OK);
    CHECK_EQ(p.u.bal.phase_1x, 735);
    r.quality = 101u;
    memset(b, 0xEE, sizeof b);
    CHECK_EQ(rcnode_enc_bal_report(&r, b, sizeof b), 0);
    CHECK_EQ(b[0], 0xEE);              /* nothing written */
}

TEST_CASE(the_balance_flags_are_shared_vibes_bit_for_bit)
{
    /* vibe_demod.h at bench/vibe 295611b15, VIBE_F_VALID to
     * VIBE_F_RESTARTED. */
    CHECK_EQ(RCNODE_BF_VALID,        0x0001);
    CHECK_EQ(RCNODE_BF_INDEX_SEEN,   0x0002);
    CHECK_EQ(RCNODE_BF_INDEX_STEADY, 0x0004);
    CHECK_EQ(RCNODE_BF_INDEX_LOST,   0x0008);
    CHECK_EQ(RCNODE_BF_NO_INDEX,     0x0010);
    CHECK_EQ(RCNODE_BF_CLIPPED,      0x0020);
    CHECK_EQ(RCNODE_BF_DC_OK,        0x0040);
    CHECK_EQ(RCNODE_BF_BLADE_OK,     0x0080);
    CHECK_EQ(RCNODE_BF_QUALITY,      0x0100);
    CHECK_EQ(RCNODE_BF_EDGE_DROPPED, 0x0200);
    CHECK_EQ(RCNODE_BF_RPM_RANGE,    0x0400);
    CHECK_EQ(RCNODE_BF_RESTARTED,    0x0800);
    CHECK_EQ(RCNODE_BF_COUNTS,       0x8000);
    CHECK_EQ(RCNODE_BLADES_MAX, 12);   /* VIBE_BLADES_MAX */
}

TEST_CASE(a_status_at_its_limits_and_a_sensor_state_past_the_last)
{
    rcnode_status_t s;
    memset(&s, 0, sizeof s);
    s.hdr.seq = 200u;
    s.hdr.t_node = 0x80000000u;
    s.flags = 0xFFFFu;
    s.vbat = 4200u;
    s.fw[0] = 0u;
    s.fw[1] = 23u;
    s.fw[2] = 255u;
    s.sensor = RCNODE_SENSOR_OFFLINE;
    s.errors = 65535u;
    s.resets = 1u;
    s.dropped = 0x1234u;
    static const uint8_t want[20] = {
        0x30, 200, 0x00, 0x00, 0x00, 0x80, 0xFF, 0xFF,
        0x68, 0x10, 0, 23, 255, 6, 0xFF, 0xFF,
        0x01, 0x00, 0x34, 0x12,
    };
    uint8_t b[20];
    CHECK_EQ(rcnode_enc_status(&s, b, sizeof b), 20);
    CHECK(same_bytes(b, want, 20));
    rcnode_pkt_t p;
    CHECK_EQ(rcnode_decode(b, 20, &p), RCNODE_OK);
    CHECK_EQ(p.u.status.vbat, 4200);
    CHECK_EQ(p.u.status.fw[2], 255);
    CHECK_EQ(p.u.status.errors, 65535);
    CHECK_EQ(p.u.status.dropped, 0x1234);
    b[13] = (uint8_t)RCNODE_SENSOR_COUNT;
    CHECK_EQ(rcnode_decode(b, 20, &p), RCNODE_ERR_RANGE);
}

TEST_CASE(the_info_names_the_address_lsb_first_and_refuses_role_0)
{
    rcnode_info_t i;
    memset(&i, 0, sizeof i);
    i.proto = RCNODE_PROTO;
    i.role = RCNODE_ROLE_POWER;
    i.board = RCNODE_BOARD_C6_ZERO;
    i.addr_type = 0u;
    static const uint8_t addr[6] = { 0x56, 0x34, 0x12, 0xA1, 0xDF, 0x7C };
    memcpy(i.addr, addr, 6);
    i.i228_addr = 0x45u;
    i.adcrange = 1u;
    i.shunt_uohm = 200u;
    static const uint8_t want[20] = {
        0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
        0x01, 0x00, 0x56, 0x34, 0x12, 0xA1, 0xDF, 0x7C,
        0x45, 0x01, 0xC8, 0x00,
    };
    uint8_t b[20];
    CHECK_EQ(rcnode_enc_info(&i, b, sizeof b), 20);
    CHECK(same_bytes(b, want, 20));
    rcnode_pkt_t p;
    CHECK_EQ(rcnode_decode(b, 20, &p), RCNODE_OK);
    CHECK_EQ(p.u.info.shunt_uohm, 200);
    CHECK_EQ(p.u.info.addr[5], 0x7C);
    b[7] = 0u;
    CHECK_EQ(rcnode_decode(b, 20, &p), RCNODE_ERR_RANGE);
    b[7] = 1u;
    b[6] = 0u;
    CHECK_EQ(rcnode_decode(b, 20, &p), RCNODE_ERR_RANGE);
}

TEST_CASE(every_type_refuses_another_length_and_an_unknown_type_is_refused)
{
    static const uint8_t types[] = {
        RCNODE_T_PWR_WIN, RCNODE_T_PWR_ACC, RCNODE_T_PWR_RUN,
        RCNODE_T_BAL_REPORT, RCNODE_T_STATUS, RCNODE_T_INFO,
    };
    static const size_t lens[] = { 20, 20, 18, 24, 20, 20 };
    uint8_t b[RCNODE_PKT_MAX + 1];
    rcnode_pkt_t p;
    for (size_t k = 0; k < sizeof types; ++k) {
        memset(b, 0, sizeof b);
        b[0] = types[k];
        b[6] = 1u;                     /* info: proto 1 */
        b[7] = 1u;                     /* info: role 1  */
        CHECK_EQ(rcnode_len(types[k]), lens[k]);
        CHECK_EQ(rcnode_decode(b, lens[k] - 1u, &p), RCNODE_ERR_LEN);
        CHECK_EQ(rcnode_decode(b, lens[k] + 1u, &p), RCNODE_ERR_LEN);
        CHECK_EQ(rcnode_decode(b, lens[k], &p), RCNODE_OK);
        CHECK_EQ(p.hdr.type, types[k]);
    }
    memset(b, 0, sizeof b);
    b[0] = 0x7Eu;
    CHECK_EQ(rcnode_len(0x7Eu), 0);
    CHECK_EQ(rcnode_decode(b, 20, &p), RCNODE_ERR_TYPE);
    CHECK_EQ(rcnode_decode(b, 0, &p), RCNODE_ERR_LEN);
    CHECK_EQ(rcnode_decode(NULL, 20, &p), RCNODE_ERR_ARG);
    CHECK_EQ(rcnode_decode(b, 20, NULL), RCNODE_ERR_ARG);
}

TEST_CASE(a_refused_packet_leaves_the_output_as_it_was)
{
    rcnode_pkt_t p;
    memset(&p, 0xA5, sizeof p);
    rcnode_pkt_t before = p;
    uint8_t b[24];
    memset(b, 0, sizeof b);
    b[0] = RCNODE_T_BAL_REPORT;
    b[8] = 200u;                       /* quality 200 */
    CHECK_EQ(rcnode_decode(b, 24, &p), RCNODE_ERR_RANGE);
    CHECK(memcmp(&p, &before, sizeof p) == 0);
    CHECK_EQ(rcnode_decode(b, 23, &p), RCNODE_ERR_LEN);
    CHECK(memcmp(&p, &before, sizeof p) == 0);
}

TEST_CASE(every_control_write_has_its_bytes_and_its_limits)
{
    uint8_t b[4];
    rcnode_ctl_t c;
    memset(&c, 0, sizeof c);

    c.op = RCNODE_OP_RUN;
    c.tag = 0xFFu;
    CHECK_EQ(rcnode_enc_ctl(&c, b, sizeof b), 2);
    CHECK_EQ(b[0], 0x01);
    CHECK_EQ(b[1], 0xFF);
    c.tag = 0u;                        /* 0 is "no RUN yet" */
    CHECK_EQ(rcnode_enc_ctl(&c, b, sizeof b), 0);
    c.op = RCNODE_OP_SLEEP;
    CHECK_EQ(rcnode_enc_ctl(&c, b, sizeof b), 1);
    CHECK_EQ(b[0], 0x02);
    c.op = RCNODE_OP_OFF;
    CHECK_EQ(rcnode_enc_ctl(&c, b, sizeof b), 1);
    CHECK_EQ(b[0], 0x03);
    c.op = RCNODE_OP_BLADES;
    c.blades = 12u;
    CHECK_EQ(rcnode_enc_ctl(&c, b, sizeof b), 2);
    CHECK_EQ(b[1], 12);
    c.blades = 13u;
    CHECK_EQ(rcnode_enc_ctl(&c, b, sizeof b), 0);
    c.blades = 0u;
    CHECK_EQ(rcnode_enc_ctl(&c, b, sizeof b), 0);
    c.op = RCNODE_OP_RPM;
    c.rpm = 60000u;
    CHECK_EQ(rcnode_enc_ctl(&c, b, sizeof b), 3);
    CHECK_EQ(b[1], 0x60);
    CHECK_EQ(b[2], 0xEA);
    c.rpm = 60001u;
    CHECK_EQ(rcnode_enc_ctl(&c, b, sizeof b), 0);
    c.rpm = 0u;
    CHECK_EQ(rcnode_enc_ctl(&c, b, 2), 0);     /* too small */
    c.op = 0x09u;
    CHECK_EQ(rcnode_enc_ctl(&c, b, sizeof b), 0);

    rcnode_ctl_t d;
    static const uint8_t run[2] = { 0x01, 0x01 };
    static const uint8_t run_0[2] = { 0x01, 0x00 };
    CHECK_EQ(rcnode_dec_ctl(run, 2, &d), RCNODE_OK);
    CHECK_EQ(d.op, RCNODE_OP_RUN);
    CHECK_EQ(d.tag, 1);
    CHECK_EQ(rcnode_dec_ctl(run_0, 2, &d), RCNODE_ERR_RANGE);
    CHECK_EQ(rcnode_dec_ctl(run, 1, &d), RCNODE_ERR_LEN);
    CHECK_EQ(rcnode_dec_ctl(run, 3, &d), RCNODE_ERR_LEN);
    static const uint8_t blades_1[2] = { 0x04, 1 };
    static const uint8_t blades_0[2] = { 0x04, 0 };
    static const uint8_t blades_33[2] = { 0x04, 13 };
    CHECK_EQ(rcnode_dec_ctl(blades_1, 2, &d), RCNODE_OK);
    CHECK_EQ(d.blades, 1);
    CHECK_EQ(rcnode_dec_ctl(blades_0, 2, &d), RCNODE_ERR_RANGE);
    CHECK_EQ(rcnode_dec_ctl(blades_33, 2, &d), RCNODE_ERR_RANGE);
    static const uint8_t rpm_max[3] = { 0x05, 0x60, 0xEA };
    static const uint8_t rpm_over[3] = { 0x05, 0x61, 0xEA };
    CHECK_EQ(rcnode_dec_ctl(rpm_max, 3, &d), RCNODE_OK);
    CHECK_EQ(d.rpm, 60000);
    CHECK_EQ(rcnode_dec_ctl(rpm_over, 3, &d), RCNODE_ERR_RANGE);
    CHECK_EQ(d.rpm, 60000);                    /* left as it was */
    static const uint8_t unknown[1] = { 0x00 };
    CHECK_EQ(rcnode_dec_ctl(unknown, 1, &d), RCNODE_ERR_TYPE);
    CHECK_EQ(rcnode_dec_ctl(unknown, 0, &d), RCNODE_ERR_LEN);
    CHECK_EQ(rcnode_dec_ctl(NULL, 1, &d), RCNODE_ERR_ARG);
}

TEST_CASE(the_power_role_takes_run_sleep_off_and_not_the_balance_writes)
{
    CHECK(rcnode_ctl_for_role(RCNODE_ROLE_POWER, RCNODE_OP_RUN));
    CHECK(rcnode_ctl_for_role(RCNODE_ROLE_POWER, RCNODE_OP_SLEEP));
    CHECK(rcnode_ctl_for_role(RCNODE_ROLE_POWER, RCNODE_OP_OFF));
    CHECK(!rcnode_ctl_for_role(RCNODE_ROLE_POWER, RCNODE_OP_BLADES));
    CHECK(!rcnode_ctl_for_role(RCNODE_ROLE_POWER, RCNODE_OP_RPM));
    CHECK(rcnode_ctl_for_role(RCNODE_ROLE_BALANCE, RCNODE_OP_BLADES));
    CHECK(rcnode_ctl_for_role(RCNODE_ROLE_BALANCE, RCNODE_OP_RPM));
    CHECK(rcnode_ctl_for_role(RCNODE_ROLE_BALANCE, RCNODE_OP_RUN));
    CHECK(!rcnode_ctl_for_role(0u, RCNODE_OP_RUN));
    CHECK(!rcnode_ctl_for_role(RCNODE_ROLE_BALANCE, 0x06u));
}

TEST_CASE(seq_counts_losses_across_its_wrap_at_256)
{
    CHECK_EQ(rcnode_seq_lost(5u, 6u), 0);
    CHECK_EQ(rcnode_seq_lost(255u, 0u), 0);
    CHECK_EQ(rcnode_seq_lost(254u, 1u), 2);
    CHECK_EQ(rcnode_seq_lost(17u, 20u), 2);
    CHECK_EQ(rcnode_seq_lost(10u, 10u), 255);    /* a repeat reads as a full wrap */
}

TEST_CASE(t_node_differences_hold_across_the_wrap_at_2_32us)
{
    CHECK_EQ(rcnode_t_diff(5u, 0xFFFFFFFBu), 10);
    CHECK_EQ(rcnode_t_diff(0xFFFFFFFBu, 5u), -10);
    CHECK_EQ(rcnode_t_diff(20000u, 0u), 20000);
    CHECK_EQ(rcnode_t_diff(0u, 0xFFFFB1E0u), 20000);  /* 2^32 - 20000 */
    CHECK_EQ(rcnode_t_diff(0x7FFFFFFFu, 0u), INT32_MAX);
    CHECK_EQ(rcnode_t_diff(0x80000000u, 0u), INT32_MIN);
    CHECK_EQ(rcnode_t_diff(0u, 0u), 0);
}

TEST_CASE(the_40_bit_accumulators_sign_extend_and_wrap)
{
    CHECK_EQ(rcnode_sext40(0x7FFFFFFFFFull), 549755813887ll);
    CHECK_EQ(rcnode_sext40(0x8000000000ull), -549755813888ll);
    CHECK_EQ(rcnode_sext40(0xFFFFFFFFFFull), -1);
    CHECK_EQ(rcnode_sext40(0x0000000000ull), 0);
    CHECK_EQ(rcnode_sext40(0xFF00000000000001ull), 1);  /* bits past 40 ignored */

    CHECK_EQ(rcnode_energy_delta(0xFFFFFFFFF0ull, 0x10ull), 0x20);
    CHECK_EQ(rcnode_energy_delta(0x10ull, 0x10ull), 0);
    CHECK_EQ(rcnode_energy_delta(0ull, 0xFFFFFFFFFFull), 0xFFFFFFFFFFll);
    CHECK_EQ(rcnode_energy_delta(1ull, 0ull), 0xFFFFFFFFFFll);

    CHECK_EQ(rcnode_charge_delta(5ull, 0xFFFFFFFFFEull), -7);   /* 5 to -2 */
    CHECK_EQ(rcnode_charge_delta(0xFFFFFFFFFEull, 3ull), 5);    /* -2 to 3 */
    CHECK_EQ(rcnode_charge_delta(0x7FFFFFFFFFull, 0x8000000000ull), 1);
    CHECK_EQ(rcnode_charge_delta(0x8000000000ull, 0x7FFFFFFFFFull), -1);
    CHECK_EQ(rcnode_charge_delta(0ull, 0x7FFFFFFFFFull), 549755813887ll);
}

TEST_CASE(voltage_fields_round_to_2mv_and_clamp)
{
    CHECK_EQ(rcnode_v_field(0), 0);
    CHECK_EQ(rcnode_v_field(-5000), 0);
    CHECK_EQ(rcnode_v_field(999), 0);
    CHECK_EQ(rcnode_v_field(1000), 1);
    CHECK_EQ(rcnode_v_field(2000), 1);
    CHECK_EQ(rcnode_v_field(5000000), 2500);
    CHECK_EQ(rcnode_v_field(85000000), 42500);
    CHECK_EQ(rcnode_v_field(131069000), 65535);
    CHECK_EQ(rcnode_v_field(131071000), 65535);
    CHECK_EQ(rcnode_v_field(INT32_MAX), 65535);
}

TEST_CASE(a_window_of_18_samples_closes_on_the_next_windows_sample)
{
    rcnode_win_t w;
    rcnode_win_init(&w);
    rcnode_pwr_win_t out;
    unsigned closed = 0u;
    for (unsigned k = 0; k < 18u; ++k) {
        /* 1000mA to 18000mA, 22.200V down to 22.166V */
        rcnode_sample_t s = sample((int32_t)(1000000u * (k + 1u)),
                                   (int32_t)(22200000u - 2000u * k));
        closed += rcnode_win_add(&w, 1130u * k, &s, RCNODE_WF_ONLINE, &out) ? 1u : 0u;
    }
    CHECK_EQ(closed, 0);
    rcnode_sample_t s = sample(0, 0);
    CHECK(rcnode_win_add(&w, 20000u, &s, RCNODE_WF_ONLINE | 0x80u, &out));
    CHECK_EQ(out.hdr.type, RCNODE_T_PWR_WIN);
    CHECK_EQ(out.hdr.t_node, 20000u);
    CHECK_EQ(out.n, 18);
    CHECK_EQ(out.i_mean, 9500000);             /* (1A + 18A) / 2 */
    CHECK_EQ(out.i_max, 18000000);
    CHECK_EQ(out.v_mean, 11092);               /* 11100 to 11083 -> 11091.5 */
    CHECK_EQ(out.v_min, 11083);
    CHECK_EQ(out.flags, RCNODE_WF_ONLINE | RCNODE_WF_PACK);   /* 0x80 ignored */
}

TEST_CASE(a_negative_mean_rounds_away_from_zero)
{
    rcnode_win_t w;
    rcnode_win_init(&w);
    rcnode_pwr_win_t out;
    rcnode_sample_t a = sample(-1, 0);
    rcnode_sample_t b = sample(-2, 0);
    CHECK(!rcnode_win_add(&w, 0u, &a, 0u, &out));
    CHECK(!rcnode_win_add(&w, 1u, &b, 0u, &out));
    CHECK(rcnode_win_add(&w, 20000u, &a, 0u, &out));
    CHECK_EQ(out.i_mean, -2);                  /* -1.5 */
    CHECK_EQ(out.i_max, -1);
    CHECK_EQ(out.flags & RCNODE_WF_PACK, 0);
}

TEST_CASE(a_clipped_sample_is_flagged_and_is_never_a_value)
{
    rcnode_win_t w;
    rcnode_win_init(&w);
    rcnode_pwr_win_t out;
    rcnode_sample_t hi = { true, 1, 999999999, 12000000 };
    rcnode_sample_t lo = { true, -1, -999999999, 12000000 };
    rcnode_sample_t ok = sample(5000, 12000000);
    rcnode_win_add(&w, 0u, &hi, 0u, &out);
    rcnode_win_add(&w, 10u, &ok, 0u, &out);
    rcnode_win_add(&w, 20u, &lo, 0u, &out);
    CHECK(rcnode_win_add(&w, 20000u, &ok, 0u, &out));
    CHECK_EQ(out.n, 3);
    CHECK_EQ(out.i_mean, 5000);
    CHECK_EQ(out.i_max, 5000);
    CHECK_EQ(out.flags & (RCNODE_WF_CLIP_HIGH | RCNODE_WF_CLIP_LOW),
             RCNODE_WF_CLIP_HIGH | RCNODE_WF_CLIP_LOW);

    /* every sample clipped: no current value, the voltage stands */
    rcnode_win_init(&w);
    rcnode_win_add(&w, 0u, &hi, 0u, &out);
    CHECK(rcnode_win_add(&w, 20000u, &hi, 0u, &out));
    CHECK_EQ(out.n, 1);
    CHECK_EQ(out.i_mean, 0);
    CHECK_EQ(out.i_max, RCNODE_I_NONE);
    CHECK_EQ(out.v_mean, 6000);
}

TEST_CASE(a_window_of_failed_reads_has_no_values_and_says_so)
{
    rcnode_win_t w;
    rcnode_win_init(&w);
    rcnode_pwr_win_t out;
    rcnode_sample_t bad = { false, 0, 7, 7 };
    rcnode_win_add(&w, 0u, &bad, 0u, &out);
    rcnode_win_add(&w, 1130u, &bad, 0u, &out);
    rcnode_sample_t s = sample(1, 1);
    CHECK(rcnode_win_add(&w, 20000u, &s, 0u, &out));
    CHECK_EQ(out.n, 0);
    CHECK_EQ(out.flags, RCNODE_WF_READ_FAIL);
    CHECK_EQ(out.i_mean, 0);
    CHECK_EQ(out.i_max, RCNODE_I_NONE);
    CHECK_EQ(out.v_mean, 0);
    CHECK_EQ(out.v_min, RCNODE_V_NONE);
}

TEST_CASE(windows_without_a_sample_between_two_are_not_sent)
{
    rcnode_win_t w;
    rcnode_win_init(&w);
    rcnode_pwr_win_t out;
    rcnode_sample_t s = sample(1000, 10000000);
    rcnode_win_add(&w, 19999u, &s, 0u, &out);
    CHECK(rcnode_win_add(&w, 5u * 20000u + 3u, &s, 0u, &out));
    CHECK_EQ(out.hdr.t_node, 20000u);
    CHECK_EQ(out.n, 1);
    CHECK(rcnode_win_add(&w, 6u * 20000u, &s, 0u, &out));
    CHECK_EQ(out.hdr.t_node, 6u * 20000u);
}

TEST_CASE(the_window_that_ends_past_2_32us_carries_the_wrapped_t_node)
{
    /* Window 214748 runs from 4294960000us to 4294980000us; its end cut to
     * 32 bits is 12704. */
    rcnode_win_t w;
    rcnode_win_init(&w);
    rcnode_pwr_win_t out;
    rcnode_sample_t s = sample(1000, 10000000);
    const uint64_t start = 4294960000ull;
    rcnode_win_add(&w, start - 1u, &s, 0u, &out);
    CHECK(rcnode_win_add(&w, start + 5u, &s, 0u, &out));
    const uint32_t before = out.hdr.t_node;
    CHECK_EQ(before, 4294960000u);
    rcnode_win_add(&w, 0x100000000ull + 1u, &s, 0u, &out);   /* past the wrap */
    CHECK(rcnode_win_add(&w, start + 20000u, &s, 0u, &out));
    CHECK_EQ(out.hdr.t_node, 12704u);
    CHECK_EQ(out.n, 2);
    CHECK_EQ(rcnode_t_diff(out.hdr.t_node, before), 20000);
}

TEST_CASE(the_run_extremes_restart_at_the_marker_and_keep_every_peak)
{
    rcnode_run_t r;
    rcnode_run_mark(&r, 3u);
    CHECK_EQ(r.tag, 3);
    CHECK_EQ(r.i_max, RCNODE_I_NONE);
    CHECK_EQ(r.v_min, RCNODE_V_NONE);

    rcnode_pwr_win_t w;
    memset(&w, 0, sizeof w);
    w.n = 17u;
    w.i_max = 14300000;
    w.v_min = 11000u;
    rcnode_run_take(&r, &w);
    w.i_max = 9000000;
    w.v_min = 10500u;
    w.flags = RCNODE_WF_CLIP_LOW;
    rcnode_run_take(&r, &w);
    CHECK_EQ(r.i_max, 14300000);
    CHECK_EQ(r.v_min, 10500);
    CHECK_EQ(r.flags, RCNODE_RF_CLIP);

    memset(&w, 0, sizeof w);           /* a window without a sample */
    w.i_max = RCNODE_I_NONE;
    w.v_min = RCNODE_V_NONE;
    w.flags = RCNODE_WF_READ_FAIL;
    rcnode_run_take(&r, &w);
    CHECK_EQ(r.windows, 3);
    CHECK_EQ(r.v_min, 10500);
    CHECK_EQ(r.flags, RCNODE_RF_CLIP | RCNODE_RF_READ_FAIL | RCNODE_RF_EMPTY);

    w.n = 1u;                          /* every sample clipped: voltage only */
    w.v_min = 9000u;
    w.flags = RCNODE_WF_CLIP_HIGH;
    rcnode_run_take(&r, &w);
    CHECK_EQ(r.i_max, 14300000);
    CHECK_EQ(r.v_min, 9000);

    rcnode_run_mark(&r, 4u);
    CHECK_EQ(r.windows, 0);
    CHECK_EQ(r.flags, 0);
    CHECK_EQ(r.i_max, RCNODE_I_NONE);
    r.windows = UINT32_MAX;
    rcnode_run_take(&r, &w);
    CHECK_EQ(r.windows, UINT32_MAX);   /* holds at the top */
}

TEST_CASE(a_window_closed_with_the_marker_stays_out_of_the_new_run)
{
    rcnode_run_t r;
    rcnode_run_mark(&r, 1u);
    rcnode_pwr_win_t w;
    memset(&w, 0, sizeof w);
    w.n = 18u;
    w.i_max = 90000000;                /* 90A before the marker */
    w.v_min = 9000u;
    rcnode_run_step(&r, &w, 2);        /* closed in the step that took RUN 2 */
    CHECK_EQ(r.tag, 2);
    CHECK_EQ(r.windows, 0);
    CHECK_EQ(r.i_max, RCNODE_I_NONE);
    CHECK_EQ(r.v_min, RCNODE_V_NONE);
    w.i_max = 5000000;
    w.v_min = 11000u;
    rcnode_run_step(&r, &w, -1);       /* the next window is the run's */
    CHECK_EQ(r.windows, 1);
    CHECK_EQ(r.i_max, 5000000);
    CHECK_EQ(r.v_min, 11000);
    rcnode_run_step(&r, NULL, 0);      /* nothing closed, no marker */
    CHECK_EQ(r.windows, 1);
    CHECK_EQ(r.tag, 2);
    rcnode_run_step(NULL, &w, 3);
}

/** An accumulator packet of @p epoch and @p energy, both registers read. */
static rcnode_pwr_acc_t acc(uint8_t epoch, uint64_t energy, uint64_t charge)
{
    rcnode_pwr_acc_t a;
    memset(&a, 0, sizeof a);
    a.flags = RCNODE_AF_CHARGE_OK | RCNODE_AF_ENERGY_OK | RCNODE_AF_ONLINE;
    a.epoch = epoch;
    a.energy = energy;
    a.charge = charge;
    return a;
}

TEST_CASE(a_fall_in_energy_under_the_same_epoch_is_a_restart_not_22gj)
{
    /* A part reset the node had not caught: 0 under the old epoch.  Taken
     * as a difference it would be 2^40 - 10^6 steps of 20mJ, about 22GJ. */
    rcnode_pwr_acc_t prev = acc(3u, 1000000u, 500000u);
    rcnode_pwr_acc_t now = acc(3u, 0u, 0u);
    CHECK_EQ(rcnode_energy_delta(prev.energy, now.energy), 1099510627776ll);
    CHECK(rcnode_acc_restarted(&prev, &now));

    now = acc(3u, 1000001u, 499000u);  /* charge falls: current back into the pack */
    CHECK(!rcnode_acc_restarted(&prev, &now));
    now = acc(3u, 1000000u, 500000u);  /* no change */
    CHECK(!rcnode_acc_restarted(&prev, &now));
    now = acc(4u, 2000000u, 600000u);  /* a set-up the node saw */
    CHECK(rcnode_acc_restarted(&prev, &now));
    now = acc(3u, 0u, 0u);
    now.flags = RCNODE_AF_ONLINE;      /* ENERGY not read: nothing to judge */
    CHECK(!rcnode_acc_restarted(&prev, &now));
    CHECK(!rcnode_acc_restarted(NULL, &now));
}

TEST_CASE(a_notification_fits_the_mtu_less_3_bytes)
{
    CHECK(rcnode_fits_mtu(23u, 20u));
    CHECK(!rcnode_fits_mtu(23u, RCNODE_BAL_REPORT_LEN));
    CHECK(rcnode_fits_mtu(27u, RCNODE_BAL_REPORT_LEN));
    CHECK(!rcnode_fits_mtu(26u, RCNODE_BAL_REPORT_LEN));
    CHECK(!rcnode_fits_mtu(0u, 1u));   /* no link */
    CHECK(!rcnode_fits_mtu(2u, 0u));
    CHECK(rcnode_fits_mtu(3u, 0u));
}

TEST_CASE(the_name_takes_the_two_address_bytes_printed_last)
{
    static const uint8_t addr[6] = { 0x56, 0x34, 0x12, 0xA1, 0xDF, 0x7C };
    static const uint8_t addr2[6] = { 0x0F, 0xA0, 0, 0, 0, 0 };
    char name[RCNODE_NAME_MAX];
    CHECK(rcnode_name(RCNODE_ROLE_POWER, addr, name));
    CHECK_STR_EQ(name, "rcbench-pwr-3456");
    CHECK(rcnode_name(RCNODE_ROLE_BALANCE, addr2, name));
    CHECK_STR_EQ(name, "rcbench-bal-A00F");
    CHECK(!rcnode_name(3u, addr, name));
    CHECK(!rcnode_name(RCNODE_ROLE_POWER, NULL, name));
}

TEST_CASE(the_advertising_has_its_bytes_and_matches_its_role)
{
    static const uint8_t addr[6] = { 0x56, 0x34, 0x12, 0xA1, 0xDF, 0x7C };
    static const uint8_t want[29] = {
        0x02, 0x01, 0x06,
        0x11, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x68, 0x63, 0x6E, 0x65, 0x62, 0x63, 0x72,
        0x07, 0xFF, 0xFF, 0xFF, 'r', 'c', 0x01, 0x01,
    };
    uint8_t ad[31];
    CHECK_EQ(rcnode_adv_build(RCNODE_ROLE_POWER, addr, ad, sizeof ad), 29);
    CHECK(same_bytes(ad, want, 29));
    CHECK_EQ(rcnode_adv_build(RCNODE_ROLE_POWER, addr, ad, 28), 0);
    CHECK_EQ(rcnode_adv_build(0u, addr, ad, sizeof ad), 0);

    uint8_t role = 99u;
    CHECK(rcnode_adv_match(want, 29, &role));
    CHECK_EQ(role, RCNODE_ROLE_POWER);

    CHECK_EQ(rcnode_adv_build(RCNODE_ROLE_BALANCE, addr, ad, sizeof ad), 29);
    CHECK(rcnode_adv_match(ad, 29, &role));
    CHECK_EQ(role, RCNODE_ROLE_BALANCE);

    /* the service UUID alone, as a report cut after it, matches with no
     * role, in a complete list and in an incomplete one */
    CHECK(rcnode_adv_match(want, 21, &role));
    CHECK_EQ(role, 0);
    uint8_t some[18];
    memcpy(some, want + 3, sizeof some);
    some[1] = 0x06u;
    role = 99u;
    CHECK(rcnode_adv_match(some, sizeof some, &role));
    CHECK_EQ(role, 0);

    static const uint8_t want_rsp[18] = {
        0x11, 0x09, 'r', 'c', 'b', 'e', 'n', 'c', 'h', '-', 'p', 'w', 'r',
        '-', '3', '4', '5', '6',
    };
    uint8_t rsp[18];
    CHECK_EQ(rcnode_rsp_build(RCNODE_ROLE_POWER, addr, rsp, sizeof rsp), 18);
    CHECK(same_bytes(rsp, want_rsp, 18));
    CHECK(rcnode_adv_match(rsp, 18, &role));
    CHECK_EQ(role, RCNODE_ROLE_POWER);
    CHECK_EQ(rcnode_rsp_build(RCNODE_ROLE_POWER, addr, rsp, 17), 0);
    CHECK_EQ(rcnode_rsp_build(0u, addr, rsp, sizeof rsp), 0);
    CHECK_EQ(rcnode_adv_build(RCNODE_ROLE_POWER, NULL, ad, sizeof ad), 0);
}

TEST_CASE(a_name_alone_matches_and_other_devices_do_not)
{
    static const uint8_t name_only[] = {
        0x11, 0x09, 'r', 'c', 'b', 'e', 'n', 'c', 'h', '-', 'b', 'a', 'l',
        '-', '0', '0', '0', '1',
    };
    uint8_t role = 0u;
    CHECK(rcnode_adv_match(name_only, sizeof name_only, &role));
    CHECK_EQ(role, RCNODE_ROLE_BALANCE);

    static const uint8_t other_name[] = {
        0x09, 0x09, 'r', 'c', 'b', 'e', 'n', 'c', 'h', '-',
    };
    CHECK(!rcnode_adv_match(other_name, sizeof other_name, &role));
    static const uint8_t wt901[] = {
        0x02, 0x01, 0x06, 0x08, 0x09, 'W', 'T', '9', '0', '1', 'B', 'L',
    };
    CHECK(!rcnode_adv_match(wt901, sizeof wt901, &role));

    /* another company's data with the same tail is not a node's */
    static const uint8_t other_maker[] = {
        0x07, 0xFF, 0xE5, 0x02, 'r', 'c', 0x01, 0x01,
    };
    CHECK(!rcnode_adv_match(other_maker, sizeof other_maker, &role));
    /* a role byte that is not a role is not a node's */
    static const uint8_t bad_role[] = {
        0x07, 0xFF, 0xFF, 0xFF, 'r', 'c', 0x01, 0x09,
    };
    CHECK(!rcnode_adv_match(bad_role, sizeof bad_role, &role));
    /* the manufacturer data alone names a role for a scan's list; a name
     * or the service alone names none */
    CHECK_EQ(rcnode_adv_maker_role(name_only, sizeof name_only), 0u);
    CHECK_EQ(rcnode_adv_maker_role(bad_role, sizeof bad_role), 0u);
    CHECK_EQ(rcnode_adv_maker_role(other_maker, sizeof other_maker), 0u);
    static const uint8_t bal_maker[] = {
        0x07, 0xFF, 0xFF, 0xFF, 'r', 'c', 0x01, 0x02,
    };
    CHECK_EQ(rcnode_adv_maker_role(bal_maker, sizeof bal_maker),
             RCNODE_ROLE_BALANCE);
    CHECK_EQ(rcnode_adv_maker_role(bal_maker, sizeof bal_maker - 1u), 0u);
    CHECK_EQ(rcnode_adv_maker_role(NULL, 0u), 0u);

    /* the manufacturer data wins over a name of the other role */
    static const uint8_t both[] = {
        0x07, 0xFF, 0xFF, 0xFF, 'r', 'c', 0x01, 0x01,
        0x11, 0x09, 'r', 'c', 'b', 'e', 'n', 'c', 'h', '-', 'b', 'a', 'l',
        '-', '0', '0', '0', '1',
    };
    CHECK(rcnode_adv_match(both, sizeof both, &role));
    CHECK_EQ(role, RCNODE_ROLE_POWER);
}

TEST_CASE(a_name_gives_its_role)
{
    CHECK_EQ(rcnode_name_role("rcbench-pwr-3456"), RCNODE_ROLE_POWER);
    CHECK_EQ(rcnode_name_role("rcbench-bal-A00F"), RCNODE_ROLE_BALANCE);
    CHECK_EQ(rcnode_name_role("rcbench-pwr"), 0);
    CHECK_EQ(rcnode_name_role("DIAL-3A10"), 0);
    CHECK_EQ(rcnode_name_role(""), 0);
    CHECK_EQ(rcnode_name_role(NULL), 0);
}

TEST_CASE(a_length_past_the_end_refuses_the_report_and_padding_ends_it)
{
    static const uint8_t cut[] = {
        0x07, 0xFF, 0xFF, 0xFF, 'r', 'c', 0x01,
    };
    uint8_t role = 0u;
    CHECK(!rcnode_adv_match(cut, sizeof cut, &role));
    static const uint8_t padded[] = {
        0x07, 0xFF, 0xFF, 0xFF, 'r', 'c', 0x01, 0x02, 0x00, 0x00, 0x00,
    };
    CHECK(rcnode_adv_match(padded, sizeof padded, &role));
    CHECK_EQ(role, RCNODE_ROLE_BALANCE);
    /* the same structure with its last byte outside the length given */
    CHECK(!rcnode_adv_match(padded, 7, &role));
    CHECK(!rcnode_adv_match(NULL, 3, &role));
    CHECK(!rcnode_adv_match(padded, sizeof padded, NULL));
    CHECK(!rcnode_adv_match(padded, 0, &role));
}

TEST_CASE(a_null_argument_does_nothing)
{
    uint8_t b[RCNODE_PKT_MAX];
    CHECK_EQ(rcnode_enc_pwr_win(NULL, b, sizeof b), 0);
    CHECK_EQ(rcnode_enc_pwr_acc(NULL, b, sizeof b), 0);
    CHECK_EQ(rcnode_enc_pwr_run(NULL, b, sizeof b), 0);
    CHECK_EQ(rcnode_enc_bal_report(NULL, b, sizeof b), 0);
    CHECK_EQ(rcnode_enc_status(NULL, b, sizeof b), 0);
    CHECK_EQ(rcnode_enc_info(NULL, b, sizeof b), 0);
    CHECK_EQ(rcnode_enc_ctl(NULL, b, sizeof b), 0);
    CHECK_EQ(rcnode_rsp_build(RCNODE_ROLE_POWER, NULL, b, 18), 0);
    CHECK_EQ(rcnode_rsp_build(RCNODE_ROLE_POWER, b, NULL, 18), 0);
    rcnode_status_t s;
    memset(&s, 0, sizeof s);
    CHECK_EQ(rcnode_enc_status(&s, NULL, 20), 0);
    CHECK_EQ(rcnode_enc_status(&s, b, 19), 0);
    rcnode_win_init(NULL);
    rcnode_run_mark(NULL, 1u);
    rcnode_run_take(NULL, NULL);
    rcnode_win_t w;
    rcnode_win_init(&w);
    rcnode_pwr_win_t out;
    CHECK(!rcnode_win_add(&w, 0u, NULL, 0u, &out));
    CHECK(!rcnode_win_add(NULL, 0u, NULL, 0u, &out));
}

int main(void)
{
    RUN(the_uuids_carry_rcbench_and_the_id_lsb_first);
    RUN(a_power_window_at_its_limits_has_its_bytes_and_comes_back);
    RUN(the_accumulators_travel_as_40_bits_at_their_ends);
    RUN(the_run_extremes_keep_their_no_value_marks);
    RUN(a_balance_report_takes_phase_3599_and_quality_100_and_refuses_past);
    RUN(the_encoder_wraps_the_phase_and_refuses_a_quality_over_100);
    RUN(the_balance_flags_are_shared_vibes_bit_for_bit);
    RUN(a_status_at_its_limits_and_a_sensor_state_past_the_last);
    RUN(the_info_names_the_address_lsb_first_and_refuses_role_0);
    RUN(every_type_refuses_another_length_and_an_unknown_type_is_refused);
    RUN(a_refused_packet_leaves_the_output_as_it_was);
    RUN(every_control_write_has_its_bytes_and_its_limits);
    RUN(the_power_role_takes_run_sleep_off_and_not_the_balance_writes);
    RUN(seq_counts_losses_across_its_wrap_at_256);
    RUN(t_node_differences_hold_across_the_wrap_at_2_32us);
    RUN(the_40_bit_accumulators_sign_extend_and_wrap);
    RUN(voltage_fields_round_to_2mv_and_clamp);
    RUN(a_window_of_18_samples_closes_on_the_next_windows_sample);
    RUN(a_negative_mean_rounds_away_from_zero);
    RUN(a_clipped_sample_is_flagged_and_is_never_a_value);
    RUN(a_window_of_failed_reads_has_no_values_and_says_so);
    RUN(windows_without_a_sample_between_two_are_not_sent);
    RUN(the_window_that_ends_past_2_32us_carries_the_wrapped_t_node);
    RUN(the_run_extremes_restart_at_the_marker_and_keep_every_peak);
    RUN(a_window_closed_with_the_marker_stays_out_of_the_new_run);
    RUN(a_fall_in_energy_under_the_same_epoch_is_a_restart_not_22gj);
    RUN(a_notification_fits_the_mtu_less_3_bytes);
    RUN(the_name_takes_the_two_address_bytes_printed_last);
    RUN(the_advertising_has_its_bytes_and_matches_its_role);
    RUN(a_name_alone_matches_and_other_devices_do_not);
    RUN(a_name_gives_its_role);
    RUN(a_length_past_the_end_refuses_the_report_and_padding_ends_it);
    RUN(a_null_argument_does_nothing);
    return test_summary("rcnode");
}
