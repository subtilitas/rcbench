/*
 * The CONTROL page as the coprocessor stores it: what a write may carry, and
 * the one side effect a write has.
 *
 * The page's rules are here rather than in the coprocessor's handler so the
 * host suite runs them.  The handler is wiring: it answers the question of
 * whether the bench may arm, hands the frame in, and acts on the clear.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_LINK_CONTROL_H
#define RCBENCH_LINK_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Validate a register window of the CONTROL page and store it.
 *
 * @p regs is the page, LINK_CT_COUNT registers.  @p may_arm is false while
 * the link is in failsafe or the heartbeat is not trusted; a non-zero ARM is
 * then refused with LINK_NACK_NOT_ARMED.  THROTTLE above LINK_THROTTLE_MAX,
 * MOTOR_POLES that is odd or outside LINK_POLES_MIN..LINK_POLES_MAX and not
 * zero, and CLEAR without LINK_CLEAR_MAGIC are refused with
 * LINK_NACK_BAD_VALUE.
 *
 * Every register of the window is checked before any of them is stored, so a
 * refusal leaves the page as it was and runs no side effect.  A frame carries
 * up to four registers, and a pass that stored as it went would commit the
 * ones ahead of the refusal while answering NACK; ARM sits ahead of
 * MOTOR_POLES, so that would be a bench armed on a refused write.
 *
 * CLEAR is an action and not a setting: it is never stored, and reads back
 * as zero.  @p cleared is set true when the window carried it with the
 * magic, and is otherwise left as it was; the caller lifts the failsafe.
 *
 * Returns 0 when the window is stored, else the NACK reason.
 */
uint8_t link_control_write(uint16_t *regs, uint8_t off, uint8_t n,
                           const uint16_t *in, bool may_arm, bool *cleared);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_CONTROL_H */
