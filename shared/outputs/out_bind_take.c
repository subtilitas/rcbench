/**
 * @file
 * @brief Moving one pin from the protocol that holds it to another.
 *
 * See out_bind_take.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "out_bind_take.h"

#include <stddef.h>

#include "link_pages.h"

/**
 * @brief How many bits of @p v are set.
 *
 * @param v  A pin set.
 * @return   The number of set bits, 0 to 32.
 */
static uint8_t bits_in(uint32_t v)
{
    uint8_t n = 0u;
    for (; v != 0u; v &= v - 1u) {
        ++n;
    }
    return n;
}

/**
 * @brief The most pins @p p may hold on board @p bd: its own cap, never
 *        more than the page's slots, and on a board with roles never more
 *        than its servo outputs.
 *
 * @param bd  The board.
 * @param p   The protocol entry.
 * @return    The cap, 0 for OFF.
 */
static uint8_t pins_cap(const outbind_board_t *bd, const outbind_proto_t *p)
{
    uint8_t cap = (p->max_pins < OUT_MAX_SLOTS) ? p->max_pins
                                                : (uint8_t)OUT_MAX_SLOTS;
    if (bd->roles != NULL && cap > OUTBIND_FIXED_OUTS) {
        cap = (uint8_t)OUTBIND_FIXED_OUTS;
    }
    return cap;
}

/**
 * @brief The first channel of the slot that drives @p gpio, as
 *        outbind_to_slots() renders @p b.
 *
 * @param b     The binding.
 * @param gpio  The pin.
 * @return      The first channel, or OUTBIND_TAKE_UNSET when no slot
 *              drives @p gpio.
 */
static uint8_t first_channel(const outbind_t *b, uint8_t gpio)
{
    uint16_t regs[LINK_OS_COUNT];
    const uint8_t used = outbind_to_slots(b, regs);
    for (uint8_t slot = 0; slot < used; ++slot) {
        const uint16_t *r = &regs[(size_t)slot * LINK_OS_STRIDE];
        if (r[LINK_OS_PIN] == gpio) {
            return LINK_OS_FIRST(r[LINK_OS_RANGE]);
        }
    }
    return OUTBIND_TAKE_UNSET;
}

/**
 * @brief The programming port whose pin is @p gpio.
 *
 * @param ctx   The ports beside the binding.
 * @param gpio  The pin.
 * @return      The port, or OUTBIND_TAKE_UNSET for none.
 */
static uint8_t port_at(const outbind_take_ctx_t *ctx, uint8_t gpio)
{
    for (uint8_t n = 0; ctx->ports != NULL && n < ctx->port_count; ++n) {
        if (ctx->ports[n] <= OUT_MAX_PIN && ctx->ports[n] == gpio) {
            return n;
        }
    }
    return OUTBIND_TAKE_UNSET;
}

/**
 * @brief The interface that holds @p gpio.
 *
 * @param ctx   The interfaces beside the binding.
 * @param gpio  The pin, 0 to OUT_MAX_PIN.
 * @return      The interface, or OUTBIND_TAKE_UNSET for none.
 */
static uint8_t iface_at(const outbind_take_ctx_t *ctx, uint8_t gpio)
{
    for (uint8_t n = 0; ctx->ifaces != NULL && n < ctx->iface_count
                        && gpio <= OUT_MAX_PIN; ++n) {
        if (((ctx->ifaces[n] >> gpio) & 1u) != 0u) {
            return n;
        }
    }
    return OUTBIND_TAKE_UNSET;
}

/**
 * @brief Every field at its "none" value, the taker named.
 *
 * @param in     The record to clear.
 * @param taker  The protocol asked to take the pin.
 */
static void info_clear(outbind_take_info_t *in, uint8_t taker)
{
    *in = (outbind_take_info_t){
        .gpio = OUTBIND_NO_GPIO,
        .taker = taker,
        .channel = OUTBIND_TAKE_UNSET,
        .role_was = OUT_ROLE_SURFACE,
        .role_now = OUT_ROLE_SURFACE,
        .with = OUTBIND_NO_GPIO,
        .port = OUTBIND_TAKE_UNSET,
        .iface = OUTBIND_TAKE_UNSET,
    };
}

/**
 * @brief The PWM rule a clash names, as a verdict.
 *
 * @param c  The clash from outbind_pwm_clash_ports().
 * @return   The matching verdict, OUTBIND_TAKE_OK for no clash.
 */
static outbind_take_t clash_verdict(outbind_clash_t c)
{
    switch (c) {
    case OUTBIND_CLASH_PROG_SLICE: return OUTBIND_TAKE_CLASH_PROG_SLICE;
    case OUTBIND_CLASH_COMPARE:    return OUTBIND_TAKE_CLASH_COMPARE;
    case OUTBIND_CLASH_OUT_SLICE:  return OUTBIND_TAKE_CLASH_OUT_SLICE;
    case OUTBIND_CLASH_NONE:
    default:                       return OUTBIND_TAKE_OK;
    }
}

outbind_take_t outbind_take_check_on(const outbind_board_t *bd,
                                     const outbind_t *b, uint8_t index,
                                     uint8_t taker,
                                     const outbind_take_ctx_t *ctx,
                                     outbind_t *after,
                                     outbind_take_info_t *info)
{
    outbind_take_info_t scratch;
    outbind_take_info_t *in = (info != NULL) ? info : &scratch;
    info_clear(in, taker);
    if (b == NULL) {
        return OUTBIND_TAKE_INVALID;
    }
    if (after != NULL) {
        *after = *b;          /* nothing moved until the verdict is OK */
    }
    if (bd == NULL || outbind_board(b->board) == NULL || index >= bd->count
        || index >= outbind_pin_count(b->board) || taker >= OUTBIND_PROTOS) {
        return OUTBIND_TAKE_INVALID;
    }
    const outbind_proto_t *protos = outbind_protos();
    const outbind_pin_t *pin = &bd->pins[index];
    const uint8_t loser = outbind_group_of(b, index);
    in->gpio = pin->gpio;
    in->pad = pin->pad;
    in->loser = loser;
    in->role_was = protos[loser].role;
    in->role_now = protos[taker].role;
    in->cap = pins_cap(bd, &protos[taker]);
    in->need = protos[taker].channels;

    if (ctx == NULL || !ctx->read) {
        return OUTBIND_TAKE_UNREAD;
    }
    if (pin->reserved) {
        return OUTBIND_TAKE_RESERVED;
    }
    /* A pin a protocol holds is that protocol's, whatever a port or an
     * interface is set to: the IOmcu gives them no pin a slot drives. */
    if (loser == 0u) {
        in->port = port_at(ctx, pin->gpio);
        if (in->port != OUTBIND_TAKE_UNSET) {
            return OUTBIND_TAKE_PORT;
        }
        in->iface = iface_at(ctx, pin->gpio);
        return (in->iface != OUTBIND_TAKE_UNSET) ? OUTBIND_TAKE_IFACE
                                                 : OUTBIND_TAKE_NOT_HELD;
    }
    if (loser == taker) {
        return OUTBIND_TAKE_SAME;
    }
    if (bd->fixed && bd->roles == NULL) {
        return OUTBIND_TAKE_FIXED;
    }
    if (protos[taker].max_pins == 0u) {
        return OUTBIND_TAKE_OFF;
    }

    /* The pin leaves its holder first: every budget and every PWM rule is
     * the one the taker meets on a pin nobody holds. */
    const uint32_t bit = (uint32_t)1u << index;
    outbind_t moved = *b;
    moved.pins[loser] &= ~bit;
    moved.proto = taker;
    in->keeps = bits_in(moved.pins[loser]);
    const uint8_t used = outbind_channels_used(&moved);
    in->free = (used < LINK_OUT_CHANNELS)
                   ? (uint8_t)(LINK_OUT_CHANNELS - used) : 0u;
    if (outbind_chosen(&moved) >= in->cap) {
        return OUTBIND_TAKE_CAP;
    }
    if (outbind_chosen_total(&moved) >= LINK_OUT_SLOTS) {
        return OUTBIND_TAKE_SLOTS;
    }
    if (in->need > in->free) {
        return OUTBIND_TAKE_CHANNELS;
    }
    const outbind_take_t clash = clash_verdict(outbind_pwm_clash_ports(
        &moved, index, ctx->pwm_ports,
        (ctx->pwm_ports != NULL) ? ctx->port_count : 0u, &in->with));
    if (clash != OUTBIND_TAKE_OK) {
        return clash;
    }

    moved.pins[taker] |= bit;
    in->channel = first_channel(&moved, pin->gpio);
    if (after != NULL) {
        *after = moved;
    }
    return OUTBIND_TAKE_OK;
}

outbind_take_t outbind_take_check(const outbind_t *b, uint8_t index,
                                  uint8_t taker,
                                  const outbind_take_ctx_t *ctx,
                                  outbind_t *after,
                                  outbind_take_info_t *info)
{
    return outbind_take_check_on((b != NULL) ? outbind_board(b->board) : NULL,
                                 b, index, taker, ctx, after, info);
}
