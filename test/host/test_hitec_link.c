/*
 * The panel's half of the HITEC page (shared/bench/hitec_link.c) against
 * the page itself (shared/outputs/hitec_port.c) in a loopback through the
 * link dispatch: the way in and out, every refusal the page gives, a
 * request the page ends without running it, the key and the check, a
 * command without an answer 1, 2 and 3 times, one taken with its answer
 * lost (its SEQ tells), STOPPED and CLEAR, ABORT at each stage of a
 * request, POWER with and without a PD mini, at most 4 exchanges a pass.
 * test_hitec_link_loss.c has the lost link, restarts, SEQ and the clock.
 * The rig is hitec_link_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_link_rig.h"

/* ===================================================================== cases */

TEST_CASE(nothing_is_sent_before_the_link_or_to_an_iomcu_before_4_23)
{
    hitec_link_xfer_t x;
    const hitec_ses_step_t st = c_read_step(0x12u);

    bench_at(0u, SUPPLY_ON, 1u);
    CHECK_EQ(hitec_link_phase(&g_l), HITEC_LINK_DOWN);
    CHECK(!hitec_link_has_page(&g_l));
    CHECK_EQ(hitec_link_open(&g_l, CH, HITEC_CLASSIC, 0u, 1u),
             HITEC_LINK_START_DOWN);
    CHECK(!hitec_link_next(&g_l, g_ms, &x));
    hitec_link_came_up(&g_l, LINK_MINOR_HITEC - 1u);
    CHECK_EQ(hitec_link_phase(&g_l), HITEC_LINK_NO_PAGE);
    CHECK(!hitec_link_has_page(&g_l));
    run_for(2000u);
    CHECK_EQ(g_sent, 0u);
    CHECK_EQ(hitec_link_open(&g_l, CH, HITEC_CLASSIC, 0u, 1u),
             HITEC_LINK_START_NO_PAGE);
    CHECK_EQ(hitec_link_step(&g_l, CH, &st, MV, MA, 1u),
             HITEC_LINK_START_NO_PAGE);
    CHECK_EQ(hitec_link_close(&g_l, CH, 1u), HITEC_LINK_START_NO_PAGE);
    hitec_link_abort(&g_l);
    run_for(100u);
    CHECK_EQ(g_sent, 0u);
    CHECK_EQ(readout().minor, LINK_MINOR_HITEC - 1u);
    CHECK(!readout().known);
    /* 4.23 has the page, without the SUPPLY hold of 4.29. */
    hitec_link_came_up(&g_l, LINK_MINOR_HITEC);
    CHECK(hitec_link_has_page(&g_l));
    until_phase(HITEC_LINK_READY);
    CHECK(!readout().hold_supply);
    hitec_link_came_up(&g_l, LINK_MINOR_HITEC_HOLD);
    until_phase(HITEC_LINK_READY);
    CHECK(readout().hold_supply);
}

TEST_CASE(an_iomcu_that_refuses_the_page_is_left_alone)
{
    hitec_link_outcome_t out;

    bench_at(0u, SUPPLY_ON, 0u);
    hitec_link_came_up(&g_l, LINK_PROTOCOL_MINOR);
    run_for(20u);
    CHECK_EQ(hitec_link_phase(&g_l), HITEC_LINK_NO_PAGE);
    CHECK_EQ(g_sent, 1u);
    CHECK(!hitec_link_outcome(&g_l, &out));
    run_for(2000u);
    CHECK_EQ(g_sent, 1u);
}

TEST_CASE(a_request_answers_why_it_sends_nothing)
{
    hitec_ses_step_t st = c_read_step(0x12u);

    up();
    CHECK_EQ(hitec_link_open(NULL, CH, 0u, 0u, 1u), HITEC_LINK_START_ARG);
    CHECK_EQ(hitec_link_step(&g_l, CH, NULL, MV, MA, 1u),
             HITEC_LINK_START_ARG);
    CHECK_EQ(hitec_link_step(NULL, CH, &st, MV, MA, 1u),
             HITEC_LINK_START_ARG);
    CHECK_EQ(hitec_link_close(NULL, CH, 1u), HITEC_LINK_START_ARG);
    CHECK_EQ(hitec_link_open(&g_l, LINK_PG_PORTS_MAX, 0u, 0u, 1u),
             HITEC_LINK_START_ARG);
    CHECK_EQ(hitec_link_open(&g_l, CH, HITEC_D + 1u, 0u, 1u),
             HITEC_LINK_START_ARG);
    CHECK_EQ(hitec_link_open(&g_l, CH, HITEC_D, HITEC_DRIVE_PUSH_PULL + 1u,
                             1u),
             HITEC_LINK_START_ARG);
    CHECK_EQ(hitec_link_close(&g_l, LINK_PG_PORTS_MAX, 1u),
             HITEC_LINK_START_ARG);
    CHECK_EQ(hitec_link_clear(&g_l, LINK_PG_PORTS_MAX, 1u),
             HITEC_LINK_START_ARG);
    CHECK_EQ(hitec_link_step(&g_l, LINK_PG_PORTS_MAX, &st, MV, MA, 1u),
             HITEC_LINK_START_ARG);
    st.kind = HITEC_SES_STEP_WAIT;
    CHECK_EQ(hitec_link_step(&g_l, CH, &st, MV, MA, 1u), HITEC_LINK_START_ARG);
    st.kind = HITEC_SES_STEP_DONE;
    CHECK_EQ(hitec_link_step(&g_l, CH, &st, MV, MA, 1u), HITEC_LINK_START_ARG);
    /* A POWER's transaction is a LISTEN. */
    st.kind = HITEC_SES_STEP_POWER;
    CHECK_EQ(hitec_link_step(&g_l, CH, &st, MV, MA, 1u), HITEC_LINK_START_ARG);
    st = c_read_step(0x12u);
    st.decoder = HITEC_DEC_LISTEN + 1u;
    CHECK_EQ(hitec_link_step(&g_l, CH, &st, MV, MA, 1u), HITEC_LINK_START_ARG);
    st = c_read_step(0x12u);
    st.xfer.n_tx = 0u;
    CHECK_EQ(hitec_link_step(&g_l, CH, &st, MV, MA, 1u), HITEC_LINK_START_ARG);
    st.xfer.n_tx = HITEC_TX_MAX + 1u;
    CHECK_EQ(hitec_link_step(&g_l, CH, &st, MV, MA, 1u), HITEC_LINK_START_ARG);
    run_for(20u);
    CHECK_EQ(g_cmds, 0u);
    CHECK_EQ(hitec_link_phase(&g_l), HITEC_LINK_READY);
    /* One request at a time. */
    CHECK_EQ(hitec_link_open(&g_l, CH, HITEC_CLASSIC, 0u, 1u),
             HITEC_LINK_START_OK);
    CHECK_EQ(hitec_link_open(&g_l, CH, HITEC_CLASSIC, 0u, 2u),
             HITEC_LINK_START_BUSY);
    CHECK_EQ(hitec_link_clear(&g_l, CH, 2u), HITEC_LINK_START_BUSY);
}

TEST_CASE(open_takes_the_port_and_reads_its_line)
{
    hitec_link_outcome_t out;
    hitec_link_readout_t r;
    uint32_t t0;

    up();
    r = readout();
    CHECK(r.known);
    CHECK(!r.held);
    CHECK(!r.ours);
    CHECK_EQ(r.pin, HITEC_LINK_NO_PIN);
    t0 = g_ms;
    out = open_as(CH, HITEC_D, HITEC_DRIVE_PUSH_PULL);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(out.op, LINK_HITEC_OP_OPEN);
    CHECK_EQ(out.port, CH);
    CHECK_EQ(out.id, g_id);
    CHECK(out.taken);
    CHECK_EQ(out.result, LINK_HITEC_RESULT_DONE);
    CHECK_EQ(out.ses.run, HITEC_SES_RAN);
    CHECK(!out.have_cap);
    CHECK(out.have_run);
    /* STOPPING lasts 50ms. */
    CHECK(g_ms - t0 >= HITEC_PORT_STOP_MS);
    CHECK(g_ms - t0 <= HITEC_PORT_STOP_MS + 5u * HITEC_LINK_POLL_MS);
    /* The command: OPEN on CH, no key, the check of the staged block. */
    CHECK_EQ(g_last_cmd[LINK_HT_W_CMD],
             LINK_HT_W_CMD_OF(LINK_HITEC_OP_OPEN, CH));
    CHECK_EQ(g_last_cmd[LINK_HT_W_SEQ], 1u);
    CHECK_EQ(g_last_cmd[LINK_HT_W_KEY], 0u);
    CHECK_EQ(g_last_cmd[LINK_HT_W_CRC],
             link_kst_crc(g_port.staged, LINK_HT_W_STAGED));
    CHECK_EQ(g_hw.take_family, HITEC_D);
    CHECK_EQ(g_hw.take_drive, HITEC_DRIVE_PUSH_PULL);
    CHECK_EQ(g_port.view, LINK_HITEC_VIEW_RUN);
    r = readout();
    CHECK_EQ(r.state, LINK_HITEC_OPEN);
    CHECK_EQ(r.port_no, CH);
    CHECK(r.held);
    CHECK(r.ours);
    CHECK(!r.stopped);
    CHECK_EQ(r.op, LINK_HITEC_OP_OPEN);
    CHECK_EQ(r.seq, 1u);
    CHECK(r.have_line);
    CHECK_EQ(r.pin, PIN);
    CHECK_EQ(r.line, LINK_HT_LINE_OF(HITEC_D, HITEC_DRIVE_PUSH_PULL));
    CHECK_EQ(r.bit_ns, hitec_bits_ns(HITEC_D, 1u));
    CHECK((r.flags & LINK_HT_F_RAIL) != 0u);
    CHECK_EQ(g_max_pass, HITEC_LINK_PASS_MAX);
    /* CLOSE gives the pin back; the port is no longer this module's. */
    out = run_bare(LINK_HITEC_OP_CLOSE, CH);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(out.op, LINK_HITEC_OP_CLOSE);
    CHECK_EQ(out.result, LINK_HITEC_RESULT_DONE);
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_FREE);
    CHECK_EQ(g_hw.gives, 1u);
    r = readout();
    CHECK(!r.held);
    CHECK(!r.ours);
    CHECK(!r.have_line);
    /* The build's second port, with a pin of its own. */
    g_hw.pin[OTHER] = PIN + 1u;
    out = open_as(OTHER, HITEC_CLASSIC, 0u);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(out.port, OTHER);
    CHECK_EQ(out.result, LINK_HITEC_RESULT_DONE);
    r = readout();
    CHECK_EQ(r.port_no, OTHER);
    CHECK(r.ours);
    CHECK_EQ(r.pin, PIN + 1u);
    CHECK_EQ(g_hw.take_port, OTHER);
}

TEST_CASE(a_classic_read_comes_back_as_the_step_result)
{
    const hitec_ses_step_t st = c_read_step(0x12u);
    hitec_link_outcome_t out;
    unsigned passes;

    opened(HITEC_CLASSIC);
    passes = g_passes;
    g_sent = 0;
    out = run_step(&st);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(out.op, LINK_HITEC_OP_SEND);
    CHECK_EQ(out.result, LINK_HITEC_RESULT_DONE);
    CHECK_EQ(out.ses.run, HITEC_SES_RAN);
    CHECK_EQ(out.ses.rx, HITEC_RX_OK);
    CHECK_EQ(out.ses.value, g_drv.eeprom[0x12]);
    CHECK(out.have_cap);
    CHECK_EQ(out.n, 2u);
    CHECK_EQ(out.bytes[0], g_drv.eeprom[0x12]);
    CHECK_EQ(out.bytes[1], HITEC_C_ACK);
    CHECK_EQ((out.cap >> 12) & 0x3u, LINK_HITEC_DEC_C);
    CHECK(out.have_run);
    CHECK_EQ(out.window_us, HITEC_C_WINDOW_US);
    CHECK(out.t_us > 0u && out.t_us <= 10000u);
    CHECK(out.delay_ns >= HITEC_C_REPLY_MIN_NS);
    CHECK(out.delay_ns <= HITEC_C_REPLY_MAX_NS);
    CHECK_EQ(out.framing, 0u);
    /* The key goes with every SEND; the frame is the one the page ran. */
    CHECK_EQ(g_last_cmd[LINK_HT_W_KEY], LINK_HITEC_WRITE_KEY);
    CHECK_EQ(g_drv.x.n_tx, 4u);
    CHECK_EQ(g_drv.x.tx[1], 0x12u);
    CHECK_EQ(g_drv.starts, 1u);
    /* 5 staged windows, the command, the verdict, a poll, the run: 9
     * exchanges in 3 passes of 5ms. */
    CHECK_EQ(g_sent, 9u);
    CHECK_EQ(g_passes - passes, 3u);
    CHECK_EQ(readout().count, 1u);
}

TEST_CASE(a_d_series_read_comes_back_as_the_step_result)
{
    const hitec_ses_step_t st = d_read_step(0x32u);
    hitec_link_outcome_t out;
    unsigned passes;

    opened(HITEC_D);
    g_drv.dreg[0x32] = 0x0007u;
    passes = g_passes;
    g_sent = 0;
    out = run_step(&st);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(out.ses.run, HITEC_SES_RAN);
    CHECK_EQ(out.ses.rx, HITEC_RX_OK);
    CHECK_EQ(out.ses.value, 0x0007u);
    CHECK_EQ(out.answer_id, 0u);
    CHECK_EQ(out.n, HITEC_D_READ_RX);
    CHECK_EQ(out.bytes[0], HITEC_D_HEADER_ANSWER);
    CHECK_EQ(out.bytes[2], 0x32u);
    CHECK(out.delay_ns >= HITEC_D_ANSWER_MIN_NS);
    CHECK_EQ(out.window_us, HITEC_D_WINDOW_US);
    /* About 7.4ms on the wire: 10 exchanges, the 5 staged windows, the
     * command, the verdict, 2 polls and the run, in 4 passes of 5ms. */
    CHECK_EQ(g_sent, 10u);
    CHECK_EQ(g_passes - passes, 4u);
}

TEST_CASE(every_refusal_of_the_page_reaches_the_outcome)
{
    hitec_ses_step_t st;
    hitec_link_outcome_t out;

    /* NOT_OPEN: a step, a CLEAR, a CLOSE on a free port. */
    up();
    st = c_read_step(0x12u);
    out = run_step(&st);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_REFUSED);
    CHECK_EQ(out.refusal, LINK_HITEC_REF_NOT_OPEN);
    CHECK(out.taken);
    CHECK_EQ(out.ses.run, HITEC_SES_FAILED);
    CHECK_EQ(run_bare(LINK_HITEC_OP_CLEAR, CH).refusal,
             LINK_HITEC_REF_NOT_OPEN);
    CHECK_EQ(run_bare(LINK_HITEC_OP_CLOSE, CH).refusal,
             LINK_HITEC_REF_NOT_OPEN);
    /* PORT: one the build does not have, the last of the protocol's. */
    out = open_as(LINK_PG_PORTS_MAX - 1u, HITEC_CLASSIC, 0u);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_REFUSED);
    CHECK_EQ(out.refusal, LINK_HITEC_REF_PORT);
    CHECK_EQ(run_bare(LINK_HITEC_OP_CLEAR, LINK_PG_PORTS_MAX - 1u).refusal,
             LINK_HITEC_REF_PORT);
    CHECK_EQ(run_bare(LINK_HITEC_OP_CLOSE, LINK_PG_PORTS_MAX - 1u).refusal,
             LINK_HITEC_REF_PORT);
    CHECK_EQ(hitec_link_step(&g_l, LINK_PG_PORTS_MAX - 1u, &st, MV, MA,
                             ++g_id),
             HITEC_LINK_START_OK);
    CHECK_EQ(outcome().refusal, LINK_HITEC_REF_PORT);
    /* NO_PIN: the second port has none. */
    CHECK_EQ(open_as(OTHER, HITEC_CLASSIC, 0u).refusal,
             LINK_HITEC_REF_NO_PIN);
    /* RANGE: a classic line driven push-pull. */
    CHECK_EQ(open_as(CH, HITEC_CLASSIC, HITEC_DRIVE_PUSH_PULL).refusal,
             LINK_HITEC_REF_RANGE);
    /* HELD: the KST page holds the port. */
    hitec_port_other_mask(&g_port, 1u << CH);
    CHECK_EQ(open_as(CH, HITEC_CLASSIC, 0u).refusal, LINK_HITEC_REF_HELD);
    hitec_port_other_mask(&g_port, 0u);
    /* ARMED. */
    hitec_port_bench_armed(&g_port, true);
    CHECK_EQ(open_as(CH, HITEC_CLASSIC, 0u).refusal, LINK_HITEC_REF_ARMED);
    hitec_port_bench_armed(&g_port, false);
    /* UNSAFE: the heartbeat not trusted. */
    g_safe = false;
    CHECK_EQ(open_as(CH, HITEC_CLASSIC, 0u).refusal, LINK_HITEC_REF_UNSAFE);
    g_safe = true;
    run_for(10u);
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_FREE);
    CHECK_EQ(g_hw.takes, 0u);

    /* BUSY: OPEN on the port held. */
    opened(HITEC_CLASSIC);
    out = open_as(CH, HITEC_CLASSIC, 0u);
    CHECK_EQ(out.refusal, LINK_HITEC_REF_BUSY);
    CHECK_EQ(g_hw.takes, 1u);
    /* PORT: a step on a port that is not the one held. */
    st = c_read_step(0x12u);
    CHECK_EQ(hitec_link_step(&g_l, OTHER, &st, MV, MA, ++g_id),
             HITEC_LINK_START_OK);
    CHECK_EQ(outcome().refusal, LINK_HITEC_REF_PORT);
    /* RANGE: the D series decoder on a classic line; a LEVEL with one. */
    st = d_read_step(0x32u);
    st.xfer.family = HITEC_CLASSIC;
    CHECK_EQ(run_step(&st).refusal, LINK_HITEC_REF_RANGE);
    CHECK(hitec_level_xfer(HITEC_CLASSIC, 0u, 100u, &st.xfer));
    st.decoder = HITEC_DEC_LISTEN;
    CHECK_EQ(run_step(&st).refusal, LINK_HITEC_REF_RANGE);
    /* NO_KEY: the key lost on the way. */
    st = c_read_step(0x12u);
    g_strip_key = true;
    CHECK_EQ(run_step(&st).refusal, LINK_HITEC_REF_NO_KEY);
    g_strip_key = false;
    /* STAGED: another writer changes a staged register before the
     * command. */
    g_poke_staged = 1u;
    CHECK_EQ(run_step(&st).refusal, LINK_HITEC_REF_STAGED);
    /* SUPPLY_RANGE: 7000mV, and 10,001ms off. */
    st = power_step(HITEC_CLASSIC, 0u, 1000u);
    CHECK_EQ(hitec_link_step(&g_l, CH, &st, 7000u, MA, ++g_id),
             HITEC_LINK_START_OK);
    CHECK_EQ(outcome().refusal, LINK_HITEC_REF_SUPPLY_RANGE);
    st.off_ms = LINK_HITEC_OFF_MS_MAX + 1u;
    CHECK_EQ(run_step(&st).refusal, LINK_HITEC_REF_SUPPLY_RANGE);
    /* STOPPED. */
    stop_pulse();
    st = c_read_step(0x12u);
    CHECK_EQ(run_step(&st).refusal, LINK_HITEC_REF_STOPPED);
    /* UNSAFE for a CLEAR while the heartbeat is not trusted. */
    g_safe = false;
    CHECK_EQ(run_bare(LINK_HITEC_OP_CLEAR, CH).refusal,
             LINK_HITEC_REF_UNSAFE);
    g_safe = true;
    /* Nothing of them reached the wire. */
    CHECK_EQ(g_drv.starts, 0u);
    CHECK_EQ(g_hw.offs, 0u);
}

TEST_CASE(a_request_the_page_ends_without_running_it_is_failed)
{
    hitec_ses_step_t st = c_read_step(0x12u);
    hitec_link_outcome_t out;

    /* The driver refuses the transaction. */
    opened(HITEC_CLASSIC);
    g_drv.start_ok = false;
    out = run_step(&st);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(out.result, LINK_HITEC_RESULT_START);
    CHECK_EQ(out.ses.run, HITEC_SES_FAILED);
    CHECK(!out.have_cap);
    /* The supply refuses the ON. */
    g_drv.start_ok = true;
    g_hw.set_ok = false;
    st = power_step(HITEC_CLASSIC, 0u, 1000u);
    out = run_step(&st);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(out.result, LINK_HITEC_RESULT_SUPPLY);
    CHECK_EQ(out.ses.run, HITEC_SES_FAILED);
    /* The driver gets no line: the port is free again. */
    up();
    g_hw.take_ok = false;
    out = open_as(CH, HITEC_D, 0u);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(out.result, LINK_HITEC_RESULT_NO_LINE);
    CHECK_EQ(out.ses.run, HITEC_SES_FAILED);
    CHECK(!out.have_run);
    CHECK(!readout().held);
    CHECK(!readout().ours);
}

TEST_CASE(a_capture_says_16_bytes_at_most)
{
    const hitec_ses_step_t st = c_read_step(0x12u);
    hitec_link_outcome_t out;

    opened(HITEC_CLASSIC);
    g_fake_cap = true;
    out = run_step(&st);
    CHECK(out.have_cap);
    CHECK_EQ(out.n, HITEC_RX_MAX);
    CHECK_EQ(out.bytes[0], g_drv.eeprom[0x12]);
    CHECK_EQ(out.bytes[HITEC_RX_MAX - 1u], 0u);
}

TEST_CASE(power_needs_a_pd_mini_that_answers)
{
    const hitec_ses_step_t st = power_step(HITEC_D, 0u, 1000u);
    const supply_kind_t kinds[2] = { SUPPLY_NONE, SUPPLY_OFFLINE };

    for (unsigned i = 0; i < 2u; ++i) {
        hitec_link_outcome_t out;

        up_at(0u, kinds[i]);
        out = open_as(CH, HITEC_D, 0u);
        CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
        CHECK_EQ(readout().flags & LINK_HT_F_RAIL, 0u);
        out = run_step(&st);
        CHECK_EQ(out.kind, HITEC_LINK_OUT_REFUSED);
        CHECK_EQ(out.refusal, LINK_HITEC_REF_NO_SUPPLY);
        CHECK_EQ(out.ses.run, HITEC_SES_FAILED);
        CHECK_EQ(g_hw.sets, 0u);
    }
}

TEST_CASE(power_cycles_the_supply_and_listens_from_the_on)
{
    hitec_ses_step_t st = power_step(HITEC_CLASSIC, 300u, 100000u);
    hitec_link_outcome_t out;
    uint32_t t0;

    opened(HITEC_CLASSIC);
    st.decoder = HITEC_DEC_LISTEN;
    st.dec_arg = 0x5Au;
    t0 = g_ms;
    out = run_step(&st);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(out.op, LINK_HITEC_OP_POWER);
    CHECK_EQ(out.result, LINK_HITEC_RESULT_DONE);
    CHECK_EQ(out.ses.run, HITEC_SES_RAN);
    CHECK_EQ(out.ses.rx, HITEC_RX_OK);
    CHECK_EQ(out.ses.value, 0x5Au);
    CHECK(out.have_cap);
    CHECK_EQ(out.n, 1u);
    CHECK_EQ(out.bytes[0], 0x5Au);
    CHECK_EQ(g_hw.offs, 1u);
    CHECK_EQ(g_hw.ons, 1u);
    CHECK_EQ(g_hw.v.set_mv, MV);
    CHECK_EQ(g_hw.v.set_ma, MA);
    /* The PD mini reads off 20ms late, then 300ms off, then the boot byte
     * 30ms after the ON. */
    CHECK(g_hw.on_ms - t0 >= LAG_MS + 300u);
    CHECK(g_ms - t0 >= LAG_MS + 300u + 30u);
    /* A LISTEN without an answer goes without the key. */
    CHECK_EQ(g_last_cmd[LINK_HT_W_KEY], 0u);
}

TEST_CASE(a_listen_that_answers_carries_the_key)
{
    hitec_ses_step_t st = listen_step(HITEC_CLASSIC, 2000000u, 1u, true);
    hitec_link_outcome_t out;

    opened(HITEC_CLASSIC);
    g_drv.listen_n = 1u;
    st.decoder = HITEC_DEC_LISTEN;
    st.dec_arg = 0xA0u;
    out = run_step(&st);
    CHECK_EQ(out.ses.run, HITEC_SES_RAN);
    CHECK_EQ(out.ses.rx, HITEC_RX_OK);
    CHECK_EQ(g_last_cmd[LINK_HT_W_KEY], LINK_HITEC_WRITE_KEY);
    CHECK((out.cap & LINK_HT_CAP_ANSWERED) != 0u);
    CHECK_EQ(g_drv.x.answer_byte, HITEC_C_ACK);
}

TEST_CASE(a_level_holds_the_line_without_the_key)
{
    hitec_ses_step_t st = listen_step(HITEC_CLASSIC, 1000u, 1u, false);
    hitec_link_outcome_t out;

    opened(HITEC_CLASSIC);
    CHECK(hitec_level_xfer(HITEC_CLASSIC, 0u, 2000u, &st.xfer));
    out = run_step(&st);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(out.op, LINK_HITEC_OP_LEVEL);
    CHECK_EQ(out.ses.run, HITEC_SES_RAN);
    CHECK(out.have_cap);
    CHECK_EQ(out.n, 0u);
    CHECK_EQ(g_last_cmd[LINK_HT_W_KEY], 0u);
    CHECK_EQ(g_drv.x.kind, HITEC_XFER_LEVEL);
    CHECK_EQ(g_drv.x.level, 0u);
    CHECK_EQ(g_drv.x.level_us, 2000u);
    /* High on an open-drain line: the driver refuses it. */
    CHECK(hitec_level_xfer(HITEC_CLASSIC, 1u, 2000u, &st.xfer));
    out = run_step(&st);
    CHECK_EQ(out.result, LINK_HITEC_RESULT_START);
    CHECK_EQ(out.ses.run, HITEC_SES_FAILED);
}

TEST_CASE(a_command_whose_answer_is_lost_once_or_twice_is_taken_once)
{
    const hitec_ses_step_t st = c_read_step(0x12u);

    for (unsigned lost = 1; lost <= 2u; ++lost) {
        hitec_link_outcome_t out;

        opened(HITEC_CLASSIC);
        memset(g_op_cmds, 0, sizeof(g_op_cmds));
        g_lose_cmd_reply = lost;
        out = run_step(&st);
        CHECK_EQ(g_op_cmds[LINK_HITEC_OP_SEND], lost + 1u);
        CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
        CHECK(out.taken);
        CHECK_EQ(out.ses.run, HITEC_SES_RAN);
        CHECK_EQ(g_drv.starts, 1u);
        /* And a command lost before the page, as often. */
        memset(g_op_cmds, 0, sizeof(g_op_cmds));
        g_lose_cmd = lost;
        out = run_step(&st);
        CHECK_EQ(g_op_cmds[LINK_HITEC_OP_SEND], lost + 1u);
        CHECK_EQ(out.ses.run, HITEC_SES_RAN);
        CHECK_EQ(g_drv.starts, 2u);
    }
}

TEST_CASE(a_command_unheard_3_times_is_given_up_and_the_seq_says_not_taken)
{
    const hitec_ses_step_t st = c_read_step(0x12u);
    hitec_link_outcome_t out;
    uint16_t seq;

    opened(HITEC_CLASSIC);
    seq = g_port.seq;
    g_lose_cmd = HITEC_LINK_RETRIES;
    out = run_step(&st);
    CHECK_EQ(g_op_cmds[LINK_HITEC_OP_SEND], HITEC_LINK_RETRIES);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_NO_ANSWER);
    CHECK(!out.taken);
    CHECK_EQ(out.ses.run, HITEC_SES_FAILED);
    CHECK_EQ(g_drv.starts, 0u);
    CHECK_EQ(g_port.seq, seq);
    /* In step with the page: the next request goes at once. */
    CHECK_EQ(hitec_link_phase(&g_l), HITEC_LINK_READY);
    CHECK_EQ(run_step(&st).ses.run, HITEC_SES_RAN);
    CHECK_EQ(g_port.seq, (uint16_t)(seq + 1u));
}

TEST_CASE(a_command_taken_with_3_answers_lost_runs_and_the_seq_says_so)
{
    const hitec_ses_step_t st = c_read_step(0x12u);
    hitec_link_outcome_t out;

    opened(HITEC_CLASSIC);
    g_lose_cmd_reply = HITEC_LINK_RETRIES;
    out = run_step(&st);
    CHECK_EQ(g_op_cmds[LINK_HITEC_OP_SEND], HITEC_LINK_RETRIES);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK(out.taken);
    CHECK_EQ(out.ses.run, HITEC_SES_RAN);
    CHECK_EQ(out.ses.value, g_drv.eeprom[0x12]);
    CHECK_EQ(g_drv.starts, 1u);
}

TEST_CASE(the_deciding_read_has_its_own_tries)
{
    const hitec_ses_step_t st = c_read_step(0x12u);
    hitec_link_outcome_t out;
    unsigned sent;

    opened(HITEC_CLASSIC);
    g_lose_cmd_reply = HITEC_LINK_RETRIES;
    g_lose_status = HITEC_LINK_RETRIES - 1u;
    out = run_step(&st);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(out.ses.run, HITEC_SES_RAN);
    /* All three lost: the request is given up, the page read again. */
    g_lose_cmd_reply = HITEC_LINK_RETRIES;
    g_lose_status = HITEC_LINK_RETRIES;
    out = run_step(&st);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_NO_ANSWER);
    CHECK_EQ(out.ses.run, HITEC_SES_FAILED);
    CHECK_EQ(hitec_link_phase(&g_l), HITEC_LINK_SYNC);
    until_phase(HITEC_LINK_READY);
    /* Nothing of the request stays owed: the held port's polls only. */
    sent = g_sent;
    run_for(10u * HITEC_LINK_HELD_MS);
    CHECK(g_sent - sent >= 9u && g_sent - sent <= 11u);
    CHECK_EQ(run_step(&st).ses.run, HITEC_SES_RAN);
}

TEST_CASE(polls_unanswered_3_times_end_a_running_step)
{
    hitec_ses_step_t st = plus_listen(200000u);
    hitec_link_outcome_t out;

    opened(HITEC_CLASSIC);
    start_until(&st, HITEC_LINK_RUNNING);
    g_lose_status = HITEC_LINK_RETRIES - 1u;
    run_for(5u * HITEC_LINK_POLL_MS);
    CHECK_EQ(hitec_link_phase(&g_l), HITEC_LINK_RUNNING);
    g_lose_status = HITEC_LINK_RETRIES;
    out = outcome();
    CHECK_EQ(out.kind, HITEC_LINK_OUT_NO_ANSWER);
    CHECK(out.taken);
    CHECK_EQ(out.ses.run, HITEC_SES_FAILED);
    /* The transaction ends at the page on its own. */
    until_phase(HITEC_LINK_READY);
    run_for(300u);
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_OPEN);
    st = c_read_step(0x12u);
    CHECK_EQ(run_step(&st).ses.run, HITEC_SES_RAN);
}

TEST_CASE(a_status_refused_for_another_reason_is_asked_again)
{
    const hitec_ses_step_t st = c_read_step(0x12u);
    hitec_link_outcome_t out;

    opened(HITEC_CLASSIC);
    g_nack_status = 2u;
    out = run_step(&st);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(out.ses.run, HITEC_SES_RAN);
    CHECK_EQ(g_nack_status, 0u);
}

TEST_CASE(a_read_answered_without_registers_counts_as_no_answer)
{
    const hitec_ses_step_t st = c_read_step(0x12u);
    hitec_link_outcome_t out;
    hitec_link_xfer_t x;
    unsigned reads = 0;

    opened(HITEC_CLASSIC);
    CHECK_EQ(hitec_link_step(&g_l, CH, &st, MV, MA, ++g_id),
             HITEC_LINK_START_OK);
    for (unsigned k = 0; k < 200u
                         && hitec_link_phase(&g_l) != HITEC_LINK_FETCHING;
         ++k) {
        tick_as(true, false);
        one_exchange();
    }
    CHECK_EQ(hitec_link_phase(&g_l), HITEC_LINK_FETCHING);
    /* The read of 16 to 31, and then of 0 to 15, acknowledged with no
     * registers: asked again, twice each. */
    for (unsigned k = 0; k < 2u; ++k) {
        CHECK(hitec_link_next(&g_l, g_ms, &x));
        CHECK(!x.write);
        CHECK_EQ(x.n, LINK_HT_COUNT);
        hitec_link_done(&g_l, HITEC_LINK_ACK, NULL);
        reads++;
    }
    CHECK_EQ(hitec_link_phase(&g_l), HITEC_LINK_FETCHING);
    one_exchange();
    CHECK(hitec_link_outcome(&g_l, &out));
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK(out.have_run);
    for (unsigned k = 0; k < 2u; ++k) {
        for (unsigned i = 0; i < HITEC_LINK_HELD_MS; ++i) {
            tick_as(true, false);
        }
        CHECK(hitec_link_next(&g_l, g_ms, &x));
        CHECK_EQ(x.off, LINK_HT_STATE);
        hitec_link_done(&g_l, HITEC_LINK_ACK, NULL);
        reads++;
    }
    CHECK_EQ(reads, 4u);
    CHECK_EQ(g_l.tries, 2u);
    CHECK(readout().known);
    CHECK_EQ(hitec_link_phase(&g_l), HITEC_LINK_READY);
    /* The third read answers: the tries start again. */
    for (unsigned i = 0; i < HITEC_LINK_HELD_MS; ++i) {
        tick_as(true, false);
    }
    one_exchange();
    CHECK_EQ(g_l.tries, 0u);
}

TEST_CASE(a_frame_the_page_refuses_ends_the_request)
{
    hitec_ses_step_t st = c_read_step(0x12u);
    hitec_link_outcome_t out;

    opened(HITEC_CLASSIC);
    g_nack_stage = LINK_NACK_BAD_RANGE;
    out = run_step(&st);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_NACK);
    CHECK_EQ(out.nack, LINK_NACK_BAD_RANGE);
    CHECK(!out.taken);
    CHECK_EQ(g_op_cmds[LINK_HITEC_OP_SEND], 0u);
    g_nack_stage = 0u;
    /* The read of 16 to 31 refused: the step ran. */
    g_nack_run = LINK_NACK_BAD_RANGE;
    out = run_step(&st);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_NACK);
    CHECK(out.taken);
    CHECK_EQ(out.ses.run, HITEC_SES_FAILED);
    CHECK_EQ(g_drv.starts, 1u);
    g_nack_run = 0u;
    st = c_read_step(0x13u);
    CHECK_EQ(run_step(&st).ses.value, g_drv.eeprom[0x13]);
}

TEST_CASE(a_command_behind_another_writer_is_refused_and_the_page_read)
{
    const hitec_ses_step_t st = c_read_step(0x12u);
    hitec_link_outcome_t out;

    opened(HITEC_CLASSIC);
    g_other_cmd = 1u;
    out = run_step(&st);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_NACK);
    CHECK_EQ(out.nack, LINK_NACK_BAD_VALUE);
    CHECK(!out.taken);
    CHECK_EQ(g_drv.starts, 0u);
    until_phase(HITEC_LINK_READY);
    CHECK_EQ(run_step(&st).ses.run, HITEC_SES_RAN);
}

TEST_CASE(stopped_then_clear_lets_steps_run_again)
{
    const hitec_ses_step_t st = c_read_step(0x12u);
    hitec_link_outcome_t out;

    opened(HITEC_CLASSIC);
    stop_pulse();
    run_for(HITEC_LINK_HELD_MS + 2u * HITEC_LINK_POLL_MS);
    CHECK(readout().stopped);
    CHECK(readout().held);
    CHECK(readout().ours);
    CHECK_EQ(run_step(&st).refusal, LINK_HITEC_REF_STOPPED);
    out = run_bare(LINK_HITEC_OP_CLEAR, CH);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(out.op, LINK_HITEC_OP_CLEAR);
    CHECK_EQ(out.result, LINK_HITEC_RESULT_DONE);
    CHECK(!readout().stopped);
    CHECK_EQ(run_step(&st).ses.run, HITEC_SES_RAN);
}

TEST_CASE(a_stop_during_a_step_cuts_it)
{
    const hitec_ses_step_t st = plus_listen(500000u);
    hitec_link_outcome_t out;

    opened(HITEC_CLASSIC);
    start_until(&st, HITEC_LINK_RUNNING);
    run_for(20u);
    stop_pulse();
    out = outcome();
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(out.result, LINK_HITEC_RESULT_CUT);
    CHECK_EQ(out.ses.run, HITEC_SES_CUT);
    CHECK_EQ(out.ses.rx, HITEC_RX_ERR_ABORTED);
    CHECK(readout().stopped);
}

TEST_CASE(abort_while_busy_cuts_the_step_and_keeps_the_port)
{
    const hitec_ses_step_t st = plus_listen(2000000u);
    hitec_link_outcome_t out;
    uint32_t t0;

    opened(HITEC_CLASSIC);
    start_until(&st, HITEC_LINK_RUNNING);
    run_for(100u);
    CHECK_EQ(readout().state, LINK_HITEC_BUSY);
    CHECK_EQ(readout().flags, LINK_HT_F_RAIL);
    t0 = g_ms;
    hitec_link_abort(&g_l);
    out = outcome();
    CHECK_EQ(g_op_cmds[LINK_HITEC_OP_ABORT], 1u);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(out.result, LINK_HITEC_RESULT_CUT);
    CHECK_EQ(out.ses.run, HITEC_SES_CUT);
    CHECK_EQ(out.ses.rx, HITEC_RX_ERR_ABORTED);
    CHECK(out.have_cap);
    CHECK((out.cap & LINK_HT_CAP_ABORTED) != 0u);
    CHECK(g_ms - t0 <= 3u * HITEC_LINK_POLL_MS);
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_OPEN);
    CHECK(readout().ours);
    CHECK(!readout().stopped);
}

TEST_CASE(abort_in_the_off_time_of_a_power_leaves_the_supply_off)
{
    const hitec_ses_step_t st = power_step(HITEC_D, 2000u, 1000u);
    hitec_link_outcome_t out;

    opened(HITEC_D);
    start_until(&st, HITEC_LINK_RUNNING);
    run_for(500u);
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_POWER_OFF);
    CHECK_EQ(hitec_link_phase(&g_l), HITEC_LINK_RUNNING);
    hitec_link_abort(&g_l);
    out = outcome();
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(out.result, LINK_HITEC_RESULT_CUT);
    CHECK_EQ(out.ses.run, HITEC_SES_CUT);
    CHECK(!out.have_cap);
    CHECK_EQ(g_hw.ons, 0u);
    CHECK(!g_hw.v.asked_on);
}

TEST_CASE(abort_before_the_command_drops_the_request)
{
    const hitec_ses_step_t st = c_read_step(0x12u);
    hitec_link_outcome_t out;

    for (unsigned at = 0; at <= 4u; at += 2u) {
        opened(HITEC_CLASSIC);
        memset(g_op_cmds, 0, sizeof(g_op_cmds));
        CHECK_EQ(hitec_link_step(&g_l, CH, &st, MV, MA, ++g_id),
                 HITEC_LINK_START_OK);
        /* 0, 2 or 4 of the 5 staged windows out. */
        for (unsigned k = 0; k < at; ++k) {
            one_exchange();
        }
        CHECK_EQ(g_l.stage_at, at);
        hitec_link_abort(&g_l);
        CHECK(hitec_link_outcome(&g_l, &out));
        CHECK_EQ(out.kind, HITEC_LINK_OUT_DROPPED);
        CHECK(!out.taken);
        CHECK_EQ(out.ses.run, HITEC_SES_FAILED);
        run_for(50u);
        CHECK_EQ(g_op_cmds[LINK_HITEC_OP_SEND], 0u);
        CHECK_EQ(g_op_cmds[LINK_HITEC_OP_ABORT], 1u);
        CHECK_EQ(g_drv.starts, 0u);
        CHECK_EQ(hitec_link_phase(&g_l), HITEC_LINK_READY);
    }
}

TEST_CASE(abort_while_the_command_is_on_its_way_sends_it_no_more)
{
    const hitec_ses_step_t st = c_read_step(0x12u);
    hitec_link_outcome_t out;

    /* Lost before the page: the SEQ says not taken. */
    opened(HITEC_CLASSIC);
    g_abort_in_flight = true;
    g_lose_cmd = 1u;
    out = run_step(&st);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DROPPED);
    CHECK(!out.taken);
    CHECK_EQ(g_op_cmds[LINK_HITEC_OP_SEND], 1u);
    CHECK_EQ(g_drv.starts, 0u);
    run_for(20u);
    CHECK_EQ(g_op_cmds[LINK_HITEC_OP_ABORT], 1u);
    /* Taken with its answer lost: it runs, and the ABORT follows. */
    opened(HITEC_CLASSIC);
    g_abort_in_flight = true;
    g_lose_cmd_reply = 1u;
    out = run_step(&st);
    run_for(20u);
    CHECK_EQ(g_op_cmds[LINK_HITEC_OP_SEND], 1u);
    CHECK_EQ(g_op_cmds[LINK_HITEC_OP_ABORT], 1u);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK(out.taken);
    CHECK_EQ(g_drv.starts, 1u);
    /* Answered: the ABORT follows the verdict. */
    opened(HITEC_CLASSIC);
    g_abort_in_flight = true;
    out = run_step(&st);
    run_for(20u);
    CHECK_EQ(g_op_cmds[LINK_HITEC_OP_SEND], 1u);
    CHECK_EQ(g_op_cmds[LINK_HITEC_OP_ABORT], 1u);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK(out.taken);
}

/* An abort while a staged window is out: the next request waits for its
 * answer, which changes nothing of it. */
TEST_CASE(an_abort_with_a_window_out_holds_the_next_request_for_its_answer)
{
    const hitec_ses_step_t st = c_read_step(0x12u);
    const hitec_ses_step_t st2 = c_read_step(0x13u);
    hitec_link_outcome_t out;
    hitec_link_xfer_t x;

    opened(HITEC_CLASSIC);
    CHECK_EQ(hitec_link_step(&g_l, CH, &st, MV, MA, ++g_id),
             HITEC_LINK_START_OK);
    CHECK(hitec_link_next(&g_l, g_ms, &x));
    CHECK(x.write && x.off == LINK_HT_W_LINE);
    hitec_link_abort(&g_l);
    CHECK(hitec_link_outcome(&g_l, &out));
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DROPPED);
    CHECK_EQ(hitec_link_step(&g_l, CH, &st2, MV, MA, ++g_id),
             HITEC_LINK_START_BUSY);
    CHECK_EQ(hitec_link_open(&g_l, CH, HITEC_CLASSIC, 0u, ++g_id),
             HITEC_LINK_START_BUSY);
    hitec_link_done(&g_l, xchg(NULL, &x, NULL), NULL);
    out = run_step(&st2);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(out.ses.run, HITEC_SES_RAN);
    CHECK_EQ(out.ses.value, g_drv.eeprom[0x13]);
}

/* Run until the request's run is the next read. */
static void until_fetching(const hitec_ses_step_t *st)
{
    CHECK_EQ(hitec_link_step(&g_l, CH, st, MV, MA, ++g_id),
             HITEC_LINK_START_OK);
    for (unsigned k = 0; k < 200u
                         && hitec_link_phase(&g_l) != HITEC_LINK_FETCHING;
         ++k) {
        tick_as(true, false);
        one_exchange();
    }
    CHECK_EQ(hitec_link_phase(&g_l), HITEC_LINK_FETCHING);
}

/* VIEW written without a command between the last poll and the run's
 * read, or another writer's command there: no run, the line kept. */
TEST_CASE(a_run_read_with_another_view_or_seq_carries_no_run)
{
    const hitec_ses_step_t st = c_read_step(0x12u);
    const uint16_t times = LINK_HITEC_VIEW_TIMES_LO;
    hitec_link_readout_t r0;
    hitec_link_outcome_t out;

    opened(HITEC_CLASSIC);
    r0 = readout();
    CHECK(r0.have_line);
    until_fetching(&st);
    CHECK_EQ(hitec_port_write(&g_port, LINK_HT_W_VIEW, 1u, &times, g_ms), 0u);
    one_exchange();
    CHECK(hitec_link_outcome(&g_l, &out));
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(out.ses.run, HITEC_SES_RAN);
    CHECK_EQ(out.ses.value, g_drv.eeprom[0x12]);
    CHECK(out.have_cap);
    CHECK(!out.have_run);
    CHECK_EQ(out.delay_ns, 0u);
    CHECK(readout().have_line);
    CHECK_EQ(readout().pin, PIN);
    CHECK_EQ(readout().bit_ns, r0.bit_ns);
    CHECK_EQ(readout().line, r0.line);
    /* VIEW back at 2 from the next request's staging: the run again. */
    out = run_step(&st);
    CHECK(out.have_run);
    CHECK_EQ(g_port.view, LINK_HITEC_VIEW_RUN);
    /* Another writer's command before the read. */
    until_fetching(&st);
    CHECK_EQ(other_cmd(LINK_HITEC_OP_CLEAR, CH, 0u), 0u);
    one_exchange();
    CHECK(hitec_link_outcome(&g_l, &out));
    CHECK_EQ(out.ses.run, HITEC_SES_RAN);
    CHECK(!out.have_run);
}

TEST_CASE(abort_between_two_tries_of_a_command_sends_it_no_more)
{
    const hitec_ses_step_t st = c_read_step(0x12u);

    for (unsigned lost = 1; lost <= HITEC_LINK_RETRIES; ++lost) {
        hitec_link_outcome_t out;

        opened(HITEC_CLASSIC);
        g_lose_cmd = lost;
        CHECK_EQ(hitec_link_step(&g_l, CH, &st, MV, MA, ++g_id),
                 HITEC_LINK_START_OK);
        /* Until the last lost try: owed again, or, after the third, in
         * doubt. */
        for (unsigned k = 0; k < 100u && g_op_cmds[LINK_HITEC_OP_SEND] < lost;
             ++k) {
            one_exchange();
        }
        CHECK_EQ(g_op_cmds[LINK_HITEC_OP_SEND], lost);
        CHECK(lost == HITEC_LINK_RETRIES ? g_l.cmd_doubt != 0u
                                         : g_l.cmd_live != 0u);
        hitec_link_abort(&g_l);
        out = outcome();
        CHECK_EQ(out.kind, HITEC_LINK_OUT_DROPPED);
        CHECK(!out.taken);
        run_for(20u);
        CHECK_EQ(g_op_cmds[LINK_HITEC_OP_SEND], lost);
        CHECK_EQ(g_op_cmds[LINK_HITEC_OP_ABORT], 1u);
        CHECK_EQ(g_drv.starts, 0u);
    }
}

TEST_CASE(an_abort_refused_for_another_reason_is_not_sent_again)
{
    const hitec_ses_step_t st = c_read_step(0x12u);

    opened(HITEC_CLASSIC);
    g_nack_cmd = LINK_NACK_BUSY;
    g_nack_cmd_n = 1u;
    hitec_link_abort(&g_l);
    one_exchange();
    CHECK_EQ(g_op_cmds[LINK_HITEC_OP_ABORT], 1u);
    /* No request until the page is read again, at once. */
    CHECK_EQ(hitec_link_phase(&g_l), HITEC_LINK_SYNC);
    CHECK_EQ(hitec_link_step(&g_l, CH, &st, MV, MA, ++g_id),
             HITEC_LINK_START_BUSY);
    one_exchange();
    CHECK_EQ(hitec_link_phase(&g_l), HITEC_LINK_READY);
    run_for(200u);
    CHECK_EQ(g_op_cmds[LINK_HITEC_OP_ABORT], 1u);
    CHECK(readout().ours);
    /* The next request goes once and runs. */
    CHECK_EQ(run_step(&st).ses.run, HITEC_SES_RAN);
    CHECK_EQ(g_op_cmds[LINK_HITEC_OP_SEND], 1u);
    CHECK_EQ(g_drv.starts, 1u);
}

TEST_CASE(a_try_refused_after_one_without_an_answer_is_decided_by_the_seq)
{
    const hitec_ses_step_t st = c_read_step(0x12u);
    hitec_link_outcome_t out;

    opened(HITEC_CLASSIC);
    g_lose_cmd = 1u;
    g_nack_cmd = LINK_NACK_BAD_VALUE;
    g_nack_cmd_n = 1u;
    out = run_step(&st);
    CHECK_EQ(g_op_cmds[LINK_HITEC_OP_SEND], 2u);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_NACK);
    CHECK_EQ(out.nack, LINK_NACK_BAD_VALUE);
    CHECK(!out.taken);
    CHECK_EQ(g_drv.starts, 0u);
    CHECK_EQ(hitec_link_phase(&g_l), HITEC_LINK_READY);
    /* The next doubt without a NACK: no answer, not the NACK before. */
    g_lose_cmd = HITEC_LINK_RETRIES;
    out = run_step(&st);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_NO_ANSWER);
    CHECK_EQ(out.nack, 0u);
    CHECK_EQ(run_step(&st).ses.run, HITEC_SES_RAN);
}

TEST_CASE(an_abort_behind_another_writer_goes_again_with_its_seq)
{
    const hitec_ses_step_t st = plus_listen(2000000u);
    hitec_link_outcome_t out;

    opened(HITEC_CLASSIC);
    start_until(&st, HITEC_LINK_RUNNING);
    g_other_cmd = 1u;
    hitec_link_abort(&g_l);
    out = outcome();
    /* The ABORT refused, the page read: another SEQ, so what runs is not
     * known any more. */
    CHECK_EQ(out.kind, HITEC_LINK_OUT_RESTARTED);
    CHECK_EQ(out.ses.run, HITEC_SES_FAILED);
    run_for(20u);
    CHECK_EQ(g_op_cmds[LINK_HITEC_OP_ABORT], 2u);
    CHECK_EQ(g_port.seq, g_l.seq);
    CHECK(!readout().ours);
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_OPEN);
}

TEST_CASE(a_pass_makes_at_most_4_exchanges)
{
    hitec_ses_step_t st;
    hitec_link_outcome_t out;
    hitec_link_readout_t r;

    opened(HITEC_CLASSIC);
    for (unsigned a = 0; a < 20u; ++a) {
        st = c_read_step((uint8_t)a);
        out = run_step(&st);
        CHECK_EQ(out.ses.value, g_drv.eeprom[a]);
    }
    CHECK_EQ(g_max_pass, HITEC_LINK_PASS_MAX);
    CHECK_EQ(readout().count, 20u);
    /* NULL is nothing. */
    CHECK_EQ(hitec_link_service(NULL, g_ms, xchg, NULL), 0u);
    CHECK_EQ(hitec_link_service(&g_l, g_ms, NULL, NULL), 0u);
    CHECK(!hitec_link_next(NULL, g_ms, NULL));
    CHECK(!hitec_link_outcome(NULL, &out));
    CHECK(!hitec_link_outcome(&g_l, NULL));
    CHECK_EQ(hitec_link_phase(NULL), HITEC_LINK_DOWN);
    CHECK(!hitec_link_has_page(NULL));
    hitec_link_readout(NULL, &r);
    CHECK_EQ(r.pin, HITEC_LINK_NO_PIN);
    CHECK(!r.known);
    hitec_link_readout(&g_l, NULL);
    hitec_link_done(NULL, 0, NULL);
    hitec_link_done(&g_l, 0, NULL);
    hitec_link_abort(NULL);
    hitec_link_lost(NULL);
    hitec_link_came_up(NULL, 29u);
    hitec_link_init(NULL);
    CHECK_EQ(hitec_link_clear(NULL, CH, 1u), HITEC_LINK_START_ARG);
}

int main(void)
{
    RUN(nothing_is_sent_before_the_link_or_to_an_iomcu_before_4_23);
    RUN(an_iomcu_that_refuses_the_page_is_left_alone);
    RUN(a_request_answers_why_it_sends_nothing);
    RUN(open_takes_the_port_and_reads_its_line);
    RUN(a_classic_read_comes_back_as_the_step_result);
    RUN(a_d_series_read_comes_back_as_the_step_result);
    RUN(every_refusal_of_the_page_reaches_the_outcome);
    RUN(a_request_the_page_ends_without_running_it_is_failed);
    RUN(a_capture_says_16_bytes_at_most);
    RUN(power_needs_a_pd_mini_that_answers);
    RUN(power_cycles_the_supply_and_listens_from_the_on);
    RUN(a_listen_that_answers_carries_the_key);
    RUN(a_level_holds_the_line_without_the_key);
    RUN(a_command_whose_answer_is_lost_once_or_twice_is_taken_once);
    RUN(a_command_unheard_3_times_is_given_up_and_the_seq_says_not_taken);
    RUN(a_command_taken_with_3_answers_lost_runs_and_the_seq_says_so);
    RUN(the_deciding_read_has_its_own_tries);
    RUN(polls_unanswered_3_times_end_a_running_step);
    RUN(a_status_refused_for_another_reason_is_asked_again);
    RUN(a_read_answered_without_registers_counts_as_no_answer);
    RUN(a_frame_the_page_refuses_ends_the_request);
    RUN(a_command_behind_another_writer_is_refused_and_the_page_read);
    RUN(stopped_then_clear_lets_steps_run_again);
    RUN(a_stop_during_a_step_cuts_it);
    RUN(abort_while_busy_cuts_the_step_and_keeps_the_port);
    RUN(abort_in_the_off_time_of_a_power_leaves_the_supply_off);
    RUN(abort_before_the_command_drops_the_request);
    RUN(abort_while_the_command_is_on_its_way_sends_it_no_more);
    RUN(an_abort_with_a_window_out_holds_the_next_request_for_its_answer);
    RUN(a_run_read_with_another_view_or_seq_carries_no_run);
    RUN(abort_between_two_tries_of_a_command_sends_it_no_more);
    RUN(an_abort_refused_for_another_reason_is_not_sent_again);
    RUN(a_try_refused_after_one_without_an_answer_is_decided_by_the_seq);
    RUN(an_abort_behind_another_writer_goes_again_with_its_seq);
    RUN(a_pass_makes_at_most_4_exchanges);
    return test_summary("hitec_link");
}
