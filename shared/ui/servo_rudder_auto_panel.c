/**
 * @file
 * @brief RUDDER AUTO's left card: TARGET 1000us and 2000us, MARGIN,
 *        TOLERANCE, the step, the last result and the report; the
 *        footer's HOLD 2s: ARM + START; and the run behind it
 *        (servo_rudder_auto.h) on a KST, Chaservo or Hitec servo on the
 *        programming port, its steps done the way a finger does them.
 *
 * See servo_rudder_auto_int.h for the layout.  The hold fires the ARM +
 * START sequence (servo_ready.c); the run starts on its last step.  The
 * gate and the READY card's rows are servo_rudder_auto_ready.c's.  The run takes the angle the
 * screen takes in (servo_screen_encoder()), as RUDDER's readout shows it:
 * the deflection from ZERO.  Its sessions with the programmer are the
 * limit run's, through the seam servo_limit_seam.c picks by the family:
 * the LIMIT RUN settings' SERVO row, which follows the programmer last
 * opened on PROGRAM, and for HITEC the Hitec programmer's series.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "kst_port.h"
#include "log_name.h"
#include "prog5v_box.h"
#include "prog_choice.h"
#include "prog_registry.h"
#include "rcbench_version.h"
#include "sensors_rows.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_value_gate.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_core_int.h"

/** @brief The box's column: RUDDER's, from x 18, 500px wide. */
#define RA_X      (PAD + 10)
#define RA_W      500                    /**< Its width, px. */
#define RA_HALF_W 245                    /**< A row's half, px. */
#define RA_GAP    10                     /**< Between two halves, px. */
#define RA_ROW1_Y (CARD_Y + 112)         /**< TARGET 1000us and 2000us. */
#define RA_ROW2_Y (CARD_Y + 172)         /**< MARGIN and TOLERANCE. */
#define RA_LINE_Y (CARD_Y + 292)         /**< The first line. */
#define RA_LINE_GAP 4                    /**< Between two lines, px. */
/** @brief The room the lines take, down to the card's floor: 136px. */
#define RA_LINES_H (CARD_FLOOR - RA_LINE_Y)

_Static_assert(2 * RA_HALF_W + RA_GAP == RA_W, "two halves fill the column");
_Static_assert(RA_LINES_H >= RA_LINES * 20 + (RA_LINES - 1) * RA_LINE_GAP,
               "three lines of one row each fit above the footer");

/* The families are the limit run's, value for value: one SERVO row and one
 * seam serve both runs. */
_Static_assert((int)SERVO_RAUTO_FAM_KST == (int)SERVO_LIMRUN_FAM_KST
                   && (int)SERVO_RAUTO_FAM_HITEC_D
                          == (int)SERVO_LIMRUN_FAM_HITEC_D
                   && (int)SERVO_RAUTO_FAM_HITEC_C
                          == (int)SERVO_LIMRUN_FAM_HITEC_C,
               "AUTO's families are the limit run's");

/* ------------------------------------------------------------ the state */

void rauto_init(void)
{
    servo_rauto_init(&s.ra.run);
    ready_init();
}

bool rauto_shown(void)
{
    return s.ra.view && rudder_shown();
}

void rauto_view(bool on)
{
    if (servo_rauto_running(&s.ra.run) || s.ra.view == on) {
        return;
    }
    rauto_cancel();
    s.ra.view = on;
    ++s.ctrl_rev;
}

/* ----------------------------------------------------------- the layout */

gfx_rect_t rauto_rect(rauto_ctl_t k)
{
    switch (k) {
    case RA_C_LO:
    case RA_C_HI:
        return (gfx_rect_t){
            (int16_t)(RA_X + ((k == RA_C_HI) ? RA_HALF_W + RA_GAP : 0)),
            RA_ROW1_Y, RA_HALF_W, UI_TAP_MIN };
    case RA_C_MARGIN:
    case RA_C_TOL:
        return (gfx_rect_t){
            (int16_t)(RA_X + ((k == RA_C_TOL) ? RA_HALF_W + RA_GAP : 0)),
            RA_ROW2_Y, RA_HALF_W, UI_TAP_MIN };
    case RA_C_START:
        return ui_footer_rect(UI_FOOTER_HOLD);
    default:
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
}

gfx_rect_t rauto_lines_rect(void)
{
    return (gfx_rect_t){ RA_X, (int16_t)(RA_LINE_Y - 2), RUD_SIDE_W,
                         (int16_t)(RA_LINES_H + 2) };
}

bool servo_screen_rauto_running(void)
{
    return servo_rauto_running(&s.ra.run);
}

int servo_screen_rauto_phase(void)
{
    return (int)s.ra.run.phase;
}

/* --------------------------------------------------- what a run reads */

/**
 * @brief The setting a row's keypad sets.
 * @param k the row
 * @return the setting; SETTING_COUNT for START
 */
static setting_id_t row_setting(rauto_ctl_t k)
{
    switch (k) {
    case RA_C_LO:     return SET_RUDDER_DEFL_LO;
    case RA_C_HI:     return SET_RUDDER_DEFL_HI;
    case RA_C_MARGIN: return SET_RUDDER_MARGIN;
    case RA_C_TOL:    return SET_RUDDER_TOL;
    default:          return SETTING_COUNT;
    }
}

/**
 * @brief The deflection on show and whether a run may take it: a reading
 *        of the source, from a zero taken.
 * @param deg where it goes
 * @return false without one
 */
static bool defl_now(float *deg)
{
    /* rudder_defl() first: it drops a zero taken from another source. */
    return rudder_defl(deg) && s.rud.geo.zeroed;
}

/**
 * @brief The configuration a run started now takes.
 * @param c receives it
 */
static void ra_cfg(servo_rauto_cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    c->target_deg[0] = settings_get(SET_RUDDER_DEFL_LO);
    c->target_deg[1] = settings_get(SET_RUDDER_DEFL_HI);
    c->margin_pct    = (uint8_t)settings_get_int(SET_RUDDER_MARGIN);
    c->tol_ddeg = (uint16_t)lroundf(settings_get(SET_RUDDER_TOL) * 10.0f);
    c->family    = svs_family_of(SERVO_FN_RAUTO);
    c->center_us = s.center_us;
    const float len = settings_get(SET_RUDDER_LEN_MM);
    c->len_mm = (len >= SERVO_RUDDER_LEN_MIN_MM) ? len : 0.0f;
    snprintf(c->dut, sizeof(c->dut), "%s", settings_text(SET_TEXT_DUT_NAME));
    snprintf(c->type, sizeof(c->type), "%s", type()->name);
    (void)servo_target_line(c->output, sizeof(c->output), s.drive_target,
                            s.drive_pin, s.drive_pwm, s.pins, s.pins_known);
    (void)sensors_rows_meter(c->meter, sizeof(c->meter));
    (void)enc_name(c->source, sizeof(c->source));
    snprintf(c->firmware, sizeof(c->firmware), "%s", RCBENCH_VERSION_STRING);
    c->text = ui_servo_table();
}

/* ----------------------------------------------------------- the run */

/**
 * @brief What the run asked for, done the way a finger does it: the
 *        screen's commands, SUPPLY's OFF, the programmer's steps.
 * @param d what it asked
 * @return false when the programmer refused a step: the run has ended
 */
static bool ra_apply(const servo_rauto_do_t *d)
{
    bool again = false;
    if (d->command && value_live()) {
        s.commanded_deg = us_to_deg(d->cmd_us);
        post(SERVO_CMD_POSITION, d->cmd_us);
        s.pending.slew_per_s = 0u;
        ++s.ctrl_rev;
    }
    if (d->off) {
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
        value_edge(ui_value_ask_leave(&s.gate), SERVO_TEST_AB_DISARMED);
        post(SERVO_CMD_DISARM, 0);
        ++s.arm_rev;
    }
    if (d->arm) {
        post(SERVO_CMD_ARM, 0);
        ++s.arm_rev;
    }
    const uint8_t fam = s.ra.run.cfg.family;
    bool taken = true;
    if (d->connect && lim_pd_feeds()
        && (!s.sup.output || !s.ra.sup_online)) {
        /* A PD mini the run did not start with, or one whose output is
         * off: the session would switch it on beside the rail's feed. */
        if (s.sup.output) {
            servo_rauto_supply_changed(&s.ra.run, test_now());
        } else {
            servo_rauto_abort(&s.ra.run, SERVO_TEST_AB_SUPPLY_OFF,
                              test_now());
        }
        again = true;
    } else if (d->connect) {
        taken = lim_seam_connect(
            fam, lim_pd_feeds(),
            (uint16_t)lroundf(supply_screen_set_v() * 1000.0f),
            (uint16_t)lroundf(supply_screen_set_i() * 1000.0f));
    } else if (d->write) {
        taken = lim_seam_write(fam, d->value);
    } else if (d->pulses) {
        taken = lim_seam_pulses(fam, d->pulse_dus);
    } else if (d->leave) {
        taken = lim_seam_leave(fam);
        s.ra.leaving = taken && !servo_rauto_running(&s.ra.run);
    }
    if (!taken) {
        servo_rauto_refused(&s.ra.run, test_now());
        again = true;
    }
    return !again;
}

/** @brief The run's step against the clock, and what it asks done: twice
 *         where a refusal ended it in the first. */
static void ra_step(void)
{
    for (int k = 0; k < 2; ++k) {
        float deg = 0.0f;
        const bool have = defl_now(&deg);
        if (servo_rauto_running(&s.ra.run) && !s.rud.geo.zeroed) {
            /* Another ANGLE SOURCE, or the IMU's other AXIS or SIGN,
             * dropped the zero the run measures from. */
            servo_rauto_abort(&s.ra.run, SERVO_TEST_AB_SETTINGS, test_now());
        }
        const servo_rauto_in_t in = {
            .live        = value_live(),
            .safe        = !bench_arming() && !s.gate.armed,
            .angle_valid = have,
            .angle_deg   = deg,
            .still_ms    = have ? s.ra.still_ms : 0u,
            .settle_ms   = (enc_src() == SERVO_ANGLE_IMU) ? s.imu_settle_ms
                                                          : 0u,
        };
        servo_rauto_do_t d;
        servo_rauto_step(&s.ra.run, test_now(), &in, &d);
        test_tell();
        if (ra_apply(&d)) {
            break;
        }
    }
}

/**
 * @brief The servo the programmer holds, in AUTO's terms: a Hitec servo
 *        with no pulses (servo_rauto_servo_t), its DIRECTION CCW as
 *        REVERSION.
 * @param fam the run's family
 * @param sv receives it
 */
static void ra_servo(uint8_t fam, servo_rauto_servo_t *sv)
{
    servo_limrun_servo_t l;
    sv->ok = lim_seam_servo(fam, &l);
    sv->value[0] = l.angle[0];
    sv->value[1] = l.angle[1];
    sv->lo = l.lo_raw;
    sv->hi = l.hi_raw;
    sv->reversion = l.reversion;
    if (fam == SERVO_RAUTO_FAM_KST) {
        memcpy(sv->pulse_dus, l.pulse_dus, sizeof(sv->pulse_dus));
    }
}

/** @brief The programmer's answer to the step under way. */
static void ra_poll(void)
{
    if (!servo_rauto_programming(&s.ra.run)) {
        return;
    }
    const uint8_t fam = s.ra.run.cfg.family;
    bool ok = false;
    if (!lim_seam_poll(fam, &ok)) {
        return;
    }
    servo_rauto_servo_t sv;
    memset(&sv, 0, sizeof(sv));
    switch (s.ra.run.phase) {
    case SERVO_RAUTO_PH_READ:
    case SERVO_RAUTO_PH_CONNECT:
        if (!ok) {
            if (lim_seam_no_servo(fam)) {
                servo_rauto_no_servo(&s.ra.run, test_now());
            } else {
                servo_rauto_refused(&s.ra.run, test_now());
            }
            break;
        }
        ra_servo(fam, &sv);
        servo_rauto_connected(&s.ra.run, &sv, test_now());
        break;
    case SERVO_RAUTO_PH_WRITE:
        if (ok) {
            ra_servo(fam, &sv);
        }
        /* The file saved before the run's first write. */
        servo_rauto_backup(&s.ra.run, lim_seam_backup(fam));
        if (s.ra.run.pulse_op) {
            servo_rauto_pulsed(&s.ra.run, ok, ok ? sv.pulse_dus : NULL,
                               test_now());
        } else {
            servo_rauto_written(&s.ra.run, ok, ok ? sv.value : NULL,
                                test_now());
        }
        break;
    case SERVO_RAUTO_PH_LEAVE:
        servo_rauto_left(&s.ra.run, ok, test_now());
        break;
    default:
        break;
    }
    ++s.ctrl_rev;
}

/** @brief The programmer's run is over: its own supply values come back.
 *         A FAIL in the read session switches the supplies off now, after
 *         its LEAVE gave the port back: an error leaves no output on. */
static void ra_seam_ended(void)
{
    lim_seam_end(s.ra.run.cfg.family);
    if (s.ra.off_due) {
        s.ra.off_due = false;
        svs_supplies_off();
        ++s.sup_rev;
        ++s.power_rev;
    }
}

/** @brief A run that was running is over: the programmer's own supply
 *         values come back once its port is. */
static void ra_ended(void)
{
    if (!s.ra.seen || servo_rauto_running(&s.ra.run)) {
        return;
    }
    s.ra.seen = false;
    s.ra.off_due = s.ra.run.verdict == SERVO_RAUTO_FAIL;
    /* A run ended with its first write on its way: no answer handed the
     * file over, and the session's own backup, saved before that write,
     * holds the servo as the run found it. */
    if (servo_rauto_restore(&s.ra.run)) {
        servo_rauto_backup(&s.ra.run,
                           lim_seam_backup_kept(s.ra.run.cfg.family));
    }
    if (s.ra.leaving) {
        s.ra.end_due = true;
    } else {
        ra_seam_ended();
    }
    test_tell();
    ++s.ctrl_rev;
}

void rauto_leave_follow(void)
{
    if (s.ra.leaving && !lim_seam_leaving(s.ra.run.cfg.family)) {
        s.ra.leaving = false;
    }
    if (s.ra.end_due && !s.ra.leaving) {
        s.ra.end_due = false;
        ra_seam_ended();
    }
}

void rauto_end_now(servo_test_abort_t why)
{
    if (!servo_rauto_running(&s.ra.run)) {
        return;
    }
    if (why == SERVO_TEST_AB_DISARMED && servo_rauto_switching(&s.ra.run)) {
        return;
    }
    servo_rauto_abort(&s.ra.run, why, test_now());
    ra_step();
    ra_ended();
}

void rauto_meter_follow(servo_source_id_t id, uint32_t changes)
{
    if (!servo_rauto_running(&s.ra.run)
        || (id == SERVO_SOURCE_INA3221 && changes == s.ra.changes)) {
        return;
    }
    servo_test_abort_t why = SERVO_TEST_AB_INA_METER;
    if (s.have_drop && (int32_t)(s.drop_at - s.ra.changes) > 0) {
        why = test_abort_of(s.drop_why);
    }
    rauto_end_now(why);
}

void rauto_angle(const servo_test_enc_t *e)
{
    if (e == NULL) {
        return;
    }
    s.ra.still_ms = e->valid ? e->still_ms : 0u;
    /* The run needs the angle: a part with no magnet gives none, as one
     * that does not answer. */
    const servo_angle_in_t a = { .valid = e->valid, .still_ms = e->still_ms,
                                 .taken_ms = e->taken_ms, .gap = e->gap };
    servo_rauto_angle(&s.ra.run, &a);
}

void rauto_supply(const servo_test_reading_t *r)
{
    if (r == NULL || !servo_rauto_running(&s.ra.run)
        || servo_rauto_programming(&s.ra.run) || lim_fixed_supply()) {
        return;
    }
    if (!s.ra.sup_watch) {
        s.ra.sup_watch  = true;
        s.ra.sup_online = r->online;
        s.ra.sup_out    = r->online && r->output;
        s.ra.sup_v      = r->set_v;
        s.ra.sup_i      = r->set_i;
        return;
    }
    servo_test_abort_t why = SERVO_TEST_AB_NONE;
    bool changed = false;
    if (!s.ra.sup_online) {
        changed = r->online;
    } else if (!r->online) {
        why = SERVO_TEST_AB_SUPPLY_LOST;
    } else if (s.ra.sup_out && r->trip != 0u) {
        why = SERVO_TEST_AB_TRIPPED;
    } else if (s.ra.sup_out && !r->output) {
        why = SERVO_TEST_AB_SUPPLY_OFF;
    } else if (r->output != s.ra.sup_out
               || fabsf(r->set_v - s.ra.sup_v) > 0.005f
               || fabsf(r->set_i - s.ra.sup_i) > 0.0005f) {
        changed = true;             /* switched on from off, or set anew */
    }
    if (changed) {
        servo_rauto_supply_changed(&s.ra.run, test_now());
        ra_step();
        ra_ended();
    } else if (why != SERVO_TEST_AB_NONE) {
        rauto_end_now(why);
    }
}

/** @brief ARM + START's hold completed: the sequence begins, and the run
 *         on its last step (ra_start()). */
static void ra_begin(void)
{
    ui_hold_spend(&s.ra.hold);          /* its flash runs on */
    s.ra.down = false;
    s.ra.note = NULL;
    ++s.ctrl_rev;
    ++s.arm_rev;
    if (rauto_hold_blocked(NULL) != NULL) {
        return;
    }
    /* The settings as they read now: the run starts on these whatever
     * changes while the steps take their time. */
    ra_cfg(&s.ra.cfg);
    (void)svs_seq_begin();
}

/**
 * @brief The sequence's last step: the run starts on the bench it left,
 *        ARMED, every part switched on.
 * @return false when the gate or the run refused it
 */
static bool ra_start(void)
{
    s.ra.note = rauto_blocked();
    if (s.ra.note != NULL) {
        return false;
    }
    /* The settings the hold fired on; the output's line as it reads now,
     * with the port's pulse the steps switched on. */
    servo_rauto_cfg_t c = s.ra.cfg;
    (void)servo_target_line(c.output, sizeof(c.output), s.drive_target,
                            s.drive_pin, s.drive_pwm, s.pins, s.pins_known);
    if (!servo_rauto_start(&s.ra.run, &c, test_now())) {
        s.ra.note = TR(RA_REFUSED);
        return false;
    }
    s.ra.changes   = s.changes;
    s.ra.sup_watch = false;
    lim_seam_begin(c.family);
    if (!lim_fixed_supply()) {
        servo_test_reading_t r;
        memset(&r, 0, sizeof(r));
        if (s.have_sup) {
            r = test_reading_of(&s.sup);
        }
        rauto_supply(&r);
    }
    test_tell();
    stop_sweep();
    s.ra.seen   = true;
    s.ra.file   = 0;
    s.ra.report = false;
    s.ra.off    = !settings_get_bool(SET_LOG_USER);
    close_panels();
    lim_seam_close_pages();
    servo_invalidate();
    return true;
}

void rauto_frame(float dt_s)
{
    if (s.ra.down) {
        ++s.arm_rev;
        /* A row that turns red under the hold drops it: the hold fires
         * only on a bench that may start. */
        if (rauto_hold_blocked(NULL) != NULL && ui_hold_leave(&s.ra.hold)) {
            s.ra.down = false;
        } else if (ui_hold_tick(&s.ra.hold, dt_s)) {
            ra_begin();
        }
    }
    if (ui_hold_flashing(&s.ra.hold)) {
        ui_hold_flash_step(&s.ra.hold, dt_s);
        ++s.arm_rev;                    /* the frames of the flash */
    }
    if (s.fn == SERVO_FN_RAUTO && svs_seq_busy()
        && svs_seq_frame(rauto_hold_blocked(NULL))) {
        const bool ok = ra_start();
        svs_seq_started(ok, s.ra.note);
    }
    ra_poll();
    ra_step();
    ra_ended();
    const uint32_t sig = rauto_sig();
    if (sig != s.ra.sig) {
        s.ra.sig = sig;
        if (rauto_shown()) {
            ++s.ctrl_rev;
        }
    }
}

/* -------------------------------------------------------- the presses */

/**
 * @brief A row's keypad over the left column, its OK taken by the
 *        settings' keypad rule (keypad_done()).
 * @param id the setting
 */
static void open_row(setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    ui_keypad_open(&s.kp, overlay_area(), ui_setting_label(id), d->unit,
                   settings_get(id), d->min, d->max, decimals_of(id));
    s.kp_target  = KT_SETTING;
    s.kp_setting = id;
}

bool rauto_down(const touch_event_t *evt)
{
    if (evt == NULL || !rauto_shown()) {
        return false;
    }
    for (int k = RA_C_LO; k < RA_C_COUNT; ++k) {
        if (!gfx_rect_contains(rauto_rect((rauto_ctl_t)k), evt->point.x,
                               evt->point.y)) {
            continue;
        }
        if (k == RA_C_START && servo_rauto_running(&s.ra.run)) {
            /* STOP RUN: at once, as every control that stops a drive. */
            rauto_end_now(SERVO_TEST_AB_OPERATOR);
            return true;
        }
        if (k == RA_C_START && svs_seq_busy()) {
            /* The sequence under way stops on the press, every output
             * off. */
            svs_seq_end(SERVO_TEST_AB_OPERATOR);
            return true;
        }
        if (s.ra.pressed != RA_C_NONE) {
            return true;
        }
        if (k != RA_C_START
            && (servo_rauto_running(&s.ra.run) || svs_seq_busy())) {
            return true;            /* the rows wait for the end */
        }
        s.ra.pressed  = (uint8_t)k;
        s.ra.press_id = evt->point.id;
        if (k == RA_C_START) {
            s.ra.note = NULL;
            if (rauto_hold_blocked(NULL) == NULL) {
                s.ra.down = true;
                ui_hold_begin(&s.ra.hold, TX_HOLD_HINT_RUDDER_AUTO);
            }
            ++s.arm_rev;
        }
        ++s.ctrl_rev;
        return true;
    }
    return false;
}

bool rauto_rest(const touch_event_t *evt)
{
    if (evt == NULL || s.ra.pressed == RA_C_NONE
        || evt->point.id != s.ra.press_id) {
        return false;
    }
    const rauto_ctl_t k = (rauto_ctl_t)s.ra.pressed;
    const gfx_rect_t r = rauto_rect(k);
    const bool over = gfx_rect_contains(r, evt->point.x, evt->point.y);
    if (evt->type != TOUCH_EVENT_UP) {
        if (k == RA_C_START && s.ra.down && !over
            && ui_hold_leave(&s.ra.hold)) {
            s.ra.down = false;
            ++s.ctrl_rev;
        }
        return true;
    }
    s.ra.pressed = RA_C_NONE;
    ++s.ctrl_rev;
    if (k == RA_C_START) {
        if (s.ra.down) {
            (void)ui_hold_end(&s.ra.hold, evt, r);
        } else if (over && touch_event_is_tap_up(evt)
                   && !servo_rauto_running(&s.ra.run)) {
            /* A tap on the grey hold: the first row that refuses it, in
             * the band. */
            char why[128];
            if (ready_refusal(why, sizeof(why)) != NULL) {
                ui_action_refused(why);
            }
        }
        s.ra.down = false;
        ++s.arm_rev;
        return true;
    }
    if (touch_event_is_tap_up(evt) && rauto_shown() && over
        && !servo_rauto_running(&s.ra.run) && !svs_seq_busy()) {
        open_row(row_setting(k));
    }
    return true;
}

void rauto_cancel(void)
{
    if (s.ra.pressed != RA_C_NONE || s.ra.down
        || s.ra.hold.held_s > 0.0f) {
        s.ra.pressed = RA_C_NONE;
        s.ra.down    = false;
        ui_hold_reset(&s.ra.hold);
        ++s.ctrl_rev;
        ++s.arm_rev;
    }
    ready_cancel();
}

/* ------------------------------------------------------------ the texts */

/**
 * @brief The report's name as the box gives it.
 * @param b where it goes
 * @param n its size
 */
static void file_name(char *b, size_t n)
{
    char f[LOG_RUN_NAME_MAX + 4];
    if (s.ra.off) {
        snprintf(b, n, "%s", TR(SV_USER_LOGS_OFF));
        return;
    }
    if (s.ra.file > 0) {
        log_name_build(f, sizeof(f), LOG_PREFIX_RUDDR, s.ra.file, true);
    } else {
        snprintf(f, sizeof(f), "%s",
                 (s.ra.file < 0) ? TR(SV_NOT_RECORDED) : TR(SV_WRITING));
    }
    snprintf(b, n, TR(RA_FILE), f);
}

/**
 * @brief The reason of the ended run as the box gives it: SURFACE BINDS AT
 *        with its angle, or the reason's entry.
 * @param b where it goes
 * @param n its size
 */
static void why_text(char *b, size_t n)
{
    const servo_rauto_t *r = &s.ra.run;
    if (r->why == SERVO_RAUTO_WHY_BINDS) {
        char deg[12];
        snprintf(b, n, TR(RA_BINDS_AT),
                 svs_angle_text(deg, sizeof(deg), r->bind_deg, false));
    } else {
        snprintf(b, n, "%s", ui_servo_str(servo_rauto_why_str(r)));
    }
}

/**
 * @brief The servo a run programs: SERVO KST/CHASERVO, or SERVO HITEC with
 *        the Hitec programmer's series.
 * @param b where it goes
 * @param n its size
 * @param fam servo_rauto_family_t
 */
static void servo_name(char *b, size_t n, uint8_t fam)
{
    if (fam == SERVO_RAUTO_FAM_KST) {
        snprintf(b, n, TR(RA_SERVO), TR(SV_LIM_SERVO_KST));
    } else {
        snprintf(b, n, TR(RA_SERVO_HITEC),
                 ui_setting_option(SET_HITEC_SERIES,
                                   fam == SERVO_RAUTO_FAM_HITEC_D ? 1 : 0));
    }
}

/**
 * @brief How the values before the run come back: KST's RESTORE, or the
 *        Hitec programmer's backup file loaded on its FILES.
 * @param b where it goes
 * @param n its size
 * @param r the ended run
 */
static void restore_text(char *b, size_t n, const servo_rauto_t *r)
{
    if (r->cfg.family == SERVO_RAUTO_FAM_KST) {
        snprintf(b, n, TR(SV_LIM_RESTORE), TR(SVH_KST));
    } else if (r->servo.backup[0] != '\0') {
        snprintf(b, n, TR(SV_LIM_RESTORE_HITEC), r->servo.backup);
    } else {
        snprintf(b, n, "%s", TR(SV_LIM_NO_BACKUP));
    }
}

/**
 * @brief Why no run is under way, as the first line gives it: the last
 *        sequence's failure, a start the run refused, or the first row
 *        that refuses the hold.
 * @return the text; NULL when the hold may begin
 */
static const char *idle_why(void)
{
    if (svs_seq_failure() != NULL) {
        return svs_seq_failure();
    }
    return (s.ra.note != NULL) ? s.ra.note : rauto_hold_blocked(NULL);
}

const char *rauto_line(int i, char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    buf[0] = '\0';
    const servo_rauto_t *r = &s.ra.run;
    const bool running = servo_rauto_running(r);
    switch (i) {
    case 0: {
        if (running) {
            snprintf(buf, n, TR(RA_STEP),
                     ui_servo_str(servo_rauto_phase_str(r->phase)),
                     r->side == 0u ? 1000u : 2000u,
                     (unsigned)r->writes[r->side],
                     (unsigned)SERVO_RAUTO_WRITES_MAX, r->sessions);
            break;
        }
        if (svs_seq_busy()) {
            (void)svs_seq_line(buf, n);
            break;
        }
        const char *why = idle_why();
        if (why != NULL) {
            snprintf(buf, n, "%s", why);
        } else {
            const float k = 1.0f + (float)settings_get_int(SET_RUDDER_MARGIN)
                                   / 100.0f;
            snprintf(buf, n, TR(RA_GOALS),
                     (double)(settings_get(SET_RUDDER_DEFL_LO) * k),
                     (double)(settings_get(SET_RUDDER_DEFL_HI) * k));
        }
        break;
    }
    case 1:
        if (r->state != SERVO_RAUTO_DONE || running) {
            break;
        }
        if (r->verdict == SERVO_RAUTO_PASS) {
            snprintf(buf, n, TR(RA_SET), (unsigned)r->servo.value[0],
                     (unsigned)r->cur[0], (unsigned)r->servo.value[1],
                     (unsigned)r->cur[1]);
        } else {
            char why[64];
            why_text(why, sizeof(why));
            snprintf(buf, n, TR(RA_LAST),
                     ui_servo_str(r->verdict == SERVO_RAUTO_FAIL
                                      ? SERVO_STR_FAIL : SERVO_STR_ABORTED),
                     why);
        }
        break;
    case 2:
        if (r->state != SERVO_RAUTO_DONE || running) {
            /* The servo the run programs, or one started now would. */
            servo_name(buf, n, running ? r->cfg.family
                                       : svs_family_of(SERVO_FN_RAUTO));
        } else if (servo_rauto_restore(r)) {
            restore_text(buf, n, r);
        } else {
            file_name(buf, n);
        }
        break;
    default:
        break;
    }
    return buf;
}

/* ---------------------------------------------------------- the drawing */

/**
 * @brief A row's button: its text and whether it takes a tap now.
 * @param c the canvas
 * @param k the row
 */
static void draw_row(gfx_canvas_t *c, rauto_ctl_t k)
{
    char t[48];
    const float len = settings_get(SET_RUDDER_LEN_MM);
    switch (k) {
    case RA_C_LO:
    case RA_C_HI: {
        const float v = settings_get(row_setting(k));
        const unsigned us = (k == RA_C_LO) ? 1000u : 2000u;
        if (len >= SERVO_RUDDER_LEN_MIN_MM) {
            snprintf(t, sizeof(t), "%uus %.1f° %.1fmm", us, (double)v,
                     (double)servo_rudder_mm(v, len));
        } else {
            snprintf(t, sizeof(t), "%uus %.1f°", us, (double)v);
        }
        break;
    }
    case RA_C_MARGIN:
        snprintf(t, sizeof(t), TR(RA_MARGIN),
                 (unsigned)settings_get_int(SET_RUDDER_MARGIN));
        break;
    default:
        snprintf(t, sizeof(t), TR(RA_TOL),
                 (double)settings_get(SET_RUDDER_TOL));
        break;
    }
    ui_button_font(c, rauto_rect(k), t, ui_theme_color(UI_C_PANEL_SUNK),
                   s.ra.pressed == (uint8_t)k,
                   !servo_rauto_running(&s.ra.run), SV_FONT_LABEL);
}

void rauto_render(gfx_canvas_t *c)
{
    for (int k = RA_C_LO; k <= RA_C_TOL; ++k) {
        draw_row(c, (rauto_ctl_t)k);
    }
    const bool running = servo_rauto_running(&s.ra.run);
    const gfx_rect_t lr = rauto_lines_rect();
    gfx_fill_rect(c, lr.x, lr.y, lr.w, lr.h, ui_theme_color(UI_C_PANEL));
    const servo_rauto_t *r = &s.ra.run;
    char line[RA_LINES][96];
    const char *t[RA_LINES];
    for (int i = 0; i < RA_LINES; ++i) {
        t[i] = rauto_line(i, line[i], sizeof(line[i]));
    }
    int pitch = 16;
    const gfx_font_t *face = rauto_lines_face(t, &pitch);
    if (face == NULL) {
        /* Past the room: the 8 x 16 face, cut at the alert band. */
        face = &gfx_font_8x16;
    }
    const gfx_rect_t old = c->clip;
    if (gfx_clip_intersect(c, lr)) {
        int y = RA_LINE_Y;
        for (int i = 0; i < RA_LINES; ++i) {
            if (t[i][0] == '\0') {
                continue;
            }
            /* A refused START and RESTORE in the warning colour, the result
             * in its verdict's. */
            const bool warn =
                (i == 0 && !running && !svs_seq_busy() && idle_why() != NULL)
                || (i == 2 && servo_rauto_restore(r));
            gfx_color_t col = ui_theme_color(warn ? UI_C_WARN : UI_C_TEXT_DIM);
            if (i == 1) {
                col = r->verdict == SERVO_RAUTO_PASS ? ui_theme_color(UI_C_OK)
                      : r->verdict == SERVO_RAUTO_FAIL
                          ? ui_theme_color(UI_C_DANGER)
                          : ui_theme_color(UI_C_WARN);
            }
            y += pitch * ui_text_wrap(c, gfx_rect_make(RA_X, y, RUD_SIDE_W,
                                                       pitch),
                                      pitch, t[i], face, col)
                 + RA_LINE_GAP;
        }
    }
    c->clip = old;
}

void rauto_draw_hold(gfx_canvas_t *c)
{
    const bool running = servo_rauto_running(&s.ra.run);
    const bool seq = svs_seq_busy();
    char label[40];
    ui_action_t now = UI_ACTION_AVAILABLE;
    if (running) {
        snprintf(label, sizeof(label), "%s", TR(SV_LIM_STOP));
        now = UI_ACTION_ACTIVE;
    } else if (seq) {
        (void)svs_seq_label(label, sizeof(label));
        now = UI_ACTION_WAITING;
    } else {
        snprintf(label, sizeof(label), "%s", TR(RA_START));
    }
    /* The fade runs from green to the amber of the sequence the hold
     * starts. */
    ui_hold_button(c, rauto_rect(RA_C_START), label, ui_action_fill(now),
                   ui_action_fill(UI_ACTION_WAITING), &s.ra.hold,
                   s.ra.pressed == RA_C_START,
                   running || seq || rauto_hold_blocked(NULL) == NULL,
                   SV_FONT_LABEL);
}

const gfx_font_t *rauto_lines_face(const char *const t[RA_LINES], int *pitch)
{
    /* The label face at its own height, else the 8 x 16 one. */
    static const struct {
        const gfx_font_t *f;
        int pitch;
    } k_face[2] = { { SV_FONT_LABEL, 20 }, { &gfx_font_8x16, 16 } };
    if (t == NULL) {
        return NULL;
    }
    for (int k = 0; k < 2; ++k) {
        const int p = k_face[k].pitch;
        int h = 0, n = 0;
        for (int i = 0; i < RA_LINES; ++i) {
            if (t[i] == NULL || t[i][0] == '\0') {
                continue;
            }
            h += (n++ > 0 ? RA_LINE_GAP : 0)
                 + p * ui_text_wrap(NULL, gfx_rect_make(0, 0, RUD_SIDE_W, p),
                                    p, t[i], k_face[k].f, 0);
        }
        if (h <= RA_LINES_H) {
            if (pitch != NULL) {
                *pitch = p;
            }
            return k_face[k].f;
        }
    }
    return NULL;
}

bool rauto_targets(float deg[2])
{
    if (deg == NULL) {
        return false;
    }
    const servo_rauto_t *r = &s.ra.run;
    for (int k = 0; k < 2; ++k) {
        /* The side's direction as the last run measured it, else 1000us
         * on the negative side, as the commanded angle has it. */
        const int sg = r->sign[k] != 0 ? r->sign[k] : (k == 0 ? -1 : 1);
        deg[k] = (float)sg * settings_get(k == 0 ? SET_RUDDER_DEFL_LO
                                                 : SET_RUDDER_DEFL_HI);
    }
    return true;
}

uint32_t rauto_sig(void)
{
    const servo_rauto_t *r = &s.ra.run;
    const uint32_t v[] = {
        s.ra.view ? 1u : 0u, (uint32_t)r->state, (uint32_t)r->phase,
        (uint32_t)r->side, (uint32_t)r->writes[0], (uint32_t)r->writes[1],
        r->sessions, (uint32_t)r->verdict, (uint32_t)r->why,
        (uint32_t)(s.ra.file + 2), s.ra.off ? 1u : 0u, s.ra.pressed,
        (uint32_t)(uintptr_t)rauto_hold_blocked(NULL),
        (uint32_t)(uintptr_t)s.ra.note,
        (uint32_t)(uintptr_t)svs_seq_failure(),
        (uint32_t)servo_seq_step_now(&s.rdy.seq),
        (uint32_t)lroundf(settings_get(SET_RUDDER_DEFL_LO) * 10.0f),
        (uint32_t)lroundf(settings_get(SET_RUDDER_DEFL_HI) * 10.0f),
        (uint32_t)settings_get_int(SET_RUDDER_MARGIN),
        (uint32_t)lroundf(settings_get(SET_RUDDER_TOL) * 10.0f),
    };
    uint32_t h = 2166136261u;
    for (size_t k = 0; k < sizeof(v) / sizeof(v[0]); ++k) {
        h = (h ^ v[k]) * 16777619u;
    }
    return h;
}
