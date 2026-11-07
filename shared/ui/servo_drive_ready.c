/**
 * @file
 * @brief LIVE, SWEEP and RUDDER on the function screens' frame: the READY
 *        rows over the output card, ARM that switches the servo's supply
 *        on first, and the card's controls under the rows.
 *
 * The output card, in the screen's coordinates x 636 to 1016, y 62 to 436:
 *
 *   - READY and its rows, 30px each from y 96 (servo_ready.c draws them):
 *     PORT, SUPPLY and ARM on LIVE and SWEEP; PORT, ANGLE, ZERO, SUPPLY
 *     and ARM on RUDDER;
 *   - on LIVE and SWEEP SPEED from y 196, its track at y 220 to 260;
 *   - the type pill, y 274 to 318;
 *   - on LIVE CENTRE and RELEASE, on SWEEP SWEEP and RELEASE, y 330 to
 *     378, each half the column;
 *   - the rail line at y 404: the servo rail's meter, its volts and amps.
 *
 * The SUPPLY row names what feeds the servo: PROG 5V on the rbmod
 * carrier's port, PD MINI where a PD mini answers, SUPPLY where none does.
 * Off on a SAFE bench it reads `ARM SWITCHES IT ON` in amber: ARM's 2s
 * hold then runs the shared steps (servo_start_seq.h) -- PORT PWM on a
 * PROG PORT, the supply, their read-back within 1000ms, ARM, ARMED within
 * 500ms -- instead of ARM alone.  A failed step switches every output off
 * and names itself in the alert band.  Where nothing reads off, the hold
 * arms as before.
 *
 * ARM never switches the PD mini on past a standard servo's 6.0V: that ON
 * is OUTPUT ON's HV warning alone, which a tap on the red row opens.  Nor
 * beside a rail the INA3221 reads at 3.0V or more with the PD mini off:
 * the rail is fed from elsewhere, the row says so and ARM leaves the PD
 * mini off.  Without an INA3221 window on the rail nothing reads it, and
 * ARM switches the PD mini on.
 *
 * DISARM switches every output off: the bench to SAFE, the carrier's PROG
 * 5V and the PD mini's output.  A leave switches to SAFE and the PD mini
 * off; PROG 5V stays on between SERVO's screens and goes off when SERVO is
 * left (servo_screen.c).
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "esc_wire_panel.h"
#include "prog5v_box.h"
#include "prog_choice.h"
#include "prog_registry.h"
#include "servo_limrun.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_slider.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_core_int.h"

/* ------------------------------------------------------------ the layout */

/** @brief SPEED's label row under LIVE's and SWEEP's three rows. */
#define DRV_SPEED_Y  (READY_ROW_Y + 3 * READY_ROW_H + 10)
#define DRV_SLIDER_Y (DRV_SPEED_Y + 24) /**< SPEED's track, px. */
/** @brief The type pill, clear of SPEED's thumb and its shadow. */
#define DRV_TAG_Y    (DRV_SLIDER_Y + SLIDER_H + UI_SLIDER_OVERHANG + 9)
#define DRV_BTN_Y    (DRV_TAG_Y + TAG_H + 12) /**< The buttons' row, px. */
/** @brief A button: half the column, 8px apart. */
#define DRV_BTN_W    ((RC_W - BTN_GAP) / 2)

_Static_assert(DRV_BTN_Y + BTN_H + 8 <= READY_RAIL_Y - 4,
               "the buttons end above the rail line");
_Static_assert(READY_ROW_Y + 5 * READY_ROW_H + 8 <= DRV_TAG_Y,
               "RUDDER's five rows end above the type pill");
_Static_assert(DRV_BTN_Y + BTN_H <= STOP_FLOOR,
               "PAUSE ends above the alert band");

/** @brief LIVE's and SWEEP's rows, top to bottom. */
static const uint8_t k_live_rows[] = { DRR_PORT, DRR_SUPPLY, DRR_ARM };
/** @brief RUDDER's. */
static const uint8_t k_rudder_rows[] = { DRR_PORT, DRR_ANGLE, DRR_ZERO,
                                         DRR_SUPPLY, DRR_ARM };

bool drive_shown(void)
{
    return s.fn == SERVO_FN_LIVE || s.fn == SERVO_FN_SWEEP
           || s.fn == SERVO_FN_RUDDER;
}

/** @brief Whether the function shown is LIVE or SWEEP: the dial's. */
static bool dial_fn(void)
{
    return s.fn == SERVO_FN_LIVE || s.fn == SERVO_FN_SWEEP;
}

/**
 * @brief The rows of the function shown.
 * @param n receives their count
 * @return the table; NULL with @p n 0 off LIVE, SWEEP and RUDDER
 */
static const uint8_t *rows_of(int *n)
{
    if (s.fn == SERVO_FN_RUDDER) {
        *n = (int)(sizeof(k_rudder_rows) / sizeof(k_rudder_rows[0]));
        return k_rudder_rows;
    }
    if (dial_fn()) {
        *n = (int)(sizeof(k_live_rows) / sizeof(k_live_rows[0]));
        return k_live_rows;
    }
    *n = 0;
    return NULL;
}

int drive_ready_count(void)
{
    int n = 0;
    (void)rows_of(&n);
    return n;
}

void drive_layout(void)
{
    const gfx_rect_t none = { 0, 0, 0, 0 };
    if (!drive_shown()) {
        /* KST and HITEC: the output card as it was, with CENTRE, SWEEP
         * and RELEASE a third of the column each under the SET row.  TEST,
         * LIMIT RUN and RUDDER AUTO draw their READY card over it. */
        const int bw = (RC_W - 2 * BTN_GAP) / 3;
        s.centre_btn  = (gfx_rect_t){ RC_X, BTN_Y, (int16_t)bw, BTN_H };
        s.sweep_btn   = (gfx_rect_t){ (int16_t)(RC_X + bw + BTN_GAP), BTN_Y,
                                      (int16_t)bw, BTN_H };
        s.release_btn = (gfx_rect_t){ (int16_t)(RC_X + 2 * (bw + BTN_GAP)),
                                      BTN_Y,
                                      (int16_t)(RC_W - 2 * (bw + BTN_GAP)),
                                      BTN_H };
        s.out_btn     = (gfx_rect_t){ SUP_OUT_X, SUP_Y,
                                      (int16_t)(RC_X + RC_W - SUP_OUT_X),
                                      SUP_H };
        s.speed.track = (gfx_rect_t){ RC_X, SLIDER_Y, RC_W, SLIDER_H };
        return;
    }
    const gfx_rect_t left  = { RC_X, DRV_BTN_Y, DRV_BTN_W, BTN_H };
    const gfx_rect_t right = { (int16_t)(RC_X + DRV_BTN_W + BTN_GAP),
                               DRV_BTN_Y,
                               (int16_t)(RC_W - DRV_BTN_W - BTN_GAP), BTN_H };
    s.centre_btn  = (s.fn == SERVO_FN_LIVE) ? left : none;
    s.sweep_btn   = (s.fn == SERVO_FN_SWEEP) ? left : none;
    s.release_btn = dial_fn() ? right : none;
    s.out_btn     = none;
    s.speed.track = dial_fn() ? (gfx_rect_t){ RC_X, DRV_SLIDER_Y, RC_W,
                                              SLIDER_H }
                              : none;
}

/* ------------------------------------------------------------ the supply */

/** @brief What feeds the servo, as the SUPPLY row names it. */
typedef enum {
    FEED_NONE = 0, /**< nothing the bench switches                 */
    FEED_P5,       /**< the rbmod carrier's PROG 5V, on its port   */
    FEED_PD,       /**< the PD mini, or the panel's model of it    */
} feed_t;

/** @brief What feeds the servo now.  @return the feed */
static feed_t feed_now(void)
{
    if (lim_fixed_supply()) {
        return FEED_P5;
    }
    return (s.have_sup && s.sup.online) ? FEED_PD : FEED_NONE;
}

/** @brief Whether the PD mini answers with its output on.
 *  @return true then */
static bool pd_on(void)
{
    return s.have_sup && s.sup.online && s.sup.output;
}

/**
 * @brief Whether the INA3221's last window reads the servo rail powered
 *        while the PD mini's output is off: at SERVO_LIMRUN_RAIL_MIN_MV
 *        (3.0V) or more, the rail is fed from elsewhere.  Only while the
 *        INA3221 is the rail's meter: a window it left behind is not.
 * @return true then; false without a window
 */
static bool rail_fed(void)
{
    return !pd_on() && s.source == SERVO_SOURCE_INA3221 && s.have_win
           && s.win.voltage
           && s.win.mean_mv >= (uint16_t)SERVO_LIMRUN_RAIL_MIN_MV;
}

/** @brief Whether the bench is SAFE and nothing arms it: what ARM switches
 *         is switched only then. */
static bool safe_now(void)
{
    return !s.gate.armed && !s.gate.arm_asked && !bench_arming()
           && !svs_seq_busy();
}

servo_seq_need_t drive_seq_need(void)
{
    servo_seq_need_t n;
    memset(&n, 0, sizeof(n));
    const int target = settings_get_int(SET_SERVO_TARGET);
    n.port_pwm = target >= 1 && target <= (int)LINK_PG_PORTS_MAX;
    n.prog_5v  = prog_choice_carrier() && prog_choice_port() != 0u
                 && prog5v_box_shown();
    n.pd_mini  = feed_now() == FEED_PD && !supply_screen_port_held()
                 && (pd_on() || !rail_fed());
    /* Nothing on these screens judges the rail's reading: the steps go on
     * once every part reads ON. */
    n.settle   = false;
    return n;
}

bool drive_switches(void)
{
    if (!drive_shown()) {
        return false;
    }
    const servo_seq_need_t n = drive_seq_need();
    const bool pwm_off = n.port_pwm
                         && (!settings_get_bool(SET_SERVO_PORT_PWM)
                             || !s.drive_pwm || prog_reg_servo_holds());
    return pwm_off || (n.prog_5v && !prog5v_box_on())
           || (n.pd_mini && !pd_on());
}

/* -------------------------------------------------------------- the rows */

/**
 * @brief The PORT row: the port line, amber where ARM switches the port's
 *        pulse on, in the warning colour with no pin, no link or the
 *        pulse off.
 * @param r the row
 */
static void port_row(rdy_row_t *r)
{
    r->label = TR(SVH_PORT);
    r->fix = true;
    const bool warn = svs_port_line(r->value, sizeof(r->value));
    const int target = settings_get_int(SET_SERVO_TARGET);
    const bool port = target >= 1 && target <= (int)LINK_PG_PORTS_MAX;
    if (port && s.drive_target != SERVO_SCREEN_TARGET_NO_LINK
        && s.drive_target != SERVO_SCREEN_TARGET_UNKNOWN) {
        const bool pwm = settings_get_bool(SET_SERVO_PORT_PWM) && s.drive_pwm
                         && !prog_reg_servo_holds();
        if (pwm) {
            r->st = (uint8_t)RDY_OK;
        } else if (safe_now() && !prog_reg_servo_holds()
                   && (settings_get_bool(SET_SERVO_PORT_PWM)
                       || !target_locked_by_bench(SET_SERVO_PORT_PWM))) {
            r->st  = (uint8_t)RDY_SEQ;
            r->why = TR(RDY_PWM_BY_ARM);
        } else {
            r->st  = (uint8_t)RDY_WARN;
            r->why = r->value;
        }
        return;
    }
    r->st  = (uint8_t)(warn ? RDY_WARN : RDY_OK);
    r->why = warn ? r->value : NULL;
}

/**
 * @brief The SUPPLY row: what feeds the servo and whether ARM switches it.
 * @param r the row
 */
static void supply_row(rdy_row_t *r)
{
    switch (feed_now()) {
    case FEED_P5:
        r->label = "PROG 5V";
        if (prog5v_box_on()) {
            r->st = (uint8_t)RDY_LIVE;
            snprintf(r->value, sizeof(r->value), "%s", ui_on_off(1));
        } else if (!prog5v_box_live()) {
            r->st  = (uint8_t)RDY_BAD;
            r->why = TR(P5_LOCKED);
        } else if (safe_now()) {
            r->st  = (uint8_t)RDY_SEQ;
            r->why = TR(RDY_ARM_ON_WHY);
            snprintf(r->value, sizeof(r->value), "%s", TR(RDY_ARM_ON));
        } else {
            r->st = (uint8_t)RDY_WARN;
            snprintf(r->value, sizeof(r->value), "%s", ui_on_off(0));
        }
        break;
    case FEED_PD:
        r->label = "PD MINI";
        if (pd_on()) {
            r->st = (uint8_t)RDY_LIVE;
            snprintf(r->value, sizeof(r->value), "%s %.2fV", ui_on_off(1),
                     (double)supply_screen_set_v());
        } else if (supply_screen_port_held()) {
            /* A programming port's: ARM leaves it to the port, and ARM's
             * own lock names the port. */
            r->st  = (uint8_t)RDY_WARN;
            r->why = esc_wire_panel_holds_port() ? TR(SUP_HELD_REFUSED_ESC)
                                                 : TR(SUP_HELD_REFUSED);
        } else if (rail_fed()) {
            /* Fed from elsewhere: ARM leaves the PD mini off, so its set
             * point, past 6.0V or not, is never applied. */
            r->st  = (uint8_t)RDY_WARN;
            r->why = TR(RDY_FED_WHY);
            snprintf(r->value, sizeof(r->value), "%s", TR(RDY_FED));
        } else if (hv_set_point()) {
            /* Past a standard servo's rating only OUTPUT ON's HV warning
             * switches it on; a tap on the row opens it. */
            r->st  = (uint8_t)RDY_BAD;
            r->why = TR(SV_TAP_HV);
            r->fix = true;
        } else if (safe_now()) {
            r->st  = (uint8_t)RDY_SEQ;
            r->why = TR(RDY_ARM_ON_WHY);
            snprintf(r->value, sizeof(r->value), "%s", TR(RDY_ARM_ON));
        } else {
            r->st = (uint8_t)RDY_WARN;
            snprintf(r->value, sizeof(r->value), "%s", ui_on_off(0));
        }
        break;
    default:
        /* The volts are the rail line's: the row changes only as the rail
         * goes past 3.0V, not with each window. */
        r->label = TR(RDY_SUPPLY);
        if (rail_fed()) {
            r->st = (uint8_t)RDY_OK;
            snprintf(r->value, sizeof(r->value), "%s", TR(RDY_FED));
        } else {
            r->st  = (uint8_t)RDY_WARN;
            r->why = TR(RDY_EXT_WHY);
            snprintf(r->value, sizeof(r->value), "%s", TR(RDY_EXTERNAL));
        }
        break;
    }
    if (r->value[0] == '\0' && r->why != NULL) {
        snprintf(r->value, sizeof(r->value), "%s", r->why);
    }
}

/**
 * @brief The ARM row: ARMED, what keeps ARM from the bench, or the hold.
 * @param r the row
 */
static void arm_row(rdy_row_t *r)
{
    r->label = "ARM";
    if (s.gate.armed) {
        r->st = (uint8_t)RDY_LIVE;
        snprintf(r->value, sizeof(r->value), "ARMED");
        return;
    }
    if (svs_arm_locked()) {
        r->st  = (uint8_t)RDY_BAD;
        r->why = prog_holds() ? prog_reg_hold_alert()
                 : servo_limrun_running(&s.lim)
                     ? ui_servo_str(SERVO_STR_START_LIMIT_RUNS)
                     : ui_servo_str(SERVO_STR_START_AUTO_RUNS);
        snprintf(r->value, sizeof(r->value), "%s", r->why);
        return;
    }
    r->st = (uint8_t)RDY_SEQ;
    snprintf(r->value, sizeof(r->value), "%s",
             (s.gate.arm_asked || svs_seq_busy()) ? TR(RDY_ARMING)
                                                  : TR(RDY_ARM_HOLD));
}

/**
 * @brief The ANGLE row, RUDDER's: the source, or that it reads nothing.  It refuses nothing: RUDDER drives without it.
 * @param r the row
 */
static void angle_row(rdy_row_t *r)
{
    r->label = TR(RDY_ANGLE);
    float d = 0.0f;
    if (rudder_defl(&d)) {
        /* The source alone: the angle is the readout's, and a row that
         * changed with each reading would repaint the rows with it. */
        r->st = (uint8_t)RDY_OK;
        (void)enc_name(r->value, sizeof(r->value));
        return;
    }
    r->st  = (uint8_t)RDY_WARN;
    r->why = TR(RA_NO_ANGLE);
    r->fix = true;
    snprintf(r->value, sizeof(r->value), "%s", r->why);
}

/**
 * @brief The ZERO row, RUDDER's: SET, or the zero to take.  A tap takes
 *        it with a reading on show.
 * @param r the row
 */
static void zero_row(rdy_row_t *r)
{
    r->label = TR(RDY_ZERO);
    if (s.rud.geo.zeroed) {
        r->st = (uint8_t)RDY_OK;
        snprintf(r->value, sizeof(r->value), "%s", TR(RDY_SET));
        return;
    }
    r->st  = (uint8_t)RDY_WARN;
    r->why = TR(RA_NO_ZERO);
    r->fix = enc_shown();
    snprintf(r->value, sizeof(r->value), "%s", r->why);
}

/**
 * @brief Card row @p i's kind.
 * @param i the card row
 * @return the kind; DRR_COUNT past the last
 */
static drive_row_t kind_of(int i)
{
    int n = 0;
    const uint8_t *t = rows_of(&n);
    return (t != NULL && i >= 0 && i < n) ? (drive_row_t)t[i] : DRR_COUNT;
}

bool drive_ready_row(int i, rdy_row_t *r)
{
    const drive_row_t k = kind_of(i);
    if (r == NULL || k == DRR_COUNT) {
        return false;
    }
    memset(r, 0, sizeof(*r));
    switch (k) {
    case DRR_PORT:   port_row(r);   break;
    case DRR_ANGLE:  angle_row(r);  break;
    case DRR_ZERO:   zero_row(r);   break;
    case DRR_SUPPLY: supply_row(r); break;
    default:         arm_row(r);    break;
    }
    return true;
}

const char *drive_hold_blocked(int *row)
{
    const int n = drive_ready_count();
    for (int i = 0; i < n; ++i) {
        rdy_row_t r;
        if (drive_ready_row(i, &r) && r.st == (uint8_t)RDY_BAD) {
            if (row != NULL) {
                *row = i;
            }
            return r.why;
        }
    }
    return NULL;
}

servo_seq_part_t drive_part_of(const char *why)
{
    const int n = drive_ready_count();
    for (int i = 0; i < n && why != NULL; ++i) {
        rdy_row_t r;
        if (!drive_ready_row(i, &r) || r.why != why) {
            continue;
        }
        switch (kind_of(i)) {
        case DRR_SUPPLY:
            return s.rdy.need.prog_5v   ? SERVO_SEQ_PART_PROG_5V
                   : s.rdy.need.pd_mini ? SERVO_SEQ_PART_PD_MINI
                                        : SERVO_SEQ_PART_NONE;
        case DRR_ARM:
            return SERVO_SEQ_PART_ARM;
        default:
            return SERVO_SEQ_PART_NONE;
        }
    }
    return SERVO_SEQ_PART_NONE;
}

void drive_ready_tap(int i)
{
    rdy_row_t r;
    if (!drive_ready_row(i, &r) || svs_seq_busy()) {
        return;                 /* the rows as the steps began */
    }
    switch (kind_of(i)) {
    case DRR_PORT:
        port_list_open();
        return;
    case DRR_ANGLE:
        if (r.fix) {
            wb_open_encoder();
            return;
        }
        break;
    case DRR_ZERO:
        if (r.fix) {
            rudder_zero();
            return;
        }
        break;
    case DRR_SUPPLY:
        /* The PD mini off past 6.0V: OUTPUT ON's HV warning, whose own
         * hold switches it on; ARM never does. */
        if (r.fix && !s.ov_have && !modal_open()) {
            ask_hv_on();
            return;
        }
        break;
    default:
        break;
    }
    if (r.why != NULL) {
        char line[128];
        snprintf(line, sizeof(line), TR(RDY_BAND), r.label, r.why);
        ui_router_set_alert(line);
    }
}

/* --------------------------------------------------------------- ARM */

bool drive_arm_press(const touch_event_t *evt)
{
    if (evt == NULL || !drive_shown()) {
        return false;
    }
    if (svs_seq_busy()) {
        /* The steps stop on the press, every output off. */
        svs_seq_end(SERVO_TEST_AB_OPERATOR);
        ++s.arm_rev;
        return true;
    }
    if (s.gate.armed) {
        /* DISARM acts on the press and switches every output off: the
         * press takes no hold, so a finger left on the button arms
         * nothing when it reads ARM again. */
        svs_all_off();
        ++s.arm_rev;
        return true;
    }
    if (s.arm_down) {
        return true;            /* the contact that began it owns it */
    }
    if (svs_arm_locked()) {
        if (prog_holds()) {
            ui_router_set_alert(prog_reg_hold_alert());
        }
        return true;
    }
    char line[128];
    if (ready_refusal(line, sizeof(line)) != NULL) {
        ui_action_refused(line);
        return true;
    }
    s.arm_down = true;
    s.arm_id   = evt->point.id;
    ui_hold_begin(&s.arm, drive_switches() ? TX_HOLD_HINT_ARM_SUPPLY
                                           : TX_HOLD_HINT_ARM);
    ++s.arm_rev;
    return true;
}

bool drive_arm_fired(void)
{
    if (!drive_shown()) {
        return false;
    }
    if (!drive_switches()) {
        post(SERVO_CMD_ARM, 0);
        return true;
    }
    /* The steps switch the supply, then arm: the press is spent, its
     * flash runs on. */
    ui_hold_spend(&s.arm);
    s.arm_down = false;
    if (drive_hold_blocked(NULL) == NULL) {
        (void)svs_seq_begin();
    }
    ++s.arm_rev;
    ++s.ctrl_rev;
    return true;
}

/** @brief What ARM's button shows, as one number: a change repaints it. */
static uint32_t s_arm_sig;

void drive_frame(float dt_s)
{
    (void)dt_s;
    if (!drive_shown()) {
        return;
    }
    /* A row that turns red under the hold drops it, and the band names
     * it: the hold fires only on a bench that may arm. */
    char line[128];
    if (s.arm_down && !s.gate.armed
        && ready_refusal(line, sizeof(line)) != NULL
        && ui_hold_leave(&s.arm)) {
        s.arm_down = false;
        ui_router_set_alert(prog_holds() ? prog_reg_hold_alert() : line);
        ++s.arm_rev;
    }
    if (svs_seq_busy() && svs_seq_frame(drive_hold_blocked(NULL))) {
        /* The steps' last: ARMED, nothing more to start. */
        svs_seq_started(true, NULL);
    }
    const uint32_t sig = (drive_hold_blocked(NULL) != NULL ? 1u : 0u)
                         | (drive_switches() ? 2u : 0u)
                         | ((uint32_t)servo_seq_step_now(&s.rdy.seq) << 2);
    if (sig != s_arm_sig) {
        s_arm_sig = sig;
        ++s.arm_rev;
    }
}

void drive_draw_arm(gfx_canvas_t *c)
{
    if (svs_seq_busy()) {
        /* The step under way, amber; a press stops it. */
        char label[40];
        (void)svs_seq_label(label, sizeof(label));
        ui_hold_button(c, s.arm_btn, label,
                       ui_action_fill(UI_ACTION_WAITING),
                       ui_action_fill(UI_ACTION_WAITING), &s.arm, false, true,
                       SV_FONT_LABEL);
        return;
    }
    /* The fade runs to the red of ARMED, or to the amber of the steps
     * where the hold switches a part on first. */
    const bool armed = s.gate.armed;
    ui_hold_button(c, s.arm_btn, armed ? "DISARM" : "ARM",
                   ui_action_fill(armed ? UI_ACTION_ACTIVE
                                        : UI_ACTION_AVAILABLE),
                   ui_action_fill(!armed && drive_switches()
                                      ? UI_ACTION_WAITING
                                      : UI_ACTION_ACTIVE),
                   &s.arm, s.arm_down,
                   armed || (!svs_arm_locked()
                             && drive_hold_blocked(NULL) == NULL),
                   SV_FONT_LABEL);
}

/* -------------------------------------------------------- the card */

void drive_render_card(gfx_canvas_t *c)
{
    gfx_fill_rect(c, RCARD_X + 1, RCARD_Y + 1, RCARD_W - 2, RCARD_H - 2,
                  ui_theme_color(UI_C_PANEL));
    ready_render(c);
    if (dial_fn()) {
        /* SPEED's row says when SPEED, not CURVE, decides how a sweep
         * moves: the curves then look alike. */
        char buf[24];
        snprintf(buf, sizeof(buf), "%d%%", s.speed_pct);
        const bool limits = s.fn == SERVO_FN_SWEEP && svs_speed_limits_sweep();
        gfx_text(c, RC_X, DRV_SPEED_Y,
                 limits ? TR(SV_SPEED_LIMITS) : TR(SV_SPEED), SV_FONT_LABEL,
                 limits ? ui_theme_color(UI_C_WARN)
                        : ui_theme_color(UI_C_TEXT_DIM), 1);
        gfx_text_in(c, (gfx_rect_t){ RC_X, DRV_SPEED_Y, RC_W, 20 }, buf,
                    SV_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1,
                    GFX_ALIGN_RIGHT);
        s.speed.color = ui_theme_color(UI_C_ACCENT);
        ui_slider_render(&s.speed, c);
    }
    svs_draw_tag(c, (gfx_rect_t){ RC_X, DRV_TAG_Y, RC_W, TAG_H });
    if (s.fn == SERVO_FN_LIVE) {
        /* CENTRE sets a position: green, dimmed while SAFE. */
        ui_button_font(c, s.centre_btn, TR(SV_CENTRE_BTN),
                       ui_action_fill(UI_ACTION_AVAILABLE), false,
                       value_live(), SV_FONT_LABEL);
    } else if (s.fn == SERVO_FN_SWEEP) {
        /* SWEEP green; PAUSE red while a sweep drives the servo; PAUSED
         * amber while it is paused. */
        ui_button_font(c, s.sweep_btn,
                       s.paused     ? TR(SV_PAUSED)
                       : s.sweeping ? TR(SV_PAUSE) : "SWEEP",
                       ui_action_fill(s.sweeping ? UI_ACTION_ACTIVE
                                      : s.paused ? UI_ACTION_WAITING
                                                 : UI_ACTION_AVAILABLE),
                       false,
                       s.sweeping || s.paused
                           || (value_live() && s.sweep_able && s.surfaces
                               && s.link_up),
                       SV_FONT_LABEL);
    }
    if (dial_fn()) {
        ui_button_font(c, s.release_btn, TR(SV_RELEASE),
                       ui_theme_color(UI_C_PANEL_HI), false, true,
                       SV_FONT_LABEL);
    }
    ready_render_rail(c);
}

gfx_rect_t drive_speed_rect(void)
{
    return dial_fn() ? (gfx_rect_t){ RC_X, DRV_SLIDER_Y, RC_W, SLIDER_H }
                     : (gfx_rect_t){ 0, 0, 0, 0 };
}

gfx_rect_t drive_tag_rect(void)
{
    return (gfx_rect_t){ RC_X, DRV_TAG_Y, RC_W, TAG_H };
}

/* ------------------------------------------------------------ for tests */

bool servo_screen_arm_switches(void)
{
    return drive_switches();
}
