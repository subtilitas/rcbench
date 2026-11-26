/**
 * @file node_setup_view.c
 * @brief The boot splash's and SETUP's drawing (node_setup_view.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "node_setup_view.h"

#include <stdio.h>
#include <string.h>

/** @brief The screen, px. @{ */
#define W      320   /**< the width */
#define H      240   /**< the height */
#define BAND_H  30   /**< SETUP's band */
#define PAD      8   /**< text from a box's ends */
/** @} */

/** @brief The colours. @{ */
#define C_BG      GFX_BLACK              /**< the screen */
#define C_BAND    GFX_GREY(28)           /**< the band */
#define C_TEXT    GFX_WHITE              /**< text */
#define C_DIM     GFX_GREY(140)          /**< second lines, values */
#define C_LINE    GFX_GREY(70)           /**< separators */
#define C_KEY     GFX_GREY(48)           /**< a key's face */
#define C_ACCENT  GFX_RGB(120, 200, 255) /**< the name, the SETUP button */
#define C_INA     GFX_RGB(140, 235, 140) /**< an INA228's chip */
#define C_WARN    GFX_RGB(255, 170, 0)   /**< a refusal, the bar */
#define C_SEL     GFX_RGB(40, 90, 140)   /**< the choice in force */
/** @} */

/** @brief The splash's lines' tops, px. @{ */
#define SP_MARK_Y   14   /**< rcbench */
#define SP_ROLE_Y   62   /**< the role */
#define SP_NAME_Y   86   /**< the node's own name */
#define SP_FW_Y    110   /**< the firmware */
#define SP_CREDIT_Y 134  /**< the credit line */
/** @} */

void nsv_init(nsv_t *v)
{
    if (v != NULL) {
        memset(v, 0, sizeof *v);
    }
}

/**
 * @brief A text centered in a box.
 * @param c The canvas.  @param r The box.  @param s The text.
 * @param f The face.  @param col Its colour.
 */
static void center(gfx_canvas_t *c, gfx_rect_t r, const char *s,
                   const gfx_font_t *f, gfx_color_t col)
{
    gfx_text_in(c, r, s, f, col, 1, GFX_ALIGN_CENTER);
}

/**
 * @brief A button or key: its face and its label in the middle.
 * @param c The canvas.  @param r The box.  @param s The label.
 * @param f The face.  @param face The face's colour.  @param ink The label's.
 */
static void button(gfx_canvas_t *c, gfx_rect_t r, const char *s,
                   const gfx_font_t *f, gfx_color_t face, gfx_color_t ink)
{
    gfx_fill_rect(c, r.x + 1, r.y + 1, r.w - 2, r.h - 2, face);
    const int th = f->height;
    center(c, gfx_rect_make(r.x, r.y + (r.h - th) / 2, r.w, th), s, f, ink);
}

/**
 * @brief The band: a title left, a text right.
 * @param c The canvas.  @param left The title.  @param right The right
 *        text, "" none.  @param right_ink Its colour.
 */
static void band(gfx_canvas_t *c, const char *left, const char *right,
                 gfx_color_t right_ink)
{
    gfx_fill_rect(c, 0, 0, W, BAND_H, C_BAND);
    gfx_text_in(c, gfx_rect_make(PAD, 5, W - 2 * PAD, 20), left,
                &gfx_font_10x20, C_TEXT, 1, GFX_ALIGN_LEFT);
    gfx_text_in(c, gfx_rect_make(PAD, 5, W - 2 * PAD, 20), right,
                &gfx_font_10x20, right_ink, 1, GFX_ALIGN_RIGHT);
}

/**
 * @brief The bottom buttons of the list, the scan and the choice.
 * @param c The canvas.  @param s The state.  @param left Its label.
 * @param right Its label.  @param right_face The right one's colour.
 */
static void bottom(gfx_canvas_t *c, const ns_t *s, const char *left,
                   const char *right, gfx_color_t right_face)
{
    gfx_rect_t r;
    if (ns_box(s, NS_T_LEFT, &r)) {
        button(c, r, left, &gfx_font_12x24, C_KEY, C_TEXT);
    }
    if (ns_box(s, NS_T_RIGHT, &r)) {
        button(c, r, right, &gfx_font_12x24, right_face, C_TEXT);
    }
}

/**
 * @brief The splash's bar of the time left.
 * @param c The canvas.  @param w Its width, px.
 */
static void draw_bar(gfx_canvas_t *c, int w)
{
    gfx_fill_rect(c, 0, NSV_BAR_Y, W, NSV_BAR_H, C_BG);
    if (w > 0) {
        gfx_fill_rect(c, 0, NSV_BAR_Y, w, NSV_BAR_H, C_WARN);
    }
}

/**
 * @brief The splash: the mark, the role, the name, the firmware, the
 *        credit and SETUP.
 * @param c The canvas.  @param s The state.
 */
static void draw_splash(gfx_canvas_t *c, const ns_t *s)
{
    gfx_fill_rect(c, 0, 0, W, H, C_BG);
    center(c, gfx_rect_make(0, SP_MARK_Y, W, 36), "rcbench", &gfx_font_20x36,
           C_ACCENT);
    center(c, gfx_rect_make(0, SP_ROLE_Y, W, 20), ns_text(s->lang, NS_TX_ROLE),
           &gfx_font_10x20, C_TEXT);
    if (s->edit.name[0] != '\0') {
        center(c, gfx_rect_make(0, SP_NAME_Y, W, 20), s->edit.name,
               &gfx_font_10x20, C_ACCENT);
    }
    char fw[32];
    (void)snprintf(fw, sizeof fw, ns_text(s->lang, NS_TX_FW),
                   (unsigned)s->fw[0], (unsigned)s->fw[1], (unsigned)s->fw[2]);
    center(c, gfx_rect_make(0, SP_FW_Y, W, 20), fw, &gfx_font_10x20, C_DIM);
    center(c, gfx_rect_make(0, SP_CREDIT_Y, W, 20),
           ns_text(s->lang, NS_TX_CREDIT), &gfx_font_10x20, C_DIM);
    gfx_rect_t r;
    if (ns_box(s, NS_T_SETUP, &r)) {
        button(c, r, ns_text(s->lang, NS_TX_SETUP), &gfx_font_20x36, C_SEL,
               C_TEXT);
    }
}

/**
 * @brief SETUP's list: four rows, BACK and SAVE.
 * @param c The canvas.  @param s The state.
 */
static void draw_list(gfx_canvas_t *c, const ns_t *s)
{
    static const int k_label[4] = { NS_TX_NAME, NS_TX_SCAN, NS_TX_ADDR,
                                    NS_TX_SHUNT };
    gfx_fill_rect(c, 0, 0, W, H, C_BG);
    band(c, ns_text(s->lang, NS_TX_TITLE), "", C_TEXT);
    for (int i = 0; i < 4; ++i) {
        gfx_rect_t r;
        if (!ns_box(s, NS_T_ROW0 + i, &r)) {
            continue;
        }
        gfx_hline(c, 0, r.y + r.h + 1, W, C_LINE);
        const gfx_rect_t t = gfx_rect_make(PAD, r.y + (r.h - 20) / 2,
                                           W - 2 * PAD, 20);
        gfx_text_in(c, t, ns_text(s->lang, k_label[i]), &gfx_font_10x20,
                    C_TEXT, 1, GFX_ALIGN_LEFT);
        char v[32];
        ns_row_value(s, i, v, sizeof v);
        gfx_text_in(c, t, v, &gfx_font_10x20, i == 0 && s->edit.name[0] != '\0'
                    ? C_ACCENT : C_DIM, 1, GFX_ALIGN_RIGHT);
    }
    bottom(c, s, ns_text(s->lang, NS_TX_BACK), ns_text(s->lang, NS_TX_SAVE),
           C_SEL);
}

/**
 * @brief NAME's keyboard: the field with the text and its cursor, the
 *        keys.
 * @param c The canvas.  @param s The state.
 */
static void draw_name(gfx_canvas_t *c, const ns_t *s)
{
    gfx_fill_rect(c, 0, 0, W, H, C_BG);
    gfx_fill_rect(c, 4, 4, W - 8, 32, C_BAND);
    const int tw = gfx_text_width(&gfx_font_12x24, s->text, 1);
    (void)gfx_text(c, 12, 8, s->text, &gfx_font_12x24, C_TEXT, 1);
    gfx_fill_rect(c, 12 + tw + 1, 8, 2, 24, C_ACCENT);
    char count[24];
    (void)snprintf(count, sizeof count, "%u/%u", (unsigned)strlen(s->text),
                   (unsigned)RCNODE_UNAME_MAX);
    gfx_text_in(c, gfx_rect_make(4, 10, W - 16, 20), count, &gfx_font_10x20,
                C_DIM, 1, GFX_ALIGN_RIGHT);
    for (int k = 0; k < NS_KB_KEYS; ++k) {
        gfx_rect_t r;
        if (!ns_box(s, NS_T_KEY0 + k, &r)) {
            continue;
        }
        const bool word = k >= NS_KB_CHARS || k == NS_KB_DEL;
        button(c, r, ns_kb_label(s, k), word ? &gfx_font_10x20 : &gfx_font_12x24,
               k == NS_KB_OK ? C_SEL : C_KEY, C_TEXT);
    }
}

/**
 * @brief SHUNT's keypad: the field with the digits and the range, the
 *        keys.
 * @param c The canvas.  @param s The state.
 */
static void draw_shunt(gfx_canvas_t *c, const ns_t *s)
{
    gfx_fill_rect(c, 0, 0, W, H, C_BG);
    gfx_fill_rect(c, 4, 4, W - 8, 34, C_BAND);
    char v[16];
    (void)snprintf(v, sizeof v, "%suΩ", s->num);
    (void)gfx_text(c, 12, 9, v, &gfx_font_12x24, C_TEXT, 1);
    gfx_text_in(c, gfx_rect_make(4, 11, W - 16, 20),
                ns_text(s->lang, NS_TX_RANGE), &gfx_font_10x20,
                s->refused ? C_WARN : C_DIM, 1, GFX_ALIGN_RIGHT);
    for (int k = 0; k < NS_KP_KEYS; ++k) {
        gfx_rect_t r;
        if (ns_box(s, NS_T_KEY0 + k, &r)) {
            button(c, r, ns_kp_label(s, k), k <= 9 ? &gfx_font_20x36
                                                   : &gfx_font_12x24,
                   k == NS_KP_OK ? C_SEL : C_KEY, C_TEXT);
        }
    }
}

/**
 * @brief An address chip: "0x40", an INA228 in its colour, the choice in
 *        force on its own face.
 * @param c The canvas.  @param r The box.  @param a The address.
 * @param ina It holds an INA228.  @param sel It is the choice in force.
 */
static void chip(gfx_canvas_t *c, gfx_rect_t r, uint8_t a, bool ina, bool sel)
{
    char t[16];
    (void)snprintf(t, sizeof t, "0x%02X", (unsigned)a);
    button(c, r, t, &gfx_font_10x20, sel ? C_SEL : C_KEY, ina ? C_INA : C_TEXT);
}

/**
 * @brief The legend in the band's right end: "INA228" in its colour.
 * @param c The canvas.  @param s The state.
 */
static void legend(gfx_canvas_t *c, const ns_t *s)
{
    gfx_text_in(c, gfx_rect_make(PAD, 5, W - 2 * PAD, 20),
                ns_text(s->lang, NS_TX_LEGEND), &gfx_font_10x20, C_INA, 1,
                GFX_ALIGN_RIGHT);
}

/**
 * @brief I2C SCAN's result: a chip per answering address, "+N" past the
 *        room, AGAIN and BACK.
 * @param c The canvas.  @param s The state.
 */
static void draw_scan(gfx_canvas_t *c, const ns_t *s)
{
    gfx_fill_rect(c, 0, 0, W, H, C_BG);
    band(c, ns_text(s->lang, NS_TX_SCAN), "", C_TEXT);
    legend(c, s);
    const int n = ns_chips(s);
    for (int i = 0; i < n; ++i) {
        gfx_rect_t r;
        const uint8_t a = ns_chip_addr(s, i);
        if (ns_box(s, NS_T_CHIP0 + i, &r)) {
            chip(c, r, a, nscan_is_ina228(&s->scan, a), false);
        }
    }
    gfx_rect_t r;
    if (s->scan.n > NS_SCAN_CHIPS && ns_box(s, NS_T_CHIP0 + n, &r)) {
        char t[16];
        (void)snprintf(t, sizeof t, ns_text(s->lang, NS_TX_MORE),
                       (unsigned)(s->scan.n - (unsigned)n));
        button(c, r, t, &gfx_font_10x20, C_BG, C_DIM);
    }
    if (s->scan.done && s->scan.n == 0u) {
        center(c, gfx_rect_make(0, 100, W, 20),
               ns_text(s->lang, NS_TX_NO_ANSWER), &gfx_font_10x20, C_WARN);
    }
    bottom(c, s, ns_text(s->lang, NS_TX_AGAIN), ns_text(s->lang, NS_TX_BACK),
           C_KEY);
}

/**
 * @brief INA228 ADDRESS's choice: AUTO, a chip per INA228 found and the
 *        fixed address in force, AGAIN and BACK.
 * @param c The canvas.  @param s The state.
 */
static void draw_addr(gfx_canvas_t *c, const ns_t *s)
{
    gfx_fill_rect(c, 0, 0, W, H, C_BG);
    band(c, ns_text(s->lang, NS_TX_ADDR), "", C_TEXT);
    legend(c, s);
    gfx_rect_t r;
    if (ns_box(s, NS_T_AUTO, &r)) {
        button(c, r, ns_text(s->lang, NS_TX_AUTO), &gfx_font_12x24,
               s->edit.i228 == RCNODE_I228_AUTO ? C_SEL : C_KEY, C_TEXT);
    }
    const int n = ns_chips(s);
    for (int i = 0; i < n; ++i) {
        const uint8_t a = ns_chip_addr(s, i);
        if (ns_box(s, NS_T_CHIP0 + i, &r)) {
            chip(c, r, a, nscan_is_ina228(&s->scan, a), a == s->edit.i228);
        }
    }
    if (s->scan.done && s->scan.n_ina228 == 0u) {
        center(c, gfx_rect_make(0, 156, W, 20), ns_text(s->lang, NS_TX_NO_INA),
               &gfx_font_10x20, C_WARN);
    }
    bottom(c, s, ns_text(s->lang, NS_TX_AGAIN), ns_text(s->lang, NS_TX_BACK),
           C_KEY);
}

int nsv_render(nsv_t *v, gfx_canvas_t *c, const ns_t *s, uint32_t now_ms)
{
    if (v == NULL || c == NULL || s == NULL || s->mode == NS_METER) {
        return 0;
    }
    int parts = 0;
    if (!v->drawn || v->mode != s->mode || v->rev != s->rev) {
        switch (s->mode) {
        case NS_SPLASH: draw_splash(c, s); break;
        case NS_LIST:   draw_list(c, s);   break;
        case NS_NAME:   draw_name(c, s);   break;
        case NS_SHUNT:  draw_shunt(c, s);  break;
        case NS_SCAN:   draw_scan(c, s);   break;
        default:        draw_addr(c, s);   break;
        }
        v->drawn = true;
        v->mode = s->mode;
        v->rev = s->rev;
        v->bar = -1;
        ++parts;
    }
    if (s->mode == NS_SPLASH) {
        const int w = (int)(ns_splash_left(s, now_ms) * (uint32_t)W / NS_SPLASH_MS);
        if (w != v->bar) {
            v->bar = (int16_t)w;
            draw_bar(c, w);
            ++parts;
        }
    }
    return parts;
}
