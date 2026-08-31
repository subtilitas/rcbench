/*
 * SPDX-License-Identifier: MIT
 */

#include "openyge.h"

#include <string.h>

void openyge_params_reset(openyge_params_t *p)
{
    if (p != NULL) {
        memset(p, 0, sizeof(*p));
    }
}

void openyge_params_observe(openyge_params_t *p, uint16_t index, uint16_t value)
{
    if (p == NULL || index >= OPENYGE_MAX_PARAMS) {
        return;
    }
    /* Nothing is believed while a write is outstanding.  A frame already in
     * flight carries the old value and would look like the ESC refusing. */
    if (p->writes_pending) {
        return;
    }

    p->value[index] = value;
    p->seen |= (uint64_t)1u << index;

    if (index == OPENYGE_P_COUNT) {
        p->count = value;
    }
}

void openyge_params_begin_writes(openyge_params_t *p)
{
    if (p == NULL) {
        return;
    }
    /*
     * The whole table is withdrawn, not just the indices being written.  A
     * table that is partly old and partly new is worse than no table: it reads
     * as the ESC's settings and is not.
     */
    p->writes_pending = true;
    p->seen           = 0;
    p->count          = 0;
}

void openyge_params_end_writes(openyge_params_t *p)
{
    if (p != NULL) {
        p->writes_pending = false;
    }
}

bool openyge_params_complete(const openyge_params_t *p)
{
    if (p == NULL || p->writes_pending) {
        return false;
    }
    /* Zero is "the count has not arrived"; above the bitmap's width is an ESC
     * with more parameters than this cache can hold, and reporting that table
     * complete would report 64 of them as though they were all of them. */
    if (p->count == 0 || p->count > OPENYGE_MAX_PARAMS) {
        return false;
    }
    /* Every index below the count, and the count itself, seen at least once.
     * Built by shifting rather than by (1 << count) - 1, which overflows at
     * exactly the width this bitmap is. */
    uint64_t want = ~0ull;
    if (p->count < OPENYGE_MAX_PARAMS) {
        want = ((uint64_t)1u << p->count) - 1u;
    }
    return (p->seen & want) == want;
}

bool openyge_params_get(const openyge_params_t *p, uint16_t index,
                        uint16_t *out)
{
    if (!openyge_params_complete(p) || index >= p->count) {
        return false;
    }
    if (out != NULL) {
        *out = p->value[index];
    }
    return true;
}

float openyge_params_progress(const openyge_params_t *p)
{
    if (p == NULL || p->writes_pending || p->count == 0
        || p->count > OPENYGE_MAX_PARAMS) {
        return 0.0f;
    }
    unsigned have = 0;
    for (unsigned i = 0; i < p->count; ++i) {
        if (p->seen & ((uint64_t)1u << i)) {
            ++have;
        }
    }
    return (float)have / (float)p->count;
}

bool openyge_motor_rpm(const openyge_params_t *p, uint32_t erpm,
                       float *motor_rpm)
{
    uint16_t poles = 0;
    if (!openyge_params_get(p, OPENYGE_P_MOTOR_POLES, &poles) || poles < 2) {
        return false;
    }
    if (motor_rpm != NULL) {
        *motor_rpm = (float)erpm / ((float)poles / 2.0f);
    }
    return true;
}

bool openyge_head_rpm(const openyge_params_t *p, uint32_t erpm, float *head_rpm)
{
    float motor = 0.0f;
    uint16_t pinion = 0, main_gear = 0;
    if (!openyge_motor_rpm(p, erpm, &motor)) {
        return false;
    }
    if (!openyge_params_get(p, OPENYGE_P_PINION_TEETH, &pinion)
        || !openyge_params_get(p, OPENYGE_P_MAIN_TEETH, &main_gear)
        || pinion == 0 || main_gear == 0) {
        return false;
    }
    if (head_rpm != NULL) {
        *head_rpm = motor * (float)pinion / (float)main_gear;
    }
    return true;
}
