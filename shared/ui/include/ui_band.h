/*
 * The status band: what is true about the bench, and the one control that has
 * to work from anywhere.
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Where STOP is, in panel coordinates.  The router hit-tests it. */
gfx_rect_t ui_band_stop_rect(void);

/**
 * Draw the band.  @p title is the current screen's, or NULL on the splash,
 * where there is nothing to go back to and nothing armed yet.
 */
void ui_band_render(gfx_canvas_t *c, const char *title, bool show_home,
                    const ui_bench_status_t *st, bool stop_pressed,
                    bool home_pressed);

#ifdef __cplusplus
}
#endif
