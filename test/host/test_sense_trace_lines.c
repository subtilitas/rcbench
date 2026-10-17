/*
 * The trace of CH1's 1 ms samples (sense_trace.h), its lines and its
 * counts, over sense_trace_rig.h: the pulse held to the frame; every line
 * at its longest inside SENSE_TRACE_LINE_MAX; a clipped sample; trigger
 * lines in the order of their times; a record that comes late, a core 1
 * that stays away and a trigger line that waits for core 1; records
 * dropped with none kept after them, counted in the end line; and the
 * shunt code back from every current ina3221_current_ua() gives.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_trace_rig.h"

TEST_CASE(a_pulse_is_the_one_the_frame_can_carry)
{
    /* 560 Hz: a frame of 1785 counts, the counter wrapping at 1784. */
    CHECK_EQ(sense_trace_rendered(2000u, 1784u), 1784u);
    CHECK_EQ(sense_trace_rendered(1785u, 1784u), 1784u);
    CHECK_EQ(sense_trace_rendered(1784u, 1784u), 1784u);
    CHECK_EQ(sense_trace_rendered(1783u, 1784u), 1783u);
    CHECK_EQ(sense_trace_rendered(0u, 1784u), 0u);
    /* 50 Hz, and a counter's top above 16 bits. */
    CHECK_EQ(sense_trace_rendered(2000u, 19999u), 2000u);
    CHECK_EQ(sense_trace_rendered(65535u, 70000u), 65535u);
    /* Two endpoints past the frame render the same pulse: no command. */
    rig();
    CHECK(!sense_trace_pulse(&tr, 0u, 5u, sense_trace_rendered(1900u, 1784u),
                             g_us));
    CHECK(!sense_trace_pulse(&tr, 0u, 5u, sense_trace_rendered(2000u, 1784u),
                             g_us + 100000u));
    CHECK(sense_trace_pulse(&tr, 0u, 5u, sense_trace_rendered(1500u, 1784u),
                            g_us + 200000u));
}

TEST_CASE(every_line_at_its_longest_fits)
{
    rig();
    /* A clipped sample at the bottom of the range, and the longest
     * numbers a header, a trigger and an end line carry: the millisecond
     * tick and the 0.1 ms count both at 10 digits. */
    i3221->amps[0] = -5.0;
    g_us = (uint64_t)4294964295u * 1000u;
    run(100u);
    svc.sched.i3221.shunt_uohm = UINT32_MAX;
    run(5u);
    tr.out.id = 65534u;
    const uint32_t t = (uint32_t)(g_us / 100u);
    CHECK(t >= 4000000000u && g_us / 1000u >= 4000000000u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us, 0u, 0u);
    tr.out.len_ms = 123456u;
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 65535u, 65535u);
    /* A slew's end at the longest numbers. */
    tr.out.watch[0].have = tr.out.watch[0].changed = true;
    tr.out.watch[0].slewed = true;
    tr.out.watch[0].ch = 65535u;
    tr.out.watch[0].pulse = 65535u;
    tr.out.watch[0].changed_t = t;
    tr.out.watch[0].cmd_t = UINT32_MAX;
    (void)sense_trace_pulse(&tr, 0u, 65535u, 65535u,
                            g_us + SENSE_TRACE_HOLD_MS * 1000u);
    run(20u);
    tr.out.n_s = tr.out.n_v = tr.out.n_lost = UINT32_MAX - 1u;
    tr.out.n_m = tr.out.n_mlost = UINT32_MAX;
    sense_trace_key(&tr, 'x', g_us);
    run(3u);
    char want[80];
    snprintf(want, sizeof(want), "$T v=3 n=65535 trig=edge t=%lu ms=%lu "
             "len=99999\r\n", (unsigned long)t,
             (unsigned long)((uint64_t)4294964295u + 105u));
    CHECK_EQ(strlen(want), SENSE_TRACE_LINE_MAX - 1u);
    CHECK(strncmp(g_log, want, strlen(want)) == 0);
    snprintf(want, sizeof(want), "$C t=%lu ch=65535 us=65535\r\n",
             (unsigned long)t);
    CHECK(strstr(g_log, want) != NULL);
    snprintf(want, sizeof(want),
             "$D t=%lu ch=65535 us=65535 cmd=4294967295\r\n",
             (unsigned long)t);
    CHECK(strstr(g_log, want) != NULL);
    CHECK(strstr(g_log, "$E t=") != NULL);
    CHECK(strstr(g_log, "$Z n=65535 s=99999999 v=9999999 l=99999999 m=9999 "
                        "ml=9999 e=k\r\n") != NULL);
    CHECK(strstr(g_log, "$H dt_us=1000 shunt_uohm=4294967295 cfg=0x") != NULL);
    CHECK(strstr(g_log, ",-4096\r\n") != NULL);
    parse();
    CHECK_EQ(seen.n_bad, 0u);          /* none past SENSE_TRACE_LINE_MAX */
    CHECK(!g_partial);
}

TEST_CASE(a_clipped_sample_reads_the_end_of_the_range)
{
    rig();
    i3221->amps[0] = 5.0;
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    i3221->amps[0] = -5.0;
    run(100u);
    sense_trace_key(&tr, 'x', g_us);
    run(2u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.code[0], 4095);
    CHECK_EQ(seen.code[seen.n_s - 1u], -4096);
    CHECK_EQ(seen.z_s, seen.n_s);
    /* The last values before the ends are values: 4094 steps of 0.4 mA,
     * and 4095 the other way. */
    g_len = 0u;
    i3221->amps[0] = 1.6376;
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    i3221->amps[0] = -1.638;
    run(100u);
    /* And across a window's close, a clipped sample again. */
    i3221->amps[0] = 5.0;
    run(120u);
    sense_trace_key(&tr, 'x', g_us);
    run(3u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.code[0], 4094);
    CHECK_EQ(seen.code[seen.n_s - 130u], -4095);
    for (unsigned k = seen.n_s - 120u; k < seen.n_s; ++k) {
        if (seen.code[k] != 4095) {
            T_FAIL("sample %u reads %ld", k, (long)seen.code[k]);
            break;
        }
    }
}

TEST_CASE(a_clipped_sample_whose_window_closed_in_its_tick_is_clipped)
{
    rig();
    i3221->amps[0] = 5.0;
    for (unsigned k = 0; k < 120u; ++k) {
        tick();
    }
    /* By hand, a tick in which CH1 read a clipped sample and a later
     * read crossed the window's end: the sample is in the history, its
     * count in the ring's newest window, and the window being filled is
     * empty. */
    sense_sched_t *sc = &svc.sched;
    const uint16_t before = (uint16_t)(sc->acc[SENSE_SRC_CH1].n_hi
                                       + sc->acc[SENSE_SRC_CH1].n_lo);
    unsigned held = held_data();
    sc->ring_at = sc->win;
    sc->ring[0].n_clip = (uint16_t)(before + 1u);
    sc->win += 1u;
    memset(&sc->acc[SENSE_SRC_CH1], 0, sizeof(sc->acc[SENSE_SRC_CH1]));
    sc->ch1[sc->ch1_head].t  = (uint32_t)(g_us / 100u);
    sc->ch1[sc->ch1_head].ua = sc->ch_clip_ua;
    sc->ch1_head = (uint8_t)((sc->ch1_head + 1u) % SENSE_CH1_HISTORY);
    sense_trace_feed(&tr, sc, g_us);
    CHECK_EQ(rec(held)->meta >> 24, SENSE_TRACE_CLIP_HI);
    /* The same at the bottom of the range. */
    g_us += 1000u;
    held = held_data();
    sc->ring_at = sc->win;
    sc->ring[0].n_clip = 1u;
    sc->win += 1u;
    sc->ch1[sc->ch1_head].t  = (uint32_t)(g_us / 100u);
    sc->ch1[sc->ch1_head].ua = -sc->ch_clip_ua;
    sc->ch1_head = (uint8_t)((sc->ch1_head + 1u) % SENSE_CH1_HISTORY);
    sense_trace_feed(&tr, sc, g_us);
    CHECK_EQ(rec(held)->meta >> 24, SENSE_TRACE_CLIP_LO);
    /* A window that closed with no more clips than were seen: a value. */
    g_us += 1000u;
    held = held_data();
    sc->ring_at = sc->win;
    sc->ring[0].n_clip = 0u;
    sc->win += 1u;
    sc->ch1[sc->ch1_head].t  = (uint32_t)(g_us / 100u);
    sc->ch1[sc->ch1_head].ua = sc->ch_clip_ua;
    sc->ch1_head = (uint8_t)((sc->ch1_head + 1u) % SENSE_CH1_HISTORY);
    sense_trace_feed(&tr, sc, g_us);
    CHECK_EQ(rec(held)->meta >> 24, SENSE_TRACE_CURRENT);
}

TEST_CASE(trigger_lines_are_written_in_the_order_of_their_times)
{
    rig();
    run(200u);
    const uint32_t t0 = (uint32_t)(g_us / 100u);
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    /* Two slots change in one pass; the slot looked at first has the
     * later frame.  A third trigger lies between them. */
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us + 18000u, 1u, 1100u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us + 2000u, 2u, 1900u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us + 9000u, 0u, 0u);
    run(100u);
    sense_trace_key(&tr, 'x', g_us);
    run(3u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.mark_t[0], t0);
    CHECK_EQ(seen.mark_t[1], t0 + 1000u + 20u);
    CHECK_EQ(seen.mark_t[2], t0 + 1000u + 90u);
    CHECK_EQ(seen.mark_t[3], t0 + 1000u + 180u);
    CHECK_EQ(seen.mark_ch, 1u);
    CHECK_EQ(seen.z_m, 4u);
    /* Each behind the samples of before its time: 2 ms, 9 ms and 18 ms
     * of them. */
    const char *c2 = strstr(g_log, " ch=2 us=1900\r\n");
    const char *e = strstr(g_log, "$E t=");
    const char *c1 = strstr(g_log, " ch=1 us=1100\r\n");
    CHECK(c2 != NULL && e != NULL && c1 != NULL && c2 < e && e < c1);
    unsigned between = 0u;
    for (const char *at = c2; at < c1; ++at) {
        between += (*at == ',');
    }
    CHECK_EQ(between, 16u);
}

/* Core 1 does nothing for @p ms; core 0 passes. */
static void parked(unsigned ms)
{
    for (unsigned k = 0; k < ms; ++k) {
        g_us += 1000u;
        pass();
    }
}

TEST_CASE(a_record_that_comes_late_is_still_its_traces)
{
    rig();
    run(200u);
    const uint32_t t0 = (uint32_t)(g_us / 100u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us, 0u, 0u);
    run(3999u);
    /* The trace's last tick: core 1 reads its sample 1 ms before the end
     * and is parked before it feeds the trace.  Core 0 finds the ring
     * empty for 30 ms past the end. */
    const uint64_t tick_us = g_us;
    sense_svc_step(&svc, &cmd, &snap);
    parked(31u);
    CHECK(sense_trace_active(&tr));
    CHECK(strstr(g_log, "$Z ") == NULL);
    /* The feed, with the time its tick started at: the record is the
     * trace's, and the trace still open, core 1 not being past the end. */
    sense_trace_feed(&tr, &svc.sched, tick_us);
    pass();
    CHECK(sense_trace_active(&tr));
    /* The next tick starts past the end. */
    run(1u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.z_e, 't');
    CHECK_EQ(seen.z_s, seen.n_s);
    CHECK_EQ(seen.t[seen.n_s - 1u], t0 + 40000u - 10u);
    const unsigned pre = (unsigned)(-seen.first_dt) / 10u;
    CHECK_EQ(seen.n_s, pre + 4000u);
}

TEST_CASE(a_core_that_stays_away_ends_the_trace_and_the_end_line_says_so)
{
    rig();
    run(200u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us, 0u, 0u);
    run(3990u);
    /* 10 ms before the end core 1 stops for good.  The end line waits
     * SENSE_TRACE_STALL_MS past the end, to the pass. */
    parked(10u + SENSE_TRACE_STALL_MS - 1u);
    CHECK(sense_trace_active(&tr));
    parked(1u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_e, 'T');
    CHECK_EQ(seen.z_s, seen.n_s);
    /* The console's stop with core 1 away: the same wait, from the
     * stop. */
    g_len = 0u;
    sense_trace_key(&tr, 't', g_us);
    parked(50u);
    sense_trace_key(&tr, 'x', g_us);
    parked(SENSE_TRACE_STALL_MS - 1u);
    CHECK(sense_trace_active(&tr));
    parked(1u);
    CHECK(!sense_trace_active(&tr));
    CHECK(strstr(g_log, " e=K\r\n") != NULL);
    /* Core 1 back: the next trace's end is its own again. */
    g_len = 0u;
    run(100u);
    sense_trace_key(&tr, 't', g_us);
    run(50u);
    sense_trace_key(&tr, 'x', g_us);
    run(1u);
    CHECK(!sense_trace_active(&tr));
    CHECK(strstr(g_log, " e=k\r\n") != NULL);
}

TEST_CASE(a_trigger_line_waits_for_core_1_and_says_when_it_did_not_come)
{
    rig();
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    /* A command whose frame starts 5 ms on; core 1 is parked 1 ms before
     * that, its sample of then read and not yet fed. */
    const uint32_t mark_t = (uint32_t)((g_us + 5000u) / 100u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us + 5000u, 3u, 1700u);
    run(4u);
    const uint64_t tick_us = g_us;
    sense_svc_step(&svc, &cmd, &snap);
    parked(30u);
    /* The line is not written past a sample still to come. */
    CHECK(strstr(g_log, "$C ") == NULL);
    sense_trace_feed(&tr, &svc.sched, tick_us);
    run(2u);
    const char *c = strstr(g_log, "$C ");
    CHECK(c != NULL);
    sense_trace_key(&tr, 'x', g_us);
    run(1u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.z_e, 'k');
    CHECK_EQ(seen.mark_t[1], mark_t);
    /* Every sample line above the command's is of before its time. */
    unsigned above = 0u;
    for (const char *at = g_log; c != NULL && at < c; ++at) {
        above += (*at == ',');
    }
    CHECK(above >= 1u);
    CHECK((int32_t)(seen.t[above - 1u] - mark_t) < 0);
    CHECK((int32_t)(seen.t[above] - mark_t) >= 0);

    /* Core 1 away for good: the line is written SENSE_TRACE_STALL_MS
     * after its time, and the end line says so although core 1 is back
     * for the end. */
    g_len = 0u;
    run(100u);
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us + 5000u, 3u, 1300u);
    parked(5u + SENSE_TRACE_STALL_MS - 1u);
    CHECK(strstr(g_log, "$C ") == NULL);
    parked(1u);
    CHECK(strstr(g_log, "$C ") != NULL);
    run(100u);
    sense_trace_key(&tr, 'x', g_us);
    run(1u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.z_e, 'K');
    CHECK_EQ(seen.z_m, 2u);
}

/* --------------------------- records dropped with none kept after them */

/* Core 1's tick with no sample in it: the trace is fed the tick's time
 * and no record. */
static void tick_empty(void)
{
    sense_trace_feed(&tr, &svc.sched, g_us);
    g_us += 1000u;
}

/* Core 0 alone, at one time, until it has nothing more to write. */
static void drain(void)
{
    for (unsigned k = 0; k < 40u; ++k) {
        pass();
    }
}

/* With the ring read out, every record core 1 dropped is counted once:
 * by the records read, or by the $L line written ahead of the one to
 * come. */
static void check_drops_accounted(void)
{
    CHECK_EQ(sense_trace_held(&tr), 0u);
    CHECK_EQ((uint32_t)atomic_load(&tr.drops), tr.src.dropped);
    CHECK_EQ(tr.out.drops_seen + tr.out.ahead, tr.src.dropped);
    CHECK_EQ(tr.out.ahead, tr.src.lost);
}

/* On a ring of 8: an edge's trace whose last 10 ticks before its end
 * find no room.  Returns the records dropped. */
static uint32_t lose_before_the_end(void)
{
    run(20u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us, 0u, 0u);
    run(3990u);
    CHECK_EQ(tr.src.lost, 0u);
    stall(10u);
    CHECK_EQ(sense_trace_held(&tr), 8u);
    CHECK(tr.src.lost >= 2u);
    return tr.src.lost;
}

/* The trace after: a key's, stopped, with no record missing. */
static void check_the_next_trace_counts_none(void)
{
    const size_t len = g_len;
    run(5u);
    sense_trace_key(&tr, 't', g_us);
    run(20u);
    sense_trace_key(&tr, 'x', g_us);
    run(3u);
    CHECK(!sense_trace_active(&tr));
    const char *next = &g_log[len];
    CHECK(strstr(next, "$T ") != NULL);
    CHECK(strstr(next, "$L ") == NULL);
    CHECK(strstr(next, " l=0 m=1 ml=0 e=k\r\n") != NULL);
    CHECK_EQ(tr.out.ahead, 0u);
    CHECK_EQ(tr.out.drops_seen, tr.src.dropped);
}

TEST_CASE(records_dropped_before_the_end_with_none_kept_since_are_in_the_end_line)
{
    /* The count from 0, and across its wrap at 2^32. */
    for (unsigned wrap = 0u; wrap < 2u; ++wrap) {
        rig_size(8u);
        if (wrap != 0u) {
            tr.src.dropped    = UINT32_MAX - 1u;
            tr.out.drops_seen = UINT32_MAX - 1u;
            atomic_store(&tr.drops, UINT32_MAX - 1u);
        }
        const uint32_t lost = lose_before_the_end();
        /* The first tick that starts at the end has no sample: core 1 is
         * past the end, and the count rides on no record. */
        tick_empty();
        CHECK_EQ(tr.src.lost, lost);
        drain();
        CHECK(!sense_trace_active(&tr));
        CHECK_EQ(sense_trace_held(&tr), 0u);
        parse();
        CHECK_EQ(seen.n_bad, 0u);
        CHECK_EQ(seen.n_z, 1u);
        CHECK_EQ(seen.z_e, 't');
        CHECK_EQ(seen.n_l, 1u);
        CHECK_EQ(seen.lost, lost);
        CHECK_EQ(seen.z_l, lost);
        CHECK_EQ(seen.z_s, seen.n_s);
        /* The $L line stands behind the last sample, before the end
         * line. */
        const char *l_at = strstr(g_log, "$L n=");
        const char *z_at = strstr(g_log, "$Z n=");
        CHECK(l_at != NULL && z_at != NULL && l_at < z_at);
        CHECK(strchr(l_at, ',') == NULL);
        CHECK_EQ(tr.out.ahead, lost);
        check_drops_accounted();
        /* The record that carries the count comes later: it is not said
         * again, in no trace. */
        check_the_next_trace_counts_none();
        parse();
        CHECK_EQ(seen.n_l, 1u);
        if (wrap != 0u) {
            CHECK(tr.src.dropped < 100u);
        }
    }
}

/* On a ring of 8 with a trace under way and the console taking nothing:
 * the ticks up to and with the first that drops a record. */
static unsigned ticks_to_the_first_drop(void)
{
    unsigned n = 0u;
    while (tr.src.lost == 0u && n < 100u) {
        tick();
        pass_as(0u, true);
        ++n;
    }
    return n;
}

TEST_CASE(a_record_dropped_in_the_last_tick_before_the_end_and_in_the_first_at_it)
{
    rig_size(8u);
    run(20u);
    sense_trace_key(&tr, 't', g_us);
    run(20u);
    const unsigned n = ticks_to_the_first_drop();
    CHECK(n >= 4u && n < 100u);
    /* The same again, with a trace that ends @p past ms after the start
     * of the tick that drops: 1, the last tick before the end; 0, the
     * first tick at it. */
    for (unsigned past = 0u; past < 2u; ++past) {
        rig_size(8u);
        run(20u);
        const uint64_t drop_us = g_us + 20000u + (n - 1u) * 1000u;
        sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE,
                            drop_us + past * 1000u
                                - SENSE_TRACE_EDGE_MS * 1000u, 0u, 0u);
        run(20u);
        CHECK_EQ(ticks_to_the_first_drop(), n);
        CHECK_EQ(g_us, drop_us + 1000u);
        const uint32_t lost = tr.src.lost;
        CHECK(lost >= 1u && lost <= 2u);
        drain();
        parse();
        CHECK_EQ(seen.n_bad, 0u);
        CHECK_EQ(seen.n_l, 1u);
        CHECK_EQ(seen.lost, lost);
        if (past != 0u) {
            /* Core 1 is not past the end: the $L line is written, the
             * end line waits. */
            CHECK(sense_trace_active(&tr));
            CHECK_EQ(seen.n_z, 0u);
            tick_empty();
            drain();
            parse();
        }
        /* At the end the dropped record is not of before it; the end
         * line counts it all the same, and the next trace does not. */
        CHECK(!sense_trace_active(&tr));
        CHECK_EQ(seen.n_z, 1u);
        CHECK_EQ(seen.z_e, 't');
        CHECK_EQ(seen.n_l, 1u);
        CHECK_EQ(seen.z_l, lost);
        CHECK_EQ(seen.z_s, seen.n_s);
        check_drops_accounted();
        check_the_next_trace_counts_none();
    }
}

TEST_CASE(records_dropped_in_a_traces_first_tick_are_said_once)
{
    /* The ring full of samples from before the trigger, and the tick the
     * trigger falls in drops its sample. */
    fill(8u);
    CHECK_EQ(tr.src.lost, 0u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_KEY, g_us + 500u, 0u, 0u);
    sense_svc_step(&svc, &cmd, &snap);
    svc.sched.acc[SENSE_SRC_CH1].n_v = 0u;
    tr.src.n_v = 0u;
    sense_trace_feed(&tr, &svc.sched, g_us);
    g_us += 1000u;
    CHECK_EQ(tr.src.lost, 1u);
    drain();
    /* The 8, then the line for the one dropped; the trigger's line
     * waits for core 1 to pass its time. */
    CHECK(strstr(g_log, "10,300\r\n$L n=1\r\n") != NULL);
    CHECK(strstr(g_log, "$K t=") == NULL);
    run(5u);
    sense_trace_key(&tr, 'x', g_us);
    run(3u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_k, 1u);
    CHECK_EQ(seen.n_l, 1u);
    CHECK_EQ(seen.z_l, 1u);
    CHECK_EQ(seen.n_s, 8u + 5u);
    CHECK_EQ(seen.z_s, seen.n_s);
    CHECK_EQ(tr.out.drops_seen, 1u);
    CHECK_EQ(tr.out.ahead, 0u);
}

TEST_CASE(records_dropped_before_a_second_trigger_are_said_once_in_the_longer_trace)
{
    rig_size(8u);
    const uint32_t lost = lose_before_the_end();
    drain();
    /* Core 1 is not past the end: the line is written, the trace open. */
    CHECK(sense_trace_active(&tr));
    CHECK(strstr(g_log, "$L n=") != NULL);
    CHECK_EQ(tr.out.ahead, lost);
    /* A second trigger 1 ms before the end moves the end; the next
     * record kept carries the count that is said already. */
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us - 1000u, 0u, 0u);
    run(50u);
    CHECK(sense_trace_active(&tr));
    CHECK_EQ(tr.out.ahead, 0u);
    /* A second gap in the same trace has a line of its own, with the
     * record the first tick after it drops before core 0's pass. */
    stall(12u);
    CHECK(tr.src.lost >= 2u);
    run(50u);
    const uint32_t again = tr.src.dropped - lost;
    CHECK(again >= 3u);
    sense_trace_key(&tr, 'x', g_us);
    run(3u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.n_e, 2u);
    CHECK_EQ(seen.n_l, 2u);
    CHECK_EQ(seen.lost, lost + again);
    CHECK_EQ(seen.z_l, lost + again);
    CHECK_EQ(seen.z_s, seen.n_s);
    CHECK_EQ(tr.out.drops_seen, tr.src.dropped);
    CHECK_EQ(tr.src.dropped, lost + again);
}

TEST_CASE(records_dropped_before_a_stop_and_a_stall_are_in_the_end_line)
{
    /* The console's stop, core 1 past it with no record. */
    rig_size(8u);
    run(20u);
    sense_trace_key(&tr, 't', g_us);
    run(20u);
    stall(30u);
    uint32_t lost = tr.src.lost;
    CHECK(lost > 20u);
    sense_trace_key(&tr, 'x', g_us);
    tick_empty();
    drain();
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.z_e, 'k');
    CHECK_EQ(seen.n_l, 1u);
    CHECK_EQ(seen.z_l, lost);
    CHECK_EQ(seen.z_s, seen.n_s);
    check_drops_accounted();
    check_the_next_trace_counts_none();
    /* Core 1 away from before the end: the end line comes
     * SENSE_TRACE_STALL_MS after it, with the count. */
    rig_size(8u);
    lost = lose_before_the_end();
    drain();
    CHECK(strstr(g_log, "$L n=") != NULL);
    parked(SENSE_TRACE_STALL_MS - 1u);
    CHECK(sense_trace_active(&tr));
    parked(1u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.z_e, 'T');
    CHECK_EQ(seen.n_l, 1u);
    CHECK_EQ(seen.z_l, lost);
    CHECK_EQ(seen.z_s, seen.n_s);
    check_drops_accounted();
    check_the_next_trace_counts_none();
}

TEST_CASE(a_set_up_record_that_found_no_room_before_the_end_is_counted_there)
{
    rig_size(8u);
    run(20u);
    sense_trace_key(&tr, 't', g_us);
    run(20u);
    stall(20u);
    CHECK_EQ(sense_trace_held(&tr), 8u);
    /* The set-up changes with the ring full: its record waits on core 1,
     * counted with each tick it finds no room in. */
    ++cmd.cfg_gen;
    cmd.parts.ina3221_shunt_uohm = 50000u;
    i3221->shunt_ohm = 0.05;
    for (unsigned k = 0u; k < 50u && !tr.src.shunt_owed; ++k) {
        tick();
    }
    CHECK(tr.src.shunt_owed);
    stall(5u);
    CHECK(tr.src.shunt_owed);
    /* The stop, and core 1 past it with the record still not in. */
    sense_trace_key(&tr, 'x', g_us);
    tick_empty();
    CHECK(tr.src.shunt_owed);
    const uint32_t lost = tr.src.lost;
    drain();
    CHECK(!sense_trace_active(&tr));
    CHECK_EQ(held_of(SENSE_TRACE_SHUNT), 0u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_e, 'k');
    CHECK_EQ(seen.n_l, 1u);
    CHECK_EQ(seen.lost, lost);
    CHECK_EQ(seen.z_l, lost);
    CHECK_EQ(seen.shunt, I3221_UOHM);
    check_drops_accounted();
    /* The record comes with the next tick; the next trace has its
     * set-up and counts none missing. */
    const size_t len = g_len;
    run(30u);
    CHECK(!tr.src.shunt_owed && !tr.src.cfg_owed);
    sense_trace_key(&tr, 't', g_us);
    run(20u);
    sense_trace_key(&tr, 'x', g_us);
    run(3u);
    CHECK(strstr(&g_log[len], "$L ") == NULL);
    CHECK(strstr(&g_log[len], " shunt_uohm=50000 ") != NULL);
    CHECK(strstr(&g_log[len], " l=0 ") != NULL);
    CHECK_EQ(tr.out.drops_seen, tr.src.dropped);
}

TEST_CASE(records_dropped_in_a_trace_that_is_dropped_are_said_where_they_are_missing)
{
    /* No console, and a console that took nothing for 29.8 h. */
    for (unsigned how = 0u; how < 2u; ++how) {
        rig_size(8u);
        run(20u);
        sense_trace_key(&tr, 't', g_us);
        run(20u);
        stall(30u);
        const uint32_t lost = tr.src.lost;
        CHECK(lost > 20u);
        if (how == 0u) {
            pass_as(SENSE_TRACE_LINE_MAX, false);
        } else {
            g_us += ((uint64_t)SENSE_TRACE_OWED_MAX_T + 100000u) * 100u;
            pass();
        }
        CHECK(!sense_trace_active(&tr));
        CHECK_EQ(tr.out.abandoned, 1u);
        CHECK(strstr(g_log, "$Z ") == NULL);
        /* The next record kept carries the count, and is one of those
         * from before the next trigger. */
        g_len = 0u;
        tick();
        CHECK_EQ(tr.src.lost, 0u);
        sense_trace_key(&tr, 't', g_us);
        run(20u);
        sense_trace_key(&tr, 'x', g_us);
        run(3u);
        CHECK(!sense_trace_active(&tr));
        parse();
        CHECK_EQ(seen.n_bad, 0u);
        CHECK_EQ(seen.n_t, 1u);
        CHECK_EQ(seen.n_l, 1u);
        CHECK_EQ(seen.lost, lost);
        CHECK_EQ(seen.z_l, lost);
        CHECK_EQ(seen.z_s, seen.n_s);
        CHECK(strstr(g_log, "$L n=") < strstr(g_log, "$K t="));
        CHECK_EQ(tr.out.drops_seen, tr.src.dropped);
        CHECK_EQ(tr.out.ahead, 0u);
    }
}

TEST_CASE(a_count_said_in_a_trace_that_is_dropped_is_said_again_in_the_next)
{
    /* Said with no record kept since, and said before its record with
     * the record's own line not written. */
    for (unsigned how = 0u; how < 2u; ++how) {
        rig_size(8u);
        const uint32_t lost = lose_before_the_end();
        if (how == 0u) {
            drain();
            CHECK_EQ(tr.out.ahead, lost);
        } else {
            /* Room for one line a pass, a record kept behind the 8, and
             * the passes up to its $L line. */
            pass_as(9u, true);
            tick();
            CHECK_EQ(tr.src.lost, 0u);
            for (unsigned k = 0u; k < 20u && !tr.out.lost_shown; ++k) {
                pass_as(9u, true);
            }
            CHECK(tr.out.lost_shown);
            CHECK_EQ(sense_trace_held(&tr), 1u);
        }
        CHECK(sense_trace_active(&tr));
        CHECK(strstr(g_log, "$L n=") != NULL);
        /* The console goes: the trace has no end line. */
        pass_as(SENSE_TRACE_LINE_MAX, false);
        CHECK(!sense_trace_active(&tr));
        CHECK_EQ(tr.out.abandoned, 1u);
        g_len = 0u;
        if (how == 0u) {
            tick();
        }
        CHECK_EQ(sense_trace_held(&tr), 1u);
        /* The record the count rides on is the next trace's first. */
        sense_trace_key(&tr, 't', g_us);
        run(20u);
        sense_trace_key(&tr, 'x', g_us);
        run(3u);
        CHECK(!sense_trace_active(&tr));
        parse();
        CHECK_EQ(seen.n_bad, 0u);
        CHECK_EQ(seen.n_z, 1u);
        CHECK_EQ(seen.n_l, 1u);
        CHECK_EQ(seen.lost, lost);
        CHECK_EQ(seen.z_l, lost);
        CHECK_EQ(seen.z_s, seen.n_s);
        CHECK(strstr(g_log, "$L n=") < strchr(g_log, ','));
        CHECK_EQ(tr.out.drops_seen, tr.src.dropped);
        CHECK_EQ(tr.out.ahead, 0u);
    }
}

TEST_CASE(a_stop_drops_the_trigger_lines_past_it)
{
    /* With the console keeping up, and with 100 ms of samples waiting. */
    for (unsigned backlog = 0u; backlog < 2u; ++backlog) {
        rig();
        run(200u);
        sense_trace_key(&tr, 't', g_us);
        run(100u);
        if (backlog != 0u) {
            stall(100u);
        }
        /* A command whose frame starts 18 ms on, and the stop before
         * it. */
        sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us + 18000u, 1u,
                            1100u);
        sense_trace_key(&tr, 'x', g_us);
        run(300u);
        /* The trace ends without the command's line, and no trace starts
         * from it. */
        CHECK(!sense_trace_active(&tr));
        parse();
        CHECK_EQ(seen.n_bad, 0u);
        CHECK_EQ(seen.n_t, 1u);
        CHECK_EQ(seen.n_z, 1u);
        CHECK_EQ(seen.n_c, 0u);
        CHECK_EQ(seen.z_m, 1u);
        CHECK_EQ(seen.z_ml, 0u);
        CHECK_EQ(seen.z_e, 'k');
        CHECK_EQ(seen.z_s, seen.n_s);
    }
}

TEST_CASE(a_stop_before_a_traces_own_trigger_keeps_its_line)
{
    rig();
    run(200u);
    /* A command starts the trace; its frame starts 15 ms on.  The stop
     * comes at once: the end is just past the frame's start. */
    const uint32_t t0 = (uint32_t)((g_us + 15000u) / 100u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us + 15000u, 2u, 1900u);
    sense_trace_key(&tr, 'x', g_us);
    run(14u);
    CHECK(sense_trace_active(&tr));
    run(3u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_t, 1u);
    CHECK_EQ(seen.n_c, 1u);
    CHECK_EQ(seen.mark_t[0], t0);
    CHECK_EQ(seen.z_m, 1u);
    CHECK_EQ(seen.z_e, 'k');
    /* The sample at the frame's start is the last. */
    CHECK_EQ(seen.t[seen.n_s - 1u], t0);
    /* A stop at the very time of the console's start. */
    g_len = 0u;
    run(100u);
    sense_trace_key(&tr, 't', g_us);
    sense_trace_key(&tr, 'x', g_us);
    run(200u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_t, 1u);
    CHECK_EQ(seen.n_k, 1u);
    CHECK_EQ(seen.z_m, 1u);
    CHECK_EQ(seen.z_e, 'k');
}

TEST_CASE(the_code_is_the_one_the_current_came_from)
{
    static const uint32_t k_shunt[] = { 200u, 1000u, 10000u, 30000u, 50000u,
                                        100000u, 330000u, 1000000u,
                                        20000000u };
    for (unsigned k = 0u; k < sizeof(k_shunt) / sizeof(k_shunt[0]); ++k) {
        for (int32_t code = -4095; code <= 4094; ++code) {
            const sense_value_t ua = ina3221_current_ua(k_shunt[k], code);
            if (sense_trace_code(ua.value, k_shunt[k]) != code) {
                T_FAIL("shunt %lu code %ld comes back as %ld",
                       (unsigned long)k_shunt[k], (long)code,
                       (long)sense_trace_code(ua.value, k_shunt[k]));
                break;
            }
        }
    }
    CHECK_EQ(sense_trace_code(120000, 0u), 0);
}

int main(void)
{
    RUN(a_record_that_comes_late_is_still_its_traces);
    RUN(a_core_that_stays_away_ends_the_trace_and_the_end_line_says_so);
    RUN(a_trigger_line_waits_for_core_1_and_says_when_it_did_not_come);
    RUN(records_dropped_before_the_end_with_none_kept_since_are_in_the_end_line);
    RUN(a_record_dropped_in_the_last_tick_before_the_end_and_in_the_first_at_it);
    RUN(records_dropped_in_a_traces_first_tick_are_said_once);
    RUN(records_dropped_before_a_second_trigger_are_said_once_in_the_longer_trace);
    RUN(records_dropped_before_a_stop_and_a_stall_are_in_the_end_line);
    RUN(a_set_up_record_that_found_no_room_before_the_end_is_counted_there);
    RUN(records_dropped_in_a_trace_that_is_dropped_are_said_where_they_are_missing);
    RUN(a_count_said_in_a_trace_that_is_dropped_is_said_again_in_the_next);
    RUN(a_stop_drops_the_trigger_lines_past_it);
    RUN(a_stop_before_a_traces_own_trigger_keeps_its_line);
    RUN(a_pulse_is_the_one_the_frame_can_carry);
    RUN(every_line_at_its_longest_fits);
    RUN(a_clipped_sample_reads_the_end_of_the_range);
    RUN(a_clipped_sample_whose_window_closed_in_its_tick_is_clipped);
    RUN(trigger_lines_are_written_in_the_order_of_their_times);
    RUN(the_code_is_the_one_the_current_came_from);
    return test_summary("sense_trace_lines");
}
