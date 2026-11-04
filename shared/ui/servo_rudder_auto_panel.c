/**
 * @file
 * @brief RUDDER's AUTO box: TARGET 1000us and 2000us, MARGIN, TOLERANCE,
 *        START held 2s, the step, the last result and the report; and the
 *        run behind it (servo_rudder_auto.h) on a KST or Chaservo servo on
 *        the programming port, its steps done the way a finger does them.
 *
 * See servo_rudder_auto_int.h for the layout.  The run takes the angle the
 * screen takes in (servo_screen_encoder()), as RUDDER's readout shows it:
 * the deflection from ZERO.  Its sessions with the programmer are the
 * limit run's (kst_panel_run_*, servo_limit_panel.c lim_apply()).
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "kst_panel.h"
#include "kst_port.h"
#include "log_name.h"
#include "prog5v_box.h"
#include "prog_choice.h"
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
#define RA_START_Y (CARD_Y + 292)        /**< START. */
#define RA_LINE_Y (CARD_Y + 348)         /**< The first line. */
#define RA_LINE_P 24                     /**< The lines' pitch, px. */

_Static_assert(2 * RA_HALF_W + RA_GAP == RA_W, "two halves fill the column");
_Static_assert(RA_LINE_Y + RA_LINES * RA_LINE_P - 4 <= STOP_FLOOR,
               "the lines end above the alert band");
_Static_assert(RA_START_Y + UI_TAP_MIN + 8 <= RA_LINE_Y,
               "START ends above the lines");

/* ------------------------------------------------------------ the state */

void rauto_init(void)
{
    servo_rauto_init(&s.ra.run);
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
        return (gfx_rect_t){ RA_X, RA_START_Y, RA_W, UI_TAP_MIN };
    default:
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
}

gfx_rect_t rauto_lines_rect(void)
{
    return (gfx_rect_t){ RA_X, (int16_t)(RA_LINE_Y - 2), RA_W,
                         (int16_t)(RA_LINES * RA_LINE_P) };
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
    c->family    = SERVO_RAUTO_FAM_KST;
    c->centre_us = s.centre_us;
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

const char *rauto_blocked(void)
{
    if (!value_live()) {
        return ui_servo_str(SERVO_STR_START_NOT_ARMED);
    }
    if (servo_test_running(&s.test)) {
        return ui_servo_str(SERVO_STR_START_TEST_RUNS);
    }
    if (servo_limrun_running(&s.lim)) {
        return ui_servo_str(SERVO_STR_START_LIMIT_RUNS);
    }
    if (!servo_test_drained(&s.test) || !servo_limrun_drained(&s.lim)
        || !servo_rauto_drained(&s.ra.run) || s.logger_busy) {
        return ui_servo_str(SERVO_STR_START_BUSY);
    }
    const char *meter = lim_meter_why();
    if (meter != NULL) {
        return meter;
    }
    if (!lim_reads()) {
        return TR(RA_PROG_PORT);
    }
    if (prog_choice_needs_5v()) {
        return ui_servo_str(SERVO_STR_START_PROG_5V);
    }
    if (!s.drive_pwm) {
        return TR(SV_LIM_PWM_OFF);
    }
    /* The end pulses lie inside the bench's own range. */
    if (s.min_us > 1000u || s.max_us < 2000u
        || !(s.centre_us > 1000u && s.centre_us < 2000u)) {
        return TR(RA_RANGE);
    }
    /* The programmer free, and a written value able to act: a PD mini, or
     * the carrier's PROG 5V, switches the servo off and on. */
    const char *prog = lim_prog_why();
    if (prog != NULL) {
        return prog;
    }
    const char *wr = lim_write_why();
    if (wr != NULL) {
        return wr;
    }
    float d = 0.0f;
    if (!rudder_defl(&d)) {
        return TR(RA_NO_ANGLE);
    }
    if (!s.rud.geo.zeroed) {
        return TR(RA_NO_ZERO);
    }
    return NULL;
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
    kst_prog_msg_id_t m = KST_PROG_MSG_NONE;
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
        m = kst_panel_run_connect(
            lim_pd_feeds(),
            (uint16_t)lroundf(supply_screen_set_v() * 1000.0f),
            (uint16_t)lroundf(supply_screen_set_i() * 1000.0f));
    } else if (d->write) {
        m = kst_panel_run_write(d->value[0], d->value[1]);
    } else if (d->pulses) {
        m = kst_panel_run_pulses(d->pulse_dus[0], d->pulse_dus[1]);
    } else if (d->leave) {
        m = kst_panel_run_leave();
        s.ra.leaving = m == KST_PROG_MSG_NONE
                       && !servo_rauto_running(&s.ra.run);
    }
    if (m != KST_PROG_MSG_NONE) {
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
        const servo_rauto_in_t in = {
            .live        = value_live(),
            .safe        = !bench_arming() && !s.gate.armed,
            .angle_valid = have,
            .angle_deg   = deg,
            .still_ms    = have ? s.ra.still_ms : 0u,
        };
        servo_rauto_do_t d;
        servo_rauto_step(&s.ra.run, test_now(), &in, &d);
        test_tell();
        if (ra_apply(&d)) {
            break;
        }
    }
}

/** @brief The programmer's answer to the step under way. */
static void ra_poll(void)
{
    if (!servo_rauto_programming(&s.ra.run)) {
        return;
    }
    const kst_panel_run_t r = kst_panel_run_poll();
    if (r == KST_PANEL_RUN_NONE || r == KST_PANEL_RUN_BUSY) {
        return;
    }
    const bool ok = r == KST_PANEL_RUN_OK;
    servo_rauto_servo_t sv;
    memset(&sv, 0, sizeof(sv));
    unsigned type_no = 0u;
    switch (s.ra.run.phase) {
    case SERVO_RAUTO_PH_READ:
    case SERVO_RAUTO_PH_CONNECT:
        if (!ok) {
            if (kst_panel_run_no_servo()) {
                servo_rauto_no_servo(&s.ra.run, test_now());
            } else {
                servo_rauto_refused(&s.ra.run, test_now());
            }
            break;
        }
        sv.ok = kst_panel_run_servo(sv.value, &sv.lo, &sv.hi, &sv.reversion,
                                    &type_no, sv.pulse_dus);
        servo_rauto_connected(&s.ra.run, &sv, test_now());
        break;
    case SERVO_RAUTO_PH_WRITE:
        if (ok) {
            (void)kst_panel_run_servo(sv.value, &sv.lo, &sv.hi,
                                      &sv.reversion, &type_no, sv.pulse_dus);
        }
        /* The file saved before the run's first write. */
        servo_rauto_backup(&s.ra.run, kst_panel_run_backup());
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

/** @brief A run that was running is over: the programmer's own supply
 *         values come back once its port is. */
static void ra_ended(void)
{
    if (!s.ra.seen || servo_rauto_running(&s.ra.run)) {
        return;
    }
    s.ra.seen = false;
    if (s.ra.leaving) {
        s.ra.end_due = true;
    } else {
        kst_panel_run_end();
    }
    test_tell();
    ++s.ctrl_rev;
}

void rauto_leave_follow(void)
{
    if (s.ra.leaving
        && kst_prog_state(kst_panel_model()) != KST_PROG_LEAVING) {
        s.ra.leaving = false;
    }
    if (s.ra.end_due && !s.ra.leaving) {
        s.ra.end_due = false;
        kst_panel_run_end();
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

/** @brief START's hold completed: the run starts. */
static void ra_begin(void)
{
    ui_hold_reset(&s.ra.hold);
    s.ra.down = false;
    s.ra.note = NULL;
    ++s.ctrl_rev;
    if (rauto_blocked() != NULL) {
        return;
    }
    servo_rauto_cfg_t c;
    ra_cfg(&c);
    if (!servo_rauto_start(&s.ra.run, &c, test_now())) {
        s.ra.note = TR(RA_REFUSED);
        return;
    }
    s.ra.changes   = s.changes;
    s.ra.sup_watch = false;
    kst_panel_run_begin();
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
    if (kst_panel_is_open() && !kst_panel_holds_port()) {
        kst_panel_close();
    }
    servo_invalidate();
}

void rauto_frame(float dt_s)
{
    if (s.ra.down) {
        ++s.ctrl_rev;
        if (ui_hold_tick(&s.ra.hold, dt_s)) {
            ra_begin();
        }
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
        if (s.ra.pressed != RA_C_NONE) {
            return true;
        }
        if (k != RA_C_START && servo_rauto_running(&s.ra.run)) {
            return true;            /* the rows wait for the run's end */
        }
        s.ra.pressed  = (uint8_t)k;
        s.ra.press_id = evt->point.id;
        if (k == RA_C_START) {
            s.ra.note = NULL;
            if (rauto_blocked() == NULL) {
                s.ra.down = true;
                ui_hold_begin(&s.ra.hold, TX_HOLD_HINT_RUDDER_AUTO);
            }
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
        }
        s.ra.down = false;
        return true;
    }
    if (touch_event_is_tap_up(evt) && rauto_shown() && over) {
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
    }
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
        snprintf(b, n, TR(RA_BINDS_AT), (double)r->bind_deg);
    } else {
        snprintf(b, n, "%s", ui_servo_str(servo_rauto_why_str(r)));
    }
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
        const char *why = s.ra.note != NULL ? s.ra.note : rauto_blocked();
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
            break;
        }
        if (servo_rauto_restore(r)) {
            snprintf(buf, n, TR(SV_LIM_RESTORE), TR(KP_PROGRAM));
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
    gfx_color_t fill = ui_theme_color(UI_C_OK);
    if (running) {
        fill = ui_theme_color(UI_C_DANGER);
    } else if (s.ra.hold.held_s > 0.0f) {
        fill = ui_hold_fill(fill, ui_theme_color(UI_C_DANGER),
                            s.ra.hold.held_s);
    }
    ui_button_font(c, rauto_rect(RA_C_START),
                   running ? TR(SV_LIM_STOP) : TR(RA_START), fill,
                   s.ra.pressed == RA_C_START,
                   running || rauto_blocked() == NULL, SV_FONT_LABEL);
    const gfx_rect_t lr = rauto_lines_rect();
    gfx_fill_rect(c, lr.x, lr.y, lr.w, lr.h, ui_theme_color(UI_C_PANEL));
    const servo_rauto_t *r = &s.ra.run;
    for (int i = 0; i < RA_LINES; ++i) {
        char line[96];
        if (rauto_line(i, line, sizeof(line))[0] == '\0') {
            continue;
        }
        gfx_color_t col = ui_theme_color(UI_C_TEXT_DIM);
        if (i == 0 && !running && (s.ra.note != NULL
                                   || rauto_blocked() != NULL)) {
            col = ui_theme_color(UI_C_WARN);
        } else if (i == 1) {
            col = r->verdict == SERVO_RAUTO_PASS ? ui_theme_color(UI_C_OK)
                  : r->verdict == SERVO_RAUTO_FAIL
                      ? ui_theme_color(UI_C_DANGER)
                      : ui_theme_color(UI_C_WARN);
        } else if (i == 2 && servo_rauto_restore(r)) {
            col = ui_theme_color(UI_C_WARN);
        }
        /* A limit run's refusal (lim_prog_why()) can run past 50 cells:
         * the 8 x 16 face takes 62. */
        const gfx_font_t *face = ui_button_face(line, RA_W, 20, 0,
                                                SV_FONT_LABEL,
                                                &gfx_font_8x16);
        gfx_text_in(c,
                    (gfx_rect_t){ RA_X, (int16_t)(RA_LINE_Y + i * RA_LINE_P),
                                  RA_W, 20 },
                    line, face, col, 1, GFX_ALIGN_LEFT);
    }
}

uint32_t rauto_sig(void)
{
    const servo_rauto_t *r = &s.ra.run;
    const uint32_t v[] = {
        s.ra.view ? 1u : 0u, (uint32_t)r->state, (uint32_t)r->phase,
        (uint32_t)r->side, (uint32_t)r->writes[0], (uint32_t)r->writes[1],
        r->sessions, (uint32_t)r->verdict, (uint32_t)r->why,
        (uint32_t)(s.ra.file + 2), s.ra.off ? 1u : 0u, s.ra.pressed,
        (uint32_t)lroundf(s.ra.hold.held_s * 20.0f),
        (uint32_t)(uintptr_t)rauto_blocked(), (uint32_t)(uintptr_t)s.ra.note,
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
