/**
 * @file test_rudder_layout.c
 * @brief RUDDER's card at the 1024 x 600 sizes: every control a finger
 *        tall, inside the card and the guide's live half, above the alert
 *        band and apart from the others; the strip's five tabs left of the
 *        output card; the card's ink above the alert band in English and
 *        German, SAFE and ARMED.
 *
 * Cases:
 * - every_control_is_a_finger_tall_inside_the_card_and_apart
 * - the_five_tabs_end_left_of_the_output_card
 * - no_text_lies_under_the_alert_band_in_either_language
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_rig.h"

#include "servo_rudder_int.h"
#include "ui_text.h"
#include "ui_workbench.h"

/** @brief RUDDER's card: x 8 to 628, y 8 to 436. */
static const gfx_rect_t k_page = { UI_MARGIN, UI_MARGIN, 620, 436 - 8 };

/** @brief The lowest row a control may reach: the card's floor, 8px over
 *         the footer. */
#define FLOOR_Y 436

/** @brief The guide's live half beside a dock on the right, panel x 0 to
 *         528. */
#define LIVE_RIGHT 528

/**
 * @brief Whether @p a lies inside @p b.
 * @return true when it does
 */
static bool inside(gfx_rect_t a, gfx_rect_t b)
{
    return a.x >= b.x && a.y >= b.y && a.x + a.w <= b.x + b.w
           && a.y + a.h <= b.y + b.h;
}

/**
 * @brief Whether @p a and @p b share a pixel.
 * @return true when they do
 */
static bool overlap(gfx_rect_t a, gfx_rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h
           && b.y < a.y + a.h;
}

/* The controls, the readout and the state lines: inside the page area and
 * the live half, above the alert band, none on another; the controls
 * UI_TAP_MIN tall and wide or more. */
TEST_CASE(every_control_is_a_finger_tall_inside_the_card_and_apart)
{
    fresh();
    gfx_rect_t r[RUD_C_COUNT + 1];
    int n = 0;
    for (int k = RUD_C_LENGTH; k < RUD_C_COUNT; ++k) {
        r[n] = rudder_rect((rudder_ctl_t)k);
        CHECK(r[n].h >= UI_TAP_MIN);
        CHECK(r[n].w >= UI_TAP_MIN);
        ++n;
    }
    r[n++] = rudder_readout_rect();
    r[n++] = rudder_state_rect();
    for (int i = 0; i < n; ++i) {
        CHECK(inside(r[i], k_page));
        CHECK(r[i].y + r[i].h <= FLOOR_Y);
        CHECK(r[i].x + r[i].w <= LIVE_RIGHT);
        for (int j = i + 1; j < n; ++j) {
            if (overlap(r[i], r[j])) {
                T_FAIL("rects %d and %d overlap", i, j);
            }
        }
    }
    CHECK_EQ(rudder_rect(RUD_C_NONE).w, 0);
    CHECK_EQ(rudder_rect(RUD_C_COUNT).h, 0);
    /* The buttons in a row, 10px apart, as the guide's item marks them. */
    const gfx_rect_t a = rudder_rect(RUD_C_1000);
    const gfx_rect_t c = rudder_rect(RUD_C_CENTRE);
    const gfx_rect_t b = rudder_rect(RUD_C_2000);
    CHECK_EQ(a.y, c.y);
    CHECK_EQ(c.y, b.y);
    CHECK_EQ(c.x - (a.x + a.w), 10);
    CHECK_EQ(b.x - (c.x + c.w), 10);
    CHECK_EQ(rudder_rect(RUD_C_ZERO).y, rudder_rect(RUD_C_RESET).y);
}

/**
 * @brief Pixels between the card's floor and the footer, over the left
 *        card's width, that are not the background.
 * @return the count
 */
static int ink_under_the_band(void)
{
    int n = 0;
    for (int y = FLOOR_Y; y < FLOOR_Y + 8; ++y) {
        for (int x = UI_MARGIN; x < UI_MARGIN + 620; ++x) {
            n += fb[(size_t)y * W + x] != ui_theme_color(UI_C_BG);
        }
    }
    return n;
}

/* The card in English and German, SAFE and ARMED, with a reading, LENGTH
 * and the source off: ink over the card's floor only. */
TEST_CASE(no_text_lies_under_the_alert_band_in_either_language)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        for (int armed = 0; armed < 2; ++armed) {
            fresh();
            ui_text_set_language((ui_lang_t)lang);
            if (armed) {
                servo_screen_set_armed(true);
            }
            go(SERVO_FN_RUDDER);
            scr->render(&cv, 0);
            CHECK_EQ(ink_under_the_band(), 0);
            settings_set(SET_ENC_EN, 1.0f);
            settings_set(SET_RUDDER_LEN_MM, 500.0f);
            const servo_test_enc_t e = { .valid = true, .raw = 1024u,
                                         .taken_ms = 10u };
            servo_screen_encoder(&e);
            servo_invalidate();
            scr->render(&cv, 0);
            CHECK_EQ(ink_under_the_band(), 0);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(every_control_is_a_finger_tall_inside_the_card_and_apart);
    RUN(no_text_lies_under_the_alert_band_in_either_language);
    return test_summary("rudder_layout");
}
