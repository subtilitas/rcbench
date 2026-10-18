/*
 * The menu icons, drawn from primitives rather than stored as bitmaps.
 *
 * Each icon is a few rings, capsules and strokes scaled to the requested
 * size.  A drawn icon is sharp at any size and needs no image data in flash.
 * Every icon is drawn centred on the point it is given, inside a box of
 * size x size px around it.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_icons.h"

#include <math.h>

#include "ui_theme.h"

static void ring(gfx_canvas_t *c, int cx, int cy, int outer, int thick,
                 gfx_color_t color)
{
    for (int i = 0; i < thick; ++i) {
        gfx_draw_circle(c, cx, cy, outer - i, color);
    }
}

void ui_icon_motor(gfx_canvas_t *c, int x, int y, int size,
                   gfx_color_t color)
{
    int cx = x;
    int cy = y;
    int r  = size / 2 - 2;

    ring(c, cx, cy, r, 2, color);
    for (int i = 0; i < 3; ++i) {
        float a = (float)i * 2.0944f - 1.5708f;       /* 120 deg apart, one up */
        int tx = cx + (int)(cosf(a) * (float)(r - 6));
        int ty = cy + (int)(sinf(a) * (float)(r - 6));
        gfx_thick_line(c, cx, cy, tx, ty, size / 9, color);
        gfx_fill_circle(c, tx, ty, size / 12, color);
    }
    gfx_fill_circle(c, cx, cy, size / 7, color);
}

void ui_icon_servo(gfx_canvas_t *c, int x, int y, int size,
                   gfx_color_t color)
{
    const int top = y - size / 2;
    int cx = x;
    int bw = size * 5 / 9;
    int bh = size * 4 / 9;
    int by = top + size - bh - 1;
    int ear = size / 7;

    /* The case is an outline, so the shaft and horn on top stay separate
     * from it. */
    gfx_fill_rect(c, cx - bw / 2 - ear, by + 4, bw + 2 * ear, 5, color);
    gfx_fill_chamfer_rect(c, cx - bw / 2, by, bw, bh, 5, color);
    gfx_fill_chamfer_rect(c, cx - bw / 2 + 3, by + 3, bw - 6, bh - 6, 4, UI_PANEL);
    gfx_fill_rect(c, cx - bw / 2 + 7, by + bh - 12, bw - 14, 3, color);
    gfx_fill_rect(c, cx - bw / 2 + 7, by + bh - 19, bw - 14, 3, color);

    /* Output shaft on the top face, horn on it, and the arc it sweeps. */
    int sy = by - 1;
    gfx_fill_circle(c, cx, sy, size / 8, color);
    gfx_thick_line(c, cx, sy, cx + size / 4, sy - size / 4, size / 11, color);
    gfx_fill_circle(c, cx + size / 4, sy - size / 4, size / 14, color);

    int ar = size / 2 - 3;
    /* The angle is computed from the index rather than accumulated, so
     * rounding does not drift across the 21 dots. */
    for (int i = 0; i < 21; ++i) {
        const float a = -2.62f + 0.10f * (float)i;
        int ax = cx + (int)(cosf(a) * (float)ar);
        int ay = sy + (int)(sinf(a) * (float)ar);
        gfx_fill_circle(c, ax, ay, 1, color);
    }
}

void ui_icon_chart(gfx_canvas_t *c, int x, int y, int size,
                   gfx_color_t color)
{
    const int left = x - size / 2;
    const int top  = y - size / 2;
    int pad = size / 8;
    int x0 = left + pad;
    int y1 = top + size - pad;

    gfx_fill_rect(c, x0, top + pad, 3, size - 2 * pad, color);
    gfx_fill_rect(c, x0, y1 - 2, size - 2 * pad, 3, color);

    static const int pts[6] = { 70, 40, 55, 20, 35, 8 };
    int px = x0 + 4;
    int py = y1 - 2 - (size - 2 * pad) * (100 - pts[0]) / 100;
    for (int i = 1; i < 6; ++i) {
        int nx = x0 + 4 + (size - 2 * pad - 6) * i / 5;
        int ny = y1 - 2 - (size - 2 * pad) * (100 - pts[i]) / 100;
        gfx_thick_line(c, px, py, nx, ny, 3, color);
        px = nx;
        py = ny;
    }
}

void ui_icon_record(gfx_canvas_t *c, int x, int y, int size,
                    gfx_color_t color)
{
    const int left = x - size / 2;
    const int top  = y - size / 2;
    int cx = x;
    int pad = size / 8;

    /* An arrow going down into a tray: capture, then storage. */
    int shaft_top = top + pad;
    int head_y = top + size / 2 + size / 12;
    gfx_fill_rect(c, cx - size / 12, shaft_top, size / 6, head_y - shaft_top - size / 6,
                  color);
    for (int i = 0; i <= size / 4; ++i) {
        int w = (size / 4 - i) * 2 + 1;
        gfx_hline(c, cx - w / 2, head_y - size / 6 + i, w, color);
    }

    int tray_y = top + size - pad - size / 5;
    gfx_fill_rect(c, left + pad, tray_y + size / 5 - 3, size - 2 * pad, 3, color);
    gfx_fill_rect(c, left + pad, tray_y, 3, size / 5, color);
    gfx_fill_rect(c, left + size - pad - 3, tray_y, 3, size / 5, color);
}

void ui_icon_chip(gfx_canvas_t *c, int x, int y, int size,
                  gfx_color_t color)
{
    const int left = x - size / 2;
    const int top  = y - size / 2;
    int pad = size / 5;
    int bw = size - 2 * pad;

    gfx_draw_chamfer_rect(c, left + pad, top + pad, bw, bw, 5, color);
    gfx_draw_chamfer_rect(c, left + pad + 1, top + pad + 1, bw - 2, bw - 2, 4, color);
    gfx_fill_circle(c, left + pad + 8, top + pad + 8, 2, color);

    for (int i = 0; i < 3; ++i) {
        int py = top + pad + bw / 4 + i * bw / 4 - 1;
        gfx_fill_rect(c, left + pad - pad + 2, py, pad - 2, 3, color);
        gfx_fill_rect(c, left + pad + bw, py, pad - 2, 3, color);
        int px = left + pad + bw / 4 + i * bw / 4 - 1;
        gfx_fill_rect(c, px, top + pad - pad + 2, 3, pad - 2, color);
        gfx_fill_rect(c, px, top + pad + bw, 3, pad - 2, color);
    }
}

void ui_icon_sliders(gfx_canvas_t *c, int x, int y, int size,
                     gfx_color_t color)
{
    const int left = x - size / 2;
    const int top  = y - size / 2;
    static const int knob_pct[3] = { 30, 68, 46 };
    int pad = size / 8;
    int w = size - 2 * pad;

    for (int i = 0; i < 3; ++i) {
        int ly = top + pad + (size - 2 * pad) * i / 2;
        gfx_fill_rect(c, left + pad, ly - 1, w, 3, color);
        int kx = left + pad + w * knob_pct[i] / 100;
        gfx_fill_chamfer_rect(c, kx - size / 14, ly - size / 10,
                              size / 7, size / 5, 3, color);
    }
}

/*
 * A cell with its terminal and three bars.  Three rather than four because at
 * 30 px a fourth bar is two pixels wide and reads as noise.
 */
void ui_icon_battery(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color)
{
    const int w = size;
    const int h = (size * 3) / 5;
    const int left = x - w / 2;
    const int top  = y - h / 2;

    gfx_draw_round_rect(c, left, top, w - 3, h, 2, color);
    gfx_fill_rect(c, left + w - 3, y - h / 6, 3, h / 3, color);

    const int bw = (w - 3 - 10) / 3;
    for (int i = 0; i < 3; ++i) {
        gfx_fill_rect(c, left + 4 + i * (bw + 1), top + 4, bw, h - 8, color);
    }
}

/*
 * A disc with an off-centre mass, and two correction ticks opposite it.
 */
void ui_icon_balance(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color)
{
    const int r = (size - 1) / 2;          /* 2r + 1 px across, in the box */
    gfx_draw_circle(c, x, y, r, color);
    gfx_draw_circle(c, x, y, r / 4, color);
    /* The heavy spot, up and to the right. */
    gfx_fill_circle(c, x + (r * 5) / 8, y - (r * 5) / 8, r / 4, color);
    /* Two ticks opposite it, where the correction goes. */
    gfx_fill_rect(c, x - (r * 3) / 4, y + (r * 2) / 3, r / 3, 2, color);
    gfx_fill_rect(c, x - (r * 2) / 3, y + (r * 3) / 4, 2, r / 3, color);
}

/*
 * A bench supply's front: the case, a set-point dial on the left and the two
 * output terminals on the right.
 */
void ui_icon_supply(gfx_canvas_t *c, int x, int y, int size, gfx_color_t color)
{
    const int w = size;
    const int h = (size * 3) / 5;
    const int left = x - w / 2;
    const int top  = y - h / 2;
    gfx_draw_round_rect(c, left, top, w, h, 3, color);
    gfx_draw_circle(c, left + w / 4, y, h / 4, color);
    gfx_fill_rect(c, left + w / 4 - 1, y - h / 4, 2, h / 4, color);
    gfx_fill_circle(c, left + (w * 5) / 8, y + h / 6, 2, color);
    gfx_fill_circle(c, left + (w * 13) / 16, y + h / 6, 2, color);
    gfx_fill_rect(c, left + (w * 9) / 16, y - h / 4, w / 3, 2, color);
}

/*
 * Three rows of a list, each a pin and its line: the bindings by GPIO.
 */
void ui_icon_list(gfx_canvas_t *c, int x, int y, int size,
                  gfx_color_t color)
{
    const int left = x - size / 2;
    const int top  = y - size / 2;
    const int pad = size / 8;
    const int dot = size / 6;
    for (int i = 0; i < 3; ++i) {
        const int ly = top + pad + (size - 2 * pad) * i / 2;
        gfx_fill_rect(c, left + pad, ly - dot / 2, dot, dot, color);
        gfx_fill_rect(c, left + pad + dot + size / 8, ly - 1,
                      size - 2 * pad - dot - size / 8, 3, color);
    }
}

/*
 * A board seen from above: its outline and a row of pads along each long
 * edge, one of them taken.
 */
void ui_icon_board(gfx_canvas_t *c, int x, int y, int size,
                   gfx_color_t color)
{
    const int left = x - size / 2;
    const int pad = size / 8;
    const int bw = size - 2 * pad;
    const int bh = (size * 3) / 5;
    const int top = y - bh / 2;
    gfx_draw_round_rect(c, left + pad, top, bw, bh, 3, color);
    for (int i = 0; i < 4; ++i) {
        const int px = left + pad + bw * (2 * i + 1) / 8;
        gfx_draw_circle(c, px, top + 5, 2, color);
        gfx_draw_circle(c, px, top + bh - 6, 2, color);
    }
    gfx_fill_circle(c, left + pad + bw * 5 / 8, top + 5, 2, color);
}

void ui_icon_wifi(gfx_canvas_t *c, int x, int y, int size,
                  gfx_color_t color)
{
    /* A dot at the foot and three arcs over it, 80 degrees each about the
     * vertical: a radio's waves.  The outer arc's ends stay inside the
     * box's width. */
    const int cy = y + size / 2 - 4;
    const int thick = (size >= 30) ? 3 : 2;
    gfx_fill_circle(c, x, cy, size / 12, color);
    for (int i = 1; i <= 3; ++i) {
        gfx_arc(c, x, cy, size * 7 * i / 30, thick, 50.0f, 130.0f, color);
    }
}
