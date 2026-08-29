/*
 * A draggable value with presets: the throttle, and anything else that wants
 * coarse reach and fine adjustment from the same control.
 *
 * The two contracts that matter are both about presses that do not end where
 * they began, because those are the ones that spin a motor by accident:
 *
 *   a press that starts on the track owns the value until it is released,
 *   wherever it wanders -- so a finger sliding off the bottom of the track
 *   does not hand the throttle to whatever is underneath;
 *
 *   and a press that starts anywhere else never becomes a drag, however far it
 *   travels across the track on its way somewhere.
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
 * How far ui_slider_render paints outside its track, above and below.
 *
 * The thumb stands proud so it reads as a grip rather than as a fill level,
 * and a caller that clears only the track rectangle before redrawing would
 * leave that overhang behind.  Clear track.y - UI_SLIDER_OVERHANG through
 * track.y + track.h + UI_SLIDER_OVERHANG, or more.
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
/** Presets are laid out in a row under the track. */
/** Draw @p n scale divisions across the track.  0 turns them off. */
void ui_slider_set_ticks(ui_slider_t *s, int n);

void ui_slider_set_presets(ui_slider_t *s, const float *values,
                           const char *const *labels, int count,
                           gfx_rect_t row);

void ui_slider_set(ui_slider_t *s, float value);

/** Returns true when the value changed as a result of this event. */
bool ui_slider_event(ui_slider_t *s, const touch_event_t *evt);

void ui_slider_render(const ui_slider_t *s, gfx_canvas_t *c);

#ifdef __cplusplus
}
#endif
