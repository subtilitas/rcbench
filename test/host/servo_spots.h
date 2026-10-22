/**
 * @file
 * @brief Where a finger lands on the SERVO workbench, in the screen's own
 *        coordinates (the body under the band), mirrored from
 *        servo_screen_int.h, servo_workbench_int.h and servo_screen.c.
 *
 * If the layout moves these move with it.  Taken by servo_rig.h and
 * render_screen.c.
 *
 *   - the strip: LIVE, TEST, PROGRAM, SETTINGS from x 9, 150 px each, 4 px
 *     apart, y 8 to 48 (SV_STRIP_*);
 *   - PROGRAM's list (prog_registry.h) in the page area from y 56: rows 34
 *     px each and 40 px apart; the first row is the KST and Chaservo
 *     programmer;
 *   - SETTINGS' own page tabs OUTPUT, SWEEP, DUT from x 18, 197 px each,
 *     y 64 to 104, and their rows from y 112, 48 px tall and 56 px apart,
 *     the left column from x 18, the right from x 323;
 *   - TEST's two run cards from y 60 and 284, each with START and SETTINGS
 *     74 px down; a run's settings' rows in three columns of 193 px from x
 *     18, 203 px apart, each its label over its 48 px value, 74 px apart
 *     from y 116; its CLOSE x 478 to 618, y 60 to 108;
 *   - the output card from SV_RCARD_X, its inner column SV_RC_X and
 *     SV_RC_W; CENTRE, SWEEP and RELEASE a third of it each, y 366 to 414;
 *     the SET row's values and OUTPUT ON at y 174 to 222;
 *   - the TARGET row's value box on the output card, from SV_RC_X + 64 to
 *     the column's end, y 308 to 356.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui_layout.h"

/** @brief The output shaft on LIVE's card: SHAFT_X and SHAFT_Y of
 *         servo_screen_core_int.h, (56 + 450) / 2. */
#define SV_SHAFT_X         400
#define SV_SHAFT_Y         253                /**< The shaft's row. */

#define SV_TAB_Y           28                 /**< The strip's tabs. */
#define SV_TAB_LIVE_X      84                 /**< LIVE's tab. */
#define SV_TAB_TEST_X      238                /**< TEST's tab. */
#define SV_TAB_PROGRAM_X   392                /**< PROGRAM's tab. */
#define SV_TAB_SETTINGS_X  546                /**< SETTINGS' tab. */
#define SV_PROG_ROW_X      250                /**< PROGRAM's rows. */
/** @brief PROGRAM's row @p r: 48 px tall at a 56 px pitch from the page
 *         area's top, 56, and 10 px. */
#define SV_PROG_ROW_Y(r)   (90 + 56 * (r))

/** @brief SETTINGS' page tab @p i: OUTPUT, SWEEP, DUT. */
#define SV_PAGE_X(i)       (116 + 201 * (i))
#define SV_PAGE_Y          84                 /**< Their row. */
/** @brief SETTINGS' row @p r, either column. */
#define SV_ROW_Y(r)        (136 + 56 * (r))

#define SV_RUN_SET_X       528                /**< A run card's SETTINGS. */
/** @brief Run card @p k's SETTINGS (0 AUTOMATIC TEST, 1 LIMIT RUN). */
#define SV_RUN_SET_Y(k)    (162 + 224 * (k))
#define SV_RUN_CLOSE_X     548                /**< CLOSE on a run's page. */
#define SV_RUN_CLOSE_Y     84                 /**< Its row. */
#define SV_START_X         178                /**< A run card's START. */
/** @brief Run card @p k's START. */
#define SV_START_Y(k)      (162 + 224 * (k))
/** @brief Run card @p k's top (0 AUTOMATIC TEST, 1 LIMIT RUN). */
#define SV_RUN_CARD_Y(k)   (60 + 224 * (k))
/** @brief The lines under a run card's START, from its top. */
#define SV_RUN_LINE_DY     142
/** @brief The run's box on LIVE's card, left under ARM FIRST: 346 px wide, its lines
 *         24 px apart from 10 px in, its button 150 x 48 px 112 px down. */
#define SV_BOX_X           18
#define SV_BOX_Y           142                /**< The box's top. */
#define SV_BOX_W           346                /**< Its width. */
/** @brief The box's line @p i, in the 10 x 20 face. */
#define SV_BOX_LINE_Y(i)   (SV_BOX_Y + 10 + 24 * (i))
#define SV_BOX_BTN_X       103                /**< STOP TEST or CLOSE. */
#define SV_BOX_BTN_Y       278                /**< Its row. */
/** @brief The run's settings: the limit run's mode note, under row 2. */
#define SV_LIM_NOTE_Y      270
/** @brief A run's settings' column @p c, the middle of its value. */
#define SV_LIST_X(c)       (114 + 203 * (c))
/** @brief A run's settings' row @p r, the middle of its value. */
#define SV_LIST_ROW_Y(r)   (162 + 74 * (r))

/** @brief SPEED's label row over its track: y 232 to 252. */
#define SV_SPEED_ROW_Y     232
/** @brief SPEED's track, its middle row: y 256 to 296. */
#define SV_SPEED_Y         276
#define SV_TARGET_X        858                /**< The TARGET row's value. */
#define SV_TARGET_Y        332                /**< Its row. */

/** @brief The left column the keypad, the keyboard and the programmer
 *         cover: OV_X, OV_Y, OV_W and OV_H of servo_screen_int.h. */
#define SV_OV_AREA \
    ((gfx_rect_t){ UI_MARGIN, UI_MARGIN, 620, UI_BODY_H - 2 * UI_MARGIN })

/** @brief RCARD_X, RCARD_W, RC_X and RC_W of servo_screen_int.h. */
#define SV_RCARD_X         (UI_MARGIN + 620 + 8)
#define SV_RCARD_W         (UI_PANEL_W - SV_RCARD_X - UI_MARGIN) /**< 380. */
#define SV_RC_X            (SV_RCARD_X + 12)  /**< The inner column. */
#define SV_RC_W            (SV_RCARD_W - 24)  /**< Its width: 356. */
/** @brief CENTRE, SWEEP and RELEASE (servo_screen.c), 113 px each, 8 px
 *         apart: their row and the middle of each. */
#define SV_BTN_Y           (366 + 24)
#define SV_CENTRE_X        (SV_RC_X + 56)            /**< CENTRE. */
#define SV_SWEEP_X         (SV_RC_X + 121 + 56)      /**< SWEEP. */
#define SV_RELEASE_X       (SV_RC_X + 242 + 57)      /**< RELEASE. */
/** @brief ARM FIRST on LIVE's card, under the horn's angle: its line. */
#define SV_HINT_X          24
#define SV_HINT_Y          114                /**< Its top. */
/** @brief ARM and DISARM across the column: y 424 to 484. */
#define SV_ARM_Y           424
#define SV_ARM_H           60                 /**< Its height. */
/** @brief The SET row: the voltage's and the current limit's value boxes,
 *         84 px each from SV_RC_X + 48, 8 px apart, and OUTPUT ON to the
 *         column's end. */
#define SV_SUP_Y           198
#define SV_SUP_V_X         (SV_RC_X + 48 + 42)        /**< The voltage. */
#define SV_SUP_I_X         (SV_RC_X + 48 + 92 + 42)   /**< The current. */
#define SV_SUP_OUT_X       ((SV_RC_X + 232 + SV_RC_X + SV_RC_W) / 2)
