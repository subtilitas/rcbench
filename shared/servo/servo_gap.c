/**
 * @file
 * @brief A measurement sensor's dropouts during a run.
 *
 * See servo_gap.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_gap.h"

#include <stddef.h>
#include <string.h>

void servo_gap_init(servo_gap_t *g)
{
    if (g != NULL) {
        memset(g, 0, sizeof(*g));
    }
}

/**
 * @brief The open dropout's length at @p at_ms, kept as the longest where it
 *        is.
 *
 * @param g     The state, a dropout open.
 * @param at_ms The time, ms.
 * @return The length, ms; 0 for a time before its start.
 */
static uint32_t length_at(servo_gap_t *g, uint32_t at_ms)
{
    const int32_t d = (int32_t)(at_ms - g->from_ms);
    const uint32_t ms = (d > 0) ? (uint32_t)d : 0u;
    if (ms > g->longest_ms) {
        g->longest_ms = ms;
    }
    return ms;
}

servo_gap_verdict_t servo_gap_lost(servo_gap_t *g, uint32_t at_ms)
{
    if (g == NULL || g->open) {
        return SERVO_GAP_RIDES;
    }
    const bool soon = g->had
                      && (uint32_t)(at_ms - g->last_from_ms)
                             < SERVO_GAP_EVERY_MS;
    g->open         = true;
    g->from_ms      = at_ms;
    g->had          = true;
    g->last_from_ms = at_ms;
    if (g->count < UINT16_MAX) {
        ++g->count;
    }
    return soon ? SERVO_GAP_TOO_SOON : SERVO_GAP_RIDES;
}

servo_gap_verdict_t servo_gap_back(servo_gap_t *g, uint32_t at_ms)
{
    if (g == NULL || !g->open) {
        return SERVO_GAP_RIDES;
    }
    g->open = false;
    return (length_at(g, at_ms) > SERVO_GAP_MAX_MS) ? SERVO_GAP_TOO_LONG
                                                    : SERVO_GAP_RIDES;
}

servo_gap_verdict_t servo_gap_check(servo_gap_t *g, uint32_t now_ms)
{
    if (g == NULL || !g->open) {
        return SERVO_GAP_RIDES;
    }
    return (length_at(g, now_ms) > SERVO_GAP_MAX_MS) ? SERVO_GAP_TOO_LONG
                                                     : SERVO_GAP_RIDES;
}
