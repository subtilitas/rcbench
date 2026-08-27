/*
 * Settings: categories on the left, the entries of the selected one on the
 * right.  The rows are rendered from the schema in components/settings, so
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
 * Push the display-related settings into the theme and repaint.
 * Call once at startup, and whenever an application setting changes.
 */
void settings_apply_ui(void);

const ui_screen_t *settings_screen(void);
void settings_screen_invalidate(void);

#ifdef __cplusplus
}
#endif
