/**
 * @file motor_events.c
 * @brief The MOTOR & ESC screen's events: a touch on the strip, the rail,
 *        LIVE's page and the pages of the other tasks, and the frame's tick.
 *        motor_screen.c holds the state, the commands and the screen's
 *        interface (motor_screen.h); motor_live_int.h is what it shares with
 *        this file and motor_draw.c.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <string.h>

#include "motor_screen.h"
#include "settings.h"
#include "ui_slider.h"
#include "ui_tabs.h"
#include "ui_value_gate.h"
#include "kst_panel.h"
#include "outputs_screen.h"
#include "port_view.h"
#include "ui_widgets.h"

#include "motor_live_int.h"

/* ------------------------------------------------------------------ events */

/*
 * Another task is shown.  A press on the page that went goes with it: a
 * drag on the throttle, a step button, PLOT or TABLE half pressed, a value
 * on SETTINGS.  ARM is on the rail, which stays, and keeps its press.  The
 * page is repainted whole.
 */
static void task_changed(void)
{
    mo_end_drag();
    ui_tabs_cancel(&s.tabs);
    msp_cancel();
    mpp_cancel_press();
    if (s.pressed == 3 || s.pressed == 4) {
        s.pressed    = 0;
        s.have_press = false;
    }
    motor_invalidate();
    ++s.ctrl_rev;
    ++s.thr_rev;
    ++s.arm_rev;
}

/*
 * A session opened over the body: the presses the screen held go, as at a
 * change of task -- a hold of ARM, a throttle drag, a step button, a tab --
 * and the session's first frame is painted whole.  ARM and the throttle
 * are out of sight under it, so a bench that is not SAFE switches to SAFE
 * first: a DISARM held by another finger, whose release the session takes,
 * is posted here, and no output drives under a session that hides DISARM.
 */
static void session_opened(void)
{
    if (!mo_bench_safe()) {
        mo_post(MOTOR_CMD_DISARM, 0.0f);
    }
    mo_end_drag();
    ui_tabs_cancel(&s.tabs);
    mwb_cancel();
    if (s.pressed == 1 && !s.gate.armed) {
        ui_hold_reset(&s.arm);
    }
    s.pressed    = 0;
    s.have_press = false;
    motor_invalidate();
    ++s.ctrl_rev;
    ++s.arm_rev;
}

/*
 * The TARGET row tapped: OUTPUTS, where the throttle's pins are bound, on
 * the protocol they are bound as; on the PROG PORT entry of a DShot
 * programming port the throttle drives alone; on the overview while they
 * are bound as several or none is known.  The router leaves this screen for it, which
 * switches the bench to SAFE as any way out does (leave()); the row takes
 * the tap only on a SAFE bench.  The home tag there comes back here
 * (ui_router_parent()), on the task the row was tapped on.
 */
static void open_outputs(void)
{
    const uint8_t ports = motor_dshot_ports();
    if (s.thr_pins == 0u && ports != 0u && (ports & (ports - 1u)) == 0u) {
        /* One DShot programming port and no slot: its PROG PORT entry. */
        uint8_t k = 0u;
        while ((ports & (1u << k)) == 0u) {
            ++k;
        }
        outputs_screen_open_on_port(k);
    } else if (s.thr_proto != MOTOR_PROTO_MIXED
               && s.thr_proto < OUTBIND_PROTOS) {
        outputs_screen_open_on(s.thr_proto);
    }
    ui_router_goto(SCREEN_OUTPUTS);
    mwb_opened_outputs();
}

/*
 * The controls whose press stops what the bench drives, where mo_event() hands
 * a DOWN at (x, y) to them now: a session's own (a stick run's ABORT), or
 * DISARM on the rail while the button reads DISARM.  The router hands a
 * press on one through the alert band and the band's help bubble
 * (ui_screen_t's safe_at); ARM is none.
 */
bool mo_safe_at(int x, int y)
{
    const prog_page_set_t *ps = mpp_shown();
    if (ps != NULL) {
        return prog_ps_safe_at(ps, x, y);
    }
    return s.gate.armed && gfx_rect_contains(s.arm_rect, x, y);
}

void mo_event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    /* A session on PROGRAM covers the body, the strip row and the rail with
     * it: every press is the session's.  ARM is the session's too, through
     * its own warning and hold. */
    const prog_page_set_t *ps = mpp_shown();
    if (ps != NULL) {
        prog_ps_event(ps, evt);
        return;
    }
    /* The strip and the TARGET row first: a press of theirs is theirs. */
    const mwb_ev_t wb = mwb_event(evt, !msp_modal(), mo_target_locked());
    if (wb == MWB_TASK) {
        task_changed();
        return;
    }
    if (wb == MWB_TARGET) {
        open_outputs();
        return;
    }
    if (wb == MWB_TAKEN) {
        ++s.ctrl_rev;           /* the TARGET row draws its press */
        return;
    }
    /* SETTINGS' page or PROGRAM's list has the left column; the rail
     * stays this file's.  A row's tap may open a session over all of it. */
    const bool on_live = mwb_task() == UI_WB_LIVE;
    if (mwb_task() == UI_WB_SETTINGS) {
        msp_event(evt, mo_endpoints_free());
    } else if (mwb_task() == UI_WB_PROGRAM && mpp_event(evt)) {
        if (mpp_shown() != NULL) {
            session_opened();
        }
        return;
    }
    /*
     * Each control bumps its own counter, rather than every touch bumping
     * one.  A drag delivers a move per frame, and repainting the buttons
     * with the throttle is what put the drag over two panel frames.
     */
    if (on_live && ui_tabs_event(&s.tabs, evt)) {
        /* The other pane paints over this one's region, so the record of
         * what was drawn there is void -- not merely the chrome's. */
        motor_invalidate();
    }
    /* Refused while the value is not live: the track takes no press, so a
     * bench armed under a finger resting on it moves nothing until the next
     * press, and a drag under way ends.  Out of sight on SETTINGS, it takes
     * none either. */
    const bool live = mo_value_live();
    if (!live || !on_live) {
        mo_end_drag();
    } else {
        const bool was_dragging = s.slider.dragging;
        const bool slider_moved = ui_slider_event(&s.slider, evt);
        if (was_dragging || s.slider.dragging) {
            s.knob_finger = true;
        }
        if (slider_moved) {
            mo_post(MOTOR_CMD_THROTTLE, s.slider.value);
            ++s.thr_rev;
        }
    }

    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        /* One percentage point, on the press rather than the release: this
         * is a fine adjustment and it should feel immediate. */
        if (on_live && (gfx_rect_contains(s.down_rect, x, y)
                        || gfx_rect_contains(s.up_rect, x, y))) {
            /* A throttle like any other, and refused like one: nothing
             * is pressed, moved or posted. */
            if (!live) {
                return;
            }
            const bool up = gfx_rect_contains(s.up_rect, x, y);
            motor_screen_set_throttle(s.slider.value + (up ? 1.0f : -1.0f));
            mo_post(MOTOR_CMD_THROTTLE, s.slider.value);
            s.have_press = true;
            s.press_id   = evt->point.id;
            s.pressed    = up ? 4 : 3;
            /* The button's own fill changes, so the row repaints; the value
             * it moved is on the throttle's counter. */
            ++s.ctrl_rev;
            ++s.thr_rev;
            return;
        }
        if (gfx_rect_contains(s.arm_rect, x, y)) {
            if (s.gate.armed) {
                /* DISARM acts on the press: a stop never waits for a lift
                 * that may not come.  The press takes no hold, so a finger
                 * left on the button arms nothing when it reads ARM again. */
                mo_post(MOTOR_CMD_DISARM, 0.0f);
                ++s.arm_rev;
                ++s.ctrl_rev;
                return;
            }
            if (s.pressed == 1) {
                return;   /* the hold belongs to the contact that began it */
            }
            if (mo_arm_locked()) {
                return;   /* programming and an armed bench are exclusive */
            }
            s.have_press = true; s.press_id = evt->point.id; s.pressed = 1;
            ui_hold_begin(&s.arm);
            ++s.arm_rev;
            ++s.ctrl_rev;
        } else if (gfx_rect_contains(s.reset_rect, x, y)) {
            s.have_press = true; s.press_id = evt->point.id; s.pressed = 2;
            ++s.ctrl_rev;
        }
        return;
    }
    if (!s.have_press || evt->point.id != s.press_id) {
        return;   /* a second finger cannot steal the first one's release */
    }
    if (evt->type == TOUCH_EVENT_MOVE) {
        /*
         * A finger that leaves ARM abandons the hold -- see ui_hold_leave().
         * A press that starts on ARM and slides onto the plot would otherwise
         * arm the bench two seconds later.
         */
        if (s.pressed == 1 && !s.gate.armed && !gfx_rect_contains(s.arm_rect, x, y)
            && ui_hold_leave(&s.arm)) {
            s.pressed = 0;
            ++s.arm_rev;
            ++s.ctrl_rev;
        }
        return;
    }
    if (evt->type == TOUCH_EVENT_UP) {
        const int was = s.pressed;
        s.have_press = false;
        s.pressed = 0;
        if (was != 0) {
            ++s.ctrl_rev;   /* the button comes back up */
        }
        if (was == 1) {
            /* Whatever was held is abandoned: the fade goes back to green,
             * and an arm that had not completed does not complete later. */
            /* Arming is a hold that has already sent its command by the
             * time the finger lifts; a release that armed nothing sends
             * nothing.  DISARM acted on its press. */
            (void)ui_hold_end(&s.arm);
            ++s.arm_rev;
        }
        if (was == 2 && touch_event_is_tap_up(evt)
            && gfx_rect_contains(s.reset_rect, x, y)) {
            mo_post(MOTOR_CMD_RESET_PEAKS, 0.0f);
            /* The peaks start again from the live reading: a measurement
             * only once one is. */
            memset(s.pk_ok, 0, sizeof(s.pk_ok));
        }
    }
}

/*
 * The two animations ARM carries.  Each advances its own counter, so a frame
 * of either repaints the button's 180 x 28 px and nothing else: the control
 * row is 800 x 90, and an animation that asked for the row would put the
 * screen back where the throttle drag was.
 */
void mo_tick(float dt_s)
{
    if (kst_panel_holds_port() != s.kst_held) {
        s.kst_held = kst_panel_holds_port();
        ++s.arm_rev;
        ++s.ctrl_rev;
    }
    if (s.pressed == 1 && mo_arm_locked()) {
        /* The port was taken under a hold of ARM: the hold ends. */
        s.pressed = 0;
        ui_hold_reset(&s.arm);
        ++s.arm_rev;
        ++s.ctrl_rev;
    }
    if (s.pressed == 1 && !s.gate.armed) {
        ++s.arm_rev;
        if (ui_hold_tick(&s.arm, dt_s)) {
            /* From the ask, not only from the bench's answer: the bench
             * arms between two frames, and a step button pressed in a frame
             * that still reads it as disarmed posts from this value onto a
             * bench that is armed by the time the command lands. */
            mo_throttle_from_zero();
            mo_post(MOTOR_CMD_ARM, 0.0f);
            ui_value_ask_arm(&s.gate);
        }
    }
    if (s.arm.flash_ms > 0) {
        ui_hold_flash_step(&s.arm, dt_s);
        ++s.arm_rev;   /* keep the frames coming while it flashes */
    }
    /* The TARGET row follows the bench and the hold: drawn locked, or
     * taking a press again. */
    if (mo_target_locked() != s.target_locked) {
        s.target_locked = mo_target_locked();
        ++s.ctrl_rev;
    }
    /* And names the DShot ports the throttle drives as they are read. */
    if (port_view_revision() != s.thr_ports_rev) {
        s.thr_ports_rev = port_view_revision();
        ++s.ctrl_rev;
    }
    msp_tick(mo_endpoints_free());
    mpp_armed(!mo_bench_safe());
    mpp_tick(dt_s);
}
