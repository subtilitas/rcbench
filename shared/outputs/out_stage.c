/*
 * A binding prepared beside the one in force.  See out_stage.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "out_stage.h"

#include <stddef.h>
#include <string.h>

#include "link_crc.h"
#include "link_msg.h"
#include "outputs_pages.h"

void out_stage_init(out_stage_t *st, const uint16_t *cfg,
                    const uint16_t *slots)
{
    if (st == NULL) {
        return;
    }
    if (cfg != NULL) {
        memcpy(st->cfg, cfg, sizeof(st->cfg));
    } else {
        outputs_chan_cfg_defaults(st->cfg);
    }
    if (slots != NULL) {
        memcpy(st->slots, slots, sizeof(st->slots));
    } else {
        outputs_slots_defaults(st->slots);
    }
}

uint8_t out_stage_cfg_write(out_stage_t *st, uint8_t off, uint8_t n,
                            const uint16_t *in)
{
    if (st == NULL) {
        return LINK_NACK_BAD_RANGE;
    }
    return outputs_chan_cfg_write(st->cfg, off, n, in);
}

uint8_t out_stage_slots_write(out_stage_t *st, uint8_t off, uint8_t n,
                              const uint16_t *in)
{
    if (st == NULL) {
        return LINK_NACK_BAD_RANGE;
    }
    return outputs_slots_write(st->slots, off, n, in);
}

static void read_window(const uint16_t *regs, unsigned count, uint8_t off,
                        uint8_t n, uint16_t *out)
{
    for (uint8_t i = 0; i < n; ++i) {
        const unsigned at = (unsigned)off + i;
        out[i] = (at < count) ? regs[at] : 0u;
    }
}

void out_stage_cfg_read(const out_stage_t *st, uint8_t off, uint8_t n,
                        uint16_t *out)
{
    if (st != NULL && out != NULL) {
        read_window(st->cfg, LINK_CC_COUNT, off, n, out);
    }
}

void out_stage_slots_read(const out_stage_t *st, uint8_t off, uint8_t n,
                          uint16_t *out)
{
    if (st != NULL && out != NULL) {
        read_window(st->slots, LINK_OS_COUNT, off, n, out);
    }
}

/* Byte by byte, so the value does not depend on either processor's order. */
static uint16_t crc_regs(uint16_t crc, const uint16_t *regs, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t b[2] = { (uint8_t)(regs[i] & 0xFFu),
                               (uint8_t)(regs[i] >> 8) };
        crc = link_crc(crc, b, sizeof(b));
    }
    return crc;
}

uint16_t out_stage_crc(const uint16_t *cfg, const uint16_t *slots)
{
    if (cfg == NULL || slots == NULL) {
        return 0u;
    }
    uint16_t crc = 0xFFFFu;
    crc = crc_regs(crc, cfg, LINK_CC_COUNT);
    return crc_regs(crc, slots, LINK_OS_COUNT);
}

uint16_t out_stage_held_crc(const out_stage_t *st)
{
    return (st != NULL) ? out_stage_crc(st->cfg, st->slots) : 0u;
}

uint8_t out_stage_commit(const out_stage_t *st, uint16_t crc,
                         const uint16_t *cfg_in_force,
                         const out_stage_ops_t *ops, void *ctx)
{
    if (st == NULL || cfg_in_force == NULL || ops == NULL
        || ops->take_cfg == NULL || ops->take_slots == NULL
        || ops->put_cfg == NULL) {
        return LINK_NACK_BAD_VALUE;
    }
    /* What is prepared is what the host meant only if it names the same
     * check: a frame that never arrived, or pages prepared before a restart,
     * leave something else here. */
    if (crc != out_stage_held_crc(st)) {
        return LINK_NACK_BAD_VALUE;
    }
    /* Kept here, because take_cfg overwrites the page @p cfg_in_force
     * points at. */
    uint16_t prev[LINK_CC_COUNT];
    memcpy(prev, cfg_in_force, sizeof(prev));

    /* CHAN_CFG first: it says what a channel is, and a slot that starts
     * rendering a channel whose role has not arrived rests it wrongly. */
    uint8_t nack = ops->take_cfg(ctx, st->cfg);
    if (nack != 0u) {
        return nack;
    }
    nack = ops->take_slots(ctx, st->slots);
    if (nack != 0u) {
        ops->put_cfg(ctx, prev);
        return nack;
    }
    if (ops->keep != NULL) {
        ops->keep(ctx);
    }
    return 0u;
}
