/**
 * @file test_keypad_sign.c
 * @brief The numeric keypad's +/- key and a caller's refusal: a signed
 *        keypad's keys, -200 taken, -201 refused, -0 read as 0, the sign on
 *        an empty entry, and a value refused by the caller's own rule.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <string.h>

#include "greatest.h"

#include "ui_keypad.h"

static const gfx_rect_t k_area = { 6, 24, 546, 402 };

static ui_keypad_t kp;

/** @brief SUPPLY's voltage keypad: 3.3 to 21.0 V, unsigned. */
static void open_v(void)
{
    ui_keypad_open(&kp, k_area, "VOLTAGE", "V", 6.0f, 3.3f, 21.0f, 2);
}

/**
 * @brief Key @p key pressed and released over its middle.
 * @param key The key.
 * @param out Where OK writes the value, or NULL.
 * @return What the release came to.
 */
static ui_keypad_result_t press(ui_key_t key, float *out)
{
    const gfx_rect_t r = ui_keypad_key_rect(&kp, key);
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)(r.x + r.w / 2),
                                   .y = (int16_t)(r.y + r.h / 2),
                                   .strength = 40 } };
    (void)ui_keypad_event(&kp, &e, out);
    e.type = TOUCH_EVENT_UP;
    return ui_keypad_event(&kp, &e, out);
}

/**
 * @brief @p s typed key by key.
 * @param s Digits, '.' the point and '-' the +/- key.
 */
static void type(const char *s)
{
    static const ui_key_t k_digit[10] = {
        UI_KEY_0, UI_KEY_1, UI_KEY_2, UI_KEY_3, UI_KEY_4,
        UI_KEY_5, UI_KEY_6, UI_KEY_7, UI_KEY_8, UI_KEY_9,
    };
    for (const char *p = s; *p != '\0'; ++p) {
        (void)press((*p == '-')   ? UI_KEY_SIGN
                    : (*p == '.') ? UI_KEY_DOT : k_digit[*p - '0'], NULL);
    }
}

/* A value inside the range that the caller's own rule refuses: the
 * keypad opens again as a range refusal leaves it, the entry kept. */
TEST_CASE(a_caller_refusal_reopens_the_keypad_marked)
{
    open_v();
    type("12.5");
    float v = 0.0f;
    CHECK_EQ(press(UI_KEY_OK, &v), UI_KEYPAD_OK);
    CHECK(!kp.open);
    const uint32_t rev = kp.revision;
    ui_keypad_refuse(&kp);
    CHECK(kp.open);
    CHECK(kp.refused);
    CHECK(kp.pressed < 0);
    CHECK(kp.revision != rev);
    CHECK_STR_EQ(kp.entry, "12.5");
    /* The next key clears the mark; OK hands the new value back. */
    type("1");
    CHECK(!kp.refused);
    CHECK_EQ(press(UI_KEY_OK, &v), UI_KEYPAD_OK);
    CHECK_NEAR(v, 12.51f, 1e-4f);
    ui_keypad_refuse(NULL);
}

/** @brief SERVO's TRIM: -200 to +200 us, whole microseconds, signed. */
static void open_trim(void)
{
    ui_keypad_open_signed(&kp, k_area, "TRIM", "us", 0.0f, -200.0f, 200.0f,
                          0);
}

/* The +/- key sits beside the point, OK takes the last column, and every
 * other key is where an unsigned keypad has it. */
TEST_CASE(a_signed_keypad_has_its_sign_key_beside_the_point)
{
    open_v();
    gfx_rect_t plain[UI_KEY_SIGN];
    for (int k = 0; k < UI_KEY_SIGN; ++k) {
        plain[k] = ui_keypad_key_rect(&kp, (ui_key_t)k);
    }
    CHECK(!kp.sign);
    open_trim();
    CHECK(kp.sign);
    const gfx_rect_t sign = ui_keypad_key_rect(&kp, UI_KEY_SIGN);
    const gfx_rect_t ok   = ui_keypad_key_rect(&kp, UI_KEY_OK);
    const gfx_rect_t dot  = ui_keypad_key_rect(&kp, UI_KEY_DOT);
    CHECK(sign.w == dot.w && sign.y == dot.y && sign.x > dot.x);
    CHECK(ok.w == dot.w && ok.x > sign.x);
    CHECK(ok.x == plain[UI_KEY_OK].x + plain[UI_KEY_OK].w - ok.w);
    for (int k = 0; k < UI_KEY_SIGN; ++k) {
        if (k != UI_KEY_OK) {
            const gfx_rect_t r = ui_keypad_key_rect(&kp, (ui_key_t)k);
            CHECK(r.x == plain[k].x && r.y == plain[k].y
                  && r.w == plain[k].w && r.h == plain[k].h);
        }
    }
    CHECK(sign.x + sign.w <= k_area.x + k_area.w && sign.w >= 60);
}

TEST_CASE(the_sign_key_takes_the_range_down_to_its_minimum)
{
    float v = 1.0f;
    open_trim();
    type("-200");                            /* the sign first */
    CHECK_STR_EQ(kp.entry, "-200");
    CHECK_EQ(press(UI_KEY_OK, &v), UI_KEYPAD_OK);
    CHECK_NEAR(v, -200.0f, 1e-6f);
    open_trim();
    type("200-");                            /* and last */
    CHECK_STR_EQ(kp.entry, "-200");
    CHECK_EQ(press(UI_KEY_OK, &v), UI_KEYPAD_OK);
    CHECK_NEAR(v, -200.0f, 1e-6f);
    /* -201 is outside: refused, the keypad open. */
    open_trim();
    type("-201");
    v = 1.0f;
    CHECK_EQ(press(UI_KEY_OK, &v), UI_KEYPAD_NONE);
    CHECK(kp.refused);
    CHECK(kp.open);
    CHECK_NEAR(v, 1.0f, 1e-6f);
    /* The key again takes the sign away: +201 is outside too, +200 not. */
    type("-");
    CHECK_STR_EQ(kp.entry, "201");
    CHECK_EQ(press(UI_KEY_OK, &v), UI_KEYPAD_NONE);
    (void)press(UI_KEY_DEL, NULL);
    type("0");
    CHECK_EQ(press(UI_KEY_OK, &v), UI_KEYPAD_OK);
    CHECK_NEAR(v, 200.0f, 1e-6f);
}

TEST_CASE(minus_zero_is_zero_and_a_lone_sign_is_nothing_typed)
{
    float v = 7.0f;
    open_trim();
    type("-0");
    CHECK_EQ(press(UI_KEY_OK, &v), UI_KEYPAD_OK);
    CHECK(v == 0.0f && !signbit(v));
    /* The sign on an empty entry: "-", which OK takes as nothing typed. */
    open_trim();
    type("-");
    CHECK_STR_EQ(kp.entry, "-");
    v = 7.0f;
    CHECK_EQ(press(UI_KEY_OK, &v), UI_KEYPAD_CANCEL);
    CHECK_NEAR(v, 7.0f, 1e-6f);
    /* The point after a lone sign puts a 0 before it; DEL takes the sign
     * back off; a full entry takes no sign. */
    ui_keypad_open_signed(&kp, k_area, "X", "", 0.0f, -9.0f, 9.0f, 2);
    type("-.5");
    CHECK_STR_EQ(kp.entry, "-0.5");
    CHECK_EQ(press(UI_KEY_OK, &v), UI_KEYPAD_OK);
    CHECK_NEAR(v, -0.5f, 1e-6f);
    ui_keypad_open_signed(&kp, k_area, "X", "", 0.0f, -9.0f, 9.0f, 2);
    type("-");
    (void)press(UI_KEY_DEL, NULL);
    CHECK_STR_EQ(kp.entry, "");
    open_trim();
    type("1234567");
    CHECK_EQ(kp.len, UI_KEYPAD_ENTRY_MAX);
    type("-");
    CHECK_STR_EQ(kp.entry, "1234567");
    /* Typed on an unsigned keypad, the key is not there to press. */
    open_v();
    type("-5");
    CHECK_STR_EQ(kp.entry, "5");
}

int main(void)
{
    RUN(a_caller_refusal_reopens_the_keypad_marked);
    RUN(a_signed_keypad_has_its_sign_key_beside_the_point);
    RUN(the_sign_key_takes_the_range_down_to_its_minimum);
    RUN(minus_zero_is_zero_and_a_lone_sign_is_nothing_typed);
    return test_summary("keypad_sign");
}
