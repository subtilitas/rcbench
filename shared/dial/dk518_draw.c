/**
 * @file
 * @brief The Guition JC3636K518C's reading faces, MOTOR & ESC and SERVO,
 *        drawn natively at 360 x 360 from the scene shared/dial builds
 *        (dk518_face.h).
 *
 * The scene (dial_scene_build()) says what a face shows: the title, the
 * pill, the link bars and the battery, six cells and six rings, two press
 * hints.  This file says where, at the K518's size: the title tag at row
 * 63, the pill at 99 with the bars to its left and the battery to its
 * right, 9px clear of it, the cells in three rows of two about columns
 * 114 and 246 and rows 150, 198 and 246, the hints at rows 281 and 302,
 * and six rings of 2px from radius 176 inward at a pitch of 4px, each from
 * 12 o'clock clockwise to its reading's share over a full-circle track of
 * its colour mixed 22% onto the background.  Every string's cells lie
 * inside radius 150 (DK518_R_TEXT) of the display's middle, every shape
 * inside 151.
 *
 * A change of the scene draws the whole face again; an unchanged scene
 * draws nothing.  Faces other than the reading faces are not drawn here
 * yet: dk518_render() says so, and the caller shows the boot face.
 *
 * SPDX-License-Identifier: MIT
 */
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

/** @brief A font's middle of the capitals and first row under the
 *         baseline in its cell. */
typedef struct {
    const gfx_font_t *font; /**< the font */
    uint8_t mid;            /**< middle of the capitals */
    uint8_t base;           /**< first row under the baseline */
} metric_t;

/** @brief Every font a K518 face draws with. */
static const metric_t k_metric[] = {
    { &gfx_font_8x16, 8, 13 },   { &gfx_font_10x20, 9, 15 },
    { &gfx_font_12x24, 11, 19 }, { &gfx_font_16x28, 15, 25 },
};

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

/**
 * @brief Draw a line centered on the middle column with its capitals'
 *        middle on row @p y, in the first font of @p chain in which its
 *        cells lie inside @p rad; in the last one cut until they do.
 * @param r     the renderer
 * @param c     the canvas
 * @param chain the fonts, largest first
 * @param n     the fonts in the chain
 * @param text  the text
 * @param y     the middle row
 * @param rad   the radius
 * @param fg    the colour
 */
static void centered(dk518_render_t *r, gfx_canvas_t *c,
                     const gfx_font_t *const *chain, size_t n,
                     const char *text, int y, int rad, gfx_color_t fg)
{
    char t[DIAL_TXT];
    dk_text_copy(t, sizeof t, text);
    for (size_t k = 0; k < n; ++k) {
        const gfx_font_t *f = chain[k];
        const bool last = k + 1u == n;
        for (int cells = gfx_text_cells(t); cells > 0; --cells) {
            t[gfx_text_prefix(text, cells)] = '\0';
            const int w = gfx_text_width(f, t, 1);
            const int top = y - metric(f)->mid;
            if (dk518_box_inside(gfx_rect_make(MID - w / 2, top, w,
                                               f->height), rad)) {
                if (strcmp(t, text) != 0) {
                    ++r->shortened;
                }
                put(r, c, f, t, MID - w / 2, top, fg, -1);
                return;
            }
            if (!last) {
                break;
            }
        }
        dk_text_copy(t, sizeof t, text);
    }
    ++r->shortened;
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

/** @brief The whole reading face. @param r the renderer @param c the
 *         canvas @param s the scene */
static void draw_reading(dk518_render_t *r, gfx_canvas_t *c,
                         const dial_scene_t *s)
{
    static const gfx_font_t *const hint_fonts[] = {
        &gfx_font_10x20, &gfx_font_8x16,
    };
    gfx_clear(c, col(UI_C_BG));
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
    draw_title(r, c, s);
    draw_pill_row(r, c, s);
    for (int i = 0; i < DIAL_CELLS; ++i) {
        draw_cell(r, c, s, i);
    }
    for (int i = 0; i < 2; ++i) {
        if (s->hint[i].text[0] != '\0') {
            centered(r, c, hint_fonts, 2u, s->hint[i].text, k_hint_y[i],
                     DK518_R_TEXT, col(s->hint[i].color));
        }
    }
}

/**
 * @brief Whether two reading scenes draw the same pixels: every field a
 *        reading face shows.
 * @param a one
 * @param b the other
 * @return true when they do
 */
static bool same(const dial_scene_t *a, const dial_scene_t *b)
{
    if (a->face != b->face || a->lang != b->lang
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
    for (int i = 0; i < 2; ++i) {
        if (a->hint[i].color != b->hint[i].color
            || strcmp(a->hint[i].text, b->hint[i].text) != 0) {
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
    dial_scene_build(&s, f, v, r->lang);
    r->lang = s.lang;
    if (s.face != DIAL_FACE_READING) {
        r->valid = false;
        return DK518_NONE;
    }
    if (r->valid && same(&s, &r->shown)) {
        return DK518_SAME;
    }
    draw_reading(r, c, &s);
    r->shown = s;
    r->valid = true;
    return DK518_DRAWN;
}
