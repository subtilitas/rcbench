/**
 * @file
 * @brief Where a finger lands on UPDATES, the start-up reminder, the
 *        IOmcu's update question and PANEL UPDATE, mirrored from
 *        upd_screen.c, coproc_update_screen.c and update_screen.c.
 *
 * If the layout moves these move with it.  Taken by the screens' suites.
 *
 *   - UPDATES, in the body under the band: the buttons 460 x 56 px from
 *     x 16 at a 64 px pitch from y 16 (CHECK, the reminder, RELEASES, the
 *     download's hold); the IOmcu's hold 460 x 56 px at y 428; the card
 *     x 484 to 1016, y 16 to 488;
 *   - the reminder, in the body: NOT NOW and NEVER REMIND ME 398 x 56 px
 *     at y 328 from x 106 and 520, the hold 812 x 64 px at y 400 from
 *     x 106;
 *   - the IOmcu's question, in the body: CANCEL and START 398 x 56 px at
 *     y 288 from x 106 and 520;
 *   - PANEL UPDATE, on the whole panel: its one button 240 x 56 px from
 *     x 24, 18 px above the panel's foot.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui_layout.h"

#define UP_BTN_X       246      /**< UPDATES' buttons' middle column. */
#define UP_BTN_W       460      /**< Their width, px. */
#define UP_BTN_H       56       /**< Their height, px. */
/** @brief The middle row of UPDATES' button in row @p r: 0 CHECK, 1 the
 *         reminder, 2 RELEASES, 3 the download's hold. */
#define UP_ROW_Y(r)    (16 + (r) * 64 + 28)
#define UP_CHECK_Y     UP_ROW_Y(0)   /**< CHECK FOR UPDATES. */
#define UP_REMIND_Y    UP_ROW_Y(1)   /**< REMINDER AT START. */
#define UP_CHAN_Y      UP_ROW_Y(2)   /**< RELEASES. */
#define UP_DL_Y        UP_ROW_Y(3)   /**< HOLD 2 s: DOWNLOAD & INSTALL. */
#define UP_CU_T        428      /**< HOLD 2 s: UPDATE IOMCU's top. */
#define UP_CU_Y        (UP_CU_T + 28)  /**< Its middle row. */
#define UP_CARD_X      484      /**< The card's left edge. */
#define UP_CARD_B      488      /**< The row under the card's foot. */

#define UP_R_IGNORE_X  305      /**< NOT NOW. */
#define UP_R_NEVER_X   719      /**< NEVER REMIND ME. */
#define UP_R_ROW_T     328      /**< Their top. */
#define UP_R_ROW_Y     (UP_R_ROW_T + 28)  /**< Their middle row. */
#define UP_R_HOLD_X    512      /**< The reminder's hold. */
#define UP_R_HOLD_T    400      /**< Its top. */
#define UP_R_HOLD_Y    (UP_R_HOLD_T + 32) /**< Its middle row. */
#define UP_R_BOX_B     488      /**< The row under the reminder's box. */

#define CU_Q_CANCEL_X  305      /**< The question's CANCEL. */
#define CU_Q_START_X   719      /**< Its START. */
#define CU_Q_BTN_T     288      /**< Their top. */
#define CU_Q_BTN_Y     (CU_Q_BTN_T + 28)  /**< Their middle row. */
#define CU_Q_BTN_W     398      /**< Their width, px. */
#define CU_Q_BTN_H     56       /**< Their height, px. */

#define UP_U_BTN_X     144      /**< PANEL UPDATE's CLOSE or CANCEL. */
#define UP_U_BTN_Y     (UI_PANEL_H - 18 - 28)  /**< Its middle row. */

/** @brief The body row 4 px over the alert band: nothing reaches it. */
#define UP_FLOOR       (UI_ALERT_Y - UI_BAND_H - 4)
