/*
 * The ESC signal-wire session's rows against the maps: every field of every layout
 * that is written in one tab and only one, every tab within its 12 rows, a help line
 * for every row in both languages, and the fit of the controls above the alert band.
 *
 * SPDX-License-Identifier: MIT
 */
#include "esc_wire_int.h"
#include "ew_id.h"
#include "greatest.h"
#include "link_pages.h"
#include "ui_text.h"

/**
 * @brief The session as a read ESC of a firmware and layout.
 * @param fw     The firmware.
 * @param layout The layout or revision.
 */
static void read_as(uint8_t fw, uint8_t layout)
{
    static esc_prog_feed_t f;

    esc_wire_pages.init((gfx_rect_t){ 0, PROG_FULL_Y, UI_PANEL_W,
                                      (int16_t)(UI_BODY_H - PROG_FULL_Y) });
    memset(&f, 0, sizeof(f));
    f.rd.phase = ESC_LINK_READY;
    f.rd.known = true;
    f.rd.held = true;
    f.rd.ours = true;
    f.rd.state = LINK_ESC_READY;
    f.rd.fw = fw;
    f.rd.layout = layout;
    f.rd.flags = LINK_EW_F_HAVE_READ | LINK_EW_F_WRITABLE;
    f.image.gen = 1u;
    esc_wire_panel_feed(&f);
    esc_wire_pages.open();
}

/**
 * @brief Every editable field of a layout in one tab, each tab within its rows.
 * @param fw     The firmware.
 * @param layout The layout.
 */
static void all_fields_placed(uint8_t fw, uint8_t layout)
{
    int seen[256];

    memset(seen, 0, sizeof(seen));
    read_as(fw, layout);
    CHECK(ewp_s.tab_count >= 2);
    CHECK(ewp_s.tab_count <= EWP_TABS);
    for (int tab = 1; tab < ewp_s.tab_count; ++tab) {
        ewp_row_t r;
        int i = 0;

        while (ewp_row(tab, i, &r)) {
            ++seen[r.f->off];
            CHECK(ewp_help_text(&r)[0] != '\0');
            ++i;
        }
        CHECK(i <= EWP_ROWS * EWP_COLS);
    }
    const ew_field_t *f;

    for (size_t i = 0u; (f = ew_map_nth(fw, layout, i)) != NULL; ++i) {
        /* A tag at the start of the page sits on IDENTITY. */
        if (f->access == (uint8_t)EW_ACC_EDIT) {
            CHECK_EQ(seen[f->off], 1);
        } else {
            CHECK(seen[f->off] <= 1);
        }
    }
}

TEST_CASE(every_field_of_a_written_layout_is_in_one_tab)
{
    all_fields_placed(EW_FW_S, 33u);
    all_fields_placed(EW_FW_S, 32u);
    all_fields_placed(EW_FW_BJ, 208u);
    all_fields_placed(EW_FW_BJ, 33u);
    all_fields_placed(EW_FW_BJ, 200u);
    all_fields_placed(EW_FW_BJ, 204u);
    all_fields_placed(EW_FW_AM, 3u);
    all_fields_placed(EW_FW_AM, 4u);
}

TEST_CASE(every_escape32_key_is_in_one_tab)
{
    int seen[EW_E32_KEYS];

    memset(seen, 0, sizeof(seen));
    read_as(EW_FW_E32, 17u);
    CHECK_EQ(ewp_s.tab_count, 7);
    for (int tab = 1; tab < ewp_s.tab_count; ++tab) {
        ewp_row_t r;
        int i = 0;

        while (ewp_row(tab, i, &r)) {
            ++seen[r.slot];
            CHECK(ewp_help_text(&r)[0] != '\0');
            ++i;
        }
        CHECK(i <= EWP_ROWS * EWP_COLS);
    }
    for (size_t i = 0u; i < EW_E32_KEYS; ++i) {
        CHECK_EQ(seen[i], 1);
    }
}

TEST_CASE(a_layout_with_no_field_names_shows_bytes)
{
    read_as(EW_FW_S, 34u);
    CHECK(!ewp_has_rows());
    CHECK_EQ(ewp_s.tab_count, 2);
    CHECK_EQ(ewp_tab_text(1), TX_EW_TAB_HEX);
}

TEST_CASE(the_warning_page_has_room_for_every_second_look_field)
{
    int n = 0;
    const ew_field_t *f;

    for (size_t i = 0u; (f = ew_map_nth(EW_FW_AM, 4u, i)) != NULL; ++i) {
        n += (f->flags & EW_FF_WARN) != 0u ? 1 : 0;
    }
    CHECK(n + 2 <= EWP_PAGE_LINES);
    n = 0;
    for (size_t i = 0u; i < EW_E32_KEYS; ++i) {
        n += (ew_map_e32[i].flags & EW_FF_WARN) != 0u ? 1 : 0;
    }
    CHECK(n + 2 <= EWP_PAGE_LINES);
}

TEST_CASE(the_controls_stay_above_the_alert_band)
{
    read_as(EW_FW_AM, 4u);
    for (int i = 0; i < EWP_ROWS * EWP_COLS; ++i) {
        const gfx_rect_t r = ewp_value_rect(i);

        CHECK(r.y + r.h <= UI_KEYS_FOOT - EWP_ACT_H - 24);
        CHECK(r.h >= UI_TAP_MIN);
        CHECK(r.x + r.w <= UI_PANEL_W);
    }
    for (int i = 0; i < 4; ++i) {
        const gfx_rect_t r = ewp_action_rect(i);

        CHECK_EQ(r.y + r.h, UI_KEYS_FOOT);
        CHECK(r.x + r.w <= UI_PANEL_W);
    }
    for (int i = 0; i < EWP_CHOICE_MAX; ++i) {
        const gfx_rect_t r = ewp_choice_rect(i);

        CHECK(r.y + r.h <= UI_KEYS_FOOT - EWP_ACT_H);
    }
}

int main(void)
{
    RUN(every_field_of_a_written_layout_is_in_one_tab);
    RUN(every_escape32_key_is_in_one_tab);
    RUN(a_layout_with_no_field_names_shows_bytes);
    RUN(the_warning_page_has_room_for_every_second_look_field);
    RUN(the_controls_stay_above_the_alert_band);
    return test_summary("esc_wire_rows");
}
