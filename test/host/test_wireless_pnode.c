/*
 * SETUP > WIRELESS > POWER NODE (wireless_pane.h, wireless_pnode.c): the
 * entry's rows, its settings and their defaults, its state words, SCAN FOR
 * NODE, BOUND NODE and NODE BATTERY.
 *
 * The failures this is about: a scan asked while ARMED (the radio's scan
 * window would starve the links), a refusal the operator does not see, a
 * found node that binds another one, INA228 SOURCE hidden while the node
 * is off (no way back to WIRED), a state word wider than its 25 cells in
 * either language, a battery read as a voltage when the node said it has
 * none, and a RESET that drops the bind.
 *
 * SPDX-License-Identifier: MIT
 */
#include "settings_rig.h"

/** @brief The band's status with the bench ARMED or SAFE. */
static void set_armed(bool armed)
{
    ui_bench_status_t st;
    memset(&st, 0, sizeof st);
    st.armed = armed;
    ui_router_set_status(&st);
}

/** @brief A view with two nodes found, the second bound. */
static pnode_view_t two_found(void)
{
    pnode_view_t v;
    memset(&v, 0, sizeof v);
    v.link = PNODE_LINK_UP;
    v.scan_done = true;
    v.found_n = 2u;
    snprintf(v.found[0].addr, sizeof v.found[0].addr, "7C:DF:A1:12:34:01 P");
    snprintf(v.found[0].name, sizeof v.found[0].name, "rcbench-pwr-3401");
    v.found[0].rssi = -52;
    snprintf(v.found[1].addr, sizeof v.found[1].addr, "7C:DF:A1:12:34:02 P");
    v.found[1].rssi = -77;
    snprintf(v.bound_addr, sizeof v.bound_addr, "7C:DF:A1:12:34:02 P");
    v.have_status = true;
    v.vbat_mv = 3916u;
    return v;
}

TEST_CASE(the_entry_shows_on_and_source_off_and_its_rows_on)
{
    fresh_hub();
    wireless_pane_set_pnode(NULL);
    CHECK_EQ(WL_ENTRY_PNODE, 3);    /* last, under BALANCE NODE */
    char buf[32];
    CHECK_STR_EQ(wireless_pane_name(WL_ENTRY_PNODE, buf, sizeof buf),
                 TR(PNODE_ENTRY));
    wl_row_t rows[WL_ENTRY_ROWS_MAX];
    CHECK_EQ(wireless_pane_rows(WL_ENTRY_PNODE, rows, WL_ENTRY_ROWS_MAX), 2);
    CHECK_EQ(rows[0].id, SET_PNODE_EN);
    CHECK_EQ(rows[1].id, SET_I228_SRC);
    /* The defaults: OFF and WIRED. */
    CHECK(!settings_get_bool(SET_PNODE_EN));
    CHECK_EQ(settings_get_int(SET_I228_SRC), 0);
    CHECK_STR_EQ(settings_def(SET_PNODE_EN)->key, "pnode_en");
    CHECK_STR_EQ(settings_def(SET_I228_SRC)->key, "i228_src");
    CHECK_EQ(settings_def(SET_I228_SRC)->option_count, 2u);
    CHECK_STR_EQ(settings_def(SET_I228_SRC)->options[0], "WIRED");
    CHECK_STR_EQ(settings_def(SET_I228_SRC)->options[1], "NODE");
    CHECK_EQ(settings_def(SET_PNODE_EN)->cat, SET_CAT_WIRELESS);
    CHECK_EQ(settings_def(SET_I228_SRC)->cat, SET_CAT_WIRELESS);
    CHECK_STR_EQ(settings_text(SET_TEXT_PNODE_ADDR), "");
    CHECK_STR_EQ(settings_text(SET_TEXT_PNODE_NAME), "");
    settings_set(SET_PNODE_EN, 1.0f);
    CHECK_EQ(wireless_pane_rows(WL_ENTRY_PNODE, rows, WL_ENTRY_ROWS_MAX), 5);
    CHECK(rows[2].custom == &wireless_pane_pnode_scan_row);
    CHECK(rows[3].custom == &wireless_pane_pnode_bind_row);
    CHECK(rows[4].custom == &wireless_pane_pnode_bat_row);
    CHECK_EQ(wireless_pane_entry_of(SET_I228_SRC), WL_ENTRY_PNODE);
    CHECK_EQ(wireless_pane_entry_of(SET_PNODE_EN), WL_ENTRY_PNODE);
    /* Opened from its tile, on its entry. */
    open_wireless(SET_PNODE_EN);
    CHECK_EQ(settings_screen_category(), SET_CAT_WIRELESS);
    CHECK_EQ(wireless_pane_selected(), WL_ENTRY_PNODE);
    CHECK_EQ(settings_screen_rows(SET_CAT_WIRELESS), 5);
}

TEST_CASE(a_node_scan_while_armed_is_refused_with_a_band_text)
{
    fresh_hub();
    settings_set(SET_PNODE_EN, 1.0f);
    pnode_cmd_t c;
    while (wireless_pane_take_pnode(&c)) { }
    set_armed(true);
    wireless_pane_pnode_scan_row.tap();
    CHECK(!wireless_pane_take_pnode(&c));
    CHECK_STR_EQ(ui_router_alert(), TR(PNODE_SCAN_ARMED));
    set_armed(false);
    wireless_pane_pnode_scan_row.tap();
    CHECK(wireless_pane_take_pnode(&c));
    CHECK_EQ(c.kind, PNODE_CMD_SCAN);
    CHECK(!wireless_pane_take_pnode(&c));
    for (int i = 0; i < 10; ++i) {
        wireless_pane_pnode_scan_row.tap();
    }
    int n = 0;
    while (wireless_pane_take_pnode(&c)) {
        ++n;
    }
    CHECK_EQ(n, 4);
    CHECK(!wireless_pane_take_pnode(NULL));
    /* The Dial's queue holds none of the node's commands. */
    dial_cmd_t d;
    CHECK(!wireless_pane_take_dial(&d));
}

TEST_CASE(a_found_node_binds_by_its_row)
{
    fresh_hub();
    const pnode_view_t v = two_found();
    wireless_pane_set_pnode(&v);
    const ui_setlist_custom_t *b = &wireless_pane_pnode_bind_row;
    CHECK_EQ(b->choices(), 2);
    CHECK_STR_EQ(b->choice(0), "rcbench-pwr-3401  -52dBm");
    CHECK_STR_EQ(b->choice(1), "7C:DF:A1:12:34:02 P  -77dBm");
    CHECK_STR_EQ(b->choice(2), "");
    CHECK_EQ(b->chosen(), 1);
    pnode_cmd_t c;
    while (wireless_pane_take_pnode(&c)) { }
    b->pick(0);
    CHECK(wireless_pane_take_pnode(&c));
    CHECK_EQ(c.kind, PNODE_CMD_BIND);
    CHECK_EQ(c.arg, 0u);
    b->pick(2);
    b->pick(-1);
    CHECK(!wireless_pane_take_pnode(&c));
    char val[48];
    b->value_text(val, sizeof val);
    CHECK_STR_EQ(val, "7C:DF:A1:12:34:02 P");
    wireless_pane_pnode_scan_row.value_text(val, sizeof val);
    char want[48];
    snprintf(want, sizeof want, TR(DIAL_SCAN_FOUND), 2u);
    CHECK_STR_EQ(val, want);
    pnode_view_t cut = two_found();
    cut.scan_cut = true;
    wireless_pane_set_pnode(&cut);
    wireless_pane_pnode_scan_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, TR(DIAL_SCAN_CUT));
    wireless_pane_set_pnode(NULL);
    CHECK_EQ(b->choices(), 0);
    b->value_text(val, sizeof val);
    CHECK_STR_EQ(val, TR(DIAL_BIND_NONE));
}

TEST_CASE(the_battery_row_says_what_the_status_says)
{
    fresh_hub();
    pnode_view_t v = two_found();
    char val[32];
    wireless_pane_set_pnode(&v);
    wireless_pane_pnode_bat_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, "3.91V");
    v.vbat_mv = 3405u;
    v.bat_low = true;
    wireless_pane_set_pnode(&v);
    wireless_pane_pnode_bat_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, "LOW 3.40V");
    v.vbat_mv = 0u;
    v.bat_low = false;
    v.bat_none = true;
    wireless_pane_set_pnode(&v);
    wireless_pane_pnode_bat_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, TR(PNODE_BAT_NONE));
    v.bat_err = true;
    wireless_pane_set_pnode(&v);
    wireless_pane_pnode_bat_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, TR(PNODE_BAT_ERR));
    v.have_status = false;
    wireless_pane_set_pnode(&v);
    wireless_pane_pnode_bat_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, TR(PNODE_BAT_UNKNOWN));
}

TEST_CASE(every_node_state_word_fits_25_cells_in_its_colour)
{
    fresh_hub();
    static const struct {
        uint8_t link;
        int ink;
    } k[] = {
        { PNODE_LINK_OFF, UI_C_TEXT_DIM },       { PNODE_LINK_STARTING, UI_C_TEXT_DIM },
        { PNODE_LINK_NO_BT, UI_C_WARN },         { PNODE_LINK_C6_FAILED, UI_C_WARN },
        { PNODE_LINK_NONE, UI_C_TEXT_DIM },      { PNODE_LINK_SCANNING, UI_C_TEXT_DIM },
        { PNODE_LINK_CONNECTING, UI_C_TEXT_DIM },
        { PNODE_LINK_NOT_NODE, UI_C_WARN },      { PNODE_LINK_WAITING, UI_C_TEXT_DIM },
        { PNODE_LINK_PROTO, UI_C_WARN },         { PNODE_LINK_SETUP, UI_C_WARN },
        { PNODE_LINK_NO_SENSOR, UI_C_WARN },     { PNODE_LINK_UP, UI_C_OK },
        { PNODE_LINK_STALE, UI_C_WARN },         { PNODE_LINK_LOST, UI_C_WARN },
    };
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
            pnode_view_t v;
            memset(&v, 0, sizeof v);
            v.link = k[i].link;
            v.proto = 255u;
            char buf[64];
            gfx_color_t ink = 0;
            const char *w = wireless_pane_pnode_word(&v, buf, sizeof buf, &ink);
            CHECK(w[0] != '\0');
            if (gfx_text_cells(w) > 25) {
                T_FAIL("link %u in language %d: \"%s\" is %d cells",
                       (unsigned)k[i].link, lang, w, gfx_text_cells(w));
            }
            CHECK_EQ(ink, ui_theme_color(k[i].ink));
        }
    }
    ui_text_set_language(UI_LANG_EN);
    pnode_view_t v = two_found();
    wireless_pane_set_pnode(&v);
    char buf[32];
    gfx_color_t ink = 0;
    CHECK_STR_EQ(wireless_pane_state(WL_ENTRY_PNODE, buf, sizeof buf, &ink),
                 TR(OFF));
    settings_set(SET_PNODE_EN, 1.0f);
    CHECK_STR_EQ(wireless_pane_state(WL_ENTRY_PNODE, buf, sizeof buf, &ink),
                 TR(DIAL_ST_UP));
    CHECK_EQ(ink, UI_OK);
    CHECK_STR_EQ(wireless_pane_pnode_word(&v, NULL, 0, NULL), "");
}

TEST_CASE(reset_keeps_the_bound_node)
{
    fresh_hub();
    settings_set(SET_PNODE_EN, 1.0f);
    settings_set(SET_I228_SRC, 1.0f);
    settings_set_text(SET_TEXT_PNODE_ADDR, "7C:DF:A1:12:34:02 P");
    wireless_pane_reset(WL_ENTRY_PNODE);
    CHECK(!settings_get_bool(SET_PNODE_EN));
    CHECK_EQ(settings_get_int(SET_I228_SRC), 0);
    CHECK_STR_EQ(settings_text(SET_TEXT_PNODE_ADDR), "7C:DF:A1:12:34:02 P");
}

/** @brief The bench's ARMED state as the settings ask it. */
static bool s_pn_armed;
/** @brief The settings' ARMED source. @return s_pn_armed. */
static bool pn_armed(void)
{
    return s_pn_armed;
}

TEST_CASE(the_source_does_not_change_while_armed)
{
    fresh_hub();
    settings_set_armed(pn_armed);
    settings_set(SET_PNODE_EN, 1.0f);
    s_pn_armed = true;
    CHECK(settings_safe_only(SET_I228_SRC));
    CHECK(settings_held(SET_I228_SRC));
    settings_set(SET_I228_SRC, 1.0f);
    settings_adjust(SET_I228_SRC, 1);
    CHECK_EQ(settings_get_int(SET_I228_SRC), 0);
    s_pn_armed = false;
    settings_set(SET_I228_SRC, 1.0f);
    CHECK_EQ(settings_get_int(SET_I228_SRC), 1);
    s_pn_armed = true;
    wireless_pane_reset(WL_ENTRY_PNODE);
    settings_set(SET_I228_SRC, 0.0f);
    CHECK_EQ(settings_get_int(SET_I228_SRC), 1);
    /* POWER NODE itself is not held: OFF while ARMED is the operator's. */
    CHECK(!settings_held(SET_PNODE_EN));
    s_pn_armed = false;
    settings_set_armed(NULL);
}

int main(void)
{
    RUN(the_entry_shows_on_and_source_off_and_its_rows_on);
    RUN(a_node_scan_while_armed_is_refused_with_a_band_text);
    RUN(a_found_node_binds_by_its_row);
    RUN(the_battery_row_says_what_the_status_says);
    RUN(every_node_state_word_fits_25_cells_in_its_colour);
    RUN(reset_keeps_the_bound_node);
    RUN(the_source_does_not_change_while_armed);
    return test_summary("wireless_pnode");
}
