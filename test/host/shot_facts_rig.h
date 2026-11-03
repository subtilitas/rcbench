/**
 * @file shot_facts_rig.h
 * @brief The screenshot gate's facts on a bench otherwise idle, for the suites
 *        that bring a screen into a state the gate reads.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "shot_gate.h"

/**
 * @brief The facts now, the panel's own readings all on the idle side.
 * @return the facts
 */
static inline shot_facts_t shot_facts_now(void)
{
    const shot_bench_t b = { .read = true, .link_up = true };
    shot_facts_t f;
    shot_facts_read(&b, &f);
    return f;
}
