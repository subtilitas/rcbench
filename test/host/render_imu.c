/**
 * @file render_imu.c
 * @brief The Bluetooth IMU's views for tools/render_ui.py (render_imu.h).
 *
 * setup-wireless-imu: WIRELESS on the IMU entry, the IMU on, bound to an
 * HC-06 and connected at 50Hz, ZERO taken, the surface at +12.3° on X, the
 * battery at 3.95V, the C6 at 2.13.0.  setup-wireless-imu-cal: the same,
 * the list dragged up to ZERO, CLEAR ZERO and CALIBRATE.
 * setup-wireless-imu-error: the IMU on with no sensor bound after a scan
 * that found none: LAST ERROR under BOUND SENSOR.
 * setup-wireless-imu-search: SEARCH from SCAN FOR SENSOR, two sensors
 * listed, the nearer first, and four other devices hidden.
 * setup-wireless-imu-off: WIRELESS on the IMU entry with the IMU off,
 * ANGLE SOURCE under it.  setup-wireless-dial: WIRELESS on the DIAL entry,
 * the Dial on and linked, a scan that found two Dials, the second bound.
 * setup-wireless-pnode: WIRELESS on the POWER NODE entry, the node on and
 * connected, INA228 SOURCE NODE, a scan that found one node named
 * XT90 MAIN, bound, its cell at 3.92V, its INA228 found at 0x40 on NODE
 * SHUNT 500uΩ, its address setting AUTO.
 * setup-wireless-pnode-shunt: the same, the list dragged to its end,
 * NODE INA228, INA228 ADDRESS and NODE SHUNT in view.
 * setup-wireless-pnode-name: the same, NODE NAME tapped, the name keyboard
 * open on XT90 MAIN.
 * setup-wireless-bal: WIRELESS on the BALANCE NODE entry, the node on and
 * reporting, a scan that found two nodes, the first bound and named
 * PROP 13X6, its cell at 3.91V.
 * setup-wireless-search: SEARCH from BOUND NODE of the power node, two
 * nodes found, the first bound and connected.  setup-wireless-search-bind:
 * the same with the second node tapped, the bind question open.
 * setup-wireless-search-scan: SEARCH from SCAN FOR DIAL, the scan 7s from
 * its end, nothing listed yet.
 *
 * SPDX-License-Identifier: MIT
 */
#include "render_imu.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "imu_view.h"
#include "sensors_rows.h"
#include "settings.h"
#include "settings_screen.h"
#include "ui_layout.h"
#include "ui_screen.h"
#include "wireless_bal.h"
#include "wireless_pane.h"
#include "wl_search.h"

bool render_imu_view(const char *view)
{
    return strncmp(view, "setup-wireless", 14) == 0;
}

/**
 * @brief A sensor of the found list.
 * @param d    the device
 * @param addr its address, colons
 * @param name its name, "" for none
 * @param rssi its RSSI, dBm
 */
static void device(imu_dev_t *d, const char *addr, const char *name,
                   int8_t rssi)
{
    snprintf(d->addr, sizeof(d->addr), "%s", addr);
    snprintf(d->name, sizeof(d->name), "%s", name);
    d->rssi = rssi;
    d->flags = IMU_DEV_UUID_18F0;
}

/**
 * @brief A tap of contact 1 at the middle of @p r of the body, through the
 *        router.
 * @param r the control
 */
static void tap_rect(gfx_rect_t r)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)(r.x + r.w / 2),
                                   .y = (int16_t)(r.y + r.h / 2 + UI_BAND_H),
                                   .strength = 40 } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

/**
 * @brief The view of the bound and connected sensor.
 * @param v takes it
 */
static void connected(imu_view_t *v)
{
    v->link = IMU_LINK_CONNECTED;
    snprintf(v->bound_addr, sizeof(v->bound_addr), "00:0C:BF:08:19:29 P");
    snprintf(v->bound_name, sizeof(v->bound_name), "HC-06");
    v->r.valid = true;
    v->r.taken_ms = 1000u;
    v->r.zeroed = true;
    v->r.zero[0] = -1.6f;
    v->r.zero[1] = 0.4f;
    v->r.zero[2] = 87.0f;
    v->r.angle[0] = 10.7f;
    v->r.angle[1] = 0.3f;
    v->r.angle[2] = 87.2f;
    v->r.rate_dhz = 500u;
    v->r.battery_v = 3.95f;
    v->r.temp_c = 24.5f;
    v->r.frames = 41236u;
    v->cal = IMU_CAL_DONE;
}

/**
 * @brief WIRELESS on the DIAL entry: the Dial on and linked, two Dials
 *        found, the second bound.
 */
static void pose_dial(void)
{
    dial_view_t d;
    memset(&d, 0, sizeof(d));
    d.link = DIAL_LINK_UP;
    d.scan_done = true;
    d.found_n = 2u;
    snprintf(d.found[0].addr, sizeof(d.found[0].addr), "A0:B1:C2:7F:3A:10 R");
    snprintf(d.found[0].name, sizeof(d.found[0].name), "DIAL-3A10");
    d.found[0].rssi = -52;
    snprintf(d.found[1].addr, sizeof(d.found[1].addr), "A0:B1:C2:41:0C:22 R");
    snprintf(d.found[1].name, sizeof(d.found[1].name), "DIAL-0C22");
    d.found[1].rssi = -67;
    snprintf(d.bound_addr, sizeof(d.bound_addr), "%s", d.found[0].addr);
    snprintf(d.bound_name, sizeof(d.bound_name), "%s", d.found[0].name);
    d.proto = 1u;
    settings_set(SET_DIAL_EN, 1.0f);
    wireless_pane_set_dial(&d);
    settings_screen_open_on(SET_CAT_WIRELESS, WL_ENTRY_DIAL);
}

/** @brief WIRELESS on the POWER NODE entry: on, connected, NODE chosen,
 *         one node found and bound, its cell at 3.92V, its INA228 at 0x40
 *         on 500uΩ. */
static void pose_pnode(void)
{
    pnode_view_t p;
    memset(&p, 0, sizeof(p));
    p.link = PNODE_LINK_UP;
    p.scan_done = true;
    p.found_n = 1u;
    snprintf(p.found[0].addr, sizeof(p.found[0].addr), "7C:DF:A1:12:34:56 P");
    snprintf(p.found[0].name, sizeof(p.found[0].name), "XT90 MAIN");
    p.found[0].rssi = -58;
    snprintf(p.bound_addr, sizeof(p.bound_addr), "%s", p.found[0].addr);
    snprintf(p.bound_name, sizeof(p.bound_name), "%s", p.found[0].name);
    p.proto = 1u;
    p.have_status = true;
    p.vbat_mv = 3920u;
    p.have_cfg = true;
    snprintf(p.node_name, sizeof(p.node_name), "XT90 MAIN");
    p.i228_set = 0u;
    p.have_info = true;
    p.i228_addr = 0x40u;
    p.shunt_uohm = 500u;
    settings_set(SET_PNODE_EN, 1.0f);
    settings_set(SET_I228_SRC, 1.0f);
    settings_set(SET_PNODE_UOHM, 500.0f);
    wireless_pane_set_pnode(&p);
    settings_screen_open_on(SET_CAT_WIRELESS, WL_ENTRY_PNODE);
}

/**
 * @brief The open SETUP list dragged up by its pane's height, from 8px
 *        above the pane's foot, through the router.
 */
static void drag_list_up(void)
{
    const gfx_rect_t pane = settings_screen_list_rect();
    const int y0 = UI_BAND_H + pane.y + pane.h - 8;
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = 300, .y = (int16_t)y0,
                                   .strength = 40 } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_MOVE;
    for (int y = y0 - 8; y > y0 - pane.h; y -= 8) {
        e.point.y = (int16_t)y;
        ui_router_event(&e);
    }
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

/**
 * @brief The open SETUP list dragged up by @p dy px, 10px at the least
 *        and its pane's height less 16px at the most, 1px at a time from
 *        8px above the pane's foot, through the router.
 * @param dy The drag, px.
 */
static void drag_list_by(int dy)
{
    const gfx_rect_t pane = settings_screen_list_rect();
    const int y0 = UI_BAND_H + pane.y + pane.h - 8;
    if (dy > pane.h - 16) {
        dy = pane.h - 16;
    }
    if (dy < 10) {
        dy = 10;            /* past the 8px slop, or the list stays */
    }
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = 300, .y = (int16_t)y0,
                                   .strength = 40 } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_MOVE;
    for (int y = y0 - 1; y >= y0 - dy; --y) {
        e.point.y = (int16_t)y;
        ui_router_event(&e);
    }
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

/**
 * @brief WIRELESS on the BALANCE NODE entry: the node on and reporting,
 *        two nodes found, the first bound, its cell at 3.91V.
 */
static void pose_bal(void)
{
    bal_view_t b;
    memset(&b, 0, sizeof(b));
    b.link = BAL_LINK_UP;
    b.scan_done = true;
    b.found_n = 2u;
    snprintf(b.found[0].addr, sizeof(b.found[0].addr), "7C:DF:A1:12:34:56 R");
    snprintf(b.found[0].name, sizeof(b.found[0].name), "PROP 13X6");
    b.found[0].rssi = -58;
    snprintf(b.found[1].addr, sizeof(b.found[1].addr), "7C:DF:A1:12:9A:0E R");
    snprintf(b.found[1].name, sizeof(b.found[1].name), "rcbench-bal-9A0E");
    b.found[1].rssi = -74;
    snprintf(b.bound_addr, sizeof(b.bound_addr), "%s", b.found[0].addr);
    snprintf(b.bound_name, sizeof(b.bound_name), "%s", b.found[0].name);
    b.info_role = 2u;
    b.info_proto = 1u;
    b.bat_known = true;
    b.bat_mv = 3912u;
    b.have_cfg = true;
    snprintf(b.node_name, sizeof(b.node_name), "PROP 13X6");
    settings_set(SET_BNODE_EN, 1.0f);
    wireless_pane_set_bal(&b);
    settings_screen_open_on(SET_CAT_WIRELESS, WL_ENTRY_BAL);
}

/**
 * @brief SEARCH from BOUND NODE of the power node: two nodes found, the
 *        first bound and connected; with @p ask the second tapped.
 * @param ask Open the bind question on the second node.
 */
static void pose_search(bool ask)
{
    pnode_view_t p;
    memset(&p, 0, sizeof(p));
    p.link = PNODE_LINK_UP;
    p.scan_done = true;
    p.found_n = 2u;
    snprintf(p.found[0].addr, sizeof(p.found[0].addr), "7C:DF:A1:12:34:56 P");
    snprintf(p.found[0].name, sizeof(p.found[0].name), "rcbench-pwr-3456");
    p.found[0].rssi = -58;
    snprintf(p.found[1].addr, sizeof(p.found[1].addr), "7C:DF:A1:12:9A:0E P");
    snprintf(p.found[1].name, sizeof(p.found[1].name), "rcbench-pwr-9A0E");
    p.found[1].rssi = -74;
    snprintf(p.bound_addr, sizeof(p.bound_addr), "%s", p.found[0].addr);
    snprintf(p.bound_name, sizeof(p.bound_name), "%s", p.found[0].name);
    p.proto = 1u;
    p.have_status = true;
    p.vbat_mv = 3920u;
    settings_set(SET_PNODE_EN, 1.0f);
    wireless_pane_set_pnode(&p);
    settings_screen_open_on(SET_CAT_WIRELESS, WL_ENTRY_PNODE);
    wireless_pane_pnode_bind_row.tap();
    if (ask) {
        tap_rect(wl_search_row_rect(1));
    }
}

/** @brief SEARCH from SCAN FOR DIAL: the scan 7s from its end, nothing
 *         listed yet, no Dial bound. */
static void pose_search_scan(void)
{
    dial_view_t d;
    memset(&d, 0, sizeof(d));
    d.link = DIAL_LINK_SCANNING;
    d.scan_left_s = 7u;
    settings_set(SET_DIAL_EN, 1.0f);
    wireless_pane_set_dial(&d);
    settings_screen_open_on(SET_CAT_WIRELESS, WL_ENTRY_DIAL);
    wireless_pane_bind_row.tap();
}

void render_imu_pose(const char *view)
{
    if (!render_imu_view(view)) {
        return;
    }
    if (strcmp(view, "setup-wireless-search") == 0
        || strcmp(view, "setup-wireless-search-bind") == 0) {
        pose_search(strcmp(view, "setup-wireless-search-bind") == 0);
        return;
    }
    if (strcmp(view, "setup-wireless-search-scan") == 0) {
        pose_search_scan();
        return;
    }
    if (strcmp(view, "setup-wireless-dial") == 0) {
        pose_dial();
        return;
    }
    if (strcmp(view, "setup-wireless-pnode") == 0) {
        pose_pnode();
        return;
    }
    if (strcmp(view, "setup-wireless-pnode-shunt") == 0) {
        pose_pnode();
        drag_list_up();
        return;
    }
    if (strcmp(view, "setup-wireless-pnode-name") == 0) {
        pose_pnode();
        wireless_pane_pnode_name_row.tap();
        return;
    }
    if (strcmp(view, "setup-wireless-bal") == 0) {
        pose_bal();
        return;
    }
    imu_view_t v;
    memset(&v, 0, sizeof(v));
    v.link = IMU_LINK_READY;
    snprintf(v.version, sizeof(v.version), "2.13.0");
    v.r.battery_v = NAN;
    v.r.temp_c = NAN;
    if (strcmp(view, "setup-wireless-imu-off") == 0) {
        settings_screen_open_on(SET_CAT_WIRELESS, WL_ENTRY_IMU);
        return;
    }
    settings_set(SET_IMU_EN, 1.0f);
    if (strcmp(view, "setup-wireless-imu-error") == 0) {
        v.scan_done = true;
        v.err = IMU_ERR_NONE_FOUND;
        v.hidden = 4u;
    } else {
        connected(&v);
    }
    imu_view_set(&v);
    sensors_rows_set_imu(v.link, v.r.rate_dhz);
    settings_screen_open_on(SET_CAT_WIRELESS, WL_ENTRY_IMU);
    if (strcmp(view, "setup-wireless-imu-cal") == 0) {
        /* Dragged until CALIBRATE, the last of the IMU's own rows, stands
         * at the pane's foot. */
        wl_row_t rows[WL_ENTRY_ROWS_MAX];
        const int n = wireless_pane_rows(WL_ENTRY_IMU, rows,
                                         WL_ENTRY_ROWS_MAX);
        int row = 0;
        while (row < n && rows[row].custom != &wireless_pane_imu_cal_row) {
            ++row;
        }
        const gfx_rect_t pane = settings_screen_list_rect();
        for (int k = 0; k < 4; ++k) {
            const gfx_rect_t r = settings_screen_row_rect(row);
            if (r.y + r.h <= pane.y + pane.h) {
                break;
            }
            drag_list_by(r.y + r.h - (pane.y + pane.h));
        }
        return;
    }
    if (strcmp(view, "setup-wireless-imu-search") == 0) {
        wireless_pane_imu_scan_row.tap();
        imu_cmd_t c;
        while (imu_view_take(&c)) { }
        v.scan_done = true;
        v.found_n = 2u;
        device(&v.found[0], "00:0C:BF:08:19:29", "HC-06", -50);
        device(&v.found[1], "00:0C:BF:0A:44:17", "", -71);
        v.hidden = 4u;
        imu_view_set(&v);
    }
}
