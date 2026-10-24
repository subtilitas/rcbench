/**
 * @file
 * @brief Where a finger lands on WI-FI, in the screen's own coordinates
 *        (the body under the band), mirrored from wifi_screen.c.
 *
 * If the layout moves these move with it.  Taken by the screen's suites
 * and render_screen.c.
 *
 *   - the buttons with the radio off: 460 x 60px at x 16, from y 16,
 *     68px apart; the card from x 492 to 1016, y 16 to 492;
 *   - WI-FI OFF with the radio on: 460 x 60px at x 16, y 424 to 484;
 *   - the scan list: rows of 492 x 48px at x 12 and 520, from y 12,
 *     52px apart, eight a column; SCAN AGAIN, HIDDEN NETWORK and CANCEL
 *     328 x 56px at x 8, 348 and 688, y 436 to 492;
 *   - the question: a card from (8, 8) to (1016, 492); CANCEL and
 *     HOLD 2s: INSTALL 460 x 72px at x 24 and 540, y 404 to 476.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui_layout.h"

/** @brief The lowest body row anything is drawn on, plus one: 4px above
 *         the alert band. */
#define WF_FOOT        (UI_ALERT_Y - UI_BAND_H - 4)

#define WF_BTN_X       16            /**< The buttons' left edge. */
#define WF_BTN_Y       16            /**< The first button's top. */
#define WF_BTN_W       460           /**< A button's width, px. */
#define WF_BTN_H       60            /**< Its height, px. */
#define WF_BTN_P       68            /**< The buttons' pitch, px. */
/** @brief Button @p i's middle column: 0 ACCESS POINT, 1 JOIN, 2 CHOOSE,
 *         3 FORGET, 4 ACCESS CODE. */
#define WF_BTN_CX(i)   (WF_BTN_X + WF_BTN_W / 2)
/** @brief Button @p i's middle row. */
#define WF_BTN_CY(i)   (WF_BTN_Y + (i) * WF_BTN_P + WF_BTN_H / 2)

#define WF_CARD_X      492           /**< The card's left edge. */
#define WF_CARD_Y      16            /**< Its top. */
#define WF_CARD_W      524           /**< Its width, px. */

#define WF_OFF_Y       424           /**< WI-FI OFF's top. */
#define WF_OFF_CX      (WF_BTN_X + WF_BTN_W / 2) /**< Its middle column. */
#define WF_OFF_CY      (WF_OFF_Y + WF_BTN_H / 2) /**< Its middle row. */

#define WF_ROW_X0      12            /**< The scan list's left column. */
#define WF_ROW_X1      520           /**< Its right column. */
#define WF_ROW_Y       12            /**< Its first row's top. */
#define WF_ROW_W       492           /**< A row's width, px. */
#define WF_ROW_H       48            /**< Its height, px. */
#define WF_ROW_P       52            /**< The rows' pitch, px. */
/** @brief Row @p i's left edge: eight a column. */
#define WF_ROW_X(i)    (((i) < 8) ? WF_ROW_X0 : WF_ROW_X1)
/** @brief Row @p i's top. */
#define WF_ROW_T(i)    (WF_ROW_Y + ((i) % 8) * WF_ROW_P)
/** @brief A point in row @p i, 100px in, at its middle row. */
#define WF_ROW_CX(i)   (WF_ROW_X(i) + 100)
/** @brief Row @p i's middle row. */
#define WF_ROW_CY(i)   (WF_ROW_T(i) + WF_ROW_H / 2)

#define WF_SCAN_BY     436           /**< The scan buttons' top. */
#define WF_SCAN_BW     328           /**< Their width, px. */
#define WF_SCAN_BH     56            /**< Their height, px. */
#define WF_SCAN_CY     (WF_SCAN_BY + WF_SCAN_BH / 2) /**< Their middle row. */
/** @brief Scan button @p i's left edge: 0 SCAN AGAIN, 1 HIDDEN NETWORK,
 *         2 CANCEL. */
#define WF_SCAN_BX(i)  (8 + (i) * 340)
#define WF_RESCAN_CX   (WF_SCAN_BX(0) + WF_SCAN_BW / 2) /**< SCAN AGAIN. */
#define WF_HIDDEN_CX   (WF_SCAN_BX(1) + WF_SCAN_BW / 2) /**< HIDDEN NETWORK. */
#define WF_SCAN_CANCEL_CX (WF_SCAN_BX(2) + WF_SCAN_BW / 2) /**< CANCEL. */

#define WF_Q_X         8             /**< The question's left edge. */
#define WF_Q_Y         8             /**< Its top. */
#define WF_Q_W         1008          /**< Its width, px. */
#define WF_Q_BY        404           /**< Its buttons' top. */
#define WF_Q_BW        460           /**< Their width, px. */
#define WF_Q_BH        72            /**< Their height, px. */
#define WF_Q_CANCEL_X  24            /**< CANCEL's left edge. */
#define WF_INSTALL_X   540           /**< HOLD 2s: INSTALL's left edge. */
#define WF_Q_CANCEL_CX (WF_Q_CANCEL_X + WF_Q_BW / 2) /**< CANCEL. */
#define WF_INSTALL_CX  (WF_INSTALL_X + WF_Q_BW / 2)  /**< INSTALL. */
#define WF_Q_CY        (WF_Q_BY + WF_Q_BH / 2)       /**< Their middle row. */
