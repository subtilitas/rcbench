/*
 * WI-FI and PANEL UPDATE: what the screens ask for, and when.
 *
 * Under test: which control asks for what with and without a kept home
 * network, with the radio's gate shut, the home network picked from a scan
 * and typed on the Wi-Fi keyboard, and INSTALL as a 2 s hold that a release,
 * a vanished image or a closed install gate ends without asking.  The
 * screens command nothing themselves; every test reads wifi_screen_take().
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "fw_image.h"
#include "fw_install.h"
#include "fw_upload.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "wa_session.h"
#include "wifi_screen.h"

#define SCREEN_W 800
#define SCREEN_H 480
#define BODY_H   432

/* Mirrored from wifi_screen.c. */
#define BTN_CX(i)    (16 + 180)
#define BTN_CY(i)    (16 + (i) * 68 + 28)
#define OFF_CX       (16 + 120)
#define OFF_CY       (360 + 28)
#define Q_BY         (12 + 408 - 80)
#define Q_CANCEL_CX  (32 + 120)
#define INSTALL_CX   (408 + 180)
#define Q_CY         (Q_BY + 32)
#define ROW_CX(i)    (((i) < 8 ? 16 : 408) + 100)
#define ROW_CY(i)    (16 + ((i) % 8) * 44 + 20)
#define RESCAN_CX    (16 + 120)
#define HIDDEN_CX    (272 + 128)
#define SCAN_CANCEL_CX (544 + 120)
#define SCAN_BY      (376 + 24)

#define TICK_S     (1.0f / 39.0f)
#define HOLD_TICKS ((int)(UI_HOLD_S / TICK_S) + 5)

static gfx_color_t s_px[SCREEN_W * SCREEN_H];
static gfx_canvas_t s_c;
static wifi_view_t v;

static const ui_screen_t *scr(void) { return wifi_screen(); }

static void show(void) { wifi_screen_set(&v); }

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

static void send(touch_event_type_t t, int x, int y)
{
    touch_event_t e = { .type = t,
                        .point = { .id = 1, .x = (int16_t)x, .y = (int16_t)y,
                                   .strength = 40 } };
    scr()->event(&e);
}

static void tap(int x, int y)
{
    send(TOUCH_EVENT_DOWN, x, y);
    send(TOUCH_EVENT_UP, x, y);
}

static void ticks(int n)
{
    for (int i = 0; i < n; ++i) {
        scr()->tick(TICK_S);
    }
}

/* Lit pixels in the body, as a check that a view drew something. */
static int render_lit(void)
{
    memset(s_px, 0, sizeof(s_px));
    wifi_screen_invalidate();
    scr()->render(&s_c, 0);
    int n = 0;
    for (int i = 0; i < SCREEN_W * BODY_H; ++i) {
        n += (s_px[i] != ui_theme_color(UI_C_BG));
    }
    return n;
}

/* A hash of the body: text on a card lights no more pixels, it changes
 * them. */
static uint32_t render_hash(void)
{
    (void)render_lit();
    uint32_t h = 2166136261u;
    for (int i = 0; i < SCREEN_W * BODY_H; ++i) {
        h = (h ^ (uint32_t)s_px[i]) * 16777619u;
    }
    return h;
}

static void staged(wifi_offer_t offer, wifi_gate_t gate)
{
    v.up = WIFI_UP_STAGED;
    v.offer = offer;
    v.gate = gate;
    strcpy(v.new_ver, "0.20.0");
    strcpy(v.run_ver, "0.19.0");
    show();
}

/* ------------------------------------------------------------ the mirror */

/* The screen's enums are the session's and the installer's, value for
 * value: the application casts one to the other. */
TEST_CASE(the_view_enums_mirror_the_session_and_the_installer)
{
    CHECK_EQ((int)WIFI_RADIO_OFF, (int)WA_ST_OFF);
    CHECK_EQ((int)WIFI_RADIO_STARTING, (int)WA_ST_STARTING);
    CHECK_EQ((int)WIFI_RADIO_AP, (int)WA_ST_AP);
    CHECK_EQ((int)WIFI_RADIO_SCANNING, (int)WA_ST_SCANNING);
    CHECK_EQ((int)WIFI_RADIO_JOINING, (int)WA_ST_JOINING);
    CHECK_EQ((int)WIFI_RADIO_ADDRESS, (int)WA_ST_ADDRESS);
    CHECK_EQ((int)WIFI_RADIO_JOINED, (int)WA_ST_JOINED);
    CHECK_EQ((int)WIFI_WHY_NONE, (int)WA_OFF_NONE);
    CHECK_EQ((int)WIFI_WHY_ARMED, (int)WA_OFF_ARMED);
    CHECK_EQ((int)WIFI_WHY_SUPPLY, (int)WA_OFF_SUPPLY);
    CHECK_EQ((int)WIFI_WHY_RUN, (int)WA_OFF_RUN);
    CHECK_EQ((int)WIFI_WHY_STOP, (int)WA_OFF_STOP);
    CHECK_EQ((int)WIFI_WHY_IDLE, (int)WA_OFF_IDLE);
    CHECK_EQ((int)WIFI_WHY_GUESSES, (int)WA_OFF_GUESSES);
    CHECK_EQ((int)WIFI_WHY_ASKED, (int)WA_OFF_ASKED);
    CHECK_EQ((int)WIFI_WHY_INSTALL, (int)WA_OFF_INSTALL);
    CHECK_EQ((int)WIFI_WHY_FAILED, (int)WA_OFF_FAILED);
    CHECK_EQ((int)WIFI_WHY_JOIN, (int)WA_OFF_JOIN);
    CHECK_EQ((int)WIFI_JOIN_OK, (int)WA_JOIN_OK);
    CHECK_EQ((int)WIFI_JOIN_PASSWORD, (int)WA_JOIN_PASSWORD);
    CHECK_EQ((int)WIFI_JOIN_NOT_FOUND, (int)WA_JOIN_NOT_FOUND);
    CHECK_EQ((int)WIFI_JOIN_NO_ADDRESS, (int)WA_JOIN_NO_ADDRESS);
    CHECK_EQ((int)WIFI_JOIN_LOST, (int)WA_JOIN_LOST);
    CHECK_EQ((int)WIFI_UP_IDLE, (int)FW_UP_IDLE);
    CHECK_EQ((int)WIFI_UP_RECEIVING, (int)FW_UP_RECEIVING);
    CHECK_EQ((int)WIFI_UP_STAGED, (int)FW_UP_STAGED);
    CHECK_EQ((int)WIFI_UP_FAILED, (int)FW_UP_FAILED);
    CHECK_EQ((int)WIFI_OFFER_NEWER, (int)FW_OFFER_NEWER);
    CHECK_EQ((int)WIFI_OFFER_OLDER, (int)FW_OFFER_OLDER);
    CHECK_EQ((int)WIFI_OFFER_REBUILD, (int)FW_OFFER_REBUILD);
    CHECK_EQ((int)WIFI_OFFER_INSTALLED, (int)FW_OFFER_INSTALLED);
    CHECK_EQ((int)WIFI_GATE_OPEN, (int)FW_GATE_OPEN);
    CHECK_EQ((int)WIFI_GATE_ARMED, (int)FW_GATE_ARMED);
    CHECK_EQ((int)WIFI_GATE_SUPPLY, (int)FW_GATE_SUPPLY);
    CHECK_EQ((int)WIFI_GATE_RUN, (int)FW_GATE_RUN);
    CHECK_EQ((int)WIFI_GATE_DRAWING, (int)FW_GATE_DRAWING);
    CHECK_EQ((int)WIFI_GATE_COPROC, (int)FW_GATE_COPROC);
    CHECK_EQ(WIFI_SSID_MAX, (int)WA_SSID_MAX);
    CHECK_EQ(WIFI_PASS_MAX, (int)WA_PASS_MAX);
    CHECK_EQ(WIFI_SCAN_ROWS, (int)WA_SCAN_MAX);
    CHECK_EQ(WIFI_VER_MAX, (int)FW_TEXT_MAX);
    CHECK_EQ(UI_TEXTKEY_SSID_MAX, (int)WA_SSID_MAX);
    CHECK_EQ(UI_TEXTKEY_PASS_MAX, (int)WA_PASS_MAX);
    CHECK_EQ(UI_TEXTKEY_PASS_MIN, (int)WA_PASS_MIN);
}

/* The numbers the texts carry are the session's. */
TEST_CASE(the_texts_name_the_session_limits)
{
    CHECK_EQ(WA_IDLE_MS, 10u * 60u * 1000u);
    CHECK(strstr(ui_tr(TX_WF_OFF_IDLE), "10 min") != NULL);
    CHECK_EQ(WA_GUESS_MAX, 16u);
    CHECK(strstr(ui_tr(TX_WF_OFF_GUESSES), "16 ") != NULL);
    CHECK_EQ(WA_ADDRESS_MS, 15000u);
    CHECK(strstr(ui_tr(TX_WF_JOIN_NO_ADDRESS), "15 s") != NULL);
    CHECK(strstr(ui_tr(TX_WF_HOLD_INSTALL), "2 s") != NULL);
    CHECK(UI_HOLD_S == 2.0f);
}

/* ------------------------------------------------------------ the modes */

/* Without a kept home network only the access point is offered. */
TEST_CASE(without_a_kept_network_only_the_access_point_is_offered)
{
    fresh();
    tap(BTN_CX(1), BTN_CY(1));               /* JOIN */
    tap(BTN_CX(3), BTN_CY(3));               /* FORGET */
    CHECK_EQ(wifi_screen_take(), 0u);
    tap(BTN_CX(0), BTN_CY(0));
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_AP);
    CHECK(render_lit() > 2000);
}

TEST_CASE(with_a_kept_network_both_are_offered_and_forget_is_there)
{
    fresh();
    v.have_net = true;
    strcpy(v.net, "Workshop");
    show();
    tap(BTN_CX(1), BTN_CY(1));
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_JOIN);
    tap(BTN_CX(0), BTN_CY(0));
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_AP);
    tap(BTN_CX(3), BTN_CY(3));
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_FORGET);
    /* After FORGET the application hands back a view without one. */
    v.have_net = false;
    v.net[0] = '\0';
    show();
    tap(BTN_CX(1), BTN_CY(1));
    tap(BTN_CX(3), BTN_CY(3));
    CHECK_EQ(wifi_screen_take(), 0u);
}

/* ARMED, the supply on or a log run open: nothing starts the radio; the
 * code switch and FORGET still work. */
TEST_CASE(a_shut_gate_offers_no_radio)
{
    static const wifi_why_t gates[] = { WIFI_WHY_ARMED, WIFI_WHY_SUPPLY,
                                        WIFI_WHY_RUN };
    for (size_t g = 0; g < sizeof(gates) / sizeof(gates[0]); ++g) {
        fresh();
        v.have_net = true;
        strcpy(v.net, "Workshop");
        v.block = gates[g];
        show();
        tap(BTN_CX(0), BTN_CY(0));
        tap(BTN_CX(1), BTN_CY(1));
        tap(BTN_CX(2), BTN_CY(2));
        CHECK_EQ(wifi_screen_take(), 0u);
        tap(BTN_CX(4), BTN_CY(4));
        CHECK_EQ(wifi_screen_take(), WIFI_REQ_CODE_OFF);
        tap(BTN_CX(3), BTN_CY(3));
        CHECK_EQ(wifi_screen_take(), WIFI_REQ_FORGET);
        CHECK(render_lit() > 2000);
    }
}

/* Wi-Fi that could not be set up at start: no control asks for anything,
 * and the screen says why in both languages. */
TEST_CASE(unavailable_wifi_offers_nothing_and_says_so)
{
    fresh();
    v.have_net = true;
    strcpy(v.net, "Workshop");
    v.block = WIFI_WHY_RUN;
    show();
    const uint32_t blocked = render_hash();
    wifi_screen_set_unavailable();               /* no view: none is needed */
    CHECK_EQ(wifi_screen_view()->block, WIFI_WHY_UNAVAILABLE);
    CHECK(!wifi_screen_view()->have_net);       /* the kept one was never read */
    for (int i = 0; i < 5; ++i) {
        tap(BTN_CX(i), BTN_CY(i));
    }
    CHECK_EQ(wifi_screen_take(), 0u);
    CHECK(render_lit() > 2000);
    CHECK(render_hash() != blocked);
    ui_text_set_language(UI_LANG_DE);
    CHECK(render_lit() > 2000);
    CHECK(strstr(ui_tr(TX_WF_UNAVAIL_1), "WLAN") != NULL);
    ui_text_set_language(UI_LANG_EN);
    CHECK(strstr(ui_tr(TX_WF_UNAVAIL_1), "unavailable") != NULL);
    /* A view handed in the same way draws the same. */
    const uint32_t direct = render_hash();
    memset(&v, 0, sizeof(v));
    v.code_on = true;
    v.block = WIFI_WHY_UNAVAILABLE;
    show();
    CHECK_EQ(render_hash(), direct);
}

/* A write of the settings that failed shows until the application says
 * one succeeded; the controls stay as they were. */
TEST_CASE(a_failed_save_shows_and_the_controls_stay)
{
    fresh();
    v.have_net = true;
    strcpy(v.net, "Workshop");
    show();
    const uint32_t kept = render_hash();
    v.save_failed = true;
    show();
    CHECK(render_hash() != kept);
    tap(BTN_CX(3), BTN_CY(3));
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_FORGET);
    tap(BTN_CX(4), BTN_CY(4));
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_CODE_OFF);
    v.save_failed = false;
    show();
    CHECK_EQ(render_hash(), kept);
}

TEST_CASE(the_code_switch_asks_for_the_other_state)
{
    fresh();
    tap(BTN_CX(4), BTN_CY(4));
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_CODE_OFF);
    v.code_on = false;
    show();
    tap(BTN_CX(4), BTN_CY(4));
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_CODE_ON);
    CHECK(render_lit() > 2000);
}

/* Each reason the radio went off has its line, and a view that draws. */
TEST_CASE(every_reason_and_join_failure_draws)
{
    for (int why = WIFI_WHY_NONE; why <= WIFI_WHY_UNAVAILABLE; ++why) {
        for (int j = WIFI_JOIN_OK; j <= WIFI_JOIN_LOST; ++j) {
            fresh();
            v.off = (wifi_why_t)why;
            v.join = (wifi_join_t)j;
            show();
            CHECK(render_lit() > 2000);
        }
    }
}

/* The radio up: its view, and WI-FI OFF. */
TEST_CASE(the_running_views_draw_and_wifi_off_asks_off)
{
    for (int r = WIFI_RADIO_STARTING; r <= WIFI_RADIO_JOINED; ++r) {
        if (r == WIFI_RADIO_SCANNING) {
            continue;
        }
        fresh();
        v.radio = (wifi_radio_t)r;
        strcpy(v.name, "rcbench-1A2B");
        strcpy(v.pass, "abcd2345");
        strcpy(v.code, "mnpq6789");
        strcpy(v.net, "Workshop");
        v.ip[0] = 192; v.ip[1] = 168; v.ip[2] = 1; v.ip[3] = 77;
        v.channel = 6;
        v.idle_left_s = 599;
        v.up = WIFI_UP_RECEIVING;
        v.received = 1000;
        v.total = 4000;
        show();
        CHECK(render_lit() > 2000);
        tap(BTN_CX(0), BTN_CY(0));           /* not a control here */
        CHECK_EQ(wifi_screen_take(), 0u);
        tap(OFF_CX, OFF_CY);
        CHECK_EQ(wifi_screen_take(), WIFI_REQ_OFF);
    }
}

/* ---------------------------------------------- the home network, typed */

static void type_wifi(const char *s)
{
    for (; *s != '\0'; ++s) {
        /* Lower-case letters and digits only: the keyboard's own tests
         * cover the layers. */
        const char *lower = "1234567890qwertyuiopasdfghjkl- zxcvbnm";
        const char *p = strchr(lower, *s);
        CHECK(p != NULL);
        if (p == NULL) {
            return;
        }
        ui_textkey_t probe;
        memset(&probe, 0, sizeof(probe));
        ui_textkey_open_wifi(&probe, gfx_rect_make(8, 8, 784, 416), "", "",
                             true);
        const gfx_rect_t r = ui_textkey_key_rect(&probe, (int)(p - lower));
        tap(r.x + r.w / 2, r.y + r.h / 2);
    }
}

static void key(int k)
{
    ui_textkey_t probe;
    memset(&probe, 0, sizeof(probe));
    ui_textkey_open_wifi(&probe, gfx_rect_make(8, 8, 784, 416), "", "", true);
    const gfx_rect_t r = ui_textkey_key_rect(&probe, k);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

static void scan_result(void)
{
    v.radio = WIFI_RADIO_OFF;
    v.scanned = true;
    v.scan_n = 3;
    strcpy(v.scan[0], "Workshop");
    v.rssi[0] = -48;
    strcpy(v.scan[1], "Neighbour 5G");
    v.rssi[1] = -71;
    strcpy(v.scan[2], "Werkstatt \xc3\xbc");    /* UTF-8 */
    v.rssi[2] = -80;
    show();
}

/* CHOOSE scans; a row picked asks for its password, which must be 8
 * characters at least, and OK keeps the pair. */
TEST_CASE(a_network_picked_from_the_scan_is_kept_with_its_password)
{
    fresh();
    tap(BTN_CX(2), BTN_CY(2));
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_SCAN);
    v.radio = WIFI_RADIO_SCANNING;
    show();
    CHECK(render_lit() > 1000);
    tap(ROW_CX(0), ROW_CY(0));               /* nothing listed yet */
    CHECK_EQ(wifi_screen_take(), 0u);
    scan_result();
    CHECK(render_lit() > 2000);
    tap(RESCAN_CX, SCAN_BY);
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_SCAN);
    tap(ROW_CX(2), ROW_CY(2));
    CHECK(render_lit() > 2000);              /* the keyboard */
    type_wifi("abc2345");                    /* 7 */
    key(UI_TK_OK);
    CHECK_EQ(wifi_screen_take(), 0u);         /* refused */
    type_wifi("6");
    key(UI_TK_OK);
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_SAVE_NET);
    char ssid[WIFI_SSID_MAX + 1], pass[WIFI_PASS_MAX + 1];
    wifi_screen_net(ssid, pass);
    CHECK_STR_EQ(ssid, "Werkstatt \xc3\xbc");
    CHECK_STR_EQ(pass, "abc23456");
    /* Back on the buttons. */
    v.have_net = true;
    strcpy(v.net, ssid);
    v.scanned = false;
    v.scan_n = 0;
    show();
    tap(BTN_CX(1), BTN_CY(1));
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_JOIN);
}

/* A hidden network: its name typed first, then its password; CANCEL goes
 * back to the list and keeps nothing. */
TEST_CASE(a_hidden_network_is_typed_and_cancel_keeps_nothing)
{
    fresh();
    tap(BTN_CX(2), BTN_CY(2));
    (void)wifi_screen_take();
    scan_result();
    tap(HIDDEN_CX, SCAN_BY);
    key(UI_TK_OK);                           /* an empty name: refused */
    type_wifi("lab");
    key(UI_TK_OK);                           /* the name: the password next */
    type_wifi("passw0rd");
    key(UI_TK_CANCEL);
    CHECK_EQ(wifi_screen_take(), 0u);
    /* The list again: the hidden network once more, to the end. */
    tap(HIDDEN_CX, SCAN_BY);
    type_wifi("lab");
    key(UI_TK_OK);
    type_wifi("passw0rd");
    key(UI_TK_OK);
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_SAVE_NET);
    char ssid[WIFI_SSID_MAX + 1], pass[WIFI_PASS_MAX + 1];
    wifi_screen_net(ssid, pass);
    CHECK_STR_EQ(ssid, "lab");
    CHECK_STR_EQ(pass, "passw0rd");
    /* CANCEL on the list goes back to the buttons. */
    tap(BTN_CX(2), BTN_CY(2));
    (void)wifi_screen_take();
    scan_result();
    tap(SCAN_CANCEL_CX, SCAN_BY);
    tap(BTN_CX(0), BTN_CY(0));
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_AP);
}

/* ------------------------------------------------------- the question */

TEST_CASE(install_is_a_2_s_hold_and_a_release_before_asks_nothing)
{
    fresh();
    staged(WIFI_OFFER_NEWER, WIFI_GATE_OPEN);
    CHECK(render_lit() > 2000);
    send(TOUCH_EVENT_DOWN, INSTALL_CX, Q_CY);
    ticks(HOLD_TICKS / 2);
    send(TOUCH_EVENT_UP, INSTALL_CX, Q_CY);
    ticks(HOLD_TICKS);
    CHECK_EQ(wifi_screen_take(), 0u);
    /* The full hold asks once. */
    send(TOUCH_EVENT_DOWN, INSTALL_CX, Q_CY);
    ticks(HOLD_TICKS);
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_INSTALL);
    ticks(HOLD_TICKS);
    CHECK_EQ(wifi_screen_take(), 0u);
    send(TOUCH_EVENT_UP, INSTALL_CX, Q_CY);
    CHECK_EQ(wifi_screen_take(), 0u);
    /* A tap is no hold. */
    tap(INSTALL_CX, Q_CY);
    ticks(HOLD_TICKS);
    CHECK_EQ(wifi_screen_take(), 0u);
}

/* Sliding off the hold abandons it; sliding back does not resume it. */
TEST_CASE(a_press_that_slides_off_install_asks_nothing)
{
    fresh();
    staged(WIFI_OFFER_NEWER, WIFI_GATE_OPEN);
    send(TOUCH_EVENT_DOWN, INSTALL_CX, Q_CY);
    ticks(HOLD_TICKS / 2);
    send(TOUCH_EVENT_MOVE, INSTALL_CX, Q_CY - 200);
    send(TOUCH_EVENT_MOVE, INSTALL_CX, Q_CY);
    ticks(HOLD_TICKS);
    send(TOUCH_EVENT_UP, INSTALL_CX, Q_CY);
    CHECK_EQ(wifi_screen_take(), 0u);
}

/* STOP or ARM during the question drops the image under the hold; the
 * hold ends there and asks nothing, even with the finger still down. */
TEST_CASE(an_image_that_goes_away_under_the_hold_ends_it)
{
    fresh();
    staged(WIFI_OFFER_NEWER, WIFI_GATE_OPEN);
    send(TOUCH_EVENT_DOWN, INSTALL_CX, Q_CY);
    ticks(HOLD_TICKS / 2);
    v.up = WIFI_UP_IDLE;                     /* STOP: the application drops it */
    v.off = WIFI_WHY_STOP;
    show();
    ticks(HOLD_TICKS);
    CHECK_EQ(wifi_screen_take(), 0u);
    /* A new image under the same finger is no hold either. */
    staged(WIFI_OFFER_NEWER, WIFI_GATE_OPEN);
    ticks(HOLD_TICKS);
    send(TOUCH_EVENT_UP, INSTALL_CX, Q_CY);
    CHECK_EQ(wifi_screen_take(), 0u);
    CHECK(render_lit() > 2000);
}

/* The install gate closing mid-hold ends it; a shut gate offers none. */
TEST_CASE(a_closed_install_gate_ends_and_refuses_the_hold)
{
    fresh();
    staged(WIFI_OFFER_NEWER, WIFI_GATE_OPEN);
    send(TOUCH_EVENT_DOWN, INSTALL_CX, Q_CY);
    ticks(HOLD_TICKS / 2);
    v.gate = WIFI_GATE_DRAWING;
    show();
    ticks(HOLD_TICKS);
    send(TOUCH_EVENT_UP, INSTALL_CX, Q_CY);
    CHECK_EQ(wifi_screen_take(), 0u);
    static const wifi_gate_t shut[] = { WIFI_GATE_ARMED, WIFI_GATE_SUPPLY,
                                        WIFI_GATE_RUN, WIFI_GATE_DRAWING,
                                        WIFI_GATE_COPROC };
    for (size_t g = 0; g < sizeof(shut) / sizeof(shut[0]); ++g) {
        staged(WIFI_OFFER_NEWER, shut[g]);
        CHECK(render_lit() > 2000);
        send(TOUCH_EVENT_DOWN, INSTALL_CX, Q_CY);
        ticks(HOLD_TICKS);
        send(TOUCH_EVENT_UP, INSTALL_CX, Q_CY);
        CHECK_EQ(wifi_screen_take(), 0u);
    }
}

/* Older and the same version in another build are offered; the build that
 * runs is not. */
TEST_CASE(the_running_build_is_not_offered)
{
    static const wifi_offer_t offers[] = { WIFI_OFFER_OLDER,
                                           WIFI_OFFER_REBUILD };
    for (size_t o = 0; o < 2; ++o) {
        fresh();
        staged(offers[o], WIFI_GATE_OPEN);
        CHECK(render_lit() > 2000);
        send(TOUCH_EVENT_DOWN, INSTALL_CX, Q_CY);
        ticks(HOLD_TICKS);
        CHECK_EQ(wifi_screen_take(), WIFI_REQ_INSTALL);
        send(TOUCH_EVENT_UP, INSTALL_CX, Q_CY);
    }
    fresh();
    staged(WIFI_OFFER_INSTALLED, WIFI_GATE_OPEN);
    CHECK(render_lit() > 2000);
    send(TOUCH_EVENT_DOWN, INSTALL_CX, Q_CY);
    ticks(HOLD_TICKS);
    send(TOUCH_EVENT_UP, INSTALL_CX, Q_CY);
    CHECK_EQ(wifi_screen_take(), 0u);
    tap(Q_CANCEL_CX, Q_CY);
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_DISCARD);
}

/* The question comes before a keyboard that was open when the image
 * arrived. */
TEST_CASE(an_image_arriving_under_the_keyboard_is_asked_about_first)
{
    fresh();
    tap(BTN_CX(2), BTN_CY(2));
    (void)wifi_screen_take();
    scan_result();
    tap(ROW_CX(0), ROW_CY(0));
    staged(WIFI_OFFER_NEWER, WIFI_GATE_OPEN);
    tap(Q_CANCEL_CX, Q_CY);
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_DISCARD);
}

/* Leaving switches the radio off and drops a staged image; leaving for the
 * write keeps the image the write takes. */
TEST_CASE(leaving_switches_off_and_drops_the_image_unless_installing)
{
    fresh();
    staged(WIFI_OFFER_NEWER, WIFI_GATE_OPEN);
    scr()->leave();
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_OFF | WIFI_REQ_DISCARD);

    fresh();
    staged(WIFI_OFFER_NEWER, WIFI_GATE_OPEN);
    send(TOUCH_EVENT_DOWN, INSTALL_CX, Q_CY);
    ticks(HOLD_TICKS);
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_INSTALL);
    v.up = WIFI_UP_IDLE;                     /* the write took it */
    show();
    scr()->leave();
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_OFF);
    /* Entered again, a leave drops an image once more. */
    scr()->enter();
    staged(WIFI_OFFER_NEWER, WIFI_GATE_OPEN);
    scr()->leave();
    CHECK_EQ(wifi_screen_take(), WIFI_REQ_OFF | WIFI_REQ_DISCARD);
}

/* A lost touch event ends the hold without asking. */
TEST_CASE(a_cancelled_press_ends_the_hold)
{
    fresh();
    staged(WIFI_OFFER_NEWER, WIFI_GATE_OPEN);
    send(TOUCH_EVENT_DOWN, INSTALL_CX, Q_CY);
    ticks(HOLD_TICKS / 2);
    scr()->cancel();
    ticks(HOLD_TICKS);
    CHECK_EQ(wifi_screen_take(), 0u);
}

/* ------------------------------------------------------- PANEL UPDATE */

TEST_CASE(the_write_screen_closes_only_after_a_failure)
{
    gfx_canvas_t full;
    gfx_canvas_init(&full, s_px, SCREEN_W, SCREEN_H, SCREEN_W);
    const ui_screen_t *u = update_screen();
    u->reset();
    update_view_t w = { .state = UPDATE_WRITING, .written = 65536u,
                        .total = 1900000u };
    strcpy(w.version, "0.20.0");
    update_screen_set(&w);
    memset(s_px, 0, sizeof(s_px));
    u->render(&full, 0);
    int n = 0;
    for (int i = 0; i < SCREEN_W * SCREEN_H; ++i) {
        n += (s_px[i] != 0u);
    }
    CHECK(n > 10000);
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = 144, .y = 434 } };
    u->event(&e);
    e.type = TOUCH_EVENT_UP;
    u->event(&e);
    CHECK(!update_screen_take_close());
    w.state = UPDATE_FAILED;
    update_screen_set(&w);
    u->render(&full, 0);
    e.type = TOUCH_EVENT_DOWN;
    u->event(&e);
    e.type = TOUCH_EVENT_UP;
    u->event(&e);
    CHECK(update_screen_take_close());
    CHECK(!update_screen_take_close());
    w.state = UPDATE_RESTARTING;
    w.written = w.total;
    update_screen_set(&w);
    u->render(&full, 1);
    CHECK_STR_EQ(u->title, "PANEL UPDATE");
    CHECK_STR_EQ(scr()->title, "WI-FI");
}

/* WF_JOBS_1 and WF_JOBS_2 are one sentence: wrapped as one, it takes the
 * card's 44 cells twice, with no line of one word between its parts. */
TEST_CASE(the_first_sentence_is_wrapped_once_in_two_lines)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        CHECK_EQ(wifi_screen_jobs_lines(), 2);
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(the_view_enums_mirror_the_session_and_the_installer);
    RUN(the_first_sentence_is_wrapped_once_in_two_lines);
    RUN(the_texts_name_the_session_limits);
    RUN(without_a_kept_network_only_the_access_point_is_offered);
    RUN(with_a_kept_network_both_are_offered_and_forget_is_there);
    RUN(a_shut_gate_offers_no_radio);
    RUN(unavailable_wifi_offers_nothing_and_says_so);
    RUN(a_failed_save_shows_and_the_controls_stay);
    RUN(the_code_switch_asks_for_the_other_state);
    RUN(every_reason_and_join_failure_draws);
    RUN(the_running_views_draw_and_wifi_off_asks_off);
    RUN(a_network_picked_from_the_scan_is_kept_with_its_password);
    RUN(a_hidden_network_is_typed_and_cancel_keeps_nothing);
    RUN(install_is_a_2_s_hold_and_a_release_before_asks_nothing);
    RUN(a_press_that_slides_off_install_asks_nothing);
    RUN(an_image_that_goes_away_under_the_hold_ends_it);
    RUN(a_closed_install_gate_ends_and_refuses_the_hold);
    RUN(the_running_build_is_not_offered);
    RUN(an_image_arriving_under_the_keyboard_is_asked_about_first);
    RUN(leaving_switches_off_and_drops_the_image_unless_installing);
    RUN(a_cancelled_press_ends_the_hold);
    RUN(the_write_screen_closes_only_after_a_failure);
    return test_summary("wifi_screen");
}
