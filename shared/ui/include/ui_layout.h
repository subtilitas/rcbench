/**
 * @file
 * @brief The panel's geometry, in one place.
 *
 * Every screen, the router and the host renderers take the panel size, the
 * band, the body and the shared metrics from here, so a different panel
 * changes these figures and nothing else.  Two other places hold the size
 * the screens draw, both checked against this header: `BOARD_UI_H_RES` and
 * `BOARD_UI_V_RES` in the panel firmware's `board_pins.h`, the display's
 * canvas (a `_Static_assert` in `main.c`, beside one that the canvas fits
 * the glass, `BOARD_LCD_H_RES` x `BOARD_LCD_V_RES`), and `INJ_RES_X` and
 * `INJ_RES_Y` in the touch injector's `injector.h` (`test_layout`).  Per-screen constants stay in the
 * screen's own files, in panel pixels.
 *
 * Plain constants only, with no includes, so a file that needs a size does
 * not pull in the drawing or screen interfaces.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/**
 * @brief The panel's width in pixels: the whole glass of the ESP32-P4 7B,
 *        about 153.6 mm across, 0.150 mm a pixel (assumed, not measured).
 */
#define UI_PANEL_W 1024
/** @brief The panel's height in pixels, about 90.0 mm. */
#define UI_PANEL_H 600

/**
 * @brief The height of the band the router owns across the top, with STOP:
 *        60 px, 9.0 mm.
 */
#define UI_BAND_H 60

/**
 * @brief The alert band the router draws across the bottom of the panel
 *        while an alert shows.
 *
 * UI_ALERT_H is its height and UI_ALERT_Y its top, in panel coordinates.
 */
#define UI_ALERT_H 44
#define UI_ALERT_Y (UI_PANEL_H - UI_ALERT_H)

/** @brief The height of the body below the band, the sub-canvas a screen gets. */
#define UI_BODY_H (UI_PANEL_H - UI_BAND_H)

/** @brief A screen's outer margin, in pixels. */
#define UI_MARGIN 8

/**
 * @brief Corner radii in pixels, one scale for cards, controls and chips.
 */
#define UI_R_CARD 10
#define UI_R_CTL  8
#define UI_R_CHIP 5

/**
 * @brief The smallest touch target for a control, in pixels: 48 px, 7.2 mm.
 *
 * No screen uses it yet; it is the figure later layouts size to.
 */
#define UI_TAP_MIN 48

/**
 * @brief Button heights in pixels: the common button and row, 44 px
 *        (6.6 mm), and the taller one, 56 px (8.4 mm), in the ESC panel.
 */
#define UI_BTN_H      44
#define UI_BTN_H_TALL 56
