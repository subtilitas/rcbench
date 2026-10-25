/**
 * @file
 * @brief Where a finger lands on the SUPPLY workbench, in the screen's own
 *        coordinates (the body under the band), mirrored from
 *        supply_screen_int.h and supply_screen.c.
 *
 * If the layout moves these move with it.  Taken by supply_screen_rig.h,
 * the screen's suites, test_safe_press.c, test_nav_drag.c and
 * render_screen.c.
 *
 *   - the strip: LIVE and SETTINGS from x 9, 150px each, 4px apart,
 *     y 8 to 48; the header right of it from x 328;
 *   - LIVE's output card from y 56: PLOT | TABLE from x 18, 200px for the
 *     two, y 82 to 122; the plot y 150 to 286;
 *   - SET POINTS from y 304: each row's step keys 72 x 48px at x 18 and
 *     418, the track x 98 to 410, the value's box x 498 to 618; the
 *     voltage's controls y 354 to 402, the current limit's y 436 to 484;
 *   - the rail from x 636, 380px: the cards 64px tall from y 56, 68px
 *     apart; the CONTROL card from y 332: RESET PEAKS y 366 to 414 and
 *     OUTPUT ON / OFF y 424 to 484, both x 648 to 1004;
 *   - SETTINGS' page and the keypad over the left column, x 8 to 628,
 *     y 56 to 532; the page's values 120 x 48px at x 158 (left column)
 *     and 498 (right), rows 56px apart;
 *   - the question's APPLY and CANCEL, 240 x 60px from y 296.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "gfx.h"
#include "ui_layout.h"

#define SU_TAB_Y          28            /**< The strip's tabs. */
/** @brief The strip's tab @p i: 0 LIVE, 1 SETTINGS. */
#define SU_TAB_X(i)       (9 + (i) * 154 + 75)
#define SU_INFO_X         328           /**< The header's left edge. */
#define SU_INFO_B         56            /**< The header's rows end here. */

#define SU_TAG_Y          56            /**< The output card's title tag:
                                             y 56 to 76, from x 8. */
#define SU_PANE_Y         102           /**< PLOT | TABLE. */
#define SU_PLOT_TAB_X     68            /**< PLOT. */
#define SU_TABLE_TAB_X    168           /**< TABLE. */
#define SU_PLOT_X         300           /**< The plot, clear of controls. */
#define SU_PLOT_Y         200

#define SU_DOWN_X         54            /**< The -0.1 keys. */
#define SU_UP_X           454           /**< The +0.1 keys. */
#define SU_TRACK_X        98            /**< The tracks' left end. */
#define SU_TRACK_W        312           /**< Their length, px. */
#define SU_VAL_X          558           /**< The values' boxes. */
#define SU_VAL_R          617           /**< Their right column. */
#define SU_V_ROW_Y        378           /**< The voltage's middle row. */
#define SU_I_ROW_Y        460           /**< The current limit's. */
#define SU_CTRL_H         48            /**< Keys, track and value, px. */

#define SU_CARD_X         826           /**< The rail's cards. */
#define SU_CARD_T(i)      (56 + (i) * 68) /**< Card @p i's top, 64px. */
#define SU_CARD_Y(i)      (SU_CARD_T(i) + 32) /**< Card @p i's middle. */
#define SU_CARD_L         636           /**< The cards' left edge. */
#define SU_CARD_R         1016          /**< Their right edge. */
#define SU_RC_X           648           /**< The CONTROL card's column. */
#define SU_RC_W           356           /**< Its width, px. */
#define SU_RESET_X        826           /**< RESET PEAKS. */
#define SU_RESET_Y        390           /**< Its middle row. */
#define SU_OUT_X          826           /**< OUTPUT ON / OFF. */
#define SU_OUT_T          424           /**< Its top. */
#define SU_OUT_H          60            /**< Its height, px. */
#define SU_OUT_Y          454           /**< Its middle row. */

/** @brief The left column the keypad, the question and SETTINGS cover. */
#define SU_OV_AREA \
    ((gfx_rect_t){ UI_MARGIN, 56, 620, UI_BODY_H - 56 - UI_MARGIN })
#define SU_ROW_L_X        218           /**< SETTINGS' left values. */
#define SU_ROW_R_X        558           /**< Its right values. */
#define SU_ROW_PITCH      56            /**< From row to row, px. */
#define SU_VMAX_Y         116           /**< VOLTAGE MAX. */
#define SU_VSTART_Y       260           /**< START VOLTAGE. */
#define SU_ISTART_Y       316           /**< START CURRENT. */
#define SU_TRIPI_Y        116           /**< CURRENT TRIP. */
#define SU_CONF_SL_Y      376           /**< SLIDER AND STEPS. */
#define SU_CONF_KP_Y      432           /**< KEYPAD. */
#define SU_MODRESET_X     148           /**< RESET PD MINI. */
#define SU_MODRESET_Y     380

#define SU_APPLY_X        168           /**< The question's APPLY. */
#define SU_DISCARD_X      468           /**< Its CANCEL. */
#define SU_ASK_Y          346           /**< Inside both, on no LIVE or
                                             SETTINGS control. */

/** @brief SETTINGS' save line, 4px above the alert band. */
#define SU_SAVE_LINE_Y    (UI_ALERT_Y - UI_BAND_H - 4 - 20)
