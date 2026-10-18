/*
 * Settings: a hub of tiles, one per category and one per screen SETUP
 * opens, and a category's entries in a list across the screen.  The rows
 * are rendered from the schema in shared/settings, so adding a setting is a
 * table row and not a screen change; adding a tile is a row of the screen's
 * tile table.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
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
 * The INTERFACES rows whose value the IOmcu does not hold:
 * @p sense_rows is sense_link_unheld()'s SENSE_LINK_ROW_* and @p tone_rows
 * tone_link_unheld()'s TONE_LINK_ROWS_*.  Such a row is drawn marked -- its
 * edge and its value in the warning colour, and "Not taken by the
 * IOmcu" in place of its help line -- until a call names it no
 * longer.  Sensor SDA and Sensor SCL are one mark: the pins are one frame.
 */
void settings_screen_set_unheld(uint16_t sense_rows, uint8_t tone_rows);

/** Whether the row of @p id is marked by settings_screen_set_unheld(). */
bool settings_screen_unheld(setting_id_t id);

/**
 * How far the open category's list is scrolled, in px from its top; 0 to
 * @p max, which is 0 for a list that fits its 348 px.  @p max may be NULL.
 */
int settings_screen_scroll(int *max);

/**
 * The list row that shows the @p index-th setting of @p cat (as
 * settings_in_category() orders them), or -1 past the last.  A category
 * with section headings has a row for each, above its section, so the row
 * is the index plus the headings above it.
 */
int settings_screen_row_of(setting_cat_t cat, int index);

/** The rows of @p cat's list: its settings and its headings. */
int settings_screen_rows(setting_cat_t cat);

/** The category whose list is open, or -1 while the hub's tiles show. */
int settings_screen_category(void);

/** The hub's slots: a grid of three by three tiles, one page. */
#define SETTINGS_HUB_SLOTS 9

/**
 * Where the hub draws its entry @p index, in the screen's own coordinates
 * (the body under the band), into @p out when it is not NULL.  False for an
 * index outside 0 to SETTINGS_HUB_SLOTS - 1: a tenth entry has no slot on
 * the page.
 */
bool settings_hub_slot(int index, gfx_rect_t *out);

/** The entries of the hub's tile table: its categories and its screens. */
int settings_hub_tiles(void);

/**
 * Whether a tile of the hub opens screen @p id.  The home tag of such a
 * screen leads back to SETUP rather than to the menu.
 */
bool settings_screen_opens(ui_screen_id_t id);

/**
 * Whether the warning shows: the home tag left SETUP with settings unsaved
 * and no save asked (settings_dirty() and not settings_save_asked()), and
 * nothing has answered it yet.  STAY, the home tag again, SAVE, DISCARD and
 * any change of screen close it.
 */
bool settings_screen_warning(void);

/** The warning's change lines: SETTINGS_WARN_LINES fit. */
#define SETTINGS_WARN_LINES 10

/**
 * Line @p i of the warning's changes into @p line, as drawn: "name: saved
 * -> now", the values as the list shows them, each with its unit; the
 * values in schema order, then the strings.  With more changes than
 * SETTINGS_WARN_LINES, the last line counts the rest.  False past the last
 * line, and while the warning does not show.
 */
bool settings_screen_warn_line(int i, char *line, size_t n);

#ifdef __cplusplus
}
#endif
