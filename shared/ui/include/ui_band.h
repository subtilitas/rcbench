/*
 * The status band: the bench state on every screen, and the STOP control that
 * works from every screen.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Where STOP is, in panel coordinates.  The router hit-tests it. */
gfx_rect_t ui_band_stop_rect(void);

/**
 * The mode the band names: BENCH while the link is up and the panel drives
 * the coprocessor's outputs, SIM while it runs on its own models.  Mode
 * names, so English in every language.
 */
const char *ui_band_mode(bool link_up);

/**
 * The armed chip's label for @p roles, UI_ARMED_* bits: ARMED MOTOR, ARMED
 * SERVO, or ARMED for both and for none.  English in every language, as
 * STOP and the other controls are.
 */
const char *ui_band_armed_label(uint8_t roles);

/**
 * Draw the band.  @p title is the current screen's title, or NULL on the
 * splash, which has no home tag and no bench status.
 */
void ui_band_render(gfx_canvas_t *c, const char *title, bool show_home,
                    const ui_bench_status_t *st, bool stop_pressed,
                    bool home_pressed);

#ifdef __cplusplus
}
#endif
