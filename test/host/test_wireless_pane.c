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
    CHECK_EQ(wireless_pane_rows(WL_ENTRY_DIAL, rows, WL_ENTRY_ROWS_MAX), 5);
    CHECK_EQ(rows[0].id, SET_DIAL_EN);
    CHECK(rows[1].custom == &wireless_pane_scan_row);
    CHECK(rows[2].custom == &wireless_pane_bind_row);
    CHECK_EQ(rows[3].id, SET_DIAL_THR);
    CHECK_EQ(rows[4].id, SET_DIAL_SRV);
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
    CHECK_EQ(settings_screen_rows(SET_CAT_WIRELESS), 5);
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
}

TEST_CASE(a_found_dial_binds_by_its_row)
{
    fresh_hub();
    const dial_view_t v = two_found();
    wireless_pane_set_dial(&v);
    const ui_setlist_custom_t *b = &wireless_pane_bind_row;
    CHECK_EQ(b->choices(), 2);
    CHECK_STR_EQ(b->choice(0), "DIAL-0001  -48dBm");
    CHECK_STR_EQ(b->choice(1), "A0:B1:C2:00:00:02 R  -71dBm");
    CHECK_STR_EQ(b->choice(2), "");
    CHECK_EQ(b->chosen(), 1);
    dial_cmd_t c;
    while (wireless_pane_take_dial(&c)) { }
    b->pick(0);
    CHECK(wireless_pane_take_dial(&c));
    CHECK_EQ(c.kind, DIAL_CMD_BIND);
    CHECK_EQ(c.arg, 0u);
    b->pick(2);
    b->pick(-1);
    CHECK(!wireless_pane_take_dial(&c));
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
    /* No scan yet: no rows, and NONE bound. */
    wireless_pane_set_dial(NULL);
    CHECK_EQ(b->choices(), 0);
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

int main(void)
{
    RUN(the_dial_entry_shows_its_rows_under_on);
    RUN(a_scan_while_armed_is_refused_with_a_band_text);
    RUN(a_found_dial_binds_by_its_row);
    RUN(every_state_word_fits_25_cells_in_its_colour);
    RUN(reset_keeps_the_bind);
    return test_summary("wireless_pane");
}
