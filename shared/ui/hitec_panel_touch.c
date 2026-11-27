/**
 * @file hitec_panel_touch.c
 * @brief The presses on the Hitec programmer's overlay: the switch on what
 *        the servo frame returns for a touch and for a frame, the home
 *        tag, and the supply's lock while a port is held.
 *
 * The frame (sprog.h) takes the presses, the CONNECT and WRITE n VALUES
 * holds, the -/+ keys' repeat, the stops on the press, the keypad, the
 * keyboard, the list and the hold page; this file says what each result
 * does.  Hitec is a name or trademark of its owner; it is used here only to
 * say which servos this overlay works with.  The state and the seam are
 * hitec_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel.h"

#include "hitec_live.h"
#include "hitec_move.h"
#include "supply_screen.h"
#include "ui_widgets.h"

/* Last: its layout macros and hp, the state, which no header included
 * after it may meet. */
#include "hitec_panel_int.h"

bool hitec_panel_safe_at(int x, int y)
{
    if (!hp.open) {
        return false;
    }
    hpn_view();
    return sprog_safe_at(&hp.f, x, y);
}

/**
 * @brief A stop on the press: MOVE SERVO's loop ends, the FIND STOP key's
 *        search ends, ABORT ends the operation under way.
 * @param r   the row
 * @param key the key of FIND STOP's pair: 1 for RIGHT END
 */
static void stop(const hpn_row_t *r, int key)
{
    switch (r->what) {
    case A_MOVE:
        hitec_prog_move_stop(&hp.prog);
        break;
    case A_FIND:
        hpn_find_act(key != 0);
        break;
    default:
        hitec_prog_abort(&hp.prog);
        break;
    }
    hpn_moved();
}

/**
 * @brief A tap on a row: FIND STOP's key, or the row's action.
 * @param r   the row
 * @param key the key of FIND STOP's pair: 1 for RIGHT END
 */
static void tap(const hpn_row_t *r, int key)
{
    if (r->kind == K_ACT && r->what == A_FIND) {
        hpn_find_act(key != 0);
        hpn_moved();
        return;
    }
    hpn_act(r);
}

/**
 * @brief A press of a -/+ key, or its repeat: @p n steps, until one is
 *        refused or MOVE SERVO goes off, which ends the press.
 * @param r    the row
 * @param plus true for +
 * @param n    the steps owed
 */
static void step(const hpn_row_t *r, bool plus, int n)
{
    for (int i = 0; i < n; ++i) {
        if (!hpn_step(r, plus)) {
            sprog_refuse(&hp.f);
            return;
        }
    }
}

/**
 * @brief A row hold that completed: CONNECT connects while the port is
 *        still in PWM and switches a supply, WRITE n VALUES writes while
 *        it still takes a write.
 * @param r the row
 */
static void hold_due(const hpn_row_t *r)
{
    if (r->what == A_PORT) {
        if (hitec_prog_state(&hp.prog) == HITEC_PROG_PWM
            && hpn_switches_supply() && hpn_row_shown(r)
            && hpn_row_live(r)) {
            hpn_act(r);         /* refuses a low carrier input itself */
            hpn_supply_sync();
        }
        return;
    }
    if (hpn_row_live(r)) {
        (void)hpn_write_go((uint8_t)HITEC_PROG_WR_EDITS);
        hpn_moved();
    }
}

/**
 * @brief The keypad's value, x 10^decimals, back to its unit.
 * @param value the frame's value
 * @return the value
 */
static float keypad_value(int32_t value)
{
    float v = (float)value;

    for (int i = 0; i < hp.f.kp_dec; ++i) {
        v /= 10.0f;
    }
    return v;
}

/**
 * @brief What the frame returned.
 * @param r the result
 */
static void act(sprog_result_t r)
{
    const hpn_row_t *row = r.row >= 0 && r.row < HP_ROW_COUNT
                               ? &hpn_rows[r.row] : &hpn_rows[0];

    switch (r.kind) {
    case SPROG_R_TAP:
        tap(row, r.key);
        break;
    case SPROG_R_STEP:
        step(row, r.key > 0, (int)r.value);
        break;
    case SPROG_R_STOP:
        stop(row, r.key);
        break;
    case SPROG_R_HOLD_BEGIN:
        /* WRITE n VALUES that the session refuses says why and starts no
         * hold; CONNECT's refusal is the cell's why. */
        if (row->what == A_WRITE && hpn_write_refused()) {
            sprog_refuse(&hp.f);
            hpn_moved();
        }
        break;
    case SPROG_R_HOLD_DUE:
        hold_due(row);
        break;
    case SPROG_R_PORT:
        hpn_port_list();
        break;
    case SPROG_R_KEYPAD:
        hpn_keypad_done(keypad_value(r.value));
        break;
    case SPROG_R_KEYPAD_CANCEL:
        hp.kp_what = KP_NONE;
        break;
    case SPROG_R_LIST:
        hpn_choice_done((int)r.value);
        break;
    case SPROG_R_NAME:
        hpn_name_done(hp.f.name);
        break;
    case SPROG_R_PAGE_APPLY:
        hpn_page_apply(r.what, hp.f.page.drop);
        break;
    case SPROG_R_PAGE_CANCEL:
        hpn_page_dropped(r.what);
        break;
    default:
        break;
    }
}

void hitec_panel_close(void)
{
    const hitec_prog_state_t st = hitec_prog_state(&hp.prog);

    /* MOVE SERVO ends first; a tap after its end leaves. */
    hitec_prog_move_stop(&hp.prog);
    if (!hitec_panel_holds_port()) {
        hpn_close_editors();
        hp.open = false;
        hp.f.open = false;
    } else if (st == HITEC_PROG_HELD && !hp.f.page.open) {
        hpn_page_leave();
    } else if (st == HITEC_PROG_OTHER && !hp.f.page.open) {
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

bool hitec_panel_has_press(uint8_t id)
{
    return sprog_has_press(&hp.f, id);
}

bool hitec_panel_pressed(void)
{
    return hp.f.have;
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
    hpn_view();
    act(sprog_event(&hp.f, evt));
    hpn_move_follow();
    hpn_supply_sync();
    ++hp.rev;
}

void hitec_panel_cancel(void)
{
    sprog_cancel(&hp.f);
    ++hp.rev;
}

void hitec_panel_leave(void)
{
    hitec_prog_move_stop(&hp.prog);
    hitec_panel_cancel();
    hpn_close_editors();
    hp.open = false;
    hp.f.open = false;
}

void hitec_panel_tick(float dt_s)
{
    hpn_view();
    act(sprog_tick(&hp.f, dt_s));
    hpn_gate_follow();
}

bool hitec_panel_write_refused(void)
{
    const bool no = hpn_write_refused();

    hpn_moved();
    return no;
}

bool hitec_panel_write_go(void)
{
    const hpn_row_t *w = hpn_row_at(HITEC_PANEL_SERVO, 3, 1);

    if (!hp.open || hp.f.have || hitec_panel_editor() != 0 || w == NULL
        || !hpn_row_live(w)) {
        return false;
    }
    /* Sent on the next tick, as the tab's hold is: a touch loss drained
     * before it cancels the write. */
    hpn_view();
    if (!sprog_due(&hp.f, (int)(w - hpn_rows))) {
        return false;
    }
    ++hp.rev;
    return true;
}
