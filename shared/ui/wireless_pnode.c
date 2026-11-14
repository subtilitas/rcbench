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

void wireless_pane_set_pnode(const pnode_view_t *v)
{
    pnode_view_t next;
    if (v != NULL) {
        memcpy(&next, v, sizeof(next));
    } else {
        memset(&next, 0, sizeof(next));
    }
    if (memcmp(&next, &s_node, sizeof(next)) != 0) {
        s_node = next;
        settings_screen_invalidate();
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

/** @brief SCAN FOR NODE tapped: refused while ARMED, else asked. */
static void scan_tap(void)
{
    if (ui_router_status()->armed) {
        ui_router_set_alert(TR(PNODE_SCAN_ARMED));
        return;
    }
    push(PNODE_CMD_SCAN, 0u);
}

/** @brief SCAN FOR NODE: the last scan's result, a tap scans. */
const ui_setlist_custom_t wireless_pane_pnode_scan_row = {
    .label = TX_PNODE_ROW_SCAN,
    .help = TX_PNODE_ROW_SCAN_HELP,
    .value_text = scan_value,
    .tap = scan_tap,
};

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

/** @brief The found list's rows. @return The count. */
static int bind_choices(void)
{
    return s_node.scan_done ? (int)s_node.found_n : 0;
}

/**
 * @brief A found node's row: its name or address and its RSSI.
 * @param i The row.
 * @return The text, valid until the next call.
 */
static const char *bind_choice(int i)
{
    static char line[48];
    if (i < 0 || i >= (int)s_node.found_n) {
        return "";
    }
    const pnode_dev_t *d = &s_node.found[i];
    snprintf(line, sizeof(line), TR(DIAL_FOUND_ROW),
             d->name[0] != '\0' ? d->name : d->addr, (int)d->rssi);
    return line;
}

/** @brief The found node that is bound. @return Its row, -1 none. */
static int bind_chosen(void)
{
    for (int i = 0; i < (int)s_node.found_n; ++i) {
        if (strcmp(s_node.found[i].addr, s_node.bound_addr) == 0) {
            return i;
        }
    }
    return -1;
}

/** @brief A found node tapped: bound. @param i Its row. */
static void bind_pick(int i)
{
    if (i >= 0 && i < (int)s_node.found_n) {
        push(PNODE_CMD_BIND, (uint8_t)i);
    }
}

/** @brief BOUND NODE: the bound node, the last scan's list, a pick binds. */
const ui_setlist_custom_t wireless_pane_pnode_bind_row = {
    .label = TX_PNODE_ROW_BIND,
    .help = TX_PNODE_ROW_BIND_HELP,
    .value_text = bind_value,
    .choices = bind_choices,
    .choice = bind_choice,
    .chosen = bind_chosen,
    .pick = bind_pick,
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
        snprintf(buf, n, TR(DIAL_ST_PROTO), (unsigned)v->proto);
    } else {
        snprintf(buf, n, "%s", w);
    }
    if (ink != NULL) {
        *ink = col;
    }
    return buf;
}
