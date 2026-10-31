/**
 * @file files_events.c
 * @brief FILES' touch events: the tile's strip, the rows, the scroll bars,
 *        the headers and the footer of the panes, NEW's choice, the DELETE question, the
 *        INFO box, the progress box and the keyboard.  files_screen.c holds
 *        the state; files_int.h is what the screen's three files share.
 *
 * A button or a row is a tap: the press arms it and the release acts, when
 * the same contact lifts inside it no further than DRAG_SLOP from where it
 * came down.  One contact holds the screen from its DOWN to its UP; another
 * presses nothing and ends nothing.  A list scrolls by a drag that starts
 * on it, and by its scroll bar.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "files_screen.h"
#include "text_view.h"
#include "ui_textkey.h"

#include "files_int.h"

/**
 * @brief A DOWN takes the press, unless another contact holds it.
 * @param e  the DOWN
 * @return true when this contact holds the press now
 */
static bool tap_begin(const touch_event_t *e)
{
    /* A DOWN with the id of the press held says that contact has gone and
     * its release with it, and is taken. */
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

/**
 * @brief Whether @p e is the held contact's.
 * @param e  the event
 * @return true for the contact that holds the press
 */
static bool tap_mine(const touch_event_t *e)
{
    return s.tap_have && e->point.id == s.tap_id;
}

/**
 * @brief Whether @p e is further than the slop from the press, in x or y.
 * @param e  the event
 * @return true past DRAG_SLOP
 */
static bool tap_far(const touch_event_t *e)
{
    int dx = e->point.x - s.tap_x0;
    int dy = e->point.y - s.tap_y0;
    dx = (dx < 0) ? -dx : dx;
    dy = (dy < 0) ? -dy : dy;
    return dx > DRAG_SLOP || dy > DRAG_SLOP;
}

/**
 * @brief Whether the release @p e is a tap on @p r: inside it, never
 *        further than the slop from its press, made by the finger
 *        (not TOUCH_FLAG_NO_TAP).
 * @param e  the release
 * @param r  the control
 * @return true for a tap on it
 */
static bool tap_on(const touch_event_t *e, gfx_rect_t r)
{
    return touch_event_is_tap_up(e) && !s.tap_moved && !tap_far(e)
           && gfx_rect_contains(r, e->point.x, e->point.y);
}

/**
 * @brief Row @p r on screen of pane @p i, the whole pitch a finger hits.
 * @param i  the pane
 * @param r  the row on screen, 0 to FS_ROWS - 1
 * @return its rectangle
 */
static gfx_rect_t row_rect(int i, int r)
{
    return gfx_rect_make(FS_PANE_X(i) + FS_ROW_IN,
                         FS_LIST_Y + FS_HEAD_H + r * FS_ROW_H, FS_ROW_W,
                         FS_ROW_H);
}

void fs_cancel_press(void)
{
    s.tap_have = false;
    s.tap_moved = false;
    s.press = FS_HIT_NONE;
    s.dragged = false;
    ui_textkey_cancel_press(&s.tk);
    text_view_cancel_press();
    logs_strip_cancel();
    files_screen_invalidate();
}

/**
 * @brief What of the panes and the footer is at (@p x, @p y).
 * @param x  the point
 * @param y  the point
 * @return an fs_hit_t; s.press_pane and s.press_row for a row or a bar
 */
static int hit_panes(int x, int y)
{
    for (int b = 0; b < FOOT_N; ++b) {
        if (gfx_rect_contains(FS_FOOT(b), x, y)) {
            return b;
        }
    }
    for (int i = 0; i < 2; ++i) {
        if (gfx_rect_contains(FS_UP(i), x, y)) {
            return FS_HIT_UP + i;
        }
        if (fs_paged(i) && gfx_rect_contains(FS_PREV(i), x, y)) {
            return FS_HIT_PREV + i;
        }
        if (fs_paged(i) && gfx_rect_contains(FS_NEXT(i), x, y)) {
            return FS_HIT_NEXT + i;
        }
        const int held = s.read[i] ? s.pane[i].held : 0;
        if (held > 0 && gfx_rect_contains(FS_BAR(i), x, y)) {
            s.press_pane = i;
            return FS_HIT_BAR;
        }
        for (int r = 0; r < FS_ROWS; ++r) {
            if (gfx_rect_contains(row_rect(i, r), x, y)) {
                s.press_pane = i;
                s.press_row = s.scroll[i] + r;
                return FS_HIT_ROW;
            }
        }
    }
    return FS_HIT_NONE;
}

/**
 * @brief A DOWN on the panes: the control under it pressed, a bar's thumb
 *        taken.
 * @param e  the DOWN
 */
static void panes_down(const touch_event_t *e)
{
    s.press = hit_panes(e->point.x, e->point.y);
    s.dragged = false;
    if (s.press == FS_HIT_ROW) {
        s.press_scroll0 = s.scroll[s.press_pane];
    } else if (s.press == FS_HIT_BAR) {
        /* A press on the thumb takes it where it is held; on the track
         * beside it, the thumb's middle comes to the finger. */
        int ty;
        int th;
        fs_bar_thumb(s.press_pane, &ty, &th);
        if (e->point.y >= ty && e->point.y < ty + th) {
            s.bar_grab = e->point.y - ty;
        } else {
            s.bar_grab = th / 2;
            s.scroll[s.press_pane] = fs_bar_scroll_at(s.press_pane,
                                                      e->point.y - s.bar_grab);
        }
    }
}

/**
 * @brief A MOVE of the held contact: a bar dragged, a list scrolled.
 * @param e  the MOVE
 */
static void panes_move(const touch_event_t *e)
{
    const int i = s.press_pane;
    if (s.press == FS_HIT_BAR) {
        const int scroll = fs_bar_scroll_at(i, e->point.y - s.bar_grab);
        if (scroll != s.scroll[i]) {
            s.scroll[i] = scroll;
            files_screen_invalidate();
        }
    } else if (s.press == FS_HIT_ROW) {
        const int dy = e->point.y - s.tap_y0;
        if (dy > DRAG_SLOP || dy < -DRAG_SLOP) {
            s.dragged = true;
            s.scroll[i] = s.press_scroll0 - dy / FS_ROW_H;
            fs_clamp_scroll(i);
            files_screen_invalidate();
        }
    }
}

/**
 * @brief The release of the held contact on the panes: the control it
 *        pressed acts when it is a tap on it.
 * @param e  the UP
 */
static void panes_up(const touch_event_t *e)
{
    const int hit = s.press;
    s.press = FS_HIT_NONE;
    if (hit >= 0 && hit < FOOT_N) {
        /* A button's number is its place in the footer. */
        for (fs_foot_t b = FS_RESCAN; b <= FS_OPEN; ++b) {
            if (hit == (int)b && tap_on(e, FS_FOOT(hit))) {
                fs_foot(b);
            }
        }
    } else if (hit == FS_HIT_UP || hit == FS_HIT_UP + 1) {
        const int i = hit - FS_HIT_UP;
        if (tap_on(e, FS_UP(i))) {
            fs_turn(i, FM_GO_FIRST, true);
        }
    } else if (hit == FS_HIT_PREV || hit == FS_HIT_PREV + 1) {
        const int i = hit - FS_HIT_PREV;
        if (tap_on(e, FS_PREV(i)) && fm_tree_has_prev(&s.pane[i])) {
            fs_turn(i, FM_GO_PREV, false);
        }
    } else if (hit == FS_HIT_NEXT || hit == FS_HIT_NEXT + 1) {
        const int i = hit - FS_HIT_NEXT;
        if (tap_on(e, FS_NEXT(i)) && fm_tree_has_next(&s.pane[i])) {
            fs_turn(i, FM_GO_NEXT, false);
        }
    } else if (hit == FS_HIT_ROW && !s.dragged) {
        const int i = s.press_pane;
        const int r = s.press_row - s.scroll[i];
        if (r >= 0 && r < FS_ROWS && tap_on(e, row_rect(i, r))) {
            fs_row_tap(i, s.press_row);
        }
    }
}

/**
 * @brief Pane and footer events.
 * @param e  the event
 */
static void panes_event(const touch_event_t *e)
{
    switch (e->type) {
    case TOUCH_EVENT_DOWN:
        if (tap_begin(e)) {
            panes_down(e);
            files_screen_invalidate();
        }
        break;
    case TOUCH_EVENT_MOVE:
        if (tap_mine(e)) {
            if (tap_far(e)) {
                s.tap_moved = true;
            }
            panes_move(e);
        }
        break;
    case TOUCH_EVENT_UP:
        if (tap_mine(e)) {
            s.tap_have = false;
            panes_up(e);
            files_screen_invalidate();
        }
        break;
    }
}

/**
 * @brief The events of a box with buttons: the DELETE question, INFO, the
 *        progress box and NEW's choice.  A press arms a button; the release
 *        of the same contact on it acts.
 * @param e     the event
 * @param btns  the buttons
 * @param hits  each one's fs_hit_t
 * @param n     how many
 * @return the fs_hit_t of a button tapped, FS_HIT_NONE otherwise
 */
static int box_event(const touch_event_t *e, const gfx_rect_t *btns,
                     const int *hits, int n)
{
    switch (e->type) {
    case TOUCH_EVENT_DOWN:
        if (!tap_begin(e)) {
            return FS_HIT_NONE;
        }
        s.press = FS_HIT_NONE;
        for (int k = 0; k < n; ++k) {
            if (gfx_rect_contains(btns[k], e->point.x, e->point.y)) {
                s.press = hits[k];
            }
        }
        files_screen_invalidate();
        return FS_HIT_NONE;
    case TOUCH_EVENT_MOVE:
        if (tap_mine(e) && tap_far(e)) {
            s.tap_moved = true;
        }
        return FS_HIT_NONE;
    case TOUCH_EVENT_UP:
    default: {
        if (!tap_mine(e)) {
            return FS_HIT_NONE;
        }
        s.tap_have = false;
        const int hit = s.press;
        s.press = FS_HIT_NONE;
        files_screen_invalidate();
        for (int k = 0; k < n; ++k) {
            if (hits[k] == hit && tap_on(e, btns[k])) {
                return hit;
            }
        }
        return FS_HIT_NONE;
    }
    }
}

/**
 * @brief NEW's choice: a row tapped opens the keyboard; a press anywhere
 *        else above the alert band closes the choice and acts on nothing.
 *        A press on the band does nothing, as it does with an alert
 *        showing there.
 * @param e  the event
 */
static void new_event(const touch_event_t *e)
{
    if (e->type == TOUCH_EVENT_DOWN && !s.tap_have
        && e->point.y < SCREEN_H - UI_ALERT_H
        && !gfx_rect_contains(FS_NEW_LIST, e->point.x, e->point.y)) {
        s.view = FILES_VIEW_PANES;
        /* The contact is held so its release presses nothing beneath. */
        (void)tap_begin(e);
        s.press = FS_HIT_NONE;
        files_screen_invalidate();
        return;
    }
    const gfx_rect_t btns[2] = { FS_NEW_ROW(0), FS_NEW_ROW(1) };
    const int hits[2] = { FS_HIT_NEW_ROW, FS_HIT_NEW_ROW + 1 };
    const int hit = box_event(e, btns, hits, 2);
    if (hit == FS_HIT_NEW_ROW || hit == FS_HIT_NEW_ROW + 1) {
        fs_new_name(hit == FS_HIT_NEW_ROW);
    }
}

void fs_event(const touch_event_t *e)
{
    if (e == NULL) {
        return;
    }
    /* The tile's strip takes a press on the panes alone, while no other
     * contact holds one here. */
    if (s.view != FILES_VIEW_PANES) {
        logs_strip_cancel();
    } else if ((!s.tap_have || e->point.id == s.tap_id)
               && logs_strip_event(e, SCREEN_FILES)) {
        s.tap_have = false;
        s.press = FS_HIT_NONE;
        files_screen_invalidate();
        return;
    }
    switch (s.view) {
    case FILES_VIEW_NAME: {
        const uint32_t rev = s.tk.revision;
        char name[UI_TEXTKEY_PASS_MAX + 1];
        const ui_textkey_result_t r = ui_textkey_event(&s.tk, e, name,
                                                       sizeof(name));
        if (r == UI_TEXTKEY_OK) {
            fs_name_typed(name);
            files_screen_invalidate();
        } else if (r == UI_TEXTKEY_CANCELLED) {
            s.view = FILES_VIEW_PANES;
            files_screen_invalidate();
        } else if (s.tk.revision != rev) {
            files_screen_invalidate();
        }
        break;
    }
    case FILES_VIEW_DELETE: {
        const gfx_rect_t btns[2] = { FS_Q_CANCEL, FS_Q_DELETE };
        const int hits[2] = { FS_HIT_Q_CANCEL, FS_HIT_Q_DELETE };
        const int hit = box_event(e, btns, hits, 2);
        if (hit == FS_HIT_Q_CANCEL) {
            s.view = FILES_VIEW_PANES;
        } else if (hit == FS_HIT_Q_DELETE) {
            fs_delete_doomed();
        }
        break;
    }
    case FILES_VIEW_INFO: {
        const gfx_rect_t btns[1] = { FS_INFO_CLOSE };
        const int hits[1] = { FS_HIT_INFO_CLOSE };
        if (box_event(e, btns, hits, 1) == FS_HIT_INFO_CLOSE) {
            s.view = FILES_VIEW_PANES;
        }
        break;
    }
    case FILES_VIEW_JOB: {
        const gfx_rect_t btns[1] = { FS_JOB_CANCEL };
        const int hits[1] = { FS_HIT_JOB_CANCEL };
        const bool copy = s.req.job.kind == FM_JOB_COPY;
        if (box_event(e, btns, hits, copy ? 1 : 0) == FS_HIT_JOB_CANCEL) {
            fs_job_cancel();
        }
        break;
    }
    case FILES_VIEW_NEW:
        new_event(e);
        break;
    case FILES_VIEW_TEXT:
        fs_text_result(text_view_event(e));
        break;
    case FILES_VIEW_PANES:
    default:
        panes_event(e);
        break;
    }
}
