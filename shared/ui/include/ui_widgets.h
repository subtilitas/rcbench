/*
 * Stateless drawing helpers shared by every screen.
 *
 * Every call takes explicit geometry and draws immediately.  There is no
 * retained widget tree: on this hardware the cost is in touching framebuffer
 * pixels, and a retained tree touches them more often.
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

/** Filled rounded button.  @p pressed lightens the fill; @p enabled false
 *  dims it. */
void ui_button(gfx_canvas_t *c, gfx_rect_t r, const char *label,
               gfx_color_t fill, bool pressed, bool enabled);

/** True when @p c is bright enough to need dark text on it. */
bool ui_is_light(gfx_color_t c);

/** A raised surface with a hairline edge: the base of every panel. */
void ui_card(gfx_canvas_t *c, gfx_rect_t r, gfx_color_t fill);

/** Small status pill: a filled dot, then a label. */
void ui_pill(gfx_canvas_t *c, gfx_rect_t r, const char *label,
             gfx_color_t dot, gfx_color_t fill);

/** Horizontal proportional bar, @p frac in 0..1, with an optional peak tick. */
void ui_bar(gfx_canvas_t *c, gfx_rect_t r, float frac, float peak_frac,
            gfx_color_t color);

/** Right-aligned numeric readout in the hero face, with a unit suffix. */
void ui_value(gfx_canvas_t *c, gfx_rect_t box, const char *number,
              const char *unit, gfx_color_t color);

/** Format @p value with @p decimals decimal places (0 to 3); a non-finite
 *  value prints as "--". */
void ui_fmt(char *out, size_t n, float value, int decimals);

/**
 * Run clock as H:MM:SS, or MM:SS below an hour.
 *
 * Shared so every screen prints the same number the same way.
 */
void ui_clock(char *out, size_t n, uint32_t seconds);

/*
 * The home tag, at the same place at the top left of every screen that has
 * one.  The router hit-tests it before the screen sees the event.
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

/** A 1 px horizontal rule. */
void ui_rule(gfx_canvas_t *c, int x, int y, int w, gfx_color_t color);

/* ------------------------------------------------------- hold-to-confirm */

/*
 * A button that is held rather than pressed, and what it looks like while it
 * is held.
 *
 * Two seconds of contact, the fill fading from where it starts to where it
 * ends across them, and the whole button flashing twice as the command goes.
 * ARM uses it because a bench should not spin a propeller on a touch that
 * could have been an elbow; the bus-fault screen uses it because an
 * acknowledgement that can be given by brushing the panel is not one.
 *
 * The state stays with each screen -- ARM's is bound up with its own flash
 * and its "already fired" latch -- and these are the timing and the colours,
 * so the two gestures cannot look different from each other.
 */
#define UI_HOLD_S 2.0f

/** Frames per flash cycle and how many cycles: white, black, settled. */
#define UI_HOLD_FLASH_CYCLE  3
#define UI_HOLD_FLASH_TIMES  2
#define UI_HOLD_FLASH_FRAMES (UI_HOLD_FLASH_CYCLE * UI_HOLD_FLASH_TIMES)

/**
 * The fill for a button held @p held_s of UI_HOLD_S, fading @p base to
 * @p target. @p held_s of 0 is @p base; the fade previews the colour the
 * button is about to hold.
 */
gfx_color_t ui_hold_fill(gfx_color_t base, gfx_color_t target, float held_s);

/**
 * The fill while the flash runs, @p frames_left counting down from
 * UI_HOLD_FLASH_FRAMES. One colour per drawn frame; at 39 Hz the whole flash
 * is about 154 ms, short enough to read as a flash rather than an animation.
 * @p settled is what it returns to.
 */
gfx_color_t ui_hold_flash(gfx_color_t settled, int frames_left);

#ifdef __cplusplus
}
#endif
