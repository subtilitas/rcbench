/**
 * @file test_settings_rows.c
 * @brief SETUP's layout at 1024 x 600: the rig's figures held to the
 *        screen's, every target at least UI_TAP_MIN, six whole rows in the
 *        pane, every row's name, help line and value inside its row in
 *        both languages, the shortest list with no scroll and no bar, the
 *        longest scrolled to its last row, and the warning's answers clear
 *        of the alert band.
 *
 * SPDX-License-Identifier: MIT
 */
#include "settings_rig.h"

#include "overview_screen.h"
#include "settings_sensors.h"

/** @brief The faces SETUP draws a row in (settings_screen_int.h). */
#define F_NAME  (&gfx_font_20x36)
#define F_VALUE (&gfx_font_12x24)
#define F_LABEL (&gfx_font_10x20)

/** @brief A row's name and help line: from 16 px into the row to 10 px
 *         short of the "-" key, where the screen clips them. */
#define TEXT_W (MINUS_X - 10 - (LIST_X + 16))
/** @brief A value and its unit: from 8 px right of the "-" key to 8 px
 *         short of the "+" key. */
#define VALUE_SPAN (PLUS_X - 8 - (MINUS_X + BTN_W + 8))

/*
 * The rig mirrors the screen's geometry; the screen's own functions say
 * where it is.  Row 0 of the open list at its top, the next 70 px under
 * it, the keys in their columns over the row's height, the value between
 * them.
 */
TEST_CASE(the_rig_matches_the_screens_rows)
{
    fresh_screen();
    const gfx_rect_t pane = settings_screen_list_rect();
    CHECK_EQ(pane.x, LIST_X);
    CHECK_EQ(pane.y + UI_BAND_H, LIST_Y);
    CHECK_EQ(pane.h, LIST_H);
    const gfx_rect_t r0 = settings_screen_row_rect(0);
    const gfx_rect_t r1 = settings_screen_row_rect(1);
    CHECK_EQ(r0.x, LIST_X);
    CHECK_EQ(r0.y + UI_BAND_H, LIST_Y);
    CHECK_EQ(r0.h, ROW_H);
    CHECK_EQ(r1.y - r0.y, ROW_PITCH);
    const gfx_rect_t mi = settings_screen_row_part(1, SETTINGS_ROW_MINUS);
    const gfx_rect_t va = settings_screen_row_part(1, SETTINGS_ROW_VALUE);
    const gfx_rect_t pl = settings_screen_row_part(1, SETTINGS_ROW_PLUS);
    CHECK_EQ(mi.x, MINUS_X);
    CHECK_EQ(mi.w, BTN_W);
    CHECK_EQ(pl.x, PLUS_X);
    CHECK_EQ(pl.w, BTN_W);
    CHECK_EQ(va.x, MINUS_X + BTN_W);
    CHECK_EQ(va.x + va.w, PLUS_X);
    CHECK_EQ(mi.y, r1.y);
    CHECK_EQ(pl.h, ROW_H);
    /* The "+" key 12 px inside the row's right edge, the row 8 px left of
     * the 12 px scroll bar, which ends at the third strip column's edge. */
    CHECK_EQ(PLUS_X + BTN_W, r0.x + r0.w - 12);
    CHECK_EQ(r0.x + r0.w + 8 + 12, SAVE_X + HUB_COL_W);
    CHECK_EQ(va.w, 166);
}

/*
 * Nothing on SETUP that takes a tap is under UI_TAP_MIN (48 px, 7.2 mm):
 * a key is 56 x 48 drawn and takes its column over the row's 64 px; the
 * strip's three columns are 48 px tall; a hub tile 328 x 130; a menu tile
 * 243 x 255.  Two keys of one row are the value's 166 px apart.
 */
TEST_CASE(every_target_of_setup_and_the_menu_is_a_full_tap)
{
    CHECK(BTN_W >= UI_TAP_MIN);
    CHECK(ROW_H >= UI_TAP_MIN);
    CHECK_EQ(STRIP_H, UI_TAP_MIN);
    CHECK(PLUS_X - (MINUS_X + BTN_W) >= 8);
    for (int i = 0; i < SETTINGS_HUB_SLOTS; ++i) {
        gfx_rect_t r = gfx_rect_make(0, 0, 0, 0);
        CHECK(settings_hub_slot(i, &r));
        CHECK_EQ(r.w, 328);
        CHECK_EQ(r.h, 130);
        /* And clear of the strip under the grid. */
        CHECK(UI_BAND_H + r.y + r.h + 8 <= STRIP_Y);
    }
    for (int i = 0; i < 8; ++i) {
        const gfx_rect_t r = overview_screen_tile_rect(i);
        CHECK(r.w >= UI_TAP_MIN && r.h >= UI_TAP_MIN);
        CHECK(r.x >= 8 && r.x + r.w <= W - 8);
        CHECK(r.y + r.h <= UI_BODY_H - 8);
    }
}

/*
 * The pane holds six rows whole and not seven: 6 x 70 px less the last
 * gap is 414 of its 428 px.  The strip under it starts 8 px lower and
 * ends 4 px above the alert band.
 */
TEST_CASE(six_rows_show_whole_in_the_pane)
{
    CHECK(6 * ROW_PITCH - (ROW_PITCH - ROW_H) <= LIST_H);
    CHECK(7 * ROW_PITCH - (ROW_PITCH - ROW_H) > LIST_H);
    CHECK_EQ(LIST_Y + LIST_H + 8, STRIP_Y);
    CHECK_EQ(STRIP_Y + STRIP_H + 4, UI_ALERT_Y);
}

/* The width of @p id's value @p v with its unit, in @p f, as the row
 * draws it. */
static int value_width(setting_id_t id, float v, const gfx_font_t *f)
{
    const setting_def_t *d = settings_def(id);
    char buf[32];
    (void)ui_setting_value_of(id, v, buf, sizeof(buf));
    int w = gfx_text_width(f, buf, 1);
    if (d->unit != NULL && d->unit[0] != '\0'
        && !(d->off_below > 0.0f && v == 0.0f)) {
        w += 6 + gfx_text_width(F_LABEL, d->unit, 1);
    }
    return w;
}

/*
 * Every row of every SETUP category, in both languages: the name on one
 * line of the head face and the help line in the label face end short of
 * the "-" key, and every value the row can show (each option of a
 * choice, both ends of a number's range) fits between the keys, in the
 * body face or, for a word too wide for it, in the label face.
 */
TEST_CASE(every_row_fits_between_its_edge_and_its_keys_in_both_languages)
{
    fresh_hub();
    int body = 0, label = 0;
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (int cat = 0; cat < SET_CAT_SETUP_COUNT; ++cat) {
            setting_id_t ids[64];
            const int n = settings_in_category((setting_cat_t)cat, ids, 64);
            for (int i = 0; i < n; ++i) {
                const setting_id_t id = ids[i];
                const setting_def_t *d = settings_def(id);
                const char *name = ui_setting_label(id);
                if (gfx_text_width(F_NAME, name, 1) > TEXT_W) {
                    T_FAIL("lang %d: name \"%s\" past the keys", lang, name);
                }
                const char *help = settings_screen_help(id);
                if (gfx_text_width(F_LABEL, help, 1) > TEXT_W) {
                    T_FAIL("lang %d: help \"%s\" past the keys", lang, help);
                }
                const bool word = d->type == SET_TYPE_ENUM
                                  || d->type == SET_TYPE_BOOL;
                const int k_end = word ? (int)d->max : 1;
                for (int k = word ? (int)d->min : 0; k <= k_end; ++k) {
                    const float v = word ? (float)k : (k ? d->max : d->min);
                    if (value_width(id, v, F_VALUE) <= VALUE_SPAN) {
                        ++body;
                    } else if (value_width(id, v, F_LABEL) <= VALUE_SPAN) {
                        ++label;
                    } else {
                        char buf[32];
                        (void)ui_setting_value_of(id, v, buf, sizeof(buf));
                        T_FAIL("lang %d: %s's value \"%s\" past its keys",
                               lang, name, buf);
                    }
                }
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
    /* Every value takes the body face, as docs/Screens.md says: a value
     * that needs the label face changes the docs with this figure. */
    CHECK(body > 0);
    CHECK_EQ(label, 0);
}

/* Whether any pixel of the scroll bar's column, 1002 to 1013 px, over the
 * pane, is the accent: the bar's knob. */
static bool bar_drawn(void)
{
    ui_router_render(&s_c, 0);
    const gfx_color_t accent = ui_theme_color(UI_C_ACCENT);
    for (int y = LIST_Y; y < LIST_Y + LIST_H; ++y) {
        for (int x = 1002; x < 1014; ++x) {
            if (s_fb[y * W + x] == accent) {
                return true;
            }
        }
    }
    return false;
}

/*
 * The shortest list, SENSORS with no INA3221 fitted (its heading and the
 * four FITTED rows), fits the pane: no scroll range, no bar, and a drag
 * moves nothing.
 */
TEST_CASE(the_shortest_list_fits_and_draws_no_scroll_bar)
{
    fresh_hub();
    for (int a = 0; a < 4; ++a) {
        settings_set(SET_INA_FIT(a), 0.0f);
    }
    open_category(SET_CAT_SENSORS);
    CHECK_EQ(settings_screen_rows(SET_CAT_SENSORS), 5);
    int max = -1;
    CHECK_EQ(settings_screen_scroll(&max), 0);
    CHECK_EQ(max, 0);
    CHECK(!bar_drawn());
    gesture_screen(SET_CAT_SENSORS);
    swipe(XL, LIST_Y + LIST_H - 8, XL, LIST_Y + 8, 9);
    CHECK_EQ(settings_screen_scroll(NULL), 0);
    CHECK_EQ(changed(), 0);
}

/*
 * The longest list, SENSORS with all four INA3221 fitted, scrolls to its
 * last row: that row ends at the pane's foot, its "+" key whole and a tap
 * on it steps the row's setting.  The bar is drawn.
 */
TEST_CASE(the_longest_list_scrolls_to_its_last_row)
{
    fresh_hub();
    for (int a = 0; a < 4; ++a) {
        settings_set(SET_INA_FIT(a), 1.0f);
    }
    int longest = 0;
    for (int cat = 0; cat < SET_CAT_SETUP_COUNT; ++cat) {
        const int n = settings_screen_rows((setting_cat_t)cat);
        longest = (n > longest) ? n : longest;
    }
    CHECK_EQ(settings_screen_rows(SET_CAT_SENSORS), longest);
    open_category(SET_CAT_SENSORS);
    int max = 0;
    (void)settings_screen_scroll(&max);
    CHECK_EQ(max, longest * ROW_PITCH - (ROW_PITCH - ROW_H) - LIST_H);
    for (int k = 0; k < 40 && settings_screen_scroll(NULL) < max; ++k) {
        swipe(XL, LIST_Y + LIST_H - 8, XL, LIST_Y + 8, 9);
    }
    CHECK_EQ(settings_screen_scroll(NULL), max);
    CHECK(bar_drawn());
    const gfx_rect_t last = settings_screen_row_rect(longest - 1);
    CHECK_EQ(UI_BAND_H + last.y + last.h, LIST_Y + LIST_H);
    snap();
    const gfx_rect_t pl = settings_screen_row_part(longest - 1,
                                                   SETTINGS_ROW_PLUS);
    tap(pl.x + pl.w / 2, UI_BAND_H + pl.y + pl.h / 2);
    CHECK_EQ(changed(), 1);
}

/*
 * The warning that holds the way out keeps its answers above the alert
 * band too: with a value unsaved and the warning open, a tap on every 4th
 * px of the band's rectangle answers nothing, and STAY, DISCARD and SAVE
 * end 4 px above it.
 */
TEST_CASE(nothing_of_the_warning_is_under_the_alert_band)
{
    fresh_hub();
    settings_set_store(&s_mem_store);
    settings_set(SET_PACK_CELLS, 4.0f);
    tap(home_x(), home_y());
    CHECK(settings_screen_warning());
    snap();
    for (int y = UI_ALERT_Y; y < H; y += 4) {
        for (int x = 0; x < W; x += 4) {
            tap(x, y);
        }
    }
    CHECK(settings_screen_warning());
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK_EQ(changed(), 0);
    CHECK(!settings_save_asked());
    CHECK(settings_dirty());
    CHECK_EQ(SAVE_Y + SAVE_H + 4, UI_ALERT_Y);
    settings_set_store(NULL);
}

int main(void)
{
    RUN(the_rig_matches_the_screens_rows);
    RUN(every_target_of_setup_and_the_menu_is_a_full_tap);
    RUN(six_rows_show_whole_in_the_pane);
    RUN(every_row_fits_between_its_edge_and_its_keys_in_both_languages);
    RUN(the_shortest_list_fits_and_draws_no_scroll_bar);
    RUN(the_longest_list_scrolls_to_its_last_row);
    RUN(nothing_of_the_warning_is_under_the_alert_band);
    return test_summary("settings_rows");
}
