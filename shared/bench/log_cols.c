/**
 * @file
 * @brief The bench log's columns per watched target.
 *
 * See log_cols.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "log_cols.h"

#include <stdio.h>
#include <string.h>

/**
 * @brief What WATCH value @p w names, into @p out; false for none.
 *
 * @param w   The WATCH value.
 * @param out Receives the name.
 * @param n   The size of out, in bytes.
 * @return true when w names a target; false for none.
 */
static bool target_name(uint16_t w, char *out, size_t n)
{
    if (w >= LINK_SM_WATCH_OUT(0) && w <= LINK_SM_WATCH_OUT(7)) {
        snprintf(out, n, "out %u", (unsigned)(w - LINK_SM_WATCH_OUT(0) + 1u));
        return true;
    }
    if (w >= LINK_SM_WATCH_PORT(0) && w <= LINK_SM_WATCH_PORT(3)) {
        snprintf(out, n, "port %u",
                 (unsigned)(w - LINK_SM_WATCH_PORT(0) + 1u));
        return true;
    }
    if (w == LINK_SM_WATCH_SUPPLY) {
        snprintf(out, n, "supply");
        return true;
    }
    if (w == LINK_SM_WATCH_RAIL_A || w == LINK_SM_WATCH_RAIL_B) {
        snprintf(out, n, "rail %c",
                 (w == LINK_SM_WATCH_RAIL_A) ? 'a' : 'b');
        return true;
    }
    if (w == LINK_SM_WATCH_PROG_5V) {
        snprintf(out, n, "prog 5v");
        return true;
    }
    return false;
}

/**
 * @brief Whether channel @p c of @p map reads something it names.
 *
 * @param map The SENSE_MAP registers.
 * @param c   The channel.
 * @return true when the channel is logged.
 */
static bool logged(const uint16_t *map, unsigned c)
{
    char name[LOG_COL_NAME_MAX];
    return (map[LINK_SM_ENABLE] & (1u << (c / 3u))) != 0u
           && map[LINK_SM_SHUNT(c)] != 0u
           && target_name(map[LINK_SM_WATCH(c)], name, sizeof(name));
}

void log_cols_from_map(const uint16_t *map, log_cols_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (map == NULL) {
        return;
    }
    /* FAST's channel first: the one the servo test reads. */
    const unsigned fast = map[LINK_SM_FAST];
    for (unsigned k = 0u; k <= LINK_SM_CHANNELS
                          && out->n < LOG_COLS_GROUPS; ++k) {
        unsigned c;
        if (k == 0u) {
            if (fast >= LINK_SM_CHANNELS) {
                continue;
            }
            c = fast;
        } else {
            c = k - 1u;
            if (c == fast) {
                continue;
            }
        }
        if (!logged(map, c)) {
            continue;
        }
        unsigned same = 0u;
        for (unsigned d = 0u; d < LINK_SM_CHANNELS; ++d) {
            same += (logged(map, d)
                     && map[LINK_SM_WATCH(d)] == map[LINK_SM_WATCH(c)])
                        ? 1u : 0u;
        }
        char *name = out->name[out->n];
        (void)target_name(map[LINK_SM_WATCH(c)], name, LOG_COL_NAME_MAX);
        if (same > 1u) {
            const size_t len = strlen(name);
            snprintf(name + len, LOG_COL_NAME_MAX - len, " 0x%02x ch%u",
                     (unsigned)(LINK_SN_I3221_ADDR_MIN + c / 3u),
                     (unsigned)(c % 3u + 1u));
        }
        out->ch[out->n] = (uint8_t)c;
        ++out->n;
    }
}
