/**
 * @file
 * @brief Where a finger lands on UPDATES, the start-up reminder, the
 *        IOmcu's update question and PANEL UPDATE, mirrored from
 *        upd_screen.c, upd_c6.h, coproc_update_screen.c and
 *        update_screen.c.
 *
 * If the layout moves these move with it.  Taken by the screens' suites.
 *
 *   - UPDATES, in the body under the band: CHECK 460 x 56px from x 16 at
 *     y 16; the IOmcu's hold 460 x 56px at y 380; the card x 484 to 1016,
 *     y 16 to 336; the C6 RADIO hold 532 x 56px from x 484 at y 380; the
 *     footer (ui_footer.h) at y 444, 48px tall: the download's hold in its
 *     middle slot from x 348, SETTINGS in its right slot from x 686, each
 *     328px wide;
 *   - the reminder, in the body: NOT NOW and NEVER REMIND ME 398 x 56px
 *     at y 328 from x 106 and 520, the hold 812 x 64px at y 400 from
 *     x 106;
 *   - the IOmcu's question, in the body: CANCEL and START 398 x 56px at
 *     y 288 from x 106 and 520;
 *   - PANEL UPDATE, on the whole panel: its one button 240 x 56px from
 *     x 24, 18px above the panel's foot.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui_layout.h"

#define UP_BTN_X       246      /**< UPDATES' buttons' middle column. */
#define UP_BTN_W       460      /**< Their width, px. */
#define UP_BTN_H       56       /**< Their height, px. */
#define UP_CHECK_Y     (16 + 28)     /**< CHECK FOR UPDATES' middle row. */
#define UP_FOOT_T      444      /**< The footer's top. */
#define UP_FOOT_Y      (UP_FOOT_T + 24)  /**< Its middle row. */
#define UP_DL_X        512      /**< HOLD 2s TO DOWNLOAD & INSTALL's middle. */
#define UP_DL_Y        UP_FOOT_Y     /**< Its middle row. */
#define UP_SET_X       850      /**< SETTINGS' middle. */
#define UP_SET_Y       UP_FOOT_Y     /**< Its middle row. */
#define UP_CU_T        380      /**< HOLD 2s: UPDATE IOMCU's top. */
#define UP_CU_Y        (UP_CU_T + 28)  /**< Its middle row. */
#define UP_CARD_X      484      /**< The card's left edge. */
#define UP_CARD_B      336      /**< The row under the card's foot. */
#define UP_C6_X        750      /**< HOLD 2s TO UPDATE THE C6 RADIO's middle. */
#define UP_C6_T        380      /**< Its top, the IOmcu hold's row. */
#define UP_C6_Y        (UP_C6_T + 28)  /**< Its middle row. */

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

/** @brief The body row 4px over the alert band: nothing reaches it. */
#define UP_FLOOR       (UI_ALERT_Y - UI_BAND_H - 4)
