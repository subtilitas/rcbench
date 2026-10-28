/**
 * @file text_view_draw.c
 * @brief The text viewer and editor drawn: the header, the rows of the
 *        text with their classes' colours, the wrap marks, the cursor, the
 *        scroll bar, the footer, the keyboard and the question.
 *        text_view.c holds the state; text_view_int.h is what they share.
 *
 * A row is drawn as runs of cells of one class, each one gfx_text() call.
 * The cells follow text_buf.h's rule: a unit is a cell, a tab reaches to
 * the next multiple of TEXT_BUF_TAB cells and ends at the row's last cell
 * where it does not fit, and a unit that does not fit a row starts the
 * next one.  A tab draws as spaces; a control character, a NUL and a byte
 * that starts no well-formed UTF-8 sequence draw as "?".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "files_screen.h"
#include "fm_name.h"
#include "text_view.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#include "text_view_int.h"

/** @brief A run's bytes at most: a row of 4-byte code points. */
#define RUN_MAX (TEXT_VIEW_CELLS_SMALL * 4 + 1)

/** @brief Expands a row of TEXT_HL_CLASS_ROWS to its colour role. */
#define TV_ROLE(name, css, role) role,

/** @brief Each class's colour role, in the order of text_hl_class_t. */
static const ui_color_id_t k_role[TEXT_HL_CLASSES] = {
    TEXT_HL_CLASS_ROWS(TV_ROLE)
};

/* ------------------------------------------------------------ the walk */

/** @brief One line's walk: where it is drawn and the run being built. */
typedef struct {
    /** @brief The call for each run and each wrap mark (tv_walk()). */
    void (*run)(void *ctx, int row, int cell, const char *bytes, int cls);
    void *ctx;        /**< passed to run                              */
    int first;        /**< the screen row of the line's row skip      */
    size_t skip;      /**< the line's rows above the screen           */
    int rows;         /**< rows on screen                             */
    char buf[RUN_MAX]; /**< the run's bytes                           */
    size_t used;      /**< bytes in buf                               */
    int run_row;      /**< the run's screen row                       */
    int run_cell;     /**< its first cell                             */
    int run_cls;      /**< its class                                  */
} row_walk_t;

/**
 * @brief Hand the run built to the call, and start an empty one.
 * @param w  the walk
 */
static void flush(row_walk_t *w)
{
    if (w->used > 0u && w->run != NULL) {
        w->buf[w->used] = '\0';
        w->run(w->ctx, w->run_row, w->run_cell, w->buf, w->run_cls);
    }
    w->used = 0u;
}

/**
 * @brief The bytes code point @p cp is drawn with: UTF-8, a control
 *        character and a NUL as "?".
 * @param cp   the code point
 * @param out  takes the bytes, 4 at most
 * @return the bytes written
 */
static size_t glyph_of(uint32_t cp, char out[4])
{
    if (cp < 0x20u || cp == 0x7Fu) {
        out[0] = '?';
        return 1u;
    }
    if (cp < 0x80u) {
        out[0] = (char)cp;
        return 1u;
    }
    if (cp < 0x800u) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2u;
    }
    if (cp < 0x10000u) {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        return 3u;
    }
    out[0] = (char)(0xF0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4u;
}

/**
 * @brief The class of byte @p off of a line, from its spans.
 * @param sp   the spans, in order
 * @param n    their count
 * @param at   the span looked at last, moved on
 * @param off  the byte
 * @return its class; TEXT past the spans
 */
static int class_at(const text_hl_span_t *sp, size_t n, size_t *at,
                    size_t off)
{
    while (*at < n && off >= (size_t)sp[*at].start + sp[*at].len) {
        ++*at;
    }
    return (*at < n) ? sp[*at].cls : TEXT_HL_TEXT;
}

/**
 * @brief Walk line @p line from its row @p w->skip onto the screen from
 *        row @p w->first.
 * @param w     the walk
 * @param line  the line
 * @return the rows of the screen it took
 */
static int walk_line(row_walk_t *w, size_t line)
{
    size_t len = 0u;
    const char *bytes = text_buf_line(&tv_tb, line, &len);
    static text_hl_span_t sp[TV_SPANS];
    const size_t nsp = text_hl_line(bytes, len, line, tv.kind,
                                    text_view_words(), sp, TV_SPANS);
    const size_t start = text_buf_line_start(&tv_tb, line);
    const unsigned width = text_view_cells();
    const size_t last = w->skip + (size_t)(w->rows - w->first);
    size_t row = 0u;
    size_t col = 0u;
    size_t at = 0u;
    w->used = 0u;
    for (size_t off = 0u; off < len;) {
        size_t ub = 1u;
        const uint32_t cp = text_buf_cp(&tv_tb, start + off, &ub);
        if (col >= width) {
            flush(w);
            ++row;
            col = 0u;
            if (row >= last) {
                break;
            }
            if (row >= w->skip && w->run != NULL) {
                w->run(w->ctx, w->first + (int)(row - w->skip), -1, "",
                       TEXT_HL_TEXT);
            }
        }
        size_t cells = 1u;
        if (cp == '\t') {
            cells = TEXT_BUF_TAB - col % TEXT_BUF_TAB;
            if (col + cells > width) {
                cells = width - col;
            }
        }
        if (row >= w->skip) {
            const int cls = class_at(sp, nsp, &at, off);
            if (w->used == 0u || cls != w->run_cls) {
                flush(w);
                w->run_row = w->first + (int)(row - w->skip);
                w->run_cell = (int)col;
                w->run_cls = cls;
            }
            char g[4];
            const size_t gn = (cp == '\t') ? 0u : glyph_of(cp, g);
            for (size_t k = 0u; cp == '\t' && k < cells; ++k) {
                w->buf[w->used++] = ' ';
            }
            memcpy(w->buf + w->used, g, gn);
            w->used += gn;
        }
        col += cells;
        off += (ub > 0u) ? ub : 1u;
    }
    flush(w);
    const size_t drawn = (row + 1u > w->skip) ? row + 1u - w->skip : 0u;
    const int left = w->rows - w->first;
    return (drawn < (size_t)left) ? (int)drawn : left;
}

int tv_walk(void (*run)(void *ctx, int row, int cell, const char *bytes,
                        int cls),
            void *ctx)
{
    if (tv.state != TEXT_VIEW_SHOWN) {
        return 0;
    }
    static row_walk_t w;
    w.run = run;
    w.ctx = ctx;
    w.rows = text_view_rows();
    w.first = 0;
    w.skip = tv.top_row;
    const size_t lines = text_view_lines();
    for (size_t line = tv.top_line; line < lines && w.first < w.rows;
         ++line) {
        w.first += walk_line(&w, line);
        w.skip = 0u;
    }
    return w.first;
}

/* ------------------------------------------------------------ drawing */

/**
 * @brief tv_walk()'s call for the screen: a run in its class's colour, or
 *        the wrap mark.
 * @param ctx    the canvas
 * @param row    the screen row
 * @param cell   the run's first cell, -1 for the wrap mark
 * @param bytes  its text
 * @param cls    its class
 */
static void draw_run(void *ctx, int row, int cell, const char *bytes,
                     int cls)
{
    gfx_canvas_t *c = (gfx_canvas_t *)ctx;
    const int pitch = tv_pitch();
    const int y = TV_TEXT_Y + row * pitch;
    if (cell < 0) {
        gfx_text(c, TV_MARK_X, y + (pitch - 16) / 2, "..", TV_FONT_FINE,
                 UI_TEXT_FAINT, 1);
        return;
    }
    const gfx_font_t *f = tv_face();
    const int cw = text_view_small() ? 8 : 10;
    const int k = (cls >= 0 && cls < TEXT_HL_CLASSES) ? cls : TEXT_HL_TEXT;
    gfx_text(c, TV_TEXT_X + cell * cw, y, bytes, f,
             ui_theme_color(k_role[k]), 1);
}

/**
 * @brief The header: the file's path cut from its front, its size,
 *        READ-ONLY, and LINE n OF m at the right.
 * @param c  the canvas
 */
static void draw_header(gfx_canvas_t *c)
{
    char path[FM_PATH_SIZE + FM_NAME_BUF + 4];
    if (tv.dir[0] != '\0') {
        snprintf(path, sizeof(path), "%s/%s", tv.dir, tv.name);
    } else {
        snprintf(path, sizeof(path), "%s", tv.name);
    }
    const int have = gfx_text_cells(path);
    char shown[sizeof(path)];
    if (have > TV_NAME_CELLS) {
        const size_t skip = gfx_text_prefix(path, have - (TV_NAME_CELLS - 2));
        snprintf(shown, sizeof(shown), "..%s", path + skip);
    } else {
        snprintf(shown, sizeof(shown), "%s", path);
    }
    int x = TV_L + 8;
    x += gfx_text(c, x, TV_HDR_TY, shown, TV_FONT_LABEL, UI_TEXT, 1) + 16;
    char size[24];
    files_size_text(size, sizeof(size), tv.size);
    x += gfx_text(c, x, TV_HDR_TY, size, TV_FONT_LABEL, UI_TEXT_FAINT, 1)
         + 16;
    /* READ-ONLY in the viewer; EDITING, or MODIFIED in the warning colour
     * while the text differs from the card, in the editor. */
    const bool dirty = text_view_dirty();
    const char *ro = !tv.edit ? TR(ED_READ_ONLY)
                     : dirty  ? TR(ED_MODIFIED)
                              : TR(ED_EDITING);
    const int pw = UI_PILL_INSET + gfx_text_width(TV_FONT_LABEL, ro, 1) + 12;
    ui_pill_font(c, gfx_rect_make(x, TV_HDR_Y + (TV_HDR_H - TV_PILL_H) / 2,
                                  pw, TV_PILL_H),
                 ro, dirty ? UI_WARN : UI_TEXT_DIM, UI_PANEL_SUNK,
                 UI_PILL_INSET, TV_FONT_LABEL);
    const size_t lines = text_view_lines();
    const gfx_rect_t right = gfx_rect_make(TV_L, TV_HDR_Y, TV_W - 8, TV_HDR_H);
    if (tv.kb.open && tv.note != TV_NOTE_NONE) {
        /* The keyboard covers the footer: why a key typed nothing shows
         * here. */
        char note[96];
        gfx_text_in(c, right, text_view_note(note, sizeof(note)),
                    TV_FONT_FINE, UI_WARN, 1, GFX_ALIGN_RIGHT);
    } else if (lines > 0u) {
        /* The top line in the viewer, the cursor's in the editor. */
        size_t line = tv.top_line;
        if (tv.edit) {
            (void)text_view_cursor(&line);
        }
        char at[48];
        snprintf(at, sizeof(at), TR(ED_LINE_OF), (unsigned)(line + 1u),
                 (unsigned)lines);
        gfx_text_in(c, right, at, TV_FONT_LABEL, UI_TEXT_DIM, 1,
                    GFX_ALIGN_RIGHT);
    }
}

/**
 * @brief The sentence in place of the text: reading, empty, or why the
 *        file did not open.
 * @param out  takes it
 * @param n    bytes at @p out
 */
static void sentence(char *out, size_t n)
{
    out[0] = '\0';
    switch (tv.state) {
    case TEXT_VIEW_LOADING:
        snprintf(out, n, "%s", TR(ED_READING));
        break;
    case TEXT_VIEW_FAILED:
        if (tv.err == FM_ERR_TOO_BIG || tv.tb_err == TEXT_BUF_TOO_LARGE) {
            snprintf(out, n, "%s", TR(ED_TOO_LARGE));
        } else if (tv.tb_err == TEXT_BUF_NOT_TEXT) {
            snprintf(out, n, "%s", TR(ED_NOT_TEXT));
        } else if (tv.tb_err == TEXT_BUF_TOO_MANY) {
            snprintf(out, n, TR(ED_TOO_MANY_LINES),
                     (unsigned)TEXT_BUF_ENDS_MAX);
        } else {
            files_err_text(tv.err, tv.name, out, n);
        }
        break;
    case TEXT_VIEW_SHOWN:
        if (text_view_lines() == 0u) {
            snprintf(out, n, "%s", TR(ED_EMPTY));
        }
        break;
    case TEXT_VIEW_CLOSED:
    default:
        break;
    }
}

/**
 * @brief The scroll bar: the track, and the thumb in the accent colour
 *        while the text is longer than a screen.
 * @param c  the canvas
 */
static void draw_bar(gfx_canvas_t *c)
{
    const gfx_rect_t t = tv_bar();
    gfx_fill_chamfer_rect(c, t.x, t.y, t.w, t.h, UI_CHAMFER_SM, UI_PANEL_SUNK);
    int ty;
    int th;
    tv_bar_thumb(&ty, &th);
    const bool scrolls = !(text_view_at_start() && text_view_at_end());
    gfx_fill_chamfer_rect(c, t.x + 4, ty + 2, t.w - 8, th - 4, UI_CHAMFER_SM,
                          scrolls ? UI_ACCENT : UI_PANEL_HI);
}

/** @brief One footer button: where, its words, whether it acts, its hit. */
typedef struct {
    gfx_rect_t r;       /**< where                                     */
    const char *label;  /**< its words                                 */
    bool live;          /**< it would change something                 */
    int hit;            /**< the tv_hit_t a press on it holds          */
} foot_btn_t;

/**
 * @brief The footer: SMALL or NORMAL and EDIT in the viewer, SAVE, REVERT
 *        and KEYBOARD in the editor, PAGE UP and PAGE DOWN, each dimmed
 *        where it would change nothing; the note between, or MIXED LINE
 *        ENDS.
 * @param c  the canvas
 */
static void draw_footer(gfx_canvas_t *c)
{
    const bool shown = (tv.state == TEXT_VIEW_SHOWN);
    const bool dirty = text_view_dirty();
    foot_btn_t b[5];
    int n = 0;
    if (!tv.edit) {
        b[n++] = (foot_btn_t){ TV_SMALL, tv.small ? TR(ED_NORMAL)
                                                  : TR(ED_SMALL),
                               true, TV_HIT_SMALL };
        b[n++] = (foot_btn_t){ TV_EDIT, TR(ED_EDIT),
                               shown && tv.edit_err == FM_OK, TV_HIT_EDIT };
    } else {
        b[n++] = (foot_btn_t){ TV_SAVE, TR(SET_SAVE),
                               dirty && !tv.saving && tv.save_err == FM_OK,
                               TV_HIT_SMALL };
        b[n++] = (foot_btn_t){ TV_REVERT, TR(ED_REVERT),
                               dirty && !tv.saving, TV_HIT_EDIT };
        b[n++] = (foot_btn_t){ TV_KEYS, TR(ED_KEYBOARD), true, TV_HIT_KEYS };
    }
    b[n++] = (foot_btn_t){ TV_PAGE_UP, TR(ED_PAGE_UP),
                           shown && !text_view_at_start(), TV_HIT_PAGE_UP };
    b[n++] = (foot_btn_t){ TV_PAGE_DN, TR(ED_PAGE_DOWN),
                           shown && !text_view_at_end(), TV_HIT_PAGE_DN };
    for (int k = 0; k < n; ++k) {
        ui_button_font(c, b[k].r, b[k].label, UI_PANEL_HI,
                       tv.press == b[k].hit, b[k].live,
                       ui_button_face(b[k].label, b[k].r.w, b[k].r.h,
                                      TV_FOOT_PAD, TV_FONT_LABEL,
                                      TV_FONT_FINE));
    }
    char note[192];
    (void)text_view_note(note, sizeof(note));
    const gfx_rect_t box = tv.edit ? TV_NOTE : TV_WARN;
    if (note[0] != '\0') {
        const bool bad = tv.note == TV_NOTE_ERR
                         || (tv.note == TV_NOTE_NONE)    /* SAVE shut */
                         || tv.note_id == TX_FM_E_STOP_ARMED
                         || tv.note_id == TX_FM_E_STOP_LOGGING
                         || tv.note_id == TX_ED_FULL
                         || tv.note_id == TX_ED_LINES_FULL;
        (void)ui_text_wrap(c, gfx_rect_make(box.x, box.y + 2, box.w, 16), 16,
                           note, TV_FONT_FINE, bad ? UI_WARN : UI_TEXT_DIM);
    } else if (shown && text_buf_mixed(&tv_tb)) {
        gfx_text_in(c, box, TR(ED_MIXED_ENDS), TV_FONT_LABEL, UI_WARN, 1,
                    GFX_ALIGN_LEFT);
    }
}

/**
 * @brief The cursor: a bar 2px wide before its cell, a row high, in the
 *        accent colour; nothing while it is off the screen.
 * @param c  the canvas
 */
static void draw_cursor(gfx_canvas_t *c)
{
    int cell = 0;
    const int k = tv_cursor_at(&cell);
    if (k < 0) {
        return;
    }
    const int cw = text_view_small() ? 8 : 10;
    gfx_fill_rect(c, TV_TEXT_X + cell * cw - 1, TV_TEXT_Y + k * tv_pitch(), 2,
                  tv_pitch(), UI_ACCENT);
}

/**
 * @brief The question: UNSAVED CHANGES, what is lost and how, STAY and
 *        DISCARD.
 * @param c  the canvas
 */
static void draw_question(gfx_canvas_t *c)
{
    const gfx_rect_t box = TV_Q_BOX;
    ui_panel(c, box, TR(SET_LEAVE_TITLE), UI_WARN);
    char what[256];
    switch (tv.ask) {
    case TEXT_VIEW_ASK_REVERT:
        snprintf(what, sizeof(what), TR(ED_Q_REVERT), tv.name);
        break;
    case TEXT_VIEW_ASK_OPEN:
        snprintf(what, sizeof(what), TR(ED_Q_OPEN), tv.name);
        break;
    case TEXT_VIEW_ASK_BACK:
    case TEXT_VIEW_ASK_NONE:
    default:
        snprintf(what, sizeof(what), TR(ED_Q_LEAVE), tv.name);
        break;
    }
    (void)ui_text_wrap(c, gfx_rect_make(box.x + 24, box.y + 52, box.w - 48,
                                        24),
                       30, what, TV_FONT_BODY, UI_TEXT);
    ui_button_font(c, TV_Q_STAY, TR(SET_STAY), UI_PANEL_HI,
                   tv.press == TV_HIT_STAY, true, TV_FONT_LABEL);
    ui_button_font(c, TV_Q_DISCARD, TR(SET_DISCARD), UI_WARN,
                   tv.press == TV_HIT_DISCARD, true, TV_FONT_LABEL);
}

void text_view_render(gfx_canvas_t *c)
{
    gfx_fill_rect(c, 0, 0, UI_PANEL_W, UI_BODY_H, UI_BG);
    draw_header(c);
    ui_panel(c, tv_area(), NULL, UI_EDGE);
    char line[192];
    sentence(line, sizeof(line));
    if (line[0] != '\0') {
        const gfx_color_t col = (tv.state == TEXT_VIEW_FAILED) ? UI_DANGER
                                                               : UI_TEXT_DIM;
        (void)ui_text_wrap(c, gfx_rect_make(TV_TEXT_X, TV_TEXT_Y + 8,
                                            TV_TEXT_R - TV_TEXT_X, 24),
                           28, line, TV_FONT_BODY, col);
    } else if (tv.state == TEXT_VIEW_SHOWN) {
        draw_bar(c);
        if (tv.edit) {
            draw_cursor(c);         /* under the text: a glyph stays whole */
        }
        (void)tv_walk(draw_run, c);
    }
    if (tv.kb.open) {
        ui_textkey_render(&tv.kb, c);
    } else {
        draw_footer(c);
    }
    if (tv.ask != TEXT_VIEW_ASK_NONE) {
        draw_question(c);
    }
}
