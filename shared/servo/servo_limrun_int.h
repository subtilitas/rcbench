/*
 * What servo_limrun.c and servo_limrun_report.c share and nothing else
 * reads: where a side stands, and a side's span from the pulses in force.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "servo_limrun.h"

#include <stdint.h>
#include <string.h>

/* Where a side stands. */
typedef enum {
    ST_NONE = 0,      /* not bound in the first search: nothing to write */
    ST_OVERSET,       /* the overset is to be written                    */
    ST_OVERSET_SEARCH,/* written; the side's search follows              */
    ST_FINAL,         /* the final value is to be written                */
    ST_DONE,          /* the final value is written                      */
} stage_t;

/* The span from the pulses in force: after SERVO PULSES at 1000-2000
 * those written, not those read. */
static inline int32_t span_now(const servo_limrun_t *r, uint8_t side)
{
    servo_limrun_servo_t sv;
    memset(&sv, 0, sizeof(sv));
    memcpy(sv.pulse_dus, r->pulse_now, sizeof(sv.pulse_dus));
    return servo_limrun_span(&sv, side);
}
