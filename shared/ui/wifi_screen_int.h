/**
 * @file
 * @brief WI-FI's private seam (wifi_screen.c, help_wifi.c): the layout of
 *        the radio-off and the radio-on views, what the screen guide may
 *        explain, and the guide's pages.
 *
 * The macros have short names (W, H, BX); nothing but the two files
 * includes this header.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "ui_layout.h"

/** @brief The area below the band: 1024 x 540px. */
#define W UI_PANEL_W
/** @brief The body's height under the band, UI_BODY_H px. */
#define H UI_BODY_H

/**
 * @brief The lowest body row anything is drawn on, plus one: 4px above
 *        the alert band, body y 492.
 */
#define FOOT    (UI_ALERT_Y - UI_BAND_H - 4)

/*
 * With the radio off: seven buttons of 460 x 60px (69.0 x 9.0mm) 8px apart on
 * the left, from body y 16; the card on the right, 16px beside them, to 8px
 * from the right edge and down to FOOT.
 * The card's lines are 49 cells of WF_FONT_LABEL, 24px apart.
 */
#define BX       16          /**< The buttons' left edge. */
#define BY       16          /**< The first button's top. */
#define BW       460         /**< A button's width, px. */
#define BH       60          /**< A button's height, px. */
#define BPITCH   68          /**< The buttons' pitch, px. */
#define RX       (BX + BW + 16) /**< The card's left edge. */
#define RY       16          /**< Its top. */
#define RW       (W - UI_MARGIN - RX) /**< Its width: 524px. */
#define RH       (FOOT - RY) /**< Its height: 476px. */
#define CARD_P   24          /**< The card's line pitch, px. */

/*
 * With the radio on: rows of 56px from body y 16, a label column
 * of 240px and a value column of 740px, an SSID of 32 characters
 * 640px of it in WF_FONT_HEAD; the lines under the rows in
 * WF_FONT_BODY 28px apart; WI-FI OFF 460 x 60px, 8px above FOOT.
 */
#define LBL_X    16          /**< The label column's left edge. */
#define LBL_W    240         /**< Its width, px. */
#define VAL_X    (LBL_X + LBL_W + 8) /**< The value column's left edge. */
#define VAL_W    740         /**< Its width, px. */
#define ROW_Y    16          /**< The first row's top. */
#define ROW_P    56          /**< The rows' pitch, px. */
#define LINE_P   28          /**< The pitch of the lines under the rows, px. */
#define OFF_Y    (FOOT - 8 - BH) /**< WI-FI OFF's top: body y 424. */
/** @brief The browser writes' button, beside WI-FI OFF at the right edge,
 *         the same size: body x 548. */
#define WR_X     (W - 16 - BW)

/** @brief The view of WI-FI the screen guide explains. */
typedef enum {
    WIFI_GUIDE_NONE = 0, /**< a list, a keyboard, a kept value's view or
                              the question: no page                     */
    WIFI_GUIDE_OFF,      /**< the radio off: the seven buttons, the card */
    WIFI_GUIDE_ON,       /**< the radio on: the rows, the lines, WI-FI
                              OFF                                      */
} wifi_guide_view_t;

/**
 * @brief The view the screen shows, as the guide explains it.
 * @return The view; WIFI_GUIDE_NONE under a level the guide does not
 *         explain.
 */
wifi_guide_view_t wifi_guide_view(void);

/**
 * @brief Whether the browser writes' button stands beside WI-FI OFF.
 * @return True while it does.
 */
bool wifi_guide_writes(void);

/**
 * @brief Whether HOTSPOT PASSWORD (@p pass) or ACCESS CODE holds a kept
 *        value, as its button reads KEPT.  The value stays in
 *        wifi_screen.c, one of the sources check_wifi_secrets.py lets
 *        hold one.
 * @param pass True for HOTSPOT PASSWORD, false for ACCESS CODE.
 * @return True for a kept value, false for RANDOM.
 */
bool wifi_guide_kept(bool pass);

struct ui_help_page;

/**
 * @brief The pages of WI-FI's guide as the screen is now: three with the
 *        radio off, one with it on, two while the writes' button stands.
 * @return The count; 0 under a level the guide does not explain.
 */
int wifi_help_count(void);

/**
 * @brief Page @p i of WI-FI's guide.
 * @param i   The page, from 0.
 * @param out The page, filled.
 * @return False past the last page.
 */
bool wifi_help_page(int i, struct ui_help_page *out);
