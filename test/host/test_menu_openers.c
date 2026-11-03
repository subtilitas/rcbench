/**
 * @file test_menu_openers.c
 * @brief The menu's openers through the router: SERVO's caps button to SUPPLY's
 * LIMITS, DUT's AS5600 button to SENSORS, UPDATES' and LOGS' buttons to WI-FI
 * and the TARGET row from a programmer asking for a port.  Each one's way back by the
 * home tag, its refusal while the target refuses, a busy target, what a later
 * entry by a tile finds, and the rule that no text names a path.
 *
 * SPDX-License-Identifier: MIT
 */

#include "logview_spots.h"
#include "nav_rig.h"
#include "sensors_pane.h"
#include "supply_spots.h"
#include "upd_screen.h"
#include "wifi_screen.h"
#include "wifi_spots.h"

/* Panel coordinates of the controls, from the suites' spots. */
#define CAPS_TEST  SV_CAPS_X, (UI_BAND_H + SV_CAPS_TEST_Y)
#define CAPS_DUT   SV_CAPS_X, (UI_BAND_H + SV_CAPS_DUT_Y)
#define ENC_DUT    SV_CAPS_X, (UI_BAND_H + SV_ENC_OPEN_Y)
/** @brief UPDATES' button that opens WI-FI: on the card, under its two lines. */
#define UPD_NET    750, (UI_BAND_H + 120)
/** @brief LOGS' WI-FI button. */
#define LOGS_WIFI  LV_WIFI_CX, (UI_BAND_H + LV_FOOT_CY)
/** @brief SETUP's WI-FI tile, the 5th. */
#define WIFI_TILE  4
/** @brief The leave warning's buttons (test_settings_leave.c). */
#define WARN_Y      (UI_PANEL_H - UI_ALERT_H - 4 - 24)
#define WARN_STAY_X 136
#define WARN_DROP_X ((10 + (UI_PANEL_W - 40) / 3 + 10) + 128)

/** @brief What the mem store holds. */
static float s_kept[SETTING_COUNT];
/** @brief s_kept holds a save. */
static bool s_have_kept;

/** @brief The store's load.  @param v takes the values  @param n their count
 *  @return whether a save is held */
static bool kept_load(float *v, int n)
{
    if (s_have_kept) {
        memcpy(v, s_kept, (size_t)n * sizeof(float));
    }
    return s_have_kept;
}

/** @brief The store's save.  @param v the values  @param n their count
 *  @return true */
static bool kept_save(const float *v, int n)
{
    memcpy(s_kept, v, (size_t)n * sizeof(float));
    s_have_kept = true;
    return true;
}

/** @brief A store that keeps the values in RAM, so an edit counts as unsaved. */
static const settings_store_t s_openers_store = { kept_load, kept_save, NULL,
                                                  NULL };

/** @brief The home tag, tapped. */
static void tap_home(void)
{
    const gfx_rect_t home =
        ui_home_tag_rect(ui_router_title(ui_router_current()));
    tap(home.x + home.w / 2, home.y + home.h / 2);
}

/** @brief The bench's ARMED bit as the router and SERVO read it.
 *  @param armed ARMED */
static void set_armed(bool armed)
{
    ui_bench_status_t st = *ui_router_status();
    st.armed = armed;
    ui_router_set_status(&st);
    servo_screen_set_armed(armed);
}

/** @brief The menu, nothing unsaved, nothing waiting. */
static void start(void)
{
    settings_set_store(&s_openers_store);
    settings_init();
    fresh();
    to_overview();
    drain_servo();
}

/** @brief SERVO on task @p tab's x, from the tile. */
static void servo_on(int tab)
{
    ui_router_goto(SCREEN_SERVO);
    tap(tab, UI_BAND_H + SV_TAB_Y);
}

/** @brief SERVO's SETTINGS on DUT. */
static void servo_on_dut(void)
{
    servo_on(SV_TAB_SETTINGS_X);
    tap(SV_PAGE_X(2), UI_BAND_H + SV_PAGE_TAB_Y);
    CHECK_EQ(servo_screen_settings_page(), 2);
}

/** @brief UPDATES with no network saved and nothing running. */
static void updates_no_net(void)
{
    static upd_view_t v;
    memset(&v, 0, sizeof(v));
    snprintf(v.running, sizeof(v.running), "%s", "0.23.3");
    upd_screen_set(&v);
    ui_router_goto(SCREEN_UPDATES);
}

/* ------------------------------------------------------- caps, SERVO > TEST */

/* The caps button opens SUPPLY on SETTINGS' LIMITS, from any tab it was left
 * on, and the home tag comes back to TEST. */
TEST_CASE(the_caps_button_opens_limits_and_the_tag_returns_to_test)
{
    start();
    ui_router_goto(SCREEN_SUPPLY);
    supply_screen_open_pdmini();                /* the tab left on PD MINI */
    CHECK_EQ(supply_screen_settings_tab(), 1);
    ui_router_goto(SCREEN_OVERVIEW);

    servo_on(SV_TAB_TEST_X);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_TEST);
    tap(CAPS_TEST);
    CHECK_EQ(ui_router_current(), SCREEN_SUPPLY);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
    CHECK_EQ(supply_screen_settings_tab(), 0);
    CHECK_EQ(ui_router_parent(SCREEN_SUPPLY), SCREEN_SERVO);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_SERVO);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_TEST);
}

/* The same button on DUT comes back to DUT: task and page. */
TEST_CASE(the_caps_button_on_dut_returns_to_dut)
{
    start();
    servo_on_dut();
    tap(CAPS_DUT);
    CHECK_EQ(ui_router_current(), SCREEN_SUPPLY);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_SETTINGS);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_SERVO);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_SETTINGS);
    CHECK_EQ(servo_screen_settings_page(), 2);
}

/* ARMED: no press, the bench stays ARMED and no DISARM is sent.  A bench that
 * arms under the finger keeps it: the release opens nothing.  So does a
 * finger that slid off and a release it did not make. */
TEST_CASE(the_caps_button_refuses_while_armed_and_a_slid_off_release)
{
    start();
    servo_on(SV_TAB_TEST_X);
    set_armed(true);
    drain_servo();
    const uint32_t nav = ui_router_navigations();
    tap(CAPS_TEST);
    CHECK_EQ(ui_router_navigations(), nav);
    CHECK_EQ(ui_router_current(), SCREEN_SERVO);
    CHECK_EQ(servo_took().kind, SERVO_CMD_NONE);

    set_armed(false);
    touch(CAPS_TEST, TOUCH_EVENT_DOWN, 1);
    set_armed(true);
    touch(CAPS_TEST, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_navigations(), nav);

    /* A press made while ARMED opens nothing either, SAFE by its release. */
    touch(CAPS_TEST, TOUCH_EVENT_DOWN, 2);
    set_armed(false);
    touch(CAPS_TEST, TOUCH_EVENT_UP, 2);
    CHECK_EQ(ui_router_navigations(), nav);

    touch(CAPS_TEST, TOUCH_EVENT_DOWN, 1);
    touch(SV_CAPS_X, UI_BAND_H + SV_CAPS_TEST_Y - 60, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_navigations(), nav);
    touch_event_t up = { .type = TOUCH_EVENT_UP, .flags = TOUCH_FLAG_NO_TAP,
                         .point = { .id = 1, .x = SV_CAPS_X,
                                    .y = UI_BAND_H + SV_CAPS_TEST_Y } };
    touch(CAPS_TEST, TOUCH_EVENT_DOWN, 1);
    ui_router_event(&up);
    CHECK_EQ(ui_router_navigations(), nav);
}

/* A keypad open on the target closes first, as on any way out; then the tag
 * returns. */
TEST_CASE(a_keypad_open_on_supply_closes_before_the_tag_returns)
{
    start();
    servo_on(SV_TAB_TEST_X);
    tap(CAPS_TEST);
    CHECK_EQ(ui_router_current(), SCREEN_SUPPLY);
    tap(SU_ROW_L_X, UI_BAND_H + SU_VMAX_Y);
    CHECK(supply_screen_keypad()->open);
    tap_home();
    CHECK(!supply_screen_keypad()->open);
    CHECK_EQ(ui_router_current(), SCREEN_SUPPLY);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_SERVO);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_TEST);
}

/* The opener holds for its one entry: SUPPLY's tile later goes home, and
 * opens LIVE; SERVO's tile opens LIVE. */
TEST_CASE(a_later_tile_finds_no_opener)
{
    start();
    servo_on(SV_TAB_TEST_X);
    tap(CAPS_TEST);
    tap_home();
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    ui_router_goto(SCREEN_SUPPLY);
    CHECK_EQ(supply_screen_task(), (int)UI_WB_LIVE);
    CHECK_EQ(ui_router_parent(SCREEN_SUPPLY), SCREEN_OVERVIEW);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_SERVO);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_LIVE);
}

/* ------------------------------------------------------- AS5600, SERVO > DUT */

/* DUT's button opens SENSORS on the AS5600 entry with no tiles above it; the
 * tag comes back to DUT.  SETUP's tile later shows the tiles and climbs. */
TEST_CASE(the_as5600_button_opens_its_entry_and_the_tag_returns_to_dut)
{
    start();
    servo_on_dut();
    tap(ENC_DUT);
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK_EQ(settings_screen_category(), (int)SET_CAT_SENSORS);
    CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_AS5600);
    CHECK_EQ(ui_router_parent(SCREEN_SETUP), SCREEN_SERVO);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_SERVO);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_SETTINGS);
    CHECK_EQ(servo_screen_settings_page(), 2);

    tap_home();
    ui_router_goto(SCREEN_SETUP);
    CHECK_EQ(settings_screen_category(), -1);
    CHECK_EQ(ui_router_parent(SCREEN_SETUP), SCREEN_OVERVIEW);
    int x = 0, y = 0;
    hub_point(settings_hub_cat_tile(SET_CAT_SENSORS), &x, &y);
    tap(x, y);
    CHECK_EQ(settings_screen_category(), (int)SET_CAT_SENSORS);
    tap_home();                                  /* the list: the tiles */
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK_EQ(settings_screen_category(), -1);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/* An unsaved edit: the tag asks, STAY keeps the list, DISCARD goes back to
 * DUT, as the leave warning does from a tile. */
TEST_CASE(an_unsaved_edit_asks_before_the_tag_returns_to_dut)
{
    start();
    servo_on_dut();
    tap(ENC_DUT);
    settings_set(SET_ENC_CENTRE, settings_get(SET_ENC_CENTRE) + 1.0f);
    CHECK(settings_dirty());
    tap_home();
    CHECK(settings_screen_warning());
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    tap(WARN_STAY_X, WARN_Y);
    CHECK(!settings_screen_warning());
    CHECK_EQ(settings_screen_category(), (int)SET_CAT_SENSORS);
    tap_home();
    CHECK(settings_screen_warning());
    tap(WARN_DROP_X, WARN_Y);
    CHECK_EQ(ui_router_current(), SCREEN_SERVO);
    CHECK_EQ(servo_screen_settings_page(), 2);
    CHECK(!settings_dirty());
}

/* ARMED: no press and no DISARM. */
TEST_CASE(the_as5600_button_refuses_while_armed)
{
    start();
    servo_on_dut();
    set_armed(true);
    drain_servo();
    const uint32_t nav = ui_router_navigations();
    tap(ENC_DUT);
    tap(CAPS_DUT);
    CHECK_EQ(ui_router_navigations(), nav);
    CHECK_EQ(servo_took().kind, SERVO_CMD_NONE);

    set_armed(false);
    touch(ENC_DUT, TOUCH_EVENT_DOWN, 1);
    set_armed(true);
    touch(ENC_DUT, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_navigations(), nav);
    set_armed(false);
}

/* ------------------------------------------------------------ WI-FI, UPDATES */

/* With no network the button opens WI-FI, whose home tag comes back; leaving
 * asks the radio off and drops a staged image as any way off WI-FI does. */
TEST_CASE(the_network_button_opens_wifi_and_the_tag_returns_to_updates)
{
    start();
    updates_no_net();
    tap(UPD_NET);
    CHECK_EQ(ui_router_current(), SCREEN_WIFI);
    CHECK_EQ(ui_router_parent(SCREEN_WIFI), SCREEN_UPDATES);
    (void)wifi_screen_take();
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    const unsigned req = wifi_screen_take();
    CHECK((req & WIFI_REQ_OFF) != 0u);
    CHECK((req & WIFI_REQ_DISCARD) != 0u);
    CHECK_EQ(ui_router_parent(SCREEN_UPDATES), SCREEN_SETUP);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
}

/* WI-FI's levels climb first: the network list, then UPDATES.  A staged image:
 * the first tag answers CANCEL, the second leaves. */
TEST_CASE(wifi_levels_and_a_staged_image_climb_before_the_tag_returns)
{
    start();
    updates_no_net();
    tap(UPD_NET);
    tap(WF_BTN_CX(2), UI_BAND_H + WF_BTN_CY(2));      /* CHOOSE */
    const uint32_t nav = ui_router_navigations();
    tap_home();
    CHECK_EQ(ui_router_navigations(), nav);
    CHECK_EQ(ui_router_current(), SCREEN_WIFI);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);

    tap(UPD_NET);
    static wifi_view_t v;
    memset(&v, 0, sizeof(v));
    v.up = WIFI_UP_STAGED;
    v.offer = WIFI_OFFER_NEWER;
    v.gate = WIFI_GATE_OPEN;
    wifi_screen_set(&v);
    (void)wifi_screen_take();
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_WIFI);
    CHECK((wifi_screen_take() & WIFI_REQ_DISCARD) != 0u);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    memset(&v, 0, sizeof(v));
    wifi_screen_set(&v);
}

/* A network saved, no Wi-Fi at this start, or a job running: no way through. */
TEST_CASE(the_network_button_shows_only_with_no_network_and_nothing_running)
{
    start();
    static upd_view_t v;
    memset(&v, 0, sizeof(v));
    v.have_net = true;
    snprintf(v.net, sizeof(v.net), "%s", "workshop");
    upd_screen_set(&v);
    ui_router_goto(SCREEN_UPDATES);
    const uint32_t nav = ui_router_navigations();
    tap(UPD_NET);
    CHECK_EQ(ui_router_navigations(), nav);

    memset(&v, 0, sizeof(v));
    v.unavailable = true;
    upd_screen_set(&v);
    tap(UPD_NET);
    CHECK_EQ(ui_router_navigations(), nav);

    memset(&v, 0, sizeof(v));
    v.phase = UPD_PH_JOINING;
    upd_screen_set(&v);
    tap(UPD_NET);
    CHECK_EQ(ui_router_navigations(), nav);

    /* The press is asked again at its release. */
    memset(&v, 0, sizeof(v));
    upd_screen_set(&v);
    touch(UPD_NET, TOUCH_EVENT_DOWN, 1);
    v.have_net = true;
    upd_screen_set(&v);
    touch(UPD_NET, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_navigations(), nav);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
}

/* ARMED, WI-FI opens as its tile opens it and its own buttons refuse; the
 * tag still returns. */
TEST_CASE(armed_the_network_button_opens_wifi_as_the_tile_does)
{
    start();
    updates_no_net();
    set_armed(true);
    tap(UPD_NET);
    CHECK_EQ(ui_router_current(), SCREEN_WIFI);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    set_armed(false);
}

/** @brief WI-FI on top from its SETUP tile. */
static void wifi_from_tile(void)
{
    ui_router_goto(SCREEN_SETUP);
    int x = 0;
    int y = 0;
    hub_point(WIFI_TILE, &x, &y);
    tap(x, y);
    CHECK_EQ(ui_router_current(), SCREEN_WIFI);
}

/* WI-FI's home is SETUP: the tile opens it and the tag returns to the hub, with
 * the radio down on the way. */
TEST_CASE(the_wifi_tile_opens_wifi_and_the_tag_returns_to_setup)
{
    start();
    wifi_from_tile();
    CHECK_EQ(ui_router_parent(SCREEN_WIFI), SCREEN_SETUP);
    CHECK(settings_screen_opens(SCREEN_WIFI));
    CHECK_EQ(wifi_screen_take(), 0u);      /* opening starts no radio */
    wifi_view_t v;
    memset(&v, 0, sizeof(v));
    v.code_on = true;
    v.radio = WIFI_RADIO_AP;
    wifi_screen_set(&v);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK_EQ(settings_screen_category(), -1);
    const unsigned req = wifi_screen_take();
    CHECK((req & WIFI_REQ_OFF) != 0u);
    CHECK((req & WIFI_REQ_DISCARD) != 0u);
    CHECK((req & (WIFI_REQ_AP | WIFI_REQ_JOIN | WIFI_REQ_SCAN)) == 0u);
    /* The way back in brings no radio with it. */
    wifi_from_tile();
    CHECK_EQ(wifi_screen_take(), 0u);
}

/* An opener holds for one entry: the tile after UPDATES' or LOGS' button that
 * returned leads to SETUP, and each button after the tile to its own screen. */
TEST_CASE(wifi_returns_to_what_opened_it_this_time)
{
    start();
    updates_no_net();
    tap(UPD_NET);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    tap_home();
    wifi_from_tile();
    CHECK_EQ(ui_router_parent(SCREEN_WIFI), SCREEN_SETUP);

    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_LOGS);
    tap(LOGS_WIFI);
    CHECK_EQ(ui_router_current(), SCREEN_WIFI);
    CHECK_EQ(ui_router_parent(SCREEN_WIFI), SCREEN_LOGS);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
    wifi_from_tile();
    CHECK_EQ(ui_router_parent(SCREEN_WIFI), SCREEN_SETUP);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);

    updates_no_net();
    tap(UPD_NET);
    CHECK_EQ(ui_router_parent(SCREEN_WIFI), SCREEN_UPDATES);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
}

/* WI-FI draws no strip and takes no tab: where the tabs were is nothing, on the
 * start screen and with the radio up. */
TEST_CASE(wifi_has_no_strip_and_its_rows_start_under_the_band)
{
    start();
    wifi_from_tile();
    const uint32_t nav = ui_router_navigations();
    (void)wifi_screen_take();
    for (int x = 100; x < 1000; x += 200) {
        tap(x, UI_BAND_H + 8);
    }
    CHECK_EQ(ui_router_navigations(), nav);
    CHECK_EQ(WF_BTN_Y, 16);
    /* The first button's top row takes the press the strip's foot once had. */
    tap(WF_BTN_CX(0), UI_BAND_H + WF_BTN_Y + 1);
    CHECK((wifi_screen_take() & WIFI_REQ_AP) != 0u);
}

/* What the application reads when an upload's install closes: PANEL UPDATE's
 * CLOSE leads to WI-FI's parent, read with WI-FI on top
 * (firmware/panel/main/panel_update.c).  SETUP for the tile, LOGS and UPDATES
 * for their buttons, each a screen with a way home of its own. */
TEST_CASE(the_way_back_from_a_failed_upload_is_what_opened_wifi)
{
    static const struct {
        int            from;   /* 0 tile, 1 LOGS, 2 UPDATES */
        ui_screen_id_t back;
        ui_screen_id_t then;
    } k_routes[] = {
        { 0, SCREEN_SETUP,   SCREEN_OVERVIEW },
        { 1, SCREEN_LOGS,    SCREEN_OVERVIEW },
        { 2, SCREEN_UPDATES, SCREEN_SETUP },
    };
    for (size_t i = 0u; i < sizeof(k_routes) / sizeof(k_routes[0]); ++i) {
        start();
        if (k_routes[i].from == 0) {
            wifi_from_tile();
        } else if (k_routes[i].from == 1) {
            ui_router_goto(SCREEN_LOGS);
            tap(LOGS_WIFI);
        } else {
            updates_no_net();
            tap(UPD_NET);
        }
        CHECK_EQ(ui_router_current(), SCREEN_WIFI);
        const ui_screen_id_t back = ui_router_parent(SCREEN_WIFI);
        CHECK_EQ(back, k_routes[i].back);
        ui_router_goto(SCREEN_UPDATE);
        (void)wifi_screen_take();
        ui_router_goto(back);
        CHECK_EQ(ui_router_current(), k_routes[i].back);
        tap_home();
        CHECK_EQ(ui_router_current(), k_routes[i].then);
    }
}

/* ---------------------------------------------------- TARGET, the programmer */

/* A programmer asks for a port: TARGET is one tap to the entry that holds
 * SERVO TESTER, and the home tag comes back to the session. */
TEST_CASE(target_opens_the_tester_entry_from_a_programmer_asking_for_a_port)
{
    start();
    static const uint8_t ports[LINK_PG_PORTS_MAX] = { 13u, 26u, 27u, 28u };
    outputs_screen_set_prog_ports(4u, ports);
    ui_bench_status_t st = *ui_router_status();
    st.capabilities = LINK_CAP_SERVO_PWM;
    ui_router_set_status(&st);
    servo_screen_set_drive(0u, 2u, true);
    ui_router_goto(SCREEN_SERVO);
    tap(SV_TAB_PROGRAM_X, UI_BAND_H + SV_TAB_Y);
    tap(SV_PROG_ROW_X, UI_BAND_H + SV_PROG_ROW_Y(0));
    CHECK(kst_panel_is_open());
    tap(SV_TARGET_X, UI_BAND_H + SV_TARGET_Y);
    CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
    CHECK_EQ(outputs_screen_port_shown(), 0);
    CHECK_EQ(ui_router_parent(SCREEN_OUTPUTS), SCREEN_SERVO);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_SERVO);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_PROGRAM);

    /* With no session TARGET opens SERVO PWM's entry, as the row reads. */
    ui_router_goto(SCREEN_OVERVIEW);
    servo_on(SV_TAB_TEST_X);
    tap(SV_TARGET_X, UI_BAND_H + SV_TARGET_Y);
    CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
    CHECK_EQ(outputs_screen_port_shown(), -1);
}

/* ------------------------------------------------------------- the text rule */

/*
 * No text names a path of two levels or more: an opener or one tile's name
 * says it.  These are the ones that stay, each with its reason in the row.
 * The browser's texts (UI_WEB_TEXT) are outside this table: a browser cannot
 * open a screen of the panel.
 */
TEST_CASE(no_text_names_a_path_but_the_listed_ones)
{
    static const ui_text_id_t k_kept[] = {
        TX_TEXTKEY_NAME_CHAR, /* the characters a name refuses, no path */
        TX_SUP_ASK_OFF,       /* a tab and a row of the screen it is on */
        TX_HP_SUM_HINT,       /* a button and its row of the same session */
    };
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        for (int id = 0; id < (int)TX_COUNT; ++id) {
            const char *t = ui_tr_in((ui_lang_t)lang, (ui_text_id_t)id);
            if (t == NULL || strstr(t, " > ") == NULL) {
                continue;
            }
            bool kept = false;
            for (size_t k = 0u; k < sizeof(k_kept) / sizeof(k_kept[0]); ++k) {
                kept = kept || (int)k_kept[k] == id;
            }
            CHECK(kept);
            if (!kept) {
                fprintf(stderr, "      a path in: %s\n", t);
            }
        }
    }
}

/* A tile's line names content: no count, on the hub and on the menu. */
TEST_CASE(every_tile_line_names_content_and_no_count)
{
    start();
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        static const ui_text_id_t k_hub[] = {
            TX_SET_HUB_PANEL, TX_SET_HUB_SENSORS, TX_SET_HUB_OUTPUTS,
            TX_SET_HUB_CARD, TX_SET_HUB_WIFI, TX_SET_HUB_UPDATES,
            TX_SET_HUB_LAYOUT,
        };
        for (size_t i = 0u; i < sizeof(k_hub) / sizeof(k_hub[0]); ++i) {
            const char *t = ui_tr(k_hub[i]);
            CHECK(t[0] != '\0');
            CHECK(strpbrk(t, "0123456789%") == NULL);
        }
        for (int i = 0; i < 8; ++i) {
            CHECK(strpbrk(overview_screen_tile_line(i), "%") == NULL);
        }
    }
    ui_text_set_language(UI_LANG_EN);
    /* The guide says what the two tiles hold now. */
    CHECK(strstr(TR(HG_OV_SETUP), "interfaces") == NULL);
    CHECK(strstr(TR(HG_OV_LOGS), "WI-FI") != NULL);
    CHECK(strstr(TR(HG_OV_LOGS), "tabs") == NULL);
    CHECK(strstr(TR(HG_OV_SETUP), "Wi-Fi") != NULL);
    CHECK(strstr(TR(OV_LOGS), "Wi-Fi") == NULL);
    CHECK(strstr(TR(OV_SETUP), "Wi-Fi") != NULL);
}

int main(void)
{
    RUN(the_caps_button_opens_limits_and_the_tag_returns_to_test);
    RUN(the_caps_button_on_dut_returns_to_dut);
    RUN(the_caps_button_refuses_while_armed_and_a_slid_off_release);
    RUN(a_keypad_open_on_supply_closes_before_the_tag_returns);
    RUN(a_later_tile_finds_no_opener);
    RUN(the_as5600_button_opens_its_entry_and_the_tag_returns_to_dut);
    RUN(an_unsaved_edit_asks_before_the_tag_returns_to_dut);
    RUN(the_as5600_button_refuses_while_armed);
    RUN(the_network_button_opens_wifi_and_the_tag_returns_to_updates);
    RUN(wifi_levels_and_a_staged_image_climb_before_the_tag_returns);
    RUN(the_network_button_shows_only_with_no_network_and_nothing_running);
    RUN(armed_the_network_button_opens_wifi_as_the_tile_does);
    RUN(the_wifi_tile_opens_wifi_and_the_tag_returns_to_setup);
    RUN(wifi_returns_to_what_opened_it_this_time);
    RUN(wifi_has_no_strip_and_its_rows_start_under_the_band);
    RUN(the_way_back_from_a_failed_upload_is_what_opened_wifi);
    RUN(target_opens_the_tester_entry_from_a_programmer_asking_for_a_port);
    RUN(no_text_names_a_path_but_the_listed_ones);
    RUN(every_tile_line_names_content_and_no_count);
    return test_summary("menu_openers");
}
