/*
 * What a record in the coprocessor's output store carries, and how a record
 * an earlier build wrote is read.
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
 *
 * Each version appends to the one before, so a record's configuration is
 * the version before's with fields after it.  A save always writes the
 * newest version.
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

#define OUT_STORE_VERSION    6u
#define OUT_STORE_VERSION_V5 5u
#define OUT_STORE_VERSION_V4 4u
#define OUT_STORE_VERSION_V3 3u

/** What is kept: the two pages that describe the outputs; the PD mini's
 *  wiring -- SUPPLY's ENABLE to BAUD -- so a module left on can be switched
 *  off after a restart before any panel has written the page; and the
 *  current monitors' set-up; and the phase tap's set-up.  The supply's
 *  command is not kept. */
typedef struct {
    uint16_t slots[LINK_OS_COUNT];
    uint16_t chan_cfg[LINK_CC_COUNT];
    uint16_t supply[LINK_SP_OUTPUT];
    uint16_t sense[LINK_SN_CONFIG_COUNT];
    uint16_t tone[LINK_TN_CONFIG_COUNT];
    uint16_t spare;   /**< 0; makes the set a whole number of 32-bit words */
} out_store_t;

/**
 * How many bytes of configuration a record of @p version carries, from the
 * start of an out_store_t: 128 for version 3, 136 for 4, 160 for 5, 176 for
 * 6 (two of them spare).  0 for a version this build does not read.
 */
size_t out_store_cfg_size(uint16_t version);

/**
 * A record's configuration, as @p version wrote it at @p cfg
 * (out_store_cfg_size() bytes), into @p out at the newest layout.  What the
 * version does not carry starts as its page does: the supply disabled at
 * the module's 19200 baud, both current monitors off at their defaults
 * (sense_page_defaults()), the phase tap off at its defaults
 * (tone_page_defaults()).
 *
 * False for a version this build does not read, and @p out is untouched.
 */
bool out_store_cfg_read(uint16_t version, const void *cfg, out_store_t *out);

#ifdef __cplusplus
}
#endif
