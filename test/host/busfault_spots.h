/**
 * @file
 * @brief Where a finger lands on the bus-fault screen, on the whole panel,
 *        mirrored from busfault_screen.c.
 *
 * If the layout moves these move with it.  Taken by the screen's suites.
 * The acknowledgement runs 976 x 56 px from x 24, 16 px above the panel's
 * foot; the rule over it is 8 px higher; the three notes start at y 432,
 * 28 px apart; the number column at x 760.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui_layout.h"

#define BF_ACK_X  24                            /**< Its left edge. */
#define BF_ACK_W  976                           /**< Its width, px. */
#define BF_ACK_H  56                            /**< Its height, px. */
#define BF_ACK_Y  (UI_PANEL_H - BF_ACK_H - 16)  /**< Its top. */
#define BF_RULE_Y (BF_ACK_Y - 8)                /**< The rule over it. */
#define BF_NOTE_Y 432                           /**< The first note. */
#define BF_NUM_X  760                           /**< The number column. */
