/**
 * @file servo_limit_panel.c
 * @brief The SERVO screen's limit run: the supply and the programmer it
 *        reads, its start by ARM + START, its steps, and the programmer's
 *        sessions it asks for.
 *
 * The SERVO screen's limit run (servo_limrun.h).  LIMIT RUN's footer hold,
 * HOLD 2s: ARM + START, fires the start sequence (servo_ready.c): PORT PWM,
 * PROG 5V or the PD mini, the read-back, ARM; the run starts on its last
 * step, on the settings the hold fired on.  This file holds the checks the
 * gate (servo_limit_ready.c) asks of the supply, the meter and the
 * programmer, the run's steps done the way a finger does them (the servo's
 * position, ARM and DISARM, SUPPLY's output), and the programmer's
 * sessions with a KST, Chaservo or Hitec servo on the programming port,
 * through the seam servo_limit_seam.c picks by the LIMIT RUN list's SERVO
 * row.  The left card and the hold are servo_limit_card.c's.
 *
 * The screen is servo_screen.c; the state and the layout the files share
 * are servo_screen_int.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "prog5v_box.h"
#include "prog_choice.h"
#include "prog_registry.h"
#include "kst_port.h"
#include "log_name.h"
#include "rcbench_version.h"
#include "sensors_rows.h"
#include "servo_limrun.h"
#include "settings.h"
#include "settings_sensors.h"
#include "supply_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_value_gate.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_int.h"

bool servo_screen_limit_running(void)
{
    return servo_limrun_running(&s.lim);
}

int servo_screen_limit_phase(void)
{
    return (int)s.lim.phase;
}

/* ------------------------------------------------------------ limit run */

/* A PD mini answers on the servo rail: the programmer's CONNECT asks it
 * for its set point as it stands. */
static bool lim_pd_answers(void)
{
    return s.have_sup && s.sup.online && !supply_screen_model();
}

/* The rbmod carrier's port: the fixed 5V on the PROG 5V box, which the
 * programmer switches for its power cycles, whatever a PD mini on the
 * servo rails does. */
bool lim_fixed_supply(void)
{
    return prog_choice_carrier() && s.drive_target >= 1u
           && s.drive_target <= LINK_PG_PORTS_MAX;
}

/**
 * @brief A PD mini that feeds the servo the run reads.
 * @return true when a PD mini answers and the port is not the carrier's
 */
bool lim_pd_feeds(void)
{
    return lim_pd_answers() && !lim_fixed_supply();
}

/* Why the INA3221 is not the meter a run may start on, or NULL.  The limit
 * run reads the INA3221 and nothing else: the PD mini's readings and the
 * model are refused.  No channel of a fitted part watching the tester's
 * target is no INA3221 set up for it (SETUP > SENSORS).  The callers name
 * a target with nothing to watch first: on the rbmod carrier a channel
 * watches a rail, and the bench's outputs are on a rail only once one is
 * bound as SERVO PWM (settings_sensors_set_tester_rail()). */
const char *lim_meter_why(void)
{
    if (settings_sensors_fast() == LINK_SM_FAST_NONE
        || s.src_why == SERVO_SOURCE_WHY_OFF
        || s.src_why == SERVO_SOURCE_WHY_OLD
        || s.src_why == SERVO_SOURCE_WHY_NOT_HELD) {
        return TR(SV_LIM_INA_SETUP);
    }
    if (!s.link_up || s.src_why == SERVO_SOURCE_WHY_NO_LINK
        || s.src_why == SERVO_SOURCE_WHY_SILENT
        || s.src_why == SERVO_SOURCE_WHY_RESET) {
        return TR(SV_LIM_INA_OFFLINE);
    }
    if (s.source != SERVO_SOURCE_INA3221 || !s.have_win) {
        return TR(SV_LIM_INA_NO_WIN);
    }
    return NULL;
}

const char *lim_pwm_why(void)
{
    /* A programmer holding the port renders no pulse either: that is no
     * PORT PWM to switch and no wait for SAFE. */
    if (prog_reg_servo_holds()) {
        return TR(OUT_PORT_HELD);
    }
    if (settings_get_bool(SET_SERVO_PORT_PWM)) {
        /* On the carrier the pulse waits for the PROG 5V as well. */
        return prog_choice_needs_5v() ? ui_servo_str(SERVO_STR_START_PROG_5V)
                                      : TR(SV_LIM_PWM_SAFE);
    }
    /* Off: TARGET opens the port's entry on OUTPUTS, which switches it
     * on, and takes the tap only on a bench that is not ARMED
     * (target_locked_for()). */
    return (s.gate.armed || s.bench_armed) ? TR(SV_LIM_PWM_OFF_ARMED)
                                           : TR(SV_LIM_PWM_OFF);
}

/* Why the programming is not offered, or NULL: only a servo on the
 * programming port, with the port in PWM, and a PD mini on and set where a
 * session can run, or without one a servo rail read at 4.8V or more.  On
 * the rbmod carrier's port the PROG 5V box on stands for the PD mini. */
const char *lim_prog_why(void)
{
    if (s.drive_target == 0u || s.drive_target == SERVO_SCREEN_TARGET_UNKNOWN
        || s.drive_target == SERVO_SCREEN_TARGET_NO_LINK) {
        return TR(SV_LIM_PROG_PORT);
    }
    if (!servo_limrun_running(&s.lim)
        && (prog_reg_servo_holds() || !lim_seam_ready(lim_family()))) {
        return TR(SV_LIM_PROG_BUSY);
    }
    /* A classic Hitec servo enters programming mode only at power-up: even
     * the read needs a supply the bench switches. */
    if (lim_family_now() == SERVO_LIMRUN_FAM_HITEC_C && !lim_pd_feeds()
        && !lim_fixed_supply()) {
        return TR(HP_NEED_PD_C);
    }
    /* The carrier's port: its supply is the PROG 5V box's. */
    if (lim_fixed_supply() && !prog5v_box_on()) {
        return ui_servo_str(SERVO_STR_START_PROG_5V);
    }
    /* A PD mini that answers with its output off, on a rail fed from
     * elsewhere: the read session's ENTER would switch it on beside that
     * supply, and the run ends on an output switched on. */
    if (!servo_limrun_running(&s.lim) && lim_pd_feeds() && !s.sup.output) {
        return TR(SV_LIM_PROG_PD_OFF);
    }
    if (lim_pd_feeds()
        && supply_screen_set_v() * 1000.0f
               < (float)KST_PORT_SUPPLY_MIN_MV - 0.5f) {
        return TR(SV_LIM_PROG_LOW);
    }
    /* No PD mini to set: an external supply, or the model.  The servo
     * rail as the INA3221's last window read it holds the same 4.8V the
     * IOmcu asks of a PD mini, which it cannot check here.  At the
     * start only: the rail is the run's to watch after it. */
    if (!servo_limrun_running(&s.lim) && !lim_pd_feeds()
        && (!s.win.voltage || s.win.mean_mv < KST_PORT_SUPPLY_MIN_MV)) {
        return TR(SV_LIM_PROG_RAIL);
    }
    return NULL;
}

/**
 * @brief Why the run writes nothing to the servo.
 *
 * A KST or Chaservo servo, and a classic Hitec servo, takes a written
 * value only from its next power-up (docs/ServoProgrammer.md, "The power
 * cycle"): with a PD mini,
 * or on the rbmod carrier's port with the PROG 5V box on, every session's
 * CONNECT and the LEAVE after a write switch it off and on; on a rail fed
 * from elsewhere nothing does, and the check after the write would search
 * the servo as it was.  A Hitec D series servo saves and restarts inside
 * the write itself (hitec_prog.h) and needs no power cycle.  Asked before a
 * start only.
 *
 * @return on the carrier's port with the PROG 5V box off, the line that
 *         asks it on; SV_LIM_PROG_NO_PD's line on the programming port
 *         without a PD mini; else NULL
 */
const char *lim_write_why(void)
{
    if (!lim_reads() || servo_limrun_running(&s.lim) || lim_pd_feeds()
        || lim_family() == SERVO_LIMRUN_FAM_HITEC_D) {
        return NULL;
    }
    /* The carrier's port has no PD mini to name: the box is its switch. */
    if (lim_fixed_supply()) {
        return prog5v_box_on() ? NULL : ui_servo_str(SERVO_STR_START_PROG_5V);
    }
    return TR(SV_LIM_PROG_NO_PD);
}

/**
 * @brief The configuration of a run the hold fires now: the settings as
 *        they read, the mode and SERVO PULSES as chosen.  What the bench
 *        offers of them is judged at the start (lim_start()), after the
 *        steps switched the supply on.
 * @param c receives it
 */
static void lim_cfg(servo_limrun_cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    c->centre_us = s.centre_us;
    c->min_us    = s.min_us;
    c->max_us    = s.max_us;
    c->read       = lim_reads();
    c->program    = c->read && s.lim_prog;
    c->set_pulses = c->read && s.lim_pulses
                    && lim_family() == SERVO_LIMRUN_FAM_KST;
    c->family     = c->read ? lim_family() : (uint8_t)SERVO_LIMRUN_FAM_KST;
    snprintf(c->dut, sizeof(c->dut), "%s", settings_text(SET_TEXT_DUT_NAME));
    snprintf(c->type, sizeof(c->type), "%s", type()->name);
    c->frame_hz = s.frame_hz;
    (void)servo_target_line(c->output, sizeof(c->output), s.drive_target,
                            s.drive_pin, s.drive_pwm, s.pins, s.pins_known);
    c->shunt_dmohm = settings_sensors_meter_dmohm();
    (void)sensors_rows_meter(c->meter, sizeof(c->meter));
    c->enc_on = enc_on_now();
    (void)enc_name(c->source, sizeof(c->source));
    c->enc_imu = (enc_src() == SERVO_ANGLE_IMU);
    snprintf(c->firmware, sizeof(c->firmware), "%s", RCBENCH_VERSION_STRING);
    c->text = ui_servo_table();
}

/* What the run asked for, done the way a finger does it here.  False when
 * the programmer refused a step: the run has ended and asks again. */
static bool lim_apply(const servo_limrun_do_t *d)
{
    bool again = false;
    if (d->command && value_live()) {
        s.commanded_deg = us_to_deg(d->cmd_us);
        post(SERVO_CMD_POSITION, d->cmd_us);
        /* A step: the servo's own travel, not SPEED's. */
        s.pending.slew_per_s = 0u;
        ++s.ctrl_rev;
    }
    if (d->off) {
        /* The supply that feeds the servo: on the carrier's port the PROG
         * 5V, not the servo rails' PD mini. */
        if (lim_fixed_supply()) {
            prog5v_box_ask_off(PROG5V_BY_SERVO);
        } else {
            supply_screen_ask_off();
        }
    }
    if (d->release) {
        release();
    }
    if (d->disarm) {
        /* The run's own SAFE, as DISARM does it: not the operator's, so
         * the run goes on (lim_end_now()). */
        value_edge(ui_value_ask_leave(&s.gate), SERVO_TEST_AB_DISARMED);
        post(SERVO_CMD_DISARM, 0);
        ++s.arm_rev;
    }
    if (d->arm) {
        post(SERVO_CMD_ARM, 0);
        ++s.arm_rev;
    }
    const uint8_t fam = s.lim.cfg.family;
    bool taken = true;
    if (d->connect && lim_pd_feeds()
        && (!s.sup.output || !s.lim.sup_online)) {
        /* A PD mini the run did not start with, or one whose output is
         * off: the session would switch it on beside the rail's feed.
         * The run ends as a reading outside a session ends it. */
        servo_limrun_supply_refused(&s.lim, s.sup.output, test_now());
        again = true;
    } else if (d->connect) {
        const bool pd = lim_pd_feeds();
        taken = lim_seam_connect(
            fam, pd, (uint16_t)lroundf(supply_screen_set_v() * 1000.0f),
            (uint16_t)lroundf(supply_screen_set_i() * 1000.0f));
    } else if (d->write) {
        taken = lim_seam_write(fam, d->angle);
    } else if (d->pulses) {
        taken = lim_seam_pulses(fam, d->pulse_dus);
    } else if (d->leave) {
        taken = lim_seam_leave(fam);
        s.lim_leaving = taken && !servo_limrun_running(&s.lim);
    }
    if (!taken) {
        servo_limrun_refused(&s.lim, test_now());
        again = true;
    }
    return !again;
}

/* The run's step against the clock, and what it asks done: twice where a
 * refusal ended it in the first. */
static void lim_step(void)
{
    for (int k = 0; k < 2; ++k) {
        const servo_limrun_in_t in = {
            .live      = value_live(),
            .safe      = !bench_arming() && !s.gate.armed,
            .enc_valid = enc_shown()
                         && (enc_src() == SERVO_ANGLE_IMU || s.enc_held),
            .enc_deg   = enc_deg_now(),
        };
        servo_limrun_do_t d;
        servo_limrun_step(&s.lim, test_now(), &in, &d);
        test_tell();
        if (lim_apply(&d)) {
            break;
        }
    }
}

/* The programmer's answer to the step under way. */
static void lim_poll(void)
{
    if (!servo_limrun_programming(&s.lim)) {
        return;
    }
    const uint8_t fam = s.lim.cfg.family;
    bool ok = false;
    if (!lim_seam_poll(fam, &ok)) {
        return;
    }
    servo_limrun_servo_t sv;
    memset(&sv, 0, sizeof(sv));
    switch (s.lim.phase) {
    case SERVO_LIMRUN_PH_CONNECT:
        if (!ok) {
            /* The CONNECT failed.  One that no servo answered is the first
             * read's NOT_KST; the port is held in programming mode then,
             * and the LEAVE the run asks gives it back.  Any other end is
             * a failed session. */
            if (lim_seam_no_servo(fam)) {
                servo_limrun_no_servo(&s.lim, test_now());
            } else {
                servo_limrun_refused(&s.lim, test_now());
            }
            break;
        }
        (void)lim_seam_servo(fam, &sv);
        /* No backup at a CONNECT: the programmer's is an earlier run's
         * until this run's first write (servo_limrun_backup()). */
        servo_limrun_connected(&s.lim, &sv, test_now());
        break;
    case SERVO_LIMRUN_PH_WRITE:
        if (ok) {
            (void)lim_seam_servo(fam, &sv);
        }
        /* The file saved before the run's first write, sent whether it
         * ended well or not. */
        servo_limrun_backup(&s.lim, lim_seam_backup(fam));
        if (s.lim.pulse_op) {
            servo_limrun_pulsed(&s.lim, ok, ok ? sv.pulse_dus : NULL,
                                test_now());
        } else {
            servo_limrun_written(&s.lim, ok, ok ? sv.angle : NULL,
                                 test_now());
        }
        break;
    case SERVO_LIMRUN_PH_LEAVE:
        servo_limrun_left(&s.lim, ok, test_now());
        break;
    default:
        break;
    }
    ++s.ctrl_rev;
}

/* A run that was running is over: the programmer's own supply values come
 * back, the left card says how it ended, and the bench goes SAFE once the
 * run's last command is taken (lim_safe_follow()). */
static void lim_ended(void)
{
    if (!s.lim_seen || servo_limrun_running(&s.lim)) {
        return;
    }
    s.lim_seen = false;
    s.lim_safe_due = true;
    /* A run ended with a write on its way: no answer handed the file over,
     * and the session's own backup, saved before that write, holds the
     * servo as the run found it. */
    if (servo_limrun_restore(&s.lim)) {
        servo_limrun_backup(&s.lim, lim_seam_backup_kept(s.lim.cfg.family));
    }
    /* A LEAVE on its way is the run's to the end: the programmer's run, and
     * the supply values it keeps, end when the port is back. */
    if (s.lim_leaving) {
        s.lim_end_due = true;
    } else {
        lim_seam_end(s.lim.cfg.family);
    }
    test_tell();
    ++s.test_rev;
    ++s.ctrl_rev;
}

/* The ended run's LEAVE: over once the port has left LEAVING, back in PWM
 * or, refused, still in programming.  Whichever screen is up. */
void lim_leave_follow(void)
{
    if (s.lim_leaving && !lim_seam_leaving(s.lim.cfg.family)) {
        s.lim_leaving = false;
    }
    if (s.lim_end_due && !s.lim_leaving) {
        s.lim_end_due = false;
        lim_seam_end(s.lim.cfg.family);
    }
}

/* End the run now and do what that asks -- the output off, the servo let
 * go -- before whatever ended it acts.  A disarm while the run switches the
 * bench or holds the programmer is the run's own and ends nothing. */
void lim_end_now(servo_test_abort_t why)
{
    if (!servo_limrun_running(&s.lim)) {
        return;
    }
    if (why == SERVO_TEST_AB_DISARMED && servo_limrun_switching(&s.lim)) {
        return;
    }
    servo_limrun_abort(&s.lim, why, test_now());
    lim_step();
    lim_ended();
}

/* The meter changed under the run: it reads the INA3221 decided at its
 * start and nothing else. */
void lim_meter_follow(servo_source_id_t id, uint32_t changes)
{
    if (!servo_limrun_running(&s.lim)
        || (id == SERVO_SOURCE_INA3221 && changes == s.lim_changes)) {
        return;
    }
    servo_test_abort_t why = SERVO_TEST_AB_INA_METER;
    if (s.have_drop && (int32_t)(s.drop_at - s.lim_changes) > 0) {
        why = test_abort_of(s.drop_why);
    }
    lim_end_now(why);
}

/* ARM + START's hold completed: the sequence begins, and the run on its
 * last step (lim_start()).  The settings as they read now: the run starts
 * on these whatever changes while the steps take their time. */
void lim_begin(void)
{
    ui_hold_spend(&s.lim_hold);         /* its flash runs on */
    s.lim_down = false;
    s.lim_note = NULL;
    ++s.test_rev;
    ++s.ctrl_rev;
    ++s.arm_rev;
    if (lim_hold_blocked(NULL) != NULL) {
        return;
    }
    lim_cfg(&s.lim_cfg);
    (void)svs_seq_begin();
}

/**
 * @brief The sequence's last step: the run starts on the bench it left,
 *        ARMED, every part switched on.  SEARCH+PROG and SERVO PULSES as
 *        chosen when the hold fired, where the bench offers them now.
 * @return false when the gate or the run refused it
 */
static bool lim_start(void)
{
    s.lim_note = lim_blocked();
    if (s.lim_note != NULL) {
        return false;
    }
    servo_limrun_cfg_t c = s.lim_cfg;
    const bool writes = c.read && lim_prog_why() == NULL
                        && lim_write_why() == NULL;
    c.program    = c.program && writes;
    c.set_pulses = c.set_pulses && writes;
    /* The output's line as it reads now, with the pulse the steps
     * switched on. */
    (void)servo_target_line(c.output, sizeof(c.output), s.drive_target,
                            s.drive_pin, s.drive_pwm, s.pins, s.pins_known);
    if (!servo_limrun_start(&s.lim, &c, test_now())) {
        s.lim_note = TR(SV_LIM_REFUSED);
        return false;
    }
    s.lim_changes = s.changes;
    s.lim_safe_due = false;
    if (c.read) {
        lim_seam_begin(c.family);
    }
    if (!lim_fixed_supply()) {
        /* The supply as the run starts: with none read yet, one that does
         * not answer, so a PD mini that comes with its output on ends the
         * run.  Not on the carrier's port, whose supply is the PROG 5V. */
        servo_test_reading_t r;
        memset(&r, 0, sizeof(r));
        if (s.have_sup) {
            r = test_reading_of(&s.sup);
        }
        servo_limrun_supply(&s.lim, &r, test_now());
    }
    if (!s.enc_valid && !s.enc_no_magnet) {
        /* An encoder not answering as the run starts: a gap from here,
         * ridden through for SERVO_GAP_MAX_MS (servo_limrun_encoder()). */
        const servo_test_enc_t none = { .valid = false,
                                        .taken_ms = test_now() };
        servo_limrun_encoder(&s.lim, &none);
    }
    test_tell();
    stop_sweep();
    s.lim_seen   = true;
    s.lim_file   = 0;
    s.lim_report = false;
    s.lim_off    = !settings_get_bool(SET_LOG_USER);   /* USER LOGS */
    close_panels();
    lim_seam_close_pages();
    servo_invalidate();
    ++s.ctrl_rev;
    return true;
}

/* A DOWN on ARM + START: STOP RUN and a sequence under way stop on it;
 * else the hold begins where no row refuses it, and a tap on a grey one
 * names the first that does on its release (ov_rest()). */
bool lim_hold_down(const touch_event_t *evt)
{
    if (evt == NULL || s.fn != SERVO_FN_LIMRUN
        || !gfx_rect_contains(ready_hold_rect(), evt->point.x,
                              evt->point.y)) {
        return false;
    }
    if (servo_limrun_running(&s.lim)) {
        lim_end_now(SERVO_TEST_AB_OPERATOR);
        return true;
    }
    if (svs_seq_busy()) {
        svs_seq_end(SERVO_TEST_AB_OPERATOR);
        return true;
    }
    if (s.ov_have) {
        return true;                /* one press at a time */
    }
    ov_take(evt, OP_LIM_START, -1);
    s.lim_note = NULL;
    if (lim_hold_blocked(NULL) == NULL) {
        s.lim_down = true;
        ui_hold_begin(&s.lim_hold, TX_HOLD_HINT_LIMIT_RUN);
    }
    ++s.arm_rev;
    return true;
}

/** @brief The hold under its finger and the sequence, each frame: a row
 *         that turns red under the hold drops it; the run starts on the
 *         sequence's last step. */
static void lim_hold_frame(void)
{
    if (s.lim_down) {
        ++s.arm_rev;
        if (lim_hold_blocked(NULL) != NULL && ui_hold_leave(&s.lim_hold)) {
            s.lim_down = false;
            ov_let_go();
        }
    }
    if (s.fn == SERVO_FN_LIMRUN && svs_seq_busy()
        && svs_seq_frame(lim_hold_blocked(NULL))) {
        const bool ok = lim_start();
        svs_seq_started(ok, s.lim_note);
    }
}

/* What the left card shows of the run and the gate, as one number. */
uint32_t lim_signature(void)
{
    const servo_limrun_t *r = &s.lim;
    const uint32_t f[] = {
        (uint32_t)r->state, (uint32_t)r->phase, (uint32_t)r->side,
        (uint32_t)r->cmd_us, (uint32_t)r->writes, (uint32_t)(s.lim_file + 2),
        s.lim_report ? 1u : 0u, s.lim_off ? 1u : 0u,
        (uint32_t)(uintptr_t)lim_blocked(),
        (uint32_t)(uintptr_t)lim_hold_blocked(NULL),
        (uint32_t)(uintptr_t)lim_prog_why(),
        (uint32_t)(uintptr_t)lim_write_why_held(),
        (uint32_t)(uintptr_t)s.lim_note,
        (uint32_t)(uintptr_t)svs_seq_failure(),
        (uint32_t)servo_seq_step_now(&s.rdy.seq),
        (s.lim_prog ? 1u : 0u) | (s.lim_pulses ? 2u : 0u)
            | (s.lim_hitec ? 4u : 0u),
    };
    uint32_t h = 2166136261u;
    for (size_t k = 0; k < sizeof(f) / sizeof(f[0]); ++k) {
        h = (h ^ f[k]) * 16777619u;
    }
    return h;
}

/** @brief The run that ended leaves the bench SAFE: ARM + START armed it,
 *         and the footer holds no DISARM.  After the run's own last
 *         command (its RELEASE), which the one command slot would lose
 *         under a DISARM; an end by STOP or DISARM is SAFE already. */
static void lim_safe_follow(void)
{
    if (!s.lim_safe_due || servo_limrun_running(&s.lim)
        || s.pending.kind != SERVO_CMD_NONE) {
        return;
    }
    s.lim_safe_due = false;
    if (value_live() || s.gate.arm_asked) {
        svs_ask_disarm();
        ++s.arm_rev;
    }
}

void lim_tick(void)
{
    lim_hold_frame();
    lim_seam_follow();
    lim_poll();
    lim_step();
    lim_ended();
    lim_safe_follow();
    /* LIMIT RUN's left card and READY card, where they show. */
    const uint32_t sig = lim_signature();
    if (sig != s.lim_sig) {
        s.lim_sig = sig;
        ++s.test_rev;
        if (s.fn == SERVO_FN_LIMRUN) {
            ++s.ctrl_rev;
        }
    }
}
