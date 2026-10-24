/**
 * @file test_kst_panel_edit.c
 * @brief The programmer overlay's tabs and edits: the keypad's range, the hard
 * and soft limits, the switch, the step key and its repeat.
 *
 * Cases:
 * - the_tabs_show_their_fields
 * - a_locked_value_takes_no_tap
 * - the_keypads_range_is_the_cores_and_a_value_inside_it_is_an_edit
 * - a_value_beyond_a_hard_limit_cannot_be_entered
 * - a_value_past_a_soft_limit_waits_for_a_2_s_hold
 * - the_hold_button_fills_through_its_own_revision
 * - stretch_is_chosen_from_the_values_the_core_takes
 * - a_switch_is_toggled_by_its_tap_and_warns_where_the_core_does
 * - a_switch_the_core_refuses_says_which_limit
 * - a_step_key_acts_on_the_press_and_repeats_while_held
 * - the_repeat_speeds_up_after_1_8_s_more
 * - a_step_past_a_soft_limit_opens_the_page_and_ends_the_repeat
 * - neutral_steps_through_its_page
 *
 * SPDX-License-Identifier: MIT
 */

#include "kst_panel_screen_rig.h"

TEST_CASE(the_tabs_show_their_fields)
{
    fresh();
    connected();
    tap(kst_panel_tab_rect(PG_TUNE));
    CHECK_EQ(kst_panel_page(), PG_TUNE);
    tap(val(PG_TUNE, 1, 0));
    CHECK_EQ(kst_panel_editor(), 1);
    CHECK_STR_EQ(kst_panel_keypad()->title, "MAX. DUTY");
    CHECK_STR_EQ(kst_panel_keypad()->unit, "%");
    key(UI_KEY_CANCEL);
    tap(kst_panel_tab_rect(PG_PULSE));
    CHECK_EQ(kst_panel_page(), PG_PULSE);
    tap(val(PG_PULSE, 1, 1));
    CHECK_STR_EQ(kst_panel_keypad()->title, "PULSE UPPER");
    CHECK_STR_EQ(kst_panel_keypad()->unit, "us");
    key(UI_KEY_CANCEL);
    tap(kst_panel_tab_rect(PG_FAILSAFE));
    CHECK_EQ(kst_panel_page(), PG_FAILSAFE);
    tap(val(PG_FAILSAFE, 1, 1));
    CHECK_STR_EQ(kst_panel_keypad()->title, TR(KP_FS_AFTER));
    CHECK_STR_EQ(kst_panel_keypad()->unit, "s");
    key(UI_KEY_CANCEL);
    tap(kst_panel_tab_rect(PG_LOCKED));
    CHECK_EQ(kst_panel_page(), PG_LOCKED);
    tap(kst_panel_tab_rect(PG_SERVO));
    CHECK_EQ(kst_panel_page(), PG_SERVO);
    CHECK_EQ(kst_prog_changes(M), 0);
}

TEST_CASE(a_locked_value_takes_no_tap)
{
    const uint32_t seq0 = (fresh(), connected(), kst_prog_message(M)->seq);

    tap(kst_panel_tab_rect(PG_LOCKED));
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 2; ++col) {
            tap(val(PG_LOCKED, row, col));
            CHECK_EQ(kst_panel_editor(), 0);
        }
    }
    CHECK_EQ(kst_prog_changes(M), 0);
    CHECK_EQ(kst_prog_message(M)->seq, seq0);
}

/* ---------------------------------------------------------------- the edits */

TEST_CASE(the_keypads_range_is_the_cores_and_a_value_inside_it_is_an_edit)
{
    uint16_t lo = 0, hi = 0;
    int32_t dlo = 0, dhi = 0;

    fresh();
    connected();
    CHECK(kst_prog_range(M, KST_F_BOOST, &lo, &hi));
    CHECK(kst_field_display(KST_F_BOOST, lo, &dlo));
    CHECK(kst_field_display(KST_F_BOOST, hi, &dhi));
    tap(kst_panel_tab_rect(PG_TUNE));
    tap(val(PG_TUNE, 1, 1));
    CHECK_EQ(kst_panel_editor(), 1);
    CHECK_NEAR(kst_panel_keypad()->min, (float)(dlo < dhi ? dlo : dhi) / 10.0f,
               0.01f);
    CHECK_NEAR(kst_panel_keypad()->max, (float)(dlo < dhi ? dhi : dlo) / 10.0f,
               0.01f);
    CHECK_NEAR(kst_panel_keypad()->value, 300.0f, 0.01f);
    /* 250.0us is below what the servo holds: taken without a page. */
    key(UI_KEY_2);
    key(UI_KEY_5);
    key(UI_KEY_0);
    key(UI_KEY_OK);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_changes(M), 1);
    CHECK(draft(KST_F_BOOST) != servo(KST_F_BOOST));
    /* The servo is untouched, and nothing was sent. */
    CHECK_EQ(g_rig.sim.n_writes, 0);
    CHECK_EQ(g_rig.starts, 2);
}

TEST_CASE(a_value_beyond_a_hard_limit_cannot_be_entered)
{
    uint16_t lo = 0, hi = 0;
    int32_t top = 0;

    fresh();
    connected();
    CHECK(kst_prog_range(M, KST_F_DUTY, &lo, &hi));
    CHECK(kst_field_display(KST_F_DUTY, KST_DUTY_RAW_MAX, &top));
    tap(kst_panel_tab_rect(PG_TUNE));
    tap(val(PG_TUNE, 1, 0));
    CHECK_NEAR(kst_panel_keypad()->max, (float)top, 0.01f);
    /* 100% and 0% are past the duty limits: the keypad keeps them. */
    key(UI_KEY_1);
    key(UI_KEY_0);
    key(UI_KEY_0);
    key(UI_KEY_OK);
    CHECK_EQ(kst_panel_editor(), 1);
    CHECK(kst_panel_keypad()->refused);
    key(UI_KEY_CLR);
    key(UI_KEY_0);
    key(UI_KEY_OK);
    CHECK_EQ(kst_panel_editor(), 1);
    key(UI_KEY_CANCEL);
    CHECK_EQ(kst_prog_changes(M), 0);
    CHECK_EQ(draft(KST_F_DUTY), servo(KST_F_DUTY));
}

TEST_CASE(a_value_past_a_soft_limit_waits_for_a_2_s_hold)
{
    const gfx_rect_t apply = (fresh(), kst_panel_apply_rect());

    connected();
    tap(kst_panel_tab_rect(PG_TUNE));
    tap(val(PG_TUNE, 1, 0));
    key(UI_KEY_9);
    key(UI_KEY_7);
    key(UI_KEY_OK);
    /* The page names the change and the reason; nothing is taken yet. */
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK_STR_EQ(kst_panel_warning_line(0), "MAX. DUTY: 96 -> 97 %");
    CHECK(warns(TR(KP_W_DUTY_RAISED)));
    CHECK_EQ(kst_prog_changes(M), 0);
    /* 1.9s is not a hold. */
    hold(1.9f);
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK_EQ(kst_prog_changes(M), 0);
    /* A late frame is worth UI_HOLD_MAX_CREDIT_S and no more. */
    ev(cx(apply), cy(apply), TOUCH_EVENT_DOWN, 1);
    tick(5.0f);
    CHECK_EQ(kst_panel_editor(), 3);
    ev(cx(apply), cy(apply), TOUCH_EVENT_UP, 1);
    /* A finger that slides off the button abandons the hold. */
    ev(cx(apply), cy(apply), TOUCH_EVENT_DOWN, 1);
    ticks(1.0f);
    ev(apply.x - 4, cy(apply), TOUCH_EVENT_MOVE, 1);
    CHECK(!kst_panel_has_press(1));
    ev(cx(apply), cy(apply), TOUCH_EVENT_MOVE, 1);
    ticks(2.0f);
    ev(cx(apply), cy(apply), TOUCH_EVENT_UP, 1);
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK_EQ(kst_prog_changes(M), 0);
    /* CANCEL drops the value. */
    tap(kst_panel_cancel_rect());
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_changes(M), 0);
    CHECK_EQ(draft(KST_F_DUTY), servo(KST_F_DUTY));
    /* The hold takes it. */
    tap(val(PG_TUNE, 1, 0));
    key(UI_KEY_9);
    key(UI_KEY_7);
    key(UI_KEY_OK);
    hold(2.1f);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_changes(M), 1);
    CHECK(draft(KST_F_DUTY) != servo(KST_F_DUTY));
    CHECK_EQ(g_rig.sim.n_writes, 0);
}

TEST_CASE(the_hold_button_fills_through_its_own_revision)
{
    const gfx_rect_t apply = (fresh(), kst_panel_apply_rect());
    uint32_t rev, hold_rev;

    connected();
    tap(val(PG_SERVO, 3, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    hold_rev = kst_panel_hold_revision();
    tick(0.1f);
    CHECK_EQ(kst_panel_hold_revision(), hold_rev);
    ev(cx(apply), cy(apply), TOUCH_EVENT_DOWN, 1);
    rev = kst_panel_revision();
    tick(0.1f);
    tick(0.1f);
    CHECK(kst_panel_hold_revision() != hold_rev);
    CHECK_EQ(kst_panel_revision(), rev);
    ev(cx(apply), cy(apply), TOUCH_EVENT_UP, 1);
    CHECK(!kst_prog_write_enabled(M));
}

TEST_CASE(stretch_is_chosen_from_the_values_the_core_takes)
{
    uint16_t lo = 0, hi = 0;

    fresh();
    connected();
    CHECK(kst_prog_range(M, KST_F_STRETCH, &lo, &hi));
    tap(kst_panel_tab_rect(PG_TUNE));
    tap(val(PG_TUNE, 2, 0));
    CHECK_EQ(kst_panel_editor(), 2);
    CHECK_EQ(choices(), (int)(hi - lo) + 1);
    /* CANCEL leaves it. */
    tap(kst_panel_choice_cancel_rect());
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_changes(M), 0);
    /* A higher stretch is past a soft limit. */
    tap(val(PG_TUNE, 2, 0));
    tap(kst_panel_choice_rect((int)(servo(KST_F_STRETCH) - lo) + 1));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_W_STRETCH_RAISED)));
    CHECK_EQ(kst_prog_changes(M), 0);
    hold(2.1f);
    CHECK_EQ(kst_prog_changes(M), 1);
    CHECK_EQ(draft(KST_F_STRETCH), servo(KST_F_STRETCH) + 1u);
    /* A release off the entry chooses nothing. */
    tap(val(PG_TUNE, 2, 0));
    ev(cx(kst_panel_choice_rect(0)), cy(kst_panel_choice_rect(0)),
       TOUCH_EVENT_DOWN, 1);
    ev(cx(kst_panel_choice_rect(0)), AREA.y + AREA.h - 60, TOUCH_EVENT_UP, 1);
    CHECK_EQ(kst_panel_editor(), 2);
    tap(kst_panel_choice_cancel_rect());
}

TEST_CASE(a_switch_is_toggled_by_its_tap_and_warns_where_the_core_does)
{
    fresh();
    connected();
    tap(kst_panel_tab_rect(PG_TUNE));
    /* SOFT START off. */
    tap(val(PG_TUNE, 2, 1));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_W_SOFT_START_OFF)));
    tap(kst_panel_cancel_rect());
    CHECK_EQ(kst_prog_changes(M), 0);
    tap(val(PG_TUNE, 2, 1));
    hold(2.1f);
    CHECK_EQ(kst_prog_changes(M), 1);
    /* Back on is no warning. */
    tap(val(PG_TUNE, 2, 1));
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(kst_prog_changes(M), 0);
    /* REVERSION. */
    tap(kst_panel_tab_rect(PG_PULSE));
    tap(val(PG_PULSE, 3, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_W_REVERSION_CHANGED)));
    tap(kst_panel_cancel_rect());
    /* MOVE ON SIGNAL LOSS. */
    tap(kst_panel_tab_rect(PG_FAILSAFE));
    tap(val(PG_FAILSAFE, 0, 0));
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_W_ALLOW_UNCONT_ON)));
    tap(kst_panel_cancel_rect());
    CHECK_EQ(kst_prog_changes(M), 0);
}

TEST_CASE(a_switch_the_core_refuses_says_which_limit)
{
    fresh();
    connected();
    /* SPEED LIMIT on with SPEED below the least the core takes. */
    tap(kst_panel_tab_rect(PG_TUNE));
    tap(val(PG_TUNE, 3, 0));
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(msg(), KST_PROG_MSG_EDIT_HARD);
    CHECK_EQ(kst_prog_message(M)->arg, KST_H_SPD_SEL_LOW_SPD);
    CHECK_STR_EQ(said(), TR(KP_H_SPD_SEL_LOW_SPD));
    CHECK_EQ(kst_prog_changes(M), 0);
}

TEST_CASE(a_step_key_acts_on_the_press_and_repeats_while_held)
{
    const uint16_t was = (fresh(), connected(), draft(KST_F_DEAD_BAND));
    const gfx_rect_t plus = kst_panel_step_rect(PG_TUNE, 0, true);
    uint16_t lo = 0, hi = 0;
    int steps;

    tap(kst_panel_tab_rect(PG_TUNE));
    CHECK(kst_prog_range(M, KST_F_DEAD_BAND, &lo, &hi));
    /* The press is one step: a wider dead band, no warning. */
    ev(cx(plus), cy(plus), TOUCH_EVENT_DOWN, 1);
    steps = abs((int)draft(KST_F_DEAD_BAND) - (int)was);
    CHECK_EQ(steps, 1);
    CHECK_EQ(kst_panel_editor(), 0);
    /* Nothing more before 0.45s. */
    ticks(0.40f);
    CHECK_EQ(abs((int)draft(KST_F_DEAD_BAND) - (int)was), 1);
    /* Then 8 a second: 1s after the press, 1 + 1 + 4. */
    ticks(0.60f);
    steps = abs((int)draft(KST_F_DEAD_BAND) - (int)was);
    CHECK(steps >= 5 && steps <= 7);
    /* A finger that leaves the key ends the repeat. */
    ev(plus.x + plus.w + 30, cy(plus), TOUCH_EVENT_MOVE, 1);
    CHECK(!kst_panel_has_press(1));
    ticks(1.0f);
    CHECK_EQ(abs((int)draft(KST_F_DEAD_BAND) - (int)was), steps);
    ev(plus.x + plus.w + 30, cy(plus), TOUCH_EVENT_UP, 1);
    /* Held long, the value stops at the end of the core's range. */
    ev(cx(plus), cy(plus), TOUCH_EVENT_DOWN, 1);
    ticks(40.0f);
    ev(cx(plus), cy(plus), TOUCH_EVENT_UP, 1);
    show();
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK(draft(KST_F_DEAD_BAND) == lo || draft(KST_F_DEAD_BAND) == hi);
    CHECK_EQ(g_rig.sim.n_writes, 0);
}

TEST_CASE(the_repeat_speeds_up_after_1_8_s_more)
{
    const gfx_rect_t plus = (fresh(), kst_panel_step_rect(PG_TUNE, 0, true));
    uint16_t at_2s, at_3s;

    connected();
    /* From the narrow end there is room for the count. */
    tap(kst_panel_tab_rect(PG_TUNE));
    ev(cx(plus), cy(plus), TOUCH_EVENT_DOWN, 1);
    ticks(2.25f);
    at_2s = draft(KST_F_DEAD_BAND);
    ticks(1.0f);
    at_3s = draft(KST_F_DEAD_BAND);
    ev(cx(plus), cy(plus), TOUCH_EVENT_UP, 1);
    /* 8Hz for 1.8s is 15 steps and the press; then 30 a second. */
    CHECK(abs((int)at_2s - (int)servo(KST_F_DEAD_BAND)) >= 15);
    CHECK(abs((int)at_2s - (int)servo(KST_F_DEAD_BAND)) <= 17);
    CHECK(abs((int)at_3s - (int)at_2s) >= 28);
    CHECK(abs((int)at_3s - (int)at_2s) <= 31);
}

TEST_CASE(a_step_past_a_soft_limit_opens_the_page_and_ends_the_repeat)
{
    const uint16_t was = (fresh(), connected(), draft(KST_F_DEAD_BAND));
    const gfx_rect_t minus = kst_panel_step_rect(PG_TUNE, 0, false);

    tap(kst_panel_tab_rect(PG_TUNE));
    /* A narrower dead band. */
    ev(cx(minus), cy(minus), TOUCH_EVENT_DOWN, 1);
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK(warns(TR(KP_W_DEAD_BAND_NARROWED)));
    CHECK_EQ(draft(KST_F_DEAD_BAND), was);
    /* The finger still on the glass holds nothing. */
    ticks(3.0f);
    ev(cx(minus), cy(minus), TOUCH_EVENT_UP, 1);
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK_EQ(draft(KST_F_DEAD_BAND), was);
    hold(2.1f);
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK(draft(KST_F_DEAD_BAND) != was);
    /* The repeat runs into the next page the same way. */
    ev(cx(minus), cy(minus), TOUCH_EVENT_DOWN, 1);
    if (kst_panel_editor() == 0) {
        ticks(30.0f);
    }
    ev(cx(minus), cy(minus), TOUCH_EVENT_UP, 1);
    show();
    CHECK(kst_panel_editor() == 3 || kst_prog_changes(M) == 1);
}

TEST_CASE(neutral_steps_through_its_page)
{
    const uint16_t was = (fresh(), connected(), draft(KST_F_NEUTRAL));
    const gfx_rect_t plus = kst_panel_step_rect(PG_PULSE, 0, true);

    tap(kst_panel_tab_rect(PG_PULSE));
    ev(cx(plus), cy(plus), TOUCH_EVENT_DOWN, 1);
    ev(cx(plus), cy(plus), TOUCH_EVENT_UP, 1);
    if (kst_panel_editor() == 3) {
        CHECK(warns(TR(KP_W_NEUTRAL_MOVED)));
        hold(2.1f);
    }
    CHECK(draft(KST_F_NEUTRAL) != was);
    CHECK_EQ(kst_prog_changes(M), 1);
}

int main(void)
{
    RUN(the_tabs_show_their_fields);
    RUN(a_locked_value_takes_no_tap);
    RUN(the_keypads_range_is_the_cores_and_a_value_inside_it_is_an_edit);
    RUN(a_value_beyond_a_hard_limit_cannot_be_entered);
    RUN(a_value_past_a_soft_limit_waits_for_a_2_s_hold);
    RUN(the_hold_button_fills_through_its_own_revision);
    RUN(stretch_is_chosen_from_the_values_the_core_takes);
    RUN(a_switch_is_toggled_by_its_tap_and_warns_where_the_core_does);
    RUN(a_switch_the_core_refuses_says_which_limit);
    RUN(a_step_key_acts_on_the_press_and_repeats_while_held);
    RUN(the_repeat_speeds_up_after_1_8_s_more);
    RUN(a_step_past_a_soft_limit_opens_the_page_and_ends_the_repeat);
    RUN(neutral_steps_through_its_page);
    return test_summary("kst_panel_edit");
}
