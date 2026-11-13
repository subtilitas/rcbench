/**
 * @file
 * @brief A draggable value with presets: the throttle, and any other control
 *        that needs coarse reach and fine adjustment in one place.
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
 * A drag is its contact's alone.  A second finger coming down, on the track
 * or anywhere else, leaves it running; a DOWN with the drag's own track id
 * ends it, since that contact's release went missing.
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

#define UI_SLIDER_MAX_PRESETS 8  /**< the most presets a slider holds */

/**
 * @brief How far ui_slider_render paints outside its track, above and below, in
 * pixels.
 *
 * The thumb stands proud of the track, and a caller that clears only the
 * track rectangle before redrawing leaves that overhang behind.  The thumb's
 * shadow is offset 2px below it, so the painted region is not symmetric and
 * this constant alone does not describe it: use ui_slider_painted_rect().
 */
#define UI_SLIDER_OVERHANG 5

/**
 * @brief The state of a slider: its track, its range, its value, its presets
 *         and the gesture under way.
 */
typedef struct {
    gfx_rect_t track;  /**< where the track is */
    gfx_rect_t presets[UI_SLIDER_MAX_PRESETS];  /**< the preset buttons' rectangles */
    float      preset_value[UI_SLIDER_MAX_PRESETS];  /**< the preset buttons' values */
    const char *preset_label[UI_SLIDER_MAX_PRESETS];  /**< the preset buttons' labels */
    int        preset_count;  /**< how many presets are set */
    int        ticks;      /**< scale divisions on the track, 0 for none */

    float min,                   /**< the range's lower end */
          max;                   /**< the range's upper end */
    float value;  /**< the value, from min to max */
    gfx_color_t color;  /**< the thumb's and the fill's color */

    /**
     * @brief Whether a press sets the value under it.  Off by default, because the
     * control this widget was written for is a throttle: a touch at the far
     * end of the track would command full travel in one contact.  A slider
     * that commands nothing dangerous can turn it on and keep the tap.
     */
    bool    tap_to_set;

    /** @brief Drawn dimmed, as a button that takes no press is.  The drawing only:
     * the owner decides which events the slider is given. */
    bool    dim;

    bool    dragging;  /**< a drag is under way */
    uint8_t drag_id;  /**< the contact that drags */
    int16_t drag_x;       /**< where the drag measures from: the finger's
                               x at the press or at the last outside set */
    float   drag_value;   /**< and the value it measures from             */
    int16_t last_x;       /**< the dragging finger's last x               */
    int     pressed_preset;   /**< index, or -1                           */
    uint8_t preset_id;  /**< the contact that pressed the preset */
} ui_slider_t;

/**
 * @brief Sets the track, the range and the color, and clears the rest.
 *
 * @param s the slider
 * @param track where the track is
 * @param min the range's lower end
 * @param max its upper end
 * @param color the thumb's and the fill's color
 */
void ui_slider_init(ui_slider_t *s, gfx_rect_t track, float min, float max,
                    gfx_color_t color);
/**
 * @brief Draw @p n scale divisions across the track.  0 turns them off.
 *
 * @param s the slider
 * @param n the divisions; 0 for none
 */
void ui_slider_set_ticks(ui_slider_t *s, int n);

/**
 * @brief Sets the presets, laid out in a row inside @p row with 4px gaps.
 * @param s the slider
 * @param values the presets' values
 * @param labels their labels
 * @param count how many
 * @param row the row they take
 */
void ui_slider_set_presets(ui_slider_t *s, const float *values,
                           const char *const *labels, int count,
                           gfx_rect_t row);

/**
 * @brief Sets the value, held to the range.
 *
 * @param s the slider
 * @param value the value
 */
void ui_slider_set(ui_slider_t *s, float value);

/**
 * @brief Let a press set the value under it. Off by default; a throttle must not
 * take a value from one contact.
 *
 * @param s the slider
 * @param on true lets a press set the value under it
 */
void ui_slider_set_tap_to_set(ui_slider_t *s, bool on);

/**
 * @brief Abandon a drag that will get no release, on leaving a screen.
 *
 * @param s the slider
 */
void ui_slider_release(ui_slider_t *s);

/**
 * @brief Ends the drag or the preset press that contact @p id holds, and
 *        nothing another contact holds.
 *
 * A DOWN with the id of a gesture held says that contact has gone and its
 * release with it: the controller reuses track ids.  A DOWN of another
 * contact is a second finger, which leaves the gesture to the first.
 *
 * @param s  the slider; NULL does nothing
 * @param id the contact that came down
 */
void ui_slider_release_contact(ui_slider_t *s, uint8_t id);

/**
 * @brief Returns true when the value changed as a result of this event.
 *
 * @param s the slider
 * @param evt the touch event
 * @return true when the value changed
 */
bool ui_slider_event(ui_slider_t *s, const touch_event_t *evt);

/**
 * @brief Draws the slider.
 *
 * @param s the slider
 * @param c the canvas to draw on
 */
void ui_slider_render(const ui_slider_t *s, gfx_canvas_t *c);

/**
 * @brief Every pixel ui_slider_render may touch, the thumb's overhang and its
 * shadow included.  A caller that repaints the slider without repainting
 * everything around it clears this and not the track, or the thumb's old
 * positions stay on the screen.
 *
 * @param s the slider
 * @return the rectangle
 */
gfx_rect_t ui_slider_painted_rect(const ui_slider_t *s);

#ifdef __cplusplus
}
#endif
