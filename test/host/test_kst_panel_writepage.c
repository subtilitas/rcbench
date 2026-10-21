/*
 * The programmer overlay's write page: each value from the servo's to the
 * new one, the count of the rest, and the horn a later write asks for.
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

/* MAX. DUTY typed as @p digits on its keypad. */
static void type_duty(const char *digits)
{
    tap(kst_panel_tab_rect(PG_TUNE));
    tap(val(PG_TUNE, 1, 0));
    CHECK_EQ(kst_panel_editor(), 1);
    type_text(digits);
}

TEST_CASE(the_write_page_lists_each_value_from_the_servos_to_the_new)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        connected();
        enable();
        /* A value past a soft limit, confirmed, and one inside its limits. */
        type_duty("97");
        CHECK_EQ(kst_panel_editor(), 3);
        hold(2.1f);
        CHECK_EQ(kst_panel_editor(), 0);
        edit(KST_F_SOFT_START, (uint16_t)(1u - servo(KST_F_SOFT_START)));
        tap(kst_panel_tab_rect(PG_SERVO));
        tap(val(PG_SERVO, 4, 0));
        CHECK_EQ(kst_panel_editor(), 3);
        /* The values first, in the order of the tabs: the servo's, the
         * new one, the unit once, a point in both languages. */
        CHECK_STR_EQ(kst_panel_warning_line(0), "MAX. DUTY: 96 -> 97 %");
        CHECK_EQ(kst_panel_warning_level(0), KST_PROG_WARN);
        CHECK_EQ(warn_at("SOFT START: "), 1);
        CHECK(strstr(kst_panel_warning_line(1), " -> ") != NULL);
        CHECK_EQ(kst_panel_warning_level(1),
                 servo(KST_F_SOFT_START) != 0u ? KST_PROG_WARN
                                               : KST_PROG_INFO);
        /* Under them what every write says. */
        CHECK(warn_at(TR(KP_HORN_FREE)) > 1);
        CHECK(warns(TR(KP_WRITE_1)));
        CHECK(warns(TR(KP_WRITE_2)));
        tap(kst_panel_cancel_rect());
    }
    ui_text_set_language(UI_LANG_EN);
}

/* More values than the page has lines for: the first of them, and the
 * count of the rest.  No line of the page is lost to them. */
TEST_CASE(a_write_of_more_values_than_lines_counts_the_rest)
{
    char more[KST_PANEL_LINE];

    fresh();
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
    CHECK_EQ(kst_prog_changes(M), 10);
    tap(kst_panel_tab_rect(PG_SERVO));
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    /* With a card: 6 values, the count of the other 4, and the 5 lines of
     * a first write. */
    CHECK_EQ(warn_lines(), 12);
    CHECK_EQ(warn_at("DEAD BAND: "), 0);
    CHECK_EQ(warn_at("PULSE LOWER: "), 5);
    snprintf(more, sizeof(more), TR(KP_VALUES_MORE), 4u);
    CHECK_STR_EQ(kst_panel_warning_line(6), more);
    CHECK_STR_EQ(kst_panel_warning_line(7), TR(KP_HORN_FREE));
    CHECK_STR_EQ(kst_panel_warning_line(8), TR(KP_BENCH_FIRST));
    CHECK_STR_EQ(kst_panel_warning_line(9), TR(KP_WRITE_1));
    CHECK_STR_EQ(kst_panel_warning_line(10), TR(KP_WRITE_2));
    CHECK_STR_EQ(kst_panel_warning_line(11), TR(KP_WRITE_BACKUP));
    tap(kst_panel_cancel_rect());

    /* With no card: 5 values, the count of the other 5, and the 6 lines
     * that say what a write with no file leaves out. */
    g_rig.card = false;
    settle();
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK_EQ(warn_lines(), 12);
    snprintf(more, sizeof(more), TR(KP_VALUES_MORE), 5u);
    CHECK_STR_EQ(kst_panel_warning_line(5), more);
    CHECK_STR_EQ(kst_panel_warning_line(6), TR(KP_HORN_FREE));
    CHECK(!warns(TR(KP_WRITE_1)));
    CHECK_STR_EQ(kst_panel_warning_line(8), TR(KP_WRITE_2));
    CHECK_STR_EQ(kst_panel_warning_line(9), TR(KP_NO_FILE_1));
    CHECK_STR_EQ(kst_panel_warning_line(11), TR(KP_NO_FILE_3));
    tap(kst_panel_cancel_rect());
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
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
    CHECK_STR_EQ(kst_panel_warning_line(0), "MAX. DUTY: 96 -> 97 %");
    hold(2.1f);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_changes(M), 1);
    kept = draft(KST_F_DUTY);
    type_duty("98");
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK_STR_EQ(kst_panel_warning_line(0), "MAX. DUTY: 96 -> 98 %");
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

/* The second write of a session asks for a free horn again where it turns
 * the horn round or moves it: the direction, the centre, an end point or an
 * angle. */
TEST_CASE(a_later_write_that_moves_the_horn_asks_for_a_free_horn_again)
{
    static const struct {
        kst_field_id_t id;
        int by;
    } k_moves[] = {
        { KST_F_REVERSION, 0 },   { KST_F_NEUTRAL, 5 },
        { KST_F_PULSE_LOWER, 10 }, { KST_F_PULSE_UPPER, -10 },
        { KST_F_LEFT_ANGLE, -1 }, { KST_F_RIGHT_ANGLE, -1 },
    };

    fresh();
    connected();
    enable();
    edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 1u));
    tap(val(PG_SERVO, 4, 0));
    CHECK(warns(TR(KP_HORN_FREE)));
    hold(2.1f);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK(!kst_prog_first_write(M));
    if (!kst_prog_write_enabled(M)) {
        enable();
    }
    for (unsigned i = 0; i < sizeof(k_moves) / sizeof(k_moves[0]); ++i) {
        const kst_field_id_t id = k_moves[i].id;

        edit(id, k_moves[i].by == 0
                     ? (uint16_t)(1u - servo(id))
                     : (uint16_t)((int)servo(id) + k_moves[i].by));
        tap(val(PG_SERVO, 4, 0));
        CHECK_EQ(kst_panel_editor(), 3);
        CHECK(warns(TR(KP_HORN_FREE)));
        CHECK(warns(TR(KP_BENCH_FIRST)));
        tap(kst_panel_cancel_rect());
        kst_prog_discard(M);
        CHECK_EQ(kst_prog_changes(M), 0);
    }
    /* A value that moves nothing does not. */
    edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 1u));
    tap(val(PG_SERVO, 4, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(!warns(TR(KP_HORN_FREE)));
    tap(kst_panel_cancel_rect());
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
        enable();
        edits();
        tap(val(PG_SERVO, 4, 0));
        hold(2.1f);
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
            CHECK(!kst_panel_row_live(PG_SERVO, 2, 0));
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
        enable();
        edit(KST_F_BOOST, (uint16_t)(servo(KST_F_BOOST) - 1u));
        tap(val(PG_SERVO, 4, 0));
        hold(2.1f);
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
        if (!kst_prog_write_enabled(M)) {
            enable();
        }
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
    RUN(the_write_page_lists_each_value_from_the_servos_to_the_new);
    RUN(a_write_of_more_values_than_lines_counts_the_rest);
    RUN(a_value_further_out_than_the_confirmed_one_opens_the_page_again);
    RUN(a_later_write_that_moves_the_horn_asks_for_a_free_horn_again);
    RUN(a_write_that_runs_says_writing_and_not_interrupted);
    RUN(the_writing_line_has_a_singular_and_a_form_without_a_count);
    return test_summary("kst_panel_writepage");
}
