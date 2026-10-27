/**
 * @file test_kst_panel_layout.c
 * @brief The KST and Chaservo programmer's layout on SERVO's left card: tap
 * targets, the alert band and the fullest list and page.
 *
 * Cases:
 * - every_control_of_every_page_is_a_tap_target_above_the_alert_band
 * - no_two_controls_of_a_page_overlap
 * - the_fullest_list_ends_above_cancel
 * - the_stretch_list_holds_every_factor_above_cancel
 * - the_fullest_write_page_ends_above_its_buttons_in_both_languages
 * - a_short_area_keeps_its_buttons_inside_it
 * - no_note_or_message_lies_under_the_alert_band
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_panel_rig.h"

/* The alert band's top in body pixels, and the 4px a control at the foot
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

/* @p r is a tap target: 48px each way at least, inside the area and
 * ending 4px above the alert band. */
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
    /* A stepped row's keys keep 4px from its value. */
    CHECK(val(PG_TUNE, 0, 0).x
          >= kst_panel_step_rect(PG_TUNE, 0, false).x
                 + kst_panel_step_rect(PG_TUNE, 0, false).w + 4);
    CHECK(kst_panel_step_rect(PG_TUNE, 0, true).x
          >= val(PG_TUNE, 0, 0).x + val(PG_TUNE, 0, 0).w + 4);
}

/* FILES with more files than a list holds: the entry that saves, five
 * files and the entry for the next, each a tap target 8px apart, the last
 * 8px above CANCEL. */
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

/* STRETCH on TUNE: a factor for every index the hard limit leaves, x1 to
 * x32, each a tap target above CANCEL. */
TEST_CASE(the_stretch_list_holds_every_factor_above_cancel)
{
    fresh();
    connected();
    tap(kst_panel_tab_rect(PG_TUNE));
    tap(val(PG_TUNE, 2, 0));
    CHECK_EQ(kst_panel_editor(), 2);
    CHECK_EQ(choices(), (int)KST_STRETCH_INDEX_MAX + 1);
    for (int i = 0; i < choices(); ++i) {
        target(kst_panel_choice_rect(i));
    }
    CHECK(kst_panel_choice_rect(choices() - 1).y
              + kst_panel_choice_rect(choices() - 1).h + 8
          <= kst_panel_choice_cancel_rect().y);
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

/* Ten values changed, a first write with a card, in the current language:
 * the write page open. */
static void fullest_write_page(void)
{
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
    tap(val(PG_SERVO, 4, 1));
    CHECK_EQ(kst_panel_editor(), 3);
}

/* Ten values changed, a first write with a card: the page with the most
 * lines.  Its rows end 8px above the buttons in English and German, and
 * both buttons are tap targets above the alert band. */
TEST_CASE(the_fullest_write_page_ends_above_its_buttons_in_both_languages)
{
    for (int lang = 0; lang < 2; ++lang) {
        fresh();
        ui_text_set_language(lang != 0 ? UI_LANG_DE : UI_LANG_EN);
        fullest_write_page();
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

/* An area that ends above the alert band: its buttons end 8px above its
 * foot, the band does not move them, and the fullest write page keeps its
 * lines to the 14 rows above them in English and German. */
TEST_CASE(a_short_area_keeps_its_buttons_inside_it)
{
    static const gfx_rect_t k_short = { UI_MARGIN, UI_MARGIN, 620, 420 };

    for (int lang = 0; lang < 2; ++lang) {
        fresh();
        ui_text_set_language(lang != 0 ? UI_LANG_DE : UI_LANG_EN);
        kst_panel_init(k_short);
        kst_panel_open();
        CHECK_EQ(kst_panel_choice_cancel_rect().y
                     + kst_panel_choice_cancel_rect().h,
                 k_short.y + k_short.h - 8);
        CHECK(k_short.y + k_short.h - 8 < FOOT);
        fullest_write_page();
        CHECK_EQ(kst_panel_apply_rect().y + kst_panel_apply_rect().h,
                 k_short.y + k_short.h - 8);
        CHECK_EQ(kst_panel_cancel_rect().y + kst_panel_cancel_rect().h,
                 k_short.y + k_short.h - 8);
        CHECK(page_rows() <= 14);
        CHECK(k_short.y + WARN_TOP_PX + page_rows() * ROW_PX + 8
              <= kst_panel_apply_rect().y);
        show();
    }
    ui_text_set_language(UI_LANG_EN);
}

/* Every message the model writes, with arguments of 1 to 5 digits, on the
 * SERVO tab of a connected servo and of a servo in doubt with its two
 * standing lines (the tallest pair in German, 4 rows), in English and
 * German: every line of the message is drawn, and the notes and the
 * message end 4px above the alert band and under the tab's last row. */
TEST_CASE(no_note_or_message_lies_under_the_alert_band)
{
    static const unsigned args[] = { 0u, 9u, 255u, 0xFFFFu };

    for (int doubt = 0; doubt < 2; ++doubt) {
        for (int lang = 0; lang < 2; ++lang) {
            fresh();
            ui_text_set_language(lang != 0 ? UI_LANG_DE : UI_LANG_EN);
            g_rig.card = true;
            if (doubt != 0) {
                cut_write();
                come_back();
                tap(kst_panel_tab_rect(PG_SERVO));
                tap(val(PG_SERVO, 1, 0));      /* READ ALL */
                settle();
                CHECK(kst_panel_standing_line(1) != NULL);
            } else {
                connected();
            }
            for (unsigned id = 1u; id < 256u; ++id) {
                for (size_t a = 0; a < sizeof(args) / sizeof(args[0]); ++a) {
                    const gfx_rect_t last = val(PG_SERVO, 4, 1);
                    char t[KST_PANEL_MSG_LINES][KST_PANEL_LINE];
                    gfx_rect_t drawn;
                    bool repeat;
                    int n;

                    M->msg = (kst_prog_msg_t){ .id = (uint8_t)id,
                                               .level = KST_PROG_WARN,
                                               .reg = 0x10u,
                                               .arg = (uint16_t)args[a],
                                               .seq = M->msg.seq + 1u };
                    n = kst_panel_message_lines(kst_prog_message(M), t);
                    /* A message that repeats the first standing line is
                     * left out; every other one is drawn whole. */
                    repeat = n > 0 && kst_panel_standing_line(0) != NULL
                             && strcmp(t[0], kst_panel_standing_line(0)) == 0;
                    drawn = kst_panel_notes_drawn();
                    CHECK_EQ(kst_panel_shown_message_lines(),
                             repeat ? 0 : n);
                    CHECK(drawn.y + drawn.h <= FOOT);
                    CHECK(drawn.h == 0 || drawn.y >= last.y + last.h);
                }
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(every_control_of_every_page_is_a_tap_target_above_the_alert_band);
    RUN(no_two_controls_of_a_page_overlap);
    RUN(the_fullest_list_ends_above_cancel);
    RUN(the_stretch_list_holds_every_factor_above_cancel);
    RUN(the_fullest_write_page_ends_above_its_buttons_in_both_languages);
    RUN(a_short_area_keeps_its_buttons_inside_it);
    RUN(no_note_or_message_lies_under_the_alert_band);
    return test_summary("kst_panel_layout");
}
