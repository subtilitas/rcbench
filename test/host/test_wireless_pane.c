/*
 * SETUP > WIRELESS > DIAL (wireless_pane.h): the entry's rows, its state
 * words, SCAN FOR DIAL and BOUND DIAL.
 *
 * The failures this is about: a scan asked while ARMED (the radio's scan
 * window would starve the link that carries the knob), a refusal the
 * operator does not see, a found Dial that binds another one, a state word
 * wider than its 25 cells in either language, a state colour that says
 * linked when the Dial is not, and a RESET that drops the bind.
 *
 * SPDX-License-Identifier: MIT
 */
#include "settings_rig.h"

#include "wl_search.h"

/** @brief The band's status with the bench ARMED or SAFE. */
static void set_armed(bool armed)
{
    ui_bench_status_t st;
    memset(&st, 0, sizeof st);
    st.armed = armed;
    ui_router_set_status(&st);
}

/** @brief A view with two Dials found, the second bound. */
static dial_view_t two_found(void)
{
    dial_view_t v;
    memset(&v, 0, sizeof v);
    v.link = DIAL_LINK_UP;
    v.scan_done = true;
    v.found_n = 2u;
    snprintf(v.found[0].addr, sizeof v.found[0].addr, "A0:B1:C2:00:00:01 R");
    snprintf(v.found[0].name, sizeof v.found[0].name, "DIAL-0001");
    v.found[0].rssi = -48;
    snprintf(v.found[1].addr, sizeof v.found[1].addr, "A0:B1:C2:00:00:02 R");
    v.found[1].rssi = -71;
    snprintf(v.bound_addr, sizeof v.bound_addr, "A0:B1:C2:00:00:02 R");
    return v;
}

TEST_CASE(the_dial_entry_shows_its_rows_under_on)
{
    fresh_hub();
    wireless_pane_set_dial(NULL);
    CHECK_EQ(WL_ENTRY_DIAL, 1);
    wl_row_t rows[WL_ENTRY_ROWS_MAX];
    CHECK_EQ(wireless_pane_rows(WL_ENTRY_DIAL, rows, WL_ENTRY_ROWS_MAX), 1);
    CHECK_EQ(rows[0].id, SET_DIAL_EN);
    CHECK(!settings_get_bool(SET_DIAL_EN));
    settings_set(SET_DIAL_EN, 1.0f);
    CHECK_EQ(wireless_pane_rows(WL_ENTRY_DIAL, rows, WL_ENTRY_ROWS_MAX), 7);
    CHECK_EQ(rows[0].id, SET_DIAL_EN);
    CHECK(rows[1].custom == &wireless_pane_scan_row);
    CHECK(rows[2].custom == &wireless_pane_bind_row);
    CHECK(rows[3].custom == &wireless_pane_dial_model_row);
    CHECK(rows[4].custom == &wireless_pane_dial_batt_row);
    CHECK_EQ(rows[5].id, SET_DIAL_THR);
    CHECK_EQ(rows[6].id, SET_DIAL_SRV);
    CHECK_EQ(wireless_pane_entry_of(SET_DIAL_THR), WL_ENTRY_DIAL);
    CHECK_EQ(wireless_pane_entry_of(SET_MOTOR_POLES), -1);
    CHECK_EQ(wireless_pane_rows(WL_ENTRY_COUNT, rows, WL_ENTRY_ROWS_MAX), 0);
    /* The steps' defaults: 1% and 1 degree a detent. */
    CHECK_EQ(dial_step_tenths(settings_get_int(SET_DIAL_THR)), 10u);
    CHECK_EQ(dial_step_tenths(settings_get_int(SET_DIAL_SRV)), 10u);
    CHECK_EQ(dial_step_tenths(0), 5u);
    CHECK_EQ(dial_step_tenths(3), 50u);
    CHECK_EQ(dial_step_tenths(9), 10u);
    /* Opened from its tile, on its entry. */
    open_wireless(SET_DIAL_EN);
    CHECK_EQ(settings_screen_category(), SET_CAT_WIRELESS);
    CHECK_EQ(wireless_pane_selected(), WL_ENTRY_DIAL);
    CHECK_EQ(settings_screen_rows(SET_CAT_WIRELESS), 7);
}

/* DIAL MODEL and DIAL BATTERY from INFO's board and STATUS: an M5Stack
 * Dial of any release reads USB or NOT MEASURED; a K518 reads its cell,
 * LOW and EMPTY; nothing read reads NOT LINKED. */
TEST_CASE(the_dial_rows_show_its_board_and_its_cell)
{
    char out[32];
    dial_view_t v;
    memset(&v, 0, sizeof v);
    wireless_pane_set_dial(&v);
    wireless_pane_dial_model_row.value_text(out, sizeof out);
    CHECK_STR_EQ(out, "NOT LINKED");
    wireless_pane_dial_batt_row.value_text(out, sizeof out);
    CHECK_STR_EQ(out, "NOT LINKED");
    v.hw = DK_HW_DIAL11;
    v.have_status = true;
    v.st_flags = DK_ST_USB | DK_ST_BATT_UNKNOWN;
    wireless_pane_set_dial(&v);
    wireless_pane_dial_model_row.value_text(out, sizeof out);
    CHECK_STR_EQ(out, "M5STACK DIAL");
    wireless_pane_dial_batt_row.value_text(out, sizeof out);
    CHECK_STR_EQ(out, "USB");
    v.st_flags = DK_ST_BATT_UNKNOWN;
    wireless_pane_set_dial(&v);
    wireless_pane_dial_batt_row.value_text(out, sizeof out);
    CHECK_STR_EQ(out, "NOT MEASURED");
    v.hw = DK_HW_K518;
    v.st_flags = 0u;
    v.batt_mv = 3920u;
    wireless_pane_set_dial(&v);
    wireless_pane_dial_model_row.value_text(out, sizeof out);
    CHECK_STR_EQ(out, "GUITION K518");
    wireless_pane_dial_batt_row.value_text(out, sizeof out);
    CHECK_STR_EQ(out, "3.92V");
    v.st_flags = DK_ST_BATT_LOW;
    v.batt_mv = 3450u;
    wireless_pane_set_dial(&v);
    wireless_pane_dial_batt_row.value_text(out, sizeof out);
    CHECK_STR_EQ(out, "LOW 3.45V");
    v.st_flags = DK_ST_BATT_LOW | DK_ST_BATT_EMPTY;
    v.batt_mv = 3330u;
    wireless_pane_set_dial(&v);
    wireless_pane_dial_batt_row.value_text(out, sizeof out);
    CHECK_STR_EQ(out, "EMPTY 3.33V");
    ui_text_set_language(UI_LANG_DE);
    wireless_pane_dial_batt_row.value_text(out, sizeof out);
    CHECK_STR_EQ(out, "LEER 3.33V");
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_scan_while_armed_is_refused_with_a_band_text)
{
    fresh_hub();
    settings_set(SET_DIAL_EN, 1.0f);
    dial_cmd_t c;
    while (wireless_pane_take_dial(&c)) { }
    set_armed(true);
    wireless_pane_scan_row.tap();
    CHECK(!wireless_pane_take_dial(&c));
    CHECK_STR_EQ(ui_router_alert(), TR(DIAL_SCAN_ARMED));
    set_armed(false);
    wireless_pane_scan_row.tap();
    CHECK(wireless_pane_take_dial(&c));
    CHECK_EQ(c.kind, DIAL_CMD_SCAN);
    CHECK(!wireless_pane_take_dial(&c));
    /* A full queue drops, it does not overrun. */
    for (int i = 0; i < 10; ++i) {
        wireless_pane_scan_row.tap();
    }
    int n = 0;
    while (wireless_pane_take_dial(&c)) {
        ++n;
    }
    CHECK_EQ(n, 4);
    CHECK(!wireless_pane_take_dial(NULL));
    /* A tap while a scan runs asks for none and says why. */
    dial_view_t v;
    memset(&v, 0, sizeof v);
    v.link = DIAL_LINK_SCANNING;
    wireless_pane_set_dial(&v);
    ui_router_set_alert("");
    wireless_pane_scan_row.tap();
    CHECK(!wireless_pane_take_dial(&c));
    CHECK_STR_EQ(ui_router_alert(), TR(NODE_SCAN_RUNNING));
    wireless_pane_set_dial(NULL);
}

TEST_CASE(a_found_dial_shows_on_search_and_binds_by_its_row)
{
    fresh_hub();
    const dial_view_t v = two_found();
    wireless_pane_set_dial(&v);
    const ui_setlist_custom_t *b = &wireless_pane_bind_row;
    CHECK(b->choices == NULL && b->pick == NULL);
    CHECK(b->tap != NULL);
    if (b->tap == NULL) {
        return;
    }
    dial_cmd_t c;
    while (wireless_pane_take_dial(&c)) { }
    b->tap();
    CHECK_EQ(ui_router_current(), SCREEN_WL_SEARCH);
    CHECK(!wireless_pane_take_dial(&c));
    CHECK_EQ(wl_search_device()->kind, TX_DIAL_ENTRY);
    wls_view_t s;
    wl_search_view(&s);
    CHECK_EQ(s.found_n, 2u);
    char line[64];
    CHECK_STR_EQ(wl_search_dev_line(&s.found[0], line, sizeof line),
                 "DIAL-0001  -48dBm");
    CHECK_STR_EQ(wl_search_dev_line(&s.found[1], line, sizeof line),
                 "A0:B1:C2:00:00:02 R  -71dBm");
    CHECK_STR_EQ(s.bound_addr, "A0:B1:C2:00:00:02 R");
    /* The entry's bind by its row, as SEARCH's BIND asks it. */
    wl_search_device()->bind(0u);
    CHECK(wireless_pane_take_dial(&c));
    CHECK_EQ(c.kind, DIAL_CMD_BIND);
    CHECK_EQ(c.arg, 0u);
    char val[48];
    b->value_text(val, sizeof val);
    CHECK_STR_EQ(val, "A0:B1:C2:00:00:02 R");
    wireless_pane_scan_row.value_text(val, sizeof val);
    char want[48];
    snprintf(want, sizeof want, TR(DIAL_SCAN_FOUND), 2u);
    CHECK_STR_EQ(val, want);
    /* A scan the switch to ARMED stopped says so. */
    dial_view_t cut = two_found();
    cut.scan_cut = true;
    wireless_pane_set_dial(&cut);
    wireless_pane_scan_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, TR(DIAL_SCAN_CUT));
    /* No scan yet: nothing listed, and NONE bound. */
    wireless_pane_set_dial(NULL);
    wl_search_view(&s);
    CHECK_EQ(s.found_n, 0u);
    b->value_text(val, sizeof val);
    CHECK_STR_EQ(val, TR(DIAL_BIND_NONE));
    wireless_pane_scan_row.value_text(val, sizeof val);
    CHECK_STR_EQ(val, TR(DIAL_SCAN_GO));
}

TEST_CASE(every_state_word_fits_25_cells_in_its_colour)
{
    fresh_hub();
    static const struct {
        uint8_t link;
        int ink;
    } k[] = {
        { DIAL_LINK_OFF, UI_C_TEXT_DIM },     { DIAL_LINK_STARTING, UI_C_TEXT_DIM },
        { DIAL_LINK_NO_BT, UI_C_WARN },       { DIAL_LINK_C6_FAILED, UI_C_WARN },
        { DIAL_LINK_NONE, UI_C_TEXT_DIM },    { DIAL_LINK_SCANNING, UI_C_TEXT_DIM },
        { DIAL_LINK_CONNECTING, UI_C_TEXT_DIM },
        { DIAL_LINK_NOT_DIAL, UI_C_WARN },    { DIAL_LINK_WAITING, UI_C_TEXT_DIM },
        { DIAL_LINK_PROTO, UI_C_WARN },       { DIAL_LINK_UP, UI_C_OK },
        { DIAL_LINK_DEGRADED, UI_C_WARN },    { DIAL_LINK_LOST, UI_C_WARN },
    };
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
            dial_view_t v;
            memset(&v, 0, sizeof v);
            v.link = k[i].link;
            v.proto = 255u;
            char buf[64];
            gfx_color_t ink = 0;
            const char *w = wireless_pane_dial_word(&v, buf, sizeof buf, &ink);
            CHECK(w[0] != '\0');
            if (gfx_text_cells(w) > 25) {
                T_FAIL("link %u in language %d: \"%s\" is %d cells",
                       (unsigned)k[i].link, lang, w, gfx_text_cells(w));
            }
            CHECK_EQ(ink, ui_theme_color(k[i].ink));
        }
    }
    ui_text_set_language(UI_LANG_EN);
    /* The entry's state: OFF while Dial is off, the link's word on. */
    dial_view_t v = two_found();
    wireless_pane_set_dial(&v);
    char buf[32];
    gfx_color_t ink = 0;
    CHECK_STR_EQ(wireless_pane_state(WL_ENTRY_DIAL, buf, sizeof buf, &ink),
                 TR(OFF));
    settings_set(SET_DIAL_EN, 1.0f);
    CHECK_STR_EQ(wireless_pane_state(WL_ENTRY_DIAL, buf, sizeof buf, &ink),
                 TR(DIAL_ST_UP));
    CHECK_EQ(ink, UI_OK);
    CHECK_STR_EQ(wireless_pane_dial_word(&v, NULL, 0, NULL), "");
}

TEST_CASE(reset_keeps_the_bind)
{
    fresh_hub();
    settings_set(SET_DIAL_EN, 1.0f);
    settings_set(SET_DIAL_THR, 3.0f);
    settings_set_text(SET_TEXT_DIAL_ADDR, "A0:B1:C2:00:00:02 R");
    wireless_pane_reset(WL_ENTRY_DIAL);
    CHECK(!settings_get_bool(SET_DIAL_EN));
    CHECK_EQ(settings_get_int(SET_DIAL_THR), 1);
    CHECK_STR_EQ(settings_text(SET_TEXT_DIAL_ADDR), "A0:B1:C2:00:00:02 R");
}

/* SCAN FOR DIAL opens SEARCH and asks for a scan; BOUND DIAL
 * opens it on the last list and asks for none.  While ARMED SCAN FOR
 * DIAL still opens SEARCH and asks for no scan, with the band's
 * text.  A full scan lists all 8 devices, and the rows carry no choice
 * list. */
TEST_CASE(the_rows_open_search)
{
    fresh_hub();
    dial_cmd_t c;
    while (wireless_pane_take_dial(&c)) { }
    dial_view_t v = two_found();
    wireless_pane_set_dial(&v);
    const ui_setlist_custom_t *sr = &wireless_pane_scan_row;
    const ui_setlist_custom_t *br = &wireless_pane_bind_row;
    CHECK(sr->choices == NULL && sr->choice == NULL && sr->pick == NULL);
    CHECK(br->choices == NULL && br->choice == NULL && br->pick == NULL);
    sr->tap();
    CHECK_EQ(ui_router_current(), SCREEN_WL_SEARCH);
    CHECK(wireless_pane_take_dial(&c));
    CHECK_EQ(c.kind, DIAL_CMD_SCAN);
    CHECK(!wireless_pane_take_dial(&c));
    ui_router_goto(SCREEN_SETUP);
    br->tap();
    CHECK_EQ(ui_router_current(), SCREEN_WL_SEARCH);
    CHECK(!wireless_pane_take_dial(&c));
    /* SCAN FOR DIAL while ARMED. */
    ui_router_goto(SCREEN_SETUP);
    set_armed(true);
    sr->tap();
    CHECK_EQ(ui_router_current(), SCREEN_WL_SEARCH);
    CHECK(!wireless_pane_take_dial(&c));
    CHECK_STR_EQ(ui_router_alert(), TR(DIAL_SCAN_ARMED));
    set_armed(false);
    /* A full scan: every device listed. */
    v = two_found();
    v.found_n = 8u;
    wireless_pane_set_dial(&v);
    wls_view_t s;
    wl_search_view(&s);
    CHECK_EQ(s.found_n, 8u);
    wl_search_device()->bind(7u);
    CHECK(wireless_pane_take_dial(&c));
    CHECK_EQ(c.kind, DIAL_CMD_BIND);
    CHECK_EQ(c.arg, 7u);
    wireless_pane_set_dial(NULL);
}

/*
 * A WIRELESS row's label and help are clipped 10px short of its "-" key
 * (ui_setlist_draw.c): from the pane's text at PANE_X 240 + 16 to MINUS_X
 * 704 - 10 (settings_screen_int.h, ui_setlist_int.h), 438px, 21 cells of
 * the 20 x 36 name face and 43 of the 10 x 20 help face.  Every setting
 * any WIRELESS entry shows fits, in every language.
 */
TEST_CASE(every_wireless_row_fits_left_of_its_keys)
{
    const int room = 704 - 10 - (240 + 16);
    for (int l = 0; l < UI_LANG_COUNT; ++l) {
        ui_text_set_language((ui_lang_t)l);
        for (int i = 0; i < SETTING_COUNT; ++i) {
            if (wireless_pane_entry_of((setting_id_t)i) < 0) {
                continue;
            }
            const char *label = ui_setting_label((setting_id_t)i);
            const char *help = ui_setting_help((setting_id_t)i);
            if (gfx_text_cells(label) * 20 > room
                || gfx_text_cells(help) * 10 > room) {
                T_FAIL("language %d %s: \"%s\" / \"%s\"", l,
                       settings_def((setting_id_t)i)->key, label, help);
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(the_dial_entry_shows_its_rows_under_on);
    RUN(a_scan_while_armed_is_refused_with_a_band_text);
    RUN(a_found_dial_shows_on_search_and_binds_by_its_row);
    RUN(the_rows_open_search);
    RUN(every_state_word_fits_25_cells_in_its_colour);
    RUN(reset_keeps_the_bind);
    RUN(every_wireless_row_fits_left_of_its_keys);
    RUN(the_dial_rows_show_its_board_and_its_cell);
    return test_summary("wireless_pane");
}
