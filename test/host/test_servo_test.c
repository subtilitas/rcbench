/*
 * The automatic servo test against the servo model and the supply model.
 *
 * Under test: what it measures at each step and how near the travel time
 * comes to the modelled servo's; the brown-out walk; PASS and FAIL against
 * the limits; every abort, each leaving the output asked off, the servo let
 * go and a report that says ABORTED and why; the caps it never exceeds; the
 * outbox; and the CSV read back by the log viewer's parser.
 *
 * The output encoder: angles from the centre count across the wrap; a run
 * with it reporting each step's end angles, their error against the
 * commanded angle and a travel time from the start of the angle's
 * stillness, beside the current's; the same run without it unchanged, its
 * CSV header and report included; a servo that turns less than commanded
 * showing the error; one that never moves counted unmoved; an encoder that
 * gives nothing said so; a stillness that began before the command not
 * taken for its end.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "log_csv.h"
#include "servo_sim.h"
#include "servo_test.h"
#include "supply.h"
#include "ui_text.h"

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
    uint16_t sample_step;    /* how far its count moves a reading */
    float    frozen_a;
    float    set_v_max;      /* the highest voltage ever asked */
    bool     released;
    bool     off_asked;
    /* The output encoder: the horn's angle at enc_deg_per_us from 1500 us,
     * read from a count with its centre at ENC_CENTRE_COUNT, every
     * enc_every ms, with the coprocessor's still time (a 12-count anchor)
     * kept at the frame rate. */
    bool     enc_model_on;
    float    enc_deg_per_us;
    uint32_t enc_every, enc_next;
    bool     enc_valid;
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
#define ENC_DEG_PER_US   0.09f      /* 90 degrees across 1000 us */

static void cfg_defaults(servo_test_cfg_t *c)
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

static void rig_fresh(void)
{
    free(g.csv_text);
    memset(&g, 0, sizeof(g));
    servo_test_init(&g.t);
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    /* Free at both ends the test drives to: 1100 and 1900 us. */
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

static void keep_csv(const char *line)
{
    if (g.check_rows && line[0] >= '0' && line[0] <= '9') {
        /* A row with a settle on it was taken at or after the reading that
         * found the settle (the row's time is from the run's start). */
        const char *last = strrchr(line, ';');
        if (last != NULL && last[1] != '\0') {
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
        g.csv_text = realloc(g.csv_text, g.csv_cap);
    }
    memcpy(g.csv_text + g.csv_len, line, n);
    g.csv_len += n;
    g.csv_text[g.csv_len++] = '\n';
    g.csv_text[g.csv_len] = '\0';
}

static void drain(void)
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
static servo_test_reading_t reading(float amps)
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

/* The horn's count now, and the still time as the coprocessor keeps it. */
static void enc_model(void)
{
    const float deg = (g.servo.position_us - 1500.0f) * g.enc_deg_per_us;
    const int counts = (int)lroundf(deg * 4096.0f / 360.0f);
    const uint16_t raw = (uint16_t)((counts + (int)ENC_CENTRE_COUNT) & 4095);
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
        servo_test_enc_t e;
        e.valid    = true;
        e.raw      = raw;
        e.still_ms = (uint16_t)(g.now - g.enc_anchor_ms);
        e.taken_ms = g.now;
        servo_test_encoder(&g.t, &e);
        ++g.enc_sent;
    }
}

static void frame(void)
{
    g.now += FRAME_MS;
    const bool out = g.sup.output;
    const bool moves = out && g.armed && g.sup.set_v >= g.brownout_v;
    /* A servo without enough voltage stays where it is. */
    const uint16_t cmd = moves ? g.cmd_us : (uint16_t)(g.servo.position_us + 0.5f);
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
    }
    if (d.release) {
        g.released = true;
        g.cmd_us = CENTRE;
    }
    if (g.drain) {
        drain();
    }
}

static void run_ms(uint32_t ms)
{
    for (uint32_t k = 0; k < ms; k += FRAME_MS) {
        frame();
    }
}

/* Until the run is over and everything handed over, at most @p ms. */
static void run_out(uint32_t ms)
{
    for (uint32_t k = 0; k < ms && !servo_test_drained(&g.t); k += FRAME_MS) {
        frame();
    }
}

static servo_test_start_t start(const servo_test_cfg_t *c)
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

/* The modelled servo's own time end to end: 800 us at 1.2 us/ms. */
#define TRAVEL_MS 667u

/* ------------------------------------------------------------------ tests */

/* Two steps run through: each measures idle, moving and holding current and
 * the travel time, the run passes, the output goes off and the servo is let
 * go, and the report and the CSV are handed over and ended. */
TEST_CASE(a_run_measures_each_step_and_passes)
{
    rig_fresh();
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.idle_max_a = 0.3f;
    c.hold_max_a = 0.3f;
    c.travel_max_ms = 1000u;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    CHECK(servo_test_running(&g.t));
    CHECK_EQ(servo_test_steps_planned(&g.t), 2u);
    run_out(120000u);
    CHECK(!servo_test_running(&g.t));
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
    CHECK(g.off_asked);
    CHECK(g.released);
    CHECK(!g.sup.output);
    CHECK_EQ(g.opens, 1u);
    CHECK_EQ(g.ends, 1u);
    CHECK(g.txt > 30u);
    CHECK(g.csv > 100u);
    CHECK_EQ(g.t.rows_lost, 0u);
    CHECK_EQ(g.t.step_count, 2u);
    for (unsigned k = 0; k < 2u; ++k) {
        const servo_test_step_t *s = &g.t.steps[k];
        CHECK(s->done);
        CHECK_EQ(s->moves, 4u);
        CHECK_EQ(s->travels, 4u);
        CHECK_EQ(s->timeouts, 0u);
        CHECK_NEAR(s->idle.sum / (float)s->idle.n, 0.12f, 0.03f);
        CHECK_NEAR(s->hold[0].sum / (float)s->hold[0].n, 0.12f, 0.03f);
        CHECK_NEAR(s->hold[1].sum / (float)s->hold[1].n, 0.12f, 0.03f);
        CHECK_NEAR(s->move.sum / (float)s->move.n, 0.95f, 0.03f);
        /* Late by up to one reading and one frame, never early. */
        CHECK(s->travel_max_ms >= TRAVEL_MS);
        CHECK(s->travel_max_ms <= TRAVEL_MS + 100u + FRAME_MS);
        CHECK_NEAR(s->v.sum / (float)s->v.n, s->set_v, 0.06f);
    }
    CHECK(strstr(g.report, "Result:         PASS") != NULL);
    CHECK(strstr(g.report, "Device:         DS3218") != NULL);
    CHECK(strstr(g.report, "STANDARD PWM") != NULL);
    CHECK(strstr(g.report, " 4.80 V 6.00 V") != NULL);
    CHECK(strstr(g.report, "NOT MEASURED") != NULL);
    float per_s = 0.0f, mod_s = 0.0f;
    uint32_t every = 0u;
    CHECK(servo_test_rates(&g.t, &per_s, &mod_s, &every));
    CHECK_NEAR(per_s, 10.0f, 0.2f);
    CHECK_EQ(every, 100u);       /* 99.8 ms, rounded */
    CHECK(servo_test_drained(&g.t));
    CHECK(g.set_v_max <= 6.0f + 0.001f);
}

/* At 20 readings a second the travel time comes nearer: late by up to one
 * reading's 50 ms and a frame. */
TEST_CASE(the_travel_time_is_late_by_at_most_a_reading)
{
    rig_fresh();
    g.read_every_ms = 50u;
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    const servo_test_step_t *s = &g.t.steps[0];
    CHECK_EQ(s->travels, 4u);
    CHECK(s->travel_max_ms >= TRAVEL_MS);
    CHECK(s->travel_max_ms <= TRAVEL_MS + 50u + FRAME_MS);
    CHECK(s->travel_sum_ms / s->travels >= TRAVEL_MS);
}

/* LENGTH BY TIME: counted moves until TEST TIME has passed. */
TEST_CASE(length_by_time_moves_for_the_test_time)
{
    rig_fresh();
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    c.by_moves = false;
    c.time_s = 6u;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    /* A move and its hold take about 0.77 + 0.6 s. */
    CHECK(g.t.steps[0].moves >= 4u);
    CHECK(g.t.steps[0].moves <= 6u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
}

/*
 * A servo whose ends hold at different currents -- 0.40 A at the low end,
 * which is loaded, 0.05 A at the high end and the centre -- and whose
 * current ramps up from where it was at 0.95 A a second while it moves,
 * for 1200 ms end to end.  Neither the first reading of a move, still at
 * the start end's level, nor the ramp passing the destination's level on
 * its way up is the arrival: every travel time is the 1200 ms the servo
 * takes, late by no more than a reading.
 */
static float ramp_level(uint16_t us) { return (us < 1300u) ? 0.40f : 0.05f; }

TEST_CASE(unequal_end_levels_do_not_arrive_early)
{
    servo_test_t t;
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    c.settle_ms  = 0u;
    c.report     = false;
    servo_test_init(&t);
    uint32_t now = 1000u, cmd_at = 0u, next = now;
    uint16_t cmd = CENTRE, from = CENTRE, samples = 1u;
    servo_test_reading_t r;
    memset(&r, 0, sizeof(r));
    r.v = 4.8f;
    r.set_v = 4.8f;
    r.output = true;
    r.online = true;
    r.ok = true;
    r.mode = 1u;
    r.taken_ms = now;
    CHECK_EQ(servo_test_start(&t, &c, now, &r, 1.0f, 20.0f),
             SERVO_TEST_START_OK);
    for (int k = 0; k < 6000 && servo_test_running(&t); ++k) {
        now += 10u;
        if (now >= next) {
            next += 100u;
            const uint32_t dt = now - cmd_at;
            float i = ramp_level(cmd);
            if (cmd != from && dt < 1200u) {
                i = ramp_level(from) + 0.00095f * (float)dt;
                if (i > 1.0f) {
                    i = 1.0f;
                }
            }
            r.samples = ++samples;
            r.taken_ms = now;
            r.i = i;
            servo_test_reading(&t, &r, 0u);
        }
        const servo_test_in_t in = { true, 20.0f };
        servo_test_do_t d;
        servo_test_step(&t, now, &in, &d);
        if (d.command) {
            from = cmd;
            cmd = d.cmd_us;
            cmd_at = now;
        }
        while (servo_test_peek(&t, NULL) != SERVO_TEST_OUT_NONE) {
            servo_test_pop(&t);
        }
    }
    const servo_test_step_t *s = &t.steps[0];
    CHECK_EQ(t.why, SERVO_TEST_AB_NONE);
    CHECK_EQ(s->moves, 4u);
    CHECK_EQ(s->travels, 4u);
    CHECK_EQ(s->timeouts, 0u);
    CHECK(s->travel_sum_ms / s->travels >= 1200u);   /* none early */
    CHECK(s->travel_max_ms <= 1200u + 100u + 10u);
    /* And each end's level is that end's, not a reading of the ramp. */
    CHECK_NEAR(s->hold[0].sum / (float)s->hold[0].n, 0.40f, 0.001f);
    CHECK_NEAR(s->hold[1].sum / (float)s->hold[1].n, 0.05f, 0.001f);
    CHECK_EQ(servo_test_verdict(&t), SERVO_TEST_PASS);
}

/* HV SERVO in the report says what ran: on with neither of its steps
 * chosen runs nothing above 6.0 V. */
TEST_CASE(the_report_says_whether_a_step_above_6_v_ran)
{
    servo_test_cfg_t c;
    rig_fresh();
    cfg_defaults(&c);
    c.step_count = 1u;
    c.hv = true;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK(strstr(g.report, "HV servo:       ON, no step above 6.0 V chosen")
          != NULL);

    rig_fresh();
    cfg_defaults(&c);
    c.steps_v[0] = 7.4f;
    c.step_count = 1u;
    c.hv = true;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK(strstr(g.report, "HV servo:       ON, steps above 6.0 V run")
          != NULL);
}

/* The brown-out walk stops at the first voltage that shows no movement. */
TEST_CASE(the_brownout_walk_stops_where_the_servo_stops)
{
    rig_fresh();
    g.brownout_v = 4.1f;
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 0u;
    c.brownout = true;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    CHECK_EQ(servo_test_steps_planned(&g.t), 1u);
    run_out(120000u);
    float moved = 0.0f;
    bool stopped = false;
    CHECK(servo_test_brownout(&g.t, &moved, &stopped));
    CHECK_NEAR(moved, 4.2f, 0.001f);
    CHECK(stopped);
    CHECK(strstr(g.report, "Moved at 4.20 V; no movement seen at 4.00 V.")
          != NULL);
    /* 5.0 down to 4.0 in 0.2 V steps. */
    CHECK_EQ(g.t.step_count, 6u);
    CHECK(g.set_v_max <= 5.0f + 0.001f);
    CHECK(g.off_asked);
}

/* A servo that moves at every voltage: the walk ends at the floor, the
 * supply's lowest set point where that is above 3.0 V. */
TEST_CASE(the_brownout_walk_ends_at_the_floor)
{
    rig_fresh();
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    c.brownout = true;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    CHECK_EQ(servo_test_steps_planned(&g.t), 2u);
    run_out(200000u);
    float moved = 0.0f;
    bool stopped = true;
    CHECK(servo_test_brownout(&g.t, &moved, &stopped));
    CHECK(!stopped);
    /* 5.0 down in 0.2 V steps to 3.4, then the floor itself: 3.3 V, the
     * model's lowest set point, as the report says. */
    CHECK_NEAR(moved, 3.3f, 0.001f);
    CHECK_NEAR(g.t.steps[g.t.step_count - 1u].set_v, 3.3f, 0.001f);
    CHECK(strstr(g.report, "in 0.20 V steps to 3.30 V") != NULL);
    CHECK(strstr(g.report, "down to 3.30 V") != NULL);
    CHECK(strstr(g.report, "lower not tested") != NULL);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
    CHECK(strstr(g.report, "Brown-out start  movement seen at 5.00 V: PASS\n")
          != NULL);

    /* Not moving at the first: said so. */
    rig_fresh();
    g.brownout_v = 9.0f;
    cfg_defaults(&c);
    c.step_count = 0u;
    c.brownout = true;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK(!servo_test_brownout(&g.t, &moved, &stopped));
    CHECK(stopped);
    CHECK(strstr(g.report, "No movement seen at 5.00 V, the first step: "
                           "not measurable.") != NULL);
    /* The walk is the whole run, and it measured nothing: the result and
     * the limits say where, not "0 of 0 moves". */
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_NOT_MEASURABLE);
    CHECK(strstr(g.report, "Result:         NOT MEASURABLE - no movement "
                           "seen at 5.00 V, the brown-out walk's first "
                           "voltage\n") != NULL);
    CHECK(strstr(g.report, "0 of 0") == NULL);
    CHECK(strstr(g.report, "Brown-out start  no movement seen at 5.00 V: "
                           "NOT MEASURABLE\n") != NULL);

    /* A step that moves and passes, and a walk that sees nothing at its
     * first voltage: the walk measured nothing, so the run is not PASS. */
    rig_fresh();
    g.brownout_v = 9.0f;
    cfg_defaults(&c);
    c.steps_v[0] = 9.6f;
    c.step_count = 1u;
    c.brownout = true;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    CHECK_EQ(g.t.steps[0].no_rise, 0u);
    CHECK_EQ(g.t.steps[0].timeouts, 0u);
    CHECK(!g.t.steps[1].moved);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_NOT_MEASURABLE);
    CHECK(strstr(g.report, "Result:         NOT MEASURABLE - no movement "
                           "seen at 5.00 V, the brown-out walk's first "
                           "voltage\n") != NULL);
    CHECK(strstr(g.report, "Moves seen       0 unseen: PASS\n") != NULL);
    CHECK(strstr(g.report, "Brown-out start  no movement seen at 5.00 V: "
                           "NOT MEASURABLE\n") != NULL);
}

/* A cap under 5.0 V starts the walk at the cap. */
TEST_CASE(the_brownout_walk_starts_under_the_cap)
{
    rig_fresh();
    g.v_max = 4.5f;
    g.brownout_v = 4.2f;
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 0u;
    c.brownout = true;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK(g.set_v_max <= 4.5f + 0.001f);
    CHECK_NEAR(g.t.steps[0].set_v, 4.5f, 0.001f);
}

/* Each limit on the LIMITS page fails the run when exceeded; 0 is not
 * checked. */
TEST_CASE(each_limit_fails_the_run)
{
    servo_test_cfg_t c;

    rig_fresh();
    cfg_defaults(&c);
    c.step_count = 1u;
    c.idle_max_a = 0.05f;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_FAIL);
    CHECK(strstr(g.report, "Result:         FAIL") != NULL);
    CHECK(strstr(g.report, "Idle current     highest") != NULL);

    rig_fresh();
    cfg_defaults(&c);
    c.step_count = 1u;
    c.travel_max_ms = 500u;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_FAIL);
    CHECK(strstr(g.report, "Travel time      longest 700 ms, limit 500 ms: "
                           "FAIL") != NULL);

    /* The same servo read by the PD mini: its readings lag and repeat, so
     * the travel time is an upper bound, reported and not checked. */
    rig_fresh();
    cfg_defaults(&c);
    c.step_count = 1u;
    c.travel_max_ms = 500u;
    servo_test_meter_pdmini(&c.meter);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
    CHECK(strstr(g.report, "Supply:         PD mini\n") != NULL);
    CHECK(strstr(g.report, "Lag:            about 300 ms from a change of "
                           "current to the reading that shows it\n") != NULL);
    CHECK(strstr(g.report, "Repeats:        a reading can repeat") != NULL);
    CHECK(strstr(g.report, "Travel times:   an upper bound, not checked "
                           "against the limit\n") != NULL);
    CHECK(strstr(g.report, "Travel time      longest 700 ms, limit 500 ms: "
                           "upper bound, not checked against the limit\n")
          != NULL);
    servo_test_meter_pdmini(NULL);          /* nothing to fill: no harm */

    rig_fresh();
    cfg_defaults(&c);
    c.step_count = 1u;
    c.hold_max_a = 0.05f;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_FAIL);

    /* STALL AT under the moving current: counted, though nothing lasts a
     * second above it. */
    rig_fresh();
    cfg_defaults(&c);
    c.step_count = 1u;
    c.stall_a = 0.5f;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_FAIL);
    CHECK(g.t.stalled);
}

/* An end past the linkage's stop: the servo pushes there, its holding
 * current is high, and above STALL AT for a second the run ends. */
TEST_CASE(a_servo_pushing_on_a_stop_ends_the_run)
{
    rig_fresh();
    g.servo.cfg.stop_hi_us = 1880u;      /* 20 us short of the high end */
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    c.stall_a = 1.0f;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_ABORTED);
    CHECK_EQ(g.t.why, SERVO_TEST_AB_STALL);
    CHECK(g.off_asked);
    CHECK(g.released);
    CHECK(strstr(g.report, "Result:         ABORTED - above STALL AT") != NULL);

    /* Under STALL AT the run goes on, and the high end's holding current
     * fails it. */
    rig_fresh();
    g.servo.cfg.stop_hi_us = 1880u;
    cfg_defaults(&c);
    c.step_count = 1u;
    c.hold_max_a = 0.5f;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_FAIL);
    const servo_test_step_t *s = &g.t.steps[0];
    CHECK(s->hold[1].sum / (float)s->hold[1].n > 0.9f);
    CHECK_EQ(s->timeouts, 0u);      /* each end's level measured first */
}

/* A servo that does not move: no counted move shows movement, so none is
 * timed and none is late.  The current cannot tell it from a servo moving
 * under the threshold, so the step and the run read NOT MEASURABLE, not
 * FAIL, and the report says how many moves went unseen. */
TEST_CASE(a_servo_that_does_not_move_is_not_measurable)
{
    rig_fresh();
    g.brownout_v = 99.0f;
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    c.moves = 2u;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_NOT_MEASURABLE);
    CHECK_EQ(g.t.steps[0].travels, 0u);
    CHECK_EQ(g.t.steps[0].no_rise, 2u);
    CHECK_EQ(g.t.steps[0].timeouts, 0u);
    CHECK(strstr(g.report, "Result:         NOT MEASURABLE - 2 of 2 counted "
                           "moves showed no movement in the current") != NULL);
    CHECK(strstr(g.report, "    2    0      2 NOT MEASURABLE\n") != NULL);
    CHECK(strstr(g.report, "Moves arrived    0 late: PASS") != NULL);
    CHECK(strstr(g.report, "Moves seen       2 unseen: NOT MEASURABLE")
          != NULL);
    /* No move arrived: there is no longest travel time to state. */
    CHECK(strstr(g.report, "Travel time      longest --, limit OFF: not "
                           "measured, no move arrived\n") != NULL);
    CHECK(strstr(g.report, "longest 0 ms") == NULL);

    /* Nor on the PD mini with a limit set: not an upper bound of 0 ms. */
    rig_fresh();
    g.brownout_v = 99.0f;
    cfg_defaults(&c);
    c.step_count = 1u;
    c.moves = 2u;
    c.travel_max_ms = 500u;
    servo_test_meter_pdmini(&c.meter);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_NOT_MEASURABLE);
    CHECK(strstr(g.report, "Travel time      longest --, limit 500 ms: not "
                           "measured, no move arrived\n") != NULL);
    CHECK(strstr(g.report, "upper bound, not checked against the limit\n")
          != NULL);                         /* the header line, not this */
    CHECK(strstr(g.report, "longest 0 ms") == NULL);

    /* A limit exceeded still fails it. */
    rig_fresh();
    g.brownout_v = 99.0f;
    cfg_defaults(&c);
    c.step_count = 1u;
    c.moves = 2u;
    c.idle_max_a = 0.05f;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_FAIL);
}

/* Every abort: the output asked off, the servo let go, ABORTED and the
 * reason in the report, and the files ended. */
static void check_aborted(servo_test_abort_t why)
{
    run_out(60000u);
    CHECK(!servo_test_running(&g.t));
    CHECK_EQ(g.t.why, why);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_ABORTED);
    CHECK(g.off_asked);
    CHECK(g.released);
    CHECK_EQ(g.ends, 1u);
    char want[80];
    snprintf(want, sizeof(want), "Result:         ABORTED - %s",
             servo_test_abort_name(why));
    CHECK(strstr(g.report, want) != NULL);
}

TEST_CASE(every_abort_switches_off_lets_go_and_reports)
{
    servo_test_cfg_t c;
    cfg_defaults(&c);

    /* The caller's: STOP, link, leaving, the operator. */
    const servo_test_abort_t callers[] = {
        SERVO_TEST_AB_STOP, SERVO_TEST_AB_LINK, SERVO_TEST_AB_LEFT,
        SERVO_TEST_AB_OPERATOR, SERVO_TEST_AB_SETTINGS, SERVO_TEST_AB_TOUCH,
    };
    for (size_t k = 0; k < sizeof(callers) / sizeof(callers[0]); ++k) {
        rig_fresh();
        CHECK_EQ(start(&c), SERVO_TEST_START_OK);
        run_ms(4000u);
        servo_test_abort(&g.t, callers[k], g.now);
        check_aborted(callers[k]);
    }

    /* A disarm. */
    rig_fresh();
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(4000u);
    g.armed = false;
    check_aborted(SERVO_TEST_AB_DISARMED);

    /* The output switched off elsewhere, and a trip. */
    rig_fresh();
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(4000u);
    supply_sim_output(&g.sup, false);
    check_aborted(SERVO_TEST_AB_SUPPLY_OFF);

    rig_fresh();
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(4000u);
    supply_sim_output(&g.sup, false);
    g.trip = 1u;
    check_aborted(SERVO_TEST_AB_TRIPPED);

    /* A supply that stops answering, and readings that stop. */
    rig_fresh();
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(4000u);
    g.online = false;
    check_aborted(SERVO_TEST_AB_SUPPLY_LOST);

    rig_fresh();
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(4000u);
    g.readings = false;
    run_ms(SERVO_TEST_STALE_MS - 200u);
    CHECK(servo_test_running(&g.t));
    check_aborted(SERVO_TEST_AB_STALE);

    /* A set point the supply does not take, and an output that does not
     * come on. */
    rig_fresh();
    g.take_set = false;
    supply_sim_set(&g.sup, 9.0f, 2.0f);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    check_aborted(SERVO_TEST_AB_SET_NOT_TAKEN);

    rig_fresh();
    g.take_on = false;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    check_aborted(SERVO_TEST_AB_NOT_ON);

    /* The cap comes down under the next step while the run goes on. */
    rig_fresh();
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(4000u);
    g.v_max = 5.5f;
    check_aborted(SERVO_TEST_AB_CAP);
    CHECK(g.set_v_max <= 4.8f + 0.001f);
    CHECK(strstr(g.report, "(cut short)") == NULL);   /* 4.8 V ran out */
    CHECK(strstr(g.report, " 6.00  not run") != NULL);
}

/*
 * A servo with its own current profile: after 40 ms of latency it moves for
 * 1200 ms at @p mv amperes, the first 100 ms of the motion at @p spike when
 * that is above zero, and settles at the end's holding level -- @p lo at the
 * low end, 0.05 A at the centre and the high end.  Readings every 100 ms,
 * @p off ms after the run starts.  Six counted moves.
 */
typedef struct {
    float    lo, mv, spike;
    uint16_t cmd, prev;
    uint32_t cmd_at;
} profile_t;

static float profile_level(const profile_t *p, uint16_t us)
{
    return (us < 1300u) ? p->lo : 0.05f;
}

static float profile_current(const profile_t *p, uint32_t now)
{
    const uint32_t dt = now - p->cmd_at;
    if (p->cmd == p->prev || dt < 40u) {
        return profile_level(p, p->prev);
    }
    if (dt >= 40u + 1200u) {
        return profile_level(p, p->cmd);
    }
    if (p->spike > 0.0f && dt - 40u < 100u) {
        return p->spike;
    }
    return p->mv;
}

static void run_profile(servo_test_t *t, float lo, float mv, float spike,
                        uint32_t off)
{
    profile_t p = { lo, mv, spike, CENTRE, CENTRE, 0u };
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    c.settle_ms  = 0u;
    c.moves      = 6u;
    c.report     = false;
    servo_test_init(t);
    uint32_t now = 1000u, next = now + off;
    uint16_t samples = 1u;
    servo_test_reading_t r;
    memset(&r, 0, sizeof(r));
    r.v = 4.8f;
    r.set_v = 4.8f;
    r.output = true;
    r.online = true;
    r.ok = true;
    r.mode = 1u;
    r.taken_ms = now;
    (void)servo_test_start(t, &c, now, &r, 1.0f, 20.0f);
    for (int k = 0; k < 20000 && servo_test_running(t); ++k) {
        now += 10u;
        if (now >= next) {
            next += 100u;
            r.samples = ++samples;
            r.taken_ms = now;
            r.i = profile_current(&p, now);
            servo_test_reading(t, &r, 0u);
        }
        const servo_test_in_t in = { true, 20.0f };
        servo_test_do_t d;
        servo_test_step(t, now, &in, &d);
        if (d.command) {
            p.prev = p.cmd;
            p.cmd = d.cmd_us;
            p.cmd_at = now;
        }
        while (servo_test_peek(t, NULL) != SERVO_TEST_OUT_NONE) {
            servo_test_pop(t);
        }
    }
}

/* Every move timed at the servo's 1240 ms, late by no more than a reading:
 * none early. */
static void check_on_time(const servo_test_t *t)
{
    const servo_test_step_t *s = &t->steps[0];
    CHECK_EQ(t->why, SERVO_TEST_AB_NONE);
    CHECK_EQ(s->travels, 6u);
    CHECK_EQ(s->timeouts, 0u);
    CHECK(s->travel_sum_ms / s->travels >= 1240u);
    CHECK(s->travel_max_ms <= 1240u + 100u);
    CHECK_NEAR(s->hold[0].sum / (float)s->hold[0].n, 0.40f, 0.001f);
    CHECK_NEAR(s->hold[1].sum / (float)s->hold[1].n, 0.05f, 0.001f);
}

/* An end held at 0.40 A, harder than the 0.25 A the servo moves at: never
 * passed on the way, so the settled readings there are the arrival. */
TEST_CASE(an_end_held_harder_than_the_servo_moves_is_reached_settled)
{
    servo_test_t t;
    run_profile(&t, 0.40f, 0.25f, 0.0f, 0u);
    check_on_time(&t);
}

/* The same end, with the first 100 ms of every move at 0.9 A: that reading
 * lies above the end's level, and the moving current after it below. A
 * reading below the level is not the arrival, whatever the phase of the
 * readings against the motion. */
TEST_CASE(an_acceleration_spike_above_the_level_is_not_the_arrival)
{
    for (uint32_t off = 0u; off < 100u; off += 25u) {
        servo_test_t t;
        run_profile(&t, 0.40f, 0.25f, 0.9f, off);
        check_on_time(&t);
        CHECK_EQ(servo_test_verdict(&t), SERVO_TEST_PASS);
    }
}

/* The limit, as documented: a servo that moves at the very current it holds
 * the low end with cannot be told from one already there.  Moves to the high
 * end are timed; moves to the low end end at their first settled readings,
 * a reading or two after the command. */
TEST_CASE(a_moving_current_equal_to_the_holding_current_is_the_limit)
{
    servo_test_t t;
    run_profile(&t, 0.40f, 0.40f, 0.0f, 0u);
    const servo_test_step_t *s = &t.steps[0];
    CHECK_EQ(s->travels, 6u);
    CHECK(s->travel_max_ms >= 1240u);                 /* to the high end */
    CHECK(s->travel_sum_ms / s->travels < 1240u);     /* to the low end */
}

/*
 * A slow servo read through a lagging meter: it holds 0.05 A, moves at
 * 0.30 A for @p travel_ms after 40 ms of latency, and the meter shows each
 * current @p lag_ms late, read every 100 ms.  Four counted moves.  A
 * @p travel_ms of 0 is a servo that moves normally to place both ends, then
 * sticks at 0.30 A from its first counted move on, the third.
 */
static void run_lagged(servo_test_t *t, uint32_t travel_ms, uint32_t lag_ms,
                       const servo_test_meter_t *meter)
{
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    c.settle_ms  = 0u;
    c.moves      = 4u;
    c.report     = false;
    c.meter      = *meter;
    servo_test_init(t);
    uint32_t now = 1000u, next = now, cmd_at = 0u, first_at = 0u;
    uint16_t cmd = CENTRE, prev = CENTRE, samples = 1u;
    unsigned moves = 0u;
    servo_test_reading_t r;
    memset(&r, 0, sizeof(r));
    r.v = 4.8f;
    r.set_v = 4.8f;
    r.output = true;
    r.online = true;
    r.ok = true;
    r.mode = 1u;
    r.taken_ms = now;
    (void)servo_test_start(t, &c, now, &r, 1.0f, 20.0f);
    for (int k = 0; k < 20000 && servo_test_running(t); ++k) {
        now += 10u;
        if (now >= next) {
            next += 100u;
            /* What the servo drew lag_ms ago. */
            const uint32_t seen = now - lag_ms;
            const uint32_t dt = seen - cmd_at;
            float i = 0.05f;
            const uint32_t travel = (travel_ms == 0u) ? 1200u : travel_ms;
            if (first_at != 0u && (int32_t)(seen - first_at) >= 40) {
                i = 0.30f;                              /* stuck */
            } else if (cmd != prev && (int32_t)dt >= 40
                       && dt < 40u + travel) {
                i = 0.30f;
            }
            r.samples = ++samples;
            r.taken_ms = now;
            r.i = i;
            servo_test_reading(t, &r, 0u);
        }
        const servo_test_in_t in = { true, 20.0f };
        servo_test_do_t d;
        servo_test_step(t, now, &in, &d);
        if (d.command) {
            prev = cmd;
            cmd = d.cmd_us;
            cmd_at = now;
            if (cmd != CENTRE && ++moves == 3u && travel_ms == 0u) {
                first_at = now;
            }
        }
        while (servo_test_peek(t, NULL) != SERVO_TEST_OUT_NONE) {
            servo_test_pop(t);
        }
    }
}

/* A servo that arrives at 2840 ms shows it on a meter 300 ms behind at
 * about 3140 ms, after the 3000 ms window: the meter's lag widens the
 * window to 3300 ms, so the move is timed and not late.  Without the lag
 * stated the same readings are late.  A servo that never arrives is late
 * on the lagging meter too, and fails the run. */
TEST_CASE(a_meters_lag_widens_the_window_and_a_stuck_move_is_late)
{
    static servo_test_t t;
    servo_test_meter_t pd;
    servo_test_meter_pdmini(&pd);
    servo_test_init(&t);
    CHECK_EQ(servo_test_travel_window_ms(&t), 3000u);
    CHECK_EQ(servo_test_travel_window_ms(NULL), 3000u);

    run_lagged(&t, 2800u, 300u, &pd);
    CHECK_EQ(servo_test_travel_window_ms(&t), 3300u);
    CHECK_EQ(t.why, SERVO_TEST_AB_NONE);
    CHECK_EQ(t.steps[0].timeouts, 0u);
    CHECK_EQ(t.steps[0].travels, 4u);
    CHECK(t.steps[0].travel_max_ms > SERVO_TEST_TRAVEL_TIMEOUT_MS);
    CHECK(t.steps[0].travel_max_ms <= 3300u);
    CHECK_EQ(servo_test_verdict(&t), SERVO_TEST_PASS);

    servo_test_meter_t none;
    memset(&none, 0, sizeof(none));
    run_lagged(&t, 2800u, 300u, &none);
    CHECK(t.steps[0].timeouts > 0u);
    CHECK_EQ(servo_test_verdict(&t), SERVO_TEST_FAIL);

    /* Stuck at 0.30 A from its first counted move on: that move rises and
     * never comes back, late at 3300 ms, and fails the run.  The end it
     * then holds measures 0.30 A, so moves after it show no change. */
    run_lagged(&t, 0u, 300u, &pd);
    CHECK_EQ(t.why, SERVO_TEST_AB_NONE);
    CHECK(t.steps[0].timeouts > 0u);
    CHECK_EQ(t.steps[0].travels, 0u);
    CHECK_EQ(servo_test_verdict(&t), SERVO_TEST_FAIL);
}

/* A supply whose reading count stops while its page goes on answering --
 * display reads on the coprocessor that are slow or fail -- shows the same
 * current over and over.  None of it is a new reading: no row is logged,
 * nothing is measured, and 1.5 s on the run ends as STALE. */
TEST_CASE(a_frozen_current_is_no_reading)
{
    rig_fresh();
    servo_test_cfg_t c;
    cfg_defaults(&c);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(4000u);
    CHECK(servo_test_running(&g.t));
    const uint32_t readings = g.t.readings;
    const unsigned rows = g.csv;
    g.frozen = true;
    run_ms(SERVO_TEST_STALE_MS - 100u);
    CHECK(servo_test_running(&g.t));
    CHECK_EQ(g.t.readings, readings);
    CHECK_EQ(g.csv, rows);
    run_ms(400u);
    CHECK_EQ(g.t.why, SERVO_TEST_AB_STALE);
    CHECK(g.off_asked);
    run_out(10000u);
    CHECK(strstr(g.report, "ABORTED - no new supply reading") != NULL);
}

/* Readings the supply took between two that reached the test are counted
 * and named in the report; the rate taken by the supply counts them. */
TEST_CASE(skipped_readings_are_reported)
{
    rig_fresh();
    g.sample_step = 3u;                  /* two skipped between each */
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK_EQ(g.t.skipped, 2u * (g.t.readings - 1u));
    float per_s = 0.0f, mod_s = 0.0f;
    CHECK(servo_test_rates(&g.t, &per_s, &mod_s, NULL));
    CHECK_NEAR(mod_s, 3.0f * per_s, 0.01f);
    char want[64];
    snprintf(want, sizeof(want), "Skipped:        %lu readings",
             (unsigned long)g.t.skipped);
    CHECK(strstr(g.report, want) != NULL);
}

/* A count that starts again, as after a coprocessor restart, is one
 * reading, not tens of thousands skipped. */
TEST_CASE(a_count_starting_again_is_not_readings_skipped)
{
    rig_fresh();
    g.samples = 5000u;
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(3000u);
    const uint32_t before = g.t.module_samples;
    g.samples = 2u;
    run_out(60000u);
    CHECK_EQ(g.t.skipped, 0u);
    CHECK(g.t.module_samples - before < 1000u);
}

/* An abort mid-step marks that step cut short. */
TEST_CASE(a_step_cut_short_is_said_so)
{
    rig_fresh();
    servo_test_cfg_t c;
    cfg_defaults(&c);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(6000u);
    servo_test_abort(&g.t, SERVO_TEST_AB_STOP, g.now);
    run_out(10000u);
    CHECK(strstr(g.report, "(cut short)") != NULL);
    /* An abort after the end changes nothing. */
    servo_test_abort(&g.t, SERVO_TEST_AB_LINK, g.now);
    CHECK_EQ(g.t.why, SERVO_TEST_AB_STOP);
}

/* A start is refused, and nothing changes, for no step, a step outside the
 * caps, a supply that does not answer, and ends that do not straddle the
 * centre. */
TEST_CASE(a_start_is_refused_for_what_cannot_run)
{
    servo_test_cfg_t c;
    rig_fresh();
    cfg_defaults(&c);
    c.step_count = 0u;
    CHECK_EQ(start(&c), SERVO_TEST_START_NO_STEPS);
    CHECK(!servo_test_running(&g.t));

    cfg_defaults(&c);
    c.steps_v[2] = 8.4f;
    c.step_count = 3u;
    g.v_max = 8.0f;
    CHECK_EQ(start(&c), SERVO_TEST_START_ABOVE_CAP);
    g.v_max = 21.0f;
    g.v_min = 5.0f;
    CHECK_EQ(start(&c), SERVO_TEST_START_ABOVE_CAP);
    g.v_min = 3.3f;

    g.online = false;
    CHECK_EQ(start(&c), SERVO_TEST_START_NO_SUPPLY);
    g.online = true;

    cfg_defaults(&c);
    c.end_lo_us = CENTRE;
    CHECK_EQ(start(&c), SERVO_TEST_START_BAD_ENDS);
    CHECK(!servo_test_running(&g.t));
    CHECK_EQ(servo_test_peek(&g.t, NULL), SERVO_TEST_OUT_NONE);
    CHECK(servo_test_drained(&g.t));

    CHECK_EQ(servo_test_start(NULL, &c, 0u, NULL, 0.0f, 0.0f),
             SERVO_TEST_START_NO_STEPS);
    CHECK_STR_EQ(servo_test_start_name(SERVO_TEST_START_ABOVE_CAP),
                 "A STEP IS OUTSIDE THE CAPS");
}

/* An output already on is not switched on again, and the run watches it
 * from the start. */
TEST_CASE(an_output_already_on_is_used_as_it_is)
{
    rig_fresh();
    supply_sim_output(&g.sup, true);
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    const servo_test_in_t in = { true, 21.0f };
    servo_test_do_t d;
    servo_test_step(&g.t, g.now, &in, &d);
    CHECK(d.set);
    CHECK(!d.on);
    CHECK(d.command);
    CHECK_EQ(d.cmd_us, CENTRE);
    supply_sim_set(&g.sup, d.set_v, d.set_i);   /* as frame() would */
    run_out(60000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
}

/* A sample that repeats the last reading's count measures nothing and logs
 * no row; its state is still checked. */
TEST_CASE(a_repeated_sample_is_not_a_reading)
{
    rig_fresh();
    servo_test_cfg_t c;
    cfg_defaults(&c);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(2000u);
    const uint32_t readings = g.t.readings;
    servo_test_reading_t r = reading(0.12f);
    r.samples = g.t.samples;
    servo_test_reading(&g.t, &r, 0u);
    CHECK_EQ(g.t.readings, readings);
    r.online = false;
    servo_test_reading(&g.t, &r, 0u);
    CHECK_EQ(g.t.why, SERVO_TEST_AB_SUPPLY_LOST);
}

/* An outbox nobody empties loses rows and counts them; the report says so,
 * and the run's files still end. */
TEST_CASE(a_full_outbox_counts_the_rows_it_loses)
{
    rig_fresh();
    g.drain = false;
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(5000u);
    CHECK(g.t.rows_lost > 0u);
    CHECK(!servo_test_drained(&g.t));
    g.drain = true;
    run_out(60000u);
    CHECK(servo_test_drained(&g.t));
    CHECK_EQ(g.ends, 1u);
    char want[64];
    snprintf(want, sizeof(want), "lost to a full queue");
    CHECK(strstr(g.report, want) != NULL);
    CHECK(strstr(g.report, " 0 lost") == NULL);
}

/* The CSV opens in the log viewer: its parser finds the columns, the time
 * axis and the numbers. */
TEST_CASE(the_csv_reads_back_in_the_viewer)
{
    rig_fresh();
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 1u;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(60000u);
    CHECK(g.csv_text != NULL);
    CHECK(strncmp(g.csv_text, servo_test_csv_header(),
                  strlen(servo_test_csv_header())) == 0);
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, g.csv_text, g.csv_len);
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    log_analysis_t an;
    CHECK_EQ(log_csv_analyse(&src, &opts, &an), LOG_OK);
    CHECK_EQ(an.n_columns, 13);
    CHECK_EQ(an.delimiter, ';');
    CHECK_EQ(an.ragged_rows, 0);
    CHECK_EQ(an.time_index, 0);
    CHECK_EQ(an.row_count, (int)g.csv - 1);
    CHECK(an.columns[9].numeric);        /* current (A) */
    CHECK(an.columns[0].monotonic);
    /* An arrival carries its travel time. */
    CHECK(strstr(g.csv_text, ";CV;700\n") != NULL);
}

/* The words come from one table. */
TEST_CASE(the_words_come_from_one_table)
{
    CHECK_STR_EQ(servo_test_phase_name(SERVO_TEST_PH_MOVE), "MOVE");
    CHECK_STR_EQ(servo_test_phase_name((servo_test_phase_t)99), "");
    CHECK_STR_EQ(servo_test_abort_name(SERVO_TEST_AB_STOP), "STOP");
    CHECK_STR_EQ(servo_test_abort_name(SERVO_TEST_AB_COUNT), "");
    CHECK_STR_EQ(servo_test_verdict_name(SERVO_TEST_FAIL), "FAIL");
    CHECK_STR_EQ(servo_test_verdict_name(SERVO_TEST_ABORTED), "ABORTED");
    CHECK_STR_EQ(servo_test_verdict_name(SERVO_TEST_NOT_MEASURABLE),
                 "NOT MEASURABLE");
    CHECK_STR_EQ(servo_test_start_name((servo_test_start_t)99), "");
    CHECK_STR_EQ(servo_str(SERVO_STR_COUNT), "");
    for (int k = 0; k < (int)SERVO_STR_COUNT; ++k) {
        CHECK(servo_str((servo_str_t)k) != NULL);
    }
    servo_test_t t;
    servo_test_init(&t);
    char b[8];
    CHECK(!servo_report_line(NULL, 0u, b, sizeof(b)));
    CHECK(servo_test_drained(&t));
    CHECK(!servo_test_rates(&t, NULL, NULL, NULL));
}

/* -------------------------------------------- replays of runs on the bench */

/*
 * Two runs on the bench on 0.13.0 with the PD mini, from a tester: an
 * MG90S micro servo (fixtures/servo-mg90s.csv) and a 1102HB digital
 * (fixtures/servo-1102hb.csv).  Each ran 4.80 and 6.00 V for 60 s and the
 * brown-out walk: LENGTH BY TIME, SETTLE 500 ms, DWELL 200 ms, the ends
 * 1100 and 1900 us, current limit 2.00 A.  0.13.0 took movement to be a
 * reading 0.10 A from the level before the command and saw none: every
 * move ran out at 3000 ms, late, and both runs read FAIL.  The files are
 * the runs' CSVs, trimmed: every SET, SETTLE and IDLE row, and of each
 * command's MOVE and HOLD rows those within 2.0 s of its first and its
 * last, the reading just before the next command.
 *
 * A replay plays a recording back against the commands the run gives
 * now.  Each command starts the recorded response to the same command: a
 * step's start its SET, SETTLE and IDLE rows, a move the next recorded
 * move to the same end.  Rows arrive at their recorded time after the
 * recorded command, which fell between two readings and is taken at their
 * middle.  Past the end of a response its last reading repeats every
 * 105 ms, and so does it across a gap the trim left.  Moves end at their
 * arrival where 0.13.0 waited 3000 ms, so a step makes more of them than
 * were recorded: they start over from the first move end to end.
 * Brown-out voltages under the recorded 5.00 V replay its response, so a
 * replay says nothing about a servo below 5.00 V.
 *
 * With RP_PRINT set in the environment each replay prints its report: the
 * sample in docs/Servo.md is the MG90S's, with TRAVEL TIME at 800 ms.
 */
#define RP_ROWS     1000u
#define RP_SEGS     48u
#define RP_STEPS    3u
#define RP_EVERY_MS 105u
#define RP_GAP_MS   400u        /* longer between two rows is a trim */

typedef struct {
    uint32_t at;            /* ms after the command */
    float    v, i;
    bool     set;           /* in SET: the set point not yet read back */
} rp_row_t;

typedef struct {
    uint16_t cmd;
    uint16_t first, n;
} rp_seg_t;

typedef struct {
    bool     brownout;
    rp_seg_t prep;          /* SET, SETTLE and IDLE */
    rp_seg_t moves[RP_SEGS];
    unsigned n_moves;
} rp_step_t;

typedef struct {
    rp_row_t  rows[RP_ROWS];
    unsigned  n_rows;
    rp_step_t steps[RP_STEPS];
    unsigned  n_steps;
} rp_rec_t;

static bool rp_is_prep(const char *phase)
{
    return strcmp(phase, "SET") == 0 || strcmp(phase, "SETTLE") == 0
           || strcmp(phase, "IDLE") == 0;
}

/* A recording into @p r, by the command each row answers. */
static bool rp_load(rp_rec_t *r, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", FIXTURE_DIR, name);
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return false;
    }
    memset(r, 0, sizeof(*r));
    char line[256];
    bool ok = fgets(line, sizeof(line), f) != NULL;     /* the header */
    char test_was[16] = "";
    unsigned step_was = 0u;
    uint32_t t_was = 0u, origin = 0u;
    rp_step_t *st = NULL;
    rp_seg_t *seg = NULL;
    while (ok && fgets(line, sizeof(line), f) != NULL) {
        double ts, set, v, lim, i;
        char test[16], phase[16];
        unsigned step, cmd;
        if (sscanf(line, "%lf;%15[^;];%u;%15[^;];%u;;%lf;%lf;%lf;%lf", &ts,
                   test, &step, phase, &cmd, &set, &v, &lim, &i) != 9) {
            ok = false;
            break;
        }
        const uint32_t t = (uint32_t)lround(ts * 1000.0);
        /* The command fell between the last reading and this one. */
        const uint32_t mid = (st != NULL) ? t_was + (t - t_was) / 2u : t;
        if (st == NULL || strcmp(test, test_was) != 0 || step != step_was) {
            if (r->n_steps >= RP_STEPS) {
                ok = false;
                break;
            }
            st = &r->steps[r->n_steps++];
            st->brownout = strcmp(test, "BROWN-OUT") == 0;
            seg = &st->prep;
            seg->cmd = (uint16_t)cmd;
            seg->first = (uint16_t)r->n_rows;
            origin = mid;
            snprintf(test_was, sizeof(test_was), "%s", test);
            step_was = step;
        } else if (!rp_is_prep(phase)
                   && (seg == &st->prep || cmd != seg->cmd)) {
            if (st->n_moves >= RP_SEGS) {
                ok = false;
                break;
            }
            seg = &st->moves[st->n_moves++];
            seg->cmd = (uint16_t)cmd;
            seg->first = (uint16_t)r->n_rows;
            origin = mid;
        }
        if (r->n_rows >= RP_ROWS) {
            ok = false;
            break;
        }
        r->rows[r->n_rows].at = t - origin;
        r->rows[r->n_rows].v  = (float)v;
        r->rows[r->n_rows].i  = (float)i;
        r->rows[r->n_rows].set = strcmp(phase, "SET") == 0;
        ++r->n_rows;
        ++seg->n;
        t_was = t;
    }
    fclose(f);
    return ok && r->n_steps > 0u;
}

typedef struct {
    const rp_rec_t  *rec;
    float            scale;     /* every current, times this */
    const rp_step_t *step;
    const rp_seg_t  *seg;       /* the response playing */
    uint32_t         seg_at;    /* its command */
    unsigned         next;      /* its next row */
    unsigned         cursor;    /* the next recorded move to look at */
    float            v, i;      /* the last reading */
    uint32_t         last_at;
    uint16_t         samples;
    float            set_v;     /* the set point asked */
    float            set_read;  /* and read back */
} rp_play_t;

/* The recorded step for the run's step: the characterisation steps in
 * order, the last for any after it; the brown-out's for every voltage. */
static const rp_step_t *rp_step_for(const rp_rec_t *r, bool brownout,
                                    unsigned idx)
{
    const rp_step_t *found = NULL;
    unsigned k = 0u;
    for (unsigned s = 0; s < r->n_steps; ++s) {
        if (r->steps[s].brownout != brownout) {
            continue;
        }
        found = &r->steps[s];
        if (k++ == idx) {
            break;
        }
    }
    return found;
}

static void rp_command(rp_play_t *p, const servo_test_t *t,
                       const servo_test_do_t *d, uint32_t now)
{
    if (d->set || p->step == NULL) {
        const servo_test_step_t *s = &t->steps[t->step];
        p->step = rp_step_for(p->rec, s->brownout, t->step);
        p->seg = (p->step != NULL) ? &p->step->prep : NULL;
        p->cursor = 0u;
    } else {
        const rp_seg_t *m = NULL;
        for (unsigned k = 0; m == NULL && k < 2u * p->step->n_moves; ++k) {
            if (p->cursor >= p->step->n_moves) {
                p->cursor = 1u;
            }
            if (p->step->moves[p->cursor].cmd == d->cmd_us) {
                m = &p->step->moves[p->cursor];
            }
            ++p->cursor;
        }
        CHECK(m != NULL);
        p->seg = m;
    }
    p->seg_at = now;
    p->next = 0u;
}

/* The readings due by @p now, to the run. */
static void rp_readings(rp_play_t *p, servo_test_t *t, uint32_t now)
{
    for (;;) {
        uint32_t at = 0u;
        const rp_row_t *row = NULL;
        if (p->seg != NULL && p->next < p->seg->n) {
            row = &p->rec->rows[p->seg->first + p->next];
            at = p->seg_at + row->at;
            /* The rows a trim took out: the last reading repeats. */
            if ((int32_t)(at - p->last_at) > (int32_t)RP_GAP_MS) {
                row = NULL;
            }
        }
        if (row != NULL) {
            if ((int32_t)(at - now) > 0) {
                return;
            }
            p->v = row->v;
            p->i = row->i * p->scale;
            /* The set point read back where the recording read it. */
            if (!row->set) {
                p->set_read = p->set_v;
            }
            ++p->next;
        } else {
            at = p->last_at + RP_EVERY_MS;
            if ((int32_t)(at - now) > 0) {
                return;
            }
        }
        if ((int32_t)(at - p->last_at) <= 0) {
            at = p->last_at + 1u;
        }
        servo_test_reading_t r;
        memset(&r, 0, sizeof(r));
        r.v = p->v;
        r.i = p->i;
        r.set_v = p->set_read;
        r.set_i = 2.0f;
        r.mode = 1u;
        r.output = true;
        r.online = true;
        r.ok = true;
        r.samples = ++p->samples;
        r.taken_ms = at;
        p->last_at = at;
        servo_test_reading(t, &r, 0u);
    }
}

/* The tester's settings, read by the PD mini. */
static void rp_cfg(servo_test_cfg_t *c)
{
    cfg_defaults(c);
    c->i_limit  = 2.0f;
    c->brownout = true;
    c->by_moves = false;
    c->time_s   = 60u;
    servo_test_meter_pdmini(&c->meter);
}

/* A run of @p c against recording @p r, its report into @p report. */
static void rp_run(servo_test_t *t, const rp_rec_t *r, float scale,
                   const servo_test_cfg_t *c, char *report, size_t cap)
{
    rp_play_t p;
    memset(&p, 0, sizeof(p));
    p.rec = r;
    p.scale = scale;
    uint32_t now = 1000u;
    p.last_at = now;
    servo_test_reading_t last;
    memset(&last, 0, sizeof(last));
    last.online = true;
    servo_test_init(t);
    CHECK_EQ(servo_test_start(t, c, now, &last, 3.0f, 20.0f),
             SERVO_TEST_START_OK);
    size_t len = 0u;
    report[0] = '\0';
    for (int k = 0; k < 100000 && !servo_test_drained(t); ++k) {
        now += 10u;
        rp_readings(&p, t, now);
        const servo_test_in_t in = { true, 20.0f };
        servo_test_do_t d;
        servo_test_step(t, now, &in, &d);
        if (d.set) {
            p.set_v = d.set_v;
        }
        if (d.command) {
            rp_command(&p, t, &d, now);
        }
        const char *text = NULL;
        servo_test_out_t o;
        while ((o = servo_test_peek(t, &text)) != SERVO_TEST_OUT_NONE) {
            const size_t n = strlen(text);
            if (o == SERVO_TEST_OUT_TXT && len + n + 2u < cap) {
                memcpy(report + len, text, n);
                len += n;
                report[len++] = '\n';
                report[len] = '\0';
            }
            servo_test_pop(t);
        }
    }
    CHECK(servo_test_drained(t));
}

static rp_rec_t     g_rec;
static servo_test_t g_rp;
static char         g_rp_report[12288];

/* What a characterisation step of a replay must show: every counted move
 * either timed or unseen, none late, at least @p seen_min of them seen. */
static void rp_check_step(const servo_test_step_t *s, unsigned seen_min)
{
    CHECK(s->done);
    CHECK(s->moves >= 20u);
    CHECK_EQ(s->timeouts, 0u);
    CHECK_EQ(s->travels + s->no_rise, s->moves);
    CHECK(s->travels >= seen_min);
}

/* The MG90S draws 0.001 A holding and 0.04 to 0.077 A moving: every move
 * is seen at the 0.020 A threshold, timed, none late, and the run passes.
 * The brown-out walk sees it move at 5.00 V. */
TEST_CASE(a_micro_servo_on_the_pd_mini_is_seen_moving)
{
    CHECK(rp_load(&g_rec, "servo-mg90s.csv"));
    CHECK_EQ(g_rec.n_steps, 3u);
    servo_test_cfg_t c;
    rp_cfg(&c);
    rp_run(&g_rp, &g_rec, 1.0f, &c, g_rp_report, sizeof(g_rp_report));
    if (getenv("RP_PRINT") != NULL) {
        fputs(g_rp_report, stdout);
    }
    CHECK_EQ(g_rp.why, SERVO_TEST_AB_NONE);
    CHECK_EQ(servo_test_verdict(&g_rp), SERVO_TEST_PASS);
    for (unsigned k = 0; k < 2u; ++k) {
        rp_check_step(&g_rp.steps[k], g_rp.steps[k].moves);
    }
    CHECK(g_rp.steps[2].brownout);
    CHECK(g_rp.steps[2].moved);
    CHECK(strstr(g_rp_report, "Result:         PASS\n") != NULL);
}

/* The 1102HB holds 0.015 to 0.029 A and peaks 0.039 to 0.044 A moving:
 * moves to the high end, from the low end's 0.028 A, never pass the level
 * before them by the 0.020 A threshold.  Those are unseen, neither timed
 * nor late; the moves seen all arrive, and the run reads NOT MEASURABLE,
 * not FAIL. */
TEST_CASE(a_servo_moving_under_the_threshold_is_not_failed)
{
    CHECK(rp_load(&g_rec, "servo-1102hb.csv"));
    CHECK_EQ(g_rec.n_steps, 3u);
    servo_test_cfg_t c;
    rp_cfg(&c);
    rp_run(&g_rp, &g_rec, 1.0f, &c, g_rp_report, sizeof(g_rp_report));
    if (getenv("RP_PRINT") != NULL) {
        fputs(g_rp_report, stdout);
    }
    CHECK_EQ(g_rp.why, SERVO_TEST_AB_NONE);
    CHECK_EQ(servo_test_verdict(&g_rp), SERVO_TEST_NOT_MEASURABLE);
    for (unsigned k = 0; k < 2u; ++k) {
        rp_check_step(&g_rp.steps[k], 1u);
        CHECK(g_rp.steps[k].no_rise > 0u);
    }
    CHECK(strstr(g_rp_report, "Result:         NOT MEASURABLE - ") != NULL);
    CHECK(strstr(g_rp_report, "Moves arrived    0 late: PASS") != NULL);
    /* As the bench writes it in German, where the tester read FAIL. */
    c.text = ui_text_table(UI_LANG_DE)->servo;
    rp_run(&g_rp, &g_rec, 1.0f, &c, g_rp_report, sizeof(g_rp_report));
    CHECK(strstr(g_rp_report, "NICHT BESTANDEN") == NULL);
    CHECK(strstr(g_rp_report, "Ergebnis:        NICHT MESSBAR - bei ")
          != NULL);
}

/* The MG90S's recording with every current three times as large, as an
 * MS24 draws: 0.003 A holding, 0.11 to 0.23 A moving.  Every move seen,
 * none late, PASS; TRAVEL TIME at 500 ms is reported against the PD mini's
 * travel times and not checked.  The same readings from a meter whose
 * travel times hold do fail it. */
TEST_CASE(a_standard_servo_on_the_pd_mini_passes)
{
    CHECK(rp_load(&g_rec, "servo-mg90s.csv"));
    servo_test_cfg_t c;
    rp_cfg(&c);
    c.travel_max_ms = 500u;
    rp_run(&g_rp, &g_rec, 3.0f, &c, g_rp_report, sizeof(g_rp_report));
    if (getenv("RP_PRINT") != NULL) {
        fputs(g_rp_report, stdout);
    }
    CHECK_EQ(g_rp.why, SERVO_TEST_AB_NONE);
    CHECK_EQ(servo_test_verdict(&g_rp), SERVO_TEST_PASS);
    for (unsigned k = 0; k < 2u; ++k) {
        rp_check_step(&g_rp.steps[k], g_rp.steps[k].moves);
    }
    CHECK(strstr(g_rp_report, "limit 500 ms: upper bound, not checked "
                              "against the limit") != NULL);

    c.meter.upper_bound = false;
    rp_run(&g_rp, &g_rec, 3.0f, &c, g_rp_report, sizeof(g_rp_report));
    CHECK_EQ(servo_test_verdict(&g_rp), SERVO_TEST_FAIL);
}

/* ---------------------------------------------------- the move's deadline */

static servo_test_t g_dl;
static uint16_t     g_dl_samples;

/* A reading of @p i amps at @p t, at a 5.00 V set point, then a pass. */
static void dl_feed(uint32_t t, float i)
{
    servo_test_reading_t r;
    memset(&r, 0, sizeof(r));
    r.v = 5.0f;
    r.i = i;
    r.set_v = 5.0f;
    r.set_i = 2.0f;
    r.mode = 1u;
    r.output = true;
    r.online = true;
    r.ok = true;
    r.samples = ++g_dl_samples;
    r.taken_ms = t;
    servo_test_reading(&g_dl, &r, 0u);
    const servo_test_in_t in = { true, 20.0f };
    servo_test_do_t d;
    servo_test_step(&g_dl, t, &in, &d);
}

/* The brown-out's first move, centre to the high end, on the PD mini's
 * meter: 0.12 A at rest, 0.9 A moving every 100 ms, and the reading back
 * at 0.12 A @p back_ms after the command. */
static void dl_run(uint32_t back_ms)
{
    servo_test_cfg_t c;
    cfg_defaults(&c);
    c.step_count = 0u;
    c.brownout = true;
    c.settle_ms = 100u;
    servo_test_meter_pdmini(&c.meter);
    servo_test_reading_t last;
    memset(&last, 0, sizeof(last));
    last.online = true;
    last.output = true;
    servo_test_init(&g_dl);
    g_dl_samples = 0u;
    CHECK_EQ(servo_test_start(&g_dl, &c, 1000u, &last, 3.0f, 20.0f),
             SERVO_TEST_START_OK);
    uint32_t t = 1000u;
    while (g_dl.phase != SERVO_TEST_PH_MOVE && t < 10000u) {
        t += 100u;
        dl_feed(t, 0.12f);
    }
    CHECK_EQ(g_dl.phase, SERVO_TEST_PH_MOVE);
    const uint32_t cmd = g_dl.cmd_ms;
    for (t = cmd + 100u; t < cmd + back_ms; t += 100u) {
        dl_feed(t, 0.9f);
    }
    dl_feed(cmd + back_ms, 0.12f);
}

/* The window is 3000 ms and the PD mini's 300 ms lag: a reading back at
 * the level 1 ms before 3300 ms is the arrival, one at 3300 ms is not --
 * the move is late. */
TEST_CASE(a_reading_at_the_deadline_is_late)
{
    CHECK_EQ(servo_test_travel_window_ms(&g_dl), 3000u);
    dl_run(3299u);
    CHECK_EQ(g_dl.steps[0].moves, 1u);
    CHECK_EQ(g_dl.steps[0].travels, 1u);
    CHECK_EQ(g_dl.steps[0].travel_max_ms, 3299u);
    CHECK_EQ(g_dl.steps[0].timeouts, 0u);
    CHECK_EQ(servo_test_travel_window_ms(&g_dl), 3300u);

    dl_run(3300u);
    CHECK_EQ(g_dl.steps[0].moves, 1u);
    CHECK_EQ(g_dl.steps[0].travels, 0u);
    CHECK_EQ(g_dl.steps[0].timeouts, 1u);
    CHECK_EQ(g_dl.phase, SERVO_TEST_PH_HOLD);
}

/* ---------------------------------------------------- the output encoder */

/* A rig whose horn is read by an encoder turning @p gain degrees a us. */
static void enc_rig(float gain, servo_test_cfg_t *c)
{
    rig_fresh();
    cfg_defaults(c);
    c->step_count = 1u;
    c->moves = 3u;
    c->enc_on = true;
    c->enc_centre = ENC_CENTRE_COUNT;
    c->enc_cmd_deg[0] = -400.0f * ENC_DEG_PER_US;
    c->enc_cmd_deg[1] =  400.0f * ENC_DEG_PER_US;
    g.enc_model_on = true;
    g.enc_valid = true;
    g.enc_every = 40u;
    g.enc_deg_per_us = gain;
}

TEST_CASE(an_angle_is_counted_from_the_centre_round_the_circle)
{
    CHECK_NEAR(servo_test_enc_deg(3000u, 3000u), 0.0f, 0.0001f);
    CHECK_NEAR(servo_test_enc_deg(4096u - 1u, 0u), -0.0879f, 0.0001f);
    CHECK_NEAR(servo_test_enc_deg(10u, 4090u), 16.0f * 360.0f / 4096.0f, 0.001f);
    CHECK_NEAR(servo_test_enc_deg(4090u, 10u), -16.0f * 360.0f / 4096.0f, 0.001f);
    CHECK_NEAR(servo_test_enc_deg(1024u, 0u), 90.0f, 0.001f);
    CHECK_NEAR(servo_test_enc_deg(3072u, 0u), -90.0f, 0.001f);
    CHECK_NEAR(servo_test_enc_deg(2048u, 0u), -180.0f, 0.001f);
    /* The tolerance in degrees is 12 counts. */
    CHECK_NEAR(SERVO_TEST_ENC_TOL_DEG, 12.0f * 360.0f / 4096.0f, 0.001f);
    CHECK_EQ(SERVO_TEST_ENC_TOL_COUNTS, 12u);
}

TEST_CASE(a_reversed_profile_reads_the_encoder_in_the_commanded_direction)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    /* REVERSE on: the commanded angles are flipped, and the measured
     * angle follows, so a horn at the right end shows no error. */
    c.reverse = true;
    c.enc_cmd_deg[0] =  400.0f * ENC_DEG_PER_US;
    c.enc_cmd_deg[1] = -400.0f * ENC_DEG_PER_US;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    const servo_test_step_t *s = &g.t.steps[0];
    CHECK_EQ(s->enc_travels, 3u);
    CHECK(s->enc_end[0].n + s->enc_end[1].n == 3u);
    if (s->enc_end[0].n > 0u) {
        CHECK_NEAR(s->enc_end[0].sum / (float)s->enc_end[0].n, 36.0f, 0.5f);
    }
    if (s->enc_end[1].n > 0u) {
        CHECK_NEAR(s->enc_end[1].sum / (float)s->enc_end[1].n, -36.0f, 0.5f);
    }
    float deg = 0.0f;
    CHECK(servo_test_enc_at(&g.t, g.now, &deg));
    CHECK(fabsf(deg) <= 36.5f);
    CHECK(strstr(g.report, "Commanded: +36.0 deg at the low end, -36.0 deg at the high end.") != NULL);
}

TEST_CASE(a_run_with_the_encoder_reports_the_angle_beside_the_current)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
    const servo_test_step_t *s = &g.t.steps[0];
    /* The current's figures are as without the encoder. */
    CHECK_EQ(s->moves, 3u);
    CHECK_EQ(s->travels, 3u);
    CHECK(s->travel_max_ms >= TRAVEL_MS);
    /* And the angle's: each counted move seen, settled, none unmoved. */
    CHECK_EQ(s->enc_moves, 3u);
    CHECK_EQ(s->enc_travels, 3u);
    CHECK_EQ(s->enc_unmoved, 0u);
    CHECK_EQ(s->enc_late, 0u);
    CHECK(s->enc_end[0].n + s->enc_end[1].n == 3u);
    const float lo = s->enc_end[0].n ? s->enc_end[0].sum / (float)s->enc_end[0].n
                                      : -36.0f;
    const float hi = s->enc_end[1].n ? s->enc_end[1].sum / (float)s->enc_end[1].n
                                      : 36.0f;
    CHECK_NEAR(lo, -36.0f, 0.5f);
    CHECK_NEAR(hi, 36.0f, 0.5f);
    /* The settle begins as the horn comes within the tolerance of its
     * end: up to the tolerance's worth of travel before the whole move,
     * within a frame and a reading of it. */
    const float tol_ms = SERVO_TEST_ENC_TOL_DEG / ENC_DEG_PER_US / 1.2f;
    CHECK((float)s->enc_travel_max_ms >= (float)TRAVEL_MS - tol_ms - 2.0f * FRAME_MS);
    CHECK(s->enc_travel_max_ms <= TRAVEL_MS + 100u);
    CHECK(s->enc_travel_sum_ms / s->enc_travels <= s->enc_travel_max_ms);
    CHECK(g.t.enc_reads > 100u);

    /* The report: the encoder's header line, its table and its notes. */
    CHECK(strstr(g.report, "Encoder:        AS5600 on the horn shaft, centre count 3000,") != NULL);
    CHECK(strstr(g.report, "ENCODER (angles in degrees from the centre count, times in ms)") != NULL);
    CHECK(strstr(g.report, "Set V  End lo   Err lo   End hi   Err hi   Travel Longest Moves Unmoved Late") != NULL);
    CHECK(strstr(g.report, "Commanded: -36.0 deg at the low end, +36.0 deg at the high end.") != NULL);
    CHECK(strstr(g.report, "Settled: the angle has stayed within 1.05 deg for 100 ms.") != NULL);
    CHECK(strstr(g.report, "Deadband: not measured; the moves go end to end.") != NULL);
    CHECK(strstr(g.report, "Position: the AS5600 measures the horn;") != NULL);
    CHECK(strstr(g.report, "Position: nothing measures the horn") == NULL);
    /* Side by side: the current's table is there too. */
    CHECK(strstr(g.report, "RESULTS PER STEP (currents in A, times in ms)") != NULL);

    /* The CSV has two more columns, and the settle on the row after it. */
    CHECK(strncmp(g.csv_text, servo_test_csv_header_enc(),
                  strlen(servo_test_csv_header_enc())) == 0);
    CHECK(strstr(servo_test_csv_header_enc(), ";travel (ms);angle (deg);travel angle (ms)") != NULL);
    unsigned rows = 0u, with_angle = 0u, with_settle = 0u;
    for (const char *p = strchr(g.csv_text, '\n'); p != NULL && *p != '\0';) {
        const char *e = strchr(p + 1, '\n');
        if (e == NULL) {
            break;
        }
        unsigned fields = 1u;
        for (const char *q = p + 1; q < e; ++q) {
            fields += (*q == ';') ? 1u : 0u;
        }
        CHECK_EQ(fields, 15u);
        ++rows;
        const char *last = e;
        while (last > p + 1 && last[-1] != ';') {
            --last;
        }
        const char *prev = last - 1;
        while (prev > p + 1 && prev[-1] != ';') {
            --prev;
        }
        with_settle += (last < e) ? 1u : 0u;
        with_angle += (prev < last - 1) ? 1u : 0u;
        p = e;
    }
    CHECK(rows > 40u);
    CHECK(with_angle > rows / 2u);
    /* One settle for each of the 3 counted moves and the 2 that place the
     * horn first. */
    CHECK_EQ(with_settle, 5u);
}

TEST_CASE(without_the_encoder_the_run_and_its_files_are_as_before)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    c.enc_on = false;               /* readings arrive, the run ignores them */
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    CHECK(g.enc_sent > 100u);
    CHECK_EQ(g.t.enc_reads, 0u);
    CHECK_EQ(g.t.steps[0].enc_moves, 0u);
    CHECK(strncmp(g.csv_text, servo_test_csv_header(),
                  strlen(servo_test_csv_header())) == 0);
    CHECK(strstr(g.csv_text, "angle") == NULL);
    CHECK(strstr(g.report, "ENCODER") == NULL);
    CHECK(strstr(g.report, "Encoder:") == NULL);
    CHECK(strstr(g.report, "Deadband") == NULL);
    CHECK(strstr(g.report, "Position: nothing measures the horn") != NULL);
    for (const char *p = strchr(g.csv_text, '\n'); p != NULL && *p != '\0';) {
        const char *e = strchr(p + 1, '\n');
        if (e == NULL) {
            break;
        }
        unsigned fields = 1u;
        for (const char *q = p + 1; q < e; ++q) {
            fields += (*q == ';') ? 1u : 0u;
        }
        CHECK_EQ(fields, 13u);
        p = e;
    }
}

/* A horn that turns 0.07 degrees a us where 0.09 is commanded: 28 degrees
 * at the ends, not 36. */
TEST_CASE(a_servo_that_turns_less_than_commanded_shows_the_error)
{
    servo_test_cfg_t c;
    enc_rig(0.07f, &c);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    const servo_test_step_t *s = &g.t.steps[0];
    CHECK_EQ(s->enc_travels, 3u);
    CHECK(s->enc_end[0].n + s->enc_end[1].n == 3u);
    if (s->enc_end[0].n > 0u) {
        CHECK_NEAR(s->enc_end[0].sum / (float)s->enc_end[0].n, -28.0f, 0.5f);
    }
    if (s->enc_end[1].n > 0u) {
        CHECK_NEAR(s->enc_end[1].sum / (float)s->enc_end[1].n, 28.0f, 0.5f);
    }
    /* Err is End less commanded: +8.00 at the low end, -8.00 at the high. */
    CHECK(strstr(g.report, "-28.0") != NULL);
    CHECK(strstr(g.report, "+28.0") != NULL);
    CHECK(strstr(g.report, "+7.9") != NULL);
    CHECK(strstr(g.report, "-7.9") != NULL);
}

TEST_CASE(a_servo_that_does_not_move_is_counted_unmoved_by_the_angle)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    g.brownout_v = 30.0f;           /* never enough voltage to move */
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    const servo_test_step_t *s = &g.t.steps[0];
    CHECK_EQ(s->moves, 3u);
    CHECK_EQ(s->enc_moves, 3u);
    CHECK_EQ(s->enc_unmoved, 3u);
    CHECK_EQ(s->enc_travels, 0u);
    CHECK_EQ(s->enc_late, 0u);
    CHECK(strstr(g.report, "Unmoved: the angle did not leave 2.0 deg of its start.") != NULL);
}

TEST_CASE(an_encoder_that_gives_nothing_is_said_so)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    g.enc_valid = false;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    CHECK_EQ(g.t.enc_reads, 0u);
    CHECK_EQ(g.t.steps[0].enc_moves, 0u);
    CHECK(strstr(g.report, "No angle reading reached the run.") != NULL);
    /* The current's result stands. */
    CHECK_EQ(servo_test_verdict(&g.t), SERVO_TEST_PASS);
    CHECK(strstr(g.csv_text, ";CV;") != NULL);
}

TEST_CASE(an_invalid_reading_ends_the_angle_until_the_next)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_ms(1000u);
    float deg = 0.0f;
    CHECK(servo_test_enc_at(&g.t, g.now, &deg));
    servo_test_enc_t none = { false, 0u, 0u, g.now };
    servo_test_encoder(&g.t, &none);
    CHECK(!servo_test_enc_at(&g.t, g.now, &deg));
    servo_test_encoder(&g.t, NULL);
    servo_test_encoder(NULL, &none);
    run_ms(100u);
    CHECK(servo_test_enc_at(&g.t, g.now, &deg));    /* the model's next reading */
}

/* Runs until a counted move has begun with a start angle and has not moved
 * yet; false when none does within 60 s. */
static bool run_to_counted_move_start(void)
{
    for (uint32_t k = 0; k < 60000u; k += FRAME_MS) {
        frame();
        if (g.t.enc_open && g.t.enc_counted && g.t.enc_ok
            && !g.t.enc_moved && g.t.phase == SERVO_TEST_PH_MOVE) {
            return true;
        }
    }
    return false;
}

/* A gap in the angle after a move's valid start leaves the move unmeasured:
 * not moved, not late, not counted, however the angle goes on afterwards. */
TEST_CASE(a_gap_in_the_angle_leaves_the_open_move_unmeasured)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    CHECK(run_to_counted_move_start());
    const servo_test_enc_t none = { false, 0u, 0u, g.now };
    servo_test_encoder(&g.t, &none);
    run_out(120000u);
    const servo_test_step_t *s = &g.t.steps[0];
    CHECK_EQ(s->moves, 3u);
    CHECK_EQ(s->enc_moves, 2u);
    CHECK_EQ(s->enc_travels, 2u);
    CHECK_EQ(s->enc_unmoved, 0u);
    CHECK_EQ(s->enc_late, 0u);
}

/* A move that had settled before the gap keeps its result. */
TEST_CASE(a_gap_after_the_move_settled_keeps_its_result)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    CHECK(run_to_counted_move_start());
    for (uint32_t k = 0; k < 10000u && !g.t.enc_settled; k += FRAME_MS) {
        frame();
    }
    CHECK(g.t.enc_settled);
    const servo_test_enc_t none = { false, 0u, 0u, g.now };
    servo_test_encoder(&g.t, &none);
    run_out(120000u);
    CHECK_EQ(g.t.steps[0].enc_moves, 3u);
    CHECK_EQ(g.t.steps[0].enc_travels, 3u);
}

/* An abort cuts the open move's window short: it is not counted unmoved or
 * late, as the current-based results do not count it either. */
TEST_CASE(an_abort_does_not_count_the_move_it_cut_short)
{
    static const servo_test_abort_t why[] = {
        SERVO_TEST_AB_STOP, SERVO_TEST_AB_DISARMED, SERVO_TEST_AB_LINK,
        SERVO_TEST_AB_SUPPLY_LOST,
    };
    for (unsigned k = 0; k < sizeof why / sizeof why[0]; ++k) {
        servo_test_cfg_t c;
        enc_rig(ENC_DEG_PER_US, &c);
        CHECK_EQ(start(&c), SERVO_TEST_START_OK);
        CHECK(run_to_counted_move_start());
        const uint16_t before = g.t.steps[0].enc_moves;
        servo_test_abort(&g.t, why[k], g.now);
        CHECK(!servo_test_running(&g.t));
        CHECK_EQ(g.t.steps[0].enc_moves, before);
        CHECK_EQ(g.t.steps[0].enc_unmoved, 0u);
        CHECK_EQ(g.t.steps[0].enc_late, 0u);
    }
}

/* A move whose angle had settled is a finished measurement: an abort in its
 * hold still counts it. */
TEST_CASE(an_abort_after_the_angle_settled_counts_the_move)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    CHECK(run_to_counted_move_start());
    for (uint32_t k = 0; k < 10000u && !g.t.enc_settled; k += FRAME_MS) {
        frame();
    }
    CHECK(g.t.enc_settled);
    const uint16_t before = g.t.steps[0].enc_moves;
    servo_test_abort(&g.t, SERVO_TEST_AB_STOP, g.now);
    CHECK_EQ(g.t.steps[0].enc_moves, before + 1u);
    CHECK_EQ(g.t.steps[0].enc_travels, 1u);
}

/* The panel drains the encoder's readings before the supply's, so a row can
 * be older than the newest reading.  The row takes the newest reading not
 * later than itself. */
TEST_CASE(a_row_takes_the_newest_angle_not_later_than_itself)
{
    servo_test_t t;
    servo_test_init(&t);
    t.cfg.enc_centre = 0u;
    float deg = 0.0f;
    CHECK(!servo_test_enc_at(&t, 1000u, &deg));            /* none yet */
    for (unsigned k = 0; k < 5u; ++k) {                    /* 1000 ... 1160 */
        const servo_test_enc_t e = { true, (uint16_t)(100u * (k + 1u)), 0u,
                                     1000u + 40u * k };
        servo_test_encoder(&t, &e);
    }
    /* A row at 1100: the reading of 1080, not the newer ones. */
    CHECK(servo_test_enc_at(&t, 1100u, &deg));
    CHECK_NEAR(deg, servo_test_enc_deg(300u, 0u), 0.0001f);
    /* At a reading's own time: that reading. */
    CHECK(servo_test_enc_at(&t, 1120u, &deg));
    CHECK_NEAR(deg, servo_test_enc_deg(400u, 0u), 0.0001f);
    /* Before the oldest: none, and not the newest by an unsigned wrap. */
    CHECK(!servo_test_enc_at(&t, 999u, &deg));
    CHECK(!servo_test_enc_at(&t, 0u, &deg));
    /* Older than SERVO_TEST_ENC_STALE_MS to the newest not later: none. */
    CHECK(servo_test_enc_at(&t, 1160u + SERVO_TEST_ENC_STALE_MS, &deg));
    CHECK(!servo_test_enc_at(&t, 1160u + SERVO_TEST_ENC_STALE_MS + 1u, &deg));
    /* The centre is the one in force when the row is matched. */
    t.cfg.enc_centre = 500u;
    CHECK(servo_test_enc_at(&t, 1160u, &deg));
    CHECK_NEAR(deg, servo_test_enc_deg(500u, 500u), 0.0001f);
    /* The ring keeps the last SERVO_TEST_ENC_HIST readings. */
    for (unsigned k = 0; k < 40u; ++k) {
        const servo_test_enc_t e = { true, (uint16_t)(2000u + k), 0u,
                                     2000u + 40u * k };
        servo_test_encoder(&t, &e);
    }
    CHECK_EQ(t.enc_hist_n, SERVO_TEST_ENC_HIST);
    CHECK(servo_test_enc_at(&t, 2000u + 40u * 39u + 10u, &deg));
    CHECK_NEAR(deg, servo_test_enc_deg(2039u, 500u), 0.0001f);
    /* The oldest one kept is 15 readings back; one older is gone. */
    CHECK(servo_test_enc_at(&t, 2000u + 40u * 24u, &deg));
    CHECK_NEAR(deg, servo_test_enc_deg(2024u, 500u), 0.0001f);
    CHECK(!servo_test_enc_at(&t, 2000u + 40u * 24u - 1u, &deg)
          || fabsf(deg - servo_test_enc_deg(2024u, 500u)) > 0.0001f);
    /* Across the 32-bit wrap of the clock. */
    servo_test_init(&t);
    const servo_test_enc_t w = { true, 700u, 0u, 0xFFFFFFF0u };
    servo_test_encoder(&t, &w);
    CHECK(servo_test_enc_at(&t, 20u, &deg));               /* 36 ms later */
    CHECK_NEAR(deg, servo_test_enc_deg(700u, 0u), 0.0001f);
    CHECK(!servo_test_enc_at(&t, 0xFFFFFFE0u, &deg));      /* before it */
}

TEST_CASE(rows_older_than_the_encoders_readings_still_get_their_angle)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    c.moves = 1u;
    g.enc_every = 20u;
    g.row_lag_ms = 30u;             /* every row older than the reading before it */
    g.check_rows = true;
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    unsigned rows = 0u, with_angle = 0u;
    for (const char *p = strchr(g.csv_text, '\n'); p != NULL && *p != '\0';) {
        const char *e = strchr(p + 1, '\n');
        if (e == NULL) {
            break;
        }
        const char *last = e;
        while (last > p + 1 && last[-1] != ';') {
            --last;
        }
        const char *prev = last - 1;
        while (prev > p + 1 && prev[-1] != ';') {
            --prev;
        }
        ++rows;
        with_angle += (prev < last - 1) ? 1u : 0u;
        p = e;
    }
    CHECK(rows > 40u);
    /* Rows have an angle from the first reading on; the few before it do
     * not. */
    CHECK(with_angle + 8u >= rows);
    /* The settles are logged, each once, none on a row older than the
     * reading that found it. */
    CHECK_EQ(g.settle_rows, 3u);
}

/* Stillness that began before the command is the servo at rest, not the
 * move's end: a horn that starts late shows no settle until it has moved
 * and stopped again. */
TEST_CASE(a_stillness_from_before_the_command_is_not_the_end_of_the_move)
{
    servo_test_cfg_t c;
    memset(&c, 0, sizeof(c));
    c.enc_on = true;
    c.enc_centre = 0u;
    servo_test_t t;
    servo_test_init(&t);
    t.cfg = c;
    t.state = SERVO_TEST_RUNNING;
    t.step_count = 1u;
    t.enc_open = true;
    t.enc_counted = true;
    t.enc_ok = true;
    t.enc_end = 1u;
    t.enc_cmd_ms = 10000u;
    t.enc_start_deg = 0.0f;
    /* Moved 5 degrees, still for 200 ms, the stillness older than the
     * command: not settled. */
    servo_test_enc_t e = { true, (uint16_t)(5.0f * 4096.0f / 360.0f), 200u, 10100u };
    servo_test_encoder(&t, &e);
    CHECK(t.enc_moved);
    CHECK(!t.enc_settled);
    /* Still for 100 ms from 10050: after the command, settled at 50 ms. */
    e.taken_ms = 10150u;
    e.still_ms = 100u;
    servo_test_encoder(&t, &e);
    CHECK(t.enc_settled);
    CHECK_EQ(t.enc_travel_ms, 50u);
    CHECK_EQ(t.enc_travel_now_ms, 50u);
    /* Settled once: a later reading does not move it. */
    e.taken_ms = 10300u;
    e.still_ms = 250u;
    servo_test_encoder(&t, &e);
    CHECK_EQ(t.enc_travel_ms, 50u);
    /* Less than SERVO_TEST_ENC_HOLD_MS still is not settled. */
    servo_test_t u = t;
    u.enc_settled = false;
    e.taken_ms = 10400u;
    e.still_ms = SERVO_TEST_ENC_HOLD_MS - 1u;
    servo_test_encoder(&u, &e);
    CHECK(!u.enc_settled);
    /* A reading from before the command is not the move. */
    servo_test_t w = t;
    w.enc_settled = false;
    w.enc_moved = false;
    e.taken_ms = 9000u;
    e.still_ms = 500u;
    servo_test_encoder(&w, &e);
    CHECK(!w.enc_moved);
}

TEST_CASE(a_start_angle_older_than_half_a_second_leaves_the_move_unjudged)
{
    servo_test_cfg_t c;
    enc_rig(ENC_DEG_PER_US, &c);
    g.enc_every = 2000u;            /* a reading every 2 s: too old to start from */
    CHECK_EQ(start(&c), SERVO_TEST_START_OK);
    run_out(120000u);
    const servo_test_step_t *s = &g.t.steps[0];
    CHECK_EQ(s->moves, 3u);
    CHECK(s->enc_moves < 3u);
}

int main(void)
{
    RUN(an_angle_is_counted_from_the_centre_round_the_circle);
    RUN(a_reversed_profile_reads_the_encoder_in_the_commanded_direction);
    RUN(a_run_with_the_encoder_reports_the_angle_beside_the_current);
    RUN(without_the_encoder_the_run_and_its_files_are_as_before);
    RUN(a_servo_that_turns_less_than_commanded_shows_the_error);
    RUN(a_servo_that_does_not_move_is_counted_unmoved_by_the_angle);
    RUN(an_encoder_that_gives_nothing_is_said_so);
    RUN(an_invalid_reading_ends_the_angle_until_the_next);
    RUN(a_gap_in_the_angle_leaves_the_open_move_unmeasured);
    RUN(a_gap_after_the_move_settled_keeps_its_result);
    RUN(an_abort_does_not_count_the_move_it_cut_short);
    RUN(an_abort_after_the_angle_settled_counts_the_move);
    RUN(a_row_takes_the_newest_angle_not_later_than_itself);
    RUN(rows_older_than_the_encoders_readings_still_get_their_angle);
    RUN(a_stillness_from_before_the_command_is_not_the_end_of_the_move);
    RUN(a_start_angle_older_than_half_a_second_leaves_the_move_unjudged);
    RUN(a_run_measures_each_step_and_passes);
    RUN(the_travel_time_is_late_by_at_most_a_reading);
    RUN(length_by_time_moves_for_the_test_time);
    RUN(unequal_end_levels_do_not_arrive_early);
    RUN(the_report_says_whether_a_step_above_6_v_ran);
    RUN(the_brownout_walk_stops_where_the_servo_stops);
    RUN(the_brownout_walk_ends_at_the_floor);
    RUN(the_brownout_walk_starts_under_the_cap);
    RUN(each_limit_fails_the_run);
    RUN(a_servo_pushing_on_a_stop_ends_the_run);
    RUN(a_servo_that_does_not_move_is_not_measurable);
    RUN(every_abort_switches_off_lets_go_and_reports);
    RUN(an_end_held_harder_than_the_servo_moves_is_reached_settled);
    RUN(an_acceleration_spike_above_the_level_is_not_the_arrival);
    RUN(a_moving_current_equal_to_the_holding_current_is_the_limit);
    RUN(a_meters_lag_widens_the_window_and_a_stuck_move_is_late);
    RUN(a_frozen_current_is_no_reading);
    RUN(skipped_readings_are_reported);
    RUN(a_count_starting_again_is_not_readings_skipped);
    RUN(a_step_cut_short_is_said_so);
    RUN(a_start_is_refused_for_what_cannot_run);
    RUN(an_output_already_on_is_used_as_it_is);
    RUN(a_repeated_sample_is_not_a_reading);
    RUN(a_full_outbox_counts_the_rows_it_loses);
    RUN(the_csv_reads_back_in_the_viewer);
    RUN(the_words_come_from_one_table);
    RUN(a_micro_servo_on_the_pd_mini_is_seen_moving);
    RUN(a_servo_moving_under_the_threshold_is_not_failed);
    RUN(a_standard_servo_on_the_pd_mini_passes);
    RUN(a_reading_at_the_deadline_is_late);
    free(g.csv_text);
    return test_summary("servo_test");
}
