/*
 * The description of one hero readout: its name, unit, colour and the label
 * of its second number.  The motor screen draws it as a rail card.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *label;
    const char *unit;
    gfx_color_t color;
    int         decimals;
    /**
     * Label of the second number: "pk" for most channels, "min" for voltage,
     * whose extreme is the sag floor rather than a peak.  NULL selects "pk".
     */
    const char *extreme_label;
} ui_hero_def_t;

#ifdef __cplusplus
}
#endif
