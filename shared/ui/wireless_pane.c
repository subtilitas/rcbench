/**
 * @file wireless_pane.c
 * @brief SETUP > WIRELESS' split pane.  See wireless_pane.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wireless_pane.h"

#include <stdio.h>
#include <string.h>

#include "sensors_rows.h"
#include "wireless_bal.h"
#include "settings_screen.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"

/** @brief The entry the pane shows; SETUP's reset selects the first. */
static int s_selected = WL_ENTRY_IMU;
/** @brief The Dial as the panel last read it. */
static dial_view_t s_dial;
/** @brief The commands the queue holds at most. */
#define CMD_Q 4u
/** @brief The rows' commands, waiting for the panel, oldest first. */
static dial_cmd_t s_q[CMD_Q];
/** @brief The commands in s_q. */
static unsigned   s_q_n;

/** @brief The IMU's rows, the enable row first and ANGLE SOURCE second:
 *         both stand while the IMU is off, so the source can go back to
 *         the AS5600 without switching the IMU on. */
static const setting_id_t k_imu[] = { SET_IMU_EN, SET_ANGLE_SOURCE,
                                      SET_IMU_AXIS, SET_IMU_SIGN,
                                      SET_IMU_RATE, SET_IMU_ALGO,
                                      SET_IMU_FILTER };
/** @brief The Dial's settings, the enable row first. */
static const setting_id_t k_dial[] = { SET_DIAL_EN, SET_DIAL_THR,
                                       SET_DIAL_SRV };

_Static_assert(sizeof(k_imu) / sizeof(k_imu[0]) <= WL_ENTRY_ROWS_MAX,
               "the IMU's rows fit an entry");

/**
 * @brief Queue a command; a full queue drops it.
 * @param kind The command.  @param arg Its argument.
 */
static void push(uint8_t kind, uint8_t arg)
{
    if (s_q_n < CMD_Q) {
        s_q[s_q_n].kind = kind;
        s_q[s_q_n].arg = arg;
        ++s_q_n;
    }
}

bool wireless_pane_take_dial(dial_cmd_t *out)
{
    if (s_q_n == 0u || out == NULL) {
        return false;
    }
    *out = s_q[0];
    --s_q_n;
    memmove(&s_q[0], &s_q[1], s_q_n * sizeof(s_q[0]));
    return true;
}

/* ------------------------------------------------- the DIAL entry's rows */

/** @brief SCAN FOR DIAL's value: what the last scan did. @param out The
 *         text. @param n Its size. */
static void scan_value(char *out, size_t n)
{
    if (s_dial.link == DIAL_LINK_SCANNING) {
        snprintf(out, n, "%s", TR(DIAL_SCAN_RUNS));
    } else if (!s_dial.scan_done) {
        snprintf(out, n, "%s", TR(DIAL_SCAN_GO));
    } else if (s_dial.scan_cut) {
        snprintf(out, n, "%s", TR(DIAL_SCAN_CUT));
    } else if (s_dial.found_n == 0u) {
        snprintf(out, n, "%s", TR(DIAL_SCAN_NONE));
    } else {
        snprintf(out, n, TR(DIAL_SCAN_FOUND), (unsigned)s_dial.found_n);
    }
}

/** @brief SCAN FOR DIAL tapped: refused while ARMED, else asked. */
static void scan_tap(void)
{
    if (ui_router_status()->armed) {
        ui_router_set_alert(TR(DIAL_SCAN_ARMED));
        return;
    }
    push(DIAL_CMD_SCAN, 0u);
}

/** @brief SCAN FOR DIAL: the last scan's result, a tap scans. */
const ui_setlist_custom_t wireless_pane_scan_row = {
    .label = TX_DIAL_ROW_SCAN,
    .help = TX_DIAL_ROW_SCAN_HELP,
    .value_text = scan_value,
    .tap = scan_tap,
};

/** @brief BOUND DIAL's value: the bound Dial's name, its address, or NONE.
 *  @param out The text. @param n Its size. */
static void bind_value(char *out, size_t n)
{
    if (s_dial.bound_name[0] != '\0') {
        snprintf(out, n, "%s", s_dial.bound_name);
    } else if (s_dial.bound_addr[0] != '\0') {
        snprintf(out, n, "%s", s_dial.bound_addr);
    } else {
        snprintf(out, n, "%s", TR(DIAL_BIND_NONE));
    }
}

/** @brief The found list's rows. @return The count. */
static int bind_choices(void)
{
    return s_dial.scan_done ? (int)s_dial.found_n : 0;
}

/**
 * @brief A found Dial's row: its name or address and its RSSI.
 * @param i The row.
 * @return The text, valid until the next call.
 */
static const char *bind_choice(int i)
{
    static char line[48];
    if (i < 0 || i >= (int)s_dial.found_n) {
        return "";
    }
    const dial_dev_t *d = &s_dial.found[i];
    snprintf(line, sizeof(line), TR(DIAL_FOUND_ROW),
             d->name[0] != '\0' ? d->name : d->addr, (int)d->rssi);
    return line;
}

/** @brief The found Dial that is bound. @return Its row, -1 none. */
static int bind_chosen(void)
{
    for (int i = 0; i < (int)s_dial.found_n; ++i) {
        if (strcmp(s_dial.found[i].addr, s_dial.bound_addr) == 0) {
            return i;
        }
    }
    return -1;
}

/** @brief A found Dial tapped: bound. @param i Its row. */
static void bind_pick(int i)
{
    if (i >= 0 && i < (int)s_dial.found_n) {
        push(DIAL_CMD_BIND, (uint8_t)i);
    }
}

/** @brief BOUND DIAL: the bound Dial, the last scan's list, a pick binds. */
const ui_setlist_custom_t wireless_pane_bind_row = {
    .label = TX_DIAL_ROW_BIND,
    .help = TX_DIAL_ROW_BIND_HELP,
    .value_text = bind_value,
    .choices = bind_choices,
    .choice = bind_choice,
    .chosen = bind_chosen,
    .pick = bind_pick,
};

/* ------------------------------------------------------------- the pane */

int wireless_pane_selected(void)
{
    return s_selected;
}

void wireless_pane_select(int entry)
{
    if (entry >= 0 && entry < WL_ENTRY_COUNT) {
        s_selected = entry;
    }
}

int wireless_pane_entry_of(setting_id_t id)
{
    for (size_t i = 0; i < sizeof(k_imu) / sizeof(k_imu[0]); ++i) {
        if (k_imu[i] == id) {
            return WL_ENTRY_IMU;
        }
    }
    for (size_t i = 0; i < sizeof(k_dial) / sizeof(k_dial[0]); ++i) {
        if (k_dial[i] == id) {
            return WL_ENTRY_DIAL;
        }
    }
    if (wireless_bal_owns(id)) {
        return WL_ENTRY_BAL;
    }
    return -1;
}

/**
 * @brief Append a setting row.
 * @param out The rows, NULL to count.  @param max Their room.
 * @param n The count so far.  @param id The setting.
 * @return The count after it.
 */
static int add_id(wl_row_t *out, int max, int n, setting_id_t id)
{
    if (out != NULL && n < max) {
        out[n].id = id;
        out[n].custom = NULL;
    }
    return n + 1;
}

/**
 * @brief Append a custom row.
 * @param out The rows, NULL to count.  @param max Their room.
 * @param n The count so far.  @param c The row.
 * @return The count after it.
 */
static int add_custom(wl_row_t *out, int max, int n,
                      const ui_setlist_custom_t *c)
{
    if (out != NULL && n < max) {
        out[n].id = (setting_id_t)0;
        out[n].custom = c;
    }
    return n + 1;
}

int wireless_pane_rows(int entry, wl_row_t *out, int max)
{
    int n = 0;
    if (entry == WL_ENTRY_IMU) {
        /* Off: IMU and ANGLE SOURCE. */
        const int shown = settings_get_bool(SET_IMU_EN)
                              ? (int)(sizeof(k_imu) / sizeof(k_imu[0])) : 2;
        for (int i = 0; i < shown; ++i) {
            n = add_id(out, max, n, k_imu[i]);
        }
    } else if (entry == WL_ENTRY_DIAL) {
        n = add_id(out, max, n, SET_DIAL_EN);
        if (settings_get_bool(SET_DIAL_EN)) {
            n = add_custom(out, max, n, &wireless_pane_scan_row);
            n = add_custom(out, max, n, &wireless_pane_bind_row);
            n = add_id(out, max, n, SET_DIAL_THR);
            n = add_id(out, max, n, SET_DIAL_SRV);
        }
    } else if (entry == WL_ENTRY_BAL) {
        n = wireless_bal_rows(out, max);
    }
    return n;
}

const char *wireless_pane_name(int entry, char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    buf[0] = '\0';
    if (entry == WL_ENTRY_IMU) {
        snprintf(buf, n, "IMU");
    } else if (entry == WL_ENTRY_DIAL) {
        snprintf(buf, n, "%s", TR(DIAL_ENTRY));
    } else if (entry == WL_ENTRY_BAL) {
        snprintf(buf, n, "%s", TR(BNODE_ENTRY));
    }
    return buf;
}

const char *wireless_pane_dial_word(const dial_view_t *v, char *buf,
                                    size_t n, gfx_color_t *ink)
{
    gfx_color_t col = UI_TEXT_DIM;
    if (buf == NULL || n == 0u) {
        return "";
    }
    const uint8_t link = v != NULL ? v->link : (uint8_t)DIAL_LINK_OFF;
    switch (link) {
    case DIAL_LINK_STARTING:
        snprintf(buf, n, "%s", TR(IMU_ST_STARTING));
        break;
    case DIAL_LINK_NO_BT:
        snprintf(buf, n, "%s", TR(IMU_ST_NO_BT));
        col = UI_WARN;
        break;
    case DIAL_LINK_C6_FAILED:
        snprintf(buf, n, "%s", TR(IMU_ST_C6_FAILED));
        col = UI_WARN;
        break;
    case DIAL_LINK_NONE:
        snprintf(buf, n, "%s", TR(DIAL_ST_NONE));
        break;
    case DIAL_LINK_SCANNING:
        snprintf(buf, n, "%s", TR(IMU_ST_SCANNING));
        break;
    case DIAL_LINK_CONNECTING:
        snprintf(buf, n, "%s", TR(IMU_ST_CONNECTING));
        break;
    case DIAL_LINK_NOT_DIAL:
        snprintf(buf, n, "%s", TR(DIAL_ST_NOT_DIAL));
        col = UI_WARN;
        break;
    case DIAL_LINK_WAITING:
        snprintf(buf, n, "%s", TR(DIAL_ST_WAITING));
        break;
    case DIAL_LINK_PROTO:
        snprintf(buf, n, TR(DIAL_ST_PROTO), (unsigned)v->proto);
        col = UI_WARN;
        break;
    case DIAL_LINK_UP:
        snprintf(buf, n, "%s", TR(DIAL_ST_UP));
        col = UI_OK;
        break;
    case DIAL_LINK_DEGRADED:
        snprintf(buf, n, "%s", TR(DIAL_ST_DEGRADED));
        col = UI_WARN;
        break;
    case DIAL_LINK_LOST:
        snprintf(buf, n, "%s", TR(DIAL_ST_LOST));
        col = UI_WARN;
        break;
    case DIAL_LINK_OFF:
    default:
        snprintf(buf, n, "%s", TR(OFF));
        break;
    }
    if (ink != NULL) {
        *ink = col;
    }
    return buf;
}

const char *wireless_pane_state(int entry, char *buf, size_t n,
                                gfx_color_t *ink)
{
    if (buf == NULL || n == 0u || ink == NULL) {
        return "";
    }
    buf[0] = '\0';
    *ink = UI_TEXT_DIM;
    if (entry == WL_ENTRY_IMU) {
        if (!settings_get_bool(SET_IMU_EN)) {
            snprintf(buf, n, "%s", TR(OFF));
        } else {
            (void)sensors_rows_imu_state(buf, n, ink);
        }
    } else if (entry == WL_ENTRY_DIAL) {
        if (!settings_get_bool(SET_DIAL_EN)) {
            snprintf(buf, n, "%s", TR(OFF));
        } else {
            (void)wireless_pane_dial_word(&s_dial, buf, n, ink);
        }
    } else if (entry == WL_ENTRY_BAL) {
        (void)wireless_bal_state(buf, n, ink);
    }
    return buf;
}

void wireless_pane_reset(int entry)
{
    if (entry == WL_ENTRY_IMU) {
        settings_reset_rows(k_imu, (int)(sizeof(k_imu) / sizeof(k_imu[0])));
    } else if (entry == WL_ENTRY_DIAL) {
        settings_reset_rows(k_dial,
                            (int)(sizeof(k_dial) / sizeof(k_dial[0])));
    } else if (entry == WL_ENTRY_BAL) {
        wireless_bal_reset();
    }
}

void wireless_pane_set_dial(const dial_view_t *v)
{
    dial_view_t next;
    if (v != NULL) {
        memcpy(&next, v, sizeof(next));
    } else {
        memset(&next, 0, sizeof(next));
    }
    if (memcmp(&next, &s_dial, sizeof(next)) != 0) {
        s_dial = next;
        settings_screen_invalidate();
    }
}
