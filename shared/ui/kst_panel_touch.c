/**
 * @file kst_panel_touch.c
 * @brief The presses on the programmer's overlay.
 *
 * The presses on the programmer's overlay for KST and Chaservo servos: a
 * DOWN, its moves and its release, the repeat of a held stepper, CLOSE and
 * the home tag, and each frame's hold and repeat.  The state and the seam
 * are kst_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_panel.h"

#include "kst_port.h"
#include "link_pages.h"
#include "supply_screen.h"
#include "ui_tabs.h"
#include "ui_textkey.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names (ROW0, PITCH, ROW_H) and k
 * names the state, which no header included after it may meet. */
#include "kst_panel_int.h"

/* -------------------------------------------------------------------- touch */

static void take(const touch_event_t *evt, int what, int row)
{
    k.have = true;
    k.id = evt->point.id;
    k.pressed = what;
    k.row = row;
    k.held_s = 0.0f;
}

static void let_go(void)
{
    k.have = false;
    k.pressed = P_NONE;
}

static void down(const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;

    if (k.warn.open) {
        if (gfx_rect_contains(kst_panel_apply_rect(), x, y)) {
            take(evt, P_APPLY, -1);
            k.warn.down = true;
            ui_hold_begin(&k.warn.hold);
        } else if (gfx_rect_contains(kst_panel_cancel_rect(), x, y)) {
            take(evt, P_CANCEL, -1);
        }
        return;
    }
    if (k.kp.open) {
        take(evt, P_KEYPAD, -1);
        (void)ui_keypad_event(&k.kp, evt, NULL);
        return;
    }
    if (k.tk.open) {
        take(evt, P_TEXTKEY, -1);
        (void)ui_textkey_event(&k.tk, evt, NULL, 0);
        return;
    }
    if (k.ch.open) {
        for (int i = 0; i < k.ch.count; ++i) {
            if (gfx_rect_contains(kst_panel_choice_rect(i), x, y)) {
                take(evt, P_CHOICE, i);
                return;
            }
        }
        if (gfx_rect_contains(kst_panel_choice_cancel_rect(), x, y)) {
            take(evt, P_CHOICE_CANCEL, -1);
        }
        return;
    }
    if (gfx_rect_contains(kpn_tabs_rect(), x, y)) {
        take(evt, P_TAB, -1);
        (void)ui_tabs_event(&k.tabs, evt);
        return;
    }
    if (gfx_rect_contains(kst_panel_close_rect(), x, y)) {
        take(evt, P_CLOSE, -1);
        return;
    }
    for (int i = 0; i < ROW_COUNT; ++i) {
        const row_t *r = &k_rows[i];

        if (r->page != k.tabs.selected || !kpn_row_live(r)) {
            continue;
        }
        if (r->kind == K_STEP) {
            /* The steps act on the press and repeat while it is held. */
            const bool up = gfx_rect_contains(kpn_step_rect(r, true), x, y);

            if (up || gfx_rect_contains(kpn_step_rect(r, false), x, y)) {
                take(evt, up ? P_PLUS : P_MINUS, i);
                kpn_step(r, up);
                return;
            }
        }
        if (gfx_rect_contains(kpn_value_rect(r), x, y)) {
            take(evt, P_ROW, i);
            return;
        }
    }
    if (kpn_notes_paged_at(x, y)) {
        take(evt, P_NOTES, -1);
    }
}

void kst_panel_close(void)
{
    if (!kst_panel_holds_port()) {
        kpn_close_editors();
        k.open = false;
        k.note_page = 0;
    } else if (kpn_programming() && !k.warn.open) {
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
    /* Programming and an armed bench are exclusive, and ABORT here ends
     * the wire's session, not a drive. */
    (void)x;
    (void)y;
    return false;
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

static void rest(const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;
    const bool up = evt->type == TOUCH_EVENT_UP;
    const int was = k.pressed;
    const int which = k.row;

    switch (was) {
    case P_KEYPAD: {
        float v = 0.0f;
        const ui_keypad_result_t r = ui_keypad_event(&k.kp, evt, &v);

        if (up) {
            let_go();
        }
        if (r == UI_KEYPAD_OK) {
            kpn_keypad_done(v);
        }
        return;
    }
    case P_TEXTKEY: {
        char name[UI_TEXTKEY_MAX + 1];
        const ui_textkey_result_t r = ui_textkey_event(&k.tk, evt, name,
                                                       sizeof(name));

        if (up) {
            let_go();
        }
        if (r == UI_TEXTKEY_OK) {
            kpn_name_done(name);
        }
        return;
    }
    case P_TAB: {
        const int was_tab = k.tabs.selected;

        (void)ui_tabs_event(&k.tabs, evt);
        if (k.tabs.selected != was_tab) {
            k.note_page = 0;
        }
        if (up) {
            let_go();
        }
        return;
    }
    case P_APPLY:
        if (!up) {
            /* A finger that leaves the button abandons the hold. */
            if (!gfx_rect_contains(kst_panel_apply_rect(), x, y)
                && ui_hold_leave(&k.warn.hold)) {
                k.warn.down = false;
                let_go();
            }
            return;
        }
        (void)ui_hold_end(&k.warn.hold);
        k.warn.down = false;
        let_go();
        return;
    case P_PLUS:
    case P_MINUS:
        /* A finger that leaves the key ends the repeat. */
        if (up || !gfx_rect_contains(kpn_step_rect(&k_rows[which],
                                               was == P_PLUS), x, y)) {
            let_go();
        }
        return;
    default:
        break;
    }
    if (!up) {
        return;
    }
    let_go();
    switch (was) {
    case P_CLOSE:
        if (gfx_rect_contains(kst_panel_close_rect(), x, y)) {
            kst_panel_close();
        }
        break;
    case P_ROW:
        if (gfx_rect_contains(kpn_value_rect(&k_rows[which]), x, y)
            && kpn_row_live(&k_rows[which])) {
            if (k_rows[which].kind == K_ACT) {
                kpn_act(&k_rows[which]);
            } else {
                kpn_edit_field(&k_rows[which]);
            }
        }
        break;
    case P_CHOICE:
        if (k.ch.open && which < k.ch.count
            && gfx_rect_contains(kst_panel_choice_rect(which), x, y)) {
            kpn_choice_done(k.ch.values[which]);
        }
        break;
    case P_CHOICE_CANCEL:
        if (gfx_rect_contains(kst_panel_choice_cancel_rect(), x, y)) {
            k.ch.open = false;
        }
        break;
    case P_NOTES:
        if (kpn_notes_paged_at(x, y)) {
            k.note_page = (uint8_t)((k.note_page + 1)
                                    % kst_panel_note_pages());
        }
        break;
    case P_CANCEL:
        if (gfx_rect_contains(kst_panel_cancel_rect(), x, y)) {
            kpn_close_editors();
        }
        break;
    default:
        break;
    }
}

bool kst_panel_has_press(uint8_t id)
{
    return k.have && k.id == id;
}

/* The supply's own controls follow the port: while one is out of PWM the
 * IOmcu takes an OFF and nothing else (supply_page.h), and SUPPLY and
 * the SERVO screen's SET row offer no more than that. */
void kpn_supply_sync(void)
{
    supply_screen_set_port_held(kst_prog_holds_port(&k.prog));
}

void kst_panel_event(const touch_event_t *evt)
{
    if (evt == NULL || !k.open) {
        return;
    }
    if (k.have && evt->point.id == k.id && evt->type != TOUCH_EVENT_DOWN) {
        rest(evt);
    } else if (evt->type == TOUCH_EVENT_DOWN && !k.have) {
        /* One press at a time in the overlay. */
        down(evt);
    }
    kpn_supply_sync();
    ++k.rev;
}

void kst_panel_cancel(void)
{
    ui_keypad_cancel_press(&k.kp);
    ui_textkey_cancel_press(&k.tk);
    ui_tabs_cancel(&k.tabs);
    ui_hold_reset(&k.warn.hold);
    k.warn.down = false;
    let_go();
    ++k.rev;
}

void kst_panel_leave(void)
{
    kst_panel_cancel();
    kpn_close_editors();
    k.open = false;
    k.note_page = 0;
}

void kst_panel_tick(float dt_s)
{
    if (k.warn.open && k.warn.down) {
        if (ui_hold_tick(&k.warn.hold, dt_s)) {
            kpn_warn_apply();
            ++k.rev;
        }
        ++k.hold_rev;
    }
    if (k.have && (k.pressed == P_PLUS || k.pressed == P_MINUS)) {
        const float before = k.held_s;
        const float hz = before > REPEAT_DELAY_S + REPEAT_FAST_S
                             ? REPEAT_FAST_HZ : REPEAT_SLOW_HZ;
        int fired_before, fired_now;

        k.held_s += ui_hold_credit(dt_s);
        if (k.held_s < REPEAT_DELAY_S) {
            return;
        }
        fired_before = before < REPEAT_DELAY_S
                           ? 0 : 1 + (int)((before - REPEAT_DELAY_S) * hz);
        fired_now = 1 + (int)((k.held_s - REPEAT_DELAY_S) * hz);
        for (int i = fired_before; i < fired_now && k.pressed != P_NONE; ++i) {
            kpn_step(&k_rows[k.row], k.pressed == P_PLUS);
            if (k.warn.open) {
                /* A soft limit: the page takes over and the repeat ends. */
                let_go();
            }
            ++k.rev;
        }
    }
}
