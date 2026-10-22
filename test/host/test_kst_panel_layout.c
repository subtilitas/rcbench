/**
 * @file test_kst_panel_layout.c
 * @brief The KST and Chaservo programmer's layout on SERVO's left card: tap
 * targets, the alert band and the fullest list and page.
 *
 * Cases:
 * - every_control_of_every_page_is_a_tap_target_above_the_alert_band
 * - no_two_controls_of_a_page_overlap
 * - the_fullest_list_ends_above_cancel
 * - the_fullest_write_page_ends_above_its_buttons_in_both_languages
 * - a_short_area_keeps_its_buttons_inside_it
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_panel_rig.h"

/* The alert band's top in body pixels, and the 4 px a control at the foot
 * keeps above it. */
#define BAND_TOP (UI_ALERT_Y - UI_BAND_H)
#define FOOT     (BAND_TOP - 4)

/* A warning page's first line under the area's top, and a row's pitch:
 * WARN_TOP and LINE_H in kst_panel_int.h. */
#define WARN_TOP_PX 64
#define ROW_PX      20

static bool overlap(gfx_rect_t a, gfx_rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h
           && b.y < a.y + a.h;
}

/* @p r is a tap target: 48 px each way at least, inside the area and
 * ending 4 px above the alert band. */
static void target(gfx_rect_t r)
{
    CHECK(r.w >= UI_TAP_MIN);
    CHECK(r.h >= UI_TAP_MIN);
    CHECK(r.x >= AREA.x);
    CHECK(r.y >= AREA.y);
    CHECK(r.x + r.w <= AREA.x + AREA.w);
    CHECK(r.y + r.h <= FOOT);
}

/* The page's controls into @p out, @p max at most: the tabs, CLOSE, every
 * value and every step key.  Returns their count. */
static int controls(int page, gfx_rect_t *out, int max)
{
    int n = 0;

    for (int t = 0; t < 5 && n < max; ++t) {
        out[n++] = kst_panel_tab_rect(t);
    }
    if (n < max) {
        out[n++] = kst_panel_close_rect();
    }
    for (int row = 0; row < 6; ++row) {
        for (int col = 0; col < 2; ++col) {
            const gfx_rect_t v = val(page, row, col);

            if (v.w == 0 || n >= max) {
                continue;
            }
            out[n++] = v;
            /* A stepped row: its value lies between its keys. */
            if (col == 0 && kst_panel_step_rect(page, row, false).x < v.x
                && n + 2 <= max) {
                out[n++] = kst_panel_step_rect(page, row, false);
                out[n++] = kst_panel_step_rect(page, row, true);
            }
        }
    }
    return n;
}

TEST_CASE(every_control_of_every_page_is_a_tap_target_above_the_alert_band)
{
    gfx_rect_t c[32];

    fresh();
    connected();
    for (int page = PG_SERVO; page <= PG_LOCKED; ++page) {
        const int n = controls(page, c, 32);

        /* Five tabs, CLOSE and a row at least. */
        CHECK(n > 6);
        for (int i = 0; i < n; ++i) {
            target(c[i]);
        }
    }
    /* The tabs and CLOSE are one row under the area's top. */
    CHECK_EQ(kst_panel_tab_rect(0).y, kst_panel_close_rect().y);
    CHECK_EQ(kst_panel_tab_rect(0).h, kst_panel_close_rect().h);
}

TEST_CASE(no_two_controls_of_a_page_overlap)
{
    gfx_rect_t c[32];

    fresh();
    connected();
    for (int page = PG_SERVO; page <= PG_LOCKED; ++page) {
        const int n = controls(page, c, 32);

        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                CHECK(!overlap(c[i], c[j]));
            }
        }
    }
    /* A stepped row's keys keep 4 px from its value. */
    CHECK(val(PG_TUNE, 0, 0).x
          >= kst_panel_step_rect(PG_TUNE, 0, false).x
                 + kst_panel_step_rect(PG_TUNE, 0, false).w + 4);
    CHECK(kst_panel_step_rect(PG_TUNE, 0, true).x
          >= val(PG_TUNE, 0, 0).x + val(PG_TUNE, 0, 0).w + 4);
}

/* FILES with more files than a list holds: the entry that saves, five
 * files and the entry for the next, each a tap target 8 px apart, the last
 * 8 px above CANCEL. */
TEST_CASE(the_fullest_list_ends_above_cancel)
{
    const kst_image_t sv = sim_bench_image();

    fresh();
    connected();
    for (unsigned i = 0; i < 7u; ++i) {
        char name[KST_PROG_NAME_MAX];

        snprintf(name, sizeof(name), "LONGNAME%u", i);
        (void)fake_card_put(&g_rig.fc, name, &sv);
    }
    files();
    CHECK_EQ(kst_panel_editor(), 2);
    CHECK_EQ(choices(), 7);
    for (int i = 0; i < choices(); ++i) {
        const gfx_rect_t r = kst_panel_choice_rect(i);

        target(r);
        if (i > 0) {
            const gfx_rect_t up = kst_panel_choice_rect(i - 1);

            CHECK_EQ(r.y - (up.y + up.h), 8);
        }
    }
    {
        const gfx_rect_t last = kst_panel_choice_rect(choices() - 1);
        const gfx_rect_t cancel = kst_panel_choice_cancel_rect();

        target(cancel);
        CHECK(last.y + last.h + 8 <= cancel.y);
        CHECK_EQ(cancel.y + cancel.h, FOOT);
    }
    show();
}

/* The rows of the open warning page's lines. */
static int page_rows(void)
{
    int rows = 0;

    for (int i = 0; kst_panel_warning_line(i) != NULL; ++i) {
        rows += kst_panel_text_rows(kst_panel_warning_line(i));
    }
    return rows;
}

/* Ten values changed, a first write with a card: the page with the most
 * lines.  Its rows end 8 px above the buttons in English and German, and
 * both buttons are tap targets above the alert band. */
TEST_CASE(the_fullest_write_page_ends_above_its_buttons_in_both_languages)
{
    for (int lang = 0; lang < 2; ++lang) {
        fresh();
        ui_text_set_language(lang != 0 ? UI_LANG_DE : UI_LANG_EN);
        g_rig.card = true;
        connected();
        enable();
        edit(KST_F_DEAD_BAND, (uint16_t)(servo(KST_F_DEAD_BAND) + 2u));
        edit(KST_F_DUTY, (uint16_t)(servo(KST_F_DUTY) - 2u));
        edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 3u));
        edit(KST_F_SOFT_START, (uint16_t)(1u - servo(KST_F_SOFT_START)));
        edit(KST_F_NEUTRAL, (uint16_t)(servo(KST_F_NEUTRAL) + 5u));
        edit(KST_F_PULSE_LOWER, (uint16_t)(servo(KST_F_PULSE_LOWER) + 10u));
        edit(KST_F_PULSE_UPPER, (uint16_t)(servo(KST_F_PULSE_UPPER) - 10u));
        edit(KST_F_LEFT_ANGLE, (uint16_t)(servo(KST_F_LEFT_ANGLE) - 1u));
        edit(KST_F_RIGHT_ANGLE, (uint16_t)(servo(KST_F_RIGHT_ANGLE) - 1u));
        edit(KST_F_REVERSION, (uint16_t)(1u - servo(KST_F_REVERSION)));
        tap(kst_panel_tab_rect(PG_SERVO));
        tap(val(PG_SERVO, 4, 0));
        CHECK_EQ(kst_panel_editor(), 3);
        {
            const gfx_rect_t apply = kst_panel_apply_rect();
            const gfx_rect_t cancel = kst_panel_cancel_rect();

            target(apply);
            target(cancel);
            CHECK(!overlap(apply, cancel));
            CHECK_EQ(apply.y + apply.h, FOOT);
            CHECK(AREA.y + WARN_TOP_PX + page_rows() * ROW_PX + 8
                  <= apply.y);
        }
        show();
    }
    ui_text_set_language(UI_LANG_EN);
}

/* An area that ends above the alert band: its buttons end 8 px above its
 * foot, the band does not move them. */
TEST_CASE(a_short_area_keeps_its_buttons_inside_it)
{
    static const gfx_rect_t k_short = { UI_MARGIN, UI_MARGIN, 620, 420 };

    fresh();
    kst_panel_init(k_short);
    kst_panel_open();
    CHECK_EQ(kst_panel_apply_rect().y + kst_panel_apply_rect().h,
             k_short.y + k_short.h - 8);
    CHECK_EQ(kst_panel_cancel_rect().y + kst_panel_cancel_rect().h,
             k_short.y + k_short.h - 8);
    CHECK_EQ(kst_panel_choice_cancel_rect().y
                 + kst_panel_choice_cancel_rect().h,
             k_short.y + k_short.h - 8);
    CHECK(k_short.y + k_short.h - 8 < FOOT);
}

int main(void)
{
    RUN(every_control_of_every_page_is_a_tap_target_above_the_alert_band);
    RUN(no_two_controls_of_a_page_overlap);
    RUN(the_fullest_list_ends_above_cancel);
    RUN(the_fullest_write_page_ends_above_its_buttons_in_both_languages);
    RUN(a_short_area_keeps_its_buttons_inside_it);
    return test_summary("kst_panel_layout");
}
