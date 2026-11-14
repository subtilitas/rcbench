/*
 * The balance node's readings, captures and solution on the panel
 * (node_balance.h), and both methods end to end: a rotor model's 1x
 * vectors encoded as reports, carried by bal_rig.h's node through the
 * controller's decode, captured and solved.
 *
 * Under test: a report to the reading (mg to g, 0.1° to degrees, no
 * magnitude and no angle without a counted revolution, no angle in
 * no-index mode); the stale rule at
 * 999ms and 1000ms; the quality word's order and its 90% and 70% edges;
 * TAKE refused without a fresh report, while another runs and for a run
 * the method does not have; a capture of 8 new reports (a report seen
 * again counts once), failing on a gap of 1000ms and not 999ms, on a
 * report without a revolution, without the index in two runs, and on an
 * rpm 5% off (not at 5%); a trial whose mean rpm is 5.1% from the
 * baseline's refused in both methods, 4.9% taken, and no row from it; the two-run and four-run corrections against
 * known imbalances; the CSV row's fields under its header; the instance's
 * one row per solution, solved again with a new row for a new trial mass,
 * the row's written / not written state; a dropped link stale, not the
 * model.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "bal_rig.h"
#include "node_balance.h"
#include "settings.h"
#include "ui_text.h"

/** @brief A steady report, as bal_rig.h's. */
static rcnode_bal_report_t steady(uint16_t mg, uint16_t tenths, uint16_t rpm)
{
    return brig_steady(mg, tenths, rpm);
}

TEST_CASE(a_report_reads_in_g_and_degrees)
{
    nb_reading_t r;
    rcnode_bal_report_t p = steady(420u, 2650u, 8000u);
    nb_read(true, &p, 10u, &r);
    CHECK_EQ(r.live, NB_LIVE_FRESH);
    CHECK_NEAR(r.mag, 0.42, 1e-6);
    CHECK_NEAR(r.deg, 265.0, 1e-4);
    CHECK_EQ(r.rpm, 8000u);
    CHECK(r.phase);
    CHECK(!r.counts);
    CHECK_EQ(r.word, NB_Q_GOOD);
    char b[32];
    CHECK_STR_EQ(nb_fmt_mag(&r, b, sizeof b), "0.42g");
    CHECK_STR_EQ(nb_fmt_deg(&r, b, sizeof b), "265°");
    CHECK_STR_EQ(nb_fmt_rpm(&r, b, sizeof b), "8000rpm");
    /* No-index mode: magnitudes, no angle. */
    p.flags |= RCNODE_BF_NO_INDEX;
    nb_read(true, &p, 10u, &r);
    CHECK(!r.phase);
    CHECK_STR_EQ(nb_fmt_deg(&r, b, sizeof b), "--");
    CHECK_EQ(r.word, NB_Q_NO_PHASE);
    /* No revolution: no magnitude, no rpm, no angle. */
    p = steady(420u, 2650u, 8000u);
    p.flags &= (uint16_t)~RCNODE_BF_VALID;
    nb_read(true, &p, 10u, &r);
    CHECK(!r.valid);
    CHECK(!r.phase);
    CHECK_EQ(r.rpm, 0u);
    CHECK_STR_EQ(nb_fmt_mag(&r, b, sizeof b), "--");
    CHECK_STR_EQ(nb_fmt_rpm(&r, b, sizeof b), "--");
    p = steady(0u, 0u, 0u);
    p.flags &= (uint16_t)~RCNODE_BF_VALID;
    nb_read(true, &p, 10u, &r);
    CHECK_STR_EQ(nb_fmt_mag(&r, b, sizeof b), "--");
    /* ADC counts (a piezo). */
    p = steady(412u, 2650u, 8000u);
    p.flags |= RCNODE_BF_COUNTS;
    nb_read(true, &p, 10u, &r);
    CHECK(r.counts);
    char want[32];
    snprintf(want, sizeof want, TR(NB_COUNTS), 412u);
    CHECK_STR_EQ(nb_fmt_mag(&r, b, sizeof b), want);
    /* 359.96° rounds to 0°, not 360°. */
    p = steady(420u, 3599u, 8000u);
    nb_read(true, &p, 10u, &r);
    CHECK_STR_EQ(nb_fmt_deg(&r, b, sizeof b), "0°");
    /* A quality over 100% reads as 100%. */
    p = steady(420u, 2650u, 8000u);
    p.quality = 101u;
    nb_read(true, &p, 10u, &r);
    CHECK_EQ(r.quality, 100u);
    /* NULL out, NULL buffers. */
    nb_read(true, &p, 10u, NULL);
    CHECK_STR_EQ(nb_fmt_mag(&r, NULL, 0), "");
}

/* A report is shown for 999ms and is "--" at 1000ms; with no node the
 * reading is the model's. */
TEST_CASE(the_stale_rule_at_its_edge)
{
    nb_reading_t r;
    const rcnode_bal_report_t p = steady(420u, 2650u, 8000u);
    char b[32];
    nb_read(true, &p, BAL_STALE_MS - 1u, &r);
    CHECK_EQ(r.live, NB_LIVE_FRESH);
    CHECK_STR_EQ(nb_fmt_mag(&r, b, sizeof b), "0.42g");
    nb_read(true, &p, BAL_STALE_MS, &r);
    CHECK_EQ(r.live, NB_LIVE_STALE);
    CHECK_STR_EQ(nb_fmt_mag(&r, b, sizeof b), "--");
    CHECK_STR_EQ(nb_fmt_deg(&r, b, sizeof b), "--");
    CHECK_STR_EQ(nb_fmt_rpm(&r, b, sizeof b), "--");
    CHECK_EQ(r.word, NB_Q_NONE);
    CHECK_STR_EQ(nb_quality_text((nb_quality_t)r.word), "--");
    nb_read(true, NULL, 0u, &r);
    CHECK_EQ(r.live, NB_LIVE_STALE);
    nb_read(false, &p, 0u, &r);
    CHECK_EQ(r.live, NB_LIVE_SIMULATED);
}

/* The quality word: each flag over the ones after it, and the 90% and 70%
 * edges. */
TEST_CASE(flags_choose_the_quality_word_in_order)
{
    const uint16_t ok = RCNODE_BF_VALID | RCNODE_BF_DC_OK | RCNODE_BF_QUALITY;
    const uint16_t all_bad = RCNODE_BF_CLIPPED | RCNODE_BF_RPM_RANGE
                             | RCNODE_BF_INDEX_LOST | RCNODE_BF_NO_INDEX;
    CHECK_EQ(nb_quality(all_bad, 99u), NB_Q_SENSOR);
    CHECK_EQ(nb_quality(RCNODE_BF_DC_OK | all_bad, 99u), NB_Q_CLIPPED);
    CHECK_EQ(nb_quality(RCNODE_BF_DC_OK | RCNODE_BF_RPM_RANGE
                            | RCNODE_BF_INDEX_LOST | RCNODE_BF_NO_INDEX,
                        99u),
             NB_Q_RPM_RANGE);
    CHECK_EQ(nb_quality(RCNODE_BF_DC_OK | RCNODE_BF_INDEX_LOST, 99u),
             NB_Q_INDEX_LOST);
    CHECK_EQ(nb_quality(RCNODE_BF_DC_OK | RCNODE_BF_NO_INDEX, 99u),
             NB_Q_NO_INDEX);
    CHECK_EQ(nb_quality(RCNODE_BF_DC_OK | RCNODE_BF_QUALITY, 99u),
             NB_Q_NO_REV);
    CHECK_EQ(nb_quality(RCNODE_BF_DC_OK | RCNODE_BF_VALID, 99u),
             NB_Q_SETTLING);
    CHECK_EQ(nb_quality(ok | RCNODE_BF_NO_INDEX, 99u), NB_Q_NO_PHASE);
    CHECK_EQ(nb_quality(ok, 100u), NB_Q_GOOD);
    CHECK_EQ(nb_quality(ok, 90u), NB_Q_GOOD);
    CHECK_EQ(nb_quality(ok, 89u), NB_Q_FAIR);
    CHECK_EQ(nb_quality(ok, 70u), NB_Q_FAIR);
    CHECK_EQ(nb_quality(ok, 69u), NB_Q_POOR);
    CHECK_EQ(nb_quality(ok, 0u), NB_Q_POOR);
    /* Every word has a text of its own in both languages. */
    for (int l = 0; l < (int)UI_LANG_COUNT; ++l) {
        ui_text_set_language((ui_lang_t)l);
        for (int q = NB_Q_SENSOR; q < NB_Q_COUNT; ++q) {
            const char *t = nb_quality_text((nb_quality_t)q);
            CHECK(strcmp(t, "--") != 0);
            for (int k = NB_Q_SENSOR; k < q; ++k) {
                CHECK(strcmp(t, nb_quality_text((nb_quality_t)k)) != 0);
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(nb_quality_text(NB_Q_COUNT), "--");
}

/** @brief The node's seq and clock across the cases' reports. */
static uint8_t  s_seq;
static uint32_t s_t;

/** @brief Feed @p n new reports @p p, each 250ms on, fresh. */
static void feed(nb_session_t *s, rcnode_bal_report_t *p, unsigned n)
{
    for (unsigned k = 0; k < n; ++k) {
        p->hdr.seq = ++s_seq;
        s_t += 250000u;
        p->hdr.t_node = s_t;
        nb_feed(s, true, p, 20u);
    }
}

TEST_CASE(take_is_refused_without_data_while_busy_and_out_of_range)
{
    nb_session_t s;
    nb_init(&s);
    CHECK_EQ(s.method, NB_TWO_RUNS);
    CHECK_EQ(nb_slots(NB_TWO_RUNS), 2u);
    CHECK_EQ(nb_slots(NB_FOUR_RUNS), 4u);
    CHECK_EQ(nb_slots(7u), 0u);
    CHECK_EQ(nb_take(&s, 0u, false), NB_TAKE_NO_DATA);
    CHECK_EQ(nb_take(&s, 2u, true), NB_TAKE_SLOT);
    CHECK_EQ(nb_take(&s, 0u, true), NB_TAKE_STARTED);
    CHECK_EQ(nb_take(&s, 1u, true), NB_TAKE_BUSY);
    CHECK_EQ(s.cap[0].state, NB_CAP_TAKING);
    /* A method switch while one runs is taken (it clears), CLEAR too. */
    nb_set_method(&s, NB_FOUR_RUNS);
    CHECK_EQ(s.taking, -1);
    CHECK_EQ(s.cap[0].state, NB_CAP_EMPTY);
    CHECK_EQ(nb_take(&s, 3u, true), NB_TAKE_STARTED);
    nb_clear(&s);
    CHECK_EQ(s.taking, -1);
    CHECK_EQ(s.cap[3].state, NB_CAP_EMPTY);
    nb_set_method(&s, 9u);
    CHECK_EQ(s.method, NB_FOUR_RUNS);
    CHECK_EQ(nb_take(NULL, 0u, true), NB_TAKE_SLOT);
}

/* 8 new reports make a capture; one seen again counts once; the mean is
 * the vector mean and the magnitude mean. */
TEST_CASE(a_capture_averages_8_new_reports)
{
    nb_session_t s;
    nb_init(&s);
    rcnode_bal_report_t p = steady(400u, 900u, 8000u);   /* 0.4g at 90° */
    nb_feed(&s, true, &p, 20u);
    CHECK_EQ(nb_take(&s, 0u, true), NB_TAKE_STARTED);
    nb_feed(&s, true, &p, 20u);     /* the one seen before TAKE */
    CHECK_EQ(s.cap[0].n, 0u);
    feed(&s, &p, 4u);
    nb_feed(&s, true, &p, 40u);     /* the same report again */
    CHECK_EQ(s.cap[0].n, 4u);
    p.mag_1x = 600u;
    p.phase_1x = 0u;                 /* 0.6g at 0° */
    feed(&s, &p, 3u);
    CHECK_EQ(s.cap[0].state, NB_CAP_TAKING);
    feed(&s, &p, 1u);
    CHECK_EQ(s.cap[0].state, NB_CAP_DONE);
    CHECK_EQ(s.taking, -1);
    /* (4 x 0.4j + 4 x 0.6) / 8 = 0.3 + 0.2j. */
    CHECK_NEAR(s.cap[0].vec.mag, sqrt(0.3 * 0.3 + 0.2 * 0.2), 1e-5);
    CHECK_NEAR(s.cap[0].vec.deg, atan2(0.2, 0.3) * 180.0 / M_PI, 1e-3);
    CHECK_NEAR(s.cap[0].mag, 0.5, 1e-6);
    CHECK_NEAR(s.cap[0].rpm, 8000.0, 1e-3);
    /* More reports do not move a done capture. */
    feed(&s, &p, 5u);
    CHECK_EQ(s.cap[0].n, NB_CAP_REPORTS);
}

/* A gap of 999ms goes on; 1000ms fails STALE; so does a node gone. */
TEST_CASE(a_capture_fails_on_a_stale_gap_at_1000ms)
{
    nb_session_t s;
    nb_init(&s);
    rcnode_bal_report_t p = steady(400u, 900u, 8000u);
    CHECK_EQ(nb_take(&s, 0u, true), NB_TAKE_STARTED);
    feed(&s, &p, 2u);
    nb_feed(&s, true, &p, BAL_STALE_MS - 1u);
    CHECK_EQ(s.cap[0].state, NB_CAP_TAKING);
    nb_feed(&s, true, &p, BAL_STALE_MS);
    CHECK_EQ(s.cap[0].state, NB_CAP_FAILED);
    CHECK_EQ(s.cap[0].fail, NB_FAIL_STALE);
    CHECK_EQ(nb_take(&s, 0u, true), NB_TAKE_STARTED);
    nb_feed(&s, false, &p, 0u);
    CHECK_EQ(s.cap[0].fail, NB_FAIL_STALE);
    CHECK_EQ(nb_take(&s, 0u, true), NB_TAKE_STARTED);
    nb_feed(&s, true, NULL, 0u);
    CHECK_EQ(s.cap[0].fail, NB_FAIL_STALE);
}

/* No revolution fails NO_REV; no index fails NO_PHASE in two runs and is
 * taken in four; an rpm 5% off is taken and over 5% fails. */
TEST_CASE(a_capture_fails_on_no_revolution_no_index_and_rpm_drift)
{
    nb_session_t s;
    nb_init(&s);
    rcnode_bal_report_t p = steady(400u, 900u, 8000u);
    CHECK_EQ(nb_take(&s, 0u, true), NB_TAKE_STARTED);
    p.flags &= (uint16_t)~RCNODE_BF_VALID;
    feed(&s, &p, 1u);
    CHECK_EQ(s.cap[0].fail, NB_FAIL_NO_REV);

    p = steady(400u, 0u, 8000u);
    p.flags |= RCNODE_BF_NO_INDEX;
    CHECK_EQ(nb_take(&s, 0u, true), NB_TAKE_STARTED);
    feed(&s, &p, 1u);
    CHECK_EQ(s.cap[0].fail, NB_FAIL_NO_PHASE);
    nb_set_method(&s, NB_FOUR_RUNS);
    CHECK_EQ(nb_take(&s, 0u, true), NB_TAKE_STARTED);
    feed(&s, &p, NB_CAP_REPORTS);
    CHECK_EQ(s.cap[0].state, NB_CAP_DONE);

    p = steady(400u, 900u, 8000u);
    CHECK_EQ(nb_take(&s, 1u, true), NB_TAKE_STARTED);
    feed(&s, &p, 1u);
    p.rpm = 8400u;                   /* +5%: taken */
    feed(&s, &p, 1u);
    p.rpm = 7600u;                   /* -5%: taken */
    feed(&s, &p, 1u);
    CHECK_EQ(s.cap[1].state, NB_CAP_TAKING);
    p.rpm = 8401u;                   /* over 5% */
    feed(&s, &p, 1u);
    CHECK_EQ(s.cap[1].state, NB_CAP_FAILED);
    CHECK_EQ(s.cap[1].fail, NB_FAIL_RPM);
    CHECK_EQ(nb_take(&s, 1u, true), NB_TAKE_STARTED);
    p.rpm = 8000u;
    feed(&s, &p, 1u);
    p.rpm = 7599u;
    feed(&s, &p, 1u);
    CHECK_EQ(s.cap[1].fail, NB_FAIL_RPM);
}

/* One capture of slot @p slot at a steady @p rpm. */
static void take_at(nb_session_t *s, uint8_t slot, uint16_t rpm)
{
    rcnode_bal_report_t p = steady((uint16_t)(300u + 40u * slot),
                                   (uint16_t)(900u * slot), rpm);
    CHECK_EQ(nb_take(s, slot, true), NB_TAKE_STARTED);
    feed(s, &p, NB_CAP_REPORTS);
}

/* A trial's mean rpm 4.9% from the baseline's is taken, one 5.1% away
 * fails as NB_FAIL_RPM_BASE and leaves nothing to solve, both above and
 * below, in both methods; a baseline taken again at another rpm fails the
 * trials taken before it. */
TEST_CASE(a_trial_at_another_rpm_than_the_baseline_fails)
{
    nb_session_t s;
    vibe_solution_t sol;
    nb_init(&s);
    take_at(&s, 0u, 8000u);
    take_at(&s, 1u, 8392u);          /* +4.9%: taken */
    CHECK_EQ(s.cap[1].state, NB_CAP_DONE);
    CHECK(nb_ready(&s));
    CHECK(nb_solve(&s, 0.5f, &sol));
    take_at(&s, 1u, 8408u);          /* +5.1% */
    CHECK_EQ(s.cap[1].state, NB_CAP_FAILED);
    CHECK_EQ(s.cap[1].fail, NB_FAIL_RPM_BASE);
    CHECK(!nb_ready(&s));
    CHECK(!nb_solve(&s, 0.5f, &sol));
    take_at(&s, 1u, 7608u);          /* -4.9%: taken */
    CHECK(nb_ready(&s));
    take_at(&s, 1u, 7592u);          /* -5.1% */
    CHECK_EQ(s.cap[1].fail, NB_FAIL_RPM_BASE);
    /* A trial taken before the baseline is held to it once it is done. */
    nb_clear(&s);
    take_at(&s, 1u, 8000u);
    CHECK_EQ(s.cap[1].state, NB_CAP_DONE);
    take_at(&s, 0u, 8500u);          /* the trial 5.9% below it */
    CHECK_EQ(s.cap[1].state, NB_CAP_FAILED);
    CHECK_EQ(s.cap[1].fail, NB_FAIL_RPM_BASE);

    nb_set_method(&s, NB_FOUR_RUNS);
    take_at(&s, 0u, 8000u);
    take_at(&s, 1u, 8000u);
    take_at(&s, 2u, 8392u);          /* +4.9% */
    take_at(&s, 3u, 7608u);          /* -4.9% */
    CHECK(nb_ready(&s));
    CHECK(nb_solve(&s, 0.5f, &sol));
    take_at(&s, 3u, 7592u);          /* -5.1% */
    CHECK_EQ(s.cap[3].fail, NB_FAIL_RPM_BASE);
    CHECK(!nb_ready(&s));
    take_at(&s, 3u, 8000u);
    take_at(&s, 2u, 8408u);          /* +5.1% */
    CHECK_EQ(s.cap[2].fail, NB_FAIL_RPM_BASE);
    CHECK_EQ(s.cap[3].state, NB_CAP_DONE);
    CHECK(!nb_ready(&s));
    /* The baseline taken again at 8500rpm: 8000rpm is 5.9% off. */
    take_at(&s, 0u, 8500u);
    take_at(&s, 2u, 8500u);
    CHECK_EQ(s.cap[1].fail, NB_FAIL_RPM_BASE);
    CHECK_EQ(s.cap[2].state, NB_CAP_DONE);
    CHECK_EQ(s.cap[3].fail, NB_FAIL_RPM_BASE);
}

/* The CSV row: a row never comes from runs at different rpm. */
TEST_CASE(no_row_from_runs_at_different_rpm)
{
    node_balance_reset();
    settings_reset_all();
    nb_session_t *s = node_balance_session();
    take_at(s, 0u, 8000u);
    take_at(s, 1u, 8408u);
    char row[NB_CSV_MAX];
    CHECK(!node_balance_result(NULL, NULL));
    CHECK_EQ(node_balance_take_csv(2, row, sizeof row), 0u);
    take_at(s, 1u, 8392u);
    CHECK(node_balance_result(NULL, NULL));
    CHECK(node_balance_take_csv(2, row, sizeof row) > 0u);
    node_balance_reset();
}

/** A complex number of polar parts in degrees. */
typedef struct {
    double re, im;
} cx_t;

static cx_t cx_polar(double mag, double deg)
{
    const double r = deg * M_PI / 180.0;
    return (cx_t){ mag * cos(r), mag * sin(r) };
}

static cx_t cx_mul(cx_t a, cx_t b)
{
    return (cx_t){ a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re };
}

static cx_t cx_add(cx_t a, cx_t b)
{
    return (cx_t){ a.re + b.re, a.im + b.im };
}

/** @brief The report of a 1x vector @p v in g, at 8000rpm. */
static rcnode_bal_report_t report_of(cx_t v)
{
    double deg = atan2(v.im, v.re) * 180.0 / M_PI;
    if (deg < 0.0) {
        deg += 360.0;
    }
    return steady((uint16_t)lround(hypot(v.re, v.im) * 1000.0),
                  (uint16_t)(lround(deg * 10.0) % 3600), 8000u);
}

/**
 * @brief One run through the controller: the node sends @p p every 250ms,
 *        the panel takes the controller's report into the session each
 *        frame (16ms), TAKE at the start; the run lasts 3s.
 */
static void run_through(brig_t *r, nb_session_t *s, uint8_t slot,
                        const rcnode_bal_report_t *p)
{
    brig_report(r, p);
    rcnode_bal_report_t got;
    uint32_t age = 0u;
    CHECK(bal_ctl_report(&r->c, r->now, &got, &age));
    nb_feed(s, true, &got, age);
    CHECK_EQ(nb_take(s, slot, true), NB_TAKE_STARTED);
    for (uint32_t t = 0; t < 3000u; t += 16u) {
        brig_run(r, 16u);
        if ((t / 250u) != ((t + 16u) / 250u)) {
            brig_report(r, p);
        }
        const bool have = bal_ctl_report(&r->c, r->now, &got, &age);
        nb_feed(s, bal_ctl_link(&r->c, r->now) >= BAL_LINK_WAITING,
                have ? &got : NULL, age);
    }
    CHECK_EQ(s->cap[slot].state, NB_CAP_DONE);
}

/* Two runs end to end: a rotor of 1.2g per g lagging 40°, its imbalance
 * 0.35g at 85° (the correction 0.35g at 265°), the trial 0.5g at blade 1;
 * and a grid of imbalances. */
TEST_CASE(two_runs_end_to_end_find_the_known_imbalance)
{
    static const struct {
        double u, u_deg, c, lag;
    } k[] = {
        { 0.35, 85.0, 1.2, 40.0 },
        { 1.00, 10.0, 0.4, 0.0 },
        { 0.20, 200.0, 2.0, 120.0 },
        { 0.60, 300.0, 0.8, 300.0 },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
        brig_t r;
        brig_up(&r);
        nb_session_t s;
        nb_init(&s);
        const cx_t c = cx_polar(k[i].c, k[i].lag);
        const cx_t v0 = cx_mul(c, cx_polar(k[i].u, k[i].u_deg));
        const cx_t v1 = cx_add(v0, cx_mul(c, cx_polar(0.5, 0.0)));
        const rcnode_bal_report_t p0 = report_of(v0);
        const rcnode_bal_report_t p1 = report_of(v1);
        run_through(&r, &s, 0u, &p0);
        run_through(&r, &s, 1u, &p1);
        CHECK(nb_ready(&s));
        vibe_solution_t sol;
        CHECK(nb_solve(&s, 0.5f, &sol));
        CHECK_EQ(sol.status, VIBE_SOLVE_OK);
        CHECK_NEAR(sol.mass, k[i].u, 0.01);
        double want = fmod(k[i].u_deg + 180.0, 360.0);
        double d = fabs(sol.deg - want);
        d = d > 180.0 ? 360.0 - d : d;
        if (d > 1.0) {
            T_FAIL("case %zu: %.2f°, want %.2f°", i, (double)sol.deg, want);
        }
    }
}

/* Four runs end to end on magnitudes alone: the trial at 0°, 120°, 240°;
 * the node in no-index mode (its angle 0) is taken as it is. */
TEST_CASE(four_runs_end_to_end_find_the_known_imbalance)
{
    brig_t r;
    brig_up(&r);
    nb_session_t s;
    nb_init(&s);
    nb_set_method(&s, NB_FOUR_RUNS);
    const cx_t c = cx_polar(1.0, 25.0);
    const cx_t u = cx_polar(0.4, 140.0);
    for (uint8_t k = 0; k < 4u; ++k) {
        cx_t v = cx_mul(c, u);
        if (k > 0u) {
            v = cx_add(v, cx_mul(c, cx_polar(0.5, 120.0 * (k - 1u))));
        }
        rcnode_bal_report_t p = report_of(v);
        p.flags |= RCNODE_BF_NO_INDEX;
        p.phase_1x = 0u;
        run_through(&r, &s, k, &p);
    }
    vibe_solution_t sol;
    CHECK(nb_solve(&s, 0.5f, &sol));
    CHECK_EQ(sol.status, VIBE_SOLVE_OK);
    CHECK_NEAR(sol.mass, 0.4, 0.01);
    CHECK_NEAR(sol.deg, 320.0, 1.5);
}

/* The CSV row has the header's 19 fields and names the method, the trial,
 * the runs and the correction; not before every run is done. */
TEST_CASE(the_csv_row_matches_its_header)
{
    nb_session_t s;
    nb_init(&s);
    vibe_solution_t sol;
    memset(&sol, 0, sizeof sol);
    char row[NB_CSV_MAX];
    CHECK_EQ(nb_csv_row(&s, 0.5f, &sol, 2, row, sizeof row), 0u);
    CHECK(!nb_solve(&s, 0.5f, &sol));
    rcnode_bal_report_t p = steady(420u, 2650u, 8000u);
    CHECK_EQ(nb_take(&s, 0u, true), NB_TAKE_STARTED);
    feed(&s, &p, NB_CAP_REPORTS);
    p = steady(300u, 1960u, 8000u);
    CHECK_EQ(nb_take(&s, 1u, true), NB_TAKE_STARTED);
    feed(&s, &p, NB_CAP_REPORTS);
    CHECK(nb_solve(&s, 0.5f, &sol));
    const size_t n = nb_csv_row(&s, 0.5f, &sol, 2, row, sizeof row);
    CHECK(n > 0u);
    CHECK_EQ(strlen(row), n);
    CHECK_EQ(row[n - 1u], '\n');
    int commas_h = 0, commas_r = 0;
    for (const char *c = nb_csv_header(); *c; ++c) {
        commas_h += *c == ',';
    }
    for (const char *c = row; *c; ++c) {
        commas_r += *c == ',';
    }
    CHECK_EQ(commas_h, 18);
    CHECK_EQ(commas_r, commas_h);
    CHECK(strncmp(row, "two,0.50,g,0.420,265.0,8000,0.300,196.0,8000,",
                  45) == 0);
    CHECK(strstr(row, ",OK,") != NULL);
    /* A buffer too small writes nothing. */
    CHECK_EQ(nb_csv_row(&s, 0.5f, &sol, 2, row, 20u), 0u);
    CHECK_STR_EQ(row, "");
    /* Four runs: no angles, four magnitudes. */
    nb_set_method(&s, NB_FOUR_RUNS);
    for (uint8_t k = 0; k < 4u; ++k) {
        p = steady((uint16_t)(300u + 50u * k), 0u, 8000u);
        CHECK_EQ(nb_take(&s, k, true), NB_TAKE_STARTED);
        feed(&s, &p, NB_CAP_REPORTS);
    }
    CHECK(nb_solve(&s, 0.5f, &sol));
    CHECK(nb_csv_row(&s, 0.5f, &sol, 3, row, sizeof row) > 0u);
    CHECK(strncmp(row, "four,0.50,g,0.300,,8000,0.350,,8000,0.400,8000,0.450,"
                       "8000,", 53) == 0);
}

/* The instance: the reading moves its revision, one CSV row per solution,
 * a run taken again drops the solution until it is done again. */
TEST_CASE(the_instance_hands_out_one_row_per_solution)
{
    node_balance_reset();
    settings_reset_all();
    const uint32_t r0 = node_balance_rev();
    CHECK_EQ(node_balance_live()->live, NB_LIVE_SIMULATED);
    rcnode_bal_report_t p = steady(420u, 2650u, 8000u);
    node_balance_feed(true, &p, 10u);
    CHECK(node_balance_rev() != r0);
    CHECK_EQ(node_balance_live()->live, NB_LIVE_FRESH);
    const uint32_t r1 = node_balance_rev();
    node_balance_feed(true, &p, 20u);
    CHECK_EQ(node_balance_rev(), r1);       /* nothing shown moved */
    char row[NB_CSV_MAX];
    CHECK_EQ(node_balance_take_csv(2, row, sizeof row), 0u);
    CHECK(!node_balance_result(NULL, NULL));
    nb_session_t *s = node_balance_session();
    for (uint8_t run = 0; run < 2u; ++run) {
        p = steady(run == 0u ? 420u : 300u, run == 0u ? 2650u : 1960u, 8000u);
        CHECK_EQ(nb_take(s, run, true), NB_TAKE_STARTED);
        for (unsigned k = 0; k < NB_CAP_REPORTS; ++k) {
            ++p.hdr.seq;
            p.hdr.t_node += 250000u;
            node_balance_feed(true, &p, 10u);
        }
    }
    vibe_solution_t sol;
    float trial = 0.0f;
    CHECK(node_balance_result(&sol, &trial));
    CHECK_NEAR(trial, 0.5, 1e-6);
    CHECK(node_balance_take_csv(2, row, sizeof row) > 0u);
    CHECK_EQ(node_balance_take_csv(2, row, sizeof row), 0u);
    /* The trial run again: no solution while it runs, a new row after. */
    CHECK_EQ(nb_take(s, 1u, true), NB_TAKE_STARTED);
    node_balance_feed(true, &p, 10u);
    CHECK(!node_balance_result(NULL, NULL));
    for (unsigned k = 0; k < NB_CAP_REPORTS; ++k) {
        ++p.hdr.seq;
        p.hdr.t_node += 250000u;
        node_balance_feed(true, &p, 10u);
    }
    CHECK(node_balance_take_csv(2, row, sizeof row) > 0u);
    node_balance_reset();
}

/* Both runs of the two-run method through the instance. */
static void take_both(void)
{
    nb_session_t *s = node_balance_session();
    for (uint8_t run = 0; run < 2u; ++run) {
        rcnode_bal_report_t p = steady(run == 0u ? 420u : 300u,
                                       run == 0u ? 2650u : 1960u, 8000u);
        p.hdr.seq = (uint8_t)(run * 100u);
        p.hdr.t_node = run * 10000000u;
        node_balance_feed(true, &p, 10u);
        CHECK_EQ(nb_take(s, run, true), NB_TAKE_STARTED);
        for (unsigned k = 0; k < NB_CAP_REPORTS; ++k) {
            ++p.hdr.seq;
            p.hdr.t_node += 250000u;
            node_balance_feed(true, &p, 10u);
        }
    }
}

/* A trial mass changed after the runs: the result is solved again with
 * it, ADD scales with it, and a new CSV row names it.  The row's state
 * follows the card's answers for the last row only. */
TEST_CASE(a_new_trial_mass_solves_again_with_a_new_row)
{
    node_balance_reset();
    settings_reset_all();
    settings_set(SET_BAL_TRIAL, 0.5f);
    take_both();
    vibe_solution_t a, b;
    float ta = 0.0f, tb = 0.0f;
    CHECK(node_balance_result(&a, &ta));
    CHECK_NEAR(ta, 0.5, 1e-6);
    CHECK_EQ(node_balance_csv_state(), NB_CSV_NONE);
    char row[NB_CSV_MAX];
    CHECK(node_balance_take_csv(2, row, sizeof row) > 0u);
    CHECK(strncmp(row, "two,0.50,", 9) == 0);
    const uint32_t seq1 = node_balance_csv_seq();
    CHECK_EQ(node_balance_csv_state(), NB_CSV_WAITING);
    uint32_t rr = node_balance_result_rev();
    node_balance_csv_done(seq1, true);
    CHECK_EQ(node_balance_csv_state(), NB_CSV_WRITTEN);
    CHECK(node_balance_result_rev() != rr);
    /* A second answer for the same row changes nothing. */
    node_balance_csv_done(seq1, false);
    CHECK_EQ(node_balance_csv_state(), NB_CSV_WRITTEN);
    /* 1.00g was taped on, not 0.50g. */
    settings_set(SET_BAL_TRIAL, 1.0f);
    rr = node_balance_result_rev();
    CHECK(node_balance_result(&b, &tb));
    CHECK_NEAR(tb, 1.0, 1e-6);
    CHECK_NEAR(b.mass, 2.0f * a.mass, 1e-4);
    CHECK_NEAR(b.deg, a.deg, 1e-3);
    CHECK(node_balance_result_rev() != rr);
    CHECK_EQ(node_balance_csv_state(), NB_CSV_NONE);
    CHECK(node_balance_take_csv(2, row, sizeof row) > 0u);
    CHECK(strncmp(row, "two,1.00,", 9) == 0);
    const uint32_t seq2 = node_balance_csv_seq();
    CHECK_EQ(seq2, seq1 + 1u);
    node_balance_csv_done(seq1, true);      /* the old row's: ignored */
    CHECK_EQ(node_balance_csv_state(), NB_CSV_WAITING);
    node_balance_csv_done(seq2, false);
    CHECK_EQ(node_balance_csv_state(), NB_CSV_FAILED);
    /* The same mass again solves nothing new and takes no row. */
    node_balance_feed(true, NULL, 0u);
    CHECK_EQ(node_balance_take_csv(2, row, sizeof row), 0u);
    CHECK_EQ(node_balance_csv_state(), NB_CSV_FAILED);
    node_balance_reset();
}

/* A node on and bound whose link dropped shows "--" and keeps the result;
 * only OFF or unbound shows the model. */
TEST_CASE(a_dropped_link_is_stale_not_the_model)
{
    node_balance_reset();
    settings_reset_all();
    take_both();
    CHECK(node_balance_result(NULL, NULL));
    node_balance_feed(true, NULL, 0u);
    CHECK_EQ(node_balance_live()->live, NB_LIVE_STALE);
    char b[16];
    CHECK_STR_EQ(nb_fmt_mag(node_balance_live(), b, sizeof b), "--");
    CHECK(node_balance_result(NULL, NULL));
    node_balance_feed(false, NULL, 0u);
    CHECK_EQ(node_balance_live()->live, NB_LIVE_SIMULATED);
    node_balance_reset();
}

int main(void)
{
    RUN(a_report_reads_in_g_and_degrees);
    RUN(the_stale_rule_at_its_edge);
    RUN(flags_choose_the_quality_word_in_order);
    RUN(take_is_refused_without_data_while_busy_and_out_of_range);
    RUN(a_capture_averages_8_new_reports);
    RUN(a_capture_fails_on_a_stale_gap_at_1000ms);
    RUN(a_capture_fails_on_no_revolution_no_index_and_rpm_drift);
    RUN(a_trial_at_another_rpm_than_the_baseline_fails);
    RUN(no_row_from_runs_at_different_rpm);
    RUN(two_runs_end_to_end_find_the_known_imbalance);
    RUN(four_runs_end_to_end_find_the_known_imbalance);
    RUN(the_csv_row_matches_its_header);
    RUN(the_instance_hands_out_one_row_per_solution);
    RUN(a_new_trial_mass_solves_again_with_a_new_row);
    RUN(a_dropped_link_is_stale_not_the_model);
    return test_summary("node_balance");
}
