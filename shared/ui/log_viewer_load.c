/**
 * @file log_viewer_load.c
 * @brief The log viewer's loading: a file the browse view opens is analysed
 *        (its delimiter, decimal convention and time column, see
 *        shared/logfile), its chosen columns are read, and lv_compute_spans()
 *        turns them into the plot's scaled traces; a report is read whole
 *        into the TEXT view.  log_viewer_screen.c holds
 *        the state, the card's listing and paging and the screen's interface
 *        (log_viewer_screen.h); log_viewer_int.h is what the screen's four
 *        files share.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "log_fields.h"
#include "log_name.h"
#include "log_viewer_screen.h"
#include "settings.h"
#include "ui_screen.h"
#include "ui_text.h"

#include "log_viewer_int.h"

/* ------------------------------------------------------------- loading --- */

/**
 * @brief The reader's verdict in the language showing; log_err_str() is the
 *        English the console logs.
 * @param err  the verdict
 * @return the words, from the text table
 */
static const char *err_text(log_err_t err)
{
    switch (err) {
    case LOG_OK:                   return TR(LOG_ERR_OK);
    case LOG_ERR_READ:             return TR(LOG_ERR_READ);
    case LOG_ERR_NO_HEADER:        return TR(LOG_ERR_NO_HEADER);
    case LOG_ERR_NO_ROWS:          return TR(LOG_ERR_NO_ROWS);
    case LOG_ERR_NO_NUMERIC:       return TR(LOG_ERR_NO_NUMERIC);
    case LOG_ERR_TOO_MANY_COLUMNS: return TR(LOG_ERR_COLUMNS);
    case LOG_ERR_MEMORY:           return TR(LOG_ERR_MEMORY);
    case LOG_ERR_ARG:              return TR(LOG_ERR_ARG);
    default:                       return TR(LOG_ERR_UNKNOWN);
    }
}

/** @brief Keep @p id and @p name as the footer message (log_viewer_int.h). */
void lv_say(ui_text_id_t id, const char *name)
{
    s.msg_kind = LV_MSG_TEXT;
    s.msg_id = id;
    s.msg_err = LOG_OK;
    snprintf(s.msg_name, sizeof(s.msg_name), "%s",
             (name != NULL) ? name : "");
    s.message_ok = false;
}

/** @brief Keep @p err as the footer message (log_viewer_int.h). */
void lv_say_err(log_err_t err)
{
    s.msg_kind = LV_MSG_ERR;
    s.msg_err = err;
    s.msg_name[0] = '\0';
    s.message_ok = false;
}

/** @brief Clear the footer message (log_viewer_int.h). */
void lv_quiet(void)
{
    s.msg_kind = LV_MSG_NONE;
}

/** @brief Word the footer message into @p buf (log_viewer_int.h). */
const char *lv_message(char *buf, size_t n)
{
    buf[0] = '\0';
    if (s.msg_kind == LV_MSG_ERR) {
        snprintf(buf, n, "%s", err_text(s.msg_err));
    } else if (s.msg_kind == LV_MSG_TEXT) {
        /* Each format by its own ID, so tools/check_formats.py sees it. */
        const char *nm = s.msg_name;
        switch (s.msg_id) {
        case TX_LOG_CANNOT_OPEN:
            snprintf(buf, n, TR(LOG_CANNOT_OPEN), nm);
            break;
        case TX_LOG_IS_FOLDER:
            snprintf(buf, n, TR(LOG_IS_FOLDER), nm);
            break;
        case TX_LOG_TOO_BIG:
            snprintf(buf, n, TR(LOG_TOO_BIG), nm);
            break;
        case TX_LOG_CANNOT_DELETE:
            snprintf(buf, n, TR(LOG_CANNOT_DELETE), nm);
            break;
        case TX_LOG_DELETED:
            snprintf(buf, n, TR(LOG_DELETED), nm);
            break;
        default:
            snprintf(buf, n, "%s", ui_tr(s.msg_id));
            break;
        }
    }
    return buf;
}

/* A delimiter's name, as log_csv_delimiter_label() gives it in English. */
const char *lv_delim_text(char delimiter)
{
    switch (delimiter) {
    case ';':  return TR(LOG_SEMICOLON);
    case ',':  return TR(LOG_COMMA);
    case '\t': return TR(LOG_TAB);
    case '|':  return TR(LOG_PIPE);
    default:   return "?";
    }
}

/* The open file again: from the mount its page was read from, or not at
 * all, so a list read since from another card lends it no file. */
static bool open_name(log_source_t *src)
{
    return s.open_mounts == s.mounts && s.mounts == lv_mounts_now()
           && s.io.open(s.name, src, s.io.ctx);
}

void lv_run_analysis(void)
{
    /*
     * Keep the names of the picked columns before the analysis overwrites
     * them.  The selection is stored as indices; both override buttons
     * re-run the analysis, and a delimiter change renumbers the columns, so
     * the selection is carried across by name.
     */
    char kept[LOG_MAX_SERIES][LOG_NAME_MAX];
    int n_kept = 0;
    if (s.have_analysis) {
        for (int i = 0; i < s.n_picked && i < LOG_MAX_SERIES; ++i) {
            int c = s.picked[i];
            if (c >= 0 && c < s.an.n_columns) {
                snprintf(kept[n_kept], sizeof(kept[0]), "%s",
                         s.an.columns[c].name);
                ++n_kept;
            }
        }
    }

    s.have_analysis = false;
    lv_drop_data();

    if (s.io.open == NULL) {
        lv_say(TX_LOG_NO_SOURCE, NULL);
        return;
    }

    log_source_t src;
    lv_point(s.open_tab);
    if (!open_name(&src)) {
        lv_say(TX_LOG_CANNOT_OPEN, s.name);
        return;
    }

    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    if (s.delim_index >= 0) {
        opts.delimiter = log_csv_delimiters[s.delim_index];
    }
    opts.convention = s.conv;

    log_err_t err = log_csv_analyse(&src, &opts, &s.an);
    if (s.io.close != NULL) {
        s.io.close(s.io.ctx);
    }

    if (err != LOG_OK) {
        lv_say_err(err);
        return;
    }

    s.have_analysis = true;
    lv_quiet();

    /*
     * Keep what the operator chose: lv_run_analysis() runs again from both
     * override buttons, and the selection has to survive it.  Re-resolve by
     * name rather than by index, because a delimiter change renumbers the
     * columns.
     */
    s.n_picked = 0;
    for (int k = 0; k < n_kept; ++k) {
        for (int c = 0; c < s.an.n_columns && s.n_picked < LOG_MAX_SERIES; ++c) {
            if (c != s.an.time_index && s.an.columns[c].numeric &&
                strcmp(s.an.columns[c].name, kept[k]) == 0) {
                s.picked[s.n_picked++] = c;
                break;
            }
        }
    }

    /* Nothing survived (a different file, or a re-read that renamed every
     * column): fall back to the file's first picks, a SERVO run's pulse,
     * angle, current and voltage, else its first numeric columns that are
     * not the time axis (log_fields_picks()). */
    if (s.n_picked == 0) {
        s.col_scroll = 0;
        s.n_picked = log_fields_picks(&s.an, s.picked, LOG_MAX_SERIES);
    }
}

void lv_compute_spans(void)
{
    /* Zero rather than return: leaving the arrays alone would let the previous
     * file's trace be drawn under this file's legend. */
    memset(lv_span, 0, sizeof(lv_span));
    memset(lv_lo, 0, sizeof(lv_lo));
    memset(lv_hi, 0, sizeof(lv_hi));
    if (!s.have_data || s.data.count <= 0) {
        return;
    }
    if (s.win_n <= 0 || s.win0 < 0 || s.win0 + s.win_n > s.data.count) {
        s.win0 = 0;
        s.win_n = s.data.count;
    }
    const int base = s.win0;
    const int count = s.win_n;

    for (int k = 0; k < s.data.n_fields; ++k) {
        float lo = s.data.field[k].min;
        float hi = s.data.field[k].max;
        /* A cell out of float range parses to an infinity, and an infinite
         * bound makes every scaling below (inf - inf) / inf = NaN, whose
         * conversion to int is undefined. */
        if (!isfinite(lo) || !isfinite(hi)) {
            lo = 0.0f;
            hi = 1.0f;
        }
        if (!(hi > lo)) {
            /* A constant trace still deserves a line through the middle. */
            lo -= 1.0f;
            hi += 1.0f;
        } else {
            float pad = (hi - lo) * 0.06f;
            float min = s.data.field[k].min;
            float max = s.data.field[k].max;
            lo -= pad;
            hi += pad;
            /* Never pad across zero: a current that never went negative should
             * not be drawn on an axis that says it might have. */
            if (min >= 0.0f && lo < 0.0f) {
                lo = 0.0f;
            }
            if (max <= 0.0f && hi > 0.0f) {
                hi = 0.0f;
            }
        }
        lv_lo[k] = lo;
        lv_hi[k] = hi;
        float span = hi - lo;

        const float *v = s.data.value[k] + base;
        for (int x = 0; x < PV_W; ++x) {
            int i0 = (int)(((long)x * count) / PV_W);
            int i1 = (int)(((long)(x + 1) * count) / PV_W);
            if (i1 <= i0) {
                i1 = i0 + 1;
            }
            if (i1 > count) {
                i1 = count;
            }
            if (i0 >= count) {
                i0 = count - 1;
            }

            /* Seed from the first sample that is a number: seeding from a
             * NaN and comparing against it leaves the whole bucket NaN even
             * when there are real samples in it. */
            float vmin = 0.0f;
            float vmax = 0.0f;
            bool any = false;
            for (int i = i0; i < i1; ++i) {
                if (!isfinite(v[i])) {
                    continue;
                }
                if (!any) {
                    vmin = vmax = v[i];
                    any = true;
                } else if (v[i] < vmin) {
                    vmin = v[i];
                } else if (v[i] > vmax) {
                    vmax = v[i];
                }
            }
            if (!any) {
                /*
                 * No number in this column.  The loader holds the previous
                 * value across an empty cell, so this is only the stretch
                 * before a column's first number: drawn as nothing rather
                 * than as a line along the bottom of the scale.
                 */
                lv_span[k][x][0] = PV_EMPTY;
                lv_span[k][x][1] = PV_EMPTY;
                continue;
            }

            int y_hi = PV_Y + PV_H - 1 -
                       (int)((vmax - lo) / span * (float)(PV_H - 1));
            int y_lo = PV_Y + PV_H - 1 -
                       (int)((vmin - lo) / span * (float)(PV_H - 1));
            if (y_hi < PV_Y) {
                y_hi = PV_Y;
            }
            if (y_hi > PV_Y + PV_H - 1) {
                y_hi = PV_Y + PV_H - 1;
            }
            if (y_lo > PV_Y + PV_H - 1) {
                y_lo = PV_Y + PV_H - 1;
            }
            if (y_lo < PV_Y) {
                y_lo = PV_Y;
            }
            lv_span[k][x][0] = (int16_t)y_hi;
            lv_span[k][x][1] = (int16_t)y_lo;
        }

        /* Join neighbouring columns so the trace is a line rather than a
         * dotted row of marks: each column reaches the one before it.  An
         * empty column joins nothing, so a gap stays a gap. */
        for (int x = 1; x < PV_W; ++x) {
            if (lv_span[k][x][0] == PV_EMPTY || lv_span[k][x - 1][0] == PV_EMPTY) {
                continue;
            }
            if (lv_span[k][x][0] > lv_span[k][x - 1][1]) {
                lv_span[k][x][0] = (int16_t)(lv_span[k][x - 1][1]);
            } else if (lv_span[k][x][1] < lv_span[k][x - 1][0]) {
                lv_span[k][x][1] = (int16_t)(lv_span[k][x - 1][0]);
            }
        }
    }
}

void lv_load_data(void)
{
    lv_drop_data();
    if (!s.have_analysis || s.io.open == NULL) {
        return;
    }

    log_source_t src;
    lv_point(s.open_tab);
    if (!open_name(&src)) {
        lv_say(TX_LOG_CANNOT_OPEN, s.name);
        return;
    }
    log_err_t err = log_csv_build(&src, &s.an, s.picked, s.n_picked, &s.data);
    if (s.io.close != NULL) {
        s.io.close(s.io.ctx);
    }

    if (err != LOG_OK) {
        lv_say_err(err);
        return;
    }

    s.have_data = true;
    s.cursor = s.data.count - 1;
    s.win0 = 0;
    s.win_n = s.data.count;
    s.pinch = false;
    s.panning = false;
    lv_quiet();
    lv_compute_spans();
    s.view = VIEW_PLOT;
}

/**
 * @brief Read the report s.name names into the TEXT view and show it.
 *
 * The file is read once, whole, up to LOG_TEXT_MAX bytes; a file the I/O
 * does not open, or one that fails to read, stays on the list with the
 * reason in the footer.
 */
static void open_text(void)
{
    log_text_free(&s.text);
    s.text_scroll = 0;
    if (s.io.open == NULL) {
        lv_say(TX_LOG_NO_SOURCE, NULL);
        return;
    }
    log_source_t src;
    lv_point(s.open_tab);
    if (!open_name(&src)) {
        lv_say(TX_LOG_CANNOT_OPEN, s.name);
        return;
    }
    const log_err_t err = log_text_load(&s.text, &src, TX_COLS);
    if (s.io.close != NULL) {
        s.io.close(s.io.ctx);
    }
    if (err != LOG_OK) {
        lv_say_err(err);
        return;
    }
    lv_quiet();
    s.view = VIEW_TEXT;
}

/*
 * OPEN on the selected row: a CSV file to the import view, a report
 * (log_report_number()) to the TEXT view (open_text()).  A folder and a file over
 * LOG_VIEWER_OPEN_MAX are not opened; the footer says why.  What the
 * import, the plot and the text held was another file's and is dropped.
 */
void lv_open_selected(void)
{
    if (s.sel < 0 || s.sel >= s.n_files) {
        return;
    }
    if (s.files[s.sel].is_dir) {
        /* Subdirectories are listed (storage_list applies the suffix filter
         * to files only).  There is no directory navigation, so opening one
         * reports a message rather than running the analysis on it. */
        lv_say(TX_LOG_IS_FOLDER, s.files[s.sel].name);
        return;
    }
    if (s.files[s.sel].size > LOG_VIEWER_OPEN_MAX) {
        /* Past what the reader's seek reaches.  Said, not read in part:
         * an import of the first 2GiB would look like the whole run. */
        lv_say(TX_LOG_TOO_BIG, s.files[s.sel].name);
        return;
    }
    snprintf(s.name, sizeof(s.name), "%s", s.files[s.sel].name);
    s.open_tab = s.tab;
    s.open_mounts = s.mounts;
    /* A report, its run's CSV beside it or not (log_select.h): its text,
     * and no import.  What the import and the plot held was another file's. */
    if (log_report_number(s.name) >= 0) {
        s.have_analysis = false;
        lv_drop_data();
        open_text();
        return;
    }
    log_text_free(&s.text);
    s.delim_index = -1;
    s.conv = LOG_CONV_AUTO;
    lv_run_analysis();
    s.view = VIEW_IMPORT;
}
