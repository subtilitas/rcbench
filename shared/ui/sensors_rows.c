/**
 * @file sensors_rows.c
 * @brief SETUP > SENSORS' list.  See sensors_rows.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sensors_rows.h"

#include <stdio.h>

#include "link_pages.h"
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

/** @brief The INA228's state as the control task last said; see s_status. */
static sens_part_t s_i228;

void sensors_rows_set_i228(sens_part_t st)
{
    if (st != s_i228) {
        s_i228 = st;
        settings_screen_invalidate();
    }
}

sens_part_t sensors_rows_i228(void)
{
    return s_i228;
}

/** @brief The limits' marks as the control task last said: bits 0 to 11
 *         the channels, 12 to 15 sense_limit_st_t. */
static uint16_t s_limits;

void sensors_rows_set_limits(uint16_t word)
{
    if (word != s_limits) {
        s_limits = word;
        settings_screen_invalidate();
    }
}

/**
 * @brief Whether a row is a LIMIT row.
 * @param id the row's setting
 * @param c  its channel, 0 to 11, for a SENSORS channel row
 * @return true for a LIMIT row
 */
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

const char *sensors_rows_title(int a, char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    snprintf(buf, n, TR(SET_HD_INA_PART),
             (unsigned)(LINK_SN_I3221_ADDR_MIN + (unsigned)a));
    return buf;
}

const char *sensors_rows_word(sens_part_t st)
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
    return ui_tr(((unsigned)st < (unsigned)SENS_PART_COUNT)
                     ? k_words[st] : TX_SENS_ST_UNKNOWN);
}

const char *sensors_rows_state(int a)
{
    return sensors_rows_word(sensors_rows_status(a));
}

gfx_color_t sensors_rows_state_color(int a)
{
    return sensors_rows_word_color(sensors_rows_status(a));
}

gfx_color_t sensors_rows_word_color(sens_part_t st)
{
    switch (st) {
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

const char *sensors_rows_alert(char *buf, size_t n, sense_limit_link_ev_t ev,
                               unsigned ch, uint16_t ma, int watch)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    buf[0] = '\0';
    switch (ev) {
    case SENSE_LIMIT_LINK_EV_OVER: {
        if (ch >= (unsigned)SET_INA_CHANNELS) {
            break;
        }
        char target[24] = "";
        if (watch > SENS_W_NONE && watch < SENS_W_COUNT) {
            snprintf(target, sizeof(target), " (%s)",
                     ui_setting_option(SET_INA_W(ch), watch));
        }
        const unsigned ca = ((unsigned)ma + 5u) / 10u;
        snprintf(buf, n, TR(ALERT_SENSE_OVER),
                 (unsigned)(LINK_SN_I3221_ADDR_MIN + ch / 3u), ch % 3u + 1u,
                 target, ca / 100u, ca % 100u);
        break;
    }
    case SENSE_LIMIT_LINK_EV_LOST:
        snprintf(buf, n, "%s", TR(ALERT_SENSE_OVER_LOST));
        break;
    case SENSE_LIMIT_LINK_EV_REFUSED:
        snprintf(buf, n, "%s", TR(ALERT_SENSE_LIMITS));
        break;
    default:
        break;
    }
    return buf;
}

/**
 * @brief @p ma as amperes to 0.001A, with its sign: "0.153A", "-0.005A".
 * @param buf where it goes.
 * @param n   the size of @p buf, bytes.
 * @param ma  the current, mA.
 * @return @p buf.
 */
static const char *amps(char *buf, size_t n, long ma)
{
    const unsigned long m = (ma < 0) ? (unsigned long)(-ma)
                                     : (unsigned long)ma;
    snprintf(buf, n, "%s%lu.%03luA", (ma < 0) ? "-" : "", m / 1000ul,
             m % 1000ul);
    return buf;
}

const char *sensors_rows_trip_line(char *buf, size_t n, unsigned ch,
                                   int watch, bool have, int16_t peak_ma,
                                   uint16_t limit_ma)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    buf[0] = '\0';
    if (ch >= (unsigned)SET_INA_CHANNELS) {
        return buf;
    }
    /* The settings' own option names: the console is English. */
    const setting_def_t *d = settings_def(SET_INA_W(ch));
    char target[24] = "";
    if (watch > SENS_W_NONE && watch < SENS_W_COUNT && d != NULL
        && d->options != NULL && watch < d->option_count) {
        snprintf(target, sizeof(target), " (%s)", d->options[watch]);
    }
    char peak[40] = "no window read";
    if (have) {
        char a[16];
        snprintf(peak, sizeof(peak), "highest window read %s",
                 amps(a, sizeof(a), peak_ma));
    }
    char limit[16];
    snprintf(buf, n, "INA3221 0x%02X CH%u%s over its limit of %s: %s",
             (unsigned)(LINK_SN_I3221_ADDR_MIN + ch / 3u), ch % 3u + 1u,
             target, amps(limit, sizeof(limit), limit_ma), peak);
    return buf;
}

/**
 * @brief The short name of a board's rail a channel watches, in the
 *        language showing.
 * @param code a SENSE_MAP WATCH value.
 * @return "RAIL A", "RAIL B" or "PROG 5V" for LINK_SM_WATCH_RAIL_A to
 *         LINK_SM_WATCH_PROG_5V; NULL for any other value.
 */
static const char *rail_name(uint16_t code)
{
    switch (code) {
    case LINK_SM_WATCH_RAIL_A:  return TR(SENS_RAIL_A);
    case LINK_SM_WATCH_RAIL_B:  return TR(SENS_RAIL_B);
    case LINK_SM_WATCH_PROG_5V: return TR(SENS_PROG_5V);
    default:                    return NULL;
    }
}

const char *sensors_rows_channel(int c, char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    buf[0] = '\0';
    const char *rail = (c >= 0 && c < SET_INA_CHANNELS)
        ? rail_name(settings_sensors_watch_code(settings_get_int(SET_INA_W(c))))
        : NULL;
    if (rail != NULL) {
        /* A board's rail, by its name: the board fixes the channel. */
        snprintf(buf, n, "%s", rail);
    } else if (c >= 0 && c < SET_INA_CHANNELS) {
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
    char ch[16];
    (void)sensors_rows_channel((int)settings_sensors_fast(), ch, sizeof(ch));
    snprintf(buf, n, (ch[0] != '\0') ? "INA3221 %s" : "INA3221", ch);
    return buf;
}

const char *sensors_rows_short(char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    char ch[16];
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

const char *sensors_rows_port_watch(unsigned port, char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    buf[0] = '\0';
    if (port >= LINK_PG_PORTS_MAX) {
        return buf;
    }
    int c = sensors_rows_watcher((uint16_t)LINK_SM_WATCH_PORT(port));
    if (c < 0) {
        /* The ports' own 5V, on a board that solders its INA3221 there,
         * feeds every port. */
        c = sensors_rows_watcher((uint16_t)LINK_SM_WATCH_PROG_5V);
    }
    if (c >= 0) {
        char name[16];
        snprintf(buf, n, "INA3221 %s",
                 sensors_rows_channel(c, name, sizeof(name)));
    }
    return buf;
}

void sensors_rows_publish_watch(void)
{
    for (unsigned port = 0u; port < LINK_PG_PORTS_MAX; ++port) {
        char label[24];
        outputs_screen_set_port_watch(
            (uint8_t)port, sensors_rows_port_watch(port, label, sizeof(label)));
    }
}
