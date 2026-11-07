/**
 * @file
 * @brief The footer's three slots: the rects, the hit test, the name with its
 *        port line and SETTINGS (ui_footer.h).
 *
 * SPDX-License-Identifier: MIT
 */

/* ui_text.h first: ui_theme.h defines UI_TEXT as a colour, the table's
 * entry macro of the same name. */
#include "ui_text.h"
#include "ui_footer.h"

#include "ui_icons.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/** @brief The name with no line under it, where it fits: 20 x 36. */
#define F_HEAD  (&gfx_font_20x36)
/** @brief The name over a line, and a name too wide for F_HEAD: 12 x 24. */
#define F_BODY  (&gfx_font_12x24)
/** @brief The line under the name and SETTINGS' label: 10 x 20. */
#define F_LABEL (&gfx_font_10x20)

/** @brief The name's and the line's left inset in a slot, px. */
#define NAME_DX 16
/** @brief The name's top over a line, px from the slot's top. */
#define NAME_DY 2
/** @brief The line's top, px from the slot's top: 2px under the name. */
#define LINE_DY 27
/** @brief The sliders icon's box, px, and its gap to the label. */
#define ICON    20
#define ICON_GAP 10 /**< between the icon and SETTINGS' label, px */

_Static_assert(LINE_DY + 20 <= UI_FOOTER_H, "the line ends inside the slot");
_Static_assert(NAME_DY + 24 < LINE_DY, "the name ends over the line");
_Static_assert(UI_FOOTER_Y + UI_FOOTER_H <= UI_BODY_H - UI_ALERT_H - 4,
               "the footer ends 4px above the alert band");
_Static_assert(UI_FOOTER_H >= UI_TAP_MIN, "a slot is a finger tall");

gfx_rect_t ui_footer_rect(ui_footer_slot_t slot)
{
    if ((int)slot < 0 || slot >= UI_FOOTER_SLOTS) {
        return gfx_rect_make(0, 0, 0, 0);
    }
    return gfx_rect_make(UI_FOOTER_M + (int)slot * (UI_FOOTER_W + UI_FOOTER_M),
                         UI_FOOTER_Y, UI_FOOTER_W, UI_FOOTER_H);
}

int ui_footer_hit(int x, int y)
{
    for (int i = 0; i < (int)UI_FOOTER_SLOTS; ++i) {
        if (gfx_rect_contains(ui_footer_rect((ui_footer_slot_t)i), x, y)) {
            return i;
        }
    }
    return -1;
}

void ui_footer_name(gfx_canvas_t *c, gfx_rect_t r, const char *name,
                    const char *line, bool warn)
{
    if (c == NULL || name == NULL) {
        return;
    }
    if (line == NULL || line[0] == '\0') {
        const bool big = gfx_text_width(F_HEAD, name, 1) <= r.w - NAME_DX;
        const gfx_font_t *nf = big ? F_HEAD : F_BODY;
        gfx_text(c, r.x + NAME_DX, r.y + (r.h - gfx_text_height(nf, 1)) / 2,
                 name, nf, UI_TEXT, 1);
        return;
    }
    const gfx_rect_t saved = gfx_clip_get(c);
    gfx_clip_intersect(c, r);
    gfx_text(c, r.x + NAME_DX, r.y + NAME_DY, name, F_BODY, UI_TEXT, 1);
    gfx_text(c, r.x + NAME_DX, r.y + LINE_DY, line, F_LABEL,
             warn ? UI_WARN : UI_TEXT_DIM, 1);
    c->clip = saved;
}

void ui_footer_settings(gfx_canvas_t *c, bool pressed, bool enabled)
{
    if (c == NULL) {
        return;
    }
    const gfx_rect_t r = ui_footer_rect(UI_FOOTER_SETTINGS);
    ui_button_font(c, r, NULL, UI_PANEL_HI, pressed, enabled, F_LABEL);
    /* The label's colour as ui_button_font() gives an enabled button's:
     * from the fill's luma. */
    const gfx_color_t ink = !enabled ? UI_TEXT_FAINT
                            : ui_is_light(UI_PANEL_HI) ? UI_TEXT_ON_LIGHT
                                                       : UI_TEXT;
    const char *label = TR(FOOT_SETTINGS);
    const int tw = gfx_text_width(F_LABEL, label, 1);
    const int x0 = r.x + (r.w - (ICON + ICON_GAP + tw)) / 2;
    ui_icon_sliders(c, x0 + ICON / 2, r.y + r.h / 2, ICON, ink);
    gfx_text(c, x0 + ICON + ICON_GAP,
             r.y + (r.h - gfx_text_height(F_LABEL, 1)) / 2, label, F_LABEL,
             ink, 1);
}

void ui_footer_clear(gfx_canvas_t *c)
{
    if (c == NULL) {
        return;
    }
    gfx_fill_rect(c, 0, UI_FOOTER_Y, UI_PANEL_W, UI_FOOTER_H, UI_BG);
}
