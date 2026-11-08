/**
 * @file
 * @brief A trapezoid with a horizontal top and bottom, filled row by row:
 *        the shape of a tag with a slanted end.  See include/gfx.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "gfx_int.h"

#include <limits.h>

/**
 * @brief An edge's column on row @p r of @p n + 1 rows: @p a on row 0, @p b
 *        on row @p n, rounded to the nearest, a half away from @p a.
 * @param a the column on the first row
 * @param b the column on the last row
 * @param r the row
 * @param n the last row's number; 0 gives @p a
 * @return the column, held to the int range
 */
static int edge_at(int a, int b, int r, int n)
{
    if (n <= 0) {
        return a;
    }
    const long long d = (long long)b - a;
    const long long num = 2 * d * r;
    const long long q = (num + (d < 0 ? -n : n)) / (2LL * n);
    const long long v = (long long)a + q;
    return (v < INT_MIN) ? INT_MIN : (v > INT_MAX) ? INT_MAX : (int)v;
}

void gfx_fill_trapezoid(gfx_canvas_t *c, int y, int h, int xl_top, int xr_top,
                        int xl_bot, int xr_bot, gfx_color_t color)
{
    if (!canvas_ok(c) || h <= 0) {
        return;
    }
#ifdef GFX_TEXT_TRACE
    {
        /* The fit check's box: the widest of the two rows. */
        const int x0 = (xl_top < xl_bot) ? xl_top : xl_bot;
        const int x1 = (xr_top > xr_bot) ? xr_top : xr_bot;
        const long long w = (long long)x1 - x0;
        TRACE_BOX(c, x0, y, (w > INT_MAX) ? INT_MAX : (int)w, h);
    }
#endif
    TRACE_HOLD();
    /* Only the rows inside the clip: a shape far taller than the canvas
     * costs what its visible rows cost. */
    const long long top = ((long long)y > c->clip.y) ? (long long)y
                                                     : (long long)c->clip.y;
    const long long bot = (long long)c->clip.y + c->clip.h;
    const long long end = ((long long)y + h < bot) ? (long long)y + h : bot;
    for (long long yy = top; yy < end; ++yy) {
        const int r = (int)(yy - y);
        /* Each span held to the clip's columns: a span of absurd ends
         * keeps the part the clip shows. */
        int l = edge_at(xl_top, xl_bot, r, h - 1);
        int e = edge_at(xr_top, xr_bot, r, h - 1);
        l = (l > c->clip.x) ? l : c->clip.x;
        e = (e < c->clip.x + c->clip.w) ? e : c->clip.x + c->clip.w;
        if (e > l) {
            gfx_hline(c, l, (int)yy, e - l, color);
        }
    }
    TRACE_RELEASE();
}
