/*
 * The link's pages, expressed as outputs.
 *
 * This is the same move link_servo.c made, for the same reason: the mapping
 * has arithmetic in it -- a pulse in microseconds becoming a proportion of
 * travel, a slew in microseconds per second becoming a proportion per second
 * -- and arithmetic that only exists inside the coprocessor is arithmetic
 * nothing on a desk can check.  Here it is host-tested, and the coprocessor
 * is wiring.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#include "outputs.h"

/**
 * Apply the servo page to a channel and its slot.
 *
 * Enable is not arming.  Arming belongs to the bank and the holder of the
 * wire decides it; what enable says is whether this output exists at all, so
 * it configures the slot or clears it.
 */
void outputs_apply_servo_page(outputs_t *o, const uint16_t *page,
                              uint8_t ch, uint8_t slot, uint8_t pin,
                              uint32_t now_ms);

/** Apply the control page's throttle to a channel. */
void outputs_apply_control_page(outputs_t *o, const uint16_t *page,
                                uint8_t ch, uint32_t now_ms);
