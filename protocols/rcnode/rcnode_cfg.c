/**
 * @file
 * @brief The node's own settings: the name's and the address's rules and
 * the shunt's sync between the panel and the node.  See rcnode.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "rcnode.h"

#include <string.h>

/**
 * @brief Whether a character is one of a node's name.
 *
 * @param ch The character.
 * @return true for A to Z, 0 to 9, space, "-", "_" and ".".
 */
static bool name_char(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' '
           || ch == '-' || ch == '_' || ch == '.';
}

bool rcnode_uname_ok(const char *s)
{
    if (s == NULL) {
        return false;
    }
    const size_t n = strnlen(s, RCNODE_UNAME_MAX + 1u);
    if (n > RCNODE_UNAME_MAX) {
        return false;
    }
    if (n > 0u && (s[0] == ' ' || s[n - 1u] == ' ')) {
        return false;
    }
    for (size_t k = 0u; k < n; ++k) {
        if (!name_char(s[k])) {
            return false;
        }
    }
    return true;
}

bool rcnode_i228_ok(uint8_t a)
{
    return a == RCNODE_I228_AUTO || (a >= RCNODE_I228_MIN && a <= RCNODE_I228_MAX);
}

rcnode_sync_t rcnode_shunt_sync(uint16_t panel, uint16_t node,
                                uint16_t agreed, uint16_t node_age_s,
                                uint32_t panel_age_ms)
{
    if (panel == node) {
        return node == agreed ? RCNODE_SYNC_KEEP : RCNODE_SYNC_ACK;
    }
    const bool node_changed = node != agreed;
    const bool panel_changed = panel != agreed;
    if (!node_changed) {
        return RCNODE_SYNC_WRITE;
    }
    if (!panel_changed) {
        return RCNODE_SYNC_ADOPT;
    }
    /* Both changed: the younger change wins; an unknown age is the oldest. */
    if (node_age_s == RCNODE_AGE_NONE) {
        return panel_age_ms == UINT32_MAX ? RCNODE_SYNC_ADOPT
                                          : RCNODE_SYNC_WRITE;
    }
    const uint32_t node_ms = (uint32_t)node_age_s * 1000u;
    return node_ms <= panel_age_ms ? RCNODE_SYNC_ADOPT : RCNODE_SYNC_WRITE;
}
