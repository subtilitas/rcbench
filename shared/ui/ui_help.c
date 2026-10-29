/**
 * @file
 * @brief The screen guide's pages, geometry and drawing (ui_help.h).
 *
 * The dock is 480 x 488px, 8px in from the panel's left or right edge,
 * from y 64 to 552: 4px under the band and 4px above the alert band, so an
 * alert covers none of it.  Inside, in panel coordinates from the dock's
 * top: a 36px header with the page's title and "n / m", the items from
 * 44px, the web line and the buttons PREV, NEXT and CLOSE at the bottom.
 * An item is its number in a 28px badge, then its label in the accent
 * colour, two spaces and its text in the 10 x 20 face, broken at spaces
 * into rows of 41 cells, 21px apart, 8px between two items.  Five items of
 * 3 rows take 347px of the 351px between the header and the web line.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_help.h"

#include <stdio.h>
#include <string.h>

#include "ui_theme.h"
#include "ui_widgets.h"

/** @brief The panel's width, UI_PANEL_W px. */
#define PANEL_W UI_PANEL_W
/** @brief The dock's width: 480px. */
#define DOCK_W     480
/** @brief The dock's height: 488px, y 64 to 552. */
#define DOCK_H     488
/** @brief The dock's distance to the panel's edge, the band and the
 *         live half: 8px. */
#define DOCK_M     8
/** @brief The dock's top: 4px under the band. */
#define DOCK_Y     (UI_BAND_H + 4)
/** @brief The text's padding inside the dock: 16px. */
#define PAD        16
/** @brief The header's height: 36px. */
#define HEAD_H     36
/** @brief A row of text to the next: 21px. */
#define PITCH      21
/** @brief More between two items: 8px. */
#define ITEM_GAP   8
/** @brief A badge's side: 28px. */
#define BADGE      28
/** @brief A badge's distance to its control's corner: 4px. */
#define BADGE_IN   4
/** @brief The item text's start right of the badge: 36px from the pad. */
#define TEXT_DX    (BADGE + 8)
/** @brief The cells of an item's row: 41. */
#define CELLS      ((DOCK_W - 2 * PAD - TEXT_DX) / 10)
/** @brief The cells of the note's and the web line's rows: 44. */
#define CELLS_FULL ((DOCK_W - 2 * PAD) / 10)
/** @brief A button: 140 x 48px. */
#define BTN_W      140
/** @brief A button's height: 48px. */
#define BTN_H      UI_BTN_H
/** @brief Between two buttons: 10px. */
#define BTN_GAP    10
/** @brief The buttons' top: 8px above the dock's bottom less their
 *         height, y 496. */
#define BTN_Y      (DOCK_Y + DOCK_H - DOCK_M - BTN_H)
/** @brief The web line's top: a row and 8px above the buttons, y 467. */
#define WEB_Y      (BTN_Y - ITEM_GAP - PITCH)
/** @brief The brackets' distance outside their control: 3px, 2px wide. */
#define OUTLINE    3
/** @brief A bracket's leg: 16px, or half the side of a smaller control. */
#define CORNER     16

_Static_assert(DOCK_Y + DOCK_H + 4 == UI_ALERT_Y,
               "the dock ends 4px above the alert band");
_Static_assert(UI_HELP_ITEMS * UI_HELP_ROWS * PITCH
                   + (UI_HELP_ITEMS - 1) * ITEM_GAP
                   <= WEB_Y - ITEM_GAP - (DOCK_Y + HEAD_H + ITEM_GAP),
               "five items of three rows fit above the web line");

/** @brief An item's label and text as the dock shows them.  Static: the
 *         guide is drawn by the render loop alone, and the render task's
 *         stack is budgeted. */
static char s_text[400];
/** @brief One row of s_text, or of another text the dock draws. */
static char s_row[200];

int ui_help_count(const ui_screen_t *scr, bool armed)
{
    if (scr == NULL || scr->help_count == NULL || scr->help_page == NULL) {
        return 0;
    }
    const int all = scr->help_count();
    if (!armed) {
        return (all > 0) ? all : 0;
    }
    int n = 0;
    for (int i = 0; i < all; ++i) {
        ui_help_page_t p;
        if (scr->help_page(i, &p) && p.armed_ok) {
            ++n;
        }
    }
    return n;
}

bool ui_help_page(const ui_screen_t *scr, bool armed, int n,
                  ui_help_page_t *out)
{
    if (scr == NULL || scr->help_count == NULL || scr->help_page == NULL
        || out == NULL || n < 0) {
        return false;
    }
    const int all = scr->help_count();
    for (int i = 0; i < all; ++i) {
        memset(out, 0, sizeof(*out));
        if (!scr->help_page(i, out) || (armed && !out->armed_ok)) {
            continue;
        }
        if (n-- == 0) {
            if (out->count > UI_HELP_ITEMS) {
                out->count = UI_HELP_ITEMS;
            }
            return true;
        }
    }
    return false;
}

bool ui_help_armed_note(const ui_screen_t *scr, bool armed)
{
    return armed && ui_help_count(scr, true) < ui_help_count(scr, false);
}

/**
 * @brief One step of the 32-bit FNV-1a hash.
 * @param h The hash so far.
 * @param p The bytes.
 * @param n Their count.
 * @return The hash with the bytes.
 */
static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; ++i) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h;
}

uint32_t ui_help_set_key(const ui_screen_t *scr, bool armed)
{
    const int count = ui_help_count(scr, armed);
    uint32_t h = fnv(2166136261u, &count, sizeof(count));
    for (int i = 0; i < count; ++i) {
        ui_help_page_t p;
        if (ui_help_page(scr, armed, i, &p)) {
            h = fnv(h, &p.title, sizeof(p.title));
        }
    }
    return h;
}

gfx_rect_t ui_help_dock_rect(ui_help_side_t side)
{
    const int x = (side == UI_HELP_DOCK_LEFT) ? DOCK_M
                                              : PANEL_W - DOCK_M - DOCK_W;
    return gfx_rect_make(x, DOCK_Y, DOCK_W, DOCK_H);
}

gfx_rect_t ui_help_live_rect(ui_help_side_t side)
{
    const int w = PANEL_W - DOCK_W - 2 * DOCK_M;
    return gfx_rect_make((side == UI_HELP_DOCK_LEFT) ? PANEL_W - w : 0,
                         UI_BAND_H, w, UI_BODY_H);
}

/**
 * @brief The half of the body the dock takes, in panel coordinates.
 * @param side The dock's side.
 * @return The rect beside ui_help_live_rect().
 */
static gfx_rect_t column_rect(ui_help_side_t side)
{
    const gfx_rect_t live = ui_help_live_rect(side);
    return gfx_rect_make((side == UI_HELP_DOCK_LEFT) ? 0 : live.w,
                         UI_BAND_H, PANEL_W - live.w, UI_BODY_H);
}

gfx_rect_t ui_help_button_rect(ui_help_side_t side, ui_help_hit_t hit)
{
    if (hit != UI_HELP_HIT_PREV && hit != UI_HELP_HIT_NEXT
        && hit != UI_HELP_HIT_CLOSE) {
        return gfx_rect_make(0, 0, 0, 0);
    }
    const gfx_rect_t d = ui_help_dock_rect(side);
    const int k = (int)hit - (int)UI_HELP_HIT_PREV;
    const int x0 = d.x + (DOCK_W - 3 * BTN_W - 2 * BTN_GAP) / 2;
    return gfx_rect_make(x0 + k * (BTN_W + BTN_GAP), BTN_Y, BTN_W, BTN_H);
}

gfx_rect_t ui_help_badge_rect(const ui_help_page_t *p, int i)
{
    const gfx_rect_t r = p->item[i].rect;
    if (r.y < 0) {
        /* In the band: under it, below the control's left end. */
        return gfx_rect_make(r.x, UI_BAND_H + BADGE_IN, BADGE, BADGE);
    }
    const int y = UI_BAND_H + r.y;
    if (r.h < 2 * BADGE || r.w < 2 * BADGE) {
        /* Too small to carry one inside: left of it, centred, where the
         * live half has the room. */
        const int x = r.x - BADGE_IN - BADGE;
        if (x >= ui_help_live_rect(p->side).x) {
            return gfx_rect_make(x, y + (r.h - BADGE) / 2, BADGE, BADGE);
        }
    }
    return gfx_rect_make(r.x + BADGE_IN, y + BADGE_IN, BADGE, BADGE);
}

ui_help_hit_t ui_help_hit(const ui_help_page_t *p, int x, int y)
{
    if (p == NULL) {
        return UI_HELP_HIT_NONE;
    }
    for (int h = UI_HELP_HIT_PREV; h <= UI_HELP_HIT_CLOSE; ++h) {
        if (gfx_rect_contains(ui_help_button_rect(p->side, (ui_help_hit_t)h),
                              x, y)) {
            return (ui_help_hit_t)h;
        }
    }
    if (gfx_rect_contains(ui_help_dock_rect(p->side), x, y)) {
        return UI_HELP_HIT_DOCK;
    }
    for (int i = 0; i < p->count && i < UI_HELP_ITEMS; ++i) {
        if (gfx_rect_contains(ui_help_badge_rect(p, i), x, y)) {
            return UI_HELP_HIT_DOCK;
        }
    }
    return UI_HELP_HIT_NONE;
}

/* ------------------------------------------------------------- the text */

/**
 * @brief The bytes of the row that starts @p s, at most @p cells: broken at
 *        the last space that falls in it, or inside a word where none does.
 * @param s     The text left.
 * @param cells The row's cells.
 * @return The row's bytes.
 */
static size_t row_len(const char *s, int cells)
{
    if (gfx_text_cells(s) <= cells) {
        return strlen(s);
    }
    const size_t max = gfx_text_prefix(s, cells);
    for (size_t i = max; i > 0u; --i) {
        if (s[i] == ' ') {
            return i;
        }
    }
    return max;
}

/**
 * @brief Draw @p len bytes of @p s at (@p x, @p y) in @p col.
 * @param c   The canvas.
 * @param x   The left edge.
 * @param y   The top.
 * @param s   The text.
 * @param len Its bytes to draw.
 * @param col The colour.
 */
static void text_n(gfx_canvas_t *c, int x, int y, const char *s, size_t len,
                   gfx_color_t col)
{
    const size_t k = (len < sizeof(s_row)) ? len : sizeof(s_row) - 1u;
    memcpy(s_row, s, k);
    s_row[k] = '\0';
    gfx_text(c, x, y, s_row, UI_FONT_BAND, col, 1);
}

/**
 * @brief @p t broken at spaces into rows of @p cells from (@p x, @p y), the
 *        first @p lead bytes in @p accent and the rest in the text colour.
 *        Draws when @p c is not NULL and counts either way, so the dock
 *        is measured by the code that draws it.
 * @param c      The canvas, or NULL to count.
 * @param x      The rows' left edge.
 * @param y      The first row's top.
 * @param t      The text.
 * @param lead   The bytes at its start drawn in @p accent.
 * @param cells  The cells of a row.
 * @param accent The colour of the lead.
 * @return The rows.
 */
static int wrap(gfx_canvas_t *c, int x, int y, const char *t, size_t lead,
                int cells, gfx_color_t accent)
{
    int rows = 0;
    const char *at = t;
    while (*at != '\0') {
        const size_t len = row_len(at, cells);
        if (c != NULL) {
            const size_t done = (size_t)(at - t);
            const size_t acc = (done >= lead) ? 0u
                               : (lead - done < len) ? lead - done : len;
            if (acc > 0u) {
                text_n(c, x, y + rows * PITCH, at, acc, accent);
            }
            size_t rest = acc;
            while (rest < len && at[rest] == ' ') {
                ++rest;
            }
            if (rest < len) {
                text_n(c, x + 10 * (int)rest, y + rows * PITCH, at + rest,
                       len - rest, UI_TEXT);
            }
        }
        ++rows;
        at += len;
        while (*at == ' ') {
            ++at;
        }
    }
    return rows;
}

/**
 * @brief Item @p i of @p p as the dock shows it, label first, into s_text.
 * @param p The page.
 * @param i The item.
 * @return The label's bytes at its start.
 */
static size_t item_text(const ui_help_page_t *p, int i)
{
    const char *label = (p->item[i].label != NULL) ? p->item[i].label : "";
    snprintf(s_text, sizeof(s_text), "%s  %s", label,
             ui_tr(p->item[i].text));
    return strlen(label);
}

int ui_help_item_rows(const ui_help_page_t *p, int i)
{
    if (p == NULL || i < 0 || i >= p->count || i >= UI_HELP_ITEMS) {
        return 0;
    }
    const size_t lead = item_text(p, i);
    return wrap(NULL, 0, 0, s_text, lead, CELLS, 0);
}

/**
 * @brief The items' first row's top: under the header, and under the
 *        ARMED note when it shows.
 * @param note The note shows.
 * @return The panel y.
 */
static int items_top(bool note)
{
    int y = DOCK_Y + HEAD_H + ITEM_GAP;
    if (note) {
        y += wrap(NULL, 0, 0, TR(HG_ARMED_NOTE), 0, CELLS_FULL, 0) * PITCH
             + ITEM_GAP;
    }
    return y;
}

int ui_help_text_bottom(const ui_help_page_t *p, bool note)
{
    int y = items_top(note);
    for (int i = 0; p != NULL && i < p->count && i < UI_HELP_ITEMS; ++i) {
        y += ((i > 0) ? ITEM_GAP : 0) + ui_help_item_rows(p, i) * PITCH;
    }
    return y;
}

int ui_help_text_limit(void) { return WEB_Y - ITEM_GAP; }

uint32_t ui_help_dock_key(const ui_help_page_t *p, int n, int count,
                          bool note, ui_help_hit_t btn)
{
    uint32_t h = 2166136261u;
    const int head[7] = { n, count, (int)note, (int)btn, (int)ui_theme_get(),
                          (int)ui_text_language(),
                          (p != NULL) ? (int)p->side : -1 };
    h = fnv(h, head, sizeof(head));
    if (p == NULL) {
        return h;
    }
    h = fnv(h, &p->title, sizeof(p->title));
    for (int i = 0; i < p->count && i < UI_HELP_ITEMS; ++i) {
        const char *label = (p->item[i].label != NULL) ? p->item[i].label
                                                       : "";
        h = fnv(h, label, strlen(label) + 1u);
        h = fnv(h, &p->item[i].text, sizeof(p->item[i].text));
    }
    return h;
}

/**
 * @brief A badge: a 28px accent disc with @p num in the dark text of the
 *        bright fills.
 * @param c   The canvas.
 * @param r   The badge's rect.
 * @param num The number, 1 to UI_HELP_ITEMS.
 */
static void badge(gfx_canvas_t *c, gfx_rect_t r, int num)
{
    const char d[2] = { (char)('0' + num), '\0' };
    gfx_fill_circle(c, r.x + BADGE / 2, r.y + BADGE / 2, BADGE / 2,
                    UI_ACCENT);
    gfx_text_in(c, r, d, UI_FONT_BAND, UI_TEXT_ON_LIGHT, 1,
                GFX_ALIGN_CENTER);
}

void ui_help_render_dock(gfx_canvas_t *c, const ui_help_page_t *p, int n,
                         int count, bool note, ui_help_hit_t btn)
{
    if (c == NULL || p == NULL) {
        return;
    }
    const gfx_rect_t col = column_rect(p->side);
    gfx_fill_rect(c, col.x, col.y, col.w, col.h, UI_BG);
    const gfx_rect_t d = ui_help_dock_rect(p->side);
    ui_panel(c, d, ui_tr(p->title), UI_ACCENT);

    char num[16];
    snprintf(num, sizeof(num), "%d / %d", n + 1, count);
    gfx_text(c, d.x + DOCK_W - PAD - gfx_text_width(UI_FONT_BAND, num, 1),
             d.y + 8, num, UI_FONT_BAND, UI_TEXT_DIM, 1);

    const int x = d.x + PAD;
    if (note) {
        (void)wrap(c, x, d.y + HEAD_H + ITEM_GAP, TR(HG_ARMED_NOTE),
                   strlen(TR(HG_ARMED_NOTE)), CELLS_FULL, UI_WARN);
    }
    int y = items_top(note);
    for (int i = 0; i < p->count && i < UI_HELP_ITEMS; ++i) {
        badge(c, gfx_rect_make(x, y - (BADGE - 20) / 2, BADGE, BADGE),
              i + 1);
        const size_t lead = item_text(p, i);
        y += wrap(c, x + TEXT_DX, y, s_text, lead, CELLS, UI_ACCENT) * PITCH
             + ITEM_GAP;
    }
    (void)wrap(c, x, WEB_Y, TR(HG_WEB), 0, CELLS_FULL, 0);

    static const ui_text_id_t k_btn[3] = { TX_LOG_PREV, TX_LOG_NEXT,
                                           TX_FM_CLOSE };
    const bool on[3] = { n > 0, n + 1 < count, true };
    for (int k = 0; k < 3; ++k) {
        const ui_help_hit_t h = (ui_help_hit_t)(UI_HELP_HIT_PREV + k);
        ui_button(c, ui_help_button_rect(p->side, h), ui_tr(k_btn[k]),
                  UI_PANEL_HI, on[k] && btn == h, on[k]);
    }
}

/**
 * @brief The four corners of @p r as brackets, 2px wide, each leg
 *        CORNER px or half the side, in the accent colour.
 * @param c The canvas.
 * @param r The rect the brackets frame.
 */
static void corners(gfx_canvas_t *c, gfx_rect_t r)
{
    const int lw = (r.w / 2 < CORNER) ? r.w / 2 : CORNER;
    const int lh = (r.h / 2 < CORNER) ? r.h / 2 : CORNER;
    const int x1 = r.x + r.w - 2;
    const int y1 = r.y + r.h - 2;
    gfx_fill_rect(c, r.x, r.y, lw, 2, UI_ACCENT);
    gfx_fill_rect(c, r.x, r.y, 2, lh, UI_ACCENT);
    gfx_fill_rect(c, r.x + r.w - lw, r.y, lw, 2, UI_ACCENT);
    gfx_fill_rect(c, x1, r.y, 2, lh, UI_ACCENT);
    gfx_fill_rect(c, r.x, y1, lw, 2, UI_ACCENT);
    gfx_fill_rect(c, r.x, r.y + r.h - lh, 2, lh, UI_ACCENT);
    gfx_fill_rect(c, r.x + r.w - lw, y1, lw, 2, UI_ACCENT);
    gfx_fill_rect(c, x1, r.y + r.h - lh, 2, lh, UI_ACCENT);
}

void ui_help_render_marks(gfx_canvas_t *c, const ui_help_page_t *p)
{
    if (c == NULL || p == NULL) {
        return;
    }
    const gfx_rect_t keep = gfx_clip_get(c);
    const gfx_rect_t band = gfx_rect_make(0, 0, PANEL_W, UI_BAND_H);
    const gfx_rect_t live = ui_help_live_rect(p->side);
    for (int i = 0; i < p->count && i < UI_HELP_ITEMS; ++i) {
        const gfx_rect_t r = p->item[i].rect;
        const bool in_band = r.y < 0;
        const int y = r.y + UI_BAND_H;
        gfx_clip_set(c, keep);
        if (gfx_clip_intersect(c, in_band ? band : live)) {
            corners(c, gfx_rect_make(r.x - OUTLINE, y - OUTLINE,
                                     r.w + 2 * OUTLINE, r.h + 2 * OUTLINE));
        }
        gfx_clip_set(c, keep);
        if (gfx_clip_intersect(c, live)) {
            badge(c, ui_help_badge_rect(p, i), i + 1);
        }
    }
    gfx_clip_set(c, keep);
}
