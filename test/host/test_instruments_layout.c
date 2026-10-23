/**
 * @file test_instruments_layout.c
 * @brief RECEIVER, BATTERY and BALANCE at the 1024 x 600 sizes: every tab,
 *        button and key is a finger tall and acts where it is drawn; none
 *        of the three names a stop control; nothing is drawn under the
 *        alert band in English or German, empty or at the fullest; the
 *        longest texts fit the lines the cards hold.
 *
 * Cases:
 * - the_tabs_are_a_finger_tall_and_act_to_their_edges
 * - balance_controls_are_a_finger_tall_and_act_to_their_edges
 * - no_screen_names_a_stop_control
 * - receiver_draws_nothing_under_the_alert_band
 * - battery_draws_nothing_under_the_alert_band
 * - balance_draws_nothing_under_the_alert_band
 * - the_longest_lines_fit_their_cards
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "analyser_screen.h"
#include "balance_screen.h"
#include "battery_screen.h"
#include "instruments_spots.h"
#include "ui_keypad.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"

#define W UI_PANEL_W  /**< The canvas's width, px. */
#define H UI_BODY_H   /**< The body's height: the screens' own canvas. */

static gfx_color_t *fb;
static gfx_canvas_t cv;

/** @brief A cleared body canvas, the dark theme, and @p scr reset.
 *  @param scr The screen. */
static void fresh(const ui_screen_t *scr)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb);
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    scr->reset();
}

/** @brief A tap at (@p x, @p y): a press and its release in place.
 *  @param scr The screen.
 *  @param x   Body column.
 *  @param y   Body row. */
static void tap(const ui_screen_t *scr, int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    scr->event(&e);
    e.type = TOUCH_EVENT_UP;
    scr->event(&e);
}

/** @brief FNV-1a of the canvas: one number for a picture.
 *  @return the hash */
static uint32_t picture(void)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < (size_t)W * H; ++i) {
        h = (h ^ fb[i]) * 16777619u;
    }
    return h;
}

/** @brief A frame of S.BUS values with channel 2 off neutral.
 *  @param now      The frame's time, ms.
 *  @param failsafe The receiver's failsafe flag.
 *  @param lost     The frame-lost flag. */
static void push(uint32_t now, bool failsafe, bool lost)
{
    sbus_frame_t f;
    memset(&f, 0, sizeof(f));
    for (unsigned i = 0; i < SBUS_CHANNELS; ++i) {
        f.channel[i] = 1024;
    }
    f.channel[1] = (uint16_t)(172 + (now / 14u) * 7u % 1640u);
    f.channel[15] = 1811;                 /* a lane at its top */
    f.channel[7] = 172;                   /* and one at its bottom */
    f.ch17 = true;
    f.ch18 = true;
    f.failsafe = failsafe;
    f.frame_lost = lost;
    sbus_decoder_t d;
    sbus_decoder_reset(&d);
    d.frames = 4000000000u;               /* the widest counters */
    d.resyncs = 4000000000u;
    d.bad_footer = 4000000000u;
    uint8_t raw[SBUS_FRAME_BYTES];
    memset(raw, 0xEE, sizeof(raw));
    raw[0] = SBUS_HEADER;
    analyser_screen_push(&f, &d, raw, SBUS_FRAME_BYTES, now);
}

/** @brief The pixels from IN_TEXT_FLOOR to 12 px above the cards' foot,
 *         clear of their edges, that are not the cards' fill; the screen
 *         drawn whole first.
 *  @param scr  The screen.
 *  @param inv  Its invalidate.
 *  @param l    The left card's left edge and width, then the right's.
 *  @return the count */
static int ink_under_the_band(const ui_screen_t *scr, void (*inv)(void),
                              const int l[4])
{
    inv();
    scr->render(&cv, 0);
    const gfx_color_t fill = ui_theme_color(UI_C_PANEL);
    int n = 0;
    for (int y = IN_TEXT_FLOOR; y < H - UI_MARGIN - 12; ++y) {
        for (int k = 0; k < 4; k += 2) {
            for (int x = l[k] + 12; x < l[k] + l[k + 1] - 12; ++x) {
                n += fb[(size_t)y * W + x] != fill;
            }
        }
    }
    return n;
}

/*
 * The tabs of RECEIVER and BALANCE are UI_TAP_MIN tall and take a tap at
 * every corner of their 150 x 48 px: a tap 1 px inside each corner of the
 * second tab switches the pane, one 1 px outside its left edge does not
 * (that is the first tab's gap).
 */
TEST_CASE(the_tabs_are_a_finger_tall_and_act_to_their_edges)
{
    CHECK(IN_TAB_H >= UI_TAP_MIN);
    CHECK(IN_TAB_T + IN_TAB_H < AN_CARD_Y);
    const int xs[2] = { IN_TAB_L(1), IN_TAB_L(1) + IN_TAB_W - 1 };
    const int ys[2] = { IN_TAB_T, IN_TAB_T + IN_TAB_H - 1 };

    const ui_screen_t *an = analyser_screen();
    fresh(an);
    an->render(&cv, 0);
    const uint32_t channels = picture();
    for (int i = 0; i < 4; ++i) {
        fresh(an);
        tap(an, IN_TAB_L(1) - 2, ys[i & 1]);      /* the gap: no tab */
        analyser_invalidate();
        an->render(&cv, 0);
        CHECK_EQ(picture(), channels);
        tap(an, xs[i >> 1], ys[i & 1]);
        analyser_invalidate();
        an->render(&cv, 0);
        CHECK(picture() != channels);             /* RAW DATA */
    }

    const ui_screen_t *bl = balance_screen();
    for (int i = 0; i < 4; ++i) {
        fresh(bl);
        CHECK(balance_screen_model_mark() != NULL);   /* BALANCE */
        tap(bl, IN_TAB_L(1) - 2, ys[i & 1]);
        CHECK(balance_screen_model_mark() != NULL);
        tap(bl, xs[i >> 1], ys[i & 1]);
        CHECK(balance_screen_model_mark() == NULL);   /* TEST RIG */
        tap(bl, IN_TAB_X(2), IN_TAB_Y);
        CHECK(balance_screen_model_mark() == NULL);   /* AIRCRAFT */
        tap(bl, IN_TAB_X(0), IN_TAB_Y);
        CHECK(balance_screen_model_mark() != NULL);
    }
}

/*
 * BALANCE's PROPELLER and EDF are 162 x 48 px, its "-" and "+" 56 x 48 px
 * and its count 114 x 48 px, 8 px apart, all above the alert band; each
 * acts on a tap 1 px inside its corners.
 */
TEST_CASE(balance_controls_are_a_finger_tall_and_act_to_their_edges)
{
    const ui_screen_t *bl = balance_screen();
    fresh(bl);
    const gfx_rect_t c = balance_screen_blades_rect();
    CHECK_EQ(c.x, BL_COUNT_L);
    CHECK_EQ(c.w, BL_COUNT_W);
    CHECK_EQ(c.y, BL_BLADES_T);
    CHECK(c.w >= UI_TAP_MIN && c.h >= UI_TAP_MIN);
    CHECK(c.x + c.w + 8 <= BL_DOWN_X - BL_STEP_W / 2);
    CHECK(BL_ROTOR_W >= UI_TAP_MIN && BL_STEP_W >= UI_TAP_MIN);
    CHECK(BL_BLADES_T + UI_TAP_MIN <= IN_TEXT_FLOOR);
    CHECK(BL_ROTOR_T + UI_TAP_MIN + 8 <= BL_BLADES_T);

    const int top = BL_ROTOR_T, bot = BL_ROTOR_T + UI_TAP_MIN - 1;
    const int el = BL_EDF_X - BL_ROTOR_W / 2, er = el + BL_ROTOR_W - 1;
    const int pl = BL_PROP_X - BL_ROTOR_W / 2, pr = pl + BL_ROTOR_W - 1;
    const int ex[4] = { el, er, el, er }, ey[4] = { top, top, bot, bot };
    const int px[4] = { pl, pr, pl, pr };
    for (int i = 0; i < 4; ++i) {
        tap(bl, ex[i], ey[i]);
        CHECK_EQ(balance_screen_rotor(), ROTOR_EDF);
        tap(bl, px[i], ey[i]);
        CHECK_EQ(balance_screen_rotor(), ROTOR_PROP);
    }
    /* Between the two: neither. */
    tap(bl, pr + 4, BL_ROTOR_Y);
    CHECK_EQ(balance_screen_rotor(), ROTOR_PROP);

    const int st = BL_BLADES_T, sb = BL_BLADES_T + UI_TAP_MIN - 1;
    const int ul = BL_UP_X - BL_STEP_W / 2, ur = ul + BL_STEP_W - 1;
    const int dl = BL_DOWN_X - BL_STEP_W / 2, dr = dl + BL_STEP_W - 1;
    CHECK_EQ(ul - dr - 1, 8);
    const int ux[4] = { ul, ur, ul, ur }, uy[4] = { st, st, sb, sb };
    const int dx[4] = { dl, dr, dl, dr };
    for (int i = 0; i < 4; ++i) {
        tap(bl, ux[i], uy[i]);
        CHECK_EQ(balance_screen_blades(), 3);
        tap(bl, dx[i], uy[i]);
        CHECK_EQ(balance_screen_blades(), 2);
    }
    /* The count opens its keypad from its corners, over both cards and
     * above the alert band. */
    tap(bl, c.x, c.y + c.h - 1);
    const ui_keypad_t *k = balance_screen_keypad();
    CHECK(k->open);
    CHECK_EQ(k->area.y, BL_KP_Y);
    CHECK_EQ(k->area.h, BL_KP_H);
    CHECK(k->area.y + k->area.h <= IN_TEXT_FLOOR);
    for (int key = 0; key < UI_KEY_COUNT; ++key) {
        const gfx_rect_t r = ui_keypad_key_rect(k, (ui_key_t)key);
        if (r.w == 0) {
            continue;                     /* +/-: not on an unsigned pad */
        }
        CHECK(r.w >= UI_TAP_MIN && r.h >= UI_TAP_MIN);
        CHECK(r.y + r.h <= IN_TEXT_FLOOR);
    }
    CHECK(bl->back());
    CHECK(!k->open);
}

/* None of the three drives an output, so none names a stop control the
 * router hands through the alert band. */
TEST_CASE(no_screen_names_a_stop_control)
{
    CHECK(analyser_screen()->safe_at == NULL);
    CHECK(battery_screen()->safe_at == NULL);
    CHECK(balance_screen()->safe_at == NULL);
}

/*
 * RECEIVER in both languages: silent, live with every lane's history
 * filled and the widest counters, failsafe, frame lost, and RAW DATA empty
 * and fed: nothing under the alert band, and the last lane and CH18 end
 * above it.
 */
TEST_CASE(receiver_draws_nothing_under_the_alert_band)
{
    const ui_screen_t *an = analyser_screen();
    const int cards[4] = { UI_MARGIN, AN_RCARD_X - 8 - UI_MARGIN,
                           AN_RCARD_X, AN_RCARD_W };
    CHECK(AN_LANE_TOP + 8 * AN_LANE_H <= IN_TEXT_FLOOR);
    CHECK(AN_CH17_Y + 2 * AN_CHN_H + 8 <= IN_TEXT_FLOOR);
    CHECK(AN_COL_X(1) + AN_LANE_W <= AN_RCARD_X - 8 - 4);
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        fresh(an);
        CHECK_EQ(ink_under_the_band(an, analyser_invalidate, cards), 0);
        tap(an, IN_TAB_X(1), IN_TAB_Y);
        CHECK_EQ(ink_under_the_band(an, analyser_invalidate, cards), 0);
        tap(an, IN_TAB_X(0), IN_TAB_Y);
        for (uint32_t t = 0; t < 260u; ++t) {
            push(14u * t, false, false);
        }
        CHECK_EQ(ink_under_the_band(an, analyser_invalidate, cards), 0);
        push(14u * 261u, true, false);
        CHECK_EQ(ink_under_the_band(an, analyser_invalidate, cards), 0);
        push(14u * 262u, false, true);
        CHECK_EQ(ink_under_the_band(an, analyser_invalidate, cards), 0);
        tap(an, IN_TAB_X(1), IN_TAB_Y);
        CHECK_EQ(ink_under_the_band(an, analyser_invalidate, cards), 0);
        analyser_screen_silent(10000u);
        CHECK_EQ(ink_under_the_band(an, analyser_invalidate, cards), 0);
    }
    ui_text_set_language(UI_LANG_EN);
}

/** @brief A pack of @p n cells about 3.8 V, the last @p dip_mv low.
 *  @param n      Cells.
 *  @param dip_mv The last cell's departure, mV. */
static void pack(int n, float dip_mv)
{
    battery_state_t b;
    memset(&b, 0, sizeof(b));
    b.cells = n;
    for (int i = 0; i < n; ++i) {
        b.volts[i] = 3.81f + 0.003f * (float)(i % 3);
        b.milliohms[i] = 99.9f;
    }
    b.volts[n - 1] -= dip_mv / 1000.0f;
    b.capacity_mah = 99999.0f;
    b.drawn_mah = 99999.0f;
    b.valid = true;
    battery_screen_set(&b);
}

/*
 * BATTERY in both languages: no pack, one cell, 6S at each verdict and
 * 14S, the fullest: nothing under the alert band; at every count each
 * voltage fits its cell's slot in the face the cells row takes, the
 * 12 x 24 face up to 11 cells.
 */
TEST_CASE(battery_draws_nothing_under_the_alert_band)
{
    const ui_screen_t *bt = battery_screen();
    const int cards[4] = { UI_MARGIN, BT_LCARD_W, BT_RCARD_X,
                           W - UI_MARGIN - BT_RCARD_X };
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        fresh(bt);
        battery_screen_set(NULL);
        CHECK_EQ(ink_under_the_band(bt, battery_invalidate, cards), 0);
        static const float k_dip[3] = { 10.0f, 40.0f, 90.0f };
        for (int v = 0; v < 3; ++v) {
            pack(6, k_dip[v]);
            CHECK_EQ(ink_under_the_band(bt, battery_invalidate, cards), 0);
        }
        pack(1, 0.0f);
        CHECK_EQ(ink_under_the_band(bt, battery_invalidate, cards), 0);
        pack(BATTERY_CELLS_MAX, 90.0f);
        CHECK_EQ(ink_under_the_band(bt, battery_invalidate, cards), 0);
    }
    ui_text_set_language(UI_LANG_EN);

    for (int n = 1; n <= BATTERY_CELLS_MAX; ++n) {
        const gfx_font_t *f = battery_screen_cells_font(n);
        CHECK(gfx_text_width(f, "3.81", 1) <= BT_PLOT_W / n);
        if (n <= 11) {
            CHECK(f == UI_FONT_BODY);
        }
    }
    CHECK(battery_screen_cells_font(BATTERY_CELLS_MAX) != UI_FONT_FINE);
    CHECK(BT_VOLT_Y + 24 <= IN_TEXT_FLOOR);
}

/*
 * BALANCE in both languages: a two-blade propeller, a six-blade ducted
 * fan (the widest picture), TEST RIG and AIRCRAFT: nothing under the alert
 * band.  The keypad open draws nothing there either: the body under it is
 * the ground.
 */
TEST_CASE(balance_draws_nothing_under_the_alert_band)
{
    const ui_screen_t *bl = balance_screen();
    const int cards[4] = { UI_MARGIN, BL_RCARD_X - 8 - UI_MARGIN,
                           BL_RCARD_X, W - UI_MARGIN - BL_RCARD_X };
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        fresh(bl);
        CHECK_EQ(ink_under_the_band(bl, balance_invalidate, cards), 0);
        tap(bl, BL_EDF_X, BL_ROTOR_Y);
        for (int i = 0; i < 4; ++i) {
            tap(bl, BL_UP_X, BL_BLADES_Y);
        }
        CHECK_EQ(balance_screen_blades(), 6);
        CHECK_EQ(ink_under_the_band(bl, balance_invalidate, cards), 0);
        tap(bl, IN_TAB_X(1), IN_TAB_Y);
        CHECK_EQ(ink_under_the_band(bl, balance_invalidate, cards), 0);
        tap(bl, IN_TAB_X(2), IN_TAB_Y);
        CHECK_EQ(ink_under_the_band(bl, balance_invalidate, cards), 0);

        tap(bl, IN_TAB_X(0), IN_TAB_Y);
        tap(bl, BL_COUNT_X, BL_BLADES_Y);
        CHECK(balance_screen_keypad()->open);
        balance_invalidate();
        bl->render(&cv, 0);
        int n = 0;
        for (int y = IN_TEXT_FLOOR; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                n += fb[(size_t)y * W + x] != ui_theme_color(UI_C_BG);
            }
        }
        CHECK_EQ(n, 0);
        CHECK(bl->back());
    }
    ui_text_set_language(UI_LANG_EN);
}

/*
 * The longest texts in both languages fit the lines their cards hold:
 * RECEIVER's state line three lines of the 12 x 24 face, BATTERY's
 * verdict line two, its note four lines of the 10 x 20 face.
 */
TEST_CASE(the_longest_lines_fit_their_cards)
{
    static const ui_text_id_t k_an[] = { TX_AN_WHY_SILENT, TX_AN_WHY_FAILSAFE,
                                         TX_AN_WHY_LOST, TX_AN_WHY_LIVE };
    static const ui_text_id_t k_bt[] = { TX_BAT_NO_PACK_WHY,
                                         TX_BAT_REPLACE_WHY,
                                         TX_BAT_WATCH_WHY,
                                         TX_BAT_HEALTHY_WHY };
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (size_t i = 0; i < sizeof(k_an) / sizeof(k_an[0]); ++i) {
            const int n = analyser_screen_why_lines(ui_tr(k_an[i]));
            CHECK(n >= 1 && n <= 3);
        }
        for (size_t i = 0; i < sizeof(k_bt) / sizeof(k_bt[0]); ++i) {
            const int n = battery_screen_text_lines(ui_tr(k_bt[i]), false);
            CHECK(n >= 1 && n <= 2);
        }
        char rest[160];
        snprintf(rest, sizeof(rest), "%s %s", TR(BAT_NOTE_2),
                 TR(BAT_NOTE_3));
        CHECK(battery_screen_text_lines(TR(BAT_NOTE_1), true)
              + battery_screen_text_lines(rest, true) <= 4);
    }
    ui_text_set_language(UI_LANG_EN);
    CHECK_EQ(analyser_screen_why_lines(NULL), 0);
    CHECK_EQ(analyser_screen_why_lines(""), 0);
    CHECK_EQ(battery_screen_text_lines(NULL, true), 0);
}

int main(void)
{
    RUN(the_tabs_are_a_finger_tall_and_act_to_their_edges);
    RUN(balance_controls_are_a_finger_tall_and_act_to_their_edges);
    RUN(no_screen_names_a_stop_control);
    RUN(receiver_draws_nothing_under_the_alert_band);
    RUN(battery_draws_nothing_under_the_alert_band);
    RUN(balance_draws_nothing_under_the_alert_band);
    RUN(the_longest_lines_fit_their_cards);
    return test_summary("instruments_layout");
}
