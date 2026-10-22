/**
 * @file test_esc_stick_panel_keypad.c
 * @brief ESC STICK's TIMING keypad: a tap on a row's value opens it; OK sets
 * the value through settings_set(), the call a "-" or "+" step ends in, and
 * CLOSE asks for the save as it does after a step; a value outside the
 * range is refused with the keypad open; CANCEL and the home tag change
 * nothing.
 *
 * Cases:
 * - a_tap_on_a_timing_value_opens_its_keypad
 * - the_range_ends_are_taken_and_a_value_past_them_refused
 * - a_digit_past_the_steps_decimals_is_not_typed
 * - a_typed_value_is_the_value_the_steps_reach
 * - cancel_and_the_home_tag_change_nothing
 * - nothing_under_the_keypad_presses
 * - an_armed_bench_takes_the_keypad_as_it_takes_the_steps
 * - a_lost_press_keeps_what_was_typed
 * - a_scrolled_page_opens_the_row_shown_and_takes_six_digits
 * - a_stop_over_the_keypad_sets_nothing
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick_panel_rig.h"

#include "ui_keypad.h"

/* The middle of a TIMING row's value, from esc_stick_int.h: the control
 * column at x 400, 216 px wide. */
#define VALUE_X (400 + 216 / 2)

/* The keypad's key @p key tapped, in the screen's coordinates. */
static void key(ui_key_t key)
{
    const gfx_rect_t r = ui_keypad_key_rect(esc_stick_panel_keypad(), key);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/* @p text typed on the keypad, digit by digit; '.' is the point. */
static void type(const char *text)
{
    static const ui_key_t k_digit[10] = {
        UI_KEY_0, UI_KEY_1, UI_KEY_2, UI_KEY_3, UI_KEY_4,
        UI_KEY_5, UI_KEY_6, UI_KEY_7, UI_KEY_8, UI_KEY_9,
    };
    for (const char *p = text; *p != '\0'; ++p) {
        key((*p == '.') ? UI_KEY_DOT : k_digit[*p - '0']);
    }
}

/* TIMING on the HobbyWing profile, at the defaults. */
static void at_timing_page(void)
{
    fresh();
    descend_to_hobbywing();
    tap(TIMING_X, TIMING_Y);
}

TEST_CASE(a_tap_on_a_timing_value_opens_its_keypad)
{
    at_timing_page();
    tap(VALUE_X, STEP_CY(2));                /* BEEP MIN */
    const ui_keypad_t *k = esc_stick_panel_keypad();
    CHECK(k->open);
    CHECK_EQ(esc_stick_panel_keypad_setting(), (int)SET_STICK_BEEP_MIN);
    CHECK_STR_EQ(k->title, ui_setting_label(SET_STICK_BEEP_MIN));
    CHECK_STR_EQ(k->unit, "ms");
    CHECK_NEAR(k->min, 20.0f, 1e-6f);
    CHECK_NEAR(k->max, 2000.0f, 1e-6f);
    CHECK_EQ(k->decimals, 0);
    /* Under the output strip, above the alert band. */
    CHECK_EQ(k->area.y, PROG_FULL_Y);
    CHECK(k->area.y + k->area.h <= UI_BODY_H - UI_ALERT_H);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    stick_invalidate();
    scr->render(&cv, 0);
    const gfx_rect_t ok = ui_keypad_key_rect(k, UI_KEY_OK);
    CHECK_EQ(fb[(ok.y + ok.h / 2) * W + ok.x + 4],
             ui_theme_color(UI_C_ACCENT));
    /* The row's name opens nothing. */
    key(UI_KEY_CANCEL);
    tap(120, STEP_CY(2));
    CHECK(!esc_stick_panel_keypad()->open);
    CHECK_EQ(esc_stick_panel_keypad_setting(), -1);
}

TEST_CASE(the_range_ends_are_taken_and_a_value_past_them_refused)
{
    at_timing_page();
    tap(VALUE_X, STEP_CY(2));
    type("20");
    key(UI_KEY_OK);
    CHECK(!esc_stick_panel_keypad()->open);
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 20);
    tap(VALUE_X, STEP_CY(2));
    type("2000");
    key(UI_KEY_OK);
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 2000);
    tap(VALUE_X, STEP_CY(2));
    type("2001");
    key(UI_KEY_OK);
    CHECK(esc_stick_panel_keypad()->open);
    CHECK(esc_stick_panel_keypad()->refused);
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 2000);
    key(UI_KEY_CLR);
    type("19");
    key(UI_KEY_OK);
    CHECK(esc_stick_panel_keypad()->refused);
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 2000);
    key(UI_KEY_CANCEL);
}

TEST_CASE(a_digit_past_the_steps_decimals_is_not_typed)
{
    at_timing_page();
    tap(VALUE_X, STEP_CY(0));                /* VOLTAGE, steps of 0.1 V */
    type("12.34");
    CHECK_STR_EQ(esc_stick_panel_keypad()->entry, "12.3");
    key(UI_KEY_OK);
    CHECK_NEAR(settings_get(SET_STICK_V), 12.3f, 1e-4f);
    tap(VALUE_X, STEP_CY(1));                /* CURRENT, steps of 0.05 A */
    CHECK_EQ(esc_stick_panel_keypad()->decimals, 2);
    type("1.234");
    CHECK_STR_EQ(esc_stick_panel_keypad()->entry, "1.23");
    key(UI_KEY_OK);
    CHECK_NEAR(settings_get(SET_STICK_I), 1.25f, 1e-4f);   /* the nearer */
}

/* The value the keypad sets is the value the steps reach, and CLOSE asks
 * for its save either way. */
TEST_CASE(a_typed_value_is_the_value_the_steps_reach)
{
    at_timing_page();
    tap(VALUE_X, STEP_CY(1));
    type("1.25");
    key(UI_KEY_OK);
    const float typed = settings_get(SET_STICK_I);
    tap(WRITE_X, BTN_CY);                    /* CLOSE */
    CHECK(settings_save_asked());
    settings_cancel_save();

    at_timing_page();
    for (int i = 0; i < 5; ++i) {
        tap(STEP_UP_X, STEP_CY(1));          /* 1.00 A + 5 x 0.05 A */
    }
    const float stepped = settings_get(SET_STICK_I);
    tap(WRITE_X, BTN_CY);
    CHECK(settings_save_asked());
    settings_cancel_save();
    CHECK(typed == stepped);
    CHECK_NEAR(typed, 1.25f, 1e-4f);
}

TEST_CASE(cancel_and_the_home_tag_change_nothing)
{
    at_timing_page();
    tap(VALUE_X, STEP_CY(3));                /* GAP MIN */
    type("555");
    key(UI_KEY_CANCEL);
    CHECK(!esc_stick_panel_keypad()->open);
    CHECK_EQ(settings_get_int(SET_STICK_GAP_MIN), 200);
    CHECK(!settings_dirty());
    /* The home tag closes the keypad, and TIMING stays: its value opens
     * the keypad again. */
    tap(VALUE_X, STEP_CY(3));
    type("555");
    CHECK(scr->back());
    CHECK(!esc_stick_panel_keypad()->open);
    CHECK_EQ(settings_get_int(SET_STICK_GAP_MIN), 200);
    tap(VALUE_X, STEP_CY(3));
    CHECK(esc_stick_panel_keypad()->open);
    /* Leaving the screen takes it with it. */
    type("555");
    scr->leave();
    CHECK(!esc_stick_panel_keypad()->open);
    CHECK_EQ(settings_get_int(SET_STICK_GAP_MIN), 200);
    CHECK(!settings_dirty());
}

TEST_CASE(nothing_under_the_keypad_presses)
{
    at_timing_page();
    tap(STEP_UP_X, STEP_CY(2));              /* BEEP MIN 210 */
    tap(VALUE_X, STEP_CY(2));
    /* DEFAULTS and CLOSE lie under the keypad, its keys over them; BACK
     * lies under its top. */
    CHECK(gfx_rect_contains(esc_stick_panel_keypad()->area, READ_X, BTN_CY));
    CHECK(gfx_rect_contains(esc_stick_panel_keypad()->area, WRITE_X, BTN_CY));
    tap(BACK_X, BACK_Y);
    CHECK(esc_stick_panel_keypad()->open);
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 210);
    CHECK(!settings_save_asked());
    key(UI_KEY_CANCEL);
}

/* TIMING's steps are not refused while the bench is ARMED, and neither is
 * its keypad. */
TEST_CASE(an_armed_bench_takes_the_keypad_as_it_takes_the_steps)
{
    at_timing_page();
    esc_stick_panel_bench(100u, true, 0u, 0u, true);
    scr->tick(0.02f);
    tap(STEP_UP_X, STEP_CY(2));
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 210);
    tap(VALUE_X, STEP_CY(2));
    CHECK(esc_stick_panel_keypad()->open);
    type("300");
    key(UI_KEY_OK);
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 300);
    esc_stick_panel_bench(200u, false, 0u, 0u, true);
}

TEST_CASE(a_lost_press_keeps_what_was_typed)
{
    at_timing_page();
    tap(VALUE_X, STEP_CY(4));
    type("12");
    const gfx_rect_t r = ui_keypad_key_rect(esc_stick_panel_keypad(),
                                            UI_KEY_3);
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 2, .x = (int16_t)(r.x + r.w / 2),
                                   .y = (int16_t)(r.y + r.h / 2),
                                   .strength = 40 } };
    scr->event(&e);
    CHECK(esc_stick_panel_keypad()->pressed >= 0);
    CHECK(esc_stick_pages.has_press(2));
    scr->cancel();
    CHECK(esc_stick_panel_keypad()->pressed < 0);
    CHECK(!esc_stick_pages.has_press(2));
    CHECK(esc_stick_panel_keypad()->open);
    CHECK_STR_EQ(esc_stick_panel_keypad()->entry, "12");
}

/* Paged down to the last rows, a value opens the keypad of the row drawn
 * there, not of the row at that place on the first page: Timeout, whose
 * 600000 ms is a six-digit entry. */
TEST_CASE(a_scrolled_page_opens_the_row_shown_and_takes_six_digits)
{
    at_timing_page();
    for (int i = 0; i < 10; ++i) {
        tap(PAGE_DN_X, PAGE_CY);             /* to the last page: 7 rows */
    }
    tap(VALUE_X, STEP_CY(4));                /* 7 + 4: Timeout */
    CHECK_EQ(esc_stick_panel_keypad_setting(), (int)SET_STICK_TIMEOUT);
    type("600001");
    key(UI_KEY_OK);
    CHECK(esc_stick_panel_keypad()->refused);
    CHECK_EQ(settings_get_int(SET_STICK_TIMEOUT), 180000);
    key(UI_KEY_DEL);
    type("0");
    CHECK_STR_EQ(esc_stick_panel_keypad()->entry, "600000");
    key(UI_KEY_OK);
    CHECK(!esc_stick_panel_keypad()->open);
    CHECK_EQ(settings_get_int(SET_STICK_TIMEOUT), 600000);
    /* The last row, Hysteresis, from the same page. */
    tap(VALUE_X, STEP_CY(6));
    CHECK_EQ(esc_stick_panel_keypad_setting(), (int)SET_STICK_HYST);
    key(UI_KEY_CANCEL);
}

/* A STOP while the keypad is open leaves it open with what was typed and
 * sets nothing. */
TEST_CASE(a_stop_over_the_keypad_sets_nothing)
{
    at_timing_page();
    tap(VALUE_X, STEP_CY(2));
    type("300");
    esc_stick_panel_bench(100u, false, 1u, 0u, true);    /* a STOP */
    scr->tick(0.02f);
    CHECK(esc_stick_panel_keypad()->open);
    CHECK_STR_EQ(esc_stick_panel_keypad()->entry, "300");
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 200);
    key(UI_KEY_CANCEL);
    CHECK_EQ(settings_get_int(SET_STICK_BEEP_MIN), 200);
}

int main(void)
{
    RUN(a_tap_on_a_timing_value_opens_its_keypad);
    RUN(the_range_ends_are_taken_and_a_value_past_them_refused);
    RUN(a_digit_past_the_steps_decimals_is_not_typed);
    RUN(a_typed_value_is_the_value_the_steps_reach);
    RUN(cancel_and_the_home_tag_change_nothing);
    RUN(nothing_under_the_keypad_presses);
    RUN(an_armed_bench_takes_the_keypad_as_it_takes_the_steps);
    RUN(a_lost_press_keeps_what_was_typed);
    RUN(a_scrolled_page_opens_the_row_shown_and_takes_six_digits);
    RUN(a_stop_over_the_keypad_sets_nothing);
    return test_summary("esc_stick_panel_keypad");
}
