#include "ui_tabs.h"

#include <string.h>

#include "ui_theme.h"
#include "ui_widgets.h"

void ui_tabs_init(ui_tabs_t *t, const char *const *labels, int count,
                  gfx_rect_t row)
{
    if (t == NULL || labels == NULL) {
        return;
    }
    memset(t, 0, sizeof(*t));
    if (count > UI_TABS_MAX) {
        count = UI_TABS_MAX;
    }
    t->count    = count;
    t->pressed  = -1;
    t->selected = 0;
    if (count <= 0) {
        return;
    }
    const int gap = 4;
    const int w = (row.w - (count - 1) * gap) / count;
    for (int i = 0; i < count; ++i) {
        t->label[i] = labels[i];
        t->rect[i]  = (gfx_rect_t){ (int16_t)(row.x + i * (w + gap)),
                                    row.y, (int16_t)w, row.h };
    }
}

bool ui_tabs_event(ui_tabs_t *t, const touch_event_t *evt)
{
    if (t == NULL || evt == NULL) {
        return false;
    }
    if (evt->type == TOUCH_EVENT_DOWN) {
        for (int i = 0; i < t->count; ++i) {
            if (gfx_rect_contains(t->rect[i], evt->point.x, evt->point.y)) {
                t->pressed  = i;
                t->press_id = evt->point.id;
                return false;
            }
        }
        return false;
    }
    if (t->pressed >= 0 && evt->point.id == t->press_id
        && evt->type == TOUCH_EVENT_UP) {
        const int i = t->pressed;
        t->pressed = -1;
        /* A press that slid off its tab is not a tap on that tab. */
        if (gfx_rect_contains(t->rect[i], evt->point.x, evt->point.y)
            && t->selected != i) {
            t->selected = i;
            return true;
        }
    }
    return false;
}

void ui_tabs_render(const ui_tabs_t *t, gfx_canvas_t *c)
{
    if (t == NULL || c == NULL) {
        return;
    }
    /*
     * A segmented control, not a row of buttons.  Filling the selected tab
     * with the accent made it the loudest thing on a screen whose job is to
     * show four live numbers -- the tab strip says where you are, and should
     * not outrank what you came to read.  So the group gets one sunken
     * trough, and selection is a raised segment inside it.
     */
    const gfx_rect_t first = t->rect[0];
    const gfx_rect_t last  = t->rect[t->count - 1];
    const int x = first.x - 3;
    const int w = (last.x + last.w + 3) - x;
    gfx_fill_round_rect(c, x, first.y - 3, w, first.h + 6, UI_R_CTL + 2,
                        ui_theme_color(UI_C_PANEL_SUNK));

    for (int i = 0; i < t->count; ++i) {
        const bool on = (t->selected == i);
        const gfx_rect_t r = t->rect[i];
        if (on) {
            gfx_fill_round_rect(c, r.x, r.y, r.w, r.h, UI_R_CHIP,
                                ui_theme_color(UI_C_PANEL_HI));
        } else if (t->pressed == i) {
            gfx_fill_round_rect(c, r.x, r.y, r.w, r.h, UI_R_CHIP,
                                ui_theme_color(UI_C_PANEL));
        }
        gfx_text_in(c, r, t->label[i], UI_FONT_LABEL,
                    on ? ui_theme_color(UI_C_ACCENT)
                       : ui_theme_color(UI_C_TEXT_DIM),
                    1, GFX_ALIGN_CENTER);
    }
}
