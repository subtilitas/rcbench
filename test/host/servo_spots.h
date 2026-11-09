/**
 * @file
 * @brief Where a finger lands on the SERVO hub and its function screens, in
 *        the screen's own coordinates (the body under the band), mirrored
 *        from servo_screen_int.h, servo_workbench_int.h, servo_hub.c and
 *        ui_footer.h.
 *
 * If the layout moves these move with it.  Taken by servo_rig.h and
 * render_screen.c.
 *
 *   - the hub's tiles: 328 x 130px at x 10, 348 and 686, y 10, 150 and
 *     290, LIVE first (SV_HUB_TILE_*);
 *   - the footer: three slots of 328 x 48px at x 10, 348 and 686, y 444 to
 *     492: the name and port line, ARM / DISARM, SETTINGS;
 *   - the PORT button over the output card: x 636 to 1016, y 8 to 56;
 *   - the left card from y 8 to 436: LIVE's dial and readings, the run
 *     card of TEST and LIMIT RUN from y 12, LIMIT RUN's with START and
 *     SETTINGS 56px down, TEST's 136px tall with its caps button at y 152
 *     to 200; the limit run's settings' rows in three columns of 193px
 *     from x 18, 203px apart, each its label over its 48px value, 70px
 *     apart from y 64; its CLOSE x 478 to 618, y 12 to 60;
 *   - the output card from SV_RCARD_X, y 62 to 436, its inner column SV_RC_X
 *     and SV_RC_W.  On LIVE, SWEEP and RUDDER: the READY rows, 30px from
 *     y 96; the SET row 4px under them, y 190 to 238 on LIVE and SWEEP,
 *     250 to 298 on RUDDER: SET V and SET A, 80px each from SV_RC_X, 8px
 *     apart, the plot to the column's end; on LIVE and SWEEP SPEED's track
 *     at y 268 to 308; the type pill at y 318 to 350; CENTER (LIVE) or
 *     SWEEP (SWEEP) and RELEASE, half of it each, y 356 to 404; the rail
 *     line at y 412.  SV_SUP_* name where the output card's SET row and
 *     OUTPUT ON once were, y 174 to 222: on LIVE the SET row's plot.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui_layout.h"

/** @brief The output shaft on LIVE's card: SHAFT_X and SHAFT_Y of
 *         servo_screen_core_int.h, (8 + 358) / 2. */
#define SV_SHAFT_X         400
#define SV_SHAFT_Y         183                /**< The shaft's row. */

/** @brief The hub's tile @p i, LIVE first: its middle. */
#define SV_HUB_TILE_X(i)   (10 + 338 * ((i) % 3) + 164)
#define SV_HUB_TILE_Y(i)   (10 + 140 * ((i) / 3) + 65) /**< Its row. */

/** @brief The footer's slots: x 10, 348, 686, each 328px; y 444 to 492. */
#define SV_FOOT_Y          468                /**< Their middle row. */
#define SV_FOOT_NAME_X     174                /**< The name's middle. */
#define SV_FOOT_SET_X      850                /**< SETTINGS' middle. */
/** @brief The PORT button, x 636 to 1016, y 8 to 56: its middle. */
#define SV_PORT_X          826
#define SV_PORT_Y          32                 /**< Its row. */

/** @brief RUDDER's LENGTH row (servo_rudder_int.h): x 18 to 518, y 120
 *         to 168; its middle. */
#define SV_RUD_LEN_X       268
#define SV_RUD_LEN_Y       144                /**< Its row. */
/** @brief RUDDER's ZERO row (servo_rudder_int.h): y 240 to 288. */
#define SV_RUD_ZERO_Y      264                /**< The ZERO row. */
/** @brief AUTO's box (servo_rudder_auto_int.h): its left and right half,
 *         x 18 to 263 and 273 to 518, their middles. */
#define SV_RA_L_X          140
#define SV_RA_R_X          395                /**< The right half. */
/** @brief The box's row @p r: TARGET (0) at y 120 to 168, MARGIN and
 *         TOLERANCE (1) at 180 to 228. */
#define SV_RA_ROW_Y(r)     (144 + 60 * (r))
/** @brief RUDDER AUTO's HOLD 2s: ARM + START, the footer's middle slot:
 *         its middle. */
#define SV_RA_START_X      512
#define SV_RA_START_Y      468                /**< Its row. */
/** @brief The function's one hold, RUDDER AUTO's, TEST's and LIMIT RUN's
 *         HOLD 2s: ARM + START, the footer's middle slot: its middle. */
#define SV_HOLD_X          512
#define SV_HOLD_Y          468                /**< Its row. */
/** @brief The READY card of RUDDER AUTO, TEST and LIMIT RUN
 *         (servo_ready_int.h): row @p i, 30px from y 96, its middle; the
 *         card's column x 648 to 1004. */
#define SV_RDY_ROW_Y(i)    (96 + 30 * (i) + 15)
#define SV_RDY_X           826                /**< The rows' middle. */
/** @brief RUDDER's readings: the numerals' top, and the mm column's left
 *         edge (the degrees' is x 18). */
#define SV_RUD_NUM_Y       38
#define SV_RUD_MM_X        268                /**< The mm column. */

/** @brief Where TEST's run card once had its SETTINGS: no control. */
#define SV_RUN_SET_X       528
#define SV_RUN_SET_Y       92                 /**< Its row. */
#define SV_RUN_CLOSE_X     548                /**< CLOSE on a page of rows. */
#define SV_RUN_CLOSE_Y     36                 /**< Its row. */
/** @brief TEST's run card's top. */
#define SV_RUN_CARD_Y      12
/** @brief The caps button under TEST's card, 600 x 48px, its middle. */
#define SV_CAPS_X          300
#define SV_CAPS_TEST_Y     176                /**< Its middle row on TEST. */
/** @brief TEST's HOLD 2s: ARM + START, the footer's middle slot: its
 *         middle. */
#define SV_TEST_HOLD_X     512
#define SV_TEST_HOLD_Y     468                /**< Its row. */
/** @brief TEST's lines on its card, from the card's top. */
#define SV_TEST_LINE_DY    56
/** @brief TEST's run box, left: 346px wide, its lines 24px apart from
 *         10px in, its button 150 x 48px 112px down, from y 204 under the
 *         caps button. */
#define SV_BOX_X           18
#define SV_BOX_Y           204                /**< The box's top. */
#define SV_BOX_W           346                /**< Its width. */
/** @brief The box's line @p i, in the 10 x 20 face, on TEST. */
#define SV_BOX_LINE_Y(i)   (SV_BOX_Y + 10 + 24 * (i))
#define SV_BOX_BTN_X       103                /**< STOP TEST or CLOSE. */
#define SV_BOX_BTN_Y       (SV_BOX_Y + 112 + 24)     /**< Its row on TEST. */
/** @brief A page of rows' column @p c, the middle of its value. */
#define SV_LIST_X(c)       (114 + 203 * (c))
/** @brief A page of rows' row @p r, the middle of its value. */
#define SV_LIST_ROW_Y(r)   (109 + 70 * (r))

/** @brief SPEED's label row over its track on LIVE and SWEEP: y 244 to
 *         264. */
#define SV_SPEED_ROW_Y     244
/** @brief SPEED's track, its middle row: y 268 to 308. */
#define SV_SPEED_Y         288
/** @brief The type pill under the SET row, y 318 to 350. */
#define SV_TAG_Y           318
#define SV_TAG_H           32                 /**< Its height. */
/** @brief The SET row of LIVE, SWEEP and RUDDER (servo_drive_ready.c):
 *         its middle row under LIVE's and SWEEP's three READY rows, y 190
 *         to 238, and under RUDDER's five, y 250 to 298. */
#define SV_SET_Y           214
#define SV_SET_RUD_Y       274                /**< The same on RUDDER. */
/** @brief SET V's box, x SV_RC_X to + 80, its middle. */
#define SV_SET_V_X         (SV_RC_X + 40)
/** @brief SET A's box, 88px right of SET V's, its middle. */
#define SV_SET_A_X         (SV_RC_X + 88 + 40)
/** @brief The plot right of SET A, 180 x 48px to the column's end: its
 *         middle. */
#define SV_PLOT_X          (SV_RC_X + 176 + 90)
/** @brief Whether (@p x, @p y) is on LIVE's plot. */
#define SV_IN_PLOT(x, y)   ((x) >= SV_PLOT_X - 90 && (x) < SV_PLOT_X + 90 \
                            && (y) >= SV_SET_Y - 24 && (y) < SV_SET_Y + 24)
/** @brief Whether (@p x, @p y) is on LIVE's SET V. */
#define SV_IN_SET_V(x, y)  ((x) >= SV_SET_V_X - 40 && (x) < SV_SET_V_X + 40 \
                            && (y) >= SV_SET_Y - 24 && (y) < SV_SET_Y + 24)

/** @brief The left column the keypad, the keyboard and the programmer
 *         cover: OV_X, OV_Y, OV_W and OV_H of servo_screen_int.h, down to
 *         the card's floor at y 436. */
#define SV_OV_AREA \
    ((gfx_rect_t){ UI_MARGIN, UI_MARGIN, 620, 436 - UI_MARGIN })

/** @brief RCARD_X, RCARD_W, RC_X and RC_W of servo_screen_int.h. */
#define SV_RCARD_X         (UI_MARGIN + 620 + 8)
#define SV_RCARD_W         (UI_PANEL_W - SV_RCARD_X - UI_MARGIN) /**< 380. */
#define SV_RC_X            (SV_RCARD_X + 12)  /**< The inner column. */
#define SV_RC_W            (SV_RCARD_W - 24)  /**< Its width: 356. */
/** @brief CENTER on LIVE, SWEEP on SWEEP, and RELEASE on both
 *         (servo_drive_ready.c), 174px each, 8px apart: their row and the
 *         middle of each. */
#define SV_BTN_Y           (356 + 24)
#define SV_CENTER_X        (SV_RC_X + 87)            /**< CENTER, LIVE. */
#define SV_SWEEP_X         (SV_RC_X + 87)            /**< SWEEP, SWEEP. */
#define SV_RELEASE_X       (SV_RC_X + 182 + 87)      /**< RELEASE. */
/** @brief The READY rows on LIVE, SWEEP and RUDDER: card row @p i, 30px
 *         from y 96, its middle.  LIVE and SWEEP: PORT 0, SUPPLY 1, ARM 2;
 *         RUDDER: PORT 0, ANGLE 1, ZERO 2, SUPPLY 3, ARM 4. */
#define SV_DRV_ROW_Y(i)    (96 + 30 * (i) + 15)
/** @brief ARM FIRST on LIVE's card, under the horn's angle: its line. */
#define SV_HINT_X          24
#define SV_HINT_Y          66                 /**< Its top. */
/** @brief ARM and DISARM in the footer's middle slot: x 348 to 676, y 444
 *         to 492. */
#define SV_ARM_L           348
#define SV_ARM_W           328                /**< Its width. */
#define SV_ARM_X           512                /**< Its middle. */
#define SV_ARM_Y           444
#define SV_ARM_H           48                 /**< Its height. */
/** @brief The SET row: the voltage's and the current limit's value boxes,
 *         84px each from SV_RC_X + 48, 8px apart, and OUTPUT ON to the
 *         column's end. */
#define SV_SUP_Y           198
#define SV_SUP_V_X         (SV_RC_X + 48 + 42)        /**< The voltage. */
#define SV_SUP_I_X         (SV_RC_X + 48 + 92 + 42)   /**< The current. */
#define SV_SUP_OUT_X       ((SV_RC_X + 232 + SV_RC_X + SV_RC_W) / 2)
