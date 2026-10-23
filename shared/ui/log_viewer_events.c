/**
 * @file log_viewer_events.c
 * @brief The log viewer's touch events: taps, drags and the scroll bar on the
 *        browse view, the DELETE question, the import view's columns and
 *        overrides, and the plot's cursor, pinch zoom and pan.
 *        log_viewer_screen.c holds the state, the card's listing and paging
 *        and the screen's interface (log_viewer_screen.h); log_viewer_int.h is
 *        what the screen's four files share.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "log_viewer_screen.h"
#include "settings.h"
#include "ui_screen.h"
#include "ui_tabs.h"
#include "ui_text.h"

#include "log_viewer_int.h"

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

/* A press that moves further than this, in x or in y, is not a tap; on a
 * list, vertical travel past it is a scroll. */
#define DRAG_SLOP 8

/*
 * A button or a row is a tap: the press arms it and the release activates
 * it.  One contact holds the press from its DOWN to its UP; another
 * presses nothing and ends nothing.
 */
static bool tap_begin(const touch_event_t *e)
{
    /* Another contact's DOWN is refused.  A DOWN with the id of the press
     * held says that contact has gone and its release with it, and is
     * taken. */
    if (s.tap_have && e->point.id != s.tap_id) {
        return false;
    }
    s.tap_have = true;
    s.tap_moved = false;
    s.tap_id = e->point.id;
    s.tap_x0 = e->point.x;
    s.tap_y0 = e->point.y;
    return true;
}

static bool tap_mine(const touch_event_t *e)
{
    return s.tap_have && e->point.id == s.tap_id;
}

/* Whether @p e is further than the slop from the press, in x or in y. */
static bool tap_far(const touch_event_t *e)
{
    int dx = e->point.x - s.tap_x0, dy = e->point.y - s.tap_y0;
    if (dx < 0) { dx = -dx; }
    if (dy < 0) { dy = -dy; }
    return dx > DRAG_SLOP || dy > DRAG_SLOP;
}

/*
 * Whether the release @p e is a tap on @p r: inside it, the contact never
 * further than the slop from its press, and the release made by the finger,
 * not for it (TOUCH_FLAG_NO_TAP).
 */
static bool tap_on(const touch_event_t *e, gfx_rect_t r)
{
    return touch_event_is_tap_up(e) && !s.tap_moved && !tap_far(e)
           && gfx_rect_contains(r, e->point.x, e->point.y);
}

/* Row @p r of the rows showing in a list: under its 30 px heading. */
static gfx_rect_t list_row_rect(gfx_rect_t list, int row_h, int r)
{
    return gfx_rect_make(list.x, list.y + 30 + r * row_h, list.w, row_h);
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

static void ask_delete(void)
{
    if (!lv_can_delete()) {
        return;
    }
    snprintf(s.doomed, sizeof(s.doomed), "%s", s.files[s.sel].name);
    s.doomed_size = s.files[s.sel].size;
    s.q_down = false;
    s.message[0] = '\0';
}

/**
 * @brief Delete the file the DELETE question named, and read the page
 *        again.
 *
 * What the import, the plot and the text held is dropped when it was read
 * from that file.  A refused delete leaves the list as it was and says so
 * in the footer.
 */
static void delete_doomed(void)
{
    char name[LOG_VIEWER_NAME_MAX];
    snprintf(name, sizeof(name), "%s", s.doomed);
    s.doomed[0] = '\0';

    lv_point(s.tab);
    if (s.io.remove == NULL || !s.io.remove(name, s.io.ctx)) {
        /* The list stays as it was, selection included: the file is still
         * there as far as anybody knows, and RESCAN says otherwise if not. */
        lv_set_message(TR(LOG_CANNOT_DELETE), name);
        return;
    }
    /* What the import, plot and text views hold was read from that
     * file. */
    if (strcmp(s.name, name) == 0 && s.open_tab == s.tab) {
        lv_drop_data();
        log_text_free(&s.text);
        s.have_analysis = false;
        s.name[0] = '\0';
    }
    /* Re-read rather than drop the row here: the card says what is on it.
     * The page stays, or is the last one when the file was the last page's
     * only entry.  The selection goes with the file: the re-read's own
     * choice, the newest run, is the top row and can be scrolled out of
     * sight, so nothing is selected and a second DELETE needs a second
     * choice.  The scroll stays near the gap. */
    const int scroll = s.scroll;
    lv_rescan();
    s.sel = -1;
    s.scroll = scroll;
    clamp_scroll(&s.scroll, s.n_files, BR_ROWS);
    lv_set_message(TR(LOG_DELETED), name);
    s.message_ok = true;
}

static void question_event(const touch_event_t *e)
{
    const gfx_rect_t btns[2] = { DQ_CANCEL, DQ_DELETE };

    switch (e->type) {
    case TOUCH_EVENT_DOWN:
        /* The first contact on a button owns the press; any other finger
         * that lands while it is down is not part of it. */
        if (!s.q_down) {
            s.press_btn = hit_button(btns, 2, e->point.x, e->point.y);
            if (s.press_btn >= 0) {
                s.q_down = true;
                s.q_id = e->point.id;
                s.tap_have = false;
                (void)tap_begin(e);
            }
            log_viewer_invalidate();
        }
        break;

    case TOUCH_EVENT_MOVE:
        break;

    case TOUCH_EVENT_UP:
        if (!s.q_down || e->point.id != s.q_id) {
            break;
        }
        /*
         * Pressed and released on the same button by the same contact, as
         * a tap: a finger that slides off DELETE has changed its mind, and
         * this is the one control on the screen that cannot be taken back.
         */
        s.q_down = false;
        s.tap_have = false;
        if (s.press_btn >= 0 && tap_on(e, btns[s.press_btn])) {
            if (s.press_btn == 0) {
                s.doomed[0] = '\0';
            } else if (s.press_btn == 1) {
                delete_doomed();
            }
        }
        s.press_btn = -1;
        log_viewer_invalidate();
        break;
    }
}

static void browse_event(const touch_event_t *e)
{
    if (s.doomed[0] != '\0') {
        question_event(e);
        return;
    }

    /* A tab tapped lists its folder from the first page. */
    if (ui_tabs_event(&s.tabs, e)) {
        s.tap_have  = false;
        s.pressing  = false;
        s.press_btn = -1;
        s.bar_drag  = false;
        log_viewer_set_tab(lv_tab_at[s.tabs.selected]);
        return;
    }
    if (e->type == TOUCH_EVENT_DOWN && s.tabs.pressed >= 0) {
        log_viewer_invalidate();
        return;     /* the tab's own press */
    }

    /* PREV and NEXT are buttons only while they show. */
    const gfx_rect_t btns[6] = { BR_RESCAN, BR_OPEN, BR_DELETE, BR_WIFI,
                                 BR_PREV, BR_NEXT };
    const int n_btns = lv_paged() ? 6 : 4;

    switch (e->type) {
    case TOUCH_EVENT_DOWN:
        if (!tap_begin(e)) {
            break;
        }
        s.press_btn = hit_button(btns, n_btns, e->point.x, e->point.y);
        s.press_row = -1;
        s.dragged = false;
        s.pressing = false;
        s.bar_drag = false;
        if (s.press_btn < 0 && s.listed == 1 && s.n_files > 0
            && gfx_rect_contains(BR_BAR, e->point.x, e->point.y)) {
            /*
             * The scroll bar.  A press on the thumb takes it where it is
             * held; a press on the track beside it brings the thumb's middle
             * to the finger.  Either way the finger then drags it.
             */
            int ty;
            int th;
            lv_bar_thumb(&ty, &th);
            s.bar_drag = true;
            if (e->point.y >= ty && e->point.y < ty + th) {
                s.bar_grab = e->point.y - ty;
            } else {
                s.bar_grab = th / 2;
                s.scroll = lv_bar_scroll_at(e->point.y - s.bar_grab);
            }
        } else if (s.press_btn < 0
                   && gfx_rect_contains(BR_LIST, e->point.x, e->point.y)) {
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
        if (s.bar_drag && tap_mine(e)) {
            const int scroll = lv_bar_scroll_at(e->point.y - s.bar_grab);
            if (scroll != s.scroll) {
                s.scroll = scroll;
                log_viewer_invalidate();
            }
        } else if (s.pressing && tap_mine(e)) {
            int dy = e->point.y - s.press_y0;
            if (dy > DRAG_SLOP || dy < -DRAG_SLOP) {
                s.dragged = true;
                s.scroll = s.press_scroll0 - dy / BR_ROW_H;
                clamp_scroll(&s.scroll, s.n_files, BR_ROWS);
                log_viewer_invalidate();
            }
        }
        break;

    case TOUCH_EVENT_UP: {
        if (!tap_mine(e)) {
            break;
        }
        s.tap_have = false;
        const int btn = s.press_btn;
        const bool row = s.pressing && !s.dragged && s.press_row >= 0
                         && s.press_row < s.n_files
                         && tap_on(e, list_row_rect(BR_LIST, BR_ROW_H,
                                                    s.press_row - s.scroll));
        s.pressing = false;
        s.bar_drag = false;
        s.press_btn = -1;
        /* A press that slid off its button is not a tap on it. */
        const bool on = btn >= 0 && tap_on(e, btns[btn]);
        if (on && btn == 0) {
            lv_rescan();
        } else if (on && btn == 4) {
            if (lv_can_prev()) {
                lv_read_page(LOG_PAGE_BEFORE, lv_page_edge(1));
            }
        } else if (on && btn == 5) {
            if (lv_can_next()) {
                lv_read_page(LOG_PAGE_AFTER, lv_page_edge(-1));
            }
        } else if (on && btn == 1) {
            lv_open_selected();
        } else if (on && btn == 3) {
            ui_router_goto(SCREEN_WIFI);
            return;
        } else if (on) {
            ask_delete();
        } else if (btn < 0 && row) {
            if (s.sel == s.press_row) {
                lv_open_selected(); /* second tap opens */
            } else {
                s.sel = s.press_row;
            }
        }
        log_viewer_invalidate();
        break;
    }
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
        IM_BTN(0), IM_BTN(1), IM_BTN(2), IM_BTN(3),
    };

    switch (e->type) {
    case TOUCH_EVENT_DOWN:
        if (!tap_begin(e)) {
            break;
        }
        s.press_btn = hit_button(btns, 4, e->point.x, e->point.y);
        s.press_row = -1;
        s.dragged = false;
        s.pressing = false;
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
        if (s.pressing && tap_mine(e)) {
            int dy = e->point.y - s.press_y0;
            if (dy > DRAG_SLOP || dy < -DRAG_SLOP) {
                s.dragged = true;
                s.col_scroll = s.press_scroll0 - dy / IM_ROW_H;
                clamp_scroll(&s.col_scroll, s.an.n_columns, IM_ROWS);
                log_viewer_invalidate();
            }
        }
        break;

    case TOUCH_EVENT_UP: {
        if (!tap_mine(e)) {
            break;
        }
        s.tap_have = false;
        /* A press that slid off its button or its row is not a tap on it:
         * it ends here and nothing acts. */
        int btn = s.press_btn;
        if (btn >= 0 && !tap_on(e, btns[btn])) {
            btn = 4;
        }
        const bool row = s.pressing && !s.dragged && s.press_row >= 0
                         && tap_on(e, list_row_rect(IM_RIGHT, IM_ROW_H,
                                                    s.press_row
                                                        - s.col_scroll));
        switch (btn) {
        case 0:
            /* AUTO, then each candidate in turn. */
            s.delim_index++;
            if (s.delim_index >= (int)sizeof(log_csv_delimiters)) {
                s.delim_index = -1;
            }
            lv_run_analysis();
            break;
        case 1:
            s.conv = (s.conv == LOG_CONV_AUTO)  ? LOG_CONV_DE
                     : (s.conv == LOG_CONV_DE) ? LOG_CONV_EN
                                                : LOG_CONV_AUTO;
            lv_run_analysis();
            break;
        case 2:
            s.view = VIEW_BROWSE;
            break;
        case 3:
            if (s.have_analysis && s.n_picked > 0) {
                lv_load_data();
            }
            break;
        case 4:
            break;
        default:
            if (row) {
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
    s.cursor = s.win0 + (int)(((long)rel * (s.win_n - 1)) / (PV_W - 1));
}

/* Keep the cursor's sample on screen after an arrow press. */
static void follow_cursor(void)
{
    if (s.cursor < s.win0) {
        s.win0 = s.cursor;
        lv_compute_spans();
    } else if (s.cursor >= s.win0 + s.win_n) {
        s.win0 = s.cursor - s.win_n + 1;
        lv_compute_spans();
    }
}

static float pinch_dist(void)
{
    float d = (float)(s.pinch_x[1] - s.pinch_x[0]);
    return d < 0.0f ? -d : d;
}

static float pinch_mid(void)
{
    return 0.5f * (float)(s.pinch_x[0] + s.pinch_x[1]);
}

/* A second finger lands on the plot: the gesture starts from here. */
static void pinch_start(uint8_t id0, int16_t x0, uint8_t id1, int16_t x1)
{
    s.pinch = true;
    s.panning = false;
    s.pinch_id[0] = id0;
    s.pinch_id[1] = id1;
    s.pinch_x[0] = x0;
    s.pinch_x[1] = x1;
    s.pinch_d0 = pinch_dist();
    if (s.pinch_d0 < 20.0f) {
        s.pinch_d0 = 20.0f;
    }
    s.pinch_n0 = (float)s.win_n;
    s.pinch_s0 = (float)s.win0 +
                 (pinch_mid() - (float)PV_X) * s.pinch_n0 / (float)PV_W;
}

/* Both fingers' positions changed: set the window from them. */
static void pinch_apply(void)
{
    const int count = s.data.count;
    float d = pinch_dist();
    if (d < 20.0f) {
        d = 20.0f;
    }
    float n = s.pinch_n0 * s.pinch_d0 / d;
    const float min_n = (float)((count < PV_MIN_WIN) ? count : PV_MIN_WIN);
    if (n < min_n) {
        n = min_n;
    }
    if (n > (float)count) {
        n = (float)count;
    }
    float w0 = s.pinch_s0 - (pinch_mid() - (float)PV_X) * n / (float)PV_W;
    if (w0 < 0.0f) {
        w0 = 0.0f;
    }
    if (w0 + n > (float)count) {
        w0 = (float)count - n;
    }
    const int nw = (int)(n + 0.5f);
    int nw0 = (int)(w0 + 0.5f);
    if (nw0 + nw > count) {
        nw0 = count - nw;
    }
    if (nw != s.win_n || nw0 != s.win0) {
        s.win_n = nw;
        s.win0 = nw0;
        lv_compute_spans();
    }
}

/* A drag pans only a view that is zoomed in, and only when the setting asks. */
static bool pan_enabled(void)
{
    return settings_get_bool(SET_PLOT_PAN) && s.have_data &&
           s.win_n < s.data.count;
}

/* The finger is at @p x: keep the sample that was under it where it began. */
static void pan_apply(int x)
{
    const int max0 = s.data.count - s.win_n;
    float w0 = (float)s.pan_win0 +
               (float)(s.pan_x0 - x) * (float)s.win_n / (float)PV_W;
    if (w0 < 0.0f) {
        w0 = 0.0f;
    }
    if (w0 > (float)max0) {
        w0 = (float)max0;
    }
    const int nw0 = (int)(w0 + 0.5f);
    if (nw0 != s.win0) {
        s.win0 = nw0;
        lv_compute_spans();
    }
}

static void plot_event(const touch_event_t *e)
{
    const gfx_rect_t btns[3] = {
        PV_BACK, PV_PREV, PV_NEXT,
    };

    /* Two fingers on the plot own it until one lifts; a third is ignored. */
    if (s.pinch) {
        int f = (e->point.id == s.pinch_id[0]) ? 0
              : (e->point.id == s.pinch_id[1]) ? 1 : -1;
        if (f < 0) {
            return;
        }
        if (e->type == TOUCH_EVENT_MOVE) {
            s.pinch_x[f] = e->point.x;
            pinch_apply();
            log_viewer_invalidate();
        } else if (e->type == TOUCH_EVENT_UP) {
            /* The view stays where the fingers left it. */
            s.pinch = false;
            s.pressing = false;
            log_viewer_invalidate();
        }
        return;
    }

    switch (e->type) {
    case TOUCH_EVENT_DOWN:
        if (s.pressing && e->point.id != s.press_id && s.have_data &&
            s.data.count > 1 &&
            gfx_rect_contains(PV_PANEL, e->point.x, e->point.y)) {
            pinch_start(s.press_id, s.press_x, e->point.id, e->point.x);
            log_viewer_invalidate();
            break;
        }
        if (s.pressing || s.press_btn >= 0) {
            break;   /* a second finger elsewhere presses nothing */
        }
        s.press_btn = hit_button(btns, 3, e->point.x, e->point.y);
        if (s.press_btn >= 0) {
            s.tap_have = false;
            (void)tap_begin(e);
        }
        if (s.press_btn < 0 && gfx_rect_contains(PV_PANEL, e->point.x, e->point.y)) {
            s.pressing = true;
            s.press_id = e->point.id;
            s.press_x = e->point.x;
            s.panning = false;
            s.pan_x0 = e->point.x;
            s.pan_cursor0 = s.cursor;
            set_cursor_from_x(e->point.x);
        }
        log_viewer_invalidate();
        break;

    case TOUCH_EVENT_MOVE:
        if (s.pressing && e->point.id == s.press_id) {
            s.press_x = e->point.x;
            if (!s.panning && pan_enabled()) {
                /* Judge the drag from where the finger touched down. */
                if (e->point.x - s.pan_x0 > PV_PAN_SLOP ||
                    s.pan_x0 - e->point.x > PV_PAN_SLOP) {
                    /* Anchored where the finger crossed the threshold, so
                     * the movement past it pans, also in one event. */
                    s.panning = true;
                    s.cursor = s.pan_cursor0;
                    s.pan_x0 = (int16_t)((e->point.x > s.pan_x0)
                                             ? s.pan_x0 + PV_PAN_SLOP
                                             : s.pan_x0 - PV_PAN_SLOP);
                    s.pan_win0 = s.win0;
                }
            }
            if (s.panning) {
                pan_apply(e->point.x);
            } else {
                set_cursor_from_x(e->point.x);
            }
            log_viewer_invalidate();
        }
        break;

    case TOUCH_EVENT_UP: {
        if (s.pressing && e->point.id != s.press_id) {
            break;
        }
        if (s.press_btn >= 0 && !tap_mine(e)) {
            break;   /* another finger's release ends no press */
        }
        /* A press that slid off its button is not a tap on it. */
        const int btn = (s.press_btn >= 0 && tap_on(e, btns[s.press_btn]))
                            ? s.press_btn : -1;
        if (btn == 0) {
            s.view = VIEW_IMPORT;
        } else if (btn == 1 && s.cursor > 0) {
            s.cursor--;
            follow_cursor();
        } else if (btn == 2 && s.have_data &&
                   s.cursor < s.data.count - 1) {
            s.cursor++;
            follow_cursor();
        }
        s.tap_have = false;
        s.pressing = false;
        s.press_btn = -1;
        log_viewer_invalidate();
        break;
    }
    }
}

/**
 * @brief A touch on the TEXT view: a drag on the rows or on the scroll bar
 *        scrolls them, a tap on BACK returns to the list.
 *
 * @param e  The event.
 */
static void text_event(const touch_event_t *e)
{
    const int total = lv_text_total();
    switch (e->type) {
    case TOUCH_EVENT_DOWN:
        if (!tap_begin(e)) {
            break;
        }
        s.press_btn = gfx_rect_contains(TX_BACK, e->point.x, e->point.y)
                          ? 0 : -1;
        s.pressing = false;
        s.bar_drag = false;
        if (s.press_btn < 0 && total > 0
            && gfx_rect_contains(BR_BAR, e->point.x, e->point.y)) {
            /* As the browse list's bar: the thumb where it is held, or its
             * middle to the finger. */
            int ty;
            int th;
            lv_bar_thumb_of(total, TX_ROWS, s.text_scroll, &ty, &th);
            s.bar_drag = true;
            if (e->point.y >= ty && e->point.y < ty + th) {
                s.bar_grab = e->point.y - ty;
            } else {
                s.bar_grab = th / 2;
                s.text_scroll = lv_bar_scroll_of(total, TX_ROWS,
                                                 e->point.y - s.bar_grab);
            }
        } else if (s.press_btn < 0
                   && gfx_rect_contains(BR_LIST, e->point.x, e->point.y)) {
            s.pressing = true;
            s.press_y0 = e->point.y;
            s.press_scroll0 = s.text_scroll;
        }
        log_viewer_invalidate();
        break;

    case TOUCH_EVENT_MOVE:
        if (s.bar_drag && tap_mine(e)) {
            const int scroll = lv_bar_scroll_of(total, TX_ROWS,
                                                e->point.y - s.bar_grab);
            if (scroll != s.text_scroll) {
                s.text_scroll = scroll;
                log_viewer_invalidate();
            }
        } else if (s.pressing && tap_mine(e)) {
            const int dy = e->point.y - s.press_y0;
            if (dy > DRAG_SLOP || dy < -DRAG_SLOP) {
                s.text_scroll = s.press_scroll0 - dy / TX_ROW_H;
                clamp_scroll(&s.text_scroll, total, TX_ROWS);
                log_viewer_invalidate();
            }
        }
        break;

    case TOUCH_EVENT_UP:
        if (!tap_mine(e)) {
            break;
        }
        s.tap_have = false;
        /* A press that slid off BACK is not a tap on it. */
        if (s.press_btn == 0 && tap_on(e, TX_BACK)) {
            lv_close_text();
        }
        s.pressing = false;
        s.bar_drag = false;
        s.press_btn = -1;
        log_viewer_invalidate();
        break;
    }
}

void lv_event(const touch_event_t *e)
{
    if (e == NULL) {
        return;
    }
    /* A press that goes further than the slop is no tap, wherever it is
     * when it lifts. */
    if (e->type == TOUCH_EVENT_MOVE && tap_mine(e) && tap_far(e)) {
        s.tap_moved = true;
    }
    switch (s.view) {
    case VIEW_IMPORT:
        import_event(e);
        break;
    case VIEW_PLOT:
        plot_event(e);
        break;
    case VIEW_TEXT:
        text_event(e);
        break;
    case VIEW_BROWSE:
    default:
        browse_event(e);
        break;
    }
}
