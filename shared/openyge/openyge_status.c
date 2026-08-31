/*
 * SPDX-License-Identifier: MIT
 */

#include "openyge.h"

#include <string.h>

#define STATE_MASK      0x0Fu
#define WARN_MASK       0xF0u

#define WARN_UNDERVOLT  0x10u
#define WARN_OVERTEMP   0x20u
#define WARN_OVERCURR   0x40u
#define WARN_DEV_BEC    0x80u
#define WARN_SETPOINT   0xC0u   /* the overloaded combination */

const char *openyge_state_name(uint8_t state)
{
    switch (state & STATE_MASK) {
    case OPENYGE_ST_DISARMED:          return "DISARMED";
    case OPENYGE_ST_POWER_CUT:         return "POWER CUT";
    case OPENYGE_ST_FAST_START:        return "BAILOUT";
    case OPENYGE_ST_ALIGN_FOR_POS:     return "ALIGNING";
    case OPENYGE_ST_BRAKING_NORM_FINI: return "BRAKED";
    case OPENYGE_ST_BRAKING_SYNC_FINI: return "BRAKED SYNC";
    case OPENYGE_ST_STARTING:          return "STARTING";
    case OPENYGE_ST_BRAKING_NORM:      return "BRAKING";
    case OPENYGE_ST_BRAKING_SYNC:      return "BRAKING SYNC";
    case OPENYGE_ST_WINDMILLING:       return "WINDMILLING";
    case OPENYGE_ST_RUNNING_NORM:      return "RUNNING";
    default:                           return "?";
    }
}

void openyge_status_decode(uint8_t status1, openyge_status_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));

    const uint8_t state = status1 & STATE_MASK;
    const uint8_t warn  = status1 & WARN_MASK;

    out->state       = state;
    out->state_known = (openyge_state_name(state)[0] != '?');

    /*
     * The overloaded combination is tested first and returns.  Getting this
     * order wrong reads a noisy servo lead as a BEC over-current fault, which
     * sends somebody looking for a short that is not there.
     */
    if (warn == WARN_SETPOINT) {
        out->setpoint_noise = true;
        return;
    }

    out->subject = (warn & WARN_DEV_BEC) ? OPENYGE_SUBJECT_BEC
                                         : OPENYGE_SUBJECT_ESC;
    out->warn_undervoltage = (warn & WARN_UNDERVOLT) != 0;
    out->warn_overtemp     = (warn & WARN_OVERTEMP) != 0;
    out->warn_overcurrent  = (warn & WARN_OVERCURR) != 0;

    /* Now qualify them by the state.  A caution while running is a fault once
     * the power has been cut, and over-voltage is the case with no flag. */
    const bool cut     = (state == OPENYGE_ST_POWER_CUT);
    const bool no_warn = (warn & (WARN_UNDERVOLT | WARN_OVERTEMP
                                  | WARN_OVERCURR)) == 0;

    out->fault_overvoltage  = cut && no_warn;
    out->fault_undervoltage = out->warn_undervoltage
                              && state < OPENYGE_ST_STARTING;
    out->fault_overtemp     = out->warn_overtemp && cut;
    out->fault_overcurrent  = out->warn_overcurrent && cut;

    out->any_fault = out->fault_overvoltage || out->fault_undervoltage
                     || out->fault_overtemp || out->fault_overcurrent;
}
