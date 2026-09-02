/*
 * The log viewer: browse the card, review the import, plot the file.
 *
 * Three views behind one tile: a file browser, an import view that shows
 * what the CSV (comma-separated values) reader made of the file, and the
 * plot.  The reader detects the delimiter, the decimal convention and the
 * time column (see shared/logfile); the import view shows the detection and
 * its evidence before anything is plotted, and offers overrides.
 *
 * compute_spans() turns the chosen columns into scaled traces when the data
 * changes, so the plot render walks prepared spans rather than reparsing on
 * every frame.
 *
 * SPDX-License-Identifier: MIT
 */

#include "log_viewer_screen.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ui_widgets.h"

#define SCREEN_W 800
/* The router owns the band and hands this screen a sub-canvas of what is
 * left, so the screen's own height is the body rather than the panel. */
#define SCREEN_H (480 - UI_BAND_H)
#define MAX_FBS  3

/* ------------------------------------------------------------- browse ---- */

/*
 * Layout in the 432 px body the router hands this screen (480 minus the
 * 48 px band).  The footer has to stay inside it for RESCAN, OPEN and PLOT
 * to be reachable.
 */
#define BR_LIST   gfx_rect_make(16, 36, 768, 352)
#define BR_ROW_H  44
/* (352 - 30) / 44 = 7 rows fit the list.  An eighth needs 382 px and would
 * cross the panel border into the footer. */
#define BR_ROWS   7

/* ------------------------------------------------------------- import ---- */

#define IM_LEFT   gfx_rect_make(16, 36, 300, 320)
#define IM_RIGHT  gfx_rect_make(328, 36, 456, 320)
#define IM_ROW_H  36
#define IM_ROWS   7
#define IM_BTN_Y  368
#define IM_BTN_H  46

/* --------------------------------------------------------------- plot ---- */

#define PV_PANEL  gfx_rect_make(16, 68, 768, 292)
#define PV_X      24
#define PV_Y      76
#define PV_W      752
#define PV_H      276
#define PV_BAND   10
#define PV_DIV_X  6
#define PV_DIV_Y  4

#define CHIP_Y    26
#define CHIP_H    36
#define CHIP_X    8
#define CHIP_W    190
#define CHIP_GAP  6

#define FOOT_Y    390
#define FOOT_H    42

typedef enum { VIEW_BROWSE = 0, VIEW_IMPORT, VIEW_PLOT } view_t;

static const ui_color_id_t k_series_color[LOG_MAX_SERIES] = {
    UI_C_VOLT, UI_C_CURR, UI_C_POWER, UI_C_RPM,
};

static struct {
    view_t view;
    log_viewer_io_t io;

    log_viewer_file_t files[LOG_VIEWER_MAX_FILES];
    int n_files;
    int listed;   /* -1 no volume, 0 not read, 1 read     */
    int sel;      /* highlighted file                     */
    int scroll;

    char name[LOG_VIEWER_NAME_MAX];
    char message[80];

    log_analysis_t an;
    bool have_analysis;
    int delim_index;      /* -1 auto, else index into log_csv_delimiters */
    log_conv_t conv;      /* LOG_CONV_AUTO respects the file            */
    int picked[LOG_MAX_SERIES];
    int n_picked;
    int col_scroll;

    log_data_t data;
    bool have_data;
    int cursor;

    /* press tracking, shared by the list views */
    bool pressing;
    int press_row;
    int press_y0;
    int press_scroll0;
    bool dragged;
    int press_btn;

    bool valid[MAX_FBS];
} s;

/* Column spans, in screen y, one pair per pixel column per series.  Computed
 * when the data changes: recomputing them per redraw would make the cursor
 * lag, and recomputing them per band would make it crawl. */
static int16_t s_span[LOG_MAX_SERIES][PV_W][2];
static gfx_color_t s_band[PV_W * PV_BAND];
static float s_lo[LOG_MAX_SERIES];
static float s_hi[LOG_MAX_SERIES];

log_viewer_view_t log_viewer_view(void)
{
    return (log_viewer_view_t)s.view;
}

const char *log_viewer_open_name(void)
{
    return s.name;
}

const log_analysis_t *log_viewer_analysis(void)
{
    return s.have_analysis ? &s.an : NULL;
}

const log_data_t *log_viewer_data(void)
{
    return s.have_data ? &s.data : NULL;
}

void log_viewer_invalidate(void)
{
    memset(s.valid, 0, sizeof(s.valid));
}

void log_viewer_set_io(const log_viewer_io_t *io)
{
    if (io != NULL) {
        s.io = *io;
    } else {
        memset(&s.io, 0, sizeof(s.io));
    }
    s.listed = 0;
    log_viewer_invalidate();
}

void log_viewer_refresh(void)
{
    s.n_files = 0;
    s.sel = -1;
    s.scroll = 0;
    if (s.io.list == NULL) {
        s.listed = -1;
    } else {
        int n = s.io.list(s.files, LOG_VIEWER_MAX_FILES, s.io.ctx);
        if (n < 0) {
            s.listed = -1;
        } else {
            s.n_files = n;
            s.listed = 1;
        }
    }
    log_viewer_invalidate();
}

static void drop_data(void)
{
    if (s.have_data) {
        log_data_free(&s.data);
        s.have_data = false;
    }
    s.cursor = -1;
}

/* ------------------------------------------------------------- loading --- */

static void set_message(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    /* The analyser cannot follow va_start through this wrapper and reports the
     * list as uninitialised; it is initialised on the line above. */
    /* NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized) */
    vsnprintf(s.message, sizeof(s.message), fmt, ap);
    va_end(ap);
}

static void run_analysis(void)
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
    drop_data();

    if (s.io.open == NULL) {
        set_message("no file source");
        return;
    }

    log_source_t src;
    if (!s.io.open(s.name, &src, s.io.ctx)) {
        set_message("cannot open %s", s.name);
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
        set_message("%s", log_err_str(err));
        return;
    }

    s.have_analysis = true;
    s.message[0] = '\0';

    /*
     * Keep what the operator chose: run_analysis() runs again from both
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

static void compute_spans(void)
{
    /* Zero rather than return: leaving the arrays alone would let the previous
     * file's trace be drawn under this file's legend. */
    memset(s_span, 0, sizeof(s_span));
    memset(s_lo, 0, sizeof(s_lo));
    memset(s_hi, 0, sizeof(s_hi));
    if (!s.have_data || s.data.count <= 0) {
        return;
    }
    int count = s.data.count;

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
        s_lo[k] = lo;
        s_hi[k] = hi;
        float span = hi - lo;

        const float *v = s.data.value[k];
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
                vmin = lo;
                vmax = lo;
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
            s_span[k][x][0] = (int16_t)y_hi;
            s_span[k][x][1] = (int16_t)y_lo;
        }

        /* Fewer samples than pixels: join neighbouring columns so the trace is
         * a line rather than a dotted row of one-pixel marks. */
        if (count < PV_W) {
            for (int x = 1; x < PV_W; ++x) {
                if (s_span[k][x][0] > s_span[k][x - 1][1]) {
                    s_span[k][x][0] = (int16_t)(s_span[k][x - 1][1]);
                } else if (s_span[k][x][1] < s_span[k][x - 1][0]) {
                    s_span[k][x][1] = (int16_t)(s_span[k][x - 1][0]);
                }
            }
        }
    }
}

static void load_data(void)
{
    drop_data();
    if (!s.have_analysis || s.io.open == NULL) {
        return;
    }

    log_source_t src;
    if (!s.io.open(s.name, &src, s.io.ctx)) {
        set_message("cannot open %s", s.name);
        return;
    }
    log_err_t err = log_csv_build(&src, &s.an, s.picked, s.n_picked, &s.data);
    if (s.io.close != NULL) {
        s.io.close(s.io.ctx);
    }

    if (err != LOG_OK) {
        set_message("%s", log_err_str(err));
        return;
    }

    s.have_data = true;
    s.cursor = s.data.count - 1;
    s.message[0] = '\0';
    compute_spans();
    s.view = VIEW_PLOT;
}

static void open_selected(void)
{
    if (s.sel < 0 || s.sel >= s.n_files) {
        return;
    }
    if (s.files[s.sel].is_dir) {
        /* Subdirectories are listed (storage_list applies the suffix filter
         * to files only).  There is no directory navigation, so opening one
         * reports a message rather than running the analysis on it. */
        set_message("%s is a folder", s.files[s.sel].name);
        return;
    }
    snprintf(s.name, sizeof(s.name), "%s", s.files[s.sel].name);
    s.delim_index = -1;
    s.conv = LOG_CONV_AUTO;
    run_analysis();
    s.view = VIEW_IMPORT;
}

/* -------------------------------------------------------------- browse --- */

static void fmt_size(char *out, size_t n, uint32_t bytes)
{
    if (bytes >= 1024u * 1024u) {
        snprintf(out, n, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024u) {
        snprintf(out, n, "%u kB", (unsigned)(bytes / 1024u));
    } else {
        snprintf(out, n, "%u B", (unsigned)bytes);
    }
}

static void render_browse(gfx_canvas_t *c)
{
    const char *vol = (s.io.volume != NULL) ? s.io.volume(s.io.ctx) : NULL;
    char head[64];
    snprintf(head, sizeof(head), "%s", (vol != NULL && vol[0] != '\0') ? vol
                                                                      : "SD CARD");
    gfx_text(c, 300, 12, head, UI_FONT_LABEL, UI_TEXT_DIM, 1);

    if (s.listed != 1 || s.n_files == 0) {
        gfx_rect_t box = gfx_rect_make(120, 160, 560, 160);
        ui_panel(c, box, "NO LOGS", UI_WARN);
        const char *why = (s.listed == -1) ? "No card. Insert one and tap RESCAN."
                                           : "No .csv or .bfl files in the root.";
        gfx_text(c, box.x + 24, box.y + 52, why, UI_FONT_LABEL, UI_TEXT, 1);
        gfx_text(c, box.x + 24, box.y + 76,
                 "The data logger writes here; so does Betaflight.",
                 UI_FONT_LABEL, UI_TEXT_FAINT, 1);
    } else {
        ui_panel(c, BR_LIST, "FILES", UI_ACCENT);
        int y = BR_LIST.y + 30;
        for (int r = 0; r < BR_ROWS; ++r) {
            int i = s.scroll + r;
            if (i >= s.n_files) {
                break;
            }
            gfx_rect_t row = gfx_rect_make(BR_LIST.x + 8, y, BR_LIST.w - 16,
                                           BR_ROW_H - 4);
            bool on = (i == s.sel);
            gfx_fill_chamfer_rect(c, row.x, row.y, row.w, row.h, UI_CHAMFER_SM,
                                  on ? UI_PANEL_HI : UI_PANEL_SUNK);
            if (on) {
                gfx_fill_rect(c, row.x, row.y, 4, row.h, UI_ACCENT);
            }
            gfx_text(c, row.x + 16, row.y + 10, s.files[i].name, UI_FONT_LABEL,
                     on ? UI_TEXT : UI_TEXT_DIM, 1);

            char size[16];
            fmt_size(size, sizeof(size), s.files[i].size);
            gfx_rect_t sz = gfx_rect_make(row.x + row.w - 132, row.y, 120, row.h);
            gfx_text_in(c, sz, s.files[i].is_dir ? "DIR" : size, UI_FONT_LABEL,
                        UI_TEXT_FAINT, 1, GFX_ALIGN_RIGHT);
            y += BR_ROW_H;
        }
    }

    ui_button(c, gfx_rect_make(16, FOOT_Y + 2, 150, FOOT_H), "RESCAN",
              UI_PANEL_HI, s.press_btn == 0, true);
    if (s.n_files > 0) {
        ui_button(c, gfx_rect_make(634, FOOT_Y + 2, 150, FOOT_H), "OPEN",
                  UI_ACCENT, s.press_btn == 1, s.sel >= 0);
    }
    if (s.message[0] != '\0') {
        gfx_text(c, 180, FOOT_Y + 14, s.message, UI_FONT_LABEL, UI_DANGER, 1);
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
    gfx_text(c, 300, 12, s.name, UI_FONT_LABEL, UI_TEXT_DIM, 1);

    if (!s.have_analysis) {
        gfx_rect_t box = gfx_rect_make(120, 160, 560, 150);
        ui_panel(c, box, "CANNOT READ", UI_DANGER);
        gfx_text(c, box.x + 24, box.y + 52,
                 s.message[0] ? s.message : "unreadable", UI_FONT_LABEL,
                 UI_TEXT, 1);
        gfx_text(c, box.x + 24, box.y + 76,
                 "Try another delimiter below, or a different file.",
                 UI_FONT_LABEL, UI_TEXT_FAINT, 1);
    } else {
        ui_panel(c, IM_LEFT, "DETECTED", UI_ACCENT);

        char line[64];
        int y = IM_LEFT.y + 38;
        const int lh = 24;

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
                 log_csv_delimiter_label(s.an.delimiter));
        rows[n].key = "SEPARATOR";
        rows[n].color = UI_TEXT;
        n++;

        snprintf(rows[n].value, sizeof(rows[n].value), "%s%s",
                 (s.an.convention == LOG_CONV_DE) ? "GERMAN" : "ENGLISH",
                 s.an.convention_forced ? " (SET)"
                                        : (s.an.convention_confident ? "" : " (?)"));
        rows[n].key = "NUMBERS";
        rows[n].color = (s.an.convention_forced || s.an.convention_confident)
                            ? UI_TEXT
                            : UI_WARN;
        n++;

        snprintf(rows[n].value, sizeof(rows[n].value), "%d%s", s.an.row_count,
                 s.an.truncated ? " (CAPPED)" : "");
        rows[n].key = "ROWS";
        rows[n].color = s.an.truncated ? UI_WARN : UI_TEXT;
        n++;

        snprintf(rows[n].value, sizeof(rows[n].value), "%d", s.an.n_columns);
        rows[n].key = "COLUMNS";
        rows[n].color = UI_TEXT;
        n++;

        if (s.an.time_index >= 0) {
            snprintf(rows[n].value, sizeof(rows[n].value), "%s %s",
                     s.an.columns[s.an.time_index].name, s.an.time_unit);
        } else {
            snprintf(rows[n].value, sizeof(rows[n].value), "ROW INDEX");
        }
        rows[n].key = "TIME AXIS";
        rows[n].color = (s.an.time_index >= 0) ? UI_TEXT : UI_WARN;
        n++;

        snprintf(rows[n].value, sizeof(rows[n].value), "%d%s", s.an.ragged_rows,
                 (s.an.long_rows > 0) ? " +LONG" : "");
        rows[n].key = "RAGGED";
        rows[n].color = (s.an.ragged_rows > 0 || s.an.long_rows > 0) ? UI_DANGER
                                                                     : UI_TEXT;
        n++;

        for (int i = 0; i < n; ++i) {
            gfx_text(c, IM_LEFT.x + 16, y, rows[i].key, UI_FONT_LABEL,
                     UI_TEXT_FAINT, 1);
            gfx_rect_t box = gfx_rect_make(IM_LEFT.x + 120, y - 2,
                                           IM_LEFT.w - 136, 20);
            gfx_text_in(c, box, rows[i].value, UI_FONT_LABEL, rows[i].color, 1,
                        GFX_ALIGN_RIGHT);
            y += lh;
        }

        ui_rule(c, IM_LEFT.x + 16, y + 2, IM_LEFT.w - 32, UI_EDGE);
        y += 14;

        /* The evidence behind the detection. */
        snprintf(line, sizeof(line), "EVIDENCE  de %d  en %d  amb %d",
                 s.an.votes.de, s.an.votes.en, s.an.votes.ambiguous);
        gfx_text(c, IM_LEFT.x + 16, y, line, UI_FONT_LABEL, UI_TEXT_FAINT, 1);
        y += 20;

        if (s.an.convention_conflict) {
            gfx_text(c, IM_LEFT.x + 16, y, "BOTH PROVEN - FILE IS MIXED",
                     UI_FONT_LABEL, UI_DANGER, 1);
        } else if (s.an.long_rows > 0) {
            /* Those rows lost every cell after the overflow, and a lost cell
             * looks exactly like a blank one further down. */
            snprintf(line, sizeof(line), "%d ROW(S) TOO LONG - CELLS LOST",
                     s.an.long_rows);
            gfx_text(c, IM_LEFT.x + 16, y, line, UI_FONT_LABEL, UI_DANGER, 1);
        } else if (s.an.ragged_rows > 0) {
            gfx_text(c, IM_LEFT.x + 16, y, "ROWS DIFFER - WRONG SEPARATOR?",
                     UI_FONT_LABEL, UI_DANGER, 1);
        } else if (!s.an.convention_confident) {
            gfx_text(c, IM_LEFT.x + 16, y, "NOTHING SETTLED IT - CHECK BELOW",
                     UI_FONT_LABEL, UI_WARN, 1);
        }

        /* The override controls for the two detections above. */
        int hy = IM_LEFT.y + IM_LEFT.h - 60;
        ui_rule(c, IM_LEFT.x + 16, hy - 10, IM_LEFT.w - 32, UI_EDGE);
        gfx_text(c, IM_LEFT.x + 16, hy, "VALUES LOOK WRONG?  CHANGE NUM",
                 UI_FONT_LABEL, UI_TEXT_FAINT, 1);
        gfx_text(c, IM_LEFT.x + 16, hy + 18, "COLUMNS LOOK WRONG?  CHANGE SEP",
                 UI_FONT_LABEL, UI_TEXT_FAINT, 1);

        /* --- columns -------------------------------------------------- */
        char title[32];
        snprintf(title, sizeof(title), "PLOT  %d/%d", s.n_picked,
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
                               : (slot >= 0) ? ui_theme_color(k_series_color[slot])
                               : col->numeric ? UI_EDGE_HI
                                              : UI_PANEL;
            gfx_fill_rect(c, row.x, row.y, 5, row.h, mark);

            gfx_color_t text = col->numeric ? UI_TEXT : UI_TEXT_FAINT;
            gfx_text(c, row.x + 16, row.y + 8, col->name, UI_FONT_LABEL, text, 1);

            /* The file's own first value, next to what was made of it: the
             * cheapest way to see a mis-read before plotting it. */
            gfx_rect_t sb = gfx_rect_make(row.x + 168, row.y, 110, row.h);
            gfx_text_in(c, sb, col->sample[0], UI_FONT_LABEL, UI_TEXT_DIM, 1,
                        GFX_ALIGN_RIGHT);

            char right[40];
            if (is_time) {
                snprintf(right, sizeof(right), "TIME");
            } else if (!col->numeric) {
                snprintf(right, sizeof(right), "TEXT");
            } else {
                char lo[12];
                char hi[12];
                ui_fmt(lo, sizeof(lo), (float)col->min, 1);
                ui_fmt(hi, sizeof(hi), (float)col->max, 1);
                snprintf(right, sizeof(right), "%s..%s%s", lo, hi, col->unit);
            }
            gfx_rect_t rb = gfx_rect_make(row.x + 288, row.y, row.w - 300,
                                          row.h);
            gfx_text_in(c, rb, right, UI_FONT_LABEL,
                        is_time ? UI_TEAL : UI_TEXT_FAINT, 1, GFX_ALIGN_RIGHT);
            ry += IM_ROW_H;
        }
    }

    /* --- controls ------------------------------------------------------ */
    char btn[24];
    snprintf(btn, sizeof(btn), "SEP %s",
             (s.delim_index < 0) ? "AUTO"
                                 : log_csv_delimiter_label(
                                       log_csv_delimiters[s.delim_index]));
    ui_button(c, gfx_rect_make(16, IM_BTN_Y, 190, IM_BTN_H), btn, UI_PANEL_HI,
              s.press_btn == 0, true);

    snprintf(btn, sizeof(btn), "NUM %s", conv_label(s.conv));
    ui_button(c, gfx_rect_make(214, IM_BTN_Y, 190, IM_BTN_H), btn, UI_PANEL_HI,
              s.press_btn == 1, true);

    ui_button(c, gfx_rect_make(412, IM_BTN_Y, 180, IM_BTN_H), "BACK",
              UI_PANEL_HI, s.press_btn == 2, true);

    ui_button(c, gfx_rect_make(600, IM_BTN_Y, 184, IM_BTN_H), "PLOT", UI_ACCENT,
              s.press_btn == 3, s.have_analysis && s.n_picked > 0);
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
    gfx_color_t col = ui_theme_color(k_series_color[k]);
    for (int x = 0; x < PV_W; ++x) {
        int y0 = s_span[k][x][0];
        int y1 = s_span[k][x][1];
        if (y1 < band_y0 || y0 >= band_y0 + rows) {
            continue;
        }
        int a = (y0 < band_y0) ? band_y0 : y0;
        int b = (y1 >= band_y0 + rows) ? band_y0 + rows - 1 : y1;
        gfx_vline(band, x, a - band_y0, b - a + 1, col);
    }
}

static int cursor_x(void)
{
    if (!s.have_data || s.data.count <= 1 || s.cursor < 0) {
        return -1;
    }
    return PV_X + (int)(((long)s.cursor * (PV_W - 1)) / (s.data.count - 1));
}

static void render_plot(gfx_canvas_t *c)
{
    char line[160];

    if (!s.have_data) {
        gfx_rect_t box = gfx_rect_make(120, 170, 560, 140);
        ui_panel(c, box, "NOTHING LOADED", UI_WARN);
        gfx_text(c, box.x + 24, box.y + 52,
                 s.message[0] ? s.message : "Pick a file first.",
                 UI_FONT_LABEL, UI_TEXT, 1);
        return;
    }

    snprintf(line, sizeof(line), "%s   %d ROWS   %.2f s   %.0f Hz", s.name,
             s.data.count, s.data.duration_s, s.data.rate_hz);
    gfx_text(c, 260, 12, line, UI_FONT_LABEL, UI_TEXT_DIM, 1);

    /* --- legend, doubling as the cursor readout ------------------------ */
    int ci = (s.cursor >= 0 && s.cursor < s.data.count) ? s.cursor : 0;
    for (int k = 0; k < s.data.n_fields; ++k) {
        gfx_rect_t chip = gfx_rect_make(CHIP_X + k * (CHIP_W + CHIP_GAP), CHIP_Y,
                                        CHIP_W, CHIP_H);
        gfx_color_t col = ui_theme_color(k_series_color[k]);
        gfx_fill_chamfer_rect(c, chip.x, chip.y, chip.w, chip.h, UI_CHAMFER_SM,
                              UI_PANEL);
        gfx_fill_rect(c, chip.x, chip.y, 5, chip.h, col);
        gfx_text(c, chip.x + 12, chip.y + 3, s.data.field[k].name,
                 UI_FONT_LABEL, UI_TEXT, 1);

        char val[16];
        ui_fmt(val, sizeof(val), s.data.value[k][ci], 2);
        snprintf(line, sizeof(line), "%s%s", val, s.data.field[k].unit);
        gfx_rect_t box = gfx_rect_make(chip.x + 12, chip.y + 18, chip.w - 24, 16);
        gfx_text_in(c, box, line, UI_FONT_LABEL, col, 1, GFX_ALIGN_RIGHT);

        char lo[10];
        char hi[10];
        ui_fmt(lo, sizeof(lo), s_lo[k], 0);
        ui_fmt(hi, sizeof(hi), s_hi[k], 0);
        snprintf(line, sizeof(line), "%s-%s", lo, hi);
        gfx_text(c, chip.x + 12, chip.y + 19, line, UI_FONT_LABEL,
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
        gfx_canvas_init(&band, s_band, PV_W, rows, PV_W);
        gfx_clear(&band, UI_PANEL_SUNK);
        draw_grid_band(&band, PV_Y + by, rows);
        for (int k = 0; k < s.data.n_fields; ++k) {
            draw_trace_band(&band, k, PV_Y + by, rows);
        }
        gfx_blit(c, PV_X, PV_Y + by, s_band, PV_W, rows, PV_W);
    }

    int cx = cursor_x();
    if (cx >= 0) {
        gfx_vline(c, cx, PV_Y, PV_H, UI_TEXT_DIM);
        for (int k = 0; k < s.data.n_fields; ++k) {
            int x = cx - PV_X;
            int y = (s_span[k][x][0] + s_span[k][x][1]) / 2;
            gfx_fill_circle(c, cx, y, 3, ui_theme_color(k_series_color[k]));
        }
    }

    /* --- time axis ------------------------------------------------------ */
    for (int i = 0; i <= PV_DIV_X; ++i) {
        int idx = (s.data.count - 1) * i / PV_DIV_X;
        char t[16];
        ui_fmt(t, sizeof(t), s.data.time[idx], 2);
        int x = PV_X + (PV_W - 1) * i / PV_DIV_X;
        int w = gfx_text_width(UI_FONT_LABEL, t, 1);
        int tx = x - w / 2;
        if (tx < PV_X) {
            tx = PV_X;
        }
        if (tx + w > PV_X + PV_W) {
            tx = PV_X + PV_W - w;
        }
        gfx_text(c, tx, PV_PANEL.y + PV_PANEL.h + 4, t, UI_FONT_LABEL,
                 UI_TEXT_FAINT, 1);
    }

    /* --- footer --------------------------------------------------------- */
    ui_button(c, gfx_rect_make(16, FOOT_Y, 170, FOOT_H), "FIELDS", UI_PANEL_HI,
              s.press_btn == 0, true);
    ui_button(c, gfx_rect_make(196, FOOT_Y, 80, FOOT_H), "<", UI_PANEL_HI,
              s.press_btn == 1, true);
    ui_button(c, gfx_rect_make(284, FOOT_Y, 80, FOOT_H), ">", UI_PANEL_HI,
              s.press_btn == 2, true);

    if (s.data.time_name[0] != '\0') {
        snprintf(line, sizeof(line), "t = %.3f s   sample %d/%d",
                 (double)s.data.time[ci], ci + 1, s.data.count);
    } else {
        /* No time column, so the x axis is the row number.  Say that rather
         * than printing a seconds value that is really an index. */
        snprintf(line, sizeof(line), "row %d of %d   (no time column)", ci + 1,
                 s.data.count);
    }
    gfx_text(c, 384, FOOT_Y + 13, line, UI_FONT_LABEL, UI_TEXT_DIM, 1);

    if (s.data.unparsed_cells > 0) {
        snprintf(line, sizeof(line), "%d CELLS UNREADABLE",
                 s.data.unparsed_cells);
        gfx_text(c, 384, FOOT_Y + 27, line, UI_FONT_LABEL, UI_WARN, 1);
    }
}

/* --------------------------------------------------------------- events -- */

static int hit_button(const gfx_rect_t *rects, int n, int x, int y)
{
    for (int i = 0; i < n; ++i) {
        if (gfx_rect_contains(rects[i], x, y)) {
            return i;
        }
    }
    return -1;
}

static void clamp_scroll(int *scroll, int total, int visible)
{
    int max = total - visible;
    if (max < 0) {
        max = 0;
    }
    if (*scroll > max) {
        *scroll = max;
    }
    if (*scroll < 0) {
        *scroll = 0;
    }
}

#define DRAG_SLOP 8

static void browse_event(const touch_event_t *e)
{
    const gfx_rect_t btns[2] = {
        gfx_rect_make(16, FOOT_Y + 2, 150, FOOT_H),
        gfx_rect_make(634, FOOT_Y + 2, 150, FOOT_H),
    };

    switch (e->type) {
    case TOUCH_EVENT_DOWN:
        s.press_btn = hit_button(btns, 2, e->point.x, e->point.y);
        s.press_row = -1;
        s.dragged = false;
        if (s.press_btn < 0 && gfx_rect_contains(BR_LIST, e->point.x, e->point.y)) {
            s.pressing = true;
            s.press_y0 = e->point.y;
            s.press_scroll0 = s.scroll;
            int rel = e->point.y - (BR_LIST.y + 30);
            int r = (rel >= 0) ? rel / BR_ROW_H : -1;
            if (r >= 0 && r < BR_ROWS) {
                s.press_row = s.scroll + r;
            }
        }
        log_viewer_invalidate();
        break;

    case TOUCH_EVENT_MOVE:
        if (s.pressing) {
            int dy = e->point.y - s.press_y0;
            if (dy > DRAG_SLOP || dy < -DRAG_SLOP) {
                s.dragged = true;
                s.scroll = s.press_scroll0 - dy / BR_ROW_H;
                clamp_scroll(&s.scroll, s.n_files, BR_ROWS);
                log_viewer_invalidate();
            }
        }
        break;

    case TOUCH_EVENT_UP:
        if (s.press_btn == 0) {
            log_viewer_refresh();
        } else if (s.press_btn == 1) {
            open_selected();
        } else if (s.pressing && !s.dragged && s.press_row >= 0 &&
                   s.press_row < s.n_files) {
            if (s.sel == s.press_row) {
                open_selected(); /* second tap opens */
            } else {
                s.sel = s.press_row;
            }
        }
        s.pressing = false;
        s.press_btn = -1;
        log_viewer_invalidate();
        break;
    }
}

static void toggle_column(int i)
{
    if (i < 0 || i >= s.an.n_columns || i == s.an.time_index) {
        return;
    }
    for (int k = 0; k < s.n_picked; ++k) {
        if (s.picked[k] == i) {
            for (int j = k; j + 1 < s.n_picked; ++j) {
                s.picked[j] = s.picked[j + 1];
            }
            s.n_picked--;
            return;
        }
    }
    if (!s.an.columns[i].numeric || s.n_picked >= LOG_MAX_SERIES) {
        return;
    }
    s.picked[s.n_picked++] = i;
}

static void import_event(const touch_event_t *e)
{
    const gfx_rect_t btns[4] = {
        gfx_rect_make(16, IM_BTN_Y, 190, IM_BTN_H),
        gfx_rect_make(214, IM_BTN_Y, 190, IM_BTN_H),
        gfx_rect_make(412, IM_BTN_Y, 180, IM_BTN_H),
        gfx_rect_make(600, IM_BTN_Y, 184, IM_BTN_H),
    };

    switch (e->type) {
    case TOUCH_EVENT_DOWN:
        s.press_btn = hit_button(btns, 4, e->point.x, e->point.y);
        s.press_row = -1;
        s.dragged = false;
        if (s.press_btn < 0 && s.have_analysis &&
            gfx_rect_contains(IM_RIGHT, e->point.x, e->point.y)) {
            s.pressing = true;
            s.press_y0 = e->point.y;
            s.press_scroll0 = s.col_scroll;
            int rel = e->point.y - (IM_RIGHT.y + 30);
            int r = (rel >= 0) ? rel / IM_ROW_H : -1;
            if (r >= 0 && r < IM_ROWS) {
                s.press_row = s.col_scroll + r;
            }
        }
        log_viewer_invalidate();
        break;

    case TOUCH_EVENT_MOVE:
        if (s.pressing) {
            int dy = e->point.y - s.press_y0;
            if (dy > DRAG_SLOP || dy < -DRAG_SLOP) {
                s.dragged = true;
                s.col_scroll = s.press_scroll0 - dy / IM_ROW_H;
                clamp_scroll(&s.col_scroll, s.an.n_columns, IM_ROWS);
                log_viewer_invalidate();
            }
        }
        break;

    case TOUCH_EVENT_UP:
        switch (s.press_btn) {
        case 0:
            /* AUTO, then each candidate in turn. */
            s.delim_index++;
            if (s.delim_index >= (int)sizeof(log_csv_delimiters)) {
                s.delim_index = -1;
            }
            run_analysis();
            break;
        case 1:
            s.conv = (s.conv == LOG_CONV_AUTO)  ? LOG_CONV_DE
                     : (s.conv == LOG_CONV_DE) ? LOG_CONV_EN
                                                : LOG_CONV_AUTO;
            run_analysis();
            break;
        case 2:
            s.view = VIEW_BROWSE;
            break;
        case 3:
            if (s.have_analysis && s.n_picked > 0) {
                load_data();
            }
            break;
        default:
            if (s.pressing && !s.dragged && s.press_row >= 0) {
                toggle_column(s.press_row);
            }
            break;
        }
        s.pressing = false;
        s.press_btn = -1;
        log_viewer_invalidate();
        break;
    }
}

static void set_cursor_from_x(int x)
{
    if (!s.have_data || s.data.count <= 1) {
        return;
    }
    int rel = x - PV_X;
    if (rel < 0) {
        rel = 0;
    }
    if (rel > PV_W - 1) {
        rel = PV_W - 1;
    }
    s.cursor = (int)(((long)rel * (s.data.count - 1)) / (PV_W - 1));
}

static void plot_event(const touch_event_t *e)
{
    const gfx_rect_t btns[3] = {
        gfx_rect_make(16, FOOT_Y, 170, FOOT_H),
        gfx_rect_make(196, FOOT_Y, 80, FOOT_H),
        gfx_rect_make(284, FOOT_Y, 80, FOOT_H),
    };

    switch (e->type) {
    case TOUCH_EVENT_DOWN:
        s.press_btn = hit_button(btns, 3, e->point.x, e->point.y);
        if (s.press_btn < 0 && gfx_rect_contains(PV_PANEL, e->point.x, e->point.y)) {
            s.pressing = true;
            set_cursor_from_x(e->point.x);
        }
        log_viewer_invalidate();
        break;

    case TOUCH_EVENT_MOVE:
        if (s.pressing) {
            set_cursor_from_x(e->point.x);
            log_viewer_invalidate();
        }
        break;

    case TOUCH_EVENT_UP:
        if (s.press_btn == 0) {
            s.view = VIEW_IMPORT;
        } else if (s.press_btn == 1 && s.cursor > 0) {
            s.cursor--;
        } else if (s.press_btn == 2 && s.have_data &&
                   s.cursor < s.data.count - 1) {
            s.cursor++;
        }
        s.pressing = false;
        s.press_btn = -1;
        log_viewer_invalidate();
        break;
    }
}

/* ---------------------------------------------------------------- screen -- */

static void render(gfx_canvas_t *c, int buffer_index)
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

static void event(const touch_event_t *e)
{
    if (e == NULL) {
        return;
    }
    switch (s.view) {
    case VIEW_IMPORT:
        import_event(e);
        break;
    case VIEW_PLOT:
        plot_event(e);
        break;
    case VIEW_BROWSE:
    default:
        browse_event(e);
        break;
    }
}

static void reset(void)
{
    log_viewer_io_t io = s.io;
    drop_data();
    memset(&s, 0, sizeof(s));
    s.io = io;
    s.sel = -1;
    s.cursor = -1;
    s.press_btn = -1;
    s.delim_index = -1;
    s.conv = LOG_CONV_AUTO;
}

static void enter(void)
{
    if (s.listed == 0) {
        log_viewer_refresh();
    }
    log_viewer_invalidate();
}

static const ui_screen_t s_screen = {
    .title = "LOG VIEWER",
    .reset = reset,
    .enter = enter,
    .leave = NULL,
    .tick = NULL,
    .event = event,
    .render = render,
};

const ui_screen_t *log_viewer_screen(void)
{
    return &s_screen;
}
