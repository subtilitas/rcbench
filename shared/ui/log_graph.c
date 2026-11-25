/**
 * @file log_graph.c
 * @brief LOGS' graph of a file the bench wrote: the load beside the plot's,
 *        the cursor, the chips' focus and the touches.  See
 *        log_graph_int.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "log_fields.h"
#include "log_graph.h"
#include "log_preset.h"
#include "ui_text.h"

#include "log_graph_int.h"

lg_state_t lg;

/** @brief RUDDR's two tabs, in the order of their presets. */
static const ui_text_id_t k_tab_text[2] = {
    TX_LOG_TAB_SEARCH, TX_LOG_TAB_STEPS,
};

bool lg_on(void)
{
    return lg.on && s.have_data;
}

bool lg_drawn(int i)
{
    return i >= 0 && i < lg.mk.rows
           && (lg.mk.row[i] & LOG_ROW_TRACE) != LOG_TRACE_NONE;
}

int lg_k0(void)
{
    return lg.time ? 0 : 1;
}

/**
 * @brief Free the strips' cells.
 */
static void free_cells(void)
{
    log_mem_free(lg.mk.cells);
    lg.mk.cells = NULL;
    lg.mk.cells_cap = 0;
}

/**
 * @brief Whether the file the plot just built suits its preset's graph: an
 *        XY file of at most LOG_MARKS_ROWS rows with its x column first, a
 *        time file with a series.
 * @param p  The preset.
 * @return true when it does.
 */
static bool suits(const log_preset_t *p)
{
    if (p == NULL || s.data.n_fields < 1) {
        return false;
    }
    if (p->plot == LOG_PLOT_TIME) {
        return true;
    }
    return s.data.n_fields >= 2 && s.data.count <= LOG_MARKS_ROWS
           && s.data.field[0].column == log_fields_find(&s.an, p->x, true);
}

bool lg_read(log_source_t *src)
{
    lg.on = false;
    lg.time = false;
    free_cells();
    const log_preset_t *p = log_preset_get(s.preset);
    if (!suits(p)) {
        return false;
    }
    lg.time = p->plot == LOG_PLOT_TIME;
    if (lg.time && s.an.row_count > 0) {
        /* A byte a row for the strips' codes; without it, no strips. */
        lg.mk.cells = (uint8_t *)log_mem_alloc((size_t)s.an.row_count);
        lg.mk.cells_cap = (lg.mk.cells != NULL) ? s.an.row_count : 0;
    }
    if (log_marks_read(src, &s.an, s.preset, &lg.mk) != LOG_OK
        || lg.mk.rows != s.data.count) {
        free_cells();
        return false;
    }
    if (lg.time) {
        lg_time_setup();
    }
    lg.on = true;
    return true;
}

void lg_start(void)
{
    if (!lg.on) {
        return;
    }
    lg.focus = -1;
    memset(lg.hidden, 0, sizeof(lg.hidden));
    lg.press = LG_PRESS_NONE;
    lg.tabbed = false;
    if (lg.time) {
        /* The cursor on the run's end, as the plain time plot's. */
        return;
    }
    lg.plot_h = PV_H;
    lg_scale();
    /* The cursor on the last row drawn: the run's end. */
    for (int i = s.data.count - 1; i >= 0; --i) {
        if (lg_drawn(i)) {
            s.cursor = i;
            break;
        }
    }
    lg.tabbed = s.preset == LOG_PRESET_RUDDR_SEARCH
                || s.preset == LOG_PRESET_RUDDR_STEPS;
    if (lg.tabbed) {
        ui_tabs_init_text(&lg.tabs, k_tab_text, 2, LG_TABS);
        lg.tabs.selected = (s.preset == LOG_PRESET_RUDDR_STEPS) ? 1 : 0;
    }
}

void lg_drop(void)
{
    lg.on = false;
    lg.press = LG_PRESS_NONE;
    free_cells();
}

void lg_cancel(void)
{
    lg.press = LG_PRESS_NONE;
    if (lg.tabbed) {
        ui_tabs_cancel(&lg.tabs);
    }
}

bool lg_step(int dir)
{
    if (!lg_on() || lg.time) {
        /* A time file steps a row at a time, as the plain plot. */
        return false;
    }
    for (int i = s.cursor + dir; i >= 0 && i < s.data.count; i += dir) {
        if (lg_drawn(i)) {
            s.cursor = i;
            break;
        }
    }
    return true;
}

/**
 * @brief A tap on series @p k's chip: focus it; a second tap hides it; a
 *        tap on a hidden series brings it back focused.  The bench screens'
 *        legend does the same (ui_plot_touch_series()).
 * @param k  The series, lg_k0() to n_fields - 1.
 */
static void chip_tap(int k)
{
    if (k < lg_k0() || k >= LOG_MAX_SERIES) {
        return;
    }
    if (lg.hidden[k]) {
        lg.hidden[k] = false;
        lg.focus = k;
    } else if (lg.focus == k) {
        lg.hidden[k] = true;
        lg.focus = -1;
    } else {
        lg.focus = k;
    }
}

/**
 * @brief The chip of the series under (@p x, @p y).
 * @param x  Pixel column.
 * @param y  Pixel row.
 * @return the series, lg_k0() to n_fields - 1; -1 for none.
 */
static int chip_at(int x, int y)
{
    for (int k = lg_k0(); k < s.data.n_fields; ++k) {
        if (gfx_rect_contains(LG_SLOT(k - lg_k0()), x, y)) {
            return k;
        }
    }
    return -1;
}

/**
 * @brief Show RUDDR's other picture: its preset's columns loaded, the
 *        graph set up again.
 * @param id  LOG_PRESET_RUDDR_SEARCH or LOG_PRESET_RUDDR_STEPS.
 */
static void show_picture(log_preset_id_t id)
{
    int picks[LOG_MAX_SERIES] = { 0 };
    const int n = log_preset_picks(id, &s.an, picks, LOG_MAX_SERIES);
    if (n <= 0) {
        return;
    }
    s.preset = id;
    memcpy(s.picked, picks, sizeof(picks));
    s.n_picked = n;
    lv_load_data();
}

/**
 * @brief An event of the contact that holds the graph's press: MOVE drags
 *        the cursor, UP ends the press and taps a chip.
 * @param e  The event.
 */
static void own_press(const touch_event_t *e)
{
    const int x = e->point.x;
    const int y = e->point.y;
    if (e->type == TOUCH_EVENT_MOVE) {
        if (lg.press == LG_PRESS_PLOT) {
            const int row = lg_row_at(x, y);
            s.cursor = (row >= 0) ? row : s.cursor;
            log_viewer_invalidate();
        }
        return;
    }
    if (e->type == TOUCH_EVENT_UP) {
        /* A release the touch layer cut (TOUCH_FLAG_NO_TAP) ends the press
         * and taps nothing. */
        if (lg.press == LG_PRESS_CHIP && touch_event_is_tap_up(e)
            && chip_at(x, y) == lg.press_chip) {
            chip_tap(lg.press_chip);
        }
        lg.press = LG_PRESS_NONE;
        log_viewer_invalidate();
    }
}

bool lg_event(const touch_event_t *e)
{
    if (!lg_on()) {
        return false;
    }
    const int x = e->point.x;
    const int y = e->point.y;
    /* Each contact ends its own press: the one holding a chip or the plot
     * is the graph's, whatever another finger does on the tabs. */
    if (lg.press != LG_PRESS_NONE && e->point.id == lg.press_id
        && e->type != TOUCH_EVENT_DOWN) {
        own_press(e);
        return true;
    }
    if (lg.tabbed) {
        const bool held = lg.tabs.pressed >= 0;
        if (ui_tabs_event(&lg.tabs, e)) {
            show_picture(lg.tabs.selected == 1 ? LOG_PRESET_RUDDR_STEPS
                                               : LOG_PRESET_RUDDR_SEARCH);
            log_viewer_invalidate();
            return true;
        }
        if (held || lg.tabs.pressed >= 0) {
            log_viewer_invalidate();
            return true;
        }
    }
    if (e->type != TOUCH_EVENT_DOWN) {
        return false;
    }
    if (lg.press != LG_PRESS_NONE) {
        /* A second finger: the graph keeps to the first.  On a time file
         * the plot is the plain plot's, which takes the finger. */
        return (!lg.time && gfx_rect_contains(PV_PANEL, x, y))
               || chip_at(x, y) >= 0;
    }
    const int k = chip_at(x, y);
    if (k >= 0) {
        lg.press = LG_PRESS_CHIP;
        lg.press_chip = k;
    } else if (!lg.time && gfx_rect_contains(PV_PANEL, x, y)) {
        lg.press = LG_PRESS_PLOT;
        const int row = lg_row_at(x, y);
        s.cursor = (row >= 0) ? row : s.cursor;
    } else {
        return false;
    }
    lg.press_id = e->point.id;
    log_viewer_invalidate();
    return true;
}

/**
 * @brief The kind of file the preset names, in words.
 * @return the words.
 */
static const char *kind_text(void)
{
    switch (s.preset) {
    case LOG_PRESET_LIMIT:  return TR(LOG_KIND_LIMIT);
    case LOG_PRESET_ATEST:  return TR(LOG_KIND_ATEST);
    case LOG_PRESET_SERVO:  return TR(LOG_KIND_SERVO);
    case LOG_PRESET_MOTOR:  return TR(LOG_KIND_MOTOR);
    case LOG_PRESET_SUPPL:  return TR(LOG_KIND_SUPPL);
    default:                return TR(LOG_KIND_RUDDR);
    }
}

bool lg_summary(char *out, size_t n)
{
    if (!lg_on()) {
        return false;
    }
    if (lg.time && lg.has_time) {
        /* The duration and the rate as the plain plot's, then the kind. */
        snprintf(out, n, TR(LOG_GRAPH_SUMMARY_TIME), s.name, s.data.count,
                 s.data.duration_s, s.data.rate_hz, kind_text());
    } else {
        snprintf(out, n, TR(LOG_GRAPH_SUMMARY), s.name, s.data.count,
                 kind_text());
    }
    return true;
}

bool lg_cursor_line(char *out, size_t n)
{
    if (!lg_on() || lg.time) {
        /* A time file's line is the plain plot's: its time and row. */
        return false;
    }
    const int ci = (s.cursor >= 0 && s.cursor < s.data.count) ? s.cursor : 0;
    char val[16];
    char at[16 + LOG_UNIT_MAX];
    ui_fmt(val, sizeof(val), s.data.value[0][ci], 0);
    snprintf(at, sizeof(at), "%s%s", val, s.data.field[0].unit);
    snprintf(out, n, TR(LOG_AT_X), s.data.field[0].name, at, ci + 1,
             s.data.count);
    return true;
}

/* --- the public view, for the tests and the renderer -------------------- */

bool log_graph_shown(void)
{
    return lg_on() && s.view == VIEW_PLOT;
}

const log_markset_t *log_graph_marks(void)
{
    return lg_on() ? &lg.mk : NULL;
}

int log_graph_focus(void)
{
    return lg_on() ? lg.focus : -1;
}

bool log_graph_hidden(int k)
{
    return lg_on() && k >= 0 && k < LOG_MAX_SERIES && lg.hidden[k];
}

int log_graph_x_px(float x)
{
    return lg_x_px(x);
}

bool log_graph_pressed(void)
{
    return lg.press != LG_PRESS_NONE;
}

int log_graph_row_px(int i)
{
    if (lg_time_on()) {
        const int c = lg_time_col(i);
        return (c >= 0) ? PV_X + c : -1;
    }
    return (lg_on() && i >= 0 && i < lg.mk.rows) ? lg.px[i] : -1;
}

bool log_graph_time(void)
{
    return lg_time_on();
}

bool log_graph_gap_before(int i)
{
    return lg_time_on() && lg_time_gap_before(i);
}

bool log_graph_gap_col(int x)
{
    const int c = x - PV_X;
    return lg_time_on() && c >= 0 && c < PV_W
           && (lg.col[c] & LG_COL_GAP) != 0u;
}

int log_graph_strip_y(int k)
{
    return (lg_time_on() && k >= 0 && k < lg.mk.n_strips)
               ? PV_Y + lg.plot_h + k * LG_STRIP_ROW
                     + (LG_STRIP_ROW - LG_STRIP_H) / 2
               : -1;
}

unsigned log_graph_strip_at(int k, int x)
{
    const int c = x - PV_X;
    return (lg_time_on() && k >= 0 && k < lg.mk.n_strips && c >= 0
            && c < PV_W) ? lg.strip_col[k][c] : LOG_STRIP_NONE;
}

bool log_graph_cursor_line(char *out, size_t n)
{
    return out != NULL && n > 0u && lg_cursor_line(out, n);
}
