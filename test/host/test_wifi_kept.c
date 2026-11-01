/**
 * @file test_wifi_kept.c
 * @brief WI-FI's HOTSPOT PASSWORD and WEB CODE: the buttons of the radio-off
 *        view, the view of each, the keyboard at 7, 8, 63 and 64 characters,
 *        BACK TO RANDOM with no typing, the home tag's levels, what a
 *        screenshot may take, and the rows and lines with a 63-character value
 *        inside the body.
 *
 * Cases:
 * - each_button_says_what_stands_and_opens_its_view
 * - the_keyboard_refuses_7_and_takes_8_and_63_and_no_64th
 * - back_to_random_needs_no_typing_and_only_stands_for_a_value
 * - the_keyboard_types_printable_ascii_only
 * - cancel_and_the_home_tag_keep_what_stands
 * - an_image_arriving_closes_the_keyboard_and_keeps_what_stands
 * - with_the_radio_up_there_is_no_way_to_change_a_value
 * - a_screenshot_is_refused_while_a_kept_value_shows
 * - the_view_says_what_stands_and_what_went_wrong
 * - a_value_of_63_characters_stays_in_the_body_in_both_languages
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "gfx.h"
#include "ui_layout.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "wifi_screen.h"
#include "wifi_spots.h"

#define SCREEN_W UI_PANEL_W
#define BODY_H   UI_BODY_H

static gfx_color_t s_px[UI_PANEL_W * UI_PANEL_H];
static gfx_canvas_t s_c;
static wifi_view_t v;

/** @brief A value of 63 characters, marks and a space among them. */
static const char k_63[] =
    "abc DEF;\"%+=,1234567890123456789012345678901234567890123456789x";

/** @brief The screen under test. @return it */
static const ui_screen_t *scr(void) { return wifi_screen(); }

/** @brief Hand the screen the view. */
static void show(void) { wifi_screen_set(&v); }

/** @brief A fresh screen: the radio off, nothing kept. */
static void fresh(void)
{
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
    scr()->reset();
    scr()->enter();
    gfx_canvas_init(&s_c, s_px, SCREEN_W, BODY_H, SCREEN_W);
    memset(&v, 0, sizeof(v));
    v.code_on = true;
    show();
    (void)wifi_screen_take();
}

/**
 * @brief One touch event.
 * @param t  its type
 * @param x  body x
 * @param y  body y
 */
static void send(touch_event_type_t t, int x, int y)
{
    touch_event_t e = { .type = t,
                        .point = { .id = 1, .x = (int16_t)x, .y = (int16_t)y,
                                   .strength = 40 } };
    scr()->event(&e);
}

/**
 * @brief A tap.
 * @param x  body x
 * @param y  body y
 */
static void tap(int x, int y)
{
    send(TOUCH_EVENT_DOWN, x, y);
    send(TOUCH_EVENT_UP, x, y);
}

/** @brief Tap button @p i of the radio-off view. @param i  0 to 6 */
static void tap_btn(int i) { tap(WF_BTN_CX(i), WF_BTN_CY(i)); }

/** @brief TYPE A VALUE. */
static void tap_type(void) { tap(WF_RESCAN_CX, WF_SCAN_CY); }

/** @brief BACK TO RANDOM. */
static void tap_random(void) { tap(WF_HIDDEN_CX, WF_SCAN_CY); }

/** @brief CLOSE. */
static void tap_close(void) { tap(WF_SCAN_CANCEL_CX, WF_SCAN_CY); }

/** @brief Tap key @p key of the open keyboard. @param key  its number */
static void key(int key_no)
{
    const gfx_rect_t r = ui_textkey_key_rect(wifi_screen_textkey(), key_no);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/** @brief The key that types a digit on the top row: "1" is key 0.
 *  @param n  how many characters to type */
static void type_n(int n)
{
    for (int i = 0; i < n; ++i) {
        key(0);
    }
}

/**
 * @brief The application's side: the value a request hands over goes into the
 *        view, as panel_update.c and wifi_access.c do.
 * @return the WIFI_REQ_KEPT_* bits taken
 */
static unsigned apply(void)
{
    const unsigned r = wifi_screen_take();
    if ((r & (WIFI_REQ_KEPT_PASS | WIFI_REQ_KEPT_CODE)) != 0u) {
        char value[WIFI_PASS_MAX + 1];
        wifi_screen_kept(value);
        if ((r & WIFI_REQ_KEPT_PASS) != 0u) {
            snprintf(v.kept_pass, sizeof(v.kept_pass), "%s", value);
        } else {
            snprintf(v.kept_code, sizeof(v.kept_code), "%s", value);
        }
        show();
    }
    return r;
}

/**
 * @brief Render, and count the pixels drawn in a band of rows. * @param y0  the first row
 * @param y1  one past the last
 * @return pixels other than the first pixel's colour
 */
static int lit(int y0, int y1)
{
    memset(s_px, 0, sizeof(s_px));
    wifi_screen_invalidate();
    scr()->render(&s_c, 0);
    const gfx_color_t bg = s_px[0];
    int n = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = 0; x < SCREEN_W; ++x) {
            n += (s_px[y * SCREEN_W + x] != bg) ? 1 : 0;
        }
    }
    return n;
}

/**
 * @brief Render, and hash a band of rows: text on a button lights no more
 *        pixels, it changes them.
 * @param y0  the first row
 * @param y1  one past the last
 * @return the hash
 */
static uint32_t band_hash(int y0, int y1)
{
    (void)lit(0, 0);
    uint32_t h = 2166136261u;
    for (int i = y0 * SCREEN_W; i < y1 * SCREEN_W; ++i) {
        h = (h ^ (uint32_t)s_px[i]) * 16777619u;
    }
    return h;
}

TEST_CASE(each_button_says_what_stands_and_opens_its_view)
{
    fresh();
    CHECK(lit(WF_BTN_Y + 6 * WF_BTN_P, WF_BTN_Y + 6 * WF_BTN_P + WF_BTN_H) > 0);
    CHECK(WF_BTN_Y + 6 * WF_BTN_P + WF_BTN_H <= WF_FOOT);
    const int y5 = WF_BTN_Y + 5 * WF_BTN_P;
    const uint32_t random_row = band_hash(y5, y5 + WF_BTN_H);
    strcpy(v.kept_pass, "12345678");
    show();
    CHECK(band_hash(y5, y5 + WF_BTN_H) != random_row);
    v.kept_pass[0] = '\0';
    show();
    /* Neither button starts or asks anything by itself. */
    tap_btn(5);
    CHECK(scr()->covered());
    CHECK_EQ(wifi_screen_take(), 0u);
    tap_close();
    CHECK(!scr()->covered());
    tap_btn(6);
    CHECK(scr()->covered());
    CHECK_EQ(wifi_screen_take(), 0u);
    tap_close();
    CHECK(!scr()->covered());
    /* The bench not SAFE leaves both open: nothing here reaches the radio. */
    v.block = WIFI_WHY_ARMED;
    show();
    tap_btn(5);
    CHECK(scr()->covered());
    tap_close();
    /* No Wi-Fi at start: no view. */
    wifi_screen_set_unavailable();
    tap_btn(5);
    CHECK(!scr()->covered());
}

TEST_CASE(the_keyboard_refuses_7_and_takes_8_and_63_and_no_64th)
{
    for (int which = 0; which < 2; ++which) {
        const unsigned bit = (which == 0) ? WIFI_REQ_KEPT_PASS
                                          : WIFI_REQ_KEPT_CODE;
        fresh();
        tap_btn(5 + which);
        tap_type();
        CHECK(wifi_screen_textkey()->open);
        CHECK(wifi_screen_textkey()->secret);
        type_n(7);
        key(UI_TK_OK);
        CHECK(wifi_screen_textkey()->open);        /* 7: refused, with its line */
        CHECK(wifi_screen_textkey()->refused);
        CHECK_EQ(wifi_screen_take(), 0u);
        type_n(1);
        key(UI_TK_OK);                             /* 8: taken */
        CHECK(!wifi_screen_textkey()->open);
        CHECK_EQ(apply(), bit);
        CHECK_STR_EQ((which == 0) ? v.kept_pass : v.kept_code, "11111111");
        CHECK(scr()->covered());                   /* back on the view */
        tap_type();
        type_n(63);
        CHECK_EQ(wifi_screen_textkey()->len, 63);
        type_n(1);                                 /* the 64th does not go in */
        CHECK_EQ(wifi_screen_textkey()->len, 63);
        key(UI_TK_OK);
        CHECK_EQ(apply(), bit);
        CHECK_EQ(strlen((which == 0) ? v.kept_pass : v.kept_code), 63u);
        /* The other value did not move. */
        CHECK_EQ(((which == 0) ? v.kept_code : v.kept_pass)[0], '\0');
    }
}

TEST_CASE(back_to_random_needs_no_typing_and_only_stands_for_a_value)
{
    for (int which = 0; which < 2; ++which) {
        const unsigned bit = (which == 0) ? WIFI_REQ_KEPT_PASS
                                          : WIFI_REQ_KEPT_CODE;
        fresh();
        tap_btn(5 + which);
        tap_random();                              /* RANDOM already: grey */
        CHECK_EQ(wifi_screen_take(), 0u);
        tap_close();
        strcpy((which == 0) ? v.kept_pass : v.kept_code, "kept value");
        show();
        tap_btn(5 + which);
        tap_random();
        CHECK_EQ(apply(), bit);
        CHECK_STR_EQ((which == 0) ? v.kept_pass : v.kept_code, "");
        CHECK(!wifi_screen_textkey()->open);
        CHECK(scr()->covered());
    }
}

TEST_CASE(the_keyboard_types_printable_ascii_only)
{
    fresh();
    tap_btn(5);
    tap_type();
    /* Every key of the three layers, space and all; DEL and the two layer
     * keys type nothing. */
    for (int layer = 0; layer < 3; ++layer) {
        for (int k = 0; k < UI_TEXTKEY_CHARS; ++k) {
            if (k == UI_TK_DEL || k == UI_TK_SHIFT || k == UI_TK_LAYER) {
                continue;
            }
            key(UI_TK_CLR);
            key(k);
            const ui_textkey_t *t = wifi_screen_textkey();
            for (int i = 0; i < t->len; ++i) {
                CHECK(t->text[i] >= 0x20 && t->text[i] <= 0x7E);
            }
        }
        if (layer == 0) {
            key(UI_TK_SHIFT);
        } else if (layer == 1) {
            key(UI_TK_LAYER);
        }
    }
    key(UI_TK_CLR);
    key(UI_TK_SPACE);
    CHECK_STR_EQ(wifi_screen_textkey()->text, " ");
}

TEST_CASE(cancel_and_the_home_tag_keep_what_stands)
{
    fresh();
    strcpy(v.kept_code, "kept value");
    show();
    tap_btn(6);
    tap_type();
    type_n(9);
    key(UI_TK_CANCEL);
    CHECK(scr()->covered());
    CHECK(!wifi_screen_textkey()->open);
    CHECK_EQ(wifi_screen_take(), 0u);
    /* The home tag climbs: keyboard, view, buttons, then the screen goes. */
    tap_type();
    CHECK(scr()->back());
    CHECK(!wifi_screen_textkey()->open);
    CHECK(scr()->covered());
    CHECK(scr()->back());
    CHECK(!scr()->covered());
    CHECK(!scr()->back());
    CHECK_EQ(wifi_screen_take(), 0u);
    /* What the keyboard typed is not handed over after a cancel. */
    char out[WIFI_PASS_MAX + 1];
    wifi_screen_kept(out);
    CHECK_STR_EQ(out, "");
    /* A value handed over stays in the screen no longer than that. */
    tap_btn(6);
    tap_type();
    type_n(8);
    key(UI_TK_OK);
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_KEPT_CODE);
    wifi_screen_kept(out);
    CHECK_STR_EQ(out, "11111111");
    wifi_screen_kept(out);
    CHECK_STR_EQ(out, "");
}

TEST_CASE(an_image_arriving_closes_the_keyboard_and_keeps_what_stands)
{
    fresh();
    tap_btn(5);
    tap_type();
    type_n(8);
    v.up = WIFI_UP_STAGED;
    show();
    send(TOUCH_EVENT_DOWN, 5, 5);
    send(TOUCH_EVENT_UP, 5, 5);
    CHECK(!wifi_screen_textkey()->open);
    CHECK_EQ(wifi_screen_take(), 0u);
    v.up = WIFI_UP_IDLE;
    show();
    CHECK(!scr()->covered());
}

TEST_CASE(with_the_radio_up_there_is_no_way_to_change_a_value)
{
    fresh();
    v.radio = WIFI_RADIO_AP;
    strcpy(v.pass, "k7mqx3ha");
    strcpy(v.code, "r4tcw9pe");
    show();
    for (int y = 0; y < WF_FOOT; y += 20) {
        tap(WF_BTN_CX(0), y);
        tap(WF_BTN_CX(0) + 600, y);
        const unsigned r = wifi_screen_take();
        CHECK_EQ(r & (WIFI_REQ_KEPT_PASS | WIFI_REQ_KEPT_CODE), 0u);
        CHECK(!scr()->covered());
    }
}

TEST_CASE(a_screenshot_is_refused_while_a_kept_value_shows)
{
    fresh();
    /* Nothing kept: every view of the two may be taken. */
    CHECK(!scr()->no_shot());
    tap_btn(5);
    CHECK(!scr()->no_shot());
    /* The keyboard of the view: what it types is about to be kept. */
    tap_type();
    CHECK(scr()->no_shot());
    key(UI_TK_CANCEL);
    tap_close();
    /* The buttons name no value. */
    strcpy(v.kept_pass, "kept value");
    show();
    CHECK(!scr()->no_shot());
    tap_btn(5);
    CHECK(scr()->no_shot());
    tap_close();
    /* The other value's view shows RANDOM. */
    tap_btn(6);
    CHECK(!scr()->no_shot());
    tap_close();
    /* The rows of a running radio show it. */
    v.radio = WIFI_RADIO_AP;
    show();
    CHECK(scr()->no_shot());
    v.kept_pass[0] = '\0';
    strcpy(v.kept_code, "kept value");
    show();
    CHECK(scr()->no_shot());
    v.kept_code[0] = '\0';
    show();
    CHECK(!scr()->no_shot());
    /* The question about an image covers them. */
    strcpy(v.kept_code, "kept value");
    v.up = WIFI_UP_STAGED;
    show();
    CHECK(!scr()->no_shot());
}

TEST_CASE(the_view_says_what_stands_and_what_went_wrong)
{
    fresh();
    tap_btn(6);
    const uint32_t random_view = band_hash(0, WF_SCAN_BY);
    tap_close();
    /* A short value in the large face, a long one in the body's. */
    strcpy(v.kept_code, "12345678");
    show();
    tap_btn(6);
    const uint32_t short_view = band_hash(0, WF_SCAN_BY);
    CHECK(short_view != random_view);
    strcpy(v.kept_code, k_63);
    show();
    const uint32_t long_view = band_hash(0, WF_SCAN_BY);
    CHECK(long_view != short_view);
    /* ACCESS CODE off says so on WEB CODE's view and not on the other's. */
    v.code_on = false;
    show();
    const uint32_t off_view = band_hash(0, WF_SCAN_BY);
    CHECK(off_view != long_view);
    tap_close();
    tap_btn(5);
    const uint32_t pass_view = band_hash(0, WF_SCAN_BY);
    v.code_on = true;
    show();
    CHECK_EQ(band_hash(0, WF_SCAN_BY), pass_view);
    /* A write that failed says so. */
    v.save_failed = true;
    show();
    CHECK(band_hash(0, WF_SCAN_BY) != pass_view);
}

TEST_CASE(a_value_of_63_characters_stays_in_the_body_in_both_languages)
{
    CHECK_EQ(strlen(k_63), 63u);
    for (int de = 0; de < 2; ++de) {
        fresh();
        ui_text_set_language(de ? UI_LANG_DE : UI_LANG_EN);
        /* The kept view: nothing past the card's right edge or its foot. */
        strcpy(v.kept_pass, k_63);
        show();
        tap_btn(5);
        CHECK(lit(0, WF_SCAN_BY) > 0);
        for (int y = 0; y < BODY_H; ++y) {
            CHECK_EQ(s_px[y * SCREEN_W + SCREEN_W - 1], s_px[0]);
        }
        tap_close();
        /* The running view: both rows, and the writes' sentence on two lines
         * with ACCESS CODE off. */
        v.radio = WIFI_RADIO_AP;
        v.channel = 6;
        strcpy(v.pass, k_63);
        strcpy(v.code, k_63);
        v.code_on = false;
        v.writes = WIFI_WRITES_CODE;
        v.up = WIFI_UP_RECEIVING;
        v.total = 100u;
        show();
        CHECK(lit(0, WF_OFF_Y) > 0);
        for (int y = 0; y < BODY_H; ++y) {
            CHECK_EQ(s_px[y * SCREEN_W + SCREEN_W - 1], s_px[0]);
        }
        /* The line that carries the code alone fits where the sentence does
         * not. */
        char line[160];
        snprintf(line, sizeof(line), TR(WF_WR_CODE), k_63);
        CHECK(gfx_text_width(UI_FONT_BODY, line, 1) > SCREEN_W - 32);
        CHECK(gfx_text_width(UI_FONT_BODY, k_63, 1) <= SCREEN_W - 32);
        CHECK(gfx_text_width(UI_FONT_BODY, TR(WF_WR_CODE_LONG), 1)
              <= SCREEN_W - 32);
        /* An 8-character code keeps the one sentence. */
        snprintf(line, sizeof(line), TR(WF_WR_CODE), "12345678");
        CHECK(gfx_text_width(UI_FONT_BODY, line, 1) <= SCREEN_W - 32);
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(each_button_says_what_stands_and_opens_its_view);
    RUN(the_keyboard_refuses_7_and_takes_8_and_63_and_no_64th);
    RUN(back_to_random_needs_no_typing_and_only_stands_for_a_value);
    RUN(the_keyboard_types_printable_ascii_only);
    RUN(cancel_and_the_home_tag_keep_what_stands);
    RUN(an_image_arriving_closes_the_keyboard_and_keeps_what_stands);
    RUN(with_the_radio_up_there_is_no_way_to_change_a_value);
    RUN(a_screenshot_is_refused_while_a_kept_value_shows);
    RUN(the_view_says_what_stands_and_what_went_wrong);
    RUN(a_value_of_63_characters_stays_in_the_body_in_both_languages);
    return test_summary("wifi_kept");
}
