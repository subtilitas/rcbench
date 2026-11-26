/*
 * SETUP > WIRELESS > POWER NODE's NODE NAME and INA228 ADDRESS
 * (wireless_pnode.c), the node's own settings written over the link, and
 * BOUND NODE showing the node's name.
 *
 * The failures this is about: a change asked while ARMED or without the
 * node's settings, a refusal the operator does not see, a name typed that
 * the node would refuse, a name that cannot be cleared from the panel, an
 * address step that leaves AUTO and 0x40 to 0x4F, and a bound node shown
 * by its address when it has a name.
 *
 * SPDX-License-Identifier: MIT
 */
#include "settings_rig.h"

#include "node_cfg_link.h"
#include "rcnode.h"
#include "ui_textkey.h"

/** @brief The band's status with the bench ARMED or SAFE. */
static void set_armed(bool armed)
{
    ui_bench_status_t st;
    memset(&st, 0, sizeof st);
    st.armed = armed;
    ui_router_set_status(&st);
}

/** @brief A linked node with its settings known. */
static pnode_view_t linked(const char *name, uint8_t i228)
{
    pnode_view_t v;
    memset(&v, 0, sizeof v);
    v.link = PNODE_LINK_UP;
    snprintf(v.bound_addr, sizeof v.bound_addr, "7C:DF:A1:12:34:56 P");
    snprintf(v.bound_name, sizeof v.bound_name, "%s", name);
    v.have_info = true;
    v.have_cfg = true;
    snprintf(v.node_name, sizeof v.node_name, "%s", name);
    v.i228_set = i228;
    return v;
}

/** @brief Drop every queued command. */
static void drain(void)
{
    pnode_cmd_t c;
    while (wireless_pane_take_pnode(&c)) {
    }
}

TEST_CASE(the_rows_say_what_the_node_holds)
{
    fresh_hub();
    char val[32];
    pnode_view_t v;
    memset(&v, 0, sizeof v);
    wireless_pane_set_pnode(&v);
    wireless_pane_pnode_name_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, TR(PNODE_BAT_UNKNOWN));
    v.have_info = true;             /* linked, a node built before CONFIG */
    wireless_pane_set_pnode(&v);
    wireless_pane_pnode_name_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, TR(PNODE_CFG_OLD));
    wireless_pane_pnode_addr_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, TR(PNODE_CFG_OLD));
    v = linked("XT90 MAIN", RCNODE_I228_AUTO);
    wireless_pane_set_pnode(&v);
    wireless_pane_pnode_name_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, "XT90 MAIN");
    wireless_pane_pnode_addr_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, TR(PNODE_ADDR_AUTO));
    v.i228_set = 0x4Fu;
    wireless_pane_set_pnode(&v);
    wireless_pane_pnode_addr_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, "0x4F");
    /* BOUND NODE shows the name, not the address. */
    wireless_pane_pnode_bind_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, "XT90 MAIN");
}

TEST_CASE(address_steps_go_auto_0x40_to_0x4f_and_around)
{
    CHECK_EQ(wireless_pane_pnode_addr_next(RCNODE_I228_AUTO, 1), 0x40u);
    CHECK_EQ(wireless_pane_pnode_addr_next(0x40u, 1), 0x41u);
    CHECK_EQ(wireless_pane_pnode_addr_next(0x4Fu, 1), RCNODE_I228_AUTO);
    CHECK_EQ(wireless_pane_pnode_addr_next(RCNODE_I228_AUTO, -1), 0x4Fu);
    CHECK_EQ(wireless_pane_pnode_addr_next(0x40u, -1), RCNODE_I228_AUTO);
    CHECK_EQ(wireless_pane_pnode_addr_next(0x77u, 1), 0x40u);
    uint8_t a = RCNODE_I228_AUTO;
    for (int i = 0; i < 17; ++i) {
        a = wireless_pane_pnode_addr_next(a, 1);
        CHECK(rcnode_i228_ok(a));
    }
    CHECK_EQ(a, RCNODE_I228_AUTO);
}

TEST_CASE(an_address_step_queues_the_write_while_safe)
{
    fresh_hub();
    drain();
    set_armed(false);
    pnode_view_t v = linked("N", 0x45u);
    wireless_pane_set_pnode(&v);
    wireless_pane_pnode_addr_row.step(1);
    pnode_cmd_t c;
    CHECK(wireless_pane_take_pnode(&c));
    CHECK_EQ(c.kind, PNODE_CMD_I228);
    CHECK_EQ(c.arg, 0x46u);
    CHECK(!wireless_pane_take_pnode(&c));
    CHECK(wireless_pane_pnode_addr_row.locked() == NULL);
}

TEST_CASE(two_quick_steps_go_two_addresses_on_until_the_echo)
{
    fresh_hub();
    drain();
    set_armed(false);
    wireless_pane_set_pnode(NULL);
    pnode_view_t v = linked("N", 0x45u);
    wireless_pane_set_pnode(&v);
    wireless_pane_pnode_addr_row.step(1);
    wireless_pane_pnode_addr_row.step(1);   /* before the node's CONFIG */
    pnode_cmd_t c;
    CHECK(wireless_pane_take_pnode(&c));
    CHECK_EQ(c.arg, 0x46u);
    CHECK(wireless_pane_take_pnode(&c));
    CHECK_EQ(c.arg, 0x47u);
    /* The first echo: 0x46.  A step now goes from the node's answer. */
    v.i228_set = 0x46u;
    wireless_pane_set_pnode(&v);
    v.i228_set = 0x47u;
    wireless_pane_set_pnode(&v);
    wireless_pane_pnode_addr_row.step(-1);
    CHECK(wireless_pane_take_pnode(&c));
    CHECK_EQ(c.arg, 0x46u);
}

TEST_CASE(a_refused_request_or_a_dropped_write_shows_on_the_band)
{
    fresh_hub();
    pnode_view_t v = linked("N", 0u);
    wireless_pane_set_pnode(&v);
    v.cfg_req_no = 1u;
    v.cfg_req_why = NCL_REQ_UNKNOWN;
    v.have_cfg = false;               /* the link dropped before the write */
    wireless_pane_set_pnode(&v);
    CHECK_STR_EQ(ui_router_alert(), TR(PNODE_CFG_NONE));
    v.cfg_req_no = 2u;
    v.cfg_req_why = NCL_REQ_ARMED;
    wireless_pane_set_pnode(&v);
    CHECK_STR_EQ(ui_router_alert(), TR(PNODE_CFG_ARMED));
    const uint32_t n0 = ui_router_alert_number();
    v.cfg_dropped = 1u;
    wireless_pane_set_pnode(&v);
    CHECK(ui_router_alert_number() != n0);
    CHECK_STR_EQ(ui_router_alert(), TR(PNODE_CFG_DROPPED));
}

TEST_CASE(no_change_while_armed_or_without_the_settings)
{
    fresh_hub();
    drain();
    pnode_view_t v = linked("N", 0x45u);
    wireless_pane_set_pnode(&v);
    set_armed(true);
    const uint32_t n0 = ui_router_alert_number();
    wireless_pane_pnode_addr_row.step(1);
    wireless_pane_pnode_name_row.tap();
    pnode_cmd_t c;
    CHECK(!wireless_pane_take_pnode(&c));
    CHECK(ui_router_alert_number() != n0);
    CHECK_STR_EQ(ui_router_alert(), TR(PNODE_CFG_ARMED));
    CHECK(!settings_screen_textkey()->open);
    CHECK_STR_EQ(wireless_pane_pnode_name_row.locked(), TR(MO_SET_SAFE_ONLY));
    set_armed(false);
    v.have_cfg = false;               /* an older node */
    wireless_pane_set_pnode(&v);
    wireless_pane_pnode_addr_row.step(1);
    CHECK_STR_EQ(ui_router_alert(), TR(PNODE_CFG_NONE));
    v.have_info = false;              /* not linked */
    wireless_pane_set_pnode(&v);
    wireless_pane_pnode_name_row.tap();
    CHECK_STR_EQ(ui_router_alert(), TR(PNODE_CFG_UNLINKED));
    CHECK(!wireless_pane_take_pnode(&c));
}

TEST_CASE(a_refused_setting_says_so_on_the_band_once)
{
    fresh_hub();
    pnode_view_t v = linked("N", 0u);
    wireless_pane_set_pnode(&v);
    const uint32_t n0 = ui_router_alert_number();
    v.cfg_refused = 1u;
    wireless_pane_set_pnode(&v);
    CHECK(ui_router_alert_number() != n0);
    CHECK_STR_EQ(ui_router_alert(), TR(PNODE_CFG_REFUSED));
    const uint32_t n1 = ui_router_alert_number();
    wireless_pane_set_pnode(&v);
    CHECK_EQ(ui_router_alert_number(), n1);
}

/** @brief Tap a key of the open keyboard. */
static void key(int k)
{
    const gfx_rect_t r = ui_textkey_key_rect(settings_screen_textkey(), k);
    tap(r.x + r.w / 2, UI_BAND_H + r.y + r.h / 2);
}

TEST_CASE(name_opens_the_keyboard_and_ok_queues_it_even_empty)
{
    fresh_hub();
    drain();
    set_armed(false);
    open_wireless(SET_PNODE_EN);
    pnode_view_t v = linked("XT90 MAIN", 0u);
    wireless_pane_set_pnode(&v);
    wireless_pane_pnode_name_row.tap();
    const ui_textkey_t *k = settings_screen_textkey();
    CHECK(k->open);
    CHECK_EQ(k->max_len, (int)RCNODE_UNAME_MAX);
    CHECK_STR_EQ(k->text, "XT90 MAIN");
    key(UI_TK_OK);
    pnode_cmd_t c;
    memset(&c, 0, sizeof c);
    CHECK(wireless_pane_take_pnode(&c));
    CHECK_EQ(c.kind, PNODE_CMD_NAME);
    CHECK_STR_EQ(c.text, "XT90 MAIN");
    CHECK(rcnode_uname_ok(c.text));
    /* CLR, OK: "" clears the node's name. */
    wireless_pane_pnode_name_row.tap();
    key(UI_TK_CLR);
    key(UI_TK_OK);
    CHECK(wireless_pane_take_pnode(&c));
    CHECK_STR_EQ(c.text, "");
    /* A node without its own name starts the keyboard empty, not on
     * "rcbench-pwr-3456", which the node would refuse. */
    v = linked("rcbench-pwr-3456", 0u);
    wireless_pane_set_pnode(&v);
    wireless_pane_pnode_name_row.tap();
    CHECK_STR_EQ(settings_screen_textkey()->text, "");
    key(UI_TK_CANCEL);
    CHECK(!wireless_pane_take_pnode(&c));
}

TEST_CASE(every_key_of_the_name_keyboard_types_a_node_name)
{
    ui_textkey_t k;
    memset(&k, 0, sizeof k);
    ui_textkey_open_node_name(&k, gfx_rect_make(0, 0, 1024, 540), "T", "");
    for (int i = 0; i < UI_TEXTKEY_CHARS; ++i) {
        const char *l = ui_textkey_key_label(&k, i);
        if (l[0] != '\0' && l[1] == '\0') {
            const char one[2] = { l[0], '\0' };
            if (!rcnode_uname_ok(one)) {
                T_FAIL("key %d types '%c', which a node refuses", i, l[0]);
            }
        }
    }
}

int main(void)
{
    RUN(the_rows_say_what_the_node_holds);
    RUN(address_steps_go_auto_0x40_to_0x4f_and_around);
    RUN(an_address_step_queues_the_write_while_safe);
    RUN(two_quick_steps_go_two_addresses_on_until_the_echo);
    RUN(a_refused_request_or_a_dropped_write_shows_on_the_band);
    RUN(no_change_while_armed_or_without_the_settings);
    RUN(a_refused_setting_says_so_on_the_band_once);
    RUN(name_opens_the_keyboard_and_ok_queues_it_even_empty);
    RUN(every_key_of_the_name_keyboard_types_a_node_name);
    return test_summary("wireless_pnode_cfg");
}
