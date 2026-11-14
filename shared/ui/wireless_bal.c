/**
 * @file
 * @brief SETUP > WIRELESS' BALANCE NODE entry.  See wireless_bal.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wireless_bal.h"

#include <stdio.h>
#include <string.h>

#include "settings_screen.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"

/** @brief The node as the panel last read it. */
static bal_view_t s_bal;
/** @brief The commands the queue holds at most. */
#define CMD_Q 4u
/** @brief The rows' commands, waiting for the panel, oldest first. */
static bal_cmd_t s_q[CMD_Q];
/** @brief The commands in s_q. */
static unsigned  s_q_n;

/** @brief The entry's settings, the enable row first. */
static const setting_id_t k_bal[] = { SET_BNODE_EN, SET_BAL_TRIAL };

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

bool wireless_pane_take_bal(bal_cmd_t *out)
{
    if (s_q_n == 0u || out == NULL) {
        return false;
    }
    *out = s_q[0];
    --s_q_n;
    memmove(&s_q[0], &s_q[1], s_q_n * sizeof(s_q[0]));
    return true;
}

/** @brief SCAN FOR NODE's value: what the last scan did. @param out The
 *         text. @param n Its size. */
static void scan_value(char *out, size_t n)
{
    if (s_bal.link == BAL_LINK_SCANNING) {
        snprintf(out, n, "%s", TR(DIAL_SCAN_RUNS));
    } else if (!s_bal.scan_done) {
        snprintf(out, n, "%s", TR(DIAL_SCAN_GO));
    } else if (s_bal.scan_cut) {
        snprintf(out, n, "%s", TR(DIAL_SCAN_CUT));
    } else if (s_bal.found_n == 0u) {
        snprintf(out, n, "%s", TR(DIAL_SCAN_NONE));
    } else {
        snprintf(out, n, TR(DIAL_SCAN_FOUND), (unsigned)s_bal.found_n);
    }
}

/** @brief SCAN FOR NODE tapped: refused while ARMED, else asked. */
static void scan_tap(void)
{
    if (ui_router_status()->armed) {
        ui_router_set_alert(TR(BNODE_SCAN_ARMED));
        return;
    }
    push(BAL_CMD_SCAN, 0u);
}

const ui_setlist_custom_t wireless_bal_scan_row = {
    .label = TX_BNODE_ROW_SCAN,
    .help = TX_BNODE_ROW_SCAN_HELP,
    .value_text = scan_value,
    .tap = scan_tap,
};

/** @brief BOUND NODE's value: its name, its address, or NONE.
 *  @param out The text. @param n Its size. */
static void bind_value(char *out, size_t n)
{
    if (s_bal.bound_name[0] != '\0') {
        snprintf(out, n, "%s", s_bal.bound_name);
    } else if (s_bal.bound_addr[0] != '\0') {
        snprintf(out, n, "%s", s_bal.bound_addr);
    } else {
        snprintf(out, n, "%s", TR(DIAL_BIND_NONE));
    }
}

/** @brief The found list's rows. @return The count. */
static int bind_choices(void)
{
    return s_bal.scan_done ? (int)s_bal.found_n : 0;
}

/**
 * @brief A found node's row: its name or address and its RSSI.
 * @param i The row.
 * @return The text, valid until the next call.
 */
static const char *bind_choice(int i)
{
    static char line[48];
    if (i < 0 || i >= (int)s_bal.found_n) {
        return "";
    }
    const bal_dev_t *d = &s_bal.found[i];
    snprintf(line, sizeof(line), TR(DIAL_FOUND_ROW),
             d->name[0] != '\0' ? d->name : d->addr, (int)d->rssi);
    return line;
}

/** @brief The found node that is bound. @return Its row, -1 none. */
static int bind_chosen(void)
{
    for (int i = 0; i < (int)s_bal.found_n; ++i) {
        if (strcmp(s_bal.found[i].addr, s_bal.bound_addr) == 0) {
            return i;
        }
    }
    return -1;
}

/** @brief A found node tapped: bound. @param i Its row. */
static void bind_pick(int i)
{
    if (i >= 0 && i < (int)s_bal.found_n) {
        push(BAL_CMD_BIND, (uint8_t)i);
    }
}

const ui_setlist_custom_t wireless_bal_bind_row = {
    .label = TX_BNODE_ROW_BIND,
    .help = TX_BNODE_ROW_BIND_HELP,
    .value_text = bind_value,
    .choices = bind_choices,
    .choice = bind_choice,
    .chosen = bind_chosen,
    .pick = bind_pick,
};

/** @brief NODE BATTERY's value: the cell's voltage, LOW under 3.50V, NO
 *         READING, or "--" before a STATUS came on this link.
 *  @param out The text. @param n Its size. */
static void bat_value(char *out, size_t n)
{
    const unsigned v = s_bal.bat_mv / 1000u;
    const unsigned c = (s_bal.bat_mv % 1000u) / 10u;
    if (!s_bal.bat_known) {
        snprintf(out, n, "--");
    } else if ((s_bal.bat_flags & (RCNODE_ST_BAT_NONE | RCNODE_ST_BAT_ERR))
               != 0u) {
        snprintf(out, n, "%s", TR(BNODE_BAT_NONE));
    } else if ((s_bal.bat_flags & RCNODE_ST_BAT_LOW) != 0u) {
        snprintf(out, n, TR(BNODE_BAT_LOW), v, c);
    } else {
        snprintf(out, n, TR(BNODE_BAT_V), v, c);
    }
}

const ui_setlist_custom_t wireless_bal_bat_row = {
    .label = TX_BNODE_ROW_BAT,
    .help = TX_BNODE_ROW_BAT_HELP,
    .value_text = bat_value,
};

int wireless_bal_rows(wl_row_t *out, int max)
{
    int n = 0;
    const struct {
        setting_id_t id;
        const ui_setlist_custom_t *c;
    } rows[] = {
        { SET_BNODE_EN, NULL },
        { (setting_id_t)0, &wireless_bal_scan_row },
        { (setting_id_t)0, &wireless_bal_bind_row },
        { (setting_id_t)0, &wireless_bal_bat_row },
        { SET_BAL_TRIAL, NULL },
    };
    const int shown = settings_get_bool(SET_BNODE_EN)
                          ? (int)(sizeof(rows) / sizeof(rows[0])) : 1;
    for (int i = 0; i < shown; ++i, ++n) {
        if (out != NULL && n < max) {
            out[n].id = rows[i].id;
            out[n].custom = rows[i].c;
        }
    }
    return n;
}

bool wireless_bal_owns(setting_id_t id)
{
    return id == SET_BNODE_EN || id == SET_BAL_TRIAL;
}

const char *wireless_pane_bal_word(const bal_view_t *v, char *buf, size_t n,
                                   gfx_color_t *ink)
{
    gfx_color_t col = UI_TEXT_DIM;
    if (buf == NULL || n == 0u) {
        return "";
    }
    const uint8_t link = v != NULL ? v->link : (uint8_t)BAL_LINK_OFF;
    switch (link) {
    case BAL_LINK_STARTING:
        snprintf(buf, n, "%s", TR(IMU_ST_STARTING));
        break;
    case BAL_LINK_NO_BT:
        snprintf(buf, n, "%s", TR(IMU_ST_NO_BT));
        col = UI_WARN;
        break;
    case BAL_LINK_C6_FAILED:
        snprintf(buf, n, "%s", TR(IMU_ST_C6_FAILED));
        col = UI_WARN;
        break;
    case BAL_LINK_NONE:
        snprintf(buf, n, "%s", TR(BNODE_ST_NONE));
        break;
    case BAL_LINK_SCANNING:
        snprintf(buf, n, "%s", TR(IMU_ST_SCANNING));
        break;
    case BAL_LINK_CONNECTING:
        snprintf(buf, n, "%s", TR(IMU_ST_CONNECTING));
        break;
    case BAL_LINK_NOT_NODE:
        snprintf(buf, n, "%s", TR(BNODE_ST_NOT_NODE));
        col = UI_WARN;
        break;
    case BAL_LINK_WAITING:
        snprintf(buf, n, "%s", TR(BNODE_ST_WAITING));
        break;
    case BAL_LINK_WRONG:
        snprintf(buf, n, TR(BNODE_ST_WRONG), (unsigned)v->info_role,
                 (unsigned)v->info_proto);
        col = UI_WARN;
        break;
    case BAL_LINK_QUIET:
        snprintf(buf, n, "%s", TR(BNODE_ST_QUIET));
        col = UI_WARN;
        break;
    case BAL_LINK_UP:
        snprintf(buf, n, "%s", TR(BNODE_ST_UP));
        col = UI_OK;
        break;
    case BAL_LINK_OFF:
    default:
        snprintf(buf, n, "%s", TR(OFF));
        break;
    }
    if (ink != NULL) {
        *ink = col;
    }
    return buf;
}

const char *wireless_bal_state(char *buf, size_t n, gfx_color_t *ink)
{
    if (buf == NULL || n == 0u || ink == NULL) {
        return "";
    }
    *ink = UI_TEXT_DIM;
    if (!settings_get_bool(SET_BNODE_EN)) {
        snprintf(buf, n, "%s", TR(OFF));
        return buf;
    }
    return wireless_pane_bal_word(&s_bal, buf, n, ink);
}

void wireless_bal_reset(void)
{
    settings_reset_rows(k_bal, (int)(sizeof(k_bal) / sizeof(k_bal[0])));
}

/**
 * @brief Whether two views show the same.
 * @param a One.  @param b The other.
 * @return true when every member SETUP shows is equal.
 */
static bool same_view(const bal_view_t *a, const bal_view_t *b)
{
    if (a->link != b->link || a->scan_done != b->scan_done
        || a->scan_cut != b->scan_cut || a->found_n != b->found_n
        || a->hidden != b->hidden || a->info_role != b->info_role
        || a->info_proto != b->info_proto || a->bat_known != b->bat_known
        || a->bat_mv != b->bat_mv || a->bat_flags != b->bat_flags
        || strcmp(a->bound_addr, b->bound_addr) != 0
        || strcmp(a->bound_name, b->bound_name) != 0) {
        return false;
    }
    for (uint8_t i = 0; i < a->found_n && i < BAL_FOUND_MAX; ++i) {
        if (a->found[i].rssi != b->found[i].rssi
            || strcmp(a->found[i].addr, b->found[i].addr) != 0
            || strcmp(a->found[i].name, b->found[i].name) != 0) {
            return false;
        }
    }
    return true;
}

void wireless_pane_set_bal(const bal_view_t *v)
{
    bal_view_t next;
    if (v != NULL) {
        memcpy(&next, v, sizeof(next));
    } else {
        memset(&next, 0, sizeof(next));
    }
    if (!same_view(&next, &s_bal)) {
        s_bal = next;
        settings_screen_invalidate();
    }
}
