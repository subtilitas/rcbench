/**
 * @file
 * @brief The SERVO screens' pages of rows and the panels they open.  KST's
 * SETTINGS opens EXPERT MODE's page, one level down.  TEST's and LIMIT
 * RUN's settings are SETTINGS lists (servo_test_list.c, servo_lists.c).
 * The servo profile's rows --
 * type, frame rate, pulse widths, trim, travel, reverse -- are edited from
 * the hub's SETTINGS list (servo_lists.c) through svs_profile_edit() and
 * svs_profile_step().  The rows open the keypad, the list, the profile
 * warning, the HV warning and SUPPLY's question about a live output, all
 * over the left column.  The right card stays where it is and works.
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
#include "servo_angle_pick.h"
#include "servo_limrun.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_setting_keypad.h"
#include "ui_tabs.h"
#include "ui_textkey.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_int.h"

/**
 * @brief The settings rows, page by page.  The profile's (PG_OUTPUT) have
 *        no page: the hub's list edits them by kind, HV SERVO among them,
 *        which TEST's list shows too.  TEST's and LIMIT RUN's settings
 *        are lists (servo_test_list.c, servo_lists.c).
 */
const ov_row_t k_rows[] = {
    { PG_OUTPUT, R_TYPE,    SETTING_COUNT, TX_SV_ROW_TYPE,  0, 0, true  },
    { PG_OUTPUT, R_RATE,    SETTING_COUNT, TX_SV_ROW_RATE,  0, 1, true  },
    { PG_OUTPUT, R_MIN,     SETTING_COUNT, TX_SV_ROW_MIN,  0, 2, false },
    { PG_OUTPUT, R_CENTER,  SETTING_COUNT, TX_SV_ROW_CENTER,  1, 2, false },
    { PG_OUTPUT, R_MAX,     SETTING_COUNT, TX_SV_ROW_MAX,  0, 3, false },
    { PG_OUTPUT, R_TRIM,    SETTING_COUNT, TX_SV_ROW_TRIM,  1, 3, false },
    { PG_OUTPUT, R_TRAVEL,  SETTING_COUNT, TX_SV_ROW_TRAVEL,  0, 4, false },
    { PG_OUTPUT, R_REVERSE, SETTING_COUNT, TX_SV_ROW_REVERSE,  1, 4, false },
    { PG_OUTPUT, R_HV,      SETTING_COUNT, TX_SV_ROW_HV,  0, 5, false },

    { PG_KST, R_KST_EXPERT, SETTING_COUNT, TX_SV_ROW_KST_EXPERT,  0, 0, false },

};
#define ROW_COUNT ((int)(sizeof(k_rows) / sizeof(k_rows[0])))
/** @brief The rows k_rows holds. */
const int k_row_count = ROW_COUNT;

/* ------------------------------------------------------------ the pages */

gfx_rect_t row_rect(int i)
{
    const ov_row_t *r = &k_rows[i];
    if (r->page < PG_COUNT) {
        return (gfx_rect_t){ 0, 0, 0, 0 };   /* the hub's list's row */
    }
    const int w = r->wide ? 2 * RL_COL_W + 10 : RL_COL_W;
    return (gfx_rect_t){ (int16_t)(OV_X + 10 + r->col * (RL_COL_W + 10)),
                         (int16_t)(RL_ROW0 + r->row * RL_PITCH),
                         (int16_t)w, RL_LBL_H + OV_ROW_H };
}

gfx_rect_t value_rect(int i)
{
    const gfx_rect_t r = row_rect(i);
    return (gfx_rect_t){ r.x, (int16_t)(r.y + RL_LBL_H), r.w, OV_ROW_H };
}

gfx_rect_t choice_rect(int i)
{
    /* The PORT, SERVO and SERIES lists: one column, entries and refusals
     * 60 cells; the ANGLE SOURCE list too while it holds 5 entries or
     * fewer. */
    if (s.ch.target == CH_PORT || s.ch.target == CH_FAMILY
        || s.ch.target == CH_SERIES
        || (s.ch.target == CH_ANGLE && s.ch.count <= CHOICE_MAX / 2)) {
        return (gfx_rect_t){ (int16_t)(OV_X + 10),
                             (int16_t)(OV_Y + 56 + i * 56),
                             (int16_t)(OV_W - 20), UI_TAP_MIN };
    }
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
    return settings_decimals(id);   /* the step's digits: 0.05 two, 0.1 one */
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

    /* A limit run's or AUTO's sessions take the port and give it back
     * inside the run, which drives the supply at the set points it found:
     * no question about them, now or after the run. */
    if (servo_limrun_running(&s.lim) || s.lim_leaving
        || servo_rauto_running(&s.ra.run) || s.ra.leaving) {
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
    if (s.ask.purpose == ASK_TEST) {
        test_hold_fired();      /* ARM + START, as TEST's hold */
    } else if (s.ask.purpose == ASK_ON) {
        supply_screen_ask_on(SUPPLY_BY_SERVO);
    } else {
        supply_screen_put(s.ask.v, s.ask.i);   /* the restore's YES */
    }
    close_alone();
    servo_invalidate();
}

void open_choice(int target, setting_id_t id, const char *title)
{
    s.ch.open    = true;
    s.ch.refused = 0u;
    s.ch.target  = target;
    s.ch.id      = id;
    s.ch.count   = 0;
    s.ch.pressed = -1;
    snprintf(s.ch.title, sizeof(s.ch.title), "%s", title);
}

void choice_add(const char *label, int value)
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
bool target_locked_by_bench(setting_id_t id)
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
                   || servo_limrun_running(&s.lim)
                   || servo_rauto_running(&s.ra.run);
        }
    }
    return s.gate.armed || s.sweeping || servo_test_running(&s.test)
           || servo_limrun_running(&s.lim) || servo_rauto_running(&s.ra.run)
           || prog_holds();
}

/**
 * @brief target_locked_by_bench(), and while a finger is on a start hold
 *        before it fires or the ARM + START sequence is under way: the hold
 *        arms the output shown when it began.
 * @param id SET_SERVO_TARGET or SET_SERVO_PORT_PWM; false for any other
 * @return true while locked
 */
bool target_locked_for(setting_id_t id)
{
    return target_locked_by_bench(id)
           || ((id == SET_SERVO_TARGET || id == SET_SERVO_PORT_PWM)
               && svs_start_pending());
}

bool target_locked(const ov_row_t *r)
{
    return r->kind == R_SETTING && target_locked_for(r->id);
}

/**
 * @brief The range PULSE MIN, PULSE CENTER or PULSE MAX is typed and
 *        stepped in.
 *
 * Each end at least 50us from the center, and the range a command carries
 * -- centered on CENTER, out to the further end; see cmd_range() -- inside
 * what the IOmcu takes and what the frame rate leaves a pause after
 * (max_pulse_for_rate(), never above the ceiling).  So an end may lie no
 * further from the center than the center lies from the floor or from that
 * top.
 * @param kind R_MIN, R_CENTER or R_MAX
 * @param lo   takes the lowest, us
 * @param hi   takes the highest, us
 */
static void pulse_range(int kind, unsigned *lo, unsigned *hi)
{
    const unsigned c = s.center_us;
    if (kind == R_MIN) {
        const unsigned top = max_pulse_for_rate();
        *lo = (2u * c > top + OUT_FLOOR_US) ? 2u * c - top : OUT_FLOOR_US;
        *hi = c - 50u;
    } else if (kind == R_CENTER) {
        *lo = (s.max_us + OUT_FLOOR_US + 1u) / 2u;
        *hi = (max_pulse_for_rate() + s.min_us) / 2u;
        if (*lo < s.min_us + 50u) { *lo = s.min_us + 50u; }
        if (*hi > s.max_us - 50u) { *hi = s.max_us - 50u; }
    } else {
        unsigned top = max_pulse_for_rate();
        if (top > 2u * c - OUT_FLOOR_US) { top = 2u * c - OUT_FLOOR_US; }
        *lo = c + 50u;
        *hi = (top > c + 50u) ? top : c + 50u;
    }
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
    /* The pulses within pulse_range(). */
    case R_MIN: {
        unsigned lo = 0u, hi = 0u;
        pulse_range(R_MIN, &lo, &hi);
        open_keypad(KT_MIN, TR(SV_ROW_MIN), "us", (float)s.min_us, (float)lo,
                    (float)hi, 0);
        break;
    }
    case R_CENTER: {
        unsigned lo = 0u, hi = 0u;
        pulse_range(R_CENTER, &lo, &hi);
        open_keypad(KT_CENTER, TR(SV_ROW_CENTER), "us", (float)s.center_us,
                    (float)lo, (float)hi, 0);
        break;
    }
    case R_MAX: {
        unsigned lo = 0u, hi = 0u;
        pulse_range(R_MAX, &lo, &hi);
        open_keypad(KT_MAX, TR(SV_ROW_MAX), "us", (float)s.max_us,
                    (float)lo, (float)hi, 0);
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

/** @brief The profile's row of @p kind in k_rows. @param kind R_TYPE to
 *         R_REVERSE, R_HV @return its index, -1 for none */
static int profile_row(int kind)
{
    for (int i = 0; i < ROW_COUNT; ++i) {
        if (k_rows[i].kind == kind
            && k_rows[i].page == PG_OUTPUT) {
            return i;
        }
    }
    return -1;
}

void svs_profile_edit(int kind)
{
    const int i = profile_row(kind);
    if (i >= 0) {
        edit_row(i);
        servo_invalidate();
    }
}

/**
 * @brief @p v held to [@p lo, @p hi].
 * @param v the value @param lo the lowest @param hi the highest
 * @return the value held
 */
static int clamp_int(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

void svs_profile_step(int kind, int dir)
{
    unsigned lo = 0u, hi = 0u;
    dir = (dir < 0) ? -1 : 1;
    switch (kind) {
    case R_MIN:
        pulse_range(R_MIN, &lo, &hi);
        s.min_us = (uint16_t)clamp_int((int)s.min_us + 10 * dir, (int)lo,
                                       (int)hi);
        reissue();
        break;
    case R_CENTER:
        pulse_range(R_CENTER, &lo, &hi);
        s.center_us = (uint16_t)clamp_int((int)s.center_us + 10 * dir,
                                          (int)lo, (int)hi);
        reissue();
        break;
    case R_MAX:
        pulse_range(R_MAX, &lo, &hi);
        s.max_us = (uint16_t)clamp_int((int)s.max_us + 10 * dir, (int)lo,
                                       (int)hi);
        apply_profile(s.type, s.frame_hz);   /* a rate it no longer fits
                                              * comes down; never up */
        break;
    case R_TRIM:
        s.trim_us = (int16_t)clamp_int(s.trim_us + 5 * dir, -200, 200);
        reissue();
        break;
    case R_TRAVEL:
        s.travel_deg = (float)clamp_int((int)s.travel_deg + 5 * dir, 10, 90);
        if (value_live()) {
            s.commanded_deg = clamp_travel(s.commanded_deg);
        }
        reissue();
        break;
    case R_REVERSE:
        s.reverse = !s.reverse;
        reissue();
        break;
    case R_HV:
        /* For a run started after it; one under way keeps its steps. */
        s.test_hv = !s.test_hv;
        break;
    default:
        break;
    }
    ++s.ctrl_rev;
}

void svs_profile_value(int kind, char *buf, size_t n)
{
    const int i = profile_row(kind);
    if (buf == NULL || n == 0u) {
        return;
    }
    buf[0] = '\0';
    if (i >= 0) {
        ov_row_value(&k_rows[i], buf, n);
    }
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
    } else if (target == CH_PORT) {
        port_list_pick(v);
    } else if (target == CH_FAMILY) {
        svs_family_pick(id, v);
    } else if (target == CH_SERIES) {
        svs_series_pick(v);
    } else if (target == CH_ANGLE) {
        angle_pick_choose(v);
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
    if (r == UI_KEYPAD_OK) {
        const uint16_t us = (uint16_t)lroundf(v);
        switch (target) {
        case KT_MIN:    s.min_us = us;    reissue(); break;
        case KT_CENTER: s.center_us = us; reissue(); break;
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
            /* A number with an off gap (RUDDER's LENGTH: 0 or 10 to
             * 500mm) takes the settings' keypad rule: a value in the gap
             * is refused and the keypad stays open, saying why. */
            if (settings_def(s.kp_setting)->off_below <= 0.0f) {
                settings_set(s.kp_setting, v);
            } else if (ui_setting_keypad_ok(&s.kp, s.kp_setting, v)
                       == UI_SETKEY_REFUSED) {
                s.kp_target = KT_SETTING;
                break;
            }
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
    const int page = rows_page();
    /* A page of rows: CLOSE, back to its screen. */
    if (page >= PG_COUNT && gfx_rect_contains(run_close_rect(), x, y)) {
        ov_take(evt, OP_RUN_CLOSE, -1);
        return;
    }
    for (int i = 0; i < ROW_COUNT; ++i) {
        if (k_rows[i].page != page) {
            continue;
        }
        if (gfx_rect_contains(row_rect(i), x, y)) {
            ov_take(evt, k_rows[i].kind == R_KST_EXPERT ? OP_KST_EXPERT
                                                        : OP_ROW, i);
            if (k_rows[i].kind == R_KST_EXPERT
                && !settings_get_bool(SET_KST_EXPERT)) {
                /* ON takes the 2s hold; OFF is the tap's release. */
                s.kx_down = true;
                ui_hold_begin(&s.kx_hold, TX_HOLD_HINT_KST_EXPERT);
                ++s.kx_rev;
            }
            return;
        }
    }
}

gfx_rect_t kx_value_rect(void)
{
    for (int i = 0; i < ROW_COUNT; ++i) {
        if (k_rows[i].kind == R_KST_EXPERT) {
            return value_rect(i);
        }
    }
    return (gfx_rect_t){ 0, 0, 0, 0 };
}

void kx_tick(float dt_s)
{
    if (!s.kx_down) {
        return;
    }
    /* A press let go of anywhere -- a lost touch, a task or screen left,
     * a stop -- takes its hold with it. */
    if (!s.ov_have || s.ov_pressed != OP_KST_EXPERT) {
        ui_hold_reset(&s.kx_hold);
        s.kx_down = false;
        ++s.kx_rev;
        return;
    }
    ++s.kx_rev;
    if (ui_hold_tick(&s.kx_hold, dt_s)) {
        /* The press is dropped here, so the hold ends with it: its fade
         * stays off the value a later OFF draws. */
        ui_hold_spend(&s.kx_hold);
        s.kx_down = false;
        ov_let_go();
        settings_set(SET_KST_EXPERT, 1.0f);
        settings_request_save();
    }
}

/**
 * @brief The rest of a press on the KST EXPERT MODE row: a finger that
 *        leaves the row ends it, a hold's early release shows its hint,
 *        and a tap that began on ON turns the mode OFF.
 * @param evt the MOVE or UP
 * @param which the row
 */
static void kx_rest(const touch_event_t *evt, int which)
{
    const bool on_row = gfx_rect_contains(row_rect(which), evt->point.x,
                                          evt->point.y);

    if (evt->type != TOUCH_EVENT_UP) {
        if (!on_row) {
            if (s.kx_down) {
                (void)ui_hold_leave(&s.kx_hold);
                s.kx_down = false;
                ++s.kx_rev;
            }
            ov_let_go();
        }
        return;
    }
    if (s.kx_down) {
        (void)ui_hold_end(&s.kx_hold, evt, row_rect(which));
        s.kx_down = false;
        ++s.kx_rev;
    } else if (on_row && touch_event_is_tap_up(evt)
               && settings_get_bool(SET_KST_EXPERT)) {
        settings_set(SET_KST_EXPERT, 0.0f);
        settings_request_save();
    }
    ov_let_go();
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
        if (r == UI_TEXTKEY_OK) {
            settings_set_text(SET_TEXT_DUT_NAME, name);
            settings_request_save();
        }
        ++s.ctrl_rev;
        return;
    }
    case OP_KST_EXPERT:
        kx_rest(evt, which);
        ++s.ctrl_rev;
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
            /* A finger that leaves ARM + START abandons the hold. */
            if (!gfx_rect_contains(test_start_rect(), x, y)
                && ui_hold_leave(&s.test_hold)) {
                s.test_down = false;
                ++s.test_rev;
                ++s.arm_rev;
                ov_let_go();
            }
            return;
        }
        (void)ui_hold_end(&s.test_hold, evt, test_start_rect());
        s.test_down = false;
        ++s.test_rev;
        ++s.arm_rev;
        ov_let_go();
        return;
    case OP_LIM_START:
        if (!s.lim_down) {
            break;                      /* a tap: the release acts */
        }
        if (!up) {
            /* A finger that leaves LIMIT RUN's ARM + START abandons the
             * hold. */
            if (!gfx_rect_contains(ready_hold_rect(), x, y)
                && ui_hold_leave(&s.lim_hold)) {
                s.lim_down = false;
                ++s.arm_rev;
                ov_let_go();
            }
            return;
        }
        (void)ui_hold_end(&s.lim_hold, evt, ready_hold_rect());
        s.lim_down = false;
        ++s.arm_rev;
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
            if ((s.ch.refused & (1u << which)) != 0u
                && s.ch.target == CH_ANGLE) {
                angle_pick_refused(which);
            } else if ((s.ch.refused & (1u << which)) != 0u) {
                /* A refused port: its line, in the band too. */
                ui_router_set_alert(s.ch.labels[which]);
            } else {
                choose(which);
            }
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
    case OP_TEST_START: {
        if (!gfx_rect_contains(test_start_rect(), x, y)) {
            break;
        }
        /* A tap on the grey ARM + START: the first row that refuses it,
         * in the band; past 6.0V the HV warning, whose hold starts it. */
        char why[128];
        if (ready_refusal(why, sizeof(why)) != NULL) {
            ui_action_refused(why);
        } else if (test_needs_hv() && !servo_test_running(&s.test)
                   && !svs_seq_busy()) {
            if (svs_press_held()) {
                ui_action_refused(TR(FOOT_PRESS_HELD));
            } else {
                ask_hv_test();
            }
        }
        ++s.test_rev;
        ++s.arm_rev;
        break;
    }
    case OP_LIM_START: {
        /* A tap on the grey hold: the first row that refuses it, in the
         * band. */
        char why[128];
        if (gfx_rect_contains(ready_hold_rect(), x, y)
            && !servo_limrun_running(&s.lim)
            && ready_refusal(why, sizeof(why)) != NULL) {
            ui_action_refused(why);
        }
        ++s.arm_rev;
        break;
    }
    case OP_RUN_CLOSE:
        /* A page's CLOSE: up to its screen, as the home tag climbs from
         * here. */
        if (gfx_rect_contains(run_close_rect(), x, y)
            && s.run_list != RL_NONE) {
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
    default:
        break;
    }
    ++s.ctrl_rev;
}
