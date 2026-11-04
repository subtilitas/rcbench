/**
 * @file
 * @brief The limit run's servo families: the register names the report gives,
 *        the Hitec pulse model, the reason a read with no servo of the family
 *        ends on, and the configurations a family takes.  See servo_limrun.h,
 *        "Servo families".
 *
 * Hitec, KST and Chaservo are names or trademarks of their owners; they are
 * used here only to say which servos the run works with.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_limrun.h"

#include "servo_limrun_int.h"

const char *servo_limrun_reg_name(uint8_t family, uint8_t reg)
{
    const unsigned side = reg & 1u;
    const bool ccw = (reg & SERVO_LIMRUN_REG_CCW) != 0u;

    switch (family) {
    case SERVO_LIMRUN_FAM_HITEC_D:
        /* A D series servo counts the same way in both directions. */
        return side == SERVO_LIMRUN_LEFT ? "LEFT END (0xB2)"
                                         : "RIGHT END (0xB0)";
    case SERVO_LIMRUN_FAM_HITEC_C:
        /* CW: left 0x14, right 0x13; CCW swaps them (hitec_reg.c). */
        if ((side == SERVO_LIMRUN_LEFT) != ccw) {
            return side == SERVO_LIMRUN_LEFT ? "LEFT END (CELL 0x14)"
                                             : "RIGHT END (CELL 0x14)";
        }
        return side == SERVO_LIMRUN_LEFT ? "LEFT END (CELL 0x13)"
                                         : "RIGHT END (CELL 0x13)";
    default:
        return side == SERVO_LIMRUN_LEFT ? "LEFT ANGLE" : "RIGHT ANGLE";
    }
}

bool servo_limrun_family_pulses(uint8_t family, uint16_t pulse_dus[3])
{
    uint16_t lo;
    uint16_t hi;

    if (family == SERVO_LIMRUN_FAM_HITEC_D) {
        lo = SERVO_LIMRUN_HITEC_D_LOWER_DUS;
        hi = SERVO_LIMRUN_HITEC_D_UPPER_DUS;
    } else if (family == SERVO_LIMRUN_FAM_HITEC_C) {
        lo = SERVO_LIMRUN_HITEC_C_LOWER_DUS;
        hi = SERVO_LIMRUN_HITEC_C_UPPER_DUS;
    } else {
        return false;
    }
    pulse_dus[SERVO_LIMRUN_P_NEUTRAL] = SERVO_LIMRUN_HITEC_NEUTRAL_DUS;
    pulse_dus[SERVO_LIMRUN_P_LOWER] = lo;
    pulse_dus[SERVO_LIMRUN_P_UPPER] = hi;
    return true;
}

servo_limrun_why_t limrun_not_servo(uint8_t family)
{
    return family == SERVO_LIMRUN_FAM_KST ? SERVO_LIMRUN_WHY_NOT_KST
                                          : SERVO_LIMRUN_WHY_NOT_HITEC;
}

bool limrun_family_ok(const servo_limrun_cfg_t *cfg)
{
    /* SERVO PULSES writes PULSE LOWER and PULSE UPPER: registers only a KST
     * or Chaservo servo has. */
    return cfg->family < (uint8_t)SERVO_LIMRUN_FAM_COUNT
           && (!cfg->set_pulses || cfg->family == SERVO_LIMRUN_FAM_KST);
}
