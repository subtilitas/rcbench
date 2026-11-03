/**
 * @file sensors_pane.c
 * @brief SETUP > SENSORS' split pane.  See sensors_pane.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sensors_pane.h"

#include <stdio.h>

#include "sensors_rows.h"
#include "settings_screen.h"
#include "settings_sensors.h"
#include "ui_text.h"
#include "ui_theme.h"

/** @brief The entry the pane shows.  Outside SETUP's state, as settings_rows.c
 *         reads it; SETUP's reset selects the first entry again. */
static int s_selected = SENS_ENTRY_BUS;

/** @brief SENSOR BUS's rows. */
static const setting_id_t k_bus[] = { SET_SENSE_SDA, SET_SENSE_SCL };
/** @brief The INA228's rows, the enable row first. */
static const setting_id_t k_ina228[] = { SET_INA228_EN, SET_INA228_ADDR,
                                         SET_INA228_UOHM, SET_INA228_MAX_A };
/** @brief The AS5600's rows, the enable row first. */
static const setting_id_t k_as5600[] = { SET_ENC_EN, SET_ENC_CENTRE };
/** @brief The phase tap's rows, the enable row first. */
static const setting_id_t k_tap[] = { SET_TONE_EN, SET_TONE_PIN, SET_TONE_F_MIN,
                                      SET_TONE_F_MAX, SET_TONE_SPLIT,
                                      SET_TONE_GAP, SET_TONE_PERIODS };

/** @brief Appends @p table to all_rows()'s out, counted in its n. */
#define COPY_ROWS(table)                                             \
    do {                                                             \
        for (size_t k = 0u; k < sizeof(table) / sizeof((table)[0]); ++k) { \
            out[n++] = (table)[k];                                   \
        }                                                            \
    } while (0)

/**
 * @brief Every row of an entry, hidden or not.
 * @param entry The entry.
 * @param out   Takes the rows: room for SENS_ENTRY_ROWS_MAX.
 * @return The rows; 0 for an entry out of range.
 */
static int all_rows(int entry, setting_id_t *out)
{
    int n = 0;
    switch (entry) {
    case SENS_ENTRY_BUS:    COPY_ROWS(k_bus);    break;
    case SENS_ENTRY_INA228: COPY_ROWS(k_ina228); break;
    case SENS_ENTRY_AS5600: COPY_ROWS(k_as5600); break;
    case SENS_ENTRY_TAP:    COPY_ROWS(k_tap);    break;
    default: {
        const int a = entry - SENS_ENTRY_INA3221_0;
        if (a < 0 || a >= SET_INA_PARTS) {
            break;
        }
        out[n++] = SET_INA_FIT(a);
        for (int c = 3 * a; c < 3 * a + 3; ++c) {
            out[n++] = SET_INA_W(c);
            out[n++] = SET_INA_SH(c);
            out[n++] = SET_INA_LIM(c);
        }
        break;
    }
    }
    return n;
}

int sensors_pane_selected(void)
{
    return s_selected;
}

void sensors_pane_select(int entry)
{
    if (entry >= 0 && entry < SENS_ENTRY_COUNT) {
        s_selected = entry;
    }
}

int sensors_pane_entry_of(setting_id_t id)
{
    for (int e = 0; e < SENS_ENTRY_COUNT; ++e) {
        setting_id_t rows[SENS_ENTRY_ROWS_MAX];
        const int n = all_rows(e, rows);
        for (int i = 0; i < n; ++i) {
            if (rows[i] == id) {
                return e;
            }
        }
    }
    return -1;
}

int sensors_pane_rows(int entry, setting_id_t *out, int max)
{
    setting_id_t rows[SENS_ENTRY_ROWS_MAX];
    int n = all_rows(entry, rows);
    if (entry != SENS_ENTRY_BUS && n > 1 && !settings_get_bool(rows[0])) {
        n = 1;
    }
    for (int i = 0; out != NULL && i < n && i < max; ++i) {
        out[i] = rows[i];
    }
    return n;
}

const char *sensors_pane_name(int entry, char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    buf[0] = '\0';
    switch (entry) {
    case SENS_ENTRY_BUS:    snprintf(buf, n, "%s", TR(SENS_BUS));    break;
    case SENS_ENTRY_INA228: snprintf(buf, n, "INA228");              break;
    case SENS_ENTRY_AS5600: snprintf(buf, n, "AS5600");              break;
    case SENS_ENTRY_TAP:    snprintf(buf, n, "%s", TR(SET_HD_TAP));  break;
    default:
        if (entry >= SENS_ENTRY_INA3221_0
            && entry < SENS_ENTRY_INA3221_0 + SET_INA_PARTS) {
            (void)sensors_rows_title(entry - SENS_ENTRY_INA3221_0, buf, n);
        }
        break;
    }
    return buf;
}

/**
 * @brief The bus's state: amber without pins or with pins the IOmcu does not
 *        hold, dim for soldered pins, else the pins it has.
 * @param buf Takes the words.
 * @param n   The size of @p buf, bytes.
 * @param ink Takes their colour.
 */
static void bus_state(char *buf, size_t n, gfx_color_t *ink)
{
    const int sda = settings_get_int(SET_SENSE_SDA);
    const int scl = settings_get_int(SET_SENSE_SCL);
    if (sda < 0 || scl < 0) {
        snprintf(buf, n, "%s", TR(SENS_ST_NO_PINS));
        *ink = UI_WARN;
    } else if (settings_screen_unheld(SET_SENSE_SDA)) {
        snprintf(buf, n, "%s", TR(SENS_ST_REFUSED));
        *ink = UI_WARN;
    } else if (settings_fixed(SET_SENSE_SDA)) {
        snprintf(buf, n, TR(SENS_ST_SOLDERED_PINS), sda, scl);
        *ink = UI_TEXT_DIM;
    } else {
        snprintf(buf, n, "GPIO %d / %d", sda, scl);
        *ink = UI_OK;
    }
}

/**
 * @brief An on-or-off sensor's state while on: NOT ACCEPTED when the IOmcu
 *        does not hold one of its rows, else ON.
 * @param entry The entry, the AS5600 or the phase tap.
 * @param buf   Takes the words.
 * @param n     The size of @p buf, bytes.
 * @param ink   Takes their colour.
 */
static void on_state(int entry, char *buf, size_t n, gfx_color_t *ink)
{
    setting_id_t rows[SENS_ENTRY_ROWS_MAX];
    const int count = all_rows(entry, rows);
    for (int i = 0; i < count; ++i) {
        if (settings_screen_unheld(rows[i])) {
            snprintf(buf, n, "%s", TR(SENS_ST_REFUSED));
            *ink = UI_WARN;
            return;
        }
    }
    snprintf(buf, n, "%s", TR(ON));
    *ink = UI_OK;
}

const char *sensors_pane_state(int entry, char *buf, size_t n, gfx_color_t *ink)
{
    if (buf == NULL || n == 0u || ink == NULL) {
        return "";
    }
    buf[0] = '\0';
    *ink = UI_TEXT_DIM;
    setting_id_t rows[SENS_ENTRY_ROWS_MAX];
    if (all_rows(entry, rows) == 0) {
        return buf;
    }
    if (entry == SENS_ENTRY_BUS) {
        bus_state(buf, n, ink);
    } else if (!settings_get_bool(rows[0])) {
        snprintf(buf, n, "%s", TR(OFF));
    } else if (entry == SENS_ENTRY_INA228) {
        const sens_part_t st = sensors_rows_i228();
        /* The INA3221 it shares an address with, not the INA228 itself. */
        snprintf(buf, n, "%s", st == SENS_PART_SAME_ADDR
                                   ? TR(SENS_ST_SAME_I3221)
                                   : sensors_rows_word(st));
        *ink = sensors_rows_word_color(st);
    } else if (entry == SENS_ENTRY_AS5600 || entry == SENS_ENTRY_TAP) {
        on_state(entry, buf, n, ink);
    } else {
        const int a = entry - SENS_ENTRY_INA3221_0;
        snprintf(buf, n, "%s", sensors_rows_state(a));
        *ink = sensors_rows_state_color(a);
    }
    return buf;
}

void sensors_pane_reset(int entry)
{
    setting_id_t rows[SENS_ENTRY_ROWS_MAX];
    const int n = all_rows(entry, rows);
    settings_reset_rows(rows, n);
}
