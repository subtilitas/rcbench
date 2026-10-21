/*
 * SETUP > SENSORS' list.  See sensors_rows.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sensors_rows.h"

#include <stdio.h>

#include "outputs_screen.h"
#include "sense_limit_link.h"
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

/* The limits' marks as the control task last said: bits 0 to 11 the
 * channels, 12 to 15 sense_limit_st_t. */
static uint16_t s_limits;

void sensors_rows_set_limits(uint16_t word)
{
    if (word != s_limits) {
        s_limits = word;
        settings_screen_invalidate();
    }
}

/* Whether @p id is a LIMIT row, and its channel in @p c. */
static bool limit_row(setting_id_t id, int *c)
{
    if (id < SET_INA_CH_0 || id > SET_INA_CH_LAST) {
        return false;
    }
    const int k = (int)id - (int)SET_INA_CH_0;
    *c = k / SET_INA_CH_ROWS;
    return k % SET_INA_CH_ROWS == SET_INA_CH_ROWS - 1;
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
        break;
    }
    int c = 0;
    return limit_row(id, &c) && (s_limits & (1u << c)) != 0u;
}

const char *sensors_rows_why(setting_id_t id)
{
    switch (sensors_rows_status(settings_sensors_part(id))) {
    case SENS_PART_OLD:       return TR(SET_SENS_OLD);
    case SENS_PART_DARK:      return TR(SET_SENS_DARK);
    case SENS_PART_SAME_ADDR: return TR(SET_SENS_SAME_ADDR);
    case SENS_PART_NO_BUS:    return TR(SET_SENS_NO_BUS);
    default:                  break;
    }
    int c = 0;
    if (limit_row(id, &c) && (s_limits >> 12) == SENSE_LIMIT_ST_OLD) {
        return TR(SET_SENS_LIM_OLD);
    }
    return TR(SET_UNHELD);
}

const char *sensors_rows_channel(int c, char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    buf[0] = '\0';
    if (c >= 0 && c < SET_INA_CHANNELS) {
        snprintf(buf, n, "0x%02X CH%u",
                 (unsigned)(LINK_SN_I3221_ADDR_MIN + (unsigned)c / 3u),
                 (unsigned)c % 3u + 1u);
    }
    return buf;
}

const char *sensors_rows_meter(char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    char ch[12];
    (void)sensors_rows_channel((int)settings_sensors_fast(), ch, sizeof(ch));
    snprintf(buf, n, (ch[0] != '\0') ? "INA3221 %s" : "INA3221", ch);
    return buf;
}

const char *sensors_rows_short(char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    char ch[12];
    (void)sensors_rows_channel((int)settings_sensors_fast(), ch, sizeof(ch));
    snprintf(buf, n, (ch[0] != '\0') ? "INA %s" : "INA3221", ch);
    return buf;
}

int sensors_rows_watcher(uint16_t code)
{
    for (int c = 0; c < SET_INA_CHANNELS; ++c) {
        if (fitted(c / 3) && settings_sensors_dmohm(c) != 0u
            && settings_sensors_watch_code(settings_get_int(SET_INA_W(c)))
                   == code) {
            return c;
        }
    }
    return -1;
}

void sensors_rows_publish_watch(void)
{
    for (unsigned port = 0u; port < LINK_PG_PORTS_MAX; ++port) {
        const int c = sensors_rows_watcher(
            (uint16_t)LINK_SM_WATCH_PORT(port));
        char name[12];
        char label[24];
        label[0] = '\0';
        if (c >= 0) {
            snprintf(label, sizeof(label), "INA3221 %s",
                     sensors_rows_channel(c, name, sizeof(name)));
        }
        outputs_screen_set_port_watch((uint8_t)port, label);
    }
}
