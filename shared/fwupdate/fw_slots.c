/**
 * @file fw_slots.c
 * @brief The IOmcu's two update slots on the RP2350 boot ROM: see
 *        fw_slots.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "fw_slots.h"

#include <stddef.h>

#include "link_pages.h"

fw_slots_part_t fw_slots_partition(int rc, const uint32_t *words,
                                   uint32_t sector_bytes)
{
    fw_slots_part_t p = { false, 0u, 0u };
    if (rc != FW_SLOTS_PT_INFO_WORDS || words == NULL
        || words[0] != FW_SLOTS_PT_INFO_HEADER) {
        return p;
    }
    const uint32_t first = (words[1] & FW_SLOTS_LOC_FIRST_SECTOR_BITS)
                           >> FW_SLOTS_LOC_FIRST_SECTOR_LSB;
    const uint32_t last = (words[1] & FW_SLOTS_LOC_LAST_SECTOR_BITS)
                          >> FW_SLOTS_LOC_LAST_SECTOR_LSB;
    if (last < first) {
        return p;
    }
    p.found = true;
    p.start = first * sector_bytes;
    p.bytes = (last - first + 1u) * sector_bytes;
    return p;
}

void fw_slots_choose(fw_slots_t *s, const fw_slots_layout_t *l,
                     const fw_slots_boot_t *boot)
{
    s->slot = -1;
    s->link_slot = LINK_UPD_SLOT_NONE;
    s->slot_bytes = l->slot_bytes;
    /* The table this build was made for, or none: an image cannot tell a
     * table written for another layout from damage, and writes into
     * neither. */
    const bool table = boot->p0.found && boot->p1.found
                       && boot->p0.start == l->a_off
                       && boot->p1.start == l->b_off
                       && boot->p0.bytes == l->slot_bytes
                       && boot->p1.bytes == l->slot_bytes;
    if (boot->have_info && table
        && (boot->partition == 0 || boot->partition == 1)) {
        s->slot = boot->partition;
        s->link_slot = (uint16_t)boot->partition;
        if ((boot->tbyb & FW_SLOTS_BUY_PENDING) != 0u) {
            s->link_slot = (uint16_t)(s->link_slot | LINK_UPD_SLOT_TRIAL);
        }
    }
    s->running_off = (s->slot == 1) ? l->b_off : l->a_off;
    s->target_off  = (s->slot == 1) ? l->a_off : l->b_off;
}

bool fw_slots_target(const fw_slots_t *s, uint32_t off, uint32_t *addr)
{
    if (off >= s->slot_bytes) {
        return false;
    }
    *addr = s->target_off + off;
    return true;
}

uint32_t fw_slots_update_base(const fw_slots_t *s, uint32_t xip_base)
{
    return xip_base + s->target_off;
}

void fw_slots_buy_begin(fw_slots_buy_t *b)
{
    b->rc = FW_SLOTS_BUY_NOT_RUN;
}

void fw_slots_buy_answer(fw_slots_buy_t *b, int rc)
{
    b->rc = rc;
}

bool fw_slots_buy_taken(const fw_slots_buy_t *b, bool window_ran)
{
    return window_ran && b->rc == 0;
}
