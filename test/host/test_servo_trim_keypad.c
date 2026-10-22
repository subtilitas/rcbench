/**
 * @file test_servo_trim_keypad.c
 * @brief SERVO > SETTINGS > OUTPUT > TRIM's keypad: a tap on TRIM's value
 *        opens a signed keypad of -200 to +200 us; OK puts the value on the
 *        5 us steps of "-" and "+" and sends the pulse at once, as a step
 *        does, with the dial untouched; a value past
 *        the range is refused; CANCEL, the home tag and a STOP change
 *        nothing.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_rig.h"

/* The middle of TRIM's value, between its two steps. */
#define TRIM_X ((TRIM_DN_X + TRIM_UP_X) / 2)
#define TRIM_Y ROW_Y(3)

/**
 * @brief A key of the keypad SERVO has open.
 * @param k The key.
 */
static void kkey(ui_key_t k)
{
    const gfx_rect_t r = ui_keypad_key_rect(servo_screen_keypad(), k);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/**
 * @brief @p text typed, '-' on the +/- key, without OK.
 * @param text Digits, '.' and '-'.
 */
static void ktype(const char *text)
{
    static const ui_key_t digit[10] = {
        UI_KEY_0, UI_KEY_1, UI_KEY_2, UI_KEY_3, UI_KEY_4,
        UI_KEY_5, UI_KEY_6, UI_KEY_7, UI_KEY_8, UI_KEY_9,
    };
    for (const char *p = text; *p != '\0'; ++p) {
        kkey((*p == '-') ? UI_KEY_SIGN
             : (*p == '.') ? UI_KEY_DOT : digit[*p - '0']);
    }
}

/**
 * @brief The commanded pulse with the dial at the centre, on an armed
 *        bench: PULSE CENTRE plus TRIM.
 * @return The pulse, us.
 */
static int centre_us(void)
{
    int x, y;
    dial_at(0.0f, ARC_R - 20, &x, &y);
    close_settings();
    tap(x, y);
    const int us = (int)last_cmd().value_us;
    open_settings();
    return us;
}

TEST_CASE(a_tap_on_trim_opens_a_signed_keypad)
{
    fresh_armed();
    open_settings();
    tap(TRIM_X, TRIM_Y);
    const ui_keypad_t *k = servo_screen_keypad();
    CHECK(k->open);
    CHECK(k->sign);
    CHECK_STR_EQ(k->title, TR(SV_ROW_TRIM));
    CHECK_STR_EQ(k->unit, "us");
    CHECK_NEAR(k->min, -200.0f, 1e-6f);
    CHECK_NEAR(k->max, 200.0f, 1e-6f);
    CHECK_EQ(k->decimals, 0);
    CHECK(ui_keypad_key_rect(k, UI_KEY_SIGN).w > 0);
    /* Every other keypad on SERVO stays unsigned: TRAVEL's. */
    kkey(UI_KEY_CANCEL);
    tap(ROW_L_X, ROW_Y(4));
    CHECK(servo_screen_keypad()->open);
    CHECK(!servo_screen_keypad()->sign);
    kkey(UI_KEY_CANCEL);
}

/* -200 is taken and sends what 40 steps of "-" send; -201 is refused. */
TEST_CASE(minus_200_is_what_40_steps_send_and_minus_201_is_refused)
{
    fresh_armed();
    open_settings();
    const int zero = centre_us();
    tap(TRIM_X, TRIM_Y);
    ktype("-201");
    kkey(UI_KEY_OK);
    CHECK(servo_screen_keypad()->open);
    CHECK(servo_screen_keypad()->refused);
    kkey(UI_KEY_CANCEL);
    CHECK_EQ(centre_us(), zero);
    tap(TRIM_X, TRIM_Y);
    ktype("-200");
    kkey(UI_KEY_OK);
    CHECK(!servo_screen_keypad()->open);
    const int typed = centre_us();
    CHECK_EQ(typed, zero - 200);

    fresh_armed();
    open_settings();
    for (int i = 0; i < 40; ++i) {
        tap(TRIM_DN_X, TRIM_Y);
    }
    CHECK_EQ(centre_us(), typed);
    /* +200, and past it. */
    tap(TRIM_X, TRIM_Y);
    ktype("201");
    kkey(UI_KEY_OK);
    CHECK(servo_screen_keypad()->refused);
    kkey(UI_KEY_DEL);
    ktype("0");
    kkey(UI_KEY_OK);
    CHECK_EQ(centre_us(), zero + 200);
}

/* OK on TRIM sends the pulse at once, as a step does: with the dial left
 * at the centre, the last command is PULSE CENTRE plus the typed trim,
 * before and after one "-" step. */
TEST_CASE(ok_on_trim_sends_the_pulse_at_once)
{
    fresh_armed();
    open_settings();
    const int zero = centre_us();             /* the dial at the centre */
    tap(TRIM_X, TRIM_Y);
    ktype("-35");
    kkey(UI_KEY_OK);
    CHECK(!servo_screen_keypad()->open);
    CHECK_EQ((int)last_cmd().value_us, zero - 35);
    tap(TRIM_DN_X, TRIM_Y);                   /* one "-" step */
    CHECK_EQ((int)last_cmd().value_us, zero - 40);
    tap(TRIM_X, TRIM_Y);
    ktype("60");
    kkey(UI_KEY_OK);
    CHECK_EQ((int)last_cmd().value_us, zero + 60);
}

/* A typed trim lands on the 5 us steps, the nearer one; -0 is 0. */
TEST_CASE(a_typed_trim_lands_on_the_steps)
{
    fresh_armed();
    open_settings();
    const int zero = centre_us();
    static const struct { const char *typed; int us; } k_cases[] = {
        { "13", 15 }, { "12", 10 }, { "-12", -10 }, { "-13", -15 },
        { "-0", 0 }, { "7", 5 },
    };
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); ++i) {
        tap(TRIM_X, TRIM_Y);
        ktype(k_cases[i].typed);
        kkey(UI_KEY_OK);
        CHECK_EQ(centre_us() - zero, k_cases[i].us);
    }
    /* The sign alone is nothing typed. */
    tap(TRIM_X, TRIM_Y);
    ktype("-");
    kkey(UI_KEY_OK);
    CHECK(!servo_screen_keypad()->open);
    CHECK_EQ(centre_us() - zero, 5);
}

/* CANCEL, the home tag and a STOP leave the trim as it was; the steps
 * are not locked while ARMED, and neither is the keypad. */
TEST_CASE(cancel_home_and_stop_change_nothing)
{
    fresh_armed();
    open_settings();
    const int zero = centre_us();
    tap(TRIM_X, TRIM_Y);
    ktype("-50");
    kkey(UI_KEY_CANCEL);
    CHECK_EQ(centre_us(), zero);
    tap(TRIM_X, TRIM_Y);
    ktype("-50");
    CHECK(scr->back());
    CHECK(!servo_screen_keypad()->open);
    tap(TRIM_X, TRIM_Y);
    ktype("-50");
    servo_screen_cancel_arm();                 /* STOP */
    scr->tick(0.02f);
    CHECK(servo_screen_keypad()->open);
    CHECK_STR_EQ(servo_screen_keypad()->entry, "-50");
    kkey(UI_KEY_CANCEL);
    CHECK_EQ(servo_screen_commanded(), (uint16_t)zero);
}

int main(void)
{
    RUN(a_tap_on_trim_opens_a_signed_keypad);
    RUN(minus_200_is_what_40_steps_send_and_minus_201_is_refused);
    RUN(ok_on_trim_sends_the_pulse_at_once);
    RUN(a_typed_trim_lands_on_the_steps);
    RUN(cancel_home_and_stop_change_nothing);
    return test_summary("servo_trim_keypad");
}
