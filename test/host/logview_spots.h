/**
 * @file
 * @brief Where a finger lands on LOGS, in the screen's own coordinates (the
 *        body under the band), mirrored from log_viewer_int.h.
 *
 * If the layout moves these move with it.  Taken by logview_rig.h, the
 * screen's suites and render_screen.c.
 *
 *   - the header row y 4 to 52: ROOT, USER, BENCH 110 x 40 px from x 11,
 *     4 px apart; PREV x 728 and NEXT x 876, 140 x 48 px;
 *   - the file list from y 60, 1008 px wide: its rows under a 30 px heading
 *     at a pitch of 48 px, drawn 44 px tall from x 16; the scroll bar
 *     x 952 to 1008, from y 90, 332 px tall;
 *   - the footer y 440 to 492: RESCAN x 8, DELETE x 728, OPEN x 876,
 *     140 px each, WI-FI x 156 and FILES x 264, 100 px each; the import's four buttons 246 px from x 8, 8 px
 *     apart; the plot's BACK x 8 (140 px), < x 156 and > x 260 (96 px);
 *   - the import's column list from x 416, y 44: its rows at a pitch of
 *     48 px under a 30 px heading;
 *   - the plot x 16 to 1008, y 88 to 404;
 *   - the DELETE question's CANCEL x 186 and DELETE x 538, 300 x 56 px
 *     from y 304;
 *   - the TEXT view's rows in the list's panel from x 24, y 90: 15 rows of
 *     92 cells of 10 x 20 at a pitch of 22 px; BACK x 8, 140 x 52 px.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui_layout.h"

#define LV_HEAD_CY    28            /**< The header row's middle. */
/** @brief The middle of the header row's tab at position @p i: 0 ROOT,
 *  1 USER, 2 BENCH. */
#define LV_TAB_CX(i)  (11 + (i) * 114 + 55)
#define LV_TAB_X0     11            /**< The first tab's left edge. */
#define LV_TAB_PITCH  114           /**< One tab to the next. */
#define LV_PREV_CX    798           /**< PREV. */
#define LV_NEXT_CX    946           /**< NEXT. */
#define LV_HEAD_TOP   4             /**< The header row's top. */
#define LV_HEAD_BOT   52            /**< Its bottom. */
#define LV_HEAD_TEXT_X 368          /**< The folder's left edge. */
#define LV_HEAD_END   720           /**< The card's name ends here with
                                         PREV and NEXT showing, */
#define LV_HEAD_FAR   1016          /**< and here without them. */

#define LV_LIST_X     8             /**< The list's left edge. */
#define LV_LIST_Y     60            /**< Its top, where its tag starts. */
#define LV_ROW_PITCH  48            /**< One row to the next. */
#define LV_ROWS       7             /**< The rows it shows at once. */
/** @brief The middle of the list's row @p i on screen. */
#define LV_ROW_CY(i)  (LV_LIST_Y + 30 + (i) * LV_ROW_PITCH + 22)
#define LV_ROW_CX     400           /**< A point inside every row. */

#define LV_BAR_CX     980           /**< The scroll bar's middle. */
#define LV_BAR_TOP    (LV_LIST_Y + 30)          /**< Its track's top. */
#define LV_BAR_H      (LV_ROWS * LV_ROW_PITCH - 4)  /**< Its height. */

/** @brief The footer's top: 52 px ending 4 px above the alert band. */
#define LV_FOOT_Y     (UI_BODY_H - UI_ALERT_H - 4 - 52)
#define LV_FOOT_CY    (LV_FOOT_Y + 26)          /**< Its middle row. */
#define LV_RESCAN_CX  78            /**< RESCAN. */
#define LV_WIFI_CX    206           /**< WI-FI. */
#define LV_FILES_CX   314           /**< FILES. */
#define LV_DELETE_CX  798           /**< DELETE. */
#define LV_OPEN_CX    946           /**< OPEN. */

/** @brief The middle of the import's footer button @p i: 0 SEP, 1 NUM,
 *  2 BACK, 3 PLOT. */
#define LV_IM_BTN_CX(i) (8 + (i) * 254 + 123)
/** @brief The middle of the import's column row @p i on screen. */
#define LV_IM_ROW_CY(i) (44 + 30 + (i) * 48 + 22)
#define LV_IM_ROW_CX  700           /**< A point inside every column row. */
/** @brief DETECTED's warning lines: 368 px, 36 cells of 10 x 20. */
#define LV_WARN_W     368
#define LV_WARN_LH    22            /**< A warning line's pitch. */
/** @brief The first warning line's top: under the six key rows and the
 *  evidence line. */
#define LV_WARN_Y     258
#define LV_WARN_X0    16            /**< DETECTED's text, left. */
#define LV_WARN_X1    392           /**< And right. */
/** @brief A file message's line in the footer, FILES to DELETE: from x 380,
 *  336 px, 28 cells of 12 x 24. */
#define LV_MSG_X      380
#define LV_MSG_W      336
#define LV_MSG_LH     24            /**< A message line's pitch. */

#define LV_PV_X0      16            /**< The plot's left column. */
#define LV_PV_COLS    992           /**< Its width in columns. */
#define LV_PV_TOP     88            /**< Its top row. */
#define LV_PV_BOTTOM  403           /**< Its bottom row. */
#define LV_PV_CX      400           /**< A point inside the plot. */
#define LV_PV_CY      200           /**< And its row. */
#define LV_PV_BACK_CX 78            /**< BACK under the plot. */
#define LV_PV_PREV_CX 204           /**< <. */
#define LV_PV_NEXT_CX 308           /**< >. */

/** @brief The empty list's box: the reason's line from LV_EMPTY_WHY_Y, what
 *  the folder holds from LV_EMPTY_HINT_Y, both 24 px tall, x LV_EMPTY_X0
 *  to LV_EMPTY_X1. */
#define LV_EMPTY_WHY_Y  202
#define LV_EMPTY_HINT_Y 242         /**< The second line's top. */
#define LV_EMPTY_X0   128           /**< The lines' left. */
#define LV_EMPTY_X1   896           /**< Their right. */

/** @brief The TEXT view's first row's top, under the list's heading. */
#define LV_TX_TOP     (LV_LIST_Y + 30)
#define LV_TX_ROW_X   24            /**< A text row's left edge. */
#define LV_TX_PITCH   22            /**< One text row to the next. */
#define LV_TX_ROWS    15            /**< The text rows on screen. */
#define LV_TX_COLS    92            /**< Cells of 10 x 20 a text row holds. */
#define LV_TX_BACK_CX 78            /**< BACK under the text. */

#define LV_DQ_CY      332           /**< The DELETE question's buttons. */
#define LV_DQ_CANCEL_CX 336         /**< CANCEL. */
#define LV_DQ_DELETE_CX 688         /**< DELETE. */
