/*
 * Small, stateless drawing helpers shared by the tester screen.
 *
 * Everything takes explicit geometry and draws immediately -- there is no
 * retained widget tree, because on this hardware the expensive thing is
 * touching framebuffer pixels, and a retained tree mostly helps you touch
 * them more often than you meant to.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "ui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Panel with a chamfered top-left / bottom-right, 1px edge, optional title. */
void ui_panel(gfx_canvas_t *c, gfx_rect_t r, const char *title,
              gfx_color_t accent);

/** The title strip a panel draws for itself; exposed for custom headers. */
void ui_panel_header(gfx_canvas_t *c, gfx_rect_t r, const char *title,
                     gfx_color_t accent);

/** Filled chamfered button.  @p pressed inverts, @p enabled dims. */
void ui_button(gfx_canvas_t *c, gfx_rect_t r, const char *label,
               gfx_color_t fill, bool pressed, bool enabled);

/** Small status pill: a filled dot, then a label. */
void ui_pill(gfx_canvas_t *c, gfx_rect_t r, const char *label,
             gfx_color_t dot, gfx_color_t fill);

/** Horizontal proportional bar, @p frac in 0..1, with an optional peak tick. */
void ui_bar(gfx_canvas_t *c, gfx_rect_t r, float frac, float peak_frac,
            gfx_color_t color);

/** Right-aligned numeric readout in the hero face, with a unit suffix. */
void ui_value(gfx_canvas_t *c, gfx_rect_t box, const char *number,
              const char *unit, gfx_color_t color);

/** Format a float with a sensible number of decimals for its magnitude. */
void ui_fmt(char *out, size_t n, float value, int decimals);

/**
 * Run clock as H:MM:SS, or MM:SS below an hour.
 *
 * Shared so the overview and the bench cannot print the same number two
 * different ways -- the overview used to drop the hours and wrap to 00:00.
 */
void ui_clock(char *out, size_t n, uint32_t seconds);

/*
 * The home tag: every screen puts one in the same place at the top left, so
 * "back to the menu" is muscle memory rather than a hunt.  The router
 * hit-tests it before the screen sees the event.
 */
#define UI_TAG_X 6
#define UI_TAG_Y 5
#define UI_TAG_H 30

gfx_rect_t ui_home_tag_rect(const char *title);
void ui_home_tag(gfx_canvas_t *c, const char *title, bool pressed);

/** The same shape without the chevron: an identity mark, not a control. */
void ui_wordmark(gfx_canvas_t *c, const char *text, gfx_color_t fill);

/** Left-pointing solid triangle, apex at (x, cy). */
void ui_chevron_left(gfx_canvas_t *c, int x, int cy, int size, gfx_color_t color);

/** Dotted/dashed horizontal rule, used to separate rows without a hard line. */
void ui_rule(gfx_canvas_t *c, int x, int y, int w, gfx_color_t color);

#ifdef __cplusplus
}
#endif
