/*
 * The coprocessor's sensor service (sense_svc.h): core 1's step, against
 * the modelled INA228 and INA3221 of fake_ina.h, a bus that opens and
 * closes on pins, and a clock the suite moves 1 ms a step.
 *
 * Under test: a set-up opening the bus on its pins and reading both
 * parts, a new one closing it and opening it again, one with no part
 * leaving it closed, and pins that do not open leaving it closed; the
 * generations taken; the run restarted on run_gen; a capture armed with
 * CH1's own level, given its edge once, timed and reported, an arm with
 * nothing to read ended lost and counted, and the count running on
 * across set-ups; the address scan -- online parts not asked, repeated
 * each SENSE_RETRY_MS while a part is missing, not on a stuck bus nor
 * during a capture, and a fault part way keeping what it found; and every
 * snapshot field.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "fake_ina.h"
#include "sense_svc.h"

#define I228_ADDR  0x45u
#define I3221_ADDR 0x40u
#define OTHER_ADDR 0x48u        /* something else on the bus */

static fake_bus_t   fb;
static fake_part_t *i228;
static fake_part_t *i3221;
static sense_svc_t  v;
static sense_cmd_t  cmd;
static sense_snap_t snap;
static uint64_t     g_us;

/* The pins side. */
static bool     g_open_ok;
static unsigned g_opens, g_closes, g_asks, g_recoveries;
static uint8_t  g_sda, g_scl;
static unsigned g_ask_fail_at;  /* the nth ask since the reset fails, 0 none */
static bool     g_ask_hold;     /* every ask fails: SDA held under the scan */
static sense_err_t g_ask_fail_with;
static uint32_t g_asked;        /* bit n: 0x40 + n was asked */

static uint64_t now_us(void *ctx)
{
    (void)ctx;
    return g_us;
}

static void recover(void *ctx)
{
    (void)ctx;
    ++g_recoveries;
}

static bool open_bus(void *ctx, uint8_t sda, uint8_t scl)
{
    (void)ctx;
    ++g_opens;
    g_sda = sda;
    g_scl = scl;
    return g_open_ok;
}

static void close_bus(void *ctx)
{
    (void)ctx;
    ++g_closes;
}

static sense_err_t ask(void *ctx, uint8_t addr)
{
    fake_bus_t *b = (fake_bus_t *)ctx;
    ++g_asks;
    g_asked |= 1u << (addr - 0x40u);
    if (g_ask_hold || (g_ask_fail_at != 0u && g_asks == g_ask_fail_at)) {
        return g_ask_fail_with;
    }
    return (fake_find(b, addr) != NULL) ? SENSE_OK : SENSE_NACK;
}

static void counts_zero(void)
{
    g_opens = g_closes = g_asks = g_recoveries = 0u;
    g_asked = 0u;
    g_ask_fail_at = 0u;
    g_ask_hold = false;
    g_ask_fail_with = SENSE_BUS_LOW;
}

/* Both parts and one other device on the bus; the service closed; an
 * order for both parts on GP16 and GP17. */
static void rig(void)
{
    sense_bus_t scratch;
    fake_bus_init(&fb, &scratch);
    i228  = fake_add(&fb, FAKE_INA228, I228_ADDR, 200e-6);
    i3221 = fake_add(&fb, FAKE_INA3221, I3221_ADDR, 0.1);
    (void)fake_add(&fb, FAKE_INA3221, OTHER_ADDR, 0.1);
    i228->volts[0]  = 16.8;
    i228->amps[0]   = 12.0;
    i228->degc      = 31.0;
    i3221->volts[0] = 6.0;
    i3221->amps[0]  = 0.12;
    g_us = 20000000u;
    g_open_ok = true;
    counts_zero();
    const sense_svc_io_t io = {
        .sched = { { fake_read, fake_write, &fb }, now_us, recover, &fb },
        .open = open_bus, .close = close_bus, .ask = ask,
    };
    sense_svc_init(&v, &io);
    memset(&cmd, 0, sizeof(cmd));
    cmd.cfg_gen = 1u;
    cmd.sda = 16u;
    cmd.scl = 17u;
    cmd.parts.ina228_en = true;
    cmd.parts.ina228_addr = I228_ADDR;
    cmd.parts.ina228_shunt_uohm = 200u;
    cmd.parts.ina228_max_ma = 204800u;
    cmd.parts.ina3221_en = true;
    cmd.parts.ina3221_addr = I3221_ADDR;
    cmd.parts.ina3221_shunt_uohm = 100000u;
    cmd.parts.ina3221_channels = 1u;
    cmd.cap.rise_ua = SENSE_CAP_RISE_AUTO;
    cmd.cap.hold_ua = 120000;
    cmd.cap.move_ua = 30000;
    cmd.cap.band_ua = 50000;
}

static void step(void)
{
    sense_svc_step(&v, &cmd, &snap);
    g_us += 1000u;
}

static void steps(unsigned n)
{
    for (unsigned k = 0; k < n; ++k) {
        step();
    }
}

#define BIT(a) ((uint16_t)(1u << ((a) - 0x40u)))

/* ------------------------------------------------------------ set-ups */

TEST_CASE(a_set_up_opens_the_bus_and_reads_both_parts)
{
    rig();
    step();
    CHECK_EQ(g_opens, 1u);
    CHECK_EQ(g_sda, 16u);
    CHECK_EQ(g_scl, 17u);
    CHECK(snap.open);
    CHECK_EQ(snap.cfg_gen, 1u);
    CHECK_EQ(snap.held, ((uint64_t)1u << 16) | ((uint64_t)1u << 17));
    CHECK_EQ(snap.i228, SENSE_PART_ONLINE);
    CHECK_EQ(snap.i3221, SENSE_PART_ONLINE);
    CHECK_EQ(snap.i228_maker, 0x5449u);
    CHECK_EQ(snap.i228_device, 0x2281u);
    CHECK_EQ(snap.i3221_maker, 0x5449u);
    CHECK_EQ(snap.i3221_die, 0x3220u);
    /* The online parts are not asked; the rest of 0x40 to 0x4F is. */
    CHECK_EQ(g_asks, 14u);
    CHECK_EQ(g_asked & (BIT(I228_ADDR) | BIT(I3221_ADDR)), 0u);
    CHECK_EQ(snap.present, BIT(I228_ADDR) | BIT(I3221_ADDR) | BIT(OTHER_ADDR));
    CHECK(!snap.stuck);
    CHECK_EQ(snap.errors, 0u);
    CHECK(!snap.have_win);

    steps(120u);
    CHECK_EQ(g_opens, 1u);
    CHECK_EQ(g_asks, 14u);               /* nothing missing: no rescan */
    CHECK(snap.have_win);
    CHECK_EQ(snap.win[SENSE_SRC_CH1].i_mean_ua, 120000);
    CHECK_EQ(snap.win[SENSE_SRC_CH1].v_mean_uv, 6000000);
    CHECK_NEAR((double)snap.win[SENSE_SRC_INA228].i_mean_ua, 12e6, 400.0);
    CHECK_NEAR((double)snap.win[SENSE_SRC_INA228].v_mean_uv, 16.8e6, 200.0);
    CHECK(snap.have_temp);
    CHECK_NEAR((double)snap.temp_mdegc, 31000.0, 8.0);
    CHECK(snap.have_diag);
    CHECK(snap.run.have_v);
    CHECK_EQ(snap.cap_state, SENSE_CAP_IDLE);
}

/* A new set-up closes the bus and opens it on the new pins with a fresh
 * schedule; one with no part leaves it closed. */
TEST_CASE(a_new_set_up_closes_the_bus_and_opens_it_again)
{
    rig();
    steps(60u);
    cmd.cfg_gen = 2u;
    cmd.sda = 4u;
    cmd.scl = 5u;
    step();
    CHECK_EQ(g_closes, 1u);
    CHECK_EQ(g_opens, 2u);
    CHECK_EQ(g_sda, 4u);
    CHECK_EQ(snap.cfg_gen, 2u);
    CHECK_EQ(snap.held, ((uint64_t)1u << 4) | ((uint64_t)1u << 5));
    CHECK(!snap.have_win);               /* afresh */

    cmd.cfg_gen = 3u;
    cmd.parts.ina228_en = false;
    cmd.parts.ina3221_en = false;
    step();
    CHECK_EQ(g_closes, 2u);
    CHECK_EQ(g_opens, 2u);
    CHECK(!snap.open);
    CHECK_EQ(snap.held, 0u);
    CHECK_EQ(snap.cfg_gen, 3u);
    CHECK_EQ(snap.i228, SENSE_PART_UNSET);
    CHECK_EQ(snap.present, 0u);
    const unsigned before = fb.transactions;
    steps(50u);
    CHECK_EQ(fb.transactions, before);   /* nothing addressed */
    CHECK_EQ(g_closes, 2u);
}

/* Pins the block cannot take: the bus stays closed, and so do the parts;
 * a run and a capture ordered meanwhile are taken as seen, the capture
 * lost. */
TEST_CASE(pins_that_do_not_open_leave_the_bus_closed)
{
    rig();
    g_open_ok = false;
    step();
    CHECK_EQ(g_opens, 1u);
    CHECK(!snap.open);
    CHECK_EQ(snap.held, 0u);
    CHECK_EQ(fb.transactions, 0u);
    cmd.run_gen = 5u;
    cmd.cap_gen = 1u;
    cmd.cap_on = true;
    step();
    CHECK_EQ(snap.run_gen, 5u);
    CHECK_EQ(snap.cap_gen, 1u);
    CHECK_EQ(snap.cap_state, SENSE_CAP_LOST);
    CHECK_EQ(snap.cap_seq, 1u);
    cmd.cap_gen = 2u;
    cmd.cap_on = false;
    step();
    CHECK_EQ(snap.cap_state, SENSE_CAP_IDLE);
    CHECK_EQ(snap.cap_seq, 1u);
    CHECK_EQ(g_opens, 1u);               /* not tried again */
}

/* -------------------------------------------------------------- the run */

TEST_CASE(a_new_run_restarts_the_peaks_and_the_totals)
{
    rig();
    steps(60u);
    CHECK(!snap.run.totals_ok);
    i228->energy = 1000u;
    i228->charge = 500u;
    cmd.run_gen = 1u;
    step();
    CHECK_EQ(snap.run_gen, 1u);
    CHECK(snap.run.totals_ok);
    CHECK_EQ(i228->energy, 0u);          /* RSTACC */
    steps(30u);
    CHECK(snap.run.totals_ok);
    step();
    CHECK_EQ(snap.run_gen, 1u);
}

/* ---------------------------------------------------------- the capture */

TEST_CASE(a_capture_is_armed_given_its_edge_once_and_reported)
{
    rig();
    steps(100u);
    cmd.cap_gen = 1u;
    cmd.cap_on = true;
    step();
    CHECK_EQ(snap.cap_gen, 1u);
    CHECK_EQ(snap.cap_state, SENSE_CAP_ARMED);
    steps(5u);
    cmd.edge_set = true;
    cmd.edge_us = g_us + 500u;
    step();
    CHECK_EQ(snap.cap_state, SENSE_CAP_WAITING);
    const uint32_t edge_t = v.sched.cap.edge_t;
    i3221->amps[0] = 0.95;
    steps(10u);
    CHECK_EQ(snap.cap_state, SENSE_CAP_MOVING);
    /* The same edge again is not given again. */
    CHECK_EQ(v.sched.cap.edge_t, edge_t);
    steps(300u);
    i3221->amps[0] = 0.12;
    for (unsigned k = 0; k < 100u && snap.cap_state == SENSE_CAP_MOVING; ++k) {
        step();
    }
    CHECK_EQ(snap.cap_state, SENSE_CAP_ARRIVED);
    CHECK_EQ(snap.cap_seq, 1u);
    CHECK(snap.cap_move_t > 0u);
    CHECK(snap.cap_move_t <= 30u);
    CHECK(snap.cap_arrive_t > 3000u);
    CHECK_NEAR((double)snap.cap_peak_ua, 950000.0, 400.0);
    CHECK(snap.cap_samples > 300u);
    CHECK(!snap.cap_clipped);
    /* The level before the command was CH1's own. */
    CHECK_EQ(v.sched.cap.arm.rise_ua, 120000);

    /* A disarm: idle, the count kept. */
    cmd.cap_gen = 2u;
    cmd.cap_on = false;
    cmd.edge_set = false;
    step();
    CHECK_EQ(snap.cap_state, SENSE_CAP_IDLE);
    CHECK_EQ(snap.cap_seq, 1u);
}

/* An arm the schedule refuses -- the INA3221 not answering -- ends at once
 * as lost and counts; the next order clears it.  The count runs on across
 * a new set-up. */
TEST_CASE(an_arm_with_nothing_to_read_ends_lost)
{
    rig();
    i3221->present = false;
    steps(5u);
    CHECK(snap.i3221 != SENSE_PART_ONLINE);
    cmd.cap_gen = 1u;
    cmd.cap_on = true;
    step();
    CHECK_EQ(snap.cap_state, SENSE_CAP_LOST);
    CHECK_EQ(snap.cap_seq, 1u);
    CHECK_EQ(snap.cap_move_t, 0u);
    cmd.edge_set = true;                 /* an edge for it changes nothing */
    cmd.edge_us = g_us;
    step();
    CHECK_EQ(snap.cap_state, SENSE_CAP_LOST);

    /* Back, armed and lost with its part: 2 ended. */
    i3221->present = true;
    g_us += (uint64_t)SENSE_RETRY_MS * 1000u;
    step();
    CHECK_EQ(snap.i3221, SENSE_PART_ONLINE);
    cmd.cap_gen = 2u;
    cmd.edge_set = false;
    steps(60u);
    CHECK_EQ(snap.cap_state, SENSE_CAP_ARMED);
    i3221->present = false;
    steps(4u);
    CHECK_EQ(snap.cap_state, SENSE_CAP_LOST);
    CHECK_EQ(snap.cap_seq, 2u);

    /* A new set-up: the count goes on from 2. */
    cmd.cfg_gen = 2u;
    step();
    CHECK_EQ(snap.cap_seq, 2u);
    cmd.cap_gen = 3u;
    step();
    CHECK_EQ(snap.cap_state, SENSE_CAP_LOST);
    CHECK_EQ(snap.cap_seq, 3u);
}

/* A set-up after a refused arm: idle, not the old LOST, whether the bus
 * then closes, fails to open or opens. */
TEST_CASE(a_new_set_up_ends_a_lost_arm)
{
    for (unsigned how = 0; how < 3u; ++how) {
        rig();
        i3221->present = false;
        steps(5u);
        cmd.cap_gen = 1u;
        cmd.cap_on = true;
        step();
        CHECK_EQ(snap.cap_state, SENSE_CAP_LOST);
        CHECK_EQ(snap.cap_seq, 1u);
        /* The page clears CAP_ARM with the new set-up. */
        cmd.cfg_gen = 2u;
        cmd.cap_on = false;
        if (how == 0u) {
            cmd.parts.ina228_en = false;     /* the bus closes */
            cmd.parts.ina3221_en = false;
        } else if (how == 1u) {
            g_open_ok = false;               /* the bus does not open */
        }
        step();
        CHECK_EQ(snap.open, how == 2u);
        CHECK_EQ(snap.cfg_gen, 2u);
        CHECK_EQ(snap.cap_gen, 1u);
        CHECK_EQ(snap.cap_state, SENSE_CAP_IDLE);
        CHECK_EQ(snap.cap_seq, 1u);
        step();
        CHECK_EQ(snap.cap_state, SENSE_CAP_IDLE);
    }
}

/* ------------------------------------------------------------- the scan */

/* A part missing: scanned again every SENSE_RETRY_MS, and what answers
 * elsewhere is found; not while the bus is stuck, nor while a capture is
 * under way. */
TEST_CASE(a_missing_part_is_scanned_for_again)
{
    rig();
    i228->addr = 0x44u;                  /* bridged elsewhere */
    step();
    CHECK(snap.i228 != SENSE_PART_ONLINE);
    CHECK_EQ(g_asks, 15u);               /* all but the INA3221 */
    CHECK_EQ(snap.present, BIT(0x44u) | BIT(I3221_ADDR) | BIT(OTHER_ADDR));
    steps(SENSE_RETRY_MS - 1u);
    CHECK_EQ(g_asks, 15u);
    step();
    CHECK_EQ(g_asks, 30u);

    /* Under way, a capture holds the scan off; done, the scan runs. */
    cmd.cap_gen = 1u;
    cmd.cap_on = true;
    steps(SENSE_RETRY_MS + 10u);
    CHECK_EQ(snap.cap_state, SENSE_CAP_ARMED);
    CHECK_EQ(g_asks, 30u);
    cmd.cap_gen = 2u;
    cmd.cap_on = false;
    step();
    CHECK_EQ(g_asks, 45u);

    /* A stuck bus is not scanned. */
    fb.low = true;
    steps(SENSE_RETRY_MS + 10u);
    CHECK(snap.stuck);
    CHECK(g_recoveries > 0u);
    CHECK_EQ(g_asks, 45u);
}

/* A fault part way: what the scan found so far stands, the addresses it
 * did not reach keep their last answer, and the scan is not taken as
 * done. */
TEST_CASE(a_scan_cut_short_keeps_what_it_found)
{
    rig();
    i228->present = false;
    step();
    CHECK_EQ(snap.present, BIT(I3221_ADDR) | BIT(OTHER_ADDR));
    /* Now 0x41 answers and 0x48 has gone; the scan fails at its 3rd ask,
     * 0x43, before reaching 0x48. */
    (void)fake_add(&fb, FAKE_INA3221, 0x41u, 0.1);
    fb.part[2].present = false;
    counts_zero();
    g_ask_fail_at = 3u;
    steps(SENSE_RETRY_MS);
    CHECK_EQ(g_asks, 3u);
    CHECK_EQ(snap.present, BIT(I3221_ADDR) | BIT(0x41u) | BIT(OTHER_ADDR));
}

/* Both parts offline, so the scan is the only traffic; SDA held under it:
 * the scan's first answer marks the bus stuck and ends it, the error
 * counts, the recovery runs at the next tick and every SENSE_RECOVER_MS
 * after, and the scan waits for the bus. */
TEST_CASE(a_line_held_under_the_scan_starts_the_recovery)
{
    rig();
    i228->present = false;
    i3221->present = false;
    step();
    CHECK(snap.i228 != SENSE_PART_ONLINE);
    CHECK(snap.i3221 != SENSE_PART_ONLINE);
    CHECK(!snap.stuck);
    const uint16_t errors = snap.errors;     /* the two probes' NACKs */
    counts_zero();
    g_ask_hold = true;
    steps(SENSE_RETRY_MS - 1u);              /* up to the next scan */
    CHECK_EQ(g_asks, 0u);
    step();                                  /* the scan */
    CHECK_EQ(g_asks, 1u);
    CHECK(snap.stuck);
    /* The two probes due at the same tick NACK, then the held line. */
    CHECK_EQ(snap.errors, (uint16_t)(errors + 3u));
    CHECK_EQ(g_recoveries, 0u);
    step();
    CHECK_EQ(g_recoveries, 1u);              /* within 1 ms */
    steps(SENSE_RECOVER_MS);
    CHECK_EQ(g_recoveries, 2u);
    CHECK_EQ(g_asks, 1u);                    /* no scan on a stuck bus */
    CHECK(snap.stuck);
}

/* Timeouts under the scan: one alone is not stuck and the scan goes on,
 * the address keeping its last answer; SENSE_STUCK_TIMEOUTS in a row are,
 * and end it. */
TEST_CASE(timeouts_under_the_scan_count_towards_stuck)
{
    rig();
    i228->present = false;
    step();
    CHECK_EQ(snap.present, BIT(I3221_ADDR) | BIT(OTHER_ADDR));
    counts_zero();
    g_ask_fail_with = SENSE_TIMEOUT;
    g_ask_fail_at = 8u;                      /* 0x48 times out, once */
    steps(SENSE_RETRY_MS);
    CHECK_EQ(g_asks, 15u);
    CHECK(!snap.stuck);
    CHECK_EQ(snap.present, BIT(I3221_ADDR) | BIT(OTHER_ADDR));

    counts_zero();
    g_ask_fail_with = SENSE_TIMEOUT;
    g_ask_hold = true;
    steps(SENSE_RETRY_MS);
    CHECK_EQ(g_asks, SENSE_STUCK_TIMEOUTS);
    CHECK(snap.stuck);
    step();
    CHECK_EQ(g_recoveries, 1u);
}

/* What sense_bus makes of an answer from outside its drivers. */
TEST_CASE(the_bus_notes_an_outside_answer)
{
    sense_bus_t b;
    fake_bus_t f;
    fake_bus_init(&f, &b);
    sense_bus_note(&b, SENSE_NACK);
    CHECK_EQ(b.errors, 0u);
    CHECK(!b.stuck);
    sense_bus_note(&b, SENSE_TIMEOUT);
    CHECK_EQ(b.errors, 1u);
    CHECK(!b.stuck);
    sense_bus_note(&b, SENSE_NACK);          /* ends the run */
    sense_bus_note(&b, SENSE_TIMEOUT);
    CHECK(!b.stuck);
    sense_bus_note(&b, SENSE_TIMEOUT);
    CHECK(b.stuck);
    sense_bus_note(&b, SENSE_OK);            /* lines back */
    CHECK(!b.stuck);
    sense_bus_note(&b, SENSE_BUS_LOW);
    CHECK(b.stuck);
    CHECK_EQ(b.errors, 4u);
    sense_bus_note(&b, SENSE_OFFLINE);       /* reached nothing */
    sense_bus_note(&b, SENSE_STUCK);
    CHECK_EQ(b.errors, 4u);
    CHECK(b.stuck);
}

/* ------------------------------------------------------------- snapshot */

/* Before a first order: nothing open, every field 0. */
TEST_CASE(a_service_starts_closed)
{
    rig();
    cmd.parts.ina228_en = false;
    cmd.parts.ina3221_en = false;
    cmd.cfg_gen = 0u;
    step();
    CHECK(v.started);
    CHECK_EQ(g_opens, 0u);
    CHECK(!snap.open);
    CHECK_EQ(snap.cfg_gen, 0u);
    CHECK_EQ(snap.cap_state, SENSE_CAP_IDLE);
    CHECK_EQ(snap.cap_seq, 0u);
}

int main(void)
{
    RUN(a_set_up_opens_the_bus_and_reads_both_parts);
    RUN(a_new_set_up_closes_the_bus_and_opens_it_again);
    RUN(pins_that_do_not_open_leave_the_bus_closed);
    RUN(a_new_run_restarts_the_peaks_and_the_totals);
    RUN(a_capture_is_armed_given_its_edge_once_and_reported);
    RUN(an_arm_with_nothing_to_read_ends_lost);
    RUN(a_new_set_up_ends_a_lost_arm);
    RUN(a_missing_part_is_scanned_for_again);
    RUN(a_scan_cut_short_keeps_what_it_found);
    RUN(a_line_held_under_the_scan_starts_the_recovery);
    RUN(timeouts_under_the_scan_count_towards_stuck);
    RUN(the_bus_notes_an_outside_answer);
    RUN(a_service_starts_closed);
    return test_summary("sense_svc");
}
