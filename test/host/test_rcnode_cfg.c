/*
 * The node's own settings in the sensor nodes' protocol
 * (protocols/rcnode/rcnode_cfg.c, the CONFIG packet and the NAME, I228
 * and CONFIG writes in rcnode.c, the named scan response in
 * rcnode_adv.c).
 *
 * The failures this is about: a name the panel cannot type or the node
 * cannot show taken on the air, a name byte after its end that is not
 * padding, a fixed address outside 0x40 to 0x4F taken, a CONFIG field at
 * the wrong offset, a write an older node or panel would read as another
 * operation, and a shunt sync that lets the older change win.  Expected
 * bytes are written out by hand from the layout table in rcnode.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "rcnode.h"

#include <stdint.h>
#include <string.h>

TEST_CASE(a_name_is_16_characters_of_the_name_keyboard_trimmed)
{
    CHECK(rcnode_uname_ok(""));
    CHECK(rcnode_uname_ok("XT90 MAIN"));
    CHECK(rcnode_uname_ok("A-B_C.D 0123456"));
    CHECK(rcnode_uname_ok("0123456789ABCDEF"));      /* 16 */
    CHECK(!rcnode_uname_ok("0123456789ABCDEFG"));    /* 17 */
    CHECK(!rcnode_uname_ok(" LEFT"));
    CHECK(!rcnode_uname_ok("RIGHT "));
    CHECK(!rcnode_uname_ok("lower"));
    CHECK(!rcnode_uname_ok("A/B"));
    CHECK(!rcnode_uname_ok("A\tB"));
    CHECK(!rcnode_uname_ok("\xC3\x84"));             /* an Ä */
    CHECK(!rcnode_uname_ok(NULL));
}

TEST_CASE(an_address_setting_is_auto_or_0x40_to_0x4f)
{
    CHECK(rcnode_i228_ok(RCNODE_I228_AUTO));
    CHECK(rcnode_i228_ok(0x40u));
    CHECK(rcnode_i228_ok(0x45u));
    CHECK(rcnode_i228_ok(0x4Fu));
    CHECK(!rcnode_i228_ok(0x3Fu));
    CHECK(!rcnode_i228_ok(0x50u));
    CHECK(!rcnode_i228_ok(0x38u));
    CHECK(!rcnode_i228_ok(0xFFu));
}

TEST_CASE(config_has_its_bytes_and_comes_back_as_written)
{
    rcnode_config_t c;
    memset(&c, 0, sizeof c);
    c.hdr.seq = 7u;
    c.hdr.t_node = 0x11223344u;
    c.flags = RCNODE_CF_SHUNT;
    c.i228_set = 0x45u;
    c.shunt_uohm = 500u;
    c.shunt_agreed = 200u;
    c.shunt_age_s = 0x1234u;
    strcpy(c.name, "XT90 MAIN");
    uint8_t b[RCNODE_CONFIG_LEN + 2];
    CHECK_EQ(rcnode_enc_config(&c, b, sizeof b), RCNODE_CONFIG_LEN);
    const uint8_t want[RCNODE_CONFIG_LEN] = {
        0x32, 7, 0x44, 0x33, 0x22, 0x11, 0x01, 0x45, 0xF4, 0x01, 0xC8, 0x00,
        0x34, 0x12, 'X', 'T', '9', '0', ' ', 'M', 'A', 'I', 'N', 0, 0, 0, 0,
        0, 0, 0,
    };
    CHECK(memcmp(b, want, sizeof want) == 0);
    CHECK_EQ(rcnode_len(RCNODE_T_CONFIG), RCNODE_CONFIG_LEN);
    rcnode_pkt_t p;
    CHECK_EQ(rcnode_decode(b, RCNODE_CONFIG_LEN, &p), RCNODE_OK);
    CHECK_EQ(p.hdr.type, RCNODE_T_CONFIG);
    CHECK_EQ(p.u.config.i228_set, 0x45u);
    CHECK_EQ(p.u.config.shunt_uohm, 500u);
    CHECK_EQ(p.u.config.shunt_agreed, 200u);
    CHECK_EQ(p.u.config.shunt_age_s, 0x1234u);
    CHECK_STR_EQ(p.u.config.name, "XT90 MAIN");
    /* All 16 bytes may be characters. */
    strcpy(c.name, "0123456789ABCDEF");
    CHECK_EQ(rcnode_enc_config(&c, b, sizeof b), RCNODE_CONFIG_LEN);
    CHECK_EQ(rcnode_decode(b, RCNODE_CONFIG_LEN, &p), RCNODE_OK);
    CHECK_STR_EQ(p.u.config.name, "0123456789ABCDEF");
    CHECK_EQ(rcnode_decode(b, RCNODE_CONFIG_LEN - 1u, &p), RCNODE_ERR_LEN);
}

TEST_CASE(config_refuses_a_bad_name_padding_or_address)
{
    rcnode_config_t c;
    memset(&c, 0, sizeof c);
    strcpy(c.name, "lower");
    uint8_t b[RCNODE_CONFIG_LEN];
    CHECK_EQ(rcnode_enc_config(&c, b, sizeof b), 0u);
    strcpy(c.name, "OK");
    c.i228_set = 0x50u;
    CHECK_EQ(rcnode_enc_config(&c, b, sizeof b), 0u);
    c.i228_set = 0u;
    CHECK_EQ(rcnode_enc_config(&c, b, sizeof b - 1u), 0u);
    CHECK_EQ(rcnode_enc_config(&c, b, sizeof b), RCNODE_CONFIG_LEN);
    rcnode_pkt_t p;
    memset(&p, 0, sizeof p);
    b[20] = 'X';    /* a byte after the name's NUL */
    CHECK_EQ(rcnode_decode(b, sizeof b, &p), RCNODE_ERR_RANGE);
    b[20] = 0u;
    b[7] = 0x3Fu;
    CHECK_EQ(rcnode_decode(b, sizeof b, &p), RCNODE_ERR_RANGE);
    b[7] = 0u;
    b[14] = 'a';
    CHECK_EQ(rcnode_decode(b, sizeof b, &p), RCNODE_ERR_RANGE);
}

TEST_CASE(the_name_write_is_17_bytes_padded_and_checked)
{
    rcnode_ctl_t c;
    memset(&c, 0, sizeof c);
    c.op = RCNODE_OP_NAME;
    strcpy(c.name, "BAL 1");
    uint8_t b[RCNODE_CTL_MAX];
    CHECK_EQ(rcnode_enc_ctl(&c, b, sizeof b), 17u);
    const uint8_t want[17] = { 0x09, 'B', 'A', 'L', ' ', '1', 0, 0, 0, 0, 0,
                               0, 0, 0, 0, 0, 0 };
    CHECK(memcmp(b, want, sizeof want) == 0);
    rcnode_ctl_t d;
    CHECK_EQ(rcnode_dec_ctl(b, 17u, &d), RCNODE_OK);
    CHECK_EQ(d.op, RCNODE_OP_NAME);
    CHECK_STR_EQ(d.name, "BAL 1");
    CHECK_EQ(rcnode_dec_ctl(b, 16u, &d), RCNODE_ERR_LEN);
    b[10] = 'Q';                       /* past the NUL */
    CHECK_EQ(rcnode_dec_ctl(b, 17u, &d), RCNODE_ERR_RANGE);
    memset(b + 1, 0, 16);              /* "": the name cleared */
    CHECK_EQ(rcnode_dec_ctl(b, 17u, &d), RCNODE_OK);
    CHECK_STR_EQ(d.name, "");
    strcpy(c.name, " X");
    CHECK_EQ(rcnode_enc_ctl(&c, b, sizeof b), 0u);
    CHECK_EQ(rcnode_enc_ctl(&c, b, 16u), 0u);
}

TEST_CASE(the_address_and_config_writes_and_their_roles)
{
    rcnode_ctl_t c;
    memset(&c, 0, sizeof c);
    c.op = RCNODE_OP_I228;
    c.i228 = 0x4Au;
    uint8_t b[RCNODE_CTL_MAX];
    CHECK_EQ(rcnode_enc_ctl(&c, b, sizeof b), 2u);
    CHECK_EQ(b[0], 0x0Au);
    CHECK_EQ(b[1], 0x4Au);
    rcnode_ctl_t d;
    CHECK_EQ(rcnode_dec_ctl(b, 2u, &d), RCNODE_OK);
    CHECK_EQ(d.i228, 0x4Au);
    b[1] = 0x38u;
    CHECK_EQ(rcnode_dec_ctl(b, 2u, &d), RCNODE_ERR_RANGE);
    b[1] = 0u;
    CHECK_EQ(rcnode_dec_ctl(b, 2u, &d), RCNODE_OK);
    CHECK_EQ(d.i228, RCNODE_I228_AUTO);
    const uint8_t cfg = RCNODE_OP_CONFIG;
    CHECK_EQ(rcnode_dec_ctl(&cfg, 1u, &d), RCNODE_OK);
    CHECK_EQ(d.op, RCNODE_OP_CONFIG);
    CHECK(rcnode_ctl_for_role(RCNODE_ROLE_POWER, RCNODE_OP_CONFIG));
    CHECK(rcnode_ctl_for_role(RCNODE_ROLE_BALANCE, RCNODE_OP_CONFIG));
    CHECK(rcnode_ctl_for_role(RCNODE_ROLE_POWER, RCNODE_OP_NAME));
    CHECK(rcnode_ctl_for_role(RCNODE_ROLE_BALANCE, RCNODE_OP_NAME));
    CHECK(rcnode_ctl_for_role(RCNODE_ROLE_POWER, RCNODE_OP_I228));
    CHECK(!rcnode_ctl_for_role(RCNODE_ROLE_BALANCE, RCNODE_OP_I228));
    CHECK(!rcnode_ctl_for_role(RCNODE_ROLE_SERVO, RCNODE_OP_NAME));
}

TEST_CASE(an_older_node_refuses_each_new_write_by_its_length_or_op)
{
    /* A node built before these writes knows operations 0x01 to 0x07 and
     * takes 3 bytes at most: NAME's 17 bytes fail its length check (ATT
     * 0x0D), CONFIG and I228 its operation check (ATT 0x13).  None of the
     * new codes is one of the old operations. */
    CHECK(RCNODE_OP_CONFIG > RCNODE_OP_SHUNT);
    CHECK(RCNODE_OP_NAME > RCNODE_OP_SHUNT);
    CHECK(RCNODE_OP_I228 > RCNODE_OP_SHUNT);
    CHECK(1u + RCNODE_UNAME_MAX > 3u);
    CHECK_EQ(RCNODE_CTL_MAX, 17u);
}

TEST_CASE(the_scan_response_carries_the_own_name_or_the_default)
{
    const uint8_t a[6] = { 0x56, 0x34, 0x12, 0xA1, 0xDF, 0x7C };
    uint8_t b[RCNODE_RSP_LEN];
    CHECK_EQ(rcnode_rsp_build_named(RCNODE_ROLE_POWER, a, "XT90 MAIN", b,
                                    sizeof b), 11u);
    CHECK_EQ(b[0], 10u);
    CHECK_EQ(b[1], 0x09u);
    CHECK(memcmp(b + 2, "XT90 MAIN", 9) == 0);
    CHECK_EQ(rcnode_rsp_build_named(RCNODE_ROLE_POWER, a, "", b, sizeof b),
             RCNODE_RSP_LEN);
    CHECK(memcmp(b + 2, "rcbench-pwr-3456", 16) == 0);
    CHECK_EQ(rcnode_rsp_build_named(RCNODE_ROLE_POWER, a, "bad", b, sizeof b),
             RCNODE_RSP_LEN);
    CHECK(memcmp(b + 2, "rcbench-pwr-3456", 16) == 0);
    CHECK_EQ(rcnode_rsp_build_named(RCNODE_ROLE_BALANCE, a,
                                    "0123456789ABCDEF", b, sizeof b),
             RCNODE_RSP_LEN);
    CHECK(memcmp(b + 2, "0123456789ABCDEF", 16) == 0);
    CHECK_EQ(rcnode_rsp_build_named(RCNODE_ROLE_POWER, a, "X", b, 17u), 0u);
    /* The role still comes from the manufacturer data, not the name. */
    uint8_t ad[RCNODE_ADV_LEN];
    CHECK_EQ(rcnode_adv_build(RCNODE_ROLE_POWER, a, ad, sizeof ad),
             RCNODE_ADV_LEN);
    CHECK_EQ(rcnode_adv_maker_role(ad, sizeof ad), RCNODE_ROLE_POWER);
}

TEST_CASE(sync_one_side_changed_goes_its_way)
{
    /* Nothing changed. */
    CHECK_EQ(rcnode_shunt_sync(200u, 200u, 200u, RCNODE_AGE_NONE, 0u),
             RCNODE_SYNC_KEEP);
    /* The panel changed it while apart. */
    CHECK_EQ(rcnode_shunt_sync(500u, 200u, 200u, RCNODE_AGE_NONE, 0u),
             RCNODE_SYNC_WRITE);
    /* The node's SETUP changed it while apart. */
    CHECK_EQ(rcnode_shunt_sync(200u, 500u, 200u, 3u, UINT32_MAX),
             RCNODE_SYNC_ADOPT);
    /* The panel took the node's value: the write marks it agreed. */
    CHECK_EQ(rcnode_shunt_sync(500u, 500u, 200u, 3u, 10u), RCNODE_SYNC_ACK);
}

TEST_CASE(sync_both_changed_the_last_change_wins)
{
    /* The node changed 5s ago, the panel 60s ago: the node's. */
    CHECK_EQ(rcnode_shunt_sync(300u, 500u, 200u, 5u, 60000u),
             RCNODE_SYNC_ADOPT);
    /* The node changed 90s ago, the panel 2s ago: the panel's. */
    CHECK_EQ(rcnode_shunt_sync(300u, 500u, 200u, 90u, 2000u),
             RCNODE_SYNC_WRITE);
    /* The panel's change is from before its start: the node's known one
     * is younger. */
    CHECK_EQ(rcnode_shunt_sync(300u, 500u, 200u, 600u, UINT32_MAX),
             RCNODE_SYNC_ADOPT);
    /* The node's change is from before its start: the panel's known one. */
    CHECK_EQ(rcnode_shunt_sync(300u, 500u, 200u, RCNODE_AGE_NONE, 999999u),
             RCNODE_SYNC_WRITE);
    /* Neither known, or the same age: the node's. */
    CHECK_EQ(rcnode_shunt_sync(300u, 500u, 200u, RCNODE_AGE_NONE,
                               UINT32_MAX), RCNODE_SYNC_ADOPT);
    CHECK_EQ(rcnode_shunt_sync(300u, 500u, 200u, 4u, 4000u),
             RCNODE_SYNC_ADOPT);
    CHECK_EQ(rcnode_shunt_sync(300u, 500u, 200u, 4u, 4001u),
             RCNODE_SYNC_ADOPT);
    CHECK_EQ(rcnode_shunt_sync(300u, 500u, 200u, 4u, 3999u),
             RCNODE_SYNC_WRITE);
    /* The longest age the field carries still compares in ms. */
    CHECK_EQ(rcnode_shunt_sync(300u, 500u, 200u, 0xFFFEu, UINT32_MAX - 1u),
             RCNODE_SYNC_ADOPT);
}

TEST_CASE(sync_ages_past_the_node_cap_compare_as_equal)
{
    /* The node's age stops at 65,534s; the panel's saturates there too,
     * so neither side's cap makes it the younger: equal goes to the node. */
    const uint32_t cap_ms = 65534000u;
    CHECK_EQ(rcnode_shunt_sync(300u, 500u, 200u, 65534u, cap_ms),
             RCNODE_SYNC_ADOPT);
    CHECK_EQ(rcnode_shunt_sync(300u, 500u, 200u, 65534u, cap_ms + 1000u),
             RCNODE_SYNC_ADOPT);
    CHECK_EQ(rcnode_shunt_sync(300u, 500u, 200u, 65534u, 4000000000u),
             RCNODE_SYNC_ADOPT);
    /* Under the cap, the younger still wins either way. */
    CHECK_EQ(rcnode_shunt_sync(300u, 500u, 200u, 65534u, cap_ms - 1000u),
             RCNODE_SYNC_WRITE);
    CHECK_EQ(rcnode_shunt_sync(300u, 500u, 200u, 65533u, cap_ms),
             RCNODE_SYNC_ADOPT);
}

int main(void)
{
    RUN(a_name_is_16_characters_of_the_name_keyboard_trimmed);
    RUN(an_address_setting_is_auto_or_0x40_to_0x4f);
    RUN(config_has_its_bytes_and_comes_back_as_written);
    RUN(config_refuses_a_bad_name_padding_or_address);
    RUN(the_name_write_is_17_bytes_padded_and_checked);
    RUN(the_address_and_config_writes_and_their_roles);
    RUN(an_older_node_refuses_each_new_write_by_its_length_or_op);
    RUN(the_scan_response_carries_the_own_name_or_the_default);
    RUN(sync_one_side_changed_goes_its_way);
    RUN(sync_both_changed_the_last_change_wins);
    RUN(sync_ages_past_the_node_cap_compare_as_equal);
    return test_summary("rcnode_cfg");
}
