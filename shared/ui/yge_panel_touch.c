/**
 * @file
 * @brief Programming for YGE ESCs: the presses.  One at a time, a tap acts
 *        on its release, ABORT on the press.  See yge_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>

#include "link_pages.h"
#include "yge_panel_int.h"

/** @brief The places of the footer. */
#define YGP_ACTIONS 4

/**
 * @brief Where a press shows.
 * @param what What it is on.
 * @param i    Its row.
 * @return The button.
 */
static gfx_rect_t press_rect(int what, int i)
{
    switch (what) {
    case YGP_P_BACK:
        return yge_panel_back_rect();
    case YGP_P_ROW:
    case YGP_P_CONNECT_ROW:
        return yge_panel_value_rect(i);
    case YGP_P_ACT:
        return ygp_action_rect(i);
    case YGP_P_HOLD:
        return yge_panel_hold_rect();
    case YGP_P_CHOICE:
        return ygp_choice_rect(i);
    default:
        return yge_panel_cancel_rect();
    }
}

/**
 * @brief The press taken.  A button is the operator's next action, so what
 *        the screen said goes; a press inside an open keypad is not on one
 *        yet.
 * @param evt  The event.
 * @param what What it is on.
 * @param i    Its row.
 */
static void take(const touch_event_t *evt, int what, int i)
{
    if (what != YGP_P_KEYPAD) {
        ygp_words_clear();
    }
    ygp_s.have = true;
    ygp_s.id = evt->point.id;
    ygp_s.pressed = what;
    ygp_s.which = i;
    /* Only the button changes, in the pressed colour: the rest stands. */
    if (what == YGP_P_TAB || what == YGP_P_KEYPAD) {
        ++ygp_s.rev;
    } else {
        ygp_s.touched = press_rect(what, i);
        ++ygp_s.hold_rev;
    }
}

/** @brief The press ended. */
static void let_go(void)
{
    ygp_s.have = false;
    ygp_s.pressed = YGP_P_NONE;
}

bool ygp_abort_shows(void)
{
    const yge_panel_stage_t st = ygp_stage();

    return st == YGE_PANEL_ENTERING || st == YGE_PANEL_HAND
           || st == YGE_PANEL_RUNNING;
}

bool ygp_safe_at(int x, int y)
{
    return ygp_s.open && !ygp_s.kp.open && ygp_s.ch_what == YGP_CH_NONE
           && ygp_s.page == YGP_PAGE_NONE && ygp_abort_shows()
           && gfx_rect_contains(ygp_action_rect(0), x, y);
}

void ygp_leave(void)
{
    const int n = ygp_changed_count();

    if (n == 0) {
        (void)yge_prog_leave(&ygp_s.prog);
        ++ygp_s.rev;
        return;
    }
    char line[YGP_LINE];

    ygp_page_open(YGP_PAGE_LEAVE);
    snprintf(line, sizeof(line), TR(YG_LEAVE_EDITS), (unsigned)n);
    ygp_page_add(line);
    ygp_page_changes();
}

/**
 * @brief READ AGAIN: at once with no edits, else its page.
 */
static void read_again(void)
{
    if (ygp_changed_count() == 0) {
        (void)yge_prog_read(&ygp_s.prog);
        return;
    }
    ygp_page_open(YGP_PAGE_READ);
    ygp_page_add(TR(EW_LEAVE_DRAFT));
    ygp_page_add(TR(EW_READ_1));
}

/**
 * @brief The footer's button @p i tapped.
 * @param i The button.
 */
static void action(int i)
{
    yge_prog_t *m = &ygp_s.prog;

    switch (ygp_stage()) {
    case YGE_PANEL_HAND:
        if (i == 1) {
            (void)yge_prog_hand_done(m);
        }
        break;
    case YGE_PANEL_SESSION:
        if (i == 0) {
            read_again();
        } else if (i == 1) {
            if (ygp_card_busy()) {
                ygp_note(TR(YG_CARD_BUSY), true);
            } else {
                ygp_s.files_first = 0u;
                ygp_files_open();
            }
        } else if (i == 3) {
            ygp_leave();
        }
        break;
    case YGE_PANEL_STOPPED:
        /* CLEAR STOP is for a page's stop: the panel's own stands until it is
         * released, which needs ARM, refused under this hold. */
        if (i == 0 && !m->f.stopped) {
            (void)yge_prog_clear(m);
        } else if (i == 1) {
            (void)yge_prog_close(m);
        }
        break;
    case YGE_PANEL_OTHER:
        if (i == 0) {
            char pin[YGP_LINE];
            char line[YGP_LINE];

            ygp_page_open(YGP_PAGE_RELEASE);
            ygp_page_add(TR(EW_RELEASE_1));
            (void)yge_panel_target_text(pin, sizeof(pin));
            snprintf(line, sizeof(line), TR(EW_RELEASE_PIN), pin);
            ygp_page_add(line);
            ygp_page_add(TR(EW_RELEASE_2));
        }
        break;
    case YGE_PANEL_NOT_BACK:
        if (i == 0) {
            (void)yge_prog_close(m);
        }
        break;
    case YGE_PANEL_PIN_HELD:
        if (i == 2) {
            (void)yge_prog_close(m);
        }
        break;
    default:
        break;
    }
}

/**
 * @brief Whether the footer's button @p i is on the screen, as the stage
 *        draws it.  WRITE n VALUES is the hold's, not a tap's.
 * @param i The button.
 * @return true for one shown.
 */
static bool action_shown(int i)
{
    switch (ygp_stage()) {
    case YGE_PANEL_HAND:
        return i == 0 || i == 1;
    case YGE_PANEL_ENTERING:
    case YGE_PANEL_RUNNING:
        return i == 0;
    case YGE_PANEL_STOPPED:
        return i == 1 || (i == 0 && !ygp_s.prog.f.stopped);
    case YGE_PANEL_SESSION:
        return i == 0 || i == 1 || i == 3;
    case YGE_PANEL_PIN_HELD:
        return i == 2;
    case YGE_PANEL_OTHER:
    case YGE_PANEL_NOT_BACK:
        return i == 0;
    default:
        return false;
    }
}

/**
 * @brief A press on the rows of the CONNECT page or of a tab.
 * @param evt The event.
 */
static void down_rows(const touch_event_t *evt)
{
    const int x = evt->point.x;
    const int y = evt->point.y;
    const yge_panel_stage_t st = ygp_stage();

    if (st == YGE_PANEL_CONNECT) {
        const bool hand = ygp_s.power == (uint8_t)LINK_YGE_BY_HAND;

        for (int i = 0; i < YGP_C_ROWS; ++i) {
            /* BY HAND has no voltage and no limit of this bench's. */
            if (hand && (i == YGP_C_VOLT || i == YGP_C_AMP)) {
                continue;
            }
            if (gfx_rect_contains(yge_panel_value_rect(i), x, y)) {
                take(evt, YGP_P_CONNECT_ROW, i);
                return;
            }
        }
        return;
    }
    if (st != YGE_PANEL_SESSION) {
        return;
    }
    for (int i = 0; i < ygp_s.tabs.count; ++i) {
        if (gfx_rect_contains(ygp_s.tabs.rect[i], x, y)) {
            take(evt, YGP_P_TAB, -1);
            (void)ui_tabs_event(&ygp_s.tabs, evt);
            return;
        }
    }
    for (int i = 0; i < YGP_TAB_ROWS; ++i) {
        ygp_row_t r;

        if (ygp_row(ygp_s.tabs.selected, i, &r) && ygp_row_live(&r)
            && gfx_rect_contains(ygp_value_rect(i), x, y)) {
            take(evt, YGP_P_ROW, i);
            return;
        }
    }
}

/**
 * @brief What a hold says it does when it is let go early.
 * @return Its hint.
 */
static ui_text_id_t hold_hint(void)
{
    switch (ygp_s.page) {
    case YGP_PAGE_LEAVE:
        return TX_HOLD_HINT_LEAVE_DROP;
    case YGP_PAGE_READ:
        return TX_EW_HINT_READ;
    case YGP_PAGE_RELEASE:
        return TX_EW_HINT_RELEASE;
    default:
        break;
    }
    switch (ygp_stage()) {
    case YGE_PANEL_PIN_HELD:
        return TX_EW_HOLD_ENTER;
    case YGE_PANEL_SESSION:
        return TX_HOLD_HINT_WRITE;
    default:
        return TX_HOLD_HINT_CONNECT;
    }
}

/**
 * @brief A press on an open editor: the keypad, or the list.
 * @param evt The event.
 * @return true when an editor is open: it takes the press or nothing does.
 */
static bool down_editor(const touch_event_t *evt)
{
    const int x = evt->point.x;
    const int y = evt->point.y;

    if (ygp_s.kp.open) {
        take(evt, YGP_P_KEYPAD, -1);
        (void)ui_keypad_event(&ygp_s.kp, evt, NULL);
        return true;
    }
    if (ygp_s.ch_what == YGP_CH_NONE) {
        return false;
    }
    for (int i = 0; i < ygp_s.ch_count; ++i) {
        if (gfx_rect_contains(ygp_choice_rect(i), x, y)) {
            take(evt, YGP_P_CHOICE, i);
            return true;
        }
    }
    if (gfx_rect_contains(yge_panel_cancel_rect(), x, y)) {
        take(evt, YGP_P_CHOICE_CANCEL, -1);
    }
    return true;
}

/**
 * @brief A DOWN: what is under it takes the press.
 * @param evt The event.
 */
static void down(const touch_event_t *evt)
{
    const int x = evt->point.x;
    const int y = evt->point.y;

    if (down_editor(evt)) {
        return;
    }
    if (gfx_rect_contains(yge_panel_hold_rect(), x, y)) {
        /* WRITE n VALUES greyed takes no press. */
        if (yge_panel_hold_live()) {
            take(evt, YGP_P_HOLD, -1);
            ygp_s.hold_down = true;
            ui_hold_begin(&ygp_s.hold, hold_hint());
        }
        return;
    }
    if (ygp_s.page != YGP_PAGE_NONE) {
        if (gfx_rect_contains(yge_panel_cancel_rect(), x, y)) {
            take(evt, YGP_P_CANCEL, -1);
        }
        return;
    }
    if (gfx_rect_contains(yge_panel_back_rect(), x, y)) {
        take(evt, YGP_P_BACK, -1);
        return;
    }
    for (int i = 0; i < YGP_ACTIONS; ++i) {
        if (action_shown(i) && gfx_rect_contains(ygp_action_rect(i), x, y)) {
            take(evt, YGP_P_ACT, i);
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
    const int was = ygp_s.pressed;
    const int i = ygp_s.which;
    ygp_row_t r;

    let_go();
    if (!gfx_rect_contains(press_rect(was, i), x, y)) {
        return;
    }
    switch (was) {
    case YGP_P_BACK:
        ygp_back();
        break;
    case YGP_P_ROW:
        if (ygp_row(ygp_s.tabs.selected, i, &r) && ygp_row_live(&r)) {
            ygp_edit_open(&r);
        }
        break;
    case YGP_P_CONNECT_ROW:
        ygp_connect_row(i);
        break;
    case YGP_P_ACT:
        action(i);
        break;
    case YGP_P_CANCEL:
        ygp_s.page = YGP_PAGE_NONE;
        break;
    case YGP_P_CHOICE:
        ygp_choice_done(i);
        break;
    case YGP_P_CHOICE_CANCEL:
        ygp_editors_close();
        break;
    default:
        break;
    }
}

/**
 * @brief The keypad's share of a press it took.
 * @param evt The event.
 * @param up  It is the release.
 */
static void rest_keypad(const touch_event_t *evt, bool up)
{
    float v = 0.0f;
    const ui_keypad_result_t r = ui_keypad_event(&ygp_s.kp, evt, &v);

    if (up) {
        let_go();
    }
    if (r == UI_KEYPAD_OK) {
        ygp_keypad_done(v);
    } else if (r == UI_KEYPAD_CANCEL) {
        ygp_editors_close();
    }
}

/**
 * @brief The rest of the press taken.
 * @param evt The event.
 */
static void rest(const touch_event_t *evt)
{
    const bool up = evt->type == TOUCH_EVENT_UP;

    switch (ygp_s.pressed) {
    case YGP_P_KEYPAD:
        rest_keypad(evt, up);
        return;
    case YGP_P_TAB:
        (void)ui_tabs_event(&ygp_s.tabs, evt);
        if (up) {
            ygp_s.note[0] = '\0';
            let_go();
        }
        return;
    case YGP_P_HOLD:
        if (!up) {
            /* A finger that leaves the button abandons the hold. */
            if (!gfx_rect_contains(yge_panel_hold_rect(), evt->point.x,
                                   evt->point.y)
                && ui_hold_leave(&ygp_s.hold)) {
                ygp_s.hold_down = false;
                let_go();
                ++ygp_s.rev;
            }
            return;
        }
        (void)ui_hold_end(&ygp_s.hold, evt, yge_panel_hold_rect());
        ygp_s.hold_down = false;
        let_go();
        return;
    default:
        /* A press cut by the band or another finger ends here and does
         * nothing. */
        if (touch_event_is_tap_up(evt)) {
            release(evt);
        } else if (up) {
            let_go();
        }
        return;
    }
}

void ygp_event(const touch_event_t *evt)
{
    if (evt == NULL || !ygp_s.open) {
        return;
    }
    /* ABORT on the press, whatever another finger holds: a stop waits for no
     * one. */
    if (evt->type == TOUCH_EVENT_DOWN
        && ygp_safe_at(evt->point.x, evt->point.y)) {
        yge_prog_abort(&ygp_s.prog);
        ++ygp_s.rev;
    } else if (ygp_s.have && evt->point.id == ygp_s.id
               && evt->type != TOUCH_EVENT_DOWN) {
        rest(evt);
        /* A move changes nothing shown; what a release does can change
         * anything. */
        if (evt->type == TOUCH_EVENT_UP) {
            ++ygp_s.rev;
        }
    } else if (evt->type == TOUCH_EVENT_DOWN && !ygp_s.have) {
        down(evt);
    }
}

void ygp_tick(float dt_s)
{
    if (ygp_s.hold_down) {
        if (ui_hold_tick(&ygp_s.hold, dt_s)) {
            ygp_hold_apply();
        }
        ++ygp_s.hold_rev;
    }
    if (ui_hold_flashing(&ygp_s.hold)) {
        ui_hold_flash_step(&ygp_s.hold, dt_s);
        ++ygp_s.hold_rev;
        if (!ui_hold_flashing(&ygp_s.hold)) {
            ++ygp_s.rev;        /* the page under the flash, whole */
        }
    }
}

void ygp_cancel(void)
{
    ui_keypad_cancel_press(&ygp_s.kp);
    ui_tabs_cancel(&ygp_s.tabs);
    ui_hold_spend(&ygp_s.hold);         /* a flash under way runs on */
    ygp_s.hold_down = false;
    let_go();
    ++ygp_s.rev;
}

void ygp_back(void)
{
    if (ygp_s.kp.open || ygp_s.ch_what != YGP_CH_NONE) {
        ygp_cancel();
        ygp_editors_close();
        return;
    }
    if (ygp_s.page != YGP_PAGE_NONE) {
        ygp_s.page = YGP_PAGE_NONE;
        ++ygp_s.rev;
        return;
    }
    const yge_panel_stage_t st = ygp_stage();

    if (st == YGE_PANEL_CONNECT) {
        ygp_s.open = false;
    } else if (st == YGE_PANEL_SESSION) {
        ygp_leave();
    } else {
        /* A session on its way or a stop: its own button ends it, and BACK
         * names it. */
        char line[YGP_LINE];

        snprintf(line, sizeof(line), TR(EW_BACK_FIRST),
                 st == YGE_PANEL_STOPPED || st == YGE_PANEL_PIN_HELD
                     ? TR(EW_CLOSE_PIN)
                 : st == YGE_PANEL_OTHER    ? TR(EW_RELEASE_BTN)
                 : st == YGE_PANEL_NOT_BACK ? TR(EW_CLOSE_AGAIN)
                                            : TR(EW_ABORT));
        ygp_note(line, true);
    }
    ++ygp_s.rev;
}
