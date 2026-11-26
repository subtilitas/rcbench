/**
 * @file test_bleuart_ext.c
 * @brief protocols/bleuart's long write, 16-bit characteristic UUIDs, the
 *        scan by address prefix and the hub's sixth client, against the
 *        scripted peer of test/host/ble_sim.h and the hub rig.
 *
 * Under test: a BLEU_ACT_WRITE_EXT answered, timed out at 1,000ms and not
 * 999ms, lost with the link (BLEU_STATUS_LOST); refused while a command
 * write is in flight and the reverse; a length of 0, 245 and one past the
 * link's MTU less 3 refused, the longest that fits taken; a CMD without
 * WRITE refused; a characteristic matched by its 16-bit UUID, by its
 * 128-bit UUID and by the 16-bit one in the Bluetooth base's 128 bits, and
 * not by another; a profile with a 16-bit UUID only valid, with neither
 * invalid; a scan that lists by OUI (organizationally unique identifier),
 * by service, and neither; a WRITE_EXT on another client's handle answered
 * lost; 6 clients in the hub, the sixth's connect answered busy behind a
 * scan and given the next turn when hurried; the sixth connection refused
 * by the host answers its client and leaves the 5 links up.
 *
 * The printer's facts these stand in for (16-bit or 128-bit form of FFE1
 * and FFE9, what its advertisement holds) are station step H0's.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "ble_capture.h"
#include "hub_rig.h"

/** @brief The printer's vendor service, 0000E0FF-3C17-D293-8E48-14FE2E4DA212,
 *         least significant byte first. */
static const uint8_t k_svc_v[16] = { 0x12, 0xA2, 0x4D, 0x2E, 0xFE, 0x14,
                                     0x48, 0x8E, 0x93, 0xD2, 0x17, 0x3C,
                                     0xFF, 0xE0, 0x00, 0x00 };
/** @brief FFE1 in the vendor's base. */
static const uint8_t k_ffe1_v[16] = { 0x12, 0xA2, 0x4D, 0x2E, 0xFE, 0x14,
                                      0x48, 0x8E, 0x93, 0xD2, 0x17, 0x3C,
                                      0xE1, 0xFF, 0x00, 0x00 };
/** @brief FFE9 in the vendor's base. */
static const uint8_t k_ffe9_v[16] = { 0x12, 0xA2, 0x4D, 0x2E, 0xFE, 0x14,
                                      0x48, 0x8E, 0x93, 0xD2, 0x17, 0x3C,
                                      0xE9, 0xFF, 0x00, 0x00 };
/** @brief FFE1 in the Bluetooth base. */
static const uint8_t k_ffe1_b[16] = { 0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00,
                                      0x00, 0x80, 0x00, 0x10, 0x00, 0x00,
                                      0xE1, 0xFF, 0x00, 0x00 };
/** @brief FFE9 in the Bluetooth base. */
static const uint8_t k_ffe9_b[16] = { 0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00,
                                      0x00, 0x80, 0x00, 0x10, 0x00, 0x00,
                                      0xE9, 0xFF, 0x00, 0x00 };
/** @brief FFE1 in the Bluetooth base with byte 14 set: another UUID. */
static const uint8_t k_ffe1_x[16] = { 0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00,
                                      0x00, 0x80, 0x00, 0x10, 0x00, 0x00,
                                      0xE1, 0xFF, 0x01, 0x00 };
/** @brief The printer's address prefix, A4:93:40, in reading order. */
static const uint8_t k_oui[3] = { 0xA4, 0x93, 0x40 };

/** @brief A printer-like profile: FFE1 STREAM, FFE9 CMD, each by both
 *         UUID forms; the sim answers DISC_SVC for any service. */
static const bleu_profile_t k_prn = {
    .svc = k_svc_v,
    .chr = {
        { .uuid = k_ffe1_v, .role = BLEU_ROLE_STREAM, .uuid16 = 0xFFE1u },
        { .uuid = k_ffe9_v, .role = BLEU_ROLE_CMD,    .uuid16 = 0xFFE9u },
    },
    .chr_n = 2u,
    .conn = { .itvl_min = 16u, .itvl_max = 32u, .latency = 0u,
              .supervision = 400u, .scan_itvl = 0x60u, .scan_window = 0x30u },
    .adv_oui = k_oui,
};

/** @brief A 16-byte command. */
static const uint8_t k_cmd[16] = { 0x7E, 0x5A, 0x0C, 0x00, 0x10, 0x01,
                                   0xAA, 0x12, 0x01, 0x00, 0x00, 0x01 };

/**
 * @brief The sim's table as the printer's: FFE1 (NOTIFY, WRITE) and FFE9
 *        (WRITE), each reported in 16 bits when @p u1 / @p u9 is NULL.
 * @param s  The sim.
 * @param u1 FFE1's 128-bit form, or NULL for 16 bits.
 * @param u9 FFE9's 128-bit form, or NULL for 16 bits.
 */
static void prn_table(bsim_t *s, const uint8_t *u1, const uint8_t *u9)
{
    s->chr[0] = (bsim_chr_t){ H_TX_DEF, H_TX_VAL,
                              BLEU_PROP_NOTIFY | BLEU_PROP_WRITE, u1 };
    s->chr[1] = (bsim_chr_t){ H_RX_DEF, H_RX_VAL, BLEU_PROP_WRITE, u9 };
    s->chr16[0] = (u1 == NULL) ? 0xFFE1u : 0u;
    s->chr16[1] = (u9 == NULL) ? 0xFFE9u : 0u;
    s->nchr = 2u;
    s->ndsc = 1u;
}

/**
 * @brief Bring @p b up on @p p against @p s at @p t0.
 * @param s  The sim.
 * @param b  The machine.
 * @param p  The profile.
 * @param t0 The clock.
 * @return The tick a write can go, or t0 + 2000.
 */
static uint32_t up_on(bsim_t *s, bleu_t *b, const bleu_profile_t *p,
                      uint32_t t0)
{
    bleu_init(b);
    CHECK(bleu_setup(b, p, 5u));
    bleu_bind(b, &k_cap_addr, t0);
    bleu_enable(b, true, t0);
    return bsim_until(s, b, t0, 2000u, bsim_can_send);
}

/** @brief The sim and the machine, off the stack. */
static bsim_t s_s;
/** @brief The machine. */
static bleu_t s_b;

TEST_CASE(a_long_write_goes_with_its_length_and_is_answered)
{
    bsim_init(&s_s);
    s_s.mtu = 240u;
    const uint32_t t = up_on(&s_s, &s_b, &bleu_profile_issc, 0);
    CHECK(bleu_can_send(&s_b));
    CHECK_EQ(s_b.mtu, 240u);
    CHECK(bleu_send_ext(&s_b, 128u, t));
    CHECK_EQ(s_b.wr, BLEU_WR_BUSY);
    CHECK(!bleu_can_send(&s_b));
    bsim_run(&s_s, &s_b, t, t + 30u);
    const bsim_log_t *w = bsim_last(&s_s, BLEU_ACT_WRITE_EXT);
    CHECK(w != NULL);
    if (w != NULL) {
        CHECK_EQ(w->a.h1, H_RX_VAL);
        CHECK_EQ(w->a.h2, 128u);
        CHECK_EQ(w->a.len, 0u);
        CHECK_EQ(bleu_token_owner(w->a.token), 5u);
        CHECK((w->a.token & BLEU_TOKEN_SEQ) != 0u);
    }
    CHECK_EQ(s_b.wr, BLEU_WR_OK);
    CHECK(bleu_can_send(&s_b));
    /* the next one takes the slot again */
    CHECK(bleu_send_ext(&s_b, 128u, t + 30u));
}

TEST_CASE(an_unanswered_long_write_times_out_at_1000ms_not_999)
{
    bsim_init(&s_s);
    s_s.mtu = 240u;
    const uint32_t t = up_on(&s_s, &s_b, &bleu_profile_issc, 0);
    s_s.silent = BSIM_Q_WRITE;
    CHECK(bleu_send_ext(&s_b, 128u, t));
    bsim_run(&s_s, &s_b, t, t + 1000u);
    CHECK_EQ(s_b.wr, BLEU_WR_BUSY);
    bsim_tick(&s_s, &s_b, t + 1000u);
    CHECK_EQ(s_b.wr, BLEU_WR_FAILED);
    CHECK_EQ(s_b.wr_status, BLEU_STATUS_TIMEOUT);
    CHECK(bleu_can_send(&s_b));
}

TEST_CASE(a_long_write_refused_by_the_peer_and_lost_with_the_link)
{
    bsim_init(&s_s);
    s_s.mtu = 240u;
    uint32_t t = up_on(&s_s, &s_b, &bleu_profile_issc, 0);
    s_s.fail = BSIM_Q_WRITE;
    s_s.fail_status = 0x0103u;
    CHECK(bleu_send_ext(&s_b, 64u, t));
    bsim_run(&s_s, &s_b, t, t + 30u);
    CHECK_EQ(s_b.wr, BLEU_WR_FAILED);
    CHECK_EQ(s_b.wr_status, 0x0103u);
    s_s.fail = 0u;
    s_s.lat = 500u;
    t += 30u;
    CHECK(bleu_send_ext(&s_b, 64u, t));
    bsim_drop(&s_s, t + 10u, 0x08u);
    bsim_run(&s_s, &s_b, t, t + 20u);
    CHECK_EQ(s_b.wr, BLEU_WR_FAILED);
    CHECK_EQ(s_b.wr_status, BLEU_STATUS_LOST);
    CHECK(!bleu_send_ext(&s_b, 64u, t + 20u));
}

TEST_CASE(a_long_write_and_a_command_write_never_overlap)
{
    bsim_init(&s_s);
    s_s.mtu = 240u;
    s_s.lat = 50u;
    const uint32_t t = up_on(&s_s, &s_b, &bleu_profile_issc, 0);
    CHECK(bleu_send(&s_b, k_cmd, sizeof k_cmd, t));
    CHECK(!bleu_send_ext(&s_b, 128u, t));
    bsim_run(&s_s, &s_b, t, t + 60u);
    CHECK_EQ(s_b.wr, BLEU_WR_OK);
    CHECK(bleu_send_ext(&s_b, 128u, t + 60u));
    CHECK(!bleu_send(&s_b, k_cmd, sizeof k_cmd, t + 60u));
    CHECK(!bleu_send_ext(&s_b, 128u, t + 60u));
    bsim_run(&s_s, &s_b, t + 60u, t + 120u);
    CHECK_EQ(s_b.wr, BLEU_WR_OK);
    CHECK_EQ(bsim_count(&s_s, BLEU_ACT_WRITE), 1u);
    CHECK_EQ(bsim_count(&s_s, BLEU_ACT_WRITE_EXT), 1u);
}

TEST_CASE(the_length_is_held_to_244_and_to_the_links_mtu)
{
    /* MTU 247: 1 and 244 taken, 0 and 245 refused */
    bsim_init(&s_s);
    s_s.mtu = 247u;
    uint32_t t = up_on(&s_s, &s_b, &bleu_profile_issc, 0);
    CHECK(!bleu_send_ext(&s_b, 0u, t));
    CHECK(!bleu_send_ext(&s_b, 245u, t));
    CHECK(!bleu_send_ext(&s_b, 0xFFFFu, t));
    CHECK_EQ(s_b.wr, BLEU_WR_IDLE);
    CHECK(bleu_send_ext(&s_b, 244u, t));
    bsim_run(&s_s, &s_b, t, t + 30u);
    CHECK(bleu_send_ext(&s_b, 1u, t + 30u));
    /* MTU 240, as the station's printer answered: 237 taken, 238 not */
    bsim_init(&s_s);
    s_s.mtu = 240u;
    t = up_on(&s_s, &s_b, &bleu_profile_issc, 0);
    CHECK(!bleu_send_ext(&s_b, 238u, t));
    CHECK(bleu_send_ext(&s_b, 237u, t));
    /* MTU 130: a 128-byte fragment does not fit (it needs 131) */
    bsim_init(&s_s);
    s_s.mtu = 130u;
    t = up_on(&s_s, &s_b, &bleu_profile_issc, 0);
    CHECK(!bleu_send_ext(&s_b, 128u, t));
    CHECK(bleu_send_ext(&s_b, 127u, t));
    /* the exchange failed: the default 23, so 20 bytes */
    bsim_init(&s_s);
    s_s.mtu = 240u;
    s_s.fail = BSIM_Q_MTU;
    s_s.fail_status = 0x0106u;
    t = up_on(&s_s, &s_b, &bleu_profile_issc, 0);
    CHECK_EQ(s_b.mtu, 0u);
    CHECK(!bleu_send_ext(&s_b, 21u, t));
    CHECK(bleu_send_ext(&s_b, 20u, t));
}

TEST_CASE(a_cmd_without_write_with_response_takes_no_long_write)
{
    bsim_init(&s_s);
    s_s.mtu = 240u;
    s_s.chr[1].props = BLEU_PROP_WRITE_NR;
    const uint32_t t = up_on(&s_s, &s_b, &bleu_profile_issc, 0);
    CHECK(bleu_can_send(&s_b));
    CHECK(!bleu_send_ext(&s_b, 20u, t));
    CHECK(bleu_send(&s_b, k_cmd, sizeof k_cmd, t));
    CHECK(!bleu_send_ext(NULL, 20u, t));
    /* down: nothing goes */
    bleu_t off;
    bleu_init(&off);
    CHECK(!bleu_send_ext(&off, 20u, t));
}

TEST_CASE(a_characteristic_is_found_by_its_16_or_128_bit_uuid)
{
    /* 16 bits, the vendor's 128 bits and the Bluetooth base's 128 bits */
    const uint8_t *forms[3][2] = {
        { NULL, NULL }, { k_ffe1_v, k_ffe9_v }, { k_ffe1_b, k_ffe9_b },
    };
    for (unsigned i = 0; i < 3u; ++i) {
        bsim_init(&s_s);
        s_s.mtu = 240u;
        prn_table(&s_s, forms[i][0], forms[i][1]);
        const uint32_t t = up_on(&s_s, &s_b, &k_prn, 0);
        CHECK(bleu_link_up(&s_b));
        CHECK_EQ(s_b.tx_val, H_TX_VAL);
        CHECK_EQ(s_b.rx_val, H_RX_VAL);
        CHECK_EQ(s_b.cccd, H_TX_CCCD);
        CHECK(bleu_send_ext(&s_b, 128u, t));
    }
    /* a 16-bit-only entry finds the base form too */
    bleu_profile_t p16 = k_prn;
    p16.chr[0].uuid = NULL;
    p16.chr[1].uuid = NULL;
    CHECK(bleu_profile_valid(&p16));
    bsim_init(&s_s);
    prn_table(&s_s, k_ffe1_b, NULL);
    (void)up_on(&s_s, &s_b, &p16, 0);
    CHECK(bleu_link_up(&s_b));
    /* ...but not the vendor's 128 bits, which only the 128-bit entry is */
    bsim_init(&s_s);
    prn_table(&s_s, k_ffe1_v, NULL);
    (void)up_on(&s_s, &s_b, &p16, 0);
    CHECK_EQ(s_b.st, BLEU_ST_NOT_PEER);
}

TEST_CASE(another_uuid_is_not_the_characteristic)
{
    /* FFE2 in 16 bits */
    bsim_init(&s_s);
    prn_table(&s_s, NULL, NULL);
    s_s.chr16[0] = 0xFFE2u;
    (void)up_on(&s_s, &s_b, &k_prn, 0);
    CHECK_EQ(s_b.st, BLEU_ST_NOT_PEER);
    CHECK_EQ(s_b.err, BLEU_ERR_NOT_PEER);
    /* FFE1 in the base with a byte beyond the 16-bit value set */
    bsim_init(&s_s);
    prn_table(&s_s, k_ffe1_x, NULL);
    (void)up_on(&s_s, &s_b, &k_prn, 0);
    CHECK_EQ(s_b.st, BLEU_ST_NOT_PEER);
    /* the ISSC profile has no 16-bit entries: a 16-bit 8841 is not it */
    bsim_init(&s_s);
    bsim_table(&s_s);
    s_s.chr16[0] = 0x1E4Du;
    (void)up_on(&s_s, &s_b, &bleu_profile_issc, 0);
    CHECK_EQ(s_b.st, BLEU_ST_NOT_PEER);
}

TEST_CASE(a_profile_entry_needs_one_of_the_two_uuids)
{
    bleu_profile_t p = k_prn;
    CHECK(bleu_profile_valid(&p));
    p.chr[1].uuid = NULL;
    CHECK(bleu_profile_valid(&p));
    p.chr[1].uuid16 = 0u;
    CHECK(!bleu_profile_valid(&p));
    p.chr[1].uuid = k_ffe9_v;
    CHECK(bleu_profile_valid(&p));
}

/**
 * @brief Advertising data with a complete local name and nothing else.
 * @param ad  Receives the bytes.
 * @return Their count.
 */
static size_t name_only(uint8_t ad[20])
{
    static const char name[] = "T0205C2608247368";
    ad[0] = (uint8_t)(sizeof name);
    ad[1] = 0x09u;
    memcpy(ad + 2, name, sizeof name - 1u);
    return sizeof name + 1u;
}

TEST_CASE(a_scan_lists_by_address_prefix_by_service_or_not_at_all)
{
    bleu_list_t l;
    bleu_list_clear(&l);
    uint8_t ad[20];
    const size_t n = name_only(ad);
    /* the printer's public address, no service in its advertisement */
    const bleu_addr_t prn = { { 0x71, 0xFA, 0x30, 0x40, 0x93, 0xA4 },
                              BLEU_ADDR_PUBLIC };
    bleu_list_report_for(&l, &k_prn, &prn, -60, ad, n);
    CHECK_EQ(l.found_n, 1u);
    CHECK_EQ(l.found[0].flags & BLEU_DEV_OUI, BLEU_DEV_OUI);
    CHECK_EQ(l.found[0].flags & BLEU_DEV_ISSC, 0u);
    CHECK_STR_EQ(l.found[0].name, "T0205C2608247368");
    /* the same bytes as a random address: not the maker's prefix */
    bleu_addr_t rnd = prn;
    rnd.type = BLEU_ADDR_RANDOM;
    bleu_list_report_for(&l, &k_prn, &rnd, -50, ad, n);
    CHECK_EQ(l.found_n, 1u);
    CHECK_EQ(l.hidden, 1u);
    /* another prefix that advertises the profile's service */
    uint8_t ad2[18];
    ad2[0] = 17u;
    ad2[1] = 0x07u;
    memcpy(ad2 + 2, k_svc_v, 16);
    const bleu_addr_t svc = { { 1, 2, 3, 0x41, 0x93, 0xA4 },
                              BLEU_ADDR_PUBLIC };
    bleu_list_report_for(&l, &k_prn, &svc, -70, ad2, sizeof ad2);
    CHECK_EQ(l.found_n, 2u);
    CHECK_EQ(l.found[1].flags & BLEU_DEV_ISSC, BLEU_DEV_ISSC);
    CHECK_EQ(l.found[1].flags & BLEU_DEV_OUI, 0u);
    /* another prefix, no service: hidden */
    const bleu_addr_t other = { { 1, 2, 3, 0x40, 0x93, 0xA5 },
                                BLEU_ADDR_PUBLIC };
    bleu_list_report_for(&l, &k_prn, &other, -40, ad, n);
    CHECK_EQ(l.found_n, 2u);
    CHECK_EQ(l.hidden, 2u);
    /* a profile without a prefix does not list the printer */
    bleu_profile_t no_oui = k_prn;
    no_oui.adv_oui = NULL;
    bleu_list_clear(&l);
    bleu_list_report_for(&l, &no_oui, &prn, -60, ad, n);
    CHECK_EQ(l.found_n, 0u);
    /* nor does the ISSC default */
    bleu_list_report(&l, &prn, -60, ad, n);
    CHECK_EQ(l.found_n, 0u);
}

TEST_CASE(a_long_write_on_another_clients_handle_is_answered_lost)
{
    rig_t *r = rig_init(2);
    rig_on(r, 0, 0);
    rig_run(r, 0, 1500);
    CHECK(bleu_link_up(&r->b[0]));
    bleu_act_t a;
    memset(&a, 0, sizeof a);
    a.kind = BLEU_ACT_WRITE_EXT;
    a.conn = r->b[0].conn;
    a.h1 = H_RX_VAL;
    a.h2 = 128u;
    a.token = (uint16_t)((1u << BLEU_OWNER_SHIFT) | 7u);
    const uint32_t refused = r->hub.refused;
    bleu_hub_action(&r->hub, 1u, &a, 1500);
    CHECK_EQ(r->hub.refused, refused + 1u);
    uint8_t who = 0xFFu;
    bleu_evt_t e;
    CHECK(bleu_hub_next_event(&r->hub, &who, &e));
    CHECK_EQ(who, 1u);
    CHECK_EQ(e.kind, BLEU_EV_WRITE_DONE);
    CHECK_EQ(e.token, a.token);
    CHECK_EQ(e.status, BLEU_STATUS_LOST);
    bleu_act_t out;
    CHECK(!bleu_hub_next_action(&r->hub, &out));
    /* on its own handle it goes to the host stack */
    a.token = (uint16_t)(0u << BLEU_OWNER_SHIFT | 9u);
    bleu_hub_action(&r->hub, 0u, &a, 1500);
    CHECK(bleu_hub_next_action(&r->hub, &out));
    CHECK_EQ(out.kind, BLEU_ACT_WRITE_EXT);
    CHECK_EQ(out.h2, 128u);
}

/**
 * @brief A CONNECT of owner @p o with token count @p seq.
 * @param o     The owner.
 * @param seq   The token's count.
 * @param hurry Hurried.
 * @return The action.
 */
static bleu_act_t connect_of(uint8_t o, uint16_t seq, bool hurry)
{
    bleu_act_t a;
    memset(&a, 0, sizeof a);
    a.kind = BLEU_ACT_CONNECT;
    a.token = (uint16_t)(((unsigned)o << BLEU_OWNER_SHIFT) | seq);
    a.ms = (uint16_t)BLEU_CONNECT_MS;
    a.flags = hurry ? BLEU_ACT_F_HURRY : 0u;
    return a;
}

/**
 * @brief Hand @p a to the hub for @p o and read what came back.
 * @param h The hub.
 * @param o The owner.
 * @param a The action.
 * @param t The clock.
 * @return The status the hub answered @p o with, or 0 when it passed the
 *         action on.
 */
static uint16_t hub_answer(bleu_hub_t *h, uint8_t o, const bleu_act_t *a,
                           uint32_t t)
{
    bleu_hub_action(h, o, a, t);
    uint8_t who = 0xFFu;
    bleu_evt_t e;
    uint16_t st = 0u;
    while (bleu_hub_next_event(h, &who, &e)) {
        CHECK_EQ(who, o);
        st = e.status;
    }
    bleu_act_t out;
    while (bleu_hub_next_action(h, &out)) { }
    return st;
}

TEST_CASE(the_hub_holds_six_clients_and_the_sixth_waits_its_turn)
{
    bleu_hub_t h;
    bleu_hub_init(&h);
    for (uint8_t o = 0; o < 6u; ++o) {
        CHECK(bleu_hub_add(&h, o));
    }
    CHECK(!bleu_hub_add(&h, 6u));
    /* owner 0 scans; owner 5's connect is answered busy */
    bleu_act_t sc;
    memset(&sc, 0, sizeof sc);
    sc.kind = BLEU_ACT_SCAN;
    sc.token = (uint16_t)(1u);
    sc.ms = 100u;
    CHECK_EQ(hub_answer(&h, 0, &sc, 0), 0u);
    CHECK_EQ(h.gap, BLEU_HUB_GAP_SCAN);
    const bleu_act_t c5 = connect_of(5, 1, false);
    CHECK_EQ(hub_answer(&h, 5, &c5, 10), BLEU_STATUS_BUSY);
    /* hurried, it keeps the next turn behind the scan */
    const bleu_act_t c5h = connect_of(5, 2, true);
    CHECK_EQ(hub_answer(&h, 5, &c5h, 20), BLEU_STATUS_TURN);
    CHECK(h.turn_set);
    CHECK_EQ(h.turn_owner, 5u);
    /* another client's connect does not take that turn */
    const bleu_act_t c2 = connect_of(2, 1, false);
    CHECK_EQ(hub_answer(&h, 2, &c2, 30), BLEU_STATUS_BUSY);
    bleu_evt_t end;
    memset(&end, 0, sizeof end);
    end.kind = BLEU_EV_SCAN_END;
    end.token = sc.token;
    bleu_hub_event(&h, &end, 100);
    uint8_t who;
    while (bleu_hub_next_event(&h, &who, &end)) { }
    CHECK_EQ(hub_answer(&h, 2, &c2, 101), BLEU_STATUS_BUSY);
    const bleu_act_t c5n = connect_of(5, 3, true);
    CHECK_EQ(hub_answer(&h, 5, &c5n, 102), 0u);
    CHECK_EQ(h.gap, BLEU_HUB_GAP_CONNECT);
    CHECK_EQ(h.gap_owner, 5u);
    CHECK(!h.turn_set);
}

TEST_CASE(a_sixth_connection_the_host_refuses_leaves_five_links_up)
{
    rig_t *r = rig_init(6);
    /* the host stack has 5 connections: the sixth connect fails */
    r->s[5].conn_status = 6u;
    for (unsigned i = 0; i < 6u; ++i) {
        rig_on(r, i, 0);
    }
    rig_run(r, 0, 6000);
    for (unsigned i = 0; i < 5u; ++i) {
        CHECK(bleu_link_up(&r->b[i]));
        CHECK(r->hub.c[i].linked);
    }
    CHECK(!bleu_link_up(&r->b[5]));
    CHECK_EQ(r->b[5].err, BLEU_ERR_CONNECT);
    CHECK_EQ(r->b[5].err_status, 6u);
    CHECK(!r->hub.c[5].linked);
    CHECK(r->b[5].attempts >= 1u);
    /* the 5 links stream on, each its own bytes */
    for (unsigned i = 0; i < 5u; ++i) {
        CHECK(r->b[i].notifies > 0u);
        CHECK_EQ(r->s[i].rx[0], k_bytes[i][0]);
    }
}

int main(void)
{
    RUN(a_long_write_goes_with_its_length_and_is_answered);
    RUN(an_unanswered_long_write_times_out_at_1000ms_not_999);
    RUN(a_long_write_refused_by_the_peer_and_lost_with_the_link);
    RUN(a_long_write_and_a_command_write_never_overlap);
    RUN(the_length_is_held_to_244_and_to_the_links_mtu);
    RUN(a_cmd_without_write_with_response_takes_no_long_write);
    RUN(a_characteristic_is_found_by_its_16_or_128_bit_uuid);
    RUN(another_uuid_is_not_the_characteristic);
    RUN(a_profile_entry_needs_one_of_the_two_uuids);
    RUN(a_scan_lists_by_address_prefix_by_service_or_not_at_all);
    RUN(a_long_write_on_another_clients_handle_is_answered_lost);
    RUN(the_hub_holds_six_clients_and_the_sixth_waits_its_turn);
    RUN(a_sixth_connection_the_host_refuses_leaves_five_links_up);
    return test_summary("bleuart_ext");
}
