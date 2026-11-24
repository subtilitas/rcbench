/**
 * @file openyge_guard.c
 * @brief The gate before the driver: the allow-list of frame types, the
 *        read-only rules, and every written index and value against the map.
 *
 * SPDX-License-Identifier: MIT
 */
#include "openyge_guard.h"

#include <string.h>

#include "openyge_map.h"

/** @brief Bytes of a request: header, control payload, CRC. */
#define REQ_BYTES (OPENYGE_HDR_V3 + OPENYGE_CONTROL_BYTES + OPENYGE_CRC_BYTES)

/**
 * @brief Little-endian 16-bit field.
 * @param p two bytes, least significant first
 * @return the value
 */
static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

void oy_guard_init(oy_guard_t *g)
{
    if (g == NULL) {
        return;
    }
    const uint16_t keep = g->read_no;

    memset(g, 0, sizeof(*g));
    g->read_no = keep;
}

uint16_t oy_guard_table_crc(const uint16_t *table, uint16_t count)
{
    uint16_t crc = OPENYGE_CRC_INIT;

    for (uint16_t i = 0; table != NULL && i < count; ++i) {
        const uint8_t b[2] = { (uint8_t)(table[i] & 0xFFu),
                               (uint8_t)(table[i] >> 8) };
        crc = openyge_crc(crc, b, sizeof(b));
    }
    return crc;
}

void oy_guard_table(oy_guard_t *g, const uint16_t *table, uint16_t count,
                    bool legacy)
{
    if (g == NULL || table == NULL || count == 0u
        || count > OPENYGE_MAX_PARAMS) {
        return;
    }
    memset(g->orig, 0, sizeof(g->orig));
    memcpy(g->orig, table, (size_t)count * sizeof(table[0]));
    g->count    = count;
    g->read_crc = oy_guard_table_crc(table, count);
    ++g->read_no;
    g->table    = true;
    g->approved = false;

    if (legacy) {
        g->ro = OY_REF_RO_LEGACY;
    } else if (count != OY_MAP_COUNT) {
        g->ro = OY_REF_RO_COUNT;
    } else if (oy_map_u32(table[OY_I_FW_LO], table[OY_I_FW_HI])
               < OY_FW_MIN_WRITE) {
        g->ro = OY_REF_RO_FIRMWARE;
    } else {
        g->ro = OY_REF_NONE;
    }
}

/**
 * @brief Whether the table read last can be written at all.
 * @param g the guard
 * @return OY_REF_NONE, OY_REF_NO_TABLE or the read-only reason
 */
static oy_ref_t gate(const oy_guard_t *g)
{
    if (!g->table) {
        return OY_REF_NO_TABLE;
    }
    return (oy_ref_t)g->ro;
}

/**
 * @brief Judge one index and its new value: the index rows, then the value.
 * @param g     the guard
 * @param index the parameter index
 * @param value the value to write
 * @param flags index 26 of the table being written
 * @return OY_REF_NONE when the value may be written there
 */
static oy_ref_t judge(const oy_guard_t *g, uint16_t index, uint16_t value,
                      uint16_t flags)
{
    if (index >= g->count) {
        return OY_REF_PAST_COUNT;
    }
    if (index == 0u || (index >= OY_I_TYPE && index <= OY_I_SERIAL_HI)) {
        return OY_REF_IDENTITY;
    }
    if (index == OY_I_TELEMETRY) {
        return OY_REF_TELEMETRY;
    }
    if (!oy_map_writable(index)) {
        return OY_REF_NOT_EDITABLE;
    }
    const oy_field_t *f = oy_map_field(index);
    const uint16_t was  = g->orig[index];

    if (f->kind == OY_KIND_FLAGS
        && ((uint16_t)(value ^ was) & (uint16_t)~f->edit_bits) != 0u) {
        return OY_REF_FLAG_BITS;
    }
    if (!oy_map_value_ok(index, value, was, flags)) {
        return OY_REF_VALUE;
    }
    return OY_REF_NONE;
}

oy_ref_t oy_guard_approve(oy_guard_t *g, const uint16_t *draft, size_t n,
                          uint16_t read_no, uint16_t read_crc, uint16_t *bad)
{
    if (g == NULL || draft == NULL) {
        return OY_REF_ARG;
    }
    g->approved = false;
    const oy_ref_t r = gate(g);
    if (r != OY_REF_NONE) {
        return r;
    }
    if (n != g->count) {
        return OY_REF_ARG;
    }
    if (read_no != g->read_no || read_crc != g->read_crc) {
        return OY_REF_OTHER_READ;
    }
    const uint16_t flags = draft[OY_I_FLAGS];

    for (uint16_t i = 0; i < g->count; ++i) {
        if (draft[i] == g->orig[i]) {
            continue;   /* written back as read: never sent */
        }
        const oy_ref_t v = judge(g, i, draft[i], flags);
        if (v != OY_REF_NONE) {
            if (bad != NULL) {
                *bad = i;
            }
            return v;
        }
    }
    memset(g->draft, 0, sizeof(g->draft));
    memcpy(g->draft, draft, n * sizeof(draft[0]));
    g->approved = true;
    return OY_REF_NONE;
}

void oy_guard_done(oy_guard_t *g)
{
    if (g != NULL) {
        g->approved = false;
    }
}

oy_ref_t oy_guard_frame(const oy_guard_t *g, const uint8_t *f, size_t n)
{
    if (g == NULL || f == NULL) {
        return OY_REF_ARG;
    }
    if (n < OPENYGE_HDR_V3 || f[0] != OPENYGE_SYNC
        || f[1] != OPENYGE_VERSION) {
        return OY_REF_FORM;
    }
    if (f[2] != OPENYGE_FT_TELE_REQ && f[2] != OPENYGE_FT_WRITE_PARAM_REQ) {
        return OY_REF_TYPE;
    }
    if (n != REQ_BYTES || f[3] != REQ_BYTES
        || (f[5] & OPENYGE_DEV_MASTER) == 0u
        || openyge_crc(OPENYGE_CRC_INIT, f, n - OPENYGE_CRC_BYTES)
               != rd16(f + n - OPENYGE_CRC_BYTES)) {
        return OY_REF_FORM;
    }
    const uint16_t index = rd16(f + OPENYGE_HDR_V3);
    const uint16_t value = rd16(f + OPENYGE_HDR_V3 + 2u);

    if (f[2] == OPENYGE_FT_TELE_REQ) {
        return (index == 0u && value == 0u) ? OY_REF_NONE : OY_REF_REQUEST;
    }
    const oy_ref_t r = gate(g);
    if (r != OY_REF_NONE) {
        return r;
    }
    if (!g->approved) {
        return OY_REF_NOT_APPROVED;
    }
    const oy_ref_t v = judge(g, index, value, g->draft[OY_I_FLAGS]);
    if (v != OY_REF_NONE) {
        return v;
    }
    if (g->draft[index] != value || value == g->orig[index]) {
        return OY_REF_NOT_APPROVED;
    }
    return OY_REF_NONE;
}
