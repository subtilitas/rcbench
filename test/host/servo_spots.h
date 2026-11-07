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
 *     and SV_RC_W; the SET row's values and OUTPUT ON at y 174 to 222;
 *     SPEED; the type pill at y 310 to 354; CENTRE, SWEEP and RELEASE a
 *     third of it each, y 366 to 414.
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
/** @brief RUDDER AUTO's READY card (servo_ready_int.h): row @p i, 30px
 *         from y 96, its middle; the card's column x 648 to 1004. */
#define SV_RDY_ROW_Y(i)    (96 + 30 * (i) + 15)
#define SV_RDY_X           826                /**< The rows' middle. */
/** @brief RUDDER's readings: the numerals' top, and the mm column's left
 *         edge (the degrees' is x 18). */
#define SV_RUD_NUM_Y       38
#define SV_RUD_MM_X        268                /**< The mm column. */

#define SV_RUN_SET_X       528                /**< The run card's SETTINGS. */
/** @brief Run card @p k's SETTINGS (0 AUTOMATIC TEST on TEST, 1 LIMIT RUN
 *         on LIMIT RUN): each card at the page's top. */
#define SV_RUN_SET_Y(k)    (92 + 0 * (k))
#define SV_RUN_CLOSE_X     548                /**< CLOSE on a run's page. */
#define SV_RUN_CLOSE_Y     36                 /**< Its row. */
#define SV_START_X         178                /**< A run card's START. */
/** @brief Run card @p k's START. */
#define SV_START_Y(k)      (92 + 0 * (k))
/** @brief Run card @p k's top (0 AUTOMATIC TEST, 1 LIMIT RUN). */
#define SV_RUN_CARD_Y(k)   (12 + 0 * (k))
/** @brief The caps button under TEST's card, 600 x 48px, its middle. */
#define SV_CAPS_X          300
#define SV_CAPS_TEST_Y     176                /**< Its middle row on TEST. */
/** @brief TEST's HOLD 2s: ARM + START, the footer's middle slot: its
 *         middle. */
#define SV_TEST_HOLD_X     512
#define SV_TEST_HOLD_Y     468                /**< Its row. */
/** @brief TEST's lines on its card, from the card's top. */
#define SV_TEST_LINE_DY    56
/** @brief The lines under a run card's START, from its top. */
#define SV_RUN_LINE_DY     108
/** @brief The run's box under the run card, left: 346px wide, its lines
 *         24px apart from 10px in, its button 150 x 48px 112px down; on
 *         TEST from y 204 under the caps button, on LIMIT RUN from y 204
 *         under its card. */
#define SV_BOX_X           18
#define SV_BOX_Y           204                /**< The box's top on TEST. */
#define SV_BOX_Y_LIM       204                /**< On LIMIT RUN. */
#define SV_BOX_W           346                /**< Its width. */
/** @brief The box's line @p i, in the 10 x 20 face, on TEST. */
#define SV_BOX_LINE_Y(i)   (SV_BOX_Y + 10 + 24 * (i))
/** @brief The same on LIMIT RUN. */
#define SV_BOX_LINE_Y_LIM(i) (SV_BOX_Y_LIM + 10 + 24 * (i))
#define SV_BOX_BTN_X       103                /**< STOP TEST or CLOSE. */
#define SV_BOX_BTN_Y       (SV_BOX_Y + 112 + 24)     /**< Its row on TEST. */
#define SV_BOX_BTN_Y_LIM   (SV_BOX_Y_LIM + 112 + 24) /**< On LIMIT RUN. */
/** @brief The run's settings: the limit run's mode note, under row 1. */
#define SV_LIM_NOTE_Y      210
/** @brief A run's settings' column @p c, the middle of its value. */
#define SV_LIST_X(c)       (114 + 203 * (c))
/** @brief A run's settings' row @p r, the middle of its value. */
#define SV_LIST_ROW_Y(r)   (109 + 70 * (r))

/** @brief SPEED's label row over its track: y 232 to 252. */
#define SV_SPEED_ROW_Y     232
/** @brief SPEED's track, its middle row: y 256 to 296. */
#define SV_SPEED_Y         276
/** @brief The type pill, y 310 to 354, in the TARGET row's old place. */
#define SV_TAG_Y           310

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
/** @brief CENTRE, SWEEP and RELEASE (servo_screen.c), 113px each, 8px
 *         apart: their row and the middle of each.  CENTRE on every
 *         function screen but SWEEP's, SWEEP on SWEEP's alone. */
#define SV_BTN_Y           (366 + 24)
#define SV_CENTRE_X        (SV_RC_X + 56)            /**< CENTRE. */
#define SV_SWEEP_X         (SV_RC_X + 121 + 56)      /**< SWEEP. */
#define SV_RELEASE_X       (SV_RC_X + 242 + 57)      /**< RELEASE. */
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
