/**
 * @file kst_panel_touch.c
 * @brief The presses on the programmer's overlay.
 *
 * The presses on the programmer's overlay for KST and Chaservo servos.
 * The frame (sprog.h) takes every touch and every frame's time and says
 * what happened; this file fills the frame's view first and acts on the
 * result: a tap's action or editor, a step, ABORT, CONNECT's and WRITE n
 * VALUES' completed holds, a value typed or chosen, a page's hold.  And
 * the home tag and the page-set hooks.  The state and the seam are
 * kst_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_panel.h"

#include "kst_port.h"
#include "link_pages.h"
#include "supply_screen.h"

/* Last: k names the state, which no header included after it may meet. */
#include "kst_panel_int.h"

/* -------------------------------------------------------------------- touch */

void kst_panel_close(void)
{
    if (!kst_panel_holds_port()) {
        kpn_close_editors();
        k.f.open = false;
        k.f.note_page = 0;
    } else if (kpn_programming() && !k.f.page.open) {
        kpn_close_editors();
        kpn_warn_leave();
    }
    ++k.rev;
}

void kst_panel_back(void)
{
    if (kst_panel_editor() != 0) {
        /* Over an editor, the home tag is its CANCEL: the editor closes
         * and the page under it stays. */
        kst_panel_cancel();
        kpn_close_editors();
        ++k.rev;
        return;
    }
    kst_panel_close();
}

bool kst_panel_safe_at(int x, int y)
{
    kpn_view();
    return sprog_safe_at(&k.f, x, y);
}

uint8_t kst_panel_pin(void)
{
    const uint8_t port = kpn_port_no();

    if (port >= LINK_PG_PORTS_MAX
        || (k.prog.f.port_pin_known & (1u << port)) == 0u
        || k.prog.f.port_pin[port] > KST_PROG_PIN_LAST) {
        return PROG_REG_NO_PIN;
    }
    return k.prog.f.port_pin[port];
}

bool kst_panel_has_press(uint8_t id)
{
    return sprog_has_press(&k.f, id);
}

bool kst_panel_pressed(void)
{
    return k.f.have;
}

/* The supply's own controls follow the port: while one is out of PWM the
 * IOmcu takes an OFF and nothing else (supply_page.h), and SUPPLY and
 * the SERVO screen's SET row offer no more than that.  A LEAVE is told
 * before the word it ends: only its release offers the operator's set
 * points back. */
void kpn_supply_sync(void)
{
    const uint32_t leaves = kst_prog_leaves(&k.prog);

    if (leaves != k.leaves_seen) {
        k.leaves_seen = leaves;
        supply_screen_kst_leave_done();
    }
    supply_screen_set_port_held(kst_prog_holds_port(&k.prog));
}

/**
 * @brief A completed 2s hold of row @p row, due a tick after it completed
 *        so a touch loss drained in between has cancelled it: CONNECT
 *        connects where the port is still in PWM and the session
 *        switches its supply, WRITE n VALUES writes where it still takes
 *        a press.
 * @param row the row
 */
static void hold_due(int row)
{
    const row_t *r = row >= 0 && row < ROW_COUNT ? &kpn_rows[row] : NULL;

    if (r == NULL || r->kind != K_ACT || !kpn_row_live(r)) {
        return;
    }
    if (r->what == A_PORT && kst_prog_state(&k.prog) == KST_PROG_PWM
        && kpn_switches_supply()) {
        kpn_act(r);                 /* refuses a low carrier input itself */
        kpn_supply_sync();
    } else if (r->what == A_WRITE) {
        (void)kpn_write_go();
    }
}

/**
 * @brief @p n steps of row @p row, while no page takes over: a soft limit
 *        opens one, and the repeat ends.
 * @param row the row
 * @param up  true for plus
 * @param n   the steps owed
 */
static void steps(int row, bool up, int n)
{
    for (int i = 0; i < n && !k.f.page.open; ++i) {
        kpn_step(&kpn_rows[row], up);
    }
}

/**
 * @brief Acts on what the frame says happened.
 * @param r the result
 */
static void act_on(sprog_result_t r)
{
    switch (r.kind) {
    case SPROG_R_TAP:
        if (kpn_rows[r.row].kind == K_ACT) {
            kpn_act(&kpn_rows[r.row]);
        } else {
            kpn_edit_field(&kpn_rows[r.row]);
        }
        break;
    case SPROG_R_STEP:
        steps(r.row, r.key > 0, (int)r.value);
        break;
    case SPROG_R_STOP:
        kst_prog_abort(&k.prog);
        break;
    case SPROG_R_HOLD_BEGIN:
        /* WRITE n VALUES: a write the session refuses says why at once,
         * and no hold starts. */
        if (kpn_rows[r.row].what == A_WRITE && kpn_write_refused()) {
            sprog_refuse(&k.f);
        }
        break;
    case SPROG_R_HOLD_DUE:
        hold_due(r.row);
        break;
    case SPROG_R_PORT:
        kpn_port_list();
        break;
    case SPROG_R_KEYPAD:
        kpn_keypad_done(r.value);
        break;
    case SPROG_R_LIST:
        kpn_choice_done(r.what, (int)r.value);
        break;
    case SPROG_R_LIST_CANCEL:
        k.type_file[0] = '\0';      /* a load that waited for the type */
        break;
    case SPROG_R_NAME:
        kpn_name_done(k.f.name);
        break;
    case SPROG_R_PAGE_APPLY:
        kpn_warn_apply(r.what);
        break;
    case SPROG_R_PAGE_CANCEL:
        if (r.what == W_SOFT) {
            kst_prog_soft_cancel(&k.prog);
        }
        kpn_close_editors();
        break;
    default:
        break;
    }
}

void kst_panel_event(const touch_event_t *evt)
{
    kpn_mode_sync();
    if (evt == NULL || !k.f.open) {
        return;
    }
    kpn_view();
    act_on(sprog_event(&k.f, evt));
    kpn_supply_sync();
    ++k.rev;
}

void kst_panel_cancel(void)
{
    sprog_cancel(&k.f);
    ++k.rev;
}

void kst_panel_leave(void)
{
    kst_panel_cancel();
    kpn_close_editors();
    k.f.open = false;
    k.f.note_page = 0;
}

void kst_panel_tick(float dt_s)
{
    sprog_result_t r;

    /* SERVO > SETTINGS may have turned KST EXPERT MODE on or off. */
    kpn_mode_sync();
    kpn_view();
    r = sprog_tick(&k.f, dt_s);
    act_on(r);
    if (r.kind == SPROG_R_HOLD_DUE || r.kind == SPROG_R_PAGE_APPLY
        || r.kind == SPROG_R_STEP) {
        ++k.rev;
    }
    kpn_gate_follow();
}

bool kst_panel_write_refused(void)
{
    return kpn_write_refused();
}

bool kst_panel_write_go(void)
{
    const row_t *w = kpn_row_at(PG_SERVO, 3, 1);

    if (w == NULL || !kpn_row_live(w)) {
        return false;
    }
    /* Due on the next tick, as the tab's hold is: a touch loss drained
     * before it cancels the write. */
    if (!sprog_due(&k.f, (int)(w - kpn_rows))) {
        return false;
    }
    ++k.rev;
    return true;
}
