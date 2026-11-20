/**
 * @file
 * @brief SEARCH's drawing, its geometry, its guide and the Dial's focus
 *        items (wl_search.h).  wl_search.c holds the model and the
 *        touches.
 *
 * The guide's first page marks the list with the dock on the right, the
 * second the device's card and SCAN AGAIN with the dock on the left.  The
 * screen drives no output, so both show while ARMED.  The question has no
 * pages.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

/* ui_text.h before ui_theme.h: ui_text.def ends by undefining UI_TEXT. */
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "wl_search_int.h"

/** @brief The state line's, a bound name's and the question's name's
 *         face: the body face, 12 x 24. */
#define WLS_F_BODY  UI_FONT_BODY
/** @brief The device kind's face: the heading face, 16 x 28. */
#define WLS_F_HEAD  UI_FONT_HEAD
/** @brief Every other text's face: 10 x 20. */
#define WLS_F_LABEL (&gfx_font_10x20)

/** @brief The guide's pages. */
#define GUIDE_PAGES 2

/* ------------------------------------------------------------ geometry */

gfx_rect_t wl_search_row_rect(int i)
{
    if (i < 0 || i >= (int)WLS_FOUND_MAX) {
        return gfx_rect_make(0, 0, 0, 0);
    }
    return gfx_rect_make(WLS_LX, WLS_LY + i * WLS_LP, WLS_LW, WLS_LH);
}

gfx_rect_t wl_search_part(wls_part_t part)
{
    switch (part) {
    case WLS_PART_AGAIN:
        return gfx_rect_make(WLS_SX, WLS_AGAIN_Y, WLS_SW, WLS_BTN_H);
    case WLS_PART_CANCEL:
        return gfx_rect_make(WLS_Q_CANCEL_X, WLS_Q_BTN_Y, WLS_Q_BTN_W,
                             WLS_BTN_H);
    case WLS_PART_BIND:
        return gfx_rect_make(WLS_Q_BIND_X, WLS_Q_BTN_Y, WLS_Q_BTN_W,
                             WLS_BTN_H);
    case WLS_PART_BOX:
        return gfx_rect_make(WLS_BOX_X, WLS_BOX_Y, WLS_BOX_W, WLS_BOX_H);
    case WLS_PART_LIST:
        return gfx_rect_make(WLS_LX, WLS_HEAD_Y, WLS_LW,
                             WLS_LY + ((int)WLS_FOUND_MAX - 1) * WLS_LP
                                 + WLS_LH - WLS_HEAD_Y);
    case WLS_PART_CARD:
        /* Down to the bound name's foot: the body face is 24px tall. */
        return gfx_rect_make(WLS_SX, WLS_KIND_Y, WLS_SW,
                             WLS_NAME_Y + gfx_text_height(WLS_F_BODY, 1)
                                 - WLS_KIND_Y);
    default:
        return gfx_rect_make(0, 0, 0, 0);
    }
}

/* ------------------------------------------------------------- drawing */

/**
 * @brief A line of text left in a box of the screen's width @p w.
 * @param c The canvas.  @param x Its left edge.  @param y Its top.
 * @param w Its width.  @param s The text.  @param f The face.
 * @param ink Its colour.
 */
static void line(gfx_canvas_t *c, int x, int y, int w, const char *s,
                 const gfx_font_t *f, gfx_color_t ink)
{
    gfx_text_in(c, gfx_rect_make(x, y, w, gfx_text_height(f, 1)), s, f, ink,
                1, GFX_ALIGN_LEFT);
}

/**
 * @brief The list: the scan's state line, then the found devices on the
 *        choice list's ground, the bound one accented.
 * @param c The canvas.
 * @param v The view.
 */
static void draw_list(gfx_canvas_t *c, const wls_view_t *v)
{
    char buf[96];
    line(c, WLS_LX, WLS_HEAD_Y, WLS_LW, wl_search_state_line(buf, sizeof(buf)),
         WLS_F_BODY, v->scanning ? UI_TEXT_DIM : UI_TEXT);
    const int n = (int)v->found_n;
    if (n > 0) {
        ui_choice_list(c, gfx_rect_make(WLS_LX - 4, WLS_LY - 4, WLS_LW + 8,
                                        (n - 1) * WLS_LP + WLS_LH + 8));
    }
    for (int i = 0; i < n; ++i) {
        const wls_dev_t *d = &v->found[i];
        const bool bound = v->bound_addr[0] != '\0'
                           && strcmp(d->addr, v->bound_addr) == 0;
        ui_choice_row(c, wl_search_row_rect(i),
                      wl_search_dev_line(d, buf, sizeof(buf)), bound,
                      !wls.asking && wls.have_press
                          && wls.hit == WLS_HIT_DEV0 + i);
    }
}

/**
 * @brief The right column: the device's kind, its link state, the bound
 *        device and SCAN AGAIN.
 * @param c The canvas.
 * @param v The view.
 */
static void draw_card(gfx_canvas_t *c, const wls_view_t *v)
{
    line(c, WLS_SX, WLS_KIND_Y, WLS_SW,
         wls.dev != NULL ? ui_tr(wls.dev->kind) : "", WLS_F_HEAD, UI_ACCENT);
    line(c, WLS_SX, WLS_WORD_Y, WLS_SW, v->word, WLS_F_LABEL, v->word_ink);
    line(c, WLS_SX, WLS_BOUND_Y, WLS_SW, TR(WLS_BOUND), WLS_F_LABEL,
         UI_TEXT_DIM);
    const char *bound = v->bound_name[0] != '\0'   ? v->bound_name
                        : v->bound_addr[0] != '\0' ? v->bound_addr
                                                   : TR(DIAL_BIND_NONE);
    line(c, WLS_SX, WLS_NAME_Y, WLS_SW, bound, WLS_F_BODY,
         v->bound_addr[0] != '\0' ? UI_TEXT : UI_TEXT_DIM);
    ui_button_font(c, wl_search_part(WLS_PART_AGAIN), TR(WL_SCAN_AGAIN),
                   UI_PANEL_HI,
                   !wls.asking && wls.have_press && wls.hit == WLS_HIT_AGAIN,
                   !v->scanning, WLS_F_LABEL);
}

/**
 * @brief The question in place of the screen: the kind in its title, the
 *        device's name, its address and RSSI, the bound device it takes
 *        the place of, the note, CANCEL and BIND.
 * @param c The canvas.
 * @param v The view.
 */
static void draw_ask(gfx_canvas_t *c, const wls_view_t *v)
{
    const gfx_rect_t box = wl_search_part(WLS_PART_BOX);
    char buf[96];
    snprintf(buf, sizeof(buf), TR(WLS_ASK_TITLE),
             wls.dev != NULL ? ui_tr(wls.dev->kind) : "");
    ui_panel(c, box, buf, UI_ACCENT);
    const int x = box.x + WLS_Q_PAD, w = box.w - 2 * WLS_Q_PAD;
    const wls_dev_t *d = &wls.ask;
    line(c, x, box.y + WLS_Q_NAME_Y, w,
         d->name[0] != '\0' ? d->name : d->addr, WLS_F_BODY, UI_TEXT);
    char dbm[12] = "";
    if (d->rssi != 127) {
        snprintf(dbm, sizeof(dbm), "%ddBm", (int)d->rssi);
    }
    snprintf(buf, sizeof(buf), "%s   %s", d->addr, dbm);
    line(c, x, box.y + WLS_Q_ADDR_Y, w, buf, WLS_F_LABEL, UI_TEXT_DIM);
    if (v->bound_addr[0] != '\0' && strcmp(v->bound_addr, d->addr) != 0) {
        snprintf(buf, sizeof(buf), TR(WLS_ASK_WAS),
                 v->bound_name[0] != '\0' ? v->bound_name : v->bound_addr);
        line(c, x, box.y + WLS_Q_WAS_Y, w, buf, WLS_F_LABEL, UI_TEXT_DIM);
    }
    line(c, x, box.y + WLS_Q_NOTE_Y, w, TR(WLS_ASK_KEEP), WLS_F_LABEL,
         UI_TEXT_DIM);
    ui_button_font(c, wl_search_part(WLS_PART_CANCEL), TR(CANCEL),
                   UI_PANEL_HI, wls.have_press && wls.hit == WLS_HIT_CANCEL,
                   true, WLS_F_LABEL);
    ui_button_font(c, wl_search_part(WLS_PART_BIND), TR(WLS_BIND), UI_OK,
                   wls.have_press && wls.hit == WLS_HIT_BIND, true,
                   WLS_F_LABEL);
}

/**
 * @brief What the screen shows as one number: the view's bytes (zeroed
 *        before the device filled them), the question and the press.
 * @param v The view.
 * @return The number.
 */
static uint32_t view_key(const wls_view_t *v)
{
    uint32_t h = 2166136261u;
    const uint8_t *p = (const uint8_t *)v;
    for (size_t i = 0; i < sizeof(*v); ++i) {
        h = (h ^ p[i]) * 16777619u;
    }
    const uint8_t *q = (const uint8_t *)&wls.ask;
    for (size_t i = 0; i < sizeof(wls.ask); ++i) {
        h = (h ^ q[i]) * 16777619u;
    }
    h = (h ^ (wls.asking ? 1u : 0u)) * 16777619u;
    h = (h ^ (wls.have_press ? 1u : 0u)) * 16777619u;
    return (h ^ (uint32_t)(wls.hit + 2)) * 16777619u;
}

void wls_render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    const int b = buffer_index & 1;
    wls_view_t v;
    wl_search_view(&v);
    const uint32_t key = view_key(&v);
    if ((wls.drawn_mask & bit) != 0u && wls.drawn_key[b] == key) {
        return;
    }
    gfx_clear(c, UI_BG);
    /* The question alone while it is open: nothing under it is drawn, as
     * nothing under it takes a press. */
    if (wls.asking) {
        draw_ask(c, &v);
    } else {
        draw_list(c, &v);
        draw_card(c, &v);
    }
    wls.drawn_mask |= bit;
    wls.drawn_key[b] = key;
}

/* ---------------------------------------------------------------- guide */

int wls_help_count(void)
{
    return wls.asking ? 0 : GUIDE_PAGES;
}

bool wls_help_page(int i, ui_help_page_t *out)
{
    if (out == NULL || i < 0 || i >= wls_help_count()) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->armed_ok = true;
    if (i == 0) {
        out->title = TX_HG_WLS_TITLE_LIST;
        out->side = UI_HELP_DOCK_RIGHT;
        /* The list's left half, which the dock leaves in view. */
        (void)ui_help_add(out, TR(HG_WLS_LIST_NAME), TX_HG_WLS_LIST,
                          gfx_rect_make(WLS_LX, WLS_LY, 480, WLS_LH));
        return true;
    }
    out->title = TX_HG_WLS_TITLE_CARD;
    out->side = UI_HELP_DOCK_LEFT;
    (void)ui_help_add_at(out, TR(WLS_BOUND), TX_HG_WLS_CARD,
                         wl_search_part(WLS_PART_CARD), UI_HELP_AT_RIGHT);
    (void)ui_help_add(out, TR(WL_SCAN_AGAIN), TX_HG_WLS_AGAIN,
                      wl_search_part(WLS_PART_AGAIN));
    return true;
}

/* ---------------------------------------------------------------- focus */

bool wl_search_focus(ui_focus_list_t *out)
{
    if (out == NULL) {
        return false;
    }
    wls_view_t v;
    wl_search_view(&v);
    char buf[96];
    if (wls.asking) {
        out->list = 0x574C5351u;     /* "WLSQ" */
        (void)ui_focus_add(out, wl_search_part(WLS_PART_CANCEL), TR(CANCEL));
        (void)ui_focus_add(out, wl_search_part(WLS_PART_BIND), TR(WLS_BIND));
        return true;
    }
    out->list = 0x574C5300u + v.found_n;
    for (int i = 0; i < (int)v.found_n; ++i) {
        (void)ui_focus_add(out, wl_search_row_rect(i),
                           wl_search_dev_line(&v.found[i], buf, sizeof(buf)));
    }
    (void)ui_focus_add(out, wl_search_part(WLS_PART_AGAIN),
                       TR(WL_SCAN_AGAIN));
    return true;
}
