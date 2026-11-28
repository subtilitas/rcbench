/**
 * @file
 * @brief The Guition JC3636K518C's faces drawn natively at 360 x 360 from
 *        the scene shared/dial builds (dk518_face.h).
 *
 * The scene (dial_scene_build()) says what a face shows: the title, the
 * pill, the link bars and the battery, six cells and six rings or the body
 * blocks, a list's rim ticks, a rim decoration, two press hints.  This
 * file says where, at the K518's size: the title tag at row
 * 63, the pill at 99 with the bars to its left and the battery to its
 * right, 9px clear of it, the cells in three rows of two about columns
 * 114 and 246 and rows 150, 198 and 246, the hints at rows 281 and 302,
 * and six rings of 2px from radius 176 inward at a pitch of 4px, each from
 * 12 o'clock clockwise to its reading's share over a full-circle track of
 * its colour mixed 22% onto the background.  Every string's cells lie
 * inside radius 150 (DK518_R_TEXT) of the display's middle, every shape
 * inside 151.
 *
 * The other faces' body blocks stand on the scene's rows taken about the
 * display's middle at 3/2 (row 120 of the M5's 240 is row 180 here), in
 * the next larger font of the K518's chain (10x20 for the M5's 8x16, up to
 * 20x36), wrapped onto the block's lines; a text too wide steps down the
 * chain, then drops its last part, two spaces apart, and a part that holds
 * a digit is dropped whole, never cut.  Numerals stand in the 32x44 face,
 * then 24x30, their unit 9px behind.  A list's rim ticks run from radius
 * 159 to 174 over 270 degrees; LINK LOST's 24 dashes of 8 degrees from
 * radius 165 to 174; the bind and wait faces' rings at radius 172 and 160.
 *
 * A change of the scene draws the whole face again; an unchanged scene
 * draws nothing.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <string.h>

#include "dk518_face.h"
#include "ui_theme.h"

/** @brief The display's middle, column and row. */
#define MID (DK518_SIZE / 2)
/** @brief The title tag: middle row, height, padding and corner cut. @{ */
#define TAG_Y   63
#define TAG_H   30 /**< height */
#define TAG_PAD 18 /**< width beyond the text */
#define TAG_CUT 9  /**< top-right and bottom-left cut */
/** @} */
/** @brief The pill: middle row, height, padding, SAFE's width. @{ */
#define PILL_Y    99
#define PILL_H    28  /**< height */
#define PILL_PAD  18  /**< width beyond ARMED's text */
#define PILL_SAFE 114 /**< SAFE's width */
#define ICON_GAP  9   /**< from the pill to an icon */
/** @} */
/** @brief The link bars: 4 of 4px at a 6px pitch, bottom row 108. @{ */
#define BARS_W      22
#define BARS_BOTTOM 108 /**< the bars' bottom row */
/** @} */
/** @brief The battery glyph: top row and size, the nub included. @{ */
#define BATT_Y 93
#define BATT_W 27 /**< width with the nub */
#define BATT_H 13 /**< height */
/** @} */
/** @brief The rows of the two hints' capitals' middles. */
static const int k_hint_y[2] = { 281, 302 };
/** @brief The grid's column middles. */
static const int k_col[2] = { 114, 246 };
/** @brief The grid's row middles. */
static const int k_row[3] = { 150, 198, 246 };
/** @brief The track: a ring's colour mixed 22% onto the background. */
#define TRACK_MIX 56u
/** @brief Readings at half strength while SAFE: 50%. */
#define DIM_MIX 128u

/** @brief A font's middle of the capitals, first row under the baseline
 *         and line pitch in its cell. */
typedef struct {
    const gfx_font_t *font; /**< the font */
    uint8_t mid;            /**< middle of the capitals */
    uint8_t base;           /**< first row under the baseline */
    uint8_t pitch;          /**< line pitch */
} metric_t;

/** @brief Every font a K518 face draws with. */
static const metric_t k_metric[] = {
    { &gfx_font_8x16, 8, 13, 14 },        { &gfx_font_10x20, 9, 15, 18 },
    { &gfx_font_12x24, 11, 19, 22 },      { &gfx_font_16x28, 15, 25, 26 },
    { &gfx_font_20x36, 17, 29, 30 },      { &gfx_font_num_24x30, 15, 29, 32 },
    { &gfx_font_num_32x44, 21, 41, 44 },
};
/** @brief The text chain, smallest first: a block's M5 font DIAL_FONT_*
 *         starts one step up here, BIG and NUM at 20x36. */
static const gfx_font_t *const k_chain[5] = {
    &gfx_font_8x16, &gfx_font_10x20, &gfx_font_12x24, &gfx_font_16x28,
    &gfx_font_20x36,
};
/** @brief A list's rim ticks: inner and outer radius, first angle and
 *         sweep in degrees clockwise from 3 o'clock. @{ */
#define TICK_R0    159.0
#define TICK_R1    174.0 /**< outer radius */
#define TICK_FROM  135.0 /**< the first tick's side of the sweep */
#define TICK_SWEEP 270.0 /**< the sweep */
/** @} */
/** @brief The rings of the bind and wait faces' decorations, pixels. @{ */
#define DECO_R_OUT 172
#define DECO_R_IN  160 /**< the bind face's inner ring */
/** @} */
/** @brief The display's middle as a coordinate: a pixel's center is its
 *         index plus 0.5. */
#define MIDF 180.0
/** @brief The constant pi. */
#define PI 3.14159265358979323846

/** @brief A font's metrics. @param f the font @return its row; the 8x16's
 *         for another */
static const metric_t *metric(const gfx_font_t *f)
{
    for (size_t i = 0; i < sizeof k_metric / sizeof k_metric[0]; ++i) {
        if (k_metric[i].font == f) {
            return &k_metric[i];
        }
    }
    return &k_metric[0];
}

gfx_color_t dk518_color(uint8_t id, unsigned k)
{
    const uint32_t a = ui_theme_rgb24(UI_THEME_DARK, (ui_color_id_t)id);
    const uint32_t b = ui_theme_rgb24(UI_THEME_DARK, UI_C_BG);
    unsigned ch[3];
    if (k > 255u) {
        k = 255u;
    }
    for (int i = 0; i < 3; ++i) {
        const unsigned sh = (unsigned)(16 - 8 * i);
        const unsigned fa = (a >> sh) & 0xFFu;
        const unsigned fb = (b >> sh) & 0xFFu;
        ch[i] = (fb * (255u - k) + fa * k + 127u) / 255u;
    }
    return GFX_RGB(ch[0], ch[1], ch[2]);
}

/** @brief A theme colour at full strength. @param id ui_color_id_t
 *  @return the colour */
static gfx_color_t col(uint8_t id)
{
    return dk518_color(id, 255u);
}

bool dk518_box_inside(gfx_rect_t b, int r)
{
    if (b.w <= 0 || b.h <= 0) {
        return true;
    }
    const int xs[2] = { b.x, b.x + b.w - 1 };
    const int ys[2] = { b.y, b.y + b.h - 1 };
    for (int i = 0; i < 2; ++i) {
        for (int k = 0; k < 2; ++k) {
            /* A pixel's center against the display's middle, in half
             * pixels: (2x + 1) - 360. */
            const long dx = 2L * xs[i] + 1 - DK518_SIZE;
            const long dy = 2L * ys[k] + 1 - DK518_SIZE;
            if (dx * dx + dy * dy > 4L * r * r) {
                return false;
            }
        }
    }
    return true;
}

/**
 * @brief Draw a string with its cells' top-left at (@p x, @p y) and tell
 *        the renderer's watcher.
 * @param r    the renderer
 * @param c    the canvas
 * @param f    the font
 * @param s    the text; "" draws nothing
 * @param x    the left edge
 * @param y    the top row
 * @param fg   the colour
 * @param cell the grid cell, or -1
 */
static void put(dk518_render_t *r, gfx_canvas_t *c, const gfx_font_t *f,
                const char *s, int x, int y, gfx_color_t fg, int cell)
{
    if (s[0] == '\0') {
        return;
    }
    gfx_text(c, x, y, s, f, fg, 1);
    if (r->seen != NULL) {
        const dk518_seen_t t = {
            s, gfx_rect_make(x, y, gfx_text_width(f, s, 1), f->height), cell,
        };
        r->seen(r->seen_ctx, &t);
    }
}

/** @brief Whether a font holds every code point of a string.
 *  @param f the font @param t the text @return true when it does */
static bool holds(const gfx_font_t *f, const char *t)
{
    const char *p = t;
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

/** @brief A line laid out: its text, font and cells' top-left corner. */
typedef struct {
    char              text[DIAL_TXT]; /**< the text */
    const gfx_font_t *font;           /**< the font */
    int               x;              /**< the left edge */
    int               y;              /**< the top row */
} line_t;

/** @brief A block laid out: up to 3 lines. */
typedef struct {
    line_t line[3]; /**< the lines */
    int    n;       /**< lines used */
} laid_t;

/** @brief Center a line on the middle column, its capitals' middle on row
 *         @p y.  @param l the line, text and font set @param y the row
 *  @return true when its cells lie inside DK518_R_TEXT */
static bool place(line_t *l, int y)
{
    const int w = gfx_text_width(l->font, l->text, 1);
    l->x = MID - w / 2;
    l->y = y - metric(l->font)->mid;
    return dk518_box_inside(gfx_rect_make(l->x, l->y, w, l->font->height),
                            DK518_R_TEXT);
}

/**
 * @brief Wrap a text onto @p n lines in a font, each line taking the most
 *        whole words that fit, the lines centered on row @p y.
 * @param out  the lines
 * @param f    the font
 * @param text the text
 * @param y    the block's middle row
 * @param n    the lines, 1 to 3
 * @return true when every word is on a line inside DK518_R_TEXT
 */
static bool wrap(laid_t *out, const gfx_font_t *f, const char *text, int y,
                 int n)
{
    const char *p = text;
    out->n = 0;
    for (int i = 0; i < n; ++i) {
        line_t *l = &out->line[i];
        const int ly = y + (2 * i - (n - 1)) * metric(f)->pitch / 2;
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
 * @brief Lay a text out at a row: from font @p top of the chain down, the
 *        fewest of @p most lines in which every line fits.
 * @param out  the lines
 * @param text the text
 * @param y    the middle row
 * @param top  the chain's first font
 * @param most the most lines
 * @return true when it fits
 */
static bool fit(laid_t *out, const char *text, int y, int top, int most)
{
    for (int f = top; f >= 0; --f) {
        if (!holds(k_chain[f], text)) {
            continue;
        }
        for (int n = 1; n <= most; ++n) {
            if (wrap(out, k_chain[f], text, y, n)) {
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief Lay a text out: whole if it fits; else without its last parts,
 *        two spaces apart, one by one; else, when the part left holds no
 *        digit, cut by characters in the 8x16 face.
 * @param out  the lines; none when nothing fits
 * @param text the text
 * @param y    the middle row
 * @param top  the chain's first font
 * @param most the most lines
 * @return true when the lines hold the whole text
 */
static bool lay(laid_t *out, const char *text, int y, int top, int most)
{
    char t[DIAL_TXT];
    dk_text_copy(t, sizeof t, text);
    if (fit(out, t, y, top, most)) {
        return true;
    }
    for (;;) {
        char *gap = NULL;
        for (char *p = strstr(t, "  "); p != NULL; p = strstr(p + 1, "  ")) {
            gap = p;
        }
        if (gap == NULL) {
            break;
        }
        *gap = '\0';
        if (fit(out, t, y, top, most)) {
            return false;
        }
    }
    out->n = 0;
    if (strpbrk(t, "0123456789") != NULL) {
        return false;
    }
    line_t *l = &out->line[0];
    l->font = k_chain[0];
    dk_text_copy(l->text, sizeof l->text, t);
    for (int cells = gfx_text_cells(t); cells > 0; --cells) {
        l->text[gfx_text_prefix(t, cells)] = '\0';
        if (place(l, y)) {
            out->n = 1;
            return false;
        }
    }
    return false;
}

/** @brief The title tag in the accent, its text in 12x24, else 10x20, else
 *         8x16, cut only in the last. @param r the renderer @param c the
 *         canvas @param s the scene */
static void draw_title(dk518_render_t *r, gfx_canvas_t *c,
                       const dial_scene_t *s)
{
    static const gfx_font_t *const fonts[] = {
        &gfx_font_12x24, &gfx_font_10x20, &gfx_font_8x16,
    };
    if (!s->has_title || s->title[0] == '\0') {
        return;
    }
    char t[DK_TITLE_MAX + 1];
    const gfx_font_t *f = fonts[0];
    for (size_t k = 0; k < sizeof fonts / sizeof fonts[0]; ++k) {
        f = fonts[k];
        dk_text_copy(t, sizeof t, s->title);
        int cells = gfx_text_cells(t);
        const bool last = k + 1u == sizeof fonts / sizeof fonts[0];
        while (cells > 0 && !dk518_box_inside(
                   gfx_rect_make(MID - (cells * f->width + TAG_PAD) / 2,
                                 TAG_Y - TAG_H / 2, cells * f->width
                                 + TAG_PAD, TAG_H), DK518_R_SHAPE)) {
            if (!last) {
                cells = 0;
                break;
            }
            t[gfx_text_prefix(s->title, --cells)] = '\0';
        }
        if (cells > 0) {
            break;
        }
    }
    if (strcmp(t, s->title) != 0) {
        ++r->shortened;
    }
    const int w = gfx_text_width(f, t, 1) + TAG_PAD;
    gfx_fill_chamfer_rect_ex(c, MID - w / 2, TAG_Y - TAG_H / 2, w, TAG_H, 0,
                             TAG_CUT, 0, TAG_CUT,
                             col(s->title_dim ? UI_C_PANEL_HI : UI_C_ACCENT));
    put(r, c, f, t, MID - gfx_text_width(f, t, 1) / 2,
        TAG_Y + 1 - metric(f)->mid,
        col(s->title_dim ? UI_C_TEXT_DIM : UI_C_TEXT_ON_LIGHT), -1);
}

/** @brief The pill, the link bars and the battery.  @param r the renderer
 *         @param c the canvas @param s the scene */
static void draw_pill_row(dk518_render_t *r, gfx_canvas_t *c,
                          const dial_scene_t *s)
{
    const gfx_font_t *f = &gfx_font_12x24;
    int left = 0;
    int right = 0;
    for (int k = 0; k < 2; ++k) {
        const int w = s->pill == DIAL_PILL_ARMED
                          ? gfx_text_width(f, s->pill_text, 1) + PILL_PAD
                          : PILL_SAFE;
        left = MID - w / 2;
        right = left + w - 1;
        if (dk518_box_inside(gfx_rect_make(left - ICON_GAP - BARS_W,
                                           BARS_BOTTOM - 17, BARS_W, 18),
                             DK518_R_SHAPE)
            && dk518_box_inside(gfx_rect_make(right + 1 + ICON_GAP, BATT_Y,
                                              BATT_W, BATT_H),
                                DK518_R_SHAPE)) {
            break;
        }
        f = &gfx_font_10x20;
    }
    if (s->pill != DIAL_PILL_NONE) {
        const bool armed = s->pill == DIAL_PILL_ARMED;
        const int w = right - left + 1;
        gfx_fill_round_rect(c, left, PILL_Y - PILL_H / 2, w, PILL_H,
                            PILL_H / 2,
                            col(armed ? UI_C_DANGER : UI_C_PANEL_HI));
        if (!armed) {
            gfx_draw_round_rect(c, left, PILL_Y - PILL_H / 2, w, PILL_H,
                                PILL_H / 2, col(UI_C_EDGE));
        }
        const int tw = gfx_text_width(f, s->pill_text, 1);
        put(r, c, f, s->pill_text, MID - tw / 2, PILL_Y - metric(f)->mid,
            col(armed ? UI_C_TEXT_ON_LIGHT : UI_C_TEXT), -1);
    }
    const uint8_t icon = s->icons_dim ? UI_C_TEXT_FAINT : UI_C_TEXT_DIM;
    /* Four bars, 4px wide, 6, 10, 14 and 18px tall, 9px left of the pill. */
    const int bx = left - ICON_GAP - BARS_W;
    for (int i = 0; i < 4; ++i) {
        const int h = 6 + 4 * i;
        gfx_fill_rect(c, bx + 6 * i, BARS_BOTTOM - h + 1, 4, h,
                      col(i < s->bars ? s->bars_color : UI_C_PANEL_HI));
    }
    /* The battery 9px right of the pill: a 24 x 13 body and a 3 x 5 nub,
     * its fill by the reading, a dash for an unknown one; or USB. */
    const int x = right + 1 + ICON_GAP;
    if (s->usb) {
        put(r, c, &gfx_font_10x20, "USB", x,
            PILL_Y - metric(&gfx_font_10x20)->mid, col(icon), -1);
        return;
    }
    gfx_draw_round_rect(c, x, BATT_Y, BATT_W - 3, BATT_H, 3, col(icon));
    gfx_fill_rect(c, x + BATT_W - 3, BATT_Y + 4, 3, 5, col(icon));
    if (!s->batt_known) {
        gfx_fill_rect(c, x + 8, BATT_Y + 6, 8, 1, col(icon));
    } else if (s->batt_pct > 0u) {
        const uint8_t fc = s->icons_dim ? UI_C_TEXT_FAINT
                           : s->batt_pct > 30u ? UI_C_OK
                           : s->batt_pct > 15u ? UI_C_WARN : UI_C_DANGER;
        const int fw = (18 * s->batt_pct + 50) / 100;
        gfx_fill_rect(c, x + 3, BATT_Y + 3, fw < 1 ? 1 : fw, 7, col(fc));
    }
}

gfx_rect_t dk518_cell_rect(int i)
{
    if (i < 0 || i >= DIAL_CELLS) {
        return gfx_rect_make(0, 0, 0, 0);
    }
    return gfx_rect_make(k_col[i % 2] - DK518_CELL_W / 2, k_row[i / 2] - 34,
                         DK518_CELL_W, DK518_CELL_H);
}

int dk518_ring_radius(int i)
{
    return DK518_RING_R0 - DK518_RING_PITCH * i;
}

/**
 * @brief A cell of the grid: the label faint in 10x20, 25px above the
 *        row's middle; the value in 16x28 with its unit in 10x20 directly
 *        behind it on one baseline 11px below the middle, in the type's
 *        colour, at half strength while SAFE.  A value too wide for the
 *        cell is drawn in 12x24, then in 10x20.
 * @param r the renderer
 * @param c the canvas
 * @param s the scene
 * @param i the cell
 */
static void draw_cell(dk518_render_t *r, gfx_canvas_t *c,
                      const dial_scene_t *s, int i)
{
    static const gfx_font_t *const values[] = {
        &gfx_font_16x28, &gfx_font_12x24, &gfx_font_10x20,
    };
    const dial_cell_t *e = &s->cell[i];
    if (!e->used) {
        return;
    }
    const int cx = k_col[i % 2];
    const int cy = k_row[i / 2];
    const gfx_color_t fg = s->dim ? dk518_color(e->color, DIM_MIX)
                                  : col(e->color);
    const gfx_font_t *lf = &gfx_font_10x20;
    put(r, c, lf, e->label, cx - gfx_text_width(lf, e->label, 1) / 2,
        cy - 25 - metric(lf)->mid, col(UI_C_TEXT_FAINT), i);
    const gfx_font_t *uf = &gfx_font_10x20;
    const int wu = gfx_text_width(uf, e->unit, 1);
    const gfx_font_t *vf = values[0];
    int total = 0;
    for (size_t k = 0; k < sizeof values / sizeof values[0]; ++k) {
        vf = values[k];
        total = gfx_text_width(vf, e->value, 1) + (wu > 0 ? 2 + wu : 0);
        if (total <= DK518_CELL_W) {
            break;
        }
    }
    const int vx = cx - total / 2;
    put(r, c, vf, e->value, vx, cy + 11 - metric(vf)->base, fg, i);
    put(r, c, uf, e->unit, vx + gfx_text_width(vf, e->value, 1) + 2,
        cy + 11 - metric(uf)->base, fg, i);
}

/** @brief A scene row of the M5's 240 x 240 face taken about the middle
 *         at 3/2.  @param y the M5 row @return the K518's row */
static int row(int y)
{
    return MID + (y - 120) * 3 / 2;
}

/**
 * @brief A numeral with its unit: the numeral in the largest numeral face
 *        it fits in, the unit 9px behind it on its baseline, dimmed.  A
 *        numeral that fits in none is not drawn.
 * @param r the renderer
 * @param c the canvas
 * @param b the block
 * @param y its middle row
 */
static void draw_number(dk518_render_t *r, gfx_canvas_t *c,
                        const dial_block_t *b, int y)
{
    static const gfx_font_t *const nums[] = {
        &gfx_font_num_32x44, &gfx_font_num_24x30, &gfx_font_16x28,
        &gfx_font_12x24, &gfx_font_10x20, &gfx_font_8x16,
    };
    static const gfx_font_t *const units[] = {
        &gfx_font_16x28, &gfx_font_12x24, &gfx_font_12x24,
        &gfx_font_10x20, &gfx_font_8x16, &gfx_font_8x16,
    };
    for (size_t k = 0; k < sizeof nums / sizeof nums[0]; ++k) {
        const gfx_font_t *nf = nums[k];
        const gfx_font_t *uf = units[k];
        if (!holds(nf, b->text)) {
            continue;
        }
        const int wn = gfx_text_width(nf, b->text, 1);
        const int wu = gfx_text_width(uf, b->unit, 1);
        const int nx = MID - (wn + (wu > 0 ? 9 + wu : 0)) / 2;
        const int ny = y - metric(nf)->mid;
        const int uy = ny + metric(nf)->base - metric(uf)->base;
        if (dk518_box_inside(gfx_rect_make(nx, ny, wn, nf->height),
                             DK518_R_TEXT)
            && dk518_box_inside(gfx_rect_make(nx + wn + 9, uy, wu,
                                              wu > 0 ? uf->height : 0),
                                DK518_R_TEXT)) {
            put(r, c, nf, b->text, nx, ny, col(b->color), -1);
            put(r, c, uf, b->unit, nx + wn + 9, uy, col(UI_C_TEXT_DIM), -1);
            return;
        }
    }
    ++r->shortened;
}

/**
 * @brief A body block or a hint at row @p y: its lines from the chain's
 *        font @p top down, and a focus's corner brackets 18px clear of the
 *        text, 30 rows above and below, 12px arms 3px wide.
 * @param r   the renderer
 * @param c   the canvas
 * @param b   the block
 * @param y   its middle row
 * @param top the chain's first font
 */
static void draw_block(dk518_render_t *r, gfx_canvas_t *c,
                       const dial_block_t *b, int y, int top)
{
    laid_t laid;
    if (b->text[0] == '\0') {
        return;
    }
    if (b->kind == DIAL_B_NUMBER) {
        draw_number(r, c, b, y);
        return;
    }
    const int most = b->lines < 1 ? 1 : b->lines > 3 ? 3 : b->lines;
    if (!lay(&laid, b->text, y, top, most)) {
        ++r->shortened;
    }
    for (int i = 0; i < laid.n; ++i) {
        put(r, c, laid.line[i].font, laid.line[i].text, laid.line[i].x,
            laid.line[i].y, col(b->color), -1);
    }
    if (b->kind != DIAL_B_FOCUS || laid.n == 0) {
        return;
    }
    const gfx_color_t a = col(UI_C_ACCENT);
    int half = gfx_text_width(laid.line[0].font, laid.line[0].text, 1) / 2
               + 18;
    while (half > 15 && !dk518_box_inside(gfx_rect_make(MID - half, y - 30,
                                                         2 * half, 61),
                                          DK518_R_SHAPE)) {
        --half;
    }
    const int x0 = MID - half;
    const int x1 = MID + half - 1;
    const int y0 = y - 30;
    const int y1 = y + 30;
    gfx_fill_rect(c, x0, y0, 12, 3, a);
    gfx_fill_rect(c, x0, y0, 3, 12, a);
    gfx_fill_rect(c, x1 - 11, y0, 12, 3, a);
    gfx_fill_rect(c, x1 - 2, y0, 3, 12, a);
    gfx_fill_rect(c, x0, y1 - 2, 12, 3, a);
    gfx_fill_rect(c, x0, y1 - 11, 3, 12, a);
    gfx_fill_rect(c, x1 - 11, y1 - 2, 12, 3, a);
    gfx_fill_rect(c, x1 - 2, y1 - 11, 3, 12, a);
}

/**
 * @brief A list's rim tick @p i of @p n: a radial stroke from TICK_R0 to
 *        TICK_R1, 2px wide in GRID, the focused one 3px in the accent,
 *        antialiased by the distance to its spine.
 * @param c     the canvas
 * @param i     the tick
 * @param n     the ticks
 * @param focus the accent's tick
 */
static void tick(gfx_canvas_t *c, int i, int n, bool focus)
{
    const double a = (TICK_FROM + TICK_SWEEP * ((double)i + 0.5) / (double)n)
                     * PI / 180.0;
    const double ax = MIDF + TICK_R0 * cos(a);
    const double ay = MIDF + TICK_R0 * sin(a);
    const double ux = (TICK_R1 - TICK_R0) * cos(a);
    const double uy = (TICK_R1 - TICK_R0) * sin(a);
    const double half = focus ? 1.5 : 1.0;
    const gfx_color_t bg = col(UI_C_BG);
    const gfx_color_t fg = col(focus ? UI_C_ACCENT : UI_C_GRID);
    const double len2 = ux * ux + uy * uy;
    for (int y = (int)floor(fmin(ay, ay + uy) - 2.0);
         y <= (int)ceil(fmax(ay, ay + uy) + 2.0); ++y) {
        for (int x = (int)floor(fmin(ax, ax + ux) - 2.0);
             x <= (int)ceil(fmax(ax, ax + ux) + 2.0); ++x) {
            const double px = x + 0.5 - ax;
            const double py = y + 0.5 - ay;
            double t = (px * ux + py * uy) / len2;
            t = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
            const double cov = half + 0.5 - hypot(px - t * ux, py - t * uy);
            if (cov > 0.0) {
                gfx_pixel(c, x, y, gfx_lerp(bg, fg, (uint8_t)lround(
                                                         fmin(cov, 1.0)
                                                         * 255.0)));
            }
        }
    }
}

/** @brief LINK LOST's rim: 24 DANGER dashes of 8 degrees every 15, from
 *         radius 165 to 174, antialiased across and along.
 *  @param c the canvas */
static void dashes(gfx_canvas_t *c)
{
    const gfx_color_t bg = col(UI_C_BG);
    const gfx_color_t fg = col(UI_C_DANGER);
    const double r0 = 165.0;
    const double r1 = 174.0;
    for (int y = 0; y < DK518_SIZE; ++y) {
        for (int x = 0; x < DK518_SIZE; ++x) {
            const double dx = x + 0.5 - MIDF;
            const double dy = y + 0.5 - MIDF;
            const double d = hypot(dx, dy);
            if (d < r0 - 1.0 || d > r1 + 1.0) {
                continue;
            }
            const double across = fmin(fmax(fmin(d - r0, r1 - d) + 0.5, 0.0),
                                       1.0);
            double a = atan2(dy, dx) * 180.0 / PI;
            a = a < 0.0 ? a + 360.0 : a;
            const double in = fmod(a, 15.0);
            const double along = fmin(fmax(fmin(in - 2.0, 10.0 - in) * PI
                                           / 180.0 * d + 0.5, 0.0), 1.0);
            const long k = lround(across * along * 255.0);
            if (k > 0) {
                gfx_pixel(c, x, y, gfx_lerp(bg, fg, (uint8_t)k));
            }
        }
    }
}

/** @brief A face's rim decoration.  @param c the canvas
 *  @param deco DIAL_DECO_* */
static void decoration(gfx_canvas_t *c, uint8_t deco)
{
    if (deco == DIAL_DECO_DASHED) {
        dashes(c);
    } else if (deco == DIAL_DECO_BIND) {
        gfx_arc(c, MID, MID, DECO_R_OUT, DK518_RING_W, 0.0f, 360.0f,
                col(UI_C_EDGE));
        gfx_arc(c, MID, MID, DECO_R_IN, DK518_RING_W, 0.0f, 360.0f,
                col(UI_C_GRID));
    } else if (deco == DIAL_DECO_WAIT) {
        gfx_arc(c, MID, MID, DECO_R_OUT, DK518_RING_W, 0.0f, 360.0f,
                col(UI_C_GRID));
    }
}

/** @brief The whole face. @param r the renderer @param c the canvas
 *  @param s the scene */
static void draw_full(dk518_render_t *r, gfx_canvas_t *c,
                      const dial_scene_t *s)
{
    gfx_clear(c, col(UI_C_BG));
    decoration(c, s->deco);
    for (int i = 0; i < DIAL_RINGS; ++i) {
        const dial_ring_t *g = &s->ring[i];
        if (!g->used) {
            continue;
        }
        const int rad = dk518_ring_radius(i);
        gfx_arc(c, MID, MID, rad, DK518_RING_W, 0.0f, 360.0f,
                dk518_color(g->color, TRACK_MIX));
        if (g->end > 0u) {
            const float sweep = 360.0f * (float)g->end / (float)GFX_RING_TURN;
            gfx_arc(c, MID, MID, rad, DK518_RING_W, 90.0f, 90.0f - sweep,
                    s->dim ? dk518_color(g->color, DIM_MIX) : col(g->color));
        }
    }
    for (int i = 0; i < s->ticks; ++i) {
        tick(c, i, s->ticks, i == s->tick_cur);
    }
    draw_title(r, c, s);
    draw_pill_row(r, c, s);
    if (s->face == DIAL_FACE_READING) {
        for (int i = 0; i < DIAL_CELLS; ++i) {
            draw_cell(r, c, s, i);
        }
    } else {
        for (int i = 0; i < DIAL_BLOCKS; ++i) {
            const dial_block_t *b = &s->block[i];
            draw_block(r, c, b, row(b->y),
                       b->font >= DIAL_FONT_BIG ? 4 : b->font + 1);
        }
    }
    /* The hints in 10x20, then 8x16, at their own rows. */
    for (int i = 0; i < 2; ++i) {
        draw_block(r, c, &s->hint[i], k_hint_y[i], 1);
    }
}

/**
 * @brief Whether two scenes draw the same pixels: every field a face
 *        shows.
 * @param a one
 * @param b the other
 * @return true when they do
 */
static bool same(const dial_scene_t *a, const dial_scene_t *b)
{
    if (a->face != b->face || a->lang != b->lang || a->deco != b->deco
        || a->ticks != b->ticks || a->tick_cur != b->tick_cur
        || a->has_title != b->has_title || a->title_dim != b->title_dim
        || strcmp(a->title, b->title) != 0 || a->pill != b->pill
        || strcmp(a->pill_text, b->pill_text) != 0 || a->bars != b->bars
        || a->bars_color != b->bars_color || a->icons_dim != b->icons_dim
        || a->batt_known != b->batt_known || a->batt_pct != b->batt_pct
        || a->usb != b->usb || a->dim != b->dim) {
        return false;
    }
    for (int i = 0; i < DIAL_CELLS; ++i) {
        const dial_cell_t *x = &a->cell[i];
        const dial_cell_t *y = &b->cell[i];
        if (x->used != y->used || x->color != y->color
            || strcmp(x->label, y->label) != 0
            || strcmp(x->value, y->value) != 0
            || strcmp(x->unit, y->unit) != 0) {
            return false;
        }
    }
    for (int i = 0; i < DIAL_RINGS; ++i) {
        if (a->ring[i].used != b->ring[i].used
            || a->ring[i].end != b->ring[i].end
            || a->ring[i].color != b->ring[i].color) {
            return false;
        }
    }
    for (int i = 0; i < DIAL_BLOCKS + 2; ++i) {
        const dial_block_t *x = i < DIAL_BLOCKS ? &a->block[i]
                                                : &a->hint[i - DIAL_BLOCKS];
        const dial_block_t *y = i < DIAL_BLOCKS ? &b->block[i]
                                                : &b->hint[i - DIAL_BLOCKS];
        if (x->y != y->y || x->font != y->font || x->color != y->color
            || x->lines != y->lines || x->kind != y->kind
            || strcmp(x->text, y->text) != 0
            || strcmp(x->unit, y->unit) != 0) {
            return false;
        }
    }
    return true;
}

bool dk518_render_init(dk518_render_t *r)
{
    if (r == NULL) {
        return false;
    }
    memset(r, 0, sizeof *r);
    return true;
}

dk518_draw_t dk518_render(dk518_render_t *r, gfx_canvas_t *c,
                          const dk_face_t *f, const dial_face_view_t *v)
{
    if (r == NULL || c == NULL || f == NULL || v == NULL
        || c->width != DK518_SIZE || c->height != DK518_SIZE) {
        return DK518_NONE;
    }
    dial_scene_t s;
    /* The K518's press is a still touch on the glass: its faces say TAP. */
    dial_face_view_t tv = *v;
    tv.touch_press = true;
    dial_scene_build(&s, f, &tv, r->lang);
    r->lang = s.lang;
    if (r->valid && same(&s, &r->shown)) {
        return DK518_SAME;
    }
    draw_full(r, c, &s);
    r->shown = s;
    r->valid = true;
    return DK518_DRAWN;
}
