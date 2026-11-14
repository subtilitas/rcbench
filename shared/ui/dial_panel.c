/**
 * @file
 * @brief The frame task's side of the M5Stack Dial (dial_panel.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "dial_panel.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "link_pages.h"
#include "motor_screen.h"
#include "servo_screen.h"
#include "settings.h"
#include "ui_focus.h"

float dial_span(ui_screen_id_t route, int32_t detents, uint8_t thr_tenths,
                uint8_t srv_tenths)
{
    if (detents == 0) {
        return 0.0f;
    }
    if (route == SCREEN_MOTOR) {
        /* The slider spans 0 to 100%: 1% is 0.01 of it. */
        return (float)detents * (float)thr_tenths / 1000.0f;
    }
    if (servo_screen_owns(route)) {
        const float travel = servo_screen_travel_deg();
        if (travel <= 0.0f) {
            return 0.0f;
        }
        /* servo_screen_knob() moves 2 x travel for a fraction of 1. */
        return (float)detents * (float)srv_tenths / 10.0f / (2.0f * travel);
    }
    return 0.0f;
}

bool dial_knob_take(int32_t knob_steps, const dial_input_t *in)
{
    const bool input = knob_steps != 0
                       || (in != NULL && (in->detents != 0 || in->presses != 0u));
    return input && ui_router_take_knob();
}

bool dial_gestures(ui_screen_id_t route, const dial_input_t *in,
                   bool press_ok)
{
    if (in == NULL) {
        return false;
    }
    const bool motor = route == SCREEN_MOTOR;
    const bool servo = servo_screen_owns(route);
    if (in->holds != 0u) {
        bool acted = false;
        if (motor) {
            acted = motor_screen_dial_hold();
        } else if (servo) {
            acted = servo_screen_dial_hold();
        }
        /* A press after a hold in one frame goes nowhere: the bench is
         * SAFE then. */
        return acted;
    }
    if (in->presses == 0u || !press_ok) {
        return false;
    }
    if (motor) {
        return motor_screen_dial_press();
    }
    if (servo) {
        return servo_screen_dial_press();
    }
    return false;
}

bool dial_navigate(ui_screen_id_t route, const dial_input_t *in,
                   bool steady, bool taken)
{
    if (in == NULL || !ui_focus_shown()) {
        return false;
    }
    if (in->holds != 0u) {
        /* The end of a sequence first, as on every SERVO screen; BACK
         * only where the hold stopped nothing. */
        if (!dial_gestures(ui_router_current(), in, false) && steady) {
            (void)ui_focus_hold();
        }
        return true;
    }
    if (!taken) {
        return true;
    }
    if (in->detents != 0) {
        (void)ui_focus_turn((int)in->detents);
    }
    if (in->presses != 0u && ui_router_current() == route) {
        /* One SELECT a frame: a second press of the frame would land on
         * whatever the first opened. */
        (void)ui_focus_press();
    }
    return true;
}

dial_input_t dial_dim_filter(screen_dim_t *d, const dial_input_t *in,
                             ui_screen_id_t route, const shot_facts_t *facts,
                             uint32_t now_ms)
{
    const dial_input_t none = { 0 };
    if (in == NULL) {
        return none;
    }
    if (in->detents == 0 && in->presses == 0u && in->holds == 0u) {
        return *in;
    }
    const bool was_dim = screen_dim_wake(d, now_ms);
    const bool stops = in->holds != 0u
                       && (route == SCREEN_MOTOR || servo_screen_owns(route));
    if (was_dim && !stops && screen_dim_idle(facts)) {
        return none;
    }
    return *in;
}

/**
 * @brief @p v rounded into @p lo to @p hi.
 * @param v The value.  @param lo The lowest.  @param hi The highest.
 * @return The integer.
 */
static int32_t round_in(float v, int32_t lo, int32_t hi)
{
    if (isnan(v)) {
        return 0;
    }
    const float r = roundf(v);
    if (r <= (float)lo) {
        return lo;
    }
    if (r >= (float)hi) {
        return hi;
    }
    return (int32_t)r;
}

/**
 * @brief MOTOR & ESC's readings as LIVE_MOTOR carries them.
 * @param in The inputs.
 * @param m  Receives them.
 */
static void motor_live(const dial_model_in_t *in, dk_live_motor_t *m)
{
    const float thr = motor_screen_throttle();
    memset(m, 0, sizeof(*m));
    m->flags = (uint8_t)((in->motor_armed ? DK_LIVE_ARMED : 0u)
                         | (in->motor_armed && thr > 0.0f ? DK_LIVE_DRIVING
                                                          : 0u)
                         | (in->fault ? DK_LIVE_FAULT : 0u));
    m->thr_cpct = (uint16_t)round_in(thr * 100.0f, 0, DK_THROTTLE_MAX);
    m->run_s = in->run_s;
    const bench_state_t *b = in->bench;
    if (b == NULL || !b->valid) {
        return;
    }
    const bool v = (b->flags & LINK_BN_VOLTAGE_OK) != 0u;
    const bool i = (b->flags & LINK_BN_CURRENT_OK) != 0u;
    if (v) {
        m->valid |= DK_MV_VOLT;
        m->mv = (uint16_t)round_in(b->voltage * 1000.0f, 0, 65535);
    }
    if (i) {
        m->valid |= DK_MV_CURR;
        m->ca = (int16_t)round_in(b->current * 100.0f, -32768, 32767);
    }
    if (v && i) {
        m->valid |= DK_MV_POWER;
        m->dw = (uint16_t)round_in(b->power * 10.0f, 0, 65535);
    }
    if ((b->flags & LINK_BN_RPM_OK) != 0u) {
        m->valid |= DK_MV_RPM;
        m->rpm = (uint16_t)round_in(b->rpm, 0, 65535);
    }
    if ((b->flags & LINK_BN_TEMP_OK) != 0u) {
        m->valid |= DK_MV_ESC_T;
        m->esc_c = (int8_t)round_in(b->temp_esc, DK_TEMP_MIN, DK_TEMP_MAX);
    }
    if ((b->flags & LINK_BN_TEMP_MOT_OK) != 0u) {
        m->valid |= DK_MV_MOT_T;
        m->mot_c = (int8_t)round_in(b->temp_motor, DK_TEMP_MIN, DK_TEMP_MAX);
    }
}

/**
 * @brief SERVO's readings as LIVE_SERVO carries them: the pulse COMMANDED
 *        shows, the angle MEASURED shows, the rail of INA3221 CH1's window.
 *        The feedback servo's position is not carried.
 * @param in The inputs.
 * @param s  Receives them.
 */
static void servo_live(const dial_model_in_t *in, dk_live_servo_t *s)
{
    memset(s, 0, sizeof(*s));
    uint16_t us = 0u;
    const bool pulse = servo_screen_log_pulse(&us);
    s->flags = (uint8_t)((in->servo_armed ? DK_LIVE_ARMED : 0u)
                         | (in->servo_armed && pulse ? DK_LIVE_DRIVING : 0u));
    s->cmd_us = pulse ? us : 0u;
    s->cmd_ddeg = (int16_t)round_in(servo_screen_commanded_deg() * 10.0f,
                                    -DK_DDEG_MAX, DK_DDEG_MAX);
    float deg;
    if (servo_screen_log_angle(&deg)) {
        s->flags |= DK_LIVE_MEAS;
        s->meas_cdeg = (int16_t)round_in(deg * 100.0f, -DK_CDEG_MAX,
                                         DK_CDEG_MAX);
    }
    const bench_state_t *b = in->bench;
    if (b != NULL && (b->servo_ok & 1u) != 0u
        && (b->servo_no_voltage & 1u) == 0u
        && (b->servo_no_current & 1u) == 0u) {
        const int32_t mv = b->servo_min_mv[0];
        const int32_t ma = b->servo_mean_ma[0];
        s->flags |= DK_LIVE_RAIL;
        s->rail_mv = (uint16_t)mv;
        s->rail_ca = (int16_t)(ma / 10);
        /* mV x mA is uW; 0.1W is 100,000uW. */
        s->rail_dw = (uint16_t)round_in((float)mv * (float)ma / 100000.0f, 0,
                                        65535);
    }
}

/**
 * @brief The readings' full scales (dial_model_build()).
 * @param in The inputs.
 * @param c  Receives them; a field 0 is not known.
 */
static void full_scales(const dial_model_in_t *in, dk_scale_t *c)
{
    memset(c, 0, sizeof(*c));
    float volt = 0.0f, curr = 0.0f, rpm = 0.0f;
    motor_screen_plot_scales(&volt, &curr, &rpm);
    const bench_state_t *b = in->bench;
    if (b != NULL && b->valid && b->rpm_max >= 1.0f) {
        rpm = b->rpm_max;
    }
    float sup_v = 0.0f, sup_i = 0.0f;
    if (in->supply_on) {
        sup_v = settings_get(SET_SUPPLY_V_MAX);
        sup_i = settings_get(SET_SUPPLY_TRIP_I);
    }
    c->rpm = (uint16_t)round_in(rpm, 0, UINT16_MAX);
    c->mv = (uint16_t)round_in((sup_v > 0.0f ? sup_v : volt) * 1000.0f, 0,
                               UINT16_MAX);
    c->ca = (uint16_t)round_in((sup_i > 0.0f ? sup_i : curr) * 100.0f, 0,
                               UINT16_MAX);
    c->travel = (uint16_t)round_in(servo_screen_travel_deg() * 10.0f, 0,
                                   DK_DDEG_MAX);
    uint16_t lo = 0u, hi = 0u;
    servo_screen_pulse_ends(&lo, &hi);
    if (lo >= DK_US_MIN && hi <= DK_US_MAX && lo < hi) {
        c->us_min = lo;
        c->us_max = hi;
    }
    c->rail_mv = (uint16_t)round_in(sup_v * 1000.0f, 0, UINT16_MAX);
    c->rail_ca = (uint16_t)round_in(sup_i * 100.0f, 0, UINT16_MAX);
}

/**
 * @brief The focus layer's face (ui_focus.h): VALUE while a value is set,
 *        NAV while the knob moves the focus; the overview stays HOME with
 *        its tiles as the rows, which the Dial draws as NAV.
 * @param in    The inputs.
 * @param out   The model, its title the screen's.
 * @return false where the layer does not navigate.
 */
static bool focus_model(const dial_model_in_t *in, dial_model_t *out)
{
    if (!ui_focus_shown()) {
        return false;
    }
    out->flags = DK_SCR_NAVIGATES;
    ui_focus_value_t v;
    if (ui_focus_value(&v)) {
        const int d = (v.decimals < 0) ? 0
                      : (v.decimals > (int)DK_DECIMALS_MAX)
                          ? (int)DK_DECIMALS_MAX : v.decimals;
        float scale = 1.0f;
        for (int i = 0; i < d; ++i) {
            scale *= 10.0f;
        }
        out->face = DK_FACE_VALUE;
        (void)dk_text_copy(out->title, sizeof(out->title),
                           v.name != NULL ? v.name : "");
        out->has_value = true;
        (void)dk_text_copy(out->meta.unit, sizeof(out->meta.unit),
                           v.unit != NULL ? v.unit : "");
        out->meta.decimals = (uint8_t)d;
        out->meta.min = round_in(v.min * scale, INT32_MIN / 2, INT32_MAX / 2);
        out->meta.max = round_in(v.max * scale, out->meta.min,
                                 INT32_MAX / 2);
        out->meta.step = round_in(v.step * scale, 1, INT32_MAX / 2);
        out->value.value = round_in(v.value * scale, out->meta.min,
                                    out->meta.max);
        out->value.was = round_in(v.was * scale, out->meta.min,
                                  out->meta.max);
        out->value.flags = (uint8_t)(DK_VAL_SET
                                     | (v.limit ? DK_VAL_LIMIT : 0u));
        return true;
    }
    out->face = (in->route == SCREEN_OVERVIEW) ? DK_FACE_HOME : DK_FACE_NAV;
    out->has_nav = true;
    const int index = ui_focus_index();
    const int count = ui_focus_count();
    for (unsigned k = 0; k < 3u; ++k) {
        dk_nav_t *n = &out->nav[k];
        n->index = (uint8_t)(index > 255 ? 255 : index);
        n->count = (uint8_t)(count > 255 ? 255 : count);
        (void)dk_text_copy(n->item, sizeof(n->item),
                           ui_focus_label(index + (int)k - 1));
    }
    return true;
}

void dial_model_build(dial_builder_t *bld, const dial_model_in_t *in,
                      dial_model_t *out)
{
    if (bld == NULL || in == NULL || out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->lang = in->lang;
    out->armed = in->armed;
    out->thr_tenths = in->thr_tenths;
    out->srv_tenths = in->srv_tenths;
    (void)dk_text_copy(out->title, sizeof(out->title),
                       in->title != NULL ? in->title : "");
    int32_t value = 0;
    bool given = false;
    if (in->route == SCREEN_MOTOR && motor_screen_knob_shown()) {
        out->face = DK_FACE_MOTOR;
        out->has_motor = true;
        motor_live(in, &out->motor);
        out->has_value = true;
        (void)dk_text_copy(out->meta.unit, sizeof(out->meta.unit), "%");
        out->meta.decimals = 1u;
        out->meta.min = 0;
        out->meta.max = 1000;
        out->meta.step = in->thr_tenths;
        value = round_in(motor_screen_throttle() * 10.0f, 0, 1000);
    } else if (servo_screen_owns(in->route) && servo_screen_knob_shown()) {
        out->face = DK_FACE_SERVO;
        out->has_servo = true;
        servo_live(in, &out->servo);
        out->has_value = true;
        const int32_t t = round_in(servo_screen_travel_deg() * 10.0f, 0,
                                   DK_DDEG_MAX);
        (void)dk_text_copy(out->meta.unit, sizeof(out->meta.unit), "°");
        out->meta.decimals = 1u;
        out->meta.min = -t;
        out->meta.max = t;
        out->meta.step = in->srv_tenths;
        value = round_in(servo_screen_commanded_deg() * 10.0f, -t, t);
    } else if (focus_model(in, out)) {
        given = true;
    } else if (in->route == SCREEN_OVERVIEW) {
        out->face = DK_FACE_HOME;
    } else {
        out->face = DK_FACE_OTHER;
    }
    if (out->face == DK_FACE_MOTOR || out->face == DK_FACE_SERVO) {
        out->has_scale = true;
        full_scales(in, &out->scale);
    }
    if (out->face != bld->face) {
        bld->face = out->face;
        bld->was = value;
    }
    if (out->has_value && !given) {
        out->value.value = value;
        out->value.was = bld->was;
    }
}
