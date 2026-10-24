/*
 * The automatic servo test on the INA3221's CH1 windows, through the
 * chain.
 *
 * The bench: the modelled INA3221 of fake_ina.h carries servo_sim's
 * current, the IOmcu's schedule and pages turn it into 50ms
 * windows, the panel's sense_link takes each once, servo_source names the
 * meter, and the run reads what a render loop hands it every 20ms
 * (sense_chain.h), beside a model of the PD mini read every 104ms.  The
 * rig header supplies the run's outbox and the CSV's columns.
 *
 * Under test: a full run on the INA3221 and on the PD mini beside a
 * working INA3221; windows that arrive together keeping their own times;
 * the meter failing mid-run never switching the meter; a part that resets
 * itself and the link going ending the run; a servo on a stop judged on
 * the range it reads and ended on constant current; and a reversed shunt
 * reading negative.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_test_win_rig.h"

/* ============================================================== the bench */

#define PASS_MS   5u            /* the control task's pass                */
#define FRAME_MS  20u           /* the render loop's frame                */
#define PD_MS     104u          /* the PD mini's reading                  */
#define SOURCE_OHM 0.2          /* the supply and its leads               */
#define Q_LEN     64u

typedef struct {
    bool                   is_win;
    servo_test_win_t       win;
    servo_test_reading_t   pd;
    servo_test_meter_now_t meter;
} item_t;

typedef struct {
    servo_test_t   t;
    out_t          out;
    servo_source_t src;
    servo_sim_t    servo;
    bool     link_up;
    bool     armed;
    /* The PD mini. */
    bool     output;
    float    set_v, set_i;
    bool     limits;        /* it holds its current limit                */
    bool     in_cc;
    uint16_t pd_samples;
    uint32_t pd_next;
    float    pd_gain;       /* its reading, of the rail's current        */
    double   sign;          /* the shunt's direction: 1 or -1            */
    uint16_t cmd;
    double   amps;          /* through the shunt now                     */
    /* From the control task to the render loop. */
    item_t   q[Q_LEN];
    unsigned q_n;
    unsigned q_lost;
    unsigned poll_every;    /* the panel polls every so many passes; 0 and
                               1 every pass                             */
    unsigned passes;
    unsigned wins_taken;
    unsigned offs, releases;
} bench_t;

static bench_t bench;

static uint32_t b_now(void) { return chain_now(); }

/* The rail for one millisecond: the servo, the supply's limit, and what
 * the INA3221 has across its shunt and at its load side. */
static void b_rail_ms(void)
{
    double amps = 0.0;
    if (bench.output) {
        amps = (double)servo_sim_step(&bench.servo, bench.cmd, b_now());
    }
    bench.in_cc = false;
    if (bench.limits && amps > (double)bench.set_i) {
        amps    = (double)bench.set_i;
        bench.in_cc = true;
    }
    bench.amps = amps;
    chain.i3221->amps[0]  = amps * bench.sign;
    chain.i3221->volts[0] = bench.output ? (double)bench.set_v - amps * SOURCE_OHM
                                  : 0.0;
    chain_far(1u);
}

static void b_push(const item_t *it)
{
    if (bench.q_n >= Q_LEN) {
        ++bench.q_lost;
        memmove(&bench.q[0], &bench.q[1], (Q_LEN - 1u) * sizeof(bench.q[0]));
        --bench.q_n;
    }
    bench.q[bench.q_n++] = *it;
}

/* One pass of the control task: the poll, the meter, the windows it
 * brought and the supply's reading when one is due. */
static void b_pass(void)
{
    const uint64_t began = chain.us;
    if (bench.link_up && (bench.poll_every < 2u || bench.passes % bench.poll_every == 0u)) {
        (void)chain_poll();
    }
    sense_link_meter_t m;
    sense_link_meter(&chain.sl, &m);
    (void)servo_source_step(&bench.src, b_now(), bench.link_up, chain.minor,
                            bench.link_up ? &m : NULL);
    item_t it;
    memset(&it, 0, sizeof(it));
    it.meter = meter_of(&bench.src);
    sense_link_win_t w;
    while (bench.link_up && sense_link_take_win(&chain.sl, &w)) {
        ++bench.wins_taken;
        it.is_win       = true;
        it.win.number   = w.number;
        it.win.current  = w.current;
        it.win.voltage  = w.voltage;
        it.win.clipped  = w.clipped;
        it.win.mean_ma  = w.mean_ma;
        it.win.max_ma   = w.max_ma;
        it.win.min_ma   = w.min_ma;
        it.win.mean_mv  = w.mean_mv;
        it.win.min_mv   = w.min_mv;
        it.win.taken_ms = w.taken_ms;
        b_push(&it);
    }
    if ((int32_t)(b_now() - bench.pd_next) >= 0) {
        bench.pd_next += PD_MS;
        it.is_win = false;
        memset(&it.pd, 0, sizeof(it.pd));
        it.pd.online   = true;
        it.pd.output   = bench.output;
        it.pd.ok       = true;
        it.pd.set_v    = bench.set_v;
        it.pd.set_i    = bench.set_i;
        it.pd.v        = bench.output ? bench.set_v : 0.0f;
        it.pd.i        = (float)bench.amps * bench.pd_gain;
        it.pd.mode     = !bench.output ? 0u : bench.in_cc ? 2u : 1u;
        it.pd.samples  = ++bench.pd_samples;
        it.pd.taken_ms = b_now();
        b_push(&it);
    }
    /* The rest of the pass, a millisecond of the rail at a time. */
    while ((chain.us - began) / 1000u < PASS_MS) {
        b_rail_ms();
    }
    ++bench.passes;
}

/* One frame of the render loop: the meter, then what the queue holds in
 * the order it came, each with the meter of its poll, and the run's pass. */
static void b_frame(void)
{
    for (unsigned k = 0u; k < FRAME_MS / PASS_MS; ++k) {
        b_pass();
    }
    const servo_test_meter_now_t snap = meter_of(&bench.src);
    servo_test_meter_now(&bench.t, &snap, b_now());
    for (unsigned k = 0u; k < bench.q_n; ++k) {
        servo_test_meter_now(&bench.t, &bench.q[k].meter, b_now());
        if (bench.q[k].is_win) {
            servo_test_window(&bench.t, &bench.q[k].win, 0u);
        } else {
            servo_test_reading(&bench.t, &bench.q[k].pd, 0u);
        }
    }
    bench.q_n = 0u;
    const servo_test_in_t in = { bench.armed, 20.0f };
    servo_test_do_t d;
    servo_test_step(&bench.t, b_now(), &in, &d);
    if (d.set) {
        bench.set_v = d.set_v;
        bench.set_i = d.set_i;
    }
    if (d.on) {
        bench.output = true;
    }
    if (d.off) {
        ++bench.offs;
        bench.output = false;
    }
    if (d.command) {
        bench.cmd = d.cmd_us;
    }
    if (d.release) {
        ++bench.releases;
        bench.cmd = CENTRE;
    }
    out_drain(&bench.out, &bench.t);
}

static void b_frames_ms(uint32_t ms)
{
    for (uint32_t k = 0u; k < ms; k += FRAME_MS) {
        b_frame();
    }
}

/* A bench at tick @p tick0 with a 4.11 IOmcu, the INA3221 on CH1 on
 * 0.1Ohm, a servo free between its ends and the link up for long enough
 * that the INA3221 is the rail's meter. */
static void b_fresh(uint32_t tick0)
{
    out_reset(&bench.out);
    memset(&bench, 0, sizeof(bench));
    servo_test_init(&bench.t);
    chain_start(LINK_MINOR_SERVO_WIN, tick0 - 2000u, 1u);
    chain.flat = true;
    chain.i3221->amps[0] = 0.0;
    servo_source_init(&bench.src);
    servo_sim_cfg_t sc;
    servo_sim_defaults(&sc);
    sc.stop_lo_us = 1000u;
    sc.stop_hi_us = 2000u;
    servo_sim_init(&bench.servo, &sc);
    bench.servo.position_us = (float)CENTRE;
    bench.armed   = true;
    bench.set_v   = 5.0f;
    bench.set_i   = 2.0f;
    bench.pd_gain = 1.0f;
    bench.sign    = 1.0;
    bench.cmd     = CENTRE;
    bench.link_up = true;
    bench.pd_next = b_now();
    chain_link_up();
    b_frames_ms(2000u);
}

/* Two steps and four counted moves each, as servo_test_rig.h's. */
static void b_cfg(servo_test_cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    c->steps_v[0] = 4.8f;
    c->steps_v[1] = 6.0f;
    c->step_count = 2u;
    c->i_limit    = 3.0f;
    c->centre_us  = CENTRE;
    c->end_lo_us  = LO_US;
    c->end_hi_us  = HI_US;
    c->settle_ms  = 500u;
    c->dwell_ms   = 200u;
    c->by_moves   = true;
    c->moves      = 4u;
    c->stall_a    = 1.5f;
    c->report     = true;
    snprintf(c->dut, sizeof(c->dut), "DS3218");
    snprintf(c->type, sizeof(c->type), "STANDARD PWM");
    snprintf(c->firmware, sizeof(c->firmware), "0.0.0");
}

/* Start a run on the meter servo_source names, as the SERVO screen does. */
static void b_start(servo_test_cfg_t *c)
{
    if (servo_source_id(&bench.src) == SERVO_SOURCE_INA3221) {
        servo_test_meter_ina3221(&c->meter, 1000u);
        snprintf(c->meter.name, sizeof(c->meter.name), "INA3221 0x41 CH2");
        c->meter_changes = servo_source_changes(&bench.src);
    } else {
        servo_test_meter_pdmini(&c->meter);
        c->ina_why = (uint8_t)SERVO_TEST_INA_SETTLING;
    }
    servo_test_reading_t last;
    memset(&last, 0, sizeof(last));
    last.online = true;
    last.output = bench.output;
    CHECK_EQ(servo_test_start(&bench.t, c, b_now(), &last, 3.0f, 20.0f),
             SERVO_TEST_START_OK);
}

static void b_out(void)
{
    for (uint32_t k = 0u; k < 120000u && !servo_test_drained(&bench.t);
         k += FRAME_MS) {
        b_frame();
    }
    CHECK(servo_test_drained(&bench.t));
}

static bool b_until(servo_test_phase_t phase)
{
    for (uint32_t k = 0u; k < 30000u && servo_test_running(&bench.t);
         k += FRAME_MS) {
        if (bench.t.phase == phase) {
            return true;
        }
        b_frame();
    }
    return false;
}

/* The run on the bench ended as @p why, and two more seconds of the bench
 * change nothing of it. */
static void b_check_ended(servo_test_abort_t why)
{
    CHECK(!servo_test_running(&bench.t));
    CHECK_EQ(bench.t.why, why);
    b_out();
    CHECK_EQ(servo_test_verdict(&bench.t), SERVO_TEST_ABORTED);
    CHECK_EQ(bench.offs, 1u);
    CHECK_EQ(bench.releases, 1u);
    CHECK(!bench.output);
    char mark[96];
    snprintf(mark, sizeof(mark), "Result:         ABORTED - %s\n",
             servo_test_abort_name(why));
    CHECK(strstr(bench.out.report, mark) != NULL);
    const unsigned rows = bench.out.rows, txt = bench.out.txt;
    b_frames_ms(2000u);
    CHECK_EQ(bench.t.why, why);
    CHECK_EQ(bench.out.rows, rows);
    CHECK_EQ(bench.out.txt, txt);
    CHECK_EQ(bench.offs, 1u);
    CHECK_EQ(bench.releases, 1u);
    /* The run never read the other meter. */
    CHECK_EQ(rows_with(bench.out.csv, COL_METER, "PDMINI"), 0u);
    CHECK_EQ(rows_with(bench.out.csv, COL_METER, "INA3221"), rows - 1u);
}

/* A full run through the chain: the modelled servo's current at the
 * INA3221, its windows through the schedule, the page and the link, and
 * the run's figures from them.  The PD mini beside it reads a quarter of
 * the current, as a slow meter does of a short peak, and none of that is
 * in the run.  At tick 5000 and with the tick wrapping during the run. */
TEST_CASE(a_full_run_on_the_ina3221_through_the_chain)
{
    static const uint32_t k_tick0[] = { 5000u, 0xFFFFE000u };
    for (size_t i = 0u; i < 2u; ++i) {
        b_fresh(k_tick0[i]);
        CHECK_EQ(servo_source_id(&bench.src), SERVO_SOURCE_INA3221);
        bench.pd_gain = 0.25f;
        servo_test_cfg_t c;
        b_cfg(&c);
        c.idle_max_a = 0.3f;
        c.hold_max_a = 0.3f;
        c.travel_max_ms = 100u;     /* not checked: an upper bound */
        b_start(&c);
        CHECK_EQ(bench.t.cfg.meter.kind, SERVO_TEST_METER_INA3221);
        const uint32_t began = b_now();
        b_out();
        if (i > 0u) {
            CHECK(began >= 0xFFFFE000u && b_now() < began);
        }
        CHECK_EQ(bench.t.why, SERVO_TEST_AB_NONE);
        CHECK_EQ(servo_test_verdict(&bench.t), SERVO_TEST_PASS);
        CHECK_EQ(bench.offs, 1u);
        CHECK_EQ(bench.releases, 1u);
        CHECK(!bench.output);
        CHECK_EQ(bench.q_lost, 0u);
        CHECK_EQ(bench.t.skipped, 0u);
        CHECK_EQ(sense_link_win_lost(&chain.sl), 0u);
        CHECK_EQ(bench.t.clipped, 0u);
        for (unsigned k = 0u; k < 2u; ++k) {
            const servo_test_step_t *s = &bench.t.steps[k];
            CHECK(s->done);
            CHECK_EQ(s->moves, 4u);
            CHECK_EQ(s->travels, 4u);
            CHECK_EQ(s->timeouts, 0u);
            CHECK_EQ(s->no_rise, 0u);
            /* The model's 0.12A free and 0.95A travelling, in windows
             * that lie wholly in their phase: nearer than the PD mini's
             * rig holds them (0.03A). */
            CHECK_NEAR(s->idle.sum / (float)s->idle.n, 0.12f, 0.005f);
            CHECK_NEAR(s->hold[0].sum / (float)s->hold[0].n, 0.12f, 0.005f);
            CHECK_NEAR(s->hold[1].sum / (float)s->hold[1].n, 0.12f, 0.005f);
            /* IDLE's 1000ms hold 19 to 20 windows, and each of the three
             * 600ms holds at an end 11 to 12: the first window of a phase
             * began before it and is left out. */
            CHECK(s->idle.n >= 19u && s->idle.n <= 20u);
            CHECK(s->hold[0].n >= 33u && s->hold[0].n <= 36u);
            CHECK(s->hold[1].n >= 33u && s->hold[1].n <= 36u);
            /* The peak is a 1ms sample of the travelling current. */
            CHECK_NEAR(s->move_peak_a, 0.96f, 0.012f);
            /* The servo's 667ms end to end, late by the rest of the
             * window the arrival falls in, the whole window after it, and
             * the poll that reads that one; never early. */
            CHECK(s->travel_max_ms >= 667u);
            CHECK(s->travel_max_ms <= 667u + 2u * SERVO_TEST_WIN_MS
                                          + SENSE_LINK_WIN_MS + PASS_MS);
            /* The bus voltage at the load side: the set point less the
             * source's drop, lowest while the servo travels. */
            CHECK_NEAR(s->v.sum / (float)s->v.n, s->set_v - 0.10f, 0.03f);
            CHECK_NEAR(s->v_min, s->set_v - 0.95f * (float)SOURCE_OHM,
                       0.03f);
        }
        /* One row a window, every window the link took. */
        CHECK_EQ(bench.out.rows - 1u, bench.t.readings);
        CHECK_EQ(rows_with(bench.out.csv, COL_METER, "INA3221"),
                 bench.out.rows - 1u);
        float per_s = 0.0f, mod_s = 0.0f;
        uint32_t every = 0u;
        CHECK(servo_test_rates(&bench.t, &per_s, &mod_s, &every));
        CHECK_NEAR(per_s, 20.0f, 0.1f);
        CHECK_NEAR(mod_s, 20.0f, 0.1f);
        CHECK_EQ(every, 50u);
        CHECK(strstr(bench.out.report, "Current:        INA3221 0x41 CH2, shunt "
                                   "100.0mOhm, range 1.638A\n") != NULL);
        CHECK(strstr(bench.out.report, "Result:         PASS\n") != NULL);
        CHECK(strstr(bench.out.report, "STALL AT 1.50A cannot be reached")
              == NULL);
    }
}

/* The time in ms of each row of @p csv after the header, at most @p cap. */
static unsigned row_times(const char *csv, uint32_t *ms, unsigned cap)
{
    unsigned n = 0u;
    for (const char *p = strchr(csv, '\n'); p != NULL && p[1] != '\0' && n < cap;
         p = strchr(p + 1, '\n')) {
        unsigned long s = 0u, frac = 0u;
        if (sscanf(p + 1, "%lu.%lu", &s, &frac) == 2) {
            ms[n++] = (uint32_t)(s * 1000u + frac);
        }
    }
    return n;
}

/* A panel that polls every 150ms gets three windows in a read.  Each is
 * the run's reading at its own time, 50ms apart: no two rows share a
 * time, a hold holds the windows that closed in it, and windows over STALL
 * AT end the run 1000ms after the first began, as with a window a poll. */
TEST_CASE(windows_that_arrive_together_keep_their_own_times)
{
    b_fresh(5000u);
    bench.poll_every = 150u / PASS_MS;
    servo_test_cfg_t c;
    b_cfg(&c);
    c.step_count = 1u;
    b_start(&c);
    b_out();
    CHECK_EQ(bench.t.why, SERVO_TEST_AB_NONE);
    CHECK_EQ(bench.t.skipped, 0u);
    CHECK_EQ(sense_link_win_lost(&chain.sl), 0u);
    static uint32_t ms[2000];
    const unsigned n = row_times(bench.out.csv, ms, 2000u);
    CHECK_EQ(n, bench.out.rows - 1u);
    unsigned same = 0u, off = 0u;
    for (unsigned k = 2u; k < n; ++k) {
        const uint32_t step = ms[k] - ms[k - 1u];
        same += (step == 0u) ? 1u : 0u;
        /* 50ms a window, give or take the exchanges of a poll. */
        off += (step < 44u || step > 56u) ? 1u : 0u;
    }
    CHECK_EQ(same, 0u);
    CHECK_EQ(off, 0u);
    const servo_test_step_t *s = &bench.t.steps[0];
    CHECK_EQ(s->travels, 4u);
    CHECK_EQ(s->timeouts, 0u);
    /* IDLE's 1000ms and the three 600ms holds at an end hold the windows
     * that closed in them, not one a read. */
    CHECK(s->idle.n >= 17u && s->idle.n <= 20u);
    CHECK(s->hold[0].n >= 27u && s->hold[0].n <= 36u);
    CHECK(s->hold[1].n >= 27u && s->hold[1].n <= 36u);
    CHECK_NEAR(s->idle.sum / (float)s->idle.n, 0.12f, 0.005f);
    /* Late by the windows as before, and by up to the 150ms to the read. */
    CHECK(s->travel_max_ms >= 667u);
    CHECK(s->travel_max_ms <= 667u + 2u * SERVO_TEST_WIN_MS + 150u + PASS_MS);

    /* A servo on a stop: 20 windows over STALL AT in a row. */
    b_fresh(5000u);
    bench.poll_every = 150u / PASS_MS;
    bench.servo.cfg.stop_hi_us = 1850u;
    b_cfg(&c);
    c.step_count = 1u;
    b_start(&c);
    b_out();
    CHECK_EQ(bench.t.why, SERVO_TEST_AB_STALL);
    /* The window that ended it is the twentieth from the first over it:
     * its time 1000ms after that one began, give or take a poll's
     * exchanges, whichever read brought it. */
    const uint32_t over_for = bench.t.end_ms - bench.t.stall_since_ms;
    CHECK(over_for >= 1000u && over_for <= 1006u);
    b_check_ended(SERVO_TEST_AB_STALL);
}

/* The same bench before the INA3221 has been the meter for 1000ms: the
 * run starts on the PD mini, reads it every 104ms to its end, and the
 * INA3221 becoming the meter under it changes nothing. */
TEST_CASE(a_full_run_on_the_pd_mini_beside_a_working_ina3221)
{
    b_fresh(5000u);
    /* A part that reset: the meter is the PD mini for the next second. */
    fake_reset3221(chain.i3221);
    for (unsigned k = 0u; k < 50u
                          && servo_source_id(&bench.src) == SERVO_SOURCE_INA3221;
         ++k) {
        b_frame();
    }
    CHECK_EQ(servo_source_id(&bench.src), SERVO_SOURCE_PDMINI);
    servo_test_cfg_t c;
    b_cfg(&c);
    b_start(&c);
    CHECK_EQ(bench.t.cfg.meter.kind, SERVO_TEST_METER_PDMINI);
    b_frames_ms(4000u);
    CHECK_EQ(servo_source_id(&bench.src), SERVO_SOURCE_INA3221);
    CHECK(servo_test_running(&bench.t));
    b_out();
    CHECK_EQ(bench.t.why, SERVO_TEST_AB_NONE);
    CHECK_EQ(servo_test_verdict(&bench.t), SERVO_TEST_PASS);
    CHECK_EQ(bench.out.rows - 1u, bench.t.readings);
    CHECK_EQ(rows_with(bench.out.csv, COL_METER, "PDMINI"), bench.out.rows - 1u);
    CHECK_EQ(rows_with(bench.out.csv, COL_WINDOW, ""), bench.out.rows - 1u);
    float per_s = 0.0f;
    uint32_t every = 0u;
    CHECK(servo_test_rates(&bench.t, &per_s, NULL, &every));
    CHECK(every >= 103u && every <= 105u);
    for (unsigned k = 0u; k < 2u; ++k) {
        const servo_test_step_t *s = &bench.t.steps[k];
        CHECK_EQ(s->travels, 4u);
        CHECK_NEAR(s->idle.sum / (float)s->idle.n, 0.12f, 0.03f);
        /* Late by up to a reading and a frame. */
        CHECK(s->travel_max_ms >= 667u);
        CHECK(s->travel_max_ms <= 667u + PD_MS + FRAME_MS + 10u);
    }
    CHECK(strstr(bench.out.report, "Current:        PD mini\n") != NULL);
    CHECK(strstr(bench.out.report, "INA3221:        not used: it has worked "
                               "for less than 1s\n") != NULL);
}

/* The part stops answering in the middle of a run on it: the run ends
 * ABORTED and never reads the PD mini, whose readings arrive throughout. */
TEST_CASE(the_meter_failing_mid_run_never_switches_meter)
{
    b_fresh(5000u);
    servo_test_cfg_t c;
    b_cfg(&c);
    b_start(&c);
    CHECK(b_until(SERVO_TEST_PH_HOLD));
    const uint32_t failed = b_now();
    chain.i3221->present = false;
    for (unsigned k = 0u; k < 100u && servo_test_running(&bench.t); ++k) {
        b_frame();
    }
    CHECK(!servo_test_running(&bench.t));
    /* Within a window, a poll and a frame of the part going; the run's own
     * 200ms without a window can come first. */
    CHECK(bench.t.end_ms - failed <= 250u);
    CHECK(bench.t.why == SERVO_TEST_AB_INA_SILENT
          || bench.t.why == SERVO_TEST_AB_INA_NO_WINDOW
          || bench.t.why == SERVO_TEST_AB_WIN_STALE);
    CHECK_EQ(servo_source_id(&bench.src), SERVO_SOURCE_PDMINI);
    CHECK(bench.pd_samples > 20u);
    b_check_ended(bench.t.why);
    /* The part back and the meter again: the run stays ended. */
    chain.i3221->present = true;
    b_frames_ms(3000u);
    CHECK_EQ(servo_test_verdict(&bench.t), SERVO_TEST_ABORTED);
    CHECK(!bench.output);
}

/* The part resets itself during a run: the IOmcu counts it in
 * RESETS, the meter drops for that poll, and the run ends with that
 * reason although the render loop looks 20ms later. */
TEST_CASE(a_part_that_resets_itself_ends_the_run)
{
    b_fresh(5000u);
    servo_test_cfg_t c;
    b_cfg(&c);
    b_start(&c);
    CHECK(b_until(SERVO_TEST_PH_HOLD));
    fake_reset3221(chain.i3221);
    for (unsigned k = 0u; k < 100u && servo_test_running(&bench.t); ++k) {
        b_frame();
    }
    b_check_ended(SERVO_TEST_AB_INA_RESET);
    CHECK(strstr(bench.out.report, "Result:         ABORTED - INA3221 reset "
                               "itself\n") != NULL);
}

/* The link goes during a run on the INA3221: the meter is the model, and
 * the run ends as link lost. */
TEST_CASE(the_link_going_ends_a_run_on_the_ina3221)
{
    b_fresh(5000u);
    servo_test_cfg_t c;
    b_cfg(&c);
    b_start(&c);
    CHECK(b_until(SERVO_TEST_PH_HOLD));
    bench.link_up = false;
    sense_link_lost(&chain.sl);
    b_frame();
    CHECK_EQ(servo_source_id(&bench.src), SERVO_SOURCE_MODEL);
    b_check_ended(SERVO_TEST_AB_LINK);
}

/* A servo on a stop, on a bench whose supply limit is above what it
 * draws there.  The model's 2.4A is over the 0.1Ohm shunt's 1.638A:
 * every sample clips and the windows read 1.638A.  With STALL AT under
 * that the run ends on STALL AT.  With STALL AT at its default, 2.00A,
 * no reading passes it: the run goes on to its end, counts the clipped
 * windows and says STALL AT cannot be reached -- the shunt decides what
 * this meter can show. */
TEST_CASE(a_servo_on_a_stop_reads_the_range_and_is_judged_on_that)
{
    b_fresh(5000u);
    bench.servo.cfg.stop_hi_us = 1850u;
    servo_test_cfg_t c;
    b_cfg(&c);
    c.step_count = 1u;
    c.stall_a = 1.5f;
    b_start(&c);
    b_out();
    CHECK(bench.t.clipped >= 19u);
    CHECK(bench.t.stalled);
    b_check_ended(SERVO_TEST_AB_STALL);
    CHECK(strstr(bench.out.report, "Stall threshold  highest 1.638A, STALL AT "
                               "1.50A: FAIL\n") != NULL);
    CHECK(strstr(bench.out.report, "Clipped:        ") != NULL);
    CHECK(rows_with(bench.out.csv, COL_CLIPPED, "50") >= 15u);
    CHECK(rows_with(bench.out.csv, COL_CURRENT, "1.638") >= 15u);

    b_fresh(5000u);
    bench.servo.cfg.stop_hi_us = 1850u;
    b_cfg(&c);
    c.step_count = 1u;
    c.stall_a = 2.0f;
    b_start(&c);
    b_out();
    CHECK_EQ(bench.t.why, SERVO_TEST_AB_NONE);
    CHECK(!bench.t.stalled);
    CHECK(bench.t.clipped > 40u);
    CHECK_EQ(bench.t.cc_readings, 0u);
    CHECK_NEAR(bench.t.stall_peak_a, 1.638f, 0.001f);
    CHECK(strstr(bench.out.report, "Stall threshold  highest 1.638A, STALL AT "
                               "2.00A: PASS\n") != NULL);
    CHECK(strstr(bench.out.report, "STALL AT 2.00A cannot be reached: INA3221 "
                               "range 1.638A\n") != NULL);
    char want[96];
    snprintf(want, sizeof(want), "Clipped:        %lu windows hold a sample "
             "at an end of the range, 1.638A", (unsigned long)bench.t.clipped);
    CHECK(strstr(bench.out.report, want) != NULL);
}

/* The same servo against a supply that holds 1.00A: the INA3221 reads
 * the limit, under STALL AT, and the supply's constant current ends the
 * run a second after it begins. */
TEST_CASE(a_servo_on_a_stop_against_the_limit_ends_on_constant_current)
{
    b_fresh(5000u);
    bench.servo.cfg.stop_hi_us = 1850u;
    bench.limits = true;
    servo_test_cfg_t c;
    b_cfg(&c);
    c.step_count = 1u;
    c.i_limit = 1.0f;
    c.stall_a = 2.0f;
    b_start(&c);
    b_out();
    CHECK(!bench.t.stalled);
    CHECK_EQ(bench.t.clipped, 0u);
    CHECK(bench.t.cc_longest_ms >= 1000u && bench.t.cc_longest_ms < 1000u + PD_MS);
    CHECK_NEAR(bench.t.stall_peak_a, 1.0f, 0.002f);
    b_check_ended(SERVO_TEST_AB_CC);
    CHECK(strstr(bench.out.report, "STALL AT 2.00A cannot be reached: current "
                               "limit 1.00A\n") != NULL);
    CHECK(strstr(bench.out.report, "STALL AT 2.00A cannot be reached: INA3221 "
                               "range 1.638A\n") != NULL);
}

/* A shunt wired the other way round, through the chain: the part's signed
 * windows reach the run signed. */
TEST_CASE(a_reversed_shunt_reads_negative_through_the_chain)
{
    b_fresh(5000u);
    bench.sign = -1.0;
    servo_test_cfg_t c;
    b_cfg(&c);
    c.step_count = 1u;
    b_start(&c);
    b_out();
    CHECK_EQ(bench.t.why, SERVO_TEST_AB_NONE);
    CHECK(servo_test_negative_at_rest(&bench.t));
    const servo_test_step_t *s = &bench.t.steps[0];
    CHECK_NEAR(s->idle.sum / (float)s->idle.n, -0.12f, 0.005f);
    CHECK_NEAR(s->peak_a, -0.96f, 0.012f);
    CHECK(strstr(bench.out.report, "Current reads negative at rest: shunt "
                               "direction\n") != NULL);
    CHECK(strstr(bench.out.csv, ";-0.1") != NULL);
}

int main(void)
{
    RUN(a_full_run_on_the_ina3221_through_the_chain);
    RUN(windows_that_arrive_together_keep_their_own_times);
    RUN(a_full_run_on_the_pd_mini_beside_a_working_ina3221);
    RUN(the_meter_failing_mid_run_never_switches_meter);
    RUN(a_part_that_resets_itself_ends_the_run);
    RUN(the_link_going_ends_a_run_on_the_ina3221);
    RUN(a_servo_on_a_stop_reads_the_range_and_is_judged_on_that);
    RUN(a_servo_on_a_stop_against_the_limit_ends_on_constant_current);
    RUN(a_reversed_shunt_reads_negative_through_the_chain);
    out_reset(&bench.out);
    return test_summary("servo_test_win_chain");
}
