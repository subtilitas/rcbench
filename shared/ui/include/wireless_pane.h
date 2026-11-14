/**
 * @file wireless_pane.h
 * @brief SETUP > WIRELESS' split pane: the panel's Bluetooth LE devices,
 *        IMU, DIAL and POWER NODE, in a left list of the kind SENSORS has
 *        (sensors_pane.h), the rows each one shows, its state word and its
 *        reset.
 *
 * IMU: the rows SENSORS held before, IMU and ANGLE SOURCE while it is off,
 * AXIS, SIGN, RATE, ALGORITHM and FILTER under them while it is on; its
 * state is the panel's (sensors_rows_set_imu()).  DIAL: Dial alone while it
 * is off; on, SCAN FOR DIAL (a tap scans 10s, refused while ARMED with the
 * band's DIAL_SCAN_ARMED), BOUND DIAL (a tap lists the Dials the last scan
 * found, a tap on one binds it) and the two steps; its state is the
 * panel's (wireless_pane_set_dial()).  POWER NODE: Power node and INA228
 * SOURCE while it is off, so the source can go back to WIRED without
 * switching the node on; on, SCAN FOR NODE (refused while ARMED with the
 * band's PNODE_SCAN_ARMED), BOUND NODE and NODE BATTERY under them; its
 * state is the panel's (wireless_pane_set_pnode(), wireless_pnode.c).
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
    WL_ENTRY_IMU = 0, /**< enable, angle source, axis, sign, rate,
                           algorithm, filter */
    WL_ENTRY_DIAL,    /**< enable, scan, bind, two steps */
    WL_ENTRY_PNODE,   /**< enable, INA228 source, scan, bind, battery */
    WL_ENTRY_COUNT
};

/** @brief The most rows an entry has: the IMU's 7. */
#define WL_ENTRY_ROWS_MAX 7

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
 *        SETUP.
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

/** @brief SCAN FOR DIAL, the DIAL entry's second row; public for the
 *         tests. */
extern const ui_setlist_custom_t wireless_pane_scan_row;
/** @brief BOUND DIAL, the DIAL entry's third row; public for the tests. */
extern const ui_setlist_custom_t wireless_pane_bind_row;

#ifdef __cplusplus
}
#endif
