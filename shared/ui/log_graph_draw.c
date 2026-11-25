/**
 * @file log_graph_draw.c
 * @brief LOGS' graph of an XY file, drawn: the series' chips with the value
 *        at the cursor, the markers' legend, RUDDR's tabs, the plot with its
 *        levels, lines, traces and cursor, the focused series' scale and
 *        the x labels.  See log_graph_int.h.
 *
 * The plot's background, grid and levels are drawn in bands of PV_BAND
 * rows, as the time plot's; the lines, traces, tags and cursor over them.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "log_preset.h"
#include "ui_text.h"
#include "ui_widgets.h"

#include "log_graph_int.h"

/** @brief A marker kind's letter, word and colour. */
typedef struct {
    ui_text_id_t letter;  /**< its tag's letter          */
    ui_text_id_t word;    /**< its word in the legend    */
    ui_color_id_t color;  /**< its colour                */
} mark_look_t;

/** @brief Each marker kind's look, in log_mark_kind_t order. */
static const mark_look_t k_look[LOG_MARK_KINDS] = {
    { TX_LOG_MK_CENTER, TX_LOG_MARK_CENTER, UI_C_TEXT_DIM },
    { TX_LOG_MK_BOUND, TX_LOG_MARK_BOUND, UI_C_DANGER },
    { TX_LOG_MK_END, TX_LOG_MARK_END, UI_C_OK },
    { TX_LOG_MK_LAST, TX_LOG_MARK_LAST, UI_C_TEXT_FAINT },
    { TX_LOG_MK_FREE, TX_LOG_MARK_FREE, UI_C_TEXT_DIM },
    { TX_LOG_MK_THRESHOLD, TX_LOG_MARK_THRESHOLD, UI_C_WARN },
};

/** @brief The kinds each preset's legend names, ended by LOG_MARK_KINDS. */
static const log_mark_kind_t k_limit_kinds[] = {
    LOG_MARK_CENTER, LOG_MARK_BOUND, LOG_MARK_END, LOG_MARK_LAST,
    LOG_MARK_FREE, LOG_MARK_THRESHOLD, LOG_MARK_KINDS,
};
static const log_mark_kind_t k_search_kinds[] = {
    LOG_MARK_CENTER, LOG_MARK_END, LOG_MARK_LAST, LOG_MARK_KINDS,
};  /**< RUDDR's SEARCH. */

/**
 * @brief The decimals a value of a scale from @p lo to @p hi needs.
 * @param lo  The scale's foot.
 * @param hi  Its top.
 * @return 0 to 3: 3 under a width of 1, none from 100 on.
 */
static int decimals_of(float lo, float hi)
{
    const float w = fabsf(hi - lo);
    return (w < 1.0f) ? 3 : (w < 10.0f) ? 2 : (w < 100.0f) ? 1 : 0;
}

/**
 * @brief A value and its unit, in the decimals of series @p k's scale.
 * @param out  Takes the text.
 * @param n    Bytes at @p out.
 * @param k    The series, 1 to n_fields - 1.
 * @param v    The value.
 */
static void value_text(char *out, size_t n, int k, float v)
{
    char num[16];
    ui_fmt(num, sizeof(num), v, decimals_of(lg.lo[k], lg.hi[k]));
    snprintf(out, n, "%s%s", num, s.data.field[k].unit);
}

/**
 * @brief Series @p k's colour, faint while hidden.
 * @param k  The series.
 * @return the colour.
 */
static gfx_color_t series_col(int k)
{
    return lg.hidden[k] ? UI_TEXT_FAINT : ui_theme_color(lv_series_color[k]);
}

/**
 * @brief A marker's tag: its letter on a box of its colour.
 * @param c     The canvas.
 * @param x     The box's left.
 * @param y     Its top.
 * @param kind  The marker's kind.
 */
static void tag(gfx_canvas_t *c, int x, int y, log_mark_kind_t kind)
{
    gfx_fill_rect(c, x, y, LG_TAG_W, LG_TAG_H,
                  ui_theme_color(k_look[kind].color));
    gfx_text_in(c, gfx_rect_make(x, y + 1, LG_TAG_W, LG_TAG_H - 2),
                ui_tr(k_look[kind].letter), LV_FONT_FINE, UI_BG, 1,
                GFX_ALIGN_CENTER);
}

/** @brief The series' chips: name, scale, the value at the cursor; the
 *         focused chip framed in its colour, a hidden one faint. */
static void draw_chips(gfx_canvas_t *c)
{
    const int ci = (s.cursor >= 0 && s.cursor < s.data.count) ? s.cursor : 0;
    char line[48];
    for (int k = 1; k < s.data.n_fields; ++k) {
        const gfx_rect_t chip = LG_SLOT(k - 1);
        const gfx_color_t col = series_col(k);
        gfx_fill_chamfer_rect(c, chip.x, chip.y, chip.w, chip.h,
                              UI_CHAMFER_SM, UI_PANEL);
        gfx_fill_rect(c, chip.x, chip.y, 5, chip.h, col);
        if (lg.focus == k) {
            gfx_draw_rect(c, chip.x, chip.y, chip.w, chip.h, col);
        }
        gfx_text_in(c, gfx_rect_make(chip.x + 12, chip.y + 2, chip.w - 20, 20),
                    s.data.field[k].name, LV_FONT_LABEL,
                    lg.hidden[k] ? UI_TEXT_FAINT : UI_TEXT, 1, GFX_ALIGN_LEFT);
        value_text(line, sizeof(line), k, s.data.value[k][ci]);
        gfx_text_in(c, gfx_rect_make(chip.x + 12, chip.y + 22, chip.w - 20, 20),
                    line, LV_FONT_LABEL, col, 1, GFX_ALIGN_RIGHT);
        log_viewer_range_text(line, sizeof(line), lg.lo[k], lg.hi[k], "");
        gfx_text(c, chip.x + 12, chip.y + 24, line, LV_FONT_FINE,
                 UI_TEXT_FAINT, 1);
    }
}

/**
 * @brief One entry of the legend at (*x, *y): a mark, then its word; to
 *        the next line when the entry does not fit the box.
 * @param c     The canvas.
 * @param box   The legend's box.
 * @param x     The entry's left; moved past it.
 * @param y     Its line's top; moved to the next line on a wrap.
 * @param word  The word.
 * @param draw  Draws the entry's mark at its left, LG_TAG_W wide.
 * @param kind  The marker kind the mark shows.
 */
static void legend_entry(gfx_canvas_t *c, gfx_rect_t box, int *x, int *y,
                         const char *word,
                         void (*draw)(gfx_canvas_t *, int, int, int),
                         int kind)
{
    const int w = LG_TAG_W + 4 + gfx_text_width(LV_FONT_FINE, word, 1);
    if (*x > box.x + 8 && *x + w > box.x + box.w - 8) {
        *x = box.x + 8;
        *y += LG_TAG_H + 2;
    }
    draw(c, *x, *y, kind);
    gfx_text(c, *x + LG_TAG_W + 4, *y + 1, word, LV_FONT_FINE, UI_TEXT_DIM, 1);
    *x += w + 12;
}

/** @brief A legend mark: a marker's tag. */
static void mark_tag(gfx_canvas_t *c, int x, int y, int kind)
{
    tag(c, x, y, (log_mark_kind_t)kind);
}

/** @brief A legend mark: a point outside the tolerance. */
static void mark_warn(gfx_canvas_t *c, int x, int y, int kind)
{
    (void)kind;
    gfx_fill_rect(c, x + 3, y + 5, LG_DOT + 2, LG_DOT + 2, UI_WARN);
}

/** @brief A legend mark: a write's tick. */
static void mark_tick(gfx_canvas_t *c, int x, int y, int kind)
{
    (void)kind;
    gfx_fill_rect(c, x + 6, y + 2, 2, LG_TAG_H - 4, UI_TEXT_DIM);
}

/** @brief The markers' legend in the chip slots the series leave free, up
 *         to the tabs. */
static void draw_legend(gfx_canvas_t *c)
{
    const int first = s.data.n_fields - 1;
    const int last = lg.tabbed ? 2 : 3;
    if (first > last) {
        return;
    }
    const gfx_rect_t a = LG_SLOT(first);
    const gfx_rect_t b = LG_SLOT(last);
    const gfx_rect_t box = gfx_rect_make(a.x, a.y, b.x + b.w - a.x, a.h);
    gfx_fill_chamfer_rect(c, box.x, box.y, box.w, box.h, UI_CHAMFER_SM,
                          UI_PANEL);
    int x = box.x + 8;
    int y = box.y + 3;
    if (s.preset == LOG_PRESET_RUDDR_STEPS) {
        legend_entry(c, box, &x, &y, TR(LOG_LEG_OUTSIDE), mark_warn, 0);
        legend_entry(c, box, &x, &y, TR(LOG_LEG_WRITE), mark_tick, 0);
        return;
    }
    const log_mark_kind_t *kinds = (s.preset == LOG_PRESET_LIMIT)
                                       ? k_limit_kinds : k_search_kinds;
    for (int i = 0; kinds[i] != LOG_MARK_KINDS; ++i) {
        legend_entry(c, box, &x, &y, ui_tr(k_look[kinds[i]].word), mark_tag,
                     (int)kinds[i]);
    }
}

/**
 * @brief The levels that cross a band of the plot, dashed.
 * @param band     The band's canvas.
 * @param band_y0  Its first row.
 * @param rows     Its rows.
 */
static void levels_band(gfx_canvas_t *band, int band_y0, int rows)
{
    for (int m = 0; m < lg.mk.n_marks; ++m) {
        const log_mark_t *mk = &lg.mk.mark[m];
        const int k = mk->level ? lg_series_of(mk->column) : -1;
        if (k < 0 || lg.hidden[k]) {
            continue;
        }
        const int y = lg_y_px(k, mk->at);
        if (y < band_y0 || y >= band_y0 + rows) {
            continue;
        }
        const int xa = lg_x_px(mk->x0) - PV_X;
        const int xb = lg_x_px(mk->x1) - PV_X;
        const int x0 = (xa < xb) ? xa : xb;
        const int x1 = (xa < xb) ? xb : xa;
        const gfx_color_t col = ui_theme_color(k_look[mk->kind].color);
        for (int x = x0; x <= x1; x += LG_DASH + LG_GAP) {
            const int w = (x + LG_DASH > x1 + 1) ? x1 + 1 - x : LG_DASH;
            gfx_hline(band, x, y - band_y0, w, col);
        }
    }
}

/**
 * @brief The row of a vertical line's tag at column @p tx: the first of
 *        the two rows whose tags placed so far leave it room, else the
 *        first.
 * @param placed  The left edges of the tags placed, per row.
 * @param n       How many, per row.
 * @param tx      The tag's left edge.
 * @return 0 or 1; the tag is counted in it.
 */
static int tag_row(int16_t placed[2][LOG_MARKS_MAX], int n[2], int tx)
{
    int row = 0;
    for (int r = 1; r >= 0; --r) {
        bool free_here = true;
        for (int i = 0; i < n[r] && free_here; ++i) {
            const int d = placed[r][i] - tx;
            free_here = d >= LG_TAG_W + 2 || d <= -(LG_TAG_W + 2);
        }
        row = free_here ? r : row;
    }
    placed[row][n[row]++] = (int16_t)tx;
    return row;
}

/**
 * @brief The vertical lines, dashed but the center's; or, drawn over the
 *        traces and the cursor, the tags: a line's in two rows at the
 *        plot's top, a level's at its end away from the center.
 * @param c     The canvas.
 * @param tags  Draw the tags, else the lines.
 */
static void draw_marks(gfx_canvas_t *c, bool tags)
{
    static int16_t placed[2][LOG_MARKS_MAX];
    int n_placed[2] = { 0, 0 };
    for (int m = 0; m < lg.mk.n_marks; ++m) {
        const log_mark_t *mk = &lg.mk.mark[m];
        const gfx_color_t col = ui_theme_color(k_look[mk->kind].color);
        if (mk->level) {
            const int k = lg_series_of(mk->column);
            if (k < 0 || lg.hidden[k] || !tags) {
                continue;
            }
            const int end = lg_x_px(mk->x1);
            int tx = (mk->x1 < mk->x0) ? end : end - LG_TAG_W;
            int ty = lg_y_px(k, mk->at) - LG_TAG_H - 1;
            tx = (tx < PV_X) ? PV_X
                 : (tx > PV_X + PV_W - LG_TAG_W) ? PV_X + PV_W - LG_TAG_W : tx;
            ty = (ty < PV_Y) ? PV_Y : ty;
            tag(c, tx, ty, mk->kind);
            continue;
        }
        const int x = lg_x_px(mk->at);
        if (tags) {
            int tx = x - LG_TAG_W / 2;
            tx = (tx < PV_X) ? PV_X
                 : (tx > PV_X + PV_W - LG_TAG_W) ? PV_X + PV_W - LG_TAG_W : tx;
            tag(c, tx,
                PV_Y + 2 + tag_row(placed, n_placed, tx) * (LG_TAG_H + 2),
                mk->kind);
        } else if (mk->kind == LOG_MARK_CENTER) {
            gfx_vline(c, x, PV_Y, PV_H, col);
        } else {
            for (int y = PV_Y; y < PV_Y + PV_H; y += LG_DASH + LG_GAP) {
                const int h = (y + LG_DASH > PV_Y + PV_H) ? PV_Y + PV_H - y
                                                          : LG_DASH;
                gfx_vline(c, x, y, h, col);
            }
        }
    }
}

/**
 * @brief One step of a trace from row @p a to row @p b, dashed where the
 *        trace is: the dash runs on across the steps.
 * @param c     The canvas.
 * @param k     The series.
 * @param a     The row it comes from.
 * @param b     The row it goes to.
 * @param col   Its colour.
 * @param dash  The trace's dash phase, px; NULL draws it solid.
 */
static void join(gfx_canvas_t *c, int k, int a, int b, gfx_color_t col,
                 int *dash)
{
    int x0 = lg.px[a];
    int y0 = lg.py[k][a];
    const int x1 = lg.px[b];
    const int y1 = lg.py[k][b];
    const int thick = (lg.focus == k) ? 2 : 1;
    if (dash == NULL) {
        for (int t = 0; t < thick; ++t) {
            gfx_line(c, x0, y0 + t, x1, y1 + t, col);
        }
        return;
    }
    const int dx = (x1 > x0) ? x1 - x0 : x0 - x1;
    const int dy = (y1 > y0) ? y0 - y1 : y1 - y0;
    const int sx = (x0 < x1) ? 1 : -1;
    const int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (*dash % (LG_DASH + LG_GAP) < LG_DASH) {
            gfx_fill_rect(c, x0, y0, 1, thick, col);
        }
        ++*dash;
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/**
 * @brief Series @p k's traces: each trace's rows joined in the file's
 *        order, a trace of points as points; on STEPS a point at every row,
 *        in the warning colour where it is outside the tolerance.
 * @param c  The canvas.
 * @param k  The series.
 */
static void draw_series(gfx_canvas_t *c, int k)
{
    const gfx_color_t col = series_col(k);
    const bool dots = s.preset == LOG_PRESET_RUDDR_STEPS;
    int prev[LOG_TRACES_MAX];
    int dash[LOG_TRACES_MAX];
    for (int t = 0; t < LOG_TRACES_MAX; ++t) {
        prev[t] = -1;
        dash[t] = 0;
    }
    for (int i = 0; i < lg.mk.rows; ++i) {
        const int t = lg.mk.row[i] & LOG_ROW_TRACE;
        if (t == (int)LOG_TRACE_NONE || lg.py[k][i] == PV_EMPTY) {
            continue;
        }
        const log_trace_t *tr = &lg.mk.trace[t];
        if (!tr->points && prev[t] >= 0) {
            join(c, k, prev[t], i, col, tr->dashed ? &dash[t] : NULL);
        }
        prev[t] = i;
    }
    /* The points over the lines; the measure outside the tolerance in the
     * warning colour, the goal it missed in its own. */
    const bool measured = strcmp(s.data.field[k].name, "measured") == 0;
    for (int i = 0; i < lg.mk.rows; ++i) {
        const int t = lg.mk.row[i] & LOG_ROW_TRACE;
        if (t == (int)LOG_TRACE_NONE || lg.py[k][i] == PV_EMPTY
            || !(dots || lg.mk.trace[t].points)) {
            continue;
        }
        const bool warn = measured && (lg.mk.row[i] & LOG_ROW_WARN) != 0u;
        gfx_fill_rect(c, lg.px[i] - LG_DOT / 2, lg.py[k][i] - LG_DOT / 2,
                      LG_DOT, LG_DOT, warn ? UI_WARN : col);
    }
}

/** @brief The writes' ticks at the plot's foot. */
static void draw_ticks(gfx_canvas_t *c)
{
    for (int i = 0; i < lg.mk.rows; ++i) {
        if ((lg.mk.row[i] & LOG_ROW_TICK) != 0u && lg_drawn(i)) {
            gfx_fill_rect(c, lg.px[i], PV_Y + PV_H - LG_TICK_H, 2, LG_TICK_H,
                          UI_TEXT_DIM);
        }
    }
}

/** @brief The cursor: a line at its row's column and a point on each
 *         series shown. */
static void draw_cursor(gfx_canvas_t *c)
{
    if (!lg_drawn(s.cursor)) {
        return;
    }
    const int x = lg.px[s.cursor];
    gfx_vline(c, x, PV_Y, PV_H, UI_TEXT);
    for (int k = 1; k < s.data.n_fields; ++k) {
        if (!lg.hidden[k] && lg.py[k][s.cursor] != PV_EMPTY) {
            gfx_fill_circle(c, x, lg.py[k][s.cursor], 4, series_col(k));
        }
    }
}

/**
 * @brief A figure at the plot's left edge on a box of the plot's
 *        background, so a trace under it does not cut it.
 * @param c    The canvas.
 * @param y    Its top.
 * @param txt  The figure.
 * @param col  Its colour.
 */
static void edge_label(gfx_canvas_t *c, int y, const char *txt,
                       gfx_color_t col)
{
    const int w = gfx_text_width(LV_FONT_FINE, txt, 1);
    gfx_fill_rect(c, PV_X, y - 1, w + 6, 18, UI_PANEL_SUNK);
    gfx_text(c, PV_X + 3, y, txt, LV_FONT_FINE, col, 1);
}

/** @brief The focused series' scale: its top and its foot at the plot's
 *         left edge, in its colour. */
static void draw_focus_scale(gfx_canvas_t *c)
{
    const int k = lg.focus;
    if (k < 1 || k >= s.data.n_fields || lg.hidden[k]) {
        return;
    }
    char txt[32];
    value_text(txt, sizeof(txt), k, lg.hi[k]);
    edge_label(c, PV_Y + 2 * (LG_TAG_H + 2) + 4, txt, series_col(k));
    value_text(txt, sizeof(txt), k, lg.lo[k]);
    edge_label(c, PV_Y + PV_H - LG_TICK_H - 20, txt, series_col(k));
}

/** @brief The x labels under the plot, in the x column's unit, in the
 *         decimals the step between two labels needs. */
static void draw_x_labels(gfx_canvas_t *c)
{
    char t[16 + LOG_UNIT_MAX];
    char num[16];
    const float step = (lg.xhi - lg.xlo) / PV_DIV_X;
    const int d = (step >= 1.0f) ? 0 : (step >= 0.1f) ? 1
                  : (step >= 0.01f) ? 2 : 3;
    for (int i = 0; i <= PV_DIV_X; ++i) {
        const float v = lg.xlo + (lg.xhi - lg.xlo) * (float)i / PV_DIV_X;
        ui_fmt(num, sizeof(num), v, d);
        snprintf(t, sizeof(t), "%s%s", num, s.data.field[0].unit);
        const int x = PV_X + (PV_W - 1) * i / PV_DIV_X;
        const int w = gfx_text_width(LV_FONT_FINE, t, 1);
        int tx = x - w / 2;
        tx = (tx < PV_X) ? PV_X : (tx + w > PV_X + PV_W) ? PV_X + PV_W - w : tx;
        gfx_text(c, tx, PV_PANEL.y + PV_PANEL.h + 4, t, LV_FONT_FINE,
                 UI_TEXT_FAINT, 1);
    }
}

void lg_render(gfx_canvas_t *c)
{
    draw_chips(c);
    draw_legend(c);
    if (lg.tabbed) {
        ui_tabs_render(&lg.tabs, c);
    }
    ui_panel(c, PV_PANEL, NULL, UI_ACCENT);
    gfx_canvas_t band;
    for (int by = 0; by < PV_H; by += PV_BAND) {
        const int rows = (PV_H - by > PV_BAND) ? PV_BAND : PV_H - by;
        gfx_canvas_init(&band, lv_plot.band, PV_W, rows, PV_W);
        gfx_clear(&band, UI_PANEL_SUNK);
        lv_grid_band(&band, PV_Y + by, rows);
        levels_band(&band, PV_Y + by, rows);
        gfx_blit(c, PV_X, PV_Y + by, lv_plot.band, PV_W, rows, PV_W);
    }
    draw_marks(c, false);
    draw_ticks(c);
    /* The first series last, on top. */
    for (int k = s.data.n_fields - 1; k >= 1; --k) {
        if (!lg.hidden[k]) {
            draw_series(c, k);
        }
    }
    draw_cursor(c);
    draw_marks(c, true);
    draw_focus_scale(c);
    draw_x_labels(c);
}
