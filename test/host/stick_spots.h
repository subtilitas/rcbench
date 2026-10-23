/**
 * @file
 * @brief Where a finger lands on ESC STICK, in the session's body
 *        coordinates (under the band), mirrored from esc_stick_int.h,
 *        esc_stick_panel_int.h, esc_stick_pages.c and esc_stick_panel.c.
 *
 * If the layout moves these move with it.  Taken by stick_rig.h,
 * esc_stick_panel_rig.h, test_esc_stick_layout.c and render_screen.c.
 *
 *   - the crumb row y 30 to 78: BACK x 20 to 140; on the list the search
 *     field x 616 to 876 with X x 828 to 876, "^" x 884 to 940 and "v"
 *     x 948 to 1004; on a profile's page TIMING x 836 to 1004;
 *   - the list's rows x 20 to 1004, 48 px tall at a 56 px pitch from y 84,
 *     7 to a page;
 *   - a profile's page and TIMING: the parameter rows 48 px tall at a
 *     56 px pitch from y 92, 6 to a page, the name x 16 to 290, the value
 *     x 298 to 548, "-" x 556 to 604 and "+" x 612 to 660; the side
 *     column's "^" x 900 to 948 and "v" x 956 to 1004 at y 92 to 140, and
 *     MANUAL INTERVENTION REQUIRED x 688 to 998 at y 148 to 196;
 *   - the foot row y 436 to 492: DEFAULTS x 660 to 828, RUN / ABORT /
 *     OK / CLOSE x 836 to 1004, a result's MANUAL INTERVENTION REQUIRED
 *     x 498 to 828;
 *   - the panels' buttons y 384 to 432: HOLD TO RUN / DONE x 28 to 288,
 *     ALL STEPS x 312 to 578, CANCEL / OK / ABORT x 816 to 996.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui_layout.h"

#define ST_RIGHT        (UI_PANEL_W - UI_MARGIN - 12) /**< 1004 */

#define ST_CRUMB_CY     54             /**< the crumb row's middle  */
#define ST_BACK_X       80             /**< BACK                    */
#define ST_FIND_X       716            /**< the search field        */
#define ST_FIND_CLR_X   852            /**< its X                   */
#define ST_LIST_UP_X    912            /**< the list's "^"          */
#define ST_LIST_DN_X    976            /**< the list's "v"          */
#define ST_TIMING_X     920            /**< TIMING                  */

#define ST_LIST_ROWS    7              /**< the list's rows a page  */
/** @brief The middle of the list's row @p n on the page. */
#define ST_ROW_CY(n)    (84 + (n) * 56 + 24)

#define ST_PARM_ROWS    6              /**< the parameter rows a page */
#define ST_PROW_X       150            /**< a parameter row's name  */
#define ST_VALUE_X      423            /**< its value (TIMING's keypad) */
#define ST_STEP_DN_X    580            /**< its "-"                 */
#define ST_STEP_UP_X    636            /**< its "+"                 */
/** @brief The middle of parameter row @p i on the page. */
#define ST_STEP_CY(i)   (92 + (i) * 56 + 24)
#define ST_PAGE_UP_X    924            /**< the side column's "^"   */
#define ST_PAGE_DN_X    980            /**< its "v"                 */
#define ST_PAGE_CY      116            /**< their middle            */
#define ST_HAND_X       843            /**< MANUAL INTERVENTION REQUIRED */
#define ST_HAND_Y       172            /**< its middle              */

#define ST_READ_X       744            /**< DEFAULTS                */
#define ST_WRITE_X      920            /**< RUN, ABORT, OK, CLOSE   */
#define ST_BTN_CY       464            /**< the foot row's middle   */
#define ST_HAND_RES_X   663            /**< a result's MANUAL ...   */

#define ST_HOLD_X       158            /**< HOLD TO RUN, DONE       */
#define ST_STEPS_X      445            /**< ALL STEPS               */
#define ST_CANCEL_X     906            /**< CANCEL, OK, ABORT       */
#define ST_PBTN_CY      408            /**< the panels' buttons     */

/** @brief The search keyboard's dock: right of x 352 from the list's top
 *         to the body's margin. */
#define ST_DOCK_X       352
#define ST_DOCK_Y       84
#define ST_DOCK_W       (UI_PANEL_W - UI_MARGIN - ST_DOCK_X)
#define ST_DOCK_H       (UI_BODY_H - UI_MARGIN - ST_DOCK_Y)
