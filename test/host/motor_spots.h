/**
 * @file
 * @brief Where a finger lands on the MOTOR & ESC workbench, in the screen's
 *        own coordinates (the body under the band), mirrored from
 *        motor_screen_int.h, motor_live_int.h and motor_settings_page.c.
 *
 * If the layout moves these move with it.  Taken by motor_rig.h, the
 * screen's suites and render_screen.c.
 *
 *   - the strip: LIVE, PROGRAM, SETTINGS from x 9, 150 px each, 4 px
 *     apart, y 8 to 48;
 *   - LIVE's telemetry card from y 56: PLOT | TABLE from x 18, 200 px for
 *     the two, y 82 to 122;
 *   - the THROTTLE card from y 352: the step keys 72 x 56 px at x 18 and
 *     546, y 406 to 462, the track x 98 to 538, y 410 to 458, the hint's
 *     line y 470 to 490;
 *   - the rail's CONTROL card from y 280: the TARGET row x 648 to 1004,
 *     y 308 to 356; RESET PEAKS y 366 to 414; ARM y 424 to 484, both as
 *     wide as the TARGET row;
 *   - PROGRAM's list in the page area from y 56, its first row 48 px tall
 *     from y 66.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui_layout.h"

#define MO_TAB_Y          28            /**< The strip's tabs. */
/** @brief The strip's tab @p i: 0 LIVE, 1 PROGRAM, 2 SETTINGS. */
#define MO_TAB_X(i)       (9 + (i) * 154 + 75)

#define MO_PANE_Y         102           /**< PLOT | TABLE. */
#define MO_PLOT_TAB_X     68            /**< PLOT. */
#define MO_TABLE_TAB_X    168           /**< TABLE. */

#define MO_TRACK_X        98            /**< The track's left end. */
#define MO_TRACK_W        440           /**< Its length, px. */
#define MO_TRACK_Y        434           /**< Its middle row. */
#define MO_DOWN_X         54            /**< The -1 key. */
#define MO_UP_X           582           /**< The +1 key. */
#define MO_HINT_Y         480           /**< The hint's line. */

#define MO_RC_X           648           /**< The CONTROL card's column. */
#define MO_RC_W           356           /**< Its width, px. */
#define MO_TARGET_X       870           /**< The TARGET row's value box. */
#define MO_TARGET_T       308           /**< Its top. */
#define MO_TARGET_B       355           /**< Its bottom row. */
#define MO_TARGET_Y       332           /**< Its middle row. */
#define MO_RESET_X        826           /**< RESET PEAKS. */
#define MO_RESET_Y        390           /**< Its middle row. */
#define MO_ARM_X          826           /**< ARM and DISARM. */
#define MO_ARM_T          424           /**< Its top. */
#define MO_ARM_H          60            /**< Its height, px. */
#define MO_ARM_Y          454           /**< Its middle row. */

#define MO_PROG_ROW_X     279           /**< PROGRAM's rows. */
#define MO_PROG_ROW_Y     90            /**< Its first row. */

/** @brief SETTINGS' save line, 4 px above the alert band. */
#define MO_SAVE_LINE_Y    (UI_ALERT_Y - UI_BAND_H - 4 - 20)
