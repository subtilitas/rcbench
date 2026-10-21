/**
 * @file
 * @brief What a record in the IOmcu's output store carries, and how a record an
 *        earlier build wrote is read.
 *
 * firmware/iomcu/src/out_store.c puts the record in flash; this file is the
 * part of it that has no flash in it -- the configuration's layout by
 * record version, and the defaults a field an older record does not carry
 * starts at -- so the host suite holds it.
 *
 * Versions read:
 *
 *   3  the output bindings: OUTPUTS and CHAN_CFG.
 *   4  and the PD mini's wiring, SUPPLY's ENABLE to BAUD (protocol 4.3).
 *   5  and the current monitors' set-up, SENSE's registers 0 to 11
 *      (protocol 4.7).
 *   6  and the phase tap's set-up, TONE's registers 0 to 6 (protocol 4.8).
 *   7  and the programming ports' pin, slew and endpoints, 4 registers for
 *      each of the PROG page's 4 ports (protocol 4.13).  From protocol
 *      4.26 the word before them, spare until then, holds each port's
 *      function (prog_func) with no version change, in version 7 and 8
 *      records alike; 0 is every port's default function (port 0 SERVO
 *      PWM, the others NONE).  An image of 4.13 to 4.25 reads the word as
 *      spare and writes 0 back, and an image older than 4.25 reads no
 *      version 8 record at all and starts with an empty store.  A board
 *      taken to such an image and back has its ports on their default
 *      functions; from 4.25 it keeps their pins, from an older image it
 *      keeps nothing.
 *   8  and the INA3221s' map, SENSE_MAP's registers 0 to 25 and whether
 *      it is in force (protocol 4.25), and 12 per-channel current limits,
 *      0 until a page carries them.  308 bytes: the first version over
 *      one 256-byte flash page.
 *
 * Each version appends to the one before, so a record's configuration is
 * the version before's with fields after it.  A save always writes the
 * newest version.
 *
 * Slots.  Version 8 is written into slots of two flash pages
 * (OUT_STORE_SLOT_BYTES, 512 bytes): 8 a 4 KiB sector, 16 in the store.
 * Versions 3 to 7 lie in slots of one page (OUT_STORE_SLOT_BYTES_V7), 16
 * a sector, and are read from that layout while the store holds no
 * version 8 record.
 *
 * The record.  A header -- magic, the count of 0 bits from the checksum
 * on and its complement, a CRC-16 from the version on, the version and
 * the sequence number -- then the configuration, out_store_cfg_size()
 * bytes.  out_store_rec_check() takes a record only when the zero count
 * holds (out_store_intact()) and the checksum agrees, so a program or an
 * erase cut off part way, in either page, reads as no record.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OUT_STORE_VERSION    8u
#define OUT_STORE_VERSION_V7 7u
#define OUT_STORE_VERSION_V6 6u
#define OUT_STORE_VERSION_V5 5u
#define OUT_STORE_VERSION_V4 4u
#define OUT_STORE_VERSION_V3 3u

/** @brief What is kept: the two pages that describe the outputs; the PD mini's
 *        wiring -- SUPPLY's ENABLE to BAUD -- so a module left on can be
 *        switched off after a restart before any panel has written the page;
 *        and the current monitors' set-up; and the phase tap's set-up; and
 *        where the programming ports are and how their pulse is shaped.
 *
 * The supply's
 *  command is not kept. */
typedef struct {
    uint16_t slots[LINK_OS_COUNT];
    uint16_t chan_cfg[LINK_CC_COUNT];
    uint16_t supply[LINK_SP_OUTPUT];
    uint16_t sense[LINK_SN_CONFIG_COUNT];
    uint16_t tone[LINK_TN_CONFIG_COUNT];
    /** Each port's function (protocol 4.26): bits 4n to 4n + 3 hold 1 +
     *  port n's FUNC, 0 none stored (prog_port_kept_func()).  0 in a
     *  record of version 6, of an image older than 4.26, or written by
     *  one.  Makes the set a whole number of 32-bit words. */
    uint16_t prog_func;
    uint16_t prog[LINK_PG_PORTS_MAX * 4u]; /**< prog_port_kept() */
    uint16_t sense_map[LINK_SM_KEPT];  /**< sense_map_kept()              */
    uint16_t sense_map_set;            /**< 1: the map is in force        */
    uint16_t sense_limit[LINK_SM_CHANNELS]; /**< mA, 0 = none (4.27)      */
    uint16_t spare8;  /**< 0; a whole number of 32-bit words again        */
} out_store_t;

/** @brief The slot a version 8 record is written into, and the one-page slot of
 *  versions 3 to 7. */
#define OUT_STORE_SLOT_BYTES    512u
#define OUT_STORE_SLOT_BYTES_V7 256u

#define OUT_STORE_MAGIC 0x7263626FuL     /* "rcbo" */

/** @brief A record's head; the configuration follows it with no padding. */
typedef struct {
    uint32_t magic;
    uint32_t zeros;           /**< 0 bits from crc onwards, as written */
    uint32_t zeros_inv;       /**< ~zeros; no partial write keeps the pair */
    uint16_t crc;             /**< over every byte from version onwards */
    uint16_t version;
    uint32_t seq;
} out_store_hdr_t;

/** @brief A record as the newest version writes it. */
typedef struct {
    out_store_hdr_t hdr;
    out_store_t     cfg;
} out_store_record_t;

/**
 * @brief How many bytes of configuration a record of @p version carries, from
 *        the start of an out_store_t: 128 for version 3, 136 for 4, 160 for 5,
 *        176 for 6 (two of them the word prog_func), 208 for 7, 288 for 8.  0
 *        for a version this build does not read.
 */
size_t out_store_cfg_size(uint16_t version);

/** @brief A whole record of @p version, head and configuration: 308 bytes for
 *  version 8, 228 for 7.  0 for a version not read. */
size_t out_store_rec_size(uint16_t version);

/**
 * @brief A record of @p version for @p cfg and @p seq into the @p slot bytes at
 * @p rec: the record, then 0xFF to the slot's end, as an erased slot
 * reads, so a program of the whole slot changes nothing past the record.
 * A save writes OUT_STORE_VERSION; an older version is what an earlier
 * build wrote.  False, and nothing written, for a version not read or a
 * record longer than @p slot.
 */
bool out_store_rec_build(uint8_t *rec, size_t slot, uint16_t version,
                         uint32_t seq, const out_store_t *cfg);

/**
 * @brief Whether the @p avail bytes at @p rec start with a whole record of a
 *        version this build reads, from @p min_version to @p max_version:
 *        magic, version, zero count and checksum.
 *
 * On true, its version and sequence number.  A record longer than @p avail is
 * no record.  The two-page layout takes version 8 alone: a version 7 record
 * that happens to start a two-page slot is the layout before's, read with the
 * rest of its slots.
 */
bool out_store_rec_check(const uint8_t *rec, size_t avail,
                         uint16_t min_version, uint16_t max_version,
                         uint16_t *version, uint32_t *seq);

/**
 * @brief A record's configuration, as @p version wrote it at @p cfg
 *        (out_store_cfg_size() bytes), into @p out at the newest layout.
 *
 * What the version does not carry starts as its page does: the supply disabled
 * at the module's 19200 baud, both current monitors off at their defaults
 * (sense_page_defaults()), the phase tap off at its defaults
 * (tone_page_defaults()), programming port 0 on PROG_PORT_DEFAULT_PIN and the
 * other ports without a pin (prog_port_defaults()).
 *
 * False for a version this build does not read, and @p out is untouched.
 */
bool out_store_cfg_read(uint16_t version, const void *cfg, out_store_t *out);

#ifdef __cplusplus
}
#endif
