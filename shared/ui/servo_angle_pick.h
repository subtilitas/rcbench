/**
 * @file
 * @brief The ANGLE SOURCE list of the SERVO function screens
 *        (servo_angle_pick.c): the AS5600, the IMUs the last scan found,
 *        SEARCH FOR IMU and NO IMU FOUND on the choice list over the left
 *        column, and what a pick sets.
 *
 * The screen's files call it: the READY rows and the SETTINGS list rows
 * that open it, the ANGLE row TEST's and LIMIT RUN's cards draw with
 * angle_pick_run_row(), the list's press and draw code
 * (servo_settings_panel.c, servo_settings_draw.c) and the frame step that
 * keeps it on the IMU's view.  The tests read its entries through the last three calls.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "servo_ready_int.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The list opened over the left column on a SAFE bench, as the
 *        SETTINGS list opens; refused with the reason as a notice while
 *        the bench is ARMED, asked to arm, sweeping or running, or a
 *        press is held.  With the IMU on, nothing bound, nothing scanned
 *        and no error, it starts a scan at once.
 */
void angle_pick_open(void);
/**
 * @brief Each frame: the entries rebuilt from the IMU's view while no
 *        finger is on the list, and the list closed when the bench leaves
 *        SAFE under it.
 */
void angle_pick_follow(void);
/**
 * @brief A pick of an entry the list does not refuse.
 * @param value The entry's value.
 */
void angle_pick_choose(int value);
/**
 * @brief A tap on a refused entry: `AS5600: OFF IN SETUP` opens SETUP >
 *        SENSORS on the AS5600 entry; any other names its line in the
 *        alert band.
 * @param k The entry.
 */
void angle_pick_refused(int k);
/**
 * @brief Whether entry @p k is the source in force: the AS5600, or with
 *        ANGLE SOURCE IMU the sensor bound.
 * @param k The entry.
 * @return true to draw it in the accent.
 */
bool angle_pick_current(int k);
/**
 * @brief The ANGLE row of TEST's and LIMIT RUN's READY cards: the source
 *        a run reads and whether it reads, never a refusal of the hold.  A
 *        tap opens the list (angle_pick_open()).
 * @param r     The row, cleared by the caller; its label, state, value and
 *              fix are set.
 * @param name  The source's name: "AS5600", "IMU X+".
 * @param reads Whether a run started now, or the run under way, reads
 *              angles from it.
 */
void angle_pick_run_row(rdy_row_t *r, const char *name, bool reads);
/**
 * @brief The list's entries, for the tests.
 * @return Their count while the list shows, -1 while it does not.
 */
int angle_pick_count(void);
/**
 * @brief Entry @p i's label, for the tests.
 * @param i       The entry.
 * @param refused Takes whether it is refused; may be NULL.
 * @return The label; "" past the last.
 */
const char *angle_pick_label(int i, bool *refused);
/**
 * @brief Entry @p i's button in the body, for the tests.
 * @param i The entry.
 * @return Its rectangle.
 */
gfx_rect_t angle_pick_rect(int i);

#ifdef __cplusplus
}
#endif
