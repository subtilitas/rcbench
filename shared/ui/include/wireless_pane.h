/**
 * @file wireless_pane.h
 * @brief SETUP > WIRELESS' split pane: the panel's Bluetooth LE devices,
 *        IMU, DIAL and POWER NODE, in a left list of the kind SENSORS has
 *        (sensors_pane.h), the rows each one shows, its state word and its
 *        reset.
 *
 * IMU: IMU and ANGLE SOURCE while it is off; on, SCAN FOR SENSOR and
 * BOUND SENSOR, which open SEARCH as the Dial's rows do (a scan with the
 * IMU's refusals: IMU_SCAN_ARMED, NODE_SCAN_RUNNING, IMU_SCAN_OFF; a bind
 * with IMU_BIND_SCANNING, IMU_BIND_GONE, IMU_BIND_BUSY), LAST ERROR while
 * the last scan, connect or write failed, the live ANGLE X, ANGLE Y and
 * ANGLE Z (the axis IMU AXIS names marked as the runs'), FRAMES, SENSOR
 * BATTERY and C6 VERSION, ZERO and CLEAR ZERO (taps), CALIBRATE (a
 * 2s hold, ui_setlist.h), then AXIS, SIGN, RATE, ALGORITHM, FILTER,
 * SETTLING TIME and PRECISION (wireless_imu.c).  Its state is the panel's
 * (sensors_rows_set_imu()), its view and commands imu_view.h's.  DIAL: Dial alone while it
 * is off; on, SCAN FOR DIAL, BOUND DIAL and the two steps.  A tap on SCAN
 * FOR DIAL opens SEARCH (wl_search.h) and asks for a scan of 10s, refused
 * while ARMED with the band's DIAL_SCAN_ARMED and while one runs with
 * NODE_SCAN_RUNNING; a tap on BOUND DIAL opens SEARCH on the last scan's
 * list.  SEARCH binds a found Dial; a bind the controller refused says why
 * on the band (DIAL_BIND_SCANNING, DIAL_BIND_GONE).  The Dial's state is
 * the panel's (wireless_pane_set_dial()).  POWER NODE: Power node and
 * INA228 SOURCE while it is off, so the source can go back to WIRED
 * without switching the node on; on, SCAN FOR NODE (refused while ARMED
 * with the band's PNODE_SCAN_ARMED), BOUND NODE and NODE BATTERY under
 * them; SCAN FOR NODE and BOUND NODE open SEARCH as the Dial's rows do,
 * with NODE_BIND_SCANNING and NODE_BIND_GONE; its state is the panel's
 * (wireless_pane_set_pnode(), wireless_pnode.c).  BALANCE NODE and
 * PRINTER are wireless_bal.h's and wireless_printer.h's.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "dial_ctl.h"
#include "gfx.h"
#include "pnode_view.h"
#include "settings.h"
#include "ui_setlist.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The left list's entries, top to bottom. */
enum {
    WL_ENTRY_IMU = 0, /**< enable, angle source, scan, bind, last error,
                           angles X, Y, Z, frames, battery, C6 version, zero,
                           clear zero, calibrate, axis, sign, rate,
                           algorithm, filter, settling time, precision */
    WL_ENTRY_DIAL,    /**< enable, scan, bind, two steps */
    WL_ENTRY_BAL,     /**< enable, scan, bind, name, battery, trial mass
                           (wireless_bal.h) */
    WL_ENTRY_PRINTER, /**< scan, bind, address, state, roll, labels
                           left, read, test label (wireless_printer.h) */
    WL_ENTRY_PNODE,   /**< enable, INA228 source, scan, bind, name,
                           battery, INA228, address, shunt: last,
                           where its guide badge finds room under the long
                           German name */
    WL_ENTRY_COUNT
};

/** @brief The most rows an entry has: the IMU's 21. */
#define WL_ENTRY_ROWS_MAX 21

/** @brief One row of an entry: a setting, or a custom row (custom set). */
typedef struct {
    setting_id_t               id;     /**< the setting, without custom */
    const ui_setlist_custom_t *custom; /**< a row that is no setting */
} wl_row_t;

/**
 * @brief The entry the pane shows.
 * @return The entry, 0 to WL_ENTRY_COUNT - 1.
 */
int wireless_pane_selected(void);

/**
 * @brief Selects the entry the pane shows; one out of range changes nothing.
 *        PRINTER selected from another entry asks a QUERY
 *        (wireless_printer_opened()).
 * @param entry The entry.
 */
void wireless_pane_select(int entry);

/**
 * @brief The entry that holds a setting.
 * @param id The setting.
 * @return Its entry; -1 for a setting no entry holds.
 */
int wireless_pane_entry_of(setting_id_t id);

/**
 * @brief The rows entry @p entry shows now, in order.
 * @param entry The entry.
 * @param out   Takes the rows; NULL only counts.
 * @param max   The room at @p out.
 * @return The rows shown, 0 for an entry out of range.
 */
int wireless_pane_rows(int entry, wl_row_t *out, int max);

/**
 * @brief An entry's name.
 * @param entry The entry.
 * @param buf   Takes the name.
 * @param n     The size of @p buf, bytes.
 * @return @p buf; "" for an entry out of range or no buffer.
 */
const char *wireless_pane_name(int entry, char *buf, size_t n);

/**
 * @brief What an entry's device is doing, the line under its name: OFF,
 *        else the IMU's link state (sensors_rows_imu_state()) or the Dial's
 *        (wireless_pane_dial_word()).
 * @param entry The entry.
 * @param buf   Takes the words.
 * @param n     The size of @p buf, bytes.
 * @param ink   Takes their colour.
 * @return @p buf; "" for an entry out of range or no buffer.
 */
const char *wireless_pane_state(int entry, char *buf, size_t n,
                                gfx_color_t *ink);

/**
 * @brief Every setting row of @p entry back to its default, the hidden ones
 *        included; the bind stays.
 * @param entry The entry.
 */
void wireless_pane_reset(int entry);

/**
 * @brief The Dial as the panel last read it.  A change repaints SETUP.
 * @param v The view; NULL is a Dial OFF.
 */
void wireless_pane_set_dial(const dial_view_t *v);

/**
 * @brief A Dial state in words, at most 25 cells.
 * @param v   The view.
 * @param buf Takes the words.
 * @param n   The size of @p buf, bytes.
 * @param ink Takes their colour; may be NULL.
 * @return @p buf; "" for no buffer.
 */
const char *wireless_pane_dial_word(const dial_view_t *v, char *buf,
                                    size_t n, gfx_color_t *ink);

/**
 * @brief The next command of the DIAL entry's rows, for the panel.
 * @param out Receives it.
 * @return false when none waits.
 */
bool wireless_pane_take_dial(dial_cmd_t *out);

/**
 * @brief The power node as the panel last read it.  A change repaints
 *        SETUP; a refusal the controller counted since the last view says
 *        why on the band: a bind (NODE_BIND_SCANNING, NODE_BIND_GONE), a
 *        scan while an arm waits (PNODE_SCAN_ARMED) or while one runs or
 *        Bluetooth is not ready (NODE_SCAN_RUNNING, PNODE_SCAN_OFF).
 * @param v The view; NULL is a node OFF.
 */
void wireless_pane_set_pnode(const pnode_view_t *v);

/**
 * @brief The power node's view as last set.
 * @return The view; never NULL.
 */
const pnode_view_t *wireless_pane_pnode(void);

/**
 * @brief A power node state in words, at most 25 cells.
 * @param v   The view.
 * @param buf Takes the words.
 * @param n   The size of @p buf, bytes.
 * @param ink Takes their colour; may be NULL.
 * @return @p buf; "" for no buffer.
 */
const char *wireless_pane_pnode_word(const pnode_view_t *v, char *buf,
                                     size_t n, gfx_color_t *ink);

/**
 * @brief The next command of the POWER NODE entry's rows, for the panel.
 * @param out Receives it.
 * @return false when none waits.
 */
bool wireless_pane_take_pnode(pnode_cmd_t *out);

/** @brief SCAN FOR NODE, the POWER NODE entry's third row; public for the
 *         tests. */
extern const ui_setlist_custom_t wireless_pane_pnode_scan_row;
/** @brief BOUND NODE, its fourth row; public for the tests. */
extern const ui_setlist_custom_t wireless_pane_pnode_bind_row;
/** @brief NODE BATTERY, its fifth row; public for the tests. */
extern const ui_setlist_custom_t wireless_pane_pnode_bat_row;
/** @brief NODE INA228: the address the node found its INA228 at and the
 *         shunt it holds, read only. */
extern const ui_setlist_custom_t wireless_pane_pnode_i228_row;
/** @brief NODE NAME: the node's own name (node_cfg_link.h); a tap opens
 *         the name keyboard while SAFE with the node's settings known. */
extern const ui_setlist_custom_t wireless_pane_pnode_name_row;
/** @brief INA228 ADDRESS: AUTO or 0x40 to 0x4F on the node; each step
 *         writes the next while SAFE. */
extern const ui_setlist_custom_t wireless_pane_pnode_addr_row;

/**
 * @brief The address setting a step of INA228 ADDRESS goes to: AUTO, then
 *        0x40 to 0x4F, then AUTO again; back the other way.
 * @param from The setting now; one out of range counts as AUTO.
 * @param dir  -1 or +1.
 * @return The next setting.
 */
uint8_t wireless_pane_pnode_addr_next(uint8_t from, int dir);

/** @brief SCAN FOR SENSOR, the IMU entry's third row while it is on;
 *         public for the tests. */
extern const ui_setlist_custom_t wireless_pane_imu_scan_row;
/** @brief BOUND SENSOR, its fourth row. */
extern const ui_setlist_custom_t wireless_pane_imu_bind_row;
/** @brief LAST ERROR, read only, while wireless_pane_imu_err(). */
extern const ui_setlist_custom_t wireless_pane_imu_err_row;
/** @brief ANGLE X: the angle about X from the zero, read only; named
 *         for the runs while IMU AXIS is X.  ANGLE Y and ANGLE Z alike. */
extern const ui_setlist_custom_t wireless_pane_imu_x_row;
/** @brief ANGLE Y. */
extern const ui_setlist_custom_t wireless_pane_imu_y_row;
/** @brief ANGLE Z. */
extern const ui_setlist_custom_t wireless_pane_imu_z_row;
/** @brief FRAMES: the frame rate and the newest angle's age, read only. */
extern const ui_setlist_custom_t wireless_pane_imu_frames_row;
/** @brief SENSOR BATTERY: the sensor's cell, read only. */
extern const ui_setlist_custom_t wireless_pane_imu_bat_row;
/** @brief C6 VERSION: the ESP32-C6's firmware, read only. */
extern const ui_setlist_custom_t wireless_pane_imu_c6_row;
/** @brief ZERO: a tap takes the angles now as 0°; locked without a
 *         reading (IMU_LOCK_NO_ANGLE). */
extern const ui_setlist_custom_t wireless_pane_imu_zero_row;
/** @brief CLEAR ZERO: a tap forgets the zero; locked while none is set
 *         (IMU_LOCK_NO_ZERO). */
extern const ui_setlist_custom_t wireless_pane_imu_unzero_row;
/** @brief CALIBRATE: a 2s hold starts the accelerometer's calibration of
 *         3s; locked while it runs (IMU_CAL_STILL with the seconds left)
 *         and while the sensor sends no frames (IMU_LOCK_NO_FRAMES). */
extern const ui_setlist_custom_t wireless_pane_imu_cal_row;

/**
 * @brief Whether the IMU's last scan, connect or write failed: LAST ERROR
 *        shows.
 * @return Whether it did.
 */
bool wireless_pane_imu_err(void);

/**
 * @brief A frame of SETUP: the reading's age moves on (imu_view_tick()),
 *        and while the IMU entry shows with the IMU on, a change of the
 *        view repaints the list, and a new reading the live values that
 *        read differently (settings_screen_live()); the age on FRAMES
 *        repaints 10 times a second at most between readings.
 * @param dt_s  The frame's time, s.
 * @param shown SETUP shows WIRELESS' list.
 */
void wireless_pane_imu_tick(float dt_s, bool shown);

/** @brief SCAN FOR DIAL, the DIAL entry's second row; public for the
 *         tests. */
extern const ui_setlist_custom_t wireless_pane_scan_row;
/** @brief BOUND DIAL, the DIAL entry's third row; public for the tests. */
extern const ui_setlist_custom_t wireless_pane_bind_row;
/** @brief DIAL MODEL: the board the linked Dial's INFO names, read only;
 *         public for the tests. */
extern const ui_setlist_custom_t wireless_pane_dial_model_row;
/** @brief DIAL BATTERY: the cell from the Dial's last STATUS, read only;
 *         public for the tests. */
extern const ui_setlist_custom_t wireless_pane_dial_batt_row;

#ifdef __cplusplus
}
#endif
