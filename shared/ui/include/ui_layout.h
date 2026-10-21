/*
 * The panel's geometry, in one place.
 *
 * Every screen, the router and the host renderers take the panel size, the
 * band, the body and the shared metrics from here, so a different panel
 * changes these figures and nothing else.  Per-screen constants stay in the
 * screen's own files, in panel pixels.
 *
 * Plain constants only: no includes, so a file that needs a size does not pull
 * in the drawing or screen interfaces.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/** The panel, in pixels. */
#define UI_PANEL_W 800
#define UI_PANEL_H 480

/** The band the router owns across the top of the panel, with STOP. */
#define UI_BAND_H 48

/**
 * The alert band the router draws across the bottom of the panel while an
 * alert shows: its height, and its top in panel coordinates.
 */
#define UI_ALERT_H 34
#define UI_ALERT_Y (UI_PANEL_H - UI_ALERT_H)

/** The body below the band: the sub-canvas the router hands a screen. */
#define UI_BODY_H (UI_PANEL_H - UI_BAND_H)

/** A screen's outer margin, in pixels. */
#define UI_MARGIN 6

/** Corner radii, one scale for cards, controls and chips. */
#define UI_R_CARD 8
#define UI_R_CTL  6
#define UI_R_CHIP 4

/** The smallest touch target for a control, in pixels. */
#define UI_TAP_MIN 48

/** Button heights: the common button, and the taller one in the ESC panel. */
#define UI_BTN_H      36
#define UI_BTN_H_TALL 44
