/*
 * The servo page's rules, in one place.
 *
 * The coprocessor holds the wire and so it enforces these; the panel applies
 * the same clamp for convenience.  Neither owns the logic -- a second copy is
 * a second opinion, and the two would disagree the first time one changed.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * Apply a write to a servo page, in place.
 *
 * @p regs is the whole page.  @p may_drive is the holder's own answer to
 * "is it safe to drive an output right now" -- the coprocessor decides that
 * from its failsafe and heartbeat, and a host does not get a vote.
 *
 * Returns a link_nack_t, or 0 when the write stands.
 */
uint8_t link_servo_write(uint16_t *regs, uint8_t off, uint8_t n,
                         const uint16_t *in, bool may_drive);

/** Put a page into the state a coprocessor should hold before anybody has
 *  configured it: a sane range, centred, not driving. */
void link_servo_defaults(uint16_t *regs);
