/*
 * The panel's balance node controller (bal_ctl.h) against the scripted
 * host stack and node of bal_rig.h.
 *
 * Under test: the link up with INFO read and BLADES then RPM written; a
 * report UP for 999ms and QUIET at 1000ms; INFO of another role or
 * protocol (WRONG, reports dropped, nothing written); an INFO read that
 * fails (the link ends and comes back); the no-index mode after 2 reports
 * without an index edge and not after 1, the bench rpm written then and 0
 * again at the first edge; CONTROL writes never closer than 250ms however
 * often the rpm moves; a refused write going again; STATUS read every 5s
 * (not at 4999ms) into the battery, one never answered ending the link as
 * a failed GATT step; a scan refused while ARMED and stopped
 * at ARM; the scan listing balance nodes only, a scan response refreshing
 * a listed one, a bind; seq gaps counted; OFF ending it all.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "bal_rig.h"

/* The link comes up: INFO read and matched, BLADES 2 at once, RPM 0 (the
 * index mode) 250ms later, nothing more while nothing changes. */
TEST_CASE(the_link_comes_up_and_blades_then_rpm_are_written)
{
    brig_t r;
    brig_up(&r);
    CHECK(r.c.linked);
    CHECK(r.c.info_ok);
    CHECK_EQ(r.info_reads, 1u);
    CHECK_EQ(bal_ctl_link(&r.c, r.now), BAL_LINK_QUIET);
    if (r.nw < 1u) {
        T_FAIL("no write after the link came up");
        return;
    }
    CHECK_EQ(r.w[0].op, RCNODE_OP_BLADES);
    CHECK_EQ(r.w[0].blades, 2u);
    brig_run(&r, 300u);
    CHECK_EQ(r.nw, 2u);
    CHECK_EQ(r.w[1].op, RCNODE_OP_RPM);
    CHECK_EQ(r.w[1].rpm, 0u);
    CHECK(r.w_ms[1] - r.w_ms[0] >= BAL_CTL_MS);
    brig_run(&r, 2000u);
    CHECK_EQ(r.nw, 2u);
    /* A new blade count goes once. */
    bal_ctl_drive(&r.c, 3u, 0u);
    brig_run(&r, 300u);
    CHECK_EQ(r.nw, 3u);
    CHECK_EQ(r.w[2].op, RCNODE_OP_BLADES);
    CHECK_EQ(r.w[2].blades, 3u);
    /* The count is held to the node's 1 to 12. */
    bal_ctl_drive(&r.c, 40u, 0u);
    CHECK_EQ(r.c.blades, RCNODE_BLADES_MAX);
    bal_ctl_drive(&r.c, RCNODE_BLADES_MAX + 1u, 0u);
    CHECK_EQ(r.c.blades, RCNODE_BLADES_MAX);
    bal_ctl_drive(&r.c, RCNODE_BLADES_MAX, 60001u);
    CHECK_EQ(r.c.blades, RCNODE_BLADES_MAX);
    CHECK_EQ(r.c.bench_rpm, RCNODE_RPM_MAX);
    bal_ctl_drive(&r.c, 0u, 0u);
    CHECK_EQ(r.c.blades, RCNODE_BLADES_MIN);
}

/* A report is UP for 999ms after it came and QUIET at 1000ms; the report
 * hands out its age. */
TEST_CASE(a_report_is_fresh_for_999ms_and_stale_at_1000ms)
{
    brig_t r;
    brig_up(&r);
    const rcnode_bal_report_t p = brig_steady(420u, 2650u, 8000u);
    brig_report(&r, &p);
    const uint32_t t0 = r.now;
    CHECK_EQ(bal_ctl_link(&r.c, r.now), BAL_LINK_UP);
    CHECK_EQ(bal_ctl_link(&r.c, t0 + BAL_STALE_MS - 1u), BAL_LINK_UP);
    CHECK_EQ(bal_ctl_link(&r.c, t0 + BAL_STALE_MS), BAL_LINK_QUIET);
    rcnode_bal_report_t got;
    uint32_t age = 0u;
    CHECK(bal_ctl_report(&r.c, t0 + 700u, &got, &age));
    CHECK_EQ(age, 700u);
    CHECK_EQ(got.mag_1x, 420u);
    CHECK_EQ(got.phase_1x, 2650u);
    CHECK_EQ(got.rpm, 8000u);
    CHECK_EQ(r.c.reports, 1u);
}

/* INFO of the power role, or of another protocol: WRONG, reports dropped,
 * no CONTROL write. */
TEST_CASE(info_of_another_role_or_protocol_takes_nothing)
{
    for (int k = 0; k < 2; ++k) {
        brig_t r;
        brig_init(&r);
        if (k == 0) {
            r.role = RCNODE_ROLE_POWER;
        } else {
            r.proto = RCNODE_PROTO + 1u;
        }
        brig_run(&r, 50u);
        CHECK(r.c.linked);
        CHECK(!r.c.info_ok);
        CHECK_EQ(bal_ctl_link(&r.c, r.now), BAL_LINK_WRONG);
        const rcnode_bal_report_t p = brig_steady(420u, 2650u, 8000u);
        brig_report(&r, &p);
        CHECK_EQ(r.c.reports, 0u);
        CHECK_EQ(r.c.unread, 1u);
        brig_run(&r, 6000u);
        CHECK_EQ(r.nw, 0u);
        CHECK_EQ(r.st_reads, 0u);
        bal_view_t v;
        bal_ctl_view(&r.c, r.now, &v);
        CHECK_EQ(v.info_role, r.role);
        CHECK_EQ(v.info_proto, r.proto);
    }
}

/* An INFO read that fails ends the link as a failed GATT step; it comes
 * back after the backoff and reads INFO again. */
TEST_CASE(an_info_that_fails_ends_the_link_and_it_comes_back)
{
    brig_t r;
    brig_init(&r);
    r.fail_info = 1u;
    brig_run(&r, 50u);
    CHECK_EQ(r.c.info_failed, 1u);
    CHECK(!r.c.info_ok);
    brig_run(&r, 10000u);
    CHECK(r.c.linked);
    CHECK(r.c.info_ok);
    CHECK_EQ(r.info_reads, 2u);
    CHECK(r.connects >= 2u);
}

/* One report without an index edge keeps the index mode; the second in a
 * row asks for the no-index mode, the bench rpm written; the first report
 * with an edge goes back to RPM 0. */
TEST_CASE(two_reports_without_an_index_write_the_bench_rpm)
{
    brig_t r;
    brig_up(&r);
    brig_run(&r, 300u);
    const unsigned w0 = r.nw;   /* BLADES and RPM 0 */
    CHECK_EQ(w0, 2u);
    bal_ctl_drive(&r.c, 2u, 9000u);
    rcnode_bal_report_t p = brig_steady(420u, 0u, 0u);
    p.flags &= (uint16_t)~(RCNODE_BF_INDEX_SEEN | RCNODE_BF_VALID);
    brig_report(&r, &p);
    brig_run(&r, 600u);
    CHECK(!r.c.noidx);
    CHECK_EQ(r.nw, w0);
    brig_report(&r, &p);
    CHECK(r.c.noidx);
    brig_run(&r, 300u);
    CHECK_EQ(r.nw, w0 + 1u);
    CHECK_EQ(r.w[w0].op, RCNODE_OP_RPM);
    CHECK_EQ(r.w[w0].rpm, 9000u);
    const rcnode_bal_report_t q = brig_steady(420u, 100u, 9000u);
    brig_report(&r, &q);
    CHECK(!r.c.noidx);
    brig_run(&r, 300u);
    CHECK_EQ(r.nw, w0 + 2u);
    CHECK_EQ(r.w[w0 + 1u].rpm, 0u);
}

/* The rpm moving every 1ms in the no-index mode is written at most every
 * 250ms: the link carries a Dial too. */
TEST_CASE(control_writes_are_never_closer_than_250ms)
{
    brig_t r;
    brig_up(&r);
    rcnode_bal_report_t p = brig_steady(420u, 0u, 0u);
    p.flags &= (uint16_t)~RCNODE_BF_INDEX_SEEN;
    brig_report(&r, &p);
    brig_report(&r, &p);
    for (uint32_t k = 0; k < 5000u; ++k) {
        bal_ctl_drive(&r.c, (uint8_t)(2u + (k / 700u) % 3u),
                      6000u + (k % 997u));
        if (k % 250u == 0u) {
            brig_report(&r, &p);
        }
        brig_run(&r, 1u);
    }
    CHECK(r.nw > 10u);
    for (unsigned i = 1; i < r.nw; ++i) {
        if (r.w_ms[i] - r.w_ms[i - 1u] < BAL_CTL_MS) {
            T_FAIL("writes %u and %u %ums apart", i - 1u, i,
                   (unsigned)(r.w_ms[i] - r.w_ms[i - 1u]));
        }
    }
    /* And 5s hold 20 periods at most, 21 writes. */
    CHECK(r.nw <= 5000u / BAL_CTL_MS + 3u);
}

/* A write the node refuses goes again 250ms later. */
TEST_CASE(a_refused_write_goes_again)
{
    brig_t r;
    brig_init(&r);
    r.fail_writes = 1u;
    brig_run(&r, 50u);
    CHECK_EQ(r.nw, 0u);
    CHECK_EQ(r.c.wr_failed, 1u);
    brig_run(&r, BAL_CTL_MS);
    CHECK_EQ(r.nw, 1u);
    CHECK_EQ(r.w[0].op, RCNODE_OP_BLADES);
}

/* STATUS is read every 5000ms while linked, not at 4999ms, and its cell
 * lands in the view; BAT_NONE and BAT_LOW go with it. */
TEST_CASE(status_is_read_every_5s_into_the_battery)
{
    brig_t r;
    brig_init(&r);
    brig_run(&r, 50u);
    CHECK_EQ(r.st_reads, 0u);
    bal_view_t v;
    bal_ctl_view(&r.c, r.now, &v);
    CHECK(!v.bat_known);
    const uint32_t t_link = r.c.t_status;
    brig_run(&r, t_link + BAL_STATUS_MS - 1u - r.now);
    CHECK_EQ(r.st_reads, 0u);
    brig_run(&r, 1u);
    CHECK_EQ(r.st_reads, 1u);
    bal_ctl_view(&r.c, r.now, &v);
    CHECK(v.bat_known);
    CHECK_EQ(v.bat_mv, 3912u);
    r.vbat = 3410u;
    r.st_flags = RCNODE_ST_BAT_LOW;
    brig_run(&r, BAL_STATUS_MS);
    CHECK_EQ(r.st_reads, 2u);
    bal_ctl_view(&r.c, r.now, &v);
    CHECK_EQ(v.bat_mv, 3410u);
    CHECK_EQ(v.bat_flags, RCNODE_ST_BAT_LOW);
    /* The link gone: the battery reads as unknown, INFO as unread. */
    brig_drop(&r);
    bal_ctl_view(&r.c, r.now, &v);
    CHECK(!v.bat_known);
    CHECK(!r.c.info_ok);
    CHECK(!r.c.info_read);
}

/* A STATUS read that is never answered ends the link as a failed GATT
 * step after BLEU_GATT_MS; the link comes back and reads again. */
TEST_CASE(a_status_read_without_an_answer_ends_the_link)
{
    brig_t r;
    brig_init(&r);
    r.mute_status = true;
    brig_run(&r, 50u);
    brig_run(&r, BAL_STATUS_MS);
    CHECK_EQ(r.st_reads, 1u);
    CHECK(r.c.b.rd_busy);
    CHECK(r.c.b.rd_aux);
    /* No CONTROL write while the read waits. */
    CHECK(!bleu_can_send(&r.c.b));
    brig_run(&r, BLEU_GATT_MS + 1u);
    CHECK_EQ(r.c.b.err, BLEU_ERR_GATT);
    r.mute_status = false;
    brig_run(&r, 10000u);
    CHECK(r.c.linked);
    CHECK(r.st_reads >= 2u);
    bal_view_t v;
    bal_ctl_view(&r.c, r.now, &v);
    CHECK(v.bat_known);
    /* bleu_read_aux() refuses without a link, and a second time while the
     * first waits. */
    bleu_t b;
    bleu_init(&b);
    CHECK(!bleu_read_aux(&b));
    CHECK(bleu_read_aux(&r.c.b));
    CHECK(!bleu_read_aux(&r.c.b));
}

/* A scan while ARMED is refused and asks the host stack for nothing; on a
 * SAFE bench it starts; ARMED while it runs stops it in that step. */
TEST_CASE(a_scan_is_refused_while_armed_and_stopped_at_arm)
{
    brig_t r;
    brig_up(&r);
    CHECK_EQ(bal_ctl_scan(&r.c, true, r.now), BAL_SCAN_ARMED);
    brig_run(&r, 20u);
    CHECK_EQ(r.scans, 0u);
    CHECK(!r.c.b.scanning);
    CHECK_EQ(r.c.refused, 1u);
    CHECK_EQ(bal_ctl_scan(&r.c, false, r.now), BAL_SCAN_STARTED);
    brig_run(&r, 1u);
    CHECK_EQ(r.scans, 1u);
    CHECK_EQ(bal_ctl_link(&r.c, r.now), BAL_LINK_SCANNING);
    CHECK_EQ(bal_ctl_scan(&r.c, false, r.now), BAL_SCAN_NOT_READY);
    brig_run(&r, 4000u);
    CHECK(r.c.b.scanning);
    r.armed = true;
    brig_run(&r, 1u);
    CHECK(!r.c.b.scanning);
    CHECK_EQ(r.stops, 1u);
    bal_view_t v;
    bal_ctl_view(&r.c, r.now, &v);
    CHECK(v.scan_done);
    CHECK(v.scan_cut);
    /* OFF refuses a scan too. */
    bal_cfg_t cfg = r.c.cfg;
    cfg.en = false;
    bal_ctl_config(&r.c, &cfg, r.now);
    r.armed = false;
    CHECK_EQ(bal_ctl_scan(&r.c, false, r.now), BAL_SCAN_NOT_READY);
    CHECK_EQ(bal_ctl_link(&r.c, r.now), BAL_LINK_OFF);
}

/* One advertising report of address @p last into the controller. */
static void adv(brig_t *r, uint8_t last, int8_t rssi, const uint8_t *ad,
                size_t len)
{
    bleu_evt_t e = brig_ev(BLEU_EV_ADV, 0u);
    e.addr.val[0] = last;
    e.addr.type = BLEU_ADDR_RANDOM;
    e.rssi = rssi;
    memcpy(e.data, ad, len);
    e.len = (uint8_t)len;
    bal_ctl_event(&r->c, &e, r->now);
}

/* The scan lists the advertising of balance nodes, not of a power node or
 * another device; a scan response refreshes a listed node's RSSI and lists
 * none; the strongest is first; a bind takes one. */
TEST_CASE(the_scan_lists_balance_nodes_and_a_bind_takes_one)
{
    brig_t r;
    brig_init(&r);
    bal_cfg_t cfg = r.c.cfg;
    cfg.addr[0] = '\0';
    bal_ctl_config(&r.c, &cfg, r.now);
    CHECK_EQ(bal_ctl_link(&r.c, r.now), BAL_LINK_NONE);
    CHECK_EQ(bal_ctl_scan(&r.c, false, r.now), BAL_SCAN_STARTED);
    brig_run(&r, 1u);
    const uint8_t a1[6] = { 0x10u, 0x3Au, 0x7Fu, 0xC2u, 0xB1u, 0xA0u };
    uint8_t bal[RCNODE_ADV_LEN], pwr[RCNODE_ADV_LEN], rsp[RCNODE_RSP_LEN];
    CHECK_EQ(rcnode_adv_build(RCNODE_ROLE_BALANCE, a1, bal, sizeof bal),
             RCNODE_ADV_LEN);
    CHECK_EQ(rcnode_adv_build(RCNODE_ROLE_POWER, a1, pwr, sizeof pwr),
             RCNODE_ADV_LEN);
    CHECK_EQ(rcnode_rsp_build(rsp, sizeof rsp), RCNODE_RSP_LEN);
    const uint8_t other[] = { 2u, 0x01u, 0x06u, 4u, 0x09u, 'F', 'O', 'O' };
    adv(&r, 1u, -70, pwr, sizeof pwr);
    adv(&r, 2u, -60, bal, sizeof bal);
    adv(&r, 3u, -80, other, sizeof other);
    adv(&r, 4u, -55, rsp, sizeof rsp);     /* a response, no listing */
    adv(&r, 5u, -75, bal, sizeof bal);
    adv(&r, 5u, -40, rsp, sizeof rsp);     /* refreshes 5: now first */
    adv(&r, 1u, -70, pwr, sizeof pwr);     /* counted once */
    adv(&r, 5u, 127, rsp, sizeof rsp);     /* no RSSI: 5 keeps -40 */
    adv(&r, 6u, -66, rsp, sizeof rsp);     /* a response alone: nothing */
    CHECK_EQ(r.c.found_n, 2u);
    CHECK_STR_EQ(r.c.found[0].addr, "00:00:00:00:00:05 R");
    CHECK_EQ(r.c.found[0].rssi, -40);
    CHECK_STR_EQ(r.c.found[1].addr, "00:00:00:00:00:02 R");
    CHECK_STR_EQ(r.c.found[1].name, "rcbench-bal-3A10");
    CHECK_EQ(r.c.hidden, 2u);
    brig_run(&r, BLEU_SCAN_MS + 10u);
    bal_view_t v;
    bal_ctl_view(&r.c, r.now, &v);
    CHECK(v.scan_done);
    CHECK(!v.scan_cut);
    CHECK_EQ(v.found_n, 2u);
    CHECK(!bal_ctl_bind(&r.c, 2u, r.now));
    CHECK(bal_ctl_bind(&r.c, 1u, r.now));
    CHECK_EQ(r.c.bind_seq, 1u);
    CHECK_STR_EQ(r.c.cfg.addr, "00:00:00:00:00:02 R");
    CHECK_STR_EQ(r.c.cfg.name, "rcbench-bal-3A10");
    brig_run(&r, 50u);
    CHECK(r.c.linked);
    bal_ctl_view(&r.c, r.now, &v);
    CHECK_STR_EQ(v.bound_name, "rcbench-bal-3A10");
}

/* 40 other devices fill the 32 remembered addresses and are counted
 * once each up to them; beyond, every report counts. */
TEST_CASE(a_crowded_scan_counts_others_without_overrun)
{
    brig_t r;
    brig_up(&r);
    CHECK_EQ(bal_ctl_scan(&r.c, false, r.now), BAL_SCAN_STARTED);
    brig_run(&r, 1u);
    const uint8_t other[] = { 2u, 0x01u, 0x06u, 4u, 0x09u, 'F', 'O', 'O' };
    for (uint8_t k = 0; k < 40u; ++k) {
        adv(&r, (uint8_t)(10u + k), -60, other, sizeof other);
    }
    CHECK_EQ(r.c.seen_n, BAL_SEEN_MAX);
    CHECK_EQ(r.c.hidden, 40u);
    adv(&r, 10u, -60, other, sizeof other);    /* remembered: once */
    CHECK_EQ(r.c.hidden, 40u);
    CHECK_EQ(r.c.found_n, 0u);
}

/* An address text without its terminator is cut, not read past. */
TEST_CASE(a_config_text_is_cut_to_its_field)
{
    brig_t r;
    brig_init(&r);
    bal_cfg_t cfg = r.c.cfg;
    memset(cfg.addr, 'A', sizeof cfg.addr);
    memset(cfg.name, 'B', sizeof cfg.name);
    bal_ctl_config(&r.c, &cfg, r.now);
    CHECK_EQ(strlen(r.c.cfg.addr), BAL_ADDR_MAX - 1u);
    CHECK_EQ(strlen(r.c.cfg.name), BAL_NAME_MAX - 1u);
    CHECK(!r.c.b.bound);
}

/* A seq gap counts the reports missing; another packet type on STREAM is
 * counted bad and taken as no report. */
TEST_CASE(seq_gaps_and_foreign_packets_are_counted)
{
    brig_t r;
    brig_up(&r);
    const rcnode_bal_report_t p = brig_steady(420u, 2650u, 8000u);
    brig_report(&r, &p);
    r.seq = (uint8_t)(r.seq + 3u);
    brig_report(&r, &p);
    CHECK_EQ(r.c.reports, 2u);
    CHECK_EQ(r.c.lost, 3u);
    bleu_evt_t e = brig_ev(BLEU_EV_NOTIFY, 0u);
    e.h1 = BRIG_STREAM_VAL;
    rcnode_status_t st;
    memset(&st, 0, sizeof st);
    st.hdr.type = RCNODE_T_STATUS;
    e.len = (uint8_t)rcnode_enc_status(&st, e.data, sizeof e.data);
    bal_ctl_event(&r.c, &e, r.now);
    CHECK_EQ(r.c.bad, 1u);
    CHECK_EQ(r.c.reports, 2u);
}

/* OFF ends the link, keeps no report and writes nothing more. */
TEST_CASE(off_ends_the_link_and_hands_on_nothing)
{
    brig_t r;
    brig_up(&r);
    const rcnode_bal_report_t p = brig_steady(420u, 2650u, 8000u);
    brig_report(&r, &p);
    bal_cfg_t cfg = r.c.cfg;
    cfg.en = false;
    bal_ctl_config(&r.c, &cfg, r.now);
    brig_run(&r, 100u);
    CHECK(!r.c.linked);
    rcnode_bal_report_t got;
    uint32_t age;
    CHECK(!bal_ctl_report(&r.c, r.now, &got, &age));
    const unsigned nw = r.nw;
    bal_ctl_drive(&r.c, 4u, 0u);
    brig_run(&r, 1000u);
    CHECK_EQ(r.nw, nw);
    CHECK_EQ(bal_ctl_link(&r.c, r.now), BAL_LINK_OFF);
    /* A scan's result goes with OFF. */
    cfg.en = true;
    bal_ctl_config(&r.c, &cfg, r.now);
    CHECK_EQ(bal_ctl_scan(&r.c, false, r.now), BAL_SCAN_STARTED);
    brig_run(&r, BLEU_SCAN_MS + 10u);
    CHECK(r.c.scan_done);
    cfg.en = false;
    bal_ctl_config(&r.c, &cfg, r.now);
    bal_view_t v;
    bal_ctl_view(&r.c, r.now, &v);
    CHECK(!v.scan_done);
}

/* The radio's states read through to the link word. */
TEST_CASE(the_radio_states_read_through)
{
    brig_t r;
    brig_init(&r);
    bal_ctl_radio(&r.c, IMU_RADIO_NO_LINK, r.now);
    CHECK_EQ(bal_ctl_link(&r.c, r.now), BAL_LINK_C6_FAILED);
    bal_ctl_radio(&r.c, IMU_RADIO_NO_SYNC, r.now);
    CHECK_EQ(bal_ctl_link(&r.c, r.now), BAL_LINK_NO_BT);
    bal_ctl_radio(&r.c, IMU_RADIO_STARTING, r.now);
    CHECK_EQ(bal_ctl_link(&r.c, r.now), BAL_LINK_STARTING);
    bal_ctl_radio(&r.c, IMU_RADIO_READY, r.now);
    CHECK_EQ(bal_ctl_link(&r.c, r.now), BAL_LINK_CONNECTING);
    CHECK_EQ(bal_ctl_link(NULL, r.now), BAL_LINK_OFF);
    CHECK(bleu_profile_valid(&bal_profile));
}

int main(void)
{
    RUN(the_link_comes_up_and_blades_then_rpm_are_written);
    RUN(a_report_is_fresh_for_999ms_and_stale_at_1000ms);
    RUN(info_of_another_role_or_protocol_takes_nothing);
    RUN(an_info_that_fails_ends_the_link_and_it_comes_back);
    RUN(two_reports_without_an_index_write_the_bench_rpm);
    RUN(control_writes_are_never_closer_than_250ms);
    RUN(a_refused_write_goes_again);
    RUN(status_is_read_every_5s_into_the_battery);
    RUN(a_status_read_without_an_answer_ends_the_link);
    RUN(a_scan_is_refused_while_armed_and_stopped_at_arm);
    RUN(the_scan_lists_balance_nodes_and_a_bind_takes_one);
    RUN(a_crowded_scan_counts_others_without_overrun);
    RUN(a_config_text_is_cut_to_its_field);
    RUN(seq_gaps_and_foreign_packets_are_counted);
    RUN(off_ends_the_link_and_hands_on_nothing);
    RUN(the_radio_states_read_through);
    return test_summary("bal_ctl");
}
