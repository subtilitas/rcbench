/*
 * The trace of CH1's 1 ms samples (sense_trace.h), the set-up's records,
 * over sense_trace_rig.h: a set-up record with the time of the tick it was
 * seen in, and one that found no room; a stop on a full ring; a changed
 * set-up and a state change under a backlog, each in its place among the
 * samples; a command, a trigger and a slewed command's end after a changed
 * set-up, in the next trace; records and samples dropped before or while
 * a set-up record waits, counted after it; idle keeping no record from
 * before a set-up.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_trace_rig.h"

/* The newest record of @p kind the ring holds, NULL for none. */
static const sense_trace_rec_t *newest_of(sense_trace_kind_t kind)
{
    const sense_trace_rec_t *found = NULL;
    const uint32_t head = (uint32_t)atomic_load(&tr.head);
    for (uint32_t k = (uint32_t)atomic_load(&tr.tail); k != head; ++k) {
        if ((g_ring[k & tr.mask].meta >> 24) == (uint32_t)kind) {
            found = &g_ring[k & tr.mask];
        }
    }
    return found;
}

TEST_CASE(a_set_up_record_has_the_time_of_the_tick_it_was_seen_in)
{
    /* The part offline: no sample for 2 s.  A command, and 300 ms after
     * it another shunt: the command was given under the old one, and its
     * line is in the trace that ends at the change. */
    rig();
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(500u);
    i3221->present = false;
    run(2000u);
    parse();
    CHECK_EQ(seen.st_on, 0u);
    const unsigned n_s = seen.n_s;
    const uint32_t last_t = seen.t[n_s - 1u];
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 3u, 1700u);
    const uint32_t cmd_t = (uint32_t)(g_us / 100u);
    CHECK((int32_t)(cmd_t - last_t) > 10000);
    run(300u);
    ++cmd.cfg_gen;
    cmd.parts.i3221.shunt_uohm[0] = 50000u;
    const uint32_t seen_at = (uint32_t)(g_us / 100u);
    tick();
    const sense_trace_rec_t *r = newest_of(SENSE_TRACE_SHUNT);
    CHECK(r != NULL);
    if (r != NULL) {
        CHECK_EQ(r->t, seen_at);
    }
    run(200u);
    const char *rest = first_trace_only();
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.z_e, 's');
    CHECK_EQ(seen.n_s, n_s);
    CHECK_EQ(seen.n_c, 1u);
    CHECK_EQ(seen.mark_ch, 3u);
    CHECK_EQ(seen.z_m, 2u);
    CHECK_EQ(seen.z_ml, 0u);
    CHECK(*rest == '\0');
    CHECK(!sense_trace_active(&tr));

    /* A command in the tick after the one the change was seen in is the
     * next trace's. */
    rig();
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(500u);
    i3221->present = false;
    run(2000u);
    ++cmd.cfg_gen;
    cmd.parts.i3221.shunt_uohm[0] = 50000u;
    tick();
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 3u, 1700u);
    run(200u);
    rest = first_trace_only();
    parse();
    CHECK_EQ(seen.z_e, 's');
    CHECK_EQ(seen.n_c, 0u);
    CHECK(strncmp(rest, "$T v=3 n=2 trig=cmd ", 20u) == 0);
    CHECK(strstr(rest, "shunt_uohm=50000 ") != NULL);

    /* A closed bus has no sample at all: the record of its set-up has
     * the time of the tick that found it closed. */
    rig();
    run(200u);
    cmd.parts.i3221.fitted = 0u;
    ++cmd.cfg_gen;
    for (unsigned k = 0u; k < 50u && svc.open; ++k) {
        tick();
    }
    CHECK(!svc.open);
    r = newest_of(SENSE_TRACE_SHUNT);
    CHECK(r != NULL);
    if (r != NULL) {
        CHECK_EQ(r->t, (uint32_t)(g_us / 100u) - 10u);
        CHECK_EQ(r->v, 0);
    }
}

TEST_CASE(a_set_up_record_that_found_no_room_keeps_its_time_or_the_rings)
{
    /* The ring full when the set-up changes, and the console takes
     * nothing for 5 ms: the record is written with the time of the tick
     * the change was seen in. */
    rig_size(8u);
    run(20u);
    sense_trace_key(&tr, 't', g_us);
    run(20u);
    stall(20u);
    CHECK_EQ(sense_trace_held(&tr), 8u);
    ++cmd.cfg_gen;
    cmd.parts.i3221.shunt_uohm[0] = 50000u;
    i3221->shunt_ohm = 0.05;
    uint32_t first_t = 0u;
    for (unsigned k = 0u; k < 50u && !tr.src.shunt_owed; ++k) {
        first_t = (uint32_t)(g_us / 100u);
        tick();
    }
    CHECK(tr.src.shunt_owed);
    stall(5u);
    CHECK(tr.src.shunt_owed);
    CHECK(!tr.src.shunt_newer);
    pass();
    tick();
    CHECK(!tr.src.shunt_owed);
    const sense_trace_rec_t *r = newest_of(SENSE_TRACE_SHUNT);
    CHECK(r != NULL);
    if (r != NULL) {
        CHECK_EQ(r->t, first_t);
    }
    /* A record kept while the shunt's found no room: the shunt's has
     * that record's time, and no time in the ring runs back. */
    tr.src.shunt_owed  = true;
    tr.src.shunt_t     = (uint32_t)(g_us / 100u) - 500u;
    tr.src.shunt_newer = true;
    const uint32_t kept_t = tr.src.kept_t;
    tr.src.shunt = 50000u;
    pass();
    tick();
    CHECK(!tr.src.shunt_owed);
    const uint32_t head = (uint32_t)atomic_load(&tr.head);
    uint32_t before = 0u;
    bool any = false;
    bool found = false;
    for (uint32_t k = (uint32_t)atomic_load(&tr.tail); k != head; ++k) {
        const sense_trace_rec_t *q = &g_ring[k & tr.mask];
        CHECK(!any || (int32_t)(q->t - before) >= 0);
        if ((q->meta >> 24) == (uint32_t)SENSE_TRACE_SHUNT
            && q->t == kept_t) {
            found = true;
        }
        before = q->t;
        any = true;
    }
    CHECK(found);
}

TEST_CASE(a_stop_on_a_full_ring_counts_what_was_dropped_before_it)
{
    rig_size(8u);
    run(20u);
    sense_trace_key(&tr, 't', g_us);
    run(20u);
    /* 30 ms with no room on a ring of 8: records are dropped, and the
     * count of them rides on the first record kept after the stop. */
    stall(30u);
    CHECK(tr.src.lost > 20u);
    sense_trace_key(&tr, 'x', g_us);
    run(20u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_e, 'k');
    CHECK_EQ(seen.n_l, 1u);
    CHECK(seen.lost > 20u);
    CHECK_EQ(seen.z_l, seen.lost);
    CHECK_EQ(seen.z_s, seen.n_s);
    /* The $L line stands before the end line, after the last sample. */
    const char *l_at = strstr(g_log, "$L n=");
    const char *z_at = strstr(g_log, "$Z n=");
    CHECK(l_at != NULL && z_at != NULL && l_at < z_at);
    CHECK(strchr(l_at, ',') == NULL);
}

TEST_CASE(a_changed_set_up_under_a_backlog_ends_the_trace_where_it_changed)
{
    rig();
    run(200u);
    const uint32_t t0 = (uint32_t)(g_us / 100u);
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    /* 300 ms wait in the ring; then another shunt, and 100 ms more. */
    stall(300u);
    ++cmd.cfg_gen;
    cmd.parts.i3221.shunt_uohm[0] = 50000u;
    i3221->shunt_ohm = 0.05;
    stall(100u);
    for (unsigned k = 0; k < 300u && sense_trace_active(&tr); ++k) {
        tick();
        pass();
    }
    CHECK(!sense_trace_active(&tr));
    /* A trigger in the pass the trace ended in, before the pump has run
     * idle: the set-up's records went with the trace they ended. */
    const size_t first_len = g_len;
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    sense_trace_key(&tr, 'x', g_us);
    run(3u);
    CHECK(strstr(&g_log[first_len], "shunt_uohm=50000 ") != NULL);
    CHECK(strstr(&g_log[first_len], " e=k\r\n") != NULL);
    g_log[first_len] = '\0';
    g_len = first_len;
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_e, 's');
    CHECK_EQ(seen.z_s, seen.n_s);
    CHECK_EQ(seen.shunt, I3221_UOHM);
    /* The 400 ms before the change, each at the header's scale, and no
     * sample of the new set-up. */
    const unsigned pre = (unsigned)(-seen.first_dt) / 10u;
    CHECK_EQ(seen.n_s, pre + 400u);
    CHECK_EQ(seen.t[seen.n_s - 1u], t0 + 4000u - 10u);
    for (unsigned k = 0u; k < seen.n_s; ++k) {
        if (seen.code[k] != 300) {
            T_FAIL("sample %u reads %ld", k, (long)seen.code[k]);
            break;
        }
    }
    /* The next trace has the new set-up in its header and its samples
     * at its scale: none from before the change. */
    g_len = 0u;
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    sense_trace_key(&tr, 'x', g_us);
    run(3u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.shunt, 50000u);
    CHECK_EQ(seen.z_e, 'k');
    for (unsigned k = 0u; k < seen.n_s; ++k) {
        if (seen.code[k] != 150) {
            T_FAIL("sample %u reads %ld", k, (long)seen.code[k]);
            break;
        }
    }
}

TEST_CASE(a_command_after_a_changed_set_up_is_the_next_traces)
{
    rig();
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    /* The console is behind when the set-up changes, and still behind
     * when a command comes 50 ms later: the trace looks open, and ended
     * before the command. */
    stall(200u);
    ++cmd.cfg_gen;
    cmd.parts.i3221.shunt_uohm[0] = 50000u;
    i3221->shunt_ohm = 0.05;
    stall(50u);
    const uint32_t t1 = (uint32_t)(g_us / 100u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 4u, 1900u);
    stall(50u);
    run(500u);
    const char *rest = first_trace_only();
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.z_e, 's');
    CHECK_EQ(seen.n_c, 0u);
    CHECK_EQ(seen.z_m, 1u);
    CHECK_EQ(seen.z_s, seen.n_s);
    /* The command's own trace, under the new set-up. */
    char want[64];
    snprintf(want, sizeof(want), "$T v=3 n=2 trig=cmd t=%lu ",
             (unsigned long)t1);
    CHECK(strncmp(rest, want, strlen(want)) == 0);
    CHECK(strstr(rest, "shunt_uohm=50000 ") != NULL);
    CHECK(strstr(rest, " ch=4 us=1900\r\n") != NULL);
    CHECK(strstr(rest, ",150\r\n") != NULL);
    CHECK(strstr(rest, ",300\r\n") == NULL);
    CHECK(sense_trace_active(&tr));
}

/* A trace by the console, 100 ms written and then the console behind:
 * the set-up changes after @p before ms of that and @p after ms pass. */
static void behind_at_a_change(unsigned before, unsigned after)
{
    rig();
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    stall(before);
    ++cmd.cfg_gen;
    cmd.parts.i3221.shunt_uohm[0] = 50000u;
    i3221->shunt_ohm = 0.05;
    stall(after);
}

TEST_CASE(a_trigger_lost_after_a_changed_set_up_is_counted_in_the_next_trace)
{
    /* The trigger queue fills with commands of after the change, and two
     * more find it full: the trace that ended before them all counts
     * none, the trace they start counts both. */
    behind_at_a_change(200u, 50u);
    for (unsigned k = 0u; k < SENSE_TRACE_MARKS + 2u; ++k) {
        sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us + k * 100u,
                            (uint16_t)k, 1500u);
    }
    CHECK_EQ(tr.out.n_mlost, 2u);
    stall(50u);
    run(600u);
    sense_trace_key(&tr, 'x', g_us);
    run(5u);
    CHECK(!sense_trace_active(&tr));
    const char *rest = first_trace_only();
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.z_e, 's');
    CHECK_EQ(seen.n_c, 0u);
    CHECK_EQ(seen.z_m, 1u);
    CHECK_EQ(seen.z_ml, 0u);
    CHECK(strncmp(rest, "$T v=3 n=2 trig=cmd ", 20u) == 0);
    CHECK(strstr(rest, " m=8 ml=2 e=k\r\n") != NULL);

    /* One of before the change and one of after it, the queue full of
     * commands of before it, and a trigger that waits for the next
     * trace: both traces count both. */
    behind_at_a_change(100u, 0u);
    for (unsigned k = 0u; k < SENSE_TRACE_MARKS + 1u; ++k) {
        sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD,
                            g_us - 50000u + k * 100u, (uint16_t)k, 1500u);
    }
    stall(50u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 9u, 1500u);
    CHECK_EQ(tr.out.n_mlost, 2u);
    const uint64_t far_us = g_us + 20000000u;
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_KEY, far_us, 0u, 0u);
    CHECK_EQ(tr.out.wait_n, 1u);
    for (unsigned k = 0u; k < 2000u && tr.out.id == 1u; ++k) {
        run(1u);
    }
    CHECK_EQ(tr.out.id, 2u);
    CHECK_EQ(tr.out.n_mlost, 2u);
    (void)first_trace_only();
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.z_e, 's');
    CHECK_EQ(seen.n_c, SENSE_TRACE_MARKS);
    CHECK_EQ(seen.z_ml, 2u);

    /* The one of before the change alone: the trace it was lost in. */
    behind_at_a_change(100u, 50u);
    for (unsigned k = 0u; k < SENSE_TRACE_MARKS + 1u; ++k) {
        sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD,
                            g_us - 100000u + k * 100u, (uint16_t)k, 1500u);
    }
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_KEY, far_us + 1000000u, 0u, 0u);
    for (unsigned k = 0u; k < 2000u && tr.out.id == 1u; ++k) {
        run(1u);
    }
    CHECK_EQ(tr.out.id, 2u);
    CHECK_EQ(tr.out.n_mlost, 0u);
    (void)first_trace_only();
    parse();
    CHECK_EQ(seen.z_e, 's');
    CHECK_EQ(seen.z_ml, 1u);

    /* One of after the change with no trigger left to start a trace: no
     * trace follows, and the one that ended says it. */
    behind_at_a_change(100u, 0u);
    for (unsigned k = 0u; k < SENSE_TRACE_MARKS; ++k) {
        sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD,
                            g_us - 50000u + k * 100u, (uint16_t)k, 1500u);
    }
    stall(50u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 9u, 1500u);
    CHECK_EQ(tr.out.n_mlost, 1u);
    run(2000u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_t, 1u);
    CHECK_EQ(seen.z_e, 's');
    CHECK_EQ(seen.z_ml, 1u);
}

TEST_CASE(a_slewed_commands_end_goes_with_its_command_into_the_next_trace)
{
    /* The command and the end of its slew, both after the change the
     * console has not come to: the next trace has both lines. */
    behind_at_a_change(200u, 50u);
    CHECK(!sense_trace_pulse(&tr, 0u, 5u, 1500u, g_us - 200000u));
    CHECK(sense_trace_pulse(&tr, 0u, 5u, 1600u, g_us));
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 5u, 1600u);
    const uint32_t c_t = (uint32_t)(g_us / 100u);
    stall(10u);
    CHECK(!sense_trace_pulse(&tr, 0u, 5u, 1610u, g_us));
    const uint32_t d_t = (uint32_t)(g_us / 100u);
    stall(60u);
    CHECK(!sense_trace_pulse(&tr, 0u, 5u, 1610u, g_us));
    CHECK_EQ(tr.out.q_n, 2u);
    run(600u);
    const char *rest = first_trace_only();
    parse();
    CHECK_EQ(seen.z_e, 's');
    CHECK_EQ(seen.n_c + seen.n_d, 0u);
    char want[64];
    snprintf(want, sizeof(want), "$D t=%lu ch=5 us=1610 cmd=%lu\r\n",
             (unsigned long)d_t, (unsigned long)c_t);
    CHECK(strncmp(rest, "$T v=3 n=2 trig=cmd ", 20u) == 0);
    CHECK(strstr(rest, " ch=5 us=1600\r\n") != NULL);
    CHECK(strstr(rest, want) != NULL);
}

TEST_CASE(a_trigger_past_the_next_traces_end_overwrites_none_that_waited)
{
    /* The console behind for 12 s.  Two commands after a changed set-up,
     * 5 s apart, and one past the end the trace seems to have: the first
     * starts the next trace, the second lies past that trace's end and
     * waits, and the third still waits behind it. */
    behind_at_a_change(100u, 100u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 1u, 1100u);
    stall(5000u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 2u, 1200u);
    const uint64_t third_us = g_us + 6000000u;
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, third_us, 3u, 1300u);
    CHECK_EQ(tr.out.q_n, 2u);
    CHECK_EQ(tr.out.wait_n, 1u);
    for (unsigned k = 0u; k < 4000u && tr.out.id == 1u; ++k) {
        run(1u);
    }
    CHECK_EQ(tr.out.id, 2u);
    CHECK_EQ(tr.out.wait_n, 2u);
    CHECK_EQ(tr.out.wait[0].ch, 2u);
    CHECK_EQ(tr.out.wait[0].us, 1200u);
    CHECK_EQ(tr.out.wait[1].ch, 3u);
    CHECK_EQ(tr.out.wait[1].us, 1300u);
    CHECK(tr.out.wait[1].at_us == third_us);
}

TEST_CASE(records_dropped_before_a_set_up_record_are_said_in_its_trace)
{
    rig();
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(50u);
    /* By hand, what core 1 leaves when the ring had one place between
     * its two set-up records: the shunt's, then 5 records dropped, then
     * the Configuration's with their count. */
    const uint32_t head = (uint32_t)atomic_load(&tr.head);
    const uint32_t t = g_ring[(head - 1u) & tr.mask].t;
    g_ring[head & tr.mask] = (sense_trace_rec_t){
        t, 50000, (uint32_t)SENSE_TRACE_SHUNT << 24 };
    g_ring[(head + 1u) & tr.mask] = (sense_trace_rec_t){
        t, (int32_t)(tr.src.cfg ^ 0x8u),
        ((uint32_t)SENSE_TRACE_CFG << 24) | 5u };
    tr.src.dropped += 5u;
    atomic_store(&tr.drops, tr.src.dropped);
    atomic_store(&tr.head, head + 2u);
    pass();
    CHECK(!sense_trace_active(&tr));
    CHECK(strstr(g_log, " l=0 m=1 ml=0 e=s\r\n") != NULL);
    /* The next trace starts in the same pass: its header has both
     * records' set-up, and the 5 are said before it. */
    const size_t len = g_len;
    sense_trace_key(&tr, 't', g_us);
    pass();
    pass();
    sense_trace_key(&tr, 'x', g_us);
    /* The first tick that starts past the stop ends it. */
    run(5u);
    const char *next = &g_log[len];
    const char *l_at = strstr(next, "$L n=5\r\n");
    const char *h_at = strstr(next, "$H dt_us=1000 shunt_uohm=50000 cfg=0x");
    CHECK(l_at != NULL && h_at != NULL && l_at < h_at);
    CHECK(strstr(next, " l=5 m=1 ml=0 e=k\r\n") != NULL);
    char cfg[16];
    snprintf(cfg, sizeof(cfg), "cfg=0x%04X ",
             (unsigned)((tr.src.cfg ^ 0x8u) & 0xFFFFu));
    CHECK(strstr(next, cfg) != NULL);
}

TEST_CASE(a_state_change_under_a_backlog_keeps_its_place)
{
    rig();
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    /* 200 ms wait in the ring when the part stops answering; it is
     * offline 3 failed reads later. */
    stall(200u);
    i3221->present = false;
    stall(300u);
    run(600u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_st, 1u);
    CHECK_EQ(seen.st_on, 0u);
    /* The $S line stands after the last sample taken before the outage:
     * 300 ms of them after the trigger. */
    const unsigned pre = (unsigned)(-seen.first_dt) / 10u;
    CHECK_EQ(seen.st_at, pre + 300u);
    CHECK_EQ(seen.n_s, pre + 300u);
}

TEST_CASE(a_set_up_record_with_no_room_is_written_when_there_is_room)
{
    rig_size(8u);
    run(20u);
    sense_trace_key(&tr, 't', g_us);
    run(20u);
    stall(20u);
    CHECK_EQ(sense_trace_held(&tr), 8u);
    /* The ring is full when the set-up changes. */
    ++cmd.cfg_gen;
    cmd.parts.i3221.shunt_uohm[0] = 50000u;
    i3221->shunt_ohm = 0.05;
    stall(5u);
    CHECK(tr.src.shunt_owed);
    CHECK_EQ(held_of(SENSE_TRACE_SHUNT), 0u);
    run(20u);
    CHECK(!tr.src.shunt_owed && !tr.src.cfg_owed);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.z_e, 's');
    CHECK_EQ(seen.z_l, seen.lost);
    CHECK(seen.lost > 0u);
    g_len = 0u;
    run(20u);
    sense_trace_key(&tr, 't', g_us);
    run(20u);
    parse();
    CHECK_EQ(seen.shunt, 50000u);
    CHECK_EQ(seen.code[seen.n_s - 1u], 150);
}

/* The ring full when the set-up changes: the samples of the ticks while
 * its records wait for room are counted after them, on the first record
 * kept behind them, not with the count they carry.  The trace that ends
 * at the change counts none of them. */
TEST_CASE(a_sample_dropped_while_a_set_up_record_waits_counts_after_it)
{
    rig_size(8u);
    run(20u);
    sense_trace_key(&tr, 't', g_us);
    run(20u);
    stall(20u);
    CHECK_EQ(sense_trace_held(&tr), 8u);
    ++cmd.cfg_gen;
    cmd.parts.i3221.shunt_uohm[0] = 50000u;
    i3221->shunt_ohm = 0.05;
    for (unsigned k = 0u; k < 50u && !tr.src.shunt_owed; ++k) {
        tick();
    }
    CHECK(tr.src.shunt_owed);
    const uint32_t lost1    = tr.src.lost;
    const uint32_t dropped1 = tr.src.dropped;
    /* 5 ticks more with no room: the shunt's record is counted in each,
     * the samples of the 5 are not. */
    stall(5u);
    CHECK(tr.src.shunt_owed);
    CHECK_EQ(tr.src.lost, lost1 + 5u);
    uint32_t fails = 5u;
    const uint32_t head0 = (uint32_t)atomic_load(&tr.head);
    for (unsigned k = 0u;
         k < 50u && (tr.src.shunt_owed || tr.src.cfg_owed); ++k) {
        pass_as(SENSE_TRACE_LINE_MAX * 4u, true);
        tick();
        if (tr.src.shunt_owed || tr.src.cfg_owed) {
            ++fails;
        }
    }
    CHECK(!tr.src.shunt_owed && !tr.src.cfg_owed);
    const uint32_t deferred = tr.src.dropped - dropped1 - fails;
    CHECK(deferred >= 5u);
    /* The records from the shunt's on: the set-up's carry their own
     * count, the record behind them the samples'. */
    const uint32_t head = (uint32_t)atomic_load(&tr.head);
    uint32_t setup_lost = 0u;
    uint32_t after_lost = 0u;
    bool seen_setup = false;
    bool seen_after = false;
    for (uint32_t k = head0; k != head; ++k) {
        const sense_trace_rec_t *r = &g_ring[k & tr.mask];
        const uint32_t kind = r->meta >> 24;
        if (kind == SENSE_TRACE_SHUNT || kind == SENSE_TRACE_CFG) {
            CHECK(!seen_after);
            setup_lost += r->meta & SENSE_TRACE_LOST_MAX;
            seen_setup = true;
        } else if (seen_setup && !seen_after) {
            after_lost = r->meta & SENSE_TRACE_LOST_MAX;
            seen_after = true;
        }
    }
    CHECK(seen_setup && seen_after);
    CHECK_EQ(setup_lost, lost1 + fails);
    CHECK_EQ(after_lost, deferred);
    CHECK_EQ((uint32_t)atomic_load(&tr.drops), tr.src.dropped);
    run(20u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_e, 's');
    CHECK_EQ(seen.z_l, seen.lost);
    CHECK(seen.z_l <= lost1 + fails);
}

TEST_CASE(idle_keeps_no_record_from_before_a_set_up)
{
    rig();
    run(500u);
    CHECK_EQ(sense_trace_held(&tr), SENSE_TRACE_PRE);
    ++cmd.cfg_gen;
    cmd.parts.i3221.shunt_uohm[0] = 50000u;
    i3221->shunt_ohm = 0.05;
    run(10u);
    /* 10 samples of the new set-up, and the voltage read with them. */
    CHECK(sense_trace_held(&tr) >= 10u && sense_trace_held(&tr) <= 11u);
    CHECK_EQ(held_of(SENSE_TRACE_SHUNT) + held_of(SENSE_TRACE_CFG), 0u);
    sense_trace_key(&tr, 't', g_us);
    run(50u);
    sense_trace_key(&tr, 'x', g_us);
    run(3u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.shunt, 50000u);
    CHECK_EQ(seen.first_dt, -100);
    CHECK_EQ(seen.code[0], 150);
    CHECK_EQ(seen.z_e, 'k');
}

int main(void)
{
    RUN(a_set_up_record_has_the_time_of_the_tick_it_was_seen_in);
    RUN(a_set_up_record_that_found_no_room_keeps_its_time_or_the_rings);
    RUN(a_stop_on_a_full_ring_counts_what_was_dropped_before_it);
    RUN(a_changed_set_up_under_a_backlog_ends_the_trace_where_it_changed);
    RUN(a_command_after_a_changed_set_up_is_the_next_traces);
    RUN(a_trigger_lost_after_a_changed_set_up_is_counted_in_the_next_trace);
    RUN(a_slewed_commands_end_goes_with_its_command_into_the_next_trace);
    RUN(a_trigger_past_the_next_traces_end_overwrites_none_that_waited);
    RUN(records_dropped_before_a_set_up_record_are_said_in_its_trace);
    RUN(a_state_change_under_a_backlog_keeps_its_place);
    RUN(a_set_up_record_with_no_room_is_written_when_there_is_room);
    RUN(a_sample_dropped_while_a_set_up_record_waits_counts_after_it);
    RUN(idle_keeps_no_record_from_before_a_set_up);
    return test_summary("sense_trace_setup");
}
