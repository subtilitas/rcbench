/**
 * @file servo_screen_touch.c
 * @brief The SERVO screen's presses, dial and knob.
 *
 * The SERVO screen's presses: the controls a DOWN that stops what the
 * bench drives reaches first, the dial, the knob, and every press, move
 * and release on the screen's own controls.  The state and the seam are
 * servo_screen_int.h and servo_screen_core_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_screen.h"

#include <math.h>

#include "servo_limrun.h"
#include "supply_screen.h"
#include "ui_screen.h"
#include "ui_slider.h"
#include "ui_text.h"
#include "ui_widgets.h"

/* Last: the layout macros of the headers it includes have short names
 * (W, H, PAD), which no header included after it may meet. */
#include "servo_screen_core_int.h"

/* ARM takes no press while the programmer holds the port: programming and
 * an armed bench are exclusive.  DISARM is always taken. */
bool svs_arm_locked(void)
{
    return (prog_holds() || servo_limrun_running(&s.lim))
           && !s.gate.armed;
}

/*
 * The controls whose press stops what the bench drives, where svs_event_body()
 * hands a DOWN at (x, y) to them now: DISARM while the button reads DISARM,
 * OUTPUT OFF while the supply's output is on, STOP RUN and STOP TEST in
 * LIVE's result box, and START TEST and START LIMIT RUN on TEST while their
 * run is under way (a tap there stops it).
 * The router hands a press on one of them through the alert band and the
 * band's help bubble (ui_screen_t's safe_at); everything else under those
 * stays theirs, ARM included.  The order of the checks is svs_event_body()'s.
 */
bool svs_safe_at(int x, int y)
{
    if (s.gate.armed && gfx_rect_contains(s.arm_btn, x, y)) {
        return true;
    }
    if (supply_screen_output_on() && gfx_rect_contains(s.out_btn, x, y)) {
        return true;
    }
    if (prog_shown_set() != NULL || modal_open()) {
        return false;
    }
    if (live_shown() && gfx_rect_contains(test_btn_rect(), x, y)) {
        if (s.lim_box) {
            return servo_limrun_running(&s.lim);
        }
        return s.test_box && servo_test_running(&s.test);
    }
    if (wb_task() == UI_WB_TEST && !s.ov_have && rows_page() < 0
        && gfx_rect_contains(page_area(), x, y)) {
        for (int k = 0; k < 2; ++k) {
            if (gfx_rect_contains(run_set_rect(k), x, y)) {
                return false;
            }
        }
        if (gfx_rect_contains(test_start_rect(), x, y)) {
            return servo_test_running(&s.test);
        }
        return gfx_rect_contains(lim_start_rect(), x, y)
               && servo_limrun_running(&s.lim);
    }
    return false;
}

/* ------------------------------------------------------------------ events */

/**
 * @brief Whether a touch is close enough to the horn's sweep to mean the horn.
 */
static bool on_the_dial(int px, int py, float *deg)
{
    const float dx = (float)(px - SHAFT_X);
    const float dy = (float)(SHAFT_Y - py);
    const float r  = sqrtf(dx * dx + dy * dy);
    /* Generous inside, bounded outside: a finger landing short of the horn
     * still means the horn, and one landing well past the arc means the
     * card behind it. */
    if (r < 30.0f || r > (float)ARC_R + 34.0f) {
        return false;
    }
    if (dx < -30.0f) {
        return false;      /* left of the shaft is the case, not the sweep */
    }
    const float phi = atan2f(dy, dx) * 180.0f / 3.14159265358979f;
    if (phi < -104.0f || phi > 104.0f) {
        return false;
    }
    *deg = phi;
    return true;
}

/* Latches that something other than the knob holds the horn. */
void svs_knob_note_owner(void)
{
    if (!live_shown() || servo_test_running(&s.test)
        || s.sweeping || s.paused) {
        s.knob_owned = true;
    }
}

void servo_screen_knob_frame(void)
{
    s.knob_finger = false;
    s.knob_owned  = false;
    svs_knob_note_owner();
}

void svs_knob_withdraw(void)
{
    if (!s.knob_live) {
        return;
    }
    s.knob_live = false;
    if (s.post_count != s.knob_post || s.pending.kind != SERVO_CMD_POSITION) {
        return;     /* something else was posted over it since */
    }
    s.commanded_deg = s.knob_prev_deg;
    /* Held as before the knob only on an armed bench: a stop or a disarm
     * since the knob's post holds nothing, and a screen that read itself as
     * driving would say the position again on the next change of SPEED. */
    s.driving       = s.knob_had_driving && value_live();
    ++s.ctrl_rev;
    if (s.knob_had_cmd) {
        s.pending.value_us = svs_deg_to_us(s.commanded_deg);
    } else {
        s.pending.kind = SERVO_CMD_NONE;
    }
}

/* ------------------------------------------------------------ the drag */

/**
 * @brief The finger moved on the dial to @p deg: the filter's new goal,
 *        held to TRAVEL.  A drag whose filter another command ended starts
 *        it again from the commanded angle, so the horn moves on from where
 *        it is.  The frame commands it, and only while the value is live
 *        (svs_drag_tick()).
 * @param deg the finger's angle, degrees
 */
static void drag_follow(float deg)
{
    if (!s.df.on) {
        s.df.on  = true;
        s.df.out = s.commanded_deg;
    }
    s.df.goal = clamp_travel(deg);
}

void svs_drag_filter_end(void)
{
    s.df.on = false;
}

void svs_drag_end(void)
{
    s.dragging = false;
    svs_drag_filter_end();
}

void svs_drag_on_post(servo_cmd_kind_t kind, uint16_t us)
{
    (void)kind;
    (void)us;
    /* Only the filter's own post and reissue()'s restatement leave it
     * running.  Every other command ends it, the knob's included, even one
     * that comes to the pulse the filter last sent. */
    if (!s.df.on || s.df.posting || s.df.restating) {
        return;
    }
    svs_drag_filter_end();
}

void svs_drag_tick(float dt_s)
{
    if (!s.df.on) {
        return;
    }
    /* A disarm, a stop or SAFE: nothing more is commanded. */
    if (!value_live()) {
        svs_drag_filter_end();
        return;
    }
    if (!(dt_s > 0.0f)) {
        return;
    }
    /* By the frame's own length, so the target follows the finger at the
     * same pace at 30, 60 or 120 frames a second. */
    const float share = 1.0f - expf(-dt_s * 1000.0f / SV_DRAG_TAU_MS);
    float next = s.df.out + (s.df.goal - s.df.out) * share;
    if (fabsf(s.df.goal - next) < SV_DRAG_SNAP_DEG) {
        next = s.df.goal;
    }
    if (next != s.df.out) {
        s.df.out     = next;
        s.df.posting = true;
        (void)svs_command(next);
        s.df.posting = false;
    }
    if (!s.dragging && s.df.out == s.df.goal) {
        svs_drag_filter_end();  /* there: the finger's last angle */
    }
}

void servo_screen_knob_cancel(void)
{
    svs_knob_withdraw();
}

void servo_screen_knob(float span_fraction)
{
    svs_knob_note_owner();
    if (span_fraction == 0.0f || s.dragging || s.knob_finger
        || s.knob_owned) {
        return;
    }
    /* A turn while the value is not live moves nothing and is not kept:
     * the next turn after an arm starts from the value the arm set. */
    if (!value_live()) {
        return;
    }
    /*
     * Posts only into an empty slot or over a position, which it may replace
     * as a drag does.  A release, an arm, a disarm, a hold or a sweep waiting
     * to be taken is not the knob's to overwrite; its delta is dropped.
     */
    if (s.pending.kind != SERVO_CMD_NONE
        && s.pending.kind != SERVO_CMD_POSITION) {
        return;
    }
    const float before = s.commanded_deg;
    const bool  was_knob = s.knob_live && s.post_count == s.knob_post;
    const bool  had_cmd  = s.pending.kind == SERVO_CMD_POSITION;
    const bool  was_driving = s.driving;
    const uint32_t posts = s.post_count;
    (void)svs_command(before + span_fraction * 2.0f * s.travel_deg);
    if (s.post_count != posts) {
        if (!was_knob) {
            s.knob_prev_deg = before;
            s.knob_had_cmd  = had_cmd;
            s.knob_had_driving = was_driving;
        }
        s.knob_live = true;
        s.knob_post = s.post_count;
    }
}

void svs_event_body(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    prog_armed(bench_arming());
    const int px = evt->point.x, py = evt->point.y;
    const prog_page_set_t *ps = prog_shown_set();

    /* The strip's, the TARGET row's and PROGRAM's list's own presses, the
     * session's, then the pages' and the panels'. */
    if (evt->type != TOUCH_EVENT_DOWN && wb_rest(evt)) {
        return;
    }
    if (ps != NULL && prog_ps_has_press(ps, evt->point.id)
        && evt->type != TOUCH_EVENT_DOWN) {
        prog_ps_event(ps, evt);
        return;
    }
    /* The overlay's own press: its moves and its release are its. */
    if (s.ov_have && evt->point.id == s.ov_id
        && evt->type != TOUCH_EVENT_DOWN) {
        ov_rest(evt);
        return;
    }

    if (evt->type == TOUCH_EVENT_DOWN) {
        /*
         * Any press ends a drag on the dial or on SPEED that is still
         * latched, wherever the press lands; ui_slider_event() gives the
         * reason.  A release can go missing, and a later contact reusing
         * the track id would command a position, or a rate, with no press
         * on the control.  A finger that was on the dial still owned it in
         * this frame, so the knob stays out of the frame.
         */
        if (s.dragging) {
            s.knob_finger = true;
            /* The drag's filter ends with it, where it has got to: no
             * command follows the press (svs_drag_tick()). */
            svs_drag_end();
        }
        ui_slider_release(&s.speed);
        if (wb_down(evt)) {
            return;
        }
        if (modal_open() && gfx_rect_contains(overlay_area(), px, py)) {
            /* One press at a time in the panels and the pages. */
            if (!s.ov_have) {
                ov_down(evt);
            }
            return;
        }
        if (ps != NULL && gfx_rect_contains(overlay_area(), px, py)) {
            if (!s.ov_have) {
                prog_ps_event(ps, evt);
            }
            return;
        }
        if (ps == NULL && wb_task() != UI_WB_LIVE
            && gfx_rect_contains(page_area(), px, py)) {
            if (!s.ov_have) {
                if (rows_page() >= 0) {
                    ov_down(evt);
                } else if (wb_task() == UI_WB_PROGRAM) {
                    prog_page_down(evt);
                } else {
                    test_page_down(evt);
                }
            }
            return;
        }
        if (gfx_rect_contains(s.out_btn, px, py)) {
            /* A programming port holds the supply: off is taken, on is
             * not. */
            if (!supply_screen_output_on() && supply_screen_port_held()) {
                ui_router_set_alert(TR(SUP_HELD_REFUSED));
                return;
            }
            /* Off, with a set point past a standard servo's rating: the HV
             * warning, whose hold switches it on.  One press in the
             * overlay at a time. */
            if (!s.out_down && !supply_screen_output_on()
                && hv_set_point()) {
                if (!s.ov_have) {
                    ask_hv_on();
                }
                return;
            }
            /* One contact owns the switch, as on ARM. */
            if (!s.out_down) {
                s.out_down     = true;
                s.out_id       = evt->point.id;
                s.out_press_on = supply_screen_output_on();
                if (!s.out_press_on) {
                    ui_hold_begin(&s.out_hold);
                }
                ++s.sup_rev;
            }
            return;
        }
        for (int k = 0; k < 2; ++k) {
            if (gfx_rect_contains(sup_val_rect(k), px, py)) {
                /* No set point moves under a programming port, and the
                 * band says why. */
                if (supply_screen_port_held()) {
                    ui_router_set_alert(TR(SUP_HELD_REFUSED));
                    return;
                }
                /* The run owns the set points while it runs. */
                test_end_now(SERVO_TEST_AB_OPERATOR);
                if (!s.ov_have) {
                    ov_take(evt, (k == 0) ? OP_SUP_V : OP_SUP_I, -1);
                }
                return;
            }
        }
        if (s.lim_box && live_shown()
            && gfx_rect_contains(test_btn_rect(), px, py)) {
            /* STOP RUN ends the limit run on the press; CLOSE puts the
             * result away. */
            if (servo_limrun_running(&s.lim)) {
                lim_end_now(SERVO_TEST_AB_OPERATOR);
            } else {
                s.lim_box = false;
            }
            servo_invalidate();
            ++s.ctrl_rev;
            return;
        }
        if (s.test_box && live_shown()
            && gfx_rect_contains(test_btn_rect(), px, py)) {
            /* STOP TEST ends the run on the press, as a stop does; CLOSE
             * puts the result away. */
            if (servo_test_running(&s.test)) {
                test_end_now(SERVO_TEST_AB_OPERATOR);
            } else {
                s.test_box = false;
            }
            ++s.ctrl_rev;
            return;
        }
        float deg;
        if (live_shown() && on_the_dial(px, py, &deg)) {
            /* Refused while the value is not live: the press is the dial's
             * and takes no drag, so a bench armed under it moves nothing
             * until the next press. */
            if (!value_live()) {
                return;
            }
            /* A finger on the dial takes the horn from a sweep, and from a
             * run. */
            test_end_now(SERVO_TEST_AB_OPERATOR);
            stop_sweep();
            s.dragging = true;
            s.knob_finger = true;
            s.drag_id  = evt->point.id;
            /* The press is commanded at once, as a tap; the drag after it
             * through the filter, from here. */
            if (svs_command(deg)) {
                s.df = (svs_drag_filter_t){ .on = true,
                                            .goal = s.commanded_deg,
                                            .out = s.commanded_deg };
            }
            return;
        }
        if (gfx_rect_contains(s.centre_btn, px, py)) {
            /* A position like any other, and refused like one. */
            if (!value_live()) {
                return;
            }
            test_end_now(SERVO_TEST_AB_OPERATOR);
            stop_sweep();
            s.commanded_deg = 0.0f;
            post(SERVO_CMD_CENTRE, svs_deg_to_us(0.0f));
            ++s.ctrl_rev;
        } else if (gfx_rect_contains(s.sweep_btn, px, py)) {
            test_end_now(SERVO_TEST_AB_OPERATOR);
            svs_toggle_sweep();
        } else if (gfx_rect_contains(s.release_btn, px, py)) {
            test_end_now(SERVO_TEST_AB_OPERATOR);
            stop_sweep();
            release();
        } else if (gfx_rect_contains(s.arm_btn, px, py)) {
            if (s.gate.armed) {
                /* DISARM acts on the press: a stop never waits for a lift
                 * that may not come.  The press takes no hold, so a finger
                 * left on the button arms nothing when it reads ARM again. */
                svs_ask_disarm();
                ++s.arm_rev;
                return;
            }
            if (svs_arm_locked()) {
                /* Programming and an armed bench are exclusive: the
                 * press arms nothing and says why in the alert band. */
                if (prog_holds()) {
                    ui_router_set_alert(TR(ALERT_ARM_PROG));
                }
                return;
            }
            if (s.arm_down) {
                /* The gesture belongs to the contact that began it.  A second
                 * finger, or a palm, taking it over would leave the first
                 * one's release ignored and arm the bench from a contact
                 * nobody made deliberately. */
                return;
            }
            s.arm_down = true;
            s.arm_id   = evt->point.id;
            ui_hold_begin(&s.arm);
            ++s.arm_rev;
        }
    }

    if (s.out_down && evt->point.id == s.out_id) {
        const bool on = supply_screen_output_on();
        if (evt->type == TOUCH_EVENT_MOVE) {
            /* Off the switch abandons the hold; a press that began on a
             * live output is an OFF, whose release is checked against the
             * switch. */
            if (!s.out_press_on && !gfx_rect_contains(s.out_btn, px, py)
                && ui_hold_leave(&s.out_hold)) {
                s.out_down = false;
                ++s.sup_rev;
            }
            return;
        }
        if (evt->type == TOUCH_EVENT_UP) {
            const bool fired = ui_hold_end(&s.out_hold);
            s.out_down = false;
            ++s.sup_rev;
            /* Off is a tap; on is a hold that has already asked by the time
             * the finger lifts.  Which one is the press's: an earlier ON
             * reported during a second hold does not make its release an
             * OFF. */
            if (s.out_press_on && on && !fired
                && gfx_rect_contains(s.out_btn, px, py)) {
                supply_screen_ask_off();
            }
            return;
        }
    }

    if (s.arm_down && evt->point.id == s.arm_id) {
        if (evt->type == TOUCH_EVENT_MOVE) {
            /* A finger that leaves ARM abandons the hold -- see
             * ui_hold_leave(). */
            if (!s.gate.armed && !gfx_rect_contains(s.arm_btn, px, py)
                && ui_hold_leave(&s.arm)) {
                s.arm_down = false;
                ++s.arm_rev;
            }
            return;
        }
        if (evt->type == TOUCH_EVENT_UP) {
            /* Arming is a hold that has already sent its command by the
             * time the finger lifts; disarming acted on its press. */
            (void)ui_hold_end(&s.arm);
            s.arm_down = false;
            ++s.arm_rev;
            return;
        }
    }

    if (s.dragging && evt->point.id == s.drag_id) {
        s.knob_finger = true;
        if (evt->type == TOUCH_EVENT_MOVE) {
            float deg;
            if (on_the_dial(px, py, &deg)) {
                drag_follow(deg);
            }
            return;
        }
        if (evt->type == TOUCH_EVENT_UP) {
            /* The target goes on to the last angle the finger gave, when
             * the finger lifted.  A release the router or the tracker made
             * (TOUCH_FLAG_NO_TAP) is no lift: the filter stops where it is,
             * and nothing is commanded after it. */
            if (touch_event_is_tap_up(evt)) {
                s.dragging = false;
            } else {
                svs_drag_end();
            }
            return;
        }
    }

    if (ui_slider_event(&s.speed, evt)) {
        s.speed_pct = (int)(s.speed.value + 0.5f);
        /* The rate is part of the command, so a held output takes the new
         * one rather than waiting for the next drag.  Not a paused sweep:
         * the hold moves nothing, and the resume carries the new rate. */
        if (!s.paused) {
            reissue();
        }
        ++s.ctrl_rev;
    }
}
