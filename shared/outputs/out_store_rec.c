/*
 * A store record's configuration by version.  See out_store_rec.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "out_store_rec.h"

#include <string.h>

#include "sense_page.h"
#include "supply_page.h"
#include "tone_page.h"

/* Each version appends: its size is where the next version's field
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

size_t out_store_cfg_size(uint16_t version)
{
    switch (version) {
    case OUT_STORE_VERSION:    return sizeof(out_store_t);
    case OUT_STORE_VERSION_V5: return offsetof(out_store_t, tone);
    case OUT_STORE_VERSION_V4: return offsetof(out_store_t, sense);
    case OUT_STORE_VERSION_V3: return offsetof(out_store_t, supply);
    default:                   return 0u;
    }
}

bool out_store_cfg_read(uint16_t version, const void *cfg, out_store_t *out)
{
    const size_t size = out_store_cfg_size(version);
    if (size == 0u || cfg == NULL || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    /* What an older record does not carry, as its page starts. */
    supply_page_t supply;
    supply_page_init(&supply);
    memcpy(out->supply, &supply.regs[LINK_SP_ENABLE], sizeof(out->supply));
    sense_page_defaults(out->sense);
    tone_page_defaults(out->tone);
    /* And over it, everything the record does carry. */
    memcpy(out, cfg, size);
    return true;
}
