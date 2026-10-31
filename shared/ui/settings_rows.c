/**
 * @file settings_rows.c
 * @brief SETUP's list rows: a category's settings in schema order and the
 * category's strings after its settings.
 * SENSORS lists the rows of the left list's selected entry and nothing else
 * (sensors_pane.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings_rows_int.h"

#include "sensors_pane.h"
#include "settings_screen.h"
#include "settings_sensors.h"

/** @brief The settings a category holds, at most: SENSORS' 58, the 40 of
 *         the INA3221 parts and 18 others. */
#define MAX_ROWS 58

/** @brief The most rows a list has: its settings and strings. */
#define MAX_ENTRIES (MAX_ROWS + SETTING_TEXT_COUNT)

/* The list of @p cat in row order, its strings after its settings; the
 * number of rows, counted past @p max. */
static int entries(setting_cat_t cat, settings_row_t *out, int max)
{
    setting_id_t ids[MAX_ROWS];
    const int n = (cat == SET_CAT_SENSORS)
                      ? sensors_pane_rows(sensors_pane_selected(), ids, MAX_ROWS)
                      : settings_in_category(cat, ids, MAX_ROWS);
    int rows = 0;
    for (int i = 0; i < n && i < MAX_ROWS; ++i) {
        if (out != NULL && rows < max) {
            out[rows] = (settings_row_t){ ids[i], -1 };
        }
        ++rows;
    }
    setting_text_id_t texts[SETTING_TEXT_COUNT];
    const int nt = settings_texts_in_category(cat, texts, SETTING_TEXT_COUNT);
    for (int i = 0; i < nt && i < SETTING_TEXT_COUNT; ++i) {
        if (out != NULL && rows < max) {
            out[rows] = (settings_row_t){ (setting_id_t)0, (int8_t)texts[i] };
        }
        ++rows;
    }
    return rows;
}

int settings_screen_row_of(setting_cat_t cat, int index)
{
    setting_id_t ids[MAX_ROWS];
    const int ni = settings_in_category(cat, ids, MAX_ROWS);
    if (index < 0 || index >= ni || index >= MAX_ROWS) {
        return -1;
    }
    settings_row_t e[MAX_ENTRIES];
    const int n = entries(cat, e, MAX_ENTRIES);
    for (int r = 0; r < n && r < MAX_ENTRIES; ++r) {
        if (e[r].text < 0 && e[r].id == ids[index]) {
            return r;
        }
    }
    return -1;                      /* not listed: not the selected entry */
}

int settings_screen_text_row(setting_cat_t cat, setting_text_id_t id)
{
    settings_row_t e[MAX_ENTRIES];
    const int n = entries(cat, e, MAX_ENTRIES);
    for (int r = 0; r < n && r < MAX_ENTRIES; ++r) {
        if (e[r].text >= 0 && e[r].text == (int8_t)id) {
            return r;
        }
    }
    return -1;
}

int settings_screen_rows(setting_cat_t cat)
{
    return entries(cat, NULL, 0);
}

settings_row_t settings_rows_entry(setting_cat_t cat, int index)
{
    settings_row_t e[MAX_ENTRIES];
    const int n = entries(cat, e, MAX_ENTRIES);
    if (index < 0 || index >= n || index >= MAX_ENTRIES) {
        return (settings_row_t){ (setting_id_t)0, -1 };
    }
    return e[index];
}

_Static_assert(MAX_ROWS == SET_INA_PARTS + SET_INA_CHANNELS * SET_INA_CH_ROWS + 18,
               "SENSORS' rows fit a list");
