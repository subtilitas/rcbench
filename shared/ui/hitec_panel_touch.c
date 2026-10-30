/**
 * @file hitec_panel_touch.c
 * @brief The presses on the Hitec programmer's overlay: a DOWN, its moves
 *        and its release, CLOSE and the home tag, each frame's hold, and the
 *        supply's lock while a port is held.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this overlay works with.  The state and the seam are
 * hitec_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel.h"

#include "supply_screen.h"
#include "ui_tabs.h"
#include "ui_textkey.h"
#include "ui_widgets.h"

/* Last: its layout macros and hp, the state, which no header included
 * after it may meet. */
#include "hitec_panel_int.h"

/**
 * @brief Takes a press: its contact, what it is on and its row.
 * @param evt  the DOWN
 * @param what P_*
 * @param row  the row or entry, -1 none
 */
static void take(const touch_event_t *evt, int what, int row)
{
    hp.have = true;
    hp.id = evt->point.id;
    hp.pressed = what;
    hp.row = row;
}

/** @brief Ends the press. */
static void let_go(void)
{
    hp.have = false;
    hp.pressed = P_NONE;
}

/**
 * @brief A DOWN: the control under it takes the press.
 * @param evt the event
 */
static void down(const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;

    if (hp.page.open) {
        if (gfx_rect_contains(hitec_panel_apply_rect(), x, y)) {
            take(evt, P_APPLY, -1);
            hp.page.down = true;
            ui_hold_begin(&hp.page.hold, hp.page.hint);
        } else if (gfx_rect_contains(hitec_panel_cancel_rect(), x, y)) {
            take(evt, P_CANCEL, -1);
        }
        return;
    }
    if (hp.kp.open) {
        take(evt, P_KEYPAD, -1);
        (void)ui_keypad_event(&hp.kp, evt, NULL);
        return;
    }
    if (hp.tk.open) {
        take(evt, P_TEXTKEY, -1);
        (void)ui_textkey_event(&hp.tk, evt, NULL, 0);
        return;
    }
    if (hp.ch.open) {
        for (int i = 0; i < hp.ch.count; ++i) {
            if (gfx_rect_contains(hitec_panel_choice_rect(i), x, y)) {
                take(evt, P_CHOICE, i);
                return;
            }
        }
        if (gfx_rect_contains(hitec_panel_choice_cancel_rect(), x, y)) {
            take(evt, P_CHOICE_CANCEL, -1);
        }
        return;
    }
    if (gfx_rect_contains(hpn_tabs_rect(), x, y)) {
        take(evt, P_TAB, -1);
        (void)ui_tabs_event(&hp.tabs, evt);
        return;
    }
    if (gfx_rect_contains(hitec_panel_close_rect(), x, y)) {
        take(evt, P_CLOSE, -1);
        return;
    }
    for (int i = 0; i < HP_ROW_COUNT; ++i) {
        const hpn_row_t *r = &hpn_rows[i];

        if (r->page == hp.tabs.selected && hpn_row_shown(r) && hpn_row_live(r)
            && gfx_rect_contains(hpn_value_rect(r), x, y)) {
            take(evt, P_ROW, i);
            return;
        }
    }
}

void hitec_panel_close(void)
{
    const hitec_prog_state_t st = hitec_prog_state(&hp.prog);

    if (!hitec_panel_holds_port()) {
        hpn_close_editors();
        hp.open = false;
    } else if (st == HITEC_PROG_HELD && !hp.page.open) {
        hpn_page_leave();
    } else if (st == HITEC_PROG_OTHER && !hp.page.open) {
        hpn_page_release();
    }
    ++hp.rev;
}

void hitec_panel_back(void)
{
    if (hitec_panel_editor() != 0) {
        /* Over an editor the home tag is its CANCEL. */
        hitec_panel_cancel();
        hpn_close_editors();
        ++hp.rev;
        return;
    }
    hitec_panel_close();
}

/**
 * @brief The rest of the press held: a move or its release.
 * @param evt the event
 */
static void rest(const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;
    const bool up = evt->type == TOUCH_EVENT_UP;
    const int was = hp.pressed;
    const int which = hp.row;

    if (was == P_KEYPAD) {
        float v = 0.0f;
        const ui_keypad_result_t r = ui_keypad_event(&hp.kp, evt, &v);

        if (up) {
            let_go();
        }
        if (r == UI_KEYPAD_OK) {
            hpn_keypad_done(v);
        } else if (r == UI_KEYPAD_CANCEL) {
            hp.kp_what = KP_NONE;
        }
        return;
    }
    if (was == P_TEXTKEY) {
        char name[UI_TEXTKEY_MAX + 1];
        const ui_textkey_result_t r = ui_textkey_event(&hp.tk, evt, name,
                                                       sizeof(name));

        if (up) {
            let_go();
        }
        if (r == UI_TEXTKEY_OK) {
            hpn_name_done(name);
        }
        return;
    }
    if (was == P_TAB) {
        (void)ui_tabs_event(&hp.tabs, evt);
        if (up) {
            let_go();
        }
        return;
    }
    if (was == P_APPLY) {
        if (!up) {
            /* A finger that leaves the button abandons the hold. */
            if (!gfx_rect_contains(hitec_panel_apply_rect(), x, y)
                && ui_hold_leave(&hp.page.hold)) {
                hp.page.down = false;
                let_go();
            }
            return;
        }
        (void)ui_hold_end(&hp.page.hold, evt, hitec_panel_apply_rect());
        hp.page.down = false;
        let_go();
        return;
    }
    if (!up) {
        return;
    }
    let_go();
    switch (was) {
    case P_CLOSE:
        if (gfx_rect_contains(hitec_panel_close_rect(), x, y)) {
            hitec_panel_close();
        }
        break;
    case P_ROW:
        if (gfx_rect_contains(hpn_value_rect(&hpn_rows[which]), x, y)
            && hpn_row_shown(&hpn_rows[which])
            && hpn_row_live(&hpn_rows[which])) {
            hpn_act(&hpn_rows[which]);
        }
        break;
    case P_CHOICE:
        if (hp.ch.open && which < hp.ch.count
            && gfx_rect_contains(hitec_panel_choice_rect(which), x, y)) {
            hpn_choice_done(hp.ch.values[which]);
        }
        break;
    case P_CHOICE_CANCEL:
        if (gfx_rect_contains(hitec_panel_choice_cancel_rect(), x, y)) {
            hp.ch.open = false;
        }
        break;
    case P_CANCEL:
        if (gfx_rect_contains(hitec_panel_cancel_rect(), x, y)) {
            hpn_close_editors();
        }
        break;
    default:
        break;
    }
}

bool hitec_panel_has_press(uint8_t id)
{
    return hp.have && hp.id == id;
}

void hpn_supply_sync(void)
{
    const hitec_prog_t *p = &hp.prog;
    const uint32_t leaves = hitec_prog_leaves(p);

    /* A LEAVE is told before the word it ends: only its release offers
     * the operator's set points back. */
    if (leaves != hp.leaves_seen) {
        hp.leaves_seen = leaves;
        supply_screen_hitec_leave_done();
    }
    supply_screen_set_hitec_held(hitec_prog_holds_port(p));
    /* The SUPPLY page's own word that a programming port holds it
     * (LINK_SP_PORT) counts too: it comes before the HITEC page is read,
     * and it says so of a KST port as well. */
    supply_screen_set_page_held(p->fed && p->f.supply_held);
}

void hitec_panel_event(const touch_event_t *evt)
{
    if (evt == NULL || !hp.open) {
        return;
    }
    if (hp.have && evt->point.id == hp.id && evt->type != TOUCH_EVENT_DOWN) {
        rest(evt);
    } else if (evt->type == TOUCH_EVENT_DOWN && !hp.have) {
        /* One press at a time in the overlay. */
        down(evt);
    }
    hpn_supply_sync();
    ++hp.rev;
}

void hitec_panel_cancel(void)
{
    ui_keypad_cancel_press(&hp.kp);
    ui_textkey_cancel_press(&hp.tk);
    ui_tabs_cancel(&hp.tabs);
    ui_hold_reset(&hp.page.hold);
    hp.page.down = false;
    let_go();
    ++hp.rev;
}

void hitec_panel_leave(void)
{
    hitec_panel_cancel();
    hpn_close_editors();
    hp.open = false;
}

void hitec_panel_tick(float dt_s)
{
    if (hp.page.open && hp.page.down) {
        if (ui_hold_tick(&hp.page.hold, dt_s)) {
            hpn_page_apply();
            ++hp.rev;
        }
        ++hp.hold_rev;
    }
}
