/*
 * A bench for the SERVO screen's runs (servo_rig.h): the servo model on the
 * output, SUPPLY's switch and set points as the panel carries them, the
 * INA3221's CH1 windows under the servo rail's meter, the output encoder,
 * and the files a run writes.  START TEST's place, the meter and the
 * expected-picture buffer go with it.  Taken by the suites of
 * servo_screen_rig.h, test_servo_test_panel.c and test_servo_limit_panel.c.
 *
 * Only static inline helpers and the state they use.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "servo_rig.h"

#include "sense_link.h"
#include "servo_screen.h"
#include "servo_sim.h"
#include "servo_test.h"
#include "settings.h"
#include "settings_sensors.h"
#include "supply_screen.h"
#include "ui_widgets.h"

/*
 * START TEST on TEST's AUTOMATIC TEST card and START LIMIT RUN on the card
 * under it (servo_spots.h); the box on LIVE's card and its button, from
 * servo_screen_int.h; HV SERVO at the right column's row 2 of the AUTOMATIC
 * TEST card's settings, from servo_settings_panel.c.
 */
#define START_X  SV_START_X
#define START_Y  SV_START_Y(0)
#define LIM_START_Y SV_START_Y(1)
#define BOXBTN_X SV_BOX_BTN_X
#define BOXBTN_Y SV_BOX_BTN_Y
#define HV_ROW_X LROW_X(0)
#define HV_ROW_Y LROW_Y(2)

/* A bench for the screen's run: the servo model on the output, SUPPLY's
 * switch and set points as the panel would carry them, and a reading every
 * 100ms stamped on the panel's clock. */
static struct {
    servo_sim_t sv;
    bool     out;
    bool     online;
    uint16_t samples;
    uint32_t now, next;
    uint16_t cmd;
    bool     released;
    unsigned disarms;          /* DISARMs posted                      */
    bool     stepped;          /* a position went out with no slew   */
    float    set_v_max;        /* the highest set point while it ran */
    unsigned opens, opens_txt, csv, txt, ends;
    char     report[4096];
    size_t   report_len;
    char     csv_head[256];     /* the CSV's first line */
    /* The output encoder: the horn at 0.09 degrees a us from 1500, counted
     * from 3000, read every 40ms with a 12-count still time. */
    bool     enc;
    uint32_t enc_next;
    bool     enc_anchored;
    uint16_t enc_anchor;
    uint32_t enc_anchor_ms;
    /* The supply's samples arrive with a meter count one behind the one in
     * force (servo_screen_supply_at()). */
    bool     behind;
    /* The INA3221's CH1 windows: one every 50ms with the servo's current,
     * under the meter last named (meter()), while ina is set. */
    bool     ina;
    bool     rail_set;          /* the windows' rail at rail_mv, not the
                                   supply's set point                    */
    unsigned rail_mv;
    bool     no_volt;           /* windows without the rail voltage      */
    uint32_t win_next;
    uint16_t win_number;
    unsigned wins;
    /* The supply holds its current limit, as supply.c models it. */
    bool     limits;
    char     csv_last[256];     /* the CSV's last line */
} bench;

static inline void window(const sense_link_win_t *w);

/* bench_fresh() sends no reading: the screen starts with none. */
static bool s_bench_no_first;

static inline void bench_fresh(void)
{
    fresh();
    memset(&bench, 0, sizeof(bench));
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    sc.stop_lo_us = 1000u;
    sc.stop_hi_us = 2000u;
    servo_sim_init(&bench.sv, &sc);
    bench.sv.position_us = 1500.0f;
    bench.online = true;
    bench.now  = 5000u;
    bench.next = bench.now;
    bench.cmd  = 1500u;
    servo_screen_clock(bench.now);
    servo_screen_set_armed(true);
    /* One reading before the run, as the panel always has one. */
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.set_v = supply_screen_set_v();
    st.samples = ++bench.samples;
    st.taken_ms = bench.now;
    st.stamped = true;
    if (!s_bench_no_first) {
        servo_screen_supply(&st);
    }
    scr->tick(0.02f);
}

static inline void bench_drain(void)
{
    const char *text = NULL;
    for (;;) {
        const servo_test_out_t o = servo_screen_test_peek(&text);
        if (o == SERVO_TEST_OUT_NONE) {
            return;
        }
        if (o == SERVO_TEST_OUT_OPEN) {
            ++bench.opens;
        } else if (o == SERVO_TEST_OUT_OPEN_TXT) {
            ++bench.opens_txt;
        } else if (o == SERVO_TEST_OUT_CSV) {
            if (bench.csv == 0u) {
                snprintf(bench.csv_head, sizeof(bench.csv_head), "%s", text);
            }
            snprintf(bench.csv_last, sizeof(bench.csv_last), "%s", text);
            ++bench.csv;
        } else if (o == SERVO_TEST_OUT_TXT) {
            ++bench.txt;
            const size_t n = strlen(text);
            if (bench.report_len + n + 2u < sizeof(bench.report)) {
                memcpy(bench.report + bench.report_len, text, n);
                bench.report_len += n;
                bench.report[bench.report_len++] = '\n';
                bench.report[bench.report_len] = '\0';
            }
        } else {
            ++bench.ends;
        }
        servo_screen_test_pop();
    }
}

/* No supply state reaches the screen: no PD mini heard yet. */
static bool s_bench_no_supply;

static inline void bench_frames(uint32_t ms)
{
    for (uint32_t k = 0; k < ms; k += 20u) {
        bench.now += 20u;
        servo_screen_clock(bench.now);
        supply_cmd_t c;
        while (supply_screen_poll_cmd(&c)) {
            if (c.off) {
                bench.out = false;
            } else if (c.on) {
                bench.out = true;
            }
        }
        supply_screen_set_output(bench.out);
        servo_cmd_t sc;
        while (servo_screen_take(&sc)) {
            if (sc.kind == SERVO_CMD_POSITION) {
                bench.cmd = sc.value_us;
                bench.stepped = bench.stepped || sc.slew_per_s == 0u;
            } else if (sc.kind == SERVO_CMD_RELEASE
                       || sc.kind == SERVO_CMD_DISARM) {
                bench.cmd = 1500u;
                bench.released = true;
                bench.disarms += (sc.kind == SERVO_CMD_DISARM) ? 1u : 0u;
            }
        }
        if (servo_screen_testing() && supply_screen_set_v() > bench.set_v_max) {
            bench.set_v_max = supply_screen_set_v();
        }
        float amps = bench.out ? servo_sim_step(&bench.sv, bench.cmd, bench.now) : 0.0f;
        const bool cc = bench.limits && amps > supply_screen_set_i();
        if (cc) {
            amps = supply_screen_set_i();
        }
        if (bench.ina && (int32_t)(bench.now - bench.win_next) >= 0) {
            bench.win_next += 50u;
            const int ma = (int)lroundf(amps * 1000.0f);
            const unsigned mv = bench.rail_set ? bench.rail_mv
                : bench.out ? (unsigned)lroundf(supply_screen_set_v() * 1000.0f)
                            : 0u;
            const sense_link_win_t w = {
                .number = ++bench.win_number, .current = true,
                .voltage = !bench.no_volt,
                .mean_ma = (int16_t)ma, .max_ma = (int16_t)ma,
                .min_ma = (int16_t)ma, .mean_mv = (uint16_t)mv,
                .min_mv = (uint16_t)mv, .taken_ms = bench.now, .stamped = true,
            };
            window(&w);
            ++bench.wins;
        }
        if ((int32_t)(bench.now - bench.next) >= 0 && !s_bench_no_supply) {
            bench.next += 100u;
            supply_state_t st;
            memset(&st, 0, sizeof(st));
            st.online = bench.online;
            st.ok     = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
            st.output = bench.out;
            st.set_v  = supply_screen_set_v();
            st.set_i  = supply_screen_set_i();
            st.v      = bench.out ? st.set_v : 0.0f;
            st.i      = amps;
            st.p      = st.v * st.i;
            st.mode   = !bench.out ? SUPPLY_MODE_OFF
                        : cc   ? SUPPLY_MODE_CC : SUPPLY_MODE_CV;
            st.samples  = ++bench.samples;
            st.taken_ms = bench.now;
            st.stamped = true;
            if (bench.behind) {
                /* Taken under a meter replaced since. */
                servo_screen_supply_at(&st, SERVO_SOURCE_INA3221,
                                       meter_changes - 1u);
            } else {
                servo_screen_supply(&st);
            }
        }
        if (bench.enc) {
            const int counts = (int)lroundf(
                (bench.sv.position_us - 1500.0f) * 0.09f * 4096.0f / 360.0f);
            const uint16_t raw = (uint16_t)((counts + 3000) & 4095);
            int d = ((int)raw - (int)bench.enc_anchor + 2048) & 4095;
            d -= 2048;
            if (!bench.enc_anchored || d > 12 || d < -12) {
                bench.enc_anchor = raw;
                bench.enc_anchor_ms = bench.now;
                bench.enc_anchored = true;
            }
            if ((int32_t)(bench.now - bench.enc_next) >= 0) {
                bench.enc_next += 40u;
                const servo_test_enc_t e = { true, raw,
                    (uint16_t)(bench.now - bench.enc_anchor_ms), bench.now, false, false, false, false };
                servo_screen_encoder(&e);
            }
        }
        /* After the OFF was taken and the samples, as the panel runs it. */
        servo_screen_service();
        scr->tick(0.02f);
        bench_drain();
    }
}

/* Frames drawn as the panel draws them, 20ms each, so the flashes a hold
 * or an output switched on starts, 150ms each, are over. */
static inline void settle_drawn(void)
{
    two_buffers();
    for (int i = 0; i < UI_HOLD_FLASH_MS / 10; ++i) {
        scr->render((i & 1) ? &cv1 : &cv, i & 1);
        scr->tick(0.02f);
    }
}

/* As both_whole(), inside @p r only: away from the grip, whose breathing is
 * repainted at the step it has reached rather than every frame. */
static inline bool both_whole_in(int x0, int y0, int w, int h)
{
    scr->render(&cv, 0);
    scr->render(&cv1, 1);
    servo_invalidate();
    scr->render(&cref, 0);
    for (int y = y0; y < y0 + h; ++y) {
        for (int x = x0; x < x0 + w; ++x) {
            const size_t i = (size_t)y * W + (size_t)x;
            if (fb[i] != fb1[i] || fb[i] != ref[i]) {
                return false;
            }
        }
    }
    return true;
}

/* Hold the button at START TEST's place for @p secs on the page @p tab,
 * which is opened first. */
static inline void hold_start_on(int card, float secs)
{
    open_test();
    const int y = SV_START_Y(card);
    ev(START_X, y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < (int)(secs * 50.0f + 0.5f); ++i) {
        if (bench.ina) {
            /* The INA3221's windows go on under the finger, as on the
             * panel: a run that starts before the release reads them, and
             * ends after 200ms without one. */
            bench_frames(20u);
            continue;
        }
        bench.now += 20u;
        servo_screen_clock(bench.now);
        scr->tick(0.02f);
    }
    ev(START_X, y, TOUCH_EVENT_UP, 1);
}

/* Hold START TEST for @p secs on TEST's AUTOMATIC TEST card, TEST shown
 * first. */
static inline void hold_start(float secs)
{
    hold_start_on(0, secs);
}

/* Short runs: two movements a step. */
static inline void short_runs(void)
{
    settings_set(SET_SERVO_LEN_BY, 1.0f);
    settings_set(SET_SERVO_LEN_MOVES, 2.0f);
}

/* A pin's bit in a 64-bit mask of the bench's pins. */
#define PIN(n) ((uint64_t)1u << (n))

/* What a row or a line is expected to look like, drawn here. */
static gfx_color_t *want_fb;
static gfx_canvas_t want_cv;

static inline void want_fresh(void)
{
    if (want_fb == NULL) {
        want_fb = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    gfx_canvas_init(&want_cv, want_fb, W, H, W);
}

static inline bool same_in(const gfx_color_t *a, const gfx_color_t *z, int x0,
                           int y0, int w, int h)
{
    for (int y = y0; y < y0 + h; ++y) {
        if (memcmp(a + (size_t)y * W + x0, z + (size_t)y * W + x0,
                   (size_t)w * sizeof(gfx_color_t)) != 0) {
            return false;
        }
    }
    return true;
}

/* A CH1 window with both quantities and no clipped sample. */
static inline sense_link_win_t rail_win(int ma, unsigned mv)
{
    static uint16_t number;
    number = (uint16_t)(number + 1u);
    const sense_link_win_t w = {
        .number = number, .current = true, .voltage = true,
        .mean_ma = (int16_t)ma, .max_ma = (int16_t)ma, .min_ma = (int16_t)ma,
        .mean_mv = (uint16_t)mv, .min_mv = (uint16_t)mv,
    };
    return w;
}

/* The meter the panel names, counted as servo_source.c counts it. */
static inline void meter(servo_source_id_t id)
{
    if (id != meter_id) {
        meter_id = id;
        ++meter_changes;
    }
    servo_screen_source(id, meter_changes);
}

/* A window taken in a poll that decided the meter last named. */
static inline void window(const sense_link_win_t *w)
{
    servo_screen_window(w, meter_id, meter_changes);
}

/* A bench with the INA3221 on in SETUP and the rail's meter, its windows
 * arriving, ready for START TEST. */
static inline void bench_on_ina(void)
{
    bench_fresh();
    settings_set(SET_INA_FIT(0), 1.0f);
    short_runs();
    meter(SERVO_SOURCE_PDMINI);
    meter(SERVO_SOURCE_INA3221);
    bench.ina = true;
    bench.win_next = bench.now;
    bench_frames(200u);
}
