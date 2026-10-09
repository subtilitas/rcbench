/*
 * Settings: categories on the left, the entries of the selected one on the
 * right.  The rows are rendered from the schema in shared/settings, so
 * adding a setting is a table row and not a screen change.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Push the display-related settings -- language, theme, brightness and
 * contrast -- into the interface and repaint every screen.
 * Call at startup, and whenever an application setting changes.
 */
void settings_apply_ui(void);

const ui_screen_t *settings_screen(void);
void settings_screen_invalidate(void);

/**
 * How far the open category's list is scrolled, in px from its top; 0 to
 * @p max, which is 0 for a list that fits its 402 px.  @p max may be NULL.
 */
int settings_screen_scroll(int *max);

#ifdef __cplusplus
}
#endif
