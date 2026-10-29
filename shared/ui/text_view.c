/**
 * @file text_view.c
 * @brief The text viewer: the state, the file's load, the scrolling, the
 *        verdict words and the touch events (text_view.h).
 *        text_view_draw.c draws it, text_view_keys.c is the editor;
 *        text_view_int.h is what they share.
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
/** @brief The file as loaded or last saved, for DISCARD: 1MiB, in PSRAM
 *         on the panel. */
uint8_t tv_orig[TEXT_BUF_FILE_MAX];

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

/** @brief The editor off: its keyboard, its question and its note gone. */
static void edit_off(void)
{
    tv.edit = false;
    tv.saving = false;
    tv.ask = TEXT_VIEW_ASK_NONE;
    tv.note = TV_NOTE_NONE;
    ui_textkey_close(&tv.kb);
}

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
    edit_off();
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
    edit_off();
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
    if (tv.kb.open) {
        return tv.small ? TV_KB_ROWS_SMALL : TV_KB_ROWS_NORMAL;
    }
    return tv.small ? TEXT_VIEW_ROWS_SMALL : TEXT_VIEW_ROWS_NORMAL;
}

int tv_rows_drawn(void)
{
    if (tv.kb.open && tv.kb.see > 0u) {
        return tv.small ? TEXT_VIEW_ROWS_SMALL : TEXT_VIEW_ROWS_NORMAL;
    }
    return text_view_rows();
}

gfx_rect_t tv_area(void)
{
    return gfx_rect_make(TV_L, TV_AREA_Y, TV_W,
                         tv.kb.open ? TV_KB_AREA_H : TV_AREA_H);
}

gfx_rect_t tv_bar(void)
{
    const gfx_rect_t a = tv_area();
    return gfx_rect_make(TV_R - 4 - TV_BAR_W, a.y + 4, TV_BAR_W, a.h - 8);
}

void tv_say(ui_text_id_t id)
{
    tv.note = TV_NOTE_TEXT;
    tv.note_id = id;
    ++tv.rev;
}

void tv_say_err(fm_err_t err)
{
    tv.note = TV_NOTE_ERR;
    tv.note_err = err;
    ++tv.rev;
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
    if (tv.state != TEXT_VIEW_SHOWN) {
        return 0u;
    }
    /* A line end at the file's end starts no line of its own, unless the
     * editor's cursor is to stand on it. */
    return tv.edit ? text_buf_lines(&tv_tb) : text_buf_lines_viewed(&tv_tb);
}

size_t tv_rows_of(size_t line)
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
        const size_t r = tv_rows_of(l);
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

void tv_clamp_top(void)
{
    size_t mr;
    const size_t ml = tv_max_top(&mr);
    if (past(tv.top_line, tv.top_row, ml, mr)) {
        tv.top_line = ml;
        tv.top_row = mr;
    }
    if (tv.top_row >= tv_rows_of(tv.top_line)) {
        tv.top_row = tv_rows_of(tv.top_line) - 1u;
    }
}

void text_view_set_small(bool small)
{
    tv.small = small;
    tv.top_row = 0u;
    tv_clamp_top();
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
        const size_t left = tv_rows_of(tv.top_line) - 1u - tv.top_row;
        if ((size_t)rows <= left) {
            tv.top_row += (size_t)rows;
            break;
        }
        if (tv.top_line + 1u >= lines) {
            tv.top_row = tv_rows_of(tv.top_line) - 1u;
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
        tv.top_row = tv_rows_of(tv.top_line) - 1u;
    }
    tv_clamp_top();
}

size_t tv_line_at(int k, size_t *row)
{
    const size_t lines = text_view_lines();
    size_t line = tv.top_line;
    size_t r = tv.top_row;
    while (k > 0 && line < lines) {
        if (r + 1u < tv_rows_of(line)) {
            ++r;
        } else {
            ++line;
            r = 0u;
        }
        --k;
    }
    *row = r;
    return line;
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

/**
 * @brief A top position as a line count: the line, and the share of its
 *        rows above the top.  The bar maps this, so a line wrapped over
 *        more rows than a screen scrolls along it too.
 * @param line  the line
 * @param row   the row in it
 * @return line + row / rows of the line
 */
static double pos_of(size_t line, size_t row)
{
    return (double)line + (double)row / (double)tv_rows_of(line);
}

void tv_bar_thumb(int *y, int *h)
{
    const gfx_rect_t t = tv_bar();
    size_t mr;
    const size_t ml = tv_max_top(&mr);
    const long rows = text_view_rows();
    if (ml == 0u && mr == 0u) {
        *y = t.y;
        *h = t.h;
        return;
    }
    /* Lines and rows past the first screen, as the travel's length. */
    int th = (int)((long)t.h * rows / (rows + (long)ml + (long)mr));
    if (th < TV_BAR_MIN) {
        th = TV_BAR_MIN;
    }
    const int travel = t.h - th;
    const double end = pos_of(ml, mr);
    double at = pos_of(tv.top_line, tv.top_row);
    at = (at < end) ? at : end;
    *y = t.y + (int)((double)travel * at / end + 0.5);
    *h = th;
}

/**
 * @brief Put the thumb's top nearest @p top: the position its place along
 *        the travel gives, to the row; the travel's ends are the first and
 *        the last screen, as PAGE UP and PAGE DOWN reach them.
 * @param top  the thumb's top the finger asks for
 */
static void bar_to(int top)
{
    const gfx_rect_t t = tv_bar();
    size_t mr;
    const size_t ml = tv_max_top(&mr);
    int ty;
    int th;
    tv_bar_thumb(&ty, &th);
    const int travel = t.h - th;
    if ((ml == 0u && mr == 0u) || travel <= 0) {
        return;
    }
    int rel = top - t.y;
    rel = (rel < 0) ? 0 : (rel > travel) ? travel : rel;
    if (rel == travel) {
        tv.top_line = ml;
        tv.top_row = mr;
        return;
    }
    const double at = pos_of(ml, mr) * rel / travel;
    size_t line = (size_t)at;
    size_t row = (size_t)((at - (double)line) * (double)tv_rows_of(line) + 0.5);
    if (row >= tv_rows_of(line)) {
        ++line;                     /* rounded up into the next line */
        row = 0u;
    }
    tv.top_line = line;
    tv.top_row = row;
    tv_clamp_top();
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
    ui_textkey_cancel_press(&tv.kb);    /* a key held: its release types
                                           nothing, and a new contact acts */
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
    /* The keyboard covers the footer. */
    if (!tv.kb.open) {
        if (gfx_rect_contains(TV_SMALL, x, y)) {
            return TV_HIT_SMALL;
        }
        if (gfx_rect_contains(TV_EDIT, x, y)) {
            return TV_HIT_EDIT;
        }
        if (tv.edit && gfx_rect_contains(TV_KEYS, x, y)) {
            return TV_HIT_KEYS;
        }
        if (gfx_rect_contains(TV_PAGE_UP, x, y)) {
            return TV_HIT_PAGE_UP;
        }
        if (gfx_rect_contains(TV_PAGE_DN, x, y)) {
            return TV_HIT_PAGE_DN;
        }
    }
    if (tv.state != TEXT_VIEW_SHOWN) {
        return TV_HIT_NONE;
    }
    if (gfx_rect_contains(tv_bar(), x, y)) {
        return TV_HIT_BAR;
    }
    const gfx_rect_t a = tv_area();
    if (gfx_rect_contains(gfx_rect_make(a.x, a.y, a.w - 4 - TV_BAR_W, a.h),
                          x, y)) {
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
 * @return TEXT_VIEW_SAVE for SAVE, else TEXT_VIEW_NONE
 */
static text_view_result_t press_up(const touch_event_t *e)
{
    const int hit = tv.press;
    tv.tap_have = false;
    tv.press = TV_HIT_NONE;
    if (!touch_event_is_tap_up(e) || tv.tap_moved || far_from_press(e)
        || hit_at(e->point.x, e->point.y) != hit) {
        return TEXT_VIEW_NONE;
    }
    if (tv.edit && hit >= TV_HIT_SMALL && hit <= TV_HIT_KEYS) {
        return tv_edit_button(hit);
    }
    switch (hit) {
    case TV_HIT_SMALL:
        text_view_set_small(!tv.small);
        break;
    case TV_HIT_EDIT:
        if (!text_view_edit() && tv.state == TEXT_VIEW_SHOWN) {
            tv_say_err(tv.edit_err);
        }
        break;
    case TV_HIT_PAGE_UP:
        text_view_scroll(-(long)text_view_rows());
        break;
    case TV_HIT_PAGE_DN:
        text_view_scroll((long)text_view_rows());
        break;
    case TV_HIT_TEXT:
        if (tv.edit) {
            tv_edit_tap(e->point.x, e->point.y);
        }
        break;
    default:
        break;
    }
    return TEXT_VIEW_NONE;
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
    const uint32_t rev = tv.rev;
    const uint32_t kb_rev = tv.kb.revision;
    text_view_result_t out = TEXT_VIEW_NONE;
    if (!tv_edit_event(e)) {
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
                out = press_up(e);
            }
            break;
        }
    }
    if (out == TEXT_VIEW_NONE) {
        out = tv_edit_take();
    }
    if (out != TEXT_VIEW_NONE) {
        return out;
    }
    return (line != tv.top_line || row != tv.top_row || press != tv.press
            || small != tv.small || rev != tv.rev
            || kb_rev != tv.kb.revision)
               ? TEXT_VIEW_CHANGED
               : TEXT_VIEW_NONE;
}
