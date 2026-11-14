/*
 * The profile and the owner of protocols/bleuart against the scripted peer
 * of test/host/ble_sim.h.
 *
 * Under test: the tokens' owner in their top 3 bits and its count's wrap;
 * DISC_SVC carrying the profile's service; a profile with other UUIDs
 * found by role, and the ISSC profile refusing that table; a CMD the
 * profile marks optional, missing; the connect's parameters from the
 * profile; every rule of bleu_profile_valid() at its limit; bleu_setup()
 * refusing and keeping what it had; a connect answered BLEU_STATUS_BUSY
 * tried again after 1,000ms, not 999ms, with the error before it kept; a
 * scan that lists what its own profile advertises and not the ISSC
 * service; a PARAM read of 20 bytes kept whole.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "ble_capture.h"
#include "ble_sim.h"
#include "bleuart.h"

static const uint8_t k_cmd[5] = { 0xFF, 0xAA, 0x03, 0x06, 0x00 };

/* A peer of rcbench's own kind: service and three characteristics. */
static const uint8_t k_svc[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                   0x01, 0x00, 0x68, 0x63, 0x6E, 0x65, 0x62,
                                   0x63, 0x72 };
static const uint8_t k_stream[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
                                      0x01, 0x00, 0x68, 0x63, 0x6E, 0x65, 0x62,
                                      0x63, 0x72 };
static const uint8_t k_ctl[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04,
                                   0x01, 0x00, 0x68, 0x63, 0x6E, 0x65, 0x62,
                                   0x63, 0x72 };
static const uint8_t k_info[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
                                    0x01, 0x00, 0x68, 0x63, 0x6E, 0x65, 0x62,
                                    0x63, 0x72 };

static const bleu_profile_t k_node = {
    .svc = k_svc,
    .chr = {
        { .uuid = k_stream, .role = BLEU_ROLE_STREAM, .flags = 0u },
        { .uuid = k_ctl,    .role = BLEU_ROLE_CMD,    .flags = 0u },
        { .uuid = k_info,   .role = BLEU_ROLE_PARAM,  .flags = 0u },
    },
    .chr_n = 3u,
    .conn = { .itvl_min = 16u, .itvl_max = 24u, .latency = 0u,
              .supervision = 200u, .scan_itvl = 0x60u, .scan_window = 0x30u },
};

/* The sim's standard table with the node's UUIDs in place of 1E4D, 8841
 * and 6DAA. */
static void node_table(bsim_t *s)
{
    s->chr[0].uuid = k_stream;
    s->chr[1].uuid = k_ctl;
    s->chr[3].uuid = k_info;
}

/* Switch on with @p p and @p owner, bound, at @p t0; @return the tick a
 * command can go, or t0 + 2000. */
static uint32_t up_with(bsim_t *s, bleu_t *b, const bleu_profile_t *p,
                        uint8_t owner, uint32_t t0)
{
    bleu_init(b);
    CHECK(bleu_setup(b, p, owner));
    bleu_bind(b, &k_cap_addr, t0);
    bleu_enable(b, true, t0);
    return bsim_until(s, b, t0, 2000u, bsim_can_send);
}

TEST_CASE(tokens_carry_the_owner_in_their_top_3_bits)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    const uint32_t t = up_with(&s, &b, &bleu_profile_issc, 5u, 0);
    CHECK(bleu_can_send(&b));
    CHECK(s.nlog >= 7u);
    for (unsigned i = 0; i < s.nlog; ++i) {
        CHECK_EQ(s.log[i].a.token >> BLEU_OWNER_SHIFT, 5);
        CHECK(bleu_token_owner(s.log[i].a.token) == 5u);
        CHECK((s.log[i].a.token & BLEU_TOKEN_SEQ) != 0u);
    }
    /* The count wraps from 0x1FFF to 1 and keeps the owner. */
    b.token = (uint16_t)((5u << BLEU_OWNER_SHIFT) | BLEU_TOKEN_SEQ);
    CHECK(bleu_send(&b, k_cmd, sizeof k_cmd, t));
    bsim_run(&s, &b, t, t + 30u);
    CHECK_EQ(bsim_last(&s, BLEU_ACT_WRITE)->a.token,
             (5u << BLEU_OWNER_SHIFT) | 1u);
    CHECK_EQ(b.wr, BLEU_WR_OK);
    /* An answer with owner 4's token is not owner 5's step. */
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_WRITE_DONE;
    e.token = (uint16_t)((4u << BLEU_OWNER_SHIFT) | 1u);
    const uint32_t stray = b.stray;
    (void)bleu_event(&b, &e, t + 30u);
    CHECK_EQ(b.stray, stray + 1u);
    CHECK_EQ(bleu_token_owner(0xFFFFu), 7);
    CHECK_EQ(bleu_token_owner(0x1FFFu), 0);
}

TEST_CASE(owner_0_counts_as_before_the_owner_bits)
{
    bleu_t b;
    bleu_init(&b);
    CHECK_EQ(b.owner, 0);
    CHECK(b.prof == &bleu_profile_issc);
    bsim_t s;
    bsim_init(&s);
    (void)up_with(&s, &b, &bleu_profile_issc, 0u, 0);
    for (unsigned i = 0; i < s.nlog; ++i) {
        CHECK_EQ(s.log[i].a.token, i + 1u);
    }
}

TEST_CASE(disc_svc_asks_the_profiles_service)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    (void)up_with(&s, &b, &bleu_profile_issc, 0u, 0);
    const bleu_act_t *a = &bsim_nth(&s, BLEU_ACT_DISC_SVC, 0)->a;
    CHECK_EQ(a->len, 16);
    CHECK(memcmp(a->data, bleu_uuid_svc, 16) == 0);

    bsim_init(&s);
    node_table(&s);
    (void)up_with(&s, &b, &k_node, 2u, 0);
    a = &bsim_nth(&s, BLEU_ACT_DISC_SVC, 0)->a;
    CHECK_EQ(a->len, 16);
    CHECK(memcmp(a->data, k_svc, 16) == 0);
}

TEST_CASE(a_profile_finds_its_characteristics_by_role)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    node_table(&s);
    static const uint8_t bytes[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    static const uint8_t cuts[] = { 8 };
    bsim_stream(&s, bytes, sizeof bytes, cuts, sizeof cuts);
    const uint32_t t = up_with(&s, &b, &k_node, 2u, 0);
    CHECK(bleu_can_send(&b));
    CHECK_EQ(b.tx_val, H_TX_VAL);
    CHECK_EQ(b.cccd, H_TX_CCCD);
    CHECK_EQ(b.rx_val, H_RX_VAL);
    CHECK_EQ(b.param_val, H_PAR_VAL);
    CHECK_EQ(b.param_len, sizeof k_param);
    bsim_run(&s, &b, t, t + 100u);
    CHECK(b.notifies > 0u);
    CHECK_EQ(s.nrx, b.notifies * 8u);
    CHECK_EQ(s.rx[0], 1);
    CHECK(bleu_send(&b, k_cmd, sizeof k_cmd, t + 100u));
    bsim_run(&s, &b, t + 100u, t + 130u);
    CHECK_EQ(bsim_last(&s, BLEU_ACT_WRITE)->a.h1, H_RX_VAL);

    /* The ISSC profile finds none of its own in that table. */
    bsim_init(&s);
    node_table(&s);
    (void)up_with(&s, &b, &bleu_profile_issc, 0u, 0);
    CHECK_EQ(b.st, BLEU_ST_NOT_PEER);
    CHECK_EQ(b.err, BLEU_ERR_NOT_PEER);
}

TEST_CASE(an_optional_cmd_may_be_missing_a_required_one_may_not)
{
    bleu_profile_t p = k_node;
    p.chr[1].flags = BLEU_CHR_OPTIONAL;
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    node_table(&s);
    bsim_drop_chr(&s, k_ctl);
    bleu_init(&b);
    CHECK(bleu_setup(&b, &p, 1u));
    bleu_bind(&b, &k_cap_addr, 0);
    bleu_enable(&b, true, 0);
    (void)bsim_until(&s, &b, 0, 2000u, bsim_is_up);
    CHECK(bleu_link_up(&b));
    bsim_run(&s, &b, 200, 400);
    CHECK_EQ(b.param_len, sizeof k_param);
    CHECK_EQ(b.rx_val, 0);
    CHECK(!bleu_can_send(&b));
    CHECK(!bleu_send(&b, k_cmd, sizeof k_cmd, 400));

    /* Present without WRITE: dropped as well. */
    bsim_init(&s);
    node_table(&s);
    s.chr[1].props = BLEU_PROP_READ;
    (void)up_with(&s, &b, &p, 1u, 0);
    CHECK(bleu_link_up(&b));
    CHECK_EQ(b.rx_val, 0);
    CHECK(!bleu_can_send(&b));

    /* Required, the same table is not the peer. */
    bsim_init(&s);
    node_table(&s);
    bsim_drop_chr(&s, k_ctl);
    (void)up_with(&s, &b, &k_node, 1u, 0);
    CHECK_EQ(b.st, BLEU_ST_NOT_PEER);
}

TEST_CASE(a_profile_without_a_cmd_comes_up_and_takes_no_command)
{
    bleu_profile_t p = k_node;
    p.chr[1] = p.chr[2];
    p.chr_n = 2u;
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    node_table(&s);
    (void)up_with(&s, &b, &p, 1u, 0);
    CHECK(bleu_link_up(&b));
    CHECK_EQ(b.rx_val, 0);
    CHECK(!bleu_send(&b, k_cmd, sizeof k_cmd, 2000));
}

TEST_CASE(the_connect_asks_the_profiles_parameters)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    (void)up_with(&s, &b, &bleu_profile_issc, 0u, 0);
    const bleu_act_t *a = &bsim_nth(&s, BLEU_ACT_CONNECT, 0)->a;
    CHECK_EQ(a->cp.itvl_min, BLEU_ITVL_MIN);
    CHECK_EQ(a->cp.itvl_max, BLEU_ITVL_MAX);
    CHECK_EQ(a->cp.latency, BLEU_LATENCY);
    CHECK_EQ(a->cp.supervision, BLEU_SUPERVISION);
    CHECK_EQ(a->cp.scan_itvl, BLEU_CONN_SCAN_ITVL);
    CHECK_EQ(a->cp.scan_window, BLEU_CONN_SCAN_WINDOW);

    bsim_init(&s);
    node_table(&s);
    (void)up_with(&s, &b, &k_node, 3u, 0);
    a = &bsim_nth(&s, BLEU_ACT_CONNECT, 0)->a;
    CHECK(memcmp(&a->cp, &k_node.conn, sizeof a->cp) == 0);
}

TEST_CASE(profile_rules_hold_at_their_limits)
{
    CHECK(bleu_profile_valid(&bleu_profile_issc));
    CHECK(bleu_profile_valid(&k_node));
    CHECK(!bleu_profile_valid(NULL));
    bleu_profile_t p = k_node;
    p.svc = NULL;
    CHECK(!bleu_profile_valid(&p));
    p = k_node;
    p.chr_n = 0u;
    CHECK(!bleu_profile_valid(&p));
    p = k_node;
    p.chr[3] = (bleu_chr_spec_t){ .uuid = k_svc, .role = BLEU_ROLE_NONE };
    p.chr_n = 4u;
    CHECK(bleu_profile_valid(&p));
    p.chr_n = 5u;
    CHECK(!bleu_profile_valid(&p));
    p = k_node;
    p.chr[1].role = BLEU_ROLE_STREAM;
    CHECK(!bleu_profile_valid(&p));
    p = k_node;
    p.chr[0].role = BLEU_ROLE_NONE;
    CHECK(!bleu_profile_valid(&p));
    p = k_node;
    p.chr[2].role = BLEU_ROLE_CMD;
    CHECK(!bleu_profile_valid(&p));
    p = k_node;
    p.chr[1].role = BLEU_ROLE_PARAM;
    CHECK(!bleu_profile_valid(&p));
    p = k_node;
    p.chr[2].uuid = NULL;
    CHECK(!bleu_profile_valid(&p));
    p = k_node;
    p.chr[2].role = BLEU_ROLE_AUX + 1u;
    CHECK(!bleu_profile_valid(&p));
    /* One AUX at most. */
    p = k_node;
    p.chr[2].role = BLEU_ROLE_AUX;
    CHECK(bleu_profile_valid(&p));
    p.chr[1].role = BLEU_ROLE_AUX;
    CHECK(!bleu_profile_valid(&p));

    p = k_node;
    p.conn.itvl_min = 6u;
    CHECK(bleu_profile_valid(&p));
    p.conn.itvl_min = 5u;
    CHECK(!bleu_profile_valid(&p));
    p = k_node;
    p.conn.itvl_min = p.conn.itvl_max = 3200u;
    p.conn.supervision = 3200u;
    CHECK(bleu_profile_valid(&p));
    p.conn.itvl_max = 3201u;
    CHECK(!bleu_profile_valid(&p));
    p = k_node;
    p.conn.itvl_min = 25u;
    CHECK(!bleu_profile_valid(&p));
    p = k_node;
    p.conn.supervision = 3201u;
    CHECK(!bleu_profile_valid(&p));
    /* Twice (1 + latency) x 30ms: 60ms with latency 0, under the 100ms
     * floor; 120ms with latency 1, so 120ms is refused and 130ms taken. */
    p = k_node;
    p.conn.supervision = 9u;
    CHECK(!bleu_profile_valid(&p));
    p.conn.supervision = 10u;
    CHECK(bleu_profile_valid(&p));
    p.conn.latency = 1u;
    p.conn.supervision = 12u;
    CHECK(!bleu_profile_valid(&p));
    p.conn.supervision = 13u;
    CHECK(bleu_profile_valid(&p));
    p = k_node;
    p.conn.scan_window = p.conn.scan_itvl + 1u;
    CHECK(!bleu_profile_valid(&p));
    p = k_node;
    p.conn.scan_itvl = p.conn.scan_window = 4u;
    CHECK(bleu_profile_valid(&p));
    p.conn.scan_window = 3u;
    CHECK(!bleu_profile_valid(&p));
    p = k_node;
    p.conn.scan_itvl = 0x4001u;
    CHECK(!bleu_profile_valid(&p));
}

TEST_CASE(setup_refuses_and_keeps_what_it_had)
{
    bleu_t b;
    bleu_init(&b);
    CHECK(bleu_setup(&b, &k_node, 7u));
    bleu_profile_t bad = k_node;
    bad.chr_n = 0u;
    CHECK(!bleu_setup(&b, &bad, 1u));
    CHECK(!bleu_setup(&b, &k_node, BLEU_OWNER_MAX));
    CHECK(!bleu_setup(&b, NULL, 1u));
    CHECK(!bleu_setup(NULL, &k_node, 1u));
    CHECK(b.prof == &k_node);
    CHECK_EQ(b.owner, 7);
    /* A machine cleared without bleu_init() runs the ISSC profile. */
    memset(&b, 0, sizeof b);
    bsim_t s;
    bsim_init(&s);
    bleu_bind(&b, &k_cap_addr, 0);
    bleu_enable(&b, true, 0);
    (void)bsim_until(&s, &b, 0, 2000u, bsim_can_send);
    CHECK(bleu_can_send(&b));
}

/* The host stack's one connect is busy at t0 + 30: the retry comes after
 * BLEU_RETRY_MS, and the error of the failure before stays. */
TEST_CASE(a_busy_connect_retries_after_1000ms_not_999_and_keeps_the_error)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    s.conn_status = BSIM_ST_REFUSED;
    bleu_init(&b);
    bleu_bind(&b, &k_cap_addr, 0);
    bleu_enable(&b, true, 0);
    bsim_run(&s, &b, 0, 31);
    CHECK_EQ(b.err, BLEU_ERR_CONNECT);
    CHECK_EQ(b.err_status, BSIM_ST_REFUSED);
    /* The second try, at 7030, is answered busy at 7040. */
    s.absent = true;
    const uint32_t t1 = 30u + BLEU_RETRY_CONNECT_MS;
    bsim_run(&s, &b, 31, t1 + 10u);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 2);
    CHECK_EQ(b.st, BLEU_ST_CONNECTING);
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_CONNECT;
    e.token = b.tok_link;
    e.status = BLEU_STATUS_BUSY;
    (void)bleu_event(&b, &e, t1 + 10u);
    CHECK_EQ(b.st, BLEU_ST_WAIT);
    CHECK_EQ(b.err, BLEU_ERR_CONNECT);
    CHECK_EQ(b.err_status, BSIM_ST_REFUSED);
    CHECK_EQ(b.stray, 0);
    s.absent = false;
    s.conn_status = 0;
    bsim_run(&s, &b, t1 + 10u, t1 + 10u + BLEU_RETRY_MS);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 2);
    bsim_tick(&s, &b, t1 + 10u + BLEU_RETRY_MS);
    CHECK_EQ(bsim_count(&s, BLEU_ACT_CONNECT), 3);
    bsim_run(&s, &b, t1 + 11u + BLEU_RETRY_MS, t1 + 500u + BLEU_RETRY_MS);
    CHECK(bleu_link_up(&b));
    CHECK_EQ(b.err, BLEU_ERR_NONE);
}

TEST_CASE(a_busy_scan_end_ends_the_scan_with_its_status)
{
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    bleu_init(&b);
    bleu_enable(&b, true, 0);
    CHECK(bleu_scan(&b, 0));
    bsim_run(&s, &b, 0, 2);
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_SCAN_END;
    e.token = bsim_nth(&s, BLEU_ACT_SCAN, 0)->a.token;
    e.status = BLEU_STATUS_BUSY;
    (void)bleu_event(&b, &e, 2);
    CHECK(!b.scanning);
    CHECK_EQ(b.scan_status, BLEU_STATUS_BUSY);
}

/* One advertising report of @p addr's last byte with a 128-bit UUID list
 * holding @p uuid, or a 16-bit list holding 0x18F0 for NULL. */
static void adv(bleu_t *b, uint8_t last, const uint8_t *uuid, uint32_t now)
{
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_ADV;
    e.rssi = -60;
    e.addr.val[0] = last;
    e.addr.type = BLEU_ADDR_RANDOM;
    if (uuid != NULL) {
        e.data[0] = 17u;
        e.data[1] = 0x07u;
        memcpy(&e.data[2], uuid, 16u);
        e.len = 18u;
    } else {
        e.data[0] = 3u;
        e.data[1] = 0x03u;
        e.data[2] = 0xF0u;
        e.data[3] = 0x18u;
        e.len = 4u;
    }
    (void)bleu_event(b, &e, now);
}

TEST_CASE(a_scan_lists_what_its_own_profile_advertises)
{
    bleu_t b;
    bleu_init(&b);
    CHECK(bleu_setup(&b, &k_node, 1u));
    bleu_enable(&b, true, 0);
    CHECK(bleu_scan(&b, 0));
    adv(&b, 1u, bleu_uuid_svc, 1);
    adv(&b, 2u, NULL, 2);
    adv(&b, 3u, k_svc, 3);
    CHECK_EQ(b.list.found_n, 1);
    CHECK_EQ(b.list.found[0].addr.val[0], 3);
    CHECK_EQ(b.list.found[0].flags, BLEU_DEV_ISSC);
    CHECK_EQ(b.list.hidden, 2);

    /* The ISSC machine lists both of its own and not the node. */
    bleu_t i;
    bleu_init(&i);
    bleu_enable(&i, true, 0);
    CHECK(bleu_scan(&i, 0));
    adv(&i, 1u, bleu_uuid_svc, 1);
    adv(&i, 2u, NULL, 2);
    adv(&i, 3u, k_svc, 3);
    CHECK_EQ(i.list.found_n, 2);
    CHECK_EQ(i.list.hidden, 1);

    /* adv_svc in place of the service, and no 16-bit UUID. */
    bleu_profile_t p = k_node;
    p.adv_svc = bleu_uuid_svc;
    bleu_adv_t a;
    const uint8_t ad16[] = { 3u, 0x03u, 0xF0u, 0x18u };
    CHECK(!bleu_adv_parse_for(&p, ad16, sizeof ad16, &a));
    p.adv_uuid16 = 0x18F0u;
    CHECK(bleu_adv_parse_for(&p, ad16, sizeof ad16, &a));
    CHECK(a.has_18f0);
    CHECK(bleu_adv_parse_for(NULL, ad16, sizeof ad16, &a));
}

TEST_CASE(a_param_of_20_bytes_is_kept_whole)
{
    bleu_t b;
    bleu_init(&b);
    CHECK(bleu_setup(&b, &k_node, 1u));
    b.rd_busy = true;
    b.tok_link = 0x2005u;
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_READ;
    e.token = 0x2005u;
    e.len = 21u;
    for (unsigned k = 0; k < 21u; ++k) {
        e.data[k] = (uint8_t)(k + 1u);
    }
    (void)bleu_event(&b, &e, 0);
    CHECK_EQ(b.param_len, 20);
    CHECK_EQ(b.param[19], 20);
}

int main(void)
{
    RUN(tokens_carry_the_owner_in_their_top_3_bits);
    RUN(owner_0_counts_as_before_the_owner_bits);
    RUN(disc_svc_asks_the_profiles_service);
    RUN(a_profile_finds_its_characteristics_by_role);
    RUN(an_optional_cmd_may_be_missing_a_required_one_may_not);
    RUN(a_profile_without_a_cmd_comes_up_and_takes_no_command);
    RUN(the_connect_asks_the_profiles_parameters);
    RUN(profile_rules_hold_at_their_limits);
    RUN(setup_refuses_and_keeps_what_it_had);
    RUN(a_busy_connect_retries_after_1000ms_not_999_and_keeps_the_error);
    RUN(a_busy_scan_end_ends_the_scan_with_its_status);
    RUN(a_scan_lists_what_its_own_profile_advertises);
    RUN(a_param_of_20_bytes_is_kept_whole);
    return test_summary("bleuart_profile");
}
