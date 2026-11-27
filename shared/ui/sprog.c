/**
 * @file sprog.c
 * @brief The state of the servo programmers' frame: the tabs, the view's
 *        cells, the presses, the row holds and their due, the step keys'
 *        repeat, the keypad, the keyboard and the list.  See sprog.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "sprog.h"

#include <stdio.h>
#include <string.h>

#include "ui_layout.h"

/** @brief The tabs' top, px under the area's top. */
#define TAB_Y 8

/* --------------------------------------------------------------- helpers */

/**
 * @brief A result.
 * @param kind  the sprog_result_kind_t
 * @param row   the row, or -1
 * @param key   the key, the direction or the tab
 * @param what  the brand's code
 * @param value the value
 * @return the result
 */
static sprog_result_t result(int kind, int row, int key, int what,
                             int32_t value)
{
    sprog_result_t r;

    r.kind = (uint8_t)kind;
    r.row = (int16_t)row;
    r.key = (int16_t)key;
    r.what = (int16_t)what;
    r.value = value;
    return r;
}

/** @brief Nothing happened. */
static sprog_result_t nothing(void)
{
    return result(SPROG_R_NONE, -1, 0, 0, 0);
}

/**
 * @brief Takes the press of @p evt's contact on @p on.
 * @param f   the frame
 * @param evt the DOWN
 * @param on  the sprog_on_t
 * @param row its row or entry, or -1
 * @param key its key of a pair
 */
static void take(sprog_t *f, const touch_event_t *evt, int on, int row,
                 int key)
{
    f->have = true;
    f->id = evt->point.id;
    f->on = (uint8_t)on;
    f->row = (int16_t)row;
    f->key = (int16_t)key;
    f->held_s = 0.0f;
}

/** @brief Ends the press.
 * @param f the frame */
static void let_go(sprog_t *f)
{
    f->have = false;
    f->on = SPROG_ON_NONE;
}

/** @brief Whether the keypad, the keyboard or the list is open.
 * @param f the frame
 * @return true while one is */
static bool editor_open(const sprog_t *f)
{
    return f->kp.open || f->tk.open || f->list.open;
}

/** @brief Counts the revision when the brand's signature moved.
 * @param f the frame */
static void sig_follow(sprog_t *f)
{
    if (f->view.sig != f->sig) {
        f->sig = f->view.sig;
        ++f->rev;
    }
}

/* ------------------------------------------------------------------ tabs */

/** @brief The tab strip: the area's top line less 8px each side.
 * @param f the frame
 * @return the rectangle */
static gfx_rect_t tabs_rect(const sprog_t *f)
{
    return gfx_rect_make(f->area.x + 8, f->area.y + TAB_Y, f->area.w - 16,
                         UI_TAP_MIN);
}

/**
 * @brief Builds the tab strip again when the tabs the mask shows change:
 *        the editors close, the press and the holds end, and the tab shown
 *        stays where it still shows, else the first.
 * @param f the frame
 */
static void tabs_follow(sprog_t *f)
{
    const sprog_def_t *d = f->def;
    ui_text_id_t ids[SPROG_TABS] = {0};
    uint8_t bits = 0;
    int n = 0;
    const int was = sprog_tab(f);

    for (int t = 0; t < d->tab_count && t < SPROG_TABS; ++t) {
        const uint8_t show = d->tab_show != NULL ? d->tab_show[t] : 0u;

        if (show == 0u || (show & f->view.mask) != 0u) {
            bits = (uint8_t)(bits | (1u << t));
        }
    }
    if (bits == f->tab_bits && f->tabs.count > 0) {
        return;
    }
    if (f->tabs.count > 0) {
        sprog_close_editors(f);
        let_go(f);
        ui_hold_reset(&f->hold);
        f->hold_due = false;
    }
    f->tab_bits = bits;
    for (int t = 0; t < d->tab_count && t < SPROG_TABS; ++t) {
        if ((bits & (1u << t)) != 0u) {
            f->tab_of[n] = (uint8_t)t;
            ids[n++] = d->tabs[t];
        }
    }
    ui_tabs_init_text(&f->tabs, ids, n, tabs_rect(f));
    for (int t = 0; t < n; ++t) {
        if (f->tab_of[t] == was) {
            f->tabs.selected = t;
        }
    }
    ++f->rev;
}

int sprog_tab(const sprog_t *f)
{
    return f->tabs.selected >= 0 && f->tabs.selected < f->tabs.count
               ? f->tab_of[f->tabs.selected] : 0;
}

void sprog_tab_set(sprog_t *f, int tab)
{
    for (int t = 0; t < f->tabs.count; ++t) {
        if (f->tab_of[t] == tab) {
            f->tabs.selected = t;
            f->note_page = 0;
            ++f->rev;
        }
    }
}

/* ------------------------------------------------------------- lifecycle */

void sprog_init(sprog_t *f, const sprog_def_t *def, gfx_rect_t area)
{
    memset(f, 0, sizeof(*f));
    f->def = def;
    f->area = area;
    f->hold_row = -1;
    f->kp.pressed = -1;
    f->tk.pressed = -1;
    f->list.current = SPROG_NO_VALUE;
    tabs_follow(f);
    sprog_view_begin(f, 0u);
}

void sprog_close_editors(sprog_t *f)
{
    ui_keypad_close(&f->kp);
    ui_textkey_close(&f->tk);
    f->list.open = false;
    f->page.open = false;
    f->page.down = false;
    ui_hold_spend(&f->page.hold);     /* a flash under way runs on */
    if (f->have && f->on >= SPROG_ON_KEYPAD) {
        f->on = SPROG_ON_ABSORB;        /* its release taps nothing */
    }
    ++f->rev;
}

void sprog_cancel(sprog_t *f)
{
    ui_keypad_cancel_press(&f->kp);
    ui_textkey_cancel_press(&f->tk);
    ui_tabs_cancel(&f->tabs);
    ui_hold_reset(&f->page.hold);
    ui_hold_reset(&f->hold);
    f->hold_due = false;
    f->page.down = false;
    let_go(f);
    ++f->rev;
}

void sprog_refuse(sprog_t *f)
{
    if (f->have && f->on == SPROG_ON_HOLD) {
        ui_hold_reset(&f->hold);
    }
    let_go(f);
    ++f->rev;
}

bool sprog_due(sprog_t *f, int row)
{
    if (!f->open || f->have || editor_open(f) || f->page.open
        || row < 0 || row >= f->def->row_count) {
        return false;
    }
    f->hold_row = (int16_t)row;
    f->hold_due = true;
    ++f->rev;
    return true;
}

/* ------------------------------------------------------------------ view */

void sprog_view_begin(sprog_t *f, uint8_t mask)
{
    const int tab = sprog_tab(f);

    memset(&f->view, 0, sizeof(f->view));
    f->view.mask = mask;
    tabs_follow(f);
    f->cells = 0;
    f->lines = 0;
    memset(f->cell_row, -1, sizeof(f->cell_row));
    for (int row = 0; row < f->def->row_count; ++row) {
        const sprog_row_t *r = &f->def->rows[row];
        gfx_rect_t rr;
        sprog_cell_t *cl;
        int line;

        if (r->tab != sprog_tab(f) || f->cells >= SPROG_CELLS) {
            continue;
        }
        rr = sprog_row_rect(f, row);
        if (rr.w == 0) {
            continue;
        }
        line = (rr.y - f->area.y - 64) / 56;
        f->lines = (uint8_t)(line + 1 > f->lines ? line + 1 : f->lines);
        f->cell_row[f->cells] = (int16_t)row;
        cl = &f->view.cell[f->cells++];
        cl->kind = r->kind;
        cl->press = r->kind == SPROG_K_SHOW || r->kind == SPROG_K_GAP
                        ? SPROG_P_OFF : SPROG_P_TAP;
        cl->press2 = cl->press;
        cl->hint = -1;
    }
    if (tab != sprog_tab(f)) {
        f->note_page = 0;
    }
}

sprog_cell_t *sprog_cell(sprog_t *f, int row)
{
    for (int i = 0; i < f->cells; ++i) {
        if (f->cell_row[i] == row) {
            return &f->view.cell[i];
        }
    }
    return NULL;
}

void sprog_note(sprog_t *f, const char *s, uint8_t level)
{
    if (s != NULL && f->view.notes < SPROG_NOTES) {
        f->view.note[f->view.notes] = s;
        f->view.note_level[f->view.notes++] = level;
    }
}

void sprog_hint(sprog_t *f)
{
    sprog_note(f, ui_tr(f->def->hint), SPROG_LV_DIM);
}

/* --------------------------------------------------------------- presses */

/**
 * @brief The stop control at (@p x, @p y): a cell whose control, or whose
 *        pair's key, is SPROG_P_STOP.  None while an overlay covers the
 *        rows.
 * @param f   the frame
 * @param x   the column
 * @param y   the line
 * @param key receives the key of a pair, 0 for any other
 * @return the row, or -1
 */
static int stop_at(const sprog_t *f, int x, int y, int *key)
{
    if (!f->open || f->page.open || editor_open(f)) {
        return -1;
    }
    for (int i = 0; i < f->cells; ++i) {
        const sprog_cell_t *cl = &f->view.cell[i];
        const int row = f->cell_row[i];

        *key = 0;
        if (cl->kind == SPROG_K_PAIR) {
            for (int k = 0; k < 2; ++k) {
                if ((k == 0 ? cl->press : cl->press2) == SPROG_P_STOP
                    && gfx_rect_contains(sprog_pair_rect(f, row, k), x, y)) {
                    *key = k;
                    return row;
                }
            }
        } else if (cl->press == SPROG_P_STOP
                   && gfx_rect_contains(sprog_value_rect(f, row), x, y)) {
            return row;
        }
    }
    return -1;
}

bool sprog_safe_at(const sprog_t *f, int x, int y)
{
    int key = 0;

    return stop_at(f, x, y, &key) >= 0;
}

/**
 * @brief A press on a control that has a why: the why in the alert band.
 * @param cl  the cell
 * @param row its row
 * @param key the key pressed
 * @return SPROG_R_REFUSED
 */
static sprog_result_t refused(const sprog_cell_t *cl, int row, int key)
{
    ui_action_refused(cl->why);
    return result(SPROG_R_REFUSED, row, key, 0, 0);
}

/**
 * @brief A DOWN on the cells of the tab shown.
 * @param f   the frame
 * @param evt the DOWN
 * @param hit receives true when a control took or refused it
 * @return what happened
 */
static sprog_result_t down_cells(sprog_t *f, const touch_event_t *evt,
                                 bool *hit)
{
    const int x = evt->point.x, y = evt->point.y;

    *hit = true;
    for (int i = 0; i < f->cells; ++i) {
        const sprog_cell_t *cl = &f->view.cell[i];
        const int row = f->cell_row[i];

        if (cl->kind == SPROG_K_GAP || cl->kind == SPROG_K_SHOW) {
            continue;
        }
        if (cl->kind == SPROG_K_STEP && cl->press != SPROG_P_OFF) {
            /* The steps act on the press and repeat while it is held. */
            const bool up = gfx_rect_contains(sprog_step_rect(f, row, true),
                                              x, y);

            if (up || gfx_rect_contains(sprog_step_rect(f, row, false), x,
                                        y)) {
                if (cl->why != NULL) {
                    return refused(cl, row, up ? 1 : -1);
                }
                take(f, evt, up ? SPROG_ON_PLUS : SPROG_ON_MINUS, row, 0);
                return result(SPROG_R_STEP, row, up ? 1 : -1, 0, 1);
            }
        }
        if (cl->kind == SPROG_K_PAIR) {
            for (int k = 0; k < 2; ++k) {
                const int p = k == 0 ? cl->press : cl->press2;

                if (!gfx_rect_contains(sprog_pair_rect(f, row, k), x, y)) {
                    continue;
                }
                if (cl->why != NULL) {
                    return refused(cl, row, k);
                }
                if (p == SPROG_P_TAP) {
                    take(f, evt, SPROG_ON_ROW, row, k);
                }
                return nothing();
            }
            continue;
        }
        if (!gfx_rect_contains(sprog_value_rect(f, row), x, y)) {
            continue;
        }
        if (cl->why != NULL) {
            return refused(cl, row, 0);
        }
        if (cl->press == SPROG_P_HOLD) {
            take(f, evt, SPROG_ON_HOLD, row, 0);
            f->hold_row = (int16_t)row;
            ui_hold_reset(&f->hold);
            ui_hold_begin(&f->hold, cl->hint);
            return result(SPROG_R_HOLD_BEGIN, row, 0, 0, 0);
        }
        if (cl->press == SPROG_P_TAP) {
            take(f, evt, SPROG_ON_ROW, row, 0);
            return nothing();
        }
    }
    *hit = false;
    return nothing();
}

/**
 * @brief A DOWN while no press is held: a stop control acts at once and
 *        its press taps nothing on its release; else the control under it
 *        takes the press.
 * @param f   the frame
 * @param evt the DOWN
 * @return what happened
 */
static sprog_result_t down(sprog_t *f, const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;
    int key = 0;
    const int stop = stop_at(f, x, y, &key);
    bool hit = false;
    sprog_result_t r;

    if (stop >= 0) {
        take(f, evt, SPROG_ON_ABSORB, -1, 0);
        return result(SPROG_R_STOP, stop, key, 0, 0);
    }
    if (f->page.open) {
        if (gfx_rect_contains(sprog_apply_rect(f), x, y)) {
            take(f, evt, SPROG_ON_APPLY, -1, 0);
            f->page.down = true;
            ui_hold_begin(&f->page.hold, f->page.hint);
        } else if (gfx_rect_contains(sprog_page_cancel_rect(f), x, y)) {
            take(f, evt, SPROG_ON_CANCEL, -1, 0);
        }
        return nothing();
    }
    if (f->kp.open) {
        take(f, evt, SPROG_ON_KEYPAD, -1, 0);
        (void)ui_keypad_event(&f->kp, evt, NULL);
        return nothing();
    }
    if (f->tk.open) {
        take(f, evt, SPROG_ON_TEXTKEY, -1, 0);
        (void)ui_textkey_event(&f->tk, evt, NULL, 0);
        return nothing();
    }
    if (f->list.open) {
        for (int i = 0; i < f->list.count; ++i) {
            if (gfx_rect_contains(sprog_list_rect(f, i), x, y)) {
                take(f, evt, SPROG_ON_LIST, i, 0);
                return nothing();
            }
        }
        if (gfx_rect_contains(sprog_list_cancel_rect(f), x, y)) {
            take(f, evt, SPROG_ON_LIST_CANCEL, -1, 0);
        }
        return nothing();
    }
    if (gfx_rect_contains(tabs_rect(f), x, y)) {
        take(f, evt, SPROG_ON_TAB, -1, 0);
        (void)ui_tabs_event(&f->tabs, evt);
        return nothing();
    }
    r = down_cells(f, evt, &hit);
    if (hit) {
        return r;
    }
    if (f->view.port_pick && gfx_rect_contains(sprog_head_rect(f), x, y)) {
        take(f, evt, SPROG_ON_PORT, -1, 0);
    } else if (gfx_rect_contains(sprog_notes_rect(f), x, y)) {
        take(f, evt, SPROG_ON_NOTES, -1, 0);
    }
    return nothing();
}

/**
 * @brief The value of the keypad's @p v, x 10^decimals, rounded.
 * @param f the frame
 * @param v the value
 * @return the value
 */
static int32_t scaled(const sprog_t *f, float v)
{
    float s = v;

    for (int i = 0; i < f->kp_dec; ++i) {
        s *= 10.0f;
    }
    return (int32_t)(s >= 0.0f ? s + 0.5f : s - 0.5f);
}

/**
 * @brief A MOVE or UP of a press on an editor: the keypad, the keyboard
 *        or the tabs.
 * @param f   the frame
 * @param evt the event
 * @return what happened
 */
static sprog_result_t rest_editor(sprog_t *f, const touch_event_t *evt)
{
    const bool up = evt->type == TOUCH_EVENT_UP;

    if (f->on == SPROG_ON_KEYPAD) {
        float v = 0.0f;
        const ui_keypad_result_t k = ui_keypad_event(&f->kp, evt, &v);

        if (up) {
            let_go(f);
        }
        return k == UI_KEYPAD_OK
                   ? result(SPROG_R_KEYPAD, -1, 0, f->kp_what, scaled(f, v))
               : k == UI_KEYPAD_CANCEL
                   ? result(SPROG_R_KEYPAD_CANCEL, -1, 0, f->kp_what, 0)
                   : nothing();
    }
    if (f->on == SPROG_ON_TEXTKEY) {
        char name[UI_TEXTKEY_MAX + 1] = "";
        const ui_textkey_result_t k = ui_textkey_event(&f->tk, evt, name,
                                                       sizeof(name));

        if (up) {
            let_go(f);
        }
        if (k == UI_TEXTKEY_OK) {
            snprintf(f->name, sizeof(f->name), "%s", name);
            return result(SPROG_R_NAME, -1, 0, 0, 0);
        }
        return k == UI_TEXTKEY_CANCELLED
                   ? result(SPROG_R_NAME_CANCEL, -1, 0, 0, 0) : nothing();
    }
    {
        const int was = f->tabs.selected;

        (void)ui_tabs_event(&f->tabs, evt);
        if (up) {
            let_go(f);
        }
        if (f->tabs.selected != was) {
            f->note_page = 0;
            return result(SPROG_R_TAB, -1, sprog_tab(f), 0, 0);
        }
        return nothing();
    }
}

/**
 * @brief A MOVE of a hold's press: one that leaves the control abandons
 *        it.  An UP ends the hold: a short release shows its hint.
 * @param f    the frame
 * @param evt  the event
 * @param h    the hold
 * @param rect the control
 */
static void rest_hold(sprog_t *f, const touch_event_t *evt, ui_hold_t *h,
                      gfx_rect_t rect)
{
    if (evt->type != TOUCH_EVENT_UP) {
        if (!gfx_rect_contains(rect, evt->point.x, evt->point.y)
            && ui_hold_leave(h)) {
            f->page.down = false;
            let_go(f);
        }
        return;
    }
    (void)ui_hold_end(h, evt, rect);
    f->page.down = false;
    let_go(f);
}

/**
 * @brief A tap release of a row's press: TAP while the control under it
 *        still takes a tap, by the view at the release.
 * @param f   the frame
 * @param row the row
 * @param key the key
 * @param x   the column
 * @param y   the line
 * @return what happened
 */
static sprog_result_t row_tap(sprog_t *f, int row, int key, int x, int y)
{
    const sprog_cell_t *cl = sprog_cell(f, row);

    if (cl == NULL || cl->why != NULL) {
        return nothing();
    }
    if (cl->kind == SPROG_K_PAIR) {
        if ((key == 0 ? cl->press : cl->press2) == SPROG_P_TAP
            && gfx_rect_contains(sprog_pair_rect(f, row, key), x, y)) {
            return result(SPROG_R_TAP, row, key, 0, 0);
        }
        return nothing();
    }
    if (cl->press == SPROG_P_TAP
        && gfx_rect_contains(sprog_value_rect(f, row), x, y)) {
        return result(SPROG_R_TAP, row, 0, 0, 0);
    }
    return nothing();
}

/**
 * @brief A MOVE or UP of the press held.
 * @param f   the frame
 * @param evt the event
 * @return what happened
 */
static sprog_result_t rest(sprog_t *f, const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;
    const int on = f->on, which = f->row, key = f->key;
    const int what_list = f->list.what, what_page = f->page.what;

    switch (on) {
    case SPROG_ON_KEYPAD:
    case SPROG_ON_TEXTKEY:
    case SPROG_ON_TAB:
        return rest_editor(f, evt);
    case SPROG_ON_HOLD:
        rest_hold(f, evt, &f->hold, sprog_value_rect(f, which));
        return nothing();
    case SPROG_ON_APPLY:
        rest_hold(f, evt, &f->page.hold, sprog_apply_rect(f));
        return nothing();
    case SPROG_ON_MINUS:
    case SPROG_ON_PLUS:
        /* A finger that leaves the key ends the repeat. */
        if (evt->type == TOUCH_EVENT_UP
            || !gfx_rect_contains(sprog_step_rect(f, which,
                                                  on == SPROG_ON_PLUS),
                                  x, y)) {
            let_go(f);
        }
        return nothing();
    default:
        break;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return nothing();
    }
    let_go(f);
    if (!touch_event_is_tap_up(evt)) {
        return nothing();
    }
    if ((on == SPROG_ON_ROW || on == SPROG_ON_PORT || on == SPROG_ON_NOTES)
        && (f->page.open || editor_open(f))) {
        return nothing();       /* an overlay opened under the press */
    }
    switch (on) {
    case SPROG_ON_ROW:
        return row_tap(f, which, key, x, y);
    case SPROG_ON_PORT:
        return f->view.port_pick
                       && gfx_rect_contains(sprog_head_rect(f), x, y)
                   ? result(SPROG_R_PORT, -1, 0, 0, 0) : nothing();
    case SPROG_ON_NOTES:
        if (gfx_rect_contains(sprog_notes_rect(f), x, y)) {
            f->note_page = (uint8_t)((f->note_page + 1)
                                     % sprog_note_pages(f));
        }
        return nothing();
    case SPROG_ON_LIST:
        if (f->list.open && which < f->list.count
            && gfx_rect_contains(sprog_list_rect(f, which), x, y)) {
            f->list.open = false;
            return result(SPROG_R_LIST, -1, 0, what_list,
                          f->list.value[which]);
        }
        return nothing();
    case SPROG_ON_LIST_CANCEL:
        if (gfx_rect_contains(sprog_list_cancel_rect(f), x, y)) {
            f->list.open = false;
            return result(SPROG_R_LIST_CANCEL, -1, 0, what_list, 0);
        }
        return nothing();
    case SPROG_ON_CANCEL:
        if (gfx_rect_contains(sprog_page_cancel_rect(f), x, y)) {
            sprog_close_editors(f);
            return result(SPROG_R_PAGE_CANCEL, -1, 0, what_page, 0);
        }
        return nothing();
    default:
        return nothing();
    }
}

sprog_result_t sprog_event(sprog_t *f, const touch_event_t *evt)
{
    sprog_result_t r = nothing();

    if (evt == NULL || !f->open) {
        return r;
    }
    sig_follow(f);
    if (f->have && evt->point.id == f->id && evt->type != TOUCH_EVENT_DOWN) {
        r = rest(f, evt);
    } else if (evt->type == TOUCH_EVENT_DOWN && !f->have) {
        r = down(f, evt);           /* one press at a time */
    } else if (evt->type == TOUCH_EVENT_DOWN) {
        /* A stop counts on the press whatever other fingers hold; a press
         * held on a row loses its tap, its release would act on what the
         * row reads after the stop. */
        int key = 0;
        const int stop = stop_at(f, evt->point.x, evt->point.y, &key);

        if (stop >= 0) {
            if (f->on == SPROG_ON_ROW) {
                let_go(f);
            }
            r = result(SPROG_R_STOP, stop, key, 0, 0);
        }
    }
    ++f->rev;
    return r;
}

/* ------------------------------------------------------------------ time */

/**
 * @brief A frame of a row hold's press: an editor, the page, the row gone
 *        from the tab or its control no hold any more ends it; a why ends
 *        it with the why; else it counts on, due on the next tick when it
 *        completes.
 * @param f    the frame
 * @param dt_s the frame's time, s
 */
static void hold_frame(sprog_t *f, float dt_s)
{
    const sprog_cell_t *cl = sprog_cell(f, f->hold_row);

    if (f->page.open || editor_open(f) || cl == NULL
        || cl->press != SPROG_P_HOLD) {
        (void)ui_hold_left(&f->hold);
        let_go(f);
        ++f->rev;
    } else if (cl->why != NULL) {
        ui_hold_reset(&f->hold);
        let_go(f);
        ui_action_refused(cl->why);
        ++f->rev;
    } else {
        if (ui_hold_tick(&f->hold, dt_s)) {
            f->hold_due = true;
            ++f->rev;
        }
        ++f->hold_rev;
    }
}

/**
 * @brief Steps the flash of @p h, if it runs: the hold revision each
 *        frame, the revision as it ends.
 * @param f    the frame
 * @param h    the hold
 * @param dt_s the frame's time, s
 */
static void flash_frame(sprog_t *f, ui_hold_t *h, float dt_s)
{
    if (ui_hold_flashing(h)) {
        ui_hold_flash_step(h, dt_s);
        ++f->hold_rev;
        if (!ui_hold_flashing(h)) {
            ++f->rev;           /* what is under the flash, whole */
        }
    }
}

sprog_result_t sprog_tick(sprog_t *f, float dt_s)
{
    sprog_result_t r = nothing();

    sig_follow(f);
    if (f->hold_due) {
        /* A tick after the completion: the frame's drains ran between,
         * so a touch loss over the hold has cancelled it already. */
        f->hold_due = false;
        ++f->rev;
        r = result(SPROG_R_HOLD_DUE, f->hold_row, 0, 0, 0);
    }
    if (f->have && f->on == SPROG_ON_HOLD && !f->hold.fired) {
        hold_frame(f, dt_s);
    }
    flash_frame(f, &f->hold, dt_s);
    if (f->page.open && f->page.down) {
        if (ui_hold_tick(&f->page.hold, dt_s)) {
            /* The page closes; its flash runs on and the press, still
             * down, taps nothing on its release. */
            f->page.open = false;
            f->page.down = false;
            ui_hold_spend(&f->page.hold);
            f->on = SPROG_ON_ABSORB;
            ++f->rev;
            r = result(SPROG_R_PAGE_APPLY, -1, 0, f->page.what, 0);
        }
        ++f->hold_rev;
    }
    flash_frame(f, &f->page.hold, dt_s);
    if (f->have && (f->on == SPROG_ON_PLUS || f->on == SPROG_ON_MINUS)) {
        const sprog_cell_t *cl = sprog_cell(f, f->row);

        if (f->page.open || editor_open(f) || cl == NULL
            || cl->press == SPROG_P_OFF) {
            let_go(f);          /* a page took over: the repeat ends */
        } else if (cl->why != NULL) {
            /* Refused now: the repeat ends as a fresh press would. */
            let_go(f);
            ui_action_refused(cl->why);
            ++f->rev;
        } else {
            const int n = ui_key_repeat(&f->held_s, dt_s);

            if (n > 0) {
                ++f->rev;
                if (r.kind == SPROG_R_NONE) {
                    r = result(SPROG_R_STEP, f->row,
                               f->on == SPROG_ON_PLUS ? 1 : -1, 0, n);
                }
            }
        }
    }
    return r;
}

bool sprog_has_press(const sprog_t *f, uint8_t id)
{
    return f->have && f->id == id;
}

uint32_t sprog_revision(const sprog_t *f)
{
    return f->rev + f->kp.revision + f->tk.revision;
}

uint32_t sprog_hold_revision(const sprog_t *f)
{
    return f->hold_rev;
}

gfx_rect_t sprog_hold_rect(const sprog_t *f)
{
    if (!f->page.open && ui_hold_flashing(&f->page.hold)
        && f->page.hold.drawn_f != NULL) {
        /* A page that closed as its hold fired: where its flash is drawn. */
        return f->page.hold.drawn_r;
    }
    if (f->hold_row >= 0 && (f->hold.down || ui_hold_flashing(&f->hold))) {
        const gfx_rect_t r = sprog_value_rect(f, f->hold_row);

        if (r.w != 0) {
            return r;
        }
    }
    return sprog_apply_rect(f);
}

/* --------------------------------------------------------------- editors */

void sprog_keypad(sprog_t *f, int16_t what, const char *title,
                  const char *unit, float value, float lo, float hi,
                  int decimals)
{
    ui_keypad_open(&f->kp, f->area, title, unit, value, lo, hi, decimals);
    f->kp_what = what;
    f->kp_dec = (uint8_t)(decimals > 0 ? decimals : 0);
    ++f->rev;
}

void sprog_list_open(sprog_t *f, int16_t what, const char *title,
                     bool files)
{
    f->list.open = true;
    f->list.files = files;
    f->list.what = what;
    f->list.count = 0;
    f->list.current = SPROG_NO_VALUE;
    snprintf(f->list.title, sizeof(f->list.title), "%s", title);
    ++f->rev;
}

bool sprog_list_add(sprog_t *f, const char *label, const char *sub,
                    int32_t value)
{
    char *out;

    if (f->list.count >= SPROG_LIST_MAX) {
        return false;
    }
    out = f->list.label[f->list.count];
    if (sub == NULL) {
        snprintf(out, SPROG_LIST_LABEL, "%s", label);
    } else if (f->list.files) {
        snprintf(out, SPROG_LIST_LABEL, "%-*s %s", SPROG_FILE_CELLS, label,
                 sub);
    } else {
        snprintf(out, SPROG_LIST_LABEL, "%s  %s", label, sub);
    }
    f->list.value[f->list.count++] = value;
    {
        /* An entry that would reach CANCEL is not taken. */
        const gfx_rect_t r = sprog_list_rect(f, f->list.count - 1);

        if (r.y + r.h > sprog_list_cancel_rect(f).y - 8) {
            --f->list.count;
            return false;
        }
    }
    return true;
}

void sprog_list_current(sprog_t *f, int32_t value)
{
    f->list.current = value;
}

void sprog_name(sprog_t *f, const char *title, const char *preset)
{
    ui_textkey_open(&f->tk, f->area, title, preset, SPROG_NAME_MAX);
    ++f->rev;
}
