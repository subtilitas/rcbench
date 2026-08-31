/*
 * A hero numeral: the live value, its peak, and its unit.
 *
 * The peak is beside the value rather than under it, because on a bench the
 * question is nearly always "how far did it go" and reading it should not need
 * a second glance.
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
     * What the second number is called: "pk" for most channels, "min" for
     * voltage.  A pack's interesting extreme is how far *down* it went, and
     * printing the sag floor under a "pk" label is a misreading waiting to
     * happen -- it was one, in the first render of this screen.
     */
    const char *extreme_label;
} ui_hero_def_t;

/** Draw one readout inside @p r.  Pass NAN as @p peak to omit it. */
void ui_hero_render(gfx_canvas_t *c, gfx_rect_t r, const ui_hero_def_t *def,
                    float value, float peak);

#ifdef __cplusplus
}
#endif
