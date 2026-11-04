/*
 * The HC-05 AT exchange state machine (protocols/hc05/hc05.c) against the
 * scripted module of test/host/hc05_sim.h, firmware 2.0 and 3.0.
 *
 * Under test: PROBE at each baud and with no module; KEY high from before
 * the first byte to the end of every operation; SCAN's commands, the
 * ERROR:(17) of AT+INIT, the INQ timeout and firmware 3.0's ERROR:(1F);
 * the late name's fence; the timeouts of AT+VERSION?, AT+INQ, AT+RNAME?
 * and the fence at their limit and one ms before it, at tick 0 and across
 * the wrap of the 32-bit ms clock; abort, a second start and the cap on
 * devices found.  BIND and ANY are in test_hc05_bind.c.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "hc05.h"
#include "hc05_rig.h"
#include "hc05_sim.h"
#include "tick_wrap.h"

TEST_CASE(probe_finds_the_module_at_115200_then_38400_then_9600)
{
    static const uint32_t bauds[3] = { HC05_BAUD_DATA, HC05_BAUD_ALT,
                                       HC05_BAUD_OLD };
    for (unsigned b = 0; b < 3u; ++b) {
        hc05_t h;
        sim_t s;
        sim_init(&s, 2, bauds[b]);
        hc05_init(&h);
        CHECK(hc05_probe(&h, 0));
        CHECK(hc05_busy(&h));
        (void)sim_run(&s, &h, 0, 10000u);
        CHECK_EQ(h.state, HC05_ST_DONE);
        CHECK_EQ(h.err, HC05_ERR_NONE);
        CHECK_EQ(h.baud, bauds[b]);
        CHECK_STR_EQ(h.version, "2.0-20100601");
        /* One BAUD a try, in the order 115200, 38400, 9600, each try
         * HC05_AT_MS after the one before. */
        CHECK_EQ(count_act(&s, HC05_ACT_BAUD), b + 1u);
        for (unsigned i = 0; i <= b; ++i) {
            const sim_log_t *l = nth_act(&s, HC05_ACT_BAUD, i);
            CHECK(l != NULL);
            if (l != NULL) {
                CHECK_EQ(l->act.baud, bauds[i]);
                CHECK_EQ(l->t, HC05_KEY_SETTLE_MS + i * HC05_AT_MS);
            }
        }
        const char *want[] = { "AT", "AT+VERSION?" };
        check_cmds(&s, want, 2u);
    }

    /* Firmware 3.0 names itself. */
    hc05_t h;
    sim_t s;
    sim_init(&s, 3, HC05_BAUD_OLD);
    hc05_init(&h);
    CHECK(hc05_probe(&h, 0));
    (void)sim_run(&s, &h, 0, 10000u);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_STR_EQ(h.version, "3.0-20170601");
}

TEST_CASE(an_operation_after_a_probe_tries_the_probed_baud_first)
{
    hc05_t h;
    sim_t s;
    sim_init(&s, 2, HC05_BAUD_ALT);
    hc05_init(&h);
    CHECK(hc05_probe(&h, 0));
    (void)sim_run(&s, &h, 0, 10000u);
    CHECK_EQ(h.baud, HC05_BAUD_ALT);

    /* The module moved to 9600 since: 38400, then 115200, then 9600. */
    s.baud = HC05_BAUD_OLD;
    s.nlog = 0;
    CHECK(hc05_scan(&h, 20000u));
    (void)sim_run(&s, &h, 20000u, 60000u);
    CHECK_EQ(count_act(&s, HC05_ACT_BAUD), 3);
    CHECK_EQ(nth_act(&s, HC05_ACT_BAUD, 0)->act.baud, HC05_BAUD_ALT);
    CHECK_EQ(nth_act(&s, HC05_ACT_BAUD, 1)->act.baud, HC05_BAUD_DATA);
    CHECK_EQ(nth_act(&s, HC05_ACT_BAUD, 2)->act.baud, HC05_BAUD_OLD);
    CHECK_EQ(h.baud, HC05_BAUD_OLD);

    /* A PROBE forgets the baud: 115200 first again. */
    s.nlog = 0;
    CHECK(hc05_probe(&h, 90000u));
    (void)sim_run(&s, &h, 90000u, 10000u);
    CHECK_EQ(nth_act(&s, HC05_ACT_BAUD, 0)->act.baud, HC05_BAUD_DATA);

    /* And its version: a probe that finds no module leaves "". */
    CHECK_STR_EQ(h.version, "2.0-20100601");
    s.silent = true;
    CHECK(hc05_probe(&h, 110000u));
    (void)sim_run(&s, &h, 110000u, 10000u);
    CHECK_EQ(h.err, HC05_ERR_NO_AT);
    CHECK_STR_EQ(h.version, "");
}

TEST_CASE(probe_with_no_answer_fails_no_at_after_three_bauds)
{
    hc05_t h;
    sim_t s;
    sim_init(&s, 2, HC05_BAUD_OLD);
    s.silent = true;
    hc05_init(&h);
    h.baud = HC05_BAUD_OLD;
    CHECK(hc05_probe(&h, 0));
    const uint32_t end = sim_run(&s, &h, 0, 10000u);
    CHECK_EQ(h.state, HC05_ST_FAILED);
    CHECK_EQ(h.err, HC05_ERR_NO_AT);
    CHECK_EQ(h.baud, 0);
    CHECK_EQ(s.sends, 3);
    CHECK_EQ(count_act(&s, HC05_ACT_BAUD), 3);
    /* The last try's AT at 2100ms, its wait 1000ms. */
    CHECK_EQ(end, HC05_KEY_SETTLE_MS + 3u * HC05_AT_MS);
    CHECK(!h.key);

    /* An operation after it starts at 115200: no baud is known. */
    s.nlog = 0;
    CHECK(hc05_any(&h, "1234", 20000u));
    (void)sim_run(&s, &h, 20000u, 10000u);
    CHECK_EQ(nth_act(&s, HC05_ACT_BAUD, 0)->act.baud, HC05_BAUD_DATA);
    CHECK_EQ(h.err, HC05_ERR_NO_AT);

    /* Garbage bytes a wrong baud brings back are no answer. */
    sim_init(&s, 2, HC05_BAUD_OLD);
    s.garble = true;
    hc05_init(&h);
    CHECK(hc05_probe(&h, 0));
    (void)sim_run(&s, &h, 0, 10000u);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_EQ(h.baud, HC05_BAUD_OLD);
}

TEST_CASE(a_probe_after_junk_at_a_wrong_baud_sends_at_once_more)
{
    hc05_t h;
    sim_t s;
    sim_init(&s, 2, HC05_BAUD_OLD);
    s.junk = true;
    hc05_init(&h);
    CHECK(hc05_probe(&h, 0));
    (void)sim_run(&s, &h, 0, 10000u);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_EQ(h.baud, HC05_BAUD_OLD);
    const char *want[] = { "AT", "AT", "AT+VERSION?" };
    check_cmds(&s, want, 3u);

    /* A second ERROR is the module's answer: REFUSED with its code. */
    hc05_act_t a;
    hc05_init(&h);
    CHECK(hc05_probe(&h, 0));
    (void)step_all(&h, 0, NULL);
    CHECK_EQ(step_all(&h, 100, &a), HC05_ACT_SEND);
    feed_line(&h, "ERROR:(0)");
    CHECK(step_all(&h, 101, &a) == HC05_ACT_SEND && is_send(&a, "AT"));
    feed_line(&h, "ERROR:(3)");
    CHECK_EQ(h.state, HC05_ST_FAILED);
    CHECK_EQ(h.err, HC05_ERR_REFUSED);
    CHECK_EQ(h.err_code, 3);
    /* The next operation has its own retry. */
    CHECK(hc05_probe(&h, 5000));
    (void)step_all(&h, 5000, NULL);
    (void)step_all(&h, 5100, NULL);
    feed_line(&h, "ERROR:(0)");
    CHECK(step_all(&h, 5101, &a) == HC05_ACT_SEND && is_send(&a, "AT"));
    CHECK(hc05_busy(&h));
    /* The retry is once per baud: a FAIL refuses at once. */
    hc05_init(&h);
    CHECK(hc05_probe(&h, 0));
    (void)step_all(&h, 0, NULL);
    (void)step_all(&h, 100, NULL);
    feed_line(&h, "FAIL");
    CHECK_EQ(h.err, HC05_ERR_REFUSED);
    CHECK_EQ(h.err_code, 0);
}

TEST_CASE(key_is_raised_before_the_first_byte_and_dropped_at_the_end)
{
    static const hc05_op_t ops[] = { HC05_OP_PROBE, HC05_OP_SCAN,
                                     HC05_OP_BIND, HC05_OP_ANY };
    for (size_t i = 0; i < sizeof ops / sizeof ops[0]; ++i) {
        hc05_t h;
        sim_t s;
        const uint32_t end = run_op(&h, &s, ops[i]);
        CHECK_EQ(h.state, HC05_ST_DONE);
        /* The first action raises KEY; the first byte goes out
         * HC05_KEY_SETTLE_MS later, none while KEY is low. */
        CHECK(s.nlog > 2u);
        CHECK_EQ(s.log[0].act.kind, HC05_ACT_KEY);
        CHECK(s.log[0].act.key);
        CHECK_EQ(s.log[0].t, 0);
        CHECK_EQ(nth_act(&s, HC05_ACT_SEND, 0)->t, HC05_KEY_SETTLE_MS);
        CHECK_EQ(s.bytes_key_low, 0);
        CHECK_EQ(s.bytes_early, 0);
        /* KEY goes low once, and nothing comes after the end. */
        CHECK_EQ(count_act(&s, HC05_ACT_KEY), 2);
        const sim_log_t *low = nth_act(&s, HC05_ACT_KEY, 1);
        CHECK(!low->act.key);
        /* BIND and ANY hold KEY low HC05_RESET_MS for the restart. */
        const bool reset = ops[i] == HC05_OP_BIND || ops[i] == HC05_OP_ANY;
        CHECK_EQ(low->t, reset ? end - HC05_RESET_MS : end);
        hc05_act_t a;
        CHECK(!hc05_step(&h, end + 1u, &a));
        CHECK_EQ(a.kind, HC05_ACT_NONE);
        CHECK(!hc05_step(&h, end + 100000u, &a));
    }

    /* BIND and ANY drop KEY after the reset's OK and before the module
     * restarts, then set the UART to the data baud: the module comes back
     * in data mode at 115200. */
    for (size_t i = 2; i < 4u; ++i) {
        hc05_t h;
        sim_t s;
        const uint32_t end = run_op(&h, &s, ops[i]);
        const sim_log_t *last = &s.log[s.nlog - 1u];
        CHECK_EQ(last->act.kind, HC05_ACT_BAUD);
        CHECK_EQ(last->act.baud, HC05_BAUD_DATA);
        CHECK_EQ(last->t, end);
        CHECK_EQ(h.baud, HC05_BAUD_DATA);
        for (uint32_t t = end + 1u; t < end + 1000u; ++t) {
            sim_tick(&s, &h, t);
        }
        CHECK_EQ(s.restarts, 1);
        CHECK(!s.restarted_key);
        CHECK_EQ(s.baud, HC05_BAUD_DATA);
    }
}

TEST_CASE(scan_on_firmware_2_0_lists_two_devices_with_their_names)
{
    hc05_t h;
    sim_t s;
    sim_init(&s, 2, HC05_BAUD_OLD);
    sim_add_dev(&s, SENSOR, 0x1F00u, 0xFFC0u, "HC-06", 2500u);
    sim_add_dev(&s, "2:72:D2224", 0x5A020Cu, 0xFFA6u, "PHONE", 6000u);
    hc05_init(&h);
    CHECK(hc05_scan(&h, 0));
    (void)sim_run(&s, &h, 0, 60000u);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_EQ(h.op, HC05_OP_SCAN);
    CHECK_EQ(h.found_n, 2);
    CHECK_STR_EQ(h.found[0].addr, SENSOR);
    CHECK_STR_EQ(h.found[0].name, "HC-06");
    CHECK_EQ(h.found[0].dev_class, 0x1F00);
    CHECK_EQ(h.found[0].rssi, -64);
    CHECK_STR_EQ(h.found[1].addr, "2:72:D2224");
    CHECK_STR_EQ(h.found[1].name, "PHONE");
    CHECK_EQ(h.found[1].dev_class, 0x020C);
    CHECK_EQ(h.found[1].rssi, -90);
    const char *want[] = { "AT", "AT+ROLE=1", "AT+INIT", "AT+INQM=1,9,9",
                           "AT+INQ",
                           "AT+RNAME?98D3,31,FD5C81", "AT+RNAME?2,72,D2224" };
    check_cmds(&s, want, 7u);

    /* A name that does not come: "" after HC05_RNAME_MS, and the next
     * device's still read.  A FAIL: "" at once. */
    sim_init(&s, 2, HC05_BAUD_OLD);
    sim_add_dev(&s, SENSOR, 0x1F00u, 0xFFC0u, "HC-06", 2500u);
    sim_add_dev(&s, "1:2:3", 0x1F00u, 0xFFC0u, NULL, 3000u);
    sim_add_dev(&s, "1:2:4", 0x1F00u, 0xFFC0u, "LAST", 3500u);
    s.devs[0].name_silent = true;
    hc05_init(&h);
    CHECK(hc05_scan(&h, 0));
    (void)sim_run(&s, &h, 0, 60000u);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_EQ(h.found_n, 3);
    CHECK_STR_EQ(h.found[0].name, "");
    CHECK_STR_EQ(h.found[1].name, "");
    CHECK_STR_EQ(h.found[2].name, "LAST");
}

TEST_CASE(a_late_name_is_not_taken_for_the_next_device)
{
    hc05_t h;
    sim_t s;
    sim_init(&s, 2, HC05_BAUD_OLD);
    sim_add_dev(&s, "1:2:3", 0x1F00u, 0xFFC0u, "DEVICE-A", 2000u);
    sim_add_dev(&s, SENSOR, 0x1F00u, 0xFFC0u, "HC-06", 2500u);
    s.devs[0].name_late_ms = HC05_RNAME_MS + 500u;
    hc05_init(&h);
    CHECK(hc05_scan(&h, 0));
    (void)sim_run(&s, &h, 0, 60000u);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_STR_EQ(h.found[0].name, "");
    CHECK_STR_EQ(h.found[1].addr, SENSOR);
    CHECK_STR_EQ(h.found[1].name, "HC-06");
    const char *want[] = { "AT", "AT+ROLE=1", "AT+INIT", "AT+INQM=1,9,9",
                           "AT+INQ",
                           "AT+RNAME?1,2,3", "AT+STATE?",
                           "AT+RNAME?98D3,31,FD5C81" };
    check_cmds(&s, want, 8u);
}

TEST_CASE(scan_sets_the_master_role_before_the_inquiry_on_both_firmwares)
{
    for (int fw = 2; fw <= 3; ++fw) {
        hc05_t h;
        sim_t s;
        sim_init(&s, fw, HC05_BAUD_OLD);
        sim_add_dev(&s, SENSOR, 0x1F00u, 0xFFC0u, "HC-06", 2500u);
        s.inq_on_3 = true;
        hc05_init(&h);
        CHECK(hc05_scan(&h, 0));
        (void)sim_run(&s, &h, 0, 60000u);
        CHECK_EQ(h.state, HC05_ST_DONE);
        CHECK_EQ(s.role, 1);
        const char *want[] = { "AT", "AT+ROLE=1", "AT+INIT", "AT+INQM=1,9,9",
                               "AT+INQ", "AT+RNAME?98D3,31,FD5C81" };
        check_cmds(&s, want, 6u);
        CHECK_STR_EQ(h.found[0].name, "HC-06");
    }
    /* An ERROR to AT+ROLE=1 refuses the scan before AT+INIT. */
    hc05_t h;
    hc05_act_t a;
    hc05_init(&h);
    CHECK(hc05_scan(&h, 0));
    (void)step_all(&h, 0, NULL);
    (void)step_all(&h, 100, NULL);
    feed_line(&h, "OK");
    CHECK(step_all(&h, 100, &a) == HC05_ACT_SEND && is_send(&a, "AT+ROLE=1"));
    feed_line(&h, "ERROR:(11)");
    CHECK_EQ(h.err, HC05_ERR_REFUSED);
    CHECK_EQ(h.err_code, 0x11);
}

TEST_CASE(scan_ignores_error_17_on_init)
{
    hc05_t h;
    sim_t s;
    sim_init(&s, 2, HC05_BAUD_OLD);
    sim_add_dev(&s, SENSOR, 0x1F00u, 0xFFC0u, "HC-06", 2500u);
    s.inited = true;
    hc05_init(&h);
    CHECK(hc05_scan(&h, 0));
    (void)sim_run(&s, &h, 0, 60000u);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_EQ(h.err_code, 0);
    CHECK_EQ(h.found_n, 1);
    CHECK(sim_cmd_index(&s, "AT+INQ") > sim_cmd_index(&s, "AT+INIT"));

    /* Another code on AT+INIT refuses. */
    sim_init(&s, 2, HC05_BAUD_OLD);
    s.init_error = true;
    hc05_init(&h);
    CHECK(hc05_scan(&h, 0));
    (void)sim_run(&s, &h, 0, 60000u);
    CHECK_EQ(h.state, HC05_ST_FAILED);
    CHECK_EQ(h.err, HC05_ERR_REFUSED);
    CHECK_EQ(h.err_code, 4);
    CHECK_EQ(sim_cmd_index(&s, "AT+INQM=1,9,9"), -1);
}

/* AT+INQ that never ends: TIMEOUT at HC05_INQ_MS(9), not 1ms before. */
static void inq_times_out(uint32_t t0)
{
    hc05_t h;
    const uint32_t sent = scan_to_inq(&h, t0);
    feed_line(&h, "+INQ:" SENSOR ",1F00,FFC0");
    CHECK_EQ(step_all(&h, sent + HC05_INQ_MS(9) - 1u, NULL), HC05_ACT_NONE);
    CHECK(hc05_busy(&h));
    CHECK_EQ(step_all(&h, sent + HC05_INQ_MS(9), NULL), HC05_ACT_KEY);
    CHECK_EQ(h.state, HC05_ST_FAILED);
    CHECK_EQ(h.err, HC05_ERR_TIMEOUT);
}

TEST_CASE(scan_runs_for_inqm_timeout_plus_two_seconds)
{
    CHECK_EQ(HC05_INQ_MS(HC05_INQ_UNITS), 13520u);
    /* The module's own end at 9 x 1.28s = 11.52s comes inside it. */
    hc05_t h;
    sim_t s;
    sim_init(&s, 2, HC05_BAUD_OLD);
    sim_add_dev(&s, SENSOR, 0x1F00u, 0xFFC0u, "HC-06", 11000u);
    hc05_init(&h);
    CHECK(hc05_scan(&h, 0));
    (void)sim_run(&s, &h, 0, 60000u);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_EQ(s.inq_units, HC05_INQ_UNITS);
    CHECK_EQ(s.inq_max, HC05_INQ_MAX);

    at_tick_0_and_before_the_wrap(inq_times_out, 5000u);

    /* No device in the whole inquiry: NONE_FOUND, no RNAME?. */
    sim_init(&s, 2, HC05_BAUD_OLD);
    hc05_init(&h);
    CHECK(hc05_scan(&h, 0));
    const uint32_t end = sim_run(&s, &h, 0, 60000u);
    CHECK_EQ(h.state, HC05_ST_FAILED);
    CHECK_EQ(h.err, HC05_ERR_NONE_FOUND);
    CHECK_EQ(s.ncmd, 5);
    CHECK(end > 9u * 1280u);
}

TEST_CASE(scan_on_firmware_3_0_ends_inq_refused_on_error_1f)
{
    hc05_t h;
    sim_t s;
    sim_init(&s, 3, HC05_BAUD_OLD);
    sim_add_dev(&s, SENSOR, 0x1F00u, 0xFFC0u, "HC-06", 2500u);
    hc05_init(&h);
    CHECK(hc05_scan(&h, 0));
    (void)sim_run(&s, &h, 0, 60000u);
    CHECK_EQ(h.state, HC05_ST_FAILED);
    CHECK_EQ(h.err, HC05_ERR_INQ_REFUSED);
    CHECK_EQ(h.err_code, 0x1F);
    CHECK_EQ(h.found_n, 0);
    CHECK(!h.key);

    /* A 3.0 module that does scan lists the sensor. */
    sim_init(&s, 3, HC05_BAUD_OLD);
    sim_add_dev(&s, SENSOR, 0x1F00u, 0xFFC0u, "HC-06", 2500u);
    s.inq_on_3 = true;
    hc05_init(&h);
    CHECK(hc05_scan(&h, 0));
    (void)sim_run(&s, &h, 0, 60000u);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_STR_EQ(h.found[0].name, "HC-06");

    /* Another code on AT+INQ is REFUSED, a FAIL too. */
    sim_init(&s, 2, HC05_BAUD_OLD);
    s.inq_answer = "ERROR:(16)";
    hc05_init(&h);
    CHECK(hc05_scan(&h, 0));
    (void)sim_run(&s, &h, 0, 60000u);
    CHECK_EQ(h.err, HC05_ERR_REFUSED);
    CHECK_EQ(h.err_code, 0x16);
    sim_init(&s, 2, HC05_BAUD_OLD);
    s.inq_answer = "FAIL";
    hc05_init(&h);
    CHECK(hc05_scan(&h, 0));
    (void)sim_run(&s, &h, 0, 60000u);
    CHECK_EQ(h.err, HC05_ERR_REFUSED);
}

/* The VERSION? answer does not come: TIMEOUT at 1000ms, not 1ms before.
 * The KEY settle at its 100ms the same way. */
static void at_times_out(uint32_t t0)
{
    hc05_t h;
    hc05_act_t a;
    hc05_init(&h);
    CHECK(hc05_probe(&h, t0));
    CHECK_EQ(step_all(&h, t0, NULL), HC05_ACT_KEY);
    CHECK_EQ(step_all(&h, t0 + 99u, NULL), HC05_ACT_NONE);
    CHECK(step_all(&h, t0 + 100u, &a) == HC05_ACT_SEND && is_send(&a, "AT"));
    CHECK_EQ(step_all(&h, t0 + 1099u, NULL), HC05_ACT_NONE);
    feed_line(&h, "OK");
    CHECK(step_all(&h, t0 + 1099u, &a) == HC05_ACT_SEND
          && is_send(&a, "AT+VERSION?"));
    CHECK_EQ(step_all(&h, t0 + 2098u, NULL), HC05_ACT_NONE);
    CHECK(hc05_busy(&h));
    CHECK_EQ(step_all(&h, t0 + 2099u, &a), HC05_ACT_KEY);
    CHECK(!a.key);
    CHECK_EQ(h.state, HC05_ST_FAILED);
    CHECK_EQ(h.err, HC05_ERR_TIMEOUT);
}

/* A scan by hand with two devices, its first AT+RNAME? sent at the
 * returned time. */
static uint32_t scan_to_rname(hc05_t *h, uint32_t t0)
{
    hc05_act_t a;
    const uint32_t sent = scan_to_inq(h, t0);
    feed_line(h, "+INQ:1:2:3,1F00,FFC0");
    feed_line(h, "+INQ:1:2:4,1F00,FFC0");
    feed_line(h, "OK");
    CHECK(step_all(h, sent + 10u, &a) == HC05_ACT_SEND
          && is_send(&a, "AT+RNAME?1,2,3"));
    return sent + 10u;
}

/* A name that never comes: AT+STATE? at HC05_RNAME_MS, the late answer
 * dropped, the next device asked after +STATE: and OK. */
static void rname_times_out(uint32_t t0)
{
    hc05_t h;
    hc05_act_t a;
    const uint32_t sent = scan_to_rname(&h, t0);
    CHECK_EQ(step_all(&h, sent + HC05_RNAME_MS - 1u, NULL), HC05_ACT_NONE);
    CHECK(step_all(&h, sent + HC05_RNAME_MS, &a) == HC05_ACT_SEND
          && is_send(&a, "AT+STATE?"));
    feed_line(&h, "+RNAME:A");
    feed_line(&h, "OK");
    feed_line(&h, "FAIL");
    CHECK_EQ(step_all(&h, sent + HC05_RNAME_MS + 1u, NULL), HC05_ACT_NONE);
    feed_line(&h, "+STATE:PAIRABLE");
    feed_line(&h, "OK");
    CHECK(step_all(&h, sent + HC05_RNAME_MS + 1u, &a) == HC05_ACT_SEND
          && is_send(&a, "AT+RNAME?1,2,4"));
    CHECK_STR_EQ(h.found[0].name, "");
    feed_line(&h, "+RNAME:B");
    feed_line(&h, "OK");
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_STR_EQ(h.found[1].name, "B");
}

/* AT+STATE? unanswered as well: the scan ends DONE at HC05_RNAME_MS
 * after it, the names left "". */
static void fence_times_out(uint32_t t0)
{
    hc05_t h;
    hc05_act_t a;
    const uint32_t sent = scan_to_rname(&h, t0);
    CHECK(step_all(&h, sent + HC05_RNAME_MS, &a) == HC05_ACT_SEND
          && is_send(&a, "AT+STATE?"));
    feed_line(&h, "+STATE:PAIRABLE"); /* no OK after it */
    const uint32_t fence = sent + HC05_RNAME_MS;
    CHECK_EQ(step_all(&h, fence + HC05_RNAME_MS - 1u, NULL), HC05_ACT_NONE);
    CHECK(hc05_busy(&h));
    CHECK(step_all(&h, fence + HC05_RNAME_MS, &a) == HC05_ACT_KEY && !a.key);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_EQ(h.found_n, 2);
    CHECK_STR_EQ(h.found[0].name, "");
    CHECK_STR_EQ(h.found[1].name, "");
}

TEST_CASE(a_timeout_fails_the_operation_at_exactly_its_limit)
{
    /* The wrap inside the VERSION? wait, inside the pairing, inside the
     * name wait, inside the reset wait. */
    at_tick_0_and_before_the_wrap(at_times_out, 1500u);
    at_tick_0_and_before_the_wrap(at_times_out, 50u);
    at_tick_0_and_before_the_wrap(rname_times_out, 2000u);
    at_tick_0_and_before_the_wrap(fence_times_out, 4000u);
}

TEST_CASE(abort_ends_a_running_scan_and_drops_key)
{
    hc05_t h;
    hc05_act_t a;
    const uint32_t sent = scan_to_inq(&h, 0);
    feed_line(&h, "+INQ:" SENSOR ",1F00,FFC0");
    hc05_abort(&h);
    CHECK(!hc05_busy(&h));
    CHECK_EQ(h.state, HC05_ST_FAILED);
    CHECK_EQ(h.err, HC05_ERR_NONE);
    CHECK(step_all(&h, sent + 1u, &a) == HC05_ACT_KEY && !a.key);
    CHECK(!h.key);
    /* The rest of the inquiry changes nothing. */
    feed_line(&h, "+INQ:1:2:3,1F00,FFC0");
    feed_line(&h, "OK");
    CHECK_EQ(h.found_n, 1);
    CHECK_EQ(h.state, HC05_ST_FAILED);
    CHECK_EQ(step_all(&h, sent + 20000u, NULL), HC05_ACT_NONE);
    /* A second abort, an abort while idle and of NULL do nothing. */
    hc05_abort(&h);
    CHECK_EQ(h.state, HC05_ST_FAILED);
    hc05_init(&h);
    hc05_abort(&h);
    CHECK_EQ(h.state, HC05_ST_IDLE);
    hc05_abort(NULL);

    /* An abort before KEY was asked high asks nothing. */
    hc05_init(&h);
    CHECK(hc05_probe(&h, 0));
    hc05_abort(&h);
    CHECK_EQ(step_all(&h, 0, NULL), HC05_ACT_NONE);

    /* A start right after an abort raises KEY again. */
    (void)scan_to_inq(&h, 0);
    hc05_abort(&h);
    CHECK(hc05_probe(&h, 200));
    CHECK(step_all(&h, 200, &a) == HC05_ACT_KEY && a.key);
}

TEST_CASE(a_second_start_while_busy_is_refused)
{
    hc05_t h;
    sim_t s;
    sim_init(&s, 2, HC05_BAUD_OLD);
    hc05_init(&h);
    CHECK(hc05_probe(&h, 0));
    for (uint32_t t = 0; t < 150u; ++t) {
        sim_tick(&s, &h, t);
    }
    const uint8_t step = h.step;
    const uint32_t sent = h.sent_ms;
    CHECK(!hc05_probe(&h, 150));
    CHECK(!hc05_scan(&h, 150));
    CHECK(!hc05_bind(&h, SENSOR, "1234", 150));
    CHECK(!hc05_bind(&h, "bad", "x", 150));
    CHECK(!hc05_any(&h, "1234", 150));
    CHECK(!hc05_any(&h, "", 150));
    CHECK_EQ(h.op, HC05_OP_PROBE);
    CHECK_EQ(h.state, HC05_ST_RUNNING);
    CHECK_EQ(h.err, HC05_ERR_NONE);
    CHECK_EQ(h.step, step);
    CHECK_EQ(h.sent_ms, sent);
    (void)sim_run(&s, &h, 150, 10000u);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_EQ(h.baud, HC05_BAUD_OLD);
    /* After the end a start is taken. */
    CHECK(hc05_scan(&h, 20000u));
    CHECK(hc05_busy(&h));

    /* NULL is refused everywhere. */
    hc05_act_t a;
    CHECK(!hc05_probe(NULL, 0));
    CHECK(!hc05_scan(NULL, 0));
    CHECK(!hc05_bind(NULL, SENSOR, "1234", 0));
    CHECK(!hc05_any(NULL, "1234", 0));
    CHECK(!hc05_busy(NULL));
    CHECK(!hc05_step(NULL, 0, &a));
    CHECK_EQ(a.kind, HC05_ACT_NONE);
    CHECK(!hc05_step(&h, 0, NULL));
    hc05_init(NULL);
    hc05_feed(NULL, (const uint8_t *)"OK\r\n", 4u);
    hc05_feed(&h, NULL, 4u);
}

TEST_CASE(found_is_capped_at_eight_devices)
{
    static const char *addrs[10] = { "1:0:1", "1:0:2", "1:0:3", "1:0:4",
                                     "1:0:5", "1:0:6", "1:0:7", "1:0:8",
                                     "1:0:9", "1:0:A" };
    hc05_t h;
    sim_t s;
    sim_init(&s, 2, HC05_BAUD_OLD);
    for (unsigned i = 0; i < 10u; ++i) {
        sim_add_dev(&s, addrs[i], 0x1F00u, 0xFFC0u, "N", 500u + i * 500u);
    }
    /* The sensor reported twice is one device. */
    s.inq_raw[0] = "+INQ:1:0:1,1F00,FFB0";
    s.nraw = 1;
    s.devs[0].inq_ms = 5000u;
    hc05_init(&h);
    CHECK(hc05_scan(&h, 0));
    const uint32_t end = sim_run(&s, &h, 0, 80000u);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_EQ(h.found_n, HC05_FOUND_MAX);
    CHECK_EQ(h.found[0].rssi, -80);
    for (unsigned i = 0; i < HC05_FOUND_MAX; ++i) {
        CHECK_STR_EQ(h.found[i].addr, addrs[i]);
        CHECK_STR_EQ(h.found[i].name, "N");
    }
    /* The inquiry ends at its ninth device, before its 11.52s. */
    CHECK_EQ(s.ncmd, 5u + HC05_FOUND_MAX);
    CHECK(end < 9u * 1280u + 8u * 1000u);
}

TEST_CASE(lines_outside_a_wait_are_ignored)
{
    hc05_t h;
    hc05_act_t a;
    hc05_init(&h);
    feed_line(&h, "OK");
    CHECK_EQ(h.state, HC05_ST_IDLE);
    CHECK(hc05_probe(&h, 0));
    /* Before the KEY action and during the settle. */
    feed_line(&h, "OK");
    CHECK_EQ(step_all(&h, 0, NULL), HC05_ACT_KEY);
    feed_line(&h, "OK");
    CHECK_EQ(h.step, 0);
    CHECK(step_all(&h, 100, &a) == HC05_ACT_SEND && is_send(&a, "AT"));
    /* A line that is no answer, an answer to another command. */
    feed_line(&h, "+VERSION:2.0");
    feed_line(&h, "+INQ:1:2:3,0,0");
    CHECK_EQ(h.step, 2);
    CHECK_STR_EQ(h.version, "");
    feed_line(&h, "OK");
    CHECK(step_all(&h, 100, &a) == HC05_ACT_SEND
          && is_send(&a, "AT+VERSION?"));
    feed_line(&h, "+VERSION:2.0-20100601-WITH-A-LONG-TAIL");
    feed_line(&h, "OK");
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_STR_EQ(h.version, "2.0-20100601-WITH-A-LON");
    feed_line(&h, "ERROR:(0)");
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_EQ(h.err, HC05_ERR_NONE);
}

int main(void)
{
    RUN(probe_finds_the_module_at_115200_then_38400_then_9600);
    RUN(an_operation_after_a_probe_tries_the_probed_baud_first);
    RUN(probe_with_no_answer_fails_no_at_after_three_bauds);
    RUN(a_probe_after_junk_at_a_wrong_baud_sends_at_once_more);
    RUN(key_is_raised_before_the_first_byte_and_dropped_at_the_end);
    RUN(scan_on_firmware_2_0_lists_two_devices_with_their_names);
    RUN(a_late_name_is_not_taken_for_the_next_device);
    RUN(scan_sets_the_master_role_before_the_inquiry_on_both_firmwares);
    RUN(scan_ignores_error_17_on_init);
    RUN(scan_runs_for_inqm_timeout_plus_two_seconds);
    RUN(scan_on_firmware_3_0_ends_inq_refused_on_error_1f);
    RUN(a_timeout_fails_the_operation_at_exactly_its_limit);
    RUN(abort_ends_a_running_scan_and_drops_key);
    RUN(a_second_start_while_busy_is_refused);
    RUN(found_is_capped_at_eight_devices);
    RUN(lines_outside_a_wait_are_ignored);
    return test_summary("hc05");
}
