/**
 * @file log_viewer_draw.c
 * @brief The log viewer's drawing: the browse view with its tabs, list, scroll
 *        bar and DELETE question, the import view, and the plot.
 *        log_viewer_screen.c holds the state, the card's listing and paging
 *        and the screen's interface (log_viewer_screen.h); log_viewer_int.h is
 *        what the screen's four files share.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "log_viewer_screen.h"
#include "settings.h"
#include "ui_screen.h"
#include "ui_card_text.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_widgets.h"

#include "log_viewer_int.h"

/* -------------------------------------------------------------- browse --- */

void log_viewer_size_text(char *out, size_t n, uint64_t bytes)
{
    if (bytes >= 1024ull * 1024u * 1024u) {
        snprintf(out, n, "%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ull * 1024u) {
        snprintf(out, n, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024u) {
        snprintf(out, n, "%u kB", (unsigned)(bytes / 1024u));
    } else {
        snprintf(out, n, "%u B", (unsigned)bytes);
    }
}

static void render_question(gfx_canvas_t *c)
{
    const gfx_rect_t box = DQ_BOX;
    ui_panel(c, box, TR(LOG_DELETE_TITLE), UI_DANGER);

    char size[16];
    char line[LOG_VIEWER_NAME_MAX + 24];
    log_viewer_size_text(size, sizeof(size), s.doomed_size);
    snprintf(line, sizeof(line), "%s   %s", s.doomed, size);
    /* The file in the label face, the two sentences under it in the body
     * face, 32 px apart. */
    gfx_text_in(c, gfx_rect_make(box.x + 24, box.y + 44, box.w - 48, 20), line,
                LV_FONT_LABEL, UI_TEXT, 1, GFX_ALIGN_LEFT);
    gfx_text(c, box.x + 24, box.y + 88, TR(LOG_DELETE_WHAT),
             LV_FONT_BODY, UI_TEXT_DIM, 1);
    gfx_text(c, box.x + 24, box.y + 120, TR(LOG_DELETE_FINAL),
             LV_FONT_BODY, UI_TEXT_DIM, 1);

    ui_button_font(c, DQ_CANCEL, TR(CANCEL), UI_PANEL_HI, s.press_btn == 0,
                   true, LV_FONT_LABEL);
    ui_button_font(c, DQ_DELETE, TR(LOG_DELETE), UI_DANGER, s.press_btn == 1,
                   true, LV_FONT_LABEL);
}

static void render_bar(gfx_canvas_t *c)
{
    const gfx_rect_t t = BR_BAR;
    gfx_fill_chamfer_rect(c, t.x, t.y, t.w, t.h, UI_CHAMFER_SM, UI_PANEL_SUNK);
    int ty;
    int th;
    lv_bar_thumb(&ty, &th);
    /* Accent while there is something to scroll, as the SETUP lists' bar;
     * the raised panel colour when the page shows whole. */
    const gfx_color_t col = (s.n_files > BR_ROWS) ? UI_ACCENT : UI_PANEL_HI;
    gfx_fill_chamfer_rect(c, t.x + 4, ty + 2, t.w - 8, th - 4, UI_CHAMFER_SM,
                          col);
}

/* The empty list's two lines: why it is empty, and what the folder holds
 * when it does not. */
static void empty_lines(char *why, size_t n, const char **hint)
{
    if (s.listed == -1) {
        snprintf(why, n, "%s", ui_card_text(s.card, UI_CARD_AT_LOGS));
    } else if (s.tab == LOG_TAB_ROOT) {
        snprintf(why, n, "%s", TR(LOG_NO_CSV));
    } else {
        snprintf(why, n, TR(LOG_NO_CSV_IN), log_viewer_tab_dir(s.tab));
    }
    switch (s.tab) {
    case LOG_TAB_USER:
        *hint = settings_get_bool(SET_LOG_USER) ? TR(LOG_TESTS_HERE)
                                                : TR(LOG_USER_OFF);
        break;
    case LOG_TAB_BENCH:
        *hint = settings_get_bool(SET_LOG_AUTO) ? TR(LOG_RUNS_HERE)
                                                : TR(LOG_AUTO_OFF);
        break;
    case LOG_TAB_ROOT:
    default:
        *hint = TR(LOG_ROOT_HERE);
        break;
    }
}

static void render_browse(gfx_canvas_t *c)
{
    /* The folder on screen after the tabs, "BENCH/LOGS" (nothing for the
     * root), and the card's name at the row's right end, "UNITEST". */
    const char *vol = lv_volume_now();
    const char *dir = log_viewer_tab_dir(s.tab);
    const char *name = (vol[0] != '\0') ? vol : TR(LOG_SD_CARD);
    const int end = lv_paged() ? BR_HEAD_END : BR_HEAD_FAR;
    const int dir_w = gfx_text_width(LV_FONT_LABEL, dir, 1);
    gfx_text(c, BR_HEAD_X, BR_HEAD_TY, dir, LV_FONT_LABEL, UI_TEXT_DIM, 1);
    const int name_x = BR_HEAD_X + dir_w + 16;
    gfx_text_in(c, gfx_rect_make(name_x, BR_PG_Y, end - name_x, BR_PG_H), name,
                LV_FONT_LABEL, UI_TEXT_DIM, 1, GFX_ALIGN_RIGHT);

    /*
     * The question replaces the list and the footer rather than sitting on
     * top of them: a footer left on screen is a RESCAN or an OPEN that looks
     * live while the only answers are CANCEL and DELETE.
     */
    if (s.doomed[0] != '\0') {
        render_question(c);
        return;
    }
    /* Not under the question: its only answers are CANCEL and DELETE. */
    ui_tabs_render(&s.tabs, c);

    if (s.listed != 1 || s.n_files == 0) {
        const gfx_rect_t box = BR_EMPTY;
        ui_panel(c, box, TR(LOG_NO_LOGS), UI_WARN);
        char why[96];
        const char *hint = "";
        empty_lines(why, sizeof(why), &hint);
        gfx_text(c, box.x + 24, box.y + 52, why, LV_FONT_BODY, UI_TEXT, 1);
        gfx_text(c, box.x + 24, box.y + 92, hint, LV_FONT_BODY,
                 UI_TEXT_FAINT, 1);
    } else {
        /*
         * The tab names the page's place in the card when the volume holds
         * more than the page does: a file missing from a list headed FILES
         * reads as a file that is not on the card.  Pages run newest run
         * first, see log_select.h.  The buffer takes the three counts
         * written out in full, so a volume that reports a nonsense one still
         * leaves a terminated title.
         */
        char title[64];
        log_viewer_browse_title(title, sizeof(title));
        ui_panel(c, BR_LIST, title, UI_ACCENT);
        render_bar(c);
        int y = BR_LIST.y + 30;
        for (int r = 0; r < BR_ROWS; ++r) {
            int i = s.scroll + r;
            if (i >= s.n_files) {
                break;
            }
            gfx_rect_t row = gfx_rect_make(BR_ROW_X, y, BR_ROW_W,
                                           BR_ROW_H - 4);
            bool on = (i == s.sel);
            gfx_fill_chamfer_rect(c, row.x, row.y, row.w, row.h, UI_CHAMFER_SM,
                                  on ? UI_PANEL_HI : UI_PANEL_SUNK);
            if (on) {
                gfx_fill_rect(c, row.x, row.y, 4, row.h, UI_ACCENT);
            }
            gfx_text(c, row.x + 16, row.y + (row.h - 20) / 2, s.files[i].name,
                     LV_FONT_LABEL, on ? UI_TEXT : UI_TEXT_DIM, 1);

            char size[16];
            log_viewer_size_text(size, sizeof(size), s.files[i].size);
            gfx_rect_t sz = gfx_rect_make(row.x + row.w - 16 - BR_SIZE_W, row.y,
                                          BR_SIZE_W, row.h);
            gfx_text_in(c, sz, s.files[i].is_dir ? TR(LOG_DIR) : size,
                        LV_FONT_LABEL,
                        UI_TEXT_FAINT, 1, GFX_ALIGN_RIGHT);
            y += BR_ROW_H;
        }
    }

    if (lv_paged()) {
        ui_button_font(c, BR_PREV, TR(LOG_PREV), UI_PANEL_HI, s.press_btn == 4,
                       lv_can_prev(), LV_FONT_LABEL);
        ui_button_font(c, BR_NEXT, TR(LOG_NEXT), UI_PANEL_HI, s.press_btn == 5,
                       lv_can_next(), LV_FONT_LABEL);
    }
    ui_button_font(c, BR_RESCAN, TR(LOG_RESCAN), UI_PANEL_HI,
                   s.press_btn == 0, true, LV_FONT_LABEL);
    /* WI-FI: the card's files to a browser, over the panel's
     * Wi-Fi.  The screen there says when Wi-Fi may not start. */
    ui_button_font(c, BR_WIFI, TR(LOG_WIFI), UI_PANEL_HI, s.press_btn == 3,
                   true, LV_FONT_LABEL);
    int msg_end = BR_OPEN.x;
    if (s.n_files > 0) {
        ui_button_font(c, BR_OPEN, TR(LOG_OPEN), UI_ACCENT, s.press_btn == 1,
                       s.sel >= 0, LV_FONT_LABEL);
        if (s.io.remove != NULL) {
            ui_button_font(c, BR_DELETE, TR(LOG_DELETE), UI_DANGER,
                           s.press_btn == 2, lv_can_delete(), LV_FONT_LABEL);
            msg_end = BR_DELETE.x;
        }
    }
    if (s.message[0] != '\0') {
        /* In the gap between the buttons, on two BR_MSG_LH px lines of the
         * 52 px footer when one is too short.  A message longer than two
         * lines is one line clipped there: a long name runs out of room
         * rather than over DELETE. */
        const gfx_color_t col = s.message_ok ? UI_TEXT_DIM : UI_DANGER;
        const gfx_rect_t two = gfx_rect_make(BR_MSG_X, FOOT_Y + 2,
                                             msg_end - 8 - BR_MSG_X, BR_MSG_LH);
        const int n = ui_text_wrap(NULL, two, BR_MSG_LH, s.message,
                                   LV_FONT_BODY, col);
        if (n == 2) {
            (void)ui_text_wrap(c, two, BR_MSG_LH, s.message, LV_FONT_BODY,
                               col);
        } else {
            gfx_text_in(c, gfx_rect_make(BR_MSG_X, FOOT_Y,
                                         msg_end - 8 - BR_MSG_X, FOOT_H),
                        s.message, LV_FONT_BODY, col, 1, GFX_ALIGN_LEFT);
        }
    }
}

/* -------------------------------------------------------------- import --- */

static const char *conv_label(log_conv_t v)
{
    switch (v) {
    case LOG_CONV_DE: return "1.234,5";
    case LOG_CONV_EN: return "1,234.5";
    default:          return "AUTO";
    }
}

static void render_import(gfx_canvas_t *c)
{
    gfx_text_in(c, gfx_rect_make(LV_L + 4, 0, LV_W - 8, IM_TOP - 4), s.name,
                LV_FONT_LABEL, UI_TEXT_DIM, 1, GFX_ALIGN_LEFT);

    if (!s.have_analysis) {
        const gfx_rect_t box = IM_FAIL;
        ui_panel(c, box, TR(LOG_CANNOT_READ), UI_DANGER);
        gfx_text(c, box.x + 24, box.y + 52,
                 s.message[0] ? s.message : TR(LOG_UNREADABLE), LV_FONT_BODY,
                 UI_TEXT, 1);
        gfx_text(c, box.x + 24, box.y + 92,
                 TR(LOG_TRY_ANOTHER),
                 LV_FONT_BODY, UI_TEXT_FAINT, 1);
    } else {
        ui_panel(c, IM_LEFT, TR(LOG_DETECTED), UI_ACCENT);

        /* Rows IM_KEY_LH px apart, so that under the evidence the warnings
         * take up to four lines of 36 cells above the hints. */
        char line[64];
        int y = IM_LEFT.y + 34;
        const int lh = IM_KEY_LH;

        struct {
            const char *key;
            char value[48];
            gfx_color_t color;
        } rows[6];
        int n = 0;

        /* cppcheck-suppress legacyUninitvar
         * snprintf writes rows[n].value, it does not read it; the array is
         * filled here and every field set before it is drawn. */
        snprintf(rows[n].value, sizeof(rows[n].value), "%s",
                 lv_delim_text(s.an.delimiter));
        rows[n].key = TR(LOG_KEY_SEPARATOR);
        rows[n].color = UI_TEXT;
        n++;

        const char *flag = s.an.convention_forced ? TR(LOG_SET)
                           : (s.an.convention_confident ? "" : "(?)");
        snprintf(rows[n].value, sizeof(rows[n].value), "%s%s%s",
                 (s.an.convention == LOG_CONV_DE) ? TR(LOG_GERMAN)
                                                  : TR(LOG_ENGLISH),
                 (flag[0] != '\0') ? " " : "", flag);
        rows[n].key = TR(LOG_KEY_NUMBERS);
        rows[n].color = (s.an.convention_forced || s.an.convention_confident)
                            ? UI_TEXT
                            : UI_WARN;
        n++;

        snprintf(rows[n].value, sizeof(rows[n].value), "%d%s%s",
                 s.an.row_count, s.an.truncated ? " " : "",
                 s.an.truncated ? TR(LOG_CAPPED) : "");
        rows[n].key = TR(LOG_KEY_ROWS);
        rows[n].color = (s.an.truncated || s.an.cut_row) ? UI_WARN : UI_TEXT;
        n++;

        snprintf(rows[n].value, sizeof(rows[n].value), "%d", s.an.n_columns);
        rows[n].key = TR(LOG_KEY_COLUMNS);
        rows[n].color = UI_TEXT;
        n++;

        if (s.an.time_index >= 0) {
            snprintf(rows[n].value, sizeof(rows[n].value), "%s %s",
                     s.an.columns[s.an.time_index].name, s.an.time_unit);
        } else {
            snprintf(rows[n].value, sizeof(rows[n].value), "%s",
                     TR(LOG_ROW_INDEX));
        }
        rows[n].key = TR(LOG_KEY_TIME);
        rows[n].color = (s.an.time_index >= 0) ? UI_TEXT : UI_WARN;
        n++;

        snprintf(rows[n].value, sizeof(rows[n].value), "%d%s%s",
                 s.an.ragged_rows, (s.an.long_rows > 0) ? " " : "",
                 (s.an.long_rows > 0) ? TR(LOG_PLUS_LONG) : "");
        rows[n].key = TR(LOG_KEY_RAGGED);
        rows[n].color = (s.an.ragged_rows > 0 || s.an.long_rows > 0) ? UI_DANGER
                                                                     : UI_TEXT;
        n++;

        /* The key at the left, its value right-aligned in 20 cells. */
        for (int i = 0; i < n; ++i) {
            gfx_text(c, IM_LEFT.x + 16, y, rows[i].key, LV_FONT_LABEL,
                     UI_TEXT_FAINT, 1);
            gfx_rect_t box = gfx_rect_make(IM_LEFT.x + IM_LEFT.w - 16 - 200,
                                           y, 200, 20);
            gfx_text_in(c, box, rows[i].value, LV_FONT_LABEL, rows[i].color, 1,
                        GFX_ALIGN_RIGHT);
            y += lh;
        }

        ui_rule(c, IM_LEFT.x + 16, y + 2, IM_LEFT.w - 32, UI_EDGE);
        y += 12;

        /* The evidence behind the detection. */
        snprintf(line, sizeof(line), TR(LOG_EVIDENCE),
                 s.an.votes.de, s.an.votes.en, s.an.votes.ambiguous);
        gfx_text(c, IM_LEFT.x + 16, y, line, LV_FONT_LABEL, UI_TEXT_FAINT, 1);
        y += IM_KEY_LH;

        /* A warning breaks onto a second line of 36 cells, IM_WARN_LH px
         * lower. */
        const gfx_rect_t warn = gfx_rect_make(IM_LEFT.x + 16, y,
                                              IM_LEFT.w - 32, IM_WARN_LH);
        int lines = 0;
        if (s.an.convention_conflict) {
            lines = ui_text_wrap(c, warn, IM_WARN_LH, TR(LOG_MIXED),
                                 LV_FONT_LABEL, UI_DANGER);
        } else if (s.an.long_rows > 0) {
            /* Those rows lost every cell after the overflow, and a lost cell
             * looks exactly like a blank one further down. */
            snprintf(line, sizeof(line), TR(LOG_TOO_LONG),
                     s.an.long_rows);
            lines = ui_text_wrap(c, warn, IM_WARN_LH, line, LV_FONT_LABEL,
                                 UI_DANGER);
        } else if (s.an.ragged_rows > 0) {
            lines = ui_text_wrap(c, warn, IM_WARN_LH, TR(LOG_ROWS_DIFFER),
                                 LV_FONT_LABEL, UI_DANGER);
        } else if (!s.an.convention_confident) {
            lines = ui_text_wrap(c, warn, IM_WARN_LH, TR(LOG_UNSETTLED),
                                 LV_FONT_LABEL, UI_WARN);
        }
        if (s.an.cut_row) {
            /* Independent of the lines above: the row left out has no other
             * place on screen.  It follows them when they are shown. */
            gfx_rect_t cut = warn;
            cut.y = (int16_t)(warn.y + IM_WARN_LH * lines);
            (void)ui_text_wrap(c, cut, IM_WARN_LH, TR(LOG_LAST_CUT),
                               LV_FONT_LABEL, UI_WARN);
        }

        /* The override controls for the two detections above. */
        int hy = IM_LEFT.y + IM_LEFT.h - 8 - 2 * IM_WARN_LH;
        ui_rule(c, IM_LEFT.x + 16, hy - 6, IM_LEFT.w - 32, UI_EDGE);
        gfx_text(c, IM_LEFT.x + 16, hy, TR(LOG_HINT_NUM),
                 LV_FONT_LABEL, UI_TEXT_FAINT, 1);
        gfx_text(c, IM_LEFT.x + 16, hy + IM_WARN_LH, TR(LOG_HINT_SEP),
                 LV_FONT_LABEL, UI_TEXT_FAINT, 1);

        /* --- columns -------------------------------------------------- */
        char title[40];
        snprintf(title, sizeof(title), TR(LOG_PLOT_N), s.n_picked,
                 LOG_MAX_SERIES);
        ui_panel(c, IM_RIGHT, title, UI_ACCENT);

        int ry = IM_RIGHT.y + 30;
        for (int r = 0; r < IM_ROWS; ++r) {
            int i = s.col_scroll + r;
            if (i >= s.an.n_columns) {
                break;
            }
            const log_column_t *col = &s.an.columns[i];
            int slot = -1;
            for (int k = 0; k < s.n_picked; ++k) {
                if (s.picked[k] == i) {
                    slot = k;
                }
            }
            bool is_time = (i == s.an.time_index);

            gfx_rect_t row = gfx_rect_make(IM_RIGHT.x + 8, ry, IM_RIGHT.w - 16,
                                           IM_ROW_H - 4);
            gfx_fill_chamfer_rect(c, row.x, row.y, row.w, row.h, UI_CHAMFER_SM,
                                  (slot >= 0) ? UI_PANEL_HI : UI_PANEL_SUNK);
            gfx_color_t mark = is_time      ? UI_TEAL
                               : (slot >= 0) ? ui_theme_color(lv_series_color[slot])
                               : col->numeric ? UI_EDGE_HI
                                              : UI_PANEL;
            gfx_fill_rect(c, row.x, row.y, 5, row.h, mark);

            /* The name in 20 cells from 16 px in, the first value in 12
             * cells to x 344, and 12 px short of the row's end the range, a
             * data figure, in the 140 px box of LOG_RANGE_CELLS glyphs of
             * 8 x 16, or TIME or TEXT in 22 cells of 10 x 20. */
            gfx_color_t text = col->numeric ? UI_TEXT : UI_TEXT_FAINT;
            gfx_text_in(c, gfx_rect_make(row.x + 16, row.y, 200, row.h),
                        col->name, LV_FONT_LABEL, text, 1, GFX_ALIGN_LEFT);

            /* The file's own first value, next to what was made of it: the
             * cheapest way to see a mis-read before plotting it. */
            gfx_rect_t sb = gfx_rect_make(row.x + 224, row.y, 120, row.h);
            gfx_text_in(c, sb, col->sample[0], LV_FONT_LABEL, UI_TEXT_DIM, 1,
                        GFX_ALIGN_RIGHT);

            char right[40];
            if (is_time) {
                snprintf(right, sizeof(right), "%s", TR(LOG_TIME));
            } else if (!col->numeric) {
                snprintf(right, sizeof(right), "%s", TR(LOG_TEXT));
            } else {
                log_viewer_range_text(right, sizeof(right), col->min,
                                      col->max, col->unit);
            }
            const bool range = !is_time && col->numeric;
            const int rw = range ? 140 : row.w - 364;
            gfx_rect_t rb = gfx_rect_make(row.x + row.w - 12 - rw, row.y, rw,
                                          row.h);
            gfx_text_in(c, rb, right, range ? LV_FONT_FINE : LV_FONT_LABEL,
                        is_time ? UI_TEAL : UI_TEXT_FAINT, 1, GFX_ALIGN_RIGHT);
            ry += IM_ROW_H;
        }
    }

    /* --- controls ------------------------------------------------------ */
    char btn[40];
    snprintf(btn, sizeof(btn), TR(LOG_BTN_SEP),
             (s.delim_index < 0) ? "AUTO"
                                 : lv_delim_text(log_csv_delimiters[s.delim_index]));
    ui_button_font(c, IM_BTN(0), btn, UI_PANEL_HI, s.press_btn == 0, true,
                   LV_FONT_LABEL);

    snprintf(btn, sizeof(btn), TR(LOG_BTN_NUM), conv_label(s.conv));
    ui_button_font(c, IM_BTN(1), btn, UI_PANEL_HI, s.press_btn == 1, true,
                   LV_FONT_LABEL);

    ui_button_font(c, IM_BTN(2), TR(LOG_BACK), UI_PANEL_HI, s.press_btn == 2,
                   true, LV_FONT_LABEL);

    ui_button_font(c, IM_BTN(3), TR(LOG_PLOT), UI_ACCENT, s.press_btn == 3,
                   s.have_analysis && s.n_picked > 0, LV_FONT_LABEL);
}

/* ---------------------------------------------------------------- plot --- */

static void draw_grid_band(gfx_canvas_t *band, int band_y0, int rows)
{
    for (int i = 1; i < PV_DIV_X; ++i) {
        int x = (PV_W * i) / PV_DIV_X;
        gfx_vline(band, x, 0, rows, UI_GRID);
    }
    for (int i = 0; i <= PV_DIV_Y; ++i) {
        int y = PV_Y + (PV_H * i) / PV_DIV_Y;
        if (y >= band_y0 && y < band_y0 + rows) {
            gfx_hline(band, 0, y - band_y0, PV_W,
                      (i == 0 || i == PV_DIV_Y) ? UI_GRID_STRONG : UI_GRID);
        }
    }
}

static void draw_trace_band(gfx_canvas_t *band, int k, int band_y0, int rows)
{
    gfx_color_t col = ui_theme_color(lv_series_color[k]);
    for (int x = 0; x < PV_W; ++x) {
        int y0 = lv_span[k][x][0];
        int y1 = lv_span[k][x][1];
        if (y0 == PV_EMPTY || y1 < band_y0 || y0 >= band_y0 + rows) {
            continue;
        }
        int a = (y0 < band_y0) ? band_y0 : y0;
        int b = (y1 >= band_y0 + rows) ? band_y0 + rows - 1 : y1;
        gfx_vline(band, x, a - band_y0, b - a + 1, col);
    }
}

static int cursor_x(void)
{
    if (!s.have_data || s.win_n <= 1 || s.cursor < s.win0 ||
        s.cursor >= s.win0 + s.win_n) {
        return -1;
    }
    return PV_X + (int)(((long)(s.cursor - s.win0) * (PV_W - 1)) /
                        (s.win_n - 1));
}

static void render_plot(gfx_canvas_t *c)
{
    char line[160];

    if (!s.have_data) {
        const gfx_rect_t box = PV_NONE;
        ui_panel(c, box, TR(LOG_NOTHING_LOADED), UI_WARN);
        gfx_text(c, box.x + 24, box.y + 52,
                 s.message[0] ? s.message : TR(LOG_PICK_FIRST),
                 LV_FONT_BODY, UI_TEXT, 1);
        return;
    }

    snprintf(line, sizeof(line), TR(LOG_SUMMARY), s.name,
             s.data.count, s.data.duration_s, s.data.rate_hz);
    gfx_text_in(c, gfx_rect_make(LV_L + 4, PV_SUM_Y, LV_W - 8, 20), line,
                LV_FONT_LABEL, UI_TEXT_DIM, 1, GFX_ALIGN_LEFT);

    /* --- legend, doubling as the cursor readout ------------------------ */
    int ci = (s.cursor >= 0 && s.cursor < s.data.count) ? s.cursor : 0;
    for (int k = 0; k < s.data.n_fields; ++k) {
        gfx_rect_t chip = gfx_rect_make(CHIP_X + k * (CHIP_W + CHIP_GAP), CHIP_Y,
                                        CHIP_W, CHIP_H);
        gfx_color_t col = ui_theme_color(lv_series_color[k]);
        gfx_fill_chamfer_rect(c, chip.x, chip.y, chip.w, chip.h, UI_CHAMFER_SM,
                              UI_PANEL);
        gfx_fill_rect(c, chip.x, chip.y, 5, chip.h, col);
        /* The name over the range at the left, the value at the cursor
         * right-aligned on the lower line: two lines of 20 px. */
        gfx_text_in(c, gfx_rect_make(chip.x + 12, chip.y + 2, chip.w - 20, 20),
                    s.data.field[k].name, LV_FONT_LABEL, UI_TEXT, 1,
                    GFX_ALIGN_LEFT);

        char val[16];
        ui_fmt(val, sizeof(val), s.data.value[k][ci], 2);
        snprintf(line, sizeof(line), "%s%s", val, s.data.field[k].unit);
        gfx_rect_t box = gfx_rect_make(chip.x + 12, chip.y + 22, chip.w - 20, 20);
        gfx_text_in(c, box, line, LV_FONT_LABEL, col, 1, GFX_ALIGN_RIGHT);

        char lo[10];
        char hi[10];
        ui_fmt(lo, sizeof(lo), lv_lo[k], 0);
        ui_fmt(hi, sizeof(hi), lv_hi[k], 0);
        snprintf(line, sizeof(line), "%s-%s", lo, hi);
        gfx_text(c, chip.x + 12, chip.y + 24, line, LV_FONT_FINE,
                 UI_TEXT_FAINT, 1);
    }

    /* --- plot ----------------------------------------------------------- */
    ui_panel(c, PV_PANEL, NULL, UI_ACCENT);

    gfx_canvas_t band;
    for (int by = 0; by < PV_H; by += PV_BAND) {
        int rows = PV_H - by;
        if (rows > PV_BAND) {
            rows = PV_BAND;
        }
        gfx_canvas_init(&band, lv_band, PV_W, rows, PV_W);
        gfx_clear(&band, UI_PANEL_SUNK);
        draw_grid_band(&band, PV_Y + by, rows);
        for (int k = 0; k < s.data.n_fields; ++k) {
            draw_trace_band(&band, k, PV_Y + by, rows);
        }
        gfx_blit(c, PV_X, PV_Y + by, lv_band, PV_W, rows, PV_W);
    }

    int cx = cursor_x();
    if (cx >= 0) {
        gfx_vline(c, cx, PV_Y, PV_H, UI_TEXT_DIM);
        for (int k = 0; k < s.data.n_fields; ++k) {
            int x = cx - PV_X;
            if (lv_span[k][x][0] == PV_EMPTY) {
                continue;
            }
            int y = (lv_span[k][x][0] + lv_span[k][x][1]) / 2;
            gfx_fill_circle(c, cx, y, 3, ui_theme_color(lv_series_color[k]));
        }
    }

    /* --- time axis ------------------------------------------------------ */
    /* --- position of a zoomed view ------------------------------------ */
    if (s.win_n > 0 && s.win_n < s.data.count) {
        gfx_fill_rect(c, PV_X, PV_POS_Y, PV_W, PV_POS_H, UI_PANEL_SUNK);
        int bx = PV_X + (int)(((long)s.win0 * PV_W) / s.data.count);
        int bw = (int)(((long)s.win_n * PV_W) / s.data.count);
        if (bw < 6) {
            bw = 6;
        }
        if (bx + bw > PV_X + PV_W) {
            bx = PV_X + PV_W - bw;
        }
        gfx_fill_rect(c, bx, PV_POS_Y, bw, PV_POS_H, UI_ACCENT);
    }

    for (int i = 0; i <= PV_DIV_X; ++i) {
        int idx = s.win0 + (s.win_n - 1) * i / PV_DIV_X;
        char t[16];
        ui_fmt(t, sizeof(t), s.data.time[idx], 2);
        int x = PV_X + (PV_W - 1) * i / PV_DIV_X;
        int w = gfx_text_width(LV_FONT_FINE, t, 1);
        int tx = x - w / 2;
        if (tx < PV_X) {
            tx = PV_X;
        }
        if (tx + w > PV_X + PV_W) {
            tx = PV_X + PV_W - w;
        }
        gfx_text(c, tx, PV_PANEL.y + PV_PANEL.h + 4, t, LV_FONT_FINE,
                 UI_TEXT_FAINT, 1);
    }

    /* --- footer --------------------------------------------------------- */
    ui_button_font(c, PV_BACK, TR(LOG_BACK), UI_PANEL_HI, s.press_btn == 0,
                   true, LV_FONT_LABEL);
    ui_button_font(c, PV_PREV, "<", UI_PANEL_HI, s.press_btn == 1, true,
                   LV_FONT_LABEL);
    ui_button_font(c, PV_NEXT, ">", UI_PANEL_HI, s.press_btn == 2, true,
                   LV_FONT_LABEL);

    if (s.data.time_name[0] != '\0') {
        snprintf(line, sizeof(line), TR(LOG_AT_TIME),
                 (double)s.data.time[ci], ci + 1, s.data.count);
    } else {
        /* No time column, so the x axis is the row number.  Say that rather
         * than printing a seconds value that is really an index. */
        snprintf(line, sizeof(line), TR(LOG_AT_ROW), ci + 1,
                 s.data.count);
    }
    /* One line in the footer's middle, or two of 24 px with the cells
     * that did not read under it. */
    const bool two = s.data.unparsed_cells > 0;
    gfx_text(c, PV_LINE_X, FOOT_Y + (two ? 4 : (FOOT_H - 20) / 2), line,
             LV_FONT_LABEL, UI_TEXT_DIM, 1);
    if (two) {
        snprintf(line, sizeof(line), TR(LOG_UNREADABLE_CELLS),
                 s.data.unparsed_cells);
        gfx_text(c, PV_LINE_X, FOOT_Y + 28, line, LV_FONT_LABEL, UI_WARN, 1);
    }
}

void lv_render(gfx_canvas_t *c, int buffer_index)
{
    bool stale = true;
    if (buffer_index >= 0 && buffer_index < MAX_FBS) {
        stale = !s.valid[buffer_index];
    }
    if (!stale) {
        return;
    }

    gfx_clear(c, UI_BG);

    switch (s.view) {
    case VIEW_IMPORT:
        render_import(c);
        break;
    case VIEW_PLOT:
        render_plot(c);
        break;
    case VIEW_BROWSE:
    default:
        render_browse(c);
        break;
    }

    if (buffer_index >= 0 && buffer_index < MAX_FBS) {
        s.valid[buffer_index] = true;
    }
}
