/*
 * The IMU screen (imu_screen.h): SETUP's IMU tile, the found list, the
 * calibration's hold, ZERO, IMU SETTINGS and the lines of its card.
 *
 * The failures this is about: a scan whose list never opens, a tap on a
 * device that binds another one, a calibration started by a brush of the
 * glass, a refused scan that leaves no way on, a line that runs past its
 * field in German, and a screen that leaves for the menu rather than SETUP.
 *
 * SPDX-License-Identifier: MIT
 */
#include "settings_rig.h"

#include <string.h>

#include "imu_screen.h"
#include "imu_screen_int.h"
#include "splash_screen.h"
#include "touch_feed.h"

/* The router on SETUP's tiles, the splash done, the model fresh. */
static void router_on(void)
{
    ui_theme_set(UI_THEME_DARK);
    fresh_hub();
    ui_text_set_language(UI_LANG_EN);
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_set_alert(NULL);
    feed_reset();
}

/* The IMU screen opened from its tile, showing @p v. */
static void on_imu(const imu_view_t *v)
{
    router_on();
    const int tile = settings_hub_tiles() - 1;
    CHECK_EQ(settings_hub_tile_screen(tile), SCREEN_IMU);
    tap(tile_x(tile), tile_y(tile));
    CHECK_EQ(ui_router_current(), SCREEN_IMU);
    imu_screen_set(v);
}

/* A tap on @p r of the body. */
static void tap_rect(gfx_rect_t r)
{
    tap(r.x + r.w / 2, UI_BAND_H + r.y + r.h / 2);
}

/* The view of a connected sensor at 50Hz, 3.95V, the angles 1, -2 and 3°. */
static imu_view_t view_connected(void)
{
    imu_view_t v;
    memset(&v, 0, sizeof v);
    v.link = IMU_LINK_CONNECTED;
    snprintf(v.version, sizeof v.version, "2.0-20100601");
    snprintf(v.bound_addr, sizeof v.bound_addr, "98D3:31:F5A1B2");
    snprintf(v.bound_name, sizeof v.bound_name, "HC-06");
    v.r.valid = true;
    v.r.taken_ms = 1000u;
    v.r.angle[0] = 1.0f;
    v.r.angle[1] = -2.0f;
    v.r.angle[2] = 3.0f;
    v.r.rate_dhz = 500u;
    v.r.battery_v = 3.95f;
    v.r.temp_c = 24.0f;
    return v;
}

/* The next command, or kind NONE. */
static imu_cmd_t took(void)
{
    imu_cmd_t c = { IMU_CMD_NONE, 0u };
    (void)imu_screen_take(&c);
    return c;
}

static bool hint_shows(int id)
{
    const char *a = ui_router_alert();
    return a != NULL && strcmp(a, ui_tr((ui_text_id_t)id)) == 0;
}

/* The home tag, tapped. */
static void tap_home(void)
{
    const gfx_rect_t home = ui_home_tag_rect(ui_router_title(ui_router_current()));
    tap(home.x + home.w / 2, home.y + home.h / 2);
}

TEST_CASE(the_tile_opens_the_screen_and_home_returns_to_setup)
{
    imu_view_t v = view_connected();
    on_imu(&v);
    CHECK_STR_EQ(ui_router_title(SCREEN_IMU), "IMU");
    CHECK_EQ(ui_router_parent(SCREEN_IMU), SCREEN_SETUP);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK_EQ(settings_screen_category(), -1);     /* on the tiles */
    /* SETUP, come back to by IMU's home tag, leaves for OVERVIEW: the
     * return is no opening. */
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    /* IMU SETTINGS opens SENSORS on the IMU entry; its home tag comes back. */
    on_imu(&v);
    tap_rect(imu_screen_row_rect(IMS_ROW_SETTINGS));
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK_EQ(settings_screen_category(), SET_CAT_SENSORS);
    CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_IMU);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_IMU);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

TEST_CASE(scan_posts_the_command_and_the_list_shows_the_found_devices)
{
    imu_view_t v = view_connected();
    on_imu(&v);
    CHECK(!imu_screen_take(NULL));
    tap_rect(imu_screen_row_rect(IMS_ROW_SCAN));
    CHECK_EQ(took().kind, IMU_CMD_SCAN);
    CHECK(imu_screen()->covered());
    /* The scan under way: the heading says so, no device takes a tap. */
    v.link = IMU_LINK_SCANNING;
    imu_screen_set(&v);
    CHECK_STR_EQ(imu_screen_line(IMS_L_HEAD), "SCANNING: UP TO 40s");
    tap_rect(imu_screen_choice_rect(0));
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    tap_rect(imu_screen_row_rect(IMS_ROW_RESCAN));
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    /* Two found. */
    v.link = IMU_LINK_READY;
    v.found_n = 2u;
    snprintf(v.found[0].addr, sizeof v.found[0].addr, "98D3:31:F5A1B2");
    snprintf(v.found[0].name, sizeof v.found[0].name, "HC-06");
    snprintf(v.found[1].addr, sizeof v.found[1].addr, "12:34:56");
    imu_screen_set(&v);
    CHECK_STR_EQ(imu_screen_line(IMS_L_HEAD), "FOUND 2: TAP ONE TO BIND IT");
    ui_router_render(&s_c, 0);
    /* None found, and why. */
    v.found_n = 0u;
    v.err = IMU_ERR_NONE_FOUND;
    imu_screen_set(&v);
    CHECK_STR_EQ(imu_screen_line(IMS_L_HEAD),
                 "NO DEVICE FOUND: IS THE SENSOR ON?");
    v.err = IMU_ERR_NONE;
    imu_screen_set(&v);
    CHECK_STR_EQ(imu_screen_line(IMS_L_HEAD), "NO SCAN YET");
    /* SCAN AGAIN posts another; CLOSE and the home tag close the list. */
    tap_rect(imu_screen_row_rect(IMS_ROW_RESCAN));
    CHECK_EQ(took().kind, IMU_CMD_SCAN);
    tap_rect(imu_screen_row_rect(IMS_ROW_CLOSE));
    CHECK(!imu_screen()->covered());
    tap_rect(imu_screen_row_rect(IMS_ROW_SCAN));
    (void)took();
    CHECK(imu_screen()->covered());
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_IMU);
    CHECK(!imu_screen()->covered());
    /* SCAN takes no press while the HC-05 is not there. */
    v.link = IMU_LINK_NO_MODULE;
    imu_screen_set(&v);
    tap_rect(imu_screen_row_rect(IMS_ROW_SCAN));
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    CHECK(!imu_screen()->covered());
    ui_router_render(&s_c, 0);
}

TEST_CASE(a_tap_on_a_device_posts_bind_with_its_index)
{
    imu_view_t v = view_connected();
    v.link = IMU_LINK_READY;
    on_imu(&v);
    tap_rect(imu_screen_row_rect(IMS_ROW_SCAN));
    CHECK_EQ(took().kind, IMU_CMD_SCAN);
    v.found_n = 3u;
    for (int i = 0; i < 3; ++i) {
        snprintf(v.found[i].addr, sizeof v.found[i].addr, "1:2:%d", i);
    }
    imu_screen_set(&v);
    tap_rect(imu_screen_choice_rect(2));
    const imu_cmd_t c = took();
    CHECK_EQ(c.kind, IMU_CMD_BIND);
    CHECK_EQ(c.arg, 2u);
    CHECK(!imu_screen()->covered());      /* the list closed */
    /* A row past the list, and a slide off a row, bind nothing. */
    tap_rect(imu_screen_row_rect(IMS_ROW_SCAN));
    (void)took();
    tap_rect(imu_screen_choice_rect(3));
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    const gfx_rect_t r = imu_screen_choice_rect(1);
    finger(FEED_LONE, r.x + 20, UI_BAND_H + r.y + r.h / 2);
    glide(FEED_LONE, r.x + 20, UI_BAND_H + r.y + r.h + 40, 4);
    lift(FEED_LONE);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    /* A second finger while one is down does nothing. */
    finger(FEED_LONE, r.x + 20, UI_BAND_H + r.y + r.h / 2);
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 7, .x = 700, .y = 300 } };
    imu_screen()->event(&e);
    imu_screen()->event(NULL);
    lift(FEED_LONE);
    CHECK_EQ(took().kind, IMU_CMD_BIND);
    /* Out of range rects are empty. */
    CHECK_EQ(imu_screen_choice_rect(-1).w, 0);
    CHECK_EQ(imu_screen_choice_rect((int)IMU_FOUND_MAX).w, 0);
    CHECK_EQ(imu_screen_row_rect(IMS_ROW_COUNT).w, 0);
}

TEST_CASE(inq_refused_offers_connect_to_any_sensor)
{
    imu_view_t v = view_connected();
    v.link = IMU_LINK_READY;
    on_imu(&v);
    tap_rect(imu_screen_row_rect(IMS_ROW_ANY));
    CHECK_EQ(took().kind, IMU_CMD_NONE);          /* not offered */
    tap_rect(imu_screen_row_rect(IMS_ROW_SCAN));
    (void)took();
    v.err = IMU_ERR_INQ_REFUSED;
    v.err_code = 0x1Fu;
    v.inq_refused = true;
    imu_screen_set(&v);
    CHECK_STR_EQ(imu_screen_line(IMS_L_HEAD),
                 "THE HC-05 REFUSED THE SCAN: ERROR:(1F)");
    tap_rect(imu_screen_row_rect(IMS_ROW_LIST_ANY));
    CHECK_EQ(took().kind, IMU_CMD_ANY);
    CHECK(!imu_screen()->covered());
    /* And on the main view, under SCAN. */
    tap_rect(imu_screen_row_rect(IMS_ROW_ANY));
    CHECK_EQ(took().kind, IMU_CMD_ANY);
    CHECK_STR_EQ(imu_screen_line(IMS_L_ERR),
                 "THE HC-05 REFUSED THE SCAN: ERROR:(1F)");
    ui_router_render(&s_c, 0);
    /* Bound to any sensor: the card says so. */
    snprintf(v.bound_addr, sizeof v.bound_addr, "*");
    v.bound_name[0] = '\0';
    imu_screen_set(&v);
    CHECK_STR_EQ(imu_screen_line(IMS_L_SENSOR), "SENSOR: ANY IN RANGE");
    CHECK_STR_EQ(imu_screen_line(IMS_L_ADDR), "AT+CMODE=1, PIN 1234");
}

/* A finger held @p s seconds on the calibration's hold, in 0.25s frames. */
static void hold_for(float s)
{
    const gfx_rect_t h = imu_screen_hold_rect();
    const int x = h.x + h.w / 2, y = UI_BAND_H + h.y + h.h / 2;
    finger(FEED_LONE, x, y);
    for (; s >= 0.25f; s -= 0.25f) {
        ui_router_tick(0.25f);
    }
    if (s > 0.0f) {
        ui_router_tick(s);
    }
    lift(FEED_LONE);
}

TEST_CASE(calibrate_needs_a_2s_hold_and_the_band_hint_on_a_short_press)
{
    imu_view_t v = view_connected();
    on_imu(&v);
    hold_for(1.999f);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    CHECK(hint_shows(TX_HOLD_HINT_IMU_CAL));
    /* 2s: the command, once. */
    ui_router_set_alert(NULL);
    hold_for(2.25f);
    CHECK_EQ(took().kind, IMU_CMD_CALIBRATE);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    for (int i = 0; i < 10; ++i) {
        ui_router_tick(0.05f);            /* the flash */
        ui_router_render(&s_c, i & 1);
    }
    /* Running: the line counts down, the hold takes no press, and SCAN
     * takes none either: the calibration keeps the UART. */
    v.cal = IMU_CAL_RUNNING;
    v.cal_left_ms = 2001u;
    imu_screen_set(&v);
    CHECK(!ims_row_enabled(IMS_ROW_SCAN));
    tap_rect(imu_screen_row_rect(IMS_ROW_SCAN));
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    CHECK_STR_EQ(imu_screen_line(IMS_L_CAL), "KEEP IT STILL AND LEVEL FOR 3s");
    v.cal_left_ms = 2000u;
    imu_screen_set(&v);
    CHECK_STR_EQ(imu_screen_line(IMS_L_CAL), "KEEP IT STILL AND LEVEL FOR 2s");
    hold_for(2.25f);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    v.cal = IMU_CAL_DONE;
    imu_screen_set(&v);
    CHECK_STR_EQ(imu_screen_line(IMS_L_CAL),
                 "ACCELEROMETER CALIBRATED AND SAVED");
    v.cal = IMU_CAL_FAILED;
    imu_screen_set(&v);
    CHECK_STR_EQ(imu_screen_line(IMS_L_CAL),
                 "CALIBRATION ENDED WITHOUT FRAMES: TRY AGAIN");
    /* A hold that loses the link under it ends unfired; so do a slide off
     * and a lost touch stream. */
    v.cal = IMU_CAL_NONE;
    imu_screen_set(&v);
    const gfx_rect_t h = imu_screen_hold_rect();
    const int x = h.x + h.w / 2, y = UI_BAND_H + h.y + h.h / 2;
    finger(FEED_LONE, x, y);
    ui_router_tick(1.0f);
    v.link = IMU_LINK_NO_FRAMES;
    v.r.valid = false;
    imu_screen_set(&v);
    ui_router_tick(1.5f);
    lift(FEED_LONE);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    v = view_connected();
    imu_screen_set(&v);
    finger(FEED_LONE, x, y);
    ui_router_tick(1.0f);
    glide(FEED_LONE, x, y + h.h, 4);
    ui_router_tick(1.5f);
    lift(FEED_LONE);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    finger(FEED_LONE, x, y);
    ui_router_tick(1.0f);
    imu_screen()->cancel();
    ui_router_tick(1.5f);
    lift(FEED_LONE);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    /* Leaving in a hold ends it. */
    finger(FEED_LONE, x, y);
    ui_router_tick(1.0f);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_tick(1.5f);
    lift(FEED_LONE);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
}

TEST_CASE(zero_posts_zero_and_the_lines_show_the_zeroed_angles)
{
    imu_view_t v = view_connected();
    on_imu(&v);
    CHECK_STR_EQ(imu_screen_line(IMS_L_X), "X  +1.0°");
    CHECK_STR_EQ(imu_screen_line(IMS_L_Y), "Y  -2.0°");
    CHECK_STR_EQ(imu_screen_line(IMS_L_Z), "Z  +3.0°");
    tap_rect(imu_screen_row_rect(IMS_ROW_UNZERO));
    CHECK_EQ(took().kind, IMU_CMD_NONE);         /* nothing to clear */
    tap_rect(imu_screen_row_rect(IMS_ROW_ZERO));
    CHECK_EQ(took().kind, IMU_CMD_ZERO);
    v.r.zeroed = true;
    v.r.zero[0] = 1.0f;
    v.r.zero[1] = -2.0f;
    v.r.zero[2] = -177.0f;
    imu_screen_set(&v);
    CHECK_STR_EQ(imu_screen_line(IMS_L_X), "X  +0.0°");
    CHECK_STR_EQ(imu_screen_line(IMS_L_Y), "Y  +0.0°");
    CHECK_STR_EQ(imu_screen_line(IMS_L_Z), "Z  -180.0°");
    tap_rect(imu_screen_row_rect(IMS_ROW_UNZERO));
    CHECK_EQ(took().kind, IMU_CMD_UNZERO);
    /* No reading: dashes, and ZERO takes no press. */
    v.r.valid = false;
    imu_screen_set(&v);
    CHECK_STR_EQ(imu_screen_line(IMS_L_X), "X  ---");
    tap_rect(imu_screen_row_rect(IMS_ROW_ZERO));
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    /* The rate and the age; the battery hidden above 6.0V. */
    v = view_connected();
    imu_screen_set(&v);
    CHECK_STR_EQ(imu_screen_line(IMS_L_RATE), "RATE 50.0Hz  AGE 0ms");
    ui_router_tick(0.02f);
    CHECK_STR_EQ(imu_screen_line(IMS_L_RATE), "RATE 50.0Hz  AGE 20ms");
    CHECK_STR_EQ(imu_screen_line(IMS_L_BATTERY), "BATTERY 3.95V");
    v.r.battery_v = 655.35f;
    imu_screen_set(&v);
    CHECK_STR_EQ(imu_screen_line(IMS_L_BATTERY), "");
    v.r.battery_v = NAN;
    v.r.valid = false;
    imu_screen_set(&v);
    CHECK_STR_EQ(imu_screen_line(IMS_L_BATTERY), "");
    CHECK_STR_EQ(imu_screen_line(IMS_L_RATE), "RATE 50.0Hz  NO ANGLE");
    CHECK(imu_screen_line(-1) == NULL);
    CHECK(imu_screen_line(IMS_L_COUNT) == NULL);
    /* Four commands are kept; a fifth drops the oldest. */
    v = view_connected();
    imu_screen_set(&v);
    tap_rect(imu_screen_row_rect(IMS_ROW_SCAN));
    tap_rect(imu_screen_row_rect(IMS_ROW_CLOSE));
    for (int i = 0; i < 4; ++i) {
        tap_rect(imu_screen_row_rect(IMS_ROW_ZERO));
    }
    int n = 0;
    while (took().kind == IMU_CMD_ZERO) {
        ++n;
    }
    CHECK_EQ(n, 4);
    /* A ZERO press while the calibration runs: CALIBRATE is off, the press
     * on ZERO stays and acts on the lift. */
    v = view_connected();
    v.cal = IMU_CAL_RUNNING;
    v.cal_left_ms = 2000u;
    imu_screen_set(&v);
    const gfx_rect_t z = imu_screen_row_rect(IMS_ROW_ZERO);
    finger(FEED_LONE, z.x + z.w / 2, UI_BAND_H + z.y + z.h / 2);
    ui_router_tick(0.1f);
    lift(FEED_LONE);
    CHECK_EQ(took().kind, IMU_CMD_ZERO);
    /* The operation's line is in the warning colour. */
    v.err = IMU_ERR_NONE_FOUND;
    imu_screen_set(&v);
    char buf[96];
    gfx_color_t ink = 0;
    CHECK(ims_line(IMS_L_ERR, buf, sizeof buf, &ink) != NULL);
    CHECK_EQ(ink, UI_WARN);
}

/* The card's state line: each of the 8 link states, in both languages. */
TEST_CASE(the_state_line_names_every_link_state)
{
    static const char *const en[] = {
        "IMU OFF IN IMU SETTINGS", "NO HC-05", "NO SENSOR BOUND", "SCANNING",
        "BINDING", "BOUND", "CONNECTED 50Hz", "NO FRAMES",
    };
    static const char *const de[] = {
        "IMU AUS IN IMU-OPTIONEN", "KEIN HC-05", "KEIN SENSOR GEBUNDEN",
        "SUCHE LÄUFT", "BINDEN LÄUFT", "GEBUNDEN", "VERBUNDEN 50Hz",
        "KEINE FRAMES",
    };
    imu_view_t v = view_connected();
    on_imu(&v);
    for (int k = 0; k <= (int)IMU_LINK_NO_FRAMES; ++k) {
        v.link = (imu_link_t)k;
        imu_screen_set(&v);
        ui_text_set_language(UI_LANG_EN);
        CHECK_STR_EQ(imu_screen_line(IMS_L_STATE), en[k]);
        ui_text_set_language(UI_LANG_DE);
        CHECK_STR_EQ(imu_screen_line(IMS_L_STATE), de[k]);
    }
    ui_text_set_language(UI_LANG_EN);
    /* The sensor's and the HC-05's lines. */
    CHECK_STR_EQ(imu_screen_line(IMS_L_SENSOR), "SENSOR: HC-06");
    CHECK_STR_EQ(imu_screen_line(IMS_L_ADDR), "98D3:31:F5A1B2");
    CHECK_STR_EQ(imu_screen_line(IMS_L_HC05), "HC-05: 2.0-20100601");
    v.bound_name[0] = '\0';
    v.version[0] = '\0';
    imu_screen_set(&v);
    CHECK_STR_EQ(imu_screen_line(IMS_L_SENSOR), "SENSOR: NO NAME");
    CHECK_STR_EQ(imu_screen_line(IMS_L_HC05), "HC-05: ---");
    v.bound_addr[0] = '\0';
    imu_screen_set(&v);
    CHECK_STR_EQ(imu_screen_line(IMS_L_SENSOR), "SENSOR: NONE BOUND");
    /* Each error of an operation in words. */
    static const struct { imu_err_t e; uint8_t code; const char *t; } k[] = {
        { IMU_ERR_NONE, 0u, "" },
        { IMU_ERR_NO_AT, 0u,
          "THE HC-05 DOES NOT ANSWER AT: CHECK ITS WIRES AND KEY" },
        { IMU_ERR_TIMEOUT, 0u, "THE HC-05 DID NOT ANSWER IN TIME" },
        { IMU_ERR_REFUSED, 0u, "THE HC-05 ANSWERED FAIL" },
        { IMU_ERR_REFUSED, 0x1Du, "THE HC-05 ANSWERED ERROR:(1D)" },
        { IMU_ERR_PAIR, 0u,
          "PAIRING FAILED: IS THE SENSOR ON AND IN RANGE?" },
        { IMU_ERR_NONE_FOUND, 0u, "NO DEVICE FOUND: IS THE SENSOR ON?" },
        { IMU_ERR_ARG, 0u,
          "THE HC-05 DOES NOT ACCEPT THIS ADDRESS OR PIN" },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
        v.err = k[i].e;
        v.err_code = k[i].code;
        imu_screen_set(&v);
        CHECK_STR_EQ(imu_screen_line(IMS_L_ERR), k[i].t);
    }
}

/* Every label in its control, every card line in the card, every left
 * line in 2 lines of its column: both languages, the longest values. */
TEST_CASE(every_string_fits_its_field)
{
    imu_view_t v = view_connected();
    on_imu(&v);
    const gfx_font_t *label = &gfx_font_10x20, *body = &gfx_font_12x24;
    static const ui_text_id_t k_buttons[] = {
        TX_IMU_SCAN, TX_IMU_ANY, TX_IMU_CAL_HOLD, TX_IMU_SETTINGS,
    };
    static const ui_text_id_t k_half[] = { TX_IMU_ZERO, TX_IMU_UNZERO };
    static const ui_text_id_t k_list[] = { TX_IMU_RESCAN, TX_IMU_ANY,
                                           TX_IMU_CLOSE };
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (size_t i = 0; i < sizeof k_buttons / sizeof k_buttons[0]; ++i) {
            CHECK(gfx_text_width(label, ui_tr(k_buttons[i]), 1)
                  <= IMS_BW - 24);
        }
        for (size_t i = 0; i < sizeof k_half / sizeof k_half[0]; ++i) {
            CHECK(gfx_text_width(label, ui_tr(k_half[i]), 1)
                  <= IMS_HALF_W - 24);
        }
        for (size_t i = 0; i < sizeof k_list / sizeof k_list[0]; ++i) {
            CHECK(gfx_text_width(label, ui_tr(k_list[i]), 1) <= IMS_SW - 24);
        }
        /* The card's lines at their longest: a 31-byte name, every state,
         * every error as the list's heading. */
        memset(v.bound_name, 'W', IMU_NAME_MAX - 1u);
        v.bound_name[IMU_NAME_MAX - 1u] = '\0';
        v.r.rate_dhz = 2000u;
        v.r.battery_v = 5.99f;
        v.r.angle[0] = -179.9f;
        imu_screen_set(&v);
        ui_router_tick(99.0f);
        for (int i = IMS_L_SENSOR; i <= IMS_L_BATTERY; ++i) {
            const gfx_font_t *f = (i >= IMS_L_X && i <= IMS_L_Z)
                                      ? &gfx_font_16x28 : body;
            CHECK(gfx_text_width(f, imu_screen_line(i), 1)
                  <= IMS_RW - 2 * IMS_RPAD);
        }
        for (int k = 0; k <= (int)IMU_LINK_NO_FRAMES; ++k) {
            v.link = (imu_link_t)k;
            imu_screen_set(&v);
            CHECK(gfx_text_width(body, imu_screen_line(IMS_L_STATE), 1)
                  <= IMS_RW - 2 * IMS_RPAD);
        }
        for (int e = IMU_ERR_NONE; e <= IMU_ERR_ARG; ++e) {
            v.err = (imu_err_t)e;
            v.err_code = 0xFFu;
            imu_screen_set(&v);
            CHECK(gfx_text_width(body, imu_screen_line(IMS_L_HEAD), 1)
                  <= IMS_LW);
            CHECK(gfx_text_width(label, imu_screen_line(IMS_L_ERR), 1)
                  <= 2 * IMS_BW);
        }
        v.found_n = 8u;
        v.err = IMU_ERR_NONE;
        imu_screen_set(&v);
        CHECK(gfx_text_width(body, imu_screen_line(IMS_L_HEAD), 1) <= IMS_LW);
        for (int c = IMU_CAL_RUNNING; c <= IMU_CAL_FAILED; ++c) {
            v.cal = (imu_cal_t)c;
            v.cal_left_ms = 3000u;
            imu_screen_set(&v);
            CHECK(gfx_text_width(label, imu_screen_line(IMS_L_CAL), 1)
                  <= 2 * IMS_BW);
        }
        v = view_connected();
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(the_tile_opens_the_screen_and_home_returns_to_setup);
    RUN(scan_posts_the_command_and_the_list_shows_the_found_devices);
    RUN(a_tap_on_a_device_posts_bind_with_its_index);
    RUN(inq_refused_offers_connect_to_any_sensor);
    RUN(calibrate_needs_a_2s_hold_and_the_band_hint_on_a_short_press);
    RUN(zero_posts_zero_and_the_lines_show_the_zeroed_angles);
    RUN(the_state_line_names_every_link_state);
    RUN(every_string_fits_its_field);
    return test_summary("imu_screen");
}
