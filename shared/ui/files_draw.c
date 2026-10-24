/**
 * @file files_draw.c
 * @brief FILES' drawing: the two panes with their headers, rows and scroll
 *        bars, the message line and the footer, NEW's choice, the DELETE
 *        question, the INFO box, the progress box and the keyboard.
 *        files_screen.c holds the state; files_int.h is what the screen's
 *        three files share.
 *
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "files_screen.h"
#include "fm_name.h"
#include "log_name.h"
#include "ui_card_text.h"
#include "ui_icons.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "wall_clock.h"

#include "files_int.h"

const char *fs_kind_word(int kind)
{
    switch ((fm_kind_t)kind) {
    case FM_KIND_FOLDER:  return TR(FM_KIND_FOLDER);
    case FM_KIND_RUN:     return TR(FM_KIND_RUN);
    case FM_KIND_REPORT:  return TR(FM_KIND_REPORT);
    case FM_KIND_KST:     return TR(FM_KIND_KST);
    case FM_KIND_PROFILE: return TR(FM_KIND_PROFILE);
    case FM_KIND_FAULT:   return TR(FM_KIND_FAULT);
    case FM_KIND_CSV:     return TR(FM_KIND_CSV);
    case FM_KIND_JSON:    return TR(FM_KIND_JSON);
    case FM_KIND_TEXT:    return TR(FM_KIND_TEXT);
    case FM_KIND_OTHER:
    case FM_KIND_COUNT:
    default:              return TR(FM_KIND_OTHER);
    }
}

/**
 * @brief @p src cut to @p cells cells: whole when it fits, else its first
 *        cells and "..", never inside a character.
 * @param src    the text
 * @param cells  the cells it may take, 3 or more
 * @param out    takes the text
 * @param n      bytes at @p out
 */
static void cut_cells(const char *src, int cells, char *out, size_t n)
{
    if (gfx_text_cells(src) <= cells) {
        snprintf(out, n, "%s", src);
        return;
    }
    const size_t keep = gfx_text_prefix(src, cells - 2);
    snprintf(out, n, "%.*s..", (int)keep, src);
}

/**
 * @brief @p src cut from its front to @p cells cells: ".." and its end.
 * @param src    the text
 * @param cells  the cells it may take, 3 or more
 * @param out    takes the text
 * @param n      bytes at @p out
 */
static void tail_cells(const char *src, int cells, char *out, size_t n)
{
    const int have = gfx_text_cells(src);
    if (have <= cells) {
        snprintf(out, n, "%s", src);
        return;
    }
    const size_t skip = gfx_text_prefix(src, have - (cells - 2));
    snprintf(out, n, "..%s", src + skip);
}

/* ---------------------------------------------------------------- panes */

/**
 * @brief A pane's header row: UP with the folder's path, PREV and NEXT
 *        while the folder holds more than its page.
 * @param c  the canvas
 * @param i  the pane
 */
static void draw_header(gfx_canvas_t *c, int i)
{
    const fm_tree_t *t = &s.pane[i];
    const gfx_rect_t up = FS_UP(i);
    const bool root = (t->dir[0] == '\0');
    ui_button_font(c, up, NULL, UI_PANEL_HI, s.press == FS_HIT_UP + i, !root,
                   FS_FONT_LABEL);
    char path[FM_PATH_SIZE + 4];
    if (root) {
        const char *vol = (s.have_io && s.io.volume != NULL)
                              ? s.io.volume(s.io.ctx) : "";
        snprintf(path, sizeof(path), "%s",
                 (vol != NULL && vol[0] != '\0') ? vol : TR(LOG_SD_CARD));
    } else {
        tail_cells(t->dir, (up.w - 40 - 8) / 10, path, sizeof(path));
        ui_chevron_left(c, up.x + 14, up.y + up.h / 2, 8,
                        (i == s.active) ? UI_ACCENT : UI_TEXT_DIM);
    }
    gfx_text_in(c, gfx_rect_make(up.x + 32, up.y, up.w - 40, up.h), path,
                FS_FONT_LABEL, root ? UI_TEXT_DIM : UI_TEXT, 1,
                GFX_ALIGN_LEFT);
    if (fs_paged(i)) {
        ui_button_font(c, FS_PREV(i), TR(LOG_PREV), UI_PANEL_HI,
                       s.press == FS_HIT_PREV + i, fm_tree_has_prev(t),
                       FS_FONT_LABEL);
        ui_button_font(c, FS_NEXT(i), TR(LOG_NEXT), UI_PANEL_HI,
                       s.press == FS_HIT_NEXT + i, fm_tree_has_next(t),
                       FS_FONT_LABEL);
    }
}

/**
 * @brief A pane's heading: how many entries the folder holds and which of
 *        them the page shows, and how many were not listed.
 * @param i    the pane
 * @param out  takes the heading
 * @param n    bytes at @p out
 */
static void heading(int i, char *out, size_t n)
{
    const fm_tree_t *t = &s.pane[i];
    if (!s.read[i]) {
        snprintf(out, n, "%s", TR(FM_READING));
        return;
    }
    if (t->total < 0) {
        snprintf(out, n, "%s", TR(LOG_SD_CARD));
        return;
    }
    int used;
    if (fs_paged(i)) {
        used = snprintf(out, n, TR(FM_ENTRIES_OF), t->ahead + 1,
                        t->ahead + t->held, t->total);
    } else {
        used = (t->total == 1) ? snprintf(out, n, "%s", TR(FM_ENTRY_ONE))
                               : snprintf(out, n, TR(FM_ENTRIES), t->total);
    }
    if (t->unlisted > 0u && used >= 0 && (size_t)used < n) {
        snprintf(out + used, n - (size_t)used, TR(FM_UNLISTED), t->unlisted);
    }
}

/**
 * @brief The lines a pane with no rows shows in their place.
 * @param c  the canvas
 * @param i  the pane
 */
static void draw_empty(gfx_canvas_t *c, int i)
{
    const fm_tree_t *t = &s.pane[i];
    if (!s.read[i]) {
        return;
    }
    const char *a;
    if (t->total < 0) {
        const card_state_t st = (s.have_io && s.io.card != NULL)
                                    ? s.io.card(s.io.ctx) : CARD_STATE_NONE;
        a = ui_card_text(st, UI_CARD_AT_LOGS);
    } else if (s.pane_err[i] == FM_ERR_NOT_FOUND) {
        a = TR(FM_NO_FOLDER);
    } else {
        /* A folder of only hidden entries is not empty: DELETE refuses
         * it, and the line says what it holds. */
        a = (t->unlisted > 0u) ? TR(FM_HIDDEN) : TR(FM_EMPTY);
    }
    const gfx_rect_t list = FS_LIST(i);
    const gfx_rect_t first = gfx_rect_make(list.x + 16, list.y + FS_HEAD_H + 24,
                                           FS_ROW_W, 24);
    (void)ui_text_wrap(c, first, 26, a, FS_FONT_BODY, UI_TEXT);
}

/**
 * @brief The word in the warning colour after an entry's kind: the logger
 *        writes it, the servo programmer's record, a folder the bench
 *        keeps; NULL for none.
 * @param e  the entry
 * @return the word
 */
static const char *state_word(const fm_entry_t *e)
{
    if (e->busy) {
        return TR(FM_WRITING);
    }
    switch ((fm_place_t)e->place) {
    case FM_PLACE_LOCKED: return TR(FM_LOCKED);
    case FM_PLACE_FIXED:  return TR(FM_FIXED);
    case FM_PLACE_FAULT:
    case FM_PLACE_FREE:
    default:              return NULL;
    }
}

/**
 * @brief One row: the icon, the name and its kind line, the size.
 * @param c    the canvas
 * @param i    the pane
 * @param idx  the entry's index on the page
 * @param y    the row's top
 */
static void draw_row(gfx_canvas_t *c, int i, int idx, int y)
{
    const fm_entry_t *e = &s.pane[i].page[idx];
    const bool on = (idx == s.pane[i].selected);
    const bool act = (i == s.active);
    const gfx_rect_t row = gfx_rect_make(FS_PANE_X(i) + FS_ROW_IN, y,
                                         FS_ROW_W, FS_ROW_H - 4);
    gfx_fill_chamfer_rect(c, row.x, row.y, row.w, row.h, UI_CHAMFER_SM,
                          on ? UI_PANEL_HI : UI_PANEL_SUNK);
    if (on) {
        gfx_fill_rect(c, row.x, row.y, 4, row.h,
                      act ? UI_ACCENT : UI_TEXT_FAINT);
    }
    const bool locked = (e->place == FM_PLACE_LOCKED) || e->busy;
    const gfx_color_t ic = locked ? UI_WARN
                           : e->is_dir ? UI_ACCENT : UI_TEXT_DIM;
    (e->is_dir ? ui_icon_folder : ui_icon_file)(c, row.x + 8 + FS_ICON / 2,
                                                row.y + row.h / 2, FS_ICON,
                                                ic);
    const int tx = row.x + 8 + FS_ICON + 8;
    const int cells = (row.x + row.w - 8 - FS_SIZE_W - 8 - tx) / 10;
    char name[FM_NAME_BUF + 4];
    cut_cells(e->name, cells, name, sizeof(name));
    gfx_text(c, tx, row.y + 3, name, FS_FONT_LABEL,
             on ? UI_TEXT : UI_TEXT_DIM, 1);
    const char *kind = fs_kind_word(e->kind);
    const int kx = tx + gfx_text(c, tx, row.y + 25, kind, FS_FONT_FINE,
                                 UI_TEXT_FAINT, 1);
    const char *word = state_word(e);
    if (word != NULL) {
        gfx_text(c, kx + 16, row.y + 25, word, FS_FONT_FINE, UI_WARN, 1);
    }
    if (!e->is_dir) {
        char size[24];
        files_size_text(size, sizeof(size), e->size);
        gfx_text_in(c, gfx_rect_make(row.x + row.w - 8 - FS_SIZE_W, row.y + 3,
                                     FS_SIZE_W, 20),
                    size, FS_FONT_LABEL, UI_TEXT_FAINT, 1, GFX_ALIGN_RIGHT);
    }
}

/**
 * @brief Pane @p i's scroll bar.
 * @param c  the canvas
 * @param i  the pane
 */
static void draw_bar(gfx_canvas_t *c, int i)
{
    const gfx_rect_t t = FS_BAR(i);
    gfx_fill_chamfer_rect(c, t.x, t.y, t.w, t.h, UI_CHAMFER_SM, UI_PANEL_SUNK);
    int ty;
    int th;
    fs_bar_thumb(i, &ty, &th);
    const gfx_color_t col = (s.pane[i].held > FS_ROWS) ? UI_ACCENT
                                                        : UI_PANEL_HI;
    gfx_fill_chamfer_rect(c, t.x + 4, ty + 2, t.w - 8, th - 4, UI_CHAMFER_SM,
                          col);
}

/**
 * @brief One pane: its header, its list's panel and rows, its bar.  The
 *        active pane's heading is an accent tag; the other's is dim text.
 * @param c  the canvas
 * @param i  the pane
 */
static void draw_pane(gfx_canvas_t *c, int i)
{
    draw_header(c, i);
    const gfx_rect_t list = FS_LIST(i);
    char title[96];
    heading(i, title, sizeof(title));
    if (i == s.active) {
        ui_panel(c, list, title, UI_ACCENT);
    } else {
        ui_panel(c, list, NULL, UI_EDGE);
        gfx_text(c, list.x + 10, list.y + 2, title, UI_FONT_LABEL,
                 UI_TEXT_DIM, 1);
    }
    const fm_tree_t *t = &s.pane[i];
    if (!s.read[i] || t->total < 0 || t->held == 0) {
        draw_empty(c, i);
        return;
    }
    draw_bar(c, i);
    for (int r = 0; r < FS_ROWS; ++r) {
        const int idx = s.scroll[i] + r;
        if (idx >= t->held) {
            break;
        }
        draw_row(c, i, idx, list.y + FS_HEAD_H + r * FS_ROW_H);
    }
}

/**
 * @brief The message line: the last result or refusal, else the write
 *        gate's sentence while it is shut.
 * @param c  the canvas
 */
static void draw_message(gfx_canvas_t *c)
{
    const char *text = s.message;
    gfx_color_t col = s.message_ok ? UI_TEXT_DIM : UI_DANGER;
    if (text[0] == '\0') {
        const fm_err_t gate = fm_gate(FM_OP_COPY, &s.bench);
        if (gate == FM_OK) {
            return;
        }
        text = (gate == FM_ERR_ARMED) ? TR(FM_GATE_ARMED)
                                      : TR(FM_GATE_LOGGING);
        col = UI_WARN;
    }
    (void)ui_text_wrap(c, gfx_rect_make(FS_L, FS_MSG_Y, FS_W, FS_MSG_LH),
                       FS_MSG_LH, text, FS_FONT_BODY, col);
}

/**
 * @brief The footer's buttons, each dimmed while it would be refused.
 * @param c  the canvas
 */
static void draw_footer(gfx_canvas_t *c)
{
    static const ui_text_id_t labels[FOOT_N] = {
        TX_LOG_OPEN, TX_FM_COPY, TX_FM_MOVE, TX_FM_RENAME, TX_LOG_DELETE,
        TX_FM_NEW, TX_FM_INFO, TX_LOG_RESCAN,
    };
    for (int b = 0; b < FOOT_N; ++b) {
        const gfx_color_t fill = (b == FS_OPEN)     ? UI_ACCENT
                                 : (b == FS_DELETE) ? UI_DANGER
                                                    : UI_PANEL_HI;
        ui_button_font(c, FS_FOOT(b), ui_tr(labels[b]), fill, s.press == b,
                       fs_foot_live((fs_foot_t)b, NULL), FS_FONT_LABEL);
    }
}

/**
 * @brief NEW's choice over the panes: FOLDER and TEXT FILE.
 * @param c  the canvas
 */
static void draw_new(gfx_canvas_t *c)
{
    ui_choice_list(c, FS_NEW_LIST);
    ui_choice_row(c, FS_NEW_ROW(0), TR(FM_KIND_FOLDER), false,
                  s.press == FS_HIT_NEW_ROW);
    ui_choice_row(c, FS_NEW_ROW(1), TR(FM_NEW_FILE), false,
                  s.press == FS_HIT_NEW_ROW + 1);
}

/* ---------------------------------------------------------------- boxes */

/**
 * @brief The DELETE question: the entry and its size, what goes, CANCEL
 *        and DELETE.
 * @param c  the canvas
 */
static void draw_question(gfx_canvas_t *c)
{
    const gfx_rect_t box = FS_BOX;
    ui_panel(c, box, TR(LOG_DELETE_TITLE), UI_DANGER);
    char size[24] = "";
    if (!s.doomed.is_dir) {
        files_size_text(size, sizeof(size), s.doomed.size);
    }
    char line[FM_NAME_BUF + 32];
    snprintf(line, sizeof(line), "%s   %s", s.doomed.name, size);
    gfx_text_in(c, gfx_rect_make(box.x + 24, box.y + 44, box.w - 48, 20),
                line, FS_FONT_LABEL, UI_TEXT, 1, GFX_ALIGN_LEFT);
    gfx_text(c, box.x + 24, box.y + 84,
             s.doomed.is_dir ? TR(FM_DELETE_FOLDER) : TR(LOG_DELETE_WHAT),
             FS_FONT_BODY, UI_TEXT_DIM, 1);
    int y = box.y + 114;
    char report[LOG_RUN_NAME_MAX] = "";
    if (!s.doomed.is_dir) {
        log_name_report_of(report, sizeof(report), s.doomed.name);
    }
    if (report[0] != '\0') {
        char with[96];
        snprintf(with, sizeof(with), TR(FM_DELETE_REPORT), report);
        gfx_text(c, box.x + 24, y, with, FS_FONT_BODY, UI_TEXT_DIM, 1);
        y += 30;
    }
    gfx_text(c, box.x + 24, y, TR(LOG_DELETE_FINAL), FS_FONT_BODY,
             UI_TEXT_DIM, 1);
    ui_button_font(c, FS_Q_CANCEL, TR(CANCEL), UI_PANEL_HI,
                   s.press == FS_HIT_Q_CANCEL, true, FS_FONT_LABEL);
    ui_button_font(c, FS_Q_DELETE, TR(LOG_DELETE), UI_DANGER,
                   s.press == FS_HIT_Q_DELETE, true, FS_FONT_LABEL);
}

/**
 * @brief One row of INFO: its key and its value.
 * @param c      the canvas
 * @param y      the row's top
 * @param key    the key
 * @param value  the value
 * @param col    the value's colour
 */
static void info_row(gfx_canvas_t *c, int y, const char *key,
                     const char *value, gfx_color_t col)
{
    const gfx_rect_t box = FS_INFO_BOX;
    gfx_text(c, box.x + 24, y, key, FS_FONT_LABEL, UI_TEXT_DIM, 1);
    gfx_text(c, box.x + 24 + FS_INFO_KEY_W, y, value, FS_FONT_LABEL, col, 1);
}

/**
 * @brief INFO's Rule row: what the bench keeps the entry from.
 * @param e  the entry
 * @return the words
 */
static const char *rule_words(const fm_entry_t *e)
{
    if (e->busy) {
        return TR(FM_RULE_BUSY);
    }
    switch ((fm_place_t)e->place) {
    case FM_PLACE_FIXED:  return TR(FM_RULE_FIXED);
    case FM_PLACE_LOCKED: return TR(FM_RULE_LOCKED);
    case FM_PLACE_FAULT:  return TR(FM_RULE_FAULT);
    case FM_PLACE_FREE:
    default:              return TR(FM_RULE_FREE);
    }
}

/**
 * @brief The INFO box: name, folder, kind, size or entries, time, rule,
 *        and CLOSE.
 * @param c  the canvas
 */
static void draw_info(gfx_canvas_t *c)
{
    const gfx_rect_t box = FS_INFO_BOX;
    const fm_entry_t *e = &s.info_entry;
    ui_panel(c, box, e->is_dir ? TR(FM_KIND_FOLDER) : TR(FM_INFO_FILE),
             UI_ACCENT);
    const bool reading = s.busy && s.req.kind == FM_REQ_INFO;
    const int cells = (box.w - 48 - FS_INFO_KEY_W) / 10;
    int y = box.y + 40;
    /* A name of more cells than the row takes goes on into a second. */
    const size_t first = gfx_text_prefix(e->name, cells);
    char part[FM_NAME_BUF];
    snprintf(part, sizeof(part), "%.*s", (int)first, e->name);
    info_row(c, y, TR(FM_INFO_NAME), part, UI_TEXT);
    y += FS_INFO_LH;
    if (e->name[first] != '\0') {
        info_row(c, y - 8, "", e->name + first, UI_TEXT);
        y += FS_INFO_LH - 8;
    }
    char folder[FM_PATH_SIZE + 4];
    if (s.info_dir[0] == '\0') {
        snprintf(folder, sizeof(folder), "%s", TR(LOG_TAB_ROOT));
    } else {
        tail_cells(s.info_dir, cells, folder, sizeof(folder));
    }
    info_row(c, y, TR(FM_INFO_FOLDER), folder, UI_TEXT);
    y += FS_INFO_LH;
    info_row(c, y, TR(FM_INFO_KIND), fs_kind_word(e->kind), UI_TEXT);
    y += FS_INFO_LH;
    char value[96];
    char size[24];
    if (reading) {
        snprintf(value, sizeof(value), "%s", TR(FM_INFO_READING));
    } else if (e->is_dir) {
        files_size_text(size, sizeof(size), s.info.size);
        int used = snprintf(value, sizeof(value), TR(FM_INFO_COUNT),
                            s.info.entries, size);
        if (s.info.unlisted > 0u && used > 0
            && (size_t)used < sizeof(value) - 3u) {
            used += snprintf(value + used, sizeof(value) - (size_t)used,
                             "; ");
            snprintf(value + used, sizeof(value) - (size_t)used,
                     TR(FM_INFO_UNLISTED), s.info.unlisted);
        }
    } else {
        const uint64_t bytes = (s.info_err == FM_OK) ? s.info.size : e->size;
        files_size_text(size, sizeof(size), bytes);
        if (bytes >= 1024u) {
            snprintf(value, sizeof(value), "%" PRIu64 "B, %s", bytes, size);
        } else {
            snprintf(value, sizeof(value), "%" PRIu64 "B", bytes);
        }
    }
    info_row(c, y, e->is_dir ? TR(FM_INFO_ENTRIES) : TR(FM_INFO_SIZE), value,
             UI_TEXT);
    y += FS_INFO_LH;
    if (reading) {
        snprintf(value, sizeof(value), "%s", TR(FM_INFO_READING));
    } else if (!wall_stamp_known(s.info.mtime)) {
        /* No stamp, or one FatFs made from an unset clock: any time in
         * 1980 (wall_clock.h).  The panel claims no date it lacks. */
        snprintf(value, sizeof(value), "%s", TR(FM_INFO_NO_TIME));
    } else {
        /* The stamp in the zone SETUP holds, as FatFs wrote it. */
        files_time_text(value, sizeof(value), wall_local_s(s.info.mtime));
    }
    info_row(c, y, TR(FM_INFO_TIME), value, UI_TEXT);
    y += FS_INFO_LH;
    info_row(c, y, TR(FM_INFO_RULE), rule_words(e),
             (e->place == FM_PLACE_FREE && !e->busy) ? UI_TEXT : UI_WARN);
    y += FS_INFO_LH;
    if (!reading && s.info_err != FM_OK) {
        char why[sizeof(s.message)];
        fs_err_text(s.info_err, e->name, why, sizeof(why));
        (void)ui_text_wrap(c, gfx_rect_make(box.x + 24, y + 4, box.w - 48, 24),
                           26, why, FS_FONT_BODY, UI_DANGER);
    }
    ui_button_font(c, FS_INFO_CLOSE, TR(FM_CLOSE), UI_PANEL_HI,
                   s.press == FS_HIT_INFO_CLOSE, true, FS_FONT_LABEL);
}

/**
 * @brief The progress box: the job and its entry, the target, a copy's
 *        bar and bytes, and a copy's CANCEL.
 * @param c  the canvas
 */
static void draw_job(gfx_canvas_t *c)
{
    const fm_job_t *j = &s.req.job;
    const char *verb;
    switch (j->kind) {
    case FM_JOB_COPY:   verb = TR(FM_JOB_COPY); break;
    case FM_JOB_MOVE:   verb = TR(FM_JOB_MOVE); break;
    case FM_JOB_RENAME: verb = TR(FM_JOB_RENAME); break;
    case FM_JOB_DELETE: verb = TR(FM_JOB_DELETE); break;
    default:            verb = TR(FM_JOB_MAKE); break;
    }
    const char *name = fm_path_base(j->src[0] != '\0' ? j->src : j->dst);
    char title[FM_NAME_BUF + 24];
    snprintf(title, sizeof(title), "%s %s", verb, name);
    const gfx_rect_t box = FS_BOX;
    ui_panel(c, box, title, UI_ACCENT);
    char dir[FM_PATH_SIZE];
    snprintf(dir, sizeof(dir), "%s", j->dst);
    if (j->kind == FM_JOB_COPY || j->kind == FM_JOB_MOVE) {
        (void)fm_path_up(dir);
    }
    char where[FM_PATH_SIZE + 4];
    if (j->kind == FM_JOB_COPY || j->kind == FM_JOB_MOVE) {
        tail_cells(dir, 60, where, sizeof(where));
        if (dir[0] == '\0') {
            snprintf(where, sizeof(where), "%s", TR(LOG_TAB_ROOT));
        }
    } else {
        snprintf(where, sizeof(where), "%s", fm_path_base(j->dst));
    }
    char line[FM_PATH_SIZE + 24];
    snprintf(line, sizeof(line), TR(FM_JOB_TO), where);
    if (j->kind != FM_JOB_DELETE) {
        gfx_text(c, box.x + 24, box.y + 52, line, FS_FONT_LABEL, UI_TEXT, 1);
    }
    if (j->kind != FM_JOB_COPY) {
        return;
    }
    const float frac = (s.prog_total > 0u)
                           ? (float)((double)s.prog_done
                                     / (double)s.prog_total)
                           : 0.0f;
    ui_bar(c, FS_JOB_BAR, frac, 0.0f, UI_ACCENT);
    char done[24];
    char total[24];
    files_size_text(done, sizeof(done), s.prog_done);
    files_size_text(total, sizeof(total), s.prog_total);
    snprintf(line, sizeof(line), TR(FM_JOB_OF), done, total);
    gfx_text(c, FS_JOB_BAR.x, FS_JOB_BAR.y + FS_JOB_BAR.h + 8, line,
             FS_FONT_LABEL, UI_TEXT_DIM, 1);
    ui_button_font(c, FS_JOB_CANCEL, TR(CANCEL), UI_PANEL_HI,
                   s.press == FS_HIT_JOB_CANCEL, !s.cancel_asked,
                   FS_FONT_LABEL);
}

/**
 * @brief Whether the progress box shows: a copy's at once, a single
 *        call's once it has run FS_JOB_BOX_S.
 * @return true when it shows
 */
static bool job_box_shows(void)
{
    if (s.view != FILES_VIEW_JOB) {
        return false;
    }
    return s.req.job.kind == FM_JOB_COPY || s.busy_s >= FS_JOB_BOX_S;
}

void fs_render(gfx_canvas_t *c, int buffer_index)
{
    const int bi = (buffer_index >= 0 && buffer_index < MAX_FBS)
                       ? buffer_index : 0;
    const bool tk_moved = (s.view == FILES_VIEW_NAME
                           && s.tk.revision != s.drawn_tk_rev);
    if (s.valid[bi] && !tk_moved) {
        return;
    }
    gfx_fill_rect(c, 0, 0, SCREEN_W, SCREEN_H, UI_BG);
    if (s.view == FILES_VIEW_NAME) {
        ui_textkey_render(&s.tk, c);
    } else if (s.view == FILES_VIEW_DELETE) {
        draw_question(c);
    } else if (s.view == FILES_VIEW_INFO) {
        draw_info(c);
    } else if (job_box_shows()) {
        draw_job(c);
    } else {
        /* The panes, also under a single call that is quick: no box. */
        draw_pane(c, 0);
        draw_pane(c, 1);
        draw_message(c);
        draw_footer(c);
        if (s.view == FILES_VIEW_NEW) {
            draw_new(c);
        }
    }
    s.valid[bi] = true;
    s.drawn_tk_rev = s.tk.revision;
}
