/*
 * The SERVO screen's limit run (servo_limrun.h).  START LIMIT RUN, on the
 * LIMITS page, starts it.  This file holds what that button checks first,
 * the run's steps done the way a finger does them (the servo's position,
 * ARM and DISARM, SUPPLY's output), and the programmer's sessions with a
 * KST or Chaservo servo on the programming port.  It also draws the run's
 * box on the left card and its lines on the LIMITS page.
 *
 * The screen is servo_screen.c; the state and the layout the files share
 * are servo_screen_int.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "kst_panel.h"
#include "kst_port.h"
#include "log_name.h"
#include "rcbench_version.h"
#include "servo_limrun.h"
#include "settings.h"
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

/* Why the INA3221 is not the meter a run may start on, or NULL.  The limit
 * run reads the INA3221 and nothing else: the PD mini's readings and the
 * model are refused. */
static const char *lim_meter_why(void)
{
    if (!settings_get_bool(SET_INA3221_EN) || s.src_why == SERVO_SOURCE_WHY_OFF
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

/* Why a limit run cannot start now, before any hold, or NULL. */
const char *lim_blocked(void)
{
    if (!value_live()) {
        return ui_servo_str(SERVO_STR_START_NOT_ARMED);
    }
    if (servo_test_running(&s.test) || !servo_test_drained(&s.test)
        || !servo_limrun_drained(&s.lim) || s.logger_busy) {
        return ui_servo_str(SERVO_STR_START_BUSY);
    }
    const char *meter = lim_meter_why();
    if (meter != NULL) {
        return meter;
    }
    /* A powered rail: the window's voltage read, and at least
     * SERVO_LIMRUN_RAIL_MIN_MV. */
    if (!s.win.voltage || s.win.mean_mv < SERVO_LIMRUN_RAIL_MIN_MV) {
        return TR(SV_LIM_RAIL_LOW);
    }
    if (s.drive_target != 0u && !s.drive_pwm) {
        return TR(SV_LIM_PWM_OFF);
    }
    /* The bench's own outputs: a SERVO PWM output bound to carry the
     * pulse, as the TARGET row names it. */
    if (s.drive_target == 0u && (!s.pins_known || s.pins == 0u)) {
        return s.pins_known ? TR(SV_TARGET_NONE) : TR(SV_TARGET_UNREAD);
    }
    if (!(s.min_us < s.centre_us && s.centre_us < s.max_us)) {
        return TR(SV_LIM_REFUSED);
    }
    /* On the programming port every run reads the servo first: the
     * programmer must be free to. */
    if (lim_reads()) {
        return lim_prog_why();
    }
    return NULL;
}

/* Why the programming is not offered, or NULL: only a servo on the
 * programming port, with the port in PWM, and a PD mini on and set where a
 * session can run, or without one a servo rail read at 4.8 V or more. */
const char *lim_prog_why(void)
{
    if (s.drive_target == 0u || s.drive_target == SERVO_SCREEN_TARGET_UNKNOWN
        || s.drive_target == SERVO_SCREEN_TARGET_NO_LINK) {
        return TR(SV_LIM_PROG_PORT);
    }
    if (!servo_limrun_running(&s.lim)
        && (kst_panel_holds_port() || !kst_panel_run_ready())) {
        return TR(SV_LIM_PROG_BUSY);
    }
    /* A PD mini that answers with its output off, on a rail fed from
     * elsewhere: the read session's ENTER would switch it on beside that
     * supply, and the run ends on an output switched on. */
    if (!servo_limrun_running(&s.lim) && lim_pd_answers() && !s.sup.output) {
        return TR(SV_LIM_PROG_PD_OFF);
    }
    if (lim_pd_answers()
        && supply_screen_set_v() * 1000.0f
               < (float)KST_PORT_SUPPLY_MIN_MV - 0.5f) {
        return TR(SV_LIM_PROG_LOW);
    }
    /* No PD mini to set: an external supply, or the model.  The servo
     * rail as the INA3221's last window read it holds the same 4.8 V the
     * coprocessor asks of a PD mini, which it cannot check here.  At the
     * start only: the rail is the run's to watch after it. */
    if (!servo_limrun_running(&s.lim) && !lim_pd_answers()
        && (!s.win.voltage || s.win.mean_mv < KST_PORT_SUPPLY_MIN_MV)) {
        return TR(SV_LIM_PROG_RAIL);
    }
    return NULL;
}

static void lim_cfg(servo_limrun_cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    c->centre_us = s.centre_us;
    c->min_us    = s.min_us;
    c->max_us    = s.max_us;
    c->read       = lim_reads();
    c->program    = c->read && lim_programs();
    c->set_pulses = c->read && s.lim_pulses;
    snprintf(c->dut, sizeof(c->dut), "%s", settings_text(SET_TEXT_DUT_NAME));
    snprintf(c->type, sizeof(c->type), "%s", type()->name);
    c->frame_hz = s.frame_hz;
    (void)servo_target_line(c->output, sizeof(c->output), s.drive_target,
                            s.drive_pin, s.drive_pwm, s.pins, s.pins_known);
    c->shunt_dmohm =
        (uint16_t)lroundf(settings_get(SET_INA3221_MOHM) * 10.0f);
    c->enc_on = settings_get_bool(SET_ENC_EN) && s.enc_held;
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
        supply_screen_ask_off();
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
    kst_prog_msg_id_t m = KST_PROG_MSG_NONE;
    if (d->connect && lim_pd_answers()
        && (!s.sup.output || !s.lim.sup_online)) {
        /* A PD mini the run did not start with, or one whose output is
         * off: the session would switch it on beside the rail's feed.
         * The run ends as a reading outside a session ends it. */
        servo_limrun_supply_refused(&s.lim, s.sup.output, test_now());
        again = true;
    } else if (d->connect) {
        const bool pd = lim_pd_answers();
        m = kst_panel_run_connect(
            pd, (uint16_t)lroundf(supply_screen_set_v() * 1000.0f),
            (uint16_t)lroundf(supply_screen_set_i() * 1000.0f));
    } else if (d->write) {
        m = kst_panel_run_write(d->angle[0], d->angle[1]);
    } else if (d->pulses) {
        m = kst_panel_run_pulses(d->pulse_dus[0], d->pulse_dus[1]);
    } else if (d->leave) {
        m = kst_panel_run_leave();
        s.lim_leaving = m == KST_PROG_MSG_NONE
                        && !servo_limrun_running(&s.lim);
    }
    if (m != KST_PROG_MSG_NONE) {
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
            .enc_valid = enc_shown() && s.enc_held,
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
    const kst_panel_run_t r = kst_panel_run_poll();
    if (r == KST_PANEL_RUN_NONE || r == KST_PANEL_RUN_BUSY) {
        return;
    }
    const bool ok = r == KST_PANEL_RUN_OK;
    servo_limrun_servo_t sv;
    memset(&sv, 0, sizeof(sv));
    unsigned type_no = 0u;
    switch (s.lim.phase) {
    case SERVO_LIMRUN_PH_CONNECT:
        if (!ok) {
            servo_limrun_refused(&s.lim, test_now());
            break;
        }
        sv.ok = kst_panel_run_servo(sv.angle, &sv.lo_raw, &sv.hi_raw,
                                    &sv.reversion, &type_no, sv.pulse_dus);
        sv.type = type_no;
        /* No backup at a CONNECT: the programmer's is an earlier run's
         * until this run's first write (servo_limrun_backup()). */
        servo_limrun_connected(&s.lim, &sv, test_now());
        break;
    case SERVO_LIMRUN_PH_WRITE:
        if (ok) {
            (void)kst_panel_run_servo(sv.angle, &sv.lo_raw, &sv.hi_raw,
                                      &sv.reversion, &type_no, sv.pulse_dus);
        }
        /* The file saved before the run's first write, sent whether it
         * ended well or not. */
        servo_limrun_backup(&s.lim, kst_panel_run_backup());
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
 * back, and the box says how it ended. */
static void lim_ended(void)
{
    if (!s.lim_seen || servo_limrun_running(&s.lim)) {
        return;
    }
    s.lim_seen = false;
    /* A LEAVE on its way is the run's to the end: the programmer's run, and
     * the supply values it keeps, end when the port is back. */
    if (s.lim_leaving) {
        s.lim_end_due = true;
    } else {
        kst_panel_run_end();
    }
    test_tell();
    ++s.test_rev;
    ++s.ctrl_rev;
}

/* The ended run's LEAVE: over once the port has left LEAVING, back in PWM
 * or, refused, still in programming.  Whichever screen is up. */
void lim_leave_follow(void)
{
    if (s.lim_leaving
        && kst_prog_state(kst_panel_model()) != KST_PROG_LEAVING) {
        s.lim_leaving = false;
    }
    if (s.lim_end_due && !s.lim_leaving) {
        s.lim_end_due = false;
        kst_panel_run_end();
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

/* START LIMIT RUN's hold completed: the run starts and the settings close
 * so the left card shows it. */
void lim_begin(void)
{
    ui_hold_reset(&s.lim_hold);
    s.lim_down = false;
    ++s.test_rev;
    s.lim_note = NULL;
    if (lim_blocked() != NULL) {
        ++s.ctrl_rev;
        return;
    }
    servo_limrun_cfg_t c;
    lim_cfg(&c);
    if (!servo_limrun_start(&s.lim, &c, test_now())) {
        s.lim_note = TR(SV_LIM_REFUSED);
        ++s.ctrl_rev;
        return;
    }
    s.lim_changes = s.changes;
    if (c.read) {
        kst_panel_run_begin();
    }
    {
        /* The supply as the run starts: with none read yet, one that does
         * not answer, so a PD mini that comes with its output on ends the
         * run. */
        servo_test_reading_t r;
        memset(&r, 0, sizeof(r));
        if (s.have_sup) {
            r = test_reading_of(&s.sup);
        }
        servo_limrun_supply(&s.lim, &r, test_now());
    }
    test_tell();
    stop_sweep();
    s.lim_seen   = true;
    s.lim_file   = 0;
    s.lim_report = false;
    s.lim_box    = true;
    s.test_box   = false;
    s.ov_open    = false;
    s.kp_alone   = false;
    close_panels();
    if (kst_panel_is_open() && !kst_panel_holds_port()) {
        kst_panel_close();
    }
    servo_invalidate();
    ++s.ctrl_rev;
}

/* The run every frame: the programmer's answers, its timers, and what the
 * box shows. */
uint32_t lim_signature(void)
{
    const servo_limrun_t *r = &s.lim;
    const uint32_t f[] = {
        (uint32_t)r->state, (uint32_t)r->phase, (uint32_t)r->side,
        (uint32_t)r->cmd_us, (uint32_t)r->writes, (uint32_t)(s.lim_file + 2),
        s.lim_report ? 1u : 0u, (uint32_t)(uintptr_t)lim_blocked(),
        (uint32_t)(uintptr_t)lim_prog_why(), (uint32_t)(uintptr_t)s.lim_note,
    };
    uint32_t h = 2166136261u;
    for (size_t k = 0; k < sizeof(f) / sizeof(f[0]); ++k) {
        h = (h ^ f[k]) * 16777619u;
    }
    return h;
}

void lim_tick(void)
{
    lim_poll();
    lim_step();
    lim_ended();
    /* The box and the LIMITS page, where they show. */
    const uint32_t sig = lim_signature();
    if (sig != s.lim_sig) {
        s.lim_sig = sig;
        ++s.test_rev;
        if (s.lim_box || (s.ov_open && s.tabs.selected == PG_LIMITS)) {
            ++s.ctrl_rev;
        }
    }
}

/* ----------------------------------------------------------------- drawing */

/* The limit run's report, as the box and the LIMITS page name it. */
static void lim_file_name(char *b, size_t n)
{
    if (s.lim_file > 0) {
        log_report_name(b, n, s.lim_file);
    } else {
        snprintf(b, n, "%s", (s.lim_file < 0) ? TR(SV_NOT_RECORDED)
                                               : TR(SV_WRITING));
    }
}

/* An end as the box gives it: its endpoint, or FREE. */
static void lim_end_text(char *b, size_t n, const servo_limrun_side_t *e)
{
    if (!e->done) {
        snprintf(b, n, "-");
    } else if (e->bound) {
        snprintf(b, n, "%u us", (unsigned)e->limit_us);
    } else {
        snprintf(b, n, "%s", ui_servo_str(SERVO_STR_L_FREE));
    }
}

/*
 * The limit run on the left card, in the automatic test's place: while it
 * runs, the values written, the phase with the end and the pulse, and the
 * output it drives; after it, the verdict, the ends or the values, and the
 * report.  Its button is STOP RUN while it runs and CLOSE after.
 */
void draw_lim_box(gfx_canvas_t *c)
{
    const gfx_rect_t b = test_box_rect();
    const servo_limrun_t *r = &s.lim;
    const bool running = servo_limrun_running(r);
    gfx_fill_round_rect(c, b.x, b.y, b.w, b.h, 6,
                        ui_theme_color(UI_C_PANEL_SUNK));
    gfx_draw_round_rect(c, b.x, b.y, b.w, b.h, 6, ui_theme_color(UI_C_EDGE));
    char l1[64], l2[64], l3[64];
    gfx_color_t head = ui_theme_color(UI_C_ACCENT);
    gfx_color_t col3 = ui_theme_color(UI_C_TEXT_DIM);
    const char *right = "INA3221";
    char f[LOG_RUN_NAME_MAX + 4] = "";
    if (running) {
        if (r->cfg.program) {
            snprintf(l1, sizeof(l1), TR(SV_LIM_TITLE_PROG), r->writes,
                     (unsigned)SERVO_LIMRUN_WRITES_MAX);
        } else {
            snprintf(l1, sizeof(l1), "%s", TR(SV_LIM_TITLE));
        }
        const char *ph = ui_servo_str(servo_limrun_phase_str(r->phase));
        if (r->phase == SERVO_LIMRUN_PH_SEARCH
            || r->phase == SERVO_LIMRUN_PH_PROBE
            || r->phase == SERVO_LIMRUN_PH_CHECK) {
            snprintf(l2, sizeof(l2), TR(SV_LIM_AT), ph,
                     r->side == SERVO_LIMRUN_LO ? TR(SV_LIM_LOW)
                                                : TR(SV_LIM_HIGH),
                     (unsigned)r->cmd_us);
        } else if (r->read_once) {
            snprintf(l2, sizeof(l2), TR(SV_LIM_VALUES), ph,
                     (unsigned)r->cur[SERVO_LIMRUN_LEFT],
                     (unsigned)r->cur[SERVO_LIMRUN_RIGHT]);
        } else {
            snprintf(l2, sizeof(l2), "%s", ph);
        }
        /* What it drives, in sight the whole run. */
        (void)servo_target_line(l3, sizeof(l3), s.drive_target, s.drive_pin,
                                s.drive_pwm, s.pins, s.pins_known);
    } else {
        const servo_limrun_verdict_t v = r->verdict;
        head = (v == SERVO_LIMRUN_PASS || v == SERVO_LIMRUN_FREE)
                   ? ui_theme_color(UI_C_OK)
               : (v == SERVO_LIMRUN_FAIL) ? ui_theme_color(UI_C_DANGER)
               : (v == SERVO_LIMRUN_ABORTED) ? ui_theme_color(UI_C_WARN)
                                             : ui_theme_color(UI_C_ACCENT);
        snprintf(l1, sizeof(l1), TR(SV_LIM_VERDICT),
                 ui_servo_str(servo_limrun_verdict_str(v)));
        uint16_t lo = 0u, hi = 0u;
        if (v == SERVO_LIMRUN_FAIL || v == SERVO_LIMRUN_ABORTED) {
            snprintf(l2, sizeof(l2), "%s",
                     ui_servo_str(servo_limrun_why_str(r)));
        } else if (v == SERVO_LIMRUN_PASS
                   && servo_limrun_original(r, &lo, &hi)) {
            snprintf(l2, sizeof(l2), TR(SV_LIM_SET), (unsigned)lo,
                     (unsigned)r->cur[SERVO_LIMRUN_LEFT], (unsigned)hi,
                     (unsigned)r->cur[SERVO_LIMRUN_RIGHT]);
        } else {
            char a[24], z[24];
            lim_end_text(a, sizeof(a), &r->first[SERVO_LIMRUN_LO]);
            lim_end_text(z, sizeof(z), &r->first[SERVO_LIMRUN_HI]);
            snprintf(l2, sizeof(l2), TR(SV_LIM_ENDS), a, z);
        }
        if (servo_limrun_restore(r)) {
            /* The values last written stay in the servo; the programmer
             * puts back the ones before the run. */
            snprintf(l3, sizeof(l3), TR(SV_LIM_RESTORE), TR(KP_PROGRAM));
            col3 = ui_theme_color(UI_C_WARN);
        } else if (r->why == SERVO_LIMRUN_WHY_RANGE) {
            /* The servo's pulses and the bench's range, side by side. */
            snprintf(l3, sizeof(l3), TR(SV_LIM_RANGE),
                     (unsigned)(r->pulse_now[SERVO_LIMRUN_P_LOWER] / 10u),
                     (unsigned)((r->pulse_now[SERVO_LIMRUN_P_UPPER] + 9u)
                                / 10u),
                     (unsigned)r->cfg.min_us, (unsigned)r->cfg.max_us);
        } else if (v == SERVO_LIMRUN_FOUND || v == SERVO_LIMRUN_FREE) {
            /* The ends are the line above: the output they were found on
             * under them, as the run named it at its start -- not the
             * target now, which may have changed since. */
            snprintf(l3, sizeof(l3), "%s", r->cfg.output);
        } else {
            char a[24], z[24];
            lim_end_text(a, sizeof(a), r->after[0].done ? &r->after[0]
                                                        : &r->first[0]);
            lim_end_text(z, sizeof(z), r->after[1].done ? &r->after[1]
                                                        : &r->first[1]);
            snprintf(l3, sizeof(l3), TR(SV_LIM_ENDS), a, z);
        }
        lim_file_name(f, sizeof(f));
    }
    gfx_text(c, b.x + 10, b.y + 8, l1, &gfx_font_8x16, head, 1);
    gfx_text(c, b.x + 10, b.y + 28, l2, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT), 1);
    gfx_text(c, b.x + 10, b.y + 48, l3, &gfx_font_8x16, col3, 1);
    /* The report under the run's lines once it has ended. */
    if (!running) {
        gfx_text(c, b.x + 10, b.y + 68, f, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
    }
    ui_button(c, test_btn_rect(), running ? TR(SV_LIM_STOP) : TR(SUP_CLOSE),
              running ? ui_theme_color(UI_C_DANGER)
                      : ui_theme_color(UI_C_PANEL_HI), false, true);
    /* The meter it reads, beside the button. */
    const gfx_rect_t btn = test_btn_rect();
    const int mw = gfx_text_width(&gfx_font_8x16, right, 1);
    gfx_text(c, b.x + b.w - 10 - mw, btn.y + 8, right, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT_DIM), 1);
}

/* START LIMIT RUN, on the LIMITS page where START TEST is on TEST. */
void draw_lim_start(gfx_canvas_t *c)
{
    const bool running = servo_limrun_running(&s.lim);
    gfx_color_t fill = ui_theme_color(UI_C_OK);
    if (running) {
        fill = ui_theme_color(UI_C_DANGER);
    } else if (s.lim_hold.held_s > 0.0f) {
        fill = ui_hold_fill(fill, ui_theme_color(UI_C_DANGER),
                            s.lim_hold.held_s);
    }
    ui_button(c, test_start_rect(),
              running ? TR(SV_LIM_STOP) : TR(SV_LIM_START), fill,
              s.ov_have && s.ov_pressed == OP_LIM_START,
              running || lim_blocked() == NULL);
}

/* Under it, across the card: what the run drives and reads, what it does
 * or why it cannot, and whether it programs. */
void draw_lim_lines(gfx_canvas_t *c)
{
    const int x = OV_X + 10;
    const int y = OV_ROW0 + 7 * OV_PITCH + 2;
    char out[32], line[96];
    (void)servo_target_line(out, sizeof(out), s.drive_target, s.drive_pin,
                            s.drive_pwm, s.pins, s.pins_known);
    snprintf(line, sizeof(line), TR(SV_LIM_LINE), out);
    gfx_text(c, x, y, line, &gfx_font_8x16, ui_theme_color(UI_C_TEXT_DIM), 1);

    gfx_color_t col = ui_theme_color(UI_C_TEXT_FAINT);
    const bool running = servo_limrun_running(&s.lim);
    const char *blocked = running ? NULL : lim_blocked();
    if (blocked != NULL || s.lim_note != NULL) {
        snprintf(line, sizeof(line), "%s",
                 blocked != NULL ? blocked : s.lim_note);
        col = ui_theme_color(UI_C_WARN);
    } else if (running) {
        snprintf(line, sizeof(line), TR(SV_LIM_RUNNING),
                 ui_servo_str(servo_limrun_phase_str(s.lim.phase)));
    } else if (s.lim.state == SERVO_LIMRUN_DONE) {
        char f[LOG_RUN_NAME_MAX + 4];
        lim_file_name(f, sizeof(f));
        snprintf(line, sizeof(line), TR(SV_LAST),
                 ui_servo_str(servo_limrun_verdict_str(s.lim.verdict)), f);
    } else {
        snprintf(line, sizeof(line), "%s", TR(SV_HOLD_START));
    }
    gfx_text(c, x, y + 18, line, &gfx_font_8x16, col, 1);

    const char *why = lim_prog_why();
    const bool prog = running ? s.lim.cfg.program : lim_programs();
    if (why != NULL && !running) {
        /* Why SEARCH+PROG is not offered: the run searches only. */
        gfx_text(c, x, y + 36, why, &gfx_font_8x16,
                 s.lim_prog ? ui_theme_color(UI_C_WARN)
                            : ui_theme_color(UI_C_TEXT_DIM), 1);
    } else {
        const bool pulses = running ? s.lim.cfg.set_pulses
                                    : (lim_reads() && s.lim_pulses);
        gfx_text(c, x, y + 36,
                 prog     ? TR(SV_LIM_PROG_NOTE)
                 : pulses ? TR(SV_LIM_PULSES_NOTE)
                          : TR(SV_LIM_SEARCH_NOTE),
                 &gfx_font_8x16, ui_theme_color(UI_C_TEXT_FAINT), 1);
    }
}
