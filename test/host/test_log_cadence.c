/*
 * The run log's cadence: one row per sample, stamped with the wall time
 * since the run's start.
 *
 * Two kinds of test.  The rules one call at a time, at each limit and one
 * step either side.  And a model of the panel's control loop -- passes on a
 * 5 ms tick grid, a 50 ms poll gate looked at once per pass, exchanges of
 * 1 to 3 ms, a command written ahead of the poll -- that drives
 * log_cadence_row() for 10 minutes of simulated time and counts samples
 * against rows.  The model also runs a rule of two independent 50 ms gates,
 * one on the poll and one on the row, to hold what that rule loses.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <string.h>

#include "greatest.h"

#include "bench_state.h"
#include "log_cadence.h"

/* ------------------------------------------------- one call at a time */

TEST_CASE(the_constants_are_the_sample_rate_and_the_totals_cap)
{
    CHECK_EQ(LOG_CADENCE_MODEL_MS, 50);
    CHECK_EQ(LOG_CADENCE_MODEL_MAX_MS, 1000);
    CHECK_NEAR((double)LOG_CADENCE_MODEL_MAX_MS / 1000.0,
               BENCH_TOTALS_MAX_STEP_S, 1e-9);
}

/* The gate at @p since ms after it last opened, from tick @p t0. */
static bool model_at(uint32_t t0, uint32_t since, bool link_up, float *step)
{
    log_cadence_t c;
    log_cadence_init(&c, t0);
    return log_cadence_model_due(&c, (uint32_t)(t0 + since), link_up, step);
}

TEST_CASE(the_model_steps_at_50_ms_and_not_before)
{
    static const uint32_t starts[] = { 0u, 1000u, 0xFFFFFFE0u, 0xFFFFFFFFu };
    for (size_t k = 0; k < sizeof(starts) / sizeof(starts[0]); ++k) {
        float step = -1.0f;
        CHECK(!model_at(starts[k], 0u, false, &step));
        CHECK_NEAR(step, 0.0, 1e-9);
        step = -1.0f;
        CHECK(!model_at(starts[k], 49u, false, &step));
        CHECK_NEAR(step, 0.0, 1e-9);
        CHECK(model_at(starts[k], 50u, false, &step));
        CHECK_NEAR(step, 0.050, 1e-6);
        CHECK(model_at(starts[k], 51u, false, &step));
        CHECK_NEAR(step, 0.051, 1e-6);
    }
}

TEST_CASE(the_models_step_is_the_time_passed_up_to_one_second)
{
    static const uint32_t starts[] = { 0u, 0xFFFFFC00u };
    for (size_t k = 0; k < sizeof(starts) / sizeof(starts[0]); ++k) {
        float step = 0.0f;
        CHECK(model_at(starts[k], 999u, false, &step));
        CHECK_NEAR(step, 0.999, 1e-6);
        CHECK(model_at(starts[k], 1000u, false, &step));
        CHECK_NEAR(step, 1.000, 1e-6);
        CHECK(model_at(starts[k], 1001u, false, &step));
        CHECK_NEAR(step, 1.000, 1e-6);
        CHECK(model_at(starts[k], 30000u, false, &step));
        CHECK_NEAR(step, 1.000, 1e-6);
    }
}

TEST_CASE(the_models_clock_moves_with_the_link_up_and_takes_no_step)
{
    log_cadence_t c;
    log_cadence_init(&c, 100u);
    float step = -1.0f;
    /* Up: the gate opens at 50 ms, no step. */
    CHECK(!log_cadence_model_due(&c, 149u, true, &step));
    CHECK(!log_cadence_model_due(&c, 150u, true, &step));
    CHECK_NEAR(step, 0.0, 1e-9);
    /* Down 49 ms after that opening: not due; at 50 ms a step of 50 ms,
     * not of the 100 ms since the start. */
    CHECK(!log_cadence_model_due(&c, 199u, false, &step));
    CHECK(log_cadence_model_due(&c, 200u, false, &step));
    CHECK_NEAR(step, 0.050, 1e-6);
}

TEST_CASE(the_model_keeps_20_steps_a_second_on_a_5_ms_grid)
{
    static const uint32_t starts[] = { 0u, 0xFFFFFE00u };
    for (size_t k = 0; k < sizeof(starts) / sizeof(starts[0]); ++k) {
        log_cadence_t c;
        log_cadence_init(&c, starts[k]);
        unsigned steps = 0u;
        double   sum   = 0.0;
        for (uint32_t ms = 5u; ms <= 10000u; ms += 5u) {
            float step = 0.0f;
            if (log_cadence_model_due(&c, (uint32_t)(starts[k] + ms), false,
                                      &step)) {
                ++steps;
                sum += (double)step;
            }
        }
        CHECK_EQ(steps, 200);
        CHECK_NEAR(sum, 10.0, 1e-3);
    }
}

TEST_CASE(a_row_needs_a_sample_and_a_bench_run)
{
    log_cadence_t c;
    log_cadence_init(&c, 0u);
    log_cadence_run_start(&c, 0u);
    float t = -1.0f;
    CHECK(!log_cadence_row(&c, 5u, false, false, &t));
    CHECK(!log_cadence_row(&c, 10u, false, true, &t));
    CHECK(!log_cadence_row(&c, 15u, true, false, &t));
    CHECK_NEAR(t, -1.0, 1e-9);                 /* written with a row only */
    CHECK(log_cadence_row(&c, 20u, true, true, &t));
    CHECK_NEAR(t, 0.020, 1e-6);
}

TEST_CASE(two_samples_in_two_passes_are_two_rows)
{
    /* No gate of its own: the sample rate is the caller's. */
    log_cadence_t c;
    log_cadence_init(&c, 0u);
    float t = 0.0f;
    CHECK(log_cadence_row(&c, 0u, true, true, &t));
    CHECK_NEAR(t, 0.000, 1e-6);
    CHECK(log_cadence_row(&c, 0u, true, true, &t));
    CHECK_NEAR(t, 0.000, 1e-6);
    CHECK(log_cadence_row(&c, 1u, true, true, &t));
    CHECK_NEAR(t, 0.001, 1e-6);
    CHECK(log_cadence_row(&c, 50u, true, true, &t));
    CHECK_NEAR(t, 0.050, 1e-6);
}

TEST_CASE(the_time_is_since_the_runs_start_across_the_tick_wrap)
{
    static const uint32_t starts[] = {
        0u, 1u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFEu, 0xFFFFFFFFu,
    };
    for (size_t k = 0; k < sizeof(starts) / sizeof(starts[0]); ++k) {
        log_cadence_t c;
        log_cadence_init(&c, (uint32_t)(starts[k] - 777u));
        log_cadence_run_start(&c, starts[k]);
        float t = -1.0f;
        CHECK(log_cadence_row(&c, starts[k], true, true, &t));
        CHECK_NEAR(t, 0.0, 1e-9);
        CHECK(log_cadence_row(&c, (uint32_t)(starts[k] + 1u), true, true, &t));
        CHECK_NEAR(t, 0.001, 1e-6);
        CHECK(log_cadence_row(&c, (uint32_t)(starts[k] + 2u), true, true, &t));
        CHECK_NEAR(t, 0.002, 1e-6);
        CHECK(log_cadence_row(&c, (uint32_t)(starts[k] + 190800u), true, true,
                              &t));
        CHECK_NEAR(t, 190.8, 1e-3);
    }
}

TEST_CASE(passes_without_a_row_advance_the_time)
{
    log_cadence_t c;
    log_cadence_init(&c, 0xFFFFFF00u);
    float t = 0.0f;
    uint32_t now = 0xFFFFFF00u;
    for (int k = 0; k < 500; ++k) {            /* 2.5 s with no sample */
        now += 5u;
        CHECK(!log_cadence_row(&c, now, false, true, &t));
    }
    CHECK(log_cadence_row(&c, now, true, true, &t));
    CHECK_NEAR(t, 2.5, 1e-4);
}

TEST_CASE(a_run_longer_than_the_ticks_range_keeps_counting)
{
    log_cadence_t c;
    log_cadence_init(&c, 0u);
    float t = 0.0f, last = -1.0f;
    uint32_t now = 0u;
    for (int k = 0; k < 8; ++k) {              /* 8 x 2^30 ms = 2^33 ms */
        now += 0x40000000u;
        CHECK(log_cadence_row(&c, now, true, true, &t));
        CHECK(t > last);
        last = t;
    }
    CHECK_NEAR(t, 8589934.592, 1.0);
}

TEST_CASE(a_run_start_zeroes_the_time_and_the_counts)
{
    log_cadence_t c;
    log_cadence_init(&c, 0u);
    float t = 0.0f;
    CHECK(log_cadence_row(&c, 4000u, true, true, &t));
    log_cadence_posted(&c, true);
    log_cadence_posted(&c, false);
    CHECK_EQ(log_cadence_sent(&c), 1);
    CHECK_EQ(log_cadence_lost(&c), 1);
    log_cadence_run_start(&c, 5000u);
    CHECK_EQ(log_cadence_sent(&c), 0);
    CHECK_EQ(log_cadence_lost(&c), 0);
    CHECK(log_cadence_row(&c, 5052u, true, true, &t));
    CHECK_NEAR(t, 0.052, 1e-6);
}

TEST_CASE(every_row_is_counted_as_sent_or_lost)
{
    log_cadence_t c;
    log_cadence_init(&c, 0u);
    CHECK_EQ(log_cadence_sent(&c), 0);
    CHECK_EQ(log_cadence_lost(&c), 0);
    log_cadence_posted(&c, true);
    CHECK_EQ(log_cadence_sent(&c), 1);
    CHECK_EQ(log_cadence_lost(&c), 0);
    log_cadence_posted(&c, false);
    CHECK_EQ(log_cadence_sent(&c), 1);
    CHECK_EQ(log_cadence_lost(&c), 1);
    log_cadence_posted(&c, false);
    log_cadence_posted(&c, true);
    CHECK_EQ(log_cadence_sent(&c), 2);
    CHECK_EQ(log_cadence_lost(&c), 2);
}

/* A pass that writes rows writes one for each INA3221 window handed over
 * in it, and one when none was: a sample is never without its row. */
TEST_CASE(a_pass_writes_one_row_for_each_window_and_one_without)
{
    CHECK_EQ(log_cadence_rows(0u), 1u);
    CHECK_EQ(log_cadence_rows(1u), 1u);
    CHECK_EQ(log_cadence_rows(2u), 2u);
    CHECK_EQ(log_cadence_rows(3u), 3u);
    CHECK_EQ(log_cadence_rows(4u), 4u);
    CHECK_EQ(log_cadence_rows(LINK_SW_RING), LINK_SW_RING);

    /* The rows of one pass carry the pass's time, and each is counted. */
    log_cadence_t c;
    log_cadence_init(&c, 0xFFFFFF00u);
    float t = -1.0f;
    CHECK(log_cadence_row(&c, 0xFFFFFF00u + 150u, true, true, &t));
    const unsigned rows = log_cadence_rows(3u);
    for (unsigned k = 0u; k < rows; ++k) {
        log_cadence_posted(&c, k != 1u);
    }
    CHECK_NEAR(t, 0.150, 1e-6);
    CHECK_EQ(log_cadence_sent(&c), 2);
    CHECK_EQ(log_cadence_lost(&c), 1);
    /* A pass without a sample writes none, whatever was handed over. */
    CHECK(!log_cadence_row(&c, 0xFFFFFF00u + 155u, false, true, &t));
    CHECK(log_cadence_row(&c, 50u, true, true, &t));    /* past the wrap */
    CHECK_NEAR(t, 0.306, 1e-6);
}

/* ---------------------------------------------- the control loop's grid */

#define PASS_MS        5u      /* the loop's delay, on a 1 ms tick          */
#define POLL_MS        50u     /* the poll gate with the link up            */
#define EXCH_MIN_US    1000u   /* one exchange with the coprocessor         */
#define EXCH_MAX_US    3000u
#define QUEUE_ROWS     64u     /* the logger's queue                        */
#define TEN_MINUTES_MS 600000u

typedef enum {
    RULE_SHARED,     /* log_cadence_row() */
    RULE_TWO_GATES,  /* a row when the poll's gate and a second 50 ms gate
                      * are open in the same pass; the time grows by the
                      * second gate's last interval per row */
} rule_t;

typedef struct {
    rule_t   rule;
    uint32_t tick0;          /* the tick at the run's start                 */
    uint32_t run_ms;
    uint32_t cmd_every_ms;   /* 0: none                                     */
    uint32_t cmd_first_ms;   /* the first command, after the run's start    */
    bool     cmd_random;     /* commands 200 to 3000 ms apart instead       */
    uint32_t seed;
    uint32_t stall_at_ms;    /* the loop held for stall_ms from here; 0: no */
    uint32_t stall_ms;
    uint32_t card_from_ms;   /* the logger takes no row in this stretch     */
    uint32_t card_to_ms;
} model_cfg_t;

typedef struct {
    unsigned samples;
    unsigned rows;           /* rows the rule granted                       */
    unsigned sent;
    unsigned lost;
    unsigned longest_ms;     /* longest time between two granted rows       */
    double   last_t_s;       /* the last granted row's time                 */
    double   worst_s;        /* largest |row time - time of its pass|       */
    bool     monotonic;
    double   widest_step_s;  /* largest step of the time between two rows   */
    double   file_gap_s;     /* largest step between two rows the queue took */
} model_out_t;

static uint32_t s_lcg;

static uint32_t rnd(uint32_t lo, uint32_t hi)
{
    s_lcg = s_lcg * 1664525u + 1013904223u;
    return lo + ((s_lcg >> 8) % (hi - lo + 1u));
}

static void model_run(const model_cfg_t *cfg, model_out_t *out)
{
    memset(out, 0, sizeof(*out));
    out->monotonic = true;
    s_lcg = cfg->seed;

    uint64_t us = 0u;        /* since the run's start */
#define NOW_MS() ((uint32_t)(cfg->tick0 + (uint32_t)(us / 1000u)))

    log_cadence_t cad;
    log_cadence_init(&cad, NOW_MS());
    log_cadence_run_start(&cad, NOW_MS());

    uint32_t last_poll   = NOW_MS();
    uint32_t last_sample = NOW_MS();      /* RULE_TWO_GATES */
    float    gate_t      = 0.0f;          /* RULE_TWO_GATES */
    uint64_t next_cmd_us = (uint64_t)cfg->cmd_first_ms * 1000u;
    bool     cmds        = cfg->cmd_every_ms != 0u || cfg->cmd_random;
    if (cfg->cmd_random) {
        next_cmd_us = (uint64_t)rnd(200u, 3000u) * 1000u;
    }
    bool     stalled     = false;
    unsigned queued      = 0u;
    uint64_t last_row_us = 0u;
    double   last_t      = -1.0;
    double   last_file_t = -1.0;

    while (us < (uint64_t)cfg->run_ms * 1000u) {
        const uint64_t pass_us = us;

        /* The logger, at a lower priority: the queue is empty again at the
         * top of a pass unless the card is not taking rows. */
        const uint32_t at_ms = (uint32_t)(us / 1000u);
        if (at_ms < cfg->card_from_ms || at_ms >= cfg->card_to_ms) {
            queued = 0u;
        }

        /* The loop held: an exchange that waits out its timeout. */
        if (cfg->stall_ms != 0u && !stalled && at_ms >= cfg->stall_at_ms) {
            stalled = true;
            us += (uint64_t)cfg->stall_ms * 1000u;
        }

        /* A command's write, ahead of the poll gate. */
        if (cmds && us >= next_cmd_us) {
            next_cmd_us += cfg->cmd_random
                               ? (uint64_t)rnd(200u, 3000u) * 1000u
                               : (uint64_t)cfg->cmd_every_ms * 1000u;
            us += rnd(EXCH_MIN_US, EXCH_MAX_US);
        }

        /* The poll: 2 to 5 exchanges, and a sample. */
        bool new_sample = false;
        if ((uint32_t)(NOW_MS() - last_poll) >= POLL_MS) {
            last_poll = NOW_MS();
            const uint32_t n = rnd(2u, 5u);
            for (uint32_t k = 0u; k < n; ++k) {
                us += rnd(EXCH_MIN_US, EXCH_MAX_US);
            }
            new_sample = true;
            ++out->samples;
        }

        bool  row = false;
        float t_s = 0.0f;
        if (cfg->rule == RULE_SHARED) {
            row = log_cadence_row(&cad, NOW_MS(), new_sample, true, &t_s);
        } else {
            const uint32_t since = (uint32_t)(NOW_MS() - last_sample);
            if (since >= 50u) {
                last_sample = NOW_MS();
                float step_s = (float)since / 1000.0f;
                if (step_s > BENCH_TOTALS_MAX_STEP_S) {
                    step_s = BENCH_TOTALS_MAX_STEP_S;
                }
                if (new_sample) {
                    gate_t += step_s;
                    t_s = gate_t;
                    row = true;
                }
            }
        }

        if (row) {
            ++out->rows;
            const bool taken = queued < QUEUE_ROWS;
            if (taken) {
                ++queued;
            }
            log_cadence_posted(&cad, taken);

            /* The row's time against the pass that took the sample: not
             * before the pass began, not after the row was decided. */
            const double begin = (double)(pass_us / 1000u) / 1000.0;
            const double end   = (double)(us / 1000u) / 1000.0;
            double err = 0.0;
            if ((double)t_s < begin) {
                err = begin - (double)t_s;
            } else if ((double)t_s > end) {
                err = (double)t_s - end;
            }
            if (err > out->worst_s) {
                out->worst_s = err;
            }
            if ((double)t_s < last_t) {
                out->monotonic = false;
            }
            if (last_t >= 0.0 && (double)t_s - last_t > out->widest_step_s) {
                out->widest_step_s = (double)t_s - last_t;
            }
            last_t = (double)t_s;
            if (taken) {
                if (last_file_t >= 0.0
                    && (double)t_s - last_file_t > out->file_gap_s) {
                    out->file_gap_s = (double)t_s - last_file_t;
                }
                last_file_t = (double)t_s;
            }
            const unsigned gap = (unsigned)((us - last_row_us) / 1000u);
            if (gap > out->longest_ms) {
                out->longest_ms = gap;
            }
            last_row_us  = us;
            out->last_t_s = (double)t_s;
        }

        /* The loop's delay: the 5th tick boundary from here. */
        us = (us / 1000u + PASS_MS) * 1000u;
    }
#undef NOW_MS
    out->sent = log_cadence_sent(&cad);
    out->lost = log_cadence_lost(&cad);
}

static model_cfg_t ten_minutes(rule_t rule)
{
    model_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.rule   = rule;
    cfg.run_ms = TEN_MINUTES_MS;
    cfg.seed   = 12345u;
    return cfg;
}

/*
 * Every phase of the command against the poll: the poll's period on this
 * grid is 50 to 70 ms, so first commands 0 to 69 ms after the start, 1 ms
 * apart, put the command in every pass of a poll period.
 */
#define PHASES 70u

TEST_CASE(every_sample_is_a_row_with_a_command_every_1750_ms)
{
    for (uint32_t phase = 0u; phase < PHASES; ++phase) {
        model_cfg_t cfg = ten_minutes(RULE_SHARED);
        cfg.cmd_every_ms = 1750u;
        cfg.cmd_first_ms = phase;
        model_out_t o;
        model_run(&cfg, &o);
        CHECK(o.samples > 8000u);
        CHECK_EQ(o.rows, o.samples);
        CHECK_EQ(o.sent, o.samples);
        CHECK_EQ(o.lost, 0);
        CHECK(o.monotonic);
        CHECK_NEAR(o.worst_s, 0.0, 1e-3);
        CHECK(o.longest_ms <= 80u);
        CHECK(o.last_t_s > 599.9 && o.last_t_s <= 600.02);
    }
}

TEST_CASE(every_sample_is_a_row_with_commands_at_random_times)
{
    for (uint32_t seed = 1u; seed <= 20u; ++seed) {
        model_cfg_t cfg = ten_minutes(RULE_SHARED);
        cfg.cmd_random = true;
        cfg.seed       = seed * 7919u;
        model_out_t o;
        model_run(&cfg, &o);
        CHECK(o.samples > 8000u);
        CHECK_EQ(o.rows, o.samples);
        CHECK_EQ(o.sent, o.samples);
        CHECK_EQ(o.lost, 0);
        CHECK(o.monotonic);
        CHECK_NEAR(o.worst_s, 0.0, 1e-3);
        CHECK(o.longest_ms <= 80u);
    }
}

TEST_CASE(every_sample_is_a_row_with_no_command)
{
    model_cfg_t cfg = ten_minutes(RULE_SHARED);
    model_out_t o;
    model_run(&cfg, &o);
    CHECK(o.samples > 8000u);
    CHECK_EQ(o.rows, o.samples);
    CHECK_EQ(o.lost, 0);
}

/*
 * What a second, independent 50 ms gate on the row costs on the same grid.
 * Not a rule of the firmware: it is here to hold the reason the log has no
 * gate of its own.
 */
TEST_CASE(a_second_50_ms_gate_loses_rows_and_compresses_the_time)
{
    unsigned samples = 0u, rows = 0u, worst_gap_ms = 0u;
    double   shortest_end_s = 1e9;
    for (uint32_t phase = 0u; phase < PHASES; ++phase) {
        model_cfg_t cfg = ten_minutes(RULE_TWO_GATES);
        cfg.cmd_every_ms = 1750u;
        cfg.cmd_first_ms = phase;
        model_out_t o;
        model_run(&cfg, &o);
        CHECK(o.rows < o.samples);
        /* The time is never ahead of the wall clock and falls behind it. */
        CHECK(o.last_t_s < 600.0);
        samples += o.samples;
        rows    += o.rows;
        if (o.longest_ms > worst_gap_ms) {
            worst_gap_ms = o.longest_ms;
        }
        if (o.last_t_s < shortest_end_s) {
            shortest_end_s = o.last_t_s;
        }
    }
    printf("  two gates, command every 1750 ms, %u phases of 10 min: "
           "%u rows for %u samples, longest time without a row %u ms, "
           "shortest time column %.1f s of 600 s\n",
           PHASES, rows, samples, worst_gap_ms, shortest_end_s);
    CHECK(rows < samples);
}

/* ----------------------------------------------------- the time column */

TEST_CASE(the_time_is_the_wall_clock_across_the_tick_wrap)
{
    /* The wrap 0, 1, 5 and 300 s into the run, and not at all. */
    static const uint32_t tick0[] = {
        0u, 0xFFFFFFFFu, 0xFFFFFC18u, 0xFFFFEC78u, 0xFFFB6C20u,
    };
    for (size_t k = 0; k < sizeof(tick0) / sizeof(tick0[0]); ++k) {
        model_cfg_t cfg = ten_minutes(RULE_SHARED);
        cfg.tick0        = tick0[k];
        cfg.cmd_every_ms = 1750u;
        model_out_t o;
        model_run(&cfg, &o);
        CHECK_EQ(o.rows, o.samples);
        CHECK(o.monotonic);
        CHECK_NEAR(o.worst_s, 0.0, 1e-3);
        CHECK(o.widest_step_s <= 0.080);
        CHECK(o.last_t_s > 599.9 && o.last_t_s <= 600.02);
    }
}

TEST_CASE(a_stall_shows_as_a_step_of_its_length)
{
    static const uint32_t stalls[] = { 100u, 1000u, 30000u };
    /* The stall before the wrap, across it and after it. */
    static const uint32_t tick0[] = { 0u, 0xFFFE7960u, 0xFFFFFFFFu };
    for (size_t k = 0; k < sizeof(stalls) / sizeof(stalls[0]); ++k) {
        for (size_t j = 0; j < sizeof(tick0) / sizeof(tick0[0]); ++j) {
            model_cfg_t cfg = ten_minutes(RULE_SHARED);
            cfg.tick0        = tick0[j];
            cfg.cmd_every_ms = 1750u;
            cfg.stall_at_ms  = 99990u;       /* 10 ms ahead of the wrap */
            cfg.stall_ms     = stalls[k];
            model_out_t o;
            model_run(&cfg, &o);
            CHECK_EQ(o.rows, o.samples);
            CHECK(o.monotonic);
            /* Every row at its pass's time, the row after the stall too. */
            CHECK_NEAR(o.worst_s, 0.0, 1e-3);
            /* The stall and at most the poll period around it. */
            const double stall_s = (double)stalls[k] / 1000.0;
            CHECK(o.widest_step_s >= stall_s);
            CHECK(o.widest_step_s <= stall_s + 0.080);
            CHECK(o.last_t_s > 599.9
                  && o.last_t_s <= 600.02 + stall_s);
        }
    }
}

TEST_CASE(a_full_queue_is_counted_and_the_time_goes_on)
{
    /* The card takes nothing for 10 s: the queue holds 64 rows of it. */
    static const uint32_t tick0[] = { 0u, 0xFFFE7960u };
    for (size_t j = 0; j < sizeof(tick0) / sizeof(tick0[0]); ++j) {
        model_cfg_t cfg = ten_minutes(RULE_SHARED);
        cfg.tick0        = tick0[j];
        cfg.cmd_every_ms = 1750u;
        cfg.card_from_ms = 95000u;
        cfg.card_to_ms   = 105000u;
        model_out_t o;
        model_run(&cfg, &o);
        CHECK_EQ(o.rows, o.samples);
        CHECK_EQ(o.sent + o.lost, o.rows);
        CHECK(o.lost > 0u);
        /* 10 s at one sample every 50 to 70 ms, less the 64 queued. */
        CHECK(o.lost >= 10000u / 70u - QUEUE_ROWS - 2u);
        CHECK(o.lost <= 10000u / 50u - QUEUE_ROWS + 2u);
        CHECK(o.monotonic);
        CHECK_NEAR(o.worst_s, 0.0, 1e-3);
        /* In the file: a step as long as the rows that were refused. */
        CHECK(o.file_gap_s >= (double)o.lost * 0.050);
        CHECK(o.file_gap_s <= (double)(o.lost + 1u) * 0.070 + 0.001);
        CHECK(o.last_t_s > 599.9 && o.last_t_s <= 600.02);
    }
}

TEST_CASE(a_queue_one_short_of_full_loses_nothing)
{
    log_cadence_t c;
    log_cadence_init(&c, 0u);
    unsigned queued = 0u;
    float    t      = 0.0f;
    uint32_t now    = 0u;
    for (unsigned k = 0u; k < QUEUE_ROWS + 1u; ++k) {
        now += 53u;
        CHECK(log_cadence_row(&c, now, true, true, &t));
        const bool taken = queued < QUEUE_ROWS;
        if (taken) {
            ++queued;
        }
        log_cadence_posted(&c, taken);
        if (k == QUEUE_ROWS - 2u) {            /* 63 rows: one slot free */
            CHECK_EQ(log_cadence_lost(&c), 0);
        }
        if (k == QUEUE_ROWS - 1u) {            /* 64 rows: full, none lost */
            CHECK_EQ(log_cadence_sent(&c), QUEUE_ROWS);
            CHECK_EQ(log_cadence_lost(&c), 0);
        }
    }
    CHECK_EQ(log_cadence_sent(&c), QUEUE_ROWS);    /* the 65th is lost */
    CHECK_EQ(log_cadence_lost(&c), 1);
    CHECK_NEAR(t, 65.0 * 0.053, 1e-4);
}

int main(void)
{
    RUN(the_constants_are_the_sample_rate_and_the_totals_cap);
    RUN(the_model_steps_at_50_ms_and_not_before);
    RUN(the_models_step_is_the_time_passed_up_to_one_second);
    RUN(the_models_clock_moves_with_the_link_up_and_takes_no_step);
    RUN(the_model_keeps_20_steps_a_second_on_a_5_ms_grid);
    RUN(a_row_needs_a_sample_and_a_bench_run);
    RUN(two_samples_in_two_passes_are_two_rows);
    RUN(the_time_is_since_the_runs_start_across_the_tick_wrap);
    RUN(passes_without_a_row_advance_the_time);
    RUN(a_run_longer_than_the_ticks_range_keeps_counting);
    RUN(a_run_start_zeroes_the_time_and_the_counts);
    RUN(every_row_is_counted_as_sent_or_lost);
    RUN(a_pass_writes_one_row_for_each_window_and_one_without);
    RUN(every_sample_is_a_row_with_a_command_every_1750_ms);
    RUN(every_sample_is_a_row_with_commands_at_random_times);
    RUN(every_sample_is_a_row_with_no_command);
    RUN(a_second_50_ms_gate_loses_rows_and_compresses_the_time);
    RUN(the_time_is_the_wall_clock_across_the_tick_wrap);
    RUN(a_stall_shows_as_a_step_of_its_length);
    RUN(a_full_queue_is_counted_and_the_time_goes_on);
    RUN(a_queue_one_short_of_full_loses_nothing);
    return test_summary("log_cadence");
}
