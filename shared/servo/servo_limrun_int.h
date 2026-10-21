/**
 * @file
 * @brief What servo_limrun.c and servo_limrun_report.c share and nothing else
 *        reads: where a side stands, a side's span, and whether RESTORE is
 *        offered.  The two predicates are inline here so that the report, in
 *        its own file, inlines them; the public servo_limrun_span() and
 *        servo_limrun_restore() wrap them.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "servo_limrun.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/** @brief Where a side stands. */
typedef enum {
    ST_NONE = 0,      /**< not bound in the first search: nothing to write */
    ST_OVERSET,       /**< the overset is to be written                    */
    ST_OVERSET_SEARCH,/**< written; the side's search follows              */
    ST_FINAL,         /**< the final value is to be written                */
    ST_DONE,          /**< the final value is written                      */
} limrun_stage_t;

/**
 * @brief Distance from NEUTRAL to a side pulse limit, 0.1 us, from the
 *        pulses given. servo_limrun_span() wraps this.
 *
 * @param sv   Servo pulses, 0.1 us.
 * @param side SERVO_LIMRUN_LO or SERVO_LIMRUN_HI.
 * @return distance from NEUTRAL to the side pulse limit, 0.1 us.
 */
static inline int32_t limrun_span(const servo_limrun_servo_t *sv,
                                  uint8_t side)
{
    const int32_t n = (int32_t)sv->pulse_dus[SERVO_LIMRUN_P_NEUTRAL];
    return side == SERVO_LIMRUN_HI
               ? (int32_t)sv->pulse_dus[SERVO_LIMRUN_P_UPPER] - n
               : n - (int32_t)sv->pulse_dus[SERVO_LIMRUN_P_LOWER];
}

/**
 * @brief Whether RESTORE is offered: the run is DONE and programmed, and ended
 *        as FAIL or ABORTED. servo_limrun_restore() wraps this.
 *
 * @param r Run state. NULL gives false.
 * @return true when the run is DONE, programmed, and ended as FAIL or ABORTED.
 */
static inline bool limrun_restore(const servo_limrun_t *r)
{
    return r != NULL && r->state == SERVO_LIMRUN_DONE && r->programmed
        && (r->verdict == SERVO_LIMRUN_FAIL
            || r->verdict == SERVO_LIMRUN_ABORTED);
}

/**
 * @brief The span from the pulses in force: after SERVO PULSES at 1000-2000
 *        those written, not those read.
 *
 * @param r    Run state holding pulse_now.
 * @param side SERVO_LIMRUN_LO or SERVO_LIMRUN_HI.
 * @return distance from NEUTRAL to the side pulse limit, 0.1 us.
 */
static inline int32_t span_now(const servo_limrun_t *r, uint8_t side)
{
    servo_limrun_servo_t sv;
    memset(&sv, 0, sizeof(sv));
    memcpy(sv.pulse_dus, r->pulse_now, sizeof(sv.pulse_dus));
    return limrun_span(&sv, side);
}
