/*
 * The BLE central of protocols/bleuart (bleuart.c) against the scripted
 * peer of test/host/ble_sim.h: the connect sequence, the stream and every
 * timeout.
 *
 * Under test: connect, the service, its characteristics, 1E4D's CCCD, the
 * MTU exchange, the subscribe and the 6DAA read in order and with their
 * handles; the owner's capture as notifications of 20, 4 and 1 bytes and a
 * frame across 3 notifications, decoded to its angles; notifications off
 * the link; the scan's 10,000ms, the connect's 5,000ms, the retry gap's
 * 1,000ms, each GATT step's 5,000ms, the write's 1,000ms and the settle's
 * 2,000ms at their limit and 1ms before it, at tick 0 and started 100ms
 * before the 32-bit ms wrap.  The write path, disconnects, scans during a
 * connect, OFF and late answers are in test_bleuart_link.c.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "ble_capture.h"
#include "ble_sim.h"
#include "bleuart.h"
#include "tick_wrap.h"
#include "witmotion.h"

/* The kind a case waits for in the log. */
static uint8_t g_kind;

/* Whether the log holds an action of g_kind. */
static bool logged(const bleu_t *b, const bsim_t *s)
{
    (void)b;
    return bsim_count(s, g_kind) > 0u;
}

/* Run until an action of @p kind is logged; @return its tick. */
static uint32_t run_to(bsim_t *s, bleu_t *b, uint32_t from, uint8_t kind)
{
    g_kind = kind;
    (void)bsim_until(s, b, from, 30000u, logged);
    const bsim_log_t *l = bsim_nth(s, kind, 0);
    CHECK(l != NULL);
    return l != NULL ? l->t : from;
}

TEST_CASE(a_bound_sensor_connects_discovers_subscribes_and_reads_6daa)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    const uint32_t t = bsim_up(&s, &b, &k_cap_addr, 0);
    CHECK_EQ(t, 150);
    CHECK(bleu_link_up(&b));
    CHECK(bleu_can_send(&b));
    CHECK_EQ(b.err, BLEU_ERR_NONE);

    static const uint8_t order[] = {
        BLEU_ACT_CONNECT, BLEU_ACT_DISC_SVC, BLEU_ACT_DISC_CHRS,
        BLEU_ACT_DISC_DSCS, BLEU_ACT_MTU, BLEU_ACT_SUBSCRIBE, BLEU_ACT_READ,
    };
    CHECK_EQ(s.nlog, sizeof order);
    for (unsigned i = 0; i < s.nlog && i < sizeof order; ++i) {
        CHECK_EQ(s.log[i].a.kind, order[i]);
        CHECK(s.log[i].a.token != 0u);
    }
    const bleu_act_t *c = &bsim_nth(&s, BLEU_ACT_CONNECT, 0)->a;
    CHECK(bleu_addr_eq(&c->addr, &k_cap_addr));
    CHECK_EQ(c->ms, BLEU_CONNECT_MS);
    const bleu_act_t *chr = &bsim_nth(&s, BLEU_ACT_DISC_CHRS, 0)->a;
    CHECK_EQ(chr->h1, H_SVC_START);
    CHECK_EQ(chr->h2, H_SVC_END);
    CHECK_EQ(chr->conn, 1);
    /* 1E4D's descriptors run from its value handle to 8841's declaration. */
    const bleu_act_t *dsc = &bsim_nth(&s, BLEU_ACT_DISC_DSCS, 0)->a;
    CHECK_EQ(dsc->h1, H_TX_VAL);
    CHECK_EQ(dsc->h2, H_RX_DEF - 1u);
    const bleu_act_t *sub = &bsim_nth(&s, BLEU_ACT_SUBSCRIBE, 0)->a;
    CHECK_EQ(sub->h1, H_TX_CCCD);
    CHECK_EQ(sub->len, 2);
    CHECK_EQ(sub->data[0], 0x01);
    CHECK_EQ(sub->data[1], 0x00);
    CHECK_EQ(bsim_nth(&s, BLEU_ACT_READ, 0)->a.h1, H_PAR_VAL);
    CHECK(bsim_writes_ok(&s));

    CHECK_EQ(b.conn, 1);
    CHECK_EQ(b.tx_val, H_TX_VAL);
    CHECK_EQ(b.cccd, H_TX_CCCD);
    CHECK_EQ(b.rx_val, H_RX_VAL);
    CHECK_EQ(b.rx_props, BLEU_PROP_WRITE | BLEU_PROP_WRITE_NR);
    CHECK_EQ(b.param_val, H_PAR_VAL);
    CHECK_EQ(b.mtu, 23);
    CHECK_EQ(b.itvl, 40);
    CHECK_EQ(b.latency, 0);
    CHECK_EQ(b.supervision, 400);
    CHECK_EQ(b.param_len, 8);
    CHECK(memcmp(b.param, k_param, 8) == 0);
    CHECK_EQ(b.param_status, 0);
    CHECK_EQ(b.t_connect, 30);
    CHECK_EQ(b.t_setup, 100);
    CHECK_EQ(b.attempts, 1);
    CHECK_EQ(b.stray, 0);

    /* Nothing more goes out while the link holds. */
    bsim_run(&s, &b, t + 1u, t + 20000u);
    CHECK_EQ(s.nlog, sizeof order);
    CHECK(bleu_link_up(&b));
}

/* Run the capture cut at @p cuts and hold the bytes and the angles. */
static void capture_cut_at(const uint8_t *cuts, size_t ncuts)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    bsim_stream(&s, k_cap_20, sizeof k_cap_20, cuts, ncuts);
    (void)bsim_up(&s, &b, &k_cap_addr, 0);
    bsim_run(&s, &b, 151, 3000);
    CHECK(s.nrx >= 3u * sizeof k_cap_20);
    for (size_t i = 0; i < s.nrx; ++i) {
        CHECK_EQ(s.rx[i], k_cap_20[i % sizeof k_cap_20]);
    }
    CHECK_EQ(b.notifies, s.notifies_sent);
    CHECK(s.angles >= 3u);
    CHECK_NEAR(s.angle.roll, CAP_ROLL, CAP_TOL);
    CHECK_NEAR(s.angle.pitch, CAP_PITCH, CAP_TOL);
    CHECK_NEAR(s.angle.yaw, CAP_YAW, CAP_TOL);
    CHECK_EQ(s.angle.version, CAP_VERSION);
}

TEST_CASE(the_capture_reaches_the_parser_as_20_4_and_1_byte_notifications)
{
    static const uint8_t c20[] = { 20 }, c4[] = { 4 }, c1[] = { 1 };
    static const uint8_t mixed[] = { 20, 4, 1, 4, 3, 7 };
    capture_cut_at(c20, 1);
    capture_cut_at(c4, 1);
    capture_cut_at(c1, 1);
    capture_cut_at(mixed, sizeof mixed);
}

/* A notification of @p n bytes on the link's 1E4D. */
static bleu_evt_t notify(const bleu_t *b, const uint8_t *d, size_t n)
{
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_NOTIFY;
    e.conn = b->conn;
    e.h1 = b->tx_val;
    e.len = (uint8_t)n;
    memcpy(e.data, d, n);
    return e;
}

TEST_CASE(a_frame_split_across_three_notifications_decodes)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    const uint32_t t = bsim_up(&s, &b, &k_cap_addr, 0);
    const uint8_t *f = &k_cap_20[CAP_ANGLE_AT];
    const bleu_evt_t parts[] = { notify(&b, f, 3), notify(&b, f + 3, 4),
                                 notify(&b, f + 7, 4) };
    for (unsigned i = 0; i < 3u; ++i) {
        bsim_feed(&s, &b, &parts[i], t);
    }
    CHECK_EQ(s.angles, 1);
    CHECK_NEAR(s.angle.roll, CAP_ROLL, CAP_TOL);
    CHECK_NEAR(s.angle.pitch, CAP_PITCH, CAP_TOL);
    CHECK_NEAR(s.angle.yaw, CAP_YAW, CAP_TOL);
    /* The two tails the phone saw are bytes the parser drops. */
    const bleu_evt_t ta = notify(&b, k_cap_tail_a, 4);
    const bleu_evt_t tb = notify(&b, k_cap_tail_b, 4);
    CHECK(bleu_event(&b, &ta, t));
    CHECK(bleu_event(&b, &tb, t));
    CHECK_EQ(b.notifies, 5);
}

TEST_CASE(notifications_off_the_link_are_not_stream_bytes)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    const uint32_t t = bsim_up(&s, &b, &k_cap_addr, 0);
    bleu_evt_t e = notify(&b, k_cap_20, 20);
    e.h1 = H_AIR_VAL;
    CHECK(!bleu_event(&b, &e, t));
    e = notify(&b, k_cap_20, 20);
    e.conn = 2;
    CHECK(!bleu_event(&b, &e, t));
    e = notify(&b, k_cap_20, 20);
    e.len = BLEU_EVT_DATA + 1u;
    CHECK(!bleu_event(&b, &e, t));
    e.len = BLEU_EVT_DATA;
    CHECK(bleu_event(&b, &e, t));
    CHECK_EQ(b.stray, 3);
    CHECK_EQ(b.notifies, 1);

    /* Before 1E4D is known, and with no link, nothing is stream. */
    bsim_t s2;
    bleu_t b2;
    bsim_init(&s2);
    s2.silent = BSIM_Q_CHR;
    s2.stream = k_cap_20;
    s2.stream_n = sizeof k_cap_20;
    s2.notify_unsub = true;
    bleu_init(&b2);
    bleu_bind(&b2, &k_cap_addr, 0);
    bleu_enable(&b2, true, 0);
    bsim_run(&s2, &b2, 0, 1000);
    CHECK_EQ(b2.st, BLEU_ST_DISC_CHR);
    CHECK(s2.notifies_sent > 0u);
    CHECK_EQ(b2.notifies, 0);
    CHECK_EQ(s2.nrx, 0);
    /* Handle 0 is no 1E4D while 1E4D is not known. */
    e = notify(&b2, k_cap_20, 20);
    CHECK_EQ(e.h1, 0);
    CHECK(!bleu_event(&b2, &e, 1000));
    bleu_enable(&b2, false, 1000);
    e = notify(&b, k_cap_20, 20);
    CHECK(!bleu_event(&b2, &e, 1000));

    /* 1E4D known before its CCCD is written: the stream is taken. */
    bsim_t s3;
    bleu_t b3;
    bsim_init(&s3);
    s3.silent = BSIM_Q_SUB;
    bsim_stream(&s3, k_cap_20, sizeof k_cap_20, NULL, 0);
    s3.notify_unsub = true;
    bleu_init(&b3);
    bleu_bind(&b3, &k_cap_addr, 0);
    bleu_enable(&b3, true, 0);
    bsim_run(&s3, &b3, 0, 1000);
    CHECK_EQ(b3.st, BLEU_ST_SUBSCRIBE);
    CHECK(s3.angles > 0u);
}

/* The scan's start tick in scan_case(). */
static void scan_ends_at_its_limit(uint32_t t0)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    bsim_adv(&s, 40, &k_cap_addr, -50, k_cap_adv, sizeof k_cap_adv);
    bleu_init(&b);
    bleu_enable(&b, true, t0);
    CHECK_EQ(b.st, BLEU_ST_IDLE);
    CHECK(bleu_scan(&b, t0));
    CHECK(!bleu_scan(&b, t0));
    bsim_tick(&s, &b, t0);
    const bsim_log_t *l = bsim_nth(&s, BLEU_ACT_SCAN, 0);
    CHECK(l != NULL);
    if (l == NULL) {
        return;
    }
    CHECK_EQ(l->t, t0);
    CHECK_EQ(l->a.ms, BLEU_SCAN_MS);
    bsim_run(&s, &b, t0 + 1u, t0 + BLEU_SCAN_MS);
    CHECK(b.scanning);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_SCAN_STOP), 0);
    bsim_tick(&s, &b, t0 + BLEU_SCAN_MS);
    CHECK(!b.scanning);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_SCAN_STOP), 1);
    CHECK_EQ(bsim_last(&s, BLEU_ACT_SCAN_STOP)->t, t0 + BLEU_SCAN_MS);
    CHECK_EQ(b.list.found_n, 1);
    CHECK_STR_EQ(b.list.found[0].name, "HC-06");
    CHECK_EQ(b.scan_status, 0);
    /* A report after the scan is not taken. */
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_ADV;
    e.addr = k_cap_addr;
    CHECK(!bleu_event(&b, &e, t0 + BLEU_SCAN_MS + 1u));
    CHECK_EQ(b.list.reports, 1);
    /* A second scan clears the list. */
    CHECK(bleu_scan(&b, t0 + BLEU_SCAN_MS + 1u));
    CHECK_EQ(b.list.found_n, 0);
}

TEST_CASE(a_scan_ends_at_10000ms_not_at_9999)
{
    at_tick_0_and_before_the_wrap(scan_ends_at_its_limit, 100);
}

TEST_CASE(a_scan_lists_the_owners_sensor_and_hides_the_rest)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    const uint8_t plain[] = { 0x02, 0x01, 0x06 };
    const uint8_t rsp[] = { 0x06, 0x09, 'H', 'C', '-', '0', '6' };
    const uint8_t adv[] = { 0x02, 0x01, 0x1A, 0x03, 0x03, 0xF0, 0x18 };
    const bleu_addr_t other = { { 1, 2, 3, 4, 5, 6 }, BLEU_ADDR_RANDOM };
    /* The sensor's advertisement, then its scan response with the name. */
    bsim_adv(&s, 100, &k_cap_addr, -52, adv, sizeof adv);
    bsim_adv(&s, 101, &k_cap_addr, -50, rsp, sizeof rsp);
    bsim_adv(&s, 300, &other, -40, plain, sizeof plain);
    bleu_init(&b);
    bleu_enable(&b, true, 0);
    CHECK(bleu_scan(&b, 0));
    bsim_run(&s, &b, 0, BLEU_SCAN_MS + 1u);
    CHECK_EQ(b.list.found_n, 1);
    CHECK_STR_EQ(b.list.found[0].name, "HC-06");
    CHECK_EQ(b.list.found[0].rssi, -50);
    CHECK_EQ(b.list.hidden, 1);
    char text[BLEU_ADDR_TEXT_MAX];
    CHECK_EQ(bleu_addr_format(&b.list.found[0].addr, true, text, sizeof text),
             19);
    CHECK_STR_EQ(text, "00:0C:BF:08:19:29 P");
    /* Nothing was bound: no connect. */
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 0);
}

static void connect_times_out(uint32_t t0)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    s.absent = true;
    bleu_init(&b);
    bleu_bind(&b, &k_cap_addr, t0);
    bleu_enable(&b, true, t0);
    bsim_tick(&s, &b, t0);
    CHECK_EQ(bsim_nth(&s, BLEU_ACT_CONNECT, 0)->t, t0);
    bsim_run(&s, &b, t0 + 1u, t0 + BLEU_CONNECT_MS);
    CHECK_EQ(b.st, BLEU_ST_CONNECTING);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CANCEL), 0);
    bsim_tick(&s, &b, t0 + BLEU_CONNECT_MS);
    CHECK_EQ(b.st, BLEU_ST_WAIT);
    CHECK_EQ(b.err, BLEU_ERR_CONNECT);
    CHECK_EQ(b.err_status, 0);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CANCEL), 1);
    CHECK_EQ(bsim_last(&s, BLEU_ACT_CANCEL)->t, t0 + BLEU_CONNECT_MS);
    /* The cancel's event comes 2ms later; the next attempt goes after the
     * gap, counted from the timeout. */
    const uint32_t tf = t0 + BLEU_CONNECT_MS;
    bsim_run(&s, &b, tf + 1u, tf + BLEU_RETRY_MS);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 1);
    CHECK_EQ(b.stray, 0);
    bsim_tick(&s, &b, tf + BLEU_RETRY_MS);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 2);
    CHECK_EQ(b.attempts, 2);
}

TEST_CASE(a_connect_times_out_at_5000ms_not_at_4999)
{
    at_tick_0_and_before_the_wrap(connect_times_out, 100);
}

/* The connect fails with HCI 0x3E at t0 + 30, as a CONNECT event or, with
 * @p lost, as NimBLE's DISCONNECT with the connect's token. */
static void retry_gap_after_a_failure(uint32_t t0, bool lost)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    s.conn_status = BSIM_ST_REFUSED;
    s.conn_lost = lost;
    bleu_init(&b);
    bleu_bind(&b, &k_cap_addr, t0);
    bleu_enable(&b, true, t0);
    bsim_run(&s, &b, t0, t0 + 31u);
    CHECK_EQ(b.st, BLEU_ST_WAIT);
    CHECK_EQ(b.err, BLEU_ERR_CONNECT);
    CHECK_EQ(b.err_status, BSIM_ST_REFUSED);
    CHECK_EQ(b.stray, 0);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CANCEL), 0);
    const uint32_t tf = t0 + 30u;
    bsim_run(&s, &b, t0 + 31u, tf + BLEU_RETRY_CONNECT_MS);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 1);
    /* Accepted the next time, the error clears at UP. */
    s.conn_status = 0;
    bsim_tick(&s, &b, tf + BLEU_RETRY_CONNECT_MS);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 2);
    CHECK_EQ(bsim_last(&s, BLEU_ACT_CONNECT)->t, tf + BLEU_RETRY_CONNECT_MS);
    CHECK_EQ(b.err, BLEU_ERR_CONNECT);
    bsim_run(&s, &b, tf + BLEU_RETRY_CONNECT_MS + 1u,
             tf + BLEU_RETRY_CONNECT_MS + 500u);
    CHECK(bleu_link_up(&b));
    CHECK_EQ(b.err, BLEU_ERR_NONE);
}

static void retry_gap_after_a_refusal(uint32_t t0)
{
    retry_gap_after_a_failure(t0, false);
}

static void retry_gap_after_a_lost_establishment(uint32_t t0)
{
    retry_gap_after_a_failure(t0, true);
}

TEST_CASE(a_failed_connect_retries_after_7000ms_not_6999)
{
    at_tick_0_and_before_the_wrap(retry_gap_after_a_refusal, 100);
}

TEST_CASE(a_connection_that_failed_to_be_established_retries_after_7000ms)
{
    at_tick_0_and_before_the_wrap(retry_gap_after_a_lost_establishment, 100);
}

/* A DISCONNECT with a token that is not the connect's in flight is a
 * stray: the connect goes on to its own event. */
static void foreign_disconnect(uint32_t t0)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    s.conn_ms = 50u;
    bleu_init(&b);
    bleu_bind(&b, &k_cap_addr, t0);
    bleu_enable(&b, true, t0);
    bsim_run(&s, &b, t0, t0 + 10u);
    CHECK_EQ(b.st, BLEU_ST_CONNECTING);
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_DISCONNECT;
    e.token = (uint16_t)(b.tok_link + 1u);
    e.status = BSIM_ST_REFUSED;
    (void)bleu_event(&b, &e, t0 + 10u);
    CHECK_EQ(b.st, BLEU_ST_CONNECTING);
    CHECK_EQ(b.stray, 1);
    bsim_run(&s, &b, t0 + 11u, t0 + 600u);
    CHECK(bleu_link_up(&b));
}

TEST_CASE(a_disconnect_with_another_token_leaves_the_connect_running)
{
    at_tick_0_and_before_the_wrap(foreign_disconnect, 100);
}

/* The step held silent in gatt_step_times_out(). */
static struct {
    uint8_t q;
    uint8_t kind;
    bleu_state_t st;
} g_step;

static void gatt_step_times_out(uint32_t t0)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    s.silent = g_step.q;
    bleu_init(&b);
    bleu_bind(&b, &k_cap_addr, t0);
    bleu_enable(&b, true, t0);
    const uint32_t ta = run_to(&s, &b, t0, g_step.kind);
    bsim_run(&s, &b, ta + 1u, ta + BLEU_GATT_MS);
    CHECK_EQ(b.st, g_step.st);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_DISCONNECT), 0);
    bsim_tick(&s, &b, ta + BLEU_GATT_MS);
    CHECK_EQ(b.st, BLEU_ST_WAIT);
    CHECK_EQ(b.err, BLEU_ERR_GATT);
    CHECK_EQ(b.err_status, 0);
    CHECK(!bleu_link_up(&b));
    CHECK_EQ(bsim_count(&s, BLEU_ACT_DISCONNECT), 1);
    CHECK_EQ(bsim_last(&s, BLEU_ACT_DISCONNECT)->a.conn, 1);
    /* The disconnect settles 5ms later; the retry goes after the gap. */
    const uint32_t tf = ta + BLEU_GATT_MS;
    bsim_run(&s, &b, tf + 1u, tf + BLEU_RETRY_MS);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 1);
    CHECK_EQ(b.reason, 0x16);
    bsim_tick(&s, &b, tf + BLEU_RETRY_MS);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 2);
    CHECK(bsim_writes_ok(&s));
}

TEST_CASE(each_gatt_step_times_out_at_5000ms_not_at_4999)
{
    static const struct {
        uint8_t q;
        uint8_t kind;
        bleu_state_t st;
    } k[] = {
        { BSIM_Q_SVC, BLEU_ACT_DISC_SVC, BLEU_ST_DISC_SVC },
        { BSIM_Q_CHR, BLEU_ACT_DISC_CHRS, BLEU_ST_DISC_CHR },
        { BSIM_Q_DSC, BLEU_ACT_DISC_DSCS, BLEU_ST_DISC_DSC },
        { BSIM_Q_MTU, BLEU_ACT_MTU, BLEU_ST_MTU },
        { BSIM_Q_SUB, BLEU_ACT_SUBSCRIBE, BLEU_ST_SUBSCRIBE },
        { BSIM_Q_READ, BLEU_ACT_READ, BLEU_ST_UP },
    };
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; ++i) {
        g_step.q = k[i].q;
        g_step.kind = k[i].kind;
        g_step.st = k[i].st;
        at_tick_0_and_before_the_wrap(gatt_step_times_out, 100);
    }
}

static void write_times_out(uint32_t t0)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    s.silent = BSIM_Q_WRITE;
    const uint32_t tw = bsim_up(&s, &b, &k_cap_addr, t0) + 1u;
    const uint8_t cmd[5] = { 0xFF, 0xAA, 0x03, 0x06, 0x00 };
    CHECK(bleu_send(&b, cmd, sizeof cmd, tw));
    CHECK(!bleu_can_send(&b));
    bsim_tick(&s, &b, tw);
    bsim_run(&s, &b, tw + 1u, tw + BLEU_WRITE_MS);
    CHECK_EQ(b.wr, BLEU_WR_BUSY);
    bsim_tick(&s, &b, tw + BLEU_WRITE_MS);
    CHECK_EQ(b.wr, BLEU_WR_FAILED);
    CHECK_EQ(b.wr_status, BLEU_STATUS_TIMEOUT);
    /* The link stays and takes the next command. */
    CHECK(bleu_link_up(&b));
    CHECK(bleu_can_send(&b));
    /* The late answer changes nothing. */
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_WRITE_DONE;
    e.conn = b.conn;
    e.token = bsim_last(&s, BLEU_ACT_WRITE)->a.token;
    const uint32_t stray = b.stray;
    CHECK(!bleu_event(&b, &e, tw + BLEU_WRITE_MS + 1u));
    CHECK_EQ(b.wr, BLEU_WR_FAILED);
    CHECK_EQ(b.stray, stray + 1u);
}

TEST_CASE(a_write_answer_times_out_at_1000ms_not_at_999)
{
    /* The bring-up takes 151ms: the write starts 100ms before the wrap. */
    at_tick_0_and_before_the_wrap(write_times_out, 251);
}

static void cancel_settles(uint32_t t0)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    s.conn_ms = 3000;
    s.cancel_silent = true;
    bleu_init(&b);
    bleu_bind(&b, &k_cap_addr, t0);
    bleu_enable(&b, true, t0);
    bsim_tick(&s, &b, t0);
    CHECK(bleu_scan(&b, t0 + 1u));
    bsim_tick(&s, &b, t0 + 1u);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CANCEL), 1);
    CHECK_EQ(b.st, BLEU_ST_WAIT);
    /* No event for the cancel: the scan waits BLEU_SETTLE_MS. */
    const uint32_t tc = t0 + 1u;
    bsim_run(&s, &b, tc + 1u, tc + BLEU_SETTLE_MS);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_SCAN), 0);
    bsim_tick(&s, &b, tc + BLEU_SETTLE_MS);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_SCAN), 1);
    CHECK_EQ(bsim_last(&s, BLEU_ACT_SCAN)->t, tc + BLEU_SETTLE_MS);
}

TEST_CASE(a_cancel_without_its_event_settles_after_2000ms)
{
    at_tick_0_and_before_the_wrap(cancel_settles, 100);
}

static void close_settles(uint32_t t0)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    s.silent = BSIM_Q_SVC;
    bleu_init(&b);
    bleu_bind(&b, &k_cap_addr, t0);
    bleu_enable(&b, true, t0);
    const uint32_t ta = run_to(&s, &b, t0, BLEU_ACT_DISC_SVC);
    /* The sim's link vanishes without a disconnect event. */
    s.connected = false;
    bsim_run(&s, &b, ta + 1u, ta + BLEU_GATT_MS + 1u);
    const uint32_t tf = ta + BLEU_GATT_MS;
    CHECK_EQ(bsim_count(&s, BLEU_ACT_DISCONNECT), 1);
    /* The gap ends at tf + 1000, the settle at tf + 2000. */
    bsim_run(&s, &b, tf + 1u, tf + BLEU_SETTLE_MS);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 1);
    bsim_tick(&s, &b, tf + BLEU_SETTLE_MS);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 2);
}

TEST_CASE(a_disconnect_without_its_event_settles_after_2000ms)
{
    at_tick_0_and_before_the_wrap(close_settles, 100);
}

TEST_CASE(a_table_with_1e4d_last_finds_its_cccd_at_the_services_end)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    bsim_table_tx_last(&s);
    const uint32_t t = bsim_up(&s, &b, &k_cap_addr, 0);
    CHECK(bleu_can_send(&b));
    const bleu_act_t *dsc = &bsim_nth(&s, BLEU_ACT_DISC_DSCS, 0)->a;
    CHECK_EQ(dsc->h1, 0x0019);
    CHECK_EQ(dsc->h2, H_SVC_END);
    CHECK_EQ(b.cccd, 0x001A);
    CHECK_EQ(b.rx_props, BLEU_PROP_WRITE);
    CHECK_EQ(b.param_val, 0x0017);
    CHECK(bsim_writes_ok(&s));
    /* ACA3's CCCD before 1E4D is not taken for 1E4D's. */
    CHECK(bsim_nth(&s, BLEU_ACT_SUBSCRIBE, 0)->a.h1 != 0x0013u);
    (void)t;
}

TEST_CASE(a_failed_mtu_exchange_and_a_failed_6daa_read_go_on)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    s.fail = BSIM_Q_MTU | BSIM_Q_READ;
    s.fail_status = 0x0106;
    const uint32_t t = bsim_up(&s, &b, &k_cap_addr, 0);
    CHECK(bleu_can_send(&b));
    CHECK_EQ(b.mtu, 0);
    CHECK_EQ(b.param_status, 0x0106);
    CHECK_EQ(b.param_len, 0);
    CHECK_EQ(b.err, BLEU_ERR_NONE);
    (void)t;

    /* An MTU of 247 answered is kept. */
    bsim_init(&s);
    s.mtu = 247;
    (void)bsim_up(&s, &b, &k_cap_addr, 0);
    CHECK_EQ(b.mtu, 247);
    CHECK_EQ(bsim_nth(&s, BLEU_ACT_MTU, 0)->a.h1, 0);
}

TEST_CASE(a_6daa_without_read_is_not_read_and_the_link_takes_commands_at_once)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    bsim_chr(&s, bleu_uuid_param)->props = BLEU_PROP_WRITE;
    bleu_init(&b);
    bleu_bind(&b, &k_cap_addr, 0);
    bleu_enable(&b, true, 0);
    const uint32_t t = bsim_until(&s, &b, 0, 2000, bsim_is_up);
    CHECK_EQ(t, 130);
    CHECK(bleu_can_send(&b));
    CHECK_EQ(b.param_val, 0);
    bsim_run(&s, &b, t + 1u, t + 1000u);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_READ), 0);

    /* Without 6DAA at all, the same. */
    bsim_init(&s);
    bsim_drop_chr(&s, bleu_uuid_param);
    bleu_init(&b);
    bleu_bind(&b, &k_cap_addr, 0);
    bleu_enable(&b, true, 0);
    CHECK_EQ(bsim_until(&s, &b, 0, 2000, bsim_can_send), 130);
}

TEST_CASE(a_parameter_update_is_kept)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    const uint32_t t = bsim_up(&s, &b, &k_cap_addr, 0);
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_CONN_UPDATE;
    e.conn = b.conn;
    e.h1 = 6;
    e.h2 = 1;
    e.h3 = 200;
    CHECK(!bleu_event(&b, &e, t));
    CHECK_EQ(b.itvl, 6);
    CHECK_EQ(b.latency, 1);
    CHECK_EQ(b.supervision, 200);
    /* On another connection it is not. */
    e.conn = 9;
    e.h1 = 24;
    (void)bleu_event(&b, &e, t);
    CHECK_EQ(b.itvl, 6);
    CHECK_EQ(b.stray, 1);
}

int main(void)
{
    RUN(a_bound_sensor_connects_discovers_subscribes_and_reads_6daa);
    RUN(the_capture_reaches_the_parser_as_20_4_and_1_byte_notifications);
    RUN(a_frame_split_across_three_notifications_decodes);
    RUN(notifications_off_the_link_are_not_stream_bytes);
    RUN(a_scan_ends_at_10000ms_not_at_9999);
    RUN(a_scan_lists_the_owners_sensor_and_hides_the_rest);
    RUN(a_connect_times_out_at_5000ms_not_at_4999);
    RUN(a_failed_connect_retries_after_7000ms_not_6999);
    RUN(a_connection_that_failed_to_be_established_retries_after_7000ms);
    RUN(a_disconnect_with_another_token_leaves_the_connect_running);
    RUN(each_gatt_step_times_out_at_5000ms_not_at_4999);
    RUN(a_write_answer_times_out_at_1000ms_not_at_999);
    RUN(a_cancel_without_its_event_settles_after_2000ms);
    RUN(a_disconnect_without_its_event_settles_after_2000ms);
    RUN(a_table_with_1e4d_last_finds_its_cccd_at_the_services_end);
    RUN(a_failed_mtu_exchange_and_a_failed_6daa_read_go_on);
    RUN(a_6daa_without_read_is_not_read_and_the_link_takes_commands_at_once);
    RUN(a_parameter_update_is_kept);
    return test_summary("bleuart");
}
