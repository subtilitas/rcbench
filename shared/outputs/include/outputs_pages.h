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

/* --- CHANNELS: what each output is asked for.  Clamped, not refused, because
 *     a command arrives many times a second from a host that may be mid-drag. */
void    outputs_channels_defaults(uint16_t *regs);
uint8_t outputs_channels_write(uint16_t *regs, uint8_t off, uint8_t n,
                               const uint16_t *in);
void    outputs_channels_apply(outputs_t *o, const uint16_t *regs,
                               uint32_t now_ms);
