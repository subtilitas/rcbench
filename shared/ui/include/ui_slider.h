/*
 * A draggable value with presets: the throttle, and any other control that
 * needs coarse reach and fine adjustment in one place.
 *
 * Two contracts about presses that do not end where they began, because
 * those are the presses that command a motor by accident:
 *
 *   a press that starts on the track owns the value until it is released,
 *   wherever it moves, so a finger sliding off the track does not hand the
 *   value to whatever is underneath;
 *
 *   a press that starts anywhere else never becomes a drag, however far it
 *   travels across the track.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "touch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_SLIDER_MAX_PRESETS 8

/*
 * How far ui_slider_render paints outside its track, above and below, in
 * pixels.
 *
 * The thumb stands proud of the track, and a caller that clears only the
 * track rectangle before redrawing leaves that overhang behind.  The thumb's
 * shadow is offset 2 px below it, so the painted region is not symmetric and
 * this constant alone does not describe it: use ui_slider_painted_rect().
 */
#define UI_SLIDER_OVERHANG 5

typedef struct {
    gfx_rect_t track;
    gfx_rect_t presets[UI_SLIDER_MAX_PRESETS];
    float      preset_value[UI_SLIDER_MAX_PRESETS];
    const char *preset_label[UI_SLIDER_MAX_PRESETS];
    int        preset_count;
    int        ticks;      /**< scale divisions on the track, 0 for none */

    float min, max, step;
    float value;
    gfx_color_t color;

    bool    dragging;
    uint8_t drag_id;
    int     pressed_preset;   /**< index, or -1                           */
    uint8_t preset_id;
} ui_slider_t;

void ui_slider_init(ui_slider_t *s, gfx_rect_t track, float min, float max,
                    gfx_color_t color);
/** Draw @p n scale divisions across the track.  0 turns them off. */
void ui_slider_set_ticks(ui_slider_t *s, int n);

/** Presets are laid out in a row inside @p row, with 4 px gaps. */
void ui_slider_set_presets(ui_slider_t *s, const float *values,
                           const char *const *labels, int count,
                           gfx_rect_t row);

void ui_slider_set(ui_slider_t *s, float value);

/** Returns true when the value changed as a result of this event. */
bool ui_slider_event(ui_slider_t *s, const touch_event_t *evt);

void ui_slider_render(const ui_slider_t *s, gfx_canvas_t *c);

/**
 * Every pixel ui_slider_render may touch, the thumb's overhang and its
 * shadow included.  A caller that repaints the slider without repainting
 * everything around it clears this and not the track, or the thumb's old
 * positions stay on the screen.
 */
gfx_rect_t ui_slider_painted_rect(const ui_slider_t *s);

#ifdef __cplusplus
}
#endif
