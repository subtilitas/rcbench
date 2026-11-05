/**
 * @file
 * @brief The stamps the screens' ARMs carry (ui_value_gate.h).
 *
 * One count for MOTOR & ESC and SERVO together, so a refusal of an ARM one
 * screen asked for never matches an ask of the other.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ui_value_gate.h"

/** @brief The stamp the last ARM posted carried; 0 before the first. */
static uint32_t s_last_ask;

uint32_t ui_value_new_ask(void)
{
    ++s_last_ask;
    if (s_last_ask == 0u) {
        s_last_ask = 1u;    /* 0 names no ask */
    }
    return s_last_ask;
}

void ui_value_ask_seed(uint32_t last)
{
    s_last_ask = last;
}
