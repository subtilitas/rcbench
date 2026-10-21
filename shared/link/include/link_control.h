/**
 * @file
 * @brief The CONTROL page as the IOmcu stores it: what a write may carry, and
 * the one side effect a write has.
 *
 * The page's rules are here rather than in the IOmcu's handler so the
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
 * @brief Whether @p arm is a value CONTROL's ARM takes: 0, LINK_ARM_ALL, or a
 * non-empty set of LINK_ARM_ROLES.
 *
 * @param arm The value to test.
 * @return true for 0, for LINK_ARM_ALL, or for a non-empty set of
 *       LINK_ARM_ROLES bits.
 */
bool link_arm_value_ok(uint16_t arm);

/**
 * @brief The roles @p arm arms, as LINK_ARM_ROLES bits: LINK_ARM_ROLES for
 * LINK_ARM_ALL, the value itself for a set of roles, and 0 for 0 and for a
 * value link_arm_value_ok() refuses.
 *
 * @param arm The ARM value.
 * @return LINK_ARM_ROLES for LINK_ARM_ALL, the value itself for a set of roles,
 *         and 0 for 0 and for a value link_arm_value_ok() refuses. The result
 *         is in LINK_ARM_ROLES bits.
 */
uint16_t link_arm_roles(uint16_t arm);

/**
 * @brief Validate a register window of the CONTROL page and store it.
 *
 * @p regs is the page, LINK_CT_COUNT registers.  @p may_arm is false while
 * the link is in failsafe or the heartbeat is not trusted; a non-zero ARM is
 * then refused with LINK_NACK_NOT_ARMED.  An ARM that link_arm_value_ok()
 * refuses, and a non-zero ARM that differs from the non-zero ARM in force,
 * are refused with LINK_NACK_BAD_VALUE ahead of that check.  THROTTLE above LINK_THROTTLE_MAX,
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
 *
 * @param regs The CONTROL page's registers, updated in place.
 * @param off Register offset of the window within the page.
 * @param n Number of registers in the window.
 * @param in The n values the window carries.
 * @param may_arm false while the link is in failsafe or the heartbeat is not
 *       trusted.
 * @param cleared Set true when the window carries CLEAR with its magic.
 * @return 0 when the window is stored, else the NACK reason
 *       (LINK_NACK_BAD_VALUE or LINK_NACK_NOT_ARMED).
 */
uint8_t link_control_write(uint16_t *regs, uint8_t off, uint8_t n,
                           const uint16_t *in, bool may_arm, bool *cleared);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_CONTROL_H */
