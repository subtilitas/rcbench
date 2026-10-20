/*
 * The trace of CH1's 1 ms samples (sense_trace.h), core 1's side, over
 * sense_trace_rig.h: a ring that is a power of two; every CH1 sample and
 * every CH1 bus voltage taken once, with its time and value, across a
 * window's close, an emptied window, a set-up and a closed bus; no bus
 * transaction and no clock read added to the tick, at most 2 records a
 * call; a voltage kept only behind the sample of its tick; a ring full by
 * exactly 0, 1 and many records, the dropped ones counted, named where
 * they are missing and never waited for; a line for each gap in a trace;
 * a mark kind that is no trigger; a count of dropped records that
 * saturates; a schedule replaced at the tick count of the one before
 * starts afresh.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_trace_rig.h"

/* ------------------------------------------------------------- core 1 */

TEST_CASE(a_ring_is_a_power_of_two)
{
    sense_trace_t t2;
    sense_trace_rec_t r[8];
    char out[64];
    CHECK(!sense_trace_init(&t2, NULL, 8u));
    CHECK(!sense_trace_init(&t2, r, 0u));
    CHECK(!sense_trace_init(&t2, r, 1u));
    CHECK(!sense_trace_init(&t2, r, 6u));
    CHECK(!sense_trace_init(&t2, r, (1u << 24) + 1u));
    CHECK(!sense_trace_init(&t2, r, 1u << 25));
    /* Refused, it takes nothing and gives nothing. */
    rig();
    tick();
    sense_trace_feed(&t2, &svc.sched, g_us);
    sense_trace_trigger(&t2, SENSE_TRACE_TRIG_KEY, g_us, 0u, 0u);
    CHECK(!sense_trace_pulse(&t2, 0u, 5u, 1500u, g_us));
    CHECK(!sense_trace_active(&t2));
    CHECK_EQ(sense_trace_pump(&t2, g_us, true, out, sizeof(out)), 0u);
    CHECK(sense_trace_init(&t2, r, 2u));
    CHECK(sense_trace_init(&t2, r, 8u));
}

TEST_CASE(every_ch1_sample_and_voltage_is_taken_once)
{
    rig();
    for (unsigned k = 0; k < 1000u; ++k) {
        i3221->amps[0] = 0.0004 * (double)(k % 2000u);
        tick();
    }
    CHECK_EQ(i3221->reads[INA3221_SHUNT1], 1000u);
    /* A voltage every 24 ms: 41 or 42 in the second, each traced. */
    CHECK(i3221->reads[INA3221_BUS1] >= 41u && i3221->reads[INA3221_BUS1] <= 42u);
    CHECK_EQ(held_of(SENSE_TRACE_CURRENT), 1000u);
    CHECK_EQ(held_of(SENSE_TRACE_BUS), i3221->reads[INA3221_BUS1]);
    /* Each sample 1 ms after the one before, at the value read. */
    unsigned n = 0u;
    for (unsigned k = 0u; k < held_data(); ++k) {
        const sense_trace_rec_t *r = rec(k);
        if ((r->meta >> 24) == SENSE_TRACE_BUS) {
            CHECK_EQ(r->v, 6000);
            continue;
        }
        CHECK_EQ(r->t, 200000u + 10u * n);
        CHECK_EQ(r->v, 400 * (int32_t)n);
        CHECK_EQ(r->meta & SENSE_TRACE_LOST_MAX, 0u);
        ++n;
    }
    CHECK_EQ(n, 1000u);
}

TEST_CASE(the_feed_adds_no_bus_transaction_and_reads_no_clock)
{
    rig();
    for (unsigned k = 0; k < 200u; ++k) {
        sense_svc_step(&svc, &cmd, &snap);
        const unsigned bus = fb.transactions;
        const unsigned clock = g_clock_reads;
        const uint32_t held = sense_trace_held(&tr);
        sense_trace_feed(&tr, &svc.sched, g_us);
        CHECK_EQ(fb.transactions, bus);
        CHECK_EQ(g_clock_reads, clock);
        /* The first tick brings the set-up's two records as well. */
        CHECK(sense_trace_held(&tr) - held <= ((k == 0u) ? 4u : 2u));
        g_us += 1000u;
    }
    CHECK_EQ(held_of(SENSE_TRACE_CURRENT), 200u);
}

TEST_CASE(a_tick_with_no_sample_feeds_none)
{
    rig();
    i3221->present = false;
    for (unsigned k = 0; k < 100u; ++k) {
        tick();
    }
    CHECK_EQ(held_data(), 0u);
    /* The part answers from its next probe on. */
    i3221->present = true;
    for (unsigned k = 0; k < 1000u; ++k) {
        tick();
    }
    CHECK_EQ(held_of(SENSE_TRACE_CURRENT), i3221->reads[INA3221_SHUNT1]);
    CHECK(held_of(SENSE_TRACE_CURRENT) > 0u);
}

TEST_CASE(a_set_up_and_a_closed_bus_lose_and_repeat_nothing)
{
    rig();
    for (unsigned k = 0; k < 130u; ++k) {
        tick();
    }
    /* A set-up: the schedule and its history start afresh. */
    ++cmd.cfg_gen;
    for (unsigned k = 0; k < 130u; ++k) {
        tick();
    }
    CHECK_EQ(held_of(SENSE_TRACE_CURRENT), i3221->reads[INA3221_SHUNT1]);
    CHECK_EQ(held_of(SENSE_TRACE_BUS), i3221->reads[INA3221_BUS1]);
    /* No part enabled: the bus closes and nothing is fed. */
    const unsigned held = held_data();
    ++cmd.cfg_gen;
    cmd.parts.i3221.fitted = 0u;
    for (unsigned k = 0; k < 20u; ++k) {
        tick();
    }
    CHECK(!svc.open);
    CHECK_EQ(held_data(), held);
    CHECK_EQ(tr.src.cfg, 0u);
    CHECK_EQ(tr.src.shunt, 0u);
    /* And open again: every read once more. */
    ++cmd.cfg_gen;
    cmd.parts.i3221.fitted = 1u;
    for (unsigned k = 0; k < 130u; ++k) {
        tick();
    }
    CHECK_EQ(held_of(SENSE_TRACE_CURRENT), i3221->reads[INA3221_SHUNT1]);
    CHECK_EQ(held_of(SENSE_TRACE_BUS), i3221->reads[INA3221_BUS1]);
    CHECK_EQ(tr.src.shunt, I3221_UOHM);
    /* The shunt went from none to 0.1 Ω, to none with the bus closed and
     * back: a record each time, and one of the Configuration with it. */
    CHECK_EQ(held_of(SENSE_TRACE_SHUNT), 3u);
    CHECK(held_of(SENSE_TRACE_CFG) >= 3u);
}

TEST_CASE(a_voltage_is_taken_from_a_window_a_reset_emptied)
{
    rig();
    for (unsigned k = 0; k < 300u; ++k) {
        tick();
    }
    /* The part resets itself; the read-back finds it within 40 ms and
     * empties CH1's window. */
    fake_reset3221(i3221);
    for (unsigned k = 0; k < 2000u; ++k) {
        tick();
    }
    CHECK_EQ(svc.sched.i3221_resets[0], 1u);
    CHECK_EQ(held_of(SENSE_TRACE_CURRENT), i3221->reads[INA3221_SHUNT1]);
    CHECK_EQ(held_of(SENSE_TRACE_BUS), i3221->reads[INA3221_BUS1]);
    for (unsigned k = 0u; k < held_data(); ++k) {
        if ((rec(k)->meta >> 24) == SENSE_TRACE_BUS) {
            CHECK_EQ(rec(k)->v, 6000);
        }
    }
    CHECK_EQ(tr.src.cfg >> 24, 1u);
}

TEST_CASE(a_set_up_that_leaves_the_history_at_the_same_place)
{
    rig();
    /* 51 samples leave the history's head at 1, where the first sample
     * after a set-up leaves it again: that sample is a new one. */
    for (unsigned k = 0; k < 51u; ++k) {
        tick();
    }
    CHECK_EQ(svc.sched.hist_head, 1u);
    ++cmd.cfg_gen;
    tick();
    CHECK_EQ(svc.sched.hist_head, 1u);
    CHECK_EQ(held_of(SENSE_TRACE_CURRENT), 52u);
    CHECK_EQ(held_of(SENSE_TRACE_CURRENT), i3221->reads[INA3221_SHUNT1]);
}

/* A tick by hand: a new CH1 sample 1 ms on, and the window being filled
 * holding @p n_v voltages that sum to @p v_sum. */
static void poke(uint16_t n_v, int64_t v_sum)
{
    sense_sched_t *sc = &svc.sched;
    sc->hist[sc->hist_head].t  = (uint32_t)(g_us / 100u);
    sc->hist[sc->hist_head].ua = 120000;
    sc->hist_head = (uint8_t)((sc->hist_head + 1u) % SENSE_FAST_HISTORY);
    sc->acc[SENSE_CH(0, 1)].n_v   = n_v;
    sc->acc[SENSE_CH(0, 1)].v_sum = v_sum;
    ++sc->ticks;
    sense_trace_feed(&tr, sc, g_us);
    g_us += 1000u;
}

TEST_CASE(a_voltage_is_told_from_the_one_before_by_window_and_count)
{
    rig();
    /* Two set-ups inside the first 50 ms: each schedule's first window
     * is number 0 and holds one voltage after 10 ticks. */
    for (unsigned k = 0; k < 10u; ++k) {
        tick();
    }
    CHECK_EQ(svc.sched.acc[SENSE_CH(0, 1)].n_v, 1u);
    ++cmd.cfg_gen;
    i3221->volts[0] = 5.0;
    for (unsigned k = 0; k < 10u; ++k) {
        tick();
    }
    CHECK_EQ(svc.sched.win, 0u);
    CHECK_EQ(svc.sched.acc[SENSE_CH(0, 1)].n_v, 1u);
    CHECK_EQ(i3221->reads[INA3221_BUS1], 2u);
    CHECK_EQ(held_of(SENSE_TRACE_BUS), 2u);
    int32_t mv[2] = { 0, 0 };
    unsigned n = 0u;
    for (unsigned k = 0u; k < held_data(); ++k) {
        if ((rec(k)->meta >> 24) == SENSE_TRACE_BUS) {
            mv[n++ % 2u] = rec(k)->v;
        }
    }
    CHECK_EQ(mv[0], 6000);
    CHECK_EQ(mv[1], 5000);
    /* The next window with as many voltages as the one before: its one
     * voltage is a new one, and the value is its own. */
    unsigned held = held_data();
    svc.sched.win += 1u;
    poke(1u, 4800000);
    CHECK_EQ(held_data(), held + 2u);
    CHECK_EQ(rec(held + 1u)->v, 4800);
    /* A second in the same window: the sum's gain. */
    poke(2u, 4800000 + 4700000);
    CHECK_EQ(rec(held + 3u)->v, 4700);
    /* No gain: the sample alone. */
    poke(2u, 4800000 + 4700000);
    CHECK_EQ(held_data(), held + 5u);
    CHECK_EQ(rec(held + 4u)->meta >> 24, SENSE_TRACE_CURRENT);
    /* The window emptied and one voltage read since: counted from
     * nothing. */
    poke(1u, 4600000);
    CHECK_EQ(held_data(), held + 7u);
    CHECK_EQ(rec(held + 6u)->v, 4600);
}

TEST_CASE(a_schedule_replaced_at_the_same_tick_count_starts_afresh)
{
    /* One tick, a set-up, one tick: both schedules have run one tick and
     * clipped once.  The second clip is the new schedule's own. */
    rig();
    i3221->amps[0] = 5.0;
    tick();
    CHECK_EQ(svc.sched.ticks, 1u);
    ++cmd.cfg_gen;
    tick();
    CHECK_EQ(svc.sched.ticks, 1u);
    CHECK_EQ(held_of(SENSE_TRACE_CLIP_HI),
             i3221->reads[INA3221_SHUNT1]);
    CHECK_EQ(held_of(SENSE_TRACE_CURRENT), 0u);
    /* A voltage, then a new schedule whose first tick brings its count
     * back to the old one with one voltage of its own: that voltage is
     * traced, not taken for the old one. */
    sense_sched_t *sc = &svc.sched;
    sc->acc[SENSE_CH(0, 1)].n_v   = 0u;
    sc->acc[SENSE_CH(0, 1)].v_sum = 0;
    sense_trace_feed(&tr, sc, g_us);
    poke(1u, 4900000);
    CHECK_EQ(rec(held_data() - 1u)->meta >> 24, SENSE_TRACE_BUS);
    CHECK_EQ(rec(held_data() - 1u)->v, 4900);
    const unsigned held = held_data();
    sc->ticks -= 1u;
    poke(1u, 4800000);
    CHECK_EQ(held_data(), held + 2u);
    if (held_data() != held + 2u) {
        return;
    }
    CHECK_EQ(rec(held + 1u)->meta >> 24, SENSE_TRACE_BUS);
    CHECK_EQ(rec(held + 1u)->v, 4800);
}

TEST_CASE(a_voltage_with_no_current_sample_of_its_tick_is_left_out)
{
    rig();
    for (unsigned k = 0; k < 10u; ++k) {
        tick();
    }
    /* CH1's current read failed in a tick whose bus voltage read went
     * through: no sample line the voltage could stand behind. */
    const unsigned held = held_data();
    svc.sched.acc[SENSE_CH(0, 1)].n_v  += 1u;
    svc.sched.acc[SENSE_CH(0, 1)].v_sum += 5900000;
    sense_trace_feed(&tr, &svc.sched, g_us);
    CHECK_EQ(held_data(), held);
    /* The voltage after it is its own, not the two together. */
    poke((uint16_t)(svc.sched.acc[SENSE_CH(0, 1)].n_v + 1u),
         svc.sched.acc[SENSE_CH(0, 1)].v_sum + 5800000);
    CHECK_EQ(held_data(), held + 2u);
    CHECK_EQ(rec(held)->meta >> 24, SENSE_TRACE_CURRENT);
    CHECK_EQ(rec(held + 1u)->v, 5800);
    /* A full ring that drops the sample drops its voltage with it. */
    rig_size(8u);
    for (unsigned k = 0; k < 8u; ++k) {
        tick();
    }
    CHECK_EQ(sense_trace_held(&tr), 8u);
    const uint32_t lost = tr.src.lost;
    poke((uint16_t)(svc.sched.acc[SENSE_CH(0, 1)].n_v + 1u),
         svc.sched.acc[SENSE_CH(0, 1)].v_sum + 5800000);
    CHECK_EQ(tr.src.lost, lost + 1u);
}

TEST_CASE(a_ring_full_by_exactly_0_1_and_many_records)
{
    fill(7u);
    CHECK_EQ(sense_trace_held(&tr), 7u);
    CHECK_EQ(tr.src.lost, 0u);
    fill(8u);                       /* full, none over */
    CHECK_EQ(sense_trace_held(&tr), 8u);
    CHECK_EQ(tr.src.lost, 0u);
    fill(9u);                       /* 1 over */
    CHECK_EQ(sense_trace_held(&tr), 8u);
    CHECK_EQ(tr.src.lost, 1u);
    fill(8u + 5000u);               /* many over */
    CHECK_EQ(sense_trace_held(&tr), 8u);
    CHECK_EQ(tr.src.lost, 5000u);
    /* The records kept are the first 8: a full ring drops the newest. */
    for (unsigned k = 0u; k < 8u; ++k) {
        CHECK_EQ(rec(k)->t, 200010u + 10u * k);
    }
}

TEST_CASE(the_dropped_records_are_named_where_they_are_missing)
{
    fill(8u + 3u);
    /* A trace reads the ring out; the next sample kept carries the 3,
     * and they are said once. */
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_KEY, g_us, 0u, 0u);
    for (unsigned k = 0; k < 20u; ++k) {
        pass();
    }
    run(5u);
    sense_trace_key(&tr, 'x', g_us);
    run(3u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_l, 1u);
    CHECK_EQ(seen.lost, 3u);
    CHECK_EQ(seen.z_l, 3u);
    CHECK_EQ(seen.n_s, 8u + 5u);
    CHECK_EQ(seen.z_s, 8u + 5u);
    /* The gap shows in the times: 8 samples, 3 missing, then the rest. */
    CHECK_EQ(seen.t[7], 200010u + 70u);
    CHECK_EQ(seen.t[8], 200010u + 110u);
    /* The ring read out with no record kept since: the line for the 3
     * stands behind the 8 samples, before the trigger's line, which
     * waits for core 1 to pass its time. */
    CHECK(strstr(g_log, "10,300\r\n$L n=3\r\n$K t=") != NULL);
    CHECK(strstr(g_log, "\r\n40,300\r\n") != NULL);
    CHECK_EQ(tr.out.drops_seen, 3u);
    CHECK_EQ(tr.out.ahead, 0u);
}

/* The mark kinds share the trigger enumeration but are no trigger: one
 * passed in as a trigger would index past the tables of the line writer. */
TEST_CASE(a_mark_kind_is_no_trigger)
{
    rig_size(8u);
    sense_trace_trigger(&tr, SENSE_TRACE_MARK_DEST, g_us, 0u, 1500u);
    sense_trace_trigger(&tr, SENSE_TRACE_MARK_STEP, g_us, 0u, 1500u);
    sense_trace_trigger(&tr, (sense_trace_trig_t)99, g_us, 0u, 1500u);
    for (unsigned k = 0; k < 20u; ++k) {
        pass();
    }
    CHECK_EQ(tr.out.stage, SENSE_TRACE_IDLE);
    CHECK_EQ(tr.out.id, 0u);
    CHECK_EQ(tr.out.wait_n, 0u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
}

TEST_CASE(each_gap_in_a_trace_has_its_line)
{
    rig_size(8u);
    run(20u);
    sense_trace_key(&tr, 't', g_us);
    run(20u);
    /* Two stalls of 20 ms on a ring of 8, a second apart. */
    for (unsigned gap = 0u; gap < 2u; ++gap) {
        for (unsigned k = 0; k < 20u; ++k) {
            tick();
            pass_as(0u, true);
        }
        run(1000u);
    }
    sense_trace_key(&tr, 'x', g_us);
    run(2u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_l, 2u);
    CHECK(seen.lost >= 24u && seen.lost <= 28u);
    CHECK_EQ(seen.z_l, seen.lost);
    CHECK_EQ(seen.z_s, seen.n_s);
}

TEST_CASE(a_count_of_dropped_records_saturates)
{
    fill(8u);
    tr.src.lost = SENSE_TRACE_LOST_MAX - 1u;
    sense_svc_step(&svc, &cmd, &snap);
    sense_trace_feed(&tr, &svc.sched, g_us);
    g_us += 1000u;
    CHECK_EQ(tr.src.lost, SENSE_TRACE_LOST_MAX);
    sense_svc_step(&svc, &cmd, &snap);
    sense_trace_feed(&tr, &svc.sched, g_us);
    CHECK_EQ(tr.src.lost, SENSE_TRACE_LOST_MAX);
}

int main(void)
{
    RUN(a_ring_is_a_power_of_two);
    RUN(every_ch1_sample_and_voltage_is_taken_once);
    RUN(the_feed_adds_no_bus_transaction_and_reads_no_clock);
    RUN(a_tick_with_no_sample_feeds_none);
    RUN(a_set_up_and_a_closed_bus_lose_and_repeat_nothing);
    RUN(a_voltage_is_taken_from_a_window_a_reset_emptied);
    RUN(a_set_up_that_leaves_the_history_at_the_same_place);
    RUN(a_voltage_is_told_from_the_one_before_by_window_and_count);
    RUN(a_schedule_replaced_at_the_same_tick_count_starts_afresh);
    RUN(a_voltage_with_no_current_sample_of_its_tick_is_left_out);
    RUN(a_ring_full_by_exactly_0_1_and_many_records);
    RUN(the_dropped_records_are_named_where_they_are_missing);
    RUN(each_gap_in_a_trace_has_its_line);
    RUN(a_mark_kind_is_no_trigger);
    RUN(a_count_of_dropped_records_saturates);
    return test_summary("sense_trace_feed");
}
