/**
 * @file render_imu.c
 * @brief The Bluetooth IMU's views for tools/render_ui.py (render_imu.h).
 *
 * imu: Bluetooth LE synced on a C6 that reports 2.13.0, no sensor bound.
 * imu-scan: SCAN tapped, two sensors listed, the nearer first, and four
 * other devices hidden.
 * imu-connected: bound to it, 50Hz, ZERO taken, the surface at +12.3° on
 * X, the battery at 3.95V and the calibration done.  setup-sensors-imu:
 * SENSORS on the IMU entry, the IMU on and connected at 50Hz.
 *
 * SPDX-License-Identifier: MIT
 */
#include "render_imu.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "imu_screen.h"
#include "sensors_pane.h"
#include "sensors_rows.h"
#include "settings.h"
#include "settings_screen.h"
#include "ui_layout.h"
#include "ui_screen.h"

bool render_imu_view(const char *view)
{
    return strcmp(view, "imu") == 0 || strncmp(view, "imu-", 4) == 0
           || strcmp(view, "setup-sensors-imu") == 0;
}

/**
 * @brief A sensor of the found list.
 * @param d    the device
 * @param addr its address, colons
 * @param name its name, "" for none
 * @param rssi its RSSI, dBm
 */
static void device(imu_dev_t *d, const char *addr, const char *name,
                   int16_t rssi)
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

void render_imu_pose(const char *view)
{
    if (!render_imu_view(view)) {
        return;
    }
    imu_view_t v;
    memset(&v, 0, sizeof(v));
    v.link = IMU_LINK_READY;
    snprintf(v.version, sizeof(v.version), "2.13.0");
    v.r.battery_v = NAN;
    v.r.temp_c = NAN;
    settings_set(SET_IMU_EN, 1.0f);
    if (strcmp(view, "setup-sensors-imu") == 0) {
        sensors_rows_set_imu(IMU_LINK_CONNECTED, 500u);
        settings_screen_open_on(SET_CAT_SENSORS, SENS_ENTRY_IMU);
        return;
    }
    if (strcmp(view, "imu-connected") == 0) {
        connected(&v);
    }
    imu_screen_set(&v);
    if (strcmp(view, "imu-scan") == 0) {
        tap_rect(imu_screen_row_rect(0));   /* SCAN FOR SENSORS */
        imu_cmd_t c;
        while (imu_screen_take(&c)) { }
        v.found_n = 2u;
        device(&v.found[0], "00:0C:BF:08:19:29", "HC-06", -50);
        device(&v.found[1], "00:0C:BF:0A:44:17", "HC-06", -71);
        v.hidden = 4u;
        imu_screen_set(&v);
    }
}
