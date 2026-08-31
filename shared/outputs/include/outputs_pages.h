/*
 * The link's output pages, expressed as bank operations.
 *
 * The wire carries three pages where the servo page used to be one, because
 * one page per protocol was the alternative.  The mapping between them and the
 * bank has arithmetic and validation in it -- a driver number that must be one
 * this build knows, an endpoint that must be a pulse a servo can take, a range
 * of channels that must fit -- and arithmetic that lives only inside the
 * coprocessor is arithmetic nothing on a desk can check.  So it lives here,
 * host-tested, and the coprocessor is wiring.
 *
 * Each page has three entry points, in the shape link_servo.c had before it:
 *   _defaults  puts the register array into the state a coprocessor holds
 *              before anybody has configured it.
 *   _write     validates a register window and stores it, refusing atomically
 *              -- a rejected write leaves the page as it was.
 *   _apply     derives the bank from the whole stored page.
 *
 * The split matters because the register array is also what a read returns:
 * the panel reads back what it wrote, so the page and the bank are two views
 * that have to be kept from disagreeing.
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
