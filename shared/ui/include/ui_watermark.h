/*
 * The mark that says the numbers behind it are not real.
 *
 * The bench is useful without hardware -- a modelled pack, motor and
 * propeller let every screen be built, reviewed and demonstrated before the
 * coprocessor exists.  The danger is entirely that a simulated number gets
 * read, screenshotted or quoted as a measured one, and no caption in a corner
 * prevents that.
 *
 * So it is written across the whole screen, corner to corner, faint enough to
 * read straight through and impossible to crop out of a photograph.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Draw "SIMULATION" across the whole canvas, blended over what is there. */
void ui_watermark(gfx_canvas_t *c);

#ifdef __cplusplus
}
#endif
