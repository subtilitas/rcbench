/*
 * The SERVO screen's limit run on a Hitec servo, for test_servo_limit_hitec.c:
 * the screen of servo_rig.h over the Hitec programmer's loopback of
 * hitec_prog_rig.h (the overlay's model, the HITEC page, the servo model of
 * hitec_sim.h on port CH at GP13), the horn behind servo_sim's current
 * model, the INA3221's CH1 windows from it every 50ms, and a bench that
 * goes SAFE and ARMED as the screen asks, an ARM refused while the
 * programmer holds the port, as panel_arm.c refuses it.
 *
 * The horn: a D series servo's target for the pulse, t = (us - 750) x 4,
 * through the registers NEUTRAL 0xC2 and the end points 0xB2 and 0xB0
 * (hitec_live.h's mapping), 0.15us of horn a count from 1500us; the
 * D485HW's 4841 and 4811 counts are 726 and 722us, past both stops at 1150
 * and 1880us.  A classic servo's horn: the share of the way from 1500us to
 * 900us or 2100us times the end cell of that side, 2.2us a count (cell
 * 0x14 the short pulses on a CW servo); the HS-5645MG's 162 and 194 are
 * 356 and 427us.  With hl.enc the AS5600 reads the surface at 0.06° a us
 * of the horn every 40ms, its centre count 2048.  One frame of the rig is
 * 1ms; the screen ticks every 20ms.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "servo_rig.h"
/* hitec_sim.h has a fresh() of its own: the SERVO screen's keeps the name. */
#define fresh hitec_sim_fresh
#include "hitec_prog_rig.h"
#undef fresh

#include "hitec_panel.h"
#include "hitec_panel_run.h"
#include "sense_link.h"
#include "servo_limrun.h"
#include "servo_sim.h"
#include "servo_source.h"
#include "settings_sensors.h"

/* The LIMIT RUN settings: LIMIT RUN across both columns, SERVO PULSES the
 * left column of row 1, SERVO the right one. */
#define HL_MODE_X  LROW_X(0)
#define HL_MODE_Y  LROW_Y(0)
#define HL_PULSE_X LROW_X(0)
#define HL_SERVO_X LROW_X(1)
#define HL_ROW1_Y  LROW_Y(1)

static struct {
    servo_sim_t sv;
    uint16_t cmd;
    bool armed;
    bool pd;                    /* a PD mini answers on the SERVO screen */
    bool pd_out;                /* its output                            */
    bool enc;                   /* the AS5600 reads the surface          */
    int last_raw;
    uint32_t still_from;
    float sum;
    unsigned n;
    uint32_t win_end;
    uint16_t win_no;
    unsigned disarms, arms, dropped, offs, releases;
    unsigned opens_txt, ends;
    char report[8192];
    size_t len;
} hl;

static inline void hl_drain(void)
{
    const char *text = NULL;

    for (;;) {
        const servo_test_out_t o = servo_screen_test_peek(&text);

        if (o == SERVO_TEST_OUT_NONE) {
            return;
        }
        if (o == SERVO_TEST_OUT_OPEN_TXT) {
            hl.opens_txt++;
        } else if (o == SERVO_TEST_OUT_END) {
            hl.ends++;
        } else if (o == SERVO_TEST_OUT_TXT) {
            const size_t n = strlen(text);

            if (hl.len + n + 2u < sizeof(hl.report)) {
                memcpy(hl.report + hl.len, text, n);
                hl.len += n;
                hl.report[hl.len++] = '\n';
                hl.report[hl.len] = '\0';
            }
        }
        servo_screen_test_pop();
    }
}

/* The AS5600's centre count; the surface's degrees a us of the horn. */
#define HL_CENTRE_COUNT 2048
#define HL_DEG_PER_US   0.06f

/* Where the horn of a classic servo on the line stands for the pulse. */
static inline uint16_t hl_horn_c(void)
{
    const bool low = hl.cmd < 1500u;
    float share = low ? (1500.0f - (float)hl.cmd) / 600.0f
                      : ((float)hl.cmd - 1500.0f) / 600.0f;
    share = share > 1.0f ? 1.0f : share;
    const bool cw = (g_drv.eeprom[0x1B] & 1u) != 0u;
    const uint8_t cell = (low == cw) ? g_drv.eeprom[0x14] : g_drv.eeprom[0x13];
    const float off = share * 2.2f * (float)cell;
    return (uint16_t)lroundf(low ? 1500.0f - off : 1500.0f + off);
}

/* Where the horn of the servo on the line stands for the pulse. */
static inline uint16_t hl_horn(void)
{
    if (hitec_prog_series(model()) == HITEC_CLASSIC) {
        return hl_horn_c();
    }
    int32_t t = ((int32_t)hl.cmd - 750) * 4;
    t = t < 0 ? 0 : t > 6000 ? 6000 : t;
    const float mid = (float)g_drv.dreg[0xC2];
    const float pos = t <= 3000
        ? mid - (float)(3000 - t) * (mid - (float)g_drv.dreg[0xB2]) / 2600.0f
        : mid + (float)(t - 3000) * ((float)g_drv.dreg[0xB0] - mid) / 2600.0f;
    return (uint16_t)lroundf(1500.0f + (pos - mid) * 0.15f);
}

/* What the panel does with the screen's commands; the IOmcu's ARM. */
static inline void hl_commands(void)
{
    servo_cmd_t c;

    while (servo_screen_take(&c)) {
        if (c.kind == SERVO_CMD_POSITION) {
            hl.cmd = c.value_us;
        } else if (c.kind == SERVO_CMD_RELEASE) {
            hl.cmd = 1500u;
            hl.releases++;
        } else if (c.kind == SERVO_CMD_DISARM) {
            hl.disarms++;
            disarm();
        } else if (c.kind == SERVO_CMD_ARM) {
            if (!hitec_panel_holds_port()
                && hitec_prog_state(model()) == HITEC_PROG_PWM && arm()) {
                hl.arms++;
            } else {
                hl.dropped++;
            }
        }
    }
}

/* The PD mini's reading on the SERVO screen, its output as it stands. */
static inline void hl_pd_reading(void)
{
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.output = hl.pd_out;
    st.set_v = supply_screen_set_v();
    st.set_i = supply_screen_set_i();
    st.v = hl.pd_out ? st.set_v : 0.0f;
    st.samples = 1u;
    st.taken_ms = g_ms;
    st.stamped = true;
    servo_screen_supply(&st);
}

/* The surface's angle as the AS5600 reads it, with its still time. */
static inline void hl_encoder(void)
{
    const float deg = (hl.sv.position_us - 1500.0f) * HL_DEG_PER_US;
    const int raw = (HL_CENTRE_COUNT + (int)lroundf(deg * 4096.0f / 360.0f))
                    & 4095;
    if (abs(raw - hl.last_raw) > 1) {
        hl.last_raw = raw;
        hl.still_from = g_ms;
    }
    const uint32_t still = g_ms - hl.still_from;
    const servo_test_enc_t e = {
        .valid = true, .raw = (uint16_t)raw,
        .still_ms = (uint16_t)(still > 60000u ? 60000u : still),
        .taken_ms = g_ms,
    };
    servo_screen_encoder(&e);
}

static inline void hl_ms(uint32_t ms)
{
    for (uint32_t k = 0; k < ms; ++k) {
        pms();
        const float amps = hl.armed ? servo_sim_step(&hl.sv, hl_horn(), g_ms)
                                    : 0.0f;
        hl.sum += amps;
        hl.n++;
        if ((int32_t)(g_ms - hl.win_end) >= 0) {
            const int ma = (int)(hl.sum / (float)hl.n * 1000.0f + 0.5f);
            const sense_link_win_t w = {
                .number = ++hl.win_no, .current = true, .voltage = true,
                .mean_ma = (int16_t)ma, .max_ma = (int16_t)ma,
                .min_ma = (int16_t)ma, .mean_mv = 6000u, .min_mv = 6000u,
                .taken_ms = g_ms, .stamped = true,
            };
            hl.win_end += 50u;
            hl.sum = 0.0f;
            hl.n = 0u;
            servo_screen_window(&w, SERVO_SOURCE_INA3221, 2u);
        }
        if (hl.enc && g_ms % 40u == 0u) {
            hl_encoder();
        }
        if (g_ms % 20u != 0u) {
            continue;
        }
        servo_screen_clock(g_ms);
        servo_screen_set_drive((uint8_t)(1u + CH), PIN,
                               hitec_prog_state(model()) == HITEC_PROG_PWM
                                   && !hitec_panel_holds_port());
        hl_commands();
        /* The render loop's frame: the model and the screen take the bench
         * as the control task last published it, from the one snapshot. */
        model_feed(&g_f);
        hl.armed = g_f.armed;
        servo_screen_set_armed(hl.armed);
        supply_cmd_t sc;
        while (supply_screen_poll_cmd(&sc)) {
            if (sc.off) {
                hl.offs++;
                hl.pd_out = false;
            } else if (sc.on) {
                hl.pd_out = true;
            }
        }
        supply_screen_set_output(hl.pd_out);
        if (hl.pd && g_ms % 100u == 0u) {
            hl_pd_reading();
        }
        scr->tick(0.02f);
        servo_screen_service();
        hl_drain();
    }
}

/**
 * @brief The SERVO screen armed on the Hitec bench: the INA3221 the rail's
 *        meter, port CH in PWM with the D485HW on it, the Hitec programmer's
 *        model in PWM, TYPE WIDE (800 to 2200us).
 * @param pd a PD mini answers on the SERVO screen with its output on
 */
static inline void hl_fresh(bool pd)
{
    servo_sim_cfg_t sc;

    fresh();
    scr->enter();
    memset(&hl, 0, sizeof(hl));
    rig_at(pd ? SUPPLY_ON : SUPPLY_NONE, true);
    servo_d485();
    g_card = true;
    servo_sim_defaults(&sc);                /* stops at 1150 and 1880us */
    servo_sim_init(&hl.sv, &sc);
    hl.sv.position_us = 1500.0f;
    hl.cmd = 1500u;
    hl.pd = pd;
    hl.pd_out = pd;
    settings_set(SET_INA_FIT(0), 1.0f);
    /* 0x40 CH1 watches the port, as a 0.21 bench seeds it. */
    settings_set(SET_INA_W(0), (float)(SENS_W_PORT_1 + CH));
    settings_set(SET_SERVO_TARGET, (float)(1u + CH));
    settings_set(SET_HITEC_SERIES, (float)HITEC_D);
    servo_screen_set_link(true);
    servo_screen_source(SERVO_SOURCE_PDMINI, 1u);
    servo_screen_source(SERVO_SOURCE_INA3221, 2u);
    choose_type(2);                         /* WIDE: 800 to 2200us */
    hl.win_end = g_ms + 50u;
    (void)arm();
    prun(10u);
    hl.armed = g_f.armed;
    servo_screen_set_armed(hl.armed);
    if (pd) {
        hl_pd_reading();
    }
    hl_ms(400u);
}

/* TEST's LIMIT RUN settings, the rows tapped as asked, back to TEST. */
static inline void hl_settings(bool hitec, bool prog)
{
    open_run_settings(1);
    if (hitec) {
        tap(HL_SERVO_X, HL_ROW1_Y);
    }
    if (prog) {
        tap(HL_MODE_X, HL_MODE_Y);
    }
    scr->tick(0.02f);
}

/* START LIMIT RUN on TEST held for 2.3s: from the run's settings, the
 * home tag climbs back to TEST's cards first. */
static inline void hl_hold_start(void)
{
    (void)home_tag();
    open_test();
    ev(SV_START_X, SV_START_Y(1), TOUCH_EVENT_DOWN, 1);
    hl_ms(2300u);
    ev(SV_START_X, SV_START_Y(1), TOUCH_EVENT_UP, 1);
    hl_ms(20u);
}

static inline void hl_run_out(void)
{
    for (int i = 0; i < 1200 && servo_screen_limit_running(); ++i) {
        hl_ms(500u);
    }
    hl_ms(2000u);
}
