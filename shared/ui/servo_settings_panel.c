/*
 * The SERVO screen's pages of rows and the panels they open.  SETTINGS, a
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

/* The settings rows, page by page. */
static const ov_row_t k_rows[] = {
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
    { PG_DUT, R_SETTING, SET_SERVO_PORT_PWM, TX_SV_ROW_PORT_PWM,  1, 1, false },

    { PG_AUTO, R_SETTING, SET_SERVO_LEN_BY,        TX_SV_ROW_LEN_BY,  0, 0, false },
    { PG_AUTO, R_SETTING, SET_SERVO_LEN_S,         TX_SV_ROW_LEN_S,  0, 1, false },
    { PG_AUTO, R_SETTING, SET_SERVO_LEN_MOVES,     TX_SV_ROW_MOVES,  0, 2, false },
    { PG_AUTO, R_SETTING, SET_SERVO_DWELL_MS,      TX_SV_ROW_DWELL,  0, 3, false },
    { PG_AUTO, R_SETTING, SET_SERVO_SETTLE_MS,     TX_SV_ROW_SETTLE,  0, 4, false },
    { PG_AUTO, R_SETTING, SET_SERVO_STALL_A,       TX_SV_ROW_STALL,  0, 5, false },
    { PG_AUTO, R_SETTING, SET_SERVO_IDLE_MAX,      TX_SV_ROW_IDLE,  0, 6, false },
    { PG_AUTO, R_SETTING, SET_SERVO_HOLD_MAX,      TX_SV_ROW_HOLD,  0, 7, false },
    { PG_AUTO, R_SETTING, SET_SERVO_STEP_48,       TX_SV_ROW_STEP_48,  1, 0, false },
    { PG_AUTO, R_SETTING, SET_SERVO_STEP_60,       TX_SV_ROW_STEP_60,  1, 1, false },
    { PG_AUTO, R_HV,      SETTING_COUNT,           TX_SV_ROW_HV,  1, 2, false },
    { PG_AUTO, R_SETTING, SET_SERVO_STEP_74,       TX_SV_ROW_STEP_74,  1, 3, false },
    { PG_AUTO, R_SETTING, SET_SERVO_STEP_84,       TX_SV_ROW_STEP_84,  1, 4, false },
    { PG_AUTO, R_SETTING, SET_SERVO_BROWNOUT,      TX_SV_ROW_BROWNOUT,  1, 5, false },
    { PG_AUTO, R_SETTING, SET_SERVO_TRAVEL_MAX_MS, TX_SV_ROW_TRAVEL_TIME,  1, 6, false },

    { PG_LIMRUN, R_LIM_MODE,   SETTING_COUNT, TX_SV_ROW_LIM_MODE,  0, 0, true  },
    { PG_LIMRUN, R_LIM_PULSES, SETTING_COUNT, TX_SV_ROW_LIM_PULSES,  0, 1, false },
};
#define ROW_COUNT ((int)(sizeof(k_rows) / sizeof(k_rows[0])))

/* ------------------------------------------------------------ the pages */

/* A row's rectangle: its column, or both for a wide one.  A run's rows
 * start under the run's name, SETTINGS' under its page tabs. */
static gfx_rect_t row_rect(int i)
{
    const ov_row_t *r = &k_rows[i];
    const int x = (r->col == 0) ? OV_X + 10 : OV_X + 20 + OV_COL_W;
    const int w = r->wide ? OV_W - 20 : OV_COL_W;
    const int y0 = (r->page >= PG_COUNT) ? RL_ROW0 : OV_ROW0;
    return (gfx_rect_t){ (int16_t)x, (int16_t)(y0 + r->row * OV_PITCH),
                         (int16_t)w, OV_ROW_H };
}

/* Where a row's value is drawn and tapped. */
static gfx_rect_t value_rect(int i)
{
    const gfx_rect_t r = row_rect(i);
    const int w = k_rows[i].wide ? r.w - 124 : OV_VAL_W;
    return (gfx_rect_t){ (int16_t)(r.x + r.w - w), r.y, (int16_t)w, r.h };
}

/* TRIM's two steps, inside its value. */
static gfx_rect_t trim_rect(int i, bool up)
{
    const gfx_rect_t v = value_rect(i);
    return (gfx_rect_t){ (int16_t)(up ? v.x + v.w - 30 : v.x), v.y, 30, v.h };
}

static gfx_rect_t choice_rect(int i)
{
    const int per_col = 5;
    const int col = i / per_col;
    const int line = i % per_col;
    return (gfx_rect_t){ (int16_t)(OV_X + 10 + col * (OV_COL_W + 10)),
                         (int16_t)(OV_Y + 48 + line * 46),
                         (int16_t)OV_COL_W, 40 };
}

static gfx_rect_t choice_cancel_rect(void)
{
    return (gfx_rect_t){ (int16_t)(OV_X + OV_W - 10 - 110),
                         (int16_t)(OV_Y + OV_H - 46), 110, 36 };
}

static gfx_rect_t warn_cancel_rect(void)
{
    return (gfx_rect_t){ (int16_t)(OV_X + OV_W - 20 - 180),
                         (int16_t)(OV_Y + OV_H - 76), 180, 56 };
}

static int decimals_of(setting_id_t id)
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
        top = OUT_CEILING_US;   /* what the coprocessor takes */
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

/* The question answered with APPLY, or the HV warning's hold completed. */
void ask_apply(void)
{
    s.ask.open = false;
    s.ask.down = false;
    /* Only the set point that was typed: the other may have moved while
     * the question stood -- a cap that follows the PD mini's input -- and
     * the value it had then is not one anybody asked for now. */
    if (s.ask.purpose == ASK_TEST) {
        test_begin();
    } else if (s.ask.purpose == ASK_ON) {
        supply_screen_ask_on(SUPPLY_BY_SERVO);
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

/*
 * The TARGET row tapped: OUTPUTS or PROG PORT, the TARGET setting's
 * options, over the left column.  Under the setting's rule: refused while
 * the row is locked, and a choice made waits for a bench at rest before
 * the coprocessor takes it.
 */
void open_target_choice(void)
{
    if (target_locked_for(SET_SERVO_TARGET)) {
        return;
    }
    close_panels();
    const setting_def_t *d = settings_def(SET_SERVO_TARGET);
    open_choice(CH_ENUM, SET_SERVO_TARGET, TR(SV_TARGET));
    for (int k = 0; k < d->option_count; ++k) {
        choice_add(ui_setting_option(SET_SERVO_TARGET, k), k);
    }
    servo_invalidate();
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

static bool target_locked(const ov_row_t *r)
{
    return r->kind == R_SETTING && target_locked_for(r->id);
}

/* A settings row tapped: the editor its kind takes. */
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
                snprintf(lbl, sizeof(lbl), "%u Hz", (unsigned)type()->rates[k]);
                choice_add(lbl, type()->rates[k]);
            }
        }
        choice_add(TR(SV_CUSTOM), -1);
        break;
    }
    /*
     * Each end at least 50 us from the centre, and the range a command
     * carries -- centred on CENTRE, out to the further end; see cmd_range()
     * -- inside what the coprocessor takes and what the frame rate leaves
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
        if (!servo_limrun_running(&s.lim) && lim_prog_why() == NULL) {
            s.lim_prog = !s.lim_prog;
        }
        break;
    case R_LIM_PULSES:
        /* For a run started after it, on the programming port only. */
        if (!servo_limrun_running(&s.lim) && lim_reads()) {
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

/* A press on a page of rows or a panel: on whichever is up. */
void ov_down(const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;
    if (s.ask.open) {
        if (gfx_rect_contains(warn_apply_rect(), x, y)) {
            ov_take(evt, OP_ASK_APPLY, -1);
            if (s.ask.hv) {
                s.ask.down = true;
                ui_hold_begin(&s.ask.hold);
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
            ui_hold_begin(&s.warn.hold);
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
            continue;
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
        (void)ui_hold_end(&s.ask.hold);
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
        (void)ui_hold_end(&s.test_hold);
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
        (void)ui_hold_end(&s.lim_hold);
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
        (void)ui_hold_end(&s.warn.hold);
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

/* ----------------------------------------------------------------- drawing */

void draw_save_line(gfx_canvas_t *c)
{
    static const ui_text_id_t k_save[] = {
        TX_SUP_SAVED, TX_SUP_SAVE_WAITING, TX_SUP_NOT_SAVED,
        TX_SUP_SETUP_NOT_SAVED,
    };
    const gfx_rect_t r = save_line_rect();
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_PANEL));
    const uint8_t st = save_state();
    gfx_text(c, r.x, r.y + 1, ui_tr(k_save[st]), &gfx_font_8x16,
             (st == 2u) ? ui_theme_color(UI_C_WARN)
                        : ui_theme_color(UI_C_TEXT_DIM), 1);
}

/* A test setting that the length rule leaves unused is drawn faint. */
static bool row_unused(const ov_row_t *r)
{
    if (r->kind == R_ENC_CENTRE) {
        return !settings_get_bool(SET_ENC_EN);
    }
    if (r->kind == R_LIM_MODE) {
        return servo_limrun_running(&s.lim) || lim_prog_why() != NULL;
    }
    if (r->kind == R_LIM_PULSES) {
        return servo_limrun_running(&s.lim);
    }
    if (r->kind != R_SETTING) {
        return false;
    }
    if (target_locked(r)) {
        return true;
    }
    const int by = settings_get_int(SET_SERVO_LEN_BY);
    return (r->id == SET_SERVO_LEN_S && by != 0)
           || (r->id == SET_SERVO_LEN_MOVES && by == 0)
           || ((r->id == SET_SERVO_STEP_74 || r->id == SET_SERVO_STEP_84)
               && !s.test_hv);
}

static void row_value(const ov_row_t *r, char *buf, size_t n)
{
    switch (r->kind) {
    case R_TYPE:    snprintf(buf, n, "%s", type()->name); return;
    case R_RATE:    snprintf(buf, n, "%u Hz", (unsigned)s.frame_hz); return;
    case R_MIN:     snprintf(buf, n, "%u us", (unsigned)s.min_us); return;
    case R_CENTRE:  snprintf(buf, n, "%u us", (unsigned)s.centre_us); return;
    case R_MAX:     snprintf(buf, n, "%u us", (unsigned)s.max_us); return;
    case R_TRIM:    snprintf(buf, n, "%+d", (int)s.trim_us); return;
    case R_TRAVEL:  snprintf(buf, n, "+/-%d", (int)s.travel_deg); return;
    case R_REVERSE: snprintf(buf, n, "%s", ui_on_off(s.reverse)); return;
    case R_TEXT:    snprintf(buf, n, "%s", settings_text(SET_TEXT_DUT_NAME));
                    return;
    case R_HV:      snprintf(buf, n, "%s", ui_on_off(s.test_hv)); return;
    case R_LIM_MODE:
        snprintf(buf, n, "%s",
                 lim_programs() ? TR(SV_LIM_PROGRAM) : TR(SV_LIM_SEARCH));
        return;
    case R_LIM_PULSES:
        snprintf(buf, n, "%s",
                 s.lim_pulses ? TR(SV_LIM_PULSES_STD) : TR(SV_LIM_PULSES_KEEP));
        return;
    case R_ENC_CENTRE:
        snprintf(buf, n, "%d", settings_get_int(SET_ENC_CENTRE));
        return;
    default:
        break;
    }
    const setting_def_t *d = settings_def(r->id);
    const float v = settings_get(r->id);
    const bool limit = (r->id == SET_SERVO_IDLE_MAX
                        || r->id == SET_SERVO_HOLD_MAX
                        || r->id == SET_SERVO_TRAVEL_MAX_MS);
    if (d->type == SET_TYPE_BOOL) {
        snprintf(buf, n, "%s", ui_on_off(v != 0.0f));
    } else if (d->type == SET_TYPE_ENUM) {
        snprintf(buf, n, "%s",
                 ui_setting_option(r->id, settings_get_int(r->id)));
    } else if (limit && !(v > 0.0f)) {
        snprintf(buf, n, "%s", TR(OFF));
    } else {
        snprintf(buf, n, "%.*f %s", decimals_of(r->id), (double)v, d->unit);
    }
}

static void draw_note(gfx_canvas_t *c, int x, int y, const char *const *lines,
                      int count)
{
    for (int i = 0; i < count; ++i) {
        gfx_text(c, x, y + i * 18, lines[i], &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    }
}

bool servo_in_force_note(char *l1, char *l2, size_t size, uint16_t hz,
                         uint8_t target, uint8_t gpio, bool pwm, int setting)
{
    if (l1 == NULL || l2 == NULL || size == 0u) {
        return false;
    }
    /* No link: the coprocessor may have restarted since the rate landed,
     * and a restart puts every pin back at its own rate, so no rate is
     * named.  The link-up resets the rate before a target is known. */
    if (target == SERVO_SCREEN_TARGET_NO_LINK) {
        snprintf(l1, size, "%s", TR(SV_IN_FORCE_NO_LINK));
        snprintf(l2, size, "%s", TR(SV_IN_FORCE_2));
        return true;
    }
    /* The page not known: the rate was acknowledged, where it runs is not
     * known. */
    if (target == SERVO_SCREEN_TARGET_UNKNOWN) {
        snprintf(l1, size, TR(SV_IN_FORCE_UNKNOWN), (unsigned)hz);
        snprintf(l2, size, "%s", TR(SV_IN_FORCE_2));
        return true;
    }
    /* The target the coprocessor holds, not the setting: the setting waits
     * for a bench at rest, and PROG PORT with no pin bound to the port
     * leaves the standard outputs the target. */
    const bool other = setting < 0 || (unsigned)setting != target;
    bool warn = other;
    if (target == 0u) {
        snprintf(l1, size, TR(SV_IN_FORCE_OUT), (unsigned)hz);
    } else if (gpio == SERVO_SCREEN_NO_PIN) {
        snprintf(l1, size, TR(SV_IN_FORCE_PORT), (unsigned)hz);
        warn = true;
    } else {
        snprintf(l1, size, TR(SV_IN_FORCE_PORT_PIN), (unsigned)gpio,
                 (unsigned)hz);
    }
    if (other) {
        snprintf(l2, size, "%s", TR(SV_IN_FORCE_NOT_SET));
    } else if (target != 0u && !pwm) {
        snprintf(l2, size, "%s", TR(SV_IN_FORCE_PWM_OFF));
        warn = true;
    } else {
        snprintf(l2, size, "%s", TR(SV_IN_FORCE_2));
    }
    return warn;
}

/* A line of a page's note: the page less 10 px each side, 58 cells. */
#define NOTE_CELLS ((OV_W - 20) / 8)

/* Whether the frame rate reached the pins, in two lines or more. */
static void draw_rate_note(gfx_canvas_t *c, int x, int y)
{
    const servo_rate_state_t st = (s.rate_hz == s.frame_hz
                                   || s.rate_st == SERVO_RATE_UNSUPPORTED)
                                      ? s.rate_st
                                      : SERVO_RATE_UNSENT;
    char l1[96], l2[96];
    bool warn = true;
    switch (st) {
    case SERVO_RATE_IN_FORCE:
        warn = servo_in_force_note(l1, l2, sizeof(l1), s.frame_hz,
                                   s.drive_target, s.drive_pin, s.drive_pwm,
                                   settings_get_int(SET_SERVO_TARGET));
        break;
    case SERVO_RATE_IN_FORCE_SURFACES:
        /* With no link the same "not known" line as from 4.19. */
        if (s.drive_target == SERVO_SCREEN_TARGET_NO_LINK) {
            warn = servo_in_force_note(l1, l2, sizeof(l1), s.frame_hz,
                                       s.drive_target, s.drive_pin,
                                       s.drive_pwm,
                                       settings_get_int(SET_SERVO_TARGET));
            break;
        }
        snprintf(l1, sizeof(l1), TR(SV_IN_FORCE_SURFACES_1),
                 (unsigned)s.frame_hz);
        snprintf(l2, sizeof(l2), "%s", TR(SV_IN_FORCE_2));
        warn = false;
        break;
    case SERVO_RATE_REFUSED:
        snprintf(l1, sizeof(l1), "%s", TR(SV_REFUSED_1));
        snprintf(l2, sizeof(l2), "%s", TR(SV_REFUSED_2));
        break;
    case SERVO_RATE_UNSUPPORTED:
        snprintf(l1, sizeof(l1), "%s", TR(SV_UNSUPPORTED_1));
        snprintf(l2, sizeof(l2), "%s", TR(SV_UNSUPPORTED_2));
        break;
    case SERVO_RATE_UNSENT:
    default:
        snprintf(l1, sizeof(l1), "%s", TR(SV_UNSENT_1));
        snprintf(l2, sizeof(l2), "%s", TR(SV_UNSENT_2));
        warn = false;
        break;
    }
    const gfx_color_t col = warn ? ui_theme_color(UI_C_WARN)
                                 : ui_theme_color(UI_C_TEXT_FAINT);
    /* Each line wraps onto the next where it is longer than the page's
     * 58 cells. */
    const int n1 = wrap_text(c, x, y, 18, l1, NOTE_CELLS, col);
    (void)wrap_text(c, x, y + 18 * n1, 18, l2, NOTE_CELLS, col);
}

/*
 * A page of rows in the page area: SETTINGS' page under its tabs, or a
 * run's settings under the run's name.  Its notes, and the save line.
 */
void draw_rows_page(gfx_canvas_t *c)
{
    const int page = rows_page();
    const gfx_rect_t a = page_area();
    gfx_fill_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2,
                  ui_theme_color(UI_C_PANEL));
    if (page < PG_COUNT) {
        ui_tabs_render(&s.tabs, c);
    } else {
        gfx_text(c, OV_X + 10, CARD_Y + 8,
                 (page == PG_AUTO) ? TR(SV_AUTO_TEST) : TR(SV_ROW_LIM_MODE),
                 &gfx_font_8x16, ui_theme_color(UI_C_ACCENT), 1);
    }
    for (int i = 0; i < ROW_COUNT; ++i) {
        const ov_row_t *r = &k_rows[i];
        if (r->page != page || row_hidden(r)) {
            continue;
        }
        const gfx_rect_t rr = row_rect(i);
        const gfx_rect_t vr = value_rect(i);
        const bool faint = row_unused(r);
        gfx_text(c, rr.x, rr.y + 9, ui_tr(r->label), &gfx_font_8x16,
                 faint ? ui_theme_color(UI_C_TEXT_FAINT)
                       : ui_theme_color(UI_C_TEXT_DIM), 1);
        char v[32];
        row_value(r, v, sizeof(v));
        const bool pressed = s.ov_have && s.ov_pressed == OP_ROW
                             && s.ov_row == i;
        if (r->kind == R_TRIM) {
            gfx_fill_rect(c, vr.x, vr.y, vr.w, vr.h,
                          ui_theme_color(UI_C_PANEL_SUNK));
            gfx_text_in(c, vr, v, &gfx_font_8x16, ui_theme_color(UI_C_TEXT),
                        1, GFX_ALIGN_CENTER);
            ui_button(c, trim_rect(i, false), "-",
                      ui_theme_color(UI_C_PANEL_HI),
                      s.ov_have && s.ov_pressed == OP_TRIM_DN, true);
            ui_button(c, trim_rect(i, true), "+",
                      ui_theme_color(UI_C_PANEL_HI),
                      s.ov_have && s.ov_pressed == OP_TRIM_UP, true);
            continue;
        }
        /* The type and the rate in force in the danger colour when they
         * are ones that can destroy a servo not made for them. */
        const bool danger = ((r->kind == R_TYPE || r->kind == R_RATE)
                             && in_force_dangerous())
                            || (r->kind == R_HV && s.test_hv);
        ui_button(c, vr, v,
                  danger ? ui_theme_color(UI_C_DANGER)
                         : ui_theme_color(UI_C_PANEL_SUNK),
                  pressed, !faint);
    }

    const int nx = OV_X + 10;
    if (page == PG_OUTPUT) {
        char l1[96], l2[96];
        snprintf(l1, sizeof(l1), TR(SV_FASTEST),
                 (unsigned)max_rate_for(s.type, cmd_top()),
                 type()->heli ? "0.5 ms" : "1 ms");
        snprintf(l2, sizeof(l2), "%s", TR(SV_AT_START));
        const char *const lines[] = { l1, l2 };
        draw_note(c, nx, OV_NOTE_Y, lines, 2);
        draw_rate_note(c, nx, OV_NOTE_Y + 2 * 18);
    } else if (page == PG_AUTO) {
        draw_test_stall(c, nx, RL_ROW0 + 8 * OV_PITCH - 4);
    } else if (page == PG_LIMRUN) {
        draw_lim_mode_note(c, nx, RL_ROW0 + 2 * OV_PITCH + 6);
    } else if (page == PG_DUT) {
        const char *const lines[] = {
            TR(SV_DUT_NOTE), TR(SV_ENC_NOTE_1), TR(SV_ENC_NOTE_2),
        };
        const int y = OV_ROW0 + 3 * OV_PITCH + 6;
        draw_note(c, nx, y, lines, 3);
        draw_caps_line(c, nx, y + 4 * 18);
    }
    draw_save_line(c);
}

static void draw_choice(gfx_canvas_t *c)
{
    gfx_text(c, OV_X + 10, OV_Y + 16, s.ch.title, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT), 1);
    for (int k = 0; k < s.ch.count; ++k) {
        bool current = false;
        if (s.ch.target == CH_TYPE) {
            current = (s.ch.values[k] == s.type);
        } else if (s.ch.target == CH_RATE) {
            current = (s.ch.values[k] == (int)s.frame_hz);
        } else if (s.ch.target == CH_ENUM) {
            current = (s.ch.values[k] == settings_get_int(s.ch.id));
        }
        ui_button(c, choice_rect(k), s.ch.labels[k],
                  current ? ui_theme_color(UI_C_ACCENT)
                          : ui_theme_color(UI_C_PANEL_SUNK),
                  s.ov_have && s.ov_pressed == OP_CHOICE && s.ov_row == k,
                  true);
    }
    ui_button(c, choice_cancel_rect(), TR(CANCEL),
              ui_theme_color(UI_C_PANEL_SUNK),
              s.ov_have && s.ov_pressed == OP_CHOICE_CANCEL, true);
}

static gfx_color_t warn_fill(void)
{
    return ui_hold_fill(ui_theme_color(UI_C_PANEL_SUNK),
                        ui_theme_color(UI_C_DANGER), s.warn.hold.held_s);
}

void draw_warn_apply(gfx_canvas_t *c)
{
    ui_button(c, warn_apply_rect(), TR(SV_HOLD_TO_APPLY), warn_fill(),
              s.warn.down, true);
}

/* A line of a warning: the left column less 20 px each side, in 8 px
 * cells. */
#define WARN_CELLS ((OV_W - 40) / 8)

/*
 * The warning a type or a frame rate that can destroy a servo needs before
 * it is applied: in the danger colour, saying what it is and what it does
 * to a servo not made for it.
 */
static void draw_warning(gfx_canvas_t *c)
{
    const gfx_rect_t a = overlay_area();
    const gfx_color_t red = ui_theme_color(UI_C_DANGER);
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, red);
    gfx_draw_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2, red);
    gfx_draw_rect(c, a.x + 2, a.y + 2, a.w - 4, a.h - 4, red);
    gfx_text(c, a.x + 20, a.y + 20, TR(SV_DESTROY), &gfx_font_8x16,
             red, 2);
    const servo_type_t *t = &k_types[s.warn.type];
    char what[80];
    snprintf(what, sizeof(what), TR(SV_DESTROY_WHAT),
             t->name, (unsigned)s.warn.hz,
             (unsigned)((s.warn.type == s.type) ? s.min_us : t->min_us),
             (unsigned)((s.warn.type == s.type) ? s.max_us : t->max_us));
    gfx_text(c, a.x + 20, a.y + 66, what, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT), 1);
    const char *const lines[] = {
        TR(SV_DESTROY_1), TR(SV_DESTROY_2), TR(SV_DESTROY_3),
        TR(SV_DESTROY_4), TR(SV_DESTROY_5),
    };
    int y = a.y + 104;
    for (int i = 0; i < 5; ++i) {
        y += 22 * wrap_text(c, a.x + 20, y, 22, lines[i], WARN_CELLS,
                            (i < 3) ? ui_theme_color(UI_C_TEXT)
                                    : ui_theme_color(UI_C_TEXT_DIM));
    }
    draw_warn_apply(c);
    ui_button(c, warn_cancel_rect(), TR(CANCEL),
              ui_theme_color(UI_C_PANEL_SUNK),
              s.ov_have && s.ov_pressed == OP_WARN_CANCEL, true);
}

void draw_ask_apply(gfx_canvas_t *c)
{
    ui_button(c, warn_apply_rect(), TR(SV_HOLD_TO_APPLY),
              ui_hold_fill(ui_theme_color(UI_C_PANEL_SUNK),
                           ui_theme_color(UI_C_DANGER), s.ask.hold.held_s),
              s.ask.down, true);
}

/*
 * A voltage raised past a standard servo's rating: in the danger colour of
 * the profile warning, saying what it does to a servo not rated for it.
 */
static void draw_hv(gfx_canvas_t *c)
{
    const gfx_rect_t a = overlay_area();
    const gfx_color_t red = ui_theme_color(UI_C_DANGER);
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, red);
    gfx_draw_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2, red);
    gfx_draw_rect(c, a.x + 2, a.y + 2, a.w - 4, a.h - 4, red);
    gfx_text(c, a.x + 20, a.y + 20, TR(SV_HV_ONLY), &gfx_font_8x16, red, 2);
    char what[64];
    if (s.ask.purpose == ASK_ON) {
        snprintf(what, sizeof(what), TR(SV_HV_ON_AT),
                 (double)supply_screen_set_v());
    } else if (s.ask.purpose == ASK_TEST) {
        snprintf(what, sizeof(what), TR(SV_HV_STEPS),
                 (double)s.ask.v);
    } else {
        snprintf(what, sizeof(what), TR(SV_HV_VOLTAGE),
                 (double)supply_screen_set_v(), (double)s.ask.v);
    }
    gfx_text(c, a.x + 20, a.y + 66, what, &gfx_font_8x16,
             ui_theme_color(UI_C_VOLT), 1);
    const char *const lines[] = {
        TR(SV_HV_1), TR(SV_HV_2), TR(SV_HV_3), TR(SV_HV_4), TR(SV_HV_5),
    };
    int y = a.y + 104;
    for (int i = 0; i < 5; ++i) {
        y += 22 * wrap_text(c, a.x + 20, y, 22, lines[i], WARN_CELLS,
                            (i < 4) ? ui_theme_color(UI_C_TEXT)
                                    : ui_theme_color(UI_C_TEXT_DIM));
    }
    if (s.ask.purpose == ASK_SET && supply_screen_output_on()) {
        (void)wrap_text(c, a.x + 20, y + 8, 22, TR(SV_HV_LIVE), WARN_CELLS,
                        ui_theme_color(UI_C_WARN));
    }
    draw_ask_apply(c);
    ui_button(c, warn_cancel_rect(), TR(CANCEL),
              ui_theme_color(UI_C_PANEL_SUNK),
              s.ov_have && s.ov_pressed == OP_ASK_CANCEL, true);
}

/*
 * SUPPLY's question before a set point changes a live output, with its
 * words: the change reaches the load at once.
 */
static void draw_ask(gfx_canvas_t *c)
{
    const gfx_rect_t a = overlay_area();
    if (s.ask.hv) {
        draw_hv(c);
        return;
    }
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_WARN));
    gfx_text(c, a.x + 20, a.y + 20, TR(SUP_OUTPUT_IS_ON), &gfx_font_8x16,
             ui_theme_color(UI_C_WARN), 2);
    gfx_text(c, a.x + 20, a.y + 70, TR(SUP_ASK_WHY),
             &gfx_font_8x16, ui_theme_color(UI_C_TEXT_DIM), 1);
    int y = a.y + 120;
    const struct { const char *label; float was, now; const char *unit;
                   gfx_color_t col; } k[] = {
        { TR(SUP_VOLTAGE), supply_screen_set_v(), s.ask.v, "V",
          ui_theme_color(UI_C_VOLT) },
        { TR(SUP_CURRENT_LIMIT), supply_screen_set_i(), s.ask.i, "A",
          ui_theme_color(UI_C_CURR) },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        if (k[i].was == k[i].now) {
            continue;
        }
        gfx_text(c, a.x + 20, y + 8, k[i].label, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        char line[48];
        snprintf(line, sizeof(line), "%.2f -> %.2f %s", (double)k[i].was,
                 (double)k[i].now, k[i].unit);
        gfx_text(c, a.x + 150, y, line, &gfx_font_8x16, k[i].col, 2);
        y += 50;
    }
    ui_button(c, warn_apply_rect(), TR(SUP_APPLY), ui_theme_color(UI_C_WARN),
              s.ov_have && s.ov_pressed == OP_ASK_APPLY, true);
    ui_button(c, warn_cancel_rect(), TR(CANCEL), ui_theme_color(UI_C_PANEL_SUNK),
              s.ov_have && s.ov_pressed == OP_ASK_CANCEL, true);
}

/* The panel open over the left column, if any. */
void draw_overlay(gfx_canvas_t *c)
{
    const gfx_rect_t a = overlay_area();
    if (s.kp.open) {
        ui_keypad_render(&s.kp, c);
        return;
    }
    if (s.tk.open) {
        ui_textkey_render(&s.tk, c);
        return;
    }
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_PANEL));
    if (s.ask.open) {
        draw_ask(c);
        return;
    }
    if (s.warn.open) {
        draw_warning(c);
        return;
    }
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_ACCENT));
    if (s.ch.open) {
        draw_choice(c);
    }
}
