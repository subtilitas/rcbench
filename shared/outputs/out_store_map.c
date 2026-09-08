/*
 * Where the next copy of the output binding goes.  See out_store_map.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "out_store_map.h"

#include <stddef.h>

/* A geometry this file can index: slots are addressed by uint8_t, and the
 * caller's plan carries them as one. */
static bool usable(const out_store_rec_t *recs, uint8_t sectors,
                   uint8_t per_sector)
{
    return recs != NULL && sectors > 0u && per_sector > 0u
           && (uint16_t)sectors * (uint16_t)per_sector <= 255u;
}

static bool sector_erased(const out_store_rec_t *recs, uint8_t sector,
                          uint8_t per_sector)
{
    const uint16_t base = (uint16_t)sector * per_sector;
    for (uint16_t i = 0; i < per_sector; ++i) {
        if (!recs[base + i].erased) {
            return false;
        }
    }
    return true;
}

int out_store_map_newest(const out_store_rec_t *recs, uint8_t sectors,
                         uint8_t per_sector)
{
    if (!usable(recs, sectors, per_sector)) {
        return -1;
    }
    const uint16_t n = (uint16_t)sectors * per_sector;
    int best = -1;
    for (uint16_t i = 0; i < n; ++i) {
        if (!recs[i].valid) {
            continue;
        }
        if (best < 0 || recs[i].seq > recs[best].seq) {
            best = (int)i;
        }
    }
    return best;
}

bool out_store_map_write(const out_store_rec_t *recs, uint8_t sectors,
                         uint8_t per_sector, out_store_write_t *out)
{
    if (out == NULL || !usable(recs, sectors, per_sector)) {
        return false;
    }
    out->at           = 0u;
    out->seq          = 1u;
    out->erase_first  = false;
    out->erase_sector = 0u;

    const int newest = out_store_map_newest(recs, sectors, per_sector);
    if (newest < 0) {
        /*
         * Nothing to follow, so anywhere erased will do.  Taking the first
         * one rather than erasing a sector keeps a store whose only record is
         * one this build cannot read -- a record from an earlier version --
         * from costing an erase to replace.
         */
        const uint16_t n = (uint16_t)sectors * per_sector;
        for (uint16_t i = 0; i < n; ++i) {
            if (recs[i].erased) {
                out->at = (uint8_t)i;
                return true;
            }
        }
        out->erase_first  = true;
        out->erase_sector = 0u;
        out->at           = 0u;
        return true;
    }

    out->seq = recs[newest].seq + 1u;
    const uint8_t live = (uint8_t)((uint16_t)newest / per_sector);

    /* The rest of the live record's own sector first: no erase at all, and
     * the record that is being replaced stays readable beside its successor
     * until the sector is left behind. */
    const uint16_t end = (uint16_t)(((uint16_t)live + 1u) * per_sector);
    for (uint16_t i = (uint16_t)newest + 1u; i < end; ++i) {
        if (recs[i].erased) {
            out->at = (uint8_t)i;
            return true;
        }
    }

    /* Then a sector that is already erased, which is the one out_store_map_
     * stale() has had erased ahead of this moment. */
    for (uint8_t k = 1u; k < sectors; ++k) {
        const uint8_t s = (uint8_t)(((uint16_t)live + k) % sectors);
        if (sector_erased(recs, s, per_sector)) {
            out->at = (uint8_t)((uint16_t)s * per_sector);
            return true;
        }
    }

    /*
     * Otherwise the next sector, erased here.  The live record is in another
     * sector and survives that erase -- except with one sector configured,
     * where there is no other sector and the erase takes it.
     */
    const uint8_t s = (uint8_t)(((uint16_t)live + 1u) % sectors);
    out->erase_first  = true;
    out->erase_sector = s;
    out->at           = (uint8_t)((uint16_t)s * per_sector);
    return true;
}

int out_store_map_stale(const out_store_rec_t *recs, uint8_t sectors,
                        uint8_t per_sector)
{
    if (!usable(recs, sectors, per_sector)) {
        return -1;
    }
    const int newest = out_store_map_newest(recs, sectors, per_sector);
    const int live = (newest < 0) ? -1 : (int)((uint16_t)newest / per_sector);
    for (uint8_t s = 0; s < sectors; ++s) {
        if ((int)s == live) {
            continue;
        }
        if (!sector_erased(recs, s, per_sector)) {
            return (int)s;
        }
    }
    return -1;
}
