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

/* --- SERVO: the frame rate of every PWM output rendering a surface */

/**
 * Whether the SERVO page may take the frame rate @p hz against the bank @p o
 * as it is: 0, or LINK_NACK_BAD_VALUE for a rate that is neither 0 nor in
 * the PWM driver's range, or one that would leave a PWM slice asked for two
 * rates -- a surface it moves sharing a slice with an output it does not.
 */
uint8_t outputs_servo_rate_check(const outputs_t *o, uint16_t hz);

/**
 * The frame rate each slot of @p o is bound at, into @p rate
 * (OUT_MAX_SLOTS entries): the slot's own, or @p servo_hz for a PWM slot
 * whose first channel is a surface when @p servo_hz is not 0.  A slot that
 * is not PWM keeps its own rate, which for DShot is a bit rate.
 */
void    outputs_slot_rates(const outputs_t *o, uint16_t servo_hz,
                           uint16_t *rate);

/**
 * Whether the bank @p o, given the CHAN_CFG page @p chan_cfg or the OUTPUTS
 * page @p slots, would leave the SERVO page's rate @p servo_hz splitting a
 * PWM slice: 0, or LINK_NACK_BAD_VALUE.  A role that stops being a surface,
 * or a slot bound beside one, moves the rate the slice is asked for; the
 * silicon would refuse whichever bound second and the page would read back
 * a binding no pin has.  Always 0 while @p servo_hz is 0.
 */
uint8_t outputs_chan_cfg_rate_check(const outputs_t *o,
                                    const uint16_t *chan_cfg,
                                    uint16_t servo_hz);
uint8_t outputs_slots_rate_check(const outputs_t *o, const uint16_t *slots,
                                 uint16_t servo_hz);

/* --- CHAN_CFG: what each channel is -- role, slew, and its pulse endpoints */
void    outputs_chan_cfg_defaults(uint16_t *regs);
uint8_t outputs_chan_cfg_write(uint16_t *regs, uint8_t off, uint8_t n,
                               const uint16_t *in);
void    outputs_chan_cfg_apply(outputs_t *o, const uint16_t *regs);

/**
 * Give every channel @p regs marks as a throttle the endpoints @p min_us and
 * @p max_us, and leave every other channel as it is.
 *
 * The bench's two pulse settings are a throttle's: an ESC (electronic speed
 * controller) calibrated on a transmitter takes that transmitter's shortest
 * pulse as zero.  A surface keeps its own range -- the schema's default, or
 * the one the SERVO screen named for the servo on it -- because a surface
 * that took an ESC's range would rest at that range's midpoint, off centre
 * whenever the range is not symmetric about 1500 us.
 *
 * Returns false and changes nothing for endpoints the page would refuse:
 * below LINK_CC_FLOOR_US, above LINK_CC_CEILING_US, or not @p min_us below
 * @p max_us.  A refused write would take the roles down with it.
 */
bool    outputs_chan_cfg_set_throttle_range(uint16_t *regs, uint16_t min_us,
                                            uint16_t max_us);

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
