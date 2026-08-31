/*
 * SPDX-License-Identifier: MIT
 */

#include "mcp2515.h"

#include <stddef.h>

/*
 * The extended identifier's layout, which is the whole reason this file is
 * testable code rather than four lines inside the driver:
 *
 *     SIDH  bits 28..21
 *     SIDL  bits 20..18 in its top three, then EXIDE, then bits 17..16
 *     EID8  bits 15..8
 *     EID0  bits  7..0
 *
 * The three-bit gap in SIDL is where the standard-identifier layout ended;
 * extended identifiers were fitted around it rather than replacing it.
 */
#define ID_MAX 0x1FFFFFFFu

bool mcp2515_pack_id(uint32_t id, uint8_t out[4])
{
    if (out == NULL || id > ID_MAX) {
        return false;
    }
    out[0] = (uint8_t)(id >> 21);
    out[1] = (uint8_t)((((id >> 18) & 0x07u) << 5) | MCP2515_SIDL_EXIDE
                       | ((id >> 16) & 0x03u));
    out[2] = (uint8_t)((id >> 8) & 0xFFu);
    out[3] = (uint8_t)(id & 0xFFu);
    return true;
}

bool mcp2515_unpack_id(const uint8_t in[4], uint32_t *id)
{
    if (in == NULL || id == NULL) {
        return false;
    }
    if ((in[1] & MCP2515_SIDL_EXIDE) == 0u) {
        return false;
    }
    *id = ((uint32_t)in[0] << 21)
          | ((uint32_t)(in[1] >> 5) << 18)
          | ((uint32_t)(in[1] & 0x03u) << 16)
          | ((uint32_t)in[2] << 8)
          | (uint32_t)in[3];
    return true;
}
