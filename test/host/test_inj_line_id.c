/*
 * The touch injector's wiring check (testbench/touch-injector/core): which
 * of GP4 and GP5 carries SCL, read from synthetic I2C (Inter-Integrated
 * Circuit) traffic sampled as core 1 samples it.
 *
 * A transaction here is what the panel's GT911 poll puts on the bus: a
 * START, the address and two register bytes, a repeated START, the address
 * and one byte read, a STOP.  Every level is held a whole number of samples.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "greatest.h"

#include "inj_line_id.h"

#define MAXE 40000u

typedef struct {
    inj_edge_t e[MAXE];
    uint32_t   n;
    uint32_t   t;        /* the sample being written                 */
    uint8_t    sda, scl;
    bool       swap;     /* SDA on GP5 and SCL on GP4                */
    uint32_t   h;        /* half a bit, in samples                   */
    uint8_t    first;
} gen_t;

static gen_t g;

static uint8_t state_of(uint8_t sda, uint8_t scl, bool swap)
{
    const uint8_t sda_bit = swap ? INJ_LINE_GP5 : INJ_LINE_GP4;
    const uint8_t scl_bit = swap ? INJ_LINE_GP4 : INJ_LINE_GP5;
    return (uint8_t)((sda ? sda_bit : 0u) | (scl ? scl_bit : 0u));
}

static void lvl(uint8_t sda, uint8_t scl)
{
    const uint8_t before = state_of(g.sda, g.scl, g.swap);
    g.sda = sda;
    g.scl = scl;
    const uint8_t after = state_of(sda, scl, g.swap);
    if (after != before && g.n < MAXE) {
        g.e[g.n].at = g.t;
        g.e[g.n].state = after;
        ++g.n;
    }
}

static void wait(uint32_t samples) { g.t += samples; }

/* @p ksps sample rate, @p bus_hz bus clock. */
static void gen_reset(uint32_t ksps, uint32_t bus_hz)
{
    memset(&g, 0, sizeof(g));
    g.sda = 1;
    g.scl = 1;
    /* Half a bit period in samples: ksps * 1000 / (2 * bus_hz). */
    g.h = (uint32_t)((uint64_t)ksps * 1000u / (2u * bus_hz));
    if (g.h == 0u) {
        g.h = 1;
    }
    g.first = state_of(1, 1, false);
}

static void bit(uint8_t b)
{
    lvl(b, 0);
    wait(g.h / 2u);
    lvl(b, 1);
    wait(g.h);
    lvl(b, 0);
    wait(g.h - g.h / 2u);
}

static void byte(uint8_t v)
{
    for (int i = 7; i >= 0; --i) {
        bit((uint8_t)((v >> i) & 1u));
    }
    bit(0);   /* the target's ACK */
}

static void start(uint32_t hold)
{
    lvl(0, 1);
    wait(hold);
    lvl(0, 0);
    wait(g.h / 2u);
}

static void stop(void)
{
    lvl(0, 0);
    wait(g.h / 2u);
    lvl(0, 1);
    wait(g.h);
    lvl(1, 1);
}

/* One status poll, then @p gap_us of idle bus. */
static void transaction(bool swap, uint32_t gap_samples)
{
    g.swap = swap;
    start(g.h);
    byte(0x14 << 1);
    byte(0x81);
    byte(0x4E);
    /* Repeated START. */
    lvl(1, 0);
    wait(g.h / 2u);
    lvl(1, 1);
    wait(g.h);
    start(g.h);
    byte((0x14 << 1) | 1);
    byte(0x00);
    stop();
    wait(gap_samples);
}

/* @p count polls at @p ksps and @p bus_hz, 50us apart, after 20us idle. */
static void traffic(uint32_t ksps, uint32_t bus_hz, int count, bool swap)
{
    gen_reset(ksps, bus_hz);
    const uint32_t gap = ksps * 50u / 1000u;
    wait(ksps * 20u / 1000u);
    for (int i = 0; i < count; ++i) {
        transaction(swap, gap);
    }
}

static inj_line_result_t classify(uint32_t ksps)
{
    inj_line_result_t r;
    inj_line_classify(g.first, g.e, g.n, g.t, ksps, &r);
    return r;
}

TEST_CASE(polls_at_400khz_name_gp5_as_scl)
{
    traffic(10000, 400000, 50, false);
    const inj_line_result_t r = classify(10000);
    CHECK_EQ(r.verdict, INJ_WIRING_OK);
    CHECK_EQ(r.tx, 50);
    CHECK_EQ(r.named, 50);
    CHECK_EQ(r.agree_pct, 100);
    CHECK_EQ(r.rate_ksps, 10000);
    CHECK_EQ(r.end_state, INJ_LINE_GP4 | INJ_LINE_GP5);
}

TEST_CASE(swapped_wires_are_named_swapped)
{
    traffic(10000, 400000, 50, true);
    const inj_line_result_t r = classify(10000);
    CHECK_EQ(r.verdict, INJ_WIRING_SWAPPED);
    CHECK_EQ(r.tx, 50);
    CHECK_EQ(r.agree_pct, 100);
}

TEST_CASE(polls_at_100khz_and_4_msps)
{
    traffic(4000, 100000, 30, false);
    CHECK_EQ(classify(4000).verdict, INJ_WIRING_OK);
    traffic(4000, 100000, 30, true);
    CHECK_EQ(classify(4000).verdict, INJ_WIRING_SWAPPED);
}

/* The slowest rate at which every level lasts the 2 samples the filter
 * keeps: half a bit of 2 samples at 400kHz is 1.6 Msample/s. */
TEST_CASE(a_two_sample_level_is_kept)
{
    traffic(1600, 400000, 25, false);
    CHECK_EQ(g.h, 2);
    const inj_line_result_t r = classify(1600);
    CHECK_EQ(r.verdict, INJ_WIRING_OK);
    CHECK_EQ(r.tx, 25);
}

static void spike_scl(uint32_t len)
{
    /* SCL dips low for @p len samples on an idle bus. */
    lvl(1, 0);
    wait(len);
    lvl(1, 1);
    wait(g.h * 8u);
}

/* A one-sample dip of SCL before every poll is a glitch and is dropped. */
TEST_CASE(a_one_sample_spike_is_dropped)
{
    gen_reset(10000, 400000);
    wait(200);
    for (int i = 0; i < 30; ++i) {
        g.swap = false;
        spike_scl(1);
        wait(100);
        transaction(false, 500);
    }
    const inj_line_result_t r = classify(10000);
    CHECK_EQ(r.verdict, INJ_WIRING_OK);
    CHECK_EQ(r.tx, 30);
    CHECK_EQ(r.agree_pct, 100);
}

/* A two-sample dip is a level: SCL falls first out of idle and rises last
 * into it, so each dip is a transaction naming GP5 as SDA, and half the
 * transactions disagree. */
TEST_CASE(a_two_sample_spike_is_a_level)
{
    gen_reset(10000, 400000);
    wait(200);
    for (int i = 0; i < 30; ++i) {
        g.swap = false;
        spike_scl(2);
        wait(100);
        transaction(false, 500);
    }
    const inj_line_result_t r = classify(10000);
    CHECK_EQ(r.tx, 60);
    CHECK_EQ(r.named, 60);
    CHECK_EQ(r.agree_pct, 50);
    CHECK_EQ(r.verdict, INJ_WIRING_NOISE);
}

TEST_CASE(nineteen_transactions_give_no_verdict_and_twenty_do)
{
    traffic(10000, 400000, 19, false);
    inj_line_result_t r = classify(10000);
    CHECK_EQ(r.tx, 19);
    CHECK_EQ(r.verdict, INJ_WIRING_NOISE);

    traffic(10000, 400000, 20, false);
    r = classify(10000);
    CHECK_EQ(r.tx, 20);
    CHECK_EQ(r.verdict, INJ_WIRING_OK);
}

/* Transactions with the lines the other way round stand for a bus whose
 * rules disagree: 19 of 20 is 95% and passes, 47 of 50 is 94% and does
 * not. */
TEST_CASE(ninety_five_percent_agreement_is_the_floor)
{
    gen_reset(10000, 400000);
    wait(200);
    for (int i = 0; i < 20; ++i) {
        transaction(i == 7, 500);
    }
    inj_line_result_t r = classify(10000);
    CHECK_EQ(r.tx, 20);
    CHECK_EQ(r.agree_pct, 95);
    CHECK_EQ(r.verdict, INJ_WIRING_OK);

    gen_reset(10000, 400000);
    wait(200);
    for (int i = 0; i < 50; ++i) {
        transaction(i == 3 || i == 17 || i == 40, 500);
    }
    r = classify(10000);
    CHECK_EQ(r.tx, 50);
    CHECK_EQ(r.agree_pct, 94);
    CHECK_EQ(r.verdict, INJ_WIRING_NOISE);
}

TEST_CASE(a_quiet_high_bus_is_idle)
{
    gen_reset(10000, 400000);
    inj_line_result_t r;
    inj_line_classify(INJ_LINE_GP4 | INJ_LINE_GP5, NULL, 0, 5000000, 10000,
                      &r);
    CHECK_EQ(r.verdict, INJ_WIRING_IDLE);
    CHECK_EQ(r.tx, 0);
    CHECK_EQ(r.edges4, 0);
    CHECK_EQ(r.edges5, 0);
}

TEST_CASE(a_line_low_all_window_is_low)
{
    inj_line_result_t r;
    /* Both low: an unpowered bus. */
    inj_line_classify(0, NULL, 0, 5000000, 10000, &r);
    CHECK_EQ(r.verdict, INJ_WIRING_LOW);
    /* GP5 low, GP4 high. */
    inj_line_classify(INJ_LINE_GP4, NULL, 0, 5000000, 10000, &r);
    CHECK_EQ(r.verdict, INJ_WIRING_LOW);
    CHECK_EQ(r.end_state, INJ_LINE_GP4);

    /* GP4 toggles, GP5 stays low: still low. */
    const inj_edge_t e[] = {
        { 100, 0 }, { 200, INJ_LINE_GP4 }, { 300, 0 }, { 400, INJ_LINE_GP4 },
    };
    inj_line_classify(INJ_LINE_GP4, e, 4, 5000000, 10000, &r);
    CHECK_EQ(r.verdict, INJ_WIRING_LOW);
    CHECK_EQ(r.edges4, 4);
}

/* A window that opens and closes in the middle of a poll counts neither
 * end: the first has no idle before its START, the last no idle after. */
TEST_CASE(transactions_cut_by_the_window_are_not_counted)
{
    gen_reset(10000, 400000);
    /* Opens inside a transaction: SDA low, SCL high. */
    g.sda = 0;
    g.scl = 1;
    g.first = state_of(0, 1, false);
    byte(0x00);
    stop();
    wait(500);
    for (int i = 0; i < 20; ++i) {
        transaction(false, 500);
    }
    /* Closes inside one. */
    start(g.h);
    byte(0x28);
    inj_line_result_t r = classify(10000);
    CHECK_EQ(r.tx, 20);
    CHECK_EQ(r.verdict, INJ_WIRING_OK);
}

/* The last poll's idle runs to the end of the window and closes it. */
TEST_CASE(an_idle_span_at_the_window_end_closes_the_last_poll)
{
    traffic(10000, 400000, 20, false);
    const uint32_t end = g.t;
    inj_line_result_t r;
    /* Window ends 9.9us into the idle: not idle yet, so 19 count. */
    inj_line_classify(g.first, g.e, g.n, end - 500u + 99u, 10000, &r);
    CHECK_EQ(r.tx, 19);
    /* 10us in: it counts. */
    inj_line_classify(g.first, g.e, g.n, end - 500u + 100u, 10000, &r);
    CHECK_EQ(r.tx, 20);
}

/* Both lines falling on one sample name no line, and neither does both
 * rising on one: such a transaction is left out of the agreement. */
TEST_CASE(simultaneous_edges_name_no_line)
{
    gen_reset(10000, 400000);
    wait(200);
    for (int i = 0; i < 20; ++i) {
        transaction(false, 500);
    }
    /* One with SDA and SCL together at both ends. */
    lvl(0, 0);
    wait(50);
    lvl(1, 1);
    wait(500);
    inj_line_result_t r = classify(10000);
    CHECK_EQ(r.tx, 21);
    CHECK_EQ(r.named, 20);
    CHECK_EQ(r.agree_pct, 100);   /* 20 of the 20 that name a line */
    CHECK_EQ(r.verdict, INJ_WIRING_OK);
}

/*
 * The panel polling 0x14 with nothing there, as seen on the bench: the
 * address byte goes unacknowledged, both lines are high after the ninth
 * clock, and 15us later the master sends a bare STOP, SDA down and up with
 * SCL high.  The address part names no line (START on SDA, SCL rises
 * last); the bare STOP names SDA.
 */
static void absent_poll(uint32_t gap_samples)
{
    g.swap = false;
    start(g.h);
    const uint8_t addr = 0x14 << 1;
    for (int i = 7; i >= 0; --i) {
        bit((uint8_t)((addr >> i) & 1u));
    }
    /* Ninth clock: nobody pulls SDA low, and SCL stays high after it. */
    lvl(1, 0);
    wait(g.h / 2u);
    lvl(1, 1);
    wait(150);   /* 15us at 10 Msps */
    /* The bare STOP. */
    lvl(0, 1);
    wait(g.h);
    lvl(1, 1);
    wait(gap_samples);
}

/* 45 reads of the glass and 5 polls of an absent 0x14 in one window. */
TEST_CASE(polls_of_an_absent_injector_do_not_spoil_the_verdict)
{
    gen_reset(10000, 400000);
    wait(200);
    for (int i = 0; i < 50; ++i) {
        if (i % 10 == 4) {
            absent_poll(500);
        } else {
            transaction(false, 500);
        }
    }
    const inj_line_result_t r = classify(10000);
    CHECK_EQ(r.tx, 55);
    CHECK_EQ(r.named, 50);
    CHECK_EQ(r.agree_pct, 100);
    CHECK_EQ(r.verdict, INJ_WIRING_OK);
}

/* SDA and SCL shorted together: every edge is on both lines at once, no
 * transaction names a line, and there is no verdict. */
TEST_CASE(shorted_lines_give_no_verdict)
{
    gen_reset(10000, 400000);
    wait(200);
    for (int i = 0; i < 50; ++i) {
        lvl(0, 0);
        wait(50);
        lvl(1, 1);
        wait(50);
        lvl(0, 0);
        wait(50);
        lvl(1, 1);
        wait(500);
    }
    const inj_line_result_t r = classify(10000);
    CHECK_EQ(r.tx, 50);
    CHECK_EQ(r.named, 0);
    CHECK_EQ(r.agree_pct, 0);
    CHECK_EQ(r.verdict, INJ_WIRING_NOISE);
}

/* Named transactions must be at least half of all: 20 named among 41 is
 * refused although all 20 agree, 21 among 41 passes.  A bus where most
 * transactions name no line is not read as wired. */
TEST_CASE(named_transactions_must_be_half_of_all)
{
    for (int named = 20; named <= 21; ++named) {
        gen_reset(10000, 400000);
        wait(200);
        for (int i = 0; i < 41; ++i) {
            if (i < named) {
                transaction(false, 500);
            } else {
                lvl(0, 0);
                wait(50);
                lvl(1, 1);
                wait(500);
            }
        }
        const inj_line_result_t r = classify(10000);
        CHECK_EQ(r.tx, 41);
        CHECK_EQ(r.named, (uint32_t)named);
        CHECK_EQ(r.agree_pct, 100);
        CHECK_EQ(r.verdict, named == 20 ? INJ_WIRING_NOISE : INJ_WIRING_OK);
    }
}

/* Twenty named are the floor, whatever the count of all: 19 named and 1
 * unnamed is refused. */
TEST_CASE(nineteen_named_give_no_verdict)
{
    gen_reset(10000, 400000);
    wait(200);
    for (int i = 0; i < 19; ++i) {
        transaction(false, 500);
    }
    lvl(0, 0);
    wait(50);
    lvl(1, 1);
    wait(500);
    const inj_line_result_t r = classify(10000);
    CHECK_EQ(r.tx, 20);
    CHECK_EQ(r.named, 19);
    CHECK_EQ(r.verdict, INJ_WIRING_NOISE);
}

/* A gap under 10us between two polls is not idle: the two are one. */
TEST_CASE(a_short_gap_joins_two_polls)
{
    gen_reset(10000, 400000);
    wait(200);
    for (int i = 0; i < 20; ++i) {
        transaction(false, 20);    /* 2us */
        transaction(false, 500);
    }
    const inj_line_result_t r = classify(10000);
    CHECK_EQ(r.tx, 20);
    CHECK_EQ(r.verdict, INJ_WIRING_OK);
}

TEST_CASE(verdicts_have_names)
{
    CHECK_STR_EQ(inj_wiring_name(INJ_WIRING_OK), "ok");
    CHECK_STR_EQ(inj_wiring_name(INJ_WIRING_SWAPPED), "swapped");
    CHECK_STR_EQ(inj_wiring_name(INJ_WIRING_IDLE), "idle");
    CHECK_STR_EQ(inj_wiring_name(INJ_WIRING_LOW), "low");
    CHECK_STR_EQ(inj_wiring_name(INJ_WIRING_NOISE), "noise");
    CHECK_STR_EQ(inj_wiring_name(INJ_WIRING_NONE), "none");
}

int main(void)
{
    RUN(polls_at_400khz_name_gp5_as_scl);
    RUN(swapped_wires_are_named_swapped);
    RUN(polls_at_100khz_and_4_msps);
    RUN(a_two_sample_level_is_kept);
    RUN(a_one_sample_spike_is_dropped);
    RUN(a_two_sample_spike_is_a_level);
    RUN(nineteen_transactions_give_no_verdict_and_twenty_do);
    RUN(ninety_five_percent_agreement_is_the_floor);
    RUN(a_quiet_high_bus_is_idle);
    RUN(a_line_low_all_window_is_low);
    RUN(transactions_cut_by_the_window_are_not_counted);
    RUN(an_idle_span_at_the_window_end_closes_the_last_poll);
    RUN(simultaneous_edges_name_no_line);
    RUN(polls_of_an_absent_injector_do_not_spoil_the_verdict);
    RUN(shorted_lines_give_no_verdict);
    RUN(named_transactions_must_be_half_of_all);
    RUN(nineteen_named_give_no_verdict);
    RUN(a_short_gap_joins_two_polls);
    RUN(verdicts_have_names);
    return test_summary("inj_line_id");
}
