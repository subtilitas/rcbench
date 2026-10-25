/**
 * @file settings_rows_int.h
 * @brief SETUP's list rows: what settings_rows.c, settings_screen.c and
 *        settings_draw.c share and nobody else sees.  A row is a setting, a
 *        heading above the next one, a part's heading on SENSORS
 *        (sensors_rows.h), or a string.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef SETTINGS_ROWS_INT_H
#define SETTINGS_ROWS_INT_H

#include <stdint.h>

#include "settings.h"
#include "ui_text.h"

/** @brief One row of a list: a setting, a heading, a string or a part's heading. */
typedef struct {
    setting_id_t id;                     /**< the setting; 0 for a heading or a string */
    int8_t       heading;    /**< a section heading's index, or -1        */
    int8_t       text;       /**< a setting_text_id_t, or -1              */
    int8_t       part;       /**< an INA3221 part's heading, 0 to 3, or -1 */
} settings_row_t;

/**
 * @brief Returns row @p index of @p cat's list; a setting of id 0 with every
 *        index -1 past the last.
 * @param cat the category
 * @param index the row
 * @return the row
 */
settings_row_t settings_rows_entry(setting_cat_t cat, int index);

/**
 * @brief Returns the words of section heading @p heading.
 * @param heading the heading's index
 * @return the text id
 */
ui_text_id_t settings_rows_heading_text(int heading);

/**
 * @brief Counts the settings and strings @p cat lists: its rows less its
 *        headings.
 * @param cat the category
 * @return the count
 */
int settings_rows_listed(setting_cat_t cat);

#endif /* SETTINGS_ROWS_INT_H */
