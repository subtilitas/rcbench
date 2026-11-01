/**
 * @file supply_events.c
 * @brief The SUPPLY screen's events: a touch on the strip, the set points, the
 *        rail and OUTPUT, SETTINGS' page, the keypad and the question, and the
 *        frame's tick.  supply_screen.c holds the state, the commands and the
 *        screen's interface (supply_screen.h); supply_screen_int.h is what it
 *        shares with this file and supply_draw.c.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>

#include "esc_wire_panel.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_hero.h"
#include "ui_keypad.h"
#include "ui_screen.h"
#include "ui_slider.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_widgets.h"
#include "ui_workbench.h"

#include "supply_screen_int.h"

/* ------------------------------------------------------------------ keypad */

/** @brief Opens the keypad for a set point or a setting. */
static void open_keypad(int target, setting_id_t id)
{
    if (target == KP_SET_V) {
        ui_keypad_open(&s.kp, sup_overlay_area(), TR(SUP_VOLTAGE), "V", s.cv,
                       s.eff.v_min, s.eff.v_max, 2);
    } else if (target == KP_SET_I) {
        ui_keypad_open(&s.kp, sup_overlay_area(), TR(SUP_CURRENT_LIMIT), "A",
                       s.ci,
                       s.eff.i_min, s.eff.i_max, 2);
    } else {
        const setting_def_t *d = settings_def(id);
        float lo = d->min;
        float hi = d->max;
        /* A cap reaches what the supply can do; a start value what the caps
         * allow.  A trip goes down to 0, which is off. */
        if (id == SET_SUPPLY_V_MAX) {
            lo = s.caps.v_min;
            hi = s.caps.v_max;
        } else if (id == SET_SUPPLY_I_MAX) {
            lo = s.caps.i_min;
            hi = s.caps.i_max;
        } else if (id == SET_SUPPLY_V_START) {
            lo = s.eff.v_min;
            hi = s.eff.v_max;
        } else if (id == SET_SUPPLY_I_START) {
            lo = s.eff.i_min;
            hi = s.eff.i_max;
        }
        const char *label = ui_setting_label(id);
        int decimals = 2;
        for (int i = 0; i < ROW_COUNT; ++i) {
            if (k_rows[i].id == id) {
                label    = ui_tr(k_rows[i].label);
                decimals = k_rows[i].decimals;
            }
        }
        ui_keypad_open(&s.kp, sup_overlay_area(), label, d->unit, settings_get(id),
                       lo, hi, decimals);
    }
    s.kp_target  = target;
    s.kp_setting = id;
    supply_invalidate();
}

/** @brief Takes the keypad's result: applies the value or closes it. */
static void keypad_done(ui_keypad_result_t r, float v)
{
    if (r == UI_KEYPAD_NONE) {
        if (s.kp.open) {
            return;
        }
    } else if (r == UI_KEYPAD_OK) {
        if (s.kp_target == KP_SET_V) {
            ui_slider_set(&s.v_slider, supply_snap(v, s.eff.v_min, s.eff.v_max,
                                                   s.eff.v_step));
            s.kp_target = KP_NONE;
            sup_propose(FROM_KEYPAD);
        } else if (s.kp_target == KP_SET_I) {
            ui_slider_set(&s.i_slider, supply_snap(v, s.eff.i_min, s.eff.i_max,
                                                   s.eff.i_step));
            s.kp_target = KP_NONE;
            sup_propose(FROM_KEYPAD);
        } else if (s.kp_target == KP_PD) {
            sup_pd_keypad_ok(v);
            if (s.kp.open) {
                return;                     /* refused: the keypad says why */
            }
        } else if (s.kp_target == KP_SETTING) {
            /* Onto the setting's own step the safe way: a cap rounds down,
             * so a typed 12.01V allows no more than 12.00V; a trip typed
             * above 0 stays a trip, never rounded to off. */
            const setting_def_t *d = settings_def(s.kp_setting);
            if (d->step > 0.0f && (s.kp_setting == SET_SUPPLY_V_MAX
                                   || s.kp_setting == SET_SUPPLY_I_MAX)) {
                v = d->min + floorf((v - d->min) / d->step + 1e-3f) * d->step;
            } else if (d->step > 0.0f && v > 0.0f && v < d->step
                       && (s.kp_setting == SET_SUPPLY_TRIP_I
                           || s.kp_setting == SET_SUPPLY_TRIP_V)) {
                v = d->step;
            }
            /* Kept as soon as the bench allows a flash write; see
             * settings_request_save().  An edit on SETUP that was not saved
             * is written with it. */
            settings_set(s.kp_setting, v);
            sup_refresh_limits();
            settings_request_save();
        }
    }
    s.kp_target = KP_NONE;
    supply_invalidate();
}

/* ------------------------------------------------------------------ events */

static void task_changed(void);

/**
 * @brief A set point moved by @p dv volts and @p di amps, snapped to the caps.
 */
static void nudge(float dv, float di)
{
    ui_slider_set(&s.v_slider, supply_snap(s.v_slider.value + dv, s.eff.v_min,
                                           s.eff.v_max, s.eff.v_step));
    ui_slider_set(&s.i_slider, supply_snap(s.i_slider.value + di, s.eff.i_min,
                                           s.eff.i_max, s.eff.i_step));
    ++s.set_rev;
    sup_propose(FROM_SLIDER);
}

void sup_take(const touch_event_t *evt, int code)
{
    s.have_press = true;
    s.press_id   = evt->point.id;
    s.pressed    = code;
    ++s.ctrl_rev;
    ++s.ov_rev;
}

/**
 * @brief A press on a control a programming port's hold locks: the alert
 *        band says why it takes none.
 */
static void held_refused(void)
{
    ui_router_set_alert(esc_wire_panel_holds_port() ? TR(SUP_HELD_REFUSED_ESC)
                                                     : TR(SUP_HELD_REFUSED));
}

/**
 * @brief Where a press takes the SIMULATED line: its button, from the top of
 *        the body to the page, so the 40px button is a finger tall.
 * @return the area, px
 */
static gfx_rect_t model_line_hit(void)
{
    const gfx_rect_t r = sup_model_line_rect();
    return (gfx_rect_t){ r.x, 0, r.w, OV_Y };
}

/**
 * @brief A contact coming down: the control under it takes the press,
 *        the output and the peaks first, whatever covers the left column.
 * @param evt the DOWN event
 */
static void down(const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;

    /* The output and the peaks work whatever covers the left column. */
    if (gfx_rect_contains(s.out_rect, x, y)) {
        /* Off is always taken; on is not while a port holds the supply. */
        if (!s.on && s.port_held) {
            held_refused();
            return;
        }
        sup_take(evt, P_OUTPUT);
        s.press_on = s.on;
        if (!s.on) {
            ui_hold_begin(&s.hold, TX_HOLD_HINT_OUTPUT_ON);
        }
        ++s.out_rev;
        return;
    }
    if (gfx_rect_contains(s.reset_rect, x, y)) {
        sup_take(evt, P_RESET);
        return;
    }
    /* The PROG 5V box, on LIVE with nothing over it; locked while a port
     * holds the ports' 5V or it is not read, and the band says why. */
    if (sup_p5_shown()
        && gfx_rect_contains(prog5v_box_button(&s.p5), x, y)) {
        if (prog5v_box_press(&s.p5)) {
            sup_take(evt, P_PROG5V);
        } else {
            ui_router_set_alert(TR(P5_LOCKED));
        }
        return;
    }
    /* A question being asked is answered before anything else is opened. */
    if (s.confirm_open) {
        if (gfx_rect_contains(s.apply_btn, x, y)) {
            sup_take(evt, P_APPLY);
        } else if (gfx_rect_contains(s.discard_btn, x, y)) {
            sup_take(evt, P_DISCARD);
        }
        return;
    }
    /* The strip, over the keypad too: a tap on the other task takes the
     * keypad with it (task_changed()). */
    if (gfx_rect_contains(sup_strip_area(), x, y)) {
        sup_take(evt, P_STRIP);
        (void)ui_wb_strip_event(&s.wb, evt);
        ++s.strip_rev;
        return;
    }
    /* The SIMULATED line, over the keypad too, as the strip: PD MINI. */
    if (s.model && gfx_rect_contains(model_line_hit(), x, y)) {
        sup_take(evt, P_MODEL_LINE);
        return;
    }
    if (gfx_rect_contains(sup_card_rect(0), x, y)) {
        if (!s.port_held) {
            sup_take(evt, P_CARD_V);
        } else {
            held_refused();
        }
        return;
    }
    if (gfx_rect_contains(sup_card_rect(1), x, y)) {
        if (!s.port_held) {
            sup_take(evt, P_CARD_I);
        } else {
            held_refused();
        }
        return;
    }

    if (s.kp.open) {
        if (gfx_rect_contains(sup_overlay_area(), x, y)) {
            sup_take(evt, P_KEYPAD);
            (void)ui_keypad_event(&s.kp, evt, NULL);
        }
        return;
    }
    if (!sup_on_live()) {
        if (gfx_rect_contains(s.ov_tabs.rect[0], x, y)
            || gfx_rect_contains(s.ov_tabs.rect[1], x, y)) {
            sup_take(evt, P_OV_TABS);
            (void)ui_tabs_event(&s.ov_tabs, evt);
            return;
        }
        if (sup_on_pdmini()) {
            sup_pd_down(evt, x, y);
            return;
        }
        if (!s.model && gfx_rect_contains(s.modreset_btn, x, y)) {
            sup_take(evt, P_MODRESET);
            return;
        }
        for (int i = 0; i < ROW_COUNT; ++i) {
            if (gfx_rect_contains(sup_row_rect(i), x, y)) {
                sup_take(evt, P_ROW);
                s.press_row = i;
                return;
            }
        }
        return;
    }

    /* LIVE, nothing over the left column: its own controls. */
    if (gfx_rect_contains((gfx_rect_t){ TAB_X, TAB_Y, TAB_W, TAB_H }, x, y)) {
        sup_take(evt, P_TABS);
        (void)ui_tabs_event(&s.tabs, evt);
        return;
    }
    if (s.port_held) {
        /* No set point moves, and the band says why. */
        if (gfx_rect_contains(s.v_slider.track, x, y)
            || gfx_rect_contains(s.i_slider.track, x, y)
            || gfx_rect_contains(s.v_down, x, y)
            || gfx_rect_contains(s.v_up, x, y)
            || gfx_rect_contains(s.i_down, x, y)
            || gfx_rect_contains(s.i_up, x, y)
            || gfx_rect_contains(sup_set_value_rect(0), x, y)
            || gfx_rect_contains(sup_set_value_rect(1), x, y)) {
            held_refused();
        }
        return;
    }
    if (gfx_rect_contains(s.v_slider.track, x, y)
        || gfx_rect_contains(s.i_slider.track, x, y)) {
        const bool v = gfx_rect_contains(s.v_slider.track, x, y);
        sup_take(evt, v ? P_V_SLIDER : P_I_SLIDER);
        if (ui_slider_event(v ? &s.v_slider : &s.i_slider, evt)) {
            ++s.set_rev;
            /* Live as it moves, unless the change needs a question: then
             * the release asks it. */
            if (!sup_confirm_needed(FROM_SLIDER)) {
                sup_propose(FROM_SLIDER);
            }
        }
        return;
    }
    const struct { gfx_rect_t r; int code; float dv, di; } k_steps[] = {
        { s.v_down, P_V_DOWN, -NUDGE_V, 0.0f },
        { s.v_up,   P_V_UP,    NUDGE_V, 0.0f },
        { s.i_down, P_I_DOWN,  0.0f, -NUDGE_I },
        { s.i_up,   P_I_UP,    0.0f,  NUDGE_I },
    };
    for (size_t k = 0; k < sizeof(k_steps) / sizeof(k_steps[0]); ++k) {
        if (gfx_rect_contains(k_steps[k].r, x, y)) {
            /* On the press: a fine step should feel immediate. */
            sup_take(evt, k_steps[k].code);
            nudge(k_steps[k].dv, k_steps[k].di);
            return;
        }
    }
    if (gfx_rect_contains(sup_set_value_rect(0), x, y)) {
        sup_take(evt, P_VALUE_V);
    } else if (gfx_rect_contains(sup_set_value_rect(1), x, y)) {
        sup_take(evt, P_VALUE_I);
    }
}

/** @brief The release of the press, over the control it began on. */
static void released(int was, int row, int x, int y)
{
    switch (was) {
    case P_RESET:
        if (gfx_rect_contains(s.reset_rect, x, y)) {
            s.pending.reset = true;
        }
        break;
    case P_CARD_V:
        if (gfx_rect_contains(sup_card_rect(0), x, y)) {
            open_keypad(KP_SET_V, SETTING_COUNT);
        }
        break;
    case P_CARD_I:
        if (gfx_rect_contains(sup_card_rect(1), x, y)) {
            open_keypad(KP_SET_I, SETTING_COUNT);
        }
        break;
    case P_VALUE_V:
        if (gfx_rect_contains(sup_set_value_rect(0), x, y)) {
            open_keypad(KP_SET_V, SETTING_COUNT);
        }
        break;
    case P_VALUE_I:
        if (gfx_rect_contains(sup_set_value_rect(1), x, y)) {
            open_keypad(KP_SET_I, SETTING_COUNT);
        }
        break;
    case P_MODRESET:
        /* The module restarted: its output goes off first. */
        if (gfx_rect_contains(s.modreset_btn, x, y)) {
            s.pending.module_reset = true;
            s.pending.off          = true;
        }
        break;
    case P_ROW:
        if (row >= 0 && row < ROW_COUNT && gfx_rect_contains(sup_row_rect(row), x, y)) {
            const setting_id_t id = k_rows[row].id;
            if (settings_def(id)->type == SET_TYPE_BOOL) {
                /* A switch flips on the tap; it has nothing to type. */
                settings_set(id, settings_get_bool(id) ? 0.0f : 1.0f);
                sup_refresh_limits();
                settings_request_save();
            } else {
                open_keypad(KP_SETTING, id);
            }
        }
        break;
    case P_PD_VALUE:
    case P_PD_MINUS:
    case P_PD_PLUS:
        sup_pd_released(was, row, x, y);
        break;
    case P_MODEL_LINE:
        if (gfx_rect_contains(model_line_hit(), x, y)) {
            task_changed();
            supply_screen_open_pdmini();
        }
        break;
    case P_APPLY:
        /* Only while the question still stands: a release drained in the
         * frame that dropped the ON it was about, or of a stop, must not
         * apply it. */
        if (gfx_rect_contains(s.apply_btn, x, y) && s.confirm_open
            && sup_confirm_stands()) {
            sup_confirm_close(true);
        }
        break;
    case P_DISCARD:
        if (gfx_rect_contains(s.discard_btn, x, y)) {
            sup_confirm_close(false);
        }
        break;
    default:
        break;
    }
}

/*
 * The press this screen holds ends without its release.  Every gesture is
 * dropped, which asks for nothing, except a tap on OUTPUT OFF: switching off
 * is a press, so a release that never came is an OFF the operator made, and
 * it is sent.  An ON this screen has posted and the application has not
 * collected is dropped: the hold that posted it may have completed on a
 * contact that had gone.  An ON already collected stays asked, with its
 * owner, until the application reports what became of it; a leave switches
 * off an ON reported on.  A key held on the keypad is let go of; what was
 * typed stays.
 */
void sup_drop_press(void)
{
    if (s.on && s.pressed == P_OUTPUT && s.press_on) {
        sup_post_off();
    }
    if (s.pending.on) {
        s.pending.on = false;
        s.on_asked   = false;
    }
    sup_owner_settle();
    ui_slider_release(&s.v_slider);
    ui_slider_release(&s.i_slider);
    /* A drag whose release went missing changes nothing: the slider goes
     * back to the set point the supply has. */
    if (s.pressed == P_V_SLIDER || s.pressed == P_I_SLIDER) {
        ui_slider_set(&s.v_slider, s.cv);
        ui_slider_set(&s.i_slider, s.ci);
        ++s.set_rev;
    }
    ui_hold_reset(&s.hold);
    prog5v_box_cancel(&s.p5);
    ui_tabs_cancel(&s.tabs);
    ui_tabs_cancel(&s.ov_tabs);
    ui_wb_strip_cancel(&s.wb);
    ++s.strip_rev;
    ui_keypad_cancel_press(&s.kp);
    sup_let_go();
    ++s.ctrl_rev;
    ++s.out_rev;
}

/**
 * @brief The strip switched the task.  A keypad open goes with the task it was
 *        opened on, typed or not, as its CANCEL does; PLOT | TABLE and the set
 *        points keep what they were.  A tab switch is no navigation level.
 */
static void task_changed(void)
{
    ui_keypad_close(&s.kp);
    s.kp_target = KP_NONE;
    ui_tabs_cancel(&s.tabs);
    ++s.set_rev;
    ++s.ov_rev;
    supply_invalidate();
}

/* OUTPUT OFF, the one control here whose press stops what the bench
 * drives: the router hands a press on it through the alert band and the
 * band's help bubble (ui_screen_t's safe_at).  OUTPUT ON is none. */
bool sup_safe_at(int x, int y)
{
    return s.on && gfx_rect_contains(s.out_rect, x, y);
}

void sup_event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    /*
     * One contact holds the screen at a time, and that is decided before
     * any widget sees the event: a second finger on a track while the first
     * holds OUTPUT OFF would otherwise move the set point of a live output,
     * and on a button it would take over the record of the first press and
     * lose its release.
     */
    if (evt->type == TOUCH_EVENT_DOWN) {
        /* A contact comes down once between two releases, so a DOWN with
         * the id of the press held says that contact has gone and its
         * release with it.  Kept, the press would refuse every later touch
         * and take this contact's release for its own. */
        if (s.have_press && evt->point.id == s.press_id) {
            sup_drop_press();
        }
        if (!s.have_press) {
            down(evt);
        }
        return;
    }
    if (!s.have_press || evt->point.id != s.press_id) {
        return;
    }
    const int x = evt->point.x, y = evt->point.y;
    const bool up = (evt->type == TOUCH_EVENT_UP);
    switch (s.pressed) {
    case P_KEYPAD: {
        float v = 0.0f;
        const ui_keypad_result_t r = ui_keypad_event(&s.kp, evt, &v);
        if (up) {
            sup_let_go();
        }
        keypad_done(r, v);
        return;
    }
    case P_V_SLIDER:
    case P_I_SLIDER: {
        ui_slider_t *sl = (s.pressed == P_V_SLIDER) ? &s.v_slider : &s.i_slider;
        if (ui_slider_event(sl, evt)) {
            ++s.set_rev;
            if (!sup_confirm_needed(FROM_SLIDER)) {
                sup_propose(FROM_SLIDER);
            }
        }
        if (up) {
            sup_let_go();
            /* A release the finger did not make -- the router's, at the
             * home tag or a change of screen, or the tracker's for a jump
             * -- asks no question: the drag ends as sup_drop_press() ends one,
             * with what it already gave the supply live and nothing more. */
            if (touch_event_is_tap_up(evt)) {
                sup_propose(FROM_SLIDER);
            }
            /* The drag is over, so the slider can be written: it shows the
             * set point it was snapped to, or, while a question is open,
             * the one the supply keeps until it is answered. */
            ui_slider_set(&s.v_slider, s.cv);
            ui_slider_set(&s.i_slider, s.ci);
            ++s.set_rev;
        }
        return;
    }
    case P_PROG5V:
        if (!up) {
            if (prog5v_box_drag(&s.p5, x, y)) {
                sup_let_go();
            }
            return;
        }
        prog5v_box_release(&s.p5, evt);
        sup_let_go();
        return;
    case P_STRIP:
        if (up) {
            sup_let_go();
            if (ui_wb_strip_event(&s.wb, evt)) {
                task_changed();
            }
            ++s.strip_rev;
        }
        return;
    case P_TABS:
        if (ui_tabs_event(&s.tabs, evt)) {
            supply_invalidate();
        }
        if (up) {
            sup_let_go();
        }
        return;
    case P_OV_TABS:
        if (ui_tabs_event(&s.ov_tabs, evt)) {
            ++s.ov_rev;
        }
        if (up) {
            sup_let_go();
        }
        return;
    case P_OUTPUT:
        if (!up) {
            /* A finger that leaves the switch abandons the hold. */
            if (!s.press_on && !gfx_rect_contains(s.out_rect, x, y)
                && ui_hold_leave(&s.hold)) {
                sup_let_go();
                ++s.out_rev;
            }
            return;
        }
        {
            const bool fired = ui_hold_end(&s.hold, evt, s.out_rect);
            ++s.out_rev;
            /* Off is a tap; on is a hold that has already sent its command
             * by the time the finger lifts.  Which one is the press's: an
             * earlier ON reported during a second hold does not make its
             * release an OFF. */
            if (s.press_on && s.on && !fired
                && gfx_rect_contains(s.out_rect, x, y)) {
                sup_post_off();
            }
            sup_let_go();
        }
        return;
    default:
        if (up) {
            const int was = s.pressed;
            const int row = s.press_row;
            sup_let_go();
            /* A release the finger did not make ends the press and
             * activates nothing. */
            if (touch_event_is_tap_up(evt)) {
                released(was, row, x, y);
            }
        }
        return;
    }
}

void sup_tick(float dt_s)
{
    sup_pd_tick();
    /* A question about a live output goes once there is none: off, and no
     * ON on its way -- a STOP or a lost touch can drop one that was -- or
     * once a stop has come since it was asked. */
    if (s.confirm_open && !sup_confirm_stands()) {
        s.confirm_open = false;
        if (s.pressed == P_APPLY || s.pressed == P_DISCARD) {
            sup_let_go();                   /* its buttons have gone with it */
        }
        ++s.set_rev;
        supply_invalidate();
    }
    if (s.pressed == P_OUTPUT && !s.press_on && !s.on && !s.port_held) {
        ++s.out_rev;
        if (ui_hold_tick(&s.hold, dt_s)) {
            sup_post_on(SUPPLY_BY_SUPPLY);
        }
    }
    if (s.hold.flash_ms > 0) {
        ui_hold_flash_step(&s.hold, dt_s);
        ++s.out_rev;
    }
    /* The PROG 5V box: its hold; a press it dropped (a port took the
     * supply) is over here too. */
    prog5v_box_tick(&s.p5, dt_s);
    if (s.pressed == P_PROG5V && !s.p5.pressed) {
        sup_let_go();
    }
    /* The box coming or going (a board change, the link) repaints the
     * card it sits in. */
    if (sup_p5_shown() != s.p5_shown) {
        s.p5_shown = sup_p5_shown();
        supply_invalidate();
    }
}
