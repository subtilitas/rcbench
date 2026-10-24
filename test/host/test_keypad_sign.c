/**
 * @file test_keypad_sign.c
 * @brief The numeric keypad's +/- key and a caller's refusal: a signed
 *        keypad's keys, -200 taken, -201 refused, -0 read as 0, the sign on
 *        an empty entry, and a value refused by the caller's own rule,
 *        with the caller's reason drawn in place of the range until the
 *        next key.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "ui_keypad.h"
#include "ui_theme.h"

static const gfx_rect_t k_area = { 6, 24, 546, 402 };

static ui_keypad_t kp;

/** @brief SUPPLY's voltage keypad: 3.3 to 21.0V, unsigned. */
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

/**
 * @brief @p k drawn on a cleared panel-sized canvas.
 * @param k The keypad.
 * @param fb The canvas's pixels, UI_PANEL_W x UI_PANEL_H.
 * @return The pixels of the header line (the title and the range or the
 *         reason) in the warning colour.
 */
static int draw_header(const ui_keypad_t *k, gfx_color_t *fb)
{
    gfx_canvas_t cv;
    memset(fb, 0, (size_t)UI_PANEL_W * UI_PANEL_H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, UI_PANEL_W, UI_PANEL_H, UI_PANEL_W);
    ui_keypad_render(k, &cv);
    int warn = 0;
    for (int y = k_area.y + 6; y < k_area.y + 24; ++y) {
        for (int x = k_area.x; x < k_area.x + k_area.w; ++x) {
            warn += (fb[(size_t)y * UI_PANEL_W + (size_t)x]
                     == ui_theme_color(UI_C_WARN)) ? 1 : 0;
        }
    }
    return warn;
}

/* A caller's reason takes the range line's place, in the warning colour,
 * and the next key brings the range back; a range refusal names no
 * reason. */
TEST_CASE(a_callers_reason_takes_the_range_line_until_the_next_key)
{
    const size_t px = (size_t)UI_PANEL_W * UI_PANEL_H;
    const size_t band = (size_t)18 * UI_PANEL_W;
    const size_t row0 = (size_t)(k_area.y + 6) * UI_PANEL_W;
    gfx_color_t *fb = calloc(px, sizeof(gfx_color_t));
    gfx_color_t *range = calloc(px, sizeof(gfx_color_t));
    REQUIRE_ALLOC(fb);
    REQUIRE_ALLOC(range);

    open_v();
    type("12.5");
    float v = 0.0f;
    CHECK_EQ(press(UI_KEY_OK, &v), UI_KEYPAD_OK);
    ui_keypad_refuse(&kp);                          /* no reason: the range */
    CHECK_STR_EQ(kp.reason, "");
    CHECK(draw_header(&kp, range) > 0);
    ui_keypad_refuse_why(&kp, "0 or 5.0 to 1000.0 mOhm");
    CHECK(kp.open);
    CHECK(kp.refused);
    CHECK_STR_EQ(kp.reason, "0 or 5.0 to 1000.0 mOhm");
    CHECK_STR_EQ(kp.entry, "12.5");
    CHECK(draw_header(&kp, fb) > 0);                /* warning colour */
    CHECK(memcmp(fb + row0, range + row0, band * sizeof(gfx_color_t)) != 0);

    /* One key: the mark and the reason go, the range is back, dim. */
    (void)press(UI_KEY_DEL, NULL);
    CHECK(!kp.refused);
    CHECK_STR_EQ(kp.reason, "");
    CHECK_EQ(draw_header(&kp, fb), 0);

    /* A range refusal after a reason names the range. */
    ui_keypad_refuse_why(&kp, "WHY");
    (void)press(UI_KEY_CLR, NULL);
    type("99");
    CHECK_EQ(press(UI_KEY_OK, &v), UI_KEYPAD_NONE);
    CHECK(kp.refused);
    CHECK_STR_EQ(kp.reason, "");

    /* A NULL reason is a plain refusal; a long one is cut at 63 bytes. */
    ui_keypad_refuse_why(&kp, NULL);
    CHECK(kp.refused);
    CHECK_STR_EQ(kp.reason, "");
    char longer[100];
    memset(longer, 'x', sizeof(longer) - 1u);
    longer[sizeof(longer) - 1u] = '\0';
    ui_keypad_refuse_why(&kp, longer);
    CHECK_EQ(strlen(kp.reason), sizeof(kp.reason) - 1u);
    ui_keypad_refuse_why(NULL, "WHY");
    free(range);
    free(fb);
}

/* A reason past 63 bytes is cut at the start of a UTF-8 character: the
 * DE PD mini line behind 49 bytes puts its 2-byte "ä" on bytes 62 and 63,
 * so the cut falls before it, at 62.  One byte less of padding and the
 * "ä" ends on byte 62 and is kept whole. */
TEST_CASE(a_long_reason_is_cut_at_a_character_start)
{
    static const char k_de[] = "Diesen GPIO h\xc3\xa4lt der andere PD-mini-Pin";
    char why[128];
    memset(why, 'x', 49);
    snprintf(why + 49, sizeof(why) - 49u, "%s", k_de);
    open_v();
    ui_keypad_refuse_why(&kp, why);
    CHECK_EQ(strlen(kp.reason), 62u);
    CHECK(strncmp(kp.reason + 49, "Diesen GPIO h", 13) == 0);
    CHECK_EQ(kp.reason[61], 'h');

    memset(why, 'x', 48);
    snprintf(why + 48, sizeof(why) - 48u, "%s", k_de);
    ui_keypad_refuse_why(&kp, why);
    CHECK_EQ(strlen(kp.reason), 63u);
    CHECK_EQ((unsigned char)kp.reason[61], 0xc3u);
    CHECK_EQ((unsigned char)kp.reason[62], 0xa4u);

    /* A 3-byte character across the cut goes whole, too. */
    memset(why, 'x', 62);
    snprintf(why + 62, sizeof(why) - 62u, "\xe2\x80\x93" "end");
    ui_keypad_refuse_why(&kp, why);
    CHECK_EQ(strlen(kp.reason), 62u);
    /* Exactly 63 bytes fit as given. */
    memset(why, 'x', 63);
    why[63] = '\0';
    ui_keypad_refuse_why(&kp, why);
    CHECK_EQ(strlen(kp.reason), 63u);
}

/** @brief SERVO's TRIM: -200 to +200us, whole microseconds, signed. */
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
    RUN(a_callers_reason_takes_the_range_line_until_the_next_key);
    RUN(a_long_reason_is_cut_at_a_character_start);
    RUN(a_signed_keypad_has_its_sign_key_beside_the_point);
    RUN(the_sign_key_takes_the_range_down_to_its_minimum);
    RUN(minus_zero_is_zero_and_a_lone_sign_is_nothing_typed);
    return test_summary("keypad_sign");
}
