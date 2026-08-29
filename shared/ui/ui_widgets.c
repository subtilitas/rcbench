/*
 * SPDX-License-Identifier: MIT
 */

#include "ui_widgets.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void ui_panel_header(gfx_canvas_t *c, gfx_rect_t r, const char *title,
                     gfx_color_t accent)
{
    if (!title || !*title) {
        return;
    }
    /* An accent tab sitting on the panel's top edge, cut on its trailing
     * corner so it reads as a tag rather than a button. */
    int tw = gfx_text_width(UI_FONT_LABEL, title, 1);
    int tab_w = tw + 26;
    if (tab_w > r.w - 8) {
        tab_w = r.w - 8;
    }
    gfx_fill_chamfer_rect_ex(c, r.x, r.y, tab_w, 20, 0, 0, 10, 0, accent);
    gfx_text(c, r.x + 10, r.y + 2, title, UI_FONT_LABEL, UI_TEXT_ON_LIGHT, 1);
}

void ui_panel(gfx_canvas_t *c, gfx_rect_t r, const char *title,
              gfx_color_t accent)
{
    gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h,
                             UI_CHAMFER, 0, UI_CHAMFER, 0, UI_PANEL);
    gfx_draw_chamfer_rect_ex(c, r.x, r.y, r.w, r.h,
                             UI_CHAMFER, 0, UI_CHAMFER, 0, UI_EDGE);
    /* A short accent stroke on the top-right corner: cheap, and it stops a
     * grid of identical panels reading as a spreadsheet. */
    gfx_hline(c, r.x + r.w - 34, r.y, 34, accent);
    gfx_vline(c, r.x + r.w - 1, r.y, 10, accent);
    ui_panel_header(c, r, title, accent);
}

/*
 * Rec. 601 luma on the 5-6-5 channels, against the middle of the range.  The
 * weights matter: pure green at full scale is bright and pure blue at full
 * scale is not, and averaging the three calls both of them the same.
 */
bool ui_is_light(gfx_color_t c)
{
    const unsigned r = (c >> 11) & 0x1Fu;
    const unsigned g = (c >> 5)  & 0x3Fu;
    const unsigned b = c         & 0x1Fu;
    /* Each channel scaled to 0-255 before weighting. */
    const unsigned luma = (299u * (r * 255u / 31u)
                         + 587u * (g * 255u / 63u)
                         + 114u * (b * 255u / 31u)) / 1000u;
    return luma >= 140u;
}

void ui_button(gfx_canvas_t *c, gfx_rect_t r, const char *label,
               gfx_color_t fill, bool pressed, bool enabled)
{
    gfx_color_t body = enabled ? fill : GFX_RGB(38, 46, 58);
    /*
     * Contrast picked from the fill, not assumed.
     *
     * This used to be near-black on every enabled button, which is right on
     * an accent-coloured one and invisible on a dark panel-coloured one --
     * so the throttle presets and RESET PEAKS were charcoal text on charcoal
     * and had to be found by memory.  Both weights are passed the same way,
     * so the button has to work out which it got.
     */
    gfx_color_t text = enabled ? (ui_is_light(body) ? UI_TEXT_ON_LIGHT
                                                    : UI_TEXT)
                               : UI_TEXT_FAINT;

    if (pressed && enabled) {
        body = gfx_lerp(body, GFX_WHITE, 110);
    }

    gfx_fill_round_rect(c, r.x, r.y, r.w, r.h, UI_R_CTL, body);
    if (!enabled) {
        gfx_draw_round_rect(c, r.x, r.y, r.w, r.h, UI_R_CTL, UI_EDGE);
    }
    if (label) {
        gfx_text_in(c, r, label, UI_FONT_LABEL, text, 1, GFX_ALIGN_CENTER);
    }
}

/*
 * The one surface everything else sits on: a raised fill with a hairline
 * edge.  Containment is what separates a reading from the three beside it --
 * bare numerals on a flat ground read as one wall of text however carefully
 * they are spaced.
 */
void ui_card(gfx_canvas_t *c, gfx_rect_t r, gfx_color_t fill)
{
    gfx_fill_round_rect(c, r.x, r.y, r.w, r.h, UI_R_CARD, fill);
    gfx_draw_round_rect(c, r.x, r.y, r.w, r.h, UI_R_CARD, UI_EDGE);
}

void ui_pill(gfx_canvas_t *c, gfx_rect_t r, const char *label,
             gfx_color_t dot, gfx_color_t fill)
{
    gfx_fill_round_rect(c, r.x, r.y, r.w, r.h, r.h / 2, fill);
    int cy = r.y + r.h / 2;
    gfx_fill_circle(c, r.x + 12, cy, 4, dot);
    gfx_draw_circle(c, r.x + 12, cy, 6, gfx_lerp(dot, fill, 130));
    if (label) {
        gfx_text(c, r.x + 24, cy - 8, label, UI_FONT_LABEL, UI_TEXT, 1);
    }
}

void ui_bar(gfx_canvas_t *c, gfx_rect_t r, float frac, float peak_frac,
            gfx_color_t color)
{
    if (frac < 0.0f) { frac = 0.0f; }
    if (frac > 1.0f) { frac = 1.0f; }

    gfx_fill_rect(c, r.x, r.y, r.w, r.h, UI_PANEL_SUNK);
    int fill_w = (int)(frac * (float)r.w + 0.5f);
    if (fill_w > 0) {
        gfx_fill_rect(c, r.x, r.y, fill_w, r.h, color);
    }
    if (peak_frac > 0.0f) {
        if (peak_frac > 1.0f) { peak_frac = 1.0f; }
        int px = r.x + (int)(peak_frac * (float)(r.w - 1) + 0.5f);
        gfx_vline(c, px, r.y - 1, r.h + 2, gfx_lerp(color, GFX_WHITE, 160));
    }
}

void ui_value(gfx_canvas_t *c, gfx_rect_t box, const char *number,
              const char *unit, gfx_color_t color)
{
    int uw = unit ? gfx_text_width(UI_FONT_LABEL, unit, 1) + 10 : 0;
    int nw = gfx_text_width(UI_FONT_NUM, number, 1);
    int x = box.x + box.w - uw - nw;
    int y = box.y + (box.h - gfx_text_height(UI_FONT_NUM, 1)) / 2;

    gfx_text(c, x, y, number, UI_FONT_NUM, color, 1);
    if (unit) {
        gfx_text(c, box.x + box.w - uw + 10,
                 y + gfx_text_height(UI_FONT_NUM, 1) - 18,
                 unit, UI_FONT_LABEL, UI_TEXT_DIM, 1);
    }
}

void ui_clock(char *out, size_t n, uint32_t seconds)
{
    if (!out || n == 0) {
        return;
    }
    unsigned h = (unsigned)(seconds / 3600u);
    unsigned m = (unsigned)((seconds / 60u) % 60u);
    unsigned sec = (unsigned)(seconds % 60u);
    if (h > 0) {
        snprintf(out, n, "%u:%02u:%02u", h, m, sec);
    } else {
        snprintf(out, n, "%02u:%02u", m, sec);
    }
}

void ui_fmt(char *out, size_t n, float value, int decimals)
{
    if (!out || n == 0) {
        return;
    }
    if (!isfinite(value)) {            /* NaN, and infinities too: "inf" has no
                                        * glyphs in the numeric face, so it
                                        * renders as blank space rather than as
                                        * the fault it is. */
        snprintf(out, n, "--");
        return;
    }
    switch (decimals) {
    case 0:  snprintf(out, n, "%.0f", (double)value); break;
    case 1:  snprintf(out, n, "%.1f", (double)value); break;
    case 2:  snprintf(out, n, "%.2f", (double)value); break;
    default: snprintf(out, n, "%.3f", (double)value); break;
    }
}

void ui_chevron_left(gfx_canvas_t *c, int x, int cy, int size, gfx_color_t color)
{
    for (int dy = -size; dy <= size; ++dy) {
        int inset = (dy < 0) ? -dy : dy;
        int span = size - inset + 1;
        if (span > 0) {
            gfx_hline(c, x + inset, cy + dy, span, color);
        }
    }
}

static int tag_width(const char *title, bool chevron)
{
    int tw = gfx_text_width(UI_FONT_HEAD, title ? title : "", 1);
    return (chevron ? 30 : 12) + tw + 14;
}

gfx_rect_t ui_home_tag_rect(const char *title)
{
    return gfx_rect_make(UI_TAG_X, UI_TAG_Y, tag_width(title, true), UI_TAG_H);
}

void ui_home_tag(gfx_canvas_t *c, const char *title, bool pressed)
{
    gfx_rect_t r = ui_home_tag_rect(title);
    gfx_color_t fill = pressed ? gfx_lerp(UI_ACCENT, GFX_WHITE, 120) : UI_ACCENT;

    gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 0, 8, 0, 8, fill);
    ui_chevron_left(c, r.x + 10, r.y + r.h / 2, 7, UI_TEXT_ON_LIGHT);
    gfx_text(c, r.x + 30, r.y + 1, title ? title : "", UI_FONT_HEAD,
             UI_TEXT_ON_LIGHT, 1);
}

void ui_wordmark(gfx_canvas_t *c, const char *text, gfx_color_t fill)
{
    int w = tag_width(text, false);
    gfx_fill_chamfer_rect_ex(c, UI_TAG_X, UI_TAG_Y, w, UI_TAG_H, 0, 8, 0, 8, fill);
    gfx_text(c, UI_TAG_X + 12, UI_TAG_Y + 1, text ? text : "", UI_FONT_HEAD,
             UI_TEXT_ON_LIGHT, 1);
}

void ui_rule(gfx_canvas_t *c, int x, int y, int w, gfx_color_t color)
{
    gfx_hline(c, x, y, w, color);
}
