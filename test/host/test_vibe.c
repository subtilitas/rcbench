/*
 * The balance node's demodulator and index comparator
 * (shared/vibe/vibe_demod.c, vibe_edge.c), on synthetic signals
 * (vibe_synth.h).
 *
 * Under test: the 1x magnitude and phase of a 167Hz sine of 400mg at
 * 265deg through the index channel and the comparator; 300rpm, 3,000rpm
 * and 10,000rpm with exact edges; noise of 5mg rms; a 2x component; the
 * blade-pass and its limit at half the sample rate; an index jittered by
 * +-1 sample; a missing pulse, an extra pulse and a lost index; blocks of
 * any length and late edges giving the same reports, and a late edge
 * after its window counting in the next; an index back after a 900s
 * pause and one unbroken across the 2^24-sample wrap of the positions; a
 * mode switch and a blade-count change inside a window; the no-index mode
 * at the right rpm, at an rpm 1% off and out of range; clipping and the
 * DC range; the CORDIC, the square root and the comparator on their own.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "vibe_demod.h"
#include "vibe_edge.h"
#include "vibe_synth.h"

static vibe_demod_t  dm;
static syn_reports_t out;
static syn_reports_t out2;

/** The largest errors of a harmonic over reports @p from to the end. */
typedef struct {
    double mag_pct;   /* largest |magnitude error| / expected, % */
    double ph_deg;    /* largest |phase error|, degrees */
    double rpm_pct;   /* largest |rpm error| / expected, % */
} errs_t;

static errs_t errors(const syn_reports_t *o, size_t from, int h, double mag_g,
                     double ph_deg, double rpm)
{
    errs_t e = { 0.0, 0.0, 0.0 };
    if (o->n <= from) {
        /* No report to measure is no accuracy. */
        T_FAIL("errors: %zu report(s), none after %zu", o->n, from);
        e.mag_pct = e.ph_deg = e.rpm_pct = 1e9;
        return e;
    }
    for (size_t i = from; i < o->n; ++i) {
        const vibe_report_t *r = &o->r[i];
        const double m = fabs((double)r->mag_ug[h] * 1e-6 - mag_g) / mag_g * 100.0;
        const double p = fabs(syn_ddeg((double)r->phase_cdeg[h] / 100.0, ph_deg));
        const double q = fabs((double)r->rpm_x10 / 10.0 - rpm) / rpm * 100.0;
        e.mag_pct = (m > e.mag_pct) ? m : e.mag_pct;
        e.ph_deg = (p > e.ph_deg) ? p : e.ph_deg;
        e.rpm_pct = (q > e.rpm_pct) ? q : e.rpm_pct;
    }
    return e;
}

/** Every report from @p from on carries all of @p flags. */
static int all_have(const syn_reports_t *o, size_t from, uint16_t flags)
{
    for (size_t i = from; i < o->n; ++i) {
        if ((o->r[i].flags & flags) != flags) {
            return 0;
        }
    }
    return o->n > from;
}

TEST_CASE(a_167hz_sine_of_400mg_at_265deg_through_the_index_channel)
{
    syn_t s = syn_plain(167.0 * 60.0);
    s.a1 = 0.400;
    s.p1 = 265.0;
    syn_run_t r;
    syn_gen(&s, 3.0, &r);
    /* The index channel is sampled half a sample after the vibration. */
    syn_demod(&dm, &out, 0, 128);
    syn_feed(&dm, &r, 250, 1);
    CHECK_EQ(out.n, 12);
    const errs_t e = errors(&out, 1, VIBE_H_1X, 0.400, 265.0, 167.0 * 60.0);
    printf("      167Hz 400mg 265deg: magnitude %.3f%%, phase %.3fdeg, rpm %.3f%%\n",
           e.mag_pct, e.ph_deg, e.rpm_pct);
    CHECK(e.mag_pct <= 1.0);
    CHECK(e.ph_deg <= 1.0);
    CHECK(e.rpm_pct <= 0.1);
    CHECK(all_have(&out, 1, VIBE_F_VALID | VIBE_F_INDEX_SEEN
                   | VIBE_F_INDEX_STEADY | VIBE_F_DC_OK | VIBE_F_QUALITY));
    CHECK(out.r[5].revs >= 41 && out.r[5].revs <= 42);
    CHECK(out.r[5].quality_pct >= 99);
    CHECK_EQ(out.r[5].quality_revs, VIBE_QUAL_REVS);
    syn_free(&r);
}

TEST_CASE(without_the_channel_offset_the_phase_is_3deg_late)
{
    /* The same signal with the half-sample offset left out: the edges
     * are read half a sample early and the peak comes 0.5 / 59.9 of a
     * turn later. */
    syn_t s = syn_plain(167.0 * 60.0);
    s.a1 = 0.400;
    s.p1 = 265.0;
    syn_run_t r;
    syn_gen(&s, 2.0, &r);
    syn_demod(&dm, &out, 0, 0);
    syn_feed(&dm, &r, 250, 1);
    const double late = syn_ddeg((double)out.r[4].phase_cdeg[0] / 100.0, 265.0);
    CHECK_NEAR(late, 0.5 / (SYN_FS / 167.0) * 360.0, 0.5);
    syn_free(&r);
}

/** One speed with exact edges: 400mg at 265deg, warm-up of @p from reports. */
static errs_t at_rpm(double rpm, double seconds, size_t from)
{
    syn_t s = syn_plain(rpm);
    s.a1 = 0.400;
    s.p1 = 265.0;
    syn_run_t r;
    syn_gen(&s, seconds, &r);
    syn_demod(&dm, &out, 0, 0);
    syn_feed(&dm, &r, 100, 0);
    const errs_t e = errors(&out, from, VIBE_H_1X, 0.400, 265.0, rpm);
    CHECK(all_have(&out, from, VIBE_F_VALID | VIBE_F_INDEX_STEADY));
    syn_free(&r);
    return e;
}

TEST_CASE(speeds_from_300_to_60000rpm_with_exact_edges)
{
    const double rpm[] = { 300.0, 3000.0, 10000.0, 60000.0 };
    for (int i = 0; i < 4; ++i) {
        const errs_t e = at_rpm(rpm[i], (rpm[i] < 1000.0) ? 8.0 : 2.0,
                                (rpm[i] < 1000.0) ? 3u : 1u);
        printf("      %5.0frpm: magnitude %.4f%%, phase %.4fdeg, rpm %.4f%%\n",
               rpm[i], e.mag_pct, e.ph_deg, e.rpm_pct);
        CHECK(e.mag_pct <= 0.2);
        CHECK(e.ph_deg <= 0.2);
        CHECK(e.rpm_pct <= 0.01);
    }
}

TEST_CASE(below_the_slowest_revolution_the_index_is_lost)
{
    /* 120rpm: 5,000 samples a revolution, more than the buffer holds. */
    syn_t s = syn_plain(120.0);
    s.a1 = 0.400;
    syn_run_t r;
    syn_gen(&s, 3.0, &r);
    syn_demod(&dm, &out, 0, 0);
    syn_feed(&dm, &r, 100, 0);
    int lost = 0;
    for (size_t i = 0; i < out.n; ++i) {
        CHECK(!(out.r[i].flags & VIBE_F_VALID));
        lost |= (out.r[i].flags & VIBE_F_INDEX_LOST) != 0;
    }
    CHECK(lost);
    syn_free(&r);
}

TEST_CASE(noise_of_5mg_rms_on_400mg_and_alone)
{
    syn_t s = syn_plain(10000.0);
    s.a1 = 0.400;
    s.p1 = 265.0;
    s.noise = 0.005;
    syn_run_t r;
    syn_gen(&s, 2.0, &r);
    syn_demod(&dm, &out, 0, 0);
    syn_feed(&dm, &r, 100, 0);
    const errs_t e = errors(&out, 1, VIBE_H_1X, 0.400, 265.0, 10000.0);
    printf("      400mg + 5mg rms noise: magnitude %.3f%%, phase %.3fdeg\n",
           e.mag_pct, e.ph_deg);
    CHECK(e.mag_pct <= 0.5);
    CHECK(e.ph_deg <= 0.5);
    /* The broadband rms: the sine's 283mg and the noise's 5mg. */
    const double rms = sqrt(0.400 * 0.400 / 2.0 + 0.005 * 0.005);
    CHECK_NEAR((double)out.r[4].rms_ug * 1e-6, rms, rms * 0.01);
    /* The first window too, before a DC reference of its own. */
    CHECK_NEAR((double)out.r[0].rms_ug * 1e-6, rms, rms * 0.01);
    CHECK(out.r[4].quality_pct >= 99);
    syn_free(&r);

    /* Noise alone: the 1x falls to under 1mg, the rms reads 5mg and the
     * revolutions' vectors point anywhere. */
    s.a1 = 0.0;
    syn_gen(&s, 2.0, &r);
    syn_demod(&dm, &out, 0, 0);
    syn_feed(&dm, &r, 100, 0);
    for (size_t i = 1; i < out.n; ++i) {
        CHECK(out.r[i].mag_ug[0] < 1000u);
        CHECK_NEAR((double)out.r[i].rms_ug * 1e-6, 0.005, 0.0005);
        CHECK(out.r[i].quality_pct < 60);
    }
    printf("      5mg rms noise alone: 1x %uug, rms %uug, quality %u%%\n",
           out.r[4].mag_ug[0], out.r[4].rms_ug, out.r[4].quality_pct);
    syn_free(&r);

    /* 10mg in the noise, on the sensor's 1.65V: 4.4 counts on 2,048.
     * The DC taken out of each sum is what keeps it within 0.5mg. */
    const double rpm[] = { 7300.0, 10020.0 };
    for (int i = 0; i < 2; ++i) {
        s = syn_plain(rpm[i]);
        s.a1 = 0.010;
        s.p1 = 265.0;
        s.noise = 0.005;
        syn_gen(&s, 8.0, &r);
        syn_demod(&dm, &out, 0, 0);
        syn_feed(&dm, &r, 100, 0);
        const errs_t ei = errors(&out, 1, VIBE_H_1X, 0.010, 265.0, rpm[i]);
        printf("      10mg + 5mg rms noise at %.0frpm: magnitude %.3fmg, phase %.2fdeg\n",
               rpm[i], ei.mag_pct * 0.1, ei.ph_deg);
        CHECK(ei.mag_pct <= 5.0);
        CHECK(ei.ph_deg <= 3.0);
        syn_free(&r);
    }
}

TEST_CASE(a_2x_component_beside_the_1x)
{
    syn_t s = syn_plain(6000.0);
    s.a1 = 0.400;
    s.p1 = 265.0;
    s.a2 = 0.100;
    s.p2 = 30.0;
    syn_run_t r;
    syn_gen(&s, 2.0, &r);
    syn_demod(&dm, &out, 0, 0);
    syn_feed(&dm, &r, 100, 0);
    const errs_t e1 = errors(&out, 1, VIBE_H_1X, 0.400, 265.0, 6000.0);
    const errs_t e2 = errors(&out, 1, VIBE_H_2X, 0.100, 30.0, 6000.0);
    printf("      2x 100mg at 30deg: magnitude %.3f%%, phase %.3fdeg\n",
           e2.mag_pct, e2.ph_deg);
    CHECK(e1.mag_pct <= 0.2);
    CHECK(e1.ph_deg <= 0.2);
    CHECK(e2.mag_pct <= 1.0);
    CHECK(e2.ph_deg <= 1.0);
    /* No blade count: no blade-pass. */
    CHECK_EQ(out.r[3].mag_ug[VIBE_H_BLADE], 0);
    CHECK(!(out.r[3].flags & VIBE_F_BLADE_OK));
    syn_free(&r);
}

TEST_CASE(the_blade_pass_and_its_limit_at_half_the_sample_rate)
{
    syn_t s = syn_plain(10000.0);
    s.a1 = 0.400;
    s.p1 = 265.0;
    s.ab = 0.050;
    s.pb = 100.0;
    s.blades = 3;
    syn_run_t r;
    syn_gen(&s, 2.0, &r);
    syn_demod(&dm, &out, 3, 0);
    syn_feed(&dm, &r, 100, 0);
    const errs_t e = errors(&out, 1, VIBE_H_BLADE, 0.050, 100.0, 10000.0);
    printf("      blade-pass 3 x 167Hz 50mg: magnitude %.3f%%, phase %.3fdeg\n",
           e.mag_pct, e.ph_deg);
    CHECK(e.mag_pct <= 1.0);
    CHECK(all_have(&out, 1, VIBE_F_BLADE_OK));
    /* The 3x is no 2x and no 1x. */
    CHECK(out.r[3].mag_ug[VIBE_H_2X] < 500u);
    syn_free(&r);

    /* 6 blades at 60,000rpm is 6kHz, over the 5kHz a 10kHz rate holds. */
    s = syn_plain(60000.0);
    s.a1 = 0.400;
    syn_gen(&s, 1.0, &r);
    syn_demod(&dm, &out, 6, 0);
    syn_feed(&dm, &r, 100, 0);
    CHECK(out.r[2].flags & VIBE_F_VALID);
    CHECK(!(out.r[2].flags & VIBE_F_BLADE_OK));
    CHECK_EQ(out.r[2].mag_ug[VIBE_H_BLADE], 0);
    syn_free(&r);
}

TEST_CASE(an_index_jittered_by_one_sample)
{
    syn_t s = syn_plain(3000.0);
    s.a1 = 0.400;
    s.p1 = 265.0;
    s.jitter = 1;
    syn_run_t r;
    syn_gen(&s, 3.0, &r);
    syn_demod(&dm, &out, 0, 0);
    syn_feed(&dm, &r, 100, 0);
    const errs_t e = errors(&out, 1, VIBE_H_1X, 0.400, 265.0, 3000.0);
    printf("      3000rpm, +-1 sample jitter: magnitude %.3f%%, phase %.3fdeg, rpm %.3f%%\n",
           e.mag_pct, e.ph_deg, e.rpm_pct);
    CHECK(e.mag_pct <= 1.0);
    CHECK(e.ph_deg <= 1.0);
    CHECK(e.rpm_pct <= 0.2);
    CHECK(all_have(&out, 1, VIBE_F_INDEX_STEADY));
    for (size_t i = 1; i < out.n; ++i) {
        CHECK_EQ(out.r[i].rejected, 0);
    }
    syn_free(&r);

    /* At 10,000rpm a sample is 6deg: each window within 3deg, the mean of
     * eight within 1deg. */
    s = syn_plain(10000.0);
    s.a1 = 0.400;
    s.p1 = 265.0;
    s.jitter = 1;
    syn_gen(&s, 3.0, &r);
    syn_demod(&dm, &out, 0, 0);
    syn_feed(&dm, &r, 100, 0);
    const errs_t f = errors(&out, 1, VIBE_H_1X, 0.400, 265.0, 10000.0);
    double mean = 0.0;
    for (size_t i = 1; i < 9; ++i) {
        mean += syn_ddeg((double)out.r[i].phase_cdeg[0] / 100.0, 265.0) / 8.0;
    }
    printf("      10000rpm, +-1 sample jitter: magnitude %.3f%%, phase %.3fdeg, mean of 8 %.3fdeg\n",
           f.mag_pct, f.ph_deg, fabs(mean));
    CHECK(f.mag_pct <= 1.0);
    CHECK(f.ph_deg <= 3.0);
    CHECK(fabs(mean) <= 1.0);
    CHECK(all_have(&out, 1, VIBE_F_INDEX_STEADY));
    syn_free(&r);
}

TEST_CASE(a_missing_pulse_costs_one_revolution)
{
    syn_t s = syn_plain(3000.0);
    s.a1 = 0.400;
    s.p1 = 265.0;
    s.drop_from = 40;   /* 0.81s, in report 3 */
    s.drop_to = 40;
    syn_run_t r;
    syn_gen(&s, 2.0, &r);
    syn_demod(&dm, &out, 0, 0);
    syn_feed(&dm, &r, 100, 0);
    const vibe_report_t *w = &out.r[3];
    CHECK_EQ(w->rejected, 1);
    CHECK(!(w->flags & VIBE_F_INDEX_STEADY));
    CHECK(w->flags & VIBE_F_VALID);
    CHECK(out.r[4].flags & VIBE_F_INDEX_STEADY);
    /* The doubled revolution is left out: the rpm is not halved and the
     * 1x is the 1x. */
    const errs_t e = errors(&out, 1, VIBE_H_1X, 0.400, 265.0, 3000.0);
    CHECK(e.mag_pct <= 0.2);
    CHECK(e.ph_deg <= 0.2);
    CHECK(e.rpm_pct <= 0.01);
    CHECK_EQ(w->spread_pm, 0);
    syn_free(&r);
}

TEST_CASE(an_extra_pulse_is_two_short_revolutions_refused)
{
    syn_t s = syn_plain(3000.0);
    s.a1 = 0.400;
    s.p1 = 265.0;
    s.extra = 40;
    syn_run_t r;
    syn_gen(&s, 2.0, &r);
    syn_demod(&dm, &out, 0, 0);
    syn_feed(&dm, &r, 100, 0);
    CHECK_EQ(out.r[3].rejected, 2);
    const errs_t e = errors(&out, 1, VIBE_H_1X, 0.400, 265.0, 3000.0);
    CHECK(e.mag_pct <= 0.2);
    CHECK(e.ph_deg <= 0.2);
    CHECK(e.rpm_pct <= 0.01);
    syn_free(&r);
}

TEST_CASE(a_lost_index_says_so_and_comes_back)
{
    syn_t s = syn_plain(3000.0);
    s.a1 = 0.400;
    s.p1 = 265.0;
    s.drop_from = 25;   /* 0.51s to 1.49s: no edges */
    s.drop_to = 74;
    syn_run_t r;
    syn_gen(&s, 3.0, &r);
    syn_demod(&dm, &out, 0, 0);
    syn_feed(&dm, &r, 100, 0);
    int lost = 0;
    for (size_t i = 3; i < 6; ++i) {
        CHECK(!(out.r[i].flags & VIBE_F_VALID));
        lost |= (out.r[i].flags & VIBE_F_INDEX_LOST) != 0;
    }
    CHECK(lost);
    /* Two periods to lock again, then the readings as before. */
    const errs_t e = errors(&out, 7, VIBE_H_1X, 0.400, 265.0, 3000.0);
    CHECK(all_have(&out, 7, VIBE_F_VALID | VIBE_F_INDEX_STEADY));
    CHECK(e.mag_pct <= 0.2);
    CHECK(e.ph_deg <= 0.2);
    syn_free(&r);
}

TEST_CASE(blocks_of_any_length_give_the_same_reports)
{
    syn_t s = syn_plain(7300.0);
    s.a1 = 0.400;
    s.p1 = 265.0;
    s.a2 = 0.050;
    s.ab = 0.030;
    s.blades = 2;
    s.noise = 0.005;
    syn_run_t r;
    syn_gen(&s, 2.0, &r);
    syn_demod(&dm, &out, 2, 128);
    syn_feed(&dm, &r, 2500, 1);
    const size_t blocks[] = { 1, 7, 64, 333, 2499, 2501, 20000 };
    for (size_t b = 0; b < sizeof(blocks) / sizeof(blocks[0]); ++b) {
        syn_demod(&dm, &out2, 2, 128);
        syn_feed(&dm, &r, blocks[b], 1);
        CHECK_EQ(out2.n, out.n);
        CHECK(memcmp(out.r, out2.r, out.n * sizeof(out.r[0])) == 0);
    }
    syn_free(&r);
}

TEST_CASE(a_late_edge_splits_the_samples_already_pushed)
{
    /* Every edge handed in 20 samples after its own sample: the same
     * revolutions, the same readings. */
    syn_t s = syn_plain(3000.0);
    s.a1 = 0.400;
    s.p1 = 265.0;
    syn_run_t r;
    syn_gen(&s, 2.0, &r);
    syn_demod(&dm, &out, 0, 0);
    size_t ne = 0;
    unsigned revs = 0;
    for (size_t k0 = 0; k0 < r.n; k0 += 10) {
        vibe_demod_push(&dm, r.vib + k0, 10);
        while (ne < r.ne && (uint64_t)r.edges[ne] + 20u * 256u < ((uint64_t)k0 + 10u) << 8) {
            vibe_demod_edge(&dm, r.edges[ne++]);
        }
    }
    CHECK_EQ(out.n, 8);
    for (size_t i = 0; i < out.n; ++i) {
        revs += out.r[i].revs;
    }
    CHECK_EQ(revs, (unsigned)r.ne - 2u);
    const errs_t e = errors(&out, 1, VIBE_H_1X, 0.400, 265.0, 3000.0);
    CHECK(e.mag_pct <= 0.2);
    CHECK(e.ph_deg <= 0.2);
    syn_free(&r);
}

TEST_CASE(an_edge_out_of_order_or_past_a_full_queue_is_dropped)
{
    int16_t z[100];
    for (int i = 0; i < 100; ++i) {
        z[i] = 2048;
    }
    syn_demod(&dm, &out, 0, 0);
    vibe_demod_edge(&dm, 1000u * 256u);
    vibe_demod_edge(&dm, 900u * 256u);
    vibe_demod_push(&dm, z, 100);
    for (int i = 0; i < 25; ++i) {
        vibe_demod_push(&dm, z, 100);
    }
    CHECK_EQ(out.n, 1);
    CHECK(out.r[0].flags & VIBE_F_EDGE_DROPPED);
    CHECK(!(out.r[0].flags & VIBE_F_INDEX_STEADY));

    syn_demod(&dm, &out, 0, 0);
    for (uint32_t i = 1; i <= VIBE_EDGE_QUEUE + 1u; ++i) {
        vibe_demod_edge(&dm, i * 100u * 256u);
    }
    for (int i = 0; i < 25; ++i) {
        vibe_demod_push(&dm, z, 100);
    }
    CHECK_EQ(out.n, 1);
    CHECK(out.r[0].flags & VIBE_F_EDGE_DROPPED);
    /* The queued edges are one revolution of 100 samples fewer. */
    CHECK_EQ(out.r[0].revs + out.r[0].rejected, VIBE_EDGE_QUEUE - 1u);
}

/** The no-index mode at @p set rpm on a rotor at @p rpm. */
static void no_index(double rpm, double set, double seconds)
{
    syn_t s = syn_plain(rpm);
    s.a1 = 0.400;
    s.p1 = 265.0;
    s.a2 = 0.100;
    syn_run_t r;
    syn_gen(&s, seconds, &r);
    syn_demod(&dm, &out, 0, 0);
    vibe_demod_set_rpm(&dm, (uint32_t)llround(set * 10.0));
    vibe_demod_push(&dm, r.vib, r.n);
    CHECK_EQ(out.n, r.n / 2500u);
    syn_free(&r);
}

TEST_CASE(the_no_index_mode_reads_the_magnitude_at_the_rpm_it_is_given)
{
    no_index(10000.0, 10000.0, 2.0);
    const errs_t e = errors(&out, 1, VIBE_H_1X, 0.400, 0.0, 10000.0);
    const errs_t e2 = errors(&out, 1, VIBE_H_2X, 0.100, 0.0, 10000.0);
    printf("      no index, rpm exact: magnitude %.3f%%, 2x %.3f%%\n",
           e.mag_pct, e2.mag_pct);
    CHECK(e.mag_pct <= 0.5);
    CHECK(e2.mag_pct <= 1.0);
    CHECK(all_have(&out, 1, VIBE_F_VALID | VIBE_F_NO_INDEX));
    CHECK_EQ(out.r[3].phase_cdeg[0], 0);
    CHECK_EQ(out.r[3].rpm_x10, 100000);
    CHECK(!(out.r[3].flags & VIBE_F_INDEX_STEADY));
    /* Units of 5 turns: 300 samples, 8 to 9 in a window. */
    CHECK(out.r[3].revs >= 8 && out.r[3].revs <= 9);
    CHECK(out.r[3].quality_pct >= 99);

    /* 300rpm: units of one turn. */
    no_index(300.0, 300.0, 8.0);
    const errs_t slow = errors(&out, 2, VIBE_H_1X, 0.400, 0.0, 300.0);
    CHECK(slow.mag_pct <= 0.5);
    CHECK(all_have(&out, 2, VIBE_F_VALID));
}

TEST_CASE(an_rpm_1pct_off_loses_little_magnitude_and_shows_in_the_quality)
{
    no_index(10000.0, 10100.0, 2.0);
    const errs_t e = errors(&out, 1, VIBE_H_1X, 0.400, 0.0, 10100.0);
    printf("      no index, rpm 1%% off: magnitude %.3f%%, quality %u%%\n",
           e.mag_pct, out.r[4].quality_pct);
    CHECK(e.mag_pct <= 1.0);
    /* The units' vectors turn 18deg each: the 16 behind the figure spread
     * over 270deg. */
    CHECK(out.r[4].quality_pct < 50);
}

TEST_CASE(an_rpm_outside_the_revolution_limits_reads_nothing)
{
    no_index(100.0, 100.0, 1.0);
    CHECK_EQ(out.n, 4);
    for (size_t i = 0; i < out.n; ++i) {
        CHECK(out.r[i].flags & VIBE_F_RPM_RANGE);
        CHECK(!(out.r[i].flags & VIBE_F_VALID));
    }
    /* Back into range on the next unit. */
    vibe_demod_set_rpm(&dm, 30000u);
    int16_t z[2500];
    for (int i = 0; i < 2500; ++i) {
        z[i] = (int16_t)(2048 + (int)(100.0 * cos(2.0 * SYN_PI * 50.0 * i / SYN_FS)));
    }
    memset(&out, 0, sizeof(out));
    vibe_demod_push(&dm, z, 2500);
    vibe_demod_push(&dm, z, 2500);
    CHECK_EQ(out.n, 2);
    CHECK(!(out.r[1].flags & VIBE_F_RPM_RANGE));
    CHECK(out.r[1].flags & VIBE_F_VALID);
}

TEST_CASE(switching_modes_drops_the_revolution_and_the_lock)
{
    syn_t s = syn_plain(3000.0);
    s.a1 = 0.400;
    s.p1 = 265.0;
    syn_run_t r;
    syn_gen(&s, 1.0, &r);
    syn_demod(&dm, &out, 0, 0);
    syn_feed(&dm, &r, 100, 0);
    CHECK(dm.ref_ok);
    vibe_demod_set_rpm(&dm, 30000u);
    CHECK(!dm.ref_ok);
    CHECK_EQ(dm.ring_n, 0);
    vibe_demod_set_rpm(&dm, 0u);
    CHECK(!dm.open);
    CHECK_EQ(dm.nbuf, 0);
    syn_free(&r);
}

TEST_CASE(a_clipped_sample_and_a_dc_out_of_range_are_flagged)
{
    syn_t s = syn_plain(3000.0);
    s.a1 = 5.0;   /* 2,180 counts: past both ends of the ADC */
    syn_run_t r;
    syn_gen(&s, 1.0, &r);
    syn_demod(&dm, &out, 0, 0);
    syn_feed(&dm, &r, 100, 0);
    CHECK(out.r[1].flags & VIBE_F_CLIPPED);
    syn_free(&r);

    /* An unpowered sensor: 0V. */
    int16_t z[2500];
    memset(z, 0, sizeof(z));
    syn_demod(&dm, &out, 0, 0);
    vibe_demod_push(&dm, z, 2500);
    vibe_demod_push(&dm, z, 2500);
    CHECK_EQ(out.n, 2);
    CHECK(!(out.r[1].flags & VIBE_F_DC_OK));
    CHECK_EQ(out.r[1].dc_x16, 0);
    /* At mid-rail: in range, 1.65V. */
    for (int i = 0; i < 2500; ++i) {
        z[i] = 2048;
    }
    vibe_demod_push(&dm, z, 2500);
    CHECK_EQ(out.n, 3);
    CHECK(out.r[2].flags & VIBE_F_DC_OK);
    CHECK(!(out.r[2].flags & VIBE_F_CLIPPED));
    CHECK_EQ(out.r[2].dc_x16, 2048 * 16);
    CHECK_EQ(out.r[2].rms_ug, 0);
}

/* A rotor streamed sample by sample: 400mg 1x at 265deg and @p a3 g of 3x
 * at 100deg, @p spr samples a revolution, the edges exact; for runs too
 * long to hold in memory. */
static double s_turn;
static vibe_report_t s_last;
static size_t s_count;
static size_t s_bad;

static void stream_cb(const vibe_report_t *r, void *ctx)
{
    (void)ctx;
    s_last = *r;
    ++s_count;
    if (s_count > 2u && (!(r->flags & VIBE_F_INDEX_STEADY)
            || fabs((double)r->mag_ug[0] - 400000.0) > 2000.0
            || fabs(syn_ddeg((double)r->phase_cdeg[0] / 100.0, 265.0)) > 1.0)) {
        ++s_bad;
    }
}

static void stream(uint32_t n, double spr, int with_edges, double a3)
{
    int16_t x[100];
    uint32_t e[4];
    const double k = SYN_PI / 180.0;
    for (uint32_t done = 0; done < n; done += 100u) {
        const uint32_t m = (n - done < 100u) ? n - done : 100u;
        const uint32_t p0 = vibe_demod_pos_q8(&dm) >> 8;
        size_t ne = 0;
        for (uint32_t j = 0; j < m; ++j) {
            const double t0 = s_turn + (double)j / spr;
            const double t1 = s_turn + (double)(j + 1u) / spr;
            const double th = 2.0 * SYN_PI * t0;
            x[j] = syn_adc(SYN_DC + SYN_PER_G * (0.400 * cos(th - 265.0 * k)
                                                + a3 * cos(3.0 * th - 100.0 * k)));
            if (with_edges && floor(t1) != floor(t0) && ne < 4u) {
                const double after = (floor(t1) - t0) * spr;
                e[ne++] = (p0 + j) * 256u + (uint32_t)llround(after * 256.0);
            }
        }
        s_turn += (double)m / spr;
        vibe_demod_feed(&dm, x, m, e, ne);
    }
}

static void stream_start(uint8_t blades)
{
    vibe_cfg_t c;
    vibe_cfg_default(&c);
    c.blades = blades;
    vibe_demod_init(&dm, &c, stream_cb, NULL);
    s_turn = 0.0;
    s_count = 0u;
    s_bad = 0u;
    memset(&s_last, 0, sizeof(s_last));
}

TEST_CASE(the_index_back_after_a_900s_pause_is_taken)
{
    /* A stopped motor for 900s: more than 2^23 samples, after which the
     * last edge before the pause reads as later than every new one. */
    stream_start(0);
    stream(20000u, 200.0, 1, 0.0);
    CHECK(s_last.flags & VIBE_F_INDEX_STEADY);
    stream(9000000u, 200.0, 0, 0.0);
    CHECK(s_last.flags & VIBE_F_INDEX_LOST);
    stream(20000u, 200.0, 1, 0.0);
    CHECK_EQ(s_count, 3616);
    CHECK(s_last.flags & VIBE_F_VALID);
    CHECK(s_last.flags & VIBE_F_INDEX_STEADY);
    CHECK(!(s_last.flags & VIBE_F_EDGE_DROPPED));
    CHECK_NEAR((double)s_last.mag_ug[0], 400000.0, 2000.0);
}

TEST_CASE(an_unbroken_index_across_the_position_wrap)
{
    /* 2^24 samples and 100,000 more: the Q8 positions wrap once. */
    stream_start(0);
    stream((1u << 24) + 100000u, 199.37, 1, 0.0);
    CHECK_EQ(s_count, ((1u << 24) + 100000u) / 2500u);
    CHECK_EQ(s_bad, 0);
}

TEST_CASE(a_mode_switch_inside_a_window_reports_the_new_mode_only)
{
    /* Index, then no index from the middle of window 4, then the index
     * again from the middle of window 7: each switch window holds the
     * new mode's revolutions and rpm, marked. */
    stream_start(0);
    stream(10000u, 200.0, 1, 0.0);
    CHECK_EQ(s_count, 4);
    stream(1250u, 200.0, 1, 0.0);
    vibe_demod_set_rpm(&dm, 30000u);
    stream(1250u, 200.0, 0, 0.0);
    CHECK_EQ(s_count, 5);
    CHECK(s_last.flags & VIBE_F_RESTARTED);
    CHECK(s_last.flags & VIBE_F_NO_INDEX);
    /* Units of 2 turns, 400 samples: at most 3 in 1,250 samples. */
    CHECK(s_last.revs <= 3);
    CHECK_NEAR((double)s_last.mag_ug[0], 400000.0, 4000.0);
    stream(2500u + 1250u, 200.0, 0, 0.0);
    CHECK(!(s_last.flags & VIBE_F_RESTARTED));
    vibe_demod_set_rpm(&dm, 0u);
    stream(1250u, 200.0, 1, 0.0);
    CHECK_EQ(s_count, 7);
    CHECK(s_last.flags & VIBE_F_RESTARTED);
    CHECK(s_last.flags & VIBE_F_VALID);
    CHECK(!(s_last.flags & VIBE_F_NO_INDEX));
    CHECK_NEAR((double)s_last.rpm_x10, 30000.0, 30.0);
    CHECK_NEAR((double)s_last.mag_ug[0], 400000.0, 4000.0);
}

TEST_CASE(a_blade_count_change_inside_a_window_clears_the_blade_pass)
{
    /* 50mg of 3x; the count goes from 2 to 3 in the middle of window 4:
     * that window's blade-pass is the 3x of the revolutions after it. */
    stream_start(2);
    stream(11250u, 200.0, 1, 0.050);
    CHECK_EQ(s_count, 4);
    CHECK(s_last.mag_ug[VIBE_H_BLADE] < 2000u);
    vibe_demod_set_blades(&dm, 3);
    stream(1250u, 200.0, 1, 0.050);
    CHECK_EQ(s_count, 5);
    CHECK(s_last.flags & VIBE_F_RESTARTED);
    CHECK(s_last.flags & VIBE_F_BLADE_OK);
    CHECK_NEAR((double)s_last.mag_ug[VIBE_H_BLADE], 50000.0, 1000.0);
    /* The same count again changes nothing. */
    vibe_demod_set_blades(&dm, 3);
    stream(2500u, 200.0, 1, 0.050);
    CHECK(!(s_last.flags & VIBE_F_RESTARTED));
    CHECK_NEAR((double)s_last.mag_ug[VIBE_H_BLADE], 50000.0, 1000.0);
}

TEST_CASE(an_edge_handed_in_after_its_window_counts_in_the_next)
{
    /* Edges every 200 samples from 90; the one at 2,490 lies in window 0.
     * Fed with its samples it counts there; handed in 15 samples late,
     * after window 0 closed at 2,500, it counts in window 1. */
    int16_t z[10];
    for (int i = 0; i < 10; ++i) {
        z[i] = 2048;
    }
    unsigned fed[4] = { 0 };
    unsigned late[4] = { 0 };
    for (int pass = 0; pass < 2; ++pass) {
        syn_demod(&dm, &out, 0, 0);
        for (uint32_t s0 = 0; s0 < 10000u; s0 += 10u) {
            uint32_t e[1];
            size_t ne = 0;
            for (uint32_t k = 0; k < 50u; ++k) {
                const uint32_t es = 90u + 200u * k;
                if (pass == 0 && es >= s0 && es < s0 + 10u) {
                    e[ne++] = es * 256u;
                }
            }
            if (pass == 0) {
                vibe_demod_feed(&dm, z, 10, e, ne);
                continue;
            }
            vibe_demod_push(&dm, z, 10);
            for (uint32_t k = 0; k < 50u; ++k) {
                const uint32_t es = 90u + 200u * k;
                if (es + 15u >= s0 && es + 15u < s0 + 10u) {
                    vibe_demod_edge(&dm, es * 256u);
                }
            }
        }
        CHECK_EQ(out.n, 4);
        for (size_t i = 0; i < 4 && i < out.n; ++i) {
            const unsigned v = (unsigned)out.r[i].revs + out.r[i].rejected;
            if (pass == 0) {
                fed[i] = v;
            } else {
                late[i] = v;
            }
        }
    }
    CHECK_EQ(fed[0], 12);
    CHECK_EQ(late[0], 11);
    CHECK_EQ(late[1], fed[1] + 1u);
    CHECK_EQ(fed[0] + fed[1] + fed[2] + fed[3], late[0] + late[1] + late[2] + late[3]);
}

TEST_CASE(the_cordic_gives_angle_and_length)
{
    for (int a = 0; a < 3600; a += 7) {
        const double rad = (double)a / 10.0 * SYN_PI / 180.0;
        const int64_t x = (int64_t)llround(1e12 * cos(rad));
        const int64_t y = (int64_t)llround(1e12 * sin(rad));
        uint64_t mag;
        const uint32_t t = vibe_atan2(x, y, &mag);
        CHECK_NEAR(syn_ddeg((double)t / 4294967296.0 * 360.0, (double)a / 10.0),
                   0.0, 1e-5);
        CHECK_NEAR((double)mag, 1e12, 1e12 * 1e-7);
        int32_t c, s;
        vibe_cossin(t, &c, &s);
        CHECK_NEAR((double)c / 1073741824.0, cos(rad), 1e-7);
        CHECK_NEAR((double)s / 1073741824.0, sin(rad), 1e-7);
    }
    /* Small vectors are scaled up first: 5 counts read as well as 1e12. */
    uint64_t small;
    const uint32_t t30 = vibe_atan2(4, 2, &small);
    CHECK_NEAR((double)t30 / 4294967296.0 * 360.0, atan2(2.0, 4.0) * 180.0 / SYN_PI,
               1e-5);
    CHECK_EQ(small, 4);
    uint64_t m = 1;
    CHECK_EQ(vibe_atan2(0, 0, &m), 0);
    CHECK_EQ(m, 0);
    CHECK_NEAR((double)(int32_t)(vibe_atan2(INT64_MIN, 0, &m) - 0x80000000u), 0.0, 64.0);
    CHECK_NEAR((double)m, 9223372036854775808.0, 9223372036854775808.0 * 1e-7);
    CHECK_NEAR((double)(int32_t)vibe_atan2(5, 0, NULL), 0.0, 64.0);
}

TEST_CASE(the_square_root_rounds_down)
{
    CHECK_EQ(vibe_isqrt64(0), 0);
    CHECK_EQ(vibe_isqrt64(1), 1);
    CHECK_EQ(vibe_isqrt64(15), 3);
    CHECK_EQ(vibe_isqrt64(16), 4);
    CHECK_EQ(vibe_isqrt64(4294967296ull * 4294967295ull), 4294967295ull);
    CHECK_EQ(vibe_isqrt64(UINT64_MAX), 4294967295ull);
    CHECK_EQ(vibe_isqrt64(1000000ull * 1000000ull - 1u), 999999);
}

TEST_CASE(the_comparator_stamps_a_step_half_a_sample_early)
{
    vibe_edge_t e;
    uint32_t st[8];
    int16_t x[40];
    for (int i = 0; i < 40; ++i) {
        x[i] = (i >= 10 && i < 12) || (i >= 30 && i < 32) ? 3000 : 400;
    }
    /* Noise inside the hysteresis band starts no mark. */
    x[20] = 2100;
    x[21] = 1300;
    x[22] = 2100;
    vibe_edge_init(&e, 1200, 2200, true);
    CHECK_EQ(vibe_edge_scan(&e, x, 40, st, 8), 2);
    CHECK_EQ(st[0], 10u * 256u - 128u);
    CHECK_EQ(st[1], 30u * 256u - 128u);
    /* A ramp: 400 to 3000 over two samples meets the midpoint 1700 at
     * one half. */
    x[9] = 400;
    x[10] = 1700;
    x[11] = 3000;
    vibe_edge_init(&e, 1200, 2200, true);
    CHECK_EQ(vibe_edge_scan(&e, x, 40, st, 8), 2);
    CHECK_EQ(st[0], 11u * 256u - 256u);
    /* The same marks as dips, thresholds given the wrong way round. */
    for (int i = 0; i < 40; ++i) {
        x[i] = (int16_t)(3400 - x[i]);
    }
    vibe_edge_init(&e, 2200, 1200, false);
    CHECK_EQ(vibe_edge_scan(&e, x, 40, st, 1), 1);
    CHECK_EQ(st[0], 11u * 256u - 256u);
    /* Positions carry across blocks. */
    vibe_edge_init(&e, 1200, 2200, false);
    CHECK_EQ(vibe_edge_scan(&e, x, 20, st, 8), 1);
    CHECK_EQ(vibe_edge_scan(&e, x + 20, 20, st, 8), 1);
    CHECK_EQ(st[0], 30u * 256u - 128u);
    CHECK_EQ(vibe_edge_scan(NULL, x, 20, st, 8), 0);
}

TEST_CASE(null_and_clamped_settings)
{
    vibe_demod_init(NULL, NULL, NULL, NULL);
    vibe_demod_push(NULL, NULL, 0);
    vibe_demod_edge(NULL, 0);
    vibe_demod_set_rpm(NULL, 1);
    vibe_demod_set_blades(NULL, 1);
    CHECK_EQ(vibe_demod_pos_q8(NULL), 0);
    vibe_cfg_default(NULL);
    vibe_cfg_t c;
    vibe_cfg_default(&c);
    c.fs_hz = 1;
    c.window = 1;
    c.blades = 200;
    vibe_demod_init(&dm, &c, NULL, NULL);
    CHECK_EQ(dm.cfg.fs_hz, 1000);
    CHECK_EQ(dm.cfg.window, 256);
    CHECK_EQ(dm.cfg.blades, VIBE_BLADES_MAX);
    vibe_demod_set_blades(&dm, 99);
    CHECK_EQ(dm.cfg.blades, VIBE_BLADES_MAX);
    c.fs_hz = 1000000;
    c.window = 1000000;
    vibe_demod_init(&dm, &c, NULL, NULL);
    CHECK_EQ(dm.cfg.fs_hz, 100000);
    CHECK_EQ(dm.cfg.window, 65535);
    vibe_demod_init(&dm, NULL, NULL, NULL);
    CHECK_EQ(dm.cfg.fs_hz, 10000);
    const int16_t z[300] = { 0 };
    vibe_demod_push(&dm, z, 300);
    CHECK_EQ(vibe_demod_pos_q8(&dm), 300u * 256u);
}

int main(void)
{
    RUN(a_167hz_sine_of_400mg_at_265deg_through_the_index_channel);
    RUN(without_the_channel_offset_the_phase_is_3deg_late);
    RUN(speeds_from_300_to_60000rpm_with_exact_edges);
    RUN(below_the_slowest_revolution_the_index_is_lost);
    RUN(noise_of_5mg_rms_on_400mg_and_alone);
    RUN(a_2x_component_beside_the_1x);
    RUN(the_blade_pass_and_its_limit_at_half_the_sample_rate);
    RUN(an_index_jittered_by_one_sample);
    RUN(a_missing_pulse_costs_one_revolution);
    RUN(an_extra_pulse_is_two_short_revolutions_refused);
    RUN(a_lost_index_says_so_and_comes_back);
    RUN(blocks_of_any_length_give_the_same_reports);
    RUN(a_late_edge_splits_the_samples_already_pushed);
    RUN(an_edge_out_of_order_or_past_a_full_queue_is_dropped);
    RUN(the_no_index_mode_reads_the_magnitude_at_the_rpm_it_is_given);
    RUN(an_rpm_1pct_off_loses_little_magnitude_and_shows_in_the_quality);
    RUN(an_rpm_outside_the_revolution_limits_reads_nothing);
    RUN(switching_modes_drops_the_revolution_and_the_lock);
    RUN(a_clipped_sample_and_a_dc_out_of_range_are_flagged);
    RUN(the_index_back_after_a_900s_pause_is_taken);
    RUN(an_unbroken_index_across_the_position_wrap);
    RUN(a_mode_switch_inside_a_window_reports_the_new_mode_only);
    RUN(a_blade_count_change_inside_a_window_clears_the_blade_pass);
    RUN(an_edge_handed_in_after_its_window_counts_in_the_next);
    RUN(the_cordic_gives_angle_and_length);
    RUN(the_square_root_rounds_down);
    RUN(the_comparator_stamps_a_step_half_a_sample_early);
    RUN(null_and_clamped_settings);
    return test_summary("vibe");
}
