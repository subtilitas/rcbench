/**
 * @file text_view_keys.c
 * @brief The text editor on the viewer: EDIT, the keyboard's keys into the
 *        text, the cursor, SAVE's preparation and answer, REVERT and the
 *        question over unsaved changes (text_view.h).  text_view.c holds
 *        the state and the viewer's touch handling; text_view_int.h is
 *        what they share.
 *
 * The cursor is the text buffer's (text_buf.h): a key types at it and it
 * moves a unit with "<-" and "->".  After every key the screen follows
 * the cursor: a cursor above the top row becomes the top row, one below
 * the last row becomes the last row.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "files_screen.h"
#include "text_view.h"
#include "ui_text.h"

#include "text_view_int.h"

/** @brief The first line of a KST file, up to its format number
 *         (kst_file.h). */
static const char k_kst_head[] = "KST servo configuration, format ";
/** @brief The first word of a KST file's checksum line. */
static const char k_kst_sum[] = "checksum";

/** @brief DISCARD on the host's question: TEXT_VIEW_LEAVE is to be taken. */
static bool s_leave;
/** @brief The save under way took a KST file's checksum line out. */
static bool s_kst_cut;

/* ------------------------------------------------------------- the rules */

bool text_view_set_rules(fm_err_t edit, fm_err_t save)
{
    if (edit == tv.edit_err && save == tv.save_err) {
        return false;
    }
    /* A note of the rule that held SAVE goes with it. */
    if (tv.note == TV_NOTE_ERR && tv.note_err == tv.save_err
        && save != tv.save_err) {
        tv.note = TV_NOTE_NONE;
    }
    tv.edit_err = edit;
    tv.save_err = save;
    ++tv.rev;
    return true;
}

bool text_view_editing(void)
{
    return tv.edit;
}

bool text_view_keyboard(void)
{
    return tv.kb.open;
}

bool text_view_dirty(void)
{
    return tv.edit && text_buf_dirty(&tv_tb);
}

bool text_view_saving(void)
{
    return tv.saving;
}

text_view_ask_t text_view_ask(void)
{
    return tv.ask;
}

size_t text_view_cursor(size_t *line)
{
    const size_t pos = text_buf_cursor(&tv_tb);
    if (line != NULL) {
        *line = text_buf_line_of(&tv_tb, pos);
    }
    return pos;
}

/* ------------------------------------------------------------ the cursor */

/**
 * @brief The cursor's line and its row in that line.
 * @param row   takes the row
 * @param col   takes the column in the row, 0 to the row's width
 * @return the line
 */
static size_t cursor_place(size_t *row, size_t *col)
{
    const unsigned width = text_view_cells();
    size_t line = 0u;
    size_t cell = 0u;
    text_buf_cell_of(&tv_tb, text_buf_cursor(&tv_tb), width, &line, &cell);
    size_t r = cell / width;
    /* At the end of a full row the cursor stands after its last cell. */
    if (r >= tv_rows_of(line)) {
        r = tv_rows_of(line) - 1u;
    }
    *row = r;
    *col = cell - r * width;
    return line;
}

int tv_cursor_at(int *cell)
{
    size_t row = 0u;
    size_t col = 0u;
    const size_t line = cursor_place(&row, &col);
    for (int k = 0; k < text_view_rows(); ++k) {
        size_t r = 0u;
        if (tv_line_at(k, &r) == line && r == row) {
            *cell = (int)col;
            return k;
        }
    }
    return -1;
}

/** @brief Scroll the least that shows the cursor's row. */
static void show_cursor(void)
{
    size_t row = 0u;
    size_t col = 0u;
    const size_t line = cursor_place(&row, &col);
    if (line < tv.top_line || (line == tv.top_line && row < tv.top_row)) {
        tv.top_line = line;
        tv.top_row = row;
        tv_clamp_top();
        return;
    }
    const int rows = text_view_rows();
    size_t lr = 0u;
    const size_t ll = tv_line_at(rows - 1, &lr);
    if (line < ll || (line == ll && row <= lr)) {
        return;                     /* on the screen */
    }
    tv.top_line = line;
    tv.top_row = row;
    text_view_scroll(-(long)(rows - 1));
}

/**
 * @brief A tap on the text: the cursor to the nearest cell of the row
 *        under it, or to the text's end under the last row.
 * @param x  the tap, body coordinates
 * @param y  the tap
 */
static void tap_cursor(int x, int y)
{
    const int pitch = tv_pitch();
    const int cw = text_view_small() ? 8 : 10;
    const unsigned width = text_view_cells();
    int k = (y - TV_TEXT_Y) / pitch;
    k = (k < 0) ? 0 : (k >= text_view_rows()) ? text_view_rows() - 1 : k;
    size_t row = 0u;
    const size_t line = tv_line_at(k, &row);
    if (line >= text_view_lines()) {
        text_buf_set_cursor(&tv_tb, text_buf_len(&tv_tb));
    } else {
        int col = (x - TV_TEXT_X + cw / 2) / cw;
        col = (col < 0) ? 0 : (col > (int)width) ? (int)width : col;
        text_buf_set_cursor(&tv_tb,
                            text_buf_pos_at(&tv_tb, line,
                                            row * width + (size_t)col,
                                            width));
    }
    ++tv.rev;
}

/* ------------------------------------------------------------- the editor */

/**
 * @brief Keep the text as it is now as the file on the card: what DISCARD
 *        puts back.
 */
static void keep_original(void)
{
    tv.orig_len = text_buf_read(&tv_tb, 0u, tv_orig, TEXT_BUF_FILE_MAX);
}

/** @brief Open the keyboard, the cursor's row on the smaller screen. */
static void keyboard_open(void)
{
    ui_textkey_open_editor(&tv.kb, TV_KB);
    tv_clamp_top();
    show_cursor();
    ++tv.rev;
}

bool text_view_edit(void)
{
    if (tv.state != TEXT_VIEW_SHOWN || tv.edit_err != FM_OK || tv.edit) {
        return false;
    }
    keep_original();
    tv.edit = true;
    tv.note = TV_NOTE_NONE;
    /* The cursor starts on the top row when it is off the screen. */
    int cell = 0;
    if (tv_cursor_at(&cell) < 0) {
        text_buf_set_cursor(&tv_tb, text_buf_line_start(&tv_tb, tv.top_line));
    }
    keyboard_open();
    return true;
}

/** @brief The text back as the file on the card; the editor stays on. */
static void discard(void)
{
    (void)text_buf_load(&tv_tb, tv_orig, tv.orig_len);
    text_buf_set_cursor(&tv_tb, text_buf_line_start(&tv_tb, tv.top_line));
    tv_clamp_top();
    tv.note = TV_NOTE_NONE;
    ++tv.rev;
}

/** @brief The editor off, the viewer on the same text. */
static void edit_leave(void)
{
    ui_textkey_close(&tv.kb);
    tv.edit = false;
    tv.note = TV_NOTE_NONE;
    tv_clamp_top();
    ++tv.rev;
}

bool text_view_back(void)
{
    if (tv.ask != TEXT_VIEW_ASK_NONE) {
        tv.ask = TEXT_VIEW_ASK_NONE;          /* as STAY */
        ++tv.rev;
        return true;
    }
    if (tv.kb.open) {
        ui_textkey_close(&tv.kb);
        tv_clamp_top();
        ++tv.rev;
        return true;
    }
    if (!tv.edit) {
        return false;
    }
    if (tv.saving) {
        return true;                          /* the save finishes first */
    }
    if (text_buf_dirty(&tv_tb)) {
        tv.ask = TEXT_VIEW_ASK_BACK;
        ++tv.rev;
        return true;
    }
    edit_leave();
    return true;
}

void text_view_ask_open(void)
{
    text_view_cancel_press();
    ui_textkey_close(&tv.kb);
    tv_clamp_top();
    tv.ask = TEXT_VIEW_ASK_OPEN;
    ++tv.rev;
}

/**
 * @brief DISCARD on the question that shows.
 */
static void answer_discard(void)
{
    const text_view_ask_t ask = tv.ask;
    tv.ask = TEXT_VIEW_ASK_NONE;
    discard();
    if (ask == TEXT_VIEW_ASK_BACK) {
        edit_leave();
    } else if (ask == TEXT_VIEW_ASK_OPEN) {
        edit_leave();
        s_leave = true;
    }
}

text_view_result_t tv_edit_take(void)
{
    if (!s_leave) {
        return TEXT_VIEW_NONE;
    }
    s_leave = false;
    return TEXT_VIEW_LEAVE;
}

/* ------------------------------------------------------------- the keys */

/**
 * @brief Say why a key typed nothing.
 * @param r  the buffer's answer
 */
static void refused(text_buf_err_t r)
{
    if (r == TEXT_BUF_TOO_MANY) {
        tv_say(TX_ED_LINES_FULL);
    } else if (r == TEXT_BUF_FULL) {
        tv_say(TX_ED_FULL);
    }
}

/**
 * @brief A key's answer into the text.
 * @param r  the keyboard's answer
 */
static void key_acts(ui_textkey_result_t r)
{
    if (r == UI_TEXTKEY_NONE) {
        return;
    }
    if (r == UI_TEXTKEY_DONE) {
        tv_clamp_top();
        ++tv.rev;
        return;
    }
    if (tv.saving) {
        return;                     /* the text holds still while saved */
    }
    text_buf_err_t err = TEXT_BUF_OK;
    switch (r) {
    case UI_TEXTKEY_TYPED:
        err = text_buf_insert(&tv_tb, (uint8_t)tv.kb.typed);
        break;
    case UI_TEXTKEY_ENTER:
        err = text_buf_insert_eol(&tv_tb);
        break;
    case UI_TEXTKEY_BACKSPACE:
        (void)text_buf_delete_before(&tv_tb);
        break;
    case UI_TEXTKEY_LEFT:
        (void)text_buf_left(&tv_tb);
        break;
    case UI_TEXTKEY_RIGHT:
    default:
        (void)text_buf_right(&tv_tb);
        break;
    }
    if (err != TEXT_BUF_OK) {
        refused(err);
    } else {
        tv.note = TV_NOTE_NONE;
    }
    show_cursor();
    ++tv.rev;
}

/**
 * @brief An event on the question: STAY and DISCARD act on the release of
 *        the contact that pressed them, over them and within the slop.
 * @param e  the event
 */
static void question_event(const touch_event_t *e)
{
    const int x = e->point.x;
    const int y = e->point.y;
    if (e->type == TOUCH_EVENT_DOWN) {
        if (tv.tap_have && e->point.id != tv.tap_id) {
            return;
        }
        tv.tap_have = true;
        tv.tap_moved = false;
        tv.tap_id = e->point.id;
        tv.tap_x0 = e->point.x;
        tv.tap_y0 = e->point.y;
        tv.press = gfx_rect_contains(TV_Q_STAY, x, y)      ? TV_HIT_STAY
                   : gfx_rect_contains(TV_Q_DISCARD, x, y) ? TV_HIT_DISCARD
                                                           : TV_HIT_NONE;
        return;
    }
    if (!tv.tap_have || e->point.id != tv.tap_id) {
        return;
    }
    const int dx = x - tv.tap_x0;
    const int dy = y - tv.tap_y0;
    if (dx > TV_SLOP || -dx > TV_SLOP || dy > TV_SLOP || -dy > TV_SLOP) {
        tv.tap_moved = true;
    }
    if (e->type != TOUCH_EVENT_UP) {
        return;
    }
    const int hit = tv.press;
    tv.tap_have = false;
    tv.press = TV_HIT_NONE;
    if (!touch_event_is_tap_up(e) || tv.tap_moved) {
        return;
    }
    if (hit == TV_HIT_STAY && gfx_rect_contains(TV_Q_STAY, x, y)) {
        tv.ask = TEXT_VIEW_ASK_NONE;
        ++tv.rev;
    } else if (hit == TV_HIT_DISCARD
               && gfx_rect_contains(TV_Q_DISCARD, x, y)) {
        answer_discard();
    }
}

bool tv_edit_event(const touch_event_t *e)
{
    if (tv.ask != TEXT_VIEW_ASK_NONE) {
        question_event(e);
        return true;
    }
    if (!tv.kb.open) {
        return false;
    }
    const bool mine = (e->type == TOUCH_EVENT_DOWN)
                          ? gfx_rect_contains(TV_KB, e->point.x, e->point.y)
                          : (tv.kb.pressed >= 0
                             && e->point.id == tv.kb.press_id);
    if (!mine) {
        return false;
    }
    key_acts(ui_textkey_event(&tv.kb, e, NULL, 0u));
    return true;
}

void tv_edit_tap(int x, int y)
{
    tap_cursor(x, y);
}

text_view_result_t tv_edit_button(int hit)
{
    switch (hit) {
    case TV_HIT_SMALL:                        /* SAVE */
        if (tv.saving) {
            return TEXT_VIEW_NONE;
        }
        if (!text_buf_dirty(&tv_tb)) {
            tv_say(TX_ED_NO_CHANGES);
            return TEXT_VIEW_NONE;
        }
        if (tv.save_err != FM_OK) {
            tv_say_err(tv.save_err);
            return TEXT_VIEW_NONE;
        }
        return TEXT_VIEW_SAVE;
    case TV_HIT_EDIT:                         /* REVERT */
        if (!tv.saving && text_buf_dirty(&tv_tb)) {
            tv.ask = TEXT_VIEW_ASK_REVERT;
            ++tv.rev;
        }
        return TEXT_VIEW_NONE;
    case TV_HIT_KEYS:
    default:
        keyboard_open();
        return TEXT_VIEW_NONE;
    }
}

/* -------------------------------------------------------------- the save */

/**
 * @brief Whether line @p line starts with the word "checksum", after
 *        spaces and tabs.
 * @param line  the line
 * @return true for a checksum line
 */
static bool is_sum_line(size_t line)
{
    size_t len = 0u;
    const char *b = text_buf_line(&tv_tb, line, &len);
    size_t k = 0u;
    while (b != NULL && k < len && (b[k] == ' ' || b[k] == '\t')) {
        ++k;
    }
    const size_t w = sizeof(k_kst_sum) - 1u;
    return b != NULL && len - k >= w && memcmp(b + k, k_kst_sum, w) == 0
           && (len - k == w || b[k + w] == ' ' || b[k + w] == '\t');
}

/**
 * @brief Take a KST file's checksum line out, with its line end.
 * @return true when the text is a KST file that held one
 */
static bool kst_cut(void)
{
    size_t len = 0u;
    const char *head = text_buf_line(&tv_tb, 0u, &len);
    const size_t hn = sizeof(k_kst_head) - 1u;
    if (head == NULL || len < hn || memcmp(head, k_kst_head, hn) != 0) {
        return false;
    }
    const size_t lines = text_buf_lines(&tv_tb);
    for (size_t i = 1u; i < lines; ++i) {
        if (!is_sum_line(i)) {
            continue;
        }
        const size_t from = text_buf_line_start(&tv_tb, i);
        const size_t to = (i + 1u < lines) ? text_buf_line_start(&tv_tb, i + 1u)
                                           : text_buf_len(&tv_tb);
        size_t cur = text_buf_cursor(&tv_tb);
        const size_t end = text_buf_len(&tv_tb) - (to - from);
        text_buf_set_cursor(&tv_tb, from);
        bool more = true;
        while (more && text_buf_len(&tv_tb) > end) {
            more = text_buf_delete_after(&tv_tb);   /* one unit a call */
        }
        cur = (cur >= to) ? cur - (to - from) : (cur > from) ? from : cur;
        text_buf_set_cursor(&tv_tb, cur);
        tv_clamp_top();
        return true;
    }
    return false;
}

size_t text_view_save_len(void)
{
    s_kst_cut = text_buf_dirty(&tv_tb) && kst_cut();
    tv.saving = true;
    tv_say(TX_ED_SAVING);
    return text_buf_size(&tv_tb);
}

size_t text_view_fill(void *ctx, uint64_t at, uint8_t *dst, size_t cap)
{
    (void)ctx;
    if (at >= (uint64_t)text_buf_size(&tv_tb)) {
        return 0u;
    }
    return text_buf_read(&tv_tb, (size_t)at, dst, cap);
}

void text_view_saved(fm_err_t err, bool stopped, bool restart)
{
    tv.saving = false;
    if (err == FM_OK) {
        text_buf_saved(&tv_tb);
        keep_original();
        tv_say(s_kst_cut ? TX_ED_SAVED_KST
               : restart ? TX_ED_SAVED_ESC
                         : TX_ED_SAVED);
    } else if (stopped && err == FM_ERR_ARMED) {
        tv_say(TX_FM_E_STOP_ARMED);
    } else if (stopped && err == FM_ERR_LOGGING) {
        tv_say(TX_FM_E_STOP_LOGGING);
    } else if (err == FM_ERR_CARD_GONE) {
        tv_say(TX_ED_NOT_SAVED_CARD);
    } else {
        tv_say_err(err);
    }
    s_kst_cut = false;
}

const char *text_view_note(char *out, size_t n)
{
    if (out == NULL || n == 0u) {
        return "";
    }
    out[0] = '\0';
    if (tv.note == TV_NOTE_ERR && tv.note_err == FM_ERR_EXISTS) {
        /* A save finds "<name>.NEW" there: a save cut short, kept. */
        char tmp[FM_NAME_BUF + 4];
        snprintf(tmp, sizeof(tmp), "%s.NEW", tv.name);
        files_err_text(FM_ERR_EXISTS, tmp, out, n);
    } else if (tv.note == TV_NOTE_ERR) {
        files_err_text(tv.note_err, tv.name, out, n);
    } else if (tv.note == TV_NOTE_TEXT) {
        /* Each format by its own ID, so tools/check_formats.py sees it. */
        if (tv.note_id == TX_ED_SAVED) {
            snprintf(out, n, TR(ED_SAVED), tv.name);
        } else if (tv.note_id == TX_ED_LINES_FULL) {
            snprintf(out, n, TR(ED_LINES_FULL), (unsigned)TEXT_BUF_ENDS_MAX);
        } else {
            snprintf(out, n, "%s", ui_tr(tv.note_id));
        }
    } else if (tv.edit && tv.save_err != FM_OK && text_buf_dirty(&tv_tb)) {
        files_err_text(tv.save_err, tv.name, out, n);   /* why SAVE is shut */
    }
    return out;
}
