/*
 * The CONTROL page's rules.  See link_control.h.
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
        /* Refusing to arm while in failsafe is the coprocessor's decision:
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
