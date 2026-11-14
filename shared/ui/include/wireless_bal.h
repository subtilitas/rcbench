/**
 * @file
 * @brief SETUP > WIRELESS' BALANCE NODE entry: ON/OFF, SCAN FOR NODE, the
 *        bound node, its battery and the trial mass, and the node's state
 *        word on the entry's line.
 *
 * The panel hands the controller's view in each frame
 * (wireless_pane_set_bal()) and takes the rows' commands
 * (wireless_pane_take_bal()).  A scan tapped while ARMED is refused on the
 * band and queued not at all.  wireless_pane.c calls the wireless_bal_
 * functions for its WL_ENTRY_BAL.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "bal_ctl.h"
#include "gfx.h"
#include "wireless_pane.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The entry's rows: ON/OFF, and while ON the scan, the bound node,
 *        the battery and the trial mass.
 * @param out The rows, NULL to count.  @param max Their room.
 * @return The count.
 */
int wireless_bal_rows(wl_row_t *out, int max);

/**
 * @brief Whether a setting is one of the entry's.
 * @param id The setting.
 * @return true for SET_BNODE_EN and SET_BAL_TRIAL.
 */
bool wireless_bal_owns(setting_id_t id);

/**
 * @brief The entry's state on the left list: OFF, or the node's word.
 * @param buf The text.  @param n Its size.  @param ink Receives its colour.
 * @return buf.
 */
const char *wireless_bal_state(char *buf, size_t n, gfx_color_t *ink);

/** @brief The entry's settings back to their defaults. */
void wireless_bal_reset(void);

/**
 * @brief The node's word for a view: OFF, STARTING, ... REPORTING.
 * @param v The view, NULL reads as OFF.
 * @param buf The text.  @param n Its size.  @param ink Receives its
 *        colour, may be NULL.
 * @return buf.
 */
const char *wireless_pane_bal_word(const bal_view_t *v, char *buf, size_t n,
                                   gfx_color_t *ink);

/**
 * @brief The controller's view, each frame; a change repaints SETUP.
 * @param v The view, NULL clears it.
 */
void wireless_pane_set_bal(const bal_view_t *v);

/**
 * @brief The oldest command the rows queued.
 * @param out Receives it.
 * @return false when none waits.
 */
bool wireless_pane_take_bal(bal_cmd_t *out);

/** @brief SCAN FOR NODE; public for the tests. */
extern const ui_setlist_custom_t wireless_bal_scan_row;
/** @brief BOUND NODE; public for the tests. */
extern const ui_setlist_custom_t wireless_bal_bind_row;
/** @brief NODE BATTERY, a row that only shows; public for the tests. */
extern const ui_setlist_custom_t wireless_bal_bat_row;

#ifdef __cplusplus
}
#endif
