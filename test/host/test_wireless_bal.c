/*
 * SETUP > WIRELESS > BALANCE NODE (wireless_bal.h): the entry's rows, its
 * settings, its state words, SCAN FOR NODE, BOUND NODE and NODE BATTERY.
 *
 * The failures this is about: a scan asked while ARMED (the scan window
 * would starve the link that carries the Dial), a refusal the operator
 * does not see, a found node that binds another one, a state word wider
 * than its 25 cells in either language, a state colour that says linked
 * when the node is not, a battery read off an old link, and a RESET that
 * drops the bind.
 *
 * SPDX-License-Identifier: MIT
 */
#include "settings_rig.h"

#include "wireless_bal.h"

/** @brief The band's status with the bench ARMED or SAFE. */
static void set_armed(bool armed)
{
    ui_bench_status_t st;
    memset(&st, 0, sizeof st);
    st.armed = armed;
    ui_router_set_status(&st);
}

/** @brief A view with two nodes found, the second bound. */
static bal_view_t two_found(void)
{
    bal_view_t v;
    memset(&v, 0, sizeof v);
    v.link = BAL_LINK_UP;
    v.scan_done = true;
    v.found_n = 2u;
    snprintf(v.found[0].addr, sizeof v.found[0].addr, "A0:B1:C2:00:00:01 R");
    snprintf(v.found[0].name, sizeof v.found[0].name, "rcbench-bal-0001");
    v.found[0].rssi = -48;
    snprintf(v.found[1].addr, sizeof v.found[1].addr, "A0:B1:C2:00:00:02 R");
    v.found[1].rssi = -71;
    snprintf(v.bound_addr, sizeof v.bound_addr, "A0:B1:C2:00:00:02 R");
    return v;
}

TEST_CASE(the_entry_shows_its_rows_under_on)
{
    fresh_hub();
    wireless_pane_set_bal(NULL);
    wl_row_t rows[WL_ENTRY_ROWS_MAX];
    CHECK_EQ(wireless_pane_rows(WL_ENTRY_BAL, rows, WL_ENTRY_ROWS_MAX), 1);
    CHECK_EQ(rows[0].id, SET_BNODE_EN);
    CHECK(rows[0].custom == NULL);
    CHECK(!settings_get_bool(SET_BNODE_EN));
    settings_set(SET_BNODE_EN, 1.0f);
    CHECK_EQ(wireless_pane_rows(WL_ENTRY_BAL, rows, WL_ENTRY_ROWS_MAX), 5);
    CHECK(rows[1].custom == &wireless_bal_scan_row);
    CHECK(rows[2].custom == &wireless_bal_bind_row);
    CHECK(rows[3].custom == &wireless_bal_bat_row);
    CHECK_EQ(rows[4].id, SET_BAL_TRIAL);
    CHECK(rows[4].custom == NULL);
    /* Counting needs no room; a short room counts all. */
    CHECK_EQ(wireless_pane_rows(WL_ENTRY_BAL, NULL, 0), 5);
    CHECK_EQ(wireless_pane_rows(WL_ENTRY_BAL, rows, 2), 5);
    CHECK_EQ(wireless_pane_entry_of(SET_BNODE_EN), WL_ENTRY_BAL);
    CHECK_EQ(wireless_pane_entry_of(SET_BAL_TRIAL), WL_ENTRY_BAL);
    char name[32];
    CHECK_STR_EQ(wireless_pane_name(WL_ENTRY_BAL, name, sizeof name),
                 TR(BNODE_ENTRY));
    /* The settings: keys, the trial mass's range, step and default. */
    CHECK_STR_EQ(settings_def(SET_BNODE_EN)->key, "bnode_en");
    CHECK_STR_EQ(settings_def(SET_BAL_TRIAL)->key, "bal_trial_g");
    CHECK_NEAR(settings_get(SET_BAL_TRIAL), 0.5, 1e-6);
    settings_set(SET_BAL_TRIAL, 40.0f);
    CHECK_NEAR(settings_get(SET_BAL_TRIAL), 20.0, 1e-6);
    settings_set(SET_BAL_TRIAL, 0.0f);
    CHECK_NEAR(settings_get(SET_BAL_TRIAL), 0.01, 1e-6);
    CHECK_STR_EQ(settings_text(SET_TEXT_BNODE_ADDR), "");
    CHECK_STR_EQ(settings_text(SET_TEXT_BNODE_NAME), "");
    /* Opened from its tile, on its entry. */
    open_wireless(SET_BNODE_EN);
    CHECK_EQ(settings_screen_category(), SET_CAT_WIRELESS);
    CHECK_EQ(wireless_pane_selected(), WL_ENTRY_BAL);
    CHECK_EQ(settings_screen_rows(SET_CAT_WIRELESS), 5);
}

TEST_CASE(a_scan_while_armed_is_refused_with_a_band_text)
{
    fresh_hub();
    settings_set(SET_BNODE_EN, 1.0f);
    bal_cmd_t c;
    while (wireless_pane_take_bal(&c)) { }
    set_armed(true);
    wireless_bal_scan_row.tap();
    CHECK(!wireless_pane_take_bal(&c));
    CHECK_STR_EQ(ui_router_alert(), TR(BNODE_SCAN_ARMED));
    set_armed(false);
    wireless_bal_scan_row.tap();
    CHECK(wireless_pane_take_bal(&c));
    CHECK_EQ(c.kind, BAL_CMD_SCAN);
    CHECK(!wireless_pane_take_bal(&c));
    for (int i = 0; i < 10; ++i) {
        wireless_bal_scan_row.tap();
    }
    int n = 0;
    while (wireless_pane_take_bal(&c)) {
        ++n;
    }
    CHECK_EQ(n, 4);
    CHECK(!wireless_pane_take_bal(NULL));
}

TEST_CASE(a_found_node_binds_by_its_row)
{
    fresh_hub();
    const bal_view_t v = two_found();
    wireless_pane_set_bal(&v);
    const ui_setlist_custom_t *b = &wireless_bal_bind_row;
    CHECK_EQ(b->choices(), 2);
    CHECK_STR_EQ(b->choice(0), "rcbench-bal-0001  -48dBm");
    CHECK_STR_EQ(b->choice(1), "A0:B1:C2:00:00:02 R  -71dBm");
    CHECK_STR_EQ(b->choice(2), "");
    CHECK_EQ(b->chosen(), 1);
    bal_cmd_t c;
    while (wireless_pane_take_bal(&c)) { }
    b->pick(0);
    CHECK(wireless_pane_take_bal(&c));
    CHECK_EQ(c.kind, BAL_CMD_BIND);
    CHECK_EQ(c.arg, 0u);
    b->pick(2);
    b->pick(-1);
    CHECK(!wireless_pane_take_bal(&c));
    char val[48];
    b->value_text(val, sizeof val);
    CHECK_STR_EQ(val, "A0:B1:C2:00:00:02 R");
    bal_view_t cut = two_found();
    cut.scan_cut = true;
    wireless_pane_set_bal(&cut);
    wireless_bal_scan_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, TR(DIAL_SCAN_CUT));
    wireless_pane_set_bal(NULL);
    CHECK_EQ(b->choices(), 0);
    b->value_text(val, sizeof val);
    CHECK_STR_EQ(val, TR(DIAL_BIND_NONE));
    wireless_bal_scan_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, TR(DIAL_SCAN_GO));
}

/* The battery row: "--" before a STATUS on this link, the cell in volts
 * to 0.01V, LOW with the flag, NO READING without a divider or after a
 * failed read. */
TEST_CASE(the_battery_row_reads_the_status)
{
    fresh_hub();
    bal_view_t v = two_found();
    char val[48];
    wireless_pane_set_bal(&v);
    wireless_bal_bat_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, "--");
    v.bat_known = true;
    v.bat_mv = 3912u;
    wireless_pane_set_bal(&v);
    wireless_bal_bat_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, "3.91V");
    v.bat_mv = 3405u;
    v.bat_flags = RCNODE_ST_BAT_LOW;
    wireless_pane_set_bal(&v);
    wireless_bal_bat_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, "3.40V LOW");
    v.bat_mv = 0u;
    v.bat_flags = RCNODE_ST_BAT_NONE;
    wireless_pane_set_bal(&v);
    wireless_bal_bat_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, TR(BNODE_BAT_NONE));
    v.bat_flags = RCNODE_ST_BAT_ERR;
    wireless_pane_set_bal(&v);
    wireless_bal_bat_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, TR(BNODE_BAT_NONE));
    CHECK(wireless_bal_bat_row.tap == NULL);
}

TEST_CASE(every_state_word_fits_25_cells_in_its_colour)
{
    fresh_hub();
    static const struct {
        uint8_t link;
        int ink;
    } k[] = {
        { BAL_LINK_OFF, UI_C_TEXT_DIM },      { BAL_LINK_STARTING, UI_C_TEXT_DIM },
        { BAL_LINK_NO_BT, UI_C_WARN },        { BAL_LINK_C6_FAILED, UI_C_WARN },
        { BAL_LINK_NONE, UI_C_TEXT_DIM },     { BAL_LINK_SCANNING, UI_C_TEXT_DIM },
        { BAL_LINK_CONNECTING, UI_C_TEXT_DIM },
        { BAL_LINK_NOT_NODE, UI_C_WARN },     { BAL_LINK_WAITING, UI_C_TEXT_DIM },
        { BAL_LINK_WRONG, UI_C_WARN },        { BAL_LINK_QUIET, UI_C_WARN },
        { BAL_LINK_UP, UI_C_OK },
    };
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
            bal_view_t v;
            memset(&v, 0, sizeof v);
            v.link = k[i].link;
            v.info_role = 255u;
            v.info_proto = 255u;
            char buf[64];
            gfx_color_t ink = 0;
            const char *w = wireless_pane_bal_word(&v, buf, sizeof buf, &ink);
            CHECK(w[0] != '\0');
            if (gfx_text_cells(w) > 25) {
                T_FAIL("link %u in language %d: \"%s\" is %d cells",
                       (unsigned)k[i].link, lang, w, gfx_text_cells(w));
            }
            CHECK_EQ(ink, ui_theme_color(k[i].ink));
        }
    }
    ui_text_set_language(UI_LANG_EN);
    bal_view_t v = two_found();
    wireless_pane_set_bal(&v);
    char buf[32];
    gfx_color_t ink = 0;
    CHECK_STR_EQ(wireless_pane_state(WL_ENTRY_BAL, buf, sizeof buf, &ink),
                 TR(OFF));
    settings_set(SET_BNODE_EN, 1.0f);
    CHECK_STR_EQ(wireless_pane_state(WL_ENTRY_BAL, buf, sizeof buf, &ink),
                 TR(BNODE_ST_UP));
    CHECK_EQ(ink, UI_OK);
    CHECK_STR_EQ(wireless_pane_bal_word(&v, NULL, 0, NULL), "");
}

TEST_CASE(reset_keeps_the_bind)
{
    fresh_hub();
    settings_set(SET_BNODE_EN, 1.0f);
    settings_set(SET_BAL_TRIAL, 1.25f);
    settings_set_text(SET_TEXT_BNODE_ADDR, "A0:B1:C2:00:00:02 R");
    wireless_pane_reset(WL_ENTRY_BAL);
    CHECK(!settings_get_bool(SET_BNODE_EN));
    CHECK_NEAR(settings_get(SET_BAL_TRIAL), 0.5, 1e-6);
    CHECK_STR_EQ(settings_text(SET_TEXT_BNODE_ADDR), "A0:B1:C2:00:00:02 R");
}

int main(void)
{
    RUN(the_entry_shows_its_rows_under_on);
    RUN(a_scan_while_armed_is_refused_with_a_band_text);
    RUN(a_found_node_binds_by_its_row);
    RUN(the_battery_row_reads_the_status);
    RUN(every_state_word_fits_25_cells_in_its_colour);
    RUN(reset_keeps_the_bind);
    return test_summary("wireless_bal");
}
