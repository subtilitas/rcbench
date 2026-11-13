/*
 * The balance corrections (shared/vibe/vibe_solve.c), against known
 * imbalances and through the demodulator.
 *
 * Under test: the two-run method on the screen's model values (0.42g,
 * 0.35g at 265deg) and over a grid of imbalances and trial positions;
 * the four-run method exact, with 1% magnitude errors, with runs 2 and 3
 * swapped (the mirror image), without effect and inconsistent; bad
 * inputs; and both methods end to end: a synthetic rotor whose 1x is
 * C U, measured by the demodulator with and without an index, solved,
 * the correction found.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "vibe_demod.h"
#include "vibe_solve.h"
#include "vibe_synth.h"

static vibe_demod_t  dm;
static syn_reports_t out;

/** A complex number of polar parts in degrees. */
typedef struct {
    double re, im;
} cx_t;

static cx_t cx_polar(double mag, double deg)
{
    const double r = deg * SYN_PI / 180.0;
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

static vibe_polar_t to_polar(cx_t v)
{
    return (vibe_polar_t){ (float)hypot(v.re, v.im),
                           (float)(atan2(v.im, v.re) * 180.0 / SYN_PI) };
}

static double cx_abs(cx_t v)
{
    return hypot(v.re, v.im);
}

TEST_CASE(two_runs_on_the_screens_model_values)
{
    /* The screen's model: 0.42g, add 0.35g at 265deg.  A rotor of
     * 1.2g per g that lags 40deg, its unbalance 0.35g at 85deg; a trial
     * of 0.5g at 0deg. */
    const cx_t c = cx_polar(1.2, 40.0);
    const cx_t v0 = cx_mul(c, cx_polar(0.35, 85.0));
    const cx_t v1 = cx_add(v0, cx_mul(c, cx_polar(0.5, 0.0)));
    CHECK_NEAR(cx_abs(v0), 0.42, 1e-9);
    vibe_solution_t s;
    CHECK_EQ(vibe_solve_two_run(to_polar(v0), to_polar(v1), 0.5f, 0.0f, &s),
             VIBE_SOLVE_OK);
    CHECK_NEAR(s.mass, 0.35, 1e-5);
    CHECK_NEAR(s.deg, 265.0, 1e-3);
    CHECK_NEAR(s.sens, 1.2, 1e-5);
    CHECK_NEAR(s.lag_deg, 40.0, 1e-3);
    CHECK_NEAR(s.baseline, 0.42, 1e-6);
    CHECK_NEAR(s.effect, 0.6 / 0.42, 1e-5);
    CHECK_NEAR(s.residual, 0.0, 0.0);
}

TEST_CASE(two_runs_over_a_grid_of_imbalances_and_trial_positions)
{
    double worst_m = 0.0;
    double worst_d = 0.0;
    for (int u = 0; u < 360; u += 15) {
        for (int tp = 0; tp < 360; tp += 45) {
            for (int lag = -170; lag <= 170; lag += 85) {
                const cx_t c = cx_polar(0.8, (double)lag);
                const cx_t v0 = cx_mul(c, cx_polar(0.2, (double)u));
                const cx_t v1 = cx_add(v0, cx_mul(c, cx_polar(0.3, (double)tp)));
                vibe_solution_t s;
                vibe_solve_two_run(to_polar(v0), to_polar(v1), 0.3f,
                                   (float)tp, &s);
                CHECK(s.status == VIBE_SOLVE_OK);
                const double em = fabs(s.mass - 0.2);
                const double ed = fabs(syn_ddeg(s.deg, (double)u + 180.0));
                worst_m = (em > worst_m) ? em : worst_m;
                worst_d = (ed > worst_d) ? ed : worst_d;
            }
        }
    }
    CHECK(worst_m <= 1e-5);
    CHECK(worst_d <= 1e-3);
}

TEST_CASE(two_runs_weak_no_effect_and_bad_input)
{
    const vibe_polar_t base = { 0.42f, 125.0f };
    vibe_solution_t s;
    /* The trial moves the vibration by 10%. */
    const cx_t v0 = cx_polar(0.42, 125.0);
    const cx_t v1 = cx_add(v0, cx_polar(0.042, 70.0));
    CHECK_EQ(vibe_solve_two_run(base, to_polar(v1), 0.1f, 30.0f, &s),
             VIBE_SOLVE_WEAK);
    CHECK(s.mass > 0.0f);
    CHECK_EQ(vibe_solve_two_run(base, base, 0.5f, 0.0f, &s), VIBE_SOLVE_NO_EFFECT);
    CHECK_EQ(s.mass, 0);
    CHECK_EQ(vibe_solve_two_run(base, base, 0.0f, 0.0f, &s), VIBE_SOLVE_BAD_INPUT);
    CHECK_EQ(vibe_solve_two_run(base, (vibe_polar_t){ -1.0f, 0.0f }, 0.5f, 0.0f, &s),
             VIBE_SOLVE_BAD_INPUT);
    CHECK_EQ(vibe_solve_two_run(base, (vibe_polar_t){ NAN, 0.0f }, 0.5f, 0.0f, NULL),
             VIBE_SOLVE_BAD_INPUT);
    /* A rotor in balance already: nothing to add. */
    CHECK_EQ(vibe_solve_two_run((vibe_polar_t){ 0.0f, 0.0f },
                                (vibe_polar_t){ 0.3f, 10.0f }, 0.5f, 0.0f, &s),
             VIBE_SOLVE_OK);
    CHECK_EQ(s.mass, 0);
}

/** The four magnitudes of a rotor with sensitivity t per trial, baseline
 *  M at d against the trial frame. */
static void four(double mm, double d, double t, float m[4])
{
    const cx_t v0 = cx_polar(mm, d);
    m[0] = (float)mm;
    for (int i = 0; i < 3; ++i) {
        m[i + 1] = (float)cx_abs(cx_add(v0, cx_polar(t, 120.0 * i)));
    }
}

TEST_CASE(four_runs_against_known_imbalances)
{
    const double ds[] = { 0.0, 37.0, 120.0, 200.0, 265.0, 359.0 };
    const double ts[] = { 0.1, 0.42, 1.5 };
    for (size_t i = 0; i < sizeof(ds) / sizeof(ds[0]); ++i) {
        for (size_t j = 0; j < sizeof(ts) / sizeof(ts[0]); ++j) {
            float m[4];
            four(0.42, ds[i], ts[j], m);
            vibe_solution_t s;
            const vibe_solve_status_t st =
                vibe_solve_four_run(m[0], m[1], m[2], m[3], 0.5f, &s);
            CHECK(st == VIBE_SOLVE_OK || st == VIBE_SOLVE_WEAK);
            CHECK_NEAR(s.mass, 0.5 * 0.42 / ts[j], 1e-3 * 0.5 * 0.42 / ts[j]);
            CHECK_NEAR(syn_ddeg(s.deg, ds[i] + 180.0), 0.0, 0.05);
            CHECK(s.residual < 1e-4f);
            CHECK_NEAR(s.sens, ts[j] / 0.5, 1e-3 * ts[j]);
        }
    }
    /* The screen's model: 0.42g, add 0.35g at 265deg, trial 0.5g. */
    float m[4];
    four(0.42, 85.0, 0.42 * 0.5 / 0.35, m);
    vibe_solution_t s;
    CHECK_EQ(vibe_solve_four_run(m[0], m[1], m[2], m[3], 0.5f, &s), VIBE_SOLVE_OK);
    CHECK_NEAR(s.mass, 0.35, 1e-4);
    CHECK_NEAR(s.deg, 265.0, 0.05);
}

TEST_CASE(four_runs_with_runs_2_and_3_swapped_mirror_the_answer)
{
    float m[4];
    four(0.42, 85.0, 0.6, m);
    vibe_solution_t a;
    vibe_solution_t b;
    vibe_solve_four_run(m[0], m[1], m[2], m[3], 0.5f, &a);
    vibe_solve_four_run(m[0], m[1], m[3], m[2], 0.5f, &b);
    CHECK_NEAR(a.deg, 265.0, 0.05);
    CHECK_NEAR(syn_ddeg(b.deg, 360.0 - a.deg), 0.0, 0.05);
    CHECK_NEAR(a.mass, b.mass, 1e-5);
}

TEST_CASE(four_runs_with_1pct_magnitude_errors)
{
    /* Each magnitude off by up to 1%, in every sign pattern. */
    double worst_m = 0.0;
    double worst_d = 0.0;
    for (int pat = 0; pat < 16; ++pat) {
        float m[4];
        four(0.42, 85.0, 0.6, m);
        for (int i = 0; i < 4; ++i) {
            m[i] *= (pat & (1 << i)) ? 1.01f : 0.99f;
        }
        vibe_solution_t s;
        CHECK_EQ(vibe_solve_four_run(m[0], m[1], m[2], m[3], 0.5f, &s),
                 VIBE_SOLVE_OK);
        const double em = fabs(s.mass - 0.35) / 0.35;
        const double ed = fabs(syn_ddeg(s.deg, 265.0));
        worst_m = (em > worst_m) ? em : worst_m;
        worst_d = (ed > worst_d) ? ed : worst_d;
    }
    printf("      four runs, magnitudes +-1%%: mass %.2f%%, angle %.2fdeg\n",
           worst_m * 100.0, worst_d);
    CHECK(worst_m <= 0.035);
    CHECK(worst_d <= 2.0);
}

TEST_CASE(four_runs_without_effect_inconsistent_or_bad)
{
    vibe_solution_t s;
    CHECK_EQ(vibe_solve_four_run(0.42f, 0.42f, 0.42f, 0.42f, 0.5f, &s),
             VIBE_SOLVE_NO_EFFECT);
    /* A baseline of 0.1g and three runs of 2g: no trial of one size
     * adds to 0.1g to make 2g at all three places. */
    CHECK_EQ(vibe_solve_four_run(0.1f, 2.0f, 0.3f, 2.0f, 0.5f, &s),
             VIBE_SOLVE_INCONSISTENT);
    CHECK_EQ(s.mass, 0);
    CHECK(s.residual > 0.1f);
    CHECK_EQ(vibe_solve_four_run(0.4f, 0.5f, 0.3f, 0.2f, -1.0f, &s),
             VIBE_SOLVE_BAD_INPUT);
    CHECK_EQ(vibe_solve_four_run(0.4f, -0.5f, 0.3f, 0.2f, 1.0f, &s),
             VIBE_SOLVE_BAD_INPUT);
    CHECK_EQ(vibe_solve_four_run(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, NULL),
             VIBE_SOLVE_BAD_INPUT);
    /* A weak trial: 10% of the baseline. */
    float m[4];
    four(0.42, 85.0, 0.042, m);
    CHECK_EQ(vibe_solve_four_run(m[0], m[1], m[2], m[3], 0.5f, &s),
             VIBE_SOLVE_WEAK);
    CHECK_NEAR(s.mass, 5.0, 0.01);
}

TEST_CASE(an_angle_wraps_into_0_to_360)
{
    CHECK_NEAR(vibe_wrap_deg(370.0f), 10.0, 1e-4);
    CHECK_NEAR(vibe_wrap_deg(-10.0f), 350.0, 1e-4);
    CHECK_NEAR(vibe_wrap_deg(720.0f), 0.0, 1e-4);
    CHECK(vibe_wrap_deg(-1e-9f) < 360.0f);
    CHECK_EQ(vibe_wrap_deg(NAN), 0);
}

/**
 * A rotor with unbalance @p u (g at the trial radius, deg from the index
 * in the order points pass the sensor) on a mount of @p c: its 1x is
 * C U, peaking C's lag plus U's angle after the index.  Measured over 2s.
 */
static vibe_polar_t measure(cx_t c, cx_t u, double rpm, int with_index)
{
    const vibe_polar_t v = to_polar(cx_mul(c, u));
    syn_t s = syn_plain(rpm);
    s.a1 = v.mag;
    s.p1 = v.deg;
    s.noise = 0.005;
    syn_run_t r;
    syn_gen(&s, 2.0, &r);
    syn_demod(&dm, &out, 0, 128);
    if (with_index) {
        syn_feed(&dm, &r, 250, 1);
    } else {
        vibe_demod_set_rpm(&dm, (uint32_t)llround(rpm * 10.0));
        vibe_demod_push(&dm, r.vib, r.n);
    }
    syn_free(&r);
    /* The panel's view: the mean of the reports after the first. */
    double re = 0.0;
    double im = 0.0;
    double mag = 0.0;
    for (size_t i = 1; i < out.n; ++i) {
        const cx_t z = cx_polar((double)out.r[i].mag_ug[0] * 1e-6,
                                (double)out.r[i].phase_cdeg[0] / 100.0);
        re += z.re;
        im += z.im;
        mag += (double)out.r[i].mag_ug[0] * 1e-6;
    }
    const double n = (double)(out.n - 1u);
    if (!with_index) {
        return (vibe_polar_t){ (float)(mag / n), 0.0f };
    }
    return to_polar((cx_t){ re / n, im / n });
}

TEST_CASE(two_runs_end_to_end_through_the_demodulator)
{
    /* 0.35g at 85deg on a mount of 1.2g per g lagging 40deg: 0.42g.
     * A trial of 0.5g at 30deg. */
    const cx_t c = cx_polar(1.2, 40.0);
    const cx_t u = cx_polar(0.35, 85.0);
    const vibe_polar_t v0 = measure(c, u, 10000.0, 1);
    const vibe_polar_t v1 = measure(c, cx_add(u, cx_polar(0.5, 30.0)), 10000.0, 1);
    vibe_solution_t s;
    CHECK_EQ(vibe_solve_two_run(v0, v1, 0.5f, 30.0f, &s), VIBE_SOLVE_OK);
    printf("      two runs end to end: %.4fg at %.2fdeg (0.35g at 265deg)\n",
           s.mass, s.deg);
    CHECK_NEAR(s.mass, 0.35, 0.35 * 0.01);
    CHECK_NEAR(syn_ddeg(s.deg, 265.0), 0.0, 1.0);
}

TEST_CASE(four_runs_end_to_end_without_an_index)
{
    const cx_t c = cx_polar(1.2, 40.0);
    const cx_t u = cx_polar(0.35, 85.0);
    const float m0 = measure(c, u, 6000.0, 0).mag;
    float m[3];
    for (int i = 0; i < 3; ++i) {
        m[i] = measure(c, cx_add(u, cx_polar(0.5, 120.0 * i)), 6000.0, 0).mag;
    }
    vibe_solution_t s;
    CHECK_EQ(vibe_solve_four_run(m0, m[0], m[1], m[2], 0.5f, &s), VIBE_SOLVE_OK);
    printf("      four runs end to end: %.4fg at %.2fdeg (0.35g at 265deg)\n",
           s.mass, s.deg);
    CHECK_NEAR(s.mass, 0.35, 0.35 * 0.01);
    CHECK_NEAR(syn_ddeg(s.deg, 265.0), 0.0, 1.0);
}

int main(void)
{
    RUN(two_runs_on_the_screens_model_values);
    RUN(two_runs_over_a_grid_of_imbalances_and_trial_positions);
    RUN(two_runs_weak_no_effect_and_bad_input);
    RUN(four_runs_against_known_imbalances);
    RUN(four_runs_with_runs_2_and_3_swapped_mirror_the_answer);
    RUN(four_runs_with_1pct_magnitude_errors);
    RUN(four_runs_without_effect_inconsistent_or_bad);
    RUN(an_angle_wraps_into_0_to_360);
    RUN(two_runs_end_to_end_through_the_demodulator);
    RUN(four_runs_end_to_end_without_an_index);
    return test_summary("vibe_solve");
}
