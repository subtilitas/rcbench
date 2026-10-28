/**
 * @file files_spots.h
 * @brief Where a finger lands on FILES, in the screen's own coordinates
 *        (the body under the band), mirrored from files_int.h.
 *
 * If the layout moves these move with it.  Taken by files_rig.h, the
 * screen's suites and render_screen.c.
 *
 *   - two panes 500px wide at x 8 and x 516;
 *   - a pane's header row y 4 to 52: UP 312px from the pane's left, PREV
 *     and NEXT 88 x 48px at its right end;
 *   - a pane's list from y 60: 6 rows under a 30px heading at a pitch of
 *     48px, 420px wide from 8px in; the scroll bar 56px wide, 8px
 *     in from the pane's right, from y 90, 284px tall;
 *   - the message line y 388 to 436;
 *   - the footer y 440 to 492: RESCAN 140px at x 8, then COPY, MOVE,
 *     RENAME, DELETE, NEW, INFO and OPEN 118px wide from x 154 at a pitch
 *     of 124px, OPEN at x 898;
 *   - the DELETE question's CANCEL x 186 and DELETE x 538, the progress
 *     box's CANCEL x 362, 300 x 56px from y 304; INFO's CLOSE x 362,
 *     300 x 56px from y 364;
 *   - NEW's choice from x 468, y 312: FOLDER and TEXT FILE, 56px rows;
 *   - the text viewer: its rows from x 32, y 66, 20px apart (16px with
 *     SMALL); its scroll bar 56px wide at x 956, from y 64, 364px tall;
 *     SMALL 140 x 52px at x 8, PAGE UP at x 730 and PAGE DOWN at x 876,
 *     from y 440.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui_layout.h"

/** @brief Pane @p i's left edge. */
#define FS_SPOT_PANE_X(i)  (8 + (i) * 508)
/** @brief The middle of row @p r on screen of pane @p i, x and y. */
#define FS_SPOT_ROW_CX(i)  (FS_SPOT_PANE_X(i) + 218)
#define FS_SPOT_ROW_CY(r)  (60 + 30 + (r) * 48 + 22)
#define FS_SPOT_ROW_TOP(r) (60 + 30 + (r) * 48)    /**< A row's top. */
#define FS_SPOT_ROWS       6                       /**< Rows on screen. */
#define FS_SPOT_ROW_X0(i)  (FS_SPOT_PANE_X(i) + 8) /**< A row's left. */
#define FS_SPOT_ROW_X1(i)  (FS_SPOT_PANE_X(i) + 427) /**< Its last column. */
/** @brief The middle of pane @p i's UP, PREV and NEXT. */
#define FS_SPOT_UP_CX(i)   (FS_SPOT_PANE_X(i) + 156)
#define FS_SPOT_PREV_CX(i) (FS_SPOT_PANE_X(i) + 364)
#define FS_SPOT_NEXT_CX(i) (FS_SPOT_PANE_X(i) + 456)
#define FS_SPOT_HEAD_CY    28                      /**< The header row. */
/** @brief Pane @p i's scroll bar: its middle column, its track's top and
 *         height. */
#define FS_SPOT_BAR_CX(i)  (FS_SPOT_PANE_X(i) + 464)
#define FS_SPOT_BAR_TOP    90
#define FS_SPOT_BAR_H      284
/** @brief The middle of footer button @p b: 0 RESCAN to 7 OPEN. */
#define FS_SPOT_FOOT_CX(b) (((b) == 0) ? 78 : 154 + ((b) - 1) * 124 + 59)
#define FS_SPOT_FOOT_CY    466
#define FS_SPOT_FOOT_Y     440                     /**< The footer's top. */
#define FS_SPOT_FOOT_BOT   492                     /**< Its foot. */
/** @brief The boxes' buttons. */
#define FS_SPOT_BOX_CY     332
#define FS_SPOT_Q_CANCEL_CX 336
#define FS_SPOT_Q_DELETE_CX 688
#define FS_SPOT_JOB_CANCEL_CX 512
#define FS_SPOT_CLOSE_CX   512
#define FS_SPOT_CLOSE_CY   392
/** @brief NEW's rows: FOLDER, TEXT FILE. */
#define FS_SPOT_NEW_CX     618
#define FS_SPOT_NEW_CY(i)  (312 + 4 + (i) * 56 + 28)
/** @brief The message line's top. */
#define FS_SPOT_MSG_Y      388

/** @brief The footer's buttons, as fs_foot_t numbers them. */
enum {
    FS_SPOT_RESCAN = 0, FS_SPOT_COPY, FS_SPOT_MOVE, FS_SPOT_RENAME,
    FS_SPOT_DELETE, FS_SPOT_NEW, FS_SPOT_INFO, FS_SPOT_OPEN
};

/** @brief The text viewer: its first row's top and left, the row pitches
 *         of its two faces, and its cells' widths. */
#define FS_SPOT_TV_TEXT_X  32
#define FS_SPOT_TV_TEXT_Y  66
#define FS_SPOT_TV_PITCH   20
#define FS_SPOT_TV_PITCH_SMALL 16
/** @brief Its scroll bar: the middle column, the track's top and height. */
#define FS_SPOT_TV_BAR_CX  984
#define FS_SPOT_TV_BAR_TOP 64
#define FS_SPOT_TV_BAR_H   364
/** @brief The middles of SMALL, PAGE UP and PAGE DOWN, and the footer's. */
#define FS_SPOT_TV_SMALL_CX 78
#define FS_SPOT_TV_PGUP_CX  800
#define FS_SPOT_TV_PGDN_CX  946
#define FS_SPOT_TV_FOOT_CY  466
/** @brief The middles of EDIT in the viewer, and of SAVE, REVERT and
 *         KEYBOARD in the editor. */
#define FS_SPOT_TV_EDIT_CX   224
#define FS_SPOT_TV_SAVE_CX   78
#define FS_SPOT_TV_REVERT_CX 224
#define FS_SPOT_TV_KEYS_CX   370
/** @brief The editor's keyboard: its area under a panel of 8 rows. */
#define FS_SPOT_TV_KB_X 8
#define FS_SPOT_TV_KB_Y 236
#define FS_SPOT_TV_KB_W 1008
#define FS_SPOT_TV_KB_H 260
/** @brief The question's STAY and DISCARD, their middles. */
#define FS_SPOT_TV_Q_CY        332
#define FS_SPOT_TV_STAY_CX     336
#define FS_SPOT_TV_DISCARD_CX  688
