/*
 * SETUP's list rows: what settings_rows.c and settings_screen.c share and
 * nobody else sees.  A row is a setting, a heading above the next one, a
 * part's heading on SENSORS (sensors_rows.h), or a string.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef SETTINGS_ROWS_INT_H
#define SETTINGS_ROWS_INT_H

#include <stdint.h>

#include "settings.h"
#include "ui_text.h"

/* One row of a list. */
typedef struct {
    setting_id_t id;
    int8_t       heading;    /**< a section heading's index, or -1        */
    int8_t       text;       /**< a setting_text_id_t, or -1              */
    int8_t       part;       /**< an INA3221 part's heading, 0 to 3, or -1 */
} settings_row_t;

/* Row @p index of @p cat's list; a setting of id 0 with every index -1
 * past the last. */
settings_row_t settings_rows_entry(setting_cat_t cat, int index);

/* The words of section heading @p heading. */
ui_text_id_t settings_rows_heading_text(int heading);

/* The settings and strings @p cat lists: its rows less its headings. */
int settings_rows_listed(setting_cat_t cat);

#endif /* SETTINGS_ROWS_INT_H */
