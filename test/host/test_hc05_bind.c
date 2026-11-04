/*
 * BIND and ANY of the HC-05 AT exchange state machine
 * (protocols/hc05/hc05.c) against the scripted module of
 * test/host/hc05_sim.h, firmware 2.0 and 3.0.
 *
 * Under test: the commands in order, the quoted PIN, a failed pairing, the
 * restart after AT+RESET with KEY low and the machine busy through it, an
 * OK that comes late or not at all, an abort after AT+RESET, the timeouts
 * of AT+PAIR and AT+RESET at tick 0 and across the wrap of the 32-bit ms
 * clock, and the refusals of a bad address or PIN.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "hc05.h"
#include "hc05_rig.h"
#include "hc05_sim.h"
#include "tick_wrap.h"

TEST_CASE(an_operation_right_after_bind_or_any_finds_the_module_in_data_mode)
{
    static const hc05_op_t ops[] = { HC05_OP_BIND, HC05_OP_ANY };
    for (size_t i = 0; i < 2u; ++i) {
        hc05_t h;
        sim_t s;
        const uint32_t end = run_op(&h, &s, ops[i]);
        CHECK_EQ(h.state, HC05_ST_DONE);
        /* DONE comes after the restart: KEY was low through it. */
        CHECK_EQ(s.restarts, 1);
        CHECK(!s.restarted_key);
        /* Busy until then: a start during the hold is refused. */
        s.nlog = 0;
        CHECK(hc05_probe(&h, end));
        (void)sim_run(&s, &h, end, 10000u);
        CHECK_EQ(h.state, HC05_ST_DONE);
        CHECK_EQ(h.baud, HC05_BAUD_DATA);
        CHECK_EQ(count_act(&s, HC05_ACT_BAUD), 1);
    }
    /* During the hold the machine is busy. */
    hc05_t h;
    sim_t s;
    sim_init(&s, 2, HC05_BAUD_OLD);
    hc05_init(&h);
    CHECK(hc05_any(&h, "1234", 0));
    uint32_t t = 0;
    for (; t < 20000u; ++t) {
        sim_tick(&s, &h, t);
        if (s.nlog > 1u && s.log[s.nlog - 1u].act.kind == HC05_ACT_KEY
            && !s.log[s.nlog - 1u].act.key) {
            break;
        }
    }
    CHECK(hc05_busy(&h));
    CHECK(!hc05_probe(&h, t + 1u));
    hc05_act_t a;
    CHECK_EQ(step_all(&h, t + HC05_RESET_MS - 1u, NULL), HC05_ACT_NONE);
    CHECK(step_all(&h, t + HC05_RESET_MS, &a) == HC05_ACT_BAUD);
    CHECK_EQ(h.state, HC05_ST_DONE);
}

TEST_CASE(a_reset_ok_after_key_went_low_still_counts)
{
    hc05_t h;
    hc05_act_t a;
    const uint32_t t = any_to_reset(&h, 0);
    CHECK_EQ(step_all(&h, t + HC05_KEY_SETTLE_MS - 1u, NULL), HC05_ACT_NONE);
    CHECK(step_all(&h, t + HC05_KEY_SETTLE_MS, &a) == HC05_ACT_KEY && !a.key);
    feed_line(&h, "OK");
    CHECK_EQ(step_all(&h, t + HC05_KEY_SETTLE_MS + HC05_RESET_MS - 1u, NULL),
             HC05_ACT_NONE);
    CHECK(step_all(&h, t + HC05_KEY_SETTLE_MS + HC05_RESET_MS, &a)
          == HC05_ACT_BAUD);
    CHECK_EQ(a.baud, HC05_BAUD_DATA);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_EQ(h.err, HC05_ERR_NONE);
}

TEST_CASE(an_abort_after_the_reset_keeps_key_low_through_the_restart)
{
    hc05_t h;
    hc05_act_t a;

    /* In the hold after the reset's OK: still busy, FAILED at its end. */
    uint32_t t = any_to_reset(&h, 0);
    feed_line(&h, "OK");
    CHECK(step_all(&h, t + 5u, &a) == HC05_ACT_KEY && !a.key);
    hc05_abort(&h);
    CHECK(hc05_busy(&h));
    CHECK(!hc05_probe(&h, t + 6u));
    CHECK_EQ(step_all(&h, t + 5u + HC05_RESET_MS - 1u, NULL), HC05_ACT_NONE);
    CHECK(hc05_busy(&h));
    CHECK_EQ(step_all(&h, t + 5u + HC05_RESET_MS, NULL), HC05_ACT_NONE);
    CHECK_EQ(h.state, HC05_ST_FAILED);
    CHECK_EQ(h.err, HC05_ERR_NONE);
    CHECK(!h.key);

    /* AT+RESET sent, its OK not yet come: KEY low at once, the hold, a
     * late OK changes nothing. */
    t = any_to_reset(&h, 0);
    hc05_abort(&h);
    CHECK(hc05_busy(&h));
    CHECK(step_all(&h, t + 1u, &a) == HC05_ACT_KEY && !a.key);
    feed_line(&h, "OK");
    CHECK_EQ(step_all(&h, t + HC05_RESET_MS, NULL), HC05_ACT_NONE);
    CHECK(hc05_busy(&h));
    CHECK_EQ(step_all(&h, t + 1u + HC05_RESET_MS, NULL), HC05_ACT_NONE);
    CHECK_EQ(h.state, HC05_ST_FAILED);

    /* Against the module: ANY, abort in the hold, PROBE at once after
     * the end: the module restarted in data mode at 115200. */
    sim_t s;
    sim_init(&s, 2, HC05_BAUD_OLD);
    hc05_init(&h);
    CHECK(hc05_any(&h, "1234", 0));
    for (t = 0; t < 20000u; ++t) {
        sim_tick(&s, &h, t);
        if (s.nlog > 1u && s.log[s.nlog - 1u].act.kind == HC05_ACT_KEY
            && !s.log[s.nlog - 1u].act.key) {
            break;
        }
    }
    hc05_abort(&h);
    const uint32_t end = sim_run(&s, &h, t + 1u, 10000u);
    CHECK_EQ(h.state, HC05_ST_FAILED);
    CHECK(hc05_probe(&h, t + 1u + end));
    (void)sim_run(&s, &h, t + 1u + end, 10000u);
    CHECK_EQ(s.restarts, 1);
    CHECK(!s.restarted_key);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_EQ(h.baud, HC05_BAUD_DATA);
}

TEST_CASE(bind_sends_role_pswd_pair_bind_cmode_uart_reset_in_order)
{
    hc05_t h;
    sim_t s;
    (void)run_op(&h, &s, HC05_OP_BIND);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_EQ(h.err, HC05_ERR_NONE);
    CHECK_STR_EQ(h.target, SENSOR);
    CHECK(!h.pswd_quoted);
    const char *want[] = { "AT", "AT+ROLE=1", "AT+PSWD=1234",
                           "AT+PAIR=98D3,31,FD5C81,20",
                           "AT+BIND=98D3,31,FD5C81", "AT+CMODE=0",
                           "AT+UART=115200,0,0", "AT+RESET" };
    check_cmds(&s, want, 8u);
    CHECK_EQ(s.role, 1);
    CHECK_EQ(s.cmode, 0);
    CHECK_STR_EQ(s.bind, SENSOR_CMD);
    CHECK_EQ(s.uart_set, HC05_BAUD_DATA);

    /* A 1-digit PIN goes in. */
    sim_init(&s, 2, HC05_BAUD_OLD);
    sim_add_dev(&s, SENSOR, 0x1F00u, 0xFFC0u, "HC-06", 3000u);
    s.dev_pin = "7";
    hc05_init(&h);
    CHECK(hc05_bind(&h, SENSOR, "7", 0));
    (void)sim_run(&s, &h, 0, 80000u);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_STR_EQ(s.cmds[2], "AT+PSWD=7");

    /* The address with commas and a 16-digit PIN go in as well. */
    sim_init(&s, 2, HC05_BAUD_OLD);
    sim_add_dev(&s, SENSOR, 0x1F00u, 0xFFC0u, "HC-06", 3000u);
    s.dev_pin = "1234567890123456";
    hc05_init(&h);
    CHECK(hc05_bind(&h, SENSOR_CMD, "1234567890123456", 0));
    CHECK_STR_EQ(h.target, SENSOR);
    (void)sim_run(&s, &h, 0, 80000u);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_STR_EQ(s.cmds[2], "AT+PSWD=1234567890123456");

    /* A refusal half way stops there. */
    hc05_act_t a;
    hc05_init(&h);
    CHECK(hc05_bind(&h, SENSOR, "1234", 0));
    (void)step_all(&h, 0, NULL);
    (void)step_all(&h, 100, NULL);
    feed_line(&h, "OK");
    CHECK(step_all(&h, 100, &a) == HC05_ACT_SEND && is_send(&a, "AT+ROLE=1"));
    feed_line(&h, "ERROR:(11)");
    CHECK_EQ(h.err, HC05_ERR_REFUSED);
    CHECK_EQ(h.err_code, 0x11);
    CHECK_EQ(step_all(&h, 101, &a), HC05_ACT_KEY);
    CHECK(!a.key);
}

TEST_CASE(bind_on_firmware_3_0_retries_the_pin_quoted)
{
    hc05_t h;
    sim_t s;
    sim_init(&s, 3, HC05_BAUD_OLD);
    sim_add_dev(&s, SENSOR, 0x1F00u, 0xFFC0u, "HC-06", 3000u);
    hc05_init(&h);
    CHECK(hc05_bind(&h, SENSOR, "1234", 0));
    (void)sim_run(&s, &h, 0, 80000u);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK(h.pswd_quoted);
    const char *want[] = { "AT", "AT+ROLE=1", "AT+PSWD=1234",
                           "AT+PSWD=\"1234\"", "AT+PAIR=98D3,31,FD5C81,20",
                           "AT+BIND=98D3,31,FD5C81", "AT+CMODE=0",
                           "AT+UART=115200,0,0", "AT+RESET" };
    check_cmds(&s, want, 9u);
    CHECK_STR_EQ(s.pin, "1234");

    /* Both forms refused: REFUSED after the quoted one, no AT+PAIR. */
    sim_init(&s, 3, HC05_BAUD_OLD);
    sim_add_dev(&s, SENSOR, 0x1F00u, 0xFFC0u, "HC-06", 3000u);
    s.pswd_any = true;
    hc05_init(&h);
    CHECK(hc05_bind(&h, SENSOR, "1234", 0));
    (void)sim_run(&s, &h, 0, 80000u);
    CHECK_EQ(h.state, HC05_ST_FAILED);
    CHECK_EQ(h.err, HC05_ERR_REFUSED);
    CHECK_EQ(h.err_code, 0);
    CHECK_EQ(s.ncmd, 4);
    CHECK_STR_EQ(s.cmds[3], "AT+PSWD=\"1234\"");

    /* A FAIL on the PIN is no reason to quote it. */
    hc05_act_t a;
    hc05_init(&h);
    CHECK(hc05_any(&h, "1234", 0));
    (void)step_all(&h, 0, NULL);
    (void)step_all(&h, 100, NULL);
    feed_line(&h, "OK");
    (void)step_all(&h, 100, NULL);
    feed_line(&h, "OK");
    CHECK(step_all(&h, 100, &a) == HC05_ACT_SEND
          && is_send(&a, "AT+PSWD=1234"));
    feed_line(&h, "FAIL");
    CHECK_EQ(h.err, HC05_ERR_REFUSED);
    CHECK(!h.pswd_quoted);
}

TEST_CASE(bind_fails_pair_when_pair_answers_fail)
{
    hc05_t h;
    sim_t s;
    /* The sensor is not in range: FAIL after the 20s pairing time. */
    sim_init(&s, 2, HC05_BAUD_OLD);
    hc05_init(&h);
    CHECK(hc05_bind(&h, SENSOR, "1234", 0));
    (void)sim_run(&s, &h, 0, 80000u);
    CHECK_EQ(h.state, HC05_ST_FAILED);
    CHECK_EQ(h.err, HC05_ERR_PAIR);
    CHECK_EQ(h.err_code, 0);
    CHECK_EQ(sim_cmd_index(&s, "AT+BIND=98D3,31,FD5C81"), -1);
    CHECK(!h.key);

    /* An ERROR on AT+PAIR is PAIR too, with its code. */
    sim_init(&s, 2, HC05_BAUD_OLD);
    sim_add_dev(&s, SENSOR, 0x1F00u, 0xFFC0u, "HC-06", 3000u);
    s.pair_error = true;
    hc05_init(&h);
    CHECK(hc05_bind(&h, SENSOR, "1234", 0));
    (void)sim_run(&s, &h, 0, 80000u);
    CHECK_EQ(h.err, HC05_ERR_PAIR);
    CHECK_EQ(h.err_code, 0x15);

    /* A wrong PIN: the sensor refuses, FAIL. */
    sim_init(&s, 2, HC05_BAUD_OLD);
    sim_add_dev(&s, SENSOR, 0x1F00u, 0xFFC0u, "HC-06", 3000u);
    hc05_init(&h);
    CHECK(hc05_bind(&h, SENSOR, "0000", 0));
    (void)sim_run(&s, &h, 0, 80000u);
    CHECK_EQ(h.err, HC05_ERR_PAIR);
}

TEST_CASE(any_sends_cmode_1_and_no_pair)
{
    hc05_t h;
    sim_t s;
    (void)run_op(&h, &s, HC05_OP_ANY);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_EQ(h.op, HC05_OP_ANY);
    CHECK_STR_EQ(h.target, "");
    const char *want[] = { "AT", "AT+ROLE=1", "AT+PSWD=1234", "AT+CMODE=1",
                           "AT+UART=115200,0,0", "AT+RESET" };
    check_cmds(&s, want, 6u);
    CHECK_EQ(s.cmode, 1);

    /* Firmware 3.0: the PIN quoted on the second try here too. */
    sim_init(&s, 3, HC05_BAUD_OLD);
    hc05_init(&h);
    CHECK(hc05_any(&h, "1234", 0));
    (void)sim_run(&s, &h, 0, 80000u);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_STR_EQ(s.cmds[3], "AT+PSWD=\"1234\"");
}

/* A bind whose AT+PAIR never answers: TIMEOUT at HC05_PAIR_MS. */
static void pair_times_out(uint32_t t0)
{
    hc05_t h;
    hc05_act_t a;
    hc05_init(&h);
    CHECK(hc05_bind(&h, SENSOR, "1234", t0));
    (void)step_all(&h, t0, NULL);
    (void)step_all(&h, t0 + 100u, NULL);
    feed_line(&h, "OK");
    (void)step_all(&h, t0 + 100u, NULL);
    feed_line(&h, "OK");
    (void)step_all(&h, t0 + 100u, NULL);
    feed_line(&h, "OK");
    CHECK(step_all(&h, t0 + 100u, &a) == HC05_ACT_SEND
          && is_send(&a, "AT+PAIR=98D3,31,FD5C81,20"));
    CHECK_EQ(step_all(&h, t0 + 100u + HC05_PAIR_MS - 1u, NULL),
             HC05_ACT_NONE);
    check_fence(&h, t0 + 100u + HC05_PAIR_MS, HC05_ERR_TIMEOUT);
}

/* AT+RESET without its OK: KEY low at 100ms, TIMEOUT after the hold. */
static void reset_times_out(uint32_t t0)
{
    hc05_t h;
    sim_t s;
    sim_init(&s, 2, HC05_BAUD_OLD);
    s.reset_silent = true;
    hc05_init(&h);
    CHECK(hc05_any(&h, "1234", t0));
    const uint32_t end = sim_run(&s, &h, t0, 80000u);
    CHECK_EQ(h.err, HC05_ERR_TIMEOUT);
    const sim_log_t *reset = nth_act(&s, HC05_ACT_SEND, 7);
    const sim_log_t *low = nth_act(&s, HC05_ACT_KEY, 1);
    CHECK(reset != NULL && low != NULL);
    if (reset == NULL || low == NULL) {
        return;
    }
    CHECK(is_send(&reset->act, "AT+RESET"));
    CHECK_EQ((uint32_t)(low->t - reset->t), HC05_KEY_SETTLE_MS);
    CHECK_EQ((uint32_t)(t0 + end - low->t), HC05_RESET_MS);
}

/* AT+RESET taken, its OK lost: KEY low at 100ms, busy through the
 * hold, TIMEOUT at its end; the module restarts with KEY low. */
static void reset_ok_lost(uint32_t t0)
{
    hc05_t h;
    sim_t s;
    sim_init(&s, 2, HC05_BAUD_OLD);
    s.reset_ok_lost = true;
    hc05_init(&h);
    CHECK(hc05_any(&h, "1234", t0));
    uint32_t k = 0;
    for (; k < 80000u; ++k) {
        sim_tick(&s, &h, t0 + k);
        if (s.nlog > 1u && s.log[s.nlog - 1u].act.kind == HC05_ACT_KEY
            && !s.log[s.nlog - 1u].act.key) {
            break;
        }
    }
    const sim_log_t *reset = nth_act(&s, HC05_ACT_SEND, 7);
    CHECK(reset != NULL);
    if (reset == NULL) {
        return;
    }
    CHECK(is_send(&reset->act, "AT+RESET"));
    CHECK_EQ((uint32_t)(t0 + k - reset->t), HC05_KEY_SETTLE_MS);
    CHECK(hc05_busy(&h));
    CHECK(!hc05_probe(&h, t0 + k));
    for (uint32_t j = 1; j < HC05_RESET_MS; ++j) {
        sim_tick(&s, &h, t0 + k + j);
    }
    CHECK(hc05_busy(&h));
    sim_tick(&s, &h, t0 + k + HC05_RESET_MS);
    CHECK_EQ(h.state, HC05_ST_FAILED);
    CHECK_EQ(h.err, HC05_ERR_TIMEOUT);
    CHECK(!h.key);
    CHECK_EQ(s.restarts, 1);
    CHECK(!s.restarted_key);
    CHECK_EQ(s.baud, HC05_BAUD_DATA);
}

TEST_CASE(bind_and_any_refuse_a_bad_address_or_pin)
{
    const char *addrs[] = { "", "3014:10", "3014:10:1711790", "x:1:2", NULL };
    const char *pins[] = { "", "12a4", "12345678901234567", " 1234", NULL };
    hc05_t h;
    hc05_act_t a;
    for (size_t i = 0; i < sizeof addrs / sizeof addrs[0]; ++i) {
        hc05_init(&h);
        CHECK(!hc05_bind(&h, addrs[i], "1234", 0));
        CHECK_EQ(h.op, HC05_OP_BIND);
        CHECK_EQ(h.state, HC05_ST_FAILED);
        CHECK_EQ(h.err, HC05_ERR_ARG);
        CHECK(!hc05_step(&h, 0, &a));
    }
    for (size_t i = 0; i < sizeof pins / sizeof pins[0]; ++i) {
        hc05_init(&h);
        CHECK(!hc05_bind(&h, SENSOR, pins[i], 0));
        CHECK_EQ(h.err, HC05_ERR_ARG);
        hc05_init(&h);
        CHECK(!hc05_any(&h, pins[i], 0));
        CHECK_EQ(h.op, HC05_OP_ANY);
        CHECK_EQ(h.err, HC05_ERR_ARG);
        CHECK(!hc05_step(&h, 0, &a));
    }
    /* A refusal after an operation that left KEY high still drops it. */
    hc05_init(&h);
    CHECK(hc05_probe(&h, 0));
    (void)step_all(&h, 0, NULL);
    hc05_abort(&h);
    CHECK(h.key);
    CHECK(!hc05_any(&h, "", 0));
    CHECK(step_all(&h, 1, &a) == HC05_ACT_KEY && !a.key);
}

TEST_CASE(a_bind_or_any_timeout_fails_at_exactly_its_limit)
{
    at_tick_0_and_before_the_wrap(pair_times_out, 10000u);
    at_tick_0_and_before_the_wrap(reset_times_out, 1000u);
    at_tick_0_and_before_the_wrap(reset_ok_lost, 2000u);
}

int main(void)
{
    RUN(an_operation_right_after_bind_or_any_finds_the_module_in_data_mode);
    RUN(a_reset_ok_after_key_went_low_still_counts);
    RUN(an_abort_after_the_reset_keeps_key_low_through_the_restart);
    RUN(bind_sends_role_pswd_pair_bind_cmode_uart_reset_in_order);
    RUN(bind_on_firmware_3_0_retries_the_pin_quoted);
    RUN(bind_fails_pair_when_pair_answers_fail);
    RUN(any_sends_cmode_1_and_no_pair);
    RUN(bind_and_any_refuse_a_bad_address_or_pin);
    RUN(a_bind_or_any_timeout_fails_at_exactly_its_limit);
    return test_summary("hc05_bind");
}
