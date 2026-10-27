/**
 * @file text_view.c
 * @brief The text viewer: the state, the file's load, the scrolling, the
 *        verdict words and the touch events (text_view.h).
 *        text_view_draw.c draws it; text_view_int.h is what the two share.
 *
 * The top of the screen is a line and a row of that line, so a scroll
 * walks only the lines it crosses: a file of 65,536 lines is never walked
 * whole.  The scroll bar spreads the file's lines over its travel; its
 * thumb takes the share of a screen of rows in the rows the text scrolls
 * through, counted as one row a line beyond the last screen.
 *
 * A button is a tap: the press arms it and the release of the same
 * contact acts, inside it and no further than TV_SLOP from where it came
 * down.  A drag on the text moves it a row per row pitch, from where it
 * stood at the press.  A drag on the bar puts the thumb under the finger.
 *
 * SPDX-License-Identifier: MIT
 */

#include "text_view.h"

#include <stdio.h>
#include <string.h>

#include "servo_str.h"
#include "ui_text.h"

#include "text_view_int.h"

/** @brief The viewer's state; text_view_int.h names it tv. */
tv_state_t tv_s;
/** @brief The text over s_mem and s_ends. */
text_buf_t tv_tb;

/** @brief The file's bytes, 1MiB, in PSRAM on the panel (psram_bss.lf). */
static uint8_t s_mem[TEXT_BUF_FILE_MAX];
/** @brief Its line index, 256KiB, in PSRAM on the panel. */
static uint32_t s_ends[TEXT_BUF_ENDS_MAX];

/* -------------------------------------------------------------- words */

/** @brief The OK words' text IDs. */
static const servo_str_t k_ok[] = { SERVO_STR_PASS };
/** @brief The FAIL words'. */
static const servo_str_t k_fail[] = { SERVO_STR_FAIL };
/** @brief The WARN words': what was stopped, cut short or not measured. */
static const servo_str_t k_warn[] = {
    SERVO_STR_ABORTED,       SERVO_STR_NOT_MEASURABLE,
    SERVO_STR_R_NOT_CHECKED, SERVO_STR_R_NOT_MEASURED,
    SERVO_STR_R_BO_NOT_RUN,  SERVO_STR_R_CUT_SHORT,
    SERVO_STR_R_BROWNOUT_NOT_RUN,
};
/** @brief The marks the writers put for a value they hold none of:
 *         servo_report.c's "--" and kst_file.c's "[not loaded]". */
static const char *const k_marks[] = { "--", "[not loaded]" };

/** @brief The entries of array @p a. */
#define N_OF(a) (sizeof(a) / sizeof((a)[0]))

/** @brief The OK words in every language. */
static const char *s_ok[N_OF(k_ok) * UI_LANG_COUNT];
/** @brief The FAIL words in every language. */
static const char *s_fail[N_OF(k_fail) * UI_LANG_COUNT];
/** @brief The WARN words in every language, and the marks. */
static const char *s_warn[N_OF(k_warn) * UI_LANG_COUNT + N_OF(k_marks)];
/** @brief The three lists, built at the first call of text_view_words(). */
static text_hl_words_t s_words;

/**
 * @brief @p ids in every language into @p out.
 * @param out  takes the strings
 * @param ids  the IDs
 * @param n    their count
 * @return the strings written
 */
static size_t fill_words(const char **out, const servo_str_t *ids, size_t n)
{
    size_t k = 0u;
    for (int l = 0; l < UI_LANG_COUNT; ++l) {
        const ui_language_t *t = ui_text_table((ui_lang_t)l);
        for (size_t i = 0u; i < n; ++i) {
            out[k++] = servo_str_in((t != NULL) ? t->servo : NULL, ids[i]);
        }
    }
    return k;
}

const text_hl_words_t *text_view_words(void)
{
    if (s_words.ok == NULL) {
        s_words.ok = s_ok;
        s_words.ok_n = fill_words(s_ok, k_ok, N_OF(k_ok));
        s_words.fail = s_fail;
        s_words.fail_n = fill_words(s_fail, k_fail, N_OF(k_fail));
        size_t w = fill_words(s_warn, k_warn, N_OF(k_warn));
        for (size_t i = 0u; i < N_OF(k_marks); ++i) {
            s_warn[w++] = k_marks[i];
        }
        s_words.warn = s_warn;
        s_words.warn_n = w;
    }
    return &s_words;
}

/* ----------------------------------------------------------- the file */

void text_view_reset(void)
{
    memset(&tv, 0, sizeof(tv));
    tv.press = TV_HIT_NONE;
    (void)text_buf_init(&tv_tb, s_mem, sizeof(s_mem), s_ends, N_OF(s_ends));
}

bool text_view_fits(uint64_t size)
{
    return size <= (uint64_t)TEXT_BUF_FILE_MAX;
}

void text_view_begin(const char *dir, const char *name, uint64_t size)
{
    text_view_cancel_press();
    snprintf(tv.dir, sizeof(tv.dir), "%s", (dir != NULL) ? dir : "");
    snprintf(tv.name, sizeof(tv.name), "%s", (name != NULL) ? name : "");
    tv.size = size;
    tv.kind = text_hl_kind_of(tv.name);
    tv.state = TEXT_VIEW_LOADING;
    tv.err = FM_OK;
    tv.tb_err = TEXT_BUF_OK;
    tv.top_line = 0u;
    tv.top_row = 0u;
}

uint8_t *text_view_load_area(size_t *cap)
{
    if (tv_tb.mem == NULL) {
        (void)text_buf_init(&tv_tb, s_mem, sizeof(s_mem), s_ends,
                            N_OF(s_ends));
    }
    if (cap != NULL) {
        *cap = text_buf_room(&tv_tb);
    }
    return text_buf_load_area(&tv_tb);
}

void text_view_loaded(fm_err_t err, size_t len)
{
    if (tv.state != TEXT_VIEW_LOADING) {
        return;
    }
    tv.top_line = 0u;
    tv.top_row = 0u;
    if (err != FM_OK) {
        (void)text_buf_init(&tv_tb, s_mem, sizeof(s_mem), s_ends,
                            N_OF(s_ends));
        tv.err = err;
        tv.state = TEXT_VIEW_FAILED;
        return;
    }
    const text_buf_err_t r = text_buf_load_done(&tv_tb, len);
    if (r != TEXT_BUF_OK) {
        tv.tb_err = r;
        tv.state = TEXT_VIEW_FAILED;
        return;
    }
    tv.state = TEXT_VIEW_SHOWN;
}

void text_view_close(void)
{
    text_view_cancel_press();
    tv.state = TEXT_VIEW_CLOSED;
    tv.dir[0] = '\0';
    tv.name[0] = '\0';
}

text_view_state_t text_view_state(void)
{
    return tv.state;
}

fm_err_t text_view_why(text_buf_err_t *tb_err)
{
    if (tb_err != NULL) {
        *tb_err = (tv.state == TEXT_VIEW_FAILED) ? tv.tb_err : TEXT_BUF_OK;
    }
    return (tv.state == TEXT_VIEW_FAILED) ? tv.err : FM_OK;
}

const char *text_view_name(void)
{
    return tv.name;
}

const char *text_view_dir(void)
{
    return tv.dir;
}

/* ------------------------------------------------------ face and rows */

bool text_view_small(void)
{
    return tv.small;
}

int text_view_rows(void)
{
    return tv.small ? TEXT_VIEW_ROWS_SMALL : TEXT_VIEW_ROWS_NORMAL;
}

unsigned text_view_cells(void)
{
    return tv.small ? TEXT_VIEW_CELLS_SMALL : TEXT_VIEW_CELLS_NORMAL;
}

int tv_pitch(void)
{
    return tv.small ? TV_PITCH_SMALL : TV_PITCH_NORMAL;
}

const gfx_font_t *tv_face(void)
{
    return tv.small ? TV_FONT_FINE : TV_FONT_LABEL;
}

size_t text_view_lines(void)
{
    if (tv.state != TEXT_VIEW_SHOWN || text_buf_len(&tv_tb) == 0u) {
        return 0u;
    }
    size_t n = text_buf_lines(&tv_tb);
    /* A line end at the file's end starts no line of its own. */
    if (n > 1u && text_buf_line_len(&tv_tb, n - 1u) == 0u) {
        --n;
    }
    return n;
}

/**
 * @brief Rows line @p line takes at the face's width.
 * @param line  the line
 * @return its rows, 1 at least
 */
static size_t rows_of(size_t line)
{
    const size_t r = text_buf_rows(&tv_tb, line, text_view_cells());
    return (r > 0u) ? r : 1u;
}

size_t tv_max_top(size_t *row)
{
    size_t need = (size_t)text_view_rows();
    size_t l = text_view_lines();
    *row = 0u;
    while (l > 0u) {
        --l;
        const size_t r = rows_of(l);
        if (r >= need) {
            *row = r - need;
            return l;
        }
        need -= r;
    }
    return 0u;
}

/**
 * @brief Whether the top (@p line, @p row) is past (@p ml, @p mr).
 * @param line  the line
 * @param row   its row
 * @param ml    the other's line
 * @param mr    its row
 * @return true when it is further down
 */
static bool past(size_t line, size_t row, size_t ml, size_t mr)
{
    return line > ml || (line == ml && row > mr);
}

/** @brief The top held to the text: its row inside its line, and not past
 *         the last screen. */
static void clamp_top(void)
{
    size_t mr;
    const size_t ml = tv_max_top(&mr);
    if (past(tv.top_line, tv.top_row, ml, mr)) {
        tv.top_line = ml;
        tv.top_row = mr;
    }
    if (tv.top_row >= rows_of(tv.top_line)) {
        tv.top_row = rows_of(tv.top_line) - 1u;
    }
}

void text_view_set_small(bool small)
{
    tv.small = small;
    tv.top_row = 0u;
    clamp_top();
}

size_t text_view_top(size_t *row)
{
    if (row != NULL) {
        *row = tv.top_row;
    }
    return tv.top_line;
}

void text_view_scroll(long rows)
{
    const size_t lines = text_view_lines();
    if (lines == 0u) {
        return;
    }
    while (rows > 0) {
        const size_t left = rows_of(tv.top_line) - 1u - tv.top_row;
        if ((size_t)rows <= left) {
            tv.top_row += (size_t)rows;
            break;
        }
        if (tv.top_line + 1u >= lines) {
            tv.top_row = rows_of(tv.top_line) - 1u;
            break;
        }
        rows -= (long)left + 1;
        ++tv.top_line;
        tv.top_row = 0u;
    }
    while (rows < 0) {
        if ((size_t)(-rows) <= tv.top_row) {
            tv.top_row -= (size_t)(-rows);
            break;
        }
        if (tv.top_line == 0u) {
            tv.top_row = 0u;
            break;
        }
        rows += (long)tv.top_row + 1;
        --tv.top_line;
        tv.top_row = rows_of(tv.top_line) - 1u;
    }
    clamp_top();
}

bool text_view_at_start(void)
{
    return tv.top_line == 0u && tv.top_row == 0u;
}

bool text_view_at_end(void)
{
    size_t mr;
    const size_t ml = tv_max_top(&mr);
    return !past(ml, mr, tv.top_line, tv.top_row);
}

/* ------------------------------------------------------------ the bar */

void tv_bar_thumb(int *y, int *h)
{
    const gfx_rect_t t = TV_BAR;
    size_t mr;
    const size_t span = tv_max_top(&mr);
    const int rows = text_view_rows();
    if (span == 0u) {
        *y = t.y;
        *h = t.h;
        return;
    }
    int th = (int)((long)t.h * rows / ((long)rows + (long)span));
    if (th < TV_BAR_MIN) {
        th = TV_BAR_MIN;
    }
    const int travel = t.h - th;
    const size_t at = (tv.top_line < span) ? tv.top_line : span;
    *y = t.y + (int)((double)travel * (double)at / (double)span + 0.5);
    *h = th;
}

/**
 * @brief Put the thumb's top nearest @p top: the line its place along the
 *        travel gives on top, from its first row.
 * @param top  the thumb's top the finger asks for
 */
static void bar_to(int top)
{
    const gfx_rect_t t = TV_BAR;
    size_t mr;
    const size_t span = tv_max_top(&mr);
    int ty;
    int th;
    tv_bar_thumb(&ty, &th);
    const int travel = t.h - th;
    if (span == 0u || travel <= 0) {
        return;
    }
    int rel = top - t.y;
    rel = (rel < 0) ? 0 : (rel > travel) ? travel : rel;
    tv.top_line = (size_t)((double)span * rel / travel + 0.5);
    tv.top_row = 0u;
    clamp_top();
}

/* ------------------------------------------------------ rows on screen */

/** @brief What text_view_row_text() and text_view_cell_class() look for. */
typedef struct {
    int row;         /**< the row on screen                    */
    int cell;        /**< the cell, for the class              */
    char *out;       /**< the row's text, or NULL              */
    size_t n;        /**< bytes at out                         */
    size_t used;     /**< bytes written                        */
    int cls;         /**< the class found                      */
    bool found;      /**< the row holds text                   */
} probe_t;

/**
 * @brief tv_walk()'s call for a probe: the row's runs into its text, and
 *        the class of the cell asked for.
 * @param ctx    the probe
 * @param row    the run's row on screen
 * @param cell   its first cell; -1 for the wrap mark
 * @param bytes  its text
 * @param cls    its class
 */
static void probe_run(void *ctx, int row, int cell, const char *bytes,
                      int cls)
{
    probe_t *p = (probe_t *)ctx;
    if (row != p->row || cell < 0) {
        return;
    }
    p->found = true;
    const int cells = gfx_text_cells(bytes);
    if (p->cell >= cell && p->cell < cell + cells) {
        p->cls = cls;
    }
    if (p->out != NULL && p->used + 1u < p->n) {
        const int w = snprintf(p->out + p->used, p->n - p->used, "%s",
                               bytes);
        if (w > 0) {
            p->used += (size_t)w;
            if (p->used >= p->n) {
                p->used = p->n - 1u;
            }
        }
    }
}

bool text_view_row_text(int k, char *out, size_t n)
{
    probe_t p = { .row = k, .cell = -1, .out = out, .n = n,
                  .cls = TEXT_HL_TEXT };
    if (out != NULL && n > 0u) {
        out[0] = '\0';
    }
    (void)tv_walk(probe_run, &p);
    return p.found;
}

int text_view_cell_class(int k, int cell)
{
    probe_t p = { .row = k, .cell = cell, .cls = TEXT_HL_TEXT };
    (void)tv_walk(probe_run, &p);
    return p.cls;
}

/* -------------------------------------------------------------- touch */

void text_view_cancel_press(void)
{
    tv.tap_have = false;
    tv.tap_moved = false;
    tv.press = TV_HIT_NONE;
}

/**
 * @brief Whether @p e is further than the slop from the press, in x or y.
 * @param e  the event
 * @return true past TV_SLOP
 */
static bool far_from_press(const touch_event_t *e)
{
    const int dx = e->point.x - tv.tap_x0;
    const int dy = e->point.y - tv.tap_y0;
    return dx > TV_SLOP || -dx > TV_SLOP || dy > TV_SLOP || -dy > TV_SLOP;
}

/**
 * @brief What is at (@p x, @p y).
 * @param x  the point
 * @param y  the point
 * @return a tv_hit_t
 */
static int hit_at(int x, int y)
{
    if (gfx_rect_contains(TV_SMALL, x, y)) {
        return TV_HIT_SMALL;
    }
    if (gfx_rect_contains(TV_PAGE_UP, x, y)) {
        return TV_HIT_PAGE_UP;
    }
    if (gfx_rect_contains(TV_PAGE_DN, x, y)) {
        return TV_HIT_PAGE_DN;
    }
    if (tv.state != TEXT_VIEW_SHOWN) {
        return TV_HIT_NONE;
    }
    if (gfx_rect_contains(TV_BAR, x, y)) {
        return TV_HIT_BAR;
    }
    if (gfx_rect_contains(TV_TEXT_HIT, x, y)) {
        return TV_HIT_TEXT;
    }
    return TV_HIT_NONE;
}

/**
 * @brief A DOWN: the press, unless another contact holds it.
 * @param e  the DOWN
 */
static void press_down(const touch_event_t *e)
{
    /* A DOWN with the id of the press held says that contact has gone and
     * its release with it, and is taken. */
    if (tv.tap_have && e->point.id != tv.tap_id) {
        return;
    }
    tv.tap_have = true;
    tv.tap_moved = false;
    tv.tap_id = e->point.id;
    tv.tap_x0 = e->point.x;
    tv.tap_y0 = e->point.y;
    tv.press = hit_at(e->point.x, e->point.y);
    tv.press_line = tv.top_line;
    tv.press_row = tv.top_row;
    if (tv.press == TV_HIT_BAR) {
        int ty;
        int th;
        tv_bar_thumb(&ty, &th);
        if (e->point.y >= ty && e->point.y < ty + th) {
            tv.bar_grab = e->point.y - ty;      /* taken by the thumb */
        } else {
            tv.bar_grab = th / 2;               /* the thumb jumps there */
            bar_to(e->point.y - tv.bar_grab);
        }
    }
}

/**
 * @brief A MOVE of the held contact: a drag on the text or on the bar.
 * @param e  the MOVE
 */
static void press_move(const touch_event_t *e)
{
    if (far_from_press(e)) {
        tv.tap_moved = true;
    }
    if (tv.press == TV_HIT_BAR) {
        bar_to(e->point.y - tv.bar_grab);
    } else if (tv.press == TV_HIT_TEXT && tv.tap_moved) {
        tv.top_line = tv.press_line;
        tv.top_row = tv.press_row;
        text_view_scroll(-(long)((e->point.y - tv.tap_y0) / tv_pitch()));
    }
}

/**
 * @brief The UP of the held contact: a button tapped acts.
 * @param e  the UP
 */
static void press_up(const touch_event_t *e)
{
    const int hit = tv.press;
    tv.tap_have = false;
    tv.press = TV_HIT_NONE;
    if (!touch_event_is_tap_up(e) || tv.tap_moved || far_from_press(e)
        || hit_at(e->point.x, e->point.y) != hit) {
        return;
    }
    switch (hit) {
    case TV_HIT_SMALL:
        text_view_set_small(!tv.small);
        break;
    case TV_HIT_PAGE_UP:
        text_view_scroll(-(long)text_view_rows());
        break;
    case TV_HIT_PAGE_DN:
        text_view_scroll((long)text_view_rows());
        break;
    default:
        break;
    }
}

text_view_result_t text_view_event(const touch_event_t *e)
{
    if (e == NULL || tv.state == TEXT_VIEW_CLOSED) {
        return TEXT_VIEW_NONE;
    }
    const size_t line = tv.top_line;
    const size_t row = tv.top_row;
    const int press = tv.press;
    const bool small = tv.small;
    switch (e->type) {
    case TOUCH_EVENT_DOWN:
        press_down(e);
        break;
    case TOUCH_EVENT_MOVE:
        if (tv.tap_have && e->point.id == tv.tap_id) {
            press_move(e);
        }
        break;
    case TOUCH_EVENT_UP:
    default:
        if (tv.tap_have && e->point.id == tv.tap_id) {
            press_up(e);
        }
        break;
    }
    return (line != tv.top_line || row != tv.top_row || press != tv.press
            || small != tv.small)
               ? TEXT_VIEW_CHANGED
               : TEXT_VIEW_NONE;
}
