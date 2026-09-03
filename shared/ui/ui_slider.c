/*
 * SPDX-License-Identifier: MIT
 */

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

void ui_slider_set_ticks(ui_slider_t *s, int n)
{
    if (s != NULL && n >= 0) {
        s->ticks = n;
    }
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

/* The shadow sits 2 px below the thumb, so the bottom reaches further than
 * the top.  Kept beside the drawing it describes. */
#define SLIDER_SHADOW_DROP 2

gfx_rect_t ui_slider_painted_rect(const ui_slider_t *s)
{
    if (s == NULL) {
        return gfx_rect_make(0, 0, 0, 0);
    }
    return gfx_rect_make(s->track.x, s->track.y - UI_SLIDER_OVERHANG,
                         s->track.w,
                         s->track.h + 2 * UI_SLIDER_OVERHANG
                             + SLIDER_SHADOW_DROP);
}

void ui_slider_render(const ui_slider_t *s, gfx_canvas_t *c)
{
    if (s == NULL || c == NULL) {
        return;
    }
    const float span = (s->max - s->min);
    const float frac = (span > 0.0f) ? (s->value - s->min) / span : 0.0f;

    /*
     * A rounded trough with a handle, so the control reads as a slider
     * rather than as a progress meter.
     */
    const int rad = s->track.h / 2;
    gfx_fill_round_rect(c, s->track.x, s->track.y, s->track.w, s->track.h,
                        rad, ui_theme_color(UI_C_PANEL_SUNK));
    /* One darker line under the trough's top edge, so it reads as cut into
     * the panel. */
    gfx_hline(c, s->track.x + rad, s->track.y + 1, s->track.w - 2 * rad,
              gfx_lerp(ui_theme_color(UI_C_PANEL_SUNK), GFX_BLACK, 70));

    int filled = (int)(frac * (float)s->track.w + 0.5f);
    /* A rounded fill narrower than its own height has no straight section
     * and draws as a lens, so the fill is at least one diameter wide. */
    if (filled > 0 && filled < s->track.h) {
        filled = s->track.h;
    }
    if (filled > 0) {
        gfx_fill_round_rect(c, s->track.x, s->track.y, filled, s->track.h,
                            rad, s->color);
        gfx_hline(c, s->track.x + rad, s->track.y + 1, filled - 2 * rad,
                  gfx_lerp(s->color, GFX_WHITE, 70));
    }

    /* Scale divisions, cut 5 px into both edges rather than ruled across the
     * track, so they stay readable while the fill moves. */
    for (int t = 1; s->ticks > 0 && t < s->ticks; ++t) {
        const int tx = s->track.x + (int)((float)s->track.w
                                          * (float)t / (float)s->ticks + 0.5f);
        /* A tick darkens over the fill and lightens over the trough, so
         * every tick is visible at every setting. */
        const gfx_color_t tc = (tx < s->track.x + filled)
            ? gfx_lerp(s->color, GFX_BLACK, 105)
            : gfx_lerp(ui_theme_color(UI_C_PANEL_SUNK),
                       ui_theme_color(UI_C_TEXT), 105);
        gfx_vline(c, tx, s->track.y + 3, 5, tc);
        gfx_vline(c, tx, s->track.y + s->track.h - 8, 5, tc);
    }

    /*
     * The thumb stands UI_SLIDER_OVERHANG px proud of the track on both
     * sides.  A caller that clears only the track before redrawing leaves the
     * overhang behind, which with alternating framebuffers shows as flicker;
     * the caller clears the overhang as well.
     */
    const int tw = 20;
    const int th = s->track.h + 2 * UI_SLIDER_OVERHANG;
    int hx = s->track.x + (int)(frac * (float)s->track.w + 0.5f);
    /* Clamped so the thumb stays over its own track at both ends. */
    if (hx < s->track.x + tw / 2) {
        hx = s->track.x + tw / 2;
    }
    if (hx > s->track.x + s->track.w - tw / 2) {
        hx = s->track.x + s->track.w - tw / 2;
    }
    const int tx = hx - tw / 2;
    const int ty = s->track.y - UI_SLIDER_OVERHANG;

    /* One shape offset 2 px below the thumb, drawn first, as its shadow. */
    gfx_fill_round_rect(c, tx, ty + SLIDER_SHADOW_DROP, tw, th, 6,
                        gfx_lerp(ui_theme_color(UI_C_BG), GFX_BLACK, 120));
    gfx_fill_round_rect(c, tx, ty, tw, th, 6, ui_theme_color(UI_C_TEXT));
    gfx_draw_round_rect(c, tx, ty, tw, th, 6,
                        gfx_lerp(ui_theme_color(UI_C_TEXT), GFX_BLACK, 80));
    gfx_hline(c, tx + 6, ty + 1, tw - 12,
              gfx_lerp(ui_theme_color(UI_C_TEXT), GFX_WHITE, 120));

    /* Two grip lines, so the thumb reads as a control rather than as a
     * marker. */
    const gfx_color_t grip = gfx_lerp(ui_theme_color(UI_C_TEXT),
                                      GFX_BLACK, 95);
    const int gy = ty + th / 2 - 6;
    gfx_vline(c, hx - 4, gy, 13, grip);
    gfx_vline(c, hx + 4, gy, 13, grip);

    for (int i = 0; i < s->preset_count; ++i) {
        ui_button(c, s->presets[i], s->preset_label[i],
                  ui_theme_color(UI_C_PANEL), s->pressed_preset == i, true);
    }
}
