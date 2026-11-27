/*
 * The Guition JC3636K518C's input (shared/dial/dk518_input.c): the
 * encoder's two pulse lines turned into detents, and the still touch that
 * is its screen button.
 *
 * The failures this is about: a pulse's bounce that counts two detents; a
 * detent lost or turned the wrong way; a count at start that turns the
 * counter; a swipe that sends PRESS; a touch short of 1,000ms that sends
 * HOLD; a hold that never reaches the switch-off or the forget; a TAP that
 * lands at the wrong point; and an update's accept taken from a swipe,
 * run through bleota's own confirmation and the `ota` task's 10ms polls
 * with their 2-poll debounce (firmware/devota/devota.c).
 * The input task steps both every 5ms; so do these cases.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "bleota.h"
#include "dk518_input.h"
#include "dialknob.h"

/** @brief The input task's period, ms. */
#define STEP 5u

/** @brief What a run of steps gave. */
typedef struct {
    unsigned any;   /**< every DIAL_BTN_* bit seen */
    int      press; /**< DIAL_BTN_PRESS steps */
    int      hold;  /**< DIAL_BTN_HOLD steps */
    int      warn;  /**< DIAL_BTN_WARN steps */
    int      forget; /**< DIAL_BTN_FORGET steps */
    int      taps;  /**< TAPs */
    int      lifts; /**< still lifts */
    uint8_t  tap_x; /**< the last TAP's column */
    uint8_t  tap_y; /**< the last TAP's row */
} seen_t;

/** @brief The clock of the case, started near its wrap. */
static uint32_t s_now;

/** @brief Step the still touch every STEP ms for @p ms.
 *  @param s the state @param touch a finger is down @param x column
 *  @param y row @param yield an update has the screen @param ms how long
 *  @param out added to */
static void run(dk518_still_t *s, bool touch, uint16_t x, uint16_t y,
                bool yield, uint32_t ms, seen_t *out)
{
    for (uint32_t t = 0; t < ms; t += STEP) {
        dk518_still_out_t o;
        const unsigned a = dk518_still_step(s, touch, x, y, 0, yield, s_now,
                                            &o);
        out->any |= a;
        out->press += (a & DIAL_BTN_PRESS) != 0u;
        out->hold += (a & DIAL_BTN_HOLD) != 0u;
        out->warn += (a & DIAL_BTN_WARN) != 0u;
        out->forget += (a & DIAL_BTN_FORGET) != 0u;
        out->lifts += o.still_lift;
        if (o.tap) {
            ++out->taps;
            out->tap_x = o.tap_x;
            out->tap_y = o.tap_y;
        }
        s_now += STEP;
    }
}

/** @brief A fresh still touch, the clock near its wrap.
 *  @param s the state @param out cleared */
static void start(dk518_still_t *s, seen_t *out)
{
    dk518_still_init(s);
    memset(out, 0, sizeof *out);
    s_now = 0xFFFFF000u;
}

TEST_CASE(the_first_step_takes_the_totals_and_turns_nothing)
{
    dk518_enc_t e;
    dk518_enc_init(&e);
    CHECK_EQ(0, dk518_enc_step(&e, 517, -3));
    CHECK_EQ(0, dk518_enc_step(&e, 517, -3));
}

TEST_CASE(one_pulse_is_one_detent_each_way)
{
    dk518_enc_t e;
    dk518_enc_init(&e);
    (void)dk518_enc_step(&e, 0, 0);
    CHECK_EQ(1, dk518_enc_step(&e, 1, 0));
    CHECK_EQ(0, dk518_enc_step(&e, 1, 0));
    CHECK_EQ(-1, dk518_enc_step(&e, 1, 1));
    CHECK_EQ(0, dk518_enc_step(&e, 1, 1));
}

TEST_CASE(a_pulse_and_its_bounce_count_once)
{
    /* Four edges in one step, then two more in the next: one pulse. */
    dk518_enc_t e;
    dk518_enc_init(&e);
    (void)dk518_enc_step(&e, 10, 20);
    CHECK_EQ(1, dk518_enc_step(&e, 14, 20));
    CHECK_EQ(0, dk518_enc_step(&e, 16, 20));
    CHECK_EQ(0, dk518_enc_step(&e, 16, 20));
    CHECK_EQ(-1, dk518_enc_step(&e, 16, 23));
    CHECK_EQ(0, dk518_enc_step(&e, 16, 24));
}

TEST_CASE(pulses_10ms_apart_all_count)
{
    /* 20 clockwise detents 10ms apart: every one counts. */
    dk518_enc_t e;
    int32_t sum = 0;
    int32_t cw = 0;
    dk518_enc_init(&e);
    (void)dk518_enc_step(&e, 0, 0);
    for (int i = 0; i < 20; ++i) {
        cw += 2;
        sum += dk518_enc_step(&e, cw, 0);
        sum += dk518_enc_step(&e, cw, 0);
    }
    CHECK_EQ(20, sum);
}

TEST_CASE(both_lines_in_one_step_cancel)
{
    dk518_enc_t e;
    dk518_enc_init(&e);
    (void)dk518_enc_step(&e, 0, 0);
    CHECK_EQ(0, dk518_enc_step(&e, 1, 1));
}

TEST_CASE(a_total_that_wraps_still_counts)
{
    dk518_enc_t e;
    dk518_enc_init(&e);
    (void)dk518_enc_step(&e, INT32_MAX, 0);
    CHECK_EQ(1, dk518_enc_step(&e, INT32_MIN, 0));
}

TEST_CASE(a_short_still_touch_is_press_and_tap_at_its_start)
{
    dk518_still_t s;
    seen_t v;
    start(&s, &v);
    run(&s, true, 300, 90, false, 200, &v);
    run(&s, false, 0, 0, false, 20, &v);
    CHECK_EQ(1, v.press);
    CHECK_EQ(0, v.hold);
    CHECK_EQ(1, v.taps);
    CHECK_EQ(1, v.lifts);
    CHECK_EQ(200, v.tap_x);
    CHECK_EQ(60, v.tap_y);
}

TEST_CASE(a_still_touch_past_500ms_is_press_alone)
{
    dk518_still_t s;
    seen_t v;
    start(&s, &v);
    run(&s, true, 180, 180, false, 800, &v);
    run(&s, false, 0, 0, false, 20, &v);
    CHECK_EQ(1, v.press);
    CHECK_EQ(0, v.taps);
    CHECK_EQ(1, v.lifts);
}

TEST_CASE(a_drift_within_20px_stays_a_press)
{
    dk518_still_t s;
    seen_t v;
    start(&s, &v);
    run(&s, true, 100, 100, false, 100, &v);
    run(&s, true, 120, 80, false, 100, &v);
    run(&s, false, 0, 0, false, 20, &v);
    CHECK_EQ(1, v.press);
    CHECK_EQ(1, v.taps);
    CHECK_EQ(100 * 2 / 3, v.tap_x);
}

TEST_CASE(a_swipe_sends_nothing_and_the_next_touch_presses)
{
    dk518_still_t s;
    seen_t v;
    start(&s, &v);
    run(&s, true, 100, 100, false, 100, &v);
    run(&s, true, 121, 100, false, 1500, &v);
    run(&s, false, 0, 0, false, 20, &v);
    CHECK_EQ(0u, v.any);
    CHECK_EQ(0, v.taps);
    CHECK_EQ(0, v.lifts);
    /* The next touch is a press again. */
    run(&s, true, 50, 50, false, 100, &v);
    run(&s, false, 0, 0, false, 20, &v);
    CHECK_EQ(1, v.press);
}

TEST_CASE(hold_comes_at_1000ms_and_a_move_after_it_keeps_it)
{
    dk518_still_t s;
    seen_t v;
    start(&s, &v);
    run(&s, true, 180, 180, false, DK_HOLD_MS - STEP, &v);
    CHECK_EQ(0, v.hold);
    run(&s, true, 180, 180, false, 2 * STEP, &v);
    CHECK_EQ(1, v.hold);
    run(&s, true, 250, 250, false, 500, &v);
    run(&s, false, 0, 0, false, 20, &v);
    CHECK_EQ(1, v.hold);
    CHECK_EQ(0, v.press);
    CHECK_EQ(0, v.lifts);
}

TEST_CASE(the_5s_hold_warns_and_the_10s_hold_forgets)
{
    dk518_still_t s;
    seen_t v;
    start(&s, &v);
    run(&s, true, 180, 180, false, DIAL_OFF_MS + STEP, &v);
    CHECK_EQ(1, v.warn);
    CHECK(dial_button_local(&s.button));
    run(&s, true, 180, 180, false, DK_FORGET_MS - DIAL_OFF_MS, &v);
    CHECK_EQ(1, v.forget);
    CHECK(!(dial_button_local(&s.button)));
    run(&s, false, 0, 0, false, 20, &v);
    CHECK_EQ(0, v.press);
}

TEST_CASE(a_lift_after_the_warning_opens_the_choice)
{
    dk518_still_t s;
    seen_t v;
    start(&s, &v);
    run(&s, true, 180, 180, false, DIAL_OFF_MS + 100, &v);
    run(&s, false, 0, 0, false, 20, &v);
    CHECK_EQ(DIAL_OFF_ASK, s.button.stage);
    /* A tap in the choice takes KEEP ON, its focus. */
    run(&s, true, 90, 90, false, 100, &v);
    run(&s, false, 0, 0, false, 20, &v);
    CHECK((v.any & DIAL_BTN_KEEP) != 0u);
    CHECK_EQ(0, v.press);
}

TEST_CASE(under_an_update_offer_only_the_still_lift_counts)
{
    dk518_still_t s;
    seen_t v;
    start(&s, &v);
    run(&s, true, 180, 180, true, 200, &v);
    run(&s, false, 0, 0, true, 20, &v);
    CHECK_EQ(0u, v.any);
    CHECK_EQ(1, v.lifts);
    /* A swipe under the offer accepts nothing. */
    run(&s, true, 180, 180, true, 100, &v);
    run(&s, true, 180, 260, true, 100, &v);
    run(&s, false, 0, 0, true, 20, &v);
    CHECK_EQ(1, v.lifts);
    /* A touch from the offer stays nobody's after it. */
    run(&s, true, 180, 180, true, 100, &v);
    run(&s, true, 180, 180, false, 100, &v);
    run(&s, false, 0, 0, false, 20, &v);
    CHECK_EQ(0, v.press);
}

/** @brief An update waiting for its confirmation, as bleota holds it. */
static void offer(bleota_dev_t *d)
{
    memset(d, 0, sizeof *d);
    d->cfg.confirm = BLEOTA_CONFIRM_PRESS;
    d->st = BLEOTA_DEV_CONFIRM;
    d->key_armed = true;
}

/**
 * @brief A touch under the offer: still at (x, y) for @p still_ms, then
 *        at (x2, y2) until @p ms, then lifted; the key goes to bleota as
 *        the `ota` task takes it, for 3,000ms in all.
 * @param d the offer
 * @param x2 the column it moves to
 * @param y2 the row it moves to
 * @param still_ms how long it stays at the start
 * @param ms how long the finger is down
 */
static void touch_offer(bleota_dev_t *d, uint16_t x2, uint16_t y2,
                        uint32_t still_ms, uint32_t ms)
{
    dk518_still_t s;
    bool raw = false;
    bool key = false;
    dk518_still_init(&s);
    for (uint32_t t = 0; t < 3000u; t += STEP) {
        const bool down = t < ms;
        const bool at = t < still_ms;
        (void)dk518_still_step(&s, down, at ? 180 : x2, at ? 180 : y2, 0,
                               true, s_now + t, NULL);
        if (t % 10u == 0u) {
            const bool now_raw = dk518_still_key(&s, s_now + t);
            if (now_raw == raw) {
                key = raw;
            }
            raw = now_raw;
            bleota_dev_key(d, key, s_now + t);
        }
    }
}

TEST_CASE(a_still_tap_accepts_the_update)
{
    bleota_dev_t d;
    s_now = 0xFFFFF000u;
    offer(&d);
    touch_offer(&d, 180, 180, 200, 200);
    CHECK_EQ(d.st, BLEOTA_DEV_OPEN);
}

TEST_CASE(a_swipe_neither_accepts_nor_refuses_the_update)
{
    bleota_dev_t d;
    s_now = 0xFFFFF000u;
    offer(&d);
    touch_offer(&d, 260, 180, 100, 300);
    CHECK_EQ(d.st, BLEOTA_DEV_CONFIRM);
    offer(&d);
    touch_offer(&d, 180, 40, 600, 1500);  /* moved before 1,000ms */
    CHECK_EQ(d.st, BLEOTA_DEV_CONFIRM);
}

TEST_CASE(a_still_hold_refuses_the_update_whenever_it_lifts)
{
    bleota_dev_t d;
    s_now = 0xFFFFF000u;
    offer(&d);
    touch_offer(&d, 180, 180, 1200, 1200);  /* lifted before 2,000ms */
    CHECK_EQ(d.st, BLEOTA_DEV_IDLE);
    offer(&d);
    touch_offer(&d, 250, 250, 1100, 2500);  /* moved after 1,000ms */
    CHECK_EQ(d.st, BLEOTA_DEV_IDLE);
}

TEST_CASE(native_points_scale_to_0_to_239)
{
    CHECK_EQ(0, dk518_touch_scale(0));
    CHECK_EQ(120, dk518_touch_scale(180));
    CHECK_EQ(239, dk518_touch_scale(359));
    CHECK_EQ(239, dk518_touch_scale(4095));
    CHECK(dk518_touch_scale(359) <= DK_TOUCH_MAX);
}

int main(void)
{
    RUN(the_first_step_takes_the_totals_and_turns_nothing);
    RUN(one_pulse_is_one_detent_each_way);
    RUN(a_pulse_and_its_bounce_count_once);
    RUN(pulses_10ms_apart_all_count);
    RUN(both_lines_in_one_step_cancel);
    RUN(a_total_that_wraps_still_counts);
    RUN(a_short_still_touch_is_press_and_tap_at_its_start);
    RUN(a_still_touch_past_500ms_is_press_alone);
    RUN(a_drift_within_20px_stays_a_press);
    RUN(a_swipe_sends_nothing_and_the_next_touch_presses);
    RUN(hold_comes_at_1000ms_and_a_move_after_it_keeps_it);
    RUN(the_5s_hold_warns_and_the_10s_hold_forgets);
    RUN(a_lift_after_the_warning_opens_the_choice);
    RUN(under_an_update_offer_only_the_still_lift_counts);
    RUN(a_still_tap_accepts_the_update);
    RUN(a_swipe_neither_accepts_nor_refuses_the_update);
    RUN(a_still_hold_refuses_the_update_whenever_it_lifts);
    RUN(native_points_scale_to_0_to_239);
    return test_summary("dk518_input");
}
