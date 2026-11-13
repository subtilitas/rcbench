/**
 * @file
 * @brief The description of one hero readout, drawn by the motor screen as a
 *        rail card.
 *
 * It holds the readout's name, unit, colour and the label of its second
 * number.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The look of one hero readout. */
typedef struct {
    const char *label;  /**< its label */
    const char *unit;  /**< its unit */
    gfx_color_t color;  /**< its color */
    int         decimals;  /**< the decimals of its number */
    /**
     * Label of the second number: "pk" for most channels, "min" for voltage,
     * whose extreme is the sag floor rather than a peak.  NULL selects "pk".
     */
    const char *extreme_label;
} ui_hero_def_t;

#ifdef __cplusplus
}
#endif
