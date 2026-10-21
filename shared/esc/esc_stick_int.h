/**
 * @file
 * @brief Stick programming: what esc_stick.c, esc_stick_rules.c and esc_det.c
 *        share and esc_stick.h does not publish.
 *
 * For shared/esc only.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_ESC_STICK_INT_H
#define RCBENCH_ESC_STICK_INT_H

#include <stdbool.h>

#include "esc_stick.h"

/**
 * @brief Forget any group under way, as if the line had been silent.
 *
 * In esc_det.c.
 *
 * @param d  The beep detector.
 */
void esc_det_drop_group(esc_det_t *d);

/**
 * @brief Floor only: no pulses, no groups.
 *
 * In esc_det.c.
 *
 * @param d  The beep detector.
 */
void esc_det_floor_only(esc_det_t *d);

/**
 * @brief Set *@p why to @p text when @p why is not NULL, and return false.
 *
 * In esc_stick_rules.c.
 *
 * @param why   Receives the reason, or NULL to set nothing.
 * @param text  The reason.
 * @return false, always.
 */
bool esc_stick_no(const char **why, const char *text);

#endif
