/**
 * @file
 * @brief The shared overlays and widgets on the 1024 x 600 panel: the faces
 *        of the keypad's and the keyboard's keys, the tabs' and the choice
 *        list's faces, the TARGET row's cells and the plot ring.
 *
 * Each face rule is held at its limit: the longest German word of a row,
 * the narrowest key it still fits, and the size one step under it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "ui_keypad.h"
#include "ui_layout.h"
#include "ui_plot.h"
#include "ui_tabs.h"
#include "ui_target_row.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W UI_PANEL_W
#define H UI_PANEL_H

static gfx_color_t *fb;
static gfx_canvas_t cv;

/** @brief A cleared panel-sized canvas in the dark theme. */
static void canvas(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb);
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
}

/**
 * @brief The pixels outside @p r that are not black.
 * @param r The box.
 * @return Their count.
 */
static int ink_outside(gfx_rect_t r)
{
    int n = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (!gfx_rect_contains(r, x, y) && fb[y * W + x] != 0) {
                ++n;
            }
        }
    }
    return n;
}

/**
 * @brief The width of a keypad whose keys are @p kw px wide.
 * @param kw A key's width.
 * @return The area's width: 8 px each side and 8 px between 4 keys.
 */
static int16_t keypad_w(int kw)
{
    return (int16_t)(4 * kw + 3 * 8 + 2 * 8);
}

/* ------------------------------------------------------------- the keypad */

TEST_CASE(a_keypad_over_a_workbench_card_has_large_keys_in_12x24)
{
    ui_keypad_t k;
    memset(&k, 0, sizeof(k));
    ui_keypad_open_signed(&k, gfx_rect_make(8, 8, 620, 476), "TRIM", "us",
                          0.0f, -200.0f, 200.0f, 0);
    for (int key = 0; key < UI_KEY_COUNT; ++key) {
        const gfx_rect_t r = ui_keypad_key_rect(&k, (ui_key_t)key);
        CHECK(r.w >= UI_TAP_MIN && r.h >= UI_TAP_MIN);
    }
    const gfx_rect_t seven = ui_keypad_key_rect(&k, UI_KEY_7);
    CHECK_EQ(seven.w, 145);
    CHECK_EQ(seven.h, 84);
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        CHECK(ui_keypad_word_font(&k) == &gfx_font_12x24);
    }
    ui_text_set_language(UI_LANG_EN);
    CHECK(ui_keypad_word_font(NULL) == &gfx_font_8x16);
}

/* ABBRECHEN, the longest word on a keypad, sets the face of all of them:
 * 12 x 24 needs a 120 px key, 10 x 20 a 102 px one, 8 x 16 an 84 px one. */
TEST_CASE(the_longest_german_word_sets_the_keypads_word_face)
{
    static const struct {
        int kw;
        const gfx_font_t *de;
    } k_at[] = {
        { 120, &gfx_font_12x24 }, { 119, &gfx_font_10x20 },
        { 102, &gfx_font_10x20 }, { 101, &gfx_font_8x16 },
        { 84, &gfx_font_8x16 },
    };
    for (size_t i = 0; i < sizeof(k_at) / sizeof(k_at[0]); ++i) {
        ui_keypad_t k;
        memset(&k, 0, sizeof(k));
        ui_keypad_open(&k, gfx_rect_make(0, 0, keypad_w(k_at[i].kw), 476),
                       "V", "V", 1.0f, 0.0f, 9.0f, 1);
        CHECK_EQ(ui_keypad_key_rect(&k, UI_KEY_CANCEL).w, k_at[i].kw);
        ui_text_set_language(UI_LANG_DE);
        CHECK(ui_keypad_word_font(&k) == k_at[i].de);
        ui_text_set_language(UI_LANG_EN);
        /* CANCEL is 6 cells: 12 x 24 down to an 84 px key. */
        CHECK(ui_keypad_word_font(&k) == &gfx_font_12x24);
    }
}

/* The entry, the faint value and the refusal draw inside the area at both
 * numeral faces: "-199.5 us" takes 32 x 44 down to a box of 264 px (an
 * area of 280); at 260 it takes 24 x 30, and at 120 even that is cut at
 * the box's right, the unit first. */
TEST_CASE(the_keypad_draws_inside_its_area_at_either_numeral_face)
{
    static const int16_t k_w[] = { 620, 300, 260, 120 };
    for (size_t i = 0; i < sizeof(k_w) / sizeof(k_w[0]); ++i) {
        canvas();
        const gfx_rect_t a = gfx_rect_make(100, 40, k_w[i], 420);
        ui_keypad_t k;
        memset(&k, 0, sizeof(k));
        ui_keypad_open_signed(&k, a, "TRIM", "us", -199.5f, -9999.0f,
                              9999.0f, 1);
        ui_keypad_render(&k, &cv);
        CHECK_EQ(ink_outside(a), 0);
        ui_keypad_refuse_why(&k, "A reason of the caller's own");
        ui_keypad_render(&k, &cv);
        CHECK_EQ(ink_outside(a), 0);
    }
}

/* ----------------------------------------------------------- the keyboard */

TEST_CASE(the_wifi_keyboard_draws_every_word_in_12x24)
{
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (int secret = 0; secret < 2; ++secret) {
            ui_textkey_t k;
            memset(&k, 0, sizeof(k));
            ui_textkey_open_wifi(&k, gfx_rect_make(8, 8, 1008, 484), "", "",
                                 secret != 0);
            CHECK(ui_textkey_word_font(&k) == &gfx_font_12x24);
        }
    }
    ui_text_set_language(UI_LANG_EN);
    CHECK(ui_textkey_word_font(NULL) == &gfx_font_8x16);
}

/* Over a 488 px area a character key is 41 px: DEL needs 10 x 20, and the
 * German CANCEL, ABBRECHEN in two keys of 88 px, needs 8 x 16. */
TEST_CASE(a_narrow_keyboard_takes_the_face_its_longest_word_fits)
{
    ui_textkey_t k;
    memset(&k, 0, sizeof(k));
    ui_textkey_open(&k, gfx_rect_make(8, 8, 488, 416), "NAME", "", 16);
    CHECK_EQ(ui_textkey_key_rect(&k, 0).w, 41);
    CHECK(ui_textkey_word_font(&k) == &gfx_font_10x20);
    ui_text_set_language(UI_LANG_DE);
    CHECK(ui_textkey_word_font(&k) == &gfx_font_8x16);
    ui_text_set_language(UI_LANG_EN);
}

/* The face does not change with a press: the password's SHOW and HIDE,
 * and the layer keys' ABC, abc and #+=, are measured in every state. */
TEST_CASE(the_keyboards_word_face_holds_across_its_states)
{
    ui_text_set_language(UI_LANG_DE);
    ui_textkey_t k;
    memset(&k, 0, sizeof(k));
    ui_textkey_open_wifi(&k, gfx_rect_make(8, 8, 620, 416), "", "", true);
    const gfx_font_t *f = ui_textkey_word_font(&k);
    k.show = true;
    CHECK(ui_textkey_word_font(&k) == f);
    k.layer = UI_TK_UPPER;
    CHECK(ui_textkey_word_font(&k) == f);
    k.layer = UI_TK_MARKS;
    CHECK(ui_textkey_word_font(&k) == f);
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(the_keyboard_draws_inside_its_area_at_every_width)
{
    static const int16_t k_w[] = { 1008, 620, 488 };
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (size_t i = 0; i < sizeof(k_w) / sizeof(k_w[0]); ++i) {
            canvas();
            const gfx_rect_t a = gfx_rect_make(8, 8, k_w[i], 484);
            ui_textkey_t k;
            memset(&k, 0, sizeof(k));
            ui_textkey_open(&k, a, "NAME", "ABCDEFGHIJKLMNOPQRSTUVW",
                            UI_TEXTKEY_MAX);
            ui_textkey_render(&k, &cv);
            CHECK_EQ(ink_outside(a), 0);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/* --------------------------------------------------------------- the tabs */

TEST_CASE(tabs_take_10x20_when_tall_and_every_label_fits)
{
    static const char *const k_four[] = { "LIVE", "TEST", "PROGRAMMIEREN",
                                          "SETTINGS" };
    ui_tabs_t t;
    /* 150 x 40: PROGRAMMIEREN is 130 px and 6 px clear each side. */
    ui_tabs_init(&t, k_four, 4, gfx_rect_make(11, 8, 4 * 150 + 3 * 4, 40));
    CHECK_EQ(t.rect[2].w, 150);
    CHECK(ui_tabs_font(&t) == &gfx_font_10x20);
    /* 141 px: one label 1 px short of its room, so the row keeps 8 x 16. */
    ui_tabs_init(&t, k_four, 4, gfx_rect_make(11, 8, 4 * 141 + 3 * 4, 40));
    CHECK_EQ(t.rect[2].w, 141);
    CHECK(ui_tabs_font(&t) == UI_FONT_LABEL);
    ui_tabs_init(&t, k_four, 4, gfx_rect_make(11, 8, 4 * 142 + 3 * 4, 40));
    CHECK(ui_tabs_font(&t) == &gfx_font_10x20);
    /* Exactly UI_TABS_LABEL_H is tall enough; a row under it keeps 8 x 16
     * whatever fits. */
    ui_tabs_init(&t, k_four, 4,
                 gfx_rect_make(11, 8, 4 * 150 + 3 * 4, UI_TABS_LABEL_H));
    CHECK(ui_tabs_font(&t) == &gfx_font_10x20);
    ui_tabs_init(&t, k_four, 4,
                 gfx_rect_make(11, 8, 4 * 150 + 3 * 4, UI_TABS_LABEL_H - 1));
    CHECK(ui_tabs_font(&t) == UI_FONT_LABEL);
    CHECK(ui_tabs_font(NULL) == UI_FONT_LABEL);
    ui_tabs_init(&t, k_four, 0, gfx_rect_make(0, 0, 100, 40));
    CHECK(ui_tabs_font(&t) == UI_FONT_LABEL);
}

/* ------------------------------------------------------------ choice list */

TEST_CASE(a_choice_takes_20x36_from_56_px)
{
    CHECK(ui_choice_font(UI_CHOICE_ROW_H - 1) == UI_FONT_HEAD);
    CHECK(ui_choice_font(UI_CHOICE_ROW_H) == UI_FONT_TAG);
    CHECK(ui_choice_font(64) == UI_FONT_TAG);
    CHECK(UI_CHOICE_ROW_H >= UI_TAP_MIN);
    /* Both sizes draw inside their box: the face, locked and not, and a
     * chosen row. */
    static const gfx_rect_t k_box[] = { { 20, 20, 292, 64 },
                                        { 20, 20, 266, 54 } };
    for (size_t i = 0; i < sizeof(k_box) / sizeof(k_box[0]); ++i) {
        canvas();
        ui_choice_face(&cv, k_box[i], "DSHOT600", true, true);
        CHECK_EQ(ink_outside(k_box[i]), 0);
        canvas();
        ui_choice_face_locked(&cv, k_box[i], "DSHOT600");
        CHECK_EQ(ink_outside(k_box[i]), 0);
        canvas();
        ui_choice_row(&cv, k_box[i], "SERVO PWM", true, false);
        CHECK_EQ(ink_outside(k_box[i]), 0);
    }
}

/* DSHOT600 BIDIR, the longest protocol name in either language, 14 cells:
 * in 20 x 36 it needs 280 px before the chevron, so a 292 px face (238 px
 * of text room) draws it in 16 x 28, and a row takes 20 x 36 from 304 px. */
TEST_CASE(a_choice_text_that_does_not_fit_20x36_takes_16x28)
{
    static const char k_long[] = "DSHOT600 BIDIR";
    CHECK_EQ(gfx_text_cells(k_long), 14);
    CHECK(ui_choice_text_font(64, 292 - 12 - 28 - 11 - 4, k_long)
          == UI_FONT_HEAD);
    CHECK(ui_choice_text_font(64, 292 - 12 - 28 - 11 - 4, "SERVO PWM")
          == UI_FONT_TAG);
    CHECK(ui_choice_text_font(56, 304 - 24, k_long) == UI_FONT_TAG);
    CHECK(ui_choice_text_font(56, 303 - 24, k_long) == UI_FONT_HEAD);
    CHECK(ui_choice_text_font(55, 600, "OFF") == UI_FONT_HEAD);
    CHECK(ui_choice_text_font(64, 600, NULL) == UI_FONT_TAG);
    /* Drawn, the long name stays left of the chevron's first column. */
    canvas();
    const gfx_rect_t face = { 20, 20, 292, 64 };
    ui_choice_face(&cv, face, k_long, false, false);
    CHECK_EQ(ink_outside(face), 0);
    const int chevron = face.x + face.w - 28 - 11 + 1;
    int text_right = 0;
    for (int y = face.y + 14; y < face.y + face.h - 14; ++y) {
        for (int x = face.x + 12; x < chevron - 1; ++x) {
            if (fb[y * W + x] == UI_TEXT) {
                text_right = (x > text_right) ? x : text_right;
            }
        }
    }
    CHECK(text_right > 0);
    CHECK(text_right < chevron - 4);
}

/* A face taller than the box fits nothing, whatever the width; a NULL face
 * fits nothing; the box's own height is enough. */
TEST_CASE(a_face_taller_than_its_box_does_not_fit)
{
    CHECK(!ui_text_fits(&gfx_font_12x24, "A", 200, 23, 0));
    CHECK(ui_text_fits(&gfx_font_12x24, "A", 200, 24, 0));
    CHECK(!ui_text_fits(NULL, "A", 200, 200, 0));
    CHECK(ui_text_fits(&gfx_font_10x20, NULL, 0, 20, 0));
    /* The padding counts on both sides: 3 cells of 10 px and 6 px each. */
    CHECK(ui_text_fits(&gfx_font_10x20, "DEL", 42, 20, 6));
    CHECK(!ui_text_fits(&gfx_font_10x20, "DEL", 41, 20, 6));
}

/* The overlays' areas on every host at this head, in body coordinates. */
static const struct {
    gfx_rect_t area;
    bool       keyboard;
    int        kw, kh;      /* a character key or a digit key */
} k_hosts[] = {
    { { 8, 8, 620, 524 },     false, 145, 88 },  /* SERVO, KST          */
    { { 8, 8, 620, 524 },     true,  55, 72 },   /* SERVO, KST names    */
    { { 8, 38, 546, 494 },    false, 126, 80 },  /* SUPPLY              */
    { { 8, 3, 546, 529 },     false, 126, 89 },  /* MOTOR & ESC SETTINGS */
    { { 352, 66, 664, 466 },  true,  59, 61 },   /* ESC STICK search    */
    { { 8, 64, 1008, 428 },   false, 242, 72 },  /* BALANCE             */
    { { 10, 10, 1004, 482 },  false, 241, 85 },  /* SETUP               */
    { { 10, 10, 1004, 482 },  true,  93, 70 },   /* SETUP folders       */
    { { 8, 8, 1008, 484 },    true,  93, 71 },   /* WI-FI               */
};

/* Every key of every host ends UI_KEYS_FOOT at the latest, 4 px above the
 * alert band, and is UI_TAP_MIN or more tall and wide; the key sizes are
 * the ones Screens.md names. */
TEST_CASE(every_hosts_keys_end_above_the_alert_band)
{
    CHECK_EQ(UI_KEYS_FOOT, 492);
    CHECK_EQ(UI_KEYS_FOOT + 4, UI_ALERT_Y - UI_BAND_H);
    for (size_t h = 0; h < sizeof(k_hosts) / sizeof(k_hosts[0]); ++h) {
        const gfx_rect_t a = k_hosts[h].area;
        int lowest = 0;
        gfx_rect_t first;
        if (k_hosts[h].keyboard) {
            ui_textkey_t k;
            memset(&k, 0, sizeof(k));
            ui_textkey_open_wifi(&k, a, "", "", true);
            for (int key = 0; key <= UI_TK_SHOW; ++key) {
                const gfx_rect_t r = ui_textkey_key_rect(&k, key);
                CHECK(r.w >= UI_TAP_MIN);
                CHECK(r.h >= UI_TAP_MIN);
                lowest = (r.y + r.h > lowest) ? r.y + r.h : lowest;
            }
            first = ui_textkey_key_rect(&k, 0);
        } else {
            ui_keypad_t k;
            memset(&k, 0, sizeof(k));
            ui_keypad_open_signed(&k, a, "", "", 0.0f, -1.0f, 1.0f, 0);
            for (int key = 0; key < UI_KEY_COUNT; ++key) {
                const gfx_rect_t r = ui_keypad_key_rect(&k, (ui_key_t)key);
                CHECK(r.w >= UI_TAP_MIN && r.h >= UI_TAP_MIN);
                lowest = (r.y + r.h > lowest) ? r.y + r.h : lowest;
            }
            first = ui_keypad_key_rect(&k, UI_KEY_7);
        }
        CHECK(lowest <= UI_KEYS_FOOT);
        CHECK(lowest <= a.y + a.h);
        CHECK_EQ(first.w, k_hosts[h].kw);
        CHECK_EQ(first.h, k_hosts[h].kh);
    }
}

/* An area that ends above UI_KEYS_FOOT keeps its own foot, 8 px in; one
 * that reaches past it stops its keys there and still paints all of it. */
TEST_CASE(a_short_area_keeps_its_foot_and_a_long_one_paints_whole)
{
    ui_keypad_t k;
    memset(&k, 0, sizeof(k));
    ui_keypad_open(&k, gfx_rect_make(8, 8, 488, 400), "", "", 0.0f, 0.0f,
                   1.0f, 0);
    gfx_rect_t r = ui_keypad_key_rect(&k, UI_KEY_0);
    CHECK_EQ(r.y + r.h, 8 + 400 - 8);
    canvas();
    const gfx_rect_t a = gfx_rect_make(8, 8, 488, 524);
    ui_keypad_open(&k, a, "", "", 0.0f, 0.0f, 1.0f, 0);
    ui_keypad_render(&k, &cv);
    CHECK_EQ(ink_outside(a), 0);
    /* The area's last row is painted: the panel, not the black ground. */
    CHECK(fb[(a.y + a.h - 2) * W + a.x + 20] != 0);
    r = ui_keypad_key_rect(&k, UI_KEY_0);
    CHECK_EQ(r.y + r.h, UI_KEYS_FOOT);
}

/* ----------------------------------------------------- TARGET and the pins */

TEST_CASE(a_tall_target_row_counts_its_cells_in_10x20)
{
    ui_target_row_t t;
    /* The plan's SERVO card: 356 px inner, 44 px tall. */
    ui_target_row_init(&t, gfx_rect_make(648, 400, 356, 44));
    CHECK(ui_target_row_font(&t) == &gfx_font_10x20);
    const gfx_rect_t v = ui_target_row_value_rect(&t);
    CHECK_EQ(v.x, 648 + UI_TARGET_LABEL_W_TALL);
    CHECK_EQ(v.w, 356 - UI_TARGET_LABEL_W_TALL);
    CHECK_EQ(ui_target_row_cells(&t), (unsigned)((268 - 16) / 10));  /* 25 */
    /* "TARGET" and "ZIEL" fit the label's share with room to spare. */
    CHECK(gfx_text_width(&gfx_font_10x20, "TARGET", 1) + 16
          <= UI_TARGET_LABEL_W_TALL);
    /* Exactly UI_TARGET_TALL_H is tall; one pixel under it is 8 x 16 and
     * the short label. */
    ui_target_row_init(&t, gfx_rect_make(648, 400, 356, UI_TARGET_TALL_H));
    CHECK(ui_target_row_font(&t) == &gfx_font_10x20);
    CHECK_EQ(ui_target_row_value_rect(&t).x, 648 + UI_TARGET_LABEL_W_TALL);
    ui_target_row_init(&t, gfx_rect_make(648, 400, 356, UI_TARGET_TALL_H - 1));
    CHECK(ui_target_row_font(&t) == UI_FONT_LABEL);
    CHECK_EQ(ui_target_row_value_rect(&t).x, 648 + UI_TARGET_LABEL_W);
    CHECK_EQ(ui_target_row_cells(&t), (unsigned)((356 - 64 - 16) / 8));
    CHECK(ui_target_row_font(NULL) == UI_FONT_LABEL);
    /* A row no wider than the tall label has no box and no cells. */
    ui_target_row_init(&t, gfx_rect_make(0, 0, UI_TARGET_LABEL_W_TALL, 44));
    CHECK_EQ(ui_target_row_cells(&t), 0u);
}

/* The text the row draws fits its box in the face it draws in: the most
 * pins a TARGET can name, at the cells the row counts. */
TEST_CASE(a_tall_target_rows_text_fits_its_box)
{
    ui_target_row_t t;
    ui_target_row_init(&t, gfx_rect_make(648, 400, 356, 44));
    char text[96];
    const size_t n = ui_target_text(text, sizeof(text), "DSHOT600 BIDIR",
                                    0x1FFFFFFFull, ui_target_row_cells(&t));
    CHECK(n <= ui_target_row_cells(&t));
    CHECK(gfx_text_width(ui_target_row_font(&t), text, 1) + 16
          <= ui_target_row_value_rect(&t).w);
}

/* --------------------------------------------------------------- the plot */

static const ui_plot_series_t k_series[] = {
    { "VOLT", "V", 0x1234, 2, 5.0f, 0 },
};

TEST_CASE(the_plot_ring_holds_one_sample_per_panel_column)
{
    CHECK_EQ(UI_PLOT_HISTORY, 1024);
    CHECK_EQ(UI_PLOT_HISTORY, UI_PANEL_W);
    ui_plot_t *p = calloc(1, sizeof(*p));
    REQUIRE_ALLOC(p);
    CHECK_EQ(sizeof(p->ring), (size_t)24576);
    ui_plot_init(p, k_series, 1, 51.2f);
    /* 1,023 samples leave the left column of a panel-wide plot empty; the
     * 1,024th reaches it; the 1,025th wraps and the ring stays full. */
    const gfx_rect_t r = gfx_rect_make(0, 100, W, 200);
    for (int i = 0; i < UI_PLOT_HISTORY + 1; ++i) {
        if (i == UI_PLOT_HISTORY - 1 || i == UI_PLOT_HISTORY) {
            canvas();
            ui_plot_render(p, &cv, r);
            bool left = false;
            for (int y = r.y; y < r.y + r.h; ++y) {
                left = left || fb[y * W] == 0x1234;
            }
            CHECK_EQ(left, i == UI_PLOT_HISTORY);
        }
        const float v = 2.5f;
        ui_plot_push(p, &v);
    }
    CHECK_EQ(p->filled, UI_PLOT_HISTORY);
    CHECK_EQ(p->head, 1);
    free(p);
}

int main(void)
{
    RUN(a_keypad_over_a_workbench_card_has_large_keys_in_12x24);
    RUN(the_longest_german_word_sets_the_keypads_word_face);
    RUN(the_keypad_draws_inside_its_area_at_either_numeral_face);
    RUN(the_wifi_keyboard_draws_every_word_in_12x24);
    RUN(a_narrow_keyboard_takes_the_face_its_longest_word_fits);
    RUN(the_keyboards_word_face_holds_across_its_states);
    RUN(the_keyboard_draws_inside_its_area_at_every_width);
    RUN(tabs_take_10x20_when_tall_and_every_label_fits);
    RUN(a_choice_takes_20x36_from_56_px);
    RUN(a_choice_text_that_does_not_fit_20x36_takes_16x28);
    RUN(a_face_taller_than_its_box_does_not_fit);
    RUN(every_hosts_keys_end_above_the_alert_band);
    RUN(a_short_area_keeps_its_foot_and_a_long_one_paints_whole);
    RUN(a_tall_target_row_counts_its_cells_in_10x20);
    RUN(a_tall_target_rows_text_fits_its_box);
    RUN(the_plot_ring_holds_one_sample_per_panel_column);
    free(fb);
    return test_summary("overlays");
}
