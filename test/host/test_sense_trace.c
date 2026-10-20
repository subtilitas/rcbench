/*
 * The trace of CH1's 1 ms samples (sense_trace.h), core 0's triggers and
 * the console, over sense_trace_rig.h: the newest SENSE_TRACE_PRE records
 * kept while idle; a trace from each trigger with its header, its records
 * from before the trigger and its length; a trigger during a trace adding
 * its line and moving the end, never back; the watch on a slot's pulse at
 * SENSE_TRACE_HOLD_MS and one step either side; a trace the console never
 * takes, dropped before its times wrap; a slewed command and the $D line
 * that ends it; traces across the wrap of the millisecond tick and of the
 * 0.1 ms count; whole lines only, whatever room the console has; a console
 * with no room for seconds, inside a trace and across its end, and one not
 * connected.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_trace_rig.h"

/* ------------------------------------------------------------- core 0 */

TEST_CASE(idle_keeps_the_newest_records)
{
    rig();
    run(1000u);
    CHECK_EQ(g_len, 0u);
    CHECK_EQ(sense_trace_held(&tr), SENSE_TRACE_PRE);
    const uint32_t tail = (uint32_t)atomic_load(&tr.tail);
    const uint32_t head = (uint32_t)atomic_load(&tr.head);
    CHECK_EQ(g_ring[(head - 1u) & tr.mask].t, 200000u + 9990u);
    CHECK(g_ring[tail & tr.mask].t >= 200000u + 9990u - 630u);
    /* A ring smaller than twice that keeps half of itself. */
    rig_size(16u);
    run(100u);
    CHECK_EQ(sense_trace_held(&tr), 8u);
}

TEST_CASE(the_console_command_gives_ten_seconds)
{
    rig();
    run(500u);
    sense_trace_key(&tr, 't', g_us);
    CHECK(sense_trace_active(&tr));
    const uint32_t t0 = (uint32_t)(g_us / 100u);
    run(10100u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK(!g_partial);
    CHECK_EQ(seen.n_t, 1u);
    CHECK_EQ(seen.n_h, 1u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.n_k, 1u);
    CHECK_EQ(seen.ver, SENSE_TRACE_FORMAT);
    CHECK_EQ(seen.id, 1u);
    CHECK_STR_EQ(seen.trig, "key");
    CHECK_EQ(seen.t0, t0);
    CHECK_EQ(seen.ms0, t0 / 10u);
    CHECK_EQ(seen.len, SENSE_TRACE_KEY_MS);
    CHECK_EQ(seen.shunt, I3221_UOHM);
    CHECK_EQ(seen.cfg, svc.sched.i3221[0].config);
    CHECK_EQ(seen.on, 1u);
    CHECK_EQ(seen.rst, 0u);
    CHECK_EQ(seen.mark_t[0], t0);
    /* The samples from before the trigger, then 10 s of them. */
    CHECK(seen.first_dt < 0 && seen.first_dt >= -640);
    const unsigned pre = (unsigned)(-seen.first_dt) / 10u;
    CHECK_EQ(seen.n_s, pre + 10000u);
    CHECK_EQ(seen.t[0], t0 + (uint32_t)seen.first_dt);
    for (unsigned k = 1u; k < seen.n_s; ++k) {
        if (seen.t[k] - seen.t[k - 1u] != 10u || seen.code[k] != 300) {
            T_FAIL("sample %u at %lu reads %ld", k, (unsigned long)seen.t[k],
                   (long)seen.code[k]);
            break;
        }
    }
    CHECK_EQ(seen.t[seen.n_s - 1u], t0 + 100000u - 10u);
    CHECK_EQ(seen.mv_last, 6000);
    CHECK_EQ(seen.z_id, 1u);
    CHECK_EQ(seen.z_s, seen.n_s);
    CHECK_EQ(seen.z_v, seen.n_v);
    CHECK(seen.n_v >= 416u && seen.n_v <= 420u);   /* every 24 ms */
    CHECK_EQ(seen.z_l, 0u);
    CHECK_EQ(seen.z_m, 1u);
    CHECK_EQ(seen.z_ml, 0u);
    CHECK_EQ(seen.z_e, 't');
    /* The lines as written. */
    char want[160];
    snprintf(want, sizeof(want),
             "$T v=3 n=1 trig=key t=%lu ms=%lu len=10000\r\n"
             "$H dt_us=1000 shunt_uohm=100000 cfg=0x%04X on=1 rst=0\r\n",
             (unsigned long)t0, (unsigned long)(t0 / 10u),
             (unsigned)svc.sched.i3221[0].config);
    CHECK(strncmp(g_log, want, strlen(want)) == 0);
    /* Idle again: the newest records are kept for the next one. */
    run(200u);
    CHECK_EQ(sense_trace_held(&tr), SENSE_TRACE_PRE);
}

TEST_CASE(a_command_and_an_edge_give_four_seconds)
{
    rig();
    run(300u);
    const uint32_t t0 = (uint32_t)(g_us / 100u);
    /* The frame that carries the pulse starts 12.3 ms ahead. */
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us + 12300u, 3u, 1900u);
    run(4100u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_STR_EQ(seen.trig, "cmd");
    CHECK_EQ(seen.t0, t0 + 123u);
    CHECK_EQ(seen.len, SENSE_TRACE_EDGE_MS);
    CHECK_EQ(seen.n_c, 1u);
    CHECK_EQ(seen.mark_ch, 3u);
    CHECK_EQ(seen.mark_us, 1900u);
    CHECK_EQ(seen.mark_t[0], t0 + 123u);
    /* The last sample is the last before the end: 4000 ms after the
     * frame's start. */
    CHECK(seen.t[seen.n_s - 1u] < t0 + 123u + 40000u);
    CHECK(seen.t[seen.n_s - 1u] >= t0 + 123u + 40000u - 10u);
    CHECK_EQ(seen.z_s, seen.n_s);
    CHECK_EQ(seen.z_e, 't');

    g_len = 0u;
    const uint32_t t1 = (uint32_t)(g_us / 100u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us, 0u, 0u);
    run(4100u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.id, 2u);
    CHECK_STR_EQ(seen.trig, "edge");
    CHECK_EQ(seen.n_e, 1u);
    CHECK_EQ(seen.t[seen.n_s - 1u], t1 + 40000u - 10u);
    CHECK_EQ(seen.z_id, 2u);
    CHECK_EQ(seen.z_s, seen.n_s);
}

TEST_CASE(a_trigger_during_a_trace_moves_its_end_and_never_back)
{
    rig();
    run(300u);
    const uint32_t t0 = (uint32_t)(g_us / 100u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 0u, 1100u);
    run(3999u);
    CHECK(sense_trace_active(&tr));
    /* 1 ms before its end: a second command, and 4 s more. */
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 0u, 1900u);
    run(3999u);
    CHECK(sense_trace_active(&tr));
    run(10u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_t, 1u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.n_c, 2u);
    CHECK_EQ(seen.mark_t[1], t0 + 39990u);
    CHECK_EQ(seen.t[seen.n_s - 1u], t0 + 39990u + 40000u - 10u);
    CHECK_EQ(seen.z_m, 2u);
    CHECK_EQ(seen.z_s, seen.n_s);

    /* A 10 s trace is not cut short by a command 1 s into it. */
    g_len = 0u;
    const uint32_t t1 = (uint32_t)(g_us / 100u);
    sense_trace_key(&tr, 'T', g_us);
    run(1000u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 0u, 1100u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us + 2000u, 0u, 0u);
    run(9100u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.n_k, 1u);
    CHECK_EQ(seen.n_c, 1u);
    CHECK_EQ(seen.n_e, 1u);
    CHECK_EQ(seen.mark_t[2], t1 + 10000u + 20u);
    CHECK_EQ(seen.t[seen.n_s - 1u], t1 + 100000u - 10u);
    CHECK_EQ(seen.z_m, 3u);
}

TEST_CASE(a_slots_pulse_is_a_command_after_it_held_still)
{
    rig();
    uint64_t us = g_us;
    /* The first pulse seen is where the watch starts. */
    CHECK(!sense_trace_pulse(&tr, 2u, 5u, 1500u, us));
    CHECK(!sense_trace_pulse(&tr, 2u, 5u, 1500u, us + 1000u));
    /* The first change is a command, whenever it comes. */
    us += 2000u;
    CHECK(sense_trace_pulse(&tr, 2u, 5u, 1900u, us));
    /* 49.9 ms after it: the same command still slewing. */
    CHECK(!sense_trace_pulse(&tr, 2u, 5u, 1890u, us + 49900u));
    /* 50.0 ms after that one: a command of its own. */
    us += 49900u;
    CHECK(sense_trace_pulse(&tr, 2u, 5u, 1100u, us + 50000u));
    us += 50000u;
    /* And 50.1 ms. */
    CHECK(sense_trace_pulse(&tr, 2u, 5u, 1900u, us + 50100u));
    us += 50100u;
    /* The bank letting go is no command; the pulse after it is one,
     * 100 ms later and 1 ms later. */
    CHECK(!sense_trace_pulse(&tr, 2u, 5u, 0u, us + 100000u));
    CHECK(sense_trace_pulse(&tr, 2u, 5u, 1500u, us + 200000u));
    us += 200000u;
    CHECK(!sense_trace_pulse(&tr, 2u, 5u, 0u, us + 1000u));
    CHECK(sense_trace_pulse(&tr, 2u, 5u, 1500u, us + 2000u));
    /* Each slot is watched on its own; one past the last is none. */
    CHECK(!sense_trace_pulse(&tr, 7u, 5u, 1500u, us));
    CHECK(sense_trace_pulse(&tr, 7u, 5u, 1501u, us));
    CHECK(!sense_trace_pulse(&tr, SENSE_TRACE_SLOTS, 5u, 1500u, us));
    CHECK(!sense_trace_pulse(&tr, SENSE_TRACE_SLOTS, 5u, 1501u, us));
    /* The hold across the 0.1 ms count's wrap. */
    us = ((uint64_t)1u << 32) * 100u - 20000u;
    CHECK(sense_trace_pulse(&tr, 0u, 5u, 1500u, us) == false);
    CHECK(sense_trace_pulse(&tr, 0u, 5u, 1600u, us));
    CHECK(!sense_trace_pulse(&tr, 0u, 5u, 1700u, us + 49900u));
    CHECK(sense_trace_pulse(&tr, 0u, 5u, 1800u, us + 49900u + 50000u));
}

TEST_CASE(a_slot_still_for_longer_than_the_count_tells_is_a_command_again)
{
    /* A slot's pulse is seen every pass.  Still for 2^31 counts of
     * 0.1 ms (59.6 h), 50 ms less and 50 ms more, and for 2^32 less
     * 1 ms: the next change is a command each time. */
    static const uint64_t still_us[] = {
        ((uint64_t)1u << 31) * 100u - 50000u,
        ((uint64_t)1u << 31) * 100u,
        ((uint64_t)1u << 31) * 100u + 50000u,
        ((uint64_t)1u << 32) * 100u - 1000u,
    };
    for (unsigned k = 0u; k < sizeof(still_us) / sizeof(still_us[0]); ++k) {
        rig();
        uint64_t us = g_us;
        CHECK(!sense_trace_pulse(&tr, 1u, 5u, 1500u, us));
        CHECK(sense_trace_pulse(&tr, 1u, 5u, 1900u, us + 1000u));
        us += 1000u;
        /* A pass 50 ms after the change, as every pass after it. */
        CHECK(!sense_trace_pulse(&tr, 1u, 5u, 1900u, us + 50000u));
        CHECK(!tr.out.watch[1].changed);
        CHECK(sense_trace_pulse(&tr, 1u, 5u, 1100u, us + still_us[k]));
    }
    /* A slewed command whose end is seen: said once, and the slot is a
     * command's again whenever its next change comes. */
    rig();
    run(100u);
    CHECK(!sense_trace_pulse(&tr, 1u, 5u, 1500u, g_us));
    CHECK(sense_trace_pulse(&tr, 1u, 5u, 1600u, g_us + 1000u));
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us + 1000u, 5u, 1600u);
    CHECK(!sense_trace_pulse(&tr, 1u, 5u, 1700u, g_us + 11000u));
    CHECK(tr.out.watch[1].slewed);
    CHECK(!sense_trace_pulse(&tr, 1u, 5u, 1700u, g_us + 61000u));
    CHECK(!tr.out.watch[1].slewed && !tr.out.watch[1].changed);
    CHECK_EQ(tr.out.q_n, 2u);
    CHECK(!sense_trace_pulse(&tr, 1u, 5u, 1700u, g_us + 70000u));
    CHECK_EQ(tr.out.q_n, 2u);
    /* Let go inside the hold: the last change is forgotten there, and
     * the pulse after it is a command, at once and 50 ms later.  A
     * change 1 ms after that command is its slew. */
    rig();
    CHECK(!sense_trace_pulse(&tr, 1u, 5u, 1500u, g_us));
    CHECK(sense_trace_pulse(&tr, 1u, 5u, 1600u, g_us + 1000u));
    CHECK(!sense_trace_pulse(&tr, 1u, 5u, 0u, g_us + 2000u));
    CHECK(!tr.out.watch[1].changed);
    CHECK(sense_trace_pulse(&tr, 1u, 5u, 1600u, g_us + 3000u));
    CHECK(!sense_trace_pulse(&tr, 1u, 5u, 0u, g_us + 4000u));
    CHECK(sense_trace_pulse(&tr, 1u, 5u, 1600u, g_us + 53000u));
    CHECK(!sense_trace_pulse(&tr, 1u, 5u, 1610u, g_us + 54000u));
    CHECK(tr.out.watch[1].slewed);
}

TEST_CASE(a_trace_the_console_never_takes_is_dropped_before_its_times_wrap)
{
    rig();
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    const uint64_t end_us = g_us - 100000u + 10000000u;
    /* A trigger that waits behind it. */
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, end_us + 1000000u, 1u,
                        1500u);
    CHECK_EQ(tr.out.wait_n, 1u);
    stall(100u);
    /* 0.1 ms short of 2^30 counts after its end: still the trace. */
    g_us = end_us + ((uint64_t)SENSE_TRACE_OWED_MAX_T - 1u) * 100u;
    pass_as(0u, true);
    CHECK(sense_trace_active(&tr));
    CHECK_EQ(tr.out.abandoned, 0u);
    /* At it: dropped, with what waited. */
    g_us += 100u;
    pass_as(0u, true);
    CHECK(!sense_trace_active(&tr));
    CHECK_EQ(tr.out.abandoned, 1u);
    CHECK_EQ(tr.out.wait_n, 0u);
    /* The next trigger is a trace of its own, far past the old end. */
    g_len = 0u;
    tick();
    g_us += ((uint64_t)1u << 31) * 100u;
    run(100u);
    sense_trace_key(&tr, 't', g_us);
    run(10100u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_t, 1u);
    CHECK_EQ(seen.id, 2u);
    CHECK_EQ(seen.z_e, 't');
    CHECK_EQ(seen.z_s, seen.n_s);
}

TEST_CASE(a_slewed_command_is_one_line_and_holds_the_trace_open)
{
    rig();
    run(300u);
    const uint32_t t0 = (uint32_t)(g_us / 100u);
    CHECK(!sense_trace_pulse(&tr, 0u, 5u, 1100u, g_us));
    /* 800 us of travel at 1 us a millisecond: a change every pass. */
    unsigned commands = 0u;
    for (unsigned k = 1u; k <= 800u; ++k) {
        if (sense_trace_pulse(&tr, 0u, 5u, (uint16_t)(1100u + k), g_us)) {
            sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 5u,
                                (uint16_t)(1100u + k));
            ++commands;
        }
        run(1u);
    }
    CHECK_EQ(commands, 1u);
    /* Open 4 s past the last change, not past the first.  The slot
     * renders its last pulse in every pass. */
    for (unsigned k = 0; k < 3990u; ++k) {
        CHECK(!sense_trace_pulse(&tr, 0u, 5u, 1900u, g_us));
        run(1u);
    }
    CHECK(sense_trace_active(&tr));
    run(20u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_c, 1u);
    CHECK_EQ(seen.mark_us, 1101u);
    CHECK_EQ(seen.t[seen.n_s - 1u], t0 + 7990u + 40000u - 10u);
    /* Where the slew ended, once the slot had held still: one line, at
     * the last change, with the pulse it ended at. */
    CHECK_EQ(seen.n_d, 1u);
    CHECK_EQ(seen.dest_ch, 5u);
    CHECK_EQ(seen.dest_us, 1900u);
    CHECK_EQ(seen.dest_t, t0 + 7990u);
    CHECK_EQ(seen.dest_cmd, seen.mark_t[0]);
    CHECK_EQ(seen.dest_cmd, t0);
    CHECK_EQ(seen.z_m, 2u);
    CHECK(strstr(g_log, "$D t=") > strstr(g_log, "$C t="));
    /* A slew that is let go before it held still ends where it was, and
     * one a new command follows ends before that command's line. */
    g_len = 0u;
    sense_trace_key(&tr, 't', g_us);
    CHECK(sense_trace_pulse(&tr, 2u, 6u, 1500u, g_us) == false);
    run(100u);
    CHECK(sense_trace_pulse(&tr, 2u, 6u, 1510u, g_us));
    run(1u);
    CHECK(!sense_trace_pulse(&tr, 2u, 6u, 1520u, g_us));
    run(1u);
    CHECK(!sense_trace_pulse(&tr, 2u, 6u, 0u, g_us));
    run(10u);
    parse();
    CHECK_EQ(seen.n_d, 1u);
    CHECK_EQ(seen.dest_us, 1520u);
    CHECK_EQ(seen.dest_ch, 6u);
    CHECK(sense_trace_pulse(&tr, 2u, 6u, 1600u, g_us + 100000u));
    CHECK(!sense_trace_pulse(&tr, 2u, 6u, 1610u, g_us + 101000u));
    CHECK(sense_trace_pulse(&tr, 2u, 6u, 1100u, g_us + 151000u));
    run(200u);
    parse();
    CHECK_EQ(seen.n_d, 2u);
    CHECK_EQ(seen.dest_us, 1610u);
    /* A step is no slew: no $D line. */
    CHECK(!sense_trace_pulse(&tr, 2u, 6u, 1100u, g_us + 400000u));
    run(10u);
    parse();
    CHECK_EQ(seen.n_d, 2u);
    sense_trace_key(&tr, 'x', g_us);
    run(3u);
    /* With no trace under way a slew starts none. */
    CHECK(!sense_trace_pulse(&tr, 1u, 5u, 1500u, g_us));
    CHECK(sense_trace_pulse(&tr, 1u, 5u, 1501u, g_us));
    CHECK(!sense_trace_pulse(&tr, 1u, 5u, 1502u, g_us + 1000u));
    CHECK(!sense_trace_active(&tr));
}

/* A $D line names its command's $C time: a slew let go and a new command
 * 11 ms after its last change each end with their own command, across
 * the 0.1 ms count's wrap. */
TEST_CASE(a_d_line_names_the_command_it_ends)
{
    rig();
    g_us = ((uint64_t)UINT32_MAX - 4100u) * 100u;
    run(300u);
    CHECK(!sense_trace_pulse(&tr, 2u, 6u, 1500u, g_us));
    run(100u);
    CHECK(sense_trace_pulse(&tr, 2u, 6u, 1510u, g_us));
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 6u, 1510u);
    const uint32_t c1 = (uint32_t)(g_us / 100u);
    run(1u);
    CHECK(!sense_trace_pulse(&tr, 2u, 6u, 1520u, g_us));
    const uint32_t d1 = (uint32_t)(g_us / 100u);
    run(1u);
    CHECK(!sense_trace_pulse(&tr, 2u, 6u, 0u, g_us));      /* let go */
    run(10u);
    CHECK(sense_trace_pulse(&tr, 2u, 6u, 1600u, g_us));
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 6u, 1600u);
    const uint32_t c2 = (uint32_t)(g_us / 100u);
    CHECK((int32_t)(c2 - d1) == 110);
    run(1u);
    CHECK(!sense_trace_pulse(&tr, 2u, 6u, 1610u, g_us));
    const uint32_t d2 = (uint32_t)(g_us / 100u);
    CHECK(d2 < c1);                                  /* past the wrap */
    run(1u);
    for (unsigned k = 0u; k < 60u; ++k) {
        (void)sense_trace_pulse(&tr, 2u, 6u, 1610u, g_us);
        run(1u);
    }
    sense_trace_key(&tr, 'x', g_us);
    run(10u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_c, 2u);
    CHECK_EQ(seen.n_d, 2u);
    char want[64];
    snprintf(want, sizeof(want), "$D t=%lu ch=6 us=1520 cmd=%lu\r\n",
             (unsigned long)d1, (unsigned long)c1);
    CHECK(strstr(g_log, want) != NULL);
    snprintf(want, sizeof(want), "$D t=%lu ch=6 us=1610 cmd=%lu\r\n",
             (unsigned long)d2, (unsigned long)c2);
    CHECK(strstr(g_log, want) != NULL);
}

/* Two watched slots: a command on channel 2 while channel 1 still slews
 * leaves channel 1's $D naming channel 1's own command. */
TEST_CASE(a_command_on_another_channel_leaves_a_slews_command_time)
{
    rig();
    run(300u);
    CHECK(!sense_trace_pulse(&tr, 1u, 1u, 1500u, g_us));
    CHECK(!sense_trace_pulse(&tr, 2u, 2u, 1500u, g_us));
    run(100u);
    CHECK(sense_trace_pulse(&tr, 1u, 1u, 1600u, g_us));
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 1u, 1600u);
    const uint32_t c1 = (uint32_t)(g_us / 100u);
    run(1u);
    CHECK(!sense_trace_pulse(&tr, 1u, 1u, 1610u, g_us));
    const uint32_t d1 = (uint32_t)(g_us / 100u);
    run(1u);
    CHECK(sense_trace_pulse(&tr, 2u, 2u, 1700u, g_us));
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 2u, 1700u);
    const uint32_t c2 = (uint32_t)(g_us / 100u);
    CHECK(c2 != c1);
    for (unsigned k = 0u; k < 60u; ++k) {
        run(1u);
        CHECK(!sense_trace_pulse(&tr, 1u, 1u, 1610u, g_us));
        CHECK(!sense_trace_pulse(&tr, 2u, 2u, 1700u, g_us));
    }
    sense_trace_key(&tr, 'x', g_us);
    run(10u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_c, 2u);
    CHECK_EQ(seen.n_d, 1u);                 /* channel 2 stepped */
    char want[64];
    snprintf(want, sizeof(want), "$D t=%lu ch=1 us=1610 cmd=%lu\r\n",
             (unsigned long)d1, (unsigned long)c1);
    CHECK(strstr(g_log, want) != NULL);
}

/* An edge and a console trigger, both on channel 0 by their call, during
 * channel 0's slew are no commands: its $D names the command. */
TEST_CASE(an_edge_or_a_key_during_a_slew_is_no_command_of_channel_0)
{
    rig();
    run(300u);
    CHECK(!sense_trace_pulse(&tr, 0u, 0u, 1500u, g_us));
    run(100u);
    CHECK(sense_trace_pulse(&tr, 0u, 0u, 1600u, g_us));
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 0u, 1600u);
    const uint32_t c1 = (uint32_t)(g_us / 100u);
    run(1u);
    CHECK(!sense_trace_pulse(&tr, 0u, 0u, 1610u, g_us));
    const uint32_t d1 = (uint32_t)(g_us / 100u);
    run(1u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us, 0u, 0u);
    run(1u);
    sense_trace_key(&tr, 't', g_us);
    for (unsigned k = 0u; k < 60u; ++k) {
        run(1u);
        CHECK(!sense_trace_pulse(&tr, 0u, 0u, 1610u, g_us));
    }
    sense_trace_key(&tr, 'x', g_us);
    run(10u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_c, 1u);
    CHECK_EQ(seen.n_e, 1u);
    CHECK_EQ(seen.n_k, 1u);
    CHECK_EQ(seen.n_d, 1u);
    char want[64];
    snprintf(want, sizeof(want), "$D t=%lu ch=0 us=1610 cmd=%lu\r\n",
             (unsigned long)d1, (unsigned long)c1);
    CHECK(strstr(g_log, want) != NULL);
}

/* A trace of 4 s from an edge at @p edge_us, the clock 300 ms before it. */
static void trace_at(uint64_t edge_us)
{
    rig();
    g_us = edge_us - 300000u;
    run(300u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, edge_us, 0u, 0u);
    run(4100u);
    parse();
}

TEST_CASE(a_trace_runs_across_the_millisecond_ticks_wrap)
{
    /* The edge 1 ms before the tick wraps at 2^32 ms. */
    const uint64_t wrap_us = ((uint64_t)1u << 32) * 1000u;
    trace_at(wrap_us - 1000u);
    CHECK(!sense_trace_active(&tr));
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.ms0, 4294967295u);
    CHECK_EQ(seen.t0, (uint32_t)((wrap_us - 1000u) / 100u));
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_e, 't');
    CHECK_EQ(seen.z_s, seen.n_s);
    CHECK_EQ(seen.t[seen.n_s - 1u], (uint32_t)seen.t0 + 40000u - 10u);
    const unsigned pre = (unsigned)(-seen.first_dt) / 10u;
    CHECK_EQ(seen.n_s, pre + 4000u);
    /* At the wrap, and 1 ms past it. */
    trace_at(wrap_us);
    CHECK_EQ(seen.ms0, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.t[seen.n_s - 1u], (uint32_t)seen.t0 + 40000u - 10u);
    trace_at(wrap_us + 1000u);
    CHECK_EQ(seen.ms0, 1u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.t[seen.n_s - 1u], (uint32_t)seen.t0 + 40000u - 10u);
}

TEST_CASE(a_trace_runs_across_the_tenth_millisecond_counts_wrap)
{
    /* The count the lines carry wraps at 2^32 * 0.1 ms: 1 s into this
     * trace, at its first sample, and 1 ms before its end. */
    const uint64_t wrap_us = ((uint64_t)1u << 32) * 100u;
    const uint64_t at[] = { wrap_us - 1000000u, wrap_us - 100u, wrap_us,
                            wrap_us - 3999000u, wrap_us - 4000000u };
    for (unsigned k = 0u; k < sizeof(at) / sizeof(at[0]); ++k) {
        trace_at(at[k]);
        CHECK(!sense_trace_active(&tr));
        CHECK_EQ(seen.n_bad, 0u);
        CHECK_EQ(seen.t0, (uint32_t)(at[k] / 100u));
        CHECK_EQ(seen.n_z, 1u);
        CHECK_EQ(seen.z_s, seen.n_s);
        const unsigned pre = (unsigned)(-seen.first_dt + 9) / 10u;
        CHECK_EQ(seen.n_s, pre + 4000u);
        for (unsigned j = 1u; j < seen.n_s; ++j) {
            if (seen.t[j] - seen.t[j - 1u] != 10u) {
                T_FAIL("case %u: sample %u is not 1 ms on", k, j);
                break;
            }
        }
        CHECK((uint32_t)(seen.t[seen.n_s - 1u] - (uint32_t)seen.t0)
              >= 40000u - 10u);
        CHECK((uint32_t)(seen.t[seen.n_s - 1u] - (uint32_t)seen.t0) < 40000u);
    }
}

TEST_CASE(the_pump_gives_whole_lines_into_the_room_it_has)
{
    /* Every room from none to more than a line, one pass a millisecond:
     * no pump hands back part of a line or more than its room. */
    for (size_t room = 0u; room <= 70u; ++room) {
        rig();
        run(100u);
        sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 15u, 2000u);
        for (unsigned k = 0; k < 300u; ++k) {
            tick();
            pass_as(room, true);
        }
        CHECK(!g_partial);
        CHECK(g_most <= room);
        parse();
        CHECK_EQ(seen.n_bad, 0u);
        /* The header's lines are the longest: under their length nothing
         * is written, and nothing is lost by waiting. */
        if (room < strlen("$T v=3 n=1 trig=cmd t=201000 ms=20100 "
                          "len=4000\r\n")) {
            CHECK_EQ(g_len, 0u);
        } else if (room >= 60u) {
            CHECK_EQ(seen.n_t, 1u);
            CHECK_EQ(seen.n_h, 1u);
            CHECK_EQ(seen.n_c, 1u);
            CHECK(seen.n_s > 300u);
        }
        CHECK(sense_trace_active(&tr));
    }
}

TEST_CASE(a_console_with_no_room_loses_samples_and_says_how_many)
{
    rig();
    run(200u);
    const uint32_t t0 = (uint32_t)(g_us / 100u);
    sense_trace_key(&tr, 't', g_us);
    run(1000u);
    /* The console takes nothing for 6 s: the ring fills after about
     * 3.9 s and the rest is dropped.  Nothing waits: every tick returns. */
    for (unsigned k = 0; k < 6000u; ++k) {
        tick();
        pass_as(0u, true);
    }
    CHECK_EQ(sense_trace_held(&tr), RING);
    CHECK(tr.src.lost > 2000u);
    run(3200u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.n_l, 1u);
    CHECK_EQ(seen.z_l, seen.lost);
    CHECK_EQ(seen.z_s, seen.n_s);
    CHECK_EQ(seen.z_v, seen.n_v);
    /* What is written and what is missing are the 10 s: 10000 samples.
     * A dropped sample's voltage is not written and not counted; a
     * voltage that found the ring full behind its sample is counted. */
    const unsigned pre = (unsigned)(-seen.first_dt) / 10u;
    CHECK(seen.lost > 2000u);
    CHECK(seen.n_s + seen.lost >= pre + 10000u);
    CHECK(seen.n_s + seen.lost <= pre + 10000u + 4u);
    /* The times run on across the gap. */
    CHECK_EQ(seen.t[seen.n_s - 1u], t0 + 100000u - 10u);
}

TEST_CASE(a_console_with_no_room_across_the_end_ends_the_trace_there)
{
    rig();
    run(200u);
    const uint32_t t0 = (uint32_t)(g_us / 100u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us, 0u, 0u);
    run(3000u);
    /* No room from 1 s before the end to 1 s after it: the ring holds
     * the 2 s, and the samples past the end are not the trace's. */
    for (unsigned k = 0; k < 2000u; ++k) {
        tick();
        pass_as(0u, true);
    }
    CHECK(sense_trace_active(&tr));
    CHECK_EQ(tr.src.lost, 0u);
    run(400u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_e, 't');
    CHECK_EQ(seen.z_l, 0u);
    CHECK_EQ(seen.z_s, seen.n_s);
    CHECK_EQ(seen.t[seen.n_s - 1u], t0 + 40000u - 10u);
    const unsigned pre = (unsigned)(-seen.first_dt) / 10u;
    CHECK_EQ(seen.n_s, pre + 4000u);
}

TEST_CASE(a_console_that_is_not_connected_drops_the_trace)
{
    rig();
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    for (unsigned k = 0; k < 5000u; ++k) {
        tick();
        pass_as(SENSE_TRACE_LINE_MAX, false);
    }
    CHECK_EQ(g_len, 0u);
    CHECK(!sense_trace_active(&tr));
    CHECK_EQ(tr.out.abandoned, 1u);
    CHECK_EQ(sense_trace_held(&tr), SENSE_TRACE_PRE);
    CHECK_EQ(tr.src.lost, 0u);
    /* A trace under way when the console goes: dropped with no line, and
     * the next one starts with a header of its own. */
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    const size_t len = g_len;
    CHECK(len > 0u);
    tick();
    pass_as(SENSE_TRACE_LINE_MAX, false);
    CHECK_EQ(g_len, len);
    CHECK_EQ(tr.out.abandoned, 2u);
    g_len = 0u;
    sense_trace_key(&tr, 't', g_us);
    run(10100u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_t, 1u);
    CHECK_EQ(seen.id, 3u);
    CHECK_EQ(seen.n_k, 1u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_s, seen.n_s);
    CHECK_EQ(seen.z_l, 0u);
}

int main(void)
{
    RUN(idle_keeps_the_newest_records);
    RUN(the_console_command_gives_ten_seconds);
    RUN(a_command_and_an_edge_give_four_seconds);
    RUN(a_trigger_during_a_trace_moves_its_end_and_never_back);
    RUN(a_slots_pulse_is_a_command_after_it_held_still);
    RUN(a_slot_still_for_longer_than_the_count_tells_is_a_command_again);
    RUN(a_trace_the_console_never_takes_is_dropped_before_its_times_wrap);
    RUN(a_slewed_command_is_one_line_and_holds_the_trace_open);
    RUN(a_d_line_names_the_command_it_ends);
    RUN(a_command_on_another_channel_leaves_a_slews_command_time);
    RUN(an_edge_or_a_key_during_a_slew_is_no_command_of_channel_0);
    RUN(a_trace_runs_across_the_millisecond_ticks_wrap);
    RUN(a_trace_runs_across_the_tenth_millisecond_counts_wrap);
    RUN(the_pump_gives_whole_lines_into_the_room_it_has);
    RUN(a_console_with_no_room_loses_samples_and_says_how_many);
    RUN(a_console_with_no_room_across_the_end_ends_the_trace_there);
    RUN(a_console_that_is_not_connected_drops_the_trace);
    return test_summary("sense_trace");
}
