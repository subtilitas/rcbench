/**
 * @file wireless_pnode.c
 * @brief SETUP > WIRELESS' POWER NODE entry: its custom rows, its state
 *        word and its commands.  See wireless_pane.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wireless_pane.h"

#include <stdio.h>
#include <string.h>

#include "settings_screen.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "wl_search.h"

/** @brief The node as the panel last read it. */
static pnode_view_t s_node;
/** @brief The commands the queue holds at most. */
#define CMD_Q 4u
/** @brief The rows' commands, waiting for the panel, oldest first. */
static pnode_cmd_t s_q[CMD_Q];
/** @brief The commands in s_q. */
static unsigned    s_q_n;

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

bool wireless_pane_take_pnode(pnode_cmd_t *out)
{
    if (s_q_n == 0u || out == NULL) {
        return false;
    }
    *out = s_q[0];
    --s_q_n;
    memmove(&s_q[0], &s_q[1], s_q_n * sizeof(s_q[0]));
    return true;
}

const pnode_view_t *wireless_pane_pnode(void)
{
    return &s_node;
}

/** @brief Two found nodes alike, field by field.
 *  @param a One. @param b The other.
 *  @return true when the address, the name and the RSSI match. */
static bool dev_same(const pnode_dev_t *a, const pnode_dev_t *b)
{
    return strncmp(a->addr, b->addr, sizeof a->addr) == 0
        && strncmp(a->name, b->name, sizeof a->name) == 0
        && a->rssi == b->rssi;
}

/** @brief Two views alike, field by field: the struct has padding, so a
 *         byte compare could see a change where none is.
 *  @param a One. @param b The other.
 *  @return true when every field matches. */
static bool view_same(const pnode_view_t *a, const pnode_view_t *b)
{
    if (a->link != b->link || a->scan_done != b->scan_done
        || a->scan_cut != b->scan_cut || a->found_n != b->found_n
        || a->scan_left_s != b->scan_left_s
        || a->hidden != b->hidden || a->proto != b->proto
        || a->have_status != b->have_status || a->bat_low != b->bat_low
        || a->bat_none != b->bat_none || a->bat_err != b->bat_err
        || a->vbat_mv != b->vbat_mv || a->bind_refused != b->bind_refused
        || a->have_info != b->have_info || a->i228_addr != b->i228_addr
        || a->shunt_uohm != b->shunt_uohm
        || a->shunt_refused != b->shunt_refused
        || a->scan_armed != b->scan_armed
        || a->scan_unready != b->scan_unready
        || strncmp(a->bound_addr, b->bound_addr, sizeof a->bound_addr) != 0
        || strncmp(a->bound_name, b->bound_name, sizeof a->bound_name) != 0) {
        return false;
    }
    for (size_t i = 0u; i < PNODE_FOUND_MAX; i++) {
        if (!dev_same(&a->found[i], &b->found[i])) {
            return false;
        }
    }
    return true;
}

void wireless_pane_set_pnode(const pnode_view_t *v)
{
    pnode_view_t next;
    if (v != NULL) {
        memcpy(&next, v, sizeof(next));
    } else {
        memset(&next, 0, sizeof(next));
    }
    if (view_same(&next, &s_node)) {
        return;
    }
    /* A command the controller refused after the frame passed it on: a
     * bind behind a scan or a changed list, a scan while an arm waits or
     * one runs. */
    const bool bind_no = v != NULL
                         && next.bind_refused != s_node.bind_refused;
    const bool armed_no = v != NULL && next.scan_armed != s_node.scan_armed;
    const bool scan_no = v != NULL
                         && next.scan_unready != s_node.scan_unready;
    const bool shunt_no = v != NULL
                          && next.shunt_refused != s_node.shunt_refused;
    s_node = next;
    settings_screen_invalidate();
    const bool scanning = s_node.link == PNODE_LINK_SCANNING;
    if (bind_no) {
        ui_router_set_alert(scanning ? TR(NODE_BIND_SCANNING)
                                     : TR(NODE_BIND_GONE));
    } else if (armed_no) {
        ui_router_set_alert(TR(PNODE_SCAN_ARMED));
    } else if (scan_no) {
        ui_router_set_alert(scanning ? TR(NODE_SCAN_RUNNING)
                                     : TR(PNODE_SCAN_OFF));
    } else if (shunt_no) {
        ui_router_set_alert(TR(PNODE_SHUNT_REFUSED));
    }
}

/* ---------------------------------------------------------- SCAN FOR NODE */

/** @brief SCAN FOR NODE's value: what the last scan did. @param out The
 *         text. @param n Its size. */
static void scan_value(char *out, size_t n)
{
    if (s_node.link == PNODE_LINK_SCANNING) {
        snprintf(out, n, "%s", TR(DIAL_SCAN_RUNS));
    } else if (!s_node.scan_done) {
        snprintf(out, n, "%s", TR(DIAL_SCAN_GO));
    } else if (s_node.scan_cut) {
        snprintf(out, n, "%s", TR(DIAL_SCAN_CUT));
    } else if (s_node.found_n == 0u) {
        snprintf(out, n, "%s", TR(DIAL_SCAN_NONE));
    } else {
        snprintf(out, n, TR(DIAL_SCAN_FOUND), (unsigned)s_node.found_n);
    }
}

/** @brief A scan for SEARCH: refused while ARMED or while a scan runs,
 *         each with its text on the band, else asked. */
static void scan_tap(void)
{
    if (ui_router_status()->armed) {
        ui_router_set_alert(TR(PNODE_SCAN_ARMED));
        return;
    }
    if (s_node.link == PNODE_LINK_SCANNING) {
        ui_router_set_alert(TR(NODE_SCAN_RUNNING));
        return;
    }
    push(PNODE_CMD_SCAN, 0u);
}


/* ------------------------------------------------------------- BOUND NODE */

/** @brief BOUND NODE's value: the bound node's name, its address, or NONE.
 *  @param out The text. @param n Its size. */
static void bind_value(char *out, size_t n)
{
    if (s_node.bound_name[0] != '\0') {
        snprintf(out, n, "%s", s_node.bound_name);
    } else if (s_node.bound_addr[0] != '\0') {
        snprintf(out, n, "%s", s_node.bound_addr);
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
    out->scanning = s_node.link == PNODE_LINK_SCANNING;
    out->scan_left_s = s_node.scan_left_s;
    out->scan_done = s_node.scan_done;
    out->scan_cut = s_node.scan_cut;
    const unsigned n = s_node.found_n < PNODE_FOUND_MAX ? s_node.found_n
                                                        : PNODE_FOUND_MAX;
    out->found_n = (uint8_t)n;
    for (unsigned i = 0u; i < n; ++i) {
        wl_search_put(out, i, s_node.found[i].addr, s_node.found[i].name,
                      s_node.found[i].rssi);
    }
    snprintf(out->bound_addr, sizeof(out->bound_addr), "%s",
             s_node.bound_addr);
    snprintf(out->bound_name, sizeof(out->bound_name), "%s",
             s_node.bound_name);
    (void)wireless_pane_pnode_word(&s_node, out->word, sizeof(out->word),
                                   &out->word_ink);
}

/** @brief SEARCH's bind of found node @p i, queued for the controller.
 *  @param i Its row. */
static void search_bind(uint8_t i)
{
    push(PNODE_CMD_BIND, i);
}

/** @brief SEARCH's UNBIND of the bound node, queued for the controller. */
static void search_unbind(void)
{
    push(PNODE_CMD_UNBIND, 0u);
}

/** @brief The node's entry as SEARCH asks it. */
static const wls_device_t k_search = {
    .entry = WL_ENTRY_PNODE,
    .kind = TX_PNODE_ENTRY,
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
const ui_setlist_custom_t wireless_pane_pnode_scan_row = {
    .label = TX_PNODE_ROW_SCAN,
    .help = TX_PNODE_ROW_SCAN_HELP,
    .value_text = scan_value,
    .tap = scan_open,
};

/** @brief BOUND NODE: the bound node; a tap opens SEARCH on the last
 *         scan's list. */
const ui_setlist_custom_t wireless_pane_pnode_bind_row = {
    .label = TX_PNODE_ROW_BIND,
    .help = TX_PNODE_ROW_BIND_HELP,
    .value_text = bind_value,
    .tap = bind_open,
};

/* ----------------------------------------------------------- NODE BATTERY */

/** @brief NODE BATTERY's value: the cell from the last STATUS, LOW under
 *         3.5V, NO CELL on USB alone, READ FAILED, or NOT LINKED.
 *  @param out The text. @param n Its size. */
static void bat_value(char *out, size_t n)
{
    const unsigned mv = s_node.vbat_mv;
    if (!s_node.have_status) {
        snprintf(out, n, "%s", TR(PNODE_BAT_UNKNOWN));
    } else if (s_node.bat_err) {
        snprintf(out, n, "%s", TR(PNODE_BAT_ERR));
    } else if (s_node.bat_none) {
        snprintf(out, n, "%s", TR(PNODE_BAT_NONE));
    } else if (s_node.bat_low) {
        snprintf(out, n, TR(PNODE_BAT_LOW), mv / 1000u, (mv % 1000u) / 10u);
    } else {
        snprintf(out, n, TR(PNODE_BAT_V), mv / 1000u, (mv % 1000u) / 10u);
    }
}

/** @brief NODE BATTERY: the node's cell, read only. */
const ui_setlist_custom_t wireless_pane_pnode_bat_row = {
    .label = TX_PNODE_ROW_BAT,
    .help = TX_PNODE_ROW_BAT_HELP,
    .value_text = bat_value,
};

/* ------------------------------------------------------------ NODE INA228 */

/** @brief NODE INA228's value: "0x40, 500uΩ" from INFO, NOT FOUND when the
 *         node names no address, NOT LINKED before INFO.
 *  @param out The text. @param n Its size. */
static void i228_value(char *out, size_t n)
{
    if (!s_node.have_info) {
        snprintf(out, n, "%s", TR(PNODE_BAT_UNKNOWN));
    } else if (s_node.i228_addr == 0u) {
        snprintf(out, n, "%s", TR(PNODE_I228_NONE));
    } else {
        snprintf(out, n, TR(PNODE_I228_AT), (unsigned)s_node.i228_addr,
                 (unsigned)s_node.shunt_uohm);
    }
}

/** @brief NODE INA228: the node's part and shunt, read only. */
const ui_setlist_custom_t wireless_pane_pnode_i228_row = {
    .label = TX_PNODE_ROW_I228,
    .help = TX_PNODE_ROW_I228_HELP,
    .value_text = i228_value,
};

/* ------------------------------------------------------------- the word */

/**
 * @brief The words of one link state and their colour.
 * @param link The state (pnode_link_t).
 * @param ink  Takes the colour.
 * @return The text, or NULL for OTHER PROTOCOL, which takes a number.
 */
static const char *word_of(uint8_t link, gfx_color_t *ink)
{
    *ink = UI_TEXT_DIM;
    switch (link) {
    case PNODE_LINK_STARTING:   return TR(IMU_ST_STARTING);
    case PNODE_LINK_NO_BT:      *ink = UI_WARN; return TR(IMU_ST_NO_BT);
    case PNODE_LINK_C6_FAILED:  *ink = UI_WARN; return TR(IMU_ST_C6_FAILED);
    case PNODE_LINK_NONE:       return TR(PNODE_ST_NONE);
    case PNODE_LINK_SCANNING:   return TR(IMU_ST_SCANNING);
    case PNODE_LINK_CONNECTING: return TR(IMU_ST_CONNECTING);
    case PNODE_LINK_NOT_NODE:   *ink = UI_WARN; return TR(PNODE_ST_NOT_NODE);
    case PNODE_LINK_WAITING:    return TR(PNODE_ST_WAITING);
    case PNODE_LINK_PROTO:      *ink = UI_WARN; return NULL;
    case PNODE_LINK_SETUP:      *ink = UI_WARN; return TR(PNODE_ST_SETUP);
    case PNODE_LINK_NO_SENSOR:  *ink = UI_WARN; return TR(PNODE_ST_NO_SENSOR);
    case PNODE_LINK_UP:         *ink = UI_OK;   return TR(DIAL_ST_UP);
    case PNODE_LINK_STALE:      *ink = UI_WARN; return TR(PNODE_ST_STALE);
    case PNODE_LINK_LOST:       *ink = UI_WARN; return TR(PNODE_ST_LOST);
    case PNODE_LINK_OFF:
    default:                    return TR(OFF);
    }
}

const char *wireless_pane_pnode_word(const pnode_view_t *v, char *buf,
                                     size_t n, gfx_color_t *ink)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    gfx_color_t col;
    const uint8_t link = v != NULL ? v->link : (uint8_t)PNODE_LINK_OFF;
    const char *w = word_of(link, &col);
    if (w == NULL) {
        /* Only a view's PNODE_LINK_PROTO gets here; v is not NULL. */
        snprintf(buf, n, TR(DIAL_ST_PROTO), v != NULL ? (unsigned)v->proto : 0u);
    } else {
        snprintf(buf, n, "%s", w);
    }
    if (ink != NULL) {
        *ink = col;
    }
    return buf;
}
