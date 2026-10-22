/**
 * @file
 * @brief The CONTROL page's rules.
 *
 * See link_control.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "link_control.h"

#include "link_can.h"
#include "link_pages.h"

/*
 * The frame that arms is ARM, THROTTLE and MOTOR_POLES from offset 0, and it
 * fits one CAN (Controller Area Network) frame only while the three stay
 * contiguous from register 0 and no wider than a frame.  Renumbering them is
 * a protocol major, and this is where the build says so.
 */
_Static_assert(LINK_CT_ARM == 0 && LINK_CT_THROTTLE == 1
               && LINK_CT_MOTOR_POLES == 2,
               "the arming frame is registers 0 to 2 of the control page");
_Static_assert(LINK_CT_ARM_FRAME == LINK_CT_MOTOR_POLES + 1u,
               "LINK_CT_ARM_FRAME ends at MOTOR_POLES");
_Static_assert(LINK_CT_ARM_FRAME <= LINK_CAN_REGS_PER_FRAME,
               "the arming frame must be one CAN frame");
_Static_assert(LINK_CT_CLEAR >= LINK_CT_ARM_FRAME,
               "CLEAR is its own transaction ahead of the arming frame");

/**
 * @brief Test whether a MOTOR_POLES value is one a motor comes in.
 *
 * @param poles The value to test.
 * @return true for 0, and for an even count within LINK_POLES_MIN to
 *       LINK_POLES_MAX.
 */
static bool poles_ok(uint16_t poles)
{
    /* Zero means nobody has said; anything else is an even count in the
     * range a motor comes in.  An odd count is a typo, and accepting one
     * would put a plausible wrong speed on the screen. */
    if (poles == 0u) {
        return true;
    }
    return poles >= LINK_POLES_MIN && poles <= LINK_POLES_MAX
           && (poles % 2u) == 0u;
}

bool link_arm_value_ok(uint16_t arm)
{
    return arm == 0u || arm == LINK_ARM_ALL
           || ((arm & (uint16_t)~LINK_ARM_ROLES) == 0u);
}

uint16_t link_arm_roles(uint16_t arm)
{
    if (!link_arm_value_ok(arm)) {
        return 0u;
    }
    return (arm == LINK_ARM_ALL) ? (uint16_t)LINK_ARM_ROLES : arm;
}

uint8_t link_control_write(uint16_t *regs, uint8_t off, uint8_t n,
                           const uint16_t *in, bool may_arm, bool *cleared)
{
    for (uint8_t i = 0; i < n; ++i) {
        const uint8_t reg = (uint8_t)(off + i);
        if (reg == LINK_CT_THROTTLE && in[i] > LINK_THROTTLE_MAX) {
            return LINK_NACK_BAD_VALUE;
        }
        if (reg == LINK_CT_CLEAR && in[i] != LINK_CLEAR_MAGIC) {
            return LINK_NACK_BAD_VALUE;
        }
        /* A value that names no role, and a change of roles while armed,
         * are refused ahead of the failsafe check: neither is a request to
         * arm that the failsafe could answer, so neither latches it.  The
         * roles change through a disarm, which every output passes through
         * at rest. */
        if (reg == LINK_CT_ARM
            && (!link_arm_value_ok(in[i])
                || (in[i] != 0u && regs[LINK_CT_ARM] != 0u
                    && in[i] != regs[LINK_CT_ARM]))) {
            return LINK_NACK_BAD_VALUE;
        }
        /* Refusing to arm while in failsafe is the IOmcu's decision:
         * the panel is not the authority on whether it is safe here. */
        if (reg == LINK_CT_ARM && in[i] != 0u && !may_arm) {
            return LINK_NACK_NOT_ARMED;
        }
        if (reg == LINK_CT_MOTOR_POLES && !poles_ok(in[i])) {
            return LINK_NACK_BAD_VALUE;
        }
    }
    for (uint8_t i = 0; i < n; ++i) {
        const uint8_t reg = (uint8_t)(off + i);
        if (reg == LINK_CT_CLEAR) {
            *cleared = true;
            continue;
        }
        regs[reg] = in[i];
    }
    return 0u;
}
