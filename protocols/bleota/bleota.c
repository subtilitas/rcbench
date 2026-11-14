/**
 * @file
 * @brief The bleota codec: control writes, notifications and chunks
 *        (bleota.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "bleota.h"

#include <string.h>

_Static_assert(sizeof(bleota_role_t) == BLEOTA_ROLE_LEN,
               "bleota_role_t is the role's 8 bytes on flash");

/** @brief The service's UUID, least significant byte first. */
const uint8_t bleota_uuid_svc[16]  = { BLEOTA_UUID_LE(BLEOTA_UUID_SVC) };
/** @brief CTRL's UUID, least significant byte first. */
const uint8_t bleota_uuid_ctrl[16] = { BLEOTA_UUID_LE(BLEOTA_UUID_CTRL) };
/** @brief DATA's UUID, least significant byte first. */
const uint8_t bleota_uuid_data[16] = { BLEOTA_UUID_LE(BLEOTA_UUID_DATA) };

/**
 * @brief Write a 16-bit value, least significant byte first.
 * @param p where
 * @param v the value
 */
static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/**
 * @brief Write a 32-bit value, least significant byte first.
 * @param p where
 * @param v the value
 */
static void put32(uint8_t *p, uint32_t v)
{
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}

/**
 * @brief Read a 16-bit value, least significant byte first.
 * @param p where
 * @return the value
 */
static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)(p[1] << 8));
}

/**
 * @brief Read a 32-bit value, least significant byte first.
 * @param p where
 * @return the value
 */
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)get16(p) | ((uint32_t)get16(p + 2) << 16);
}

size_t bleota_begin_encode(uint8_t kind, uint32_t size, const uint16_t ver[3],
                           uint8_t flags, uint8_t *out)
{
    if (ver == NULL || out == NULL) {
        return 0u;
    }
    out[0] = BLEOTA_OP_BEGIN;
    out[1] = BLEOTA_PROTO;
    out[2] = kind;
    out[3] = flags;
    put32(out + 4, size);
    put16(out + 8, ver[0]);
    put16(out + 10, ver[1]);
    put16(out + 12, ver[2]);
    return BLEOTA_LEN_BEGIN;
}

size_t bleota_hash_encode(const uint8_t sha[32], uint8_t part, uint8_t *out)
{
    if (sha == NULL || out == NULL || part > 1u) {
        return 0u;
    }
    out[0] = BLEOTA_OP_HASH;
    out[1] = part;
    memcpy(out + 2, sha + (size_t)16u * part, 16u);
    return BLEOTA_LEN_HASH;
}

size_t bleota_op_encode(uint8_t op, uint8_t *out)
{
    if (out == NULL || (op != BLEOTA_OP_END && op != BLEOTA_OP_ABORT
                        && op != BLEOTA_OP_POLL)) {
        return 0u;
    }
    out[0] = op;
    return BLEOTA_LEN_OP;
}

bool bleota_ctrl_decode(const uint8_t *b, size_t n, bleota_ctrl_t *out)
{
    if (b == NULL || out == NULL || n == 0u) {
        return false;
    }
    memset(out, 0, sizeof *out);
    out->op = b[0];
    switch (b[0]) {
    case BLEOTA_OP_BEGIN:
        if (n != BLEOTA_LEN_BEGIN) {
            return false;
        }
        out->proto = b[1];
        out->kind = b[2];
        out->flags = b[3];
        out->size = get32(b + 4);
        out->ver[0] = get16(b + 8);
        out->ver[1] = get16(b + 10);
        out->ver[2] = get16(b + 12);
        return true;
    case BLEOTA_OP_HASH:
        if (n != BLEOTA_LEN_HASH || b[1] > 1u) {
            return false;
        }
        out->part = b[1];
        memcpy(out->half, b + 2, 16u);
        return true;
    case BLEOTA_OP_END:
    case BLEOTA_OP_ABORT:
    case BLEOTA_OP_POLL:
        return n == BLEOTA_LEN_OP;
    default:
        return false;
    }
}

size_t bleota_status_encode(const bleota_status_t *s, uint8_t *out)
{
    if (s == NULL || out == NULL || s->ev < BLEOTA_EV_WAIT
        || s->ev > BLEOTA_EV_FAIL) {
        return 0u;
    }
    memset(out, 0, BLEOTA_LEN_STATUS);
    out[0] = s->ev;
    out[1] = s->code;
    put32(out + 2, s->next);
    put32(out + 6, s->limit);
    put16(out + 10, s->secs);
    return BLEOTA_LEN_STATUS;
}

bool bleota_status_decode(const uint8_t *b, size_t n, bleota_status_t *out)
{
    if (b == NULL || out == NULL || n != BLEOTA_LEN_STATUS
        || b[0] < BLEOTA_EV_WAIT || b[0] > BLEOTA_EV_FAIL) {
        return false;
    }
    out->ev = b[0];
    out->code = b[1];
    out->next = get32(b + 2);
    out->limit = get32(b + 6);
    out->secs = get16(b + 10);
    if (out->ev == BLEOTA_EV_FAIL && out->code > BLEOTA_E_MAX) {
        return false;
    }
    if (out->ev == BLEOTA_EV_CREDIT && out->code > BLEOTA_CREDIT_GAP) {
        return false;
    }
    return out->ev != BLEOTA_EV_CREDIT || out->limit >= out->next;
}

uint16_t bleota_chunk_bytes(uint16_t mtu)
{
    if (mtu < BLEOTA_MTU_MIN) {
        mtu = BLEOTA_MTU_MIN;
    }
    const uint32_t n = (uint32_t)mtu - 3u - BLEOTA_CHUNK_HDR;
    return (uint16_t)(n > BLEOTA_CHUNK_MAX ? BLEOTA_CHUNK_MAX : n);
}

void bleota_chunk_header(uint32_t off, uint8_t out[BLEOTA_CHUNK_HDR])
{
    put32(out, off);
}

bool bleota_chunk_decode(const uint8_t *b, size_t n, uint32_t *off,
                         const uint8_t **data, uint16_t *len)
{
    if (b == NULL || off == NULL || data == NULL || len == NULL
        || n <= BLEOTA_CHUNK_HDR || n > BLEOTA_CHUNK_HDR + BLEOTA_CHUNK_MAX) {
        return false;
    }
    *off = get32(b);
    *data = b + BLEOTA_CHUNK_HDR;
    *len = (uint16_t)(n - BLEOTA_CHUNK_HDR);
    return true;
}

uint8_t bleota_head_check(const uint8_t *b, size_t n, uint16_t chip,
                          uint8_t kind)
{
    if (b == NULL || n < BLEOTA_HEAD_LEN || b[0] != BLEOTA_IMG_MAGIC
        || b[1] == 0u || get32(b + BLEOTA_HEAD_DESC) != BLEOTA_DESC_MAGIC) {
        return BLEOTA_E_IMAGE;
    }
    if (get16(b + BLEOTA_HEAD_CHIP) != chip) {
        return BLEOTA_E_KIND;
    }
    const uint8_t *r = b + BLEOTA_HEAD_ROLE;
    if (get32(r) != BLEOTA_ROLE_MAGIC || r[5] != BLEOTA_PROTO) {
        return BLEOTA_E_IMAGE;
    }
    return r[4] == kind ? (uint8_t)BLEOTA_OK : (uint8_t)BLEOTA_E_KIND;
}
