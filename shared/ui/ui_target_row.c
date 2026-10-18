/*
 * SPDX-License-Identifier: MIT
 */

#include "ui_target_row.h"

#include <stdio.h>
#include <string.h>

#include "ui_pinlist.h"
#include "ui_theme.h"

void ui_target_row_init(ui_target_row_t *t, gfx_rect_t row)
{
    if (t == NULL) {
        return;
    }
    memset(t, 0, sizeof(*t));
    t->rect = row;
}

gfx_rect_t ui_target_row_value_rect(const ui_target_row_t *t)
{
    if (t == NULL || t->rect.w <= UI_TARGET_LABEL_W) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    return (gfx_rect_t){ (int16_t)(t->rect.x + UI_TARGET_LABEL_W), t->rect.y,
                         (int16_t)(t->rect.w - UI_TARGET_LABEL_W),
                         t->rect.h };
}

unsigned ui_target_row_cells(const ui_target_row_t *t)
{
    const gfx_rect_t v = ui_target_row_value_rect(t);
    /* 8 px of padding each side, as a value box's text has. */
    return (v.w > 16) ? (unsigned)((v.w - 16) / 8) : 0u;
}

ui_target_ev_t ui_target_row_event(ui_target_row_t *t,
                                   const touch_event_t *evt, bool locked)
{
    if (t == NULL || evt == NULL) {
        return UI_TARGET_NONE;
    }
    const gfx_rect_t v = ui_target_row_value_rect(t);
    const bool on = gfx_rect_contains(v, evt->point.x, evt->point.y);
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (!on) {
            return UI_TARGET_NONE;
        }
        if (!locked && !t->down) {
            t->down = true;
            t->id   = evt->point.id;
        }
        return UI_TARGET_TAKEN;
    }
    if (!t->down || evt->point.id != t->id) {
        return UI_TARGET_NONE;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return UI_TARGET_TAKEN;
    }
    t->down = false;
    return (on && !locked && touch_event_is_tap_up(evt)) ? UI_TARGET_TAP
                                                          : UI_TARGET_TAKEN;
}

void ui_target_row_cancel(ui_target_row_t *t)
{
    if (t != NULL) {
        t->down = false;
    }
}

void ui_target_row_render(const ui_target_row_t *t, gfx_canvas_t *c,
                          const char *label, const char *value, bool warn,
                          bool locked)
{
    if (t == NULL || c == NULL) {
        return;
    }
    const gfx_color_t ink = warn ? ui_theme_color(UI_C_WARN)
                                 : ui_theme_color(UI_C_TEXT_DIM);
    if (label != NULL) {
        gfx_text(c, t->rect.x, t->rect.y + (t->rect.h - 16) / 2, label,
                 UI_FONT_LABEL, ink, 1);
    }
    const gfx_rect_t v = ui_target_row_value_rect(t);
    gfx_color_t body = ui_theme_color(UI_C_PANEL_SUNK);
    if (t->down && !locked) {
        body = gfx_lerp(body, GFX_WHITE, 110);
    }
    gfx_fill_round_rect(c, v.x, v.y, v.w, v.h, UI_R_CTL, body);
    if (locked) {
        /* A control that takes no press, as ui_button() draws one. */
        gfx_draw_round_rect(c, v.x, v.y, v.w, v.h, UI_R_CTL, UI_EDGE);
    } else {
        gfx_hline(c, v.x + UI_R_CTL, v.y + 1, v.w - 2 * UI_R_CTL,
                  gfx_lerp(body, GFX_WHITE, 38));
    }
    if (value != NULL) {
        const gfx_color_t txt = warn     ? ui_theme_color(UI_C_WARN)
                                : locked ? ui_theme_color(UI_C_TEXT_DIM)
                                         : ui_theme_color(UI_C_TEXT);
        gfx_text_in(c, v, value, UI_FONT_LABEL, txt, 1, GFX_ALIGN_CENTER);
    }
}

size_t ui_target_text(char *out, size_t size, const char *protocol,
                      uint64_t pins, unsigned cells)
{
    if (out == NULL || size == 0u) {
        return 0u;
    }
    out[0] = '\0';
    if (protocol == NULL) {
        protocol = "";
    }
    size_t n = strlen(protocol);
    if (n > cells) {
        n = cells;
    }
    if (n > size - 1u) {
        n = size - 1u;
    }
    memcpy(out, protocol, n);
    out[n] = '\0';
    /* The pins after a space, in what the protocol leaves of the cells;
     * with no protocol, the pins alone. */
    const size_t sep = (n > 0u) ? 1u : 0u;
    if (pins == 0u || n + sep + 1u > cells || n + sep + 1u > size - 1u) {
        return n;
    }
    /* What is left: of the cells, and of @p size after the terminator. */
    size_t room = cells - n - sep;
    if (room > size - 1u - n - sep) {
        room = size - 1u - n - sep;
    }
    char list[UI_PINLIST_PINS * 5u];
    const size_t k = ui_pinlist(list, sizeof(list), pins, (unsigned)room);
    if (k == 0u || n + sep + k > size - 1u) {
        return n;
    }
    if (sep != 0u) {
        out[n] = ' ';
    }
    memcpy(out + n + sep, list, k);
    out[n + sep + k] = '\0';
    return n + sep + k;
}
