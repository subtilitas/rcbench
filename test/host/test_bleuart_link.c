/*
 * The BLE central of protocols/bleuart (bleuart.c) against the scripted
 * peer of test/host/ble_sim.h: the write path, the sensors it refuses, the
 * link's end in every state, reconnect, scans beside a connect, OFF, late
 * answers and the bound address.
 *
 * Under test: a command with response and without; the refusals of
 * bleu_send(); a device without the service, without 8841's write, without
 * 1E4D's NOTIFY or its CCCD; a failed discovery step; a disconnect in every
 * state and the retry 1,000ms after it; a drop that ends a pending step; a
 * scan during a pending connect, beside a link and one that does not
 * start; OFF in every state; a late answer and a connect that comes through
 * after its cancel; binding another sensor, the same one and none; the full
 * action queue; NULL arguments.  ACA3 and 6DAA are never written in any of
 * these runs.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "ble_capture.h"
#include "ble_sim.h"
#include "bleuart.h"

/* A RATE command: unlock-free, 5 bytes. */
static const uint8_t k_cmd[5] = { 0xFF, 0xAA, 0x03, 0x06, 0x00 };

/* The state a case waits for. */
static bleu_state_t g_st;

/* Whether the machine stands in g_st. */
static bool in_state(const bleu_t *b, const bsim_t *s)
{
    (void)s;
    return b->st == g_st;
}

/* Switch on with the sensor bound and run until @p st; @return the tick. */
static uint32_t run_to_state(bsim_t *s, bleu_t *b, bleu_state_t st)
{
    g_st = st;
    bleu_init(b);
    bleu_bind(b, &k_cap_addr, 0);
    bleu_enable(b, true, 0);
    const uint32_t t = bsim_until(s, b, 0, 3000, in_state);
    CHECK_EQ(b->st, st);
    return t;
}

TEST_CASE(a_command_goes_with_response_on_8841)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    bleu_init(&b);
    CHECK(!bleu_send(&b, k_cmd, sizeof k_cmd, 0));
    bleu_bind(&b, &k_cap_addr, 0);
    bleu_enable(&b, true, 0);
    /* UP at 130, the 6DAA read holds the slot until 150. */
    bsim_run(&s, &b, 0, 135);
    CHECK(bleu_link_up(&b));
    CHECK(!bleu_can_send(&b));
    CHECK(!bleu_send(&b, k_cmd, sizeof k_cmd, 135));
    bsim_run(&s, &b, 135, 151);
    CHECK(bleu_can_send(&b));
    CHECK_EQ(b.wr, BLEU_WR_IDLE);

    CHECK(!bleu_send(&b, k_cmd, 0, 151));
    CHECK(!bleu_send(&b, NULL, 5, 151));
    uint8_t big[BLEU_WRITE_MAX + 1u];
    memset(big, 0x11, sizeof big);
    CHECK(!bleu_send(&b, big, sizeof big, 151));
    CHECK_EQ(bsim_count(&s, BLEU_ACT_WRITE), 0);

    CHECK(bleu_send(&b, k_cmd, sizeof k_cmd, 151));
    CHECK_EQ(b.wr, BLEU_WR_BUSY);
    CHECK(!bleu_send(&b, k_cmd, sizeof k_cmd, 151));
    bsim_tick(&s, &b, 151);
    const bleu_act_t *w = &bsim_nth(&s, BLEU_ACT_WRITE, 0)->a;
    CHECK_EQ(w->h1, H_RX_VAL);
    CHECK_EQ(w->conn, 1);
    CHECK_EQ(w->len, 5);
    CHECK(memcmp(w->data, k_cmd, 5) == 0);
    bsim_run(&s, &b, 152, 172);
    CHECK_EQ(b.wr, BLEU_WR_OK);
    CHECK_EQ(b.wr_status, 0);
    CHECK(bleu_can_send(&b));

    /* The longest command fits. */
    CHECK(bleu_send(&b, big, BLEU_WRITE_MAX, 172));
    bsim_run(&s, &b, 172, 200);
    CHECK_EQ(bsim_last(&s, BLEU_ACT_WRITE)->a.len, BLEU_WRITE_MAX);
    CHECK_EQ(b.wr, BLEU_WR_OK);

    /* A refused write is FAILED with its status. */
    s.fail = BSIM_Q_WRITE;
    s.fail_status = 0x0103;
    CHECK(bleu_send(&b, k_cmd, sizeof k_cmd, 200));
    bsim_run(&s, &b, 200, 230);
    CHECK_EQ(b.wr, BLEU_WR_FAILED);
    CHECK_EQ(b.wr_status, 0x0103);
    CHECK(bleu_link_up(&b));
    CHECK(bsim_writes_ok(&s));
}

TEST_CASE(write_no_response_only_writes_without_response)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    bsim_chr(&s, bleu_uuid_rx)->props = BLEU_PROP_WRITE_NR;
    const uint32_t t = bsim_up(&s, &b, &k_cap_addr, 0);
    CHECK(bleu_send(&b, k_cmd, sizeof k_cmd, t));
    bsim_run(&s, &b, t, t + 3u);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_WRITE), 0);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_WRITE_NR), 1);
    CHECK_EQ(bsim_last(&s, BLEU_ACT_WRITE_NR)->a.h1, H_RX_VAL);
    CHECK_EQ(b.wr, BLEU_WR_OK);
    CHECK(bsim_writes_ok(&s));
}

/* Run a table the central must refuse; hold the refusal and its ends. */
static void refused(bsim_t *s)
{
    bleu_t b;
    bleu_init(&b);
    bleu_bind(&b, &k_cap_addr, 0);
    bleu_enable(&b, true, 0);
    bsim_run(s, &b, 0, 500);
    CHECK_EQ(b.st, BLEU_ST_NOT_WIT);
    CHECK_EQ(b.err, BLEU_ERR_NOT_WIT);
    CHECK_EQ(bsim_count(s, BLEU_ACT_DISCONNECT), 1);
    CHECK_EQ(bsim_count(s, BLEU_ACT_SUBSCRIBE), 0);
    CHECK_EQ(bsim_count(s, BLEU_ACT_WRITE), 0);
    CHECK(!bleu_send(&b, k_cmd, sizeof k_cmd, 500));
    bsim_run(s, &b, 500, 20000);
    CHECK_EQ(bsim_count(s, BLEU_ACT_CONNECT), 1);
    /* The same address bound again tries again; so does OFF and on. */
    bleu_bind(&b, &k_cap_addr, 20000);
    bsim_run(s, &b, 20000, 20001);
    CHECK_EQ(bsim_count(s, BLEU_ACT_CONNECT), 2);
    bsim_run(s, &b, 20001, 20500);
    CHECK_EQ(b.st, BLEU_ST_NOT_WIT);
    bleu_enable(&b, false, 20500);
    bleu_enable(&b, true, 20500);
    CHECK_EQ(b.err, BLEU_ERR_NONE);
    bsim_run(s, &b, 20500, 20501);
    CHECK_EQ(bsim_count(s, BLEU_ACT_CONNECT), 3);
    CHECK(bsim_writes_ok(s));
}

TEST_CASE(a_device_without_the_service_8841_notify_or_cccd_is_refused)
{
    bsim_t s;
    bsim_init(&s);
    s.no_svc = true;
    refused(&s);
    bsim_init(&s);
    bsim_drop_chr(&s, bleu_uuid_rx);
    refused(&s);
    bsim_init(&s);
    bsim_chr(&s, bleu_uuid_rx)->props = BLEU_PROP_READ;
    refused(&s);
    bsim_init(&s);
    bsim_drop_chr(&s, bleu_uuid_tx);
    refused(&s);
    bsim_init(&s);
    bsim_chr(&s, bleu_uuid_tx)->props = BLEU_PROP_READ;
    refused(&s);
    bsim_init(&s);
    bsim_drop_cccd(&s);
    refused(&s);
    /* ACA3's CCCD outside 1E4D's range is not taken for it. */
    bsim_init(&s);
    bsim_drop_cccd(&s);
    s.dsc[s.ndsc++] = (bsim_dsc_t){ H_RX_DEF, BLEU_UUID16_CCCD };
    refused(&s);
    /* A descriptor of another UUID is none. */
    bsim_init(&s);
    bsim_drop_cccd(&s);
    s.dsc[s.ndsc++] = (bsim_dsc_t){ H_TX_CCCD, 0x2901u };
    refused(&s);
}

TEST_CASE(a_failed_discovery_step_retries_after_the_gap)
{
    static const uint8_t q[] = { BSIM_Q_SVC, BSIM_Q_CHR, BSIM_Q_DSC,
                                 BSIM_Q_SUB };
    for (unsigned i = 0; i < sizeof q; ++i) {
        bsim_t s;
        bleu_t b;
        bsim_init(&s);
        s.fail = q[i];
        s.fail_status = 0x0105;
        bleu_init(&b);
        bleu_bind(&b, &k_cap_addr, 0);
        bleu_enable(&b, true, 0);
        bsim_run(&s, &b, 0, 400);
        CHECK_EQ(b.st, BLEU_ST_WAIT);
        CHECK_EQ(b.err, BLEU_ERR_GATT);
        CHECK_EQ(b.err_status, 0x0105);
        CHECK_EQ(bsim_count(&s, BLEU_ACT_DISCONNECT), 1);
        const uint32_t tf = bsim_last(&s, BLEU_ACT_DISCONNECT)->t;
        bsim_run(&s, &b, 400, tf + BLEU_RETRY_MS);
        CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 1);
        bsim_tick(&s, &b, tf + BLEU_RETRY_MS);
        CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 2);
    }
}

TEST_CASE(a_disconnect_in_every_state_retries_after_the_gap)
{
    static const struct {
        uint8_t silent;
        bleu_state_t st;
        bool write;
    } k[] = {
        { BSIM_Q_SVC, BLEU_ST_DISC_SVC, false },
        { BSIM_Q_CHR, BLEU_ST_DISC_CHR, false },
        { BSIM_Q_DSC, BLEU_ST_DISC_DSC, false },
        { BSIM_Q_MTU, BLEU_ST_MTU, false },
        { BSIM_Q_SUB, BLEU_ST_SUBSCRIBE, false },
        { BSIM_Q_READ, BLEU_ST_UP, false },
        { 0, BLEU_ST_UP, false },
        { BSIM_Q_WRITE, BLEU_ST_UP, true },
    };
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; ++i) {
        bsim_t s;
        bleu_t b;
        bsim_init(&s);
        s.silent = k[i].silent;
        uint32_t t = run_to_state(&s, &b, k[i].st);
        if (k[i].st == BLEU_ST_UP) {
            t = bsim_until(&s, &b, t, 100, k[i].silent == BSIM_Q_READ
                                               ? bsim_is_up : bsim_can_send);
        }
        if (k[i].write) {
            CHECK(bleu_send(&b, k_cmd, sizeof k_cmd, t));
            bsim_tick(&s, &b, ++t);
            CHECK_EQ(b.wr, BLEU_WR_BUSY);
        }
        bsim_drop(&s, t + 1u, 0x08);
        bsim_tick(&s, &b, t + 1u);
        CHECK_EQ(b.st, BLEU_ST_WAIT);
        CHECK_EQ(b.err, BLEU_ERR_LOST);
        CHECK_EQ(b.reason, 0x08);
        CHECK_EQ(b.err_status, 0x08);
        CHECK(!bleu_link_up(&b));
        if (k[i].write) {
            CHECK_EQ(b.wr, BLEU_WR_FAILED);
            CHECK_EQ(b.wr_status, BLEU_STATUS_LOST);
        }
        /* A drop gets no disconnect of its own: the link is gone. */
        CHECK_EQ(bsim_count(&s, BLEU_ACT_DISCONNECT), 0);
        const uint32_t tf = t + 1u;
        bsim_run(&s, &b, tf + 1u, tf + BLEU_RETRY_MS);
        CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 1);
        bsim_tick(&s, &b, tf + BLEU_RETRY_MS);
        CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 2);
        /* The reconnect discovers the table again and comes up. */
        s.silent = 0;
        const uint32_t up = bsim_until(&s, &b, tf + BLEU_RETRY_MS + 1u, 1000,
                                       bsim_can_send);
        CHECK(bleu_can_send(&b));
        CHECK_EQ(b.conn, 2);
        CHECK_EQ(bsim_count(&s, BLEU_ACT_DISC_SVC), 2);
        CHECK_EQ(b.err, BLEU_ERR_NONE);
        CHECK(bsim_writes_ok(&s));
        (void)up;
    }
}

TEST_CASE(a_disconnect_while_connecting_or_on_another_link_is_ignored)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    s.absent = true;
    (void)run_to_state(&s, &b, BLEU_ST_CONNECTING);
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_DISCONNECT;
    e.conn = 1;
    (void)bleu_event(&b, &e, 10);
    CHECK_EQ(b.st, BLEU_ST_CONNECTING);
    CHECK_EQ(b.stray, 1);

    bsim_init(&s);
    const uint32_t t = bsim_up(&s, &b, &k_cap_addr, 0);
    e.conn = 7;
    (void)bleu_event(&b, &e, t);
    CHECK(bleu_link_up(&b));
    CHECK_EQ(b.stray, 1);
}

TEST_CASE(a_drop_during_a_step_ends_the_step_and_retries)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    bleu_init(&b);
    bleu_bind(&b, &k_cap_addr, 0);
    bleu_enable(&b, true, 0);
    /* DISC_SVC goes out at 30; its answer is due at 50. */
    bsim_run(&s, &b, 0, 40);
    CHECK_EQ(b.st, BLEU_ST_DISC_SVC);
    bsim_drop(&s, 40, 0x08);
    bsim_tick(&s, &b, 40);
    /* The host stack ends the step with ENOTCONN before the disconnect. */
    CHECK_EQ(b.st, BLEU_ST_WAIT);
    CHECK_EQ(b.err, BLEU_ERR_GATT);
    CHECK_EQ(b.err_status, BSIM_ST_NOTCONN);
    CHECK_EQ(b.reason, 0x08);
    bsim_run(&s, &b, 41, 40 + BLEU_RETRY_MS);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 1);
    bsim_tick(&s, &b, 40 + BLEU_RETRY_MS);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 2);
}

TEST_CASE(a_scan_during_a_pending_connect_cancels_it_and_the_connect_resumes)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    s.conn_ms = 3000;
    bsim_adv(&s, 50, &k_cap_addr, -50, k_cap_adv, sizeof k_cap_adv);
    (void)run_to_state(&s, &b, BLEU_ST_CONNECTING);
    bsim_run(&s, &b, 1, 100);
    CHECK(bleu_scan(&b, 100));
    bsim_tick(&s, &b, 100);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CANCEL), 1);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_SCAN), 0);
    CHECK_EQ(b.st, BLEU_ST_WAIT);
    /* The cancel's event at 102 lets the scan go. */
    bsim_run(&s, &b, 101, 103);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_SCAN), 1);
    CHECK_EQ(bsim_last(&s, BLEU_ACT_SCAN)->t, 102);
    CHECK_EQ(b.stray, 0);
    s.conn_ms = 30;
    bsim_run(&s, &b, 103, 102 + BLEU_SCAN_MS);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 1);
    CHECK_EQ(b.list.found_n, 1);
    /* The scan's end at 10,102 starts the connect in the same ms. */
    bsim_tick(&s, &b, 102 + BLEU_SCAN_MS);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_SCAN_STOP), 1);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 2);
    CHECK_EQ(bsim_last(&s, BLEU_ACT_CONNECT)->t, 102 + BLEU_SCAN_MS);
    bsim_run(&s, &b, 103 + BLEU_SCAN_MS, 103 + BLEU_SCAN_MS + 400);
    CHECK(bleu_can_send(&b));
}

TEST_CASE(a_scan_beside_a_link_keeps_it_and_beside_a_wait_holds_the_connect)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    bsim_stream(&s, k_cap_20, sizeof k_cap_20, NULL, 0);
    const uint32_t t = bsim_up(&s, &b, &k_cap_addr, 0);
    CHECK(bleu_scan(&b, t));
    bsim_run(&s, &b, t, t + BLEU_SCAN_MS + 10u);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_SCAN), 1);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_SCAN_STOP), 1);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_DISCONNECT), 0);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 1);
    CHECK(bleu_link_up(&b));
    CHECK(s.angles > 100u);

    /* Waiting for the retry: the connect waits for the scan's end. */
    bsim_init(&s);
    s.conn_status = BSIM_ST_REFUSED;
    (void)run_to_state(&s, &b, BLEU_ST_CONNECTING);
    bsim_run(&s, &b, 1, 40);
    CHECK_EQ(b.st, BLEU_ST_WAIT);
    CHECK(bleu_scan(&b, 40));
    bsim_run(&s, &b, 40, 40 + BLEU_SCAN_MS);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 1);
    bsim_tick(&s, &b, 40 + BLEU_SCAN_MS);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 2);
}

TEST_CASE(a_scan_that_does_not_start_or_is_stopped_ends_at_once)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    s.scan_fail = true;
    bleu_init(&b);
    CHECK(!bleu_scan(&b, 0));
    bleu_enable(&b, true, 0);
    CHECK(bleu_scan(&b, 0));
    bsim_run(&s, &b, 0, 3);
    CHECK(!b.scanning);
    CHECK_EQ(b.scan_status, 2);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_SCAN_STOP), 0);
    /* An end with an old token is not this scan's. */
    CHECK(bleu_scan(&b, 3));
    s.scan_fail = false;
    bsim_tick(&s, &b, 3);
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_SCAN_END;
    e.token = bsim_nth(&s, BLEU_ACT_SCAN, 0)->a.token;
    (void)bleu_event(&b, &e, 4);
    CHECK(b.scanning);
    CHECK_EQ(b.stray, 1);
    /* The caller's stop: one SCAN_STOP, the list kept. */
    bsim_adv(&s, 0, &k_cap_addr, -50, k_cap_adv, sizeof k_cap_adv);
    e.kind = BLEU_EV_ADV;
    e.addr = k_cap_addr;
    e.len = sizeof k_cap_adv;
    memcpy(e.data, k_cap_adv, sizeof k_cap_adv);
    (void)bleu_event(&b, &e, 5);
    bleu_scan_stop(&b, 6);
    bleu_scan_stop(&b, 6);
    bsim_tick(&s, &b, 6);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_SCAN_STOP), 1);
    CHECK_EQ(b.list.found_n, 1);
    /* Stopped before it went out: no SCAN_STOP. */
    bsim_init(&s);
    s.conn_ms = 3000;
    s.cancel_silent = true;
    (void)run_to_state(&s, &b, BLEU_ST_CONNECTING);
    CHECK(bleu_scan(&b, 1));
    bleu_scan_stop(&b, 1);
    bsim_run(&s, &b, 1, 3000);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_SCAN), 0);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_SCAN_STOP), 0);
}

TEST_CASE(off_in_every_state_ends_the_scan_the_connect_and_the_link)
{
    static const struct {
        uint8_t silent;
        bleu_state_t st;
        bool scan;
        uint8_t ends; /* the action OFF sends, NONE for none */
    } k[] = {
        { 0, BLEU_ST_IDLE, false, BLEU_ACT_NONE },
        { 0, BLEU_ST_IDLE, true, BLEU_ACT_SCAN_STOP },
        { 0, BLEU_ST_WAIT, false, BLEU_ACT_NONE },
        { 0, BLEU_ST_CONNECTING, false, BLEU_ACT_CANCEL },
        { BSIM_Q_SVC, BLEU_ST_DISC_SVC, false, BLEU_ACT_DISCONNECT },
        { BSIM_Q_CHR, BLEU_ST_DISC_CHR, false, BLEU_ACT_DISCONNECT },
        { BSIM_Q_DSC, BLEU_ST_DISC_DSC, false, BLEU_ACT_DISCONNECT },
        { BSIM_Q_MTU, BLEU_ST_MTU, false, BLEU_ACT_DISCONNECT },
        { BSIM_Q_SUB, BLEU_ST_SUBSCRIBE, false, BLEU_ACT_DISCONNECT },
        { 0, BLEU_ST_UP, false, BLEU_ACT_DISCONNECT },
        { 0, BLEU_ST_UP, true, BLEU_ACT_DISCONNECT },
        { 0, BLEU_ST_NOT_WIT, false, BLEU_ACT_NONE },
    };
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; ++i) {
        bsim_t s;
        bleu_t b;
        bsim_init(&s);
        s.silent = k[i].silent;
        uint32_t t = 0;
        if (k[i].st == BLEU_ST_IDLE) {
            bleu_init(&b);
            bleu_enable(&b, true, 0);
        } else {
            s.conn_status = k[i].st == BLEU_ST_WAIT ? BSIM_ST_REFUSED : 0u;
            s.no_svc = k[i].st == BLEU_ST_NOT_WIT;
            s.conn_ms = k[i].st == BLEU_ST_CONNECTING ? 3000u : 30u;
            t = run_to_state(&s, &b, k[i].st);
            /* NOT_WIT's own disconnect settles first. */
            bsim_run(&s, &b, t + 1u, t + 10u);
            t += 10u;
        }
        if (k[i].scan) {
            CHECK(bleu_scan(&b, t));
            bsim_tick(&s, &b, ++t);
            CHECK_EQ(bsim_count(&s, BLEU_ACT_SCAN), 1);
        }
        CHECK_EQ(b.st, k[i].st);
        static const uint8_t kinds[] = { BLEU_ACT_SCAN_STOP, BLEU_ACT_CANCEL,
                                         BLEU_ACT_DISCONNECT, BLEU_ACT_CONNECT,
                                         BLEU_ACT_SCAN };
        unsigned before[sizeof kinds];
        for (unsigned j = 0; j < sizeof kinds; ++j) {
            before[j] = bsim_count(&s, kinds[j]);
        }
        bleu_enable(&b, false, ++t);
        bleu_enable(&b, false, t);
        CHECK_EQ(b.st, BLEU_ST_OFF);
        CHECK(!b.scanning);
        CHECK(!bleu_link_up(&b));
        bsim_run(&s, &b, t, t + 20000u);
        for (unsigned j = 0; j < sizeof kinds; ++j) {
            const unsigned want = kinds[j] == BLEU_ACT_SCAN_STOP
                                      ? (k[i].scan ? 1u : 0u)
                                      : kinds[j] == k[i].ends ? 1u : 0u;
            CHECK_EQ(bsim_count(&s, kinds[j]) - before[j], want);
        }
        CHECK(!s.connected);
        CHECK(bleu_scan(&b, t + 20000u) == false);
        /* On again: a bound sensor connects and comes up. */
        bleu_enable(&b, true, t + 20000u);
        if (k[i].st != BLEU_ST_IDLE) {
            s.silent = 0;
            s.no_svc = false;
            s.conn_status = 0;
            s.conn_ms = 30;
            (void)bsim_until(&s, &b, t + 20000u, 1000, bsim_can_send);
            CHECK(bleu_can_send(&b));
        } else {
            CHECK_EQ(b.st, BLEU_ST_IDLE);
        }
        CHECK(bsim_writes_ok(&s));
    }
}

TEST_CASE(on_again_soon_after_off_waits_for_the_cancel_to_settle)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    s.conn_ms = 3000;
    s.cancel_silent = true;
    (void)run_to_state(&s, &b, BLEU_ST_CONNECTING);
    bleu_enable(&b, false, 100);
    bsim_tick(&s, &b, 100);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CANCEL), 1);
    /* The settle ends while OFF, at 2,100. */
    bsim_run(&s, &b, 101, 100 + BLEU_SETTLE_MS);
    CHECK(b.settle != 0u);
    bsim_tick(&s, &b, 100 + BLEU_SETTLE_MS);
    CHECK_EQ(b.settle, 0);
    /* On before the settle: the connect waits for it. */
    bsim_init(&s);
    s.conn_ms = 3000;
    s.cancel_silent = true;
    (void)run_to_state(&s, &b, BLEU_ST_CONNECTING);
    bleu_enable(&b, false, 100);
    bsim_tick(&s, &b, 100);
    bleu_enable(&b, true, 200);
    bsim_run(&s, &b, 200, 100 + BLEU_SETTLE_MS);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 1);
    bsim_tick(&s, &b, 100 + BLEU_SETTLE_MS);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 2);
}

TEST_CASE(a_late_answer_after_a_timeout_changes_nothing)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    s.silent = BSIM_Q_SVC;
    (void)run_to_state(&s, &b, BLEU_ST_DISC_SVC);
    const uint16_t tok = bsim_nth(&s, BLEU_ACT_DISC_SVC, 0)->a.token;
    bsim_run(&s, &b, 31, 30 + BLEU_GATT_MS + 1u);
    CHECK_EQ(b.st, BLEU_ST_WAIT);
    const uint32_t stray = b.stray;
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.conn = 1;
    e.token = tok;
    static const uint8_t kinds[] = { BLEU_EV_SVC, BLEU_EV_SVC_DONE,
                                     BLEU_EV_CHR, BLEU_EV_CHR_DONE,
                                     BLEU_EV_DSC, BLEU_EV_DSC_DONE,
                                     BLEU_EV_MTU, BLEU_EV_WRITE_DONE,
                                     BLEU_EV_READ, BLEU_EV_NONE };
    for (unsigned i = 0; i < sizeof kinds; ++i) {
        e.kind = kinds[i];
        e.h1 = H_SVC_START;
        e.h2 = H_SVC_END;
        CHECK(!bleu_event(&b, &e, 30 + BLEU_GATT_MS + 2u));
        CHECK_EQ(b.st, BLEU_ST_WAIT);
    }
    CHECK_EQ(b.stray, stray + sizeof kinds);
    CHECK_EQ(b.svc_start, 0);

    /* An answer to the step under way of another kind moves nothing. */
    bsim_init(&s);
    s.silent = BSIM_Q_SVC;
    (void)run_to_state(&s, &b, BLEU_ST_DISC_SVC);
    e.token = b.tok_link;
    for (unsigned i = 2; i < sizeof kinds; ++i) {
        e.kind = kinds[i];
        (void)bleu_event(&b, &e, 40);
        CHECK_EQ(b.st, BLEU_ST_DISC_SVC);
    }
    /* A second service in the answer keeps the first. */
    e.kind = BLEU_EV_SVC;
    (void)bleu_event(&b, &e, 40);
    e.h1 = 0x0030;
    e.h2 = 0x0040;
    (void)bleu_event(&b, &e, 40);
    CHECK_EQ(b.svc_start, H_SVC_START);
    CHECK_EQ(b.svc_end, H_SVC_END);
}

TEST_CASE(a_connect_that_comes_through_after_its_cancel_is_disconnected)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    s.absent = true;
    s.cancel_race = true;
    (void)run_to_state(&s, &b, BLEU_ST_CONNECTING);
    /* The timeout cancels at 5,000; the connection comes at 5,002. */
    bsim_run(&s, &b, 1, BLEU_CONNECT_MS + 3u);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CANCEL), 1);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_DISCONNECT), 1);
    const bleu_act_t *d = &bsim_last(&s, BLEU_ACT_DISCONNECT)->a;
    CHECK_EQ(d->conn, 1);
    CHECK_EQ(bsim_last(&s, BLEU_ACT_DISCONNECT)->t, BLEU_CONNECT_MS + 2u);
    CHECK_EQ(b.st, BLEU_ST_WAIT);
    CHECK_EQ(b.conn, 0);
    /* Its disconnect at 5,007 settles; the retry at 6,000 comes up. */
    s.absent = false;
    s.cancel_race = false;
    bsim_run(&s, &b, BLEU_CONNECT_MS + 3u, BLEU_CONNECT_MS + BLEU_RETRY_MS);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 1);
    CHECK_EQ(b.settle, 0);
    (void)bsim_until(&s, &b, BLEU_CONNECT_MS + BLEU_RETRY_MS, 500,
                     bsim_can_send);
    CHECK(bleu_can_send(&b));
    CHECK_EQ(b.conn, 2);

    /* One that comes with no settle pending is disconnected too. */
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_CONNECT;
    e.conn = 9;
    e.token = 0x7777;
    const uint32_t stray = b.stray;
    (void)bleu_event(&b, &e, 7000);
    CHECK_EQ(b.stray, stray + 1u);
    bleu_act_t a;
    CHECK(bleu_step(&b, 7000, &a));
    CHECK_EQ(a.kind, BLEU_ACT_DISCONNECT);
    CHECK_EQ(a.conn, 9);
    CHECK(bleu_link_up(&b));
    /* A failed one with an old token is only counted. */
    e.status = 2;
    (void)bleu_event(&b, &e, 7000);
    CHECK(!bleu_step(&b, 7000, &a));
}

TEST_CASE(binding_another_sensor_ends_the_link_and_connects_the_new_one)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    const uint32_t t = bsim_up(&s, &b, &k_cap_addr, 0);
    /* The same sensor again changes nothing. */
    bleu_bind(&b, &k_cap_addr, t);
    bsim_run(&s, &b, t, t + 100u);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_DISCONNECT), 0);
    CHECK(bleu_link_up(&b));

    const bleu_addr_t other = { { 1, 2, 3, 4, 5, 6 }, BLEU_ADDR_RANDOM };
    bleu_bind(&b, &other, t + 100u);
    CHECK_EQ(b.st, BLEU_ST_WAIT);
    bsim_tick(&s, &b, t + 100u);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_DISCONNECT), 1);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 1);
    /* The new connect waits for the old link's disconnect, 5ms. */
    bsim_run(&s, &b, t + 101u, t + 105u);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 1);
    bsim_tick(&s, &b, t + 105u);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 2);
    CHECK(bleu_addr_eq(&bsim_last(&s, BLEU_ACT_CONNECT)->a.addr, &other));
    CHECK(bleu_addr_eq(&b.addr, &other));
    CHECK_EQ(b.err, BLEU_ERR_NONE);

    /* Unbound: the link ends and nothing connects. */
    bsim_run(&s, &b, t + 106u, t + 400u);
    CHECK(bleu_link_up(&b));
    bleu_bind(&b, NULL, t + 400u);
    CHECK_EQ(b.st, BLEU_ST_IDLE);
    CHECK(!b.bound);
    bsim_run(&s, &b, t + 400u, t + 20000u);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_DISCONNECT), 2);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 2);

    /* Bound while OFF: kept, connected when on. */
    bleu_init(&b);
    bleu_bind(&b, &k_cap_addr, 0);
    CHECK_EQ(b.st, BLEU_ST_OFF);
    CHECK(b.bound);
    bleu_act_t a;
    CHECK(!bleu_step(&b, 0, &a));
    CHECK_EQ(a.kind, BLEU_ACT_NONE);
}

TEST_CASE(a_full_action_queue_counts_what_it_drops)
{
    bleu_t b;
    bleu_init(&b);
    bleu_enable(&b, true, 0);
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_CONNECT;
    for (unsigned i = 0; i < BLEU_ACT_QUEUE + 1u; ++i) {
        e.conn = (uint16_t)(i + 1u);
        e.token = (uint16_t)(0x100u + i);
        (void)bleu_event(&b, &e, 0);
    }
    CHECK_EQ(b.act_drops, 1);
    bleu_act_t a;
    unsigned n = 0;
    while (bleu_step(&b, 0, &a)) {
        CHECK_EQ(a.kind, BLEU_ACT_DISCONNECT);
        CHECK_EQ(a.conn, n + 1u);
        ++n;
    }
    CHECK_EQ(n, BLEU_ACT_QUEUE);
    /* A send while the queue is full is refused and changes nothing. */
    bsim_t s;
    bsim_init(&s);
    (void)bsim_up(&s, &b, &k_cap_addr, 0);
    for (unsigned i = 0; i < BLEU_ACT_QUEUE; ++i) {
        e.conn = (uint16_t)(i + 10u);
        (void)bleu_event(&b, &e, 200);
    }
    CHECK(!bleu_send(&b, k_cmd, sizeof k_cmd, 200));
    CHECK_EQ(b.wr, BLEU_WR_IDLE);
}

TEST_CASE(tokens_skip_0_across_their_wrap)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    const uint32_t t = bsim_up(&s, &b, &k_cap_addr, 0);
    b.token = 0xFFFF;
    CHECK(bleu_send(&b, k_cmd, sizeof k_cmd, t));
    bsim_run(&s, &b, t, t + 30u);
    CHECK_EQ(bsim_last(&s, BLEU_ACT_WRITE)->a.token, 1);
    CHECK_EQ(b.wr, BLEU_WR_OK);
    /* An answer with token 0 is never the step's. */
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_WRITE_DONE;
    const uint32_t stray = b.stray;
    (void)bleu_event(&b, &e, t + 30u);
    CHECK_EQ(b.stray, stray + 1u);
}

TEST_CASE(null_arguments_change_nothing)
{
    bleu_t b;
    bleu_init(NULL);
    bleu_enable(NULL, true, 0);
    bleu_bind(NULL, &k_cap_addr, 0);
    CHECK(!bleu_scan(NULL, 0));
    bleu_scan_stop(NULL, 0);
    CHECK(!bleu_send(NULL, k_cmd, sizeof k_cmd, 0));
    CHECK(!bleu_link_up(NULL));
    CHECK(!bleu_can_send(NULL));
    bleu_act_t a;
    memset(&a, 0x5A, sizeof a);
    CHECK(!bleu_step(NULL, 0, &a));
    CHECK_EQ(a.kind, BLEU_ACT_NONE);
    bleu_init(&b);
    bleu_bind(&b, &k_cap_addr, 0);
    bleu_enable(&b, true, 0);
    CHECK(!bleu_step(&b, 0, NULL));
    CHECK_EQ(b.q_n, 1);
    CHECK(!bleu_event(&b, NULL, 0));
    CHECK(!bleu_event(NULL, NULL, 0));
}

int main(void)
{
    RUN(a_command_goes_with_response_on_8841);
    RUN(write_no_response_only_writes_without_response);
    RUN(a_device_without_the_service_8841_notify_or_cccd_is_refused);
    RUN(a_failed_discovery_step_retries_after_the_gap);
    RUN(a_disconnect_in_every_state_retries_after_the_gap);
    RUN(a_disconnect_while_connecting_or_on_another_link_is_ignored);
    RUN(a_drop_during_a_step_ends_the_step_and_retries);
    RUN(a_scan_during_a_pending_connect_cancels_it_and_the_connect_resumes);
    RUN(a_scan_beside_a_link_keeps_it_and_beside_a_wait_holds_the_connect);
    RUN(a_scan_that_does_not_start_or_is_stopped_ends_at_once);
    RUN(off_in_every_state_ends_the_scan_the_connect_and_the_link);
    RUN(on_again_soon_after_off_waits_for_the_cancel_to_settle);
    RUN(a_late_answer_after_a_timeout_changes_nothing);
    RUN(a_connect_that_comes_through_after_its_cancel_is_disconnected);
    RUN(binding_another_sensor_ends_the_link_and_connects_the_new_one);
    RUN(a_full_action_queue_counts_what_it_drops);
    RUN(tokens_skip_0_across_their_wrap);
    RUN(null_arguments_change_nothing);
    return test_summary("bleuart_link");
}
