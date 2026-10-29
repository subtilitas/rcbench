/**
 * @file
 * @brief Settings: a hub of tiles, one per category and one per screen SETUP
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
#include "ui_keypad.h"
#include "ui_screen.h"
#include "ui_textkey.h"

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

/**
 * What the IOmcu holds of Hitec D receive (SET_HITEC_RX), a
 * hitec_rx_link_mark_t: HITEC_RX_LINK_HELD draws the row plain, the others
 * mark it as an unheld row; HITEC_RX_LINK_OLD says in its help line that
 * the IOmcu receives with PULL-UP.
 */
void settings_screen_set_hitec_rx(uint8_t mark);

/** Whether the row of @p id is marked by settings_screen_set_unheld() or
 *  settings_screen_set_hitec_rx(). */
bool settings_screen_unheld(setting_id_t id);

/**
 * The help line of the row of @p id: PD mini TX and RX on one pin first,
 * then a mark's line, then the setting's help.  Hitec D receive's help
 * follows its value: PULL-UP the setting's, PRECHARGE its own.
 */
const char *settings_screen_help(setting_id_t id);

/**
 * @brief How far the open category's list is scrolled.
 * @param max Its scroll range into, when not NULL: 0 for a list that fits
 *            its 428px.
 * @return The scroll, in px from the list's top: 0 to the range.
 */
int settings_screen_scroll(int *max);

/**
 * @brief The open list's pane, in the screen's own coordinates (the body
 *        under the band): the rows show inside it and nowhere else.
 * @return The pane.
 */
gfx_rect_t settings_screen_list_rect(void);

/**
 * @brief Where list row @p row lies at the list's present scroll, in the
 *        screen's own coordinates; partly or wholly outside the pane for a
 *        row scrolled out of it.
 * @param row The row, from 0.
 * @return The row's rectangle.
 */
gfx_rect_t settings_screen_row_rect(int row);

/**
 * @brief The parts of list row @p row a tap acts on, in the screen's own
 *        coordinates at the list's present scroll: the "-" key, the value
 *        between the keys and the "+" key, each over the row's height.
 */
typedef enum {
    SETTINGS_ROW_MINUS = 0,   /**< the "-" or "<" key                  */
    SETTINGS_ROW_VALUE,       /**< the value: a tap opens the keypad    */
    SETTINGS_ROW_PLUS,        /**< the "+" or ">" key                  */
} settings_row_part_t;

/**
 * @brief Where @p part of list row @p row lies; settings_row_part_t says
 *        which part.  Whether the row has keys or a keypad is the row's
 *        own: the rectangle is where they are when it does.
 * @param row  The row, from 0.
 * @param part The part.
 * @return The part's rectangle.
 */
gfx_rect_t settings_screen_row_part(int row, settings_row_part_t part);

/**
 * The list row that shows the @p index-th setting of @p cat (as
 * settings_in_category() orders them), or -1 past the last and for a
 * setting not listed (SENSORS' rows of a part not fitted).  A category
 * with section headings has a row for each, above its section, so the row
 * is the index plus the headings above it.
 */
int settings_screen_row_of(setting_cat_t cat, int index);

/** The list row that shows string @p id of @p cat, after its settings
 *  (settings_texts_in_category()), or -1 for a string not in @p cat. */
int settings_screen_text_row(setting_cat_t cat, setting_text_id_t id);

/** The rows of @p cat's list: its settings, its headings and its
 *  strings. */
int settings_screen_rows(setting_cat_t cat);

/**
 * The keyboard a string's row opens, in folder mode for SD CARD's log
 * folders (ui_textkey.h): open over the list and the strip until OK takes
 * the text, CANCEL or the home tag drops it, or SETUP is left.  OK sets
 * the string as an edit; SAVE keeps it.
 */
const ui_textkey_t *settings_screen_textkey(void);

/**
 * @brief The keypad a tap on a number's value opens: over the list and the
 *        strip until OK sets the value (settings_set(), as a step does),
 *        CANCEL or the home tag drops it, or SETUP is left.
 * @return The keypad, open or closed.
 */
const ui_keypad_t *settings_screen_keypad(void);

/**
 * @brief The setting the open keypad types.
 * @return Its setting_id_t, or -1 while the keypad is closed.
 */
int settings_screen_keypad_setting(void);

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
 * @brief Whether hub tile @p i is drawn and takes a tap: every tile but PICK
 *        A PIN on a board whose pins are soldered (ui_board_roles()).
 * @param i The tile, 0 to TILE_COUNT - 1.
 * @return false for a tile out of range and for the hidden one.
 */
bool settings_hub_tile_shown(int i);

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
