/**
 * @file
 * @brief The ESC signal-wire session's presses: one at a time, a tap acts on its
 *        release, ABORT on the press.  See esc_wire_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>

#include "esc_wire_int.h"

/** @brief What a press is on. */
enum {
    P_NONE = 0,
    P_TAB,
    P_BACK,
    P_ROW,
    P_CONNECT_ROW,
    P_ACT,
    P_HOLD,
    P_CANCEL,
    P_CHOICE,
    P_CHOICE_CANCEL,
    P_KEYPAD,
    P_NEXT,
    P_PREV,
};

/**
 * @brief The press taken.
 * @param evt  The event.
 * @param what What it is on.
 * @param i    Its row.
 */
static void take(const touch_event_t *evt, int what, int i)
{
    ewp_s.have = true;
    ewp_s.id = evt->point.id;
    ewp_s.pressed = what;
    ewp_s.which = i;
}

/** @brief The press ended. */
static void let_go(void)
{
    ewp_s.have = false;
    ewp_s.pressed = P_NONE;
}

/**
 * @brief The stage's ABORT: the session's action on the press.
 * @return true where a stage shows one.
 */
static bool abort_shows(void)
{
    const ewp_stage_t st = ewp_stage();

    return st == ESC_WIRE_ENTERING || st == ESC_WIRE_HAND || st == ESC_WIRE_RUNNING;
}

bool ewp_safe_at(int x, int y)
{
    return ewp_s.open && abort_shows()
           && gfx_rect_contains(ewp_action_rect(0), x, y);
}

/**
 * @brief The action row's button @p i tapped.
 * @param i The button.
 */
static void action(int i)
{
    esc_prog_t *m = &ewp_s.prog;
    const uint16_t flags = m->f.rd.flags;

    switch (ewp_stage()) {
    case ESC_WIRE_HAND:
        if (i == 1) {
            (void)esc_prog_powered(m);
        }
        break;
    case ESC_WIRE_SESSION:
        if ((flags & LINK_EW_F_ERASED) != 0u) {
            if (i == 0) {
                ewp_s.files_first = 0u;
                ewp_files_open(true);
            }
        } else if (i == 0) {
            ewp_s.note[0] = '\0';
            (void)esc_prog_read(m);
        } else if (i == 1) {
            ewp_s.files_first = 0u;
            ewp_files_open(false);
        } else if (i == 2 && (flags & LINK_EW_F_WRITABLE) != 0u) {
            if (ewp_card_busy()) {
                ewp_note(TR(EW_CARD_BUSY), true);
            } else if (esc_prog_backup_state(m) == ESC_PROG_BACKUP_NONE) {
                ewp_note(ewp_no_backup_text(), true);
            } else if (ewp_changed_count() > 0) {
                ewp_page_write();
            } else {
                ewp_note(TR(EW_NO_CHANGE), false);
            }
        }
        break;
    case ESC_WIRE_STOPPED:
        if (i == 0) {
            (void)esc_prog_clear(m);
        } else if (i == 1) {
            (void)esc_prog_close(m);
        }
        break;
    case ESC_WIRE_OTHER:
        if (i == 0) {
            ewp_page_open(EWP_PAGE_RELEASE);
            ewp_page_add(TR(EW_RELEASE_1));
            ewp_page_add(TR(EW_RELEASE_2));
        }
        break;
    case ESC_WIRE_NOT_BACK:
        if (i == 0) {
            (void)esc_prog_close(m);
        }
        break;
    case ESC_WIRE_PIN_HELD:
        if (i == 2) {
            (void)esc_prog_close(m);
        }
        break;
    default:
        break;
    }
}

/**
 * @brief How many actions a stage has.
 * @return Their count.
 */
static int action_count(void)
{
    switch (ewp_stage()) {
    case ESC_WIRE_HAND:
    case ESC_WIRE_STOPPED:
        return 2;
    case ESC_WIRE_SESSION:
    case ESC_WIRE_PIN_HELD:
        return 3;
    case ESC_WIRE_OTHER:
    case ESC_WIRE_NOT_BACK:
        return 1;
    default:
        return 0;
    }
}

/**
 * @brief A press on the rows of the connect page or the session.
 * @param evt The event.
 */
static void down_rows(const touch_event_t *evt)
{
    const int x = evt->point.x;
    const int y = evt->point.y;
    const ewp_stage_t st = ewp_stage();

    if (st == ESC_WIRE_CONNECT) {
        const bool hand = ewp_s.power == (uint8_t)LINK_ESC_BY_HAND;

        for (int i = 0; i < EWP_ROWS; ++i) {
            /* BY HAND has no voltage, limit or off time of this bench's. */
            if (hand && (i == 2 || i == 3 || i == 5)) {
                continue;
            }
            if (gfx_rect_contains(ewp_value_rect(i), x, y)) {
                take(evt, P_CONNECT_ROW, i);
                return;
            }
        }
        return;
    }
    if (st != ESC_WIRE_SESSION) {
        return;
    }
    if (gfx_rect_contains(ewp_tabs_rect(), x, y)) {
        take(evt, P_TAB, -1);
        (void)ui_tabs_event(&ewp_s.tabs, evt);
        return;
    }
    for (int i = 0; i < EWP_ROWS * EWP_COLS; ++i) {
        ewp_row_t r;

        if (ewp_row(ewp_s.tabs.selected, i, &r) && ewp_row_live(&r)
            && gfx_rect_contains(ewp_value_rect(i), x, y)) {
            take(evt, P_ROW, i);
            return;
        }
    }
}

/**
 * @brief A DOWN: what is under it takes the press.
 * @param evt The event.
 */
static void down(const touch_event_t *evt)
{
    const int x = evt->point.x;
    const int y = evt->point.y;

    if (ewp_s.kp.open) {
        take(evt, P_KEYPAD, -1);
        (void)ui_keypad_event(&ewp_s.kp, evt, NULL);
        return;
    }
    if (ewp_s.ch_what != EWP_CH_NONE) {
        for (int i = 0; i < ewp_s.ch_count; ++i) {
            if (gfx_rect_contains(ewp_choice_rect(i), x, y)) {
                take(evt, P_CHOICE, i);
                return;
            }
        }
        if (gfx_rect_contains(ewp_choice_cancel_rect(), x, y)) {
            take(evt, P_CHOICE_CANCEL, -1);
        }
        return;
    }
    if (esc_wire_panel_hold_rect().w != 0
        && gfx_rect_contains(esc_wire_panel_hold_rect(), x, y)) {
        take(evt, P_HOLD, -1);
        ewp_s.hold_down = true;
        ui_hold_begin(&ewp_s.hold,
                      ewp_s.page == EWP_PAGE_LEAVE ? TX_HOLD_HINT_LEAVE
                      : ewp_s.page != EWP_PAGE_NONE ? TX_HOLD_HINT_WRITE
                      : ewp_stage() == ESC_WIRE_PIN_HELD ? TX_EW_HOLD_ENTER
                                                         : TX_EW_HOLD_CONNECT);
        return;
    }
    if (ewp_s.page != EWP_PAGE_NONE) {
        if (gfx_rect_contains(esc_wire_panel_cancel_rect(), x, y)) {
            take(evt, P_CANCEL, -1);
        } else if (gfx_rect_contains(esc_wire_panel_next_rect(), x, y)) {
            take(evt, P_NEXT, -1);
        } else if (gfx_rect_contains(esc_wire_panel_prev_rect(), x, y)) {
            take(evt, P_PREV, -1);
        }
        return;
    }
    if (gfx_rect_contains(esc_wire_panel_back_rect(), x, y)) {
        take(evt, P_BACK, -1);
        return;
    }
    for (int i = 0; i < action_count(); ++i) {
        if (gfx_rect_contains(ewp_action_rect(i), x, y)) {
            take(evt, P_ACT, i);
            return;
        }
    }
    down_rows(evt);
}

/**
 * @brief A release on what the press took.
 * @param evt The event.
 */
static void release(const touch_event_t *evt)
{
    const int x = evt->point.x;
    const int y = evt->point.y;
    const int was = ewp_s.pressed;
    const int i = ewp_s.which;
    ewp_row_t r;

    let_go();
    switch (was) {
    case P_BACK:
        if (gfx_rect_contains(esc_wire_panel_back_rect(), x, y)) {
            ewp_back();
        }
        break;
    case P_ROW:
        if (gfx_rect_contains(ewp_value_rect(i), x, y)
            && ewp_row(ewp_s.tabs.selected, i, &r) && ewp_row_live(&r)) {
            ewp_edit_open(&r);
        }
        break;
    case P_CONNECT_ROW:
        if (gfx_rect_contains(ewp_value_rect(i), x, y)) {
            ewp_connect_row(i);
        }
        break;
    case P_ACT:
        if (gfx_rect_contains(ewp_action_rect(i), x, y)) {
            action(i);
        }
        break;
    case P_CANCEL:
        if (gfx_rect_contains(esc_wire_panel_cancel_rect(), x, y)) {
            ewp_s.page = EWP_PAGE_NONE;
        }
        break;
    case P_NEXT:
        if (gfx_rect_contains(esc_wire_panel_next_rect(), x, y)) {
            ewp_write_turn(1);
        }
        break;
    case P_PREV:
        if (gfx_rect_contains(esc_wire_panel_prev_rect(), x, y)) {
            ewp_write_turn(-1);
        }
        break;
    case P_CHOICE:
        if (gfx_rect_contains(ewp_choice_rect(i), x, y)) {
            ewp_choice_done(i);
        }
        break;
    case P_CHOICE_CANCEL:
        if (gfx_rect_contains(ewp_choice_cancel_rect(), x, y)) {
            ewp_editors_close();
        }
        break;
    default:
        break;
    }
}

/**
 * @brief The rest of the press taken.
 * @param evt The event.
 */
static void rest(const touch_event_t *evt)
{
    const bool up = evt->type == TOUCH_EVENT_UP;

    switch (ewp_s.pressed) {
    case P_KEYPAD: {
        float v = 0.0f;
        const ui_keypad_result_t r = ui_keypad_event(&ewp_s.kp, evt, &v);

        if (up) {
            let_go();
        }
        if (r == UI_KEYPAD_OK) {
            ewp_keypad_done(v);
        } else if (r == UI_KEYPAD_CANCEL) {
            ewp_editors_close();
        }
        return;
    }
    case P_TAB:
        (void)ui_tabs_event(&ewp_s.tabs, evt);
        if (up) {
            ewp_s.note[0] = '\0';
            let_go();
        }
        return;
    case P_HOLD:
        if (!up) {
            /* A finger that leaves the button abandons the hold. */
            if (!gfx_rect_contains(esc_wire_panel_hold_rect(), evt->point.x,
                                   evt->point.y)
                && ui_hold_leave(&ewp_s.hold)) {
                ewp_s.hold_down = false;
                let_go();
            }
            return;
        }
        (void)ui_hold_end(&ewp_s.hold, evt, esc_wire_panel_hold_rect());
        ewp_s.hold_down = false;
        let_go();
        return;
    default:
        /* A press cut by the band or another finger ends here and does nothing. */
        if (touch_event_is_tap_up(evt)) {
            release(evt);
        } else if (up) {
            let_go();
        }
        return;
    }
}

void ewp_event(const touch_event_t *evt)
{
    if (evt == NULL || !ewp_s.open) {
        return;
    }
    /* ABORT on the press, whatever another finger holds: a stop waits for no one. */
    if (evt->type == TOUCH_EVENT_DOWN && !ewp_s.kp.open && ewp_s.ch_what == EWP_CH_NONE
        && ewp_s.page == EWP_PAGE_NONE && abort_shows()
        && gfx_rect_contains(ewp_action_rect(0), evt->point.x, evt->point.y)) {
        ewp_abort();
    } else if (ewp_s.have && evt->point.id == ewp_s.id && evt->type != TOUCH_EVENT_DOWN) {
        rest(evt);
    } else if (evt->type == TOUCH_EVENT_DOWN && !ewp_s.have) {
        down(evt);
    }
    ++ewp_s.rev;
}

void ewp_tick(float dt_s)
{
    if (ewp_s.hold_down) {
        if (ui_hold_tick(&ewp_s.hold, dt_s)) {
            ewp_hold_apply();
        }
        ++ewp_s.hold_rev;
    }
}

void ewp_cancel(void)
{
    ui_keypad_cancel_press(&ewp_s.kp);
    ui_tabs_cancel(&ewp_s.tabs);
    ui_hold_reset(&ewp_s.hold);
    ewp_s.hold_down = false;
    let_go();
    ++ewp_s.rev;
}

void ewp_back(void)
{
    if (ewp_s.kp.open || ewp_s.ch_what != EWP_CH_NONE) {
        ewp_cancel();
        ewp_editors_close();
        return;
    }
    if (ewp_s.page != EWP_PAGE_NONE) {
        ewp_s.page = EWP_PAGE_NONE;
        ++ewp_s.rev;
        return;
    }
    switch (ewp_stage()) {
    case ESC_WIRE_CONNECT:
        ewp_s.open = false;
        break;
    case ESC_WIRE_SESSION:
        ewp_page_open(EWP_PAGE_LEAVE);
        if ((ewp_s.prog.f.rd.flags & LINK_EW_F_ERASED) != 0u) {
            ewp_page_add(TR(EW_LEAVE_ERASED_1));
            ewp_page_add(ewp_s.power == (uint8_t)LINK_ESC_BY_HAND
                             ? TR(EW_LEAVE_ERASED_HAND) : TR(EW_LEAVE_ERASED_2));
            break;
        }
        if (ewp_s.prog.f.rd.fw == (uint8_t)EW_FW_E32) {
            ewp_page_add(TR(EW_LEAVE_E32));
            ewp_page_add(ewp_s.power == (uint8_t)LINK_ESC_BY_HAND
                             ? TR(EW_LEAVE_E32_HAND) : TR(EW_LEAVE_E32_SWITCHED));
        } else {
            ewp_page_add(TR(EW_LEAVE_1));
        }
        ewp_page_add(TR(EW_LEAVE_2));
        if (ewp_changed_count() > 0) {
            ewp_page_add(TR(EW_LEAVE_DRAFT));
        }
        break;
    default: {
        /* A session on its way or a stop: its own button ends it, and BACK names it. */
        const ewp_stage_t st = ewp_stage();
        char line[EWP_LINE];

        snprintf(line, sizeof(line), TR(EW_BACK_FIRST),
                 st == ESC_WIRE_STOPPED || st == ESC_WIRE_PIN_HELD ? TR(EW_CLOSE_PIN)
                 : st == ESC_WIRE_OTHER ? TR(EW_RELEASE_BTN)
                 : st == ESC_WIRE_NOT_BACK ? TR(EW_CLOSE_AGAIN)
                                           : TR(EW_ABORT));
        ewp_note(line, true);
        break;
    }
    }
    ++ewp_s.rev;
}
