/**
 * @file
 * @brief Where a finger lands on RECEIVER, BATTERY and BALANCE, and where
 *        their parts lie, in the screens' own coordinates (the body under
 *        the band), mirrored from analyser_screen.c, battery_screen.c and
 *        balance_screen.c.
 *
 * If the layout moves these move with it.  Taken by the screens' suites
 * and render_screen.c.
 *
 *   - RECEIVER: CHANNELS | RAW DATA from x 11, 150 px each, 4 px apart,
 *     y 8 to 56; the lanes' card from y 64, x 8 to 748; eight lanes of
 *     52 px from y 72 in two columns 362 px apart from x 16; the state
 *     card x 756 to 1016, its counter rows 32 px from y 182, CH17 and
 *     CH18 44 px tall from y 384, 8 px apart;
 *   - BATTERY: the plot card x 8 to 648, the right card x 656 to 1016,
 *     the cells row's voltages from y 440;
 *   - BALANCE: BALANCE | TEST RIG | AIRCRAFT from x 11, 150 px each, 4 px
 *     apart, y 8 to 56; both cards from y 64, the right one x 656 to
 *     1016 with its column x 670 to 1002; PROPELLER and EDF 162 x 48 px
 *     at y 102, 8 px apart; the BLADES row y 160 to 208: the count x 760
 *     to 874, "-" x 882 to 938, "+" x 946 to 1002.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui_layout.h"

/** @brief The lowest row a control or a line of text reaches on these
 *         screens: 4 px above the alert band, body y 492. */
#define IN_TEXT_FLOOR      (UI_ALERT_Y - UI_BAND_H - 4)

/** @brief The tabs' middle row, both screens. */
#define IN_TAB_Y           32
#define IN_TAB_T           8             /**< The tabs' top. */
#define IN_TAB_H           48            /**< Their height, px. */
#define IN_TAB_W           150           /**< A tab's width, px. */
/** @brief Tab @p i's left edge. */
#define IN_TAB_L(i)        (11 + (i) * 154)
/** @brief Tab @p i's middle column. */
#define IN_TAB_X(i)        (IN_TAB_L(i) + 75)

#define AN_CARD_Y          64            /**< The lanes' card's top. */
#define AN_LANE_TOP        72            /**< The first lane's top. */
#define AN_LANE_H          52            /**< A lane's height, px. */
#define AN_COL_X(i)        (16 + (i) * 362)  /**< Lane column @p i. */
#define AN_LANE_W          350           /**< A lane's width, px. */
#define AN_RCARD_X         756           /**< The state card's left edge. */
#define AN_RCARD_W         260           /**< Its width, px. */
#define AN_CH17_Y          384           /**< CH17's top. */
#define AN_CHN_H           44            /**< CH17's and CH18's height. */

#define BT_LCARD_W         640           /**< The plot card's width, px. */
#define BT_RCARD_X         656           /**< The right card's left edge. */
#define BT_PLOT_X          26            /**< The plot's left edge. */
#define BT_PLOT_W          604           /**< Its width, px. */
#define BT_VOLT_Y          440           /**< The voltages' row. */

#define BL_CARD_Y          64            /**< Both cards' top. */
#define BL_RCARD_X         656           /**< The right card's left edge. */
#define BL_RC_X            670           /**< The right column. */
#define BL_RC_W            332           /**< Its width, px. */
#define BL_ROTOR_T         102           /**< PROPELLER's and EDF's top. */
#define BL_ROTOR_Y         126           /**< Their middle row. */
#define BL_PROP_X          751           /**< PROPELLER. */
#define BL_EDF_X           921           /**< EDF. */
#define BL_ROTOR_W         162           /**< A rotor button's width, px. */
#define BL_BLADES_T        160           /**< The BLADES row's top. */
#define BL_BLADES_Y        184           /**< Its middle row. */
#define BL_COUNT_X         817           /**< The count. */
#define BL_COUNT_L         760           /**< Its left edge. */
#define BL_COUNT_W         114           /**< Its width, px. */
#define BL_DOWN_X          910           /**< "-". */
#define BL_UP_X            974           /**< "+". */
#define BL_STEP_W          56            /**< A step key's width, px. */
#define BL_KP_Y            64            /**< The keypad's top. */
#define BL_KP_H            428           /**< Its height, px. */
#define BL_MARK_Y          378           /**< The SIMULATED pill's top. */
#define BL_NOTE_RULE_Y     406           /**< The rule over the note. */
