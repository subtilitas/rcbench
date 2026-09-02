/*
 * A hero numeral: the live value, its unit, and the peak (or minimum) in a
 * footer under it.
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

/** Draw one readout inside @p r.  Pass NAN as @p peak to omit it. */
void ui_hero_render(gfx_canvas_t *c, gfx_rect_t r, const ui_hero_def_t *def,
                    float value, float peak);

#ifdef __cplusplus
}
#endif
