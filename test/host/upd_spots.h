/**
 * @file
 * @brief Where a finger lands on UPDATES, SINGLE UPDATES, the start-up
 *        reminder and PANEL UPDATE, mirrored from upd_screen.c,
 *        upd_single.h, upd_c6.h, upd_remind.c and update_screen.c; and the
 *        taps that open SINGLE UPDATES.
 *
 * If the layout moves these move with it.  Taken by the screens' suites.
 *
 *   - UPDATES, in the body under the band: CHECK 460 x 56px from x 16 at
 *     y 16; the six step rows 460 x 48px from x 16, y 88, 56px apart; the
 *     card x 484 to 1016, y 16 to 436, the current step's control
 *     500 x 64px from x 500 at y 292 (the download's hold while PANEL's
 *     step is current), SKIP THIS STEP 500 x 56px at y 364; the footer
 *     (ui_footer.h) at y 444, 48px tall: CANCEL UPDATE in its middle slot
 *     from x 348, OPTIONS in its right slot from x 686, each 328px wide;
 *     OPTIONS' third row, Single updates, opens SINGLE UPDATES;
 *   - SINGLE UPDATES, in the body: the IOmcu's hold 460 x 56px at y 380;
 *     the heading in the card's place; the C6 RADIO hold 532 x 56px from
 *     x 484 at y 380; BACK in the footer's right slot;
 *   - the reminder, in the body: NOT NOW and NEVER REMIND ME 398 x 56px
 *     at y 328 from x 106 and 520, the hold 812 x 64px at y 400 from
 *     x 106;
 *   - PANEL UPDATE, on the whole panel: its one button 240 x 56px from
 *     x 24, 18px above the panel's foot.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "touch_types.h"
#include "ui_layout.h"
#include "ui_screen.h"
#include "ui_setlist.h"
#include "upd_screen.h"

#define UP_BTN_X       246      /**< UPDATES' buttons' middle column. */
#define UP_BTN_W       460      /**< Their width, px. */
#define UP_BTN_H       56       /**< Their height, px. */
#define UP_CHECK_Y     (16 + 28)     /**< CHECK FOR UPDATES' middle row. */
#define UP_FOOT_T      444      /**< The footer's top. */
#define UP_FOOT_Y      (UP_FOOT_T + 24)  /**< Its middle row. */
#define UP_ACT_X       750      /**< The current step's control's middle. */
#define UP_ACT_T       292      /**< Its top. */
#define UP_ACT_Y       (UP_ACT_T + 32)  /**< Its middle row. */
#define UP_SKIP_T      364      /**< SKIP THIS STEP's top. */
#define UP_SKIP_Y      (UP_SKIP_T + 28) /**< Its middle row. */
#define UP_ROW_T       88       /**< The first step row's top. */
#define UP_ROW_PITCH   56       /**< From one step row to the next. */
#define UP_CANCEL_X    512      /**< CANCEL UPDATE, the footer's middle. */
#define UP_DL_X        UP_ACT_X /**< HOLD 2s TO DOWNLOAD & INSTALL: PANEL's
                                     step's control. */
#define UP_DL_Y        UP_ACT_Y /**< Its middle row. */
#define UP_SET_X       850      /**< SETTINGS' middle. */
#define UP_SET_Y       UP_FOOT_Y     /**< Its middle row. */
#define UP_SINGLE_ROW  2        /**< OPTIONS' row Single updates. */
#define UP_BACK_X      UP_SET_X /**< SINGLE UPDATES' BACK: OPTIONS' slot. */
#define UP_BACK_Y      UP_FOOT_Y     /**< Its middle row. */
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

#define UP_U_BTN_X     144      /**< PANEL UPDATE's CLOSE or CANCEL. */
#define UP_U_BTN_Y     (UI_PANEL_H - 18 - 28)  /**< Its middle row. */

/** @brief The body row 4px over the alert band: nothing reaches it. */
#define UP_FLOOR       (UI_ALERT_Y - UI_BAND_H - 4)

/** @brief A tap of finger 1 at (@p x, @p y) on @p scr. */
static inline void upd_spot_tap(const ui_screen_t *scr, int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    scr->event(&e);
    e.type = TOUCH_EVENT_UP;
    scr->event(&e);
}

/** @brief The way an operator opens SINGLE UPDATES on UPDATES (@p scr):
 *         OPTIONS, then its row Single updates. */
static inline void upd_open_single(const ui_screen_t *scr)
{
    upd_spot_tap(scr, UP_SET_X, UP_SET_Y);
    const gfx_rect_t r = ui_setlist_row_rect(upd_screen_settings_list(),
                                             UP_SINGLE_ROW);
    upd_spot_tap(scr, r.x + r.w / 2, r.y + r.h / 2);
}
