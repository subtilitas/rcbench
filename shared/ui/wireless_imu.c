/**
 * @file wireless_imu.c
 * @brief SETUP > WIRELESS' IMU entry: its custom rows, SEARCH's view of
 *        the sensors and the live rows' repaint.  See wireless_pane.h;
 *        the view and the commands are imu_view.h's.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wireless_pane.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "imu.h"
#include "imu_view.h"
#include "sensors_rows.h"
#include "settings_screen.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "wl_search.h"

/** @brief The age on FRAMES repaints at most this often between readings,
 *         s: 10 times a second. */
#define LIVE_PERIOD_S 0.1f
/** @brief SENSOR BATTERY reads "---" above this: 0xFFFF reads 655.35V. */
#define BATTERY_MAX_V 6.0f

/** @brief imu_view_rev() as SETUP last drew it. */
static uint32_t s_drawn_rev;
/** @brief imu_view_read_rev() as SETUP last drew it. */
static uint32_t s_drawn_read;
/** @brief The time since the live rows last repainted, s. */
static float    s_live_s;

/* ------------------------------------------------------ SCAN FOR SENSOR */

/** @brief SCAN FOR SENSOR's value: the search that waits for Bluetooth LE,
 *         the scan under way, or what the last scan did.
 *  @param out The text. @param n Its size. */
static void scan_value(char *out, size_t n)
{
    const imu_view_t *v = imu_view_get();
    if (v->link == IMU_LINK_SCANNING) {
        snprintf(out, n, "%s", TR(DIAL_SCAN_RUNS));
    } else if (imu_view_search_owed()) {
        snprintf(out, n, "%s", TR(IMU_ST_STARTING));
    } else if (!v->scan_done) {
        snprintf(out, n, "%s", TR(DIAL_SCAN_GO));
    } else if (v->scan_cut) {
        snprintf(out, n, "%s", TR(DIAL_SCAN_CUT));
    } else if (v->found_n == 0u) {
        snprintf(out, n, "%s", TR(DIAL_SCAN_NONE));
    } else {
        snprintf(out, n, TR(DIAL_SCAN_FOUND), (unsigned)v->found_n);
    }
}

/** @brief A scan for SEARCH: refused while ARMED, while one runs and
 *         while Bluetooth LE has no C6, each with its text on the band;
 *         else asked, with the IMU switched on when it is off. */
static void scan_tap(void)
{
    const imu_link_t link = imu_view_get()->link;
    if (ui_router_status()->armed) {
        ui_router_set_alert(TR(IMU_SCAN_ARMED));
        return;
    }
    if (link == IMU_LINK_SCANNING) {
        ui_router_set_alert(TR(NODE_SCAN_RUNNING));
        return;
    }
    if (link == IMU_LINK_NO_BT || link == IMU_LINK_C6_FAILED) {
        ui_router_set_alert(TR(IMU_SCAN_OFF));
        return;
    }
    (void)imu_view_search();
}

/* --------------------------------------------------------- BOUND SENSOR */

/** @brief BOUND SENSOR's value: the bound sensor's name, its address, or
 *         NONE.  @param out The text. @param n Its size. */
static void bind_value(char *out, size_t n)
{
    const imu_view_t *v = imu_view_get();
    if (v->bound_name[0] != '\0') {
        snprintf(out, n, "%s", v->bound_name);
    } else if (v->bound_addr[0] != '\0') {
        snprintf(out, n, "%s", v->bound_addr);
    } else {
        snprintf(out, n, "%s", TR(DIAL_BIND_NONE));
    }
}

/**
 * @brief SEARCH's view of the sensors: the scan, the found sensors, the
 *        devices the scan heard and did not list, the bound one and the
 *        link state.
 * @param out The view, zeroed.
 */
static void search_view(wls_view_t *out)
{
    const imu_view_t *v = imu_view_get();
    out->scanning = v->link == IMU_LINK_SCANNING;
    out->scan_left_s = v->scan_left_s;
    out->scan_done = v->scan_done;
    out->scan_cut = v->scan_cut;
    const unsigned n = v->found_n < IMU_FOUND_MAX ? v->found_n
                                                  : IMU_FOUND_MAX;
    out->found_n = (uint8_t)n;
    for (unsigned i = 0u; i < n; ++i) {
        wl_search_put(out, i, v->found[i].addr, v->found[i].name,
                      v->found[i].rssi);
    }
    out->hidden = out->scanning ? 0u : v->hidden;
    snprintf(out->bound_addr, sizeof(out->bound_addr), "%s", v->bound_addr);
    /* SEARCH's names hold 23 bytes, a scan's 31: the tail goes. */
    snprintf(out->bound_name, sizeof(out->bound_name), "%.*s",
             (int)sizeof(out->bound_name) - 1, v->bound_name);
    if (v->link == IMU_LINK_OFF) {
        snprintf(out->word, sizeof(out->word), "%s", TR(OFF));
    } else {
        (void)sensors_rows_imu_word(v->link, v->r.rate_dhz, out->word,
                                    sizeof(out->word));
    }
    out->word_ink = sensors_rows_imu_color(v->link);
}

/** @brief SEARCH's bind of found sensor @p i, queued for the controller;
 *         refused on the band while a calibration runs or Bluetooth LE
 *         does not.  @param i Its row. */
static void search_bind(uint8_t i)
{
    if (!imu_view_bind(i)) {
        ui_router_set_alert(TR(IMU_BIND_BUSY));
    }
}

/** @brief SEARCH's UNBIND: SETUP forgets the bound sensor, with a save
 *         asked; the controller ends the link as it takes the empty
 *         address. */
static void search_unbind(void)
{
    settings_set_text(SET_TEXT_IMU_ADDR, "");
    settings_set_text(SET_TEXT_IMU_NAME, "");
    settings_request_save();
}

/** @brief The IMU's entry as SEARCH asks it. */
static const wls_device_t k_search = {
    .entry = WL_ENTRY_IMU,
    .kind = TX_IMU_ENTRY,
    .bind_scanning = TX_IMU_BIND_SCANNING,
    .bind_gone = TX_IMU_BIND_GONE,
    .view = search_view,
    .scan = scan_tap,
    .bind = search_bind,
    .unbind = search_unbind,
};

/** @brief SCAN FOR SENSOR tapped: SEARCH opens and a scan is asked. */
static void scan_open(void)
{
    wl_search_open(&k_search, true);
}

/** @brief BOUND SENSOR tapped: SEARCH opens on the last scan's list. */
static void bind_open(void)
{
    wl_search_open(&k_search, false);
}

/** @brief SCAN FOR SENSOR: the last scan's result; a tap opens SEARCH and
 *         scans. */
const ui_setlist_custom_t wireless_pane_imu_scan_row = {
    .label = TX_IMU_ROW_SCAN,
    .help = TX_IMU_ROW_SCAN_HELP,
    .value_text = scan_value,
    .tap = scan_open,
};

/** @brief BOUND SENSOR: the bound sensor; a tap opens SEARCH on the last
 *         scan's list. */
const ui_setlist_custom_t wireless_pane_imu_bind_row = {
    .label = TX_IMU_ROW_BIND,
    .help = TX_IMU_ROW_BIND_HELP,
    .value_text = bind_value,
    .tap = bind_open,
};

/* ----------------------------------------------------------- LAST ERROR */

/**
 * @brief The last operation's error in words, LAST ERROR's line.
 * @return The words; NULL for none.
 */
static const char *err_line(void)
{
    static char buf[96];
    const imu_view_t *v = imu_view_get();
    switch (v->err) {
    case IMU_ERR_CONNECT:  return TR(IMU_ERR_CONNECT);
    case IMU_ERR_NOT_WIT:  return TR(IMU_ERR_NOT_WIT);
    case IMU_ERR_WRITE:    return TR(IMU_ERR_WRITE);
    case IMU_ERR_NONE_FOUND:
        /* The bound sensor holds the link and does not advertise. */
        return imu_view_bound_linked(v) ? TR(IMU_ERR_NONE_OTHER)
                                        : TR(IMU_ERR_NONE_FOUND);
    case IMU_ERR_SCAN:
        snprintf(buf, sizeof(buf), TR(IMU_ERR_SCAN), (unsigned)v->err_code);
        return buf;
    case IMU_ERR_NONE:
    default:
        return NULL;
    }
}

/** @brief LAST ERROR: the last scan's, connect's or write's error in its
 *         help line, in the warning colour; shown only while there is
 *         one. */
const ui_setlist_custom_t wireless_pane_imu_err_row = {
    .label = TX_IMU_ROW_ERR,
    .help = TX_COUNT,
    .locked = err_line,
};

bool wireless_pane_imu_err(void)
{
    return err_line() != NULL;
}

/* ------------------------------------------------- the live values */

/**
 * @brief An angle row's value: the axis' degrees from the zero in
 *        hundredths, or "---" with no reading.
 * @param axis The axis.
 * @param out  The text.
 * @param n    Its size.
 */
static void angle_value(imu_axis_t axis, char *out, size_t n)
{
    const imu_reading_t *r = &imu_view_get()->r;
    if (!r->valid) {
        snprintf(out, n, "---");
        return;
    }
    char deg[12];
    snprintf(out, n, "%s°",
             imu_deg_text(deg, sizeof(deg), imu_deg(r, axis, false), true));
}

/**
 * @brief An angle row's name: the axis, and "read by runs" for the one
 *        IMU AXIS names.
 * @param axis  The axis.
 * @param plain Its name.
 * @param run   Its name as the axis the runs read.
 * @return The name's text.
 */
static ui_text_id_t angle_label(imu_axis_t axis, ui_text_id_t plain,
                                ui_text_id_t run)
{
    return settings_get_int(SET_IMU_AXIS) == (int)axis ? run : plain;
}

/** @brief ANGLE X's value. @param out The text. @param n Its size. */
static void x_value(char *out, size_t n) { angle_value(IMU_AXIS_X, out, n); }
/** @brief ANGLE Y's value. @param out The text. @param n Its size. */
static void y_value(char *out, size_t n) { angle_value(IMU_AXIS_Y, out, n); }
/** @brief ANGLE Z's value. @param out The text. @param n Its size. */
static void z_value(char *out, size_t n) { angle_value(IMU_AXIS_Z, out, n); }
/** @brief ANGLE X's name. @return Its text. */
static ui_text_id_t x_label(void)
{
    return angle_label(IMU_AXIS_X, TX_IMU_ROW_ANG_X, TX_IMU_ROW_ANG_X_RUN);
}
/** @brief ANGLE Y's name. @return Its text. */
static ui_text_id_t y_label(void)
{
    return angle_label(IMU_AXIS_Y, TX_IMU_ROW_ANG_Y, TX_IMU_ROW_ANG_Y_RUN);
}
/** @brief ANGLE Z's name. @return Its text. */
static ui_text_id_t z_label(void)
{
    return angle_label(IMU_AXIS_Z, TX_IMU_ROW_ANG_Z, TX_IMU_ROW_ANG_Z_RUN);
}

/** @brief ANGLE X: the live angle about X, read only. */
const ui_setlist_custom_t wireless_pane_imu_x_row = {
    .label = TX_IMU_ROW_ANG_X,
    .help = TX_IMU_ROW_ANGLE_HELP,
    .value_text = x_value,
    .live = true,
    .label_of = x_label,
};

/** @brief ANGLE Y: the live angle about Y, read only. */
const ui_setlist_custom_t wireless_pane_imu_y_row = {
    .label = TX_IMU_ROW_ANG_Y,
    .help = TX_IMU_ROW_ANGLE_HELP,
    .value_text = y_value,
    .live = true,
    .label_of = y_label,
};

/** @brief ANGLE Z: the live angle about Z, read only. */
const ui_setlist_custom_t wireless_pane_imu_z_row = {
    .label = TX_IMU_ROW_ANG_Z,
    .help = TX_IMU_ROW_ANGLE_HELP,
    .value_text = z_value,
    .live = true,
    .label_of = z_label,
};

/** @brief FRAMES' value: the frames of the last second and the newest
 *         angle's age, or the rate and NO ANGLE.
 *  @param out The text. @param n Its size. */
static void frames_value(char *out, size_t n)
{
    const imu_reading_t *r = &imu_view_get()->r;
    if (r->valid) {
        snprintf(out, n, TR(IMU_RATE_AGE), (double)r->rate_dhz / 10.0,
                 (unsigned)imu_view_age_ms());
    } else {
        snprintf(out, n, TR(IMU_RATE_NONE), (double)r->rate_dhz / 10.0);
    }
}

/** @brief FRAMES: the angle frames a second and the newest one's age. */
const ui_setlist_custom_t wireless_pane_imu_frames_row = {
    .label = TX_IMU_ROW_FRAMES,
    .help = TX_IMU_ROW_FRAMES_HELP,
    .value_text = frames_value,
    .live = true,
};

/** @brief SENSOR BATTERY's value: the cell the sensor reports, "---" for
 *         none.  @param out The text. @param n Its size. */
static void bat_value(char *out, size_t n)
{
    const float v = imu_view_get()->r.battery_v;
    if (isfinite(v) && v > 0.0f && v <= BATTERY_MAX_V) {
        snprintf(out, n, "%.2fV", (double)v);
    } else {
        snprintf(out, n, "---");
    }
}

/** @brief SENSOR BATTERY: the sensor's cell, read only. */
const ui_setlist_custom_t wireless_pane_imu_bat_row = {
    .label = TX_IMU_ROW_BAT,
    .help = TX_IMU_ROW_BAT_HELP,
    .value_text = bat_value,
    .live = true,
};

/** @brief C6 VERSION's value: the radio's firmware, "---" unknown.
 *  @param out The text. @param n Its size. */
static void c6_value(char *out, size_t n)
{
    const char *ver = imu_view_get()->version;
    snprintf(out, n, "%s", ver[0] != '\0' ? ver : "---");
}

/** @brief C6 VERSION: the ESP32-C6's firmware, read only. */
const ui_setlist_custom_t wireless_pane_imu_c6_row = {
    .label = TX_IMU_ROW_C6,
    .help = TX_IMU_ROW_C6_HELP,
    .value_text = c6_value,
};

/* ------------------------------------------- ZERO, CLEAR ZERO, CALIBRATE */

/** @brief ZERO takes no tap without a valid reading.
 *  @return Why, NULL while it takes one. */
static const char *zero_locked(void)
{
    return imu_view_get()->r.valid ? NULL : TR(IMU_LOCK_NO_ANGLE);
}

/** @brief ZERO tapped: the angles now as the zero. */
static void zero_tap(void)
{
    (void)imu_view_zero();
}

/** @brief ZERO: a tap takes the angles now as 0°. */
const ui_setlist_custom_t wireless_pane_imu_zero_row = {
    .label = TX_IMU_ROW_ZERO,
    .help = TX_IMU_ROW_ZERO_HELP,
    .tap = zero_tap,
    .locked = zero_locked,
};

/** @brief CLEAR ZERO takes no tap while no zero is set.
 *  @return Why, NULL while it takes one. */
static const char *unzero_locked(void)
{
    return imu_view_get()->r.zeroed ? NULL : TR(IMU_LOCK_NO_ZERO);
}

/** @brief CLEAR ZERO tapped: the zero forgotten. */
static void unzero_tap(void)
{
    (void)imu_view_unzero();
}

/** @brief CLEAR ZERO: a tap forgets the zero. */
const ui_setlist_custom_t wireless_pane_imu_unzero_row = {
    .label = TX_IMU_ROW_UNZERO,
    .help = TX_IMU_ROW_UNZERO_HELP,
    .tap = unzero_tap,
    .locked = unzero_locked,
};

/** @brief CALIBRATE takes no hold while the calibration runs, with the
 *         seconds left to keep the sensor still, or while the sensor
 *         sends no frames.
 *  @return Why, NULL while it takes one. */
static const char *cal_locked(void)
{
    const imu_view_t *v = imu_view_get();
    if (v->cal == IMU_CAL_RUNNING) {
        static char buf[96];
        snprintf(buf, sizeof(buf), TR(IMU_CAL_STILL),
                 (unsigned)((v->cal_left_ms + 999u) / 1000u));
        return buf;
    }
    return imu_view_cal_free() ? NULL : TR(IMU_LOCK_NO_FRAMES);
}

/** @brief CALIBRATE's button label. @param out The text. @param n Its
 *         size. */
static void cal_value(char *out, size_t n)
{
    snprintf(out, n, "%s", TR(IMU_CAL_HOLD));
}

/** @brief CALIBRATE held 2s: the accelerometer's calibration. */
static void cal_held(void)
{
    (void)imu_view_calibrate();
}

/** @brief CALIBRATE: a 2s hold starts the accelerometer's calibration. */
const ui_setlist_custom_t wireless_pane_imu_cal_row = {
    .label = TX_IMU_ROW_CAL,
    .help = TX_IMU_ROW_CAL_HELP,
    .value_text = cal_value,
    .locked = cal_locked,
    .held = cal_held,
    .hold_hint = TX_HOLD_HINT_IMU_CAL,
};

/* ------------------------------------------------------------ the repaint */

void wireless_pane_imu_tick(float dt_s, bool shown)
{
    imu_view_tick(dt_s);
    s_live_s += dt_s;
    if (!shown || wireless_pane_selected() != WL_ENTRY_IMU
        || !settings_get_bool(SET_IMU_EN)) {
        s_drawn_rev = imu_view_rev();
        s_drawn_read = imu_view_read_rev();
        return;
    }
    /* A change of the link, the scan, the bind or the calibration
     * repaints the list; a new reading the live values that read
     * differently, and the age on FRAMES 10 times a second at most. */
    if (imu_view_rev() != s_drawn_rev) {
        s_drawn_rev = imu_view_rev();
        s_drawn_read = imu_view_read_rev();
        s_live_s = 0.0f;
        settings_screen_invalidate();
    } else if (imu_view_read_rev() != s_drawn_read
               || (imu_view_get()->r.valid && s_live_s >= LIVE_PERIOD_S)) {
        s_drawn_read = imu_view_read_rev();
        s_live_s = 0.0f;
        settings_screen_live();
    }
}
