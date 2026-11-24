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
#include "wl_search.h"

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

/** @brief A scan for SEARCH: refused while ARMED or while a scan runs,
 *         each with its text on the band, else asked. */
static void scan_tap(void)
{
    if (ui_router_status()->armed) {
        ui_router_set_alert(TR(BNODE_SCAN_ARMED));
        return;
    }
    if (s_bal.link == BAL_LINK_SCANNING) {
        ui_router_set_alert(TR(NODE_SCAN_RUNNING));
        return;
    }
    push(BAL_CMD_SCAN, 0u);
}


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

/**
 * @brief SEARCH's view of the node: the scan, the found nodes, the bound
 *        one and the link state.
 * @param out The view, zeroed.
 */
static void search_view(wls_view_t *out)
{
    out->scanning = s_bal.link == BAL_LINK_SCANNING;
    out->scan_left_s = s_bal.scan_left_s;
    out->scan_done = s_bal.scan_done;
    out->scan_cut = s_bal.scan_cut;
    const unsigned n = s_bal.found_n < BAL_FOUND_MAX ? s_bal.found_n
                                                     : BAL_FOUND_MAX;
    out->found_n = (uint8_t)n;
    for (unsigned i = 0u; i < n; ++i) {
        wl_search_put(out, i, s_bal.found[i].addr, s_bal.found[i].name,
                      s_bal.found[i].rssi);
    }
    snprintf(out->bound_addr, sizeof(out->bound_addr), "%s",
             s_bal.bound_addr);
    snprintf(out->bound_name, sizeof(out->bound_name), "%s",
             s_bal.bound_name);
    (void)wireless_pane_bal_word(&s_bal, out->word, sizeof(out->word),
                                 &out->word_ink);
}

/** @brief SEARCH's bind of found node @p i, queued for the controller.
 *  @param i Its row. */
static void search_bind(uint8_t i)
{
    push(BAL_CMD_BIND, i);
}

/** @brief SEARCH's UNBIND of the bound node, queued for the controller. */
static void search_unbind(void)
{
    push(BAL_CMD_UNBIND, 0u);
}

/** @brief The node's entry as SEARCH asks it. */
static const wls_device_t k_search = {
    .entry = WL_ENTRY_BAL,
    .kind = TX_BNODE_ENTRY,
    .bind_scanning = TX_NODE_BIND_SCANNING,
    .bind_gone = TX_NODE_BIND_GONE,
    .view = search_view,
    .scan = scan_tap,
    .bind = search_bind,
    .unbind = search_unbind,
};

/** @brief SCAN FOR NODE tapped: SEARCH opens and a scan is asked. */
static void scan_open(void)
{
    wl_search_open(&k_search, true);
}

/** @brief BOUND NODE tapped: SEARCH opens on the last scan's list. */
static void bind_open(void)
{
    wl_search_open(&k_search, false);
}

/** @brief SCAN FOR NODE: the last scan's result; a tap opens SEARCH and
 *         scans. */
const ui_setlist_custom_t wireless_bal_scan_row = {
    .label = TX_BNODE_ROW_SCAN,
    .help = TX_BNODE_ROW_SCAN_HELP,
    .value_text = scan_value,
    .tap = scan_open,
};

/** @brief BOUND NODE: the bound node; a tap opens SEARCH on the last
 *         scan's list. */
const ui_setlist_custom_t wireless_bal_bind_row = {
    .label = TX_BNODE_ROW_BIND,
    .help = TX_BNODE_ROW_BIND_HELP,
    .value_text = bind_value,
    .tap = bind_open,
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
        || a->scan_left_s != b->scan_left_s
        || a->hidden != b->hidden || a->info_role != b->info_role
        || a->info_proto != b->info_proto || a->bat_known != b->bat_known
        || a->bat_mv != b->bat_mv || a->bat_flags != b->bat_flags
        || a->bind_refused != b->bind_refused
        || a->scan_armed != b->scan_armed
        || a->scan_unready != b->scan_unready
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
    if (same_view(&next, &s_bal)) {
        return;
    }
    /* A command the controller refused after the frame passed it on: a
     * bind behind a scan or a changed list, a scan while an arm waits or
     * one runs. */
    const bool bind_no = v != NULL
                         && next.bind_refused != s_bal.bind_refused;
    const bool armed_no = v != NULL && next.scan_armed != s_bal.scan_armed;
    const bool scan_no = v != NULL
                         && next.scan_unready != s_bal.scan_unready;
    s_bal = next;
    settings_screen_invalidate();
    const bool scanning = s_bal.link == BAL_LINK_SCANNING;
    if (bind_no) {
        ui_router_set_alert(scanning ? TR(NODE_BIND_SCANNING)
                                     : TR(NODE_BIND_GONE));
    } else if (armed_no) {
        ui_router_set_alert(TR(BNODE_SCAN_ARMED));
    } else if (scan_no) {
        ui_router_set_alert(scanning ? TR(NODE_SCAN_RUNNING)
                                     : TR(BNODE_SCAN_OFF));
    }
}
