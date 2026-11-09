/*
 * RUDDER AUTO through the SERVO screen on a KST servo (kst_panel_rig.h),
 * for test_servo_rauto_kst.c and test_servo_rauto_seq.c: the servo behind
 * the KST horn model, the AS5600 or the IMU on the surface, the INA3221's
 * windows, the bench's ARM and DISARM, the PD mini's ON and OFF, the
 * control task's PROG 5V, and the rbmod carrier's port.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <math.h>

#include "kst_panel_rig.h"
#include "hold_flash.h"
#include "imu.h"
#include "log_name.h"
#include "servo_rudder_auto.h"
#include "servo_rudder_auto_int.h"
#include "servo_ready_int.h"
#include "outputs_screen.h"
#include "outputs_spots.h"
#include "port_view.h"
#include "prog5v_box.h"
#include "prog_choice.h"
#include "prog_registry.h"
#include "settings_sensors.h"
#include "ui_board.h"
#include "wit_frame.h"

/** @brief The AS5600's center count. */
#define CENTER_COUNT 2048
/** @brief The surface's degrees per microsecond of the horn. */
#define DEG_PER_US 0.06f
/** @brief RUDDER's ZERO, AUTO and START (servo_spots.h). */
#define ZERO_X 98
/* RESET MAX, right of ZERO on its row (servo_rudder_int.h). */
#define RESET_X 268

static struct {
    servo_sim_t sv;
    uint16_t cmd;
    bool armed;
    unsigned offs;          /* the supply's OFF taken */
    uint32_t win_end, win_no;
    float sum;
    unsigned n;
    unsigned disarms, arms, dropped;
    unsigned opens_csv, opens_txt, ends;
    int prefix;
    char report[16384];
    size_t len;
    /* The encoder's still time. */
    int last_raw;
    uint32_t still_from;
    bool enc;               /* readings arrive */
    /* The rbmod carrier's port pulse as the panel's link holds it
     * (servo_link_prog_service(), prog_link_next()): off at once while the
     * panel does not ask for it (prog_choice_want_pwm()), on only while
     * SAFE. */
    bool port_rule;
    bool port_pwm;
    /* The IMU on the surface in the AS5600's place, through its filter. */
    bool imu;
    imu_t m;
    bool imu_ok;
    uint32_t imu_taken;
    /* The control task's PROG_SUPPLY: an ON or OFF the boxes asked, read
     * back p5_delay ms after it was taken (ON) or at once (OFF). */
    bool p5_task;
    uint32_t p5_delay;
    bool p5_pending, p5_want;
    uint32_t p5_due;
    unsigned p5_ons, p5_offs;
    /* ARM dropped by the bench, whatever asks it. */
    bool no_arm;
    unsigned pd_ons;        /* the supply's ON taken */
    /* The servo rail as the INA3221 reads it: 6000mV while its supply is
     * on, 0V while off -- the carrier's PROG 5V as the box reads it back,
     * the PD mini's output where it feeds the rail -- and a window's mean
     * the share of its 50ms the rail was on. */
    bool rail_was;
    uint32_t rail_from;
    bool rail_fed;          /* fed from elsewhere: on whatever is off */
    bool rail_dead;         /* a broken feed: off whatever is on */
    /* The feed's output rises rail_rise ms after the feed reads ON, as
     * the station's PD mini reads its output ON before the rail has
     * risen; 0 at once. */
    uint32_t rail_rise;
    bool feed_was;
    uint32_t feed_from;
    /* The IOmcu's failsafe (safety_gate.h): latched (FAULT 01) and the
     * heartbeat distrusted (FAULT 10) as the IOmcu holds them; STATUS
     * shows them after ra_faults().  The supply's and PROG 5V's ON are
     * refused in either; a CLEAR taken (servo_screen_take_clear()) and
     * ARM's own CLEAR lift the latch unless clear_dead or line_out, and
     * the panel reads STATUS after the CLEAR. */
    bool latched, line_out, clear_dead;
    uint16_t faults_more;   /* other STATUS bits shown with them */
    unsigned clears, refused;
    /* The PD mini's last reading: a set point moved since is answered
     * with a reading of its own on the next 20ms, unless pd_deaf. */
    bool pd_sent, pd_sent_on, pd_deaf;
    float pd_sent_v;
    /* The horn's long pulses from hi_reg, whatever the servo holds: a side
     * no write moves. */
    bool hi_fixed;
    uint8_t hi_reg[KST_REG_COUNT];
} ra;

/** @brief The IOmcu's STATUS faults, read now: the failsafe as the rig's
 *         IOmcu holds it. */
static inline void ra_faults(void)
{
    ui_bench_status_t st = *ui_router_status();
    st.faults = (uint16_t)(ra.faults_more
                           | (ra.latched ? LINK_FAULT_LINK_SILENT : 0u)
                           | (ra.line_out ? LINK_FAULT_HEARTBEAT : 0u));
    ui_router_set_status(&st);
}

/** @brief Whether the rig's IOmcu is in its failsafe: no supply ON. */
static inline bool ra_failsafe(void)
{
    return ra.latched || ra.line_out;
}

/** @brief IMU FILTER on the IMU of these cases. */
#define RA_IMU_FILTER 10

static inline void ra_drain(void)
{
    const char *text = NULL;
    for (;;) {
        const servo_test_out_t o = servo_screen_test_peek(&text);
        if (o == SERVO_TEST_OUT_NONE) {
            return;
        }
        ra.prefix = servo_screen_test_prefix();
        if (o == SERVO_TEST_OUT_OPEN) {
            ra.opens_csv++;
        } else if (o == SERVO_TEST_OUT_OPEN_TXT) {
            ra.opens_txt++;
        } else if (o == SERVO_TEST_OUT_END) {
            ra.ends++;
        } else if (o == SERVO_TEST_OUT_TXT) {
            const size_t n = strlen(text);
            if (ra.len + n + 2u < sizeof(ra.report)) {
                memcpy(ra.report + ra.len, text, n);
                ra.len += n;
                ra.report[ra.len++] = '\n';
                ra.report[ra.len] = '\0';
            }
        }
        servo_screen_test_pop();
        if (o == SERVO_TEST_OUT_OPEN) {
            servo_screen_test_files(7, true);   /* the card's answer */
        }
    }
}

static inline void ra_pd_reading(bool on);

/* What the panel does with the screen's commands. */
static inline void ra_commands(void)
{
    if (servo_screen_take_clear()) {
        ra.clears++;
        if (!ra.clear_dead && !ra.line_out) {
            ra.latched = false;
        }
        ra_faults();
    }
    servo_cmd_t c;
    while (servo_screen_take(&c)) {
        if (c.kind == SERVO_CMD_POSITION) {
            ra.cmd = c.value_us;
        } else if (c.kind == SERVO_CMD_RELEASE) {
            ra.cmd = 1500u;
        } else if (c.kind == SERVO_CMD_DISARM) {
            /* The IOmcu drives no pulse while SAFE. */
            ra.disarms++;
            ra.armed = false;
            ra.cmd = 1500u;
        } else if (c.kind == SERVO_CMD_ARM) {
            /* ARM writes its own CLEAR once the line is trusted. */
            if (!ra.line_out && !ra.clear_dead) {
                ra.latched = false;
            }
            if (ra.no_arm || ra_failsafe() || kst_panel_holds_port()
                || kst_prog_state(M) != KST_PROG_PWM) {
                ra.dropped++;
            } else {
                ra.arms++;
                ra.armed = true;
            }
        }
    }
    g_rig.safe = !ra.armed;
    servo_screen_set_armed(ra.armed);
    supply_cmd_t sc;
    while (supply_screen_poll_cmd(&sc)) {
        if (sc.off) {
            ra.offs++;
            g_rig.sup_on = false;
        } else if (sc.on && ra_failsafe()) {
            ra.refused++;       /* the IOmcu refuses it: stays off */
        } else if (sc.on) {
            ra.pd_ons++;
            g_rig.sup_on = true;
            ra_pd_reading(true);
        }
    }
    if (ra.pd_sent && !ra.pd_deaf
        && fabsf(supply_screen_set_v() - ra.pd_sent_v) > 0.001f) {
        ra_pd_reading(ra.pd_sent_on);
    }
}

/* The control task's side of PROG 5V: what the boxes and the bench
 * asked, published as the PROG_SUPPLY page reads it back. */
static inline void ra_p5(void)
{
    prog5v_cmd_t c;
    if (ra.p5_task && prog5v_box_poll_cmd(&c)) {
        ra.p5_pending = true;
        ra.p5_want = c.on;
        ra.p5_due = g_rig.ms + (c.on ? ra.p5_delay : 0u);
        if (c.on && ra_failsafe()) {
            ra.refused++;       /* the IOmcu refuses it: stays off */
            ra.p5_pending = false;
        } else if (c.on) {
            ra.p5_ons++;
        } else {
            ra.p5_offs++;
        }
    }
    if (ra.p5_pending && (int32_t)(g_rig.ms - ra.p5_due) >= 0) {
        ra.p5_pending = false;
        prog5v_box_publish(ra.p5_want ? 3u : 2u, 21u, PROG5V_HELD_NONE);
    }
}

/* The surface's angle as the AS5600 reads it, with its still time. */
static inline void ra_encoder(void)
{
    const float deg = (ra.sv.position_us - 1500.0f) * DEG_PER_US;
    const int raw = (CENTER_COUNT + (int)lroundf(deg * 4096.0f / 360.0f))
                    & 4095;
    if (abs(raw - ra.last_raw) > 1) {
        ra.last_raw = raw;
        ra.still_from = g_rig.ms;
    }
    const uint32_t still = g_rig.ms - ra.still_from;
    const servo_test_enc_t e = {
        .valid = true, .raw = (uint16_t)raw,
        .still_ms = (uint16_t)(still > 60000u ? 60000u : still),
        .taken_ms = g_rig.ms,
    };
    servo_screen_encoder(&e);
}

/* The surface's angle as the IMU reads it at 10Hz through IMU FILTER
 * RA_IMU_FILTER, handed on as the panel does: each frame once, a stale
 * reading once, the settle allowance with it. */
static inline void ra_imu(void)
{
    if (g_rig.ms % 100u == 0u) {
        uint8_t b[WIT_FRAME_LEN];
        wit_angle_frame(b, (ra.sv.position_us - 1500.0f) * DEG_PER_US, 0.0f,
                        0.0f);
        imu_feed(&ra.m, b, sizeof b, g_rig.ms);
    }
    imu_tick(&ra.m, g_rig.ms);
    imu_reading_t r;
    const bool ok = imu_reading(&ra.m, &r);
    servo_screen_set_imu_settle(imu_filter_settle_ms(RA_IMU_FILTER,
                                                     r.rate_dhz));
    if (ok == ra.imu_ok && (!ok || r.taken_ms == ra.imu_taken)) {
        return;
    }
    ra.imu_ok = ok;
    ra.imu_taken = r.taken_ms;
    const servo_angle_in_t in = {
        .valid = ok, .deg = imu_deg(&r, IMU_AXIS_X, false),
        .still_ms = imu_still_ms(&r, IMU_AXIS_X),
        .taken_ms = ok ? r.taken_ms : g_rig.ms,
    };
    servo_test_enc_t e;
    (void)servo_angle_enc(&in, &e);
    servo_screen_encoder(&e);
}

/** @brief Whether the servo rail is fed now. */
static inline bool ra_rail_on(void)
{
    if (ra.rail_dead || ra.rail_fed) {
        return ra.rail_fed;
    }
    if (g_rig.fixed) {
        return prog5v_box_on();
    }
    return !g_rig.rail || g_rig.sup_on;
}

/** @brief The rail's mean over the window ending now, mV. */
static inline uint16_t ra_rail_mv(void)
{
    if (!ra.rail_was) {
        return 0u;
    }
    const uint32_t on = g_rig.ms - ra.rail_from;
    return (uint16_t)(on >= 50u ? 6000u : 6000u * on / 50u);
}

static inline void ra_ms(uint32_t ms)
{
    for (uint32_t k = 0; k < ms; ++k) {
        rig_frame();
        const float amps = servo_sim_step(
            &ra.sv,
            sim_horn_cmd(ra.hi_fixed && ra.cmd > 1500u ? ra.hi_reg
                         : g_rig.on_rail ? g_rig.sim.acting : g_rig.sim.reg,
                         ra.cmd, 4.0f, false),
            g_rig.ms);
        ra.sum += amps;
        ra.n++;
        const bool feed = ra_rail_on();
        if (feed && !ra.feed_was) {
            ra.feed_from = g_rig.ms;
        }
        ra.feed_was = feed;
        const bool rail = feed && g_rig.ms - ra.feed_from >= ra.rail_rise;
        if (rail && !ra.rail_was) {
            ra.rail_from = g_rig.ms;
        }
        ra.rail_was = rail;
        if ((int32_t)(g_rig.ms - ra.win_end) >= 0) {
            const uint16_t mv = ra_rail_mv();
            const int ma = (int)(ra.sum / (float)ra.n * 1000.0f + 0.5f);
            const sense_link_win_t w = {
                .number = (uint16_t)++ra.win_no, .current = true,
                .voltage = true, .mean_ma = (int16_t)ma,
                .max_ma = (int16_t)ma, .min_ma = (int16_t)ma,
                .mean_mv = mv, .min_mv = mv,
                .taken_ms = g_rig.ms, .stamped = true,
            };
            ra.win_end += 50u;
            ra.sum = 0.0f;
            ra.n = 0u;
            servo_screen_window(&w, SERVO_SOURCE_INA3221, 2u);
        }
        if (ra.imu) {
            ra_imu();
        } else if (ra.enc && g_rig.ms % 40u == 0u) {
            ra_encoder();
        }
        if (g_rig.ms % 20u == 0u) {
            servo_screen_clock(g_rig.ms);
            ra_p5();
            if (!prog_choice_want_pwm()) {
                ra.port_pwm = false;
            } else if (!ra.armed) {
                ra.port_pwm = true;
            }
            servo_screen_set_drive(1u + RIG_PORT, RIG_PIN,
                                   kst_prog_state(M) == KST_PROG_PWM
                                       && !kst_panel_holds_port()
                                       && (!ra.port_rule || ra.port_pwm));
            ra_commands();
            scr->tick(0.02f);
            servo_screen_service();
            ra_drain();
        }
    }
}

/* One reading of a PD mini that answers, its output on or off; with
 * ra.pd_deaf at the set point of its last reading, whatever SUPPLY asks. */
static inline void ra_pd_reading(bool on)
{
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.output = on;
    st.set_v = (ra.pd_deaf && ra.pd_sent) ? ra.pd_sent_v
                                          : supply_screen_set_v();
    st.set_i = supply_screen_set_i();
    st.v = on ? st.set_v : 0.0f;
    st.samples = 1u;
    st.taken_ms = g_rig.ms;
    st.stamped = true;
    servo_screen_supply(&st);
    ra.pd_sent = true;
    ra.pd_sent_on = on;
    ra.pd_sent_v = st.set_v;
}

/**
 * @brief The SERVO screen on the rig, armed, the INA3221 the rail's meter,
 *        the port in PWM with the servo on it, the AS5600 or the IMU
 *        reading the surface; RUDDER with its zero taken and AUTO's box
 *        shown.
 * @param pd a PD mini answers with its output on behind the servo
 * @param zero ZERO tapped at the center
 * @param imu the IMU through IMU FILTER RA_IMU_FILTER is ANGLE SOURCE
 */
static inline void ra_setup_on(bool pd, bool zero, bool imu)
{
    servo_sim_cfg_t sc;
    fresh_screen();
    memset(&ra, 0, sizeof(ra));
    ra_faults();                /* no failsafe from a case before */
    ra.imu = imu;
    imu_init(&ra.m);
    imu_set_filter(&ra.m, RA_IMU_FILTER);
    if (imu) {
        settings_set(SET_ANGLE_SOURCE, (float)SERVO_ANGLE_IMU);
    }
    ra.last_raw = -100;
    memcpy(g_rig.sim.reg, rig_servo().r, sizeof(g_rig.sim.reg));
    servo_sim_defaults(&sc);
    sc.stop_lo_us = 700u;
    sc.stop_hi_us = 2300u;
    servo_sim_init(&ra.sv, &sc);
    ra.sv.position_us = 1500.0f;
    ra.cmd = 1500u;
    ra.armed = true;
    ra.enc = true;
    settings_set(SET_INA_FIT(0), 1.0f);
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTER, (float)CENTER_COUNT);
    servo_screen_source(SERVO_SOURCE_PDMINI, 1u);
    servo_screen_source(SERVO_SOURCE_INA3221, 2u);
    if (pd) {
        g_rig.rail = true;
        g_rig.on_rail = true;
        g_rig.sup_on = true;
        g_rig.sup_mv = (uint16_t)lroundf(supply_screen_set_v() * 1000.0f);
        g_rig.sup_ma = (uint16_t)lroundf(supply_screen_set_i() * 1000.0f);
        g_rig.sim.needs_power_up = true;
    }
    link_up();
    ra.win_end = g_rig.ms + 50u;
    if (pd) {
        ra_pd_reading(true);
    }
    ra_ms(400u);
    go(SERVO_FN_RAUTO);
    if (zero) {
        screen_tap(ZERO_X, SV_RUD_ZERO_Y);
    }
    CHECK(rauto_shown());
    ra_ms(100u);
}

/**
 * @brief ra_setup_on() with the AS5600.
 * @param pd a PD mini answers with its output on behind the servo
 * @param zero ZERO tapped at the center
 */
static inline void ra_setup(bool pd, bool zero)
{
    ra_setup_on(pd, zero, false);
}

/* HOLD 2s TO START held for 2.3s. */
static inline void ra_hold_start(void)
{
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_DOWN, 1);
    ra_ms(2300u);
    ev(SV_RA_START_X, SV_RA_START_Y, TOUCH_EVENT_UP, 1);
    ra_ms(20u);
}

static inline void ra_run_out(void)
{
    for (int i = 0; i < 1200 && servo_screen_rauto_running(); ++i) {
        ra_ms(500u);
    }
    ra_ms(2000u);
}


/**
 * @brief The rbmod carrier, TARGET on PROGP01 with PORT PWM on, the PROG 5V
 *        box off and the port's pulse as the panel's link holds it.
 * @param armed the bench ARMED at the start, else SAFE
 */
static inline void carrier_port(bool armed)
{
    ra_setup(false, true);
    g_rig.fixed = true;
    ra.port_rule = true;
    ui_board_set(OUTBIND_BOARD_RBMOD);
    (void)ui_board_apply_pins(NULL);
    prog_choice_set_far_minor(LINK_MINOR_CARRIER_PWM);
    prog5v_box_publish(2u, 21u, PROG5V_HELD_NONE);
    settings_set(SET_SERVO_TARGET, (float)(1u + RIG_PORT));
    settings_set(SET_SERVO_PORT_PWM, 1.0f);
    ra.armed = armed;
    servo_screen_set_armed(armed);
    ra_ms(200u);
}

/** @brief The board and the port as the other cases take them. */
static inline void carrier_done(void)
{
    g_rig.fixed = false;
    ra.port_rule = false;
    prog5v_box_reset();
    prog_choice_reset();
    settings_set(SET_SERVO_PORT_PWM, 1.0f);
    ui_board_set(OUTBIND_BOARD_PICO_HEADER);
    (void)ui_board_apply_pins(NULL);
}

/**
 * @brief The carrier's four ports as read, @p port at FUNC SERVO PWM and
 *        the others NONE, as the OUTPUTS entry reads them.
 * @param port the chosen port, 0 for port 1
 */
static inline void carrier_ports(uint8_t port)
{
    static const uint8_t pins[4] = { 13u, 14u, 15u, 0u };
    port_view_t v;
    port_view_unknown(&v);
    v.count = 4u;
    v.has_func = true;
    for (uint8_t n = 0u; n < 4u; ++n) {
        v.pin[n] = pins[n];
        v.func[n] = n == port ? (uint8_t)LINK_PG_FN_SERVO_PWM
                              : (uint8_t)LINK_PG_FN_NONE;
    }
    port_view_set(&v);
    outputs_screen()->reset();
    outputs_screen_set_prog_ports(4u, pins);
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, OUTBIND_BOARD_RBMOD);
    outputs_screen_set_binding(&b);
}

/**
 * @brief The rbmod carrier from SAFE with PORT PWM off and the PROG 5V
 *        off, the control task answering PROG 5V after @p delay_ms.
 * @param delay_ms the read-back's delay, ms
 */
static inline void carrier_safe_off(uint32_t delay_ms)
{
    carrier_port(false);
    carrier_ports(RIG_PORT);
    settings_set(SET_SERVO_PORT_PWM, 0.0f);
    ra.p5_task = true;
    ra.p5_delay = delay_ms;
    ra_ms(200u);
}

/** @brief READY row @p i of RUDDER AUTO's card, its state and value. */
static inline rdy_row_t row_now(int i)
{
    rdy_row_t r;
    CHECK(rauto_ready_row(i, &r));
    return r;
}

