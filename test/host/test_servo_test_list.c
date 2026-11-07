/**
 * @file test_servo_test_list.c
 * @brief TEST's SETTINGS list (servo_test_list.c): the live line of each
 *        heading, the rows that lock and the reason each gives, HV SERVO's
 *        value and step, and SAVE and DISCARD.  The screen and its
 *        fixtures are servo_rig.h, the bench servo_bench_rig.h.
 *
 * Cases:
 * - the_headings_carry_a_live_line_each
 * - no_step_chosen_is_said_and_warns
 * - a_step_outside_the_supply_caps_warns
 * - hv_servo_shows_its_state_and_adds_the_high_steps
 * - the_stall_line_goes_with_the_edit_that_answers_it
 * - the_step_rows_lock_without_hv_servo_and_say_why
 * - the_length_rows_lock_by_the_length_mode_and_say_why
 * - save_asks_for_the_write_and_discard_restores_the_list
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"
#include "touch_feed.h"

#include "servo_screen.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_screen.h"
#include "ui_setlist.h"
#include "ui_text.h"

#include "servo_bench_rig.h"
#include "servo_rig.h"

/** @brief The stall note a STALL AT of 2.00A at a limit of 2.00A gives. */
#define STALL_NOTE_SHIPPED \
    "STALL AT 2.00A cannot be reached: current limit 2.00A"

/** @brief A fresh bench on TEST with its SETTINGS list open (the bench is
 *         put to SAFE for it). */
static void list_fresh(void)
{
    bench_fresh();
    short_runs();
    open_run_settings(0);
}

/**
 * @brief Heading row @p i's live line, as the list draws it.
 * @param i   the heading's row
 * @param out takes the line, "" for none
 * @param n   its size
 * @return Whether the line is in the warning colour.
 */
static bool heading_line(int i, char *out, size_t n)
{
    const ui_setlist_row_t r = servo_screen_list()->host->row(i);
    out[0] = '\0';
    if (r.custom != NULL && r.custom->value_text != NULL) {
        r.custom->value_text(out, n);
    }
    return r.custom != NULL && r.custom->locked != NULL
           && r.custom->locked() != NULL;
}

/**
 * @brief Custom row @p i's value text.
 * @param i   the row
 * @param out takes it
 * @param n   its size
 */
static void custom_value(int i, char *out, size_t n)
{
    const ui_setlist_row_t r = servo_screen_list()->host->row(i);
    out[0] = '\0';
    r.custom->value_text(out, n);
}

/** @brief The reason row @p id of the open list is locked, NULL if it is
 *         not.  @param id the setting */
static const char *locked_why(setting_id_t id)
{
    return servo_screen_list()->host->locked(id);
}

/* Four headings: the first three carry a line, LENGTH AND TIMING none.  As
 * shipped the steps are 4.8V and 6.0V and the brown-out, the walk starts at
 * 5.00V and the stall note is the shipped 2.00A at 2.00A. */
TEST_CASE(the_headings_carry_a_live_line_each)
{
    list_fresh();
    CHECK(servo_screen_list()->open);
    char line[96];
    CHECK(!heading_line(TL_H_STEPS, line, sizeof(line)));
    CHECK_STR_EQ(line, "4.8V, 6.0V, BROWN-OUT");
    CHECK(!heading_line(TL_H_BO, line, sizeof(line)));
    CHECK(strstr(line, "From 5.00V down in 0.20V steps to 3.30V") == line);
    CHECK(heading_line(TL_H_LIMITS, line, sizeof(line)));
    CHECK_STR_EQ(line, STALL_NOTE_SHIPPED);
    CHECK(!heading_line(TL_H_LENGTH, line, sizeof(line)));
    CHECK_STR_EQ(line, "");
    /* The steps line follows its rows. */
    sl_step(TL_STEP_60, -1);
    heading_line(TL_H_STEPS, line, sizeof(line));
    CHECK_STR_EQ(line, "4.8V, BROWN-OUT");
    sl_step(TL_BROWNOUT, -1);
    heading_line(TL_H_STEPS, line, sizeof(line));
    CHECK_STR_EQ(line, "4.8V");
}

/* Every step off and BROWN-OUT off leave nothing to run: the line says so
 * in the warning colour, and BROWN-OUT alone is a run. */
TEST_CASE(no_step_chosen_is_said_and_warns)
{
    list_fresh();
    char line[96];
    sl_step(TL_STEP_48, -1);
    sl_step(TL_STEP_60, -1);
    CHECK(!heading_line(TL_H_STEPS, line, sizeof(line)));
    CHECK_STR_EQ(line, "BROWN-OUT");
    sl_step(TL_BROWNOUT, -1);
    CHECK(heading_line(TL_H_STEPS, line, sizeof(line)));
    CHECK_STR_EQ(line, ui_servo_str(SERVO_STR_START_NO_STEPS));
    CHECK_STR_EQ(line, "NO STEP CHOSEN");
    /* A step back on ends it. */
    sl_step(TL_STEP_48, +1);
    CHECK(!heading_line(TL_H_STEPS, line, sizeof(line)));
    CHECK_STR_EQ(line, "4.8V");
}

/* A step the supply cannot give warns, over its top or under its bottom;
 * the line still names the steps the run would take. */
TEST_CASE(a_step_outside_the_supply_caps_warns)
{
    list_fresh();
    char line[96];
    const supply_caps_t low_top = { 3.3f, 5.5f, 0.02f, 0.5f, 5.0f, 0.05f };
    supply_screen_set_caps(&low_top);
    CHECK(heading_line(TL_H_STEPS, line, sizeof(line)));
    CHECK_STR_EQ(line, "4.8V, 6.0V, BROWN-OUT");
    sl_step(TL_STEP_60, -1);
    CHECK(!heading_line(TL_H_STEPS, line, sizeof(line)));
    const supply_caps_t high_bottom = { 5.0f, 21.0f, 0.02f, 0.5f, 5.0f,
                                        0.05f };
    supply_screen_set_caps(&high_bottom);
    CHECK(heading_line(TL_H_STEPS, line, sizeof(line)));
    CHECK_STR_EQ(line, "4.8V, BROWN-OUT");
    sl_step(TL_STEP_48, -1);
    CHECK(!heading_line(TL_H_STEPS, line, sizeof(line)));
}

/* HV SERVO's row reads the profile's state, a step takes the other, and
 * with it on the line names 7.4V and 8.4V too. */
TEST_CASE(hv_servo_shows_its_state_and_adds_the_high_steps)
{
    list_fresh();
    char v[16], line[96];
    custom_value(TL_HV, v, sizeof(v));
    const bool was_on = strcmp(v, "ON") == 0;
    CHECK(!was_on);
    CHECK_STR_EQ(v, "OFF");
    sl_step(TL_HV, +1);
    custom_value(TL_HV, v, sizeof(v));
    CHECK_STR_EQ(v, "ON");
    heading_line(TL_H_STEPS, line, sizeof(line));
    CHECK_STR_EQ(line, "4.8V, 6.0V, 7.4V, 8.4V, BROWN-OUT");
    sl_step(TL_HV, -1);
    custom_value(TL_HV, v, sizeof(v));
    CHECK_STR_EQ(v, "OFF");
    heading_line(TL_H_STEPS, line, sizeof(line));
    CHECK_STR_EQ(line, "4.8V, 6.0V, BROWN-OUT");
}

/* STALL AT at 1.95A is under the 2.00A limit: the note and its colour go
 * and come back with the edit. */
TEST_CASE(the_stall_line_goes_with_the_edit_that_answers_it)
{
    list_fresh();
    char line[96];
    CHECK(heading_line(TL_H_LIMITS, line, sizeof(line)));
    CHECK_STR_EQ(line, STALL_NOTE_SHIPPED);
    sl_step(TL_STALL, -1);
    CHECK_NEAR(settings_get(SET_SERVO_STALL_A), 1.95f, 1e-4f);
    CHECK(!heading_line(TL_H_LIMITS, line, sizeof(line)));
    CHECK_STR_EQ(line, "");
    sl_step(TL_STALL, +1);
    CHECK(heading_line(TL_H_LIMITS, line, sizeof(line)));
    CHECK_STR_EQ(line, STALL_NOTE_SHIPPED);
}

/* 7.4V and 8.4V give "With HV SERVO on only" until HV SERVO is on, and
 * the other rows of the steps never lock. */
TEST_CASE(the_step_rows_lock_without_hv_servo_and_say_why)
{
    list_fresh();
    CHECK_STR_EQ(locked_why(SET_SERVO_STEP_74), "With HV SERVO on only");
    CHECK_STR_EQ(locked_why(SET_SERVO_STEP_84), "With HV SERVO on only");
    CHECK(locked_why(SET_SERVO_STEP_48) == NULL);
    CHECK(locked_why(SET_SERVO_STEP_60) == NULL);
    CHECK(locked_why(SET_SERVO_STALL_A) == NULL);
    sl_step(TL_HV, +1);
    CHECK(locked_why(SET_SERVO_STEP_74) == NULL);
    CHECK(locked_why(SET_SERVO_STEP_84) == NULL);
    const bool on = settings_get_bool(SET_SERVO_STEP_84);
    sl_step(TL_STEP_84, +1);
    CHECK(settings_get_bool(SET_SERVO_STEP_84) != on);
}

/* By moves, TEST TIME is locked "With Length by TIME only"; by time,
 * MOVEMENTS is locked "With Length by MOVES only" and takes no step, and
 * TEST TIME does. */
TEST_CASE(the_length_rows_lock_by_the_length_mode_and_say_why)
{
    list_fresh();
    CHECK_EQ(settings_get_int(SET_SERVO_LEN_BY), 1);
    CHECK_STR_EQ(locked_why(SET_SERVO_LEN_S), "With Length by TIME only");
    CHECK(locked_why(SET_SERVO_LEN_MOVES) == NULL);
    CHECK(locked_why(SET_SERVO_LEN_BY) == NULL);
    CHECK(locked_why(SET_SERVO_DWELL_MS) == NULL);

    sl_step(TL_LEN_BY, -1);
    CHECK_EQ(settings_get_int(SET_SERVO_LEN_BY), 0);
    CHECK(locked_why(SET_SERVO_LEN_S) == NULL);
    CHECK_STR_EQ(locked_why(SET_SERVO_LEN_MOVES),
                 "With Length by MOVES only");
    const int moves = settings_get_int(SET_SERVO_LEN_MOVES);
    sl_step(TL_MOVES, +1);
    CHECK_EQ(settings_get_int(SET_SERVO_LEN_MOVES), moves);
    const int secs = settings_get_int(SET_SERVO_LEN_S);
    sl_step(TL_LEN_S, +1);
    CHECK(settings_get_int(SET_SERVO_LEN_S) != secs);
}

/* A change writes nothing until SAVE: SAVE asks for the write and the
 * list then closes without the leave question.  A change left with DISCARD
 * comes back as stored, rows and heading lines with it. */
TEST_CASE(save_asks_for_the_write_and_discard_restores_the_list)
{
    list_fresh();
    char line[96];
    sl_step(TL_STALL, -1);                              /* 1.95A */
    CHECK(settings_dirty());
    CHECK(!settings_save_asked());
    const gfx_rect_t save = ui_setlist_strip_rect(2);
    tap(save.x + save.w / 2, save.y + save.h / 2);
    CHECK(settings_save_asked());
    CHECK(servo_screen_list()->open);
    CHECK(home_tag());
    CHECK(!servo_screen_list()->warn);
    CHECK(!servo_screen_list()->open);
    CHECK_NEAR(settings_get(SET_SERVO_STALL_A), 1.95f, 1e-4f);
    settings_cancel_save();

    /* A second change, left with DISCARD: all of it goes. */
    open_run_settings(0);
    sl_step(TL_STALL, +1);
    sl_step(TL_STEP_48, -1);
    sl_step(TL_BROWNOUT, -1);
    heading_line(TL_H_STEPS, line, sizeof(line));
    CHECK_STR_EQ(line, "6.0V");
    CHECK(home_tag());
    CHECK(servo_screen_list()->warn);
    const gfx_rect_t d = ui_setlist_strip_rect(1);      /* DISCARD */
    tap(d.x + d.w / 2, d.y + d.h / 2);
    CHECK(settings_get_bool(SET_SERVO_STEP_48));
    CHECK(settings_get_bool(SET_SERVO_BROWNOUT));
    CHECK(!settings_save_asked());
    heading_line(TL_H_STEPS, line, sizeof(line));
    CHECK_STR_EQ(line, "4.8V, 6.0V, BROWN-OUT");
}

int main(void)
{
    RUN(the_headings_carry_a_live_line_each);
    RUN(no_step_chosen_is_said_and_warns);
    RUN(a_step_outside_the_supply_caps_warns);
    RUN(hv_servo_shows_its_state_and_adds_the_high_steps);
    RUN(the_stall_line_goes_with_the_edit_that_answers_it);
    RUN(the_step_rows_lock_without_hv_servo_and_say_why);
    RUN(the_length_rows_lock_by_the_length_mode_and_say_why);
    RUN(save_asks_for_the_write_and_discard_restores_the_list);
    return test_summary("servo_test_list");
}
