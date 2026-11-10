/**
 * @file
 * @brief Where a finger lands on the MOTOR & ESC workbench, in the screen's
 *        own coordinates (the body under the band), mirrored from
 *        motor_screen_int.h, motor_live_int.h and ui_footer.h.
 *
 * If the layout moves these move with it.  Taken by motor_rig.h, the
 * screen's suites and render_screen.c.
 *
 *   - the strip: LIVE, PROGRAM from x 9, 150px each, 4px apart, y 8 to
 *     48; RENAME LOG x 449 to 629 in the same row;
 *   - LIVE's telemetry card from y 56: PLOT | TABLE from x 18, 200px for
 *     the two, y 82 to 122;
 *   - the THROTTLE card from y 294: the step keys 72 x 48px at x 18 and
 *     528, y 348 to 396, the track x 98 to 520, y 348 to 396, the hint's
 *     line y 404 to 424;
 *   - the rail's CONTROL card from y 280: the TARGET row x 648 to 1004,
 *     y 308 to 356; RESET PEAKS y 366 to 414, as wide as the TARGET row;
 *   - the footer at y 444 to 492: MOTOR & ESC x 10 to 338, ARM x 348 to
 *     676, SETTINGS x 686 to 1014;
 *   - PROGRAM's list in the page area from y 56, its first row 48px tall
 *     from y 66.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui_layout.h"

#define MO_TAB_Y          28            /**< The strip's tabs. */
/** @brief The strip's tab @p i: 0 LIVE, 1 PROGRAM. */
#define MO_TAB_X(i)       (9 + (i) * 154 + 75)

/** @brief RENAME LOG, in the strip row at x 449 to 629. */
#define MO_LOGBTN_X       539
#define MO_LOGBTN_L       449           /**< Its left edge. */
#define MO_LOGBTN_R       628           /**< Its right column. */

#define MO_PANE_Y         102           /**< PLOT | TABLE. */
#define MO_PLOT_TAB_X     68            /**< PLOT. */
#define MO_TABLE_TAB_X    168           /**< TABLE. */

#define MO_TRACK_X        98            /**< The track's left end. */
#define MO_TRACK_W        422           /**< Its length, px. */
#define MO_TRACK_Y        372           /**< Its middle row. */
#define MO_DOWN_X         54            /**< The -1 key. */
#define MO_UP_X           564           /**< The +1 key. */
#define MO_UP_R           599           /**< Its right column. */
#define MO_HINT_Y         414           /**< The hint's line. */

#define MO_RC_X           648           /**< The CONTROL card's column. */
#define MO_RC_W           356           /**< Its width, px. */
#define MO_TARGET_X       870           /**< The TARGET row's value box. */
#define MO_TARGET_T       308           /**< Its top. */
#define MO_TARGET_B       355           /**< Its bottom row. */
#define MO_TARGET_Y       332           /**< Its middle row. */
#define MO_RESET_X        826           /**< RESET PEAKS. */
#define MO_RESET_Y        390           /**< Its middle row. */
#define MO_ARM_X          512           /**< ARM and DISARM. */
#define MO_ARM_T          444           /**< Its top. */
#define MO_ARM_H          48            /**< Its height, px. */
#define MO_ARM_Y          468           /**< Its middle row. */

#define MO_PROG_ROW_X     279           /**< PROGRAM's rows. */
#define MO_PROG_ROW_Y     90            /**< Its first row. */

#define MO_SETTINGS_X     850           /**< The footer's SETTINGS. */
#define MO_SETTINGS_Y     468           /**< Its middle row. */
