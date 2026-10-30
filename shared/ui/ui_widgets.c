/**
 * @file
 * @brief The shared widgets every screen draws with: panels, cards, buttons,
 * pills.
 *
 * One place for the chrome, so every screen draws the same button.  The one
 * piece of logic here is ui_is_light(): a button picks its label colour from
 * the luma of its fill, so the text is legible on a dark panel and on a
 * bright accent alike.
 *
 * SPDX-License-Identifier: MIT
 */

/* ui_text.h first: ui_theme.h defines UI_TEXT as a colour, the table's
 * entry macro of the same name. */
#include "ui_text.h"
#include "ui_widgets.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui_screen.h"

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
    /* A short accent stroke on the top-right corner, so a grid of identical
     * panels does not read as a spreadsheet. */
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

bool ui_text_fits(const gfx_font_t *f, const char *s, int w, int h, int pad)
{
    if (f == NULL || gfx_text_height(f, 1) > h) {
        return false;
    }
    return gfx_text_width(f, (s != NULL) ? s : "", 1) + 2 * pad <= w;
}

const gfx_font_t *ui_button_face(const char *label, int w, int h, int pad,
                                 const gfx_font_t *big,
                                 const gfx_font_t *small)
{
    return ui_text_fits(big, label, w, h, pad) ? big : small;
}

void ui_button(gfx_canvas_t *c, gfx_rect_t r, const char *label,
               gfx_color_t fill, bool pressed, bool enabled)
{
    ui_button_font(c, r, label, fill, pressed, enabled, UI_FONT_LABEL);
}

void ui_button_font(gfx_canvas_t *c, gfx_rect_t r, const char *label,
                    gfx_color_t fill, bool pressed, bool enabled,
                    const gfx_font_t *f)
{
    /* The label colour of an enabled button follows the luma of the fill:
     * near-black on an accent-coloured button, light on a panel-coloured
     * one.  A button that takes no press is ui_button_off_fill(). */
    gfx_color_t body = enabled ? fill : ui_button_off_fill();
    gfx_color_t text = enabled ? (ui_is_light(body) ? UI_TEXT_ON_LIGHT
                                                    : UI_TEXT)
                               : UI_TEXT_FAINT;

    if (pressed && enabled) {
        body = gfx_lerp(body, GFX_WHITE, 110);
    }

    gfx_fill_round_rect(c, r.x, r.y, r.w, r.h, UI_R_CTL, body);
    if (enabled) {
        /* The same hairline the cards get, so a button reads as cut from the
         * same material rather than pasted onto it. */
        gfx_hline(c, r.x + UI_R_CTL, r.y + 1, r.w - 2 * UI_R_CTL,
                  gfx_lerp(body, GFX_WHITE, 38));
    } else {
        gfx_draw_round_rect(c, r.x, r.y, r.w, r.h, UI_R_CTL,
                            gfx_lerp(body, UI_EDGE, 160));
    }
    if (label) {
        gfx_text_in(c, r, label, f, text, 1, GFX_ALIGN_CENTER);
    }
}

gfx_color_t ui_button_off_fill(void)
{
    return UI_BG;
}

/*
 * The surface everything else sits on: a raised fill with a hairline edge.
 */
void ui_card(gfx_canvas_t *c, gfx_rect_t r, gfx_color_t fill)
{
    gfx_fill_round_rect(c, r.x, r.y, r.w, r.h, UI_R_CARD, fill);
    gfx_draw_round_rect(c, r.x, r.y, r.w, r.h, UI_R_CARD, UI_EDGE);
    /*
     * One hairline inside the top edge and one along the bottom.  Light comes
     * from above, so the top inside edge catches it and the bottom sits in
     * its own shadow; two lines do what a gradient would, for two rows of
     * pixels instead of the card's whole height.
     */
    gfx_hline(c, r.x + UI_R_CARD, r.y + 1, r.w - 2 * UI_R_CARD,
              gfx_lerp(fill, GFX_WHITE, 24));
    gfx_hline(c, r.x + UI_R_CARD, r.y + r.h - 2, r.w - 2 * UI_R_CARD,
              gfx_lerp(fill, GFX_BLACK, 40));
}

int ui_text_wrap(gfx_canvas_t *c, gfx_rect_t first, int pitch, const char *t,
                 const gfx_font_t *f, gfx_color_t col)
{
    if (t == NULL || f == NULL) {
        return 0;
    }
    const int m = gfx_text_width(f, "M", 1);
    const int cells = (m > 0) ? first.w / m : 0;
    if (cells < 1) {
        return 0;
    }
    char line[128];
    int n = 0;
    while (*t == ' ') {
        ++t;
    }
    while (*t != '\0') {
        /* The whole rest, or up to the last space inside the line, or the
         * line's cells of a word longer than it. */
        size_t len = strlen(t);
        if (gfx_text_cells(t) > cells) {
            const size_t max = gfx_text_prefix(t, cells);
            len = max;
            for (size_t i = max; i > 0u; --i) {
                if (t[i] == ' ') {
                    len = i;
                    break;
                }
            }
        }
        if (c != NULL) {
            const size_t k = (len < sizeof(line)) ? len : sizeof(line) - 1u;
            memcpy(line, t, k);
            line[k] = '\0';
            gfx_text_in(c, gfx_rect_make(first.x, first.y + n * pitch,
                                         first.w, first.h),
                        line, f, col, 1, GFX_ALIGN_LEFT);
        }
        ++n;
        t += len;
        while (*t == ' ') {
            ++t;
        }
    }
    return n;
}

void ui_pill_ink(gfx_canvas_t *c, gfx_rect_t r, const char *label,
                 gfx_color_t dot, gfx_color_t fill, gfx_color_t ink,
                 int inset, const gfx_font_t *f)
{
    gfx_fill_round_rect(c, r.x, r.y, r.w, r.h, r.h / 2, fill);
    const int cy = r.y + r.h / 2;
    /* A 4px dot in a 6px ring; 5 in 7 on a pill 40px tall or more. */
    const int rd = (r.h >= 40) ? 5 : 4;
    gfx_fill_circle(c, r.x + inset / 2, cy, rd, dot);
    gfx_draw_circle(c, r.x + inset / 2, cy, rd + 2, gfx_lerp(dot, fill, 130));
    if (label) {
        gfx_text(c, r.x + inset, cy - gfx_text_height(f, 1) / 2, label, f,
                 ink, 1);
    }
}

void ui_pill_font(gfx_canvas_t *c, gfx_rect_t r, const char *label,
                  gfx_color_t dot, gfx_color_t fill, int inset,
                  const gfx_font_t *f)
{
    ui_pill_ink(c, r, label, dot, fill, UI_TEXT, inset, f);
}

void ui_pill(gfx_canvas_t *c, gfx_rect_t r, const char *label,
             gfx_color_t dot, gfx_color_t fill)
{
    ui_pill_font(c, r, label, dot, fill, UI_PILL_INSET, UI_FONT_LABEL);
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

/* The tag's text from its left edge: past the chevron, or 12px in. */
/** @brief A home tag's text from its left edge, past the chevron: 28px. */
#define TAG_TEXT_X_HOME 28
/** @brief The wordmark's text from the tag's left edge: 12px. */
#define TAG_TEXT_X_MARK 12
/** @brief The tag's text to its right edge: 12px. */
#define TAG_TEXT_PAD_R  12

static int tag_width(const char *title, bool chevron)
{
    int tw = gfx_text_width(UI_FONT_TAG, title ? title : "", 1);
    return (chevron ? TAG_TEXT_X_HOME : TAG_TEXT_X_MARK) + tw
           + TAG_TEXT_PAD_R;
}

gfx_rect_t ui_home_tag_rect(const char *title)
{
    return gfx_rect_make(UI_TAG_X, UI_TAG_Y, tag_width(title, true), UI_TAG_H);
}

void ui_tile(gfx_canvas_t *c, gfx_rect_t r, const ui_tile_look_t *look,
             ui_icon_fn icon, const char *name, const char *line,
             bool pressed)
{
    gfx_fill_round_rect(c, r.x, r.y, r.w, r.h, UI_R_CARD,
                        pressed ? UI_PANEL_HI : UI_PANEL);
    gfx_draw_round_rect(c, r.x, r.y, r.w, r.h, UI_R_CARD, UI_EDGE);
    if (look == NULL) {
        return;
    }

    if (icon != NULL) {
        /* Centred over the name and the line, its box's top at icon_dy. */
        icon(c, r.x + r.w / 2, r.y + look->icon_dy + look->icon / 2,
             look->icon, UI_ACCENT);
    }

    /* Clipped to the tile less its padding: centring a string wider than
     * its tile puts the overhang on the neighbour. */
    const int16_t tx = (int16_t)(r.x + UI_TILE_PAD);
    const int16_t tw = (int16_t)(r.w - 2 * UI_TILE_PAD);
    const gfx_font_t *nf = look->name_f;
    const gfx_rect_t nr = { tx, (int16_t)(r.y + look->name_dy), tw,
                            (int16_t)gfx_text_height(nf, 1) };
    gfx_text_in(c, nr, name ? name : "", nf, UI_TEXT, 1, GFX_ALIGN_CENTER);

    /* A line wider than the padded tile breaks at the last space that fits
     * and takes a second row line_pitch px under the first, where the tile
     * has one; a line with no such space is clipped. */
    const gfx_font_t *lf = look->line_f;
    const int lh = gfx_text_height(lf, 1);
    const char *ln = (line != NULL) ? line : "";
    const int cells = tw / gfx_text_width(lf, "M", 1);
    size_t cut = strlen(ln);
    if (gfx_text_cells(ln) > cells && look->line_pitch > 0
        && look->line_dy + look->line_pitch + lh <= r.h) {
        /* The prefix of a line that does not fit is shorter than the line:
         * i < cut keeps the scan inside it. */
        for (size_t i = gfx_text_prefix(ln, cells); i > 0u && i < cut; --i) {
            if (ln[i] == ' ') {
                cut = i;
                break;
            }
        }
    }
    char first[64];
    const size_t k = (cut < sizeof(first)) ? cut : sizeof(first) - 1u;
    memcpy(first, ln, k);
    first[k] = '\0';
    const gfx_rect_t lr = { tx, (int16_t)(r.y + look->line_dy), tw,
                            (int16_t)lh };
    gfx_text_in(c, lr, first, lf, UI_TEXT_DIM, 1, GFX_ALIGN_CENTER);
    if (ln[cut] != '\0') {
        const gfx_rect_t lr2 = { tx, (int16_t)(lr.y + look->line_pitch), tw,
                                 (int16_t)lh };
        gfx_text_in(c, lr2, ln + cut + 1, lf, UI_TEXT_DIM, 1,
                    GFX_ALIGN_CENTER);
    }
}

void ui_home_tag(gfx_canvas_t *c, const char *title, bool pressed)
{
    gfx_rect_t r = ui_home_tag_rect(title);
    gfx_color_t fill = pressed ? gfx_lerp(UI_ACCENT, GFX_WHITE, 120) : UI_ACCENT;

    gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 0, UI_CHAMFER, 0,
                             UI_CHAMFER, fill);
    ui_chevron_left(c, r.x + 8, r.y + r.h / 2, 9, UI_TEXT_ON_LIGHT);
    gfx_text(c, r.x + TAG_TEXT_X_HOME,
             r.y + (r.h - gfx_text_height(UI_FONT_TAG, 1)) / 2,
             title ? title : "", UI_FONT_TAG, UI_TEXT_ON_LIGHT, 1);
}

void ui_wordmark(gfx_canvas_t *c, const char *text, gfx_color_t fill)
{
    int w = tag_width(text, false);
    gfx_fill_chamfer_rect_ex(c, UI_TAG_X, UI_TAG_Y, w, UI_TAG_H, 0,
                             UI_CHAMFER, 0, UI_CHAMFER, fill);
    gfx_text(c, UI_TAG_X + TAG_TEXT_X_MARK,
             UI_TAG_Y + (UI_TAG_H - gfx_text_height(UI_FONT_TAG, 1)) / 2,
             text ? text : "", UI_FONT_TAG,
             ui_is_light(fill) ? UI_TEXT_ON_LIGHT : UI_PANEL, 1);
}

void ui_rule(gfx_canvas_t *c, int x, int y, int w, gfx_color_t color)
{
    gfx_hline(c, x, y, w, color);
}

const gfx_font_t *ui_choice_font(int h)
{
    return (h >= UI_CHOICE_ROW_H) ? UI_FONT_TAG : UI_FONT_HEAD;
}

const gfx_font_t *ui_choice_text_font(int h, int w, const char *text)
{
    const gfx_font_t *f = ui_choice_font(h);
    return ui_text_fits(f, text, w, h, 0) ? f : UI_FONT_HEAD;
}

/**
 * @brief A choice face's text and chevron, as tall a face takes them.
 * @param c    Canvas.
 * @param r    The face.
 * @param text Its text, or NULL for none.
 * @param ink  The text's colour.
 * @param mark The chevron's colour.
 */
static void choice_face_ink(gfx_canvas_t *c, gfx_rect_t r, const char *text,
                            gfx_color_t ink, gfx_color_t mark)
{
    const bool tall = r.h >= UI_CHOICE_ROW_H;
    /* A 7px chevron 22px from the right edge; 11px and 28px when tall.
     * The text ends 4px before the chevron's first column. */
    const int span = tall ? 11 : 7;
    const int inset = tall ? 12 : 10;
    const int room = r.w - inset - (tall ? 28 : 22) - span - 4;
    const gfx_font_t *f = ui_choice_text_font(r.h, room, text);
    gfx_text(c, r.x + inset, r.y + (r.h - gfx_text_height(f, 1)) / 2,
             text ? text : "", f, ink, 1);
    const int cx = r.x + r.w - (tall ? 28 : 22), cy = r.y + r.h / 2;
    const int half = span / 2;
    for (int i = 0; i < span; ++i) {
        const int dy = (i <= half) ? i : span - 1 - i;
        gfx_fill_rect(c, cx - span + 1 + i, cy - half + dy, 1, tall ? 3 : 2,
                      mark);
    }
}

void ui_choice_face(gfx_canvas_t *c, gfx_rect_t r, const char *text,
                    bool pressed, bool open)
{
    gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 10, 0, 10, 0,
                             pressed ? UI_PANEL_HI : UI_PANEL);
    gfx_draw_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 10, 0, 10, 0,
                             open ? UI_ACCENT : UI_EDGE);
    choice_face_ink(c, r, text, UI_TEXT, UI_TEXT_DIM);
}

void ui_choice_face_locked(gfx_canvas_t *c, gfx_rect_t r, const char *text)
{
    gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 10, 0, 10, 0,
                             UI_PANEL_SUNK);
    gfx_draw_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 10, 0, 10, 0, UI_EDGE);
    choice_face_ink(c, r, text, UI_TEXT_DIM, UI_EDGE);
}

void ui_choice_list(gfx_canvas_t *c, gfx_rect_t r)
{
    /* A shadow rather than a border, so the list reads as being over what
     * it covers rather than beside it. */
    gfx_fill_chamfer_rect_ex(c, r.x + 4, r.y + 4, r.w, r.h, 10, 0, 10, 0,
                             UI_PANEL_SUNK);
    gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 10, 0, 10, 0, UI_PANEL);
    gfx_draw_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 10, 0, 10, 0, UI_ACCENT);
}

void ui_choice_row(gfx_canvas_t *c, gfx_rect_t r, const char *text,
                   bool chosen, bool pressed)
{
    if (chosen || pressed) {
        gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 6, 0, 6, 0,
                                 chosen ? UI_ACCENT : UI_PANEL_HI);
    }
    const gfx_font_t *f = ui_choice_text_font(r.h, r.w - 24, text);
    gfx_text(c, r.x + 12, r.y + (r.h - gfx_text_height(f, 1)) / 2,
             text ? text : "", f, chosen ? UI_TEXT_ON_LIGHT : UI_TEXT, 1);
}

gfx_color_t ui_hold_fill(gfx_color_t base, gfx_color_t target, float held_s)
{
    if (held_s <= 0.0f) {
        return base;
    }
    if (held_s >= UI_HOLD_S) {
        return target;
    }
    const uint8_t t = (uint8_t)(held_s / UI_HOLD_S * 255.0f + 0.5f);
    return gfx_lerp(base, target, t);
}

void ui_hold_reset(ui_hold_t *h)
{
    if (h == NULL) {
        return;
    }
    h->held_s = 0.0f;
    h->down = false;
    h->fired = false;
    h->flash_ms = 0;
    h->flash_new = false;
}

float ui_hold_credit(float dt_s)
{
    /*
     * A frame is credited with the time it took, and a late frame took a
     * long time.  The duration is measured at the top of the frame and
     * applied at the end of it, so a frame that dispatched the press it is
     * now crediting would hand a hold begun this frame the whole of that
     * frame's stall -- an arm two seconds after a press milliseconds old.
     *
     * UI_HOLD_MAX_CREDIT_S caps what one frame is worth, so a hold spans at
     * least UI_HOLD_S / UI_HOLD_MAX_CREDIT_S frames with the press standing.
     * A bench drawing at its ordinary rate is far below the cap.
     *
     * Every hold in the tree goes through here, including the ones that keep
     * their own timer rather than a ui_hold_t.
     */
    if (dt_s > UI_HOLD_MAX_CREDIT_S) {
        return UI_HOLD_MAX_CREDIT_S;
    }
    return (dt_s < 0.0f) ? 0.0f : dt_s;
}

void ui_hold_begin(ui_hold_t *h, int hint)
{
    if (h == NULL) {
        return;
    }
    h->down = true;
    h->held_s = 0.0f;
    h->hint = hint;
}

bool ui_hold_leave(ui_hold_t *h)
{
    /* Only while the hold is running.  Once it has fired, the press is
     * waiting for the release that consumes it. */
    if (h == NULL || !h->down || h->fired) {
        return false;
    }
    h->down = false;
    h->held_s = 0.0f;
    return true;
}

bool ui_hold_end(ui_hold_t *h, const touch_event_t *up, gfx_rect_t rect)
{
    if (h == NULL) {
        return false;
    }
    const bool fired = h->fired;
    /* A press still on the control that lifted before it completed.  The
     * band is the router's: a screen without one shows no alert, and an
     * alert set there would show once the next screen has it. */
    if (h->down && !fired && up != NULL && touch_event_is_tap_up(up)
        && gfx_rect_contains(rect, up->point.x, up->point.y)
        && ui_router_stop_live()) {
        ui_router_set_alert(ui_tr((ui_text_id_t)h->hint));
    }
    h->down = false;
    h->held_s = 0.0f;
    h->fired = false;
    return fired;
}

bool ui_hold_tick(ui_hold_t *h, float dt_s)
{
    if (h == NULL || !h->down || h->fired) {
        return false;
    }
    h->held_s += ui_hold_credit(dt_s);
    if (h->held_s < UI_HOLD_S) {
        return false;
    }
    /* The hold is the gesture: it completes here, not on the release, so
     * letting go early asks for nothing. */
    h->held_s = UI_HOLD_S;
    h->fired = true;
    return true;
}

void ui_hold_reached(ui_hold_t *h)
{
    if (h == NULL) {
        return;
    }
    h->flash_ms = UI_HOLD_FLASH_MS;
    h->flash_new = true;
    h->held_s = 0.0f;
}

bool ui_hold_left(ui_hold_t *h)
{
    if (h == NULL) {
        return false;
    }
    h->held_s = 0.0f;
    if (!h->down) {
        return false;
    }
    h->down = false;
    h->fired = false;
    return true;
}

int ui_hold_flash_left(int ms_left, float dt_s)
{
    if (ms_left <= 0) {
        return 0;
    }
    /* Rounded to the ms; a NaN or a negative step spends nothing. */
    const float ms = dt_s * 1000.0f + 0.5f;
    if (!(ms > 0.0f)) {
        return ms_left;
    }
    /* No further than the start of the next phase: the phase now drawn
     * ends at the next multiple of UI_HOLD_FLASH_PHASE_MS run. */
    const int run = UI_HOLD_FLASH_MS - ms_left;
    const int to_next = (run < 0) ? ms_left
        : UI_HOLD_FLASH_PHASE_MS - run % UI_HOLD_FLASH_PHASE_MS;
    const int spend = (ms >= (float)to_next) ? to_next : (int)ms;
    return (spend >= ms_left) ? 0 : ms_left - spend;
}

void ui_hold_flash_step(ui_hold_t *h, float dt_s)
{
    if (h == NULL) {
        return;
    }
    if (h->flash_new) {
        h->flash_new = false;
        return;
    }
    h->flash_ms = ui_hold_flash_left(h->flash_ms, dt_s);
}

gfx_color_t ui_hold_flash(gfx_color_t settled, int ms_left)
{
    if (ms_left <= 0 || ms_left > UI_HOLD_FLASH_MS) {
        return settled;
    }
    const int run = UI_HOLD_FLASH_MS - ms_left;
    switch ((run / UI_HOLD_FLASH_PHASE_MS) % UI_HOLD_FLASH_CYCLE) {
    case 0:  return GFX_WHITE;
    case 1:  return GFX_BLACK;
    default: return settled;
    }
}
