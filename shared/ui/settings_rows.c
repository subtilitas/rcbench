/**
 * @file settings_rows.c
 * @brief SETUP's list rows: a category's settings in schema order, a heading
 * above each section, and the category's strings after its settings.
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

/*
 * Section headings.  INTERFACES' 5 rows fall into two sections.  A heading
 * is a row of the list's own pitch, ROW_PITCH px, with no keys, drawn above the
 * setting that opens its section; it does not count as a setting.
 */
typedef struct {
    setting_id_t first;      /**< the section's first setting            */
    ui_text_id_t text;
} heading_t;

/** @brief INTERFACES' headings, each with the setting it stands above. */
static const heading_t k_headings[] = {
    { SET_PDMINI_EN, TX_SET_HD_SUPPLY },    /* the PD mini                  */
    { SET_HITEC_RX,  TX_SET_HD_PROG },      /* the programming port         */
};

#define N_HEADINGS ((int)(sizeof(k_headings) / sizeof(k_headings[0])))
/** @brief The most rows a list has: its settings, headings and strings. */
#define MAX_ENTRIES (MAX_ROWS + N_HEADINGS + SETTING_TEXT_COUNT)

/* The list of @p cat in row order, headings included, its strings after
 * its settings; the number of rows, counted past @p max. */
static int entries(setting_cat_t cat, settings_row_t *out, int max)
{
    setting_id_t ids[MAX_ROWS];
    const int n = (cat == SET_CAT_SENSORS)
                      ? sensors_pane_rows(sensors_pane_selected(), ids, MAX_ROWS)
                      : settings_in_category(cat, ids, MAX_ROWS);
    int rows = 0;
    for (int i = 0; i < n && i < MAX_ROWS; ++i) {
        for (int h = 0; h < N_HEADINGS; ++h) {
            if (k_headings[h].first == ids[i]) {
                if (out != NULL && rows < max) {
                    out[rows] = (settings_row_t){ ids[i], (int8_t)h, -1 };
                }
                ++rows;
            }
        }
        if (out != NULL && rows < max) {
            out[rows] = (settings_row_t){ ids[i], -1, -1 };
        }
        ++rows;
    }
    setting_text_id_t texts[SETTING_TEXT_COUNT];
    const int nt = settings_texts_in_category(cat, texts, SETTING_TEXT_COUNT);
    for (int i = 0; i < nt && i < SETTING_TEXT_COUNT; ++i) {
        if (out != NULL && rows < max) {
            out[rows] = (settings_row_t){ (setting_id_t)0, -1,
                                          (int8_t)texts[i] };
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
        if (e[r].heading < 0 && e[r].text < 0 && e[r].id == ids[index]) {
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
        return (settings_row_t){ (setting_id_t)0, -1, -1 };
    }
    return e[index];
}

ui_text_id_t settings_rows_heading_text(int heading)
{
    return (heading >= 0 && heading < N_HEADINGS) ? k_headings[heading].text
                                                  : TX_COUNT;
}

int settings_rows_listed(setting_cat_t cat)
{
    settings_row_t e[MAX_ENTRIES];
    const int n = entries(cat, e, MAX_ENTRIES);
    int listed = 0;
    for (int r = 0; r < n && r < MAX_ENTRIES; ++r) {
        listed += (e[r].heading < 0) ? 1 : 0;
    }
    return listed;
}

_Static_assert(MAX_ROWS == SET_INA_PARTS + SET_INA_CHANNELS * SET_INA_CH_ROWS + 18,
               "SENSORS' rows fit a list");
