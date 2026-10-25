/**
 * @file servo_screen_link.c
 * @brief What the panel tells the SERVO screen and takes from it.
 *
 * What the panel tells the SERVO screen and takes from it: the bench's
 * arm and disarm, a stop, the command collected, the servo's feedback,
 * the output encoder, the servo rail's meter and the supply's samples,
 * the link, the drive target and the bound pins; and the readings the
 * panel and the tests ask of the screen.  The state and the seam are
 * servo_screen_int.h and servo_screen_core_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "outputs.h"
#include "servo_limrun.h"
#include "ui_plot.h"
#include "ui_text.h"
#include "ui_value_gate.h"
#include "ui_widgets.h"

/* Last: the layout macros of the headers it includes have short names
 * (W, H, PAD), which no header included after it may meet. */
#include "servo_screen_core_int.h"

/*
 * What an edge of the value's state does, in one place.
 *
 * Into LIVE: the value is the rest.
 *
 * Out of LIVE, to LEAVING or to OFF: everything that drives the servo by
 * itself ends -- a run, ended as @p why, a sweep, a pause, a drag, and a
 * held position, which a change of SPEED or of the profile would otherwise
 * say again -- and a motion command not yet taken is not sent.  The value
 * shown stays where it was last driven: the release a run ends with moves
 * none, because the value is no longer live (release()).
 */
void value_edge(ui_value_edge_t edge, servo_test_abort_t why)
{
    if (edge == UI_VALUE_EDGE_LEFT) {
        test_end_now(why);
        svs_drop_motion();
        stop_sweep();
        s.dragging         = false;
        s.driving          = false;
        s.knob_had_driving = false;
        s.at_rest          = false;
        ++s.ctrl_rev;
    }
    if (edge == UI_VALUE_EDGE_LIVE) {
        /*
         * The bench armed: the pins drive every surface at its rest, the
         * release the arm pays having put them there, whatever this screen
         * commanded before.  The value shown is that rest, and the first
         * input starts from it.  Without feedback the horn is drawn there
         * at once, as the output is: nothing slews to a rest it is
         * already at.
         */
        s.commanded_deg = svs_rest_deg();
        s.at_rest       = true;
        if (!s.have_feedback) {
            s.shown_cmd = (float)outputs_role_rest(OUT_ROLE_SURFACE);
            s.shown_deg = s.commanded_deg;
            /* And where it was drawn before the arm is no part of where
             * a sweep started after it begins. */
            s.trail_n = 0u;
        }
        ++s.ctrl_rev;
    }
}

void servo_screen_set_armed(bool armed)
{
    const bool was = s.gate.armed;
    value_edge(ui_value_report(&s.gate, armed), SERVO_TEST_AB_DISARMED);
    if (was == armed) {
        return;
    }
    /* A finger on the dial does not carry its drag across the edge: after
     * an arm the first input is a new press, and a disarmed dial takes
     * none. */
    svs_drag_end();
    /*
     * Nothing is being held across this edge, in either direction.  An arm
     * starts from nothing -- the panel drops the position and the slot on the
     * way through -- and a disarm holds nothing by definition.  A screen that
     * went on believing it was driving would say a discarded position again
     * on the next change of profile, trim or travel, onto a bench that is
     * now armed.
     */
    s.driving = false;
    /* Nor is what the knob found before the edge: a withdrawal after it
     * puts back no drive from the other side. */
    s.knob_had_driving = false;
    stop_sweep();
    if (armed) {
        ui_hold_reached(&s.arm);
        /*
         * The profile changed after ARM was asked for and nothing answered
         * it yet: the rest is restated under the profile in force, its
         * range and rate in the panel's order.  reissue() answers a change
         * while the arm is on its way, so this is the backstop.
         */
        if (s.profile_rev != s.arm_profile_rev) {
            post(SERVO_CMD_RELEASE, 0);
            s.arm_profile_rev = s.profile_rev;
        }
    } else {
        /*
         * Disarmed, however it happened -- this screen's button, a STOP, a
         * dead touch, or the far end.  Nothing is being held any more: the
         * rings must stop pulsing, and a change to the profile, the trim or
         * the travel must not say a position again and rebuild a command
         * the stop had just released.
         */
        s.at_rest = false;
        svs_drop_motion();
        /* The flash says the bench armed; disarmed, it is over, so both
         * buffers end on ARM's own colour. */
        s.arm.flash_ms = 0;
        if (ui_hold_left(&s.arm)) {
            /* The bench disarmed under a finger still down on the button, and
             * the hold ended with it; see ui_hold_left(). */
            s.arm_down = false;
        }
    }
    ++s.arm_rev;
    ++s.ctrl_rev;
}

void servo_screen_cancel_arm(void)
{
    /*
     * A stop latched, so a hold under way is abandoned and an arm it has
     * already produced is dropped.
     *
     * The command is dealt with first and on its own account.  A hold that
     * completed and whose finger has since lifted leaves nothing held and
     * nothing counting -- the release cleared both -- while its arm is still
     * waiting to be read, and that is precisely the one that would be
     * forwarded a frame later and clear the latch the stop had just set.
     */
    bool changed = false;
    /* Counted: a question about the live output stands only while this
     * count is the one it was asked under (ask_stands()). */
    ++s.stops;
    /* The stop ends the arm on its way too, and an armed bench is on its
     * way down from here.  A run ends, and START TEST's hold with it; the
     * value shown stays where the run last drove the servo, as after any
     * disarm, rather than at the rest a run's end on a live bench releases
     * to. */
    value_edge(ui_value_ask_leave(&s.gate), SERVO_TEST_AB_STOP);
    test_end_now(SERVO_TEST_AB_STOP);
    if (s.test_down || s.test_hold.held_s > 0.0f
        || s.lim_down || s.lim_hold.held_s > 0.0f) {
        ui_hold_reset(&s.test_hold);
        s.test_down = false;
        ui_hold_reset(&s.lim_hold);
        s.lim_down = false;
        ++s.test_rev;
    }
    if (s.pending.kind == SERVO_CMD_ARM) {
        s.pending.kind = SERVO_CMD_NONE;
        changed = true;
    }
    if (s.arm_down || s.arm.held_s > 0.0f) {
        ui_hold_reset(&s.arm);
        s.arm_down = false;
        changed = true;
    }
    if (changed) {
        ++s.arm_rev;
    }
    /* OUTPUT ON's hold too: the stop cuts the output, and a hold that ran
     * on would switch it back on.  An ON already posted is SUPPLY's to drop
     * (supply_screen_cancel_on()). */
    if (s.out_down || s.out_hold.held_s > 0.0f) {
        ui_hold_reset(&s.out_hold);
        s.out_down = false;
        ++s.sup_rev;
    }
    /* And the two warnings' holds: a voltage past a standard servo's rating
     * or a profile that can destroy one is not applied by a hold the stop
     * interrupted.  The warnings stay open; a new hold applies them. */
    /* An APPLY being pressed -- a tap on the question as well as a hold --
     * is let go of: its release must not apply a set point after the stop. */
    if (s.ov_have && (s.ov_pressed == OP_ASK_APPLY
                      || s.ov_pressed == OP_WARN_APPLY
                      || s.ov_pressed == OP_TEST_START
                      || s.ov_pressed == OP_LIM_START)) {
        s.ov_have    = false;           /* the press is over, as ov_let_go() */
        s.ov_pressed = OP_NONE;
        ++s.ctrl_rev;
    }
    if (s.ask.down || s.warn.down) {
        ui_hold_reset(&s.ask.hold);
        ui_hold_reset(&s.warn.hold);
        s.ask.down  = false;
        s.warn.down = false;
        ++s.ask.rev;
        ++s.warn.rev;
    }
    /*
     * And nothing is being held any more.  The armed state need not have
     * moved -- a bench that was not armed is stopped just the same, and the
     * panel centres the surface either way -- so this cannot wait for that
     * edge: the rings would go on pulsing, and the next change of profile,
     * trim or travel would say the released position again.
     */
    if (s.driving) {
        s.driving = false;
        ++s.ctrl_rev;
    }
    /* Nor is what the knob found before the stop: a withdrawal after it
     * puts back no drive, while the screen still reads the bench as armed. */
    s.knob_had_driving = false;
    /* A position not yet taken would be sent after the stop. */
    svs_drop_motion();
    svs_drag_end();
    stop_sweep();
}

bool servo_screen_take(servo_cmd_t *out)
{
    if (out == NULL || s.pending.kind == SERVO_CMD_NONE) {
        return false;
    }
    *out = s.pending;
    s.pending.kind = SERVO_CMD_NONE;
    s.knob_live = false;
    return true;
}

void servo_screen_feedback(uint16_t position_us, float current_a, bool valid)
{
    const float deg = us_to_deg(position_us);

    /*
     * A reading counts as new only if it is drawn differently: compare at
     * the precision shown, tenths of a degree.  Feedback arrives at the
     * poll rate whether or not the servo moved, and a revision bump per
     * reading would repaint the 488x418 card at that rate and never reach
     * the grip's clipped repaint.  The current is not drawn from here: the
     * CURRENT row reads the servo rail's meter (svs_rail_now()).
     */
    (void)current_a;
    const int q_deg = (int)(deg * 10.0f + (deg >= 0.0f ? 0.5f : -0.5f));
    const bool same = valid && s.have_feedback && q_deg == s.shown_q_deg;

    s.measured_deg  = deg;
    s.have_feedback = valid;
    /*
     * The arm is drawn where the servo reports it, with no easing: easing
     * would add the screen's lag to the servo's, and the two are
     * indistinguishable when drawn.  The smoothing in tick() applies only
     * while nothing is reporting.
     */
    if (valid) {
        s.shown_deg = deg;
        /*
         * Paused, the servo is held where it is, and it goes on moving while
         * the HOLD is on its way and while it settles on the output held:
         * the angle a changed profile says again (reissue()) is the one it
         * reports, not the one it had at the tap.
         */
        if (s.paused) {
            const uint16_t was = svs_deg_to_us(s.commanded_deg);
            s.commanded_deg = clamp_travel(deg);
            if (svs_deg_to_us(s.commanded_deg) != was) {
                ++s.dial_rev;       /* COMMANDED shows it */
            }
        }
    }
    if (!same) {
        s.shown_q_deg = q_deg;
        ++s.dial_rev;
    }
}

void servo_screen_encoder(const servo_test_enc_t *e)
{
    if (e == NULL) {
        return;
    }
    /* A reading that was queued before the link went down is not the
     * angle now. */
    const servo_test_enc_t none = { .valid = false };
    if (!s.link_up) {
        e = &none;
    }
    s.enc_valid     = e->valid;
    s.enc_no_magnet = !e->valid && e->no_magnet;
    if (e->valid) {
        s.enc_raw = e->raw;
    }
    servo_test_encoder(&s.test, e);
    /* Redrawn only when the digits change: tenths of a degree. */
    const int q = e->valid ? (int)lroundf(enc_deg_now() * 10.0f) : 0x7FFF;
    if (q != s.shown_q_enc) {
        s.shown_q_enc = q;
        ++s.dial_rev;           /* MEASURED shows it */
    }
}

/* @p x in units of 1/@p per, rounded half away from zero, for the digits of
 * a reading; held to +-10^8 so the count fits whatever the float holds. */
int32_t svs_fixed_of(float x, float per)
{
    const float q = x * per;
    if (!(q > -1.0e8f)) {
        return -100000000;
    }
    if (!(q < 1.0e8f)) {
        return 100000000;
    }
    return (int32_t)lroundf(q);
}

/*
 * The servo rail as its meter reads it.  With the INA3221 as the meter, its
 * last CH1 window: the mean bus voltage at the load side of the shunt, the
 * mean current and their product.  Otherwise the supply's last sample, which
 * is the PD mini's reading or the model's.  Only what arrived: a quantity
 * with no reading is not ok and is drawn as dashes and as a gap.
 */
rail_t svs_rail_now(void)
{
    rail_t r;
    memset(&r, 0, sizeof(r));
    if (s.source == SERVO_SOURCE_INA3221) {
        if (!s.have_win || !s.link_up) {
            return r;
        }
        r.v_ok    = s.win.voltage;
        r.i_ok    = s.win.current;
        r.v       = (float)s.win.mean_mv / 1000.0f;
        r.i       = (float)s.win.mean_ma / 1000.0f;
        r.p       = r.v * r.i;
        r.clipped = r.i_ok && (s.win.clip_hi || s.win.clip_lo);
        const int32_t ma = s.win.mean_ma;
        r.q_a     = (ma >= 0) ? (ma + 5) / 10 : -((5 - ma) / 10);
        return r;
    }
    if (!s.have_sup || !s.sup.online) {
        return r;
    }
    /* A reading that is not a number is no reading. */
    r.v_ok = (s.sup.ok & SUPPLY_OK_VOLTAGE) != 0u && isfinite(s.sup.v);
    r.i_ok = (s.sup.ok & SUPPLY_OK_CURRENT) != 0u && isfinite(s.sup.i);
    r.v    = s.sup.v;
    r.i    = s.sup.i;
    r.p    = s.sup.p;
    r.q_a  = r.i_ok ? svs_fixed_of(s.sup.i, 100.0f) : 0;
    return r;
}

static int32_t cur_key_of(const rail_t *r)
{
    return r->i_ok ? r->q_a * 2 + (r->clipped ? 1 : 0) : CUR_KEY_NONE;
}

/* The CURRENT row after its meter or the meter's reading changed: redrawn
 * only when what it shows did, hundredths of an amp and the colour. */
static void cur_follow(void)
{
    const rail_t r = svs_rail_now();
    const int32_t key = cur_key_of(&r);
    if (key != s.cur_key) {
        s.cur_key = key;
        ++s.cur_rev;
    }
}

/* One point of the plot from the meter's reading: a quantity that did not
 * arrive is a gap, not a zero. */
static void rail_plot(void)
{
    const rail_t r = svs_rail_now();
    const float v[PS_COUNT] = {
        r.v_ok ? r.v : NAN,
        r.i_ok ? r.i : NAN,
        (r.v_ok && r.i_ok) ? r.p : NAN,
    };
    ui_plot_push(&s.power, v);
    ui_plot_update_scales(&s.power, RC_W);
    ++s.power_rev;
}

servo_test_reading_t test_reading_of(const supply_state_t *st)
{
    servo_test_reading_t r;
    memset(&r, 0, sizeof(r));
    r.v        = st->v;
    r.i        = st->i;
    r.set_v    = st->set_v;
    r.set_i    = st->set_i;
    r.mode     = (uint8_t)st->mode;
    r.output   = st->output;
    r.online   = st->online;
    r.ok       = (st->ok & (SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT))
                 == (SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT);
    r.trip     = st->trip;
    r.samples  = st->samples;
    /* Stamped where the panel had it; a sample without a stamp at its
     * arrival. */
    r.taken_ms = st->stamped ? st->taken_ms : test_now();
    return r;
}

/* One sample of the supply.  @p in_force: it was taken under the meter in
 * force, and not under one replaced since. */
static void supply_sample(const supply_state_t *st, bool in_force)
{
    s.sup = *st;
    s.have_sup = true;
    /* A restore waits for a sample taken after the run's OFF went in which
     * the supply itself reports the output off.  The panel's own flag is
     * its request: the PD mini switches off a link exchange and a module
     * transaction behind it, and a set point put back before then reaches
     * an output that is still on. */
    if (s.test_restore && s.restore_stage == RS_SAMPLE && !st->output
        && st->online && st->mode == SUPPLY_MODE_OFF
        && (int32_t)(test_reading_of(st).taken_ms - s.restore_ms) >= 0) {
        s.restore_stage = RS_OFF_SEEN;
    }
    /* The run's reading, with the horn's position where it is measured;
     * the supply's state alone in a run on the INA3221. */
    const servo_test_reading_t r = test_reading_of(st);
    servo_test_reading(&s.test, &r,
                       s.have_feedback ? svs_deg_to_us(s.measured_deg) : 0u);
    servo_limrun_supply(&s.lim, &r, test_now());
    test_tell();
    /* The line, the plot and the CURRENT row are the supply's while it is
     * the rail's meter; with the INA3221 as the meter they are fed by its
     * windows (servo_screen_window()). */
    if (s.source == SERVO_SOURCE_INA3221) {
        return;
    }
    if (in_force) {
        rail_plot();
    } else {
        /* The supply's last reading all the same, on the line and in the
         * row; no point of a trace that began after it was taken. */
        ++s.power_rev;
    }
    cur_follow();
}

/*
 * The meter @p id as decided at change count @p changes, unless the meter in
 * force was decided at a later one: the snapshot, the windows and the
 * supply's samples reach the render task on three paths, and each can be
 * the oldest.  Returns whether it is the meter in force.
 */
static bool source_take(servo_source_id_t id, uint32_t changes)
{
    const int32_t ahead = s.have_changes ? (int32_t)(changes - s.changes) : 0;
    if (ahead < 0) {
        return false;
    }
    test_meter_follow(id, changes);
    lim_meter_follow(id, changes);
    s.have_changes = true;
    s.changes      = changes;
    /* More than one change ahead: changes went by unseen, and another meter
     * may have been the rail's in between, whatever the meter is called on
     * both sides of them. */
    const bool skipped = ahead > 1;
    if (id == s.source && !skipped) {
        return true;
    }
    const bool was_ina = (s.source == SERVO_SOURCE_INA3221);
    const bool now_ina = (id == SERVO_SOURCE_INA3221);
    s.source = id;
    if (was_ina != now_ina && wb_task() != UI_WB_LIVE) {
        ++s.ctrl_rev;           /* the STALL AT line names a range */
    }
    if (was_ina != now_ina || skipped) {
        /* The two meters do not agree, and the label names one of them:
         * the trace starts again with the meter it is labelled with. */
        ui_plot_clear(&s.power);
        ++s.power_rev;
    }
    if (skipped) {
        /* And a window kept from before them is not this meter's. */
        s.have_win = false;
    }
    cur_follow();
    return true;
}

void servo_screen_source(servo_source_id_t id, uint32_t changes)
{
    (void)source_take(id, changes);
}

void servo_screen_source_why(servo_source_why_t why)
{
    s.src_why = why;
}

void servo_screen_source_dropped(servo_source_why_t why, uint32_t at)
{
    if (why == SERVO_SOURCE_WHY_NONE) {
        return;
    }
    if (!s.have_drop || (int32_t)(at - s.drop_at) > 0) {
        s.have_drop = true;
        s.drop_why  = why;
        s.drop_at   = at;
    }
}

void servo_screen_supply(const supply_state_t *st)
{
    if (st != NULL) {
        supply_sample(st, true);
    }
}

void servo_screen_supply_at(const supply_state_t *st, servo_source_id_t id,
                            uint32_t changes)
{
    if (st != NULL) {
        supply_sample(st, source_take(id, changes));
    }
}

void servo_screen_window(const sense_link_win_t *w, servo_source_id_t id,
                         uint32_t changes)
{
    /* A window that was queued before the link went down is not the rail's
     * reading now, and neither is one taken under a meter that has been
     * replaced since. */
    if (w == NULL || !s.link_up || !source_take(id, changes)) {
        return;
    }
    s.win      = *w;
    s.have_win = true;
    if (s.source != SERVO_SOURCE_INA3221) {
        return;
    }
    {
        /* The runs' reading, stamped where the panel had it. */
        const servo_test_win_t tw = {
            .number   = w->number,
            .current  = w->current,
            .voltage  = w->voltage,
            .clipped  = w->clipped,
            .mean_ma  = w->mean_ma,
            .max_ma   = w->max_ma,
            .min_ma   = w->min_ma,
            .mean_mv  = w->mean_mv,
            .min_mv   = w->min_mv,
            .taken_ms = w->stamped ? w->taken_ms : test_now(),
        };
        if (s.test_on_ina) {
            servo_test_window(&s.test, &tw,
                              s.have_feedback ? svs_deg_to_us(s.measured_deg)
                                              : 0u);
        }
        servo_limrun_window(&s.lim, &tw);
        test_tell();
    }
    rail_plot();
    cur_follow();
}

int servo_screen_power_points(void) { return s.power.filled; }

float servo_screen_power_sample(int series, int back)
{
    return ui_plot_sample(&s.power, series, back);
}

uint16_t servo_screen_commanded(void)
{
    return svs_deg_to_us(s.commanded_deg);
}

bool servo_screen_log_pulse(uint16_t *us)
{
    if (us == NULL || prog_holds()) {
        return false;   /* a session holds the port: no pulse is on it */
    }
    *us = svs_deg_to_us(s.commanded_deg);
    return true;
}

bool servo_screen_log_angle(float *deg)
{
    if (deg == NULL || !enc_shown()) {
        return false;
    }
    *deg = enc_deg_now();
    return true;
}

const ui_keypad_t *servo_screen_keypad(void)
{
    return &s.kp;
}

const char *servo_screen_commanded_text(void)
{
    static char text[16];

    if (prog_holds()) {
        /* A session holds the port: no pulse is on it. */
        return "---";
    }
    snprintf(text, sizeof(text), "%uus",
             (unsigned)svs_deg_to_us(s.commanded_deg));
    return text;
}

uint16_t servo_screen_drawn(void) { return svs_deg_to_us(s.shown_deg); }

uint16_t servo_screen_frame_hz(void) { return s.frame_hz; }

const char *servo_screen_type_name(void) { return type()->name; }

void servo_screen_rate(servo_rate_state_t st, uint16_t hz)
{
    if (st == s.rate_st && hz == s.rate_hz) {
        return;
    }
    s.rate_st = st;
    s.rate_hz = hz;
    if (wb_task() == UI_WB_SETTINGS) {
        ++s.ctrl_rev;   /* the OUTPUT page's note says it */
    }
}

void servo_screen_set_commanded(float deg)
{
    (void)svs_command(deg);
}

void servo_screen_set_link(bool up)
{
    if (s.link_up && !up) {
        test_end_now(SERVO_TEST_AB_LINK);
        /* The encoder's last angle is not the horn's now: the dashes, and
         * no ENC CENTRE from it, until a reading arrives with the link. */
        const servo_test_enc_t none = { .valid = false };
        s.enc_valid     = false;
        s.enc_no_magnet = false;
        servo_test_encoder(&s.test, &none);
        if (s.shown_q_enc != 0x7FFF) {
            s.shown_q_enc = 0x7FFF;
            ++s.ctrl_rev;
        }
        /* And the INA3221's last window is not the rail's reading now: the
         * dashes in the row and on the line while it is the meter. */
        s.have_win = false;
        if (s.source == SERVO_SOURCE_INA3221) {
            ++s.power_rev;
        }
        cur_follow();
        /*
         * A sweep or a pause: the far end stops a sweep and lets a hold go
         * 500ms after the last write it heard, and the surfaces rest.  The
         * screen ends them as well and draws the output at rest, so nothing
         * stays PAUSED over an output that has gone, and the next tap starts
         * a sweep rather than resuming one.
         */
        /* And one not yet taken is not sent once the link is back. */
        if (!servo_cmd_survives_link_loss(&s.pending)) {
            s.pending.kind = SERVO_CMD_NONE;
            s.toggle_live  = false;
        }
        if (s.sweeping || s.paused || s.awaiting || s.dr.on) {
            stop_sweep();
            s.paused        = false;
            s.driving       = false;
            s.toggle_live   = false;
            s.sw.running    = false;
            s.sw.paused     = false;
            if (!s.have_feedback) {
                s.shown_cmd = (float)OUT_SPAN / 2.0f;
                s.shown_deg = us_to_deg_f(svs_cmd_to_us(s.shown_cmd));
            }
            s.commanded_deg = clamp_travel(s.shown_deg);
            ++s.ctrl_rev;
        }
    }
    s.link_up = up;
}

void servo_screen_set_enc_held(bool held)
{
    s.enc_held = held;
}

void servo_screen_set_drive(uint8_t target, uint8_t gpio, bool pwm)
{
    if (target == 0u || target == SERVO_SCREEN_TARGET_UNKNOWN
        || target == SERVO_SCREEN_TARGET_NO_LINK) {
        gpio = SERVO_SCREEN_NO_PIN;
        pwm  = false;
    }
    if (target != s.drive_target || gpio != s.drive_pin
        || pwm != s.drive_pwm) {
        s.drive_target = target;
        s.drive_pin    = gpio;
        s.drive_pwm    = pwm;
        ++s.ctrl_rev;
    }
}

void servo_screen_set_pins(uint64_t gpio_mask, bool known)
{
    if (gpio_mask != s.pins || known != s.pins_known) {
        s.pins = gpio_mask;
        s.pins_known = known;
        ++s.ctrl_rev;
    }
}

void servo_screen_set_surfaces(bool any)
{
    if (any == s.surfaces) {
        return;
    }
    s.surfaces = any;
    ++s.ctrl_rev;                       /* SWEEP greys or comes back */
}

void servo_screen_sweep_refused(void)
{
    s.toggle_live = false;      /* no undo across a notification */
    /* Nothing bound to sweep: no start comes, nothing moves, and nothing
     * is driven for a change of profile to say again. */
    if (s.sweeping || s.paused) {
        stop_sweep();
        s.paused  = false;
        s.driving = false;
        s.commanded_deg = clamp_travel(s.shown_deg);
        ++s.ctrl_rev;
    }
}

void servo_screen_set_bench_armed(bool armed)
{
    s.bench_armed = armed;
}
