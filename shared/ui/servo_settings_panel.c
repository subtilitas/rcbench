/**
 * @file
 * @brief The SERVO screen's pages of rows and the panels they open.  SETTINGS, a
 * task of the strip, holds three pages under its own tabs -- OUTPUT, SWEEP
 * and DUT: the servo's type, frame rate and pulse widths, the sweep's
 * curve, and the device under test.  Each run card on TEST opens its run's
 * settings as one more page, one level down: the automatic test's steps,
 * length, timing and pass/fail limits, and the limit run's mode.  The rows
 * open the keypad, the keyboard, the list, the profile warning, the HV
 * warning and SUPPLY's question about a live output, all over the left
 * column.  The right card stays where it is and works.
 *
 * The screen is servo_screen.c; the state and the layout both share are
 * servo_screen_int.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "kst_panel.h"
#include "outputs.h"
#include "servo_limrun.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_tabs.h"
#include "ui_textkey.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_int.h"

/**
 * @brief The settings rows, page by page.  SETTINGS' pages in two
 *        columns; the automatic test's in three of five rows: the steps,
 *        the pass/fail limits and the brown-out, the length and timing.
 *        The length rule tops the right column, under TEST's SETTINGS: a
 *        second tap there opens its list and changes nothing.
 */
const ov_row_t k_rows[] = {
    { PG_OUTPUT, R_TYPE,    SETTING_COUNT, TX_SV_ROW_TYPE,  0, 0, true  },
    { PG_OUTPUT, R_RATE,    SETTING_COUNT, TX_SV_ROW_RATE,  0, 1, true  },
    { PG_OUTPUT, R_MIN,     SETTING_COUNT, TX_SV_ROW_MIN,  0, 2, false },
    { PG_OUTPUT, R_CENTRE,  SETTING_COUNT, TX_SV_ROW_CENTRE,  1, 2, false },
    { PG_OUTPUT, R_MAX,     SETTING_COUNT, TX_SV_ROW_MAX,  0, 3, false },
    { PG_OUTPUT, R_TRIM,    SETTING_COUNT, TX_SV_ROW_TRIM,  1, 3, false },
    { PG_OUTPUT, R_TRAVEL,  SETTING_COUNT, TX_SV_ROW_TRAVEL,  0, 4, false },
    { PG_OUTPUT, R_REVERSE, SETTING_COUNT, TX_SV_ROW_REVERSE,  1, 4, false },

    { PG_SWEEP, R_SETTING, SET_SERVO_CURVE,      TX_SV_ROW_CURVE,  0, 0, false },
    { PG_SWEEP, R_SETTING, SET_SERVO_TEST_HZ,    TX_SV_ROW_SPEED,  0, 1, false },
    { PG_SWEEP, R_SETTING, SET_SERVO_TEST_RANGE, TX_SV_ROW_RANGE,  0, 2, false },

    { PG_DUT, R_TEXT,    SETTING_COUNT,    TX_SV_ROW_NAME,  0, 0, true  },
    { PG_DUT, R_SETTING, SET_SERVO_REPORT, TX_SV_ROW_REPORT,  0, 1, false },
    { PG_DUT, R_ENC_CENTRE, SETTING_COUNT, TX_SV_ROW_ENC_CENTRE,  0, 2, false },

    { PG_AUTO, R_SETTING, SET_SERVO_LEN_BY,        TX_SV_ROW_LEN_BY,  2, 0, false },
    { PG_AUTO, R_SETTING, SET_SERVO_LEN_S,         TX_SV_ROW_LEN_S,  2, 1, false },
    { PG_AUTO, R_SETTING, SET_SERVO_LEN_MOVES,     TX_SV_ROW_MOVES,  2, 2, false },
    { PG_AUTO, R_SETTING, SET_SERVO_DWELL_MS,      TX_SV_ROW_DWELL,  2, 3, false },
    { PG_AUTO, R_SETTING, SET_SERVO_SETTLE_MS,     TX_SV_ROW_SETTLE,  2, 4, false },
    { PG_AUTO, R_SETTING, SET_SERVO_STALL_A,       TX_SV_ROW_STALL,  1, 0, false },
    { PG_AUTO, R_SETTING, SET_SERVO_IDLE_MAX,      TX_SV_ROW_IDLE,  1, 1, false },
    { PG_AUTO, R_SETTING, SET_SERVO_HOLD_MAX,      TX_SV_ROW_HOLD,  1, 2, false },
    { PG_AUTO, R_SETTING, SET_SERVO_STEP_48,       TX_SV_ROW_STEP_48,  0, 0, false },
    { PG_AUTO, R_SETTING, SET_SERVO_STEP_60,       TX_SV_ROW_STEP_60,  0, 1, false },
    { PG_AUTO, R_HV,      SETTING_COUNT,           TX_SV_ROW_HV,  0, 2, false },
    { PG_AUTO, R_SETTING, SET_SERVO_STEP_74,       TX_SV_ROW_STEP_74,  0, 3, false },
    { PG_AUTO, R_SETTING, SET_SERVO_STEP_84,       TX_SV_ROW_STEP_84,  0, 4, false },
    { PG_AUTO, R_SETTING, SET_SERVO_BROWNOUT,      TX_SV_ROW_BROWNOUT,  1, 4, false },
    { PG_AUTO, R_SETTING, SET_SERVO_TRAVEL_MAX_MS, TX_SV_ROW_TRAVEL_TIME,  1, 3, false },

    { PG_LIMRUN, R_LIM_MODE,   SETTING_COUNT, TX_SV_ROW_LIM_MODE,  0, 0, true  },
    { PG_LIMRUN, R_LIM_PULSES, SETTING_COUNT, TX_SV_ROW_LIM_PULSES,  0, 1, false },
};
#define ROW_COUNT ((int)(sizeof(k_rows) / sizeof(k_rows[0])))
/** @brief The rows k_rows holds. */
const int k_row_count = ROW_COUNT;

/* ------------------------------------------------------------ the pages */

gfx_rect_t row_rect(int i)
{
    const ov_row_t *r = &k_rows[i];
    if (r->page >= PG_COUNT) {
        const int w = r->wide ? 2 * RL_COL_W + 10 : RL_COL_W;
        return (gfx_rect_t){ (int16_t)(OV_X + 10 + r->col * (RL_COL_W + 10)),
                             (int16_t)(RL_ROW0 + r->row * RL_PITCH),
                             (int16_t)w, RL_LBL_H + OV_ROW_H };
    }
    const int x = (r->col == 0) ? OV_X + 10 : OV_X + 20 + OV_COL_W;
    const int w = r->wide ? OV_W - 20 : OV_COL_W;
    return (gfx_rect_t){ (int16_t)x, (int16_t)(OV_ROW0 + r->row * OV_PITCH),
                         (int16_t)w, OV_ROW_H };
}

gfx_rect_t value_rect(int i)
{
    const gfx_rect_t r = row_rect(i);
    if (k_rows[i].page >= PG_COUNT) {
        return (gfx_rect_t){ r.x, (int16_t)(r.y + RL_LBL_H), r.w, OV_ROW_H };
    }
    const int w = k_rows[i].wide ? r.w - 170 : OV_VAL_W;
    return (gfx_rect_t){ (int16_t)(r.x + r.w - w), r.y, (int16_t)w, r.h };
}

gfx_rect_t trim_rect(int i, bool up)
{
    const gfx_rect_t v = value_rect(i);
    return (gfx_rect_t){ (int16_t)(up ? v.x + v.w - UI_TAP_MIN : v.x), v.y,
                         UI_TAP_MIN, v.h };
}

gfx_rect_t choice_rect(int i)
{
    const int per_col = CHOICE_MAX / 2;
    const int col = i / per_col;
    const int line = i % per_col;
    return (gfx_rect_t){ (int16_t)(OV_X + 10 + col * (OV_COL_W + 10)),
                         (int16_t)(OV_Y + 56 + line * 56),
                         (int16_t)OV_COL_W, UI_TAP_MIN };
}

gfx_rect_t choice_cancel_rect(void)
{
    return (gfx_rect_t){ (int16_t)(OV_X + OV_W - 10 - 160),
                         (int16_t)(WARN_BTN_Y + 8), 160, UI_TAP_MIN };
}

gfx_rect_t warn_cancel_rect(void)
{
    return (gfx_rect_t){ (int16_t)(OV_X + OV_W - 20 - 180), WARN_BTN_Y, 180,
                         56 };
}

int decimals_of(setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    return (d->type == SET_TYPE_FLOAT && d->step < 1.0f) ? 2 : 0;
}

/* The longest pulse the frame rate in force leaves room for. */
static uint16_t max_pulse_for_rate(void)
{
    const unsigned pause = type()->heli ? HELI_MIN_PAUSE_US : PWM_MIN_PAUSE_US;
    const unsigned period = 1000000u / (unsigned)s.frame_hz;
    unsigned top = (period > pause) ? period - pause : 0u;
    if (top > OUT_CEILING_US) {
        top = OUT_CEILING_US;   /* what the IOmcu takes */
    }
    return (uint16_t)top;
}

/* A set point tapped: the keypad, over the left card as the settings' is. */
static void open_set_point(int target)
{
    close_panels();
    const supply_caps_t caps = supply_screen_caps();
    if (target == KT_SUP_V) {
        ui_keypad_open(&s.kp, overlay_area(), TR(SUP_VOLTAGE), "V",
                       supply_screen_set_v(), caps.v_min, caps.v_max, 2);
    } else {
        ui_keypad_open(&s.kp, overlay_area(), TR(SUP_CURRENT_LIMIT), "A",
                       supply_screen_set_i(), caps.i_min, caps.i_max, 2);
    }
    s.kp_target = target;
    servo_invalidate();
}

/* A set point typed: to the supply, or held for the question SUPPLY asks
 * before a live output changes. */
static void set_point_typed(int target, float typed)
{
    const supply_caps_t caps = supply_screen_caps();
    float v = supply_screen_set_v();
    float i = supply_screen_set_i();
    if (target == KT_SUP_V) {
        v = supply_snap(typed, caps.v_min, caps.v_max, caps.v_step);
    } else {
        i = supply_snap(typed, caps.i_min, caps.i_max, caps.i_step);
    }
    if (v == supply_screen_set_v() && i == supply_screen_set_i()) {
        close_alone();
        return;
    }
    /* Raised past a standard servo's rating, from at or under it: the HV
     * warning, which stands in for the live output's question too. */
    const bool hv = v > STD_SERVO_V_MAX + 0.001f
                    && supply_screen_set_v() <= STD_SERVO_V_MAX + 0.001f;
    if (hv || supply_screen_typed_asks()) {
        s.ask.open      = true;
        s.ask.hv        = hv;
        s.ask.purpose   = ASK_SET;
        s.ask.target    = target;
        s.ask.off_count = supply_screen_off_count();
        s.ask.stops     = s.stops;
        s.ask.v      = v;
        s.ask.i      = i;
        s.ask.down = false;
        ui_hold_reset(&s.ask.hold);
        servo_invalidate();
        return;
    }
    supply_screen_put(v, i);
    close_alone();
}

/* ------------------------------------------------------------- the editors */

/*
 * OUTPUT ON with a set point past a standard servo's rating: the HV warning
 * and its two-second hold, in place of the ordinary hold.  A set point made
 * on SUPPLY, or one already in force before this screen was opened, reaches
 * the servo only through it.
 */
void ask_hv_on(void)
{
    close_panels();
    s.ask.open      = true;
    s.ask.hv        = true;
    s.ask.purpose   = ASK_ON;
    s.ask.target    = KT_NONE;
    s.ask.off_count = supply_screen_off_count();
    s.ask.stops     = s.stops;
    s.ask.v         = supply_screen_set_v();
    s.ask.i         = supply_screen_set_i();
    s.ask.down      = false;
    ui_hold_reset(&s.ask.hold);
    servo_invalidate();
}

/**
 * @brief The operator's set points offered back onto the live output a
 *        programming port left on: SUPPLY's question, YES or NO.
 * @param v the voltage set point offered, V
 * @param i the current limit offered, A
 */
static void ask_restore(float v, float i)
{
    s.ask.open      = true;
    s.ask.hv        = false;
    s.ask.purpose   = ASK_RESTORE;
    s.ask.target    = KT_NONE;
    s.ask.off_count = supply_screen_off_count();
    s.ask.stops     = s.stops;
    s.ask.off_asks  = supply_screen_off_asks();
    s.ask.v         = v;
    s.ask.i         = i;
    s.ask.down      = false;
    ui_hold_reset(&s.ask.hold);
    ++s.ctrl_rev;
    servo_invalidate();
}

/**
 * @brief Whether the restore question still stands: the output it is about
 *        live and unstopped (ask_stands()), no OFF asked since it opened,
 *        and no port holding the supply.
 * @return true while it stands
 */
static bool restore_stands(void)
{
    return ask_stands() && supply_screen_off_asks() == s.ask.off_asks
           && !supply_screen_port_held();
}

void ask_restore_follow(void)
{
    float v = 0.0f;
    float i = 0.0f;

    /* A limit run's read session takes the port and gives it back inside
     * the run, which drives the supply at the set points it found: no
     * question about them, now or after the run. */
    if (servo_limrun_running(&s.lim) || s.lim_leaving) {
        if (supply_screen_restore_offer(NULL, NULL)) {
            supply_screen_restore_taken();
        }
        return;
    }
    /* A port that holds the supply again, an OFF asked, or an output gone
     * off or stopped, takes the question with it, the HV warning after
     * YES included: both are about the live output the port left. */
    if (s.ask.open && s.ask.purpose == ASK_RESTORE && !restore_stands()) {
        if (s.ov_pressed == OP_ASK_APPLY || s.ov_pressed == OP_ASK_CANCEL) {
            ov_let_go();
        }
        close_alone();
        return;
    }
    /* Not over another panel or question, a press on one, or a hold of
     * OUTPUT ON here or on SUPPLY: the offer waits for them. */
    if (modal_open() || s.ov_have || (s.out_down && !s.out_press_on)
        || supply_screen_output_held()
        || !supply_screen_restore_offer(&v, &i)) {
        return;
    }
    supply_screen_restore_taken();
    ask_restore(v, i);
}

/* The question answered with APPLY, or the HV warning's hold completed. */
void ask_apply(void)
{
    /* The restore's answer after an OFF was asked in the same frame, before
     * ask_restore_follow() saw it: nothing applied. */
    if (s.ask.purpose == ASK_RESTORE && !restore_stands()) {
        close_alone();
        return;
    }
    /* YES to a voltage past a standard servo's rating, from at or under it:
     * the HV warning and its hold, as a typed one takes. */
    if (s.ask.purpose == ASK_RESTORE && !s.ask.hv
        && s.ask.v > STD_SERVO_V_MAX + 0.001f
        && supply_screen_set_v() <= STD_SERVO_V_MAX + 0.001f) {
        s.ask.hv   = true;
        s.ask.down = false;
        ui_hold_reset(&s.ask.hold);
        ++s.ask.rev;
        ++s.ctrl_rev;
        servo_invalidate();
        return;
    }
    s.ask.open = false;
    s.ask.down = false;
    /* Only the set point that was typed: the other may have moved while
     * the question stood -- a cap that follows the PD mini's input -- and
     * the value it had then is not one anybody asked for now. */
    if (s.ask.purpose == ASK_TEST) {
        test_begin();
    } else if (s.ask.purpose == ASK_ON) {
        supply_screen_ask_on(SUPPLY_BY_SERVO);
    } else if (s.ask.purpose == ASK_RESTORE) {
        supply_screen_put(s.ask.v, s.ask.i);
    } else if (s.ask.target == KT_SUP_V) {
        supply_screen_put(s.ask.v, supply_screen_set_i());
    } else {
        supply_screen_put(supply_screen_set_v(), s.ask.i);
    }
    close_alone();
    servo_invalidate();
}

static void open_choice(int target, setting_id_t id, const char *title)
{
    s.ch.open    = true;
    s.ch.target  = target;
    s.ch.id      = id;
    s.ch.count   = 0;
    s.ch.pressed = -1;
    snprintf(s.ch.title, sizeof(s.ch.title), "%s", title);
}

static void choice_add(const char *label, int value)
{
    if (s.ch.count < CHOICE_MAX) {
        snprintf(s.ch.labels[s.ch.count], sizeof(s.ch.labels[0]), "%s", label);
        s.ch.values[s.ch.count] = value;
        ++s.ch.count;
    }
}

static void open_keypad(int target, const char *title, const char *unit,
                        float value, float lo, float hi, int decimals)
{
    ui_keypad_open(&s.kp, overlay_area(), title, unit, value, lo, hi,
                   decimals);
    s.kp_target = target;
}

/*
 * What the tester drives changes only on a bench at rest: disarmed, no test
 * and no sweep, and the port not in the programmer's hands.  The port's
 * pulse is switched on under the same rule, and off at any time but in a
 * test or a sweep: one that went on would step and record a servo that
 * gets no pulse.  STOP and DISARM end those.
 */
bool target_locked_for(setting_id_t id)
{
    if (id != SET_SERVO_TARGET && id != SET_SERVO_PORT_PWM) {
        return false;
    }
    if (settings_held(id)) {
        return true;                    /* the programmer has the port */
    }
    if (id == SET_SERVO_PORT_PWM) {
        if (settings_get_int(SET_SERVO_TARGET) == 0) {
            return true;                /* no port to switch */
        }
        if (settings_get_bool(SET_SERVO_PORT_PWM)) {
            /* Off is taken, armed or not, outside a run. */
            return s.sweeping || servo_test_running(&s.test)
                   || servo_limrun_running(&s.lim);
        }
    }
    return s.gate.armed || s.sweeping || servo_test_running(&s.test)
           || servo_limrun_running(&s.lim) || prog_holds();
}

bool target_locked(const ov_row_t *r)
{
    return r->kind == R_SETTING && target_locked_for(r->id);
}

/** @brief A settings row tapped: the editor its kind takes; TRIM's keypad
 *         is signed.  @param i The row. */
static void edit_row(int i)
{
    const ov_row_t *r = &k_rows[i];
    if (target_locked(r)) {
        return;
    }
    switch (r->kind) {
    case R_TYPE:
        open_choice(CH_TYPE, SETTING_COUNT, TR(SV_SERVO_TYPE));
        for (int t = 0; t < TYPE_COUNT; ++t) {
            choice_add(k_types[t].name, t);
        }
        break;
    case R_RATE: {
        open_choice(CH_RATE, SETTING_COUNT, TR(SV_ROW_RATE));
        const uint16_t top = max_rate_for(s.type, cmd_top());
        for (int k = 0; k < type()->rate_count; ++k) {
            if (type()->rates[k] <= top) {
                char lbl[16];
                snprintf(lbl, sizeof(lbl), "%uHz", (unsigned)type()->rates[k]);
                choice_add(lbl, type()->rates[k]);
            }
        }
        choice_add(TR(SV_CUSTOM), -1);
        break;
    }
    /*
     * Each end at least 50us from the centre, and the range a command
     * carries -- centred on CENTRE, out to the further end; see cmd_range()
     * -- inside what the IOmcu takes and what the frame rate leaves
     * a pause after (max_pulse_for_rate(), never above the ceiling).  So an
     * end may lie no further from the centre than the centre lies from the
     * floor or from that top.
     */
    case R_MIN: {
        const unsigned c = s.centre_us;
        const unsigned top = max_pulse_for_rate();
        const unsigned lo = (2u * c > top + OUT_FLOOR_US) ? 2u * c - top
                                                          : OUT_FLOOR_US;
        open_keypad(KT_MIN, TR(SV_ROW_MIN), "us", (float)s.min_us, (float)lo,
                    (float)(c - 50u), 0);
        break;
    }
    case R_CENTRE: {
        unsigned lo = (s.max_us + OUT_FLOOR_US + 1u) / 2u;
        unsigned hi = (max_pulse_for_rate() + s.min_us) / 2u;
        if (lo < s.min_us + 50u) { lo = s.min_us + 50u; }
        if (hi > s.max_us - 50u) { hi = s.max_us - 50u; }
        open_keypad(KT_CENTRE, TR(SV_ROW_CENTRE), "us", (float)s.centre_us,
                    (float)lo, (float)hi, 0);
        break;
    }
    case R_MAX: {
        const unsigned c = s.centre_us;
        unsigned top = max_pulse_for_rate();
        if (top > 2u * c - OUT_FLOOR_US) { top = 2u * c - OUT_FLOOR_US; }
        open_keypad(KT_MAX, TR(SV_ROW_MAX), "us", (float)s.max_us,
                    (float)(c + 50u),
                    (float)((top > c + 50u) ? top : c + 50u), 0);
        break;
    }
    case R_TRAVEL:
        open_keypad(KT_TRAVEL, TR(SV_ROW_TRAVEL), "deg", s.travel_deg, 10.0f,
                    90.0f, 0);
        break;
    case R_TRIM:                        /* -200 to +200us: the +/- key */
        ui_keypad_open_signed(&s.kp, overlay_area(), TR(SV_ROW_TRIM), "us",
                              (float)s.trim_us, -200.0f, 200.0f, 0);
        s.kp_target = KT_TRIM;
        break;
    case R_REVERSE:
        s.reverse = !s.reverse;
        reissue();
        break;
    case R_HV:
        /* For a run started after it; one under way keeps its steps. */
        s.test_hv = !s.test_hv;
        break;
    case R_LIM_MODE:
        /* For a run started after it, and only where it is offered. */
        if (!servo_limrun_running(&s.lim) && lim_mode_why() == NULL) {
            s.lim_prog = !s.lim_prog;
        }
        break;
    case R_LIM_PULSES:
        /* For a run started after it, on the programming port only. */
        if (!servo_limrun_running(&s.lim) && lim_reads()
            && lim_write_why() == NULL) {
            s.lim_pulses = !s.lim_pulses;
        }
        break;
    case R_ENC_CENTRE:
        /* The servo is at its neutral: the live count is the centre. */
        if (enc_shown()) {
            settings_set(SET_ENC_CENTRE, (float)s.enc_raw);
            settings_request_save();
        }
        break;
    case R_TEXT:
        ui_textkey_open(&s.tk, overlay_area(), TR(SV_DUT_TITLE),
                        settings_text(SET_TEXT_DUT_NAME), UI_TEXTKEY_MAX);
        break;
    case R_SETTING: {
        const setting_def_t *d = settings_def(r->id);
        if (d->type == SET_TYPE_BOOL) {
            settings_set(r->id, settings_get_bool(r->id) ? 0.0f : 1.0f);
            settings_request_save();
        } else if (d->type == SET_TYPE_ENUM) {
            open_choice(CH_ENUM, r->id, ui_tr(r->label));
            for (int k = 0; k < d->option_count; ++k) {
                choice_add(ui_setting_option(r->id, k), k);
            }
        } else {
            open_keypad(KT_SETTING, ui_tr(r->label), d->unit,
                        settings_get(r->id),
                        d->min, d->max, decimals_of(r->id));
            s.kp_setting = r->id;
        }
        break;
    }
    default:
        break;
    }
    ++s.ctrl_rev;
}

static void choose(int k)
{
    const int v = s.ch.values[k];
    const int target = s.ch.target;
    const setting_id_t id = s.ch.id;
    s.ch.open = false;
    if (target == CH_TYPE) {
        ask_profile(v, k_types[v].default_hz);
    } else if (target == CH_RATE) {
        if (v < 0) {
            open_keypad(KT_RATE, TR(SV_ROW_RATE), "Hz", (float)s.frame_hz,
                        50.0f,
                        (float)max_rate_for(s.type, cmd_top()), 0);
        } else {
            ask_profile(s.type, (uint16_t)v);
        }
    } else if (target == CH_ENUM) {
        settings_set(id, (float)v);
        settings_request_save();
    }
    ++s.ctrl_rev;
}

/** @brief The keypad closed: @p v to its target on OK.
 *  @param r How it closed.  @param v The value OK handed back. */
static void keypad_done(ui_keypad_result_t r, float v)
{
    if (r == UI_KEYPAD_NONE) {
        return;
    }
    const int target = s.kp_target;
    s.kp_target = KT_NONE;
    if (target == KT_SUP_V || target == KT_SUP_I) {
        if (r == UI_KEYPAD_OK) {
            set_point_typed(target, v);
        } else {
            close_alone();
        }
        ++s.ctrl_rev;
        return;
    }
    if (r == UI_KEYPAD_OK) {
        const uint16_t us = (uint16_t)lroundf(v);
        switch (target) {
        case KT_MIN:    s.min_us = us;    reissue(); break;
        case KT_CENTRE: s.centre_us = us; reissue(); break;
        case KT_MAX:
            s.max_us = us;
            apply_profile(s.type, s.frame_hz);   /* a rate it no longer fits
                                                  * comes down; never up */
            break;
        case KT_TRAVEL:
            s.travel_deg = (float)lroundf(v);
            /* Not live, the value shown stays what was last driven. */
            if (value_live()) {
                s.commanded_deg = clamp_travel(s.commanded_deg);
            }
            reissue();
            break;
        case KT_RATE:
            ask_profile(s.type, us);
            break;
        case KT_TRIM:                   /* onto the 5us steps of - and + */
            s.trim_us = (int16_t)(5L * lroundf(v / 5.0f));
            reissue();
            break;
        case KT_SETTING:
            settings_set(s.kp_setting, v);
            settings_request_save();
            break;
        default:
            break;
        }
    }
    ++s.ctrl_rev;
}

/** @brief A press on a page of rows or a panel: on whichever is up; on
 *         TRIM, beside its steps, the row.  @param evt The DOWN. */
void ov_down(const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;
    if (s.ask.open) {
        if (gfx_rect_contains(warn_apply_rect(), x, y)) {
            ov_take(evt, OP_ASK_APPLY, -1);
            if (s.ask.hv) {
                s.ask.down = true;
                ui_hold_begin(&s.ask.hold, TX_HOLD_HINT_APPLY);
                ++s.ask.rev;
            }
        } else if (gfx_rect_contains(warn_cancel_rect(), x, y)) {
            ov_take(evt, OP_ASK_CANCEL, -1);
        }
        return;
    }
    if (s.warn.open) {
        if (gfx_rect_contains(warn_apply_rect(), x, y)) {
            ov_take(evt, OP_WARN_APPLY, -1);
            s.warn.down = true;
            ui_hold_begin(&s.warn.hold, TX_HOLD_HINT_APPLY);
            ++s.warn.rev;
        } else if (gfx_rect_contains(warn_cancel_rect(), x, y)) {
            ov_take(evt, OP_WARN_CANCEL, -1);
        }
        return;
    }
    if (s.kp.open) {
        ov_take(evt, OP_KEYPAD, -1);
        (void)ui_keypad_event(&s.kp, evt, NULL);
        return;
    }
    if (s.tk.open) {
        ov_take(evt, OP_TEXT, -1);
        (void)ui_textkey_event(&s.tk, evt, NULL, 0);
        return;
    }
    if (s.ch.open) {
        for (int k = 0; k < s.ch.count; ++k) {
            if (gfx_rect_contains(choice_rect(k), x, y)) {
                ov_take(evt, OP_CHOICE, k);
                return;
            }
        }
        if (gfx_rect_contains(choice_cancel_rect(), x, y)) {
            ov_take(evt, OP_CHOICE_CANCEL, -1);
        }
        return;
    }
    /* SETTINGS' page tabs; a run's settings have none. */
    if (wb_task() == UI_WB_SETTINGS
        && gfx_rect_contains((gfx_rect_t){ (int16_t)(OV_X + 10), OV_TAB_Y,
                                           OV_TAB_W, OV_TAB_H }, x, y)) {
        ov_take(evt, OP_TAB, -1);
        (void)ui_tabs_event(&s.tabs, evt);
        return;
    }
    const int page = rows_page();
    /* DUT's two buttons, which open their settings' homes. */
    if (page == PG_DUT
        && (opener_down(evt, enc_open_rect(), OP_OPEN_ENC)
            || opener_down(evt, caps_open_rect(), OP_OPEN_CAPS))) {
        return;
    }
    /* A run's settings: CLOSE, back to TEST's cards. */
    if (page >= PG_COUNT && gfx_rect_contains(run_close_rect(), x, y)) {
        ov_take(evt, OP_RUN_CLOSE, -1);
        return;
    }
    for (int i = 0; i < ROW_COUNT; ++i) {
        if (k_rows[i].page != page || row_hidden(&k_rows[i])) {
            continue;
        }
        if (k_rows[i].kind == R_TRIM) {
            /* The steps act on the press, as the old CENTRE buttons did:
             * a fine step should feel immediate. */
            const bool up = gfx_rect_contains(trim_rect(i, true), x, y);
            if (up || gfx_rect_contains(trim_rect(i, false), x, y)) {
                ov_take(evt, up ? OP_TRIM_UP : OP_TRIM_DN, i);
                s.trim_us = (int16_t)(s.trim_us + (up ? 5 : -5));
                if (s.trim_us > 200) { s.trim_us = 200; }
                if (s.trim_us < -200) { s.trim_us = -200; }
                reissue();
                return;
            }
        }
        if (gfx_rect_contains(row_rect(i), x, y)) {
            ov_take(evt, OP_ROW, i);
            return;
        }
    }
}

/* The rest of such a press: its moves and its release. */
void ov_rest(const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;
    const bool up = (evt->type == TOUCH_EVENT_UP);
    const int was = s.ov_pressed;
    const int which = s.ov_row;
    switch (was) {
    case OP_KEYPAD: {
        float v = 0.0f;
        const ui_keypad_result_t r = ui_keypad_event(&s.kp, evt, &v);
        if (up) {
            ov_let_go();
        }
        keypad_done(r, v);
        ++s.ctrl_rev;
        return;
    }
    case OP_TEXT: {
        char name[SETTINGS_TEXT_MAX];
        const ui_textkey_result_t r =
            ui_textkey_event(&s.tk, evt, name, sizeof(name));
        if (up) {
            ov_let_go();
        }
        /* The keyboard covers the strip: on PROGRAM it types the list's
         * search, on DUT the name. */
        if (r == UI_TEXTKEY_OK && wb_task() == UI_WB_PROGRAM) {
            prog_search_done(name);
        } else if (r == UI_TEXTKEY_OK) {
            settings_set_text(SET_TEXT_DUT_NAME, name);
            settings_request_save();
        }
        ++s.ctrl_rev;
        return;
    }
    case OP_TAB:
        if (ui_tabs_event(&s.tabs, evt)) {
            ++s.ctrl_rev;
        }
        if (up) {
            ov_let_go();
        }
        return;
    case OP_ASK_APPLY:
        if (!s.ask.hv) {
            break;                      /* a tap: the release applies it */
        }
        if (!up) {
            /* A finger that leaves APPLY abandons the hold, as on ARM. */
            if (!gfx_rect_contains(warn_apply_rect(), x, y)
                && ui_hold_leave(&s.ask.hold)) {
                s.ask.down = false;
                ++s.ask.rev;
                ov_let_go();
            }
            return;
        }
        (void)ui_hold_end(&s.ask.hold, evt, warn_apply_rect());
        s.ask.down = false;
        ++s.ask.rev;
        ov_let_go();
        return;
    case OP_TEST_START:
        if (!s.test_down) {
            break;                      /* a tap: the release acts */
        }
        if (!up) {
            /* A finger that leaves START TEST abandons the hold. */
            if (!gfx_rect_contains(test_start_rect(), x, y)
                && ui_hold_leave(&s.test_hold)) {
                s.test_down = false;
                ++s.test_rev;
                ov_let_go();
            }
            return;
        }
        (void)ui_hold_end(&s.test_hold, evt, test_start_rect());
        s.test_down = false;
        ++s.test_rev;
        ov_let_go();
        return;
    case OP_LIM_START:
        if (!s.lim_down) {
            break;                      /* a tap: the release acts */
        }
        if (!up) {
            /* A finger that leaves START LIMIT RUN abandons the hold. */
            if (!gfx_rect_contains(lim_start_rect(), x, y)
                && ui_hold_leave(&s.lim_hold)) {
                s.lim_down = false;
                ++s.test_rev;
                ov_let_go();
            }
            return;
        }
        (void)ui_hold_end(&s.lim_hold, evt, lim_start_rect());
        s.lim_down = false;
        ++s.test_rev;
        ov_let_go();
        return;
    case OP_WARN_APPLY:
        if (!up) {
            /* A finger that leaves APPLY abandons the hold, as on ARM. */
            if (!gfx_rect_contains(warn_apply_rect(), x, y)
                && ui_hold_leave(&s.warn.hold)) {
                s.warn.down = false;
                ++s.warn.rev;
                ov_let_go();
            }
            return;
        }
        (void)ui_hold_end(&s.warn.hold, evt, warn_apply_rect());
        s.warn.down = false;
        ++s.warn.rev;
        ov_let_go();
        return;
    default:
        break;
    }
    if (!up) {
        return;
    }
    ov_let_go();
    /* A release the finger did not make activates nothing, except the tap
     * that stops a run. */
    if (!touch_event_is_tap_up(evt)
        && !(was == OP_TEST_START && servo_test_running(&s.test))
        && !(was == OP_LIM_START && servo_limrun_running(&s.lim))) {
        ++s.ctrl_rev;
        return;
    }
    switch (was) {
    case OP_ROW:
        if (which >= 0 && gfx_rect_contains(row_rect(which), x, y)) {
            edit_row(which);
        }
        break;
    case OP_CHOICE:
        if (which >= 0 && which < s.ch.count
            && gfx_rect_contains(choice_rect(which), x, y)) {
            choose(which);
        }
        break;
    case OP_CHOICE_CANCEL:
        if (gfx_rect_contains(choice_cancel_rect(), x, y)) {
            s.ch.open = false;
        }
        break;
    case OP_WARN_CANCEL:
        if (gfx_rect_contains(warn_cancel_rect(), x, y)) {
            s.warn.open = false;
        }
        break;
    case OP_SUP_V:
    case OP_SUP_I: {
        const int k = (was == OP_SUP_V) ? 0 : 1;
        if (gfx_rect_contains(sup_val_rect(k), x, y)) {
            open_set_point((k == 0) ? KT_SUP_V : KT_SUP_I);
        }
        break;
    }
    case OP_ASK_APPLY:
        /* Only while the question still stands: a release drained in the
         * frame that took the live output away must not apply it. */
        if (gfx_rect_contains(warn_apply_rect(), x, y) && s.ask.open
            && (s.ask.hv || ask_stands())) {
            ask_apply();
        }
        break;
    case OP_ASK_CANCEL:
        if (gfx_rect_contains(warn_cancel_rect(), x, y)) {
            s.ask.open = false;
            close_alone();
            servo_invalidate();
        }
        break;
    case OP_TEST_START:
        if (!gfx_rect_contains(test_start_rect(), x, y)) {
            break;
        }
        if (servo_test_running(&s.test)) {
            test_end_now(SERVO_TEST_AB_OPERATOR);
        } else if (test_blocked() == 0 && test_needs_hv()) {
            ask_hv_test();
        }
        ++s.test_rev;
        break;
    case OP_LIM_START:
        if (gfx_rect_contains(lim_start_rect(), x, y)
            && servo_limrun_running(&s.lim)) {
            lim_end_now(SERVO_TEST_AB_OPERATOR);
        }
        ++s.test_rev;
        break;
    case OP_RUN_CLOSE:
        /* A run's settings' CLOSE: up to TEST's cards, as the home tag
         * climbs from here. */
        if (gfx_rect_contains(run_close_rect(), x, y)
            && wb_task() == UI_WB_TEST && s.run_list != RL_NONE) {
            s.run_list = RL_NONE;
            servo_invalidate();
        }
        break;
    case OP_OPEN_CAPS:
        /* Asked again: a bench that armed under the finger stays armed. */
        if (gfx_rect_contains(caps_open_rect(), x, y)
            && !target_locked_for(SET_SERVO_TARGET)) {
            wb_open_caps();
        }
        break;
    case OP_OPEN_ENC:
        if (gfx_rect_contains(enc_open_rect(), x, y)
            && !target_locked_for(SET_SERVO_TARGET)) {
            wb_open_encoder();
        }
        break;
    case OP_RUN_SET:
        /* A run card's SETTINGS: that run's settings, one level down. */
        if (which >= 0 && gfx_rect_contains(run_set_rect(which), x, y)
            && wb_task() == UI_WB_TEST) {
            s.run_list = (which == 0) ? RL_AUTO : RL_LIMIT;
            servo_invalidate();
        }
        break;
    default:
        break;
    }
    ++s.ctrl_rev;
}
