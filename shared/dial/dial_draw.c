/**
 * @file
 * @brief The faces drawn from a scene, in full or by what changed.  See
 *        include/dial_face.h.
 *
 * The display's rows fall into bands that a repaint clears and draws
 * alone: the title tag (32 to 51, drawn in full only), the pill's row (56
 * to 77), the body (78 to 176, or the six cells there on a reading face)
 * and the press hints (177 to 206).  A band is cleared only inside radius
 * DIAL_R_CLEAR, so the rings, the rim ticks and the rim decorations
 * outside it are never touched by a clear.  The rings and the ticks are
 * painted onto a fixed background colour, so a repaint of one equals its
 * first paint.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <string.h>

#include "dial_face.h"
#include "ui_theme.h"

/** @brief The display's center, continuous coordinates: a pixel's center
 *         is its index plus 0.5. */
#define MID 120
/** @brief The bands' rows, first and last. @{ */
#define BAND_PILL_Y0 56
#define BAND_PILL_Y1 77  /**< the pill's row, last */
#define BAND_BODY_Y0 78  /**< the body, first */
#define BAND_BODY_Y1 175 /**< the body, last: the bottom cells' last row */
#define BAND_HINT_Y0 176 /**< the hints, first: an umlaut's dots on the
                              first hint, at y 183, start here */
#define BAND_HINT_Y1 206 /**< the hints, last */
/** @} */
/** @brief The title tag: middle row, height, padding and corner cut. @{ */
#define TAG_Y   42
#define TAG_H   20 /**< height */
#define TAG_PAD 12 /**< width beyond the text */
#define TAG_CUT 6  /**< top-right and bottom-left cut */
/** @} */
/** @brief The pill: middle row, height, padding, SAFE's width. @{ */
#define PILL_Y     66
#define PILL_H     19 /**< height */
#define PILL_PAD   12 /**< width beyond ARMED's text */
#define PILL_SAFE  76 /**< SAFE's width */
#define ICON_GAP   6  /**< from the pill to an icon */
/** @} */
/** @brief The link bars: 4 of 3px at a 4px pitch, bottom row 72. @{ */
#define BARS_W      15
#define BARS_BOTTOM 72 /**< the bars' bottom row */
/** @} */
/** @brief The battery glyph: top row and size, the nub included. @{ */
#define BATT_Y 62
#define BATT_W 18 /**< width with the nub */
#define BATT_H 9  /**< height */
/** @} */
/** @brief The grid's column middles. */
static const int k_col[2] = { 76, 164 };
/** @brief The grid's row middles. */
static const int k_row[3] = { 100, 133, 166 };
/** @brief The rim ticks of a list: inner and outer radius, first angle
 *         and sweep in degrees clockwise from 3 o'clock. @{ */
#define TICK_R0    106.0
#define TICK_R1    116.0 /**< outer radius */
#define TICK_FROM  135.0 /**< the first tick's side of the sweep */
#define TICK_SWEEP 270.0 /**< the sweep */
/** @} */
/** @brief The track: a ring's colour mixed this many 255ths onto the
 *         background (22%). */
#define TRACK_MIX 56u
/** @brief Readings at half strength while SAFE: 50%. */
#define DIM_MIX 128u

/** @brief The text faces by DIAL_FONT_*. */
static const gfx_font_t *const k_text[5] = {
    &gfx_font_8x16, &gfx_font_10x20, &gfx_font_12x24, &gfx_font_16x28,
    &gfx_font_20x36,
};

/** @brief A font's metrics in its cell: the row a line's middle sits on
 *         (between cap top and baseline), the baseline and the line pitch.
 */
typedef struct {
    const gfx_font_t *font; /**< the font */
    uint8_t mid;            /**< middle of the capitals */
    uint8_t base;           /**< first row under the baseline */
    uint8_t pitch;          /**< line pitch */
} metric_t;

/** @brief Every font a face draws with. */
static const metric_t k_metric[] = {
    { &gfx_font_8x16, 8, 13, 14 },
    { &gfx_font_10x20, 9, 15, 18 },
    { &gfx_font_12x24, 11, 19, 22 },
    { &gfx_font_16x28, 15, 25, 26 },
    { &gfx_font_20x36, 17, 29, 30 },
    { &gfx_font_num_24x30, 15, 29, 32 },
    { &gfx_font_num_32x44, 21, 41, 44 },
};

/** @brief A font's metrics. @param f the font @return its row */
static const metric_t *metric(const gfx_font_t *f)
{
    for (size_t i = 0; i < sizeof k_metric / sizeof k_metric[0]; ++i) {
        if (k_metric[i].font == f) {
            return &k_metric[i];
        }
    }
    return &k_metric[0];
}

/* ------------------------------------------------------------- colours */

/**
 * @brief A palette colour mixed onto the background.
 * @param id ui_color_id_t
 * @param k  its weight, 0 (the background) to 255 (itself)
 * @return the mix
 */
static gfx_color_t mix(uint8_t id, unsigned k)
{
    const uint32_t a = ui_theme_rgb24(UI_THEME_DARK, (ui_color_id_t)id);
    const uint32_t b = ui_theme_rgb24(UI_THEME_DARK, UI_C_BG);
    unsigned ch[3];
    for (int i = 0; i < 3; ++i) {
        const unsigned sh = (unsigned)(16 - 8 * i);
        const unsigned fa = (a >> sh) & 0xFFu;
        const unsigned fb = (b >> sh) & 0xFFu;
        ch[i] = (fb * (255u - k) + fa * k + 127u) / 255u;
    }
    return GFX_RGB(ch[0], ch[1], ch[2]);
}

/** @brief A palette colour. @param id ui_color_id_t @return it */
static gfx_color_t col(uint8_t id)
{
    return mix(id, 255u);
}

/* ---------------------------------------------------------------- text */

/** @brief A glyph's coverage rows, as gfx_text() finds them.
 *  @param f the font @param cp the code point
 *  @return the rows, or NULL when the font holds neither it nor '?' */
static const uint8_t *glyph(const gfx_font_t *f, uint32_t cp)
{
    int idx = -1;
    for (int pass = 0; pass < 2 && idx < 0; ++pass) {
        const uint32_t want = pass == 0 ? cp : (uint32_t)'?';
        if (want >= f->first && want <= f->last) {
            idx = (int)(want - f->first);
        }
        for (unsigned k = 0; idx < 0 && k < f->extra_count; ++k) {
            if (f->extra[k] == want) {
                idx = (int)((unsigned)(f->last - f->first) + 1u + k);
            }
        }
    }
    if (idx < 0) {
        return NULL;
    }
    return f->glyphs + (size_t)idx * f->height * f->bytes_per_row;
}

/** @brief Whether a font holds every code point of a string.
 *  @param f the font @param s the text @return true when it does */
static bool holds(const gfx_font_t *f, const char *s)
{
    const char *p = s;
    for (uint32_t cp = gfx_utf8_next(&p); cp != 0u; cp = gfx_utf8_next(&p)) {
        bool in = cp >= f->first && cp <= f->last;
        for (unsigned k = 0; !in && k < f->extra_count; ++k) {
            in = f->extra[k] == cp;
        }
        if (!in) {
            return false;
        }
    }
    return true;
}

bool dial_ink(const gfx_font_t *font, const char *s, int x, int y,
              gfx_rect_t *out)
{
    int x0 = INT32_MAX;
    int y0 = INT32_MAX;
    int x1 = INT32_MIN;
    int y1 = INT32_MIN;
    const char *p = s;
    int cx = x;
    *out = gfx_rect_make(0, 0, 0, 0);
    if (font == NULL || s == NULL) {
        return false;
    }
    for (uint32_t cp = gfx_utf8_next(&p); cp != 0u;
         cp = gfx_utf8_next(&p), cx += font->width) {
        const uint8_t *rows = glyph(font, cp);
        if (rows == NULL) {
            continue;
        }
        for (int r = 0; r < font->height; ++r) {
            const uint8_t *line = rows + (size_t)r * font->bytes_per_row;
            for (int k = 0; k < font->width; ++k) {
                const bool on = font->bpp == 8
                    ? line[k] != 0u
                    : (line[k >> 3] & (0x80u >> (k & 7))) != 0u;
                if (on) {
                    x0 = cx + k < x0 ? cx + k : x0;
                    x1 = cx + k > x1 ? cx + k : x1;
                    y0 = y + r < y0 ? y + r : y0;
                    y1 = y + r > y1 ? y + r : y1;
                }
            }
        }
    }
    if (x1 < x0) {
        return false;
    }
    *out = gfx_rect_make(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    return true;
}

/** @brief Whether a pixel's center lies within a radius of the display's
 *         center. @param x its column @param y its row @param r the radius
 *         @return true when it does */
static bool px_inside(int x, int y, int r)
{
    const int dx = 2 * x + 1 - 2 * MID;
    const int dy = 2 * y + 1 - 2 * MID;
    return dx * dx + dy * dy <= 4 * r * r;
}

bool dial_box_inside(gfx_rect_t b, int r)
{
    if (b.w <= 0 || b.h <= 0) {
        return true;
    }
    return px_inside(b.x, b.y, r) && px_inside(b.x + b.w - 1, b.y, r)
           && px_inside(b.x, b.y + b.h - 1, r)
           && px_inside(b.x + b.w - 1, b.y + b.h - 1, r);
}

/** @brief One line placed: its font, text, left edge and top row. */
typedef struct {
    const gfx_font_t *font; /**< the font */
    char text[DIAL_TXT];    /**< the line */
    int  x;                 /**< its cells' left edge */
    int  y;                 /**< their top row */
} line_t;

/**
 * @brief Place a line centered on the display's middle column with its
 *        capitals' middle on row @p y.
 * @param l    the line, its font and text set
 * @param y    the middle row
 * @return true when its ink lies inside DIAL_R_TEXT
 */
static bool place(line_t *l, int y)
{
    gfx_rect_t ink;
    const int w = gfx_text_width(l->font, l->text, 1);
    l->x = MID - w / 2;
    l->y = y - metric(l->font)->mid;
    return !dial_ink(l->font, l->text, l->x, l->y, &ink)
           || dial_box_inside(ink, DIAL_R_TEXT);
}

/** @brief A block laid out: up to 3 lines. */
typedef struct {
    line_t line[3]; /**< the lines */
    int    n;       /**< lines used */
} laid_t;

/**
 * @brief Wrap a text onto @p n lines in a font, each at its row of a block
 *        centered on @p y: each line takes the most whole words that fit.
 * @param out  the lines
 * @param f    the font
 * @param text the text
 * @param y    the block's middle row
 * @param n    the lines, 1 to 3
 * @return true when every word is on a line inside DIAL_R_TEXT
 */
static bool wrap(laid_t *out, const gfx_font_t *f, const char *text, int y,
                 int n)
{
    const char *p = text;
    const int pitch = metric(f)->pitch;
    out->n = 0;
    for (int i = 0; i < n; ++i) {
        line_t *l = &out->line[i];
        const int ly = y + (2 * i - (n - 1)) * pitch / 2;
        size_t take = 0;
        l->font = f;
        while (*p == ' ') {
            ++p;
        }
        if (*p == '\0') {
            return false;
        }
        for (size_t k = 1; k < sizeof l->text; ++k) {
            if (p[k] == ' ' || p[k] == '\0') {
                memcpy(l->text, p, k);
                l->text[k] = '\0';
                if (!place(l, ly)) {
                    break;
                }
                take = k;
            }
            if (p[k] == '\0') {
                break;
            }
        }
        if (take == 0u) {
            return false;
        }
        memcpy(l->text, p, take);
        l->text[take] = '\0';
        (void)place(l, ly);
        p += take;
        out->n = i + 1;
    }
    while (*p == ' ') {
        ++p;
    }
    return *p == '\0';
}

/**
 * @brief Lay a text out in a block's place: the largest font of its
 *        chain, then the fewest lines, in which every line fits.
 * @param out  the lines
 * @param b    the block: its font, lines and row
 * @param text the text
 * @return true when it fits
 */
static bool fit(laid_t *out, const dial_block_t *b, const char *text)
{
    const int top = b->font > DIAL_FONT_BIG ? DIAL_FONT_BIG : b->font;
    const int most = b->lines < 1 ? 1 : b->lines > 3 ? 3 : b->lines;
    for (int f = top; f >= 0; --f) {
        if (!holds(k_text[f], text)) {
            continue;
        }
        for (int n = 1; n <= most; ++n) {
            if (wrap(out, k_text[f], text, b->y, n)) {
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief Lay a block out: its text whole if it fits (fit()); else without
 *        its last parts, two spaces apart, one by one; else, when the one
 *        part left holds no digit, cut by characters in the 8x16 face.  A
 *        number is never shown in part: a part with a digit that does not
 *        fit is dropped whole.
 * @param out the lines; none when nothing fits
 * @param b   the block
 * @return true when the lines hold the whole text
 */
static bool lay(laid_t *out, const dial_block_t *b)
{
    char text[DIAL_TXT];
    dk_text_copy(text, sizeof text, b->text);
    if (fit(out, b, text)) {
        return true;
    }
    for (;;) {
        char *gap = NULL;
        for (char *p = strstr(text, "  "); p != NULL; p = strstr(p + 1, "  ")) {
            gap = p;
        }
        if (gap == NULL) {
            break;
        }
        *gap = '\0';
        if (fit(out, b, text)) {
            return false;
        }
    }
    line_t *l = &out->line[0];
    out->n = 0;
    if (strpbrk(text, "0123456789") != NULL) {
        return false;
    }
    l->font = k_text[0];
    dk_text_copy(l->text, sizeof l->text, text);
    for (int cells = gfx_text_cells(text); cells > 0; --cells) {
        l->text[gfx_text_prefix(text, cells)] = '\0';
        if (place(l, b->y)) {
            out->n = 1;
            return false;
        }
    }
    return false;
}

/**
 * @brief Draw a placed line and tell the test.
 * @param r    the renderer
 * @param c    the canvas
 * @param l    the line
 * @param fg   its colour
 * @param cell the grid cell, or -1
 * @param band the band a partial draw clears for it
 */
static void draw_line(dial_render_t *r, gfx_canvas_t *c, const line_t *l,
                      gfx_color_t fg, int cell, dial_band_t band)
{
    if (l->text[0] == '\0') {
        return;
    }
    gfx_text(c, l->x, l->y, l->text, l->font, fg, 1);
    if (r->seen != NULL) {
        const dial_seen_t t = { l->text, l->font, l->x, l->y, cell, band };
        r->seen(r->seen_ctx, &t);
    }
}

/* -------------------------------------------------------------- shapes */

/**
 * @brief Fill the pixels of a box that lie inside DIAL_R_CLEAR with the
 *        background.
 * @param c  the canvas
 * @param x0 the first column
 * @param y0 the first row
 * @param x1 the last column
 * @param y1 the last row
 */
static void clear_disc(gfx_canvas_t *c, int x0, int y0, int x1, int y1)
{
    const gfx_color_t bg = col(UI_C_BG);
    for (int y = y0; y <= y1; ++y) {
        int a = x0;
        int b = x1;
        while (a <= b && !px_inside(a, y, DIAL_R_CLEAR)) {
            ++a;
        }
        while (b >= a && !px_inside(b, y, DIAL_R_CLEAR)) {
            --b;
        }
        if (a <= b) {
            gfx_fill_rect(c, a, y, b - a + 1, 1, bg);
        }
    }
}

/**
 * @brief Paint one rim tick of a list onto the background: a line 2px wide
 *        (3px for the accent's), antialiased by the distance to its spine.
 *        Only its own pixels are written, so a repaint of a tick equals its
 *        first paint and leaves its neighbours alone.
 * @param c     the canvas
 * @param i     the tick
 * @param n     the ticks
 * @param focus the accent's tick
 * @param erase paint the accent's wider footprint in the background
 */
static void tick(gfx_canvas_t *c, int i, int n, bool focus, bool erase)
{
    const double a = (TICK_FROM + TICK_SWEEP * ((double)i + 0.5) / (double)n)
                     * 3.14159265358979323846 / 180.0;
    const double ax = MID + TICK_R0 * cos(a);
    const double ay = MID + TICK_R0 * sin(a);
    const double bx = MID + TICK_R1 * cos(a);
    const double by = MID + TICK_R1 * sin(a);
    const double half = (focus || erase) ? 1.5 : 1.0;
    const gfx_color_t bg = col(UI_C_BG);
    const gfx_color_t fg = erase ? bg : col(focus ? UI_C_ACCENT : UI_C_GRID);
    const int x0 = (int)floor(fmin(ax, bx) - 2.0);
    const int x1 = (int)ceil(fmax(ax, bx) + 2.0);
    const int y0 = (int)floor(fmin(ay, by) - 2.0);
    const int y1 = (int)ceil(fmax(ay, by) + 2.0);
    const double ux = bx - ax;
    const double uy = by - ay;
    const double len2 = ux * ux + uy * uy;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double px = x + 0.5 - ax;
            const double py = y + 0.5 - ay;
            double t = (px * ux + py * uy) / len2;
            t = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
            const double d = hypot(px - t * ux, py - t * uy);
            const double cov = half + 0.5 - d;
            if (cov <= 0.0) {
                continue;
            }
            const long k = lround((cov > 1.0 ? 1.0 : cov) * 255.0);
            if (k > 0) {
                gfx_pixel(c, x, y, gfx_lerp(bg, fg, (uint8_t)k));
            }
        }
    }
}

/**
 * @brief Paint a ring of the stack in full.
 * @param r     the renderer
 * @param c     the canvas
 * @param i     the ring
 * @param end   the arc's end
 * @param color ui_color_id_t, or UI_C_BG to take it off
 */
static void ring_full(dial_render_t *r, gfx_canvas_t *c, int i,
                      uint32_t end, uint8_t color)
{
    const gfx_color_t on = color == UI_C_BG ? col(UI_C_BG) : col(color);
    const gfx_color_t tr = color == UI_C_BG ? col(UI_C_BG)
                                            : mix(color, TRACK_MIX);
    gfx_ring_aa(c, &r->ring[i], 0, GFX_RING_TURN, end, on, tr, col(UI_C_BG));
}

/**
 * @brief LINK LOST's rim: 24 DANGER dashes of 8° every 15°, from radius
 *        110 to 116, each pixel antialiased by its distance to the dash's
 *        edges, across and along.
 * @param c the canvas
 */
static void dashes(gfx_canvas_t *c)
{
    const gfx_color_t bg = col(UI_C_BG);
    const gfx_color_t fg = col(UI_C_DANGER);
    const double r0 = 110.0;
    const double r1 = 116.0;
    for (int y = 0; y < DIAL_FACE_SIZE; ++y) {
        for (int x = 0; x < DIAL_FACE_SIZE; ++x) {
            if (!px_inside(x, y, (int)r1 + 1) || px_inside(x, y, (int)r0 - 2)) {
                continue;
            }
            const double dx = x + 0.5 - MID;
            const double dy = y + 0.5 - MID;
            const double d = hypot(dx, dy);
            double across = fmin(d - r0, r1 - d) + 0.5;
            /* Degrees clockwise from 3 o'clock: y runs down. */
            double a = atan2(dy, dx) * 180.0 / 3.14159265358979323846;
            a = a < 0.0 ? a + 360.0 : a;
            const double in = fmod(a, 15.0);
            double along = fmin(in - 2.0, 10.0 - in) * 3.14159265358979323846
                           / 180.0 * d + 0.5;
            across = across < 0.0 ? 0.0 : across > 1.0 ? 1.0 : across;
            along = along < 0.0 ? 0.0 : along > 1.0 ? 1.0 : along;
            const long k = lround(across * along * 255.0);
            if (k > 0) {
                gfx_pixel(c, x, y, gfx_lerp(bg, fg, (uint8_t)k));
            }
        }
    }
}

/** @brief The rim decoration of a face. @param r the renderer
 *  @param c the canvas @param deco DIAL_DECO_* */
static void decoration(dial_render_t *r, gfx_canvas_t *c, uint8_t deco)
{
    const gfx_color_t bg = col(UI_C_BG);
    if (deco == DIAL_DECO_DASHED) {
        dashes(c);
    } else if (deco == DIAL_DECO_BIND) {
        gfx_ring_aa(c, &r->ring[1], 0, GFX_RING_TURN, GFX_RING_TURN,
                    col(UI_C_EDGE), bg, bg);
        gfx_ring_aa(c, &r->ring[4], 0, GFX_RING_TURN, GFX_RING_TURN,
                    col(UI_C_GRID), bg, bg);
    } else if (deco == DIAL_DECO_WAIT) {
        gfx_ring_aa(c, &r->ring[1], 0, GFX_RING_TURN, GFX_RING_TURN,
                    col(UI_C_GRID), bg, bg);
    }
}

/** @brief The title tag: the text on the accent, or dimmed; cut so the
 *         tag stays inside DIAL_R_SHAPE.  @param r the renderer
 *         @param c the canvas @param s the scene */
static void draw_title(dial_render_t *r, gfx_canvas_t *c,
                       const dial_scene_t *s)
{
    line_t l;
    int w;
    if (!s->has_title || s->title[0] == '\0') {
        return;
    }
    l.font = &gfx_font_8x16;
    dk_text_copy(l.text, sizeof l.text, s->title);
    for (int cells = gfx_text_cells(s->title); cells > 0; --cells) {
        l.text[gfx_text_prefix(s->title, cells)] = '\0';
        w = cells * l.font->width + TAG_PAD;
        if (dial_box_inside(gfx_rect_make(MID - w / 2, TAG_Y - TAG_H / 2, w,
                                          TAG_H), DIAL_R_SHAPE)) {
            break;
        }
    }
    if (strcmp(l.text, s->title) != 0) {
        ++r->shortened;
    }
    w = gfx_text_width(l.font, l.text, 1) + TAG_PAD;
    gfx_fill_chamfer_rect_ex(c, MID - w / 2, TAG_Y - TAG_H / 2, w, TAG_H, 0,
                             TAG_CUT, 0, TAG_CUT,
                             col(s->title_dim ? UI_C_PANEL_HI : UI_C_ACCENT));
    (void)place(&l, TAG_Y + 1);
    draw_line(r, c, &l, col(s->title_dim ? UI_C_TEXT_DIM
                                         : UI_C_TEXT_ON_LIGHT), -1,
              DIAL_BAND_TITLE);
}

/** @brief Where the pill's row lies: the pill's columns and its font. */
typedef struct {
    int left;               /**< the pill's first column */
    int right;              /**< its last */
    const gfx_font_t *font; /**< its text's font */
    bool usb;               /**< USB is drawn in the battery's place */
} pill_row_t;

/**
 * @brief Lay out the pill's row: ARMED's text in the 10x20 face, or in the
 *        8x16 one when the icons beside it would leave DIAL_R_SHAPE.
 * @param s   the scene
 * @param out the row
 */
static void pill_row(const dial_scene_t *s, pill_row_t *out)
{
    out->font = &gfx_font_10x20;
    out->usb = false;
    for (int k = 0; k < 2; ++k) {
        int w = PILL_SAFE;
        if (s->pill == DIAL_PILL_ARMED) {
            w = gfx_text_width(out->font, s->pill_text, 1) + PILL_PAD;
        }
        out->left = MID - w / 2;
        out->right = out->left + w - 1;
        const int bars_x = out->left - ICON_GAP - BARS_W;
        const int batt_x = out->right + 1 + ICON_GAP;
        if (dial_box_inside(gfx_rect_make(bars_x, BARS_BOTTOM - 12, BARS_W,
                                          13), DIAL_R_SHAPE)
            && dial_box_inside(gfx_rect_make(batt_x, BATT_Y, BATT_W, BATT_H),
                               DIAL_R_SHAPE)) {
            break;
        }
        out->font = &gfx_font_8x16;
    }
    if (s->usb) {
        gfx_rect_t ink;
        const int batt_x = out->right + 1 + ICON_GAP;
        out->usb = !dial_ink(&gfx_font_8x16, "USB", batt_x,
                             PILL_Y - metric(&gfx_font_8x16)->mid, &ink)
                   || dial_box_inside(ink, DIAL_R_SHAPE);
    }
}

/** @brief The pill, the link bars and the battery.  @param r the renderer
 *         @param c the canvas @param s the scene */
static void draw_pill_row(dial_render_t *r, gfx_canvas_t *c,
                          const dial_scene_t *s)
{
    pill_row_t p;
    const uint8_t icon = s->icons_dim ? UI_C_TEXT_FAINT : UI_C_TEXT_DIM;
    pill_row(s, &p);
    if (s->pill != DIAL_PILL_NONE) {
        const bool armed = s->pill == DIAL_PILL_ARMED;
        const int w = p.right - p.left + 1;
        line_t l;
        gfx_fill_round_rect(c, p.left, PILL_Y - PILL_H / 2, w, PILL_H,
                            PILL_H / 2,
                            col(armed ? UI_C_DANGER : UI_C_PANEL_HI));
        if (!armed) {
            gfx_draw_round_rect(c, p.left, PILL_Y - PILL_H / 2, w, PILL_H,
                                PILL_H / 2, col(UI_C_EDGE));
        }
        l.font = p.font;
        dk_text_copy(l.text, sizeof l.text, s->pill_text);
        (void)place(&l, PILL_Y);
        draw_line(r, c, &l, col(armed ? UI_C_TEXT_ON_LIGHT : UI_C_TEXT), -1,
                  DIAL_BAND_PILL);
    }
    /* Four bars, 3px wide, 4, 7, 10 and 13px tall, ending 6px left of the
     * pill. */
    const int bx = p.left - ICON_GAP - BARS_W;
    for (int i = 0; i < 4; ++i) {
        const int h = 4 + 3 * i;
        gfx_fill_rect(c, bx + 4 * i, BARS_BOTTOM - h + 1, 3, h,
                      col(i < s->bars ? s->bars_color : UI_C_PANEL_HI));
    }
    /* The battery 6px right of the pill: a 16 x 9 body and a nub, its fill
     * by the reading, a dash for an unknown reading; or USB. */
    const int x = p.right + 1 + ICON_GAP;
    if (p.usb) {
        line_t l;
        l.font = &gfx_font_8x16;
        dk_text_copy(l.text, sizeof l.text, "USB");
        l.x = x;
        l.y = PILL_Y - metric(l.font)->mid;
        draw_line(r, c, &l, col(icon), -1, DIAL_BAND_PILL);
        return;
    }
    gfx_draw_round_rect(c, x, BATT_Y, BATT_W - 2, BATT_H, 2, col(icon));
    gfx_fill_rect(c, x + BATT_W - 2, BATT_Y + 3, 2, 3, col(icon));
    if (!s->batt_known) {
        gfx_fill_rect(c, x + 5, BATT_Y + 4, 6, 1, col(icon));
    } else if (s->batt_pct > 0u) {
        const uint8_t fc = s->icons_dim ? UI_C_TEXT_FAINT
                           : s->batt_pct > 30u ? UI_C_OK
                           : s->batt_pct > 15u ? UI_C_WARN : UI_C_DANGER;
        const int fw = (12 * s->batt_pct + 50) / 100;
        gfx_fill_rect(c, x + 2, BATT_Y + 2, fw < 1 ? 1 : fw, 5, col(fc));
    }
}

/* ---------------------------------------------------------------- body */

gfx_rect_t dial_cell_rect(int i)
{
    if (i < 0 || i >= DIAL_CELLS) {
        return gfx_rect_make(0, 0, 0, 0);
    }
    return gfx_rect_make(k_col[i % 2] - DIAL_CELL_W / 2, k_row[i / 2] - 21,
                         DIAL_CELL_W, DIAL_CELL_H);
}

bool dial_band_rows(dial_band_t band, int *y0, int *y1)
{
    static const int16_t rows[][2] = {
        { 0, BAND_PILL_Y0 - 1 },
        { BAND_PILL_Y0, BAND_PILL_Y1 },
        { BAND_BODY_Y0, BAND_BODY_Y1 },
        { BAND_HINT_Y0, BAND_HINT_Y1 },
    };
    if (y0 == NULL || y1 == NULL || (unsigned)band >= 4u) {
        return false;
    }
    *y0 = rows[band][0];
    *y1 = rows[band][1];
    return true;
}

int dial_ring_radius(int i)
{
    return DIAL_RING_R0 - DIAL_RING_PITCH * i;
}

/**
 * @brief A cell of the grid: the label faint, 17px above the row's middle;
 *        the value in the 10x20 face and its unit in the 8x16 one directly
 *        behind it, on one baseline 7px below the middle, in the type's
 *        colour; at half strength while SAFE.  A value too wide for the
 *        cell is drawn in the 8x16 face.
 * @param r the renderer
 * @param c the canvas
 * @param s the scene
 * @param i the cell
 */
static void draw_cell(dial_render_t *r, gfx_canvas_t *c,
                      const dial_scene_t *s, int i)
{
    const dial_cell_t *e = &s->cell[i];
    const int cx = k_col[i % 2];
    const int cy = k_row[i / 2];
    const gfx_color_t fg = s->dim ? mix(e->color, DIM_MIX) : col(e->color);
    line_t v;
    line_t u;
    line_t l;
    if (!e->used) {
        return;
    }
    l.font = &gfx_font_8x16;
    dk_text_copy(l.text, sizeof l.text, e->label);
    l.x = cx - gfx_text_width(l.font, l.text, 1) / 2;
    l.y = cy - 17 - metric(l.font)->mid;
    draw_line(r, c, &l, col(UI_C_TEXT_FAINT), i, DIAL_BAND_CELL);

    u.font = &gfx_font_8x16;
    dk_text_copy(u.text, sizeof u.text, e->unit);
    dk_text_copy(v.text, sizeof v.text, e->value);
    v.font = &gfx_font_10x20;
    int wu = gfx_text_width(u.font, u.text, 1);
    int total = gfx_text_width(v.font, v.text, 1) + (wu > 0 ? 1 + wu : 0);
    if (total > DIAL_CELL_W) {
        v.font = &gfx_font_8x16;
        total = gfx_text_width(v.font, v.text, 1) + (wu > 0 ? 1 + wu : 0);
    }
    v.x = cx - total / 2;
    v.y = cy + 7 - metric(v.font)->base;
    u.x = v.x + gfx_text_width(v.font, v.text, 1) + 1;
    u.y = cy + 7 - metric(u.font)->base;
    draw_line(r, c, &v, fg, i, DIAL_BAND_CELL);
    draw_line(r, c, &u, fg, i, DIAL_BAND_CELL);
}

/**
 * @brief A numeral with its unit: the numeral in the largest numeral face
 *        it fits in, the unit 6px behind it on its baseline, dimmed.
 * @param r the renderer
 * @param c the canvas
 * @param b the block
 * @param band its band
 */
static void draw_number(dial_render_t *r, gfx_canvas_t *c,
                        const dial_block_t *b, dial_band_t band)
{
    static const gfx_font_t *const nums[] = {
        &gfx_font_num_32x44, &gfx_font_num_24x30, &gfx_font_16x28,
        &gfx_font_12x24, &gfx_font_10x20, &gfx_font_8x16,
    };
    static const gfx_font_t *const units[] = {
        &gfx_font_12x24, &gfx_font_10x20, &gfx_font_10x20,
        &gfx_font_8x16, &gfx_font_8x16, &gfx_font_8x16,
    };
    line_t n;
    line_t u;
    for (size_t k = 0; k < sizeof nums / sizeof nums[0]; ++k) {
        gfx_rect_t a;
        gfx_rect_t e;
        n.font = nums[k];
        u.font = units[k];
        if (!holds(n.font, b->text)) {
            continue;
        }
        dk_text_copy(n.text, sizeof n.text, b->text);
        dk_text_copy(u.text, sizeof u.text, b->unit);
        const int wn = gfx_text_width(n.font, n.text, 1);
        const int wu = gfx_text_width(u.font, u.text, 1);
        const int w = wn + (wu > 0 ? 6 + wu : 0);
        n.x = MID - w / 2;
        n.y = b->y - metric(n.font)->mid;
        u.x = n.x + wn + 6;
        u.y = n.y + metric(n.font)->base - metric(u.font)->base;
        const bool fit_n = !dial_ink(n.font, n.text, n.x, n.y, &a)
                           || dial_box_inside(a, DIAL_R_TEXT);
        const bool fit_u = !dial_ink(u.font, u.text, u.x, u.y, &e)
                           || dial_box_inside(e, DIAL_R_TEXT);
        if (fit_n && fit_u) {
            draw_line(r, c, &n, col(b->color), -1, band);
            draw_line(r, c, &u, col(UI_C_TEXT_DIM), -1, band);
            return;
        }
    }
}

/**
 * @brief A body block or a hint: its lines, and a focus's brackets.
 * @param r the renderer
 * @param c the canvas
 * @param b the block
 * @param band DIAL_BAND_BODY or DIAL_BAND_HINT
 */
static void draw_block(dial_render_t *r, gfx_canvas_t *c,
                       const dial_block_t *b, dial_band_t band)
{
    laid_t laid;
    if (b->text[0] == '\0') {
        return;
    }
    if (b->kind == DIAL_B_NUMBER) {
        draw_number(r, c, b, band);
        return;
    }
    if (!lay(&laid, b)) {
        ++r->shortened;
    }
    for (int i = 0; i < laid.n; ++i) {
        draw_line(r, c, &laid.line[i], col(b->color), -1, band);
    }
    if (b->kind == DIAL_B_FOCUS && laid.n > 0) {
        /* Corner brackets 12px clear of the text, 20 rows above and below
         * the middle, 8px arms 2px wide, inside DIAL_R_SHAPE. */
        const gfx_color_t a = col(UI_C_ACCENT);
        int half = gfx_text_width(laid.line[0].font, laid.line[0].text, 1) / 2
                   + 12;
        while (half > 10
               && !dial_box_inside(gfx_rect_make(MID - half, b->y - 20,
                                                 2 * half, 41),
                                   DIAL_R_SHAPE)) {
            --half;
        }
        const int x0 = MID - half;
        const int x1 = MID + half - 1;
        const int y0 = b->y - 20;
        const int y1 = b->y + 20;
        gfx_fill_rect(c, x0, y0, 8, 2, a);
        gfx_fill_rect(c, x0, y0, 2, 8, a);
        gfx_fill_rect(c, x1 - 7, y0, 8, 2, a);
        gfx_fill_rect(c, x1 - 1, y0, 2, 8, a);
        gfx_fill_rect(c, x0, y1 - 1, 8, 2, a);
        gfx_fill_rect(c, x0, y1 - 7, 2, 8, a);
        gfx_fill_rect(c, x1 - 7, y1 - 1, 8, 2, a);
        gfx_fill_rect(c, x1 - 1, y1 - 7, 2, 8, a);
    }
}

/** @brief Every body block. @param r the renderer @param c the canvas
 *  @param s the scene */
static void draw_body(dial_render_t *r, gfx_canvas_t *c,
                      const dial_scene_t *s)
{
    if (s->face == DIAL_FACE_READING) {
        for (int i = 0; i < DIAL_CELLS; ++i) {
            draw_cell(r, c, s, i);
        }
        return;
    }
    for (int i = 0; i < DIAL_BLOCKS; ++i) {
        draw_block(r, c, &s->block[i], DIAL_BAND_BODY);
    }
}

/* ------------------------------------------------------------- the face */

/** @brief The whole face. @param r the renderer @param c the canvas
 *  @param s the scene */
static void draw_full(dial_render_t *r, gfx_canvas_t *c,
                      const dial_scene_t *s)
{
    gfx_clear(c, col(UI_C_BG));
    decoration(r, c, s->deco);
    for (int i = 0; i < DIAL_RINGS; ++i) {
        if (s->ring[i].used) {
            ring_full(r, c, i, s->ring[i].end, s->ring[i].color);
        }
    }
    for (int i = 0; i < s->ticks; ++i) {
        tick(c, i, s->ticks, i == s->tick_cur, false);
    }
    draw_title(r, c, s);
    draw_pill_row(r, c, s);
    draw_body(r, c, s);
    draw_block(r, c, &s->hint[0], DIAL_BAND_HINT);
    draw_block(r, c, &s->hint[1], DIAL_BAND_HINT);
}

/** @brief Whether two scenes differ in what only a full draw redraws.
 *  @param a one @param b the other @return true when they do */
static bool frame_differs(const dial_scene_t *a, const dial_scene_t *b)
{
    return a->face != b->face || a->lang != b->lang || a->deco != b->deco
           || a->has_title != b->has_title || a->title_dim != b->title_dim
           || strcmp(a->title, b->title) != 0 || a->ticks != b->ticks;
}

/** @brief Whether two scenes differ in the pill's row.
 *  @param a one @param b the other @return true when they do */
static bool pill_differs(const dial_scene_t *a, const dial_scene_t *b)
{
    return a->pill != b->pill || strcmp(a->pill_text, b->pill_text) != 0
           || a->bars != b->bars || a->bars_color != b->bars_color
           || a->icons_dim != b->icons_dim || a->batt_known != b->batt_known
           || a->batt_pct != b->batt_pct || a->usb != b->usb;
}

bool dial_render_init(dial_render_t *r)
{
    int at = 0;
    if (r == NULL) {
        return false;
    }
    memset(r, 0, sizeof *r);
    for (int i = 0; i < DIAL_RINGS; ++i) {
        gfx_ring_t *g = &r->ring[i];
        g->px = &dial_ring_px[at];
        g->n = dial_ring_n[i];
        g->cx2 = DIAL_FACE_C2;
        g->cy2 = DIAL_FACE_C2;
        g->r = (int16_t)dial_ring_radius(i);
        at += dial_ring_n[i];
    }
    r->lang = DK_LANG_EN;
    return true;
}

void dial_render_invalidate(dial_render_t *r)
{
    if (r != NULL) {
        r->valid = false;
    }
}

bool dial_render(dial_render_t *r, gfx_canvas_t *c, const dk_face_t *f,
                 const dial_face_view_t *v)
{
    dial_scene_t s;
    bool drew = false;
    if (r == NULL || c == NULL || f == NULL || v == NULL) {
        return false;
    }
    dial_scene_build(&s, f, v, r->lang);
    r->lang = s.lang;
    const dial_scene_t *o = &r->shown;
    if (!r->valid || frame_differs(&s, o)) {
        draw_full(r, c, &s);
        r->shown = s;
        r->valid = true;
        return true;
    }
    if (pill_differs(&s, o)) {
        clear_disc(c, 0, BAND_PILL_Y0, DIAL_FACE_SIZE - 1, BAND_PILL_Y1);
        draw_pill_row(r, c, &s);
        drew = true;
    }
    if (s.face == DIAL_FACE_READING) {
        for (int i = 0; i < DIAL_CELLS; ++i) {
            if (s.dim == o->dim
                && memcmp(&s.cell[i], &o->cell[i], sizeof s.cell[i]) == 0) {
                continue;
            }
            const gfx_rect_t b = dial_cell_rect(i);
            gfx_fill_rect(c, b.x, b.y, b.w, b.h, col(UI_C_BG));
            draw_cell(r, c, &s, i);
            drew = true;
        }
    } else if (memcmp(s.block, o->block, sizeof s.block) != 0) {
        clear_disc(c, 0, BAND_BODY_Y0, DIAL_FACE_SIZE - 1, BAND_BODY_Y1);
        draw_body(r, c, &s);
        drew = true;
    }
    if (memcmp(s.hint, o->hint, sizeof s.hint) != 0) {
        clear_disc(c, 0, BAND_HINT_Y0, DIAL_FACE_SIZE - 1, BAND_HINT_Y1);
        draw_block(r, c, &s.hint[0], DIAL_BAND_HINT);
        draw_block(r, c, &s.hint[1], DIAL_BAND_HINT);
        drew = true;
    }
    for (int i = 0; i < DIAL_RINGS; ++i) {
        const dial_ring_t *n = &s.ring[i];
        const dial_ring_t *w = &o->ring[i];
        if (n->used != w->used || (n->used && n->color != w->color)) {
            ring_full(r, c, i, n->end, n->used ? n->color : (uint8_t)UI_C_BG);
            drew = true;
        } else if (n->used && n->end != w->end) {
            gfx_ring_move(c, &r->ring[i], w->end, n->end, col(n->color),
                          mix(n->color, TRACK_MIX), col(UI_C_BG));
            drew = true;
        }
    }
    if (s.ticks > 0u && s.tick_cur != o->tick_cur) {
        /* The old accent's tick is 3px wide: its footprint goes to the
         * background before the 2px tick is painted there. */
        tick(c, o->tick_cur, s.ticks, false, true);
        tick(c, o->tick_cur, s.ticks, false, false);
        tick(c, s.tick_cur, s.ticks, true, false);
        drew = true;
    }
    r->shown = s;
    return drew;
}
