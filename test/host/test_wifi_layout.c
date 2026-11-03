/**
 * @file test_wifi_layout.c
 * @brief WI-FI at the 1024 x 600 sizes: every control is a finger tall,
 *        acts where wifi_spots.h says it is drawn and nowhere beside it;
 *        nothing of any view, keyboards included, reaches the alert band,
 *        in English or German; the longest texts stay inside their cards
 *        and buttons; an SSID of 32 characters shows whole in the list,
 *        the rows, the joining line and the keyboard's title; a full list
 *        of 16 networks and an empty one.
 *
 * Cases:
 * - the_spots_are_a_finger_tall_and_end_above_the_alert_band
 * - every_button_acts_inside_and_not_beside_its_rectangle
 * - every_scan_row_and_the_hold_act_where_they_are_drawn
 * - nothing_lies_under_the_alert_band
 * - the_longest_texts_stay_inside_their_cards_and_buttons
 * - an_ssid_of_32_characters_shows_whole
 * - a_full_list_takes_16_rows_and_an_empty_one_none
 * - wifi_has_no_stop_control
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "wifi_screen.h"
#include "wifi_spots.h"

#define W UI_PANEL_W

/** @brief One frame at 39Hz, and enough of them for the 2s hold. */
#define TICK_S     (1.0f / 39.0f)
#define HOLD_TICKS ((int)(UI_HOLD_S / TICK_S) + 5)

static gfx_color_t s_px[UI_PANEL_W * UI_BODY_H];
static gfx_canvas_t s_c;
static wifi_view_t v;

/** @return the screen under test */
static const ui_screen_t *scr(void) { return wifi_screen(); }

/** @brief Hand the screen the view in @c v. */
static void show(void) { wifi_screen_set(&v); }

/** @brief The screen as it opens, English, the radio off, nothing kept. */
static void fresh(void)
{
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
    scr()->reset();
    scr()->enter();
    gfx_canvas_init(&s_c, s_px, W, UI_BODY_H, W);
    memset(&v, 0, sizeof(v));
    v.code_on = true;
    strcpy(v.name, "rcbench-3F7A");
    show();
    (void)wifi_screen_take();
}

/** @brief A touch event of @p t at (@p x, @p y), body coordinates. */
static void send(touch_event_type_t t, int x, int y)
{
    touch_event_t e = { .type = t,
                        .point = { .id = 1, .x = (int16_t)x, .y = (int16_t)y,
                                   .strength = 40 } };
    scr()->event(&e);
}

/** @brief A tap at (@p x, @p y). */
static void tap(int x, int y)
{
    send(TOUCH_EVENT_DOWN, x, y);
    send(TOUCH_EVENT_UP, x, y);
}

/** @brief Draw the view showing into the body. */
static void render(void)
{
    wifi_screen_invalidate();
    scr()->render(&s_c, 0);
}

/** @return the colour at body (@p x, @p y) */
static gfx_color_t px(int x, int y) { return s_px[(size_t)y * W + x]; }

/** @brief A kept network, as the views that offer JOIN and FORGET have. */
static void kept(void)
{
    v.have_net = true;
    strcpy(v.net, "Workshop 2.4");
    show();
}

/** @brief CHOOSE tapped and @p n networks found, the 32-character one at
 *         @p long_at (-1 for none). */
static void scanned(int n, int long_at)
{
    tap(WF_BTN_CX(2), WF_BTN_CY(2));
    (void)wifi_screen_take();
    v.radio = WIFI_RADIO_OFF;
    v.scanned = true;
    v.scan_n = n;
    for (int i = 0; i < n; ++i) {
        snprintf(v.scan[i], sizeof(v.scan[i]), "Network %02d", i);
        v.rssi[i] = (int8_t)(-40 - 4 * i);
    }
    if (long_at >= 0) {
        memset(v.scan[long_at], 'M', 32);
        v.scan[long_at][32] = '\0';
        v.rssi[long_at] = -100;
    }
    show();
}

/** @brief An image staged, as the page hands it, its offer and gate. */
static void staged(wifi_offer_t offer, wifi_gate_t gate)
{
    v.radio = WIFI_RADIO_AP;
    v.up = WIFI_UP_STAGED;
    v.offer = offer;
    v.gate = gate;
    strcpy(v.new_ver, "0.22.1");
    strcpy(v.run_ver, "0.22.0");
    show();
}

/* ------------------------------------------------------------ the spots */

/*
 * Every control is UI_TAP_MIN or taller and wider, the gap between two
 * neighbours is 8px or more, and the lowest of each view ends at
 * WF_FOOT, 4px above the alert band, or higher.
 */
TEST_CASE(the_spots_are_a_finger_tall_and_end_above_the_alert_band)
{
    CHECK_EQ(WF_FOOT, UI_KEYS_FOOT);
    CHECK(WF_BTN_H >= UI_TAP_MIN && WF_BTN_W >= UI_TAP_MIN);
    CHECK(WF_ROW_H >= UI_TAP_MIN);
    CHECK(WF_SCAN_BH >= UI_TAP_MIN);
    CHECK(WF_Q_BH >= UI_TAP_MIN);
    CHECK(WF_BTN_P - WF_BTN_H >= 8);
    CHECK(WF_SCAN_BX(1) - WF_SCAN_BX(0) - WF_SCAN_BW >= 8);
    CHECK(WF_BTN_Y + 4 * WF_BTN_P + WF_BTN_H <= WF_OFF_Y);
    CHECK(WF_OFF_Y + WF_BTN_H <= WF_FOOT);
    CHECK(WF_ROW_T(7) + WF_ROW_H + 8 <= WF_SCAN_BY);
    CHECK(WF_SCAN_BY + WF_SCAN_BH <= WF_FOOT);
    CHECK(WF_Q_BY + WF_Q_BH <= WF_FOOT);
    CHECK(WF_SCAN_BX(2) + WF_SCAN_BW <= W - UI_MARGIN);
    CHECK(WF_ROW_X1 + WF_ROW_W <= W - UI_MARGIN);
    CHECK(WF_INSTALL_X + WF_Q_BW <= WF_Q_X + WF_Q_W);
    CHECK(WF_CARD_X + WF_CARD_W <= W - UI_MARGIN);
}

/* ---------------------------------------------------------- the presses */

/** @brief How a button's view is set up before each tap. */
typedef void (*setup_fn)(void);

static void setup_main(void) { fresh(); kept(); }

static void setup_running(void)
{
    fresh();
    v.radio = WIFI_RADIO_AP;
    strcpy(v.pass, "k7mqx3ha");
    strcpy(v.code, "r4tcw9pe");
    show();
}

static void setup_scan(void) { fresh(); scanned(3, -1); }

static void setup_question(void)
{
    fresh();
    staged(WIFI_OFFER_NEWER, WIFI_GATE_OPEN);
}

/**
 * @brief Taps 2px inside each corner of a @p w x @p h button at
 *        (@p x, @p y) ask for @p want; taps 3px outside each edge ask for
 *        none of it.
 * @param set  The view's set-up, run before each tap.
 * @param x    Left edge.
 * @param y    Top edge.
 * @param w    Width.
 * @param h    Height.
 * @param want The request bits.
 */
static void edges(setup_fn set, int x, int y, int w, int h, unsigned want)
{
    const int in[4][2] = { { x + 2, y + 2 }, { x + w - 3, y + 2 },
                           { x + 2, y + h - 3 }, { x + w - 3, y + h - 3 } };
    for (int i = 0; i < 4; ++i) {
        set();
        tap(in[i][0], in[i][1]);
        CHECK_EQ(wifi_screen_take() & want, want);
    }
    const int out[4][2] = { { x - 3, y + h / 2 }, { x + w + 2, y + h / 2 },
                            { x + w / 2, y - 3 }, { x + w / 2, y + h + 2 } };
    for (int i = 0; i < 4; ++i) {
        set();
        tap(out[i][0], out[i][1]);
        CHECK_EQ(wifi_screen_take() & want, 0u);
    }
}

TEST_CASE(every_button_acts_inside_and_not_beside_its_rectangle)
{
    static const unsigned k_main[5] = { WIFI_REQ_AP, WIFI_REQ_JOIN,
                                        WIFI_REQ_SCAN, WIFI_REQ_FORGET,
                                        WIFI_REQ_CODE_OFF };
    for (int i = 0; i < 5; ++i) {
        edges(setup_main, WF_BTN_X, WF_BTN_Y + i * WF_BTN_P, WF_BTN_W,
              WF_BTN_H, k_main[i]);
    }
    edges(setup_running, WF_BTN_X, WF_OFF_Y, WF_BTN_W, WF_BTN_H,
          WIFI_REQ_OFF);
    edges(setup_scan, WF_SCAN_BX(0), WF_SCAN_BY, WF_SCAN_BW, WF_SCAN_BH,
          WIFI_REQ_SCAN);
    edges(setup_question, WF_Q_CANCEL_X, WF_Q_BY, WF_Q_BW, WF_Q_BH,
          WIFI_REQ_DISCARD);
}

/** @brief A press held at (@p x, @p y) for the 2s hold, then lifted.
 *  @return what the screen asked for */
static unsigned hold_at(int x, int y)
{
    send(TOUCH_EVENT_DOWN, x, y);
    for (int i = 0; i < HOLD_TICKS; ++i) {
        scr()->tick(TICK_S);
    }
    send(TOUCH_EVENT_UP, x, y);
    return wifi_screen_take();
}

/*
 * A row opens the password keyboard from 2px inside its corners and not
 * from the 4px between two rows; HIDDEN NETWORK opens the name keyboard
 * and CANCEL closes the list where they are drawn; HOLD 2s: INSTALL
 * installs from its corners and not from beside them.
 */
TEST_CASE(every_scan_row_and_the_hold_act_where_they_are_drawn)
{
    static const int k_rows[] = { 0, 7, 8, 15 };
    for (size_t r = 0; r < sizeof(k_rows) / sizeof(k_rows[0]); ++r) {
        const int i = k_rows[r];
        const int x = WF_ROW_X(i), y = WF_ROW_T(i);
        const int pts[4][2] = { { x + 2, y + 2 },
                                { x + WF_ROW_W - 3, y + 2 },
                                { x + 2, y + WF_ROW_H - 3 },
                                { x + WF_ROW_W - 3, y + WF_ROW_H - 3 } };
        for (int p = 0; p < 4; ++p) {
            fresh();
            scanned(16, -1);
            tap(pts[p][0], pts[p][1]);
            CHECK(wifi_screen_textkey()->open);
            CHECK(strstr(wifi_screen_textkey()->title, v.scan[i]) != NULL);
        }
        fresh();
        scanned(16, -1);
        tap(x + WF_ROW_W / 2, y + WF_ROW_H + 2);    /* between two rows */
        CHECK(!wifi_screen_textkey()->open);
    }

    fresh();
    scanned(3, -1);
    tap(WF_SCAN_BX(1) + 2, WF_SCAN_BY + 2);
    CHECK(wifi_screen_textkey()->open);
    CHECK(!wifi_screen_textkey()->secret);
    fresh();
    scanned(3, -1);
    tap(WF_SCAN_BX(2) + WF_SCAN_BW - 3, WF_SCAN_BY + WF_SCAN_BH - 3);
    tap(WF_BTN_CX(0), WF_BTN_CY(0));               /* the buttons again */
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_AP);

    const int ix = WF_INSTALL_X, iy = WF_Q_BY;
    const int in[2][2] = { { ix + 2, iy + 2 },
                           { ix + WF_Q_BW - 3, iy + WF_Q_BH - 3 } };
    for (int p = 0; p < 2; ++p) {
        setup_question();
        CHECK_EQ(hold_at(in[p][0], in[p][1]), WIFI_REQ_INSTALL);
    }
    const int out[2][2] = { { ix - 3, iy + WF_Q_BH / 2 },
                            { ix + WF_Q_BW / 2, iy + WF_Q_BH + 2 } };
    for (int p = 0; p < 2; ++p) {
        setup_question();
        CHECK_EQ(hold_at(out[p][0], out[p][1]) & WIFI_REQ_INSTALL, 0u);
    }
}

/* ------------------------------------------------------- the alert band */

/** @brief The views the band and the text cases draw. */
typedef enum {
    V_HOME, V_TALLEST, V_UNAVAILABLE, V_AP, V_JOINED, V_JOINING,
    V_ADDRESS, V_STARTING, V_SCANNING, V_SCAN_FULL, V_SCAN_EMPTY,
    V_Q_OLDER, V_Q_COPROC, V_Q_NEEDS, V_Q_INSTALLED, V_KEY_NAME,
    V_KEY_PASS, V_KEY_MARKS, V_COUNT
} view_id_t;

/** @brief Bring the screen to view @p id in language @p lang. */
static void pose(view_id_t id, ui_lang_t lang)
{
    fresh();
    ui_text_set_language(lang);
    switch (id) {
    case V_HOME:
        kept();
        break;
    case V_TALLEST:      /* every line the card holds at once */
        kept();
        v.code_on = false;
        v.save_failed = true;
        v.block = WIFI_WHY_RUN;
        v.off = WIFI_WHY_JOIN;
        v.join = WIFI_JOIN_NO_ADDRESS;
        show();
        break;
    case V_UNAVAILABLE:
        wifi_screen_set_unavailable();
        break;
    case V_AP:
        v.radio = WIFI_RADIO_AP;
        v.code_on = false;
        v.channel = 6;
        v.idle_left_s = 600;
        v.up = WIFI_UP_FAILED;
        show();
        break;
    case V_JOINED:
        kept();
        v.radio = WIFI_RADIO_JOINED;
        v.code_on = false;
        v.up = WIFI_UP_RECEIVING;
        v.received = 4063232u;
        v.total = 4063232u;
        show();
        break;
    case V_JOINING:
    case V_ADDRESS:
        kept();
        v.radio = (id == V_JOINING) ? WIFI_RADIO_JOINING
                                    : WIFI_RADIO_ADDRESS;
        show();
        break;
    case V_STARTING:
        v.radio = WIFI_RADIO_STARTING;
        show();
        break;
    case V_SCANNING:
        tap(WF_BTN_CX(2), WF_BTN_CY(2));
        v.radio = WIFI_RADIO_SCANNING;
        show();
        break;
    case V_SCAN_FULL:
        scanned(WIFI_SCAN_ROWS, 15);
        break;
    case V_SCAN_EMPTY:
        scanned(0, -1);
        break;
    case V_Q_OLDER:
        staged(WIFI_OFFER_OLDER, WIFI_GATE_DRAWING);
        break;
    case V_Q_COPROC:
        staged(WIFI_OFFER_REBUILD, WIFI_GATE_COPROC);
        break;
    case V_Q_NEEDS:
        staged(WIFI_OFFER_OLDER, WIFI_GATE_ARMED);
        break;
    case V_Q_INSTALLED:
        staged(WIFI_OFFER_INSTALLED, WIFI_GATE_OPEN);
        break;
    case V_KEY_NAME:
    case V_KEY_MARKS:
        scanned(3, -1);
        tap(WF_HIDDEN_CX, WF_SCAN_CY);
        if (id == V_KEY_MARKS) {
            const gfx_rect_t r = ui_textkey_key_rect(wifi_screen_textkey(),
                                                     UI_TK_LAYER);
            tap(r.x + r.w / 2, r.y + r.h / 2);
        }
        break;
    case V_KEY_PASS:
    case V_COUNT:
    default:
        scanned(16, 15);
        tap(WF_ROW_CX(15), WF_ROW_CY(15));
        break;
    }
}

/*
 * No view paints a pixel on the body rows from WF_FOOT down: the alert
 * band and its 4px stay the router's, in English and in German.
 */
TEST_CASE(nothing_lies_under_the_alert_band)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        for (int id = 0; id < V_COUNT; ++id) {
            pose((view_id_t)id, (ui_lang_t)lang);
            render();
            int ink = 0;
            for (int y = WF_FOOT; y < UI_BODY_H; ++y) {
                for (int x = 0; x < W; ++x) {
                    ink += (px(x, y) != ui_theme_color(UI_C_BG));
                }
            }
            if (ink != 0) {
                T_FAIL("view %d, language %d: %d px under the foot", id,
                       lang, ink);
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/* ---------------------------------------------------------- the texts */

/** @brief The pixels in @p r not of colour @p col: text where a card's
 *         ground should be. */
static int not_of(gfx_rect_t r, gfx_color_t col)
{
    int n = 0;
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            n += (px(x, y) != col);
        }
    }
    return n;
}

/*
 * The tallest card, every line at once, ends 16px or more above the
 * card's foot; the question's longest sentences end above its buttons; the
 * button labels fit their buttons in 10 x 20 with 8px each side, the row
 * labels their 240px; in English and German.
 */
TEST_CASE(the_longest_texts_stay_inside_their_cards_and_buttons)
{
    const gfx_font_t *label = &gfx_font_10x20;
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        pose(V_TALLEST, (ui_lang_t)lang);
        render();
        const gfx_color_t card = ui_theme_color(UI_C_PANEL);
        CHECK_EQ(not_of(gfx_rect_make(WF_CARD_X + 12, WF_FOOT - 16,
                                      WF_CARD_W - 24, 12), card), 0);
        static const view_id_t k_q[] = { V_Q_OLDER, V_Q_COPROC, V_Q_NEEDS,
                                         V_Q_INSTALLED };
        for (size_t q = 0; q < sizeof(k_q) / sizeof(k_q[0]); ++q) {
            pose(k_q[q], (ui_lang_t)lang);
            render();
            CHECK_EQ(not_of(gfx_rect_make(WF_Q_X + 12, WF_Q_BY - 12,
                                          WF_Q_W - 24, 10), card), 0);
        }

        static const ui_text_id_t k_wide[] = { TX_WF_AP, TX_WF_JOIN,
            TX_WF_CHOOSE, TX_WF_FORGET, TX_WF_CODE_ON, TX_WF_CODE_OFF,
            TX_WF_OFF, TX_WF_HOLD_INSTALL, TX_CANCEL };
        for (size_t i = 0; i < sizeof(k_wide) / sizeof(k_wide[0]); ++i) {
            CHECK(ui_text_fits(label, ui_tr(k_wide[i]), WF_Q_BW, WF_Q_BH,
                               8));
            CHECK(ui_text_fits(label, ui_tr(k_wide[i]), WF_BTN_W,
                               WF_BTN_H, 8));
        }
        static const ui_text_id_t k_scan[] = { TX_WF_RESCAN, TX_WF_HIDDEN,
                                               TX_CANCEL };
        for (size_t i = 0; i < sizeof(k_scan) / sizeof(k_scan[0]); ++i) {
            CHECK(ui_text_fits(label, ui_tr(k_scan[i]), WF_SCAN_BW,
                               WF_SCAN_BH, 8));
        }
        static const ui_text_id_t k_rows[] = { TX_WF_NETWORK,
            TX_WF_PASSWORD, TX_WF_ADDRESS, TX_WF_NAME, TX_WF_CODE };
        for (size_t i = 0; i < sizeof(k_rows) / sizeof(k_rows[0]); ++i) {
            CHECK(ui_text_fits(label, ui_tr(k_rows[i]), 240, 56, 0));
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/** @brief The rightmost column in body rows @p y0 to @p y1, x from @p xa
 *         to @p xb, that is not colour @p bg; -1 for none. */
static int right_ink(int y0, int y1, int xa, int xb, gfx_color_t bg)
{
    int hi = -1;
    for (int y = y0; y < y1; ++y) {
        for (int x = xa; x < xb; ++x) {
            if (px(x, y) != bg && x > hi) {
                hi = x;
            }
        }
    }
    return hi;
}

/** @brief The leftmost column in body rows @p y0 to @p y1, x from @p xa
 *         to @p xb, that is colour @p col; -1 for none. */
static int left_of(int y0, int y1, int xa, int xb, gfx_color_t col)
{
    for (int x = xa; x < xb; ++x) {
        for (int y = y0; y < y1; ++y) {
            if (px(x, y) == col) {
                return x;
            }
        }
    }
    return -1;
}

/*
 * A network name of 32 characters (WIFI_SSID_MAX) shows its 32nd
 * character: in the scan list in 12 x 24, left of its signal "-100dBm";
 * as the joined network's value in 20 x 36; in the German joining line;
 * and whole in the password keyboard's title.
 */
TEST_CASE(an_ssid_of_32_characters_shows_whole)
{
    char ssid[WIFI_SSID_MAX + 1];
    memset(ssid, 'M', WIFI_SSID_MAX);
    ssid[WIFI_SSID_MAX] = '\0';
    const gfx_color_t bg = ui_theme_color(UI_C_BG);

    pose(V_SCAN_FULL, UI_LANG_DE);
    render();
    const int rx = WF_ROW_X(15), ry = WF_ROW_T(15);
    const gfx_color_t row_bg = px(rx + 4, ry + 4);
    const int name_end = right_ink(ry + 4, ry + WF_ROW_H - 4, rx + 12,
                                   rx + 12 + 32 * 12 + 4, row_bg);
    CHECK(name_end >= rx + 12 + 31 * 12);
    const int dbm = left_of(ry + 4, ry + WF_ROW_H - 4, rx + 12 + 32 * 12,
                            rx + WF_ROW_W, ui_theme_color(UI_C_TEXT_DIM));
    CHECK(dbm > name_end + 4);
    CHECK(right_ink(ry + 4, ry + WF_ROW_H - 4, rx, rx + WF_ROW_W, row_bg)
          < rx + WF_ROW_W - 8);

    fresh();
    v.radio = WIFI_RADIO_JOINED;
    strcpy(v.net, ssid);
    show();
    render();
    const int val = WF_BTN_X + 240 + 8;
    CHECK(right_ink(16, 16 + 56, val, W, bg) >= val + 31 * 20);
    CHECK(right_ink(16, 16 + 56, val, W, bg) < val + 740);

    /* The radio-on rows, 56px from body y 16: the fourth, the access code's
     * 20 x 36 value centred at y 212, keeps its ink from y 194 to 230. */
    fresh();
    v.radio = WIFI_RADIO_JOINED;
    v.have_net = true;
    strcpy(v.net, "Workshop 2.4");
    v.code_on = false;
    v.ip[0] = 192;
    show();
    render();
    CHECK_EQ(right_ink(184, 194, val, W, bg), -1);
    CHECK(right_ink(218, 224, val, W, bg) >= val);

    fresh();
    ui_text_set_language(UI_LANG_DE);
    v.radio = WIFI_RADIO_JOINING;
    strcpy(v.net, ssid);
    show();
    render();
    char line[96];
    snprintf(line, sizeof(line), ui_tr(TX_WF_JOINING), ssid);
    const int cells = gfx_text_cells(line);
    CHECK(right_ink(16, 16 + 56, 0, W, bg) >= 16 + (cells - 1) * 12);
    CHECK(right_ink(16, 16 + 56, 0, W, bg) < W - 16);

    pose(V_KEY_PASS, UI_LANG_DE);
    CHECK(strstr(wifi_screen_textkey()->title, ssid) != NULL);
    ui_text_set_language(UI_LANG_EN);
}

/*
 * Sixteen networks fill both columns, the sixteenth at the foot of the
 * right one; none leaves no row to tap and says so, and SCAN AGAIN and
 * HIDDEN NETWORK still act.
 */
TEST_CASE(a_full_list_takes_16_rows_and_an_empty_one_none)
{
    CHECK_EQ(WIFI_SCAN_ROWS, 16);
    pose(V_SCAN_FULL, UI_LANG_EN);
    tap(WF_ROW_CX(15), WF_ROW_CY(15));
    CHECK(wifi_screen_textkey()->open);

    pose(V_SCAN_EMPTY, UI_LANG_EN);
    render();
    CHECK(not_of(gfx_rect_make(16, WF_ROW_Y, W - 32, 56),
                 ui_theme_color(UI_C_BG)) > 100);
    for (int i = 0; i < WIFI_SCAN_ROWS; ++i) {
        tap(WF_ROW_CX(i), WF_ROW_CY(i));
        CHECK(!wifi_screen_textkey()->open);
    }
    tap(WF_RESCAN_CX, WF_SCAN_CY);
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_SCAN);
    tap(WF_HIDDEN_CX, WF_SCAN_CY);
    CHECK(wifi_screen_textkey()->open);
}

/* WI-FI drives no output: it names no stop for the router to pass an
 * alert's press through to. */
TEST_CASE(wifi_has_no_stop_control)
{
    CHECK(scr()->safe_at == NULL);
}

int main(void)
{
    RUN(the_spots_are_a_finger_tall_and_end_above_the_alert_band);
    RUN(every_button_acts_inside_and_not_beside_its_rectangle);
    RUN(every_scan_row_and_the_hold_act_where_they_are_drawn);
    RUN(nothing_lies_under_the_alert_band);
    RUN(the_longest_texts_stay_inside_their_cards_and_buttons);
    RUN(an_ssid_of_32_characters_shows_whole);
    RUN(a_full_list_takes_16_rows_and_an_empty_one_none);
    RUN(wifi_has_no_stop_control);
    return test_summary("wifi_layout");
}
