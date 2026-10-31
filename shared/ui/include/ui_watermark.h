/**
 * @file
 * @brief The SIMULATION watermark: the mark that the numbers behind it are
 *        modelled.
 *
 * It is written across the whole screen, corner to corner, at 15% opacity:
 * readable through, and not croppable out of a photograph.  A caption in a
 * corner would not stop a simulated reading being quoted as a measured one.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Draw "SIMULATION" across the whole canvas, blended over what is
 *        there.  Draws nothing while SETUP > PANEL > Demo mode (SET_DEMO_MODE)
 *        is ON; the screens under the mark repaint in full when the setting
 *        changes (settings_apply_ui()).
 * @param c The canvas.
 */
void ui_watermark(gfx_canvas_t *c);

/** @brief Drop the cached stencil, so the next call rebuilds it. */
void ui_watermark_invalidate(void);

#ifdef __cplusplus
}
#endif
