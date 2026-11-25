/**
 * @file
 * @brief The traces and markers of an XY file, read in one pass over its
 *        rows.  See log_marks.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "log_marks.h"

#include <stdio.h>
#include <string.h>

#include "log_fields.h"
#include "log_numbers.h"

/** @brief A side's figures as the LIMIT rows give them. */
typedef struct {
    bool  any;       /**< a row of the side was placed                 */
    float ext;       /**< its outermost pulse                          */
    bool  bound;     /**< a BOUND row was read                         */
    float bpulse;    /**< the last BOUND row's pulse                   */
    bool  have_end;  /**< that row holds an end point                  */
    float end;       /**< the end point                                */
    bool  level;     /**< a row with a threshold was read              */
    float free_a;    /**< the free current of the last such row        */
    float thr_a;     /**< its threshold                                */
} side_t;

/** @brief What one strip column of a preset shows. */
typedef struct {
    log_preset_id_t id;      /**< the preset                     */
    log_strip_kind_t kind;   /**< the strip                      */
    const char *column;      /**< its column's name              */
} strip_rule_t;

/** @brief The strips of each time plot, in the order they stand. */
static const strip_rule_t k_strip_rules[] = {
    { LOG_PRESET_ATEST, LOG_STRIP_PHASE, "phase" },
    { LOG_PRESET_ATEST, LOG_STRIP_CC, "mode" },
    { LOG_PRESET_SERVO, LOG_STRIP_MODEL, "source" },
    { LOG_PRESET_MOTOR, LOG_STRIP_MODEL, "source" },
    { LOG_PRESET_SUPPL, LOG_STRIP_CC, "mode" },
    { LOG_PRESET_SUPPL, LOG_STRIP_MODEL, "source" },
};

/** @brief A RUDDR row's kind. */
typedef enum { RK_OTHER = 0, RK_STEP, RK_VERIFY, RK_SEARCH } row_kind_t;

/** @brief The walk's state: the columns, and what the rows read so far
 *         add up to. */
typedef struct {
    log_markset_t *mk;              /**< the result                        */
    const log_analysis_t *an;     /**< the file's analysis               */
    log_preset_id_t id;           /**< the preset                        */
    int c_x;                      /**< the x column                      */
    int c_search;                 /**< LIMIT: `search`                   */
    int c_side;                   /**< `side`: LIMIT's, RUDDR's `side (us)` */
    int c_state;                  /**< LIMIT: `state`                    */
    int c_free;                   /**< LIMIT: `free`                     */
    int c_thr;                    /**< LIMIT: `threshold`                */
    int c_end;                    /**< LIMIT: `end point`                */
    int c_cur;                    /**< `current`, the levels' column     */
    int c_kind;                   /**< RUDDR: `kind`                     */
    int c_write;                  /**< RUDDR: `write`                    */
    int c_within;                 /**< RUDDR: `within`                   */
    bool  have_center;            /**< the center is read                */
    float center;                 /**< the first placed row's x          */
    side_t side[2];               /**< LIMIT: LOW and HIGH               */
    /* RUDDR SEARCH: the trace being read and the way it runs. */
    int   cur;                    /**< the trace, -1 before the first     */
    float prev_x;                 /**< the last SEARCH row's pulse       */
    int   dir;                    /**< -1 falling, 1 rising, 0 not yet   */
    float last_x[LOG_TRACES_MAX]; /**< each trace's last pulse           */
    /* RUDDR: the `side (us)` values the STEP rows hold. */
    float key[LOG_TRACES_MAX];    /**< STEPS: each trace's side (us)     */
    float ends[2];                /**< SEARCH: the first two side values */
    int   n_ends;                 /**< how many                          */
    int   verify;                 /**< STEPS: the verifies' trace, -1    */
    /* A time plot: the step, the spans being read, the strips' columns. */
    int   c_step;                 /**< ATEST: `step`                     */
    int   c_clip;                 /**< `clipped`                         */
    bool  have_step;              /**< a step was read                   */
    float step;                   /**< the last step read                */
    int   span[2];                /**< OVER's and CLIP's open mark, -1   */
    bool  time;                   /**< the preset is a time plot         */
    bool  strip_set[LOG_STRIPS];  /**< a row set the strip               */
} walk_t;

bool log_marks_add(log_markset_t *mk, const log_mark_t *m)
{
    if (mk == NULL || m == NULL) {
        return false;
    }
    if (mk->n_marks >= LOG_MARKS_MAX) {
        ++mk->dropped;
        return false;
    }
    mk->mark[mk->n_marks++] = *m;
    return true;
}

/**
 * @brief Add a vertical line.
 * @param mk    The markers.
 * @param kind  What it shows.
 * @param x     Where, on x.
 */
static void add_line(log_markset_t *mk, log_mark_kind_t kind, float x)
{
    const log_mark_t m = { kind, false, -1, x, x, x, -1, -1 };
    (void)log_marks_add(mk, &m);
}

/**
 * @brief Add a level of column @p column from @p x0 to @p x1.
 * @param mk      The markers.
 * @param kind    What it shows.
 * @param column  The column it is a level of.
 * @param v       The level.
 * @param x0      Where it starts on x: the center.
 * @param x1      Where it ends: the side's outermost pulse.
 */
static void add_level(log_markset_t *mk, log_mark_kind_t kind, int column,
                      float v, float x0, float x1)
{
    const log_mark_t m = { kind, true, column, v, x0, x1, -1, -1 };
    (void)log_marks_add(mk, &m);
}

/**
 * @brief Add a trace.
 * @param mk  The markers.
 * @param t   The trace.
 * @return its number, LOG_TRACE_NONE past LOG_TRACES_MAX.
 */
static int add_trace(log_markset_t *mk, log_trace_t t)
{
    if (mk->n_traces >= LOG_TRACES_MAX) {
        return (int)LOG_TRACE_NONE;
    }
    mk->trace[mk->n_traces] = t;
    return mk->n_traces++;
}

/**
 * @brief Cell @p c of a row.
 * @param cells  The row's cells.
 * @param n      How many.
 * @param c      The column, -1 for none.
 * @return the cell, "" for a column the row or the file lacks.
 */
static const char *cell(const char *const *cells, int n, int c)
{
    return (c >= 0 && c < n) ? cells[c] : "";
}

/**
 * @brief Read a cell as a number in the file's convention.
 * @param w  The walk.
 * @param s  The cell.
 * @param v  Takes the number.
 * @return false for an empty cell or one that is no number.
 */
static bool num(const walk_t *w, const char *s, float *v)
{
    double d = 0.0;
    if (s[0] == '\0' || !log_parse_with(s, w->an->convention, &d, NULL, 0)) {
        return false;
    }
    *v = (float)d;
    return true;
}

/**
 * @brief A row of a LIMIT file: its trace, and its side's figures.
 * @param w      The walk.
 * @param x      Its pulse.
 * @param cells  Its cells.
 * @param n      How many.
 * @return its trace.
 */
static int limit_row(walk_t *w, float x, const char *const *cells, int n)
{
    float v = 1.0f;
    uint8_t search = 1u;
    if (num(w, cell(cells, n, w->c_search), &v) && v >= 1.0f && v <= 3.0f) {
        search = (uint8_t)v;
    }
    const char *sd = cell(cells, n, w->c_side);
    const log_side_t side = strcmp(sd, "HIGH") == 0  ? LOG_SIDE_HIGH
                            : strcmp(sd, "LOW") == 0 ? LOG_SIDE_LOW
                                                     : LOG_SIDE_NONE;
    int t = -1;
    for (int k = 0; k < w->mk->n_traces; ++k) {
        if (w->mk->trace[k].search == search && w->mk->trace[k].side == side) {
            t = k;
        }
    }
    if (t < 0) {
        const log_trace_t nt = { search, side, search != 1u, false };
        t = add_trace(w->mk, nt);
    }
    if (side == LOG_SIDE_NONE) {
        return t;
    }
    side_t *s = &w->side[side == LOG_SIDE_HIGH ? 1 : 0];
    if (!s->any || (side == LOG_SIDE_HIGH ? x > s->ext : x < s->ext)) {
        s->ext = x;
    }
    s->any = true;
    if (strcmp(cell(cells, n, w->c_state), "BOUND") == 0) {
        s->bound = true;
        s->bpulse = x;
        s->have_end = num(w, cell(cells, n, w->c_end), &s->end);
    }
    float fa = 0.0f;
    float th = 0.0f;
    if (num(w, cell(cells, n, w->c_thr), &th)
        && num(w, cell(cells, n, w->c_free), &fa)) {
        s->level = true;
        s->free_a = fa;
        s->thr_a = th;
    }
    return t;
}

/**
 * @brief A RUDDR row's kind: by `kind`, else by `write`.
 * @param w      The walk.
 * @param cells  Its cells.
 * @param n      How many.
 * @return the kind.
 */
static row_kind_t ruddr_kind(const walk_t *w, const char *const *cells,
                             int n)
{
    const char *k = cell(cells, n, w->c_kind);
    if (k[0] != '\0') {
        return strcmp(k, "SEARCH") == 0   ? RK_SEARCH
               : strcmp(k, "VERIFY") == 0 ? RK_VERIFY
               : strcmp(k, "STEP") == 0   ? RK_STEP
                                          : RK_OTHER;
    }
    const char *wr = cell(cells, n, w->c_write);
    float v = 0.0f;
    return strcmp(wr, "S") == 0   ? RK_SEARCH
           : strcmp(wr, "V") == 0 ? RK_VERIFY
           : num(w, wr, &v)       ? RK_STEP
                                  : RK_OTHER;
}

/**
 * @brief Note a STEP row's `side (us)` among the first two values met.
 * @param w      The walk.
 * @param cells  Its cells.
 * @param n      How many.
 */
static void note_end(walk_t *w, const char *const *cells, int n)
{
    float v = 0.0f;
    if (!num(w, cell(cells, n, w->c_side), &v)) {
        return;
    }
    for (int k = 0; k < w->n_ends; ++k) {
        if (w->ends[k] == v) {
            return;
        }
    }
    if (w->n_ends < 2) {
        w->ends[w->n_ends++] = v;
    }
}

/**
 * @brief A SEARCH row of RUDDR's SEARCH picture: a new trace where the
 *        pulse turns back.
 * @param w  The walk.
 * @param x  Its pulse.
 * @return its trace.
 */
static int search_row(walk_t *w, float x)
{
    if (!w->have_center) {
        w->have_center = true;
        w->center = x;
    }
    const int d = (x > w->prev_x) ? 1 : (x < w->prev_x) ? -1 : 0;
    if (w->cur < 0 || (d != 0 && w->dir != 0 && d != w->dir)) {
        const log_trace_t nt = { 0u, LOG_SIDE_NONE, false, false };
        w->cur = add_trace(w->mk, nt);
        w->dir = 0;
        if (w->cur == (int)LOG_TRACE_NONE) {
            w->cur = -1;
            return (int)LOG_TRACE_NONE;
        }
    } else if (w->dir == 0 && d != 0) {
        w->dir = d;
        w->mk->trace[w->cur].side = d < 0 ? LOG_SIDE_LOW : LOG_SIDE_HIGH;
    }
    w->prev_x = x;
    w->last_x[w->cur] = x;
    return w->cur;
}

/**
 * @brief A row of RUDDR's STEPS picture: STEP rows a trace for each
 *        `side (us)`, VERIFY rows one trace of points.
 * @param w      The walk.
 * @param rk     Its kind.
 * @param cells  Its cells.
 * @param n      How many.
 * @return its trace and flags.
 */
static int steps_row(walk_t *w, row_kind_t rk, const char *const *cells,
                     int n)
{
    int t = (int)LOG_TRACE_NONE;
    if (rk == RK_VERIFY) {
        if (w->verify < 0) {
            const log_trace_t nt = { 0u, LOG_SIDE_NONE, false, true };
            w->verify = add_trace(w->mk, nt);
        }
        t = w->verify;
    } else {
        float key = 0.0f;
        (void)num(w, cell(cells, n, w->c_side), &key);
        for (int k = 0; k < w->mk->n_traces; ++k) {
            if (!w->mk->trace[k].points && w->key[k] == key) {
                t = k;
            }
        }
        if (t == (int)LOG_TRACE_NONE) {
            const log_trace_t nt = { 0u, LOG_SIDE_NONE, false, false };
            t = add_trace(w->mk, nt);
            if (t != (int)LOG_TRACE_NONE) {
                w->key[t] = key;
            }
        }
        float wr = 0.0f;
        if (num(w, cell(cells, n, w->c_write), &wr) && wr > 0.0f) {
            t |= (int)LOG_ROW_TICK;
        }
    }
    float within = 1.0f;
    if (num(w, cell(cells, n, w->c_within), &within) && within == 0.0f) {
        t |= (int)LOG_ROW_WARN;
    }
    return t;
}

/**
 * @brief Open or widen the span of kind @p kind over row @p row: a row next
 *        to the open span's last widens it, any other opens a new one.
 * @param w     The walk.
 * @param slot  0 OVER, 1 CLIP.
 * @param kind  LOG_MARK_OVER or LOG_MARK_CLIP.
 * @param row   The row.
 */
static void span_row(walk_t *w, int slot, log_mark_kind_t kind, int row)
{
    log_markset_t *mk = w->mk;
    const int open = w->span[slot];
    if (open >= 0 && mk->mark[open].row_end == row - 1) {
        mk->mark[open].row_end = row;
        return;
    }
    const log_mark_t m = { kind, false, -1, 0.0f, 0.0f, 0.0f, row, row };
    w->span[slot] = log_marks_add(mk, &m) ? mk->n_marks - 1 : -1;
}

/**
 * @brief A strip's code for a cell.
 * @param st    The strip; PHASE learns a name it has not met.
 * @param cell  The cell.
 * @return the code: 0 for no value, a PHASE name's number, 1 for a CC or a
 *         modelled row.
 */
static unsigned strip_code(log_strip_t *st, const char *cell)
{
    if (st->kind == LOG_STRIP_CC) {
        return strcmp(cell, "CC") == 0 ? 1u : LOG_STRIP_NONE;
    }
    if (st->kind == LOG_STRIP_MODEL) {
        return strcmp(cell, "model") == 0 ? 1u : LOG_STRIP_NONE;
    }
    if (cell[0] == '\0') {
        return LOG_STRIP_NONE;
    }
    for (int k = 0; k < st->n_values; ++k) {
        if (strncmp(st->value[k], cell, LOG_STRIP_NAME - 1) == 0) {
            return (unsigned)k + 1u;
        }
    }
    if (st->n_values >= LOG_STRIP_VALUES) {
        return LOG_STRIP_OTHER;
    }
    snprintf(st->value[st->n_values], LOG_STRIP_NAME, "%s", cell);
    return (unsigned)++st->n_values;
}

/**
 * @brief A row of a time plot: its step line, its OVER and CLIP spans and
 *        its strips' codes.
 * @param w      The walk.
 * @param row    The row.
 * @param cells  Its cells.
 * @param n      How many.
 * @return true: the walk goes on; a row past the caller's cells sets cut
 *         and has no strip codes.
 */
static bool time_row(walk_t *w, int row, const char *const *cells, int n)
{
    log_markset_t *mk = w->mk;
    float v = 0.0f;
    if (num(w, cell(cells, n, w->c_step), &v)
        && (!w->have_step || v != w->step)) {
        w->have_step = true;
        w->step = v;
        const log_mark_t m = { LOG_MARK_STEP, false, -1, v, v, v, row, row };
        (void)log_marks_add(mk, &m);
    }
    bool over = false;
    for (int c = 0; c < n && !over; ++c) {
        over = cells[c][0] == '>';
    }
    if (over) {
        span_row(w, 0, LOG_MARK_OVER, row);
    }
    if (num(w, cell(cells, n, w->c_clip), &v) && v > 0.0f) {
        span_row(w, 1, LOG_MARK_CLIP, row);
    }
    if (mk->n_strips == 0) {
        return true;
    }
    if (row >= mk->cells_cap) {
        mk->cut = true;
        return true;
    }
    unsigned byte = 0u;
    for (int k = 0; k < mk->n_strips; ++k) {
        const unsigned code = strip_code(&mk->strip[k],
                                         cell(cells, n, mk->strip[k].column));
        w->strip_set[k] = w->strip_set[k] || code != LOG_STRIP_NONE;
        byte |= code << (4 * k);
    }
    mk->cells[row] = (uint8_t)byte;
    return true;
}

/**
 * @brief A time plot's strips, before the walk: one for each rule of the
 *        preset whose column the file holds, where the caller gave cells.
 * @param w  The walk.
 */
static void strips_begin(walk_t *w)
{
    log_markset_t *mk = w->mk;
    if (mk->cells == NULL || mk->cells_cap <= 0) {
        return;
    }
    for (size_t r = 0; r < sizeof(k_strip_rules) / sizeof(k_strip_rules[0])
                       && mk->n_strips < LOG_STRIPS; ++r) {
        const int c = (k_strip_rules[r].id == w->id)
                          ? log_fields_find(w->an, k_strip_rules[r].column,
                                            false)
                          : -1;
        if (c >= 0) {
            log_strip_t *st = &mk->strip[mk->n_strips++];
            st->kind = k_strip_rules[r].kind;
            st->column = c;
        }
    }
}

/**
 * @brief A time plot's strips, after the walk: a strip no row set is
 *        dropped, and the codes of the one after it move down.
 * @param w  The walk.
 */
static void strips_end(walk_t *w)
{
    log_markset_t *mk = w->mk;
    for (int k = mk->n_strips - 1; k >= 0; --k) {
        if (w->strip_set[k]) {
            continue;
        }
        for (int r = 0; r < mk->rows && r < mk->cells_cap; ++r) {
            const unsigned b = mk->cells[r];
            mk->cells[r] = (uint8_t)((k == 0) ? (b >> 4) : (b & 0x0Fu));
        }
        if (k == 0 && mk->n_strips > 1) {
            mk->strip[0] = mk->strip[1];
            w->strip_set[0] = w->strip_set[1];
        }
        --mk->n_strips;
    }
}

unsigned log_marks_strip_code(const log_markset_t *mk, int k, int row)
{
    if (mk == NULL || mk->cells == NULL || k < 0 || k >= mk->n_strips
        || row < 0 || row >= mk->rows || row >= mk->cells_cap) {
        return LOG_STRIP_NONE;
    }
    return (mk->cells[row] >> (4 * k)) & 0x0Fu;
}

/**
 * @brief Classify one row (log_row_fn).
 * @param ctx    The walk.
 * @param row    The row's index.
 * @param cells  Its cells.
 * @param n      How many.
 * @return false once an XY file's LOG_MARKS_ROWS rows are read; a time
 *         plot's rows are all read.
 */
static bool take_row(void *ctx, int row, const char *const *cells, int n)
{
    walk_t *w = (walk_t *)ctx;
    if (row >= LOG_MARKS_ROWS && !w->time) {
        w->mk->cut = true;
        return false;
    }
    w->mk->rows = row + 1;
    if (w->time) {
        w->mk->n_traces = 1;
        if (row < LOG_MARKS_ROWS) {
            w->mk->row[row] = 0u;
        }
        return time_row(w, row, cells, n);
    }
    int t = (int)LOG_TRACE_NONE;
    float x = 0.0f;
    const bool placed = num(w, cell(cells, n, w->c_x), &x);
    if (w->id == LOG_PRESET_LIMIT) {
        if (placed) {
            if (!w->have_center) {
                w->have_center = true;
                w->center = x;
            }
            t = limit_row(w, x, cells, n);
        }
    } else if (w->id == LOG_PRESET_RUDDR_SEARCH
               || w->id == LOG_PRESET_RUDDR_STEPS) {
        const row_kind_t rk = ruddr_kind(w, cells, n);
        if (rk == RK_STEP) {
            note_end(w, cells, n);
        }
        if (!placed) {
            t = (int)LOG_TRACE_NONE;
        } else if (w->id == LOG_PRESET_RUDDR_SEARCH) {
            t = (rk == RK_SEARCH) ? search_row(w, x) : (int)LOG_TRACE_NONE;
        } else if (rk == RK_STEP || rk == RK_VERIFY) {
            t = steps_row(w, rk, cells, n);
        }
    } else {
        t = 0;
        w->mk->n_traces = 1;
    }
    w->mk->row[row] = (uint8_t)t;
    return true;
}

/**
 * @brief LIMIT's markers, once every row is read.
 * @param w  The walk.
 */
static void limit_marks(walk_t *w)
{
    log_markset_t *mk = w->mk;
    if (!w->have_center) {
        return;
    }
    add_line(mk, LOG_MARK_CENTER, w->center);
    for (int k = 0; k < 2; ++k) {
        const side_t *s = &w->side[k];
        if (!s->any) {
            continue;
        }
        if (s->bound) {
            add_line(mk, LOG_MARK_BOUND, s->bpulse);
            if (s->have_end) {
                add_line(mk, LOG_MARK_END, s->end);
            }
        }
        if (!s->bound || s->ext != s->bpulse) {
            add_line(mk, LOG_MARK_LAST, s->ext);
        }
        if (s->level && w->c_cur >= 0) {
            add_level(mk, LOG_MARK_FREE, w->c_cur, s->free_a, w->center,
                      s->ext);
            add_level(mk, LOG_MARK_THRESHOLD, w->c_cur, s->thr_a, w->center,
                      s->ext);
        }
    }
}

/**
 * @brief RUDDR's markers and STEPS' sides, once every row is read.
 * @param w  The walk.
 */
static void ruddr_marks(walk_t *w)
{
    log_markset_t *mk = w->mk;
    if (w->id == LOG_PRESET_RUDDR_STEPS) {
        /* Of two sides the lower `side (us)` is the 1000us side. */
        float lo = 0.0f;
        float hi = 0.0f;
        int n = 0;
        for (int k = 0; k < mk->n_traces; ++k) {
            if (mk->trace[k].points) {
                continue;
            }
            lo = (n == 0 || w->key[k] < lo) ? w->key[k] : lo;
            hi = (n == 0 || w->key[k] > hi) ? w->key[k] : hi;
            ++n;
        }
        for (int k = 0; k < mk->n_traces && n > 1; ++k) {
            if (!mk->trace[k].points) {
                mk->trace[k].side = w->key[k] == lo   ? LOG_SIDE_LOW
                                    : w->key[k] == hi ? LOG_SIDE_HIGH
                                                      : LOG_SIDE_NONE;
            }
        }
        return;
    }
    if (!w->have_center) {
        return;
    }
    add_line(mk, LOG_MARK_CENTER, w->center);
    bool side_done[2] = { false, false };
    for (int k = 0; k < w->n_ends; ++k) {
        const int sd = w->ends[k] < w->center ? 0 : 1;
        if (w->ends[k] != w->center && !side_done[sd]) {
            side_done[sd] = true;
            add_line(mk, LOG_MARK_END, w->ends[k]);
        }
    }
    for (int k = 0; k < mk->n_traces; ++k) {
        bool at_end = false;
        for (int e = 0; e < w->n_ends; ++e) {
            at_end = at_end || w->ends[e] == w->last_x[k];
        }
        if (!at_end) {
            add_line(mk, LOG_MARK_LAST, w->last_x[k]);
        }
    }
}

log_err_t log_marks_read(log_source_t *src, const log_analysis_t *an,
                         log_preset_id_t id, log_markset_t *out)
{
    if (src == NULL || an == NULL || out == NULL) {
        return LOG_ERR_ARG;
    }
    uint8_t *const cells = out->cells;
    const int cap = out->cells_cap;
    memset(out, 0, sizeof(*out));
    out->cells = cells;
    out->cells_cap = (cells != NULL) ? cap : 0;
    walk_t w;
    memset(&w, 0, sizeof(w));
    w.mk = out;
    w.an = an;
    w.id = id;
    w.cur = -1;
    w.verify = -1;
    const log_preset_t *p = log_preset_get(id);
    w.c_x = (p != NULL) ? log_fields_find(an, p->x, false) : -1;
    w.c_search = log_fields_find(an, "search", false);
    w.c_side = log_fields_find(an, "side", false);
    w.c_state = log_fields_find(an, "state", false);
    w.c_free = log_fields_find(an, "free", false);
    w.c_thr = log_fields_find(an, "threshold", false);
    w.c_end = log_fields_find(an, "end point", false);
    w.c_cur = log_fields_find(an, "current", true);
    w.c_kind = log_fields_find(an, "kind", false);
    w.c_write = log_fields_find(an, "write", false);
    w.c_within = log_fields_find(an, "within", false);
    w.c_step = (id == LOG_PRESET_ATEST) ? log_fields_find(an, "step", false)
                                        : -1;
    w.c_clip = log_fields_find(an, "clipped", false);
    w.span[0] = -1;
    w.span[1] = -1;
    w.time = p != NULL && p->plot == LOG_PLOT_TIME;
    if (w.time) {
        strips_begin(&w);
    }
    const log_err_t err = log_csv_each_row(src, an, take_row, &w);
    if (err != LOG_OK) {
        return err;
    }
    if (w.time) {
        strips_end(&w);
    } else if (id == LOG_PRESET_LIMIT) {
        limit_marks(&w);
    } else if (id == LOG_PRESET_RUDDR_SEARCH || id == LOG_PRESET_RUDDR_STEPS) {
        ruddr_marks(&w);
    }
    return LOG_OK;
}
