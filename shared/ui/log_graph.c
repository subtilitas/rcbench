/**
 * @file log_graph.c
 * @brief LOGS' graph of an XY file: the load beside the plot's, the cursor,
 *        the chips' focus and the touches.  See log_graph_int.h.
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

bool lg_read(log_source_t *src)
{
    lg.on = false;
    const log_preset_t *p = log_preset_get(s.preset);
    if (p == NULL || p->plot != LOG_PLOT_XY || s.data.n_fields < 2
        || s.data.count > LOG_MARKS_ROWS
        || s.data.field[0].column != log_fields_find(&s.an, p->x, true)) {
        return false;
    }
    if (log_marks_read(src, &s.an, s.preset, &lg.mk) != LOG_OK
        || lg.mk.rows != s.data.count) {
        return false;
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
    if (!lg_on()) {
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
 * @param k  The series, 1 to n_fields - 1.
 */
static void chip_tap(int k)
{
    if (k < 1 || k >= LOG_MAX_SERIES) {
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
 * @return the series, 1 to n_fields - 1; -1 for none.
 */
static int chip_at(int x, int y)
{
    for (int k = 1; k < s.data.n_fields; ++k) {
        if (gfx_rect_contains(LG_SLOT(k - 1), x, y)) {
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
        /* A second finger: the graph keeps to the first. */
        return gfx_rect_contains(PV_PANEL, x, y) || chip_at(x, y) > 0;
    }
    const int k = chip_at(x, y);
    if (k > 0) {
        lg.press = LG_PRESS_CHIP;
        lg.press_chip = k;
    } else if (gfx_rect_contains(PV_PANEL, x, y)) {
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
    return (s.preset == LOG_PRESET_LIMIT) ? TR(LOG_KIND_LIMIT)
                                          : TR(LOG_KIND_RUDDR);
}

bool lg_summary(char *out, size_t n)
{
    if (!lg_on()) {
        return false;
    }
    snprintf(out, n, TR(LOG_GRAPH_SUMMARY), s.name, s.data.count,
             kind_text());
    return true;
}

bool lg_cursor_line(char *out, size_t n)
{
    if (!lg_on()) {
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
    return (lg_on() && i >= 0 && i < lg.mk.rows) ? lg.px[i] : -1;
}

bool log_graph_cursor_line(char *out, size_t n)
{
    return out != NULL && n > 0u && lg_cursor_line(out, n);
}
