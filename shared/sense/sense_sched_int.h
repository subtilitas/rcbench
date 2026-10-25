/**
 * @file
 * @brief The schedule's parts shared between its two files: sense_sched.c reads
 * the bus, sense_sched_cap.c runs the move capture and FAST's history on what
 * it read.
 *
 * Not for any other module.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "sense_sched.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The capture's step: FAST's sample of this tick, or NULL with none, at
 * @p at_t (0.1ms).
 *
 * Then the sample into the history.
 *
 * @param s The schedule whose capture and history are updated.
 * @param v FAST's sample of this tick, or NULL when there is none.
 * @param at_t Time of the step, in 0.1ms.
 */
void sense_cap_step(sense_sched_t *s, const sense_value_t *v, uint32_t at_t);

/**
 * @brief Whether a capture is armed, waiting or moving.
 *
 * @param c The capture to test.
 * @return true while the capture is armed, waiting or moving.
 */
bool sense_cap_under_way(const sense_cap_t *c);

#ifdef __cplusplus
}
#endif
