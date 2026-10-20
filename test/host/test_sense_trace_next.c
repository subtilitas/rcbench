/*
 * The trace of CH1's 1 ms samples (sense_trace.h), a trace's end and the
 * next trace, over sense_trace_rig.h: the trigger queue full by 0 and 1;
 * the console's stop, which writes what came before it, and its other
 * characters; a changed set-up, a part going offline and a reset during a
 * trace; a trace with no bus; a trigger past the end of a trace still
 * being written; a start after a stop; a command, its slew and its end
 * line that wait for the next trace; the pulse after the bank let go; a
 * slot that changes hands.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_trace_rig.h"

TEST_CASE(the_trigger_queue_full_by_0_and_1)
{
    rig();
    run(100u);
    for (unsigned k = 0u; k < SENSE_TRACE_MARKS; ++k) {
        sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us + k * 100u,
                            (uint16_t)k, 1500u);
    }
    CHECK_EQ(tr.out.n_mlost, 0u);
    run(50u);
    sense_trace_key(&tr, 'x', g_us);
    run(2u);
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_c, SENSE_TRACE_MARKS);
    CHECK_EQ(seen.z_m, SENSE_TRACE_MARKS);
    CHECK_EQ(seen.z_ml, 0u);
    CHECK_EQ(seen.z_e, 'k');

    g_len = 0u;
    for (unsigned k = 0u; k < SENSE_TRACE_MARKS + 1u; ++k) {
        sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us + k * 100u, 0u,
                            0u);
    }
    CHECK_EQ(tr.out.n_mlost, 1u);
    run(50u);
    /* Room again: a later trigger has its line. */
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_KEY, g_us, 0u, 0u);
    run(50u);
    sense_trace_key(&tr, 'X', g_us);
    run(2u);
    parse();
    CHECK_EQ(seen.n_e, SENSE_TRACE_MARKS);
    CHECK_EQ(seen.n_k, 1u);
    CHECK_EQ(seen.z_m, SENSE_TRACE_MARKS + 1u);
    CHECK_EQ(seen.z_ml, 1u);
}

TEST_CASE(the_consoles_stop_and_other_characters)
{
    rig();
    run(100u);
    /* No trace: a stop is nothing, and so is any other character. */
    sense_trace_key(&tr, 'x', g_us);
    sense_trace_key(&tr, 'q', g_us);
    sense_trace_key(&tr, '\n', g_us);
    sense_trace_key(&tr, -1, g_us);
    run(10u);
    CHECK(!sense_trace_active(&tr));
    CHECK_EQ(g_len, 0u);
    sense_trace_key(&tr, 't', g_us);
    run(500u);
    sense_trace_key(&tr, 'q', g_us);
    run(10u);
    CHECK(sense_trace_active(&tr));
    sense_trace_key(&tr, 'x', g_us);
    run(1u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_e, 'k');
    CHECK_EQ(seen.z_s, seen.n_s);
    /* The stop is not kept for the next trace. */
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    CHECK(sense_trace_active(&tr));
}

TEST_CASE(a_changed_set_up_ends_a_trace)
{
    rig();
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(1000u);
    /* Another shunt: the codes after it are not the header's. */
    ++cmd.cfg_gen;
    cmd.parts.i3221.shunt_uohm[0] = 50000u;
    i3221->shunt_ohm = 0.05;
    run(5u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.shunt, I3221_UOHM);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_e, 's');
    CHECK_EQ(seen.z_s, seen.n_s);
    /* The next trace carries the new one: 0.12 A is 150 steps of 0.8 mA. */
    g_len = 0u;
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(300u);
    parse();
    CHECK_EQ(seen.shunt, 50000u);
    CHECK_EQ(seen.code[seen.n_s - 1u], 150);
    /* The bus closed: the same. */
    ++cmd.cfg_gen;
    cmd.parts.i3221.fitted = 0u;
    run(5u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.z_e, 's');
}

TEST_CASE(a_part_offline_and_a_reset_are_said_in_the_trace)
{
    rig();
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(1000u);
    /* The part stops answering: offline after 3 failed reads. */
    i3221->present = false;
    run(500u);
    parse();
    CHECK_EQ(seen.n_st, 1u);
    CHECK_EQ(seen.st_on, 0u);
    CHECK_EQ(seen.st_rst, 0u);
    const unsigned n_s = seen.n_s;
    /* And answers again from its next probe. */
    i3221->present = true;
    run(1500u);
    parse();
    CHECK_EQ(seen.n_st, 2u);
    CHECK_EQ(seen.st_on, 1u);
    CHECK(seen.n_s > n_s);
    /* A reset of its own: found, counted, set up again. */
    fake_reset3221(i3221);
    run(2000u);
    parse();
    CHECK_EQ(seen.st_rst, 1u);
    CHECK_EQ(seen.st_on, 1u);
    CHECK(seen.n_st >= 4u);
    run(6000u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_e, 't');
    CHECK_EQ(seen.z_s, seen.n_s);
    /* The time across the outage is the samples' own. */
    CHECK_EQ(seen.t[seen.n_s - 1u], (uint32_t)seen.t0 + 100000u - 10u);
    /* Idle after a trace that held state records: the newest records
     * stay, as before it. */
    run(100u);
    CHECK_EQ(sense_trace_held(&tr), SENSE_TRACE_PRE);
}

TEST_CASE(a_trace_with_no_bus_has_a_header_and_ends_on_time)
{
    rig();
    cmd.parts.i3221.fitted = 0u;
    run(200u);
    CHECK(!svc.open);
    sense_trace_key(&tr, 't', g_us);
    /* With no record to end it, the end line waits for the tick that
     * starts at the end. */
    run(10000u);
    CHECK(sense_trace_active(&tr));
    run(1u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_h, 1u);
    CHECK_EQ(seen.shunt, 0u);
    CHECK_EQ(seen.cfg, 0u);
    CHECK_EQ(seen.on, 0u);
    CHECK_EQ(seen.n_s, 0u);
    CHECK_EQ(seen.z_s, 0u);
    CHECK_EQ(seen.z_v, 0u);
    CHECK_EQ(seen.z_e, 't');
}

TEST_CASE(a_stop_under_a_backlog_writes_what_came_before_it)
{
    rig();
    run(200u);
    const uint32_t t0 = (uint32_t)(g_us / 100u);
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    /* 500 ms of samples wait in the ring when the stop comes. */
    stall(500u);
    sense_trace_key(&tr, 'x', g_us);
    CHECK(sense_trace_active(&tr));
    /* A second stop and a changed pulse after it move nothing. */
    stall(50u);
    sense_trace_key(&tr, 'x', g_us);
    CHECK(!sense_trace_pulse(&tr, 0u, 5u, 1500u, g_us - 200000u));
    CHECK(sense_trace_pulse(&tr, 0u, 5u, 1600u, g_us - 10000u));
    CHECK(!sense_trace_pulse(&tr, 0u, 5u, 1610u, g_us));
    run(200u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_e, 'k');
    CHECK_EQ(seen.z_l, 0u);
    CHECK_EQ(seen.z_s, seen.n_s);
    /* Every sample up to the stop, 600 ms after the trigger, and none
     * after it. */
    const unsigned pre = (unsigned)(-seen.first_dt) / 10u;
    CHECK_EQ(seen.n_s, pre + 600u);
    CHECK_EQ(seen.t[seen.n_s - 1u], t0 + 6000u - 10u);
}

TEST_CASE(a_trigger_past_the_end_of_a_trace_still_being_written_is_the_next)
{
    rig();
    run(200u);
    const uint32_t t0 = (uint32_t)(g_us / 100u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us, 0u, 0u);
    run(3900u);
    /* The console takes nothing from 100 ms before the end.  600 ms past
     * the end a command comes: the 4 s trace is over, written or not. */
    stall(700u);
    CHECK(sense_trace_active(&tr));
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 3u, 1900u);
    /* And 1 ms before the end of that one, an edge: its own to extend. */
    stall(100u);
    run(1000u);
    const char *rest = first_trace_only();
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.id, 1u);
    CHECK_EQ(seen.n_z, 1u);
    CHECK_EQ(seen.z_e, 't');
    CHECK_EQ(seen.n_c, 0u);
    CHECK_EQ(seen.z_m, 1u);
    CHECK_EQ(seen.z_s, seen.n_s);
    CHECK_EQ(seen.t[seen.n_s - 1u], t0 + 40000u - 10u);
    /* The next trace: the command's, with every sample since the first
     * one's end before its own. */
    char want[96];
    snprintf(want, sizeof(want), "$T v=3 n=2 trig=cmd t=%lu ms=%lu len=4000",
             (unsigned long)t0 + 46000ul, ((unsigned long)t0 + 46000ul) / 10ul);
    CHECK(strncmp(rest, want, strlen(want)) == 0);
    CHECK(strstr(rest, "$C t=") != NULL);
    CHECK(strstr(rest, "\r\n-6000,300\r\n") != NULL);
    CHECK(sense_trace_active(&tr));
    CHECK_EQ(tr.out.end_t, t0 + 46000u + 40000u);
}

TEST_CASE(a_start_after_a_stop_is_a_trace_of_its_own)
{
    rig();
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    stall(300u);
    sense_trace_key(&tr, 'x', g_us);
    stall(100u);
    /* The stopped trace still has 300 ms to write when `t` comes. */
    const uint32_t t1 = (uint32_t)(g_us / 100u);
    sense_trace_key(&tr, 't', g_us);
    /* More triggers than wait their turn: the ones with no place are
     * counted in the trace they would have been in. */
    for (unsigned k = 0u; k < SENSE_TRACE_MARKS; ++k) {
        sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us + 1000u, 0u, 0u);
    }
    run(300u);
    sense_trace_key(&tr, 'x', g_us);
    run(3u);
    CHECK(!sense_trace_active(&tr));
    const char *rest = first_trace_only();
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.id, 1u);
    CHECK_EQ(seen.z_e, 'k');
    CHECK_EQ(seen.n_k, 1u);
    CHECK_EQ(seen.z_m, 1u);
    char want[64];
    snprintf(want, sizeof(want), "$T v=3 n=2 trig=key t=%lu ",
             (unsigned long)t1);
    CHECK(strncmp(rest, want, strlen(want)) == 0);
    CHECK(strstr(rest, " m=8 ml=1 e=k\r\n") != NULL);
    /* A console that goes away takes the waiting triggers with it. */
    sense_trace_key(&tr, 't', g_us);
    run(10u);
    sense_trace_key(&tr, 'x', g_us);
    sense_trace_key(&tr, 't', g_us);
    tick();
    pass_as(SENSE_TRACE_LINE_MAX, false);
    run(20u);
    CHECK(!sense_trace_active(&tr));
}

/* A stopped trace with 300 ms still to write: a trigger now waits. */
static void owe_a_stopped_trace(void)
{
    rig();
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(100u);
    stall(300u);
    sense_trace_key(&tr, 'x', g_us);
    stall(100u);
    CHECK(sense_trace_active(&tr));
}

/* Channel 5's pulse on slot 0 in this pass, as the firmware gives it;
 * whether it is a command. */
static bool drive(uint16_t us)
{
    if (!sense_trace_pulse(&tr, 0u, 5u, us, g_us)) {
        return false;
    }
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_CMD, g_us, 5u, us);
    return true;
}

/* The console takes again until the trace numbered @p id is under way. */
static void run_to_trace(unsigned id)
{
    for (unsigned k = 0u; k < 30000u && tr.out.id != id; ++k) {
        run(1u);
    }
    CHECK_EQ(tr.out.id, id);
}

static void run_out(void)
{
    for (unsigned k = 0u; k < 30000u && sense_trace_active(&tr); ++k) {
        run(1u);
    }
    CHECK(!sense_trace_active(&tr));
}

/* The log from the $T line of the trace numbered @p id on, parsed. */
static void parse_from(unsigned id)
{
    char want[32];
    snprintf(want, sizeof(want), "$T v=3 n=%u ", id);
    const char *at = strstr(g_log, want);
    CHECK(at != NULL);
    if (at != NULL) {
        g_len = strlen(at);
        memmove(g_log, at, g_len + 1u);
    }
    parse();
}

static uint32_t now_t(void)
{
    return (uint32_t)(g_us / 100u);
}

TEST_CASE(a_command_that_waits_takes_its_slew_and_its_end_to_the_next_trace)
{
    owe_a_stopped_trace();
    CHECK(!drive(1100u));
    stall(1u);
    const uint32_t t_cmd = now_t();
    CHECK(drive(1200u));
    CHECK_EQ(tr.out.wait_n, 1u);
    /* 30 ms of slew: one entry behind the command, at the last change. */
    uint32_t t_last = 0u;
    for (unsigned k = 1u; k <= 30u; ++k) {
        stall(1u);
        t_last = now_t();
        CHECK(!drive((uint16_t)(1200u + k)));
        CHECK_EQ(tr.out.wait_n, 2u);
    }
    CHECK_EQ(tr.out.wait[1].kind, SENSE_TRACE_MARK_STEP);
    CHECK_EQ(tr.out.wait[1].t, t_last);
    /* Held still for 50 ms: the entry is the command's end. */
    for (unsigned k = 0u; k < 60u; ++k) {
        stall(1u);
        CHECK(!drive(1230u));
    }
    CHECK(!tr.out.watch[0].slewed);
    CHECK_EQ(tr.out.wait_n, 2u);
    CHECK_EQ(tr.out.wait[1].kind, SENSE_TRACE_MARK_DEST);
    CHECK_EQ(tr.out.wait[1].t, t_last);
    CHECK_EQ(tr.out.wait[1].us, 1230u);
    CHECK_EQ(tr.out.wait[1].cmd_t, t_cmd);
    run_to_trace(2u);
    CHECK_EQ(tr.out.wait_n, 0u);
    CHECK_EQ(tr.out.end_t, t_last + 40000u);
    run_out();
    parse_from(2u);
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_t, 1u);
    CHECK(strcmp(seen.trig, "cmd") == 0);
    CHECK_EQ(seen.t0, t_cmd);
    CHECK_EQ(seen.n_c, 1u);
    CHECK_EQ(seen.mark_us, 1200u);
    CHECK_EQ(seen.n_d, 1u);
    CHECK_EQ(seen.dest_t, t_last);
    CHECK_EQ(seen.dest_ch, 5u);
    CHECK_EQ(seen.dest_us, 1230u);
    CHECK_EQ(seen.dest_cmd, t_cmd);
    CHECK_EQ(seen.z_m, 2u);
    CHECK_EQ(seen.z_e, 't');
    CHECK_EQ(seen.z_s, seen.n_s);
    CHECK_EQ(seen.t[(seen.n_s - 1u) % MAX_S], t_last + 40000u - 10u);
}

/* A command that waits behind a trace that itself waits, and lies past
 * that trace's end: it waits again, its slew's end with it, and the $D
 * line in the trace after names it. */
TEST_CASE(a_command_that_waits_twice_keeps_its_time_on_its_end)
{
    owe_a_stopped_trace();
    CHECK(!drive(1100u));
    stall(1u);
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us, 0u, 0u);
    const uint32_t t_edge = now_t();
    stall(SENSE_TRACE_EDGE_MS + 100u);
    const uint32_t t_cmd = now_t();
    CHECK(drive(1200u));
    stall(1u);
    const uint32_t t_last = now_t();
    CHECK(!drive(1210u));
    for (unsigned k = 0u; k < 60u; ++k) {
        stall(1u);
        CHECK(!drive(1210u));
    }
    CHECK_EQ(tr.out.wait_n, 3u);
    CHECK_EQ(tr.out.wait[2].kind, SENSE_TRACE_MARK_DEST);
    CHECK_EQ(tr.out.wait[2].cmd_t, t_cmd);
    /* The edge's trace is next; the command lies past its end. */
    run_to_trace(2u);
    CHECK_EQ(tr.out.t0, t_edge);
    CHECK_EQ(tr.out.wait_n, 2u);
    CHECK_EQ(tr.out.wait[0].kind, SENSE_TRACE_TRIG_CMD);
    CHECK_EQ(tr.out.wait[1].kind, SENSE_TRACE_MARK_DEST);
    CHECK_EQ(tr.out.wait[1].cmd_t, t_cmd);
    run_to_trace(3u);
    run_out();
    parse_from(3u);
    CHECK_EQ(seen.n_bad, 0u);
    CHECK(strcmp(seen.trig, "cmd") == 0);
    CHECK_EQ(seen.t0, t_cmd);
    CHECK_EQ(seen.n_d, 1u);
    CHECK_EQ(seen.dest_t, t_last);
    CHECK_EQ(seen.dest_us, 1210u);
    CHECK_EQ(seen.dest_cmd, t_cmd);
}

TEST_CASE(a_slew_longer_than_a_trace_that_waits_holds_the_next_trace_open)
{
    owe_a_stopped_trace();
    CHECK(!drive(1100u));
    stall(1u);
    const uint32_t t_cmd = now_t();
    CHECK(drive(1200u));
    /* 4.5 s of slew with the console taking nothing, longer than the
     * 4 s the command asks for. */
    uint32_t t_last = 0u;
    uint16_t us = 0u;
    unsigned k = 0u;
    for (; k < 4500u; ++k) {
        stall(1u);
        t_last = now_t();
        us = (uint16_t)(1300u + (k & 1u));
        CHECK(!drive(us));
    }
    CHECK_EQ(tr.out.wait_n, 2u);
    CHECK_EQ(tr.out.wait[1].t, t_last);
    CHECK((int32_t)(t_last - (t_cmd + 40000u)) > 0);
    /* The console takes again; the slew goes on through the next
     * trace's start and 20 ms past it. */
    unsigned on = 0u;
    for (; k < 40000u && on < 20u; ++k) {
        run(1u);
        t_last = now_t();
        us = (uint16_t)(1300u + (k & 1u));
        CHECK(!drive(us));
        if (tr.out.id == 2u) {
            ++on;
        }
    }
    CHECK_EQ(tr.out.id, 2u);
    CHECK(sense_trace_active(&tr));
    CHECK_EQ(tr.out.end_t, t_last + 40000u);
    for (unsigned j = 0u; j < 60u; ++j) {
        run(1u);
        CHECK(!drive(us));
    }
    CHECK(!tr.out.watch[0].slewed);
    run_out();
    parse_from(2u);
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_t, 1u);
    CHECK_EQ(seen.t0, t_cmd);
    CHECK_EQ(seen.n_c, 1u);
    CHECK_EQ(seen.mark_us, 1200u);
    CHECK_EQ(seen.n_d, 1u);
    CHECK_EQ(seen.dest_t, t_last);
    CHECK_EQ(seen.dest_us, us);
    CHECK_EQ(seen.z_e, 't');
    CHECK_EQ(seen.z_s, seen.n_s);
    CHECK_EQ(seen.t[(seen.n_s - 1u) % MAX_S], t_last + 40000u - 10u);
}

TEST_CASE(a_slew_that_stops_as_its_trace_starts_ends_in_that_trace)
{
    owe_a_stopped_trace();
    CHECK(!drive(1100u));
    stall(1u);
    CHECK(drive(1200u));
    /* A change every pass while the command waits; the last one in the
     * pass before the next trace starts. */
    uint16_t us = 1200u;
    uint32_t t_last = 0u;
    while (tr.out.id == 1u && us < 3000u) {
        ++us;
        t_last = now_t();
        CHECK(!drive(us));
        run(1u);
    }
    CHECK_EQ(tr.out.id, 2u);
    CHECK(tr.out.watch[0].slewed);
    CHECK_EQ(tr.out.wait_n, 0u);
    CHECK_EQ(tr.out.end_t, t_last + 40000u);
    for (unsigned j = 0u; j < 60u; ++j) {
        CHECK(!drive(us));
        run(1u);
    }
    CHECK(!tr.out.watch[0].slewed);
    run_out();
    parse_from(2u);
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_c, 1u);
    CHECK_EQ(seen.n_d, 1u);
    CHECK_EQ(seen.dest_t, t_last);
    CHECK_EQ(seen.dest_us, us);
    CHECK_EQ(seen.z_m, 2u);
    CHECK_EQ(seen.t[(seen.n_s - 1u) % MAX_S], t_last + 40000u - 10u);
}

TEST_CASE(a_command_with_no_line_has_no_end_line_either)
{
    /* Waiting: a command of the channel waits, the places fill, and the
     * channel's next command finds none.  Its slew and its end are not
     * the first command's. */
    owe_a_stopped_trace();
    CHECK(!drive(1100u));
    stall(1u);
    CHECK(drive(1200u));
    for (unsigned k = 0u; k < 60u; ++k) {
        stall(1u);
        CHECK(!drive(1200u));
    }
    CHECK(!tr.out.watch[0].changed);
    for (unsigned k = 1u; k < SENSE_TRACE_MARKS; ++k) {
        sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us, 0u, 0u);
    }
    CHECK_EQ(tr.out.wait_n, SENSE_TRACE_MARKS);
    CHECK(drive(1500u));
    CHECK_EQ(tr.out.wait_lost, 1u);
    CHECK(tr.out.watch[0].orphan);
    stall(1u);
    CHECK(!drive(1510u));
    for (unsigned k = 0u; k < 60u; ++k) {
        stall(1u);
        CHECK(!drive(1520u));
    }
    CHECK(!tr.out.watch[0].slewed);
    CHECK_EQ(tr.out.wait_lost, 1u);
    CHECK_EQ(tr.out.wait_n, SENSE_TRACE_MARKS);
    for (unsigned k = 0u; k < tr.out.wait_n; ++k) {
        CHECK(tr.out.wait[k].kind <= SENSE_TRACE_TRIG_KEY);
    }
    run_out();
    parse_from(2u);
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_c, 1u);
    CHECK_EQ(seen.mark_us, 1200u);
    CHECK_EQ(seen.n_d, 0u);
    CHECK_EQ(seen.z_ml, 1u);

    /* In a trace: the same with the trigger queue full. */
    rig();
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(50u);
    CHECK(!drive(1100u));
    stall(1u);
    CHECK(drive(1200u));
    for (unsigned k = 0u; k < 60u; ++k) {
        stall(1u);
        CHECK(!drive(1200u));
    }
    while (tr.out.q_n < SENSE_TRACE_MARKS) {
        sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us, 0u, 0u);
    }
    CHECK_EQ(tr.out.n_mlost, 0u);
    CHECK(drive(1500u));
    CHECK_EQ(tr.out.n_mlost, 1u);
    CHECK(tr.out.watch[0].orphan);
    stall(1u);
    CHECK(!drive(1510u));
    run(100u);
    CHECK_EQ(tr.out.q_n, 0u);
    CHECK(!drive(1510u));
    CHECK(!tr.out.watch[0].slewed);
    CHECK_EQ(tr.out.q_n, 0u);
    CHECK_EQ(tr.out.n_mlost, 1u);
    /* The channel's next command has a line, and its end one. */
    CHECK(drive(1800u));
    CHECK(!tr.out.watch[0].orphan);
    run(1u);
    CHECK(!drive(1810u));
    for (unsigned k = 0u; k < 60u; ++k) {
        run(1u);
        CHECK(!drive(1810u));
    }
    sense_trace_key(&tr, 'x', g_us);
    run(3u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_c, 2u);
    CHECK_EQ(seen.mark_us, 1800u);
    CHECK_EQ(seen.n_d, 1u);
    CHECK_EQ(seen.dest_us, 1810u);
    CHECK_EQ(seen.z_ml, 1u);
}

TEST_CASE(a_waiting_slews_end_that_finds_no_place_is_counted)
{
    owe_a_stopped_trace();
    CHECK(!drive(1100u));
    stall(1u);
    CHECK(drive(1200u));
    for (unsigned k = 1u; k < SENSE_TRACE_MARKS; ++k) {
        sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us, 0u, 0u);
    }
    CHECK_EQ(tr.out.wait_n, SENSE_TRACE_MARKS);
    /* A change is no line: not counted. */
    stall(1u);
    CHECK(!drive(1210u));
    CHECK_EQ(tr.out.wait_lost, 0u);
    /* The end is one. */
    for (unsigned k = 0u; k < 60u; ++k) {
        stall(1u);
        CHECK(!drive(1210u));
    }
    CHECK_EQ(tr.out.wait_n, SENSE_TRACE_MARKS);
    CHECK_EQ(tr.out.wait_lost, 1u);
    run_out();
    parse_from(2u);
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_c, 1u);
    CHECK_EQ(seen.n_d, 0u);
    CHECK_EQ(seen.n_e, SENSE_TRACE_MARKS - 1u);
    CHECK_EQ(seen.z_ml, 1u);
}

TEST_CASE(a_slews_end_waits_again_with_a_command_that_waits_again)
{
    /* An edge waits, and 5 s after it a slewed command: past the end of
     * the trace the edge starts, so it waits again, its end behind it. */
    owe_a_stopped_trace();
    CHECK(!drive(1100u));
    sense_trace_trigger(&tr, SENSE_TRACE_TRIG_EDGE, g_us, 0u, 0u);
    stall(5000u);
    const uint32_t t_cmd = now_t();
    CHECK(drive(1200u));
    stall(1u);
    const uint32_t t_last = now_t();
    CHECK(!drive(1250u));
    for (unsigned k = 0u; k < 60u; ++k) {
        stall(1u);
        CHECK(!drive(1250u));
    }
    CHECK_EQ(tr.out.wait_n, 3u);
    CHECK_EQ(tr.out.wait[2].kind, SENSE_TRACE_MARK_DEST);
    run_to_trace(2u);
    CHECK_EQ(tr.out.trig, SENSE_TRACE_TRIG_EDGE);
    CHECK_EQ(tr.out.wait_n, 2u);
    CHECK_EQ(tr.out.wait[0].kind, SENSE_TRACE_TRIG_CMD);
    CHECK_EQ(tr.out.wait[1].kind, SENSE_TRACE_MARK_DEST);
    CHECK_EQ(tr.out.wait[1].t, t_last);
    CHECK_EQ(tr.out.wait[1].us, 1250u);
    run_to_trace(3u);
    CHECK_EQ(tr.out.wait_n, 0u);
    CHECK_EQ(tr.out.end_t, t_last + 40000u);
    run_out();
    parse_from(3u);
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_t, 1u);
    CHECK(strcmp(seen.trig, "cmd") == 0);
    CHECK_EQ(seen.t0, t_cmd);
    CHECK_EQ(seen.n_c, 1u);
    CHECK_EQ(seen.n_d, 1u);
    CHECK_EQ(seen.dest_t, t_last);
    CHECK_EQ(seen.dest_us, 1250u);
    CHECK_EQ(seen.z_m, 2u);
}

TEST_CASE(the_pulse_after_the_bank_let_go_is_a_command_however_soon)
{
    rig();
    run(200u);
    sense_trace_key(&tr, 't', g_us);
    run(20u);
    CHECK(!drive(1500u));
    run(1u);
    CHECK(drive(1600u));
    run(1u);
    CHECK(!drive(1610u));
    run(1u);
    /* Let go 1 ms after a change: the slewed command ends where it was. */
    CHECK(!drive(0u));
    CHECK(!tr.out.watch[0].slewed && !tr.out.watch[0].changed);
    run(1u);
    /* Driven again 1 ms later, at the pulse it had: a command, and what
     * follows within 50 ms its slew. */
    CHECK(drive(1610u));
    run(1u);
    CHECK(!drive(1620u));
    for (unsigned k = 0u; k < 60u; ++k) {
        run(1u);
        CHECK(!drive(1620u));
    }
    /* Let go while it stands still, and driven again at once. */
    CHECK(!drive(0u));
    run(1u);
    CHECK(drive(1620u));
    run(10u);
    sense_trace_key(&tr, 'x', g_us);
    run(3u);
    CHECK(!sense_trace_active(&tr));
    parse();
    CHECK_EQ(seen.n_bad, 0u);
    CHECK_EQ(seen.n_c, 3u);
    CHECK_EQ(seen.n_d, 2u);
    CHECK_EQ(seen.dest_us, 1620u);
    CHECK_EQ(seen.z_m, 6u);
    CHECK(strstr(g_log, "us=1610\r\n") != NULL);
}

TEST_CASE(a_slot_that_changes_hands_is_watched_afresh)
{
    rig();
    /* A slot let go of -- unbound, or another driver's -- and bound
     * again: its first pulse is where the watch starts, not a command. */
    CHECK(!sense_trace_pulse(&tr, 1u, 5u, 1500u, g_us));
    sense_trace_unwatch(&tr, 1u);
    CHECK(!sense_trace_pulse(&tr, 1u, 5u, 1900u, g_us + 1000000u));
    CHECK(sense_trace_pulse(&tr, 1u, 5u, 1100u, g_us + 2000000u));
    /* The same with another channel on the slot, and a slew of the old
     * channel's has no end line under the new one's name. */
    sense_trace_key(&tr, 't', g_us);
    CHECK(!sense_trace_pulse(&tr, 1u, 5u, 1110u, g_us + 2001000u));
    CHECK(tr.out.watch[1].slewed);
    CHECK(!sense_trace_pulse(&tr, 1u, 6u, 1700u, g_us + 3000000u));
    CHECK(!tr.out.watch[1].slewed);
    CHECK(!sense_trace_pulse(&tr, 1u, 6u, 1700u, g_us + 4000000u));
    run(20u);
    parse();
    CHECK_EQ(seen.n_d, 0u);
    CHECK(sense_trace_pulse(&tr, 1u, 6u, 1800u, g_us + 5000000u));
    /* One past the last slot is none. */
    sense_trace_unwatch(&tr, SENSE_TRACE_SLOTS);
}

int main(void)
{
    RUN(the_trigger_queue_full_by_0_and_1);
    RUN(the_consoles_stop_and_other_characters);
    RUN(a_changed_set_up_ends_a_trace);
    RUN(a_part_offline_and_a_reset_are_said_in_the_trace);
    RUN(a_trace_with_no_bus_has_a_header_and_ends_on_time);
    RUN(a_stop_under_a_backlog_writes_what_came_before_it);
    RUN(a_trigger_past_the_end_of_a_trace_still_being_written_is_the_next);
    RUN(a_start_after_a_stop_is_a_trace_of_its_own);
    RUN(a_slot_that_changes_hands_is_watched_afresh);
    RUN(a_command_that_waits_takes_its_slew_and_its_end_to_the_next_trace);
    RUN(a_command_that_waits_twice_keeps_its_time_on_its_end);
    RUN(a_slew_longer_than_a_trace_that_waits_holds_the_next_trace_open);
    RUN(a_slew_that_stops_as_its_trace_starts_ends_in_that_trace);
    RUN(a_command_with_no_line_has_no_end_line_either);
    RUN(a_waiting_slews_end_that_finds_no_place_is_counted);
    RUN(a_slews_end_waits_again_with_a_command_that_waits_again);
    RUN(the_pulse_after_the_bank_let_go_is_a_command_however_soon);
    return test_summary("sense_trace_next");
}
