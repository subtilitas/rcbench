/**
 * @file
 * @brief TEST's gate, the rows of its READY card and its HOLD 2s: ARM +
 *        START, one table of checks for the three.
 *
 * The gate is a list of checks, each a part of what a run asks of the
 * bench (check()); test_why() is the first one's text that is not NULL.
 * The card evaluates every check, each on its own, and shows on a row the
 * first text that belongs to it (test_row_of()), so every text of the
 * gate is on exactly one row:
 *
 *   - PORT: OUTPUTS, or a PROG PORT off the rbmod carrier, and its pulse.
 *     The carrier's ports run at a fixed 5V and take no steps;
 *   - METER: the INA3221 channel, the PD mini or the panel's model, the
 *     meter the run reads to its end.  On the PD mini a tap opens SETUP >
 *     SENSORS on the INA3221 (wb_open_meter());
 *   - PD MINI: its output and SUPPLY's SET A, the current limit the steps
 *     set (`ON 2.00A`), a tap SET A's keypad (rcur_edit()); answering,
 *     and not beside a rail fed from elsewhere: with
 *     its output off an INA3221 window that reads the rail at 3.0V or
 *     more (SERVO_LIMRUN_RAIL_MIN_MV) is fed by another supply, which the
 *     steps would switch the PD mini on beside;
 *   - STEPS: a step or the brown-out walk chosen, each step inside
 *     SUPPLY's caps.  A step past 6.0V reads `TAP: HV WARNING`: the hold
 *     is then a tap that opens the HV warning, whose own 2s hold fires;
 *   - RANGE: the run's ends either side of PULSE CENTER;
 *   - ARM: ARMED, no limit run or AUTO under way, no report on its way.
 *
 * The hold fixes NOT ARMED and a port's PORT PWM off on a SAFE bench
 * itself (amber, `START ARMS`, `START SWITCHES IT ON`); every other text
 * is red and refuses it (test_hold_blocked()).  The PD mini's output is
 * the run's to switch, at each step's voltage: its row reads `START
 * SWITCHES IT ON` while it is off.
 *
 * When the hold fires the run's settings are read (test_cfg()) and the
 * steps begin (servo_start_seq.h): PORT PWM on a port, ARM, ARMED within
 * 500ms, then the run on those settings with the meter and the angle
 * source read again.  A failed step switches every output off and names
 * itself.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "link_pages.h"
#include "prog_choice.h"
#include "prog_registry.h"
#include "sensors_rows.h"
#include "servo_angle_pick.h"
#include "servo_limrun.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_plot.h"
#include "ui_setlist.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_core_int.h"

/* ------------------------------------------------------------ the checks */

/** @brief The gate's checks, in test_why()'s order. */
enum {
    CK_NOT_ARMED = 0, /**< not ARMED                                   */
    CK_LIMIT,         /**< a limit run runs                            */
    CK_AUTO,          /**< AUTO runs                                   */
    CK_CARRIER,       /**< the rbmod carrier's PROG PORT: 5V fixed     */
    CK_PWM,           /**< a port's pulse off, or a session holding it */
    CK_BUSY,          /**< a report still on its way to the card       */
    CK_NO_STEPS,      /**< no step and no brown-out walk               */
    CK_NO_SUPPLY,     /**< no PD mini answering                        */
    CK_FED,           /**< the rail live with the PD mini's output off */
    CK_ENDS,          /**< the ends not either side of PULSE CENTER    */
    CK_CAP,           /**< a step outside SUPPLY's caps                */
    CK_COUNT          /**< the count                                   */
};

/** @brief Whether the tester drives a PROG PORT.  @return true then */
static bool port_target(void)
{
    return s.drive_target >= 1u && s.drive_target <= LINK_PG_PORTS_MAX;
}

/** @brief Whether a PD mini answers that the steps set: not the panel's
 *         model.  @return true then */
static bool pd_real(void)
{
    return s.have_sup && s.sup.online && !supply_screen_model();
}

/**
 * @brief Whether the servo rail is fed from elsewhere: the PD mini's
 *        output off, and the INA3221, the rail's meter, reads it at
 *        SERVO_LIMRUN_RAIL_MIN_MV or more.
 * @return true then; false without a window
 */
static bool fed_elsewhere(void)
{
    return pd_real() && !s.sup.output && s.source == SERVO_SOURCE_INA3221
           && s.have_win && s.win.voltage
           && s.win.mean_mv >= (uint16_t)SERVO_LIMRUN_RAIL_MIN_MV;
}

/**
 * @brief Check @p k: why a run cannot start now on its account.
 * @param k the check
 * @return the text; NULL when the check is met
 */
static const char *check(int k)
{
    switch (k) {
    case CK_NOT_ARMED:
        return !value_live() ? ui_servo_str(SERVO_STR_START_NOT_ARMED)
                             : NULL;
    case CK_LIMIT:
        return servo_limrun_running(&s.lim)
                   ? ui_servo_str(SERVO_STR_START_LIMIT_RUNS) : NULL;
    case CK_AUTO:
        return servo_rauto_running(&s.ra.run)
                   ? ui_servo_str(SERVO_STR_START_AUTO_RUNS) : NULL;
    case CK_CARRIER:
        /* The rbmod carrier's programming port: the steps' voltages reach
         * the servo rails, not the port, whatever its PROG 5V reads. */
        return (prog_choice_carrier() && prog_choice_port() != 0u)
                   ? ui_servo_str(SERVO_STR_START_PROG_STEPS) : NULL;
    case CK_PWM:
        /* A port off the carrier: its pulse, which a programmer holding
         * the port takes away too. */
        return (port_target() && !prog_choice_carrier()
                && (!s.drive_pwm || prog_reg_servo_holds()))
                   ? lim_pwm_why() : NULL;
    case CK_BUSY:
        return (!servo_test_drained(&s.test) || !servo_limrun_drained(&s.lim)
                || !servo_rauto_drained(&s.ra.run) || s.logger_busy)
                   ? ui_servo_str(SERVO_STR_START_BUSY) : NULL;
    case CK_NO_STEPS:
        return (test_steps(NULL) == 0u
                && !settings_get_bool(SET_SERVO_BROWNOUT))
                   ? ui_servo_str(SERVO_STR_START_NO_STEPS) : NULL;
    case CK_NO_SUPPLY:
        return (!s.have_sup || !s.sup.online)
                   ? ui_servo_str(SERVO_STR_START_NO_SUPPLY) : NULL;
    case CK_FED:
        return fed_elsewhere() ? TR(TST_RAIL_FED) : NULL;
    case CK_ENDS: {
        uint16_t lo, hi;
        test_ends(&lo, &hi);
        return !(lo < s.center_us && s.center_us < hi)
                   ? ui_servo_str(SERVO_STR_START_BAD_ENDS) : NULL;
    }
    case CK_CAP: {
        /* A step the supply's caps do not reach: refused here, before a
         * warning names a voltage the run would not use. */
        float v[SERVO_TEST_STEPS_MAX];
        const uint8_t n = test_steps(v);
        const supply_caps_t caps = supply_screen_caps();
        for (uint8_t i = 0; i < n; ++i) {
            if (v[i] > caps.v_max + 0.001f || v[i] < caps.v_min - 0.001f) {
                return ui_servo_str(SERVO_STR_START_ABOVE_CAP);
            }
        }
        return NULL;
    }
    default:
        return NULL;
    }
}

const char *test_why(void)
{
    for (int k = 0; k < CK_COUNT; ++k) {
        const char *why = check(k);
        if (why != NULL) {
            return why;
        }
    }
    return NULL;
}

/* -------------------------------------------------------------- the rows */

test_row_t test_row_of(const char *why)
{
    if (why == NULL) {
        return TRR_COUNT;
    }
    /* Each text of the gate, by the row it belongs to.  A text is the
     * table's own string, so the pointers compare. */
    const char *const port[] = {
        ui_servo_str(SERVO_STR_START_PROG_STEPS), TR(OUT_PORT_HELD),
        TR(SV_LIM_PWM_SAFE), TR(SV_LIM_PWM_OFF), TR(SV_LIM_PWM_OFF_ARMED),
        ui_servo_str(SERVO_STR_START_PROG_5V),
    };
    const char *const pd[] = {
        ui_servo_str(SERVO_STR_START_NO_SUPPLY), TR(TST_RAIL_FED),
    };
    const char *const steps[] = {
        ui_servo_str(SERVO_STR_START_NO_STEPS),
        ui_servo_str(SERVO_STR_START_ABOVE_CAP),
    };
    const char *const arm[] = {
        ui_servo_str(SERVO_STR_START_NOT_ARMED),
        ui_servo_str(SERVO_STR_START_LIMIT_RUNS),
        ui_servo_str(SERVO_STR_START_AUTO_RUNS),
        ui_servo_str(SERVO_STR_START_BUSY),
    };
    const struct {
        const char *const *t;
        size_t n;
        test_row_t row;
    } k_own[] = {
        { port, sizeof(port) / sizeof(port[0]), TRR_PORT },
        { pd, sizeof(pd) / sizeof(pd[0]), TRR_PD },
        { steps, sizeof(steps) / sizeof(steps[0]), TRR_STEPS },
        { arm, sizeof(arm) / sizeof(arm[0]), TRR_ARM },
    };
    for (size_t g = 0; g < sizeof(k_own) / sizeof(k_own[0]); ++g) {
        for (size_t i = 0; i < k_own[g].n; ++i) {
            if (why == k_own[g].t[i]) {
                return k_own[g].row;
            }
        }
    }
    if (why == ui_servo_str(SERVO_STR_START_BAD_ENDS)) {
        return TRR_RANGE;
    }
    return TRR_COUNT;
}

/** @brief Whether the bench is SAFE and nothing arms it: the parts the
 *         hold switches are taken only then. */
static bool safe_now(void)
{
    return !s.gate.armed && !s.gate.arm_asked && !bench_arming();
}

/**
 * @brief Whether the hold fixes @p why itself: NOT ARMED, and on a SAFE
 *        bench a port's PORT PWM off or its pulse waiting for SAFE.
 * @param why a gate text
 * @return true for amber
 */
static bool seq_fixes(const char *why)
{
    if (why == NULL) {
        return false;
    }
    if (why == ui_servo_str(SERVO_STR_START_NOT_ARMED)) {
        return true;
    }
    if (!safe_now()) {
        return false;
    }
    if (why == TR(SV_LIM_PWM_OFF)) {
        return !target_locked_by_bench(SET_SERVO_PORT_PWM);
    }
    /* The pulse comes on while SAFE: the read-back waits for it. */
    return why == TR(SV_LIM_PWM_SAFE);
}

const char *test_hold_blocked(test_row_t *row)
{
    for (int k = 0; k < CK_COUNT; ++k) {
        const char *why = check(k);
        if (why != NULL && !seq_fixes(why)) {
            if (row != NULL) {
                *row = test_row_of(why);
            }
            return why;
        }
    }
    return NULL;
}

/**
 * @brief Whether a tap on METER opens SETUP > SENSORS: no run under way,
 *        and one started now would read the PD mini, not the INA3221 nor
 *        the panel's model.
 * @return true then
 */
static bool meter_opens(void)
{
    return !servo_test_running(&s.test) && !test_on_ina_now()
           && !supply_screen_model();
}

/** @brief Each row's label; PD MINI and ARM are the bench's own words. */
static const char *row_label(int i)
{
    switch ((test_row_t)i) {
    case TRR_PORT:  return TR(SVH_PORT);
    case TRR_METER: return TR(RDY_METER);
    case TRR_ANGLE: return TR(RDY_ANGLE);
    case TRR_PD:    return "PD MINI";
    case TRR_STEPS: return TR(TST_STEPS);
    case TRR_RANGE: return TR(RDY_RANGE);
    default:        return "ARM";
    }
}

/**
 * @brief A met row's value: what it holds now, or as the run under way
 *        started.
 * @param i the row
 * @param b where it goes
 * @param n its size
 * @param st receives RDY_LIVE where an output is live through it
 */
static void met_value(int i, char *b, size_t n, uint8_t *st)
{
    const bool running = servo_test_running(&s.test);
    const servo_test_cfg_t *rc = &s.test.cfg;
    b[0] = '\0';
    switch ((test_row_t)i) {
    case TRR_PORT:
        (void)ready_port_line(b, n);
        break;
    case TRR_METER:
        if (running ? rc->meter.kind == (uint8_t)SERVO_TEST_METER_INA3221
                    : test_on_ina_now()) {
            if (running) {
                snprintf(b, n, "%s", rc->meter.name);
            } else {
                (void)sensors_rows_meter(b, n);
            }
        } else if (running ? rc->model : supply_screen_model()) {
            snprintf(b, n, "%s", TR(SV_METER_MODEL));
        } else {
            snprintf(b, n, "PD MINI");
        }
        break;
    case TRR_PD: {
        /* The output and SET A, the limit the steps set: the run's own
         * while it runs. */
        char a[16];
        rcur_amps(a, sizeof(a), running ? rc->i_limit
                                        : supply_screen_set_i());
        snprintf(b, n, "%s %s",
                 supply_screen_model() ? TR(SV_METER_MODEL)
                                       : ui_on_off(s.sup.output),
                 a);
        if (s.sup.output) {
            *st = (uint8_t)RDY_LIVE;
        }
        break;
    }
    case TRR_STEPS:
        if (running) {
            test_steps_text(b, n, rc->steps_v, rc->step_count,
                            rc->brownout);
        } else {
            float v[SERVO_TEST_STEPS_MAX];
            const uint8_t k = test_steps(v);
            test_steps_text(b, n, v, k,
                            settings_get_bool(SET_SERVO_BROWNOUT));
        }
        break;
    case TRR_RANGE: {
        uint16_t lo = rc->end_lo_us, hi = rc->end_hi_us;
        if (!running) {
            test_ends(&lo, &hi);
        }
        snprintf(b, n, "%u-%uus", (unsigned)lo, (unsigned)hi);
        break;
    }
    default:
        snprintf(b, n, "ARMED");
        *st = (uint8_t)RDY_LIVE;
        break;
    }
}

bool test_ready_row(int i, rdy_row_t *r)
{
    if (r == NULL || i < 0 || i >= (int)TRR_COUNT) {
        return false;
    }
    memset(r, 0, sizeof(*r));
    r->label = row_label(i);
    if (i == TRR_ANGLE) {
        /* The source the run reads, as it started while it runs; no check
         * of the gate belongs to it. */
        const servo_test_cfg_t *rc = &s.test.cfg;
        char name[24];
        const bool running = servo_test_running(&s.test);
        angle_pick_run_row(r, running ? servo_test_enc_source(rc)
                                      : enc_name(name, sizeof(name)),
                           running ? rc->enc_on : enc_on_now());
        return true;
    }
    /* The row's first text that refuses the hold, else its first the hold
     * does: an amber text never hides a red one of the same row. */
    const char *bad = NULL, *seq = NULL;
    for (int k = 0; k < CK_COUNT; ++k) {
        const char *why = check(k);
        if (why == NULL || test_row_of(why) != (test_row_t)i) {
            continue;
        }
        if (!seq_fixes(why)) {
            if (bad == NULL) {
                bad = why;
            }
        } else if (seq == NULL) {
            seq = why;
        }
    }
    const bool running = servo_test_running(&s.test);
    /* A run under way: its rows as it started, the ARM row its own. */
    const char *mine = running ? NULL : (bad != NULL) ? bad : seq;
    if (mine != NULL && seq_fixes(mine)) {
        r->st  = (uint8_t)RDY_SEQ;
        r->why = mine;
        snprintf(r->value, sizeof(r->value), "%s",
                 (i == TRR_ARM) ? (s.gate.arm_asked ? TR(RDY_ARMING)
                                                    : TR(RDY_SEQ_ARM))
                                : TR(RDY_SEQ_ON));
    } else if (mine != NULL) {
        /* The whole text for the band, its tag on the row. */
        r->st = (uint8_t)RDY_BAD;
        ready_set_why(r, mine);
    } else if (!running && i == TRR_PD && pd_real() && !s.sup.output) {
        /* The run's first step switches it on at its own voltage, at
         * SET A. */
        r->st = (uint8_t)RDY_SEQ;
        rcur_seq(r->value, sizeof(r->value), true);
    } else if (!running && i == TRR_STEPS && test_needs_hv()) {
        /* A step past 6.0V: the HV warning's hold starts it. */
        r->st = (uint8_t)RDY_SEQ;
        ready_set_why(r, TR(SV_TAP_HV));
    } else {
        r->st = (uint8_t)RDY_OK;
        met_value(i, r->value, sizeof(r->value), &r->st);
    }
    /* The rows a tap fixes: PORT always, STEPS refused or past 6.0V,
     * PD MINI's SET A on its keypad unless a text refuses, METER on SETUP
     * while the run would not read the INA3221 nor the model. */
    r->fix = (i == TRR_PORT)
             || (i == TRR_STEPS && (r->st == (uint8_t)RDY_BAD
                                    || r->why == TR(SV_TAP_HV)))
             || (i == TRR_PD && r->st != (uint8_t)RDY_BAD)
             || (i == TRR_METER && meter_opens());
    return true;
}

void test_ready_tap(int i)
{
    rdy_row_t r;
    if (!test_ready_row(i, &r)) {
        return;
    }
    /* Nothing changes under a run or the steps: the rows as they
     * started. */
    if (servo_test_running(&s.test) || svs_seq_busy()) {
        return;
    }
    if (i == TRR_PORT) {
        port_list_open();
        return;
    }
    if (i == TRR_ANGLE) {
        angle_pick_open();
        return;
    }
    if (i == TRR_PD && r.fix) {
        rcur_edit();                /* SET A, the steps' current limit */
        return;
    }
    if (i == TRR_METER && r.fix) {
        wb_open_meter();            /* SETUP > SENSORS, the INA3221 */
        return;
    }
    if (i == TRR_STEPS && r.fix) {
        if (svs_press_held()) {
            ui_action_refused(TR(FOOT_PRESS_HELD));
        } else if (r.why == TR(SV_TAP_HV)) {
            ask_hv_test();
        } else if (svs_list_refused() != NULL) {
            ui_action_refused(svs_list_refused());
        } else {
            svs_list_open();        /* the list at its steps */
        }
        return;
    }
    if (r.why != NULL && r.st != (uint8_t)RDY_SEQ) {
        char line[128];
        snprintf(line, sizeof(line), TR(RDY_BAND), r.label, r.why);
        ui_router_set_alert(line);
    }
}

/* -------------------------------------------------------------- the hold */

/**
 * @brief The run started on the settings the hold read, once the steps
 *        have armed the bench: the gate asked again on the bench now.
 * @param why receives the refusal, for the failure's line
 * @return whether the run took it
 */
static bool start_now(const char **why)
{
    *why = test_why();
    if (*why != NULL) {
        return false;
    }
    servo_test_cfg_t c = s.test_snap;
    test_cfg_bench(&c);
    if (!test_launch(&c)) {
        *why = ui_servo_str((servo_str_t)s.test_note);
        return false;
    }
    return true;
}

/** @brief The steps' frame on TEST, and the run on their last. */
static void seq_step(void)
{
    if (s.fn != SERVO_FN_TEST || !svs_seq_busy()) {
        return;
    }
    if (svs_seq_frame(test_hold_blocked(NULL))) {
        const char *why = NULL;
        const bool ok = start_now(&why);
        svs_seq_started(ok, why);
    }
}

void test_hold_fired(void)
{
    /* The hold that fired is spent, whatever comes of it: a press the
     * overlay let go of never reaches ui_hold_end().  Its flash runs on. */
    ui_hold_spend(&s.test_hold);
    s.test_down = false;
    s.test_note = 0;
    ++s.test_rev;
    ++s.arm_rev;
    ++s.ctrl_rev;
    if (test_hold_blocked(NULL) != NULL || svs_seq_busy()) {
        return;
    }
    /* The settings as they read now: the run starts on these whatever
     * changes while the steps take their time. */
    test_cfg(&s.test_snap);
    close_panels();
    if (svs_seq_begin()) {
        seq_step();             /* an ARMED bench starts at once */
    }
}

bool test_hold_down(const touch_event_t *evt)
{
    if (evt == NULL
        || !gfx_rect_contains(test_start_rect(), evt->point.x,
                              evt->point.y)) {
        return false;
    }
    /* STOP TEST, and a stop of the steps, at once on the press, every
     * output off. */
    if (servo_test_running(&s.test)) {
        test_end_now(SERVO_TEST_AB_OPERATOR);
        return true;
    }
    if (svs_seq_busy()) {
        svs_seq_end(SERVO_TEST_AB_OPERATOR);
        return true;
    }
    if (s.ov_have) {
        return true;                    /* one press at a time */
    }
    ov_take(evt, OP_TEST_START, -1);
    /* A hold, unless a row refuses it (a tap says why) or a step is past
     * 6.0V (a tap opens the HV warning, whose hold starts it). */
    s.test_note = 0;
    if (test_hold_blocked(NULL) == NULL && !test_needs_hv()) {
        s.test_down = true;
        ui_hold_begin(&s.test_hold, TX_HOLD_HINT_TEST);
    }
    ++s.test_rev;
    ++s.arm_rev;
    return true;
}

void test_frame(float dt_s)
{
    if (s.test_down) {
        ++s.test_rev;
        ++s.arm_rev;
        /* A row that turns red under the hold drops it: the hold fires
         * only on a bench that may start. */
        if (test_hold_blocked(NULL) != NULL && ui_hold_leave(&s.test_hold)) {
            s.test_down = false;
        } else if (ui_hold_tick(&s.test_hold, dt_s)) {
            ov_let_go();
            test_hold_fired();
        }
    }
    if (ui_hold_flashing(&s.test_hold)) {
        ui_hold_flash_step(&s.test_hold, dt_s);
        ++s.arm_rev;                    /* the frames of the flash */
    }
    seq_step();
    test_tick();
}

void test_draw_hold(gfx_canvas_t *c)
{
    const bool running = servo_test_running(&s.test);
    const bool seq = svs_seq_busy();
    char label[40];
    ui_action_t now = UI_ACTION_AVAILABLE;
    if (running) {
        snprintf(label, sizeof(label), "%s", TR(SV_STOP_TEST));
        now = UI_ACTION_ACTIVE;
    } else if (seq) {
        (void)svs_seq_label(label, sizeof(label));
        now = UI_ACTION_WAITING;
    } else {
        snprintf(label, sizeof(label), "%s",
                 test_needs_hv() ? TR(SV_TAP_HV) : TR(RA_START));
    }
    /* The fade runs from green to the amber of the steps the hold
     * starts. */
    ui_hold_button(c, test_start_rect(), label, ui_action_fill(now),
                   ui_action_fill(UI_ACTION_WAITING), &s.test_hold,
                   s.ov_have && s.ov_pressed == OP_TEST_START,
                   running || seq || test_hold_blocked(NULL) == NULL,
                   SV_FONT_LABEL);
}

void test_draw_plot(gfx_canvas_t *c)
{
    svs_power_view(c, test_plot_rect());
}
