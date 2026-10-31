/**
 * @file sensors_pane.h
 * @brief SETUP > SENSORS' split pane: the 8 entries of the left list (SENSOR
 *        BUS, INA228, INA3221 0x40 to 0x43, AS5600, PHASE TAP), the rows each
 *        one shows, its state word, and its reset.  SETUP's own list renderer
 *        draws the rows (settings_rows.c, settings_draw.c).
 *
 * An entry with an enable row shows that row alone while it is off and the
 * sensor's other rows under it while it is on.  The bus has no enable row and
 * shows its 2 pin rows always.  Vibration and the optical tacho have keys in
 * this category and no entry: no firmware reads them.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>

#include "gfx.h"
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The left list's entries, top to bottom. */
enum {
    SENS_ENTRY_BUS = 0,      /**< SDA and SCL                              */
    SENS_ENTRY_INA228,       /**< enable, address, shunt, range            */
    SENS_ENTRY_INA3221_0,    /**< FITTED and 3 channels x 3 rows, 0x40     */
    SENS_ENTRY_AS5600 = SENS_ENTRY_INA3221_0 + SET_INA_PARTS, /**< enable, centre */
    SENS_ENTRY_TAP,          /**< enable, pin, 2 tones, split, gap, periods */
    SENS_ENTRY_COUNT
};

/** @brief The most rows an entry has: an INA3221 part's FITTED and 9. */
#define SENS_ENTRY_ROWS_MAX (1 + 3 * SET_INA_CH_ROWS)

/**
 * @brief The entry the pane shows.
 * @return The entry, 0 to SENS_ENTRY_COUNT - 1.
 */
int sensors_pane_selected(void);

/**
 * @brief Selects the entry the pane shows; one out of range changes nothing.
 * @param entry The entry.
 */
void sensors_pane_select(int entry);

/**
 * @brief The entry that holds a setting.
 * @param id The setting.
 * @return Its entry; -1 for a setting no entry holds.
 */
int sensors_pane_entry_of(setting_id_t id);

/**
 * @brief The rows entry @p entry shows now, in order.
 * @param entry The entry.
 * @param out   Takes the settings; NULL only counts.
 * @param max   The room at @p out.
 * @return The rows shown, 0 for an entry out of range.
 */
int sensors_pane_rows(int entry, setting_id_t *out, int max);

/**
 * @brief An entry's name in the language showing.
 * @param entry The entry.
 * @param buf   Takes the name.
 * @param n     The size of @p buf, bytes.
 * @return @p buf; "" for an entry out of range or no buffer.
 */
const char *sensors_pane_name(int entry, char *buf, size_t n);

/**
 * @brief What an entry's sensor is doing, the line under its name.
 *
 * The bus: its pins, or NO PINS SET.  An enabled INA228 or INA3221: the word
 * of its sens_part_t.  An enabled AS5600 or phase tap: ON.  A row the IOmcu
 * does not hold makes the bus, the AS5600 and the tap NOT ACCEPTED.
 *
 * @param entry The entry.
 * @param buf   Takes the words.
 * @param n     The size of @p buf, bytes.
 * @param ink   Takes their colour.
 * @return @p buf; "" for an entry out of range or no buffer.
 */
const char *sensors_pane_state(int entry, char *buf, size_t n, gfx_color_t *ink);

/**
 * @brief Every row of @p entry back to its default, the rows hidden by its
 *        enable row included, through settings_reset_rows().
 * @param entry The entry.
 */
void sensors_pane_reset(int entry);

#ifdef __cplusplus
}
#endif
