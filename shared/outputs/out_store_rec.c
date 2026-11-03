/**
 * @file
 * @brief A store record's configuration by version.
 *
 * See out_store_rec.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "out_store_rec.h"

#include <string.h>

#include "link_crc.h"
#include "out_store_map.h"

#include "prog_port.h"
#include "sense_page.h"
#include "supply_page.h"
#include "tone_page.h"

/** @brief Each version appends: its size is where the next version's field
 * starts. */
_Static_assert(offsetof(out_store_t, supply)
               == (LINK_OS_COUNT + LINK_CC_COUNT) * sizeof(uint16_t),
               "the configuration has no padding before the supply");
_Static_assert(offsetof(out_store_t, sense)
               == offsetof(out_store_t, supply)
                  + LINK_SP_OUTPUT * sizeof(uint16_t),
               "the configuration has no padding before the sense set-up");
_Static_assert(offsetof(out_store_t, tone)
               == offsetof(out_store_t, sense)
                  + LINK_SN_CONFIG_COUNT * sizeof(uint16_t),
               "the configuration has no padding before the tone set-up");

_Static_assert(offsetof(out_store_t, prog)
               == offsetof(out_store_t, tone)
                  + (LINK_TN_CONFIG_COUNT + 1u) * sizeof(uint16_t),
               "the configuration has no padding before the ports");
_Static_assert(sizeof(((out_store_t *)0)->prog)
               == sizeof(uint16_t[PROG_PORT_KEPT_COUNT]),
               "the kept registers of the programming ports");

_Static_assert(offsetof(out_store_t, sense_map)
               == offsetof(out_store_t, prog)
                  + (size_t)LINK_PG_PORTS_MAX * 4u * sizeof(uint16_t),
               "the configuration has no padding before the map");
_Static_assert(sizeof(out_store_t) % 4u == 0u,
               "the configuration is a whole number of 32-bit words");
_Static_assert(sizeof(out_store_hdr_t) == 20u, "the record's head");
_Static_assert(offsetof(out_store_record_t, cfg) == sizeof(out_store_hdr_t)
               && sizeof(out_store_record_t)
                  == sizeof(out_store_hdr_t) + sizeof(out_store_t),
               "the configuration follows the head and ends the record");
_Static_assert(sizeof(out_store_record_t) <= OUT_STORE_SLOT_BYTES,
               "the record has outgrown its two-page slot");
_Static_assert(sizeof(out_store_hdr_t) + offsetof(out_store_t, sense_map)
               <= OUT_STORE_SLOT_BYTES_V7,
               "a version 7 record fits its one-page slot");

size_t out_store_cfg_size(uint16_t version)
{
    switch (version) {
    case OUT_STORE_VERSION:    return sizeof(out_store_t);
    case OUT_STORE_VERSION_V7: return offsetof(out_store_t, sense_map);
    case OUT_STORE_VERSION_V6: return offsetof(out_store_t, prog);
    case OUT_STORE_VERSION_V5: return offsetof(out_store_t, tone);
    case OUT_STORE_VERSION_V4: return offsetof(out_store_t, sense);
    case OUT_STORE_VERSION_V3: return offsetof(out_store_t, supply);
    default:                   return 0u;
    }
}

void out_store_cfg_defaults(out_store_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    supply_page_t supply;
    supply_page_init(&supply);
    memcpy(out->supply, &supply.regs[LINK_SP_ENABLE], sizeof(out->supply));
    sense_page_defaults(out->sense);
    tone_page_defaults(out->tone);
    prog_port_defaults(out->prog, PROG_PORT_DEFAULT_PIN);
}

bool out_store_cfg_read(uint16_t version, const void *cfg, out_store_t *out)
{
    const size_t size = out_store_cfg_size(version);
    if (size == 0u || cfg == NULL || out == NULL) {
        return false;
    }
    /* What an older record does not carry, as its page starts. */
    out_store_cfg_defaults(out);
    /* And over it, everything the record does carry. */
    memcpy(out, cfg, size);
    return true;
}

size_t out_store_rec_size(uint16_t version)
{
    const size_t cfg = out_store_cfg_size(version);
    return (cfg == 0u) ? 0u : sizeof(out_store_hdr_t) + cfg;
}

/**
 * @brief The checksum: from the version to the record's end.
 *
 * @param rec The record, at least @p size bytes.
 * @param size Record size in bytes, from out_store_rec_size().
 * @return CRC-16 over the bytes from the version field to @p size.
 */
static uint16_t rec_crc(const uint8_t *rec, size_t size)
{
    const size_t at = offsetof(out_store_hdr_t, version);
    return link_crc(LINK_CRC_INIT, rec + at, size - at);
}

/**
 * @brief The 0 bits from the checksum to the record's end: everything but the
 *        magic and the count itself.
 *
 * @param rec The record, at least @p size bytes.
 * @param size Record size in bytes, from out_store_rec_size().
 * @return Number of 0 bits in the bytes from the checksum field to @p size.
 */
static uint32_t rec_zeros(const uint8_t *rec, size_t size)
{
    uint32_t zeros = 0u;
    for (size_t i = offsetof(out_store_hdr_t, crc); i < size; ++i) {
        uint8_t v = (uint8_t)~rec[i];
        while (v != 0u) {
            zeros += (uint32_t)(v & 1u);
            v = (uint8_t)(v >> 1);
        }
    }
    return zeros;
}

bool out_store_rec_build(uint8_t *rec, size_t slot, uint16_t version,
                         uint32_t seq, const out_store_t *cfg)
{
    const size_t size = out_store_rec_size(version);
    if (rec == NULL || cfg == NULL || size == 0u || size > slot) {
        return false;
    }
    memset(rec, 0xFF, slot);
    out_store_hdr_t h;
    memset(&h, 0, sizeof(h));
    h.magic   = OUT_STORE_MAGIC;
    h.version = version;
    h.seq     = seq;
    memcpy(rec, &h, sizeof(h));
    memcpy(rec + sizeof(h), cfg, size - sizeof(h));
    h.crc = rec_crc(rec, size);
    memcpy(rec + offsetof(out_store_hdr_t, crc), &h.crc, sizeof(h.crc));
    /* Last, because it counts everything from the checksum on. */
    h.zeros     = rec_zeros(rec, size);
    h.zeros_inv = ~h.zeros;
    memcpy(rec + offsetof(out_store_hdr_t, zeros), &h.zeros, sizeof(h.zeros));
    memcpy(rec + offsetof(out_store_hdr_t, zeros_inv), &h.zeros_inv,
           sizeof(h.zeros_inv));
    return true;
}

bool out_store_rec_check(const uint8_t *rec, size_t avail,
                         uint16_t min_version, uint16_t max_version,
                         uint16_t *version, uint32_t *seq)
{
    if (rec == NULL || avail < sizeof(out_store_hdr_t)) {
        return false;
    }
    out_store_hdr_t h;
    memcpy(&h, rec, sizeof(h));
    const size_t size = (h.version >= min_version && h.version <= max_version)
                            ? out_store_rec_size(h.version) : 0u;
    if (h.magic != OUT_STORE_MAGIC || size == 0u || size > avail
        || !out_store_intact(h.zeros, h.zeros_inv, rec_zeros(rec, size))
        || h.crc != rec_crc(rec, size)) {
        return false;
    }
    if (version != NULL) {
        *version = h.version;
    }
    if (seq != NULL) {
        *seq = h.seq;
    }
    return true;
}
