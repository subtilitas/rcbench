/*
 * The Dial's screen button (shared/dial/dial_button.c): PRESS and HOLD for
 * the panel, the switch-off's warning at 5,000ms and its choice OFF /
 * KEEP ON, and the forget at 10,000ms.
 *
 * The failures this is about: a Dial that switches off on a hold meant for
 * the panel; a choice that opens on a hold short of 5,000ms; a gesture or a
 * turn that reaches the panel while the warning or the choice is up; a
 * choice that never closes; and a forget that leaves a choice behind.  The
 * input task steps the button every 5ms; so do these cases.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include "dial_button.h"
#include "dialknob.h"

/** @brief The input task's period, ms. */
#define STEP 5u

/** @brief What a run of steps asked for. */
typedef struct {
    unsigned any;   /**< every DIAL_BTN_* bit seen */
    int      press; /**< DIAL_BTN_PRESS steps */
    int      hold;  /**< DIAL_BTN_HOLD steps */
    int      warn;  /**< DIAL_BTN_WARN steps */
} seen_t;

/** @brief The clock of the case. */
static uint32_t s_now;

/** @brief Step the button every STEP ms for @p ms with the level @p down.
 *  @param b the state @param down the level @param ms how long
 *  @param out the actions, added to */
static void run(dial_button_t *b, bool down, uint32_t ms, seen_t *out)
{
    for (uint32_t t = 0; t < ms; t += STEP) {
        const unsigned a = dial_button_step(b, down, 0, s_now);
        out->any |= a;
        out->press += (a & DIAL_BTN_PRESS) != 0u;
        out->hold += (a & DIAL_BTN_HOLD) != 0u;
        out->warn += (a & DIAL_BTN_WARN) != 0u;
        s_now += STEP;
    }
}

/** @brief One step at the current time. @param b the state
 *  @param down the level @param turns detents @return DIAL_BTN_* */
static unsigned step(dial_button_t *b, bool down, int32_t turns)
{
    const unsigned a = dial_button_step(b, down, turns, s_now);
    s_now += STEP;
    return a;
}

/** @brief A fresh button, the clock near its wrap. @param b the state */
static void start(dial_button_t *b)
{
    dial_button_init(b);
    s_now = 0xFFFFF000u;
}

/** @brief Hold for @p ms and release: the choice opens when @p ms is
 *  5,000 to 9,999.  @param b the state @param ms the hold
 *  @param out the actions */
static void hold_release(dial_button_t *b, uint32_t ms, seen_t *out)
{
    run(b, true, ms, out);
    out->any |= step(b, false, 0);
}

TEST_CASE(a_release_before_1000ms_is_press)
{
    dial_button_t b;
    seen_t s = { 0 };
    start(&b);
    hold_release(&b, 995u, &s);
    CHECK_EQ(s.any, DIAL_BTN_PRESS);
    CHECK(!dial_button_local(&b));
}

TEST_CASE(a_hold_of_1000ms_is_hold_once_and_its_release_no_press)
{
    dial_button_t b;
    seen_t s = { 0 };
    start(&b);
    run(&b, true, 3000u, &s);
    CHECK_EQ(s.hold, 1);
    CHECK_EQ(s.press, 0);
    CHECK_EQ(step(&b, false, 0), 0u);
    CHECK(!dial_button_local(&b));
}

TEST_CASE(a_release_at_4900ms_does_nothing_but_the_hold)
{
    dial_button_t b;
    seen_t s = { 0 };
    dial_face_view_t v;
    start(&b);
    hold_release(&b, 4900u, &s);
    CHECK_EQ(s.any, DIAL_BTN_HOLD);
    CHECK_EQ(s.warn, 0);
    CHECK(!dial_button_local(&b));
    dial_button_view(&b, s_now, &v);
    CHECK_EQ(v.off, DIAL_OFF_NONE);
    run(&b, false, 20000u, &s);
    CHECK_EQ(s.any, DIAL_BTN_HOLD);
}

TEST_CASE(the_warning_starts_at_5000ms_with_one_beep_and_blinks)
{
    dial_button_t b;
    seen_t s = { 0 };
    dial_face_view_t v;
    start(&b);
    run(&b, true, 5000u, &s);
    CHECK_EQ(s.warn, 0);
    CHECK(!dial_button_local(&b));
    const uint32_t at = s_now;
    run(&b, true, STEP, &s);
    CHECK_EQ(s.warn, 1);
    CHECK(dial_button_local(&b));
    dial_button_view(&b, at, &v);
    CHECK_EQ(v.off, DIAL_OFF_WARN);
    CHECK(v.off_lit);
    dial_button_view(&b, at + DIAL_OFF_BLINK_MS - 1u, &v);
    CHECK(v.off_lit);
    dial_button_view(&b, at + DIAL_OFF_BLINK_MS, &v);
    CHECK(!v.off_lit);
    dial_button_view(&b, at + 2u * DIAL_OFF_BLINK_MS, &v);
    CHECK(v.off_lit);
    run(&b, true, 4000u, &s);
    CHECK_EQ(s.warn, 1);
    CHECK_EQ(s.hold, 1);
}

TEST_CASE(a_release_from_5000ms_to_9999ms_opens_the_choice_on_keep_on)
{
    static const uint32_t holds[] = { 5005u, 7000u, 9995u };
    for (size_t k = 0; k < sizeof holds / sizeof holds[0]; ++k) {
        dial_button_t b;
        seen_t s = { 0 };
        dial_face_view_t v;
        start(&b);
        hold_release(&b, holds[k], &s);
        CHECK_EQ(s.any, DIAL_BTN_HOLD | DIAL_BTN_WARN);
        CHECK(dial_button_local(&b));
        dial_button_view(&b, s_now, &v);
        CHECK_EQ(v.off, DIAL_OFF_ASK);
        CHECK(!v.off_focus);
        CHECK(!v.off_lit);
        CHECK_EQ(v.off_secs, 10u);
    }
}

TEST_CASE(a_turn_moves_the_focus_clockwise_to_off_and_back)
{
    dial_button_t b;
    seen_t s = { 0 };
    dial_face_view_t v;
    start(&b);
    hold_release(&b, 6000u, &s);
    CHECK_EQ(step(&b, false, 1), 0u);
    dial_button_view(&b, s_now, &v);
    CHECK(v.off_focus);
    CHECK_EQ(step(&b, false, 3), 0u);
    dial_button_view(&b, s_now, &v);
    CHECK(v.off_focus);
    CHECK_EQ(step(&b, false, -1), 0u);
    dial_button_view(&b, s_now, &v);
    CHECK(!v.off_focus);
    CHECK(dial_button_local(&b));
}

TEST_CASE(a_press_on_off_switches_off)
{
    dial_button_t b;
    seen_t s = { 0 };
    start(&b);
    hold_release(&b, 6000u, &s);
    CHECK_EQ(step(&b, false, 1), 0u);
    s.any = 0u;
    hold_release(&b, 200u, &s);
    CHECK_EQ(s.any, DIAL_BTN_OFF);
    CHECK(!dial_button_local(&b));
}

TEST_CASE(a_press_on_keep_on_keeps_the_dial_on)
{
    dial_button_t b;
    seen_t s = { 0 };
    start(&b);
    hold_release(&b, 6000u, &s);
    s.any = 0u;
    hold_release(&b, 200u, &s);
    CHECK_EQ(s.any, DIAL_BTN_KEEP);
    CHECK(!dial_button_local(&b));
}

TEST_CASE(a_hold_in_the_choice_keeps_on_at_1000ms_and_sends_nothing)
{
    dial_button_t b;
    seen_t s = { 0 };
    start(&b);
    hold_release(&b, 6000u, &s);
    CHECK_EQ(step(&b, false, 1), 0u);       /* the focus on OFF */
    s = (seen_t){ 0 };
    run(&b, true, 1000u, &s);               /* down 0 to 995ms */
    CHECK_EQ(s.any, 0u);
    CHECK(dial_button_local(&b));
    run(&b, true, STEP, &s);
    CHECK_EQ(s.any, DIAL_BTN_KEEP);
    CHECK(!dial_button_local(&b));
    /* Held on past 5,000ms and 10,000ms, the press stays the choice's:
     * no HOLD, no warning, no forget, and its release is no PRESS. */
    run(&b, true, 12000u, &s);
    CHECK_EQ(step(&b, false, 0), 0u);
    CHECK_EQ(s.any, DIAL_BTN_KEEP);
    /* The next press is the panel's again. */
    s = (seen_t){ 0 };
    hold_release(&b, 100u, &s);
    CHECK_EQ(s.any, DIAL_BTN_PRESS);
}

TEST_CASE(ten_seconds_without_a_turn_or_a_press_keep_on)
{
    dial_button_t b;
    seen_t s = { 0 };
    dial_face_view_t v;
    start(&b);
    hold_release(&b, 6000u, &s);
    s.any = 0u;
    run(&b, false, 4000u, &s);
    CHECK_EQ(step(&b, false, 1), 0u);       /* a turn starts the 10s again */
    run(&b, false, 9995u, &s);              /* up to 9,995ms after it */
    CHECK_EQ(s.any, 0u);
    dial_button_view(&b, s_now - 1u, &v);
    CHECK_EQ(v.off, DIAL_OFF_ASK);
    CHECK_EQ(v.off_secs, 1u);
    run(&b, false, STEP, &s);
    CHECK_EQ(s.any, DIAL_BTN_KEEP);
    CHECK(!dial_button_local(&b));
    dial_button_view(&b, s_now, &v);
    CHECK_EQ(v.off, DIAL_OFF_NONE);
}

TEST_CASE(a_hold_to_10000ms_forgets_and_opens_no_choice)
{
    dial_button_t b;
    seen_t s = { 0 };
    dial_face_view_t v;
    start(&b);
    run(&b, true, 10000u, &s);
    CHECK_EQ(s.any & DIAL_BTN_FORGET, 0u);
    run(&b, true, STEP, &s);
    CHECK_EQ(s.any, DIAL_BTN_HOLD | DIAL_BTN_WARN | DIAL_BTN_FORGET);
    CHECK(!dial_button_local(&b));
    dial_button_view(&b, s_now, &v);
    CHECK_EQ(v.off, DIAL_OFF_NONE);
    run(&b, true, 5000u, &s);
    CHECK_EQ(step(&b, false, 0), 0u);
    CHECK_EQ(s.warn, 1);
    CHECK(!dial_button_local(&b));
    run(&b, false, 20000u, &s);
    CHECK_EQ(s.any, DIAL_BTN_HOLD | DIAL_BTN_WARN | DIAL_BTN_FORGET);
}

TEST_CASE(no_panel_gesture_comes_out_during_the_warning_or_the_choice)
{
    dial_button_t b;
    seen_t s = { 0 };
    start(&b);
    run(&b, true, 1500u, &s);
    CHECK_EQ(s.hold, 1);
    s = (seen_t){ 0 };
    run(&b, true, 6000u, &s);               /* the warning */
    CHECK_EQ(step(&b, false, 0), 0u);       /* the choice */
    /* Presses and holds in the choice, turns between them. */
    for (int k = 0; k < 3; ++k) {
        CHECK_EQ(step(&b, false, 1) & (DIAL_BTN_PRESS | DIAL_BTN_HOLD), 0u);
        CHECK_EQ(step(&b, false, -1) & (DIAL_BTN_PRESS | DIAL_BTN_HOLD), 0u);
    }
    run(&b, true, 400u, &s);
    run(&b, false, 100u, &s);
    CHECK_EQ(s.press, 0);
    CHECK_EQ(s.hold, 0);
    CHECK_EQ(s.any, DIAL_BTN_WARN | DIAL_BTN_KEEP);
    /* The same with a hold in the choice. */
    start(&b);
    s = (seen_t){ 0 };
    hold_release(&b, 5500u, &s);
    s = (seen_t){ 0 };
    run(&b, true, 3000u, &s);
    run(&b, false, 100u, &s);
    CHECK_EQ(s.press, 0);
    CHECK_EQ(s.hold, 0);
    CHECK_EQ(s.any, DIAL_BTN_KEEP);
}

TEST_CASE(a_choice_after_off_on_usb_starts_again_from_the_faces)
{
    dial_button_t b;
    seen_t s = { 0 };
    dial_face_view_t v;
    start(&b);
    hold_release(&b, 6000u, &s);
    CHECK_EQ(step(&b, false, 2), 0u);
    s.any = 0u;
    hold_release(&b, 50u, &s);
    CHECK_EQ(s.any, DIAL_BTN_OFF);
    /* On USB the Dial is still on: the next hold starts over. */
    dial_button_view(&b, s_now, &v);
    CHECK_EQ(v.off, DIAL_OFF_NONE);
    s = (seen_t){ 0 };
    hold_release(&b, 5500u, &s);
    CHECK_EQ(s.any, DIAL_BTN_HOLD | DIAL_BTN_WARN);
    dial_button_view(&b, s_now, &v);
    CHECK_EQ(v.off, DIAL_OFF_ASK);
    CHECK(!v.off_focus);
}

int main(void)
{
    RUN(a_release_before_1000ms_is_press);
    RUN(a_hold_of_1000ms_is_hold_once_and_its_release_no_press);
    RUN(a_release_at_4900ms_does_nothing_but_the_hold);
    RUN(the_warning_starts_at_5000ms_with_one_beep_and_blinks);
    RUN(a_release_from_5000ms_to_9999ms_opens_the_choice_on_keep_on);
    RUN(a_turn_moves_the_focus_clockwise_to_off_and_back);
    RUN(a_press_on_off_switches_off);
    RUN(a_press_on_keep_on_keeps_the_dial_on);
    RUN(a_hold_in_the_choice_keeps_on_at_1000ms_and_sends_nothing);
    RUN(ten_seconds_without_a_turn_or_a_press_keep_on);
    RUN(a_hold_to_10000ms_forgets_and_opens_no_choice);
    RUN(no_panel_gesture_comes_out_during_the_warning_or_the_choice);
    RUN(a_choice_after_off_on_usb_starts_again_from_the_faces);
    return test_summary("dial_button");
}
