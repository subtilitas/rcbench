/*
 * The rig of the automatic servo test's suites against the servo model and
 * the supply model: test_servo_test.c, test_servo_test_judge.c,
 * test_servo_test_replay.c and test_servo_test_enc.c.
 *
 * The rig: servo_sim's servo, supply_sim's supply and the run stepped
 * together at the panel's 20ms frame; the supply's readings and rows; an
 * encoder reading the horn's angle from a count; and the run's outbox
 * kept, its CSV rows checked as they come, its report and its files
 * counted.  Every function is static inline and `g` is the rig's one
 * instance.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "servo_sim.h"
#include "servo_test.h"
#include "supply.h"

#define FRAME_MS 20u            /* the panel's loop, about 50 frames a second */
#define LO_US    1100u
#define HI_US    1900u
#define CENTRE   1500u

/* The servo, the supply and the run, stepped together. */
typedef struct {
    servo_test_t t;
    servo_sim_t  servo;
    supply_sim_t sup;
    uint32_t now;
    uint16_t cmd_us;
    bool     armed;
    float    v_min, v_max;
    float    brownout_v;     /* below this the servo does not move */
    uint16_t samples;
    uint32_t read_every_ms;
    uint32_t next_read;
    bool     readings;       /* readings arrive */
    bool     online;
    bool     take_set;       /* the supply takes a set point */
    bool     take_on;        /* and switches on */
    uint8_t  trip;
    bool     drain;
    bool     frozen;         /* the supply's count and current stand still */
    bool     cc_limits;      /* the supply holds its current limit: a load
                                that wants more reads the limit, in CC   */
    unsigned cc_readings;    /* readings it gave in CC                   */
    uint32_t on_ms;          /* how long the output was on               */
    uint16_t sample_step;    /* how far its count moves a reading */
    float    frozen_a;
    float    set_v_max;      /* the highest voltage ever asked */
    bool     released;
    bool     off_asked;
    /* The output encoder: the horn's angle at enc_deg_per_us from 1500us,
     * read from a count with its centre at ENC_CENTRE_COUNT, every
     * enc_every ms, with the IOmcu's still time (a 12-count anchor)
     * kept at the frame rate. */
    bool     enc_model_on;
    float    enc_deg_per_us;
    uint32_t enc_every, enc_next;
    bool     enc_valid;
    int      enc_flutter;    /* counts, + and - on alternate readings */
    int      enc_by_move;    /* counts, + and - on alternate pairs of
                                commands: the same end reads either side
                                of its place from one move to the next */
    bool     enc_weak;       /* every reading says the field is too weak */
    unsigned cmds;           /* commands taken */
    uint16_t enc_raw;        /* the count last read */
    uint16_t enc_at_cmd[16]; /* ... as each command found it */
    uint32_t row_lag_ms;     /* the supply's rows are stamped this much older */
    bool     check_rows;     /* keep_csv() checks where a settle was logged */
    unsigned settle_rows;
    bool     enc_anchored;
    uint16_t enc_anchor;
    uint32_t enc_anchor_ms;
    unsigned enc_sent;
    /* What came out of the outbox. */
    unsigned opens, csv, txt, ends;
    char    *csv_text;
    size_t   csv_len, csv_cap;
    char     report[8192];
    size_t   report_len;
} rig_t;

static rig_t g;

#define ENC_CENTRE_COUNT 3000u
#define ENC_DEG_PER_US   0.09f      /* 90 degrees across 1000us */

static inline void cfg_defaults(servo_test_cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    c->steps_v[0] = 4.8f;
    c->steps_v[1] = 6.0f;
    c->step_count = 2u;
    c->brownout   = false;
    c->i_limit    = 3.0f;
    c->centre_us  = CENTRE;
    c->end_lo_us  = LO_US;
    c->end_hi_us  = HI_US;
    c->settle_ms  = 500u;
    c->dwell_ms   = 200u;
    c->by_moves   = true;
    c->moves      = 4u;
    c->time_s     = 5u;
    c->stall_a    = 2.0f;
    c->report     = true;
    snprintf(c->dut, sizeof(c->dut), "DS3218");
    snprintf(c->type, sizeof(c->type), "STANDARD PWM");
    c->min_us = 1000u;
    c->max_us = 2000u;
    c->frame_hz = 50u;
    c->travel_deg = 90u;
    c->range_pct = 80u;
    snprintf(c->firmware, sizeof(c->firmware), "0.0.0");
}

static inline void rig_fresh(void)
{
    free(g.csv_text);
    memset(&g, 0, sizeof(g));
    servo_test_init(&g.t);
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    /* Free at both ends the test drives to: 1100 and 1900us. */
    sc.stop_lo_us = 1000u;
    sc.stop_hi_us = 2000u;
    servo_sim_init(&g.servo, &sc);
    g.servo.position_us = (float)CENTRE;
    supply_sim_init(&g.sup);
    supply_sim_set(&g.sup, 6.0f, 2.0f);
    g.now = 1000u;
    g.cmd_us = CENTRE;
    g.armed = true;
    g.v_min = 3.3f;
    g.v_max = 21.0f;
    g.brownout_v = 0.0f;
    g.read_every_ms = 100u;
    g.next_read = g.now;
    g.readings = true;
    g.online = true;
    g.take_set = true;
    g.take_on = true;
    g.drain = true;
}

/* The ';' before cell @p idx (0-based, idx at least 1) of a CSV row, or
 * NULL when the row has fewer cells. */
static inline const char *csv_cell(const char *line, unsigned idx)
{
    const char *p = line;
    for (unsigned k = 0u; k < idx; ++k) {
        p = strchr(k == 0u ? p : p + 1, ';');
        if (p == NULL) {
            return NULL;
        }
    }
    return p;
}

static inline void keep_csv(const char *line)
{
    if (g.check_rows && line[0] >= '0' && line[0] <= '9') {
        /* A row with a settle on it was taken at or after the reading that
         * found the settle (the row's time is from the run's start). */
        const char *last = csv_cell(line, 14u);
        if (last != NULL && last[1] != ';') {
            unsigned long sec = 0u, ms = 0u;
            CHECK_EQ(sscanf(line, "%lu.%lu", &sec, &ms), 2);
            const uint32_t at = g.t.start_ms + (uint32_t)(sec * 1000u + ms);
            CHECK((int32_t)(at - g.t.enc_travel_at_ms) >= 0);
            ++g.settle_rows;
        }
    }
    const size_t n = strlen(line);
    if (g.csv_len + n + 2u > g.csv_cap) {
        g.csv_cap = (g.csv_cap + n + 2u) * 2u;
        REQUIRE_REALLOC(g.csv_text, g.csv_cap);
    }
    memcpy(g.csv_text + g.csv_len, line, n);
    g.csv_len += n;
    g.csv_text[g.csv_len++] = '\n';
    g.csv_text[g.csv_len] = '\0';
}

static inline void drain(void)
{
    const char *text = NULL;
    for (;;) {
        const servo_test_out_t k = servo_test_peek(&g.t, &text);
        if (k == SERVO_TEST_OUT_NONE) {
            return;
        }
        switch (k) {
        case SERVO_TEST_OUT_OPEN: ++g.opens; break;
        case SERVO_TEST_OUT_CSV:  ++g.csv; keep_csv(text); break;
        case SERVO_TEST_OUT_TXT: {
            ++g.txt;
            const size_t n = strlen(text);
            if (g.report_len + n + 2u < sizeof(g.report)) {
                memcpy(g.report + g.report_len, text, n);
                g.report_len += n;
                g.report[g.report_len++] = '\n';
                g.report[g.report_len] = '\0';
            }
            break;
        }
        case SERVO_TEST_OUT_END:  ++g.ends; break;
        default: break;
        }
        servo_test_pop(&g.t);
    }
}

/* What the supply reads now: its set point less the source's drop, and the
 * servo's current. */
static inline servo_test_reading_t reading(float amps)
{
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    supply_sim_step(&g.sup, 0.0f, &st);
    servo_test_reading_t r;
    memset(&r, 0, sizeof(r));
    r.output = st.output;
    r.online = g.online;
    r.ok     = true;
    r.set_v  = st.set_v;
    r.set_i  = st.set_i;
    r.i      = st.output ? amps : 0.0f;
    r.v      = st.output ? st.set_v - r.i * SUPPLY_SIM_SOURCE_OHMS : 0.0f;
    r.mode   = st.output ? 1u : 0u;
    if (g.cc_limits && st.output && r.i > st.set_i) {
        /* As supply.c models it: the reading is the limit, the mode CC. */
        r.i    = st.set_i;
        r.mode = 2u;
        ++g.cc_readings;
    }
    r.trip   = g.trip;
    if (g.frozen) {
        /* A page read with no new module reading behind it: the count and
         * the current as they were, the stamp of when they came. */
        r.i = g.frozen_a;
        r.samples = g.samples;
        r.taken_ms = g.now - g.row_lag_ms;
        return r;
    }
    g.samples = (uint16_t)(g.samples + (g.sample_step ? g.sample_step : 1u));
    g.frozen_a = r.i;
    r.samples = g.samples;
    r.taken_ms = g.now - g.row_lag_ms;
    return r;
}

/* The horn's count now, and the still time as the IOmcu keeps it. */
static inline void enc_model(void)
{
    const float deg = (g.servo.position_us - 1500.0f) * g.enc_deg_per_us;
    const int counts = (int)lroundf(deg * 4096.0f / 360.0f);
    const int flutter = (g.enc_sent & 1u) ? g.enc_flutter : -g.enc_flutter;
    const int by_move = ((g.cmds >> 1) & 1u) ? g.enc_by_move : -g.enc_by_move;
    const uint16_t raw = (uint16_t)((counts + (int)ENC_CENTRE_COUNT + flutter
                                     + by_move) & 4095);
    int d = (int)raw - (int)g.enc_anchor;
    d = ((d + 2048) & 4095) - 2048;
    if (d < 0) {
        d = -d;
    }
    if (!g.enc_anchored || d > (int)SERVO_TEST_ENC_TOL_COUNTS) {
        g.enc_anchor    = raw;
        g.enc_anchor_ms = g.now;
        g.enc_anchored  = true;
    }
    if (g.enc_valid && (int32_t)(g.now - g.enc_next) >= 0) {
        g.enc_next += g.enc_every;
        servo_test_enc_t e = { 0 };
        e.valid    = true;
        e.raw      = raw;
        e.still_ms = (uint16_t)(g.now - g.enc_anchor_ms);
        e.taken_ms = g.now;
        e.weak     = g.enc_weak;
        g.enc_raw  = raw;
        servo_test_encoder(&g.t, &e);
        ++g.enc_sent;
    }
}

static inline void frame(void)
{
    g.now += FRAME_MS;
    const bool out = g.sup.output;
    if (out) {
        g.on_ms += FRAME_MS;
    }
    const bool moves = out && g.armed && g.sup.set_v >= g.brownout_v;
    /* A servo without enough voltage stays where it is. */
    const uint16_t cmd = moves ? g.cmd_us : (uint16_t)(lroundf(g.servo.position_us));
    const float amps = out ? servo_sim_step(&g.servo, cmd, g.now) : 0.0f;
    if (g.enc_model_on) {
        enc_model();
    }
    if (g.readings && (int32_t)(g.now - g.next_read) >= 0) {
        g.next_read += g.read_every_ms;
        const servo_test_reading_t r = reading(amps);
        servo_test_reading(&g.t, &r, 0u);
    }
    const servo_test_in_t in = { g.armed, g.v_max };
    servo_test_do_t d;
    servo_test_step(&g.t, g.now, &in, &d);
    if (d.set) {
        CHECK(d.set_v <= g.v_max + 0.001f);
        if (d.set_v > g.set_v_max) {
            g.set_v_max = d.set_v;
        }
        if (g.take_set) {
            supply_sim_set(&g.sup, d.set_v, d.set_i);
        }
    }
    if (d.on && g.take_on) {
        supply_sim_output(&g.sup, true);
    }
    if (d.off) {
        g.off_asked = true;
        supply_sim_output(&g.sup, false);
    }
    if (d.command) {
        CHECK(d.cmd_us >= LO_US && d.cmd_us <= HI_US);
        g.cmd_us = d.cmd_us;
        g.enc_at_cmd[g.cmds % 16u] = g.enc_raw;
        ++g.cmds;
    }
    if (d.release) {
        g.released = true;
        g.cmd_us = CENTRE;
    }
    if (g.drain) {
        drain();
    }
}

static inline void run_ms(uint32_t ms)
{
    for (uint32_t k = 0; k < ms; k += FRAME_MS) {
        frame();
    }
}

/* Until the run is over and everything handed over, at most @p ms. */
static inline void run_out(uint32_t ms)
{
    for (uint32_t k = 0; k < ms && !servo_test_drained(&g.t); k += FRAME_MS) {
        frame();
    }
}

static inline servo_test_start_t start(const servo_test_cfg_t *c)
{
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    supply_sim_step(&g.sup, 0.0f, &st);
    servo_test_reading_t last;
    memset(&last, 0, sizeof(last));
    last.online = g.online;
    last.output = st.output;
    return servo_test_start(&g.t, c, g.now, &last, g.v_min, g.v_max);
}

/* The modelled servo's own time end to end: 800us at 1.2us/ms. */
#define TRAVEL_MS 667u
