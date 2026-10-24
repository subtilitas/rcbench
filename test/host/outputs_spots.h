/**
 * @file
 * @brief Where a finger lands on OUTPUTS, in the screen's own coordinates
 *        (the body under the band), mirrored from outputs_screen_int.h,
 *        outputs_screen.c and outputs_port_entry.c.
 *
 * If the layout moves these move with it.  Taken by outputs_screen_rig.h,
 * test_port_screens.c, test_bind_link.c and the OUTPUTS layout suite.
 *
 *   - the left column x 16 to 316: PROTOCOL from y 16, the protocol
 *     list's face y 44 to 108;
 *   - the open list over it from y 44: the 8 protocols in rows of 48px
 *     from x 20, the PROG PORT entries in a second column of the same rows
 *     from x 320, each row 4px in from its column's edge, the list and
 *     its shadow ending at y 440;
 *   - UNBIND ALL PINS, while the pages describe no binding, y 218 to 274;
 *   - the grid of 4 x 7 cells of 167 x 64px from x 324, y 8: 8px between
 *     columns, 6px between rows, the last row ending at y 492;
 *   - a PROG PORT entry's column: PIN y 112 to 160; FUNCTION's face
 *     x 19 to 313, y 188 to 236, its list's rows of 48px from y 244;
 *     SERVO TESTER y 292 to 340 and PORT PWM y 370 to 418, two segments
 *     of 145px 4px apart from x 19.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui_layout.h"

#define OU_COL_X       16            /**< The left column. */
#define OU_COL_W       300           /**< Its width, px. */
#define OU_DD_Y        44            /**< The protocol list's face. */
#define OU_DD_H        64            /**< Its height, px. */

/** @brief The lowest row the screen draws on: 4px above the alert band. */
#define OU_FOOT        (UI_ALERT_Y - UI_BAND_H - 4)

/** @brief The open list's row height: UI_TAP_MIN. */
#define OU_POP_ROW     48
/** @brief A column of the open list, px. */
#define OU_POP_COL_W   300
/** @brief The middle row of list row @p r of a column. */
#define OU_POP_ROW_Y(r) (OU_DD_Y + 4 + (r) * OU_POP_ROW + OU_POP_ROW / 2)
/** @brief A column a press lands in: 0 the protocols, 1 the PROG PORT
 *         entries; 40px into its row. */
#define OU_POP_X(col)  (OU_COL_X + (col) * OU_POP_COL_W + 40)

#define OU_CLR_Y       218           /**< UNBIND ALL PINS' top. */
#define OU_CLR_H       56            /**< Its height, px. */

#define OU_GRID_X      324           /**< The pin grid. */
#define OU_GRID_Y      8             /**< Its top. */
#define OU_CELL_W      167           /**< A cell, px. */
#define OU_CELL_H      64            /**< Its height, px. */
#define OU_CELL_GAP    8             /**< Between columns, px. */
#define OU_ROW_GAP     6             /**< Between rows, px. */
#define OU_GRID_ROWS   7             /**< Cells down a column. */
#define OU_CELL_PAD    8             /**< The sub-line from the left. */
#define OU_CELL_LABEL_Y 44           /**< The sub-line's top in a cell. */

/** @brief The left edge of the cell of catalogue entry @p i. */
#define OU_CELL_X(i)   (OU_GRID_X + ((i) / OU_GRID_ROWS) * (OU_CELL_W + OU_CELL_GAP))
/** @brief Its top. */
#define OU_CELL_Y(i)   (OU_GRID_Y + ((i) % OU_GRID_ROWS) * (OU_CELL_H + OU_ROW_GAP))

#define OU_PIN_Y       136           /**< A PROG PORT entry's PIN, middle. */
#define OU_FN_FACE_Y   212           /**< FUNCTION's face, middle. */
#define OU_FN_FACE_X   166           /**< Its middle column. */
/** @brief The middle row of FUNCTION list row @p i. */
#define OU_FN_ROW_Y(i) (244 + (i) * 48 + 24)
/** @brief The middle column of segment @p i of SERVO TESTER or PORT PWM. */
#define OU_SEG_X(i)    (19 + 72 + (i) * 149)
#define OU_TESTER_Y    316           /**< SERVO TESTER, middle row. */
#define OU_PWM_Y       394           /**< PORT PWM, middle row. */
