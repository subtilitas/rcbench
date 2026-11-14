/*
 * The balance node's ADC stream (shared/vibe/vibe_frame.c) and the index
 * with its polarity found (vibe_index_*, shared/vibe/vibe_edge.c).
 *
 * The failures this is about: a channel taken for another, a round split
 * across two DMA frames lost or doubled, a lost conversion that shifts
 * every later sample onto the wrong channel without a word, the scale off
 * by the ADC's range (3.1V taken for 3.3V is 6%), a DC window or a
 * threshold in the wrong counts, a bright mark on a dark bell giving no
 * index or the trailing edge, and the report's flags drifting from the
 * radio packet's.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "rcnode.h"
#include "vibe_demod.h"
#include "vibe_edge.h"
#include "vibe_frame.h"

/* The report's flags travel bit for bit (rcnode.h, RCNODE_BF_*). */
_Static_assert(RCNODE_BF_VALID == VIBE_F_VALID, "flag");
_Static_assert(RCNODE_BF_INDEX_SEEN == VIBE_F_INDEX_SEEN, "flag");
_Static_assert(RCNODE_BF_INDEX_STEADY == VIBE_F_INDEX_STEADY, "flag");
_Static_assert(RCNODE_BF_INDEX_LOST == VIBE_F_INDEX_LOST, "flag");
_Static_assert(RCNODE_BF_NO_INDEX == VIBE_F_NO_INDEX, "flag");
_Static_assert(RCNODE_BF_CLIPPED == VIBE_F_CLIPPED, "flag");
_Static_assert(RCNODE_BF_DC_OK == VIBE_F_DC_OK, "flag");
_Static_assert(RCNODE_BF_BLADE_OK == VIBE_F_BLADE_OK, "flag");
_Static_assert(RCNODE_BF_QUALITY == VIBE_F_QUALITY, "flag");
_Static_assert(RCNODE_BF_EDGE_DROPPED == VIBE_F_EDGE_DROPPED, "flag");
_Static_assert(RCNODE_BF_RPM_RANGE == VIBE_F_RPM_RANGE, "flag");
_Static_assert(RCNODE_BF_RESTARTED == VIBE_F_RESTARTED, "flag");

/* The balance node's pattern: vibration, index, battery. */
static const uint8_t k_pat[3] = { 0u, 1u, 3u };

/** Writes one conversion word: reserved bits set, so a decoder that reads
 *  them fails. */
static void put_word(uint8_t *b, uint16_t raw, uint8_t ch)
{
    const uint32_t w = (uint32_t)(raw & 0x0FFFu) | 0x1000u
                       | ((uint32_t)(ch & 0x0Fu) << 13) | 0xFFFE0000u;
    b[0] = (uint8_t)w;
    b[1] = (uint8_t)(w >> 8);
    b[2] = (uint8_t)(w >> 16);
    b[3] = (uint8_t)(w >> 24);
}

/** Rounds of the pattern: entry k of round r holds 100 r + k. */
static size_t make_rounds(uint8_t *b, size_t rounds)
{
    for (size_t r = 0; r < rounds; ++r) {
        for (size_t k = 0; k < 3u; ++k) {
            put_word(b + (3u * r + k) * 4u, (uint16_t)(100u * r + k), k_pat[k]);
        }
    }
    return rounds * 12u;
}

static int16_t c0[64], c1[64], c2[64];
static int16_t *const k_out[3] = { c0, c1, c2 };

TEST_CASE(a_word_gives_bits_0_to_11_and_the_channel_in_bits_13_to_16)
{
    uint8_t b[4];
    put_word(b, 0x0ABCu, 5u);
    uint8_t ch = 0u;
    CHECK_EQ(vibe_frame_word(b, &ch), 0x0ABC);
    CHECK_EQ(ch, 5);
    put_word(b, 0x0FFFu, 15u);
    CHECK_EQ(vibe_frame_word(b, NULL), 0x0FFF);
    (void)vibe_frame_word(b, &ch);
    CHECK_EQ(ch, 15);
}

TEST_CASE(rounds_come_out_one_row_each_in_the_patterns_order)
{
    vibe_frame_t f;
    vibe_frame_init(&f, k_pat, 3u);
    uint8_t b[12u * 10u];
    const size_t n = make_rounds(b, 10u);
    const vibe_split_t r = vibe_frame_split(&f, b, n, k_out, 64u);
    CHECK_EQ(r.rows, 10);
    CHECK_EQ(r.used, n);
    CHECK(!r.broke);
    for (size_t i = 0; i < 10u; ++i) {
        CHECK_EQ(c0[i], (int16_t)(100u * i));
        CHECK_EQ(c1[i], (int16_t)(100u * i + 1u));
        CHECK_EQ(c2[i], (int16_t)(100u * i + 2u));
    }
    CHECK_EQ(f.breaks, 0);
    CHECK_EQ(f.skipped, 0);
}

TEST_CASE(a_round_cut_by_the_frame_end_finishes_in_the_next_frame)
{
    uint8_t b[12u * 4u];
    const size_t n = make_rounds(b, 4u);
    /* Every cut, word by word, gives the same rows as one call. */
    for (size_t cut = 0; cut <= n; cut += 4u) {
        vibe_frame_t f;
        vibe_frame_init(&f, k_pat, 3u);
        vibe_split_t a = vibe_frame_split(&f, b, cut, k_out, 64u);
        CHECK_EQ(a.used, cut);
        int16_t d0[8], d1[8], d2[8];
        int16_t *const o2[3] = { d0, d1, d2 };
        vibe_split_t z = vibe_frame_split(&f, b + cut, n - cut, o2, 8u);
        CHECK_EQ(a.rows + z.rows, 4);
        for (size_t i = 0; i < 4u; ++i) {
            const int16_t v0 = i < a.rows ? c0[i] : d0[i - a.rows];
            const int16_t v2 = i < a.rows ? c2[i] : d2[i - a.rows];
            CHECK_EQ(v0, (int16_t)(100u * i));
            CHECK_EQ(v2, (int16_t)(100u * i + 2u));
        }
    }
}

TEST_CASE(words_before_the_first_entry_are_skipped_and_counted)
{
    vibe_frame_t f;
    vibe_frame_init(&f, k_pat, 3u);
    uint8_t b[12u * 3u];
    const size_t n = make_rounds(b, 3u);
    /* Start at the index entry of round 0. */
    const vibe_split_t r = vibe_frame_split(&f, b + 4u, n - 4u, k_out, 64u);
    CHECK_EQ(f.skipped, 2);
    CHECK_EQ(r.rows, 2);
    CHECK_EQ(c0[0], 100);
    CHECK(!r.broke);
}

TEST_CASE(a_lost_conversion_stops_the_split_in_front_of_it_and_resyncs)
{
    vibe_frame_t f;
    vibe_frame_init(&f, k_pat, 3u);
    uint8_t b[12u * 6u];
    make_rounds(b, 6u);
    /* Round 2 loses its index conversion: vib(2), bat(2), vib(3)... */
    uint8_t g[12u * 6u];
    const size_t keep = 2u * 12u + 4u;
    memcpy(g, b, keep);
    memcpy(g + keep, b + keep + 4u, sizeof b - keep - 4u);
    const size_t n = sizeof b - 4u;
    vibe_split_t r = vibe_frame_split(&f, g, n, k_out, 64u);
    CHECK(r.broke);
    CHECK_EQ(r.rows, 2);
    CHECK_EQ(r.used, keep);          /* in front of bat(2) */
    CHECK_EQ(f.breaks, 1);
    /* The next call skips bat(2) and starts at round 3. */
    r = vibe_frame_split(&f, g + r.used, n - r.used, k_out, 64u);
    CHECK(!r.broke);
    CHECK_EQ(f.skipped, 1);
    CHECK_EQ(r.rows, 3);
    CHECK_EQ(c0[0], 300);
    CHECK_EQ(c1[0], 301);
    CHECK_EQ(c2[2], 502);
}

TEST_CASE(a_word_of_an_invalid_channel_is_a_break)
{
    vibe_frame_t f;
    vibe_frame_init(&f, k_pat, 3u);
    uint8_t b[12u * 2u];
    make_rounds(b, 2u);
    put_word(b + 16u, 77u, 15u);    /* round 1's index */
    const vibe_split_t r = vibe_frame_split(&f, b, sizeof b, k_out, 64u);
    CHECK(r.broke);
    CHECK_EQ(r.rows, 1);
    CHECK_EQ(r.used, 16);
}

TEST_CASE(a_full_output_ends_the_call_after_whole_rounds)
{
    vibe_frame_t f;
    vibe_frame_init(&f, k_pat, 3u);
    uint8_t b[12u * 5u + 2u];
    const size_t n = make_rounds(b, 5u);
    vibe_split_t r = vibe_frame_split(&f, b, n + 2u, k_out, 2u);
    CHECK_EQ(r.rows, 2);
    CHECK_EQ(r.used, 24);
    r = vibe_frame_split(&f, b + 24u, n + 2u - 24u, k_out, 8u);
    CHECK_EQ(r.rows, 3);
    CHECK_EQ(r.used, 36);           /* the 2 bytes past the last word stay */
    CHECK_EQ(c0[0], 200);
}

TEST_CASE(a_pattern_past_four_entries_is_cut_and_none_writes_nothing)
{
    const uint8_t six[6] = { 0u, 1u, 2u, 3u, 4u, 5u };
    vibe_frame_t f;
    vibe_frame_init(&f, six, 6u);
    CHECK_EQ(f.nch, VIBE_FRAME_CH_MAX);
    vibe_frame_init(&f, NULL, 3u);
    uint8_t b[12];
    make_rounds(b, 1u);
    const vibe_split_t r = vibe_frame_split(&f, b, sizeof b, k_out, 4u);
    CHECK_EQ(r.rows, 0);
    CHECK_EQ(r.used, 0);
}

/* ------------------------------------------------------------ the scale */

TEST_CASE(the_nominal_curve_is_3300mV_over_4096_counts)
{
    vibe_adc_cal_t c;
    vibe_adc_cal_nominal(&c);
    CHECK_EQ(c.raw_lo, 1000);
    CHECK_EQ(c.raw_hi, 3000);
    CHECK_EQ(c.mv_lo, 806);         /* 805.66 */
    CHECK_EQ(c.mv_hi, 2417);        /* 2416.99 */
    CHECK(vibe_adc_cal_ok(&c));
}

TEST_CASE(the_scale_at_330mV_per_g_on_the_nominal_curve_is_2441ug_a_count)
{
    vibe_adc_cal_t c;
    vibe_adc_cal_nominal(&c);
    vibe_cfg_t cfg;
    vibe_cfg_default(&cfg);
    CHECK(vibe_cfg_scale(&cfg, &c, 330000u, 1000u, 2300u));
    /* (2417 - 806)mV / 2000 counts = 805.5uV a count; / 330mV/g. */
    const double ug = (double)cfg.ug_per_count_q16 / 65536.0;
    CHECK_NEAR(ug, 805.5e3 / 330.0, 0.01);
    /* The 3.1V range of vibe_cfg_default() would read 6% low. */
    CHECK(ug > 1.06 * (double)150311927u / 65536.0);
    CHECK_EQ(cfg.dc_min, 1241);     /* 1000mV */
    CHECK_EQ(cfg.dc_max, 2855);     /* 2300mV */
    CHECK_EQ(cfg.adc_max, 4095);
    CHECK_EQ(cfg.fs_hz, 10000);     /* untouched */
}

TEST_CASE(counts_and_millivolts_round_and_hold_to_the_range)
{
    const vibe_adc_cal_t c = { 1000u, 3000u, 800u, 2400u };   /* 0.8mV a count */
    CHECK_EQ(vibe_cal_counts(&c, 800u, 4095u), 1000);
    CHECK_EQ(vibe_cal_counts(&c, 1650u, 4095u), 2063);         /* 2062.5 */
    CHECK_EQ(vibe_cal_counts(&c, 0u, 4095u), 0);
    CHECK_EQ(vibe_cal_counts(&c, 100u, 4095u), 125);           /* below raw_lo */
    CHECK_EQ(vibe_cal_counts(&c, 5000u, 4095u), 4095);
    CHECK_EQ(vibe_cal_mv(&c, 2000u * 16u), 1600u);
    CHECK_EQ(vibe_cal_mv(&c, 2000u * 16u + 8u), 1600u);        /* 1600.4 */
    CHECK_EQ(vibe_cal_mv(&c, 0u), 0u);
    CHECK_EQ(vibe_cal_mv(&c, 100u * 16u), 80u);
}

TEST_CASE(bad_points_a_zero_sensitivity_or_an_empty_window_change_nothing)
{
    vibe_cfg_t cfg;
    vibe_cfg_default(&cfg);
    const vibe_cfg_t before = cfg;
    const vibe_adc_cal_t flat = { 1000u, 1000u, 800u, 2400u };
    const vibe_adc_cal_t down = { 1000u, 3000u, 2400u, 800u };
    const vibe_adc_cal_t ok = { 1000u, 3000u, 800u, 2400u };
    CHECK(!vibe_cfg_scale(&cfg, &flat, 330000u, 1000u, 2300u));
    CHECK(!vibe_cfg_scale(&cfg, &down, 330000u, 1000u, 2300u));
    CHECK(!vibe_cfg_scale(&cfg, NULL, 330000u, 1000u, 2300u));
    CHECK(!vibe_cfg_scale(&cfg, &ok, 0u, 1000u, 2300u));
    CHECK(!vibe_cfg_scale(&cfg, &ok, 330000u, 2300u, 2300u));
    CHECK(memcmp(&cfg, &before, sizeof cfg) == 0);
    CHECK_EQ(vibe_cal_counts(&flat, 1000u, 4095u), 0);
    CHECK_EQ(vibe_cal_mv(&down, 1000u), 0u);
    CHECK(!vibe_cfg_scale(NULL, &ok, 330000u, 1000u, 2300u));
}

/* ------------------------------------------------------- the index */

/** A D0 step at 10kHz: @p mark_high marks at 3200mV-ish, else at 100. */
static void index_wave(int16_t *x, size_t n, double rpm, double t0,
                       double width, bool mark_high)
{
    const double f = rpm / 60.0;
    for (size_t k = 0; k < n; ++k) {
        double turn = f * ((double)k / 10000.0 - t0);
        turn -= floor(turn);
        const bool in = turn < width;
        x[k] = (int16_t)((in == mark_high) ? 3970 : 120);
    }
}

TEST_CASE(a_dark_mark_gives_the_rising_starts_from_the_first_block)
{
    static int16_t x[20000];
    index_wave(x, 20000u, 600.0, 0.00513, 0.03, true);
    vibe_index_t ix;
    vibe_index_init(&ix, 1365, 2730);
    uint32_t e[64];
    size_t ne = 0;
    for (size_t at = 0; at < 20000u; at += 100u) {
        ne += vibe_index_scan(&ix, x + at, 100u, e + ne, 64u - ne);
    }
    CHECK(!ix.low_mark);
    CHECK_EQ(ne, 20);               /* 10Hz for 2s */
    /* The start of the mark: 51.3 samples, half a sample before the first
     * high sample (52). */
    CHECK_NEAR((double)e[0] / 256.0, 51.5, 0.01);
    CHECK_NEAR((double)(e[1] - e[0]) / 256.0, 1000.0, 0.01);
}

TEST_CASE(a_bright_mark_switches_to_the_falling_starts_after_one_span)
{
    static int16_t x[20000];
    index_wave(x, 20000u, 600.0, 0.00513, 0.03, false);
    vibe_index_t ix;
    vibe_index_init(&ix, 1365, 2730);
    uint32_t e[64];
    size_t ne = 0;
    for (size_t at = 0; at < 20000u; at += 100u) {
        const bool low_before = ix.low_mark;
        const size_t got = vibe_index_scan(&ix, x + at, 100u, e + ne, 64u - ne);
        if (low_before) {
            ne += got;
        }
    }
    CHECK(ix.low_mark);
    /* From block 25 on (the span of 2,500 samples): 17 marks at 51.3 +
     * 1000 k, k = 3..19, each starting at the fall. */
    CHECK_EQ(ne, 17);
    CHECK_NEAR((double)e[0] / 256.0, 3051.5, 0.01);
}

TEST_CASE(a_mark_that_turns_long_switches_back)
{
    static int16_t x[10000];
    index_wave(x, 10000u, 6000.0, 0.0, 0.9, true);  /* high 90% */
    vibe_index_t ix;
    vibe_index_init(&ix, 1365, 2730);
    (void)vibe_index_scan(&ix, x, 10000u, NULL, 0u);
    CHECK(ix.low_mark);
    index_wave(x, 10000u, 6000.0, 0.0, 0.1, true);  /* high 10% */
    (void)vibe_index_scan(&ix, x, 10000u, NULL, 0u);
    CHECK(!ix.low_mark);
    CHECK_EQ(vibe_index_scan(NULL, x, 10u, NULL, 0u), 0);
}

/* ------------------------------------------------ the node's path */

static vibe_report_t s_last;
static unsigned s_reports;

static void take(const vibe_report_t *r, void *ctx)
{
    (void)ctx;
    s_last = *r;
    ++s_reports;
}

/* Words as the node's ADC writes them: vibration (400mg at 265deg, 330mV/g
 * on 1.65V), the index D0 converted a third of a sample later, the battery
 * at 1.9V; split, the index found, the demodulator scaled from the curve.
 * The report must read 400mg at 265deg. */
TEST_CASE(words_to_report_reads_400mg_at_265deg)
{
    vibe_adc_cal_t c;
    vibe_adc_cal_nominal(&c);
    vibe_cfg_t cfg;
    vibe_cfg_default(&cfg);
    cfg.fs_hz = 10000u;
    cfg.edge_shift_q8 = 85;
    CHECK(vibe_cfg_scale(&cfg, &c, 330000u, 1000u, 2300u));
    static vibe_demod_t d;
    s_reports = 0u;
    vibe_demod_init(&d, &cfg, take, NULL);
    vibe_frame_t f;
    vibe_frame_init(&f, k_pat, 3u);
    vibe_index_t ix;
    vibe_index_init(&ix, (int16_t)vibe_cal_counts(&c, 1100u, 4095u),
                    (int16_t)vibe_cal_counts(&c, 2200u, 4095u));

    const double rpm = 10020.0, fr = rpm / 60.0, t0 = 0.00731;
    const double pi = 3.14159265358979323846;
    const double mv_per_count = 3300.0 / 4096.0;
    uint8_t frame[100u * 12u];
    int16_t v[100], ixs[100], bat[100];
    int16_t *const out[3] = { v, ixs, bat };
    long sample = 0;
    for (int fr_n = 0; fr_n < 200; ++fr_n) {           /* 2s */
        for (size_t r = 0; r < 100u; ++r, ++sample) {
            const double tv = (double)sample / 10000.0;
            const double ti = ((double)sample + 1.0 / 3.0) / 10000.0;
            const double g = 0.4 * cos(2.0 * pi * fr * (tv - t0) - 265.0 * pi / 180.0);
            const double mv = 1650.0 + 330.0 * g;
            double turn = fr * (ti - t0);
            turn -= floor(turn);
            const uint16_t ixr = turn < 0.03 ? 3970u : 120u;
            put_word(frame + r * 12u, (uint16_t)lround(mv / mv_per_count), 0u);
            put_word(frame + r * 12u + 4u, ixr, 1u);
            put_word(frame + r * 12u + 8u, 2358u, 3u);
        }
        const vibe_split_t s = vibe_frame_split(&f, frame, sizeof frame, out, 100u);
        CHECK_EQ(s.rows, 100);
        uint32_t e[16];
        const size_t ne = vibe_index_scan(&ix, ixs, s.rows, e, 16u);
        vibe_demod_feed(&d, v, s.rows, e, ne);
    }
    CHECK_EQ(s_reports, 8);
    CHECK((s_last.flags & VIBE_F_VALID) != 0u);
    CHECK((s_last.flags & VIBE_F_DC_OK) != 0u);
    CHECK((s_last.flags & VIBE_F_INDEX_STEADY) != 0u);
    CHECK_NEAR((double)s_last.mag_ug[VIBE_H_1X], 400000.0, 4000.0);
    CHECK_NEAR((double)s_last.phase_cdeg[VIBE_H_1X] / 100.0, 265.0, 1.0);
    CHECK_NEAR((double)s_last.rpm_x10 / 10.0, rpm, rpm * 0.01);
    CHECK_NEAR((double)vibe_cal_mv(&c, s_last.dc_x16), 1650.0, 3.0);
}

int main(void)
{
    RUN(a_word_gives_bits_0_to_11_and_the_channel_in_bits_13_to_16);
    RUN(rounds_come_out_one_row_each_in_the_patterns_order);
    RUN(a_round_cut_by_the_frame_end_finishes_in_the_next_frame);
    RUN(words_before_the_first_entry_are_skipped_and_counted);
    RUN(a_lost_conversion_stops_the_split_in_front_of_it_and_resyncs);
    RUN(a_word_of_an_invalid_channel_is_a_break);
    RUN(a_full_output_ends_the_call_after_whole_rounds);
    RUN(a_pattern_past_four_entries_is_cut_and_none_writes_nothing);
    RUN(the_nominal_curve_is_3300mV_over_4096_counts);
    RUN(the_scale_at_330mV_per_g_on_the_nominal_curve_is_2441ug_a_count);
    RUN(counts_and_millivolts_round_and_hold_to_the_range);
    RUN(bad_points_a_zero_sensitivity_or_an_empty_window_change_nothing);
    RUN(a_dark_mark_gives_the_rising_starts_from_the_first_block);
    RUN(a_bright_mark_switches_to_the_falling_starts_after_one_span);
    RUN(a_mark_that_turns_long_switches_back);
    RUN(words_to_report_reads_400mg_at_265deg);
    return test_summary("vibe_frame");
}
