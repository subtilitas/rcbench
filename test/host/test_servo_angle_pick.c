/*
 * The ANGLE SOURCE list (servo_angle_pick.c) on RUDDER and RUDDER AUTO, and
 * the ANGLE row of TEST's and LIMIT RUN's cards: the READY rows ANGLE and
 * the SETTINGS list's ANGLE SOURCE row open it on a
 * SAFE bench, with a reading and without; the AS5600 and the IMUs the last
 * scan found as its entries; what a pick sets; SEARCH FOR IMU switching
 * the IMU on and scanning once Bluetooth LE runs; NO IMU FOUND; and the
 * list following the IMU's view while it is open.  The rig is
 * servo_rauto_kst_rig.h; the IMU's view is handed to the IMU screen as the
 * panel hands it over (imu_screen_set()).
 *
 * Cases:
 * - the_angle_row_opens_the_list_with_a_reading_and_without
 * - the_as5600_pick_sets_the_source_and_closes_the_list
 * - an_imu_pick_binds_it_switches_the_imu_on_and_sets_the_source
 * - search_switches_the_imu_on_and_scans_once_bluetooth_runs
 * - the_list_follows_the_scan_to_no_imu_found_and_a_failed_scan
 * - eight_found_fill_ten_entries_and_a_bound_one_drops_the_eighth
 * - the_list_scans_by_itself_with_nothing_bound_and_nothing_scanned
 * - a_finger_on_an_entry_keeps_the_entries_under_it
 * - the_list_is_refused_armed_and_closes_when_the_bench_arms
 * - rudder_autos_angle_row_and_list_row_open_the_list
 * - a_search_owed_is_dropped_by_imu_off_and_refused_on_a_failed_c6
 * - test_and_limit_run_show_the_angle_and_never_refuse_on_it
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_rauto_kst_rig.h"
#include "imu_screen.h"
#include "servo_angle_pick.h"
#include "ui_setlist.h"

/** @brief RUDDER's card rows (test_servo_drive_ready.c). */
enum { RD_PORT = 0, RD_ANGLE };

/** @brief The IMU's view as the cases hand it to the IMU screen. */
static imu_view_t g_v;

/** @brief The view handed over now. */
static void view_now(void)
{
    imu_screen_set(&g_v);
}

/**
 * @brief Sensor @p i of the scan in the view.
 * @param i    the index
 * @param addr its address
 * @param name its name, "" none
 * @param rssi its RSSI, dBm
 */
static void found(int i, const char *addr, const char *name, int8_t rssi)
{
    imu_dev_t *d = &g_v.found[i];
    snprintf(d->addr, sizeof(d->addr), "%s", addr);
    snprintf(d->name, sizeof(d->name), "%s", name);
    d->rssi = rssi;
    d->flags = IMU_DEV_UUID_18F0;
}

/**
 * @brief The next command the IMU screen queued for the panel.
 * @return it; IMU_CMD_NONE when none is queued
 */
static imu_cmd_t imu_took(void)
{
    imu_cmd_t c = { IMU_CMD_NONE, 0u };
    (void)imu_screen_take(&c);
    return c;
}

/**
 * @brief The rig's bench SAFE on function @p fn, the AS5600 reading at the
 *        center and ZERO taken; the IMU screen reset and the view @p link
 *        with nothing found and nothing bound.
 * @param fn   the function
 * @param link the IMU's link state
 */
static void safe_on(servo_fn_t fn, imu_link_t link)
{
    ra_setup(true, true);
    ra.armed = false;
    servo_screen_set_armed(false);
    imu_screen()->reset();
    memset(&g_v, 0, sizeof(g_v));
    g_v.link = link;
    if (link != IMU_LINK_OFF) {
        settings_set(SET_IMU_EN, 1.0f);
    }
    view_now();
    go(fn);
    ra_ms(200u);
}

/** @brief A tap on RUDDER's card row ANGLE. */
static void tap_angle_row(void)
{
    ev(SV_RDY_X, SV_DRV_ROW_Y(RD_ANGLE), TOUCH_EVENT_DOWN, 2);
    ev(SV_RDY_X, SV_DRV_ROW_Y(RD_ANGLE), TOUCH_EVENT_UP, 2);
}

/** @brief A tap on the list's entry @p i.  @param i the entry */
static void tap_entry(int i)
{
    const gfx_rect_t r = angle_pick_rect(i);
    ev(cx(r), cy(r), TOUCH_EVENT_DOWN, 3);
    ev(cx(r), cy(r), TOUCH_EVENT_UP, 3);
}

/**
 * @brief Entry @p i's label, and whether it is refused.
 * @param i the entry
 * @param refused takes whether it is refused
 * @return the label
 */
static const char *entry(int i, bool *refused)
{
    return angle_pick_label(i, refused);
}

/** @brief A frame of the screen: the list follows the view. */
static void frame(void)
{
    scr->tick(0.02f);
}

/* The row opens the list with an AS5600 reading on show and without one:
 * AS5600 in force in the accent, SEARCH FOR IMU offered; with the AS5600
 * off in SETUP its entry is refused with where to switch it on. */
TEST_CASE(the_angle_row_opens_the_list_with_a_reading_and_without)
{
    safe_on(SERVO_FN_RUDDER, IMU_LINK_OFF);
    rdy_row_t r;
    CHECK(ready_row(RD_ANGLE, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_OK);
    CHECK(r.fix);
    tap_angle_row();
    CHECK_EQ(angle_pick_count(), 2);
    bool refused = true;
    CHECK_STR_EQ(entry(0, &refused), "AS5600");
    CHECK(!refused);
    CHECK(angle_pick_current(0));
    CHECK_STR_EQ(entry(1, &refused), TR(SV_ANG_SEARCH));
    CHECK(!refused);
    CHECK(!angle_pick_current(1));
    /* One column of entries while 5 or fewer: the PORT list's width. */
    CHECK(angle_pick_rect(0).w > angle_pick_rect(0).h * 8);
    CHECK_EQ(imu_took().kind, IMU_CMD_NONE);   /* the IMU off: no scan */
    tap_entry(0);
    CHECK_EQ(angle_pick_count(), -1);

    settings_set(SET_ENC_EN, 0.0f);
    ra_ms(100u);
    CHECK(ready_row(RD_ANGLE, &r));
    CHECK_EQ(r.st, (uint8_t)RDY_WARN);
    tap_angle_row();
    CHECK_EQ(angle_pick_count(), 2);
    CHECK_STR_EQ(entry(0, &refused), TR(SV_ANG_AS5600_OFF));
    CHECK(refused);
    scr->render(&cv, 0);
}

/* AS5600 picked with the IMU in force: ANGLE SOURCE AS5600, the list
 * closed. */
TEST_CASE(the_as5600_pick_sets_the_source_and_closes_the_list)
{
    safe_on(SERVO_FN_RUDDER, IMU_LINK_OFF);
    settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_IMU);
    tap_angle_row();
    CHECK(!angle_pick_current(0));
    tap_entry(0);
    CHECK_EQ(settings_get_int(SET_ANGLE_SOURCE), (int)SERVO_ANGLE_AS5600);
    CHECK_EQ(angle_pick_count(), -1);
}

/* A sensor of the scan picked: BIND with its index, ANGLE SOURCE IMU, the
 * list closed.  The bound one picked again binds nothing new, and with
 * the IMU switched off meanwhile its pick switches the IMU on. */
TEST_CASE(an_imu_pick_binds_it_switches_the_imu_on_and_sets_the_source)
{
    safe_on(SERVO_FN_RUDDER, IMU_LINK_READY);
    g_v.found_n = 2u;
    found(0, "00:0C:BF:08:19:29", "HC-06", -50);
    found(1, "00:0C:BF:0A:44:17", "", -71);
    view_now();
    tap_angle_row();
    CHECK_EQ(angle_pick_count(), 4);
    bool refused = true;
    CHECK_STR_EQ(entry(1, &refused), "HC-06 19:29 -50dBm");
    CHECK(!refused);
    char want[64];
    snprintf(want, sizeof(want), "%s 44:17 -71dBm", TR(IMU_NO_NAME));
    CHECK_STR_EQ(entry(2, NULL), want);
    CHECK_STR_EQ(entry(3, NULL), TR(SV_ANG_SEARCH));
    CHECK_EQ(imu_took().kind, IMU_CMD_NONE);   /* listed: no scan of its own */
    tap_entry(2);
    const imu_cmd_t c = imu_took();
    CHECK_EQ(c.kind, IMU_CMD_BIND);
    CHECK_EQ(c.arg, 1u);
    CHECK_EQ(settings_get_int(SET_ANGLE_SOURCE), (int)SERVO_ANGLE_IMU);
    CHECK_EQ(angle_pick_count(), -1);

    /* Bound as the panel reports it, its type after the address. */
    g_v.link = IMU_LINK_CONNECTED;
    snprintf(g_v.bound_addr, sizeof(g_v.bound_addr), "00:0C:BF:0A:44:17 P");
    view_now();
    settings_set(SET_IMU_EN, 0.0f);
    tap_angle_row();
    CHECK(angle_pick_current(2));
    CHECK(!angle_pick_current(1));
    CHECK(!angle_pick_current(0));
    tap_entry(2);
    CHECK_EQ(imu_took().kind, IMU_CMD_NONE);
    CHECK(settings_get_bool(SET_IMU_EN));
    CHECK_EQ(settings_get_int(SET_ANGLE_SOURCE), (int)SERVO_ANGLE_IMU);
}

/* SEARCH FOR IMU with the IMU off: IMU ON in SETUP, the list open with
 * the entry waiting while Bluetooth LE starts, and the SCAN queued once it
 * runs; SCANNING while the scan runs. */
TEST_CASE(search_switches_the_imu_on_and_scans_once_bluetooth_runs)
{
    safe_on(SERVO_FN_RUDDER, IMU_LINK_OFF);
    CHECK(!settings_get_bool(SET_IMU_EN));
    tap_angle_row();
    tap_entry(1);
    CHECK(settings_get_bool(SET_IMU_EN));
    CHECK_EQ(angle_pick_count(), 2);
    bool refused = false;
    CHECK_STR_EQ(entry(1, &refused), TR(IMU_ST_STARTING));
    CHECK(refused);
    CHECK_EQ(imu_took().kind, IMU_CMD_NONE);
    g_v.link = IMU_LINK_STARTING;
    view_now();
    frame();
    CHECK_EQ(imu_took().kind, IMU_CMD_NONE);
    g_v.link = IMU_LINK_NO_BT;
    view_now();
    frame();
    CHECK_STR_EQ(entry(1, NULL), TR(IMU_ST_NO_BT));
    g_v.link = IMU_LINK_READY;
    view_now();
    CHECK_EQ(imu_took().kind, IMU_CMD_SCAN);
    CHECK_EQ(imu_took().kind, IMU_CMD_NONE);
    frame();
    CHECK_STR_EQ(entry(1, &refused), TR(SV_ANG_SEARCH));
    CHECK(!refused);
    g_v.link = IMU_LINK_SCANNING;
    view_now();
    frame();
    CHECK_STR_EQ(entry(1, &refused), TR(IMU_SCANNING));
    CHECK(refused);
    /* A tap on it names it and asks nothing. */
    ui_router_set_alert(NULL);
    tap_entry(1);
    CHECK_EQ(imu_took().kind, IMU_CMD_NONE);
    CHECK_EQ(angle_pick_count(), 2);
}

/* The scan ends with none: NO IMU FOUND refused, its tap the reason in the
 * band; a scan that did not run names its code; a new SEARCH clears both
 * while it runs. */
TEST_CASE(the_list_follows_the_scan_to_no_imu_found_and_a_failed_scan)
{
    safe_on(SERVO_FN_RUDDER, IMU_LINK_READY);
    g_v.err = IMU_ERR_NONE_FOUND;
    view_now();
    tap_angle_row();
    CHECK_EQ(imu_took().kind, IMU_CMD_NONE);   /* scanned before: no scan */
    CHECK_EQ(angle_pick_count(), 3);
    bool refused = false;
    CHECK_STR_EQ(entry(1, &refused), TR(SV_ANG_NONE));
    CHECK(refused);
    CHECK_STR_EQ(entry(2, NULL), TR(SV_ANG_SEARCH));
    ui_router_set_alert(NULL);
    tap_entry(1);
    CHECK_STR_EQ(ui_router_alert(), TR(IMU_ERR_NONE_FOUND));
    CHECK_EQ(angle_pick_count(), 3);

    tap_entry(2);
    CHECK_EQ(imu_took().kind, IMU_CMD_SCAN);
    g_v.link = IMU_LINK_SCANNING;
    g_v.err = IMU_ERR_NONE;
    view_now();
    frame();
    CHECK_EQ(angle_pick_count(), 2);
    CHECK_STR_EQ(entry(1, NULL), TR(IMU_SCANNING));

    g_v.link = IMU_LINK_READY;
    g_v.err = IMU_ERR_SCAN;
    g_v.err_code = 6u;
    view_now();
    frame();
    char want[64];
    snprintf(want, sizeof(want), TR(SV_ANG_SCAN_FAIL), 6u);
    CHECK_STR_EQ(entry(1, &refused), want);
    CHECK(refused);
    ui_router_set_alert(NULL);
    tap_entry(1);
    CHECK_STR_EQ(ui_router_alert(), want);
}

/* 8 sensors: AS5600, the 8 and SEARCH fill CHOICE_MAX's 10 in two columns;
 * the bound sensor not in the scan stands second, in the accent with the
 * IMU in force, and the eighth found gives way.  A long name is cut to 12
 * cells. */
TEST_CASE(eight_found_fill_ten_entries_and_a_bound_one_drops_the_eighth)
{
    safe_on(SERVO_FN_RUDDER, IMU_LINK_READY);
    g_v.found_n = 8u;
    for (int i = 0; i < 8; ++i) {
        char addr[24];
        snprintf(addr, sizeof(addr), "00:0C:BF:08:19:%02d", 10 + i);
        found(i, addr, i == 0 ? "WT901BLE68-ABCDEFGH" : "HC-06",
              (int8_t)(-40 - i));
    }
    view_now();
    tap_angle_row();
    CHECK_EQ(angle_pick_count(), 10);
    CHECK_STR_EQ(entry(1, NULL), "WT901BLE68-A 19:10 -40dBm");
    CHECK_STR_EQ(entry(8, NULL), "HC-06 19:17 -47dBm");
    CHECK_STR_EQ(entry(9, NULL), TR(SV_ANG_SEARCH));
    /* Two columns of 5. */
    CHECK(angle_pick_rect(5).x > angle_pick_rect(0).x);
    CHECK_EQ(angle_pick_rect(5).y, angle_pick_rect(0).y);
    scr->render(&cv, 0);

    settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_IMU);
    snprintf(g_v.bound_addr, sizeof(g_v.bound_addr), "00:0C:BF:01:02:03 P");
    snprintf(g_v.bound_name, sizeof(g_v.bound_name), "BWT901CL");
    view_now();
    frame();
    CHECK_EQ(angle_pick_count(), 10);
    CHECK_STR_EQ(entry(1, NULL), "BWT901CL 02:03");
    CHECK(angle_pick_current(1));
    CHECK(!angle_pick_current(0));
    CHECK_STR_EQ(entry(8, NULL), "HC-06 19:16 -46dBm");
    CHECK_STR_EQ(entry(9, NULL), TR(SV_ANG_SEARCH));
}

/* With the IMU on, nothing bound, nothing scanned and no error, opening
 * the list scans at once; with a sensor bound it does not. */
TEST_CASE(the_list_scans_by_itself_with_nothing_bound_and_nothing_scanned)
{
    safe_on(SERVO_FN_RUDDER, IMU_LINK_READY);
    tap_angle_row();
    CHECK_EQ(imu_took().kind, IMU_CMD_SCAN);
    tap_entry(0);                              /* AS5600: closes */

    g_v.link = IMU_LINK_CONNECTED;
    snprintf(g_v.bound_addr, sizeof(g_v.bound_addr), "00:0C:BF:08:19:29 P");
    view_now();
    tap_angle_row();
    CHECK_EQ(imu_took().kind, IMU_CMD_NONE);
    CHECK_EQ(angle_pick_count(), 3);           /* AS5600, the bound, SEARCH */
}

/* A finger down on an entry: a scan that ends under it changes no entry
 * until the release, which picks what it pressed. */
TEST_CASE(a_finger_on_an_entry_keeps_the_entries_under_it)
{
    safe_on(SERVO_FN_RUDDER, IMU_LINK_READY);
    g_v.found_n = 1u;
    found(0, "00:0C:BF:08:19:29", "HC-06", -50);
    view_now();
    tap_angle_row();
    const gfx_rect_t r = angle_pick_rect(1);
    ev(cx(r), cy(r), TOUCH_EVENT_DOWN, 3);
    g_v.found_n = 2u;
    memmove(&g_v.found[1], &g_v.found[0], sizeof(g_v.found[0]));
    found(0, "00:0C:BF:0A:44:17", "OTHER", -40);
    view_now();
    frame();
    CHECK_STR_EQ(entry(1, NULL), "HC-06 19:29 -50dBm");
    ev(cx(r), cy(r), TOUCH_EVENT_UP, 3);
    const imu_cmd_t c = imu_took();
    CHECK_EQ(c.kind, IMU_CMD_BIND);
    /* HC-06 at its index in the view of the release. */
    CHECK_EQ(c.arg, 1u);
}

/* ARMED: the row opens no list and changes nothing; a list open on a SAFE
 * bench closes when the bench arms under it. */
TEST_CASE(the_list_is_refused_armed_and_closes_when_the_bench_arms)
{
    safe_on(SERVO_FN_RUDDER, IMU_LINK_OFF);
    servo_screen_set_armed(true);
    ra.armed = true;
    ra_ms(100u);
    tap_angle_row();
    CHECK_EQ(angle_pick_count(), -1);
    CHECK_EQ(settings_get_int(SET_ANGLE_SOURCE), (int)SERVO_ANGLE_AS5600);

    servo_screen_set_armed(false);
    ra.armed = false;
    ra_ms(100u);
    tap_angle_row();
    CHECK_EQ(angle_pick_count(), 2);
    servo_screen_set_armed(true);
    ra.armed = true;
    ra_ms(40u);
    CHECK_EQ(angle_pick_count(), -1);
}

/* RUDDER AUTO: its READY row ANGLE opens the list, red or not, and so
 * does the ANGLE SOURCE row of its SETTINGS list, which closes. */
TEST_CASE(rudder_autos_angle_row_and_list_row_open_the_list)
{
    safe_on(SERVO_FN_RAUTO, IMU_LINK_OFF);
    rdy_row_t r;
    CHECK(rauto_ready_row(RAR_ANGLE, &r));
    CHECK(r.fix);
    ev(SV_RDY_X, SV_RDY_ROW_Y((int)RAR_ANGLE), TOUCH_EVENT_DOWN, 2);
    ev(SV_RDY_X, SV_RDY_ROW_Y((int)RAR_ANGLE), TOUCH_EVENT_UP, 2);
    CHECK_EQ(angle_pick_count(), 2);
    tap_entry(0);

    ev(SV_FOOT_SET_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, 1);
    ev(SV_FOOT_SET_X, SV_FOOT_Y, TOUCH_EVENT_UP, 1);
    const ui_setlist_t *sl = servo_screen_list();
    CHECK(sl->open);
    ui_setlist_scroll_to((ui_setlist_t *)sl, 6);
    const gfx_rect_t row = ui_setlist_row_rect(sl, 6);
    ev(row.x + 60, row.y + row.h / 2, TOUCH_EVENT_DOWN, 1);
    ev(row.x + 60, row.y + row.h / 2, TOUCH_EVENT_UP, 1);
    CHECK(!servo_screen_list()->open);
    CHECK_EQ(angle_pick_count(), 2);
}

/* A search owed while Bluetooth LE starts is dropped when the IMU goes off
 * before it runs; a search without sync with the C6 or on a C6 that failed
 * is refused, and its entry names the state. */
TEST_CASE(a_search_owed_is_dropped_by_imu_off_and_refused_on_a_failed_c6)
{
    safe_on(SERVO_FN_RUDDER, IMU_LINK_OFF);
    CHECK(imu_screen_search());
    CHECK(imu_screen_search_owed());
    settings_set(SET_IMU_EN, 0.0f);
    g_v.link = IMU_LINK_STARTING;
    view_now();
    CHECK(!imu_screen_search_owed());
    g_v.link = IMU_LINK_READY;
    view_now();
    CHECK_EQ(imu_took().kind, IMU_CMD_NONE);

    g_v.link = IMU_LINK_NO_BT;
    view_now();
    CHECK(!imu_screen_search());
    tap_angle_row();
    bool refused = false;
    CHECK_STR_EQ(entry(1, &refused), TR(IMU_ST_NO_BT));
    CHECK(refused);
    tap_entry(0);

    g_v.link = IMU_LINK_C6_FAILED;
    view_now();
    CHECK(!imu_screen_search());
    CHECK(!imu_screen_bind(0u));
    tap_angle_row();
    CHECK_STR_EQ(entry(1, &refused), TR(IMU_ST_C6_FAILED));
    CHECK(refused);
}

/**
 * @brief The ANGLE row of the card shown, TEST's or LIMIT RUN's.
 * @param test TEST's, else LIMIT RUN's
 * @return the row
 */
static rdy_row_t run_angle(bool test)
{
    rdy_row_t r;
    CHECK(test ? test_ready_row(TRR_ANGLE, &r)
               : lim_ready_row(LRR_ANGLE, &r));
    return r;
}

/**
 * @brief The text the card's gate refuses the hold with now.
 * @param test TEST's, else LIMIT RUN's
 * @return it; "" when none
 */
static const char *run_gate(bool test)
{
    const char *why = test ? test_hold_blocked(NULL) : lim_hold_blocked(NULL);
    return why != NULL ? why : "";
}

/* TEST and LIMIT RUN: ANGLE names the source, green while a run reads it,
 * amber with OFF or NO READING while it does not; the gate is the same
 * either way.  A tap opens the list on a SAFE bench. */
TEST_CASE(test_and_limit_run_show_the_angle_and_never_refuse_on_it)
{
    for (int k = 0; k < 2; ++k) {
        const bool test = (k == 0);
        safe_on(test ? SERVO_FN_TEST : SERVO_FN_LIMRUN, IMU_LINK_OFF);
        /* The IOmcu holds the AS5600 on, as the panel reports it. */
        servo_screen_set_enc_held(true);
        rdy_row_t r = run_angle(test);
        CHECK_STR_EQ(r.label, TR(RDY_ANGLE));
        CHECK(r.fix);
        CHECK_STR_EQ(r.value, "AS5600");
        CHECK_EQ(r.st, (uint8_t)RDY_OK);
        CHECK(r.why == NULL);
        char gate[128];
        snprintf(gate, sizeof(gate), "%s", run_gate(test));

        settings_set(SET_ENC_EN, 0.0f);
        ra_ms(100u);
        r = run_angle(test);
        char want[64];
        snprintf(want, sizeof(want), TR(SV_ANG_RUN_OFF), "AS5600");
        CHECK_STR_EQ(r.value, want);
        CHECK_EQ(r.st, (uint8_t)RDY_WARN);
        CHECK(r.why == NULL);
        CHECK_STR_EQ(run_gate(test), gate);

        /* The IMU in force with no reading handed over, as the panel
         * hands the IMU's when it is the source. */
        settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_IMU);
        ra.enc = false;
        const servo_test_enc_t none = { .valid = false,
                                        .taken_ms = g_rig.ms };
        servo_screen_encoder(&none);
        ra_ms(100u);
        r = run_angle(test);
        snprintf(want, sizeof(want), TR(SV_ANG_RUN_NONE), "IMU X+");
        CHECK_STR_EQ(r.value, want);
        CHECK_EQ(r.st, (uint8_t)RDY_WARN);
        CHECK_STR_EQ(run_gate(test), gate);

        const int row = test ? (int)TRR_ANGLE : (int)LRR_ANGLE;
        ev(SV_RDY_X, SV_RDY_ROW_Y(row), TOUCH_EVENT_DOWN, 2);
        ev(SV_RDY_X, SV_RDY_ROW_Y(row), TOUCH_EVENT_UP, 2);
        CHECK_EQ(angle_pick_count(), 2);
        bool refused = false;
        CHECK_STR_EQ(entry(0, &refused), TR(SV_ANG_AS5600_OFF));
        CHECK(refused);
        scr->render(&cv, 0);
        /* ARMED: refused, the list shut. */
        tap_entry(1);                           /* SEARCH: stays open */
        CHECK_EQ(angle_pick_count(), 2);
        servo_screen_set_armed(true);
        ra.armed = true;
        ra_ms(40u);
        CHECK_EQ(angle_pick_count(), -1);
        ev(SV_RDY_X, SV_RDY_ROW_Y(row), TOUCH_EVENT_DOWN, 2);
        ev(SV_RDY_X, SV_RDY_ROW_Y(row), TOUCH_EVENT_UP, 2);
        CHECK_EQ(angle_pick_count(), -1);
    }
    /* Ten rows on LIMIT RUN end over the rail line. */
    CHECK_EQ((int)LRR_COUNT, READY_ROWS_MAX);
    CHECK(ready_row_rect(READY_ROWS_MAX - 1).y + READY_ROW_H
          <= ready_rail_rect().y);
}

int main(void)
{
    RUN(the_angle_row_opens_the_list_with_a_reading_and_without);
    RUN(the_as5600_pick_sets_the_source_and_closes_the_list);
    RUN(an_imu_pick_binds_it_switches_the_imu_on_and_sets_the_source);
    RUN(search_switches_the_imu_on_and_scans_once_bluetooth_runs);
    RUN(the_list_follows_the_scan_to_no_imu_found_and_a_failed_scan);
    RUN(eight_found_fill_ten_entries_and_a_bound_one_drops_the_eighth);
    RUN(the_list_scans_by_itself_with_nothing_bound_and_nothing_scanned);
    RUN(a_finger_on_an_entry_keeps_the_entries_under_it);
    RUN(the_list_is_refused_armed_and_closes_when_the_bench_arms);
    RUN(rudder_autos_angle_row_and_list_row_open_the_list);
    RUN(a_search_owed_is_dropped_by_imu_off_and_refused_on_a_failed_c6);
    RUN(test_and_limit_run_show_the_angle_and_never_refuse_on_it);
    return test_summary("servo_angle_pick");
}
