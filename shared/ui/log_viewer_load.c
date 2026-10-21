/*
 * The log viewer's loading: a file the browse view opens is analysed (its
 * delimiter, decimal convention and time column, see shared/logfile), its
 * chosen columns are read, and lv_compute_spans() turns them into the
 * plot's scaled traces.  log_viewer_screen.c holds the state, the card's
 * listing and paging and the screen's interface (log_viewer_screen.h);
 * log_viewer_int.h is what the screen's four files share.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "log_viewer_screen.h"
#include "settings.h"
#include "ui_screen.h"
#include "ui_text.h"

#include "log_viewer_int.h"

/* ------------------------------------------------------------- loading --- */

void lv_set_message(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    /* The analyser cannot follow va_start through this wrapper and reports the
     * list as uninitialised; it is initialised on the line above. */
    /* NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized) */
    vsnprintf(s.message, sizeof(s.message), fmt, ap);
    va_end(ap);
    s.message_ok = false;
}

/* The reader's verdict in the language showing; log_err_str() is the
 * English the console logs. */
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
        lv_set_message("%s", TR(LOG_NO_SOURCE));
        return;
    }

    log_source_t src;
    lv_point(s.open_tab);
    if (!open_name(&src)) {
        lv_set_message(TR(LOG_CANNOT_OPEN), s.name);
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
        lv_set_message("%s", err_text(err));
        return;
    }

    s.have_analysis = true;
    s.message[0] = '\0';

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
     * column): fall back to the first numeric columns that are not the time
     * axis. */
    if (s.n_picked == 0) {
        s.col_scroll = 0;
        for (int c = 0; c < s.an.n_columns && s.n_picked < LOG_MAX_SERIES; ++c) {
            if (c != s.an.time_index && s.an.columns[c].numeric) {
                s.picked[s.n_picked++] = c;
            }
        }
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
        lv_set_message(TR(LOG_CANNOT_OPEN), s.name);
        return;
    }
    log_err_t err = log_csv_build(&src, &s.an, s.picked, s.n_picked, &s.data);
    if (s.io.close != NULL) {
        s.io.close(s.io.ctx);
    }

    if (err != LOG_OK) {
        lv_set_message("%s", err_text(err));
        return;
    }

    s.have_data = true;
    s.cursor = s.data.count - 1;
    s.win0 = 0;
    s.win_n = s.data.count;
    s.pinch = false;
    s.panning = false;
    s.message[0] = '\0';
    lv_compute_spans();
    s.view = VIEW_PLOT;
}

void lv_open_selected(void)
{
    if (s.sel < 0 || s.sel >= s.n_files) {
        return;
    }
    if (s.files[s.sel].is_dir) {
        /* Subdirectories are listed (storage_list applies the suffix filter
         * to files only).  There is no directory navigation, so opening one
         * reports a message rather than running the analysis on it. */
        lv_set_message(TR(LOG_IS_FOLDER), s.files[s.sel].name);
        return;
    }
    if (s.files[s.sel].size > LOG_VIEWER_OPEN_MAX) {
        /* Past what the reader's seek reaches.  Said, not read in part:
         * an import of the first 2 GiB would look like the whole run. */
        lv_set_message(TR(LOG_TOO_BIG), s.files[s.sel].name);
        return;
    }
    snprintf(s.name, sizeof(s.name), "%s", s.files[s.sel].name);
    s.open_tab = s.tab;
    s.open_mounts = s.mounts;
    s.delim_index = -1;
    s.conv = LOG_CONV_AUTO;
    lv_run_analysis();
    s.view = VIEW_IMPORT;
}
