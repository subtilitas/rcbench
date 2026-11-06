/**
 * @file test_footer.c
 * @brief The common footer (ui_footer.h): its three slots of 328 x 48px at
 *        x 10, 348 and 686, body y 444 to 492, 4px above the alert band;
 *        the hit test with its 10px gaps; the name and its line, the line
 *        in the warning colour when the screen drives nothing; SETTINGS'
 *        neutral fill; the ground cleared under the slots only.
 *
 * Cases:
 * - the_slots_sit_at_setups_strip_columns_over_the_alert_band
 * - the_hit_test_finds_each_slot_and_no_gap
 * - a_name_alone_takes_the_head_face_where_it_fits
 * - a_line_under_the_name_takes_the_warning_colour_on_request
 * - settings_is_neutral_and_greys_when_it_takes_no_press
 * - the_ground_clears_the_footer_rows_only
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "gfx.h"
#include "ui_footer.h"
#include "ui_layout.h"
#include "ui_text.h"
#include "ui_theme.h"

#define W UI_PANEL_W
#define H UI_BODY_H

static gfx_color_t *fb;
static gfx_canvas_t cv;

/** @brief A body-sized canvas in the dark theme, filled with @p fill. */
static void fresh(gfx_color_t fill)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb);
    }
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
    gfx_canvas_init(&cv, fb, W, H, W);
    for (int i = 0; i < W * H; ++i) {
        fb[i] = fill;
    }
}

/** @brief Pixels of colour @p col inside @p r. */
static int count_in(gfx_rect_t r, gfx_color_t col)
{
    int n = 0;
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            n += (fb[y * W + x] == col) ? 1 : 0;
        }
    }
    return n;
}

/** @brief Pixels outside @p r that are not @p bg. */
static int ink_outside(gfx_rect_t r, gfx_color_t bg)
{
    int n = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (!gfx_rect_contains(r, x, y)) {
                n += (fb[y * W + x] != bg) ? 1 : 0;
            }
        }
    }
    return n;
}

/** @brief The rows of @p r that hold a pixel other than @p bg: their
 *         first and last, -1 for none. */
static void ink_rows(gfx_rect_t r, gfx_color_t bg, int *top, int *bottom)
{
    *top = -1;
    *bottom = -1;
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            if (fb[y * W + x] != bg) {
                *top = (*top < 0) ? y : *top;
                *bottom = y;
                break;
            }
        }
    }
}

TEST_CASE(the_slots_sit_at_setups_strip_columns_over_the_alert_band)
{
    static const int k_x[UI_FOOTER_SLOTS] = { 10, 348, 686 };
    for (int i = 0; i < UI_FOOTER_SLOTS; ++i) {
        const gfx_rect_t r = ui_footer_rect((ui_footer_slot_t)i);
        CHECK_EQ(r.x, k_x[i]);
        CHECK_EQ(r.y, 444);
        CHECK_EQ(r.w, 328);
        CHECK_EQ(r.h, 48);
        CHECK(r.h >= UI_TAP_MIN);
    }
    /* The last slot ends 10px short of the panel's edge. */
    const gfx_rect_t s = ui_footer_rect(UI_FOOTER_SETTINGS);
    CHECK_EQ(s.x + s.w, W - UI_FOOTER_M);
    /* 4px above the alert band, which covers panel y 556 to 600. */
    CHECK_EQ(UI_FOOTER_Y + UI_FOOTER_H + 4 + UI_BAND_H, UI_ALERT_Y);
    CHECK_EQ(UI_FOOTER_FLOOR, 436);
    /* A slot out of range is empty. */
    const gfx_rect_t none = ui_footer_rect(UI_FOOTER_SLOTS);
    CHECK_EQ(none.w, 0);
    CHECK_EQ(none.h, 0);
}

TEST_CASE(the_hit_test_finds_each_slot_and_no_gap)
{
    for (int i = 0; i < UI_FOOTER_SLOTS; ++i) {
        const gfx_rect_t r = ui_footer_rect((ui_footer_slot_t)i);
        CHECK_EQ(ui_footer_hit(r.x, r.y), i);
        CHECK_EQ(ui_footer_hit(r.x + r.w - 1, r.y + r.h - 1), i);
        CHECK_EQ(ui_footer_hit(r.x - 1, r.y + r.h / 2), -1);
        CHECK_EQ(ui_footer_hit(r.x + r.w, r.y + r.h / 2), -1);
        CHECK_EQ(ui_footer_hit(r.x + r.w / 2, r.y - 1), -1);
        CHECK_EQ(ui_footer_hit(r.x + r.w / 2, r.y + r.h), -1);
    }
    /* Every column of the 10px gaps is off every slot. */
    for (int x = 338; x < 348; ++x) {
        CHECK_EQ(ui_footer_hit(x, 468), -1);
    }
    for (int x = 676; x < 686; ++x) {
        CHECK_EQ(ui_footer_hit(x, 468), -1);
    }
}

TEST_CASE(a_name_alone_takes_the_head_face_where_it_fits)
{
    const gfx_color_t bg = ui_theme_color(UI_C_BG);
    const gfx_rect_t r = ui_footer_rect(UI_FOOTER_NAME);
    int top, bottom;

    fresh(bg);
    ui_footer_name(&cv, r, "SUPPLY", NULL, false);
    CHECK_EQ(ink_outside(r, bg), 0);
    ink_rows(r, bg, &top, &bottom);
    CHECK(top >= 0);
    const int head = bottom - top + 1;
    /* The same name as a body face draws it. */
    fresh(bg);
    gfx_text(&cv, 0, 0, "SUPPLY", &gfx_font_12x24, ui_theme_color(UI_C_TEXT),
             1);
    int btop, bbottom;
    ink_rows((gfx_rect_t){ 0, 0, 200, 40 }, bg, &btop, &bbottom);
    const int body = bbottom - btop + 1;
    CHECK(head > body);                        /* the 20 x 36 face */

    /* 16 cells and more do not fit 16px short of 328px in 20px cells: the
     * 12 x 24 face. */
    fresh(bg);
    ui_footer_name(&cv, r, "MOTOR & ESC LONGER", "", false);
    CHECK_EQ(ink_outside(r, bg), 0);
    ink_rows(r, bg, &top, &bottom);
    CHECK(top >= 0);
    CHECK(bottom - top + 1 <= body + 4);
    /* Centred in the slot's height. */
    const int above = top - r.y, below = r.y + r.h - 1 - bottom;
    CHECK(abs(above - below) <= 6);
}

TEST_CASE(a_line_under_the_name_takes_the_warning_colour_on_request)
{
    const gfx_color_t bg = ui_theme_color(UI_C_BG);
    const gfx_rect_t r = ui_footer_rect(UI_FOOTER_NAME);
    const gfx_rect_t line = { r.x, (int16_t)(r.y + 27), r.w, 21 };
    const gfx_rect_t name = { r.x, r.y, r.w, 27 };

    fresh(bg);
    ui_footer_name(&cv, r, "MOTOR & ESC", "DSHOT300 GPIO 0", false);
    CHECK_EQ(ink_outside(r, bg), 0);
    CHECK(count_in(line, ui_theme_color(UI_C_TEXT_DIM)) > 0);
    CHECK_EQ(count_in(line, ui_theme_color(UI_C_WARN)), 0);
    CHECK(count_in(name, ui_theme_color(UI_C_TEXT)) > 0);
    /* 16px from the slot's left edge. */
    int left = W;
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            if (fb[y * W + x] != bg && x < left) {
                left = x;
            }
        }
    }
    CHECK(left >= r.x + 16 && left <= r.x + 18);

    fresh(bg);
    ui_footer_name(&cv, r, "MOTOR & ESC", "NO MOTOR PIN", true);
    CHECK(count_in(line, ui_theme_color(UI_C_WARN)) > 0);
    CHECK_EQ(count_in(line, ui_theme_color(UI_C_TEXT_DIM)), 0);
}

TEST_CASE(settings_is_neutral_and_greys_when_it_takes_no_press)
{
    const gfx_color_t bg = ui_theme_color(UI_C_BG);
    const gfx_rect_t r = ui_footer_rect(UI_FOOTER_SETTINGS);
    const size_t n = (size_t)W * H * sizeof(gfx_color_t);

    fresh(bg);
    ui_footer_settings(&cv, false, true);
    CHECK_EQ(ink_outside(r, bg), 0);
    /* The neutral fill over most of the button, no accent or warning. */
    CHECK(count_in(r, ui_theme_color(UI_C_PANEL_HI)) > r.w * r.h / 2);
    CHECK_EQ(count_in(r, ui_theme_color(UI_C_WARN)), 0);
    gfx_color_t *on = malloc(n);
    REQUIRE_ALLOC(on);
    memcpy(on, fb, n);

    fresh(bg);
    ui_footer_settings(&cv, true, true);
    CHECK(memcmp(on, fb, n) != 0);             /* pressed draws pressed */

    fresh(bg);
    ui_footer_settings(&cv, false, false);
    CHECK(memcmp(on, fb, n) != 0);             /* grey, faint label */
    CHECK_EQ(ink_outside(r, bg), 0);
    free(on);

    /* German: OPTIONEN fits the slot too. */
    ui_text_set_language(UI_LANG_DE);
    fresh(bg);
    ui_footer_settings(&cv, false, true);
    CHECK_EQ(ink_outside(r, bg), 0);
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(the_ground_clears_the_footer_rows_only)
{
    const gfx_color_t bg = ui_theme_color(UI_C_BG);
    const gfx_color_t junk = ui_theme_color(UI_C_DANGER);
    fresh(junk);
    ui_footer_clear(&cv);
    const gfx_rect_t band = { 0, UI_FOOTER_Y, W, UI_FOOTER_H };
    CHECK_EQ(count_in(band, bg), W * UI_FOOTER_H);
    const gfx_rect_t above = { 0, 0, W, UI_FOOTER_Y };
    CHECK_EQ(count_in(above, junk), W * UI_FOOTER_Y);
    const gfx_rect_t below = { 0, UI_FOOTER_Y + UI_FOOTER_H, W,
                               H - UI_FOOTER_Y - UI_FOOTER_H };
    CHECK_EQ(count_in(below, junk), below.w * below.h);
    ui_footer_clear(NULL);                     /* no canvas: nothing */
}

int main(void)
{
    RUN(the_slots_sit_at_setups_strip_columns_over_the_alert_band);
    RUN(the_hit_test_finds_each_slot_and_no_gap);
    RUN(a_name_alone_takes_the_head_face_where_it_fits);
    RUN(a_line_under_the_name_takes_the_warning_colour_on_request);
    RUN(settings_is_neutral_and_greys_when_it_takes_no_press);
    RUN(the_ground_clears_the_footer_rows_only);
    free(fb);
    return test_summary("footer");
}
