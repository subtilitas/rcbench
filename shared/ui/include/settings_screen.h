/*
 * Settings: categories on the left, the entries of the selected one on the
 * right.  The rows are rendered from the schema in shared/settings, so
 * adding a setting is a table row and not a screen change.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "settings.h"
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
 * The INTERFACES rows whose value the coprocessor does not hold:
 * @p sense_rows is sense_link_unheld()'s SENSE_LINK_ROW_* and @p tone_rows
 * tone_link_unheld()'s TONE_LINK_ROWS_*.  Such a row is drawn marked -- its
 * edge and its value in the warning colour, and "Not taken by the
 * coprocessor" in place of its help line -- until a call names it no
 * longer.  Sensor SDA and Sensor SCL are one mark: the pins are one frame.
 */
void settings_screen_set_unheld(uint16_t sense_rows, uint8_t tone_rows);

/** Whether the row of @p id is marked by settings_screen_set_unheld(). */
bool settings_screen_unheld(setting_id_t id);

/**
 * How far the open category's list is scrolled, in px from its top; 0 to
 * @p max, which is 0 for a list that fits its 402 px.  @p max may be NULL.
 */
int settings_screen_scroll(int *max);

#ifdef __cplusplus
}
#endif
