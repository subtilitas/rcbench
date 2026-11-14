/**
 * @file
 * @brief The frame task's side of the M5Stack Dial (dial_panel.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "dial_panel.h"

#include <math.h>
#include <string.h>

#include "link_pages.h"
#include "motor_screen.h"
#include "servo_screen.h"
#include "settings.h"

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

void dial_gestures(ui_screen_id_t route, const dial_input_t *in,
                   bool press_ok)
{
    if (in == NULL) {
        return;
    }
    const bool motor = route == SCREEN_MOTOR;
    const bool servo = servo_screen_owns(route);
    if (in->holds != 0u) {
        if (motor) {
            (void)motor_screen_dial_hold();
        } else if (servo) {
            (void)servo_screen_dial_hold();
        }
        /* A press after a hold in one frame goes nowhere: the bench is
         * SAFE then. */
        return;
    }
    if (in->presses == 0u || !press_ok) {
        return;
    }
    if (motor) {
        (void)motor_screen_dial_press();
    } else if (servo) {
        (void)servo_screen_dial_press();
    }
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
    if (out->has_value) {
        out->value.value = value;
        out->value.was = bld->was;
    }
}
