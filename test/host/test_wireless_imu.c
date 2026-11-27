/*
 * SETUP > WIRELESS > IMU (wireless_imu.c, imu_view.h): the scan and the
 * bind through SEARCH, the live rows, ZERO, CLEAR ZERO, CALIBRATE's 2s
 * hold, LAST ERROR, and the hub without an IMU tile.
 *
 * The failures this is about: a scan that never reaches the controller, a
 * bind of another sensor than the one tapped, a calibration started by a
 * brush of the glass, a refused scan that says nothing, a search with the
 * IMU off that never scans, a zero taken without a reading, a hub that
 * still opens a screen that is gone, and a row that runs past its field in
 * German.
 *
 * SPDX-License-Identifier: MIT
 */
#include "settings_rig.h"
#include "wl_bind_rig.h"

#include <string.h>

#include "imu_ctl.h"
#include "imu_view.h"
#include "splash_screen.h"

/* A step the rest of a case cannot do without: names it and ends the case. */
#define REQUIRE(cond)                                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("expected %s", #cond);                                     \
            return;                                                           \
        }                                                                     \
    } while (0)

/* The router on SETUP's tiles, the splash done, the model fresh, SAFE. */
static void router_on(void)
{
    ui_theme_set(UI_THEME_DARK);
    fresh_hub();
    ui_text_set_language(UI_LANG_EN);
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_bench_status_t st;
    memset(&st, 0, sizeof st);
    ui_router_set_status(&st);
    ui_router_set_alert(NULL);
}

/* The band's status with the bench ARMED or SAFE. */
static void set_armed(bool armed)
{
    ui_bench_status_t st;
    memset(&st, 0, sizeof st);
    st.armed = armed;
    ui_router_set_status(&st);
}

/* The view of a connected sensor at 50Hz, 3.95V, the angles 1, -2 and 3°. */
static imu_view_t view_connected(void)
{
    imu_view_t v;
    memset(&v, 0, sizeof v);
    v.link = IMU_LINK_CONNECTED;
    snprintf(v.version, sizeof v.version, "2.13.0");
    snprintf(v.bound_addr, sizeof v.bound_addr, "00:0C:BF:08:19:29 P");
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

/* WIRELESS on the IMU entry, the IMU on, showing @p v. */
static void on_imu(const imu_view_t *v)
{
    router_on();
    open_wireless(SET_IMU_EN);
    CHECK_EQ(settings_screen_category(), SET_CAT_WIRELESS);
    CHECK_EQ(wireless_pane_selected(), WL_ENTRY_IMU);
    imu_view_set(v);
    ui_router_tick(0.016f);
}

/* The next command, or kind NONE. */
static imu_cmd_t took(void)
{
    imu_cmd_t c = { IMU_CMD_NONE, 0u };
    (void)imu_view_take(&c);
    return c;
}

/* Whether the band shows text @p id. */
static bool band_is(ui_text_id_t id)
{
    return strcmp(wbr_band(), ui_tr(id)) == 0;
}

/* The list scrolled until row @p row shows whole, by a drag on the list's
 * left edge, as a finger scrolls it. */
static void show_row(int row)
{
    const gfx_rect_t pane = settings_screen_list_rect();
    for (int k = 0; k < 8; ++k) {
        const gfx_rect_t r = settings_screen_row_rect(row);
        if (r.y >= pane.y && r.y + r.h <= pane.y + pane.h) {
            return;
        }
        const int dy = (r.y < pane.y) ? 140 : -140;
        const int x = pane.x + 20;
        int y = UI_BAND_H + pane.y + pane.h / 2;
        wbr_down(x, y);
        for (int i = 0; i < 7; ++i) {
            y += dy / 7;
            touch_event_t e = { .type = TOUCH_EVENT_MOVE,
                                .point = { .id = 1, .x = (int16_t)x,
                                           .y = (int16_t)y } };
            ui_router_event(&e);
        }
        wbr_up(x, y);
    }
}

/* A tap on the IMU entry's custom row @p c, scrolled into view. */
static void tap_row(const ui_setlist_custom_t *c)
{
    const int row = wbr_row(WL_ENTRY_IMU, c);
    REQUIRE(row >= 0);
    show_row(row);
    int x = 0;
    int y = 0;
    wbr_row_at(row, &x, &y);
    tap(x, y);
}

/* CALIBRATE's button in panel coordinates, scrolled into view. */
static gfx_rect_t cal_button(void)
{
    const int row = wbr_row(WL_ENTRY_IMU, &wireless_pane_imu_cal_row);
    CHECK(row >= 0);
    show_row(row);
    gfx_rect_t b = settings_screen_row_part(row, SETTINGS_ROW_HOLD);
    b.y = (int16_t)(b.y + UI_BAND_H);
    return b;
}

/* CALIBRATE's button pressed and held @p ms, ticked 250ms at a time, then
 * released over it. */
static void hold_cal(uint32_t ms)
{
    const gfx_rect_t b = cal_button();
    const int x = b.x + b.w / 2;
    const int y = b.y + b.h / 2;
    wbr_down(x, y);
    for (uint32_t t = 0u; t < ms; t += 250u) {
        ui_router_tick(0.25f);
    }
    wbr_up(x, y);
    ui_router_tick(0.016f);
}

TEST_CASE(the_hub_has_no_imu_tile_and_wireless_stands_beside_wifi)
{
    router_on();
    CHECK_EQ(settings_hub_tiles(), 7);
    CHECK_EQ(settings_hub_tile_screen(4), SCREEN_WIFI);
    CHECK_EQ(settings_hub_cat_tile(SET_CAT_WIRELESS), 5);
    CHECK_EQ(settings_hub_tile_screen(6), SCREEN_UPDATES);
    /* The WIRELESS tile opens the list on the IMU, its first entry. */
    gfx_rect_t t;
    REQUIRE(settings_hub_slot(5, &t));
    tap(t.x + t.w / 2, UI_BAND_H + t.y + t.h / 2);
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK_EQ(settings_screen_category(), SET_CAT_WIRELESS);
    CHECK_EQ(wireless_pane_selected(), WL_ENTRY_IMU);
}

TEST_CASE(the_imu_entry_holds_every_control_of_the_imu_while_on)
{
    imu_view_t v = view_connected();
    on_imu(&v);
    static const ui_setlist_custom_t *const want[] = {
        &wireless_pane_imu_scan_row, &wireless_pane_imu_bind_row,
        &wireless_pane_imu_x_row, &wireless_pane_imu_y_row,
        &wireless_pane_imu_z_row, &wireless_pane_imu_frames_row,
        &wireless_pane_imu_bat_row, &wireless_pane_imu_c6_row,
        &wireless_pane_imu_zero_row, &wireless_pane_imu_unzero_row,
        &wireless_pane_imu_cal_row,
    };
    wl_row_t rows[WL_ENTRY_ROWS_MAX];
    const int n = wireless_pane_rows(WL_ENTRY_IMU, rows, WL_ENTRY_ROWS_MAX);
    CHECK_EQ(n, 20);
    CHECK_EQ(rows[0].id, SET_IMU_EN);
    CHECK_EQ(rows[1].id, SET_ANGLE_SOURCE);
    for (int i = 0; i < 11; ++i) {
        CHECK(rows[2 + i].custom == want[i]);
    }
    CHECK_EQ(rows[13].id, SET_IMU_AXIS);
    CHECK_EQ(rows[19].id, SET_IMU_PREC);
    CHECK_EQ(settings_screen_rows(SET_CAT_WIRELESS), 20);
    /* The live values: the bound sensor, the angles, the battery, the C6. */
    CHECK_STR_EQ(wbr_value(&wireless_pane_imu_bind_row), "HC-06");
    CHECK_STR_EQ(wbr_value(&wireless_pane_imu_x_row), "+1.00°");
    CHECK_STR_EQ(wbr_value(&wireless_pane_imu_y_row), "-2.00°");
    CHECK_STR_EQ(wbr_value(&wireless_pane_imu_z_row), "+3.00°");
    CHECK_STR_EQ(wbr_value(&wireless_pane_imu_bat_row), "3.95V");
    CHECK_STR_EQ(wbr_value(&wireless_pane_imu_c6_row), "2.13.0");
    /* The age counts on between frames, a new frame starts it again. */
    const uint32_t age = imu_view_age_ms();
    ui_router_tick(0.1f);
    ui_router_tick(0.1f);
    CHECK_EQ(imu_view_age_ms(), age + 200u);
    char want_rate[48];
    snprintf(want_rate, sizeof want_rate, "RATE 50.0Hz  AGE %ums",
             (unsigned)(age + 200u));
    CHECK_STR_EQ(wbr_value(&wireless_pane_imu_frames_row), want_rate);
    v.r.taken_ms += 20u;
    imu_view_set(&v);
    CHECK_EQ(imu_view_age_ms(), 0u);
    /* The axis the runs read is named so: X by default, then Y. */
    CHECK_EQ(ui_setlist_custom_label(&wireless_pane_imu_x_row),
             TX_IMU_ROW_ANG_X_RUN);
    CHECK_EQ(ui_setlist_custom_label(&wireless_pane_imu_y_row),
             TX_IMU_ROW_ANG_Y);
    settings_set(SET_IMU_AXIS, 1.0f);
    CHECK_EQ(ui_setlist_custom_label(&wireless_pane_imu_x_row),
             TX_IMU_ROW_ANG_X);
    CHECK_EQ(ui_setlist_custom_label(&wireless_pane_imu_y_row),
             TX_IMU_ROW_ANG_Y_RUN);
    /* No reading: "---" for the angles and the battery, the rate alone. */
    v.r.valid = false;
    v.r.battery_v = 655.35f;
    v.version[0] = '\0';
    imu_view_set(&v);
    CHECK_STR_EQ(wbr_value(&wireless_pane_imu_x_row), "---");
    CHECK_STR_EQ(wbr_value(&wireless_pane_imu_bat_row), "---");
    CHECK_STR_EQ(wbr_value(&wireless_pane_imu_c6_row), "---");
    CHECK_STR_EQ(wbr_value(&wireless_pane_imu_frames_row),
                 "RATE 50.0Hz  NO ANGLE");
    ui_router_render(&s_c, 0);
    /* OFF: IMU and ANGLE SOURCE alone. */
    settings_set(SET_IMU_EN, 0.0f);
    CHECK_EQ(wireless_pane_rows(WL_ENTRY_IMU, rows, WL_ENTRY_ROWS_MAX), 2);
}

TEST_CASE(scan_opens_search_posts_the_scan_and_a_found_sensor_binds)
{
    imu_view_t v = view_connected();
    v.link = IMU_LINK_READY;
    v.bound_addr[0] = '\0';
    v.bound_name[0] = '\0';
    on_imu(&v);
    CHECK_STR_EQ(wbr_value(&wireless_pane_imu_scan_row), "SCAN");
    CHECK_STR_EQ(wbr_value(&wireless_pane_imu_bind_row), "NONE");
    tap_row(&wireless_pane_imu_scan_row);
    CHECK_EQ(ui_router_current(), SCREEN_WL_SEARCH);
    CHECK_EQ(wl_search_device()->entry, WL_ENTRY_IMU);
    CHECK_EQ(took().kind, IMU_CMD_SCAN);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    /* The scan under way, 7s left. */
    v.link = IMU_LINK_SCANNING;
    v.scan_left_s = 7u;
    imu_view_set(&v);
    CHECK_STR_EQ(wbr_state(), "SCANNING: 7s LEFT");
    CHECK_STR_EQ(wbr_value(&wireless_pane_imu_scan_row), "SCANNING");
    /* Two found, three others heard. */
    v.link = IMU_LINK_READY;
    v.scan_left_s = 0u;
    v.scan_done = true;
    v.found_n = 2u;
    snprintf(v.found[0].addr, sizeof v.found[0].addr, "00:0C:BF:08:19:29");
    snprintf(v.found[0].name, sizeof v.found[0].name, "HC-06");
    v.found[0].rssi = -50;
    snprintf(v.found[1].addr, sizeof v.found[1].addr, "00:0C:BF:08:19:30");
    v.found[1].rssi = 127;
    v.hidden = 3u;
    imu_view_set(&v);
    CHECK_STR_EQ(wbr_state(), "FOUND 2: TAP ONE TO BIND IT");
    CHECK_STR_EQ(wbr_value(&wireless_pane_imu_scan_row), "FOUND 2");
    wls_view_t sv;
    wl_search_view(&sv);
    CHECK_EQ(sv.hidden, 3u);
    CHECK_STR_EQ(sv.found[1].addr, "00:0C:BF:08:19:30 P");
    ui_router_render(&s_c, 0);
    /* The second one tapped, then BIND: its index goes out. */
    gfx_rect_t r;
    REQUIRE(wbr_found(1, &r));
    tap(r.x + r.w / 2, r.y + r.h / 2);
    CHECK(wl_search_asking());
    const gfx_rect_t b = wbr_part(WLS_PART_BIND);
    tap(b.x + b.w / 2, b.y + b.h / 2);
    const imu_cmd_t c = took();
    CHECK_EQ(c.kind, IMU_CMD_BIND);
    CHECK_EQ(c.arg, 1u);
    /* A bind while the calibration runs is refused on the band. */
    v.cal = IMU_CAL_RUNNING;
    imu_view_set(&v);
    REQUIRE(wbr_found(0, &r));
    tap(r.x + r.w / 2, r.y + r.h / 2);
    tap(b.x + b.w / 2, b.y + b.h / 2);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    CHECK(band_is(TX_IMU_BIND_BUSY));
    /* The home tag returns to WIRELESS on the IMU. */
    int hx = 0;
    int hy = 0;
    wbr_home_at(&hx, &hy);
    tap(hx, hy);
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK_EQ(wireless_pane_selected(), WL_ENTRY_IMU);
    /* BOUND SENSOR opens SEARCH on the list, without a scan. */
    v.cal = IMU_CAL_NONE;
    imu_view_set(&v);
    tap_row(&wireless_pane_imu_bind_row);
    CHECK_EQ(ui_router_current(), SCREEN_WL_SEARCH);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
}

/* SEARCH knows the bound sensor among the found ones by its address as
 * text: the found entries carry the type, as the bind keeps it, so the
 * bound one is marked and another one of the same address but the other
 * type is not. */
TEST_CASE(search_knows_the_bound_sensor_by_its_address_and_type)
{
    imu_view_t v = view_connected();
    v.link = IMU_LINK_READY;
    on_imu(&v);
    tap_row(&wireless_pane_imu_bind_row);
    REQUIRE(ui_router_current() == SCREEN_WL_SEARCH);
    v.scan_done = true;
    v.found_n = 2u;
    snprintf(v.found[0].addr, sizeof v.found[0].addr, "00:0C:BF:08:19:29");
    v.found[0].addr_type = 0u;
    snprintf(v.found[1].addr, sizeof v.found[1].addr, "00:0C:BF:08:19:29");
    v.found[1].addr_type = 1u;
    imu_view_set(&v);
    wls_view_t sv;
    wl_search_view(&sv);
    CHECK_STR_EQ(sv.bound_addr, "00:0C:BF:08:19:29 P");
    CHECK_STR_EQ(sv.found[0].addr, sv.bound_addr);
    CHECK_STR_EQ(sv.found[1].addr, "00:0C:BF:08:19:29 R");
}

TEST_CASE(a_scan_is_refused_on_the_band_while_armed_scanning_or_without_c6)
{
    imu_view_t v = view_connected();
    on_imu(&v);
    set_armed(true);
    tap_row(&wireless_pane_imu_scan_row);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    CHECK(band_is(TX_IMU_SCAN_ARMED));
    CHECK(!imu_view_search());
    set_armed(false);
    ui_router_goto(SCREEN_SETUP);
    v.link = IMU_LINK_SCANNING;
    imu_view_set(&v);
    tap_row(&wireless_pane_imu_scan_row);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    CHECK(band_is(TX_NODE_SCAN_RUNNING));
    ui_router_goto(SCREEN_SETUP);
    v.link = IMU_LINK_NO_BT;
    imu_view_set(&v);
    tap_row(&wireless_pane_imu_scan_row);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    CHECK(band_is(TX_IMU_SCAN_OFF));
}

/* An ARM hold completed and ARMED not reported yet: the band reads SAFE,
 * so the scan goes out, and the controller, which counts the arm on its
 * way as ARMED, refuses it.  The next view says why on the band; a view
 * with the same count says nothing, a second refusal says it again. */
TEST_CASE(a_scan_refused_while_an_arm_waits_says_why_on_the_band)
{
    imu_view_t v = view_connected();
    on_imu(&v);
    imu_ctl_t c;
    imu_ctl_init(&c);
    const imu_cfg_t cfg = { .en = true };
    imu_ctl_config(&c, &cfg, 0u);
    imu_ctl_live(&c, true);             /* the arm waits */
    tap_row(&wireless_pane_imu_scan_row);
    imu_cmd_t cmd = took();
    CHECK_EQ(cmd.kind, IMU_CMD_SCAN);
    CHECK(!imu_ctl_command(&c, &cmd, 0u));
    CHECK(!band_is(TX_IMU_SCAN_ARMED));
    imu_view_t seen;
    imu_ctl_view(&c, &seen);
    imu_view_set(&seen);
    CHECK(band_is(TX_IMU_SCAN_ARMED));
    ui_router_set_alert(NULL);
    seen.r.taken_ms += 20u;             /* a new reading, the same count */
    imu_view_set(&seen);
    CHECK(!band_is(TX_IMU_SCAN_ARMED));
    CHECK(!imu_ctl_command(&c, &cmd, 0u));
    imu_ctl_view(&c, &seen);
    imu_view_set(&seen);
    CHECK(band_is(TX_IMU_SCAN_ARMED));
}

/* A search while Bluetooth LE starts waits for it and scans once; one owed
 * when the start ends in NO BLUETOOTH ON C6 or the bench arms is dropped. */
TEST_CASE(a_search_while_bluetooth_starts_scans_once_it_runs)
{
    imu_view_t v;
    memset(&v, 0, sizeof v);
    v.link = IMU_LINK_STARTING;
    on_imu(&v);
    tap_row(&wireless_pane_imu_scan_row);
    CHECK(imu_view_search_owed());
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    CHECK_STR_EQ(wbr_value(&wireless_pane_imu_scan_row),
                 "STARTING BLUETOOTH");
    v.link = IMU_LINK_READY;
    imu_view_set(&v);
    CHECK_EQ(took().kind, IMU_CMD_SCAN);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    CHECK(!imu_view_search_owed());
    /* Owed, then no sync with the C6: dropped, and the sync back later
     * starts nothing. */
    v.link = IMU_LINK_STARTING;
    imu_view_set(&v);
    CHECK(imu_view_search());
    v.link = IMU_LINK_NO_BT;
    imu_view_set(&v);
    CHECK(!imu_view_search_owed());
    v.link = IMU_LINK_READY;
    imu_view_set(&v);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    /* Owed, then ARMED: dropped on the next frame's view. */
    v.link = IMU_LINK_STARTING;
    imu_view_set(&v);
    CHECK(imu_view_search());
    set_armed(true);
    imu_view_set(&v);
    CHECK(!imu_view_search_owed());
    set_armed(false);
}

TEST_CASE(unbind_in_search_forgets_the_sensor_with_a_save_asked)
{
    imu_view_t v = view_connected();
    on_imu(&v);
    settings_set_text(SET_TEXT_IMU_ADDR, "00:0C:BF:08:19:29 P");
    settings_set_text(SET_TEXT_IMU_NAME, "HC-06");
    tap_row(&wireless_pane_imu_bind_row);
    REQUIRE(ui_router_current() == SCREEN_WL_SEARCH);
    CHECK(wl_search_unbind_ready());
    wbr_hold_unbind(NULL, 2000u);
    CHECK_STR_EQ(settings_text(SET_TEXT_IMU_ADDR), "");
    CHECK_STR_EQ(settings_text(SET_TEXT_IMU_NAME), "");
    CHECK(settings_save_asked());
}

TEST_CASE(zero_and_clear_zero_are_taps_locked_without_their_state)
{
    imu_view_t v = view_connected();
    on_imu(&v);
    CHECK(wireless_pane_imu_unzero_row.locked() != NULL);
    tap_row(&wireless_pane_imu_unzero_row);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    tap_row(&wireless_pane_imu_zero_row);
    CHECK_EQ(took().kind, IMU_CMD_ZERO);
    v.r.zeroed = true;
    imu_view_set(&v);
    CHECK(wireless_pane_imu_unzero_row.locked() == NULL);
    tap_row(&wireless_pane_imu_unzero_row);
    CHECK_EQ(took().kind, IMU_CMD_UNZERO);
    /* No reading: ZERO is locked and says why in its help line. */
    v.r.valid = false;
    imu_view_set(&v);
    CHECK_STR_EQ(wireless_pane_imu_zero_row.locked(),
                 ui_tr(TX_IMU_LOCK_NO_ANGLE));
    tap_row(&wireless_pane_imu_zero_row);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    CHECK(!imu_view_zero());
}

/* An error that comes between the DOWN and the UP on CLEAR ZERO puts
 * LAST ERROR in above it, and CLEAR ZERO's place is now ZERO's: the
 * release acts on neither.  The same when the error goes. */
TEST_CASE(an_error_row_that_comes_or_goes_under_a_press_ends_it)
{
    imu_view_t v = view_connected();
    v.r.zeroed = true;
    on_imu(&v);
    const int row = wbr_row(WL_ENTRY_IMU, &wireless_pane_imu_unzero_row);
    REQUIRE(row >= 0);
    show_row(row);
    int x = 0;
    int y = 0;
    wbr_row_at(row, &x, &y);
    wbr_down(x, y);
    v.err = IMU_ERR_WRITE;
    imu_view_set(&v);
    REQUIRE(wbr_row(WL_ENTRY_IMU, &wireless_pane_imu_unzero_row) == row + 1);
    wbr_up(x, y);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    /* The error goes under a press on CLEAR ZERO, one row lower now. */
    show_row(row + 1);
    wbr_row_at(row + 1, &x, &y);
    wbr_down(x, y);
    v.err = IMU_ERR_NONE;
    imu_view_set(&v);
    ui_router_tick(0.016f);
    wbr_up(x, y);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    /* Without a change under it the tap acts. */
    tap_row(&wireless_pane_imu_unzero_row);
    CHECK_EQ(took().kind, IMU_CMD_UNZERO);
}

TEST_CASE(calibrate_needs_a_2s_hold_and_the_band_hint_on_a_short_press)
{
    imu_view_t v = view_connected();
    on_imu(&v);
    CHECK(wireless_pane_imu_cal_row.locked() == NULL);
    /* 1s and let go: the hint, nothing sent. */
    hold_cal(1000u);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    CHECK(band_is(TX_HOLD_HINT_IMU_CAL));
    /* 2s: CALIBRATE once. */
    ui_router_set_alert(NULL);
    hold_cal(2500u);
    CHECK_EQ(took().kind, IMU_CMD_CALIBRATE);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    ui_router_render(&s_c, 0);
    /* Running: locked with the seconds left, a hold takes nothing. */
    v.cal = IMU_CAL_RUNNING;
    v.cal_left_ms = 2400u;
    imu_view_set(&v);
    CHECK_STR_EQ(wireless_pane_imu_cal_row.locked(),
                 "KEEP IT STILL AND LEVEL FOR 3s");
    hold_cal(2500u);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    ui_router_render(&s_c, 0);
    /* Done: a notice on the band; failed: an alert. */
    v.cal = IMU_CAL_DONE;
    imu_view_set(&v);
    CHECK(band_is(TX_IMU_CAL_DONE));
    v.cal = IMU_CAL_RUNNING;
    imu_view_set(&v);
    v.cal = IMU_CAL_FAILED;
    imu_view_set(&v);
    CHECK(band_is(TX_IMU_CAL_FAILED));
    /* Without frames: locked. */
    v.link = IMU_LINK_NO_FRAMES;
    imu_view_set(&v);
    CHECK_STR_EQ(wireless_pane_imu_cal_row.locked(),
                 ui_tr(TX_IMU_LOCK_NO_FRAMES));
    CHECK(!imu_view_calibrate());
}

/* A press that slides off CALIBRATE's button ends the hold unfired; a
 * calibration that locks the row under the finger ends it too. */
TEST_CASE(a_hold_that_leaves_the_button_or_locks_fires_nothing)
{
    imu_view_t v = view_connected();
    on_imu(&v);
    const gfx_rect_t b = cal_button();
    const int x = b.x + b.w / 2;
    const int y = b.y + b.h / 2;
    wbr_down(x, y);
    ui_router_tick(0.25f);
    touch_event_t e = { .type = TOUCH_EVENT_MOVE,
                        .point = { .id = 1, .x = (int16_t)(b.x - 40),
                                   .y = (int16_t)y } };
    ui_router_event(&e);
    for (int i = 0; i < 10; ++i) {
        ui_router_tick(0.25f);
    }
    wbr_up(b.x - 40, y);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
    wbr_down(x, y);
    ui_router_tick(0.25f);
    v.link = IMU_LINK_NO_FRAMES;
    imu_view_set(&v);
    for (int i = 0; i < 10; ++i) {
        ui_router_tick(0.25f);
    }
    wbr_up(x, y);
    CHECK_EQ(took().kind, IMU_CMD_NONE);
}

TEST_CASE(last_error_shows_only_with_an_error_and_names_it)
{
    imu_view_t v = view_connected();
    on_imu(&v);
    CHECK(!wireless_pane_imu_err());
    CHECK_EQ(wbr_row(WL_ENTRY_IMU, &wireless_pane_imu_err_row), -1);
    v.err = IMU_ERR_CONNECT;
    imu_view_set(&v);
    CHECK_EQ(wbr_row(WL_ENTRY_IMU, &wireless_pane_imu_err_row), 4);
    CHECK_STR_EQ(wireless_pane_imu_err_row.locked(),
                 ui_tr(TX_IMU_ERR_CONNECT));
    v.err = IMU_ERR_SCAN;
    v.err_code = 13u;
    imu_view_set(&v);
    CHECK_STR_EQ(wireless_pane_imu_err_row.locked(),
                 "THE SCAN DID NOT START: CODE 13");
    /* None found while the bound sensor holds the link: none other. */
    v.err = IMU_ERR_NONE_FOUND;
    imu_view_set(&v);
    CHECK_STR_EQ(wireless_pane_imu_err_row.locked(),
                 ui_tr(TX_IMU_ERR_NONE_OTHER));
    v.link = IMU_LINK_READY;
    v.bound_addr[0] = '\0';
    imu_view_set(&v);
    CHECK_STR_EQ(wireless_pane_imu_err_row.locked(),
                 ui_tr(TX_IMU_ERR_NONE_FOUND));
    ui_router_render(&s_c, 0);
}

/* Every row's name and value fits its field in each language, at the
 * widest values: -179.99°, 999.9Hz and an age of 99999ms. */
TEST_CASE(every_row_fits_its_field)
{
    static const ui_setlist_custom_t *const rows[] = {
        &wireless_pane_imu_scan_row, &wireless_pane_imu_bind_row,
        &wireless_pane_imu_err_row, &wireless_pane_imu_x_row,
        &wireless_pane_imu_y_row, &wireless_pane_imu_z_row,
        &wireless_pane_imu_frames_row, &wireless_pane_imu_bat_row,
        &wireless_pane_imu_c6_row, &wireless_pane_imu_zero_row,
        &wireless_pane_imu_unzero_row, &wireless_pane_imu_cal_row,
    };
    imu_view_t v = view_connected();
    on_imu(&v);
    v.r.angle[0] = -179.99f;
    v.r.rate_dhz = 9999u;
    imu_view_set(&v);
    for (int i = 0; i < 200; ++i) {
        ui_router_tick(1.0f);
    }
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
            const ui_setlist_custom_t *c = rows[i];
            /* The name in the head face left of the value, 31 cells. */
            CHECK(gfx_text_width(&gfx_font_20x36,
                                 ui_tr(ui_setlist_custom_label(c)), 1)
                  <= 31 * 20);
            if (c->value_text != NULL) {
                char buf[64];
                c->value_text(buf, sizeof buf);
                /* The value at the right, 278px in the label face. */
                CHECK(gfx_text_width(&gfx_font_10x20, buf, 1) <= 278);
            }
        }
        v.cal = IMU_CAL_RUNNING;
        v.cal_left_ms = 3000u;
        imu_view_set(&v);
        /* The hold row's help line left of its button: 43 cells. */
        CHECK(gfx_text_width(&gfx_font_10x20,
                             wireless_pane_imu_cal_row.locked(), 1)
              <= 43 * 10);
        v.cal = IMU_CAL_NONE;
        imu_view_set(&v);
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(the_hub_has_no_imu_tile_and_wireless_stands_beside_wifi);
    RUN(the_imu_entry_holds_every_control_of_the_imu_while_on);
    RUN(scan_opens_search_posts_the_scan_and_a_found_sensor_binds);
    RUN(search_knows_the_bound_sensor_by_its_address_and_type);
    RUN(a_scan_is_refused_on_the_band_while_armed_scanning_or_without_c6);
    RUN(a_scan_refused_while_an_arm_waits_says_why_on_the_band);
    RUN(a_search_while_bluetooth_starts_scans_once_it_runs);
    RUN(unbind_in_search_forgets_the_sensor_with_a_save_asked);
    RUN(zero_and_clear_zero_are_taps_locked_without_their_state);
    RUN(an_error_row_that_comes_or_goes_under_a_press_ends_it);
    RUN(calibrate_needs_a_2s_hold_and_the_band_hint_on_a_short_press);
    RUN(a_hold_that_leaves_the_button_or_locks_fires_nothing);
    RUN(last_error_shows_only_with_an_error_and_names_it);
    RUN(every_row_fits_its_field);
    return test_summary("wireless_imu");
}
