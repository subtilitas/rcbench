/**
 * @file
 * @brief The MCP2515's frame arithmetic: the 29-bit identifier packed into and
 *        unpacked from its four registers, and the receive buffer's decode.
 *
 * See mcp2515.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "mcp2515.h"

#include <stddef.h>
#include <string.h>

/*
 * The extended identifier's layout across the four registers:
 *
 *     SIDH  bits 28..21
 *     SIDL  bits 20..18 in its top three, then EXIDE, then bits 17..16
 *     EID8  bits 15..8
 *     EID0  bits  7..0
 *
 * The three-bit gap in SIDL is the end of the standard 11-bit layout; the
 * extended bits are fitted around it.
 */
#define ID_MAX 0x1FFFFFFFu /**< the largest 29-bit identifier */

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

bool mcp2515_tx_load(const link_can_frame_t *f, uint8_t txp,
                     uint8_t out[MCP2515_TXB_LOAD_BYTES])
{
    if (f == NULL || out == NULL || f->dlc > 8u) {
        return false;
    }
    uint8_t id[4];
    if (!mcp2515_pack_id(f->id, id)) {
        return false;
    }
    memset(out, 0, MCP2515_TXB_LOAD_BYTES);
    out[0] = (uint8_t)(txp & MCP2515_TXB_TXP_MASK);
    memcpy(out + 1, id, sizeof(id));
    out[5] = f->dlc;
    memcpy(out + 6, f->data, f->dlc);
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

/**
 * @brief Turn the 13 registers of one receive buffer into a frame.
 *
 * False for a frame this link does not carry: an 11-bit identifier, a remote
 * frame (bit 6 of the DLC register, RTR) whose data registers hold stale
 * bytes, or a length above 8.
 *
 * @param buf The 13 registers of the receive buffer.
 * @param f   Takes the frame.
 * @return true when @p f holds a frame; false for one this link does not carry.
 */
static bool mcp2515_decode_rx(const uint8_t buf[13], link_can_frame_t *f)
{
    if (buf == NULL || f == NULL) {
        return false;
    }
    memset(f, 0, sizeof(*f));
    if (!mcp2515_unpack_id(buf, &f->id)) {
        return false;   /* an 11-bit identifier; nothing here sends those */
    }
    /*
     * Bit 6 of the DLC (data length code) register is RTR (remote
     * transmission request): a remote frame, which carries a length but no
     * data.  Its data registers hold whatever the last data frame left
     * there, so accepting one would deliver stale bytes under a fresh
     * identifier.  Nothing on this bus sends remote frames, so one is
     * foreign traffic or a fault.
     */
    if ((buf[4] & 0x40u) != 0u) {
        return false;
    }
    f->dlc = (uint8_t)(buf[4] & 0x0Fu);
    if (f->dlc > 8) {
        return false;
    }
    memcpy(f->data, buf + 5, f->dlc);
    return true;
}

bool mcp2515_recv_next(mcp2515_fetch_fn fetch, void *ctx, link_can_frame_t *f,
                       uint32_t *refused)
{
    if (fetch == NULL || f == NULL) {
        return false;
    }
    for (unsigned i = 0; i < MCP2515_RX_BUFFERS; ++i) {
        uint8_t buf[13];
        if (!fetch(ctx, buf)) {
            return false;
        }
        if (mcp2515_decode_rx(buf, f)) {
            return true;
        }
        if (refused != NULL) {
            ++*refused;
        }
    }
    return false;
}
