#include "ui_slider.h"

#include <string.h>

#include "ui_theme.h"
#include "ui_widgets.h"

static float clampf(float v, float lo, float hi)
{
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}

void ui_slider_init(ui_slider_t *s, gfx_rect_t track, float min, float max,
                    gfx_color_t color)
{
    if (s == NULL) {
        return;
    }
    memset(s, 0, sizeof(*s));
    s->track = track;
    s->min   = min;
    s->max   = max;
    s->color = color;
    s->pressed_preset = -1;
}

void ui_slider_set_presets(ui_slider_t *s, const float *values,
                           const char *const *labels, int count,
                           gfx_rect_t row)
{
    if (s == NULL || values == NULL || labels == NULL) {
        return;
    }
    if (count > UI_SLIDER_MAX_PRESETS) {
        count = UI_SLIDER_MAX_PRESETS;
    }
    s->preset_count = count;
    if (count <= 0) {
        return;
    }
    const int gap = 4;
    const int w = (row.w - (count - 1) * gap) / count;
    for (int i = 0; i < count; ++i) {
        s->preset_value[i] = values[i];
        s->preset_label[i] = labels[i];
        s->presets[i] = (gfx_rect_t){ (int16_t)(row.x + i * (w + gap)),
                                      row.y, (int16_t)w, row.h };
    }
}

void ui_slider_set(ui_slider_t *s, float value)
{
    if (s != NULL) {
        s->value = clampf(value, s->min, s->max);
    }
}

static bool from_x(ui_slider_t *s, int x)
{
    if (s->track.w <= 1) {
        return false;
    }
    const float frac = clampf((float)(x - s->track.x) / (float)(s->track.w - 1),
                              0.0f, 1.0f);
    const float was = s->value;
    s->value = s->min + frac * (s->max - s->min);
    return s->value != was;
}

bool ui_slider_event(ui_slider_t *s, const touch_event_t *evt)
{
    if (s == NULL || evt == NULL) {
        return false;
    }
    const int x = evt->point.x;
    const int y = evt->point.y;

    if (evt->type == TOUCH_EVENT_DOWN) {
        if (gfx_rect_contains(s->track, x, y)) {
            s->dragging = true;
            s->drag_id  = evt->point.id;
            return from_x(s, x);
        }
        for (int i = 0; i < s->preset_count; ++i) {
            if (gfx_rect_contains(s->presets[i], x, y)) {
                s->pressed_preset = i;
                s->preset_id      = evt->point.id;
                return false;
            }
        }
        return false;
    }

    /* Only the contact that started the gesture may continue it: a second
     * finger or a resting palm must not steal the release. */
    if (s->dragging && evt->point.id == s->drag_id) {
        if (evt->type == TOUCH_EVENT_MOVE) {
            /* Deliberately not requiring the finger to stay on the track: a
             * press that began there owns the value until it lets go. */
            return from_x(s, x);
        }
        if (evt->type == TOUCH_EVENT_UP) {
            s->dragging = false;
            return from_x(s, x);
        }
        return false;
    }

    if (s->pressed_preset >= 0 && evt->point.id == s->preset_id
        && evt->type == TOUCH_EVENT_UP) {
        const int i = s->pressed_preset;
        s->pressed_preset = -1;
        /* A press that slid off its preset is not a tap on that preset. */
        if (gfx_rect_contains(s->presets[i], x, y)) {
            const float was = s->value;
            s->value = clampf(s->preset_value[i], s->min, s->max);
            return s->value != was;
        }
    }
    return false;
}

void ui_slider_render(const ui_slider_t *s, gfx_canvas_t *c)
{
    if (s == NULL || c == NULL) {
        return;
    }
    const float span = (s->max - s->min);
    const float frac = (span > 0.0f) ? (s->value - s->min) / span : 0.0f;

    /*
     * A track with a handle rather than a bar with a notch.  The rounded
     * trough reads as something you drag; a square-ended fill reads as a
     * progress meter, which is the wrong affordance for the one control on
     * this screen that commands a motor.
     */
    const int rad = s->track.h / 2;
    gfx_fill_round_rect(c, s->track.x, s->track.y, s->track.w, s->track.h,
                        rad, ui_theme_color(UI_C_PANEL_SUNK));
    /* The trough's own shadow: one line under its top edge, which is what
     * makes it read as cut into the panel instead of laid on it. */
    gfx_hline(c, s->track.x + rad, s->track.y + 1, s->track.w - 2 * rad,
              gfx_lerp(ui_theme_color(UI_C_PANEL_SUNK), GFX_BLACK, 70));

    int filled = (int)(frac * (float)s->track.w + 0.5f);
    /* Below one diameter a rounded fill has no straight section left and
     * draws as a lens narrower than the trough it sits in, which reads as a
     * rendering fault rather than as a small number. */
    if (filled > 0 && filled < s->track.h) {
        filled = s->track.h;
    }
    if (filled > 0) {
        gfx_fill_round_rect(c, s->track.x, s->track.y, filled, s->track.h,
                            rad, s->color);
        gfx_hline(c, s->track.x + rad, s->track.y + 1, filled - 2 * rad,
                  gfx_lerp(s->color, GFX_WHITE, 70));
    }

    /*
     * The handle is drawn inside the track's height, not proud of it.  The
     * predecessor's rode five pixels over each side while the clear covered
     * only the track, which left stale pixels behind on every redraw -- and
     * with alternating framebuffers that reads as flicker rather than as an
     * obviously wrong pixel.
     */
    const int hr = rad - 2;
    int hx = s->track.x + (int)(frac * (float)s->track.w + 0.5f);
    /* Clamped inside the track at both ends, for the same reason. */
    if (hx < s->track.x + rad) {
        hx = s->track.x + rad;
    }
    if (hx > s->track.x + s->track.w - rad) {
        hx = s->track.x + s->track.w - rad;
    }
    const int hy = s->track.y + rad;
    /* A seat under the knob before the knob: one darker ring, so it sits in
     * the trough rather than floating over it. */
    gfx_fill_circle(c, hx, hy + 1, hr,
                    gfx_lerp(ui_theme_color(UI_C_PANEL_SUNK), GFX_BLACK, 90));
    gfx_fill_circle(c, hx, hy, hr, ui_theme_color(UI_C_TEXT));
    gfx_draw_circle(c, hx, hy, hr,
                    gfx_lerp(ui_theme_color(UI_C_TEXT), GFX_BLACK, 70));

    /*
     * Two grip lines, not three.  A plain disc reads as a position marker and
     * a gripped one reads as something to take hold of -- but at this radius
     * a third line down the middle closes the gaps and the whole knob goes
     * grey, which is a texture rather than a grip.
     */
    const gfx_color_t grip = gfx_lerp(ui_theme_color(UI_C_TEXT),
                                      GFX_BLACK, 95);
    gfx_vline(c, hx - 3, hy - 4, 9, grip);
    gfx_vline(c, hx + 3, hy - 4, 9, grip);

    for (int i = 0; i < s->preset_count; ++i) {
        ui_button(c, s->presets[i], s->preset_label[i],
                  ui_theme_color(UI_C_PANEL), s->pressed_preset == i, true);
    }
}
