/**
 * @file settings_rows_int.h
 * @brief SETUP's list rows: what settings_rows.c, settings_screen.c and
 *        settings_draw.c share and nobody else sees.  A row is a setting or
 *        a string.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef SETTINGS_ROWS_INT_H
#define SETTINGS_ROWS_INT_H

#include <stdint.h>

#include "settings.h"
#include "ui_text.h"

/** @brief One row of a list: a setting or a string. */
typedef struct {
    setting_id_t id;                     /**< the setting; 0 for a string */
    int8_t       text;       /**< a setting_text_id_t, or -1              */
} settings_row_t;

/**
 * @brief Returns row @p index of @p cat's list; a setting of id 0 with text
 *        -1 past the last.
 * @param cat the category
 * @param index the row
 * @return the row
 */
settings_row_t settings_rows_entry(setting_cat_t cat, int index);

#endif /* SETTINGS_ROWS_INT_H */
