/*
 * SETUP > SENSORS' list.  See sensors_rows.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sensors_rows.h"

#include <stdio.h>

#include "settings_screen.h"
#include "settings_sensors.h"
#include "ui_text.h"
#include "ui_theme.h"

/* The parts' states as the control task last said.  Outside any screen's
 * state: what the IOmcu reads does not change when SETUP is reset. */
static uint16_t s_status;

void sensors_rows_set_status(uint16_t word)
{
    if (word != s_status) {
        s_status = word;
        settings_screen_invalidate();
    }
}

sens_part_t sensors_rows_status(int a)
{
    if (a < 0 || a >= SET_INA_PARTS) {
        return SENS_PART_UNKNOWN;
    }
    return SENS_PART_OF(s_status, (unsigned)a);
}

static bool fitted(int a)
{
    return a >= 0 && a < SET_INA_PARTS && settings_get_bool(SET_INA_FIT(a));
}

bool sensors_rows_listed(setting_id_t id)
{
    if (!settings_sensors_row(id) || id <= SET_INA_FIT_LAST) {
        return true;
    }
    return fitted(settings_sensors_part(id));
}

int sensors_rows_heading(setting_id_t id)
{
    const int a = settings_sensors_part(id);
    return (a >= 0 && id == SET_INA_W(3 * a) && fitted(a)) ? a : -1;
}

const char *sensors_rows_title(int a, char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    snprintf(buf, n, TR(SET_HD_INA_PART),
             (unsigned)(LINK_SN_I3221_ADDR_MIN + (unsigned)a));
    return buf;
}

const char *sensors_rows_state(int a)
{
    static const ui_text_id_t k_words[SENS_PART_COUNT] = {
        [SENS_PART_UNKNOWN]   = TX_SENS_ST_UNKNOWN,
        [SENS_PART_PENDING]   = TX_SENS_ST_PENDING,
        [SENS_PART_SEARCHING] = TX_SENS_ST_SEARCHING,
        [SENS_PART_ONLINE]    = TX_SENS_ST_ONLINE,
        [SENS_PART_SILENT]    = TX_SENS_ST_SILENT,
        [SENS_PART_WRONG]     = TX_SENS_ST_WRONG,
        [SENS_PART_REFUSED]   = TX_SENS_ST_REFUSED,
        [SENS_PART_DARK]      = TX_SENS_ST_DARK,
        [SENS_PART_SAME_ADDR] = TX_SENS_ST_SAME_ADDR,
        [SENS_PART_OLD]       = TX_SENS_ST_OLD,
        [SENS_PART_NO_BUS]    = TX_SENS_ST_NO_BUS,
    };
    const sens_part_t st = sensors_rows_status(a);
    return ui_tr(((unsigned)st < (unsigned)SENS_PART_COUNT)
                     ? k_words[st] : TX_SENS_ST_UNKNOWN);
}

gfx_color_t sensors_rows_state_color(int a)
{
    switch (sensors_rows_status(a)) {
    case SENS_PART_ONLINE:
        return UI_OK;
    case SENS_PART_UNKNOWN:
    case SENS_PART_PENDING:
    case SENS_PART_SEARCHING:
        return UI_TEXT_DIM;
    default:
        return UI_WARN;
    }
}

bool sensors_rows_unheld(setting_id_t id)
{
    if (!settings_sensors_row(id) || id <= SET_INA_FIT_LAST) {
        return false;
    }
    switch (sensors_rows_status(settings_sensors_part(id))) {
    case SENS_PART_REFUSED:
    case SENS_PART_DARK:
    case SENS_PART_SAME_ADDR:
    case SENS_PART_OLD:
    case SENS_PART_NO_BUS:
        return true;
    default:
        return false;
    }
}

const char *sensors_rows_why(setting_id_t id)
{
    switch (sensors_rows_status(settings_sensors_part(id))) {
    case SENS_PART_OLD:       return TR(SET_SENS_OLD);
    case SENS_PART_DARK:      return TR(SET_SENS_DARK);
    case SENS_PART_SAME_ADDR: return TR(SET_SENS_SAME_ADDR);
    case SENS_PART_NO_BUS:    return TR(SET_SENS_NO_BUS);
    default:                  return TR(SET_UNHELD);
    }
}
