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
 *   - the header from x 18, y 8 to 48: SIMULATED's button on the model;
 *   - LIVE's output card from y 56: PLOT | TABLE from x 18, 200px for the
 *     two, y 82 to 122; the plot y 150 to 228;
 *   - SET POINTS from y 246: each row's step keys 72 x 48px at x 18 and
 *     418, the track x 98 to 410, the value's box x 498 to 618; the
 *     voltage's controls y 296 to 344, the current limit's y 378 to 426;
 *   - the rail from x 636, 380px: the cards 64px tall from y 56, 68px
 *     apart; the CONTROL card from y 328: RESET PEAKS y 354 to 402;
 *   - the footer (ui_footer.h), y 444 to 492: OUTPUT ON / OFF x 348 to
 *     676 and SETTINGS x 686 to 1014;
 *   - the keypad and the question over the left column, x 8 to 628,
 *     y 56 to 436; the question's APPLY and CANCEL, 240 x 60px from
 *     y 296;
 *   - the SETTINGS list over the whole body (ui_setlist.h): its rows'
 *     rects come from supply_settings_list() and ui_setlist_row_part().
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "gfx.h"
#include "ui_layout.h"

#define SU_INFO_X         18            /**< The header's left edge. */
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
#define SU_V_ROW_Y        320           /**< The voltage's middle row. */
#define SU_I_ROW_Y        402           /**< The current limit's. */
#define SU_CTRL_H         48            /**< Keys, track and value, px. */

#define SU_CARD_X         826           /**< The rail's cards. */
#define SU_CARD_T(i)      (56 + (i) * 68) /**< Card @p i's top, 64px. */
#define SU_CARD_Y(i)      (SU_CARD_T(i) + 32) /**< Card @p i's middle. */
#define SU_CARD_L         636           /**< The cards' left edge. */
#define SU_CARD_R         1016          /**< Their right edge. */
#define SU_RC_X           648           /**< The CONTROL card's column. */
#define SU_RC_W           356           /**< Its width, px. */
#define SU_RESET_X        826           /**< RESET PEAKS. */
#define SU_RESET_Y        378           /**< Its middle row. */
#define SU_OUT_X          512           /**< OUTPUT ON / OFF: the footer's
                                             middle slot. */
#define SU_OUT_L          348           /**< Its left edge. */
#define SU_OUT_W          328           /**< Its width, px. */
#define SU_OUT_T          444           /**< Its top. */
#define SU_OUT_H          48            /**< Its height, px. */
#define SU_OUT_Y          468           /**< Its middle row. */
#define SU_SETTINGS_X     850           /**< The footer's SETTINGS. */
#define SU_SETTINGS_Y     468

/** @brief The left column the keypad and the question cover. */
#define SU_OV_AREA        ((gfx_rect_t){ UI_MARGIN, 56, 620, 380 })
/** @brief The SIMULATED line's button, on the header's row: x 18 to 368 in
 *         the wider of the two languages. */
#define SU_MODEL_X        100
#define SU_MODEL_Y        28

/** @brief The SETTINGS list's keypad: over the body down to the list's
 *         strip (ui_setlist_int.h). */
#define SU_LIST_KP_AREA   ((gfx_rect_t){ 10, 10, 1004, 482 })
#define SU_LIST_RESET_X   512           /**< The list's RESET SUPPLY. */
#define SU_LIST_SAVE_X    850           /**< Its SAVE. */
#define SU_LIST_STRIP_Y   468           /**< The list's strip, the footer's
                                             place. */
#define SU_LIST_DRAG_X    300           /**< A drag on the list's rows. */
#define SU_LIST_DRAG_Y    240

#define SU_APPLY_X        168           /**< The question's APPLY. */
#define SU_DISCARD_X      468           /**< Its CANCEL. */
#define SU_ASK_Y          352           /**< Inside both, on no LIVE
                                             control: between the voltage's
                                             controls and the current
                                             limit's. */
