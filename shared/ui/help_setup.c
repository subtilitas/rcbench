/**
 * @file
 * @brief SETUP's screen guide (settings_screen_int.h): two pages for the
 *        tiles, two for a list, two for SENSORS.
 *
 * The tiles: the left and middle columns' first two rows and the third
 * row's first tile with the dock on the right, the right column, the third
 * row's middle tile and SAVE with the dock on the left.  A middle tile is
 * marked where the dock leaves it in view.  A list: a row, the list's name
 * and RESET with the dock on the right, then one row's keys and value and
 * SAVE with the dock on the left, the row whose value opens the keypad
 * where one shows; SENSORS adds its selected entry to the first, the
 * IMU's as the panel's own over Bluetooth LE.  The warning, the keyboard
 * and the keypad leave no page.  SETUP drives no output, so every page
 * shows while ARMED as well.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "settings.h"
#include "ui_help.h"
#include "ui_setting_keypad.h"

#include "settings_screen_int.h"

/** @brief The hub's slots of page 1 and of page 2, -1 past the last. */
static const int8_t k_hub_slots[2][4] = { { 0, 1, 3, 4 }, { 2, 5, 7, -1 } };

/** @brief The name of SENSORS' selected entry, the label of its mark. */
static char s_entry[40];

int stg_help_count(void)
{
    return (s.warn || s.tk.open || s.kp.open) ? 0 : 2;
}

/**
 * @brief What tile @p t opens, for the guide.
 * @param t The tile.
 * @return Its text; TX_COUNT for a tile the guide has no text for.
 */
static ui_text_id_t tile_text(const hub_tile_t *t)
{
    switch (t->cat) {
    case SET_CAT_APP:     return TX_HG_SET_PANEL;
    case SET_CAT_SENSORS: return TX_HG_SET_SENSORS;
    case SET_CAT_CARD:    return TX_HG_SET_CARD;
    default:              break;
    }
    switch (t->screen) {
    case SCREEN_OUTPUTS: return TX_HG_SET_OUTPUTS;
    case SCREEN_WIFI:    return TX_HG_SET_WIFI;
    case SCREEN_UPDATES: return TX_HG_SET_UPDATES;
    case SCREEN_IMU:     return TX_HG_SET_IMU;
    case SCREEN_LAYOUT:  return TX_HG_SET_LAYOUT;
    default:             return TX_COUNT;
    }
}

/**
 * @brief Mark the hub's tile in @p slot, when the hub shows one there.
 * @param p    The page.
 * @param slot The slot, -1 for none.
 */
static void add_tile(ui_help_page_t *p, int slot)
{
    if (slot < 0 || slot >= stg_tile_count()) {
        return;
    }
    const hub_tile_t *t = stg_hub_tile(slot);
    const char *name = (t->cat >= 0)
                           ? ui_setting_category((setting_cat_t)t->cat)
                           : ui_router_title(t->screen);
    (void)ui_help_add(p, name, tile_text(t), stg_tile_rect(slot));
}

/**
 * @brief Whether list row @p i has "-" and "+" keys: a setting shown and
 *        set here.
 * @param i The row.
 * @return True for a row with keys.
 */
static bool keyed(int i)
{
    const settings_row_t e = stg_row_entry(i);
    return e.text < 0 && settings_def(e.id) != NULL && !settings_derived(e.id)
           && !settings_fixed(e.id) && !settings_held(e.id);
}

/**
 * @brief A row whole in the list's pane: the first of all, or of the rows
 *        with keys the first whose value opens the keypad, else the first
 *        with keys; -1 for none.
 * @param keys Only a row with "-" and "+" keys.
 * @return The row.
 */
static int first_row(bool keys)
{
    const gfx_rect_t pane = settings_screen_list_rect();
    int with_keys = -1;
    for (int i = 0; i < stg_row_count(); ++i) {
        const gfx_rect_t r = stg_row_rect(i);
        if (r.y < pane.y) {
            continue;
        }
        if (r.y + r.h > pane.y + pane.h) {
            break;
        }
        if (!keys) {
            return i;
        }
        if (keyed(i)) {
            if (ui_setting_keypad_offered(stg_row_entry(i).id)) {
                return i;
            }
            with_keys = (with_keys < 0) ? i : with_keys;
        }
    }
    return with_keys;
}

/**
 * @brief The list's first page: SENSORS' entry, a row, the name, RESET.
 * @param p The page, its side set.
 */
static void list_rows(ui_help_page_t *p)
{
    const bool sensors = s.cat == SET_CAT_SENSORS;
    p->title = sensors ? TX_HG_SET_TITLE_SENSORS : TX_HG_SET_TITLE_ROWS;
    if (sensors) {
        const int e = sensors_pane_selected();
        /* The IMU is the panel's, over Bluetooth LE; the rest the IOmcu's. */
        (void)ui_help_add(p, sensors_pane_name(e, s_entry, sizeof(s_entry)),
                          (e == SENS_ENTRY_IMU) ? TX_HG_SET_ENTRY_IMU
                                                : TX_HG_SET_ENTRY,
                          settings_screen_entry_rect(e));
    }
    const int row = first_row(false);
    if (row >= 0) {
        (void)ui_help_add(p, TR(HG_SET_ROW_NAME), TX_HG_SET_ROW,
                          stg_row_rect(row));
    }
    (void)ui_help_add(p, ui_setting_category(s.cat), TX_HG_SET_HEAD,
                      stg_strip_rect(0));
    (void)ui_help_add(p, TR(HG_SET_RESET_NAME), TX_HG_SET_RESET,
                      stg_reset_rect());
}

/**
 * @brief The list's second page: one row's "-", value and "+", and SAVE.
 * @param p The page, its side set.
 */
static void list_values(ui_help_page_t *p)
{
    p->title = TX_HG_SET_TITLE_VALUES;
    const int row = first_row(true);
    if (row >= 0) {
        const setting_id_t id = stg_row_entry(row).id;
        const bool cycles = settings_cycles(id);
        (void)ui_help_add(p, cycles ? "<" : "-", TX_HG_SET_MINUS,
                          settings_screen_row_part(row, SETTINGS_ROW_MINUS));
        if (ui_setting_keypad_offered(id)) {
            (void)ui_help_add(p, TR(HG_SET_VALUE_NAME), TX_HG_SET_VALUE,
                              settings_screen_row_part(row,
                                                       SETTINGS_ROW_VALUE));
        }
        (void)ui_help_add(p, cycles ? ">" : "+", TX_HG_SET_PLUS,
                          settings_screen_row_part(row, SETTINGS_ROW_PLUS));
    }
    (void)ui_help_add(p, TR(SET_SAVE), TX_HG_SET_SAVE, stg_save_rect());
}

bool stg_help_page(int i, ui_help_page_t *out)
{
    if (out == NULL || i < 0 || i >= stg_help_count()) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->armed_ok = true;
    out->side = (i == 0) ? UI_HELP_DOCK_RIGHT : UI_HELP_DOCK_LEFT;
    if (s.list) {
        if (i == 0) {
            list_rows(out);
        } else {
            list_values(out);
        }
        return true;
    }
    out->title = (i == 0) ? TX_HG_SET_TITLE_HUB_L : TX_HG_SET_TITLE_HUB_R;
    for (int k = 0; k < 4; ++k) {
        add_tile(out, k_hub_slots[i][k]);
    }
    if (i == 0) {
        add_tile(out, 6);
    } else {
        (void)ui_help_add(out, TR(SET_SAVE), TX_HG_SET_SAVE, stg_save_rect());
    }
    return true;
}
