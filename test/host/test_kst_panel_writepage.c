/**
 * @file test_kst_panel_writepage.c
 * @brief The programmer overlay's unwritten edits on LEAVE's page: each value
 * from the servo's to the new one and the count of the rest; a soft limit
 * asked again; and the lines of a write that runs.
 *
 * Cases:
 * - the_leave_page_lists_each_unwritten_value_from_the_servos_to_the_new
 * - more_unwritten_values_than_lines_are_counted
 * - a_value_further_out_than_the_confirmed_one_opens_the_page_again
 * - a_write_that_runs_says_writing_and_not_interrupted
 * - the_writing_line_has_a_singular_and_a_form_without_a_count
 *
 * SPDX-License-Identifier: MIT
 */

#include "kst_panel_screen_rig.h"

/* --------------------------------------------------------- the SERVO screen */

/* ------------------------------------------- what a write is about to change */

static int warn_lines(void)
{
    int n = 0;

    while (kst_panel_warning_line(n) != NULL) {
        ++n;
    }
    return n;
}

/* The line of the open warning page that begins with @p head, -1 for
 * none. */
static int warn_at(const char *head)
{
    for (int i = 0; kst_panel_warning_line(i) != NULL; ++i) {
        if (strncmp(kst_panel_warning_line(i), head, strlen(head)) == 0) {
            return i;
        }
    }
    return -1;
}

/** @brief The rows the open warning page's lines take. */
static int page_rows(void)
{
    int rows = 0;

    for (int i = 0; kst_panel_warning_line(i) != NULL; ++i) {
        rows += kst_panel_text_rows(kst_panel_warning_line(i));
    }
    return rows;
}

/** @brief The open page's lines end 8px above its buttons: WARN_TOP, 64px,
 *         and LINE_H, 20px, of kst_panel_int.h. */
static bool page_fits(void)
{
    return AREA.y + 64 + page_rows() * 20 + 8 <= kst_panel_apply_rect().y;
}

/* MAX. DUTY typed as @p digits on its keypad. */
static void type_duty(const char *digits)
{
    tap(kst_panel_tab_rect(PG_TUNE));
    tap(val(PG_TUNE, 1, 0));
    CHECK_EQ(kst_panel_editor(), 1);
    type_text(digits);
}

TEST_CASE(the_leave_page_lists_each_unwritten_value_from_the_servos_to_the_new)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        connected();
        /* A value past a soft limit, confirmed, and one inside its limits. */
        type_duty("97");
        CHECK_EQ(kst_panel_editor(), 3);
        hold(2.1f);
        CHECK_EQ(kst_panel_editor(), 0);
        edit(KST_F_SOFT_START, (uint16_t)(1u - servo(KST_F_SOFT_START)));
        tap(kst_panel_tab_rect(PG_SERVO));
        tap(val(PG_SERVO, 0, 0));       /* LEAVE */
        CHECK_EQ(kst_panel_editor(), 3);
        /* Under the line that says they are lost, the values in the order
         * of the tabs: the servo's, the new one, the unit once, a point in
         * both languages. */
        CHECK_STR_EQ(kst_panel_warning_line(0), TR(KP_LEAVE_CHANGES));
        CHECK_STR_EQ(kst_panel_warning_line(1), "MAX. DUTY: 96 -> 97%");
        CHECK_EQ(kst_panel_warning_level(1), KST_PROG_WARN);
        CHECK_EQ(warn_at("SOFT START: "), 2);
        CHECK(strstr(kst_panel_warning_line(2), " -> ") != NULL);
        CHECK_EQ(kst_panel_warning_level(2),
                 servo(KST_F_SOFT_START) != 0u ? KST_PROG_WARN
                                               : KST_PROG_INFO);
        /* Under them what every LEAVE says. */
        CHECK_EQ(warn_at(TR(KP_LEAVE_1)), 3);
        CHECK_EQ(warn_at(TR(KP_LEAVE_2)), 4);
        CHECK(page_fits());
        tap(kst_panel_cancel_rect());
        CHECK_EQ(kst_prog_changes(M), 2);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* More unwritten values than LEAVE's page has lines for: the first of
 * them, and the count of the rest.  No line of the page is lost to them. */
TEST_CASE(more_unwritten_values_than_lines_are_counted)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        char more[KST_PANEL_LINE];
        int n;

        fresh();
        ui_text_set_language((ui_lang_t)lang);
        connected();
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
        CHECK_EQ(kst_prog_changes(M), 10);
        tap(kst_panel_tab_rect(PG_SERVO));
        tap(val(PG_SERVO, 0, 0));
        CHECK_EQ(kst_panel_editor(), 3);
        n = warn_lines();
        /* The lost line, the values that fit, the count of the rest, and
         * the two lines of every LEAVE. */
        CHECK_STR_EQ(kst_panel_warning_line(0), TR(KP_LEAVE_CHANGES));
        CHECK_EQ(warn_at("DEAD BAND: "), 1);
        snprintf(more, sizeof(more), TR(KP_VALUES_MORE),
                 (unsigned)(10 - (n - 4)));
        CHECK_STR_EQ(kst_panel_warning_line(n - 3), more);
        CHECK(n - 4 >= 6);
        CHECK(n - 4 < 10);
        CHECK_STR_EQ(kst_panel_warning_line(n - 2), TR(KP_LEAVE_1));
        CHECK_STR_EQ(kst_panel_warning_line(n - 1), TR(KP_LEAVE_2));
        CHECK(page_fits());
        tap(kst_panel_cancel_rect());
        rig_frames(50);
        CHECK_EQ(g_rig.sim.n_writes, 0);
        CHECK_EQ(kst_prog_changes(M), 10);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* A soft limit confirmed for a value is confirmed for that value: one
 * further from the servo's is asked about again, and the page counts from
 * the servo's value. */
TEST_CASE(a_value_further_out_than_the_confirmed_one_opens_the_page_again)
{
    uint16_t kept;

    fresh();
    connected();
    type_duty("97");
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK_STR_EQ(kst_panel_warning_line(0), "MAX. DUTY: 96 -> 97%");
    hold(2.1f);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_changes(M), 1);
    kept = draft(KST_F_DUTY);
    type_duty("98");
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK_STR_EQ(kst_panel_warning_line(0), "MAX. DUTY: 96 -> 98%");
    CHECK(warns(TR(KP_W_DUTY_RAISED)));
    /* CANCEL keeps the value confirmed before. */
    tap(kst_panel_cancel_rect());
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(draft(KST_F_DUTY), kept);
    type_duty("97");
    CHECK_EQ(kst_panel_editor(), 0);
    type_duty("98");
    CHECK_EQ(kst_panel_editor(), 3);
    hold(2.1f);
    CHECK_EQ(kst_panel_editor(), 0);
    /* Back towards the servo's value needs no page. */
    type_duty("97");
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_changes(M), 1);
}

/* A write that runs has its record on the card and is no interrupted
 * write: the tab says that it writes and how many registers, RESTORE and
 * its entry that discards the record take no tap, and nothing of it stays
 * when the write is done. */
TEST_CASE(a_write_that_runs_says_writing_and_not_interrupted)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        char want[KST_PANEL_LINE];
        unsigned seen = 0, counted = 0;

        fresh();
        ui_text_set_language((ui_lang_t)lang);
        g_rig.card = true;
        connected();
        edits();
        hold_write();
        CHECK_EQ(kst_panel_editor(), 0);
        for (unsigned i = 0; i < 30000u; ++i) {
            unsigned regs = 0, frames = 0;

            rig_frame();
            tick(0.001f);
            if (i % 16u == 0u) {
                show();
            }
            if (kst_prog_state(M) == KST_PROG_PROGRAMMING
                && !kst_prog_writing(M)) {
                break;
            }
            if (!kst_prog_writing(M)) {
                continue;
            }
            ++seen;
            /* The record of this write is held, and is not what stands. */
            CHECK_STR_EQ(kst_panel_standing_line(0), TR(KP_WRITING));
            CHECK(kst_panel_standing_line(1) == NULL);
            CHECK(!kst_panel_row_live(PG_SERVO, 3, 0));
            if (kst_prog_state(M) != KST_PROG_WORKING
                || kst_prog_planning(M)) {
                continue;
            }
            CHECK(kst_prog_journal(M));
            CHECK_EQ(sscanf(status(0), TR(KP_STATE_WRITING_N), &regs,
                            &frames), 2);
            CHECK_EQ(regs, kst_prog_write_steps(M));
            CHECK(regs >= 2u);
            snprintf(want, sizeof(want), TR(KP_STATE_WRITING_N), regs,
                     (unsigned)M->f.rd.frames);
            CHECK_STR_EQ(status(0), want);
            ++counted;
        }
        CHECK(seen > 0u);
        CHECK(counted > 0u);
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
        CHECK(kst_panel_standing_line(0) == NULL);
        CHECK_EQ(kst_prog_write_steps(M), 0);
        CHECK_EQ(g_rig.sim.n_bad_writes, 0);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* A write of one register, and one whose order the IOmcu plans. */
TEST_CASE(the_writing_line_has_a_singular_and_a_form_without_a_count)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        char want[KST_PANEL_LINE];
        bool one = false, any = false;

        fresh();
        ui_text_set_language((ui_lang_t)lang);
        connected();
        edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 1u));
        hold_write();
        for (unsigned i = 0; i < 30000u && !one; ++i) {
            rig_frame();
            tick(0.001f);
            if (kst_prog_state(M) == KST_PROG_WORKING
                && !kst_prog_planning(M) && kst_prog_writing(M)) {
                CHECK_EQ(kst_prog_write_steps(M), 1);
                snprintf(want, sizeof(want), TR(KP_STATE_WRITING_ONE),
                         (unsigned)M->f.rd.frames);
                CHECK_STR_EQ(status(0), want);
                one = true;
            }
        }
        CHECK(one);
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
        /* RELEASE PAIRING: the IOmcu's own plan, no count here. */
        tap(val(PG_SERVO, 2, 1));
        tap(kst_panel_choice_rect(choice(TR(KP_RELEASE_TITLE))));
        CHECK_EQ(kst_panel_editor(), 3);
        hold(2.1f);
        for (unsigned i = 0; i < 30000u && !any; ++i) {
            rig_frame();
            tick(0.001f);
            if (kst_prog_state(M) == KST_PROG_WORKING
                && !kst_prog_planning(M) && kst_prog_writing(M)) {
                CHECK_EQ(kst_prog_write_steps(M), 0);
                snprintf(want, sizeof(want), TR(KP_STATE_WRITING),
                         (unsigned)M->f.rd.frames);
                CHECK_STR_EQ(status(0), want);
                any = true;
            }
        }
        CHECK(any);
        settle();
        CHECK_EQ(msg(), KST_PROG_MSG_RELEASED);
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(the_leave_page_lists_each_unwritten_value_from_the_servos_to_the_new);
    RUN(more_unwritten_values_than_lines_are_counted);
    RUN(a_value_further_out_than_the_confirmed_one_opens_the_page_again);
    RUN(a_write_that_runs_says_writing_and_not_interrupted);
    RUN(the_writing_line_has_a_singular_and_a_form_without_a_count);
    return test_summary("kst_panel_writepage");
}
