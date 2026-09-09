/*
 * The link's three output pages (CHAN_CFG, OUTPUTS, CHANNELS), expressed as
 * bank operations.
 *
 * The mapping validates a driver number this build knows, an endpoint a
 * servo can take and a channel range that fits.  It is host-tested, and the
 * coprocessor is wiring.
 *
 * Each page has three entry points:
 *   _defaults  puts the register array into the state a coprocessor holds
 *              before anybody has configured it.
 *   _write     validates a register window and stores it, refusing
 *              atomically: a rejected write leaves the page as it was.
 *   _apply     derives the bank from the whole stored page.
 *
 * The register array is also what a read returns, so the page and the bank
 * are two views kept from disagreeing.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#include "outputs.h"

/**
 * A bank driver as the wire numbers it.
 *
 * The wire's numbers are a contract and the bank's enum is not, so the two
 * are mapped rather than cast in both directions.  Returns LINK_DRIVER_NONE
 * for anything this build does not know.
 */
uint16_t link_driver_of(out_driver_t d);

/* --- CHAN_CFG: what each channel is -- role, slew, and its pulse endpoints */
void    outputs_chan_cfg_defaults(uint16_t *regs);
uint8_t outputs_chan_cfg_write(uint16_t *regs, uint8_t off, uint8_t n,
                               const uint16_t *in);
void    outputs_chan_cfg_apply(outputs_t *o, const uint16_t *regs);

/* --- OUTPUTS: which driver renders which channels, on which pin, how often */
void    outputs_slots_defaults(uint16_t *regs);
uint8_t outputs_slots_write(uint16_t *regs, uint8_t off, uint8_t n,
                            const uint16_t *in);
void    outputs_slots_apply(outputs_t *o, const uint16_t *regs);

/**
 * The channels these two pages render and mark with @p role, as a mask.
 *
 * Bit n is channel n.  Read per channel from the pages themselves, because a
 * binding cannot answer this: outbind_from_slots() reads a slot's role from
 * its first channel and lets the rest differ, so a multi-channel slot whose
 * CHAN_CFG roles disagree collapses to one of them.  A screen that commands a
 * role has to reach exactly the channels that carry it on the wire, and a
 * servo horn reaching a channel bound as a throttle is a motor commanded to
 * where a surface rests.
 *
 * A channel no slot renders is never in the mask, so a command sent to what
 * this returns always reaches a pin.  Either page NULL returns zero, which is
 * a caller that knows nothing rather than one that assumes.
 *
 * One bit per channel over LINK_OUT_CHANNELS channels, which is 8.
 */
uint8_t outputs_role_channels(const uint16_t *slots, const uint16_t *chan_cfg,
                              out_role_t role);

/* --- CHANNELS: what each output is asked for.  Clamped, not refused, because
 *     a command arrives many times a second from a host that may be mid-drag. */
void    outputs_channels_defaults(uint16_t *regs);
/*
 * The page as a mirror of the bank, for the two moments the bank is the
 * authority and the page is not: at boot, and after a failsafe.  Zero is not
 * the answer at either -- zero is a throttle's rest and a surface's low
 * endpoint, so a zero-filled page applied as commands asks every surface for
 * its endpoint.  Filling the page from the bank leaves a channel nobody has
 * commanded reading back at its role's rest, which is where it sits.
 */
void    outputs_channels_from_bank(const outputs_t *o, uint16_t *regs);
uint8_t outputs_channels_write(uint16_t *regs, uint8_t off, uint8_t n,
                               const uint16_t *in);
void    outputs_channels_apply(outputs_t *o, const uint16_t *regs,
                               uint32_t now_ms);
