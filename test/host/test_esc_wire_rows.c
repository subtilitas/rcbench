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
    CHECK_EQ(ewp_s.tab_count, 6);
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

/**
 * @brief A press and a release at the middle of @p r.
 * @param r The control.
 */
static void tap(gfx_rect_t r)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)(r.x + r.w / 2),
                                   .y = (int16_t)(r.y + r.h / 2), .strength = 40 } };

    esc_wire_pages.event(&e);
    e.type = TOUCH_EVENT_UP;
    esc_wire_pages.event(&e);
}

/**
 * @brief Each tab selected by a press, and its last row opened by one.
 * @param fw     The firmware.
 * @param layout The layout.
 */
static void every_tab_reached(uint8_t fw, uint8_t layout)
{
    read_as(fw, layout);
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_SESSION);
    CHECK_EQ(ewp_s.tabs.count, ewp_s.tab_count);
    for (int tab = ewp_s.tab_count - 1; tab >= 0; --tab) {
        tap(esc_wire_panel_tab_rect(tab));
        CHECK_EQ(ewp_s.tabs.selected, tab);
        ewp_row_t r;
        int last = -1;

        while (ewp_row(tab, last + 1, &r)) {
            ++last;
        }
        if (tab == 0) {
            CHECK_EQ(last, -1);
            continue;
        }
        CHECK(last >= 0);
        CHECK(ewp_row(tab, last, &r));
        CHECK(esc_wire_panel_value_rect(last).w > 0);
        CHECK(esc_wire_panel_value_rect(last).y + esc_wire_panel_value_rect(last).h
              <= esc_wire_panel_action_rect(0).y);
        tap(esc_wire_panel_value_rect(last));
        if (ewp_row_live(&r)) {
            CHECK(esc_wire_panel_keypad()->open || ewp_s.ch_what != EWP_CH_NONE);
            esc_wire_pages.back();
        }
        CHECK_EQ(esc_wire_panel_page(), EWP_PAGE_NONE);
    }
}

TEST_CASE(every_tab_of_every_firmware_selects_and_opens_its_last_row)
{
    every_tab_reached(EW_FW_S, 33u);
    every_tab_reached(EW_FW_S, 32u);
    every_tab_reached(EW_FW_BJ, 208u);
    every_tab_reached(EW_FW_BJ, 33u);
    every_tab_reached(EW_FW_BJ, 200u);
    every_tab_reached(EW_FW_BJ, 204u);
    every_tab_reached(EW_FW_AM, 3u);
    every_tab_reached(EW_FW_AM, 4u);
    every_tab_reached(EW_FW_E32, 17u);
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

/**
 * @brief The row of a field's byte, in any tab.
 * @param off The byte.
 * @param out Takes the row.
 * @return true when a tab has it.
 */
static bool row_of(uint8_t off, ewp_row_t *out)
{
    for (int tab = 1; tab < ewp_s.tab_count; ++tab) {
        for (int i = 0; ewp_row(tab, i, out); ++i) {
            if (out->f != NULL && out->f->off == off) {
                return true;
            }
        }
    }
    return false;
}

/* A byte that holds another field in another layout has that field's help
 * line: Bluejay's 0x04 and 0x09 change at layout 201. */
TEST_CASE(a_help_line_is_the_one_of_the_layouts_field)
{
    ewp_row_t r;

    read_as(EW_FW_BJ, 208u);
    CHECK(row_of(0x04u, &r));
    CHECK_STR_EQ(r.f->name, "MIN STARTUP POWER");
    CHECK_STR_EQ(ewp_help_text(&r), TR(EW_HB_04));
    CHECK(row_of(0x09u, &r));
    CHECK_STR_EQ(r.f->name, "POWER SLOPE");
    CHECK_STR_EQ(ewp_help_text(&r), TR(EW_HB_09));
    read_as(EW_FW_BJ, 200u);
    CHECK(row_of(0x04u, &r));
    CHECK_STR_EQ(r.f->name, "STARTUP BOOST");
    CHECK_STR_EQ(ewp_help_text(&r), TR(EW_HB_04_BOOST));
    CHECK(row_of(0x09u, &r));
    CHECK_STR_EQ(r.f->name, "STARTUP POWER");
    CHECK_STR_EQ(ewp_help_text(&r), TR(EW_HS_09));
    read_as(EW_FW_BJ, 33u);
    CHECK(!row_of(0x04u, &r));
    CHECK(row_of(0x09u, &r));
    CHECK_STR_EQ(ewp_help_text(&r), TR(EW_HS_09));
}

/* The screen writes ° and °C where the maps, whose spelling the ASCII settings
 * files keep, write deg and C: on the row, in the keypad and in the list. */
TEST_CASE(degrees_are_shown_with_their_sign)
{
    char text[32];
    ewp_row_t r;

    read_as(EW_FW_AM, 4u);
    CHECK(row_of(0x17u, &r));
    ewp_s.draft[0x17] = 26u;
    ewp_row_text(&r, text, sizeof(text));
    CHECK_STR_EQ(text, "15°");
    (void)ew_field_text(r.f, 26u, text, sizeof(text));
    CHECK_STR_EQ(text, "15deg");
    ewp_edit_open(&r);
    CHECK(esc_wire_panel_keypad()->open);
    CHECK_STR_EQ(esc_wire_panel_keypad()->unit, "°");
    esc_wire_pages.back();
    CHECK(row_of(0x2Bu, &r));
    ewp_s.draft[0x2B] = 90u;
    ewp_row_text(&r, text, sizeof(text));
    CHECK_STR_EQ(text, "90°C");
    /* BLHeli_S names its temperatures in the list. */
    read_as(EW_FW_S, 33u);
    CHECK(row_of(0x23u, &r));
    ewp_s.draft[0x23] = 7u;
    ewp_row_text(&r, text, sizeof(text));
    CHECK_STR_EQ(text, "140°C");
    /* A text that only ends like a unit keeps its letters. */
    snprintf(text, sizeof(text), "DISC");
    ewp_units_shown(text, sizeof(text));
    CHECK_STR_EQ(text, "DISC");
    CHECK_STR_EQ(ewp_unit_shown("%"), "%");
}

/* ESCape32's timing: the row, the keypad's range and the help line name the
 * same degrees, 0.9375° to 29.0625°. */
TEST_CASE(escape32_timing_reads_in_degrees_everywhere)
{
    char text[32];
    ewp_row_t r;
    bool got = false;

    read_as(EW_FW_E32, 17u);
    for (int tab = 1; tab < ewp_s.tab_count && !got; ++tab) {
        for (int i = 0; !got && ewp_row(tab, i, &r); ++i) {
            got = r.k != NULL && strcmp(r.k->key, "timing") == 0;
        }
    }
    CHECK(got);
    ew_e32_put(ewp_s.draft, r.slot, 1);
    ewp_row_text(&r, text, sizeof(text));
    CHECK_STR_EQ(text, "0.9375°");
    ew_e32_put(ewp_s.draft, r.slot, 31);
    ewp_row_text(&r, text, sizeof(text));
    CHECK_STR_EQ(text, "29.0625°");
    ewp_edit_open(&r);
    const ui_keypad_t *k = esc_wire_panel_keypad();

    CHECK(k->open);
    CHECK_STR_EQ(k->unit, "°");
    CHECK(k->min > 0.937f && k->min < 0.938f);
    CHECK(k->max > 29.062f && k->max < 29.063f);
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language(lang == 0 ? UI_LANG_EN : UI_LANG_DE);
        CHECK(strstr(ewp_help_text(&r), "0.9375° ") != NULL);
        CHECK(strstr(ewp_help_text(&r), " 29.0625° ") != NULL);
        CHECK(strstr(ewp_help_text(&r), "1 to 31") == NULL);
        CHECK(strstr(ewp_help_text(&r), "1 bis 31") == NULL);
    }
    ui_text_set_language(UI_LANG_EN);
    esc_wire_pages.back();
}

int main(void)
{
    RUN(every_field_of_a_written_layout_is_in_one_tab);
    RUN(every_escape32_key_is_in_one_tab);
    RUN(every_tab_of_every_firmware_selects_and_opens_its_last_row);
    RUN(a_layout_with_no_field_names_shows_bytes);
    RUN(the_warning_page_has_room_for_every_second_look_field);
    RUN(the_controls_stay_above_the_alert_band);
    RUN(a_help_line_is_the_one_of_the_layouts_field);
    RUN(degrees_are_shown_with_their_sign);
    RUN(escape32_timing_reads_in_degrees_everywhere);
    return test_summary("esc_wire_rows");
}
