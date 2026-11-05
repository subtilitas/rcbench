/*
 * The HC-05 emulator's radio side (protocols/hc05emu): the inquiry and its
 * +INQ: lines, the name reads, the pairing, the restart, and the data-mode
 * link, against the scripted radio of hc05emu_rig.h.
 *
 * Under test: the +INQ: forms; the inquiry's end, the name's and the
 * pairing's FAIL, the restart, the connect's limit and the retry, each at
 * its limit and one ms before it, at tick 0 and across the wrap of the
 * 32-bit ms clock; a command that comes while the radio works; KEY low in
 * the middle of one; full AT mode's baud; the link lost in either mode;
 * links nobody asked for; CMODE 1's search by RSSI and its new start.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <string.h>

#include "greatest.h"

#include "hc05emu.h"
#include "hc05emu_rig.h"
#include "tick_wrap.h"

static const uint8_t k_phone[6] = { 0x00, 0x1A, 0x7D, 0xDA, 0x71, 0x13 };
static const uint8_t k_other[6] = { 0x20, 0x16, 0x05, 0x12, 0x34, 0x56 };

static char g_text[RIG_TEXT_MAX];
static hc05emu_t g_e;
static radio_t g_r;

/* AT mode at 38400, SPP initialised, from @p t0. */
static void at_ready(uint32_t t0)
{
    radio_init(&g_r);
    hc05emu_init(&g_e, NULL, NULL, true, t0);
    (void)emu_drain(&g_e, &g_r, t0, NULL);
    emu_cmd(&g_e, &g_r, "AT+INIT", t0, g_text);
}

/* Data mode bound to the sensor, from @p t0. */
static void data_bound(uint32_t t0, bool silent)
{
    hc05emu_cfg_t c;
    hc05emu_cfg_default(&c);
    c.bound = 1u;
    memcpy(c.bind, k_sensor, 6);
    radio_init(&g_r);
    rdev_t *d = radio_add(&g_r, k_sensor, -60, "HC-06", 100u);
    d->silent = silent;
    hc05emu_init(&g_e, &c, NULL, false, t0);
    (void)emu_drain(&g_e, &g_r, t0, NULL);
}

TEST_CASE(inquiry_lines_print_as_the_hc05_prints_them)
{
    at_ready(0);
    radio_add(&g_r, k_sensor, -64, "HC-06", 100u);
    rdev_t *p = radio_add(&g_r, k_phone, INT16_MAX, "PHONE", 200u);
    p->cod = 0x5A020Cu;
    p->name_in_inq = true;
    emu_cmd(&g_e, &g_r, "AT+INQM=1,9,2", 0, g_text);
    emu_cmd(&g_e, &g_r, "AT+INQ", 0, g_text);
    CHECK_STR_EQ(g_text, "");
    CHECK_EQ(g_r.inqs, 1u);
    CHECK_EQ(g_r.last_units, 2u);
    CHECK_EQ(g_r.last_max, 9u);
    emu_run(&g_e, &g_r, 1, 300, g_text);
    CHECK_STR_EQ(g_text, "+INQ:98D3:31:FD5C81,1F00,FFC0\r\n"
                         "+INQ:1A:7D:DA7113,5A020C,7FFF\r\n");
    /* A second report of an address prints nothing; its name is kept. */
    hc05emu_found(&g_e, k_sensor, 0x1F00u, -50, "LATE", 301);
    (void)emu_drain(&g_e, &g_r, 301, g_text);
    CHECK_STR_EQ(g_text, "");
    CHECK_EQ(g_e.found[0].rssi, -64);
    hc05emu_found(&g_e, k_sensor, 0x1F00u, -50, "LATER", 302);
    emu_run(&g_e, &g_r, 302, 2560, g_text);
    CHECK_STR_EQ(g_text, "OK\r\n");
    emu_cmd(&g_e, &g_r, "AT+RNAME?98D3,31,FD5C81", 2561, g_text);
    CHECK_STR_EQ(g_text, "+RNAME:LATE\r\nOK\r\n");
    emu_cmd(&g_e, &g_r, "AT+RNAME?1A:7D:DA7113", 2561, g_text);
    CHECK_STR_EQ(g_text, "+RNAME:PHONE\r\nOK\r\n");
    CHECK_EQ(g_r.names, 0u);
    /* Standard mode: no RSSI field; the zero address. */
    static const uint8_t zero[6] = { 0 };
    emu_cmd(&g_e, &g_r, "AT+INQM=0,9,2", 3000, g_text);
    emu_cmd(&g_e, &g_r, "AT+INQ", 3000, g_text);
    g_r.nev = 0;
    hc05emu_found(&g_e, zero, 0u, -1, NULL, 3001);
    (void)emu_drain(&g_e, &g_r, 3001, g_text);
    CHECK_STR_EQ(g_text, "+INQ:0:0:0,0\r\n");
    /* A result outside an inquiry is no line. */
    hc05emu_inq_done(&g_e, 3002);
    (void)emu_drain(&g_e, &g_r, 3002, g_text);
    CHECK_STR_EQ(g_text, "OK\r\n");
    hc05emu_found(&g_e, k_other, 0u, -1, NULL, 3003);
    hc05emu_inq_done(&g_e, 3003);
    (void)emu_drain(&g_e, &g_r, 3003, g_text);
    CHECK_STR_EQ(g_text, "");
}

TEST_CASE(an_inquiry_keeps_sixteen_devices)
{
    at_ready(0);
    emu_cmd(&g_e, &g_r, "AT+INQM=1,255,2", 0, g_text);
    emu_cmd(&g_e, &g_r, "AT+INQ", 0, g_text);
    for (unsigned i = 0; i < HC05EMU_FOUND_MAX + 2u; ++i) {
        const uint8_t a[6] = { 0, 0, 0, 0, 0, (uint8_t)i };
        hc05emu_found(&g_e, a, 0u, -1, NULL, 1);
        (void)emu_drain(&g_e, &g_r, 1, NULL);
    }
    CHECK_EQ(g_e.found_n, 16u);
    CHECK_EQ(g_e.inq_told, 16u);
    CHECK_EQ(g_e.op, HC05EMU_OP_INQ);
}

/* AT+INQ of 2 units ends with OK and STOP at 2560ms, not 1ms before. */
static void inq_limit(uint32_t t0)
{
    at_ready(t0);
    emu_cmd(&g_e, &g_r, "AT+INQM=1,9,2", t0, g_text);
    emu_cmd(&g_e, &g_r, "AT+INQ", t0, g_text);
    emu_run(&g_e, &g_r, t0 + 1u, t0 + 2559u, g_text);
    CHECK_STR_EQ(g_text, "");
    CHECK_EQ(g_r.stops, 0u);
    emu_run(&g_e, &g_r, t0 + 2560u, t0 + 2560u, g_text);
    CHECK_STR_EQ(g_text, "OK\r\n");
    CHECK_EQ(g_r.stops, 1u);
    CHECK_EQ(g_e.op, HC05EMU_OP_NONE);
}

TEST_CASE(an_inquiry_ends_with_ok_at_exactly_its_time)
{
    at_tick_0_and_before_the_wrap(inq_limit, 1000u);
}

/* AT+RNAME? to a silent device fails at 2500ms, not 1ms before. */
static void name_limit(uint32_t t0)
{
    at_ready(t0);
    rdev_t *d = radio_add(&g_r, k_sensor, -60, "HC-06", 100u);
    d->name_silent = true;
    emu_cmd(&g_e, &g_r, "AT+RNAME?98D3,31,FD5C81", t0, g_text);
    CHECK_EQ(g_r.names, 1u);
    emu_run(&g_e, &g_r, t0 + 1u, t0 + 2499u, g_text);
    CHECK_STR_EQ(g_text, "");
    emu_run(&g_e, &g_r, t0 + 2500u, t0 + 2500u, g_text);
    CHECK_STR_EQ(g_text, "FAIL\r\n");
    /* The read goes on in the radio; its late result is no line. */
    CHECK_EQ(g_r.stops, 0u);
    hc05emu_name_done(&g_e, k_sensor, "HC-06", t0 + 2600u);
    (void)emu_drain(&g_e, &g_r, t0 + 2600u, g_text);
    CHECK_STR_EQ(g_text, "");
}

TEST_CASE(a_name_read_fails_at_exactly_its_limit)
{
    at_tick_0_and_before_the_wrap(name_limit, 1000u);
}

TEST_CASE(a_name_is_cut_shown_printable_and_kept_for_the_next_read)
{
    at_ready(0);
    radio_add(&g_r, k_sensor, -60,
              "A\x01" "B\x7F" " ~DEFGHIJKLMNOPQRSTUVWXYZ0123456789", 100u);
    radio_add(&g_r, k_phone, -60, NULL, 200u);
    emu_cmd(&g_e, &g_r, "AT+INQM=1,9,1", 0, g_text);
    emu_cmd(&g_e, &g_r, "AT+INQ", 0, g_text);
    emu_run(&g_e, &g_r, 1, 1280, NULL);
    emu_cmd(&g_e, &g_r, "AT+RNAME?98D3,31,FD5C81", 1300, g_text);
    /* A result for another device is not this read's. */
    hc05emu_name_done(&g_e, k_other, "OTHER", 1301);
    emu_run(&g_e, &g_r, 1301, 1300u + g_r.name_ms, g_text);
    CHECK_STR_EQ(g_text, "+RNAME:A?B? ~DEFGHIJKLMNOPQRSTUVWXYZ01\r\nOK\r\n");
    emu_cmd(&g_e, &g_r, "AT+RNAME?98D3,31,FD5C81", 2100, g_text);
    CHECK_STR_EQ(g_text, "+RNAME:A?B? ~DEFGHIJKLMNOPQRSTUVWXYZ01\r\nOK\r\n");
    CHECK_EQ(g_r.names, 1u);
    /* A read that fails, and one of a device no inquiry found. */
    emu_cmd(&g_e, &g_r, "AT+RNAME?1A,7D,DA7113", 2200, g_text);
    emu_run(&g_e, &g_r, 2201, 2200u + g_r.name_ms, g_text);
    CHECK_STR_EQ(g_text, "FAIL\r\n");
    emu_cmd(&g_e, &g_r, "AT+RNAME?1,2,3", 3000, g_text);
    hc05emu_name_done(&g_e, (const uint8_t[6]){ 0, 1, 2, 0, 0, 3 }, "N",
                      3001);
    (void)emu_drain(&g_e, &g_r, 3001, g_text);
    CHECK_STR_EQ(g_text, "+RNAME:N\r\nOK\r\n");
}

/* AT+PAIR=...,5 to a silent device fails at 5000ms with STOP. */
static void pair_limit(uint32_t t0)
{
    at_ready(t0);
    rdev_t *d = radio_add(&g_r, k_sensor, -60, "HC-06", 100u);
    d->silent = true;
    emu_cmd(&g_e, &g_r, "AT+PAIR=98D3,31,FD5C81,5", t0, g_text);
    CHECK_STR_EQ(g_text, "");
    CHECK_EQ(g_r.pairs, 1u);
    emu_run(&g_e, &g_r, t0 + 1u, t0 + 4999u, g_text);
    CHECK_STR_EQ(g_text, "");
    emu_run(&g_e, &g_r, t0 + 5000u, t0 + 5000u, g_text);
    CHECK_STR_EQ(g_text, "FAIL\r\n");
    CHECK_EQ(g_r.stops, 1u);
    hc05emu_pair_done(&g_e, k_sensor, true, t0 + 5001u);
    (void)emu_drain(&g_e, &g_r, t0 + 5001u, g_text);
    CHECK_STR_EQ(g_text, "");
}

TEST_CASE(a_pairing_fails_at_exactly_its_time)
{
    at_tick_0_and_before_the_wrap(pair_limit, 3000u);
}

TEST_CASE(a_pairing_answers_ok_or_fail_as_the_radio_says)
{
    at_ready(0);
    radio_add(&g_r, k_sensor, -60, "HC-06", 100u);
    emu_cmd(&g_e, &g_r, "AT+PAIR=98D3,31,FD5C81,20", 0, g_text);
    hc05emu_pair_done(&g_e, k_other, false, 1);
    emu_run(&g_e, &g_r, 1, g_r.pair_ms, g_text);
    CHECK_STR_EQ(g_text, "OK\r\n");
    emu_cmd(&g_e, &g_r, "AT+PSWD=9999", 4000, g_text);
    emu_cmd(&g_e, &g_r, "AT+PAIR=98D3,31,FD5C81,20", 4000, g_text);
    emu_run(&g_e, &g_r, 4001, 4000u + g_r.fail_ms, g_text);
    CHECK_STR_EQ(g_text, "FAIL\r\n");
}

TEST_CASE(a_command_while_the_radio_works_waits_and_a_second_is_dropped)
{
    at_ready(0);
    emu_cmd(&g_e, &g_r, "AT+INQM=1,9,1", 0, g_text);
    emu_cmd(&g_e, &g_r, "AT+INQ", 0, g_text);
    emu_cmd(&g_e, &g_r, "AT+STATE?", 10, g_text);
    emu_cmd(&g_e, &g_r, "AT+VERSION?", 11, g_text);
    CHECK_STR_EQ(g_text, "");
    emu_run(&g_e, &g_r, 12, 1280, g_text);
    CHECK_STR_EQ(g_text, "OK\r\n+STATE:PAIRABLE\r\nOK\r\n");
    emu_run(&g_e, &g_r, 1281, 1300, g_text);
    CHECK_STR_EQ(g_text, "");
    /* A held line past the buffer answers ERROR:(0) when its turn comes. */
    emu_cmd(&g_e, &g_r, "AT+INQ", 2000, g_text);
    char longline[HC05EMU_LINE_MAX + 4u];
    memset(longline, 'A', sizeof longline - 1u);
    longline[sizeof longline - 1u] = '\0';
    emu_cmd(&g_e, &g_r, longline, 2001, g_text);
    hc05emu_inq_done(&g_e, 2002);
    (void)emu_drain(&g_e, &g_r, 2002, g_text);
    CHECK_STR_EQ(g_text, "OK\r\nERROR:(0)\r\n");
}

TEST_CASE(key_low_ends_a_waiting_command_without_an_answer)
{
    at_ready(0);
    emu_cmd(&g_e, &g_r, "AT+INQ", 0, g_text);
    emu_cmd(&g_e, &g_r, "AT+STATE?", 1, g_text);
    const unsigned stops = g_r.stops;
    hc05emu_key(&g_e, false, 2);
    (void)emu_drain(&g_e, &g_r, 2, g_text);
    CHECK_STR_EQ(g_text, "");
    CHECK_EQ(g_r.stops, stops + 1u);
    CHECK_EQ(g_e.op, HC05EMU_OP_NONE);
    CHECK(!g_e.held_due);
    CHECK_EQ(g_e.mode, HC05EMU_MODE_DATA);
    /* Full AT mode left: the data baud. */
    CHECK_EQ(g_r.baud, HC05EMU_BAUD_DEFAULT);
    /* Raised again: AT mode at the data baud, no BAUD. */
    const unsigned bauds = g_r.bauds;
    hc05emu_key(&g_e, true, 3);
    hc05emu_key(&g_e, true, 4);
    (void)emu_drain(&g_e, &g_r, 4, NULL);
    CHECK(hc05emu_at_mode(&g_e));
    CHECK_EQ(g_r.bauds, bauds);
    emu_cmd(&g_e, &g_r, "AT+INIT", 5, g_text);
    CHECK_STR_EQ(g_text, "ERROR:(17)\r\n");
}

/* AT+RESET restarts 300ms after its OK with KEY as it is then. */
static void restart_limit(uint32_t t0)
{
    radio_init(&g_r);
    hc05emu_init(&g_e, NULL, NULL, false, t0);
    (void)emu_drain(&g_e, &g_r, t0, NULL);
    hc05emu_key(&g_e, true, t0);
    emu_cmd(&g_e, &g_r, "AT+UART=9600,0,0", t0, g_text);
    emu_cmd(&g_e, &g_r, "AT+RESET", t0, g_text);
    CHECK_STR_EQ(g_text, "OK\r\n");
    hc05emu_key(&g_e, false, t0 + 50u);
    emu_run(&g_e, &g_r, t0 + 1u, t0 + 299u, NULL);
    CHECK_EQ(g_e.mode, HC05EMU_MODE_RESTART);
    CHECK(!hc05emu_led(&g_e, t0 + 100u));
    emu_run(&g_e, &g_r, t0 + 300u, t0 + 300u, NULL);
    CHECK_EQ(g_e.mode, HC05EMU_MODE_DATA);
    CHECK_EQ(g_r.baud, 9600u);
    /* With KEY high through the restart: full AT mode at 38400. */
    const uint32_t t1 = t0 + 1000u;
    hc05emu_key(&g_e, true, t1);
    emu_cmd(&g_e, &g_r, "AT+RESET", t1, g_text);
    emu_run(&g_e, &g_r, t1 + 1u, t1 + HC05EMU_RESTART_MS, NULL);
    CHECK_EQ(g_e.mode, HC05EMU_MODE_AT);
    CHECK_EQ(g_r.baud, HC05EMU_BAUD_FULL_AT);
}

TEST_CASE(the_restart_comes_at_exactly_its_time_and_reads_key)
{
    at_tick_0_and_before_the_wrap(restart_limit, 100u);
}

/* A connect with no answer is stopped at 15s; the next goes 1s later. */
static void connect_limit(uint32_t t0)
{
    data_bound(t0, true);
    CHECK_EQ(g_r.connects, 1u);
    CHECK_EQ(g_e.link, HC05EMU_LINK_CONNECTING);
    emu_run(&g_e, &g_r, t0 + 1u, t0 + 14999u, NULL);
    CHECK_EQ(g_r.stops, 0u);
    emu_run(&g_e, &g_r, t0 + 15000u, t0 + 15000u, NULL);
    CHECK_EQ(g_r.stops, 1u);
    CHECK_EQ(g_e.link, HC05EMU_LINK_WAIT);
    const uint32_t w = t0 + 15000u;
    emu_run(&g_e, &g_r, w + 1u, w + 999u, NULL);
    CHECK_EQ(g_r.connects, 1u);
    emu_run(&g_e, &g_r, w + 1000u, w + 1000u, NULL);
    CHECK_EQ(g_r.connects, 2u);
}

TEST_CASE(a_connect_that_never_ends_is_stopped_at_its_limit_and_tried_again)
{
    at_tick_0_and_before_the_wrap(connect_limit, 8000u);
}

/* A link lost in data mode is tried again 1s later. */
static void loss_retry(uint32_t t0)
{
    data_bound(t0, false);
    emu_run(&g_e, &g_r, t0 + 1u, t0 + g_r.conn_ms, NULL);
    CHECK(hc05emu_forwarding(&g_e));
    CHECK(hc05emu_led(&g_e, t0 + 100u));
    const uint32_t t = t0 + g_r.conn_ms + 1u;
    radio_lose(&g_r, &g_e, t);
    CHECK_EQ(g_e.link, HC05EMU_LINK_WAIT);
    emu_run(&g_e, &g_r, t, t + 999u, NULL);
    CHECK_EQ(g_r.connects, 1u);
    emu_run(&g_e, &g_r, t + 1000u, t + 1000u, NULL);
    CHECK_EQ(g_r.connects, 2u);
}

TEST_CASE(a_lost_link_is_tried_again_after_the_retry_time)
{
    at_tick_0_and_before_the_wrap(loss_retry, 1000u);
}

TEST_CASE(a_link_lost_in_at_mode_waits_for_key_low)
{
    data_bound(0, false);
    emu_run(&g_e, &g_r, 1, g_r.conn_ms, NULL);
    hc05emu_key(&g_e, true, 2000);
    CHECK(hc05emu_connected(&g_e));
    emu_cmd(&g_e, &g_r, "AT+STATE?", 2000, g_text);
    CHECK_STR_EQ(g_text, "+STATE:CONNECTED\r\nOK\r\n");
    radio_lose(&g_r, &g_e, 2001);
    CHECK_EQ(g_e.link, HC05EMU_LINK_IDLE);
    emu_run(&g_e, &g_r, 2001, 5000, NULL);
    CHECK_EQ(g_r.connects, 1u);
    hc05emu_key(&g_e, false, 5001);
    (void)emu_drain(&g_e, &g_r, 5001, NULL);
    CHECK_EQ(g_r.connects, 2u);
    /* KEY raised over a connect in progress stops it; low starts it again. */
    hc05emu_key(&g_e, true, 5002);
    (void)emu_drain(&g_e, &g_r, 5002, NULL);
    CHECK_EQ(g_e.link, HC05EMU_LINK_IDLE);
    CHECK(g_r.stops >= 1u);
    hc05emu_key(&g_e, false, 5003);
    (void)emu_drain(&g_e, &g_r, 5003, NULL);
    CHECK_EQ(g_r.connects, 3u);
    /* An inquiry closes an open link first. */
    emu_run(&g_e, &g_r, 5004, 5003u + g_r.conn_ms, NULL);
    CHECK(hc05emu_connected(&g_e));
    hc05emu_key(&g_e, true, 7000);
    const unsigned stops = g_r.stops;
    emu_cmd(&g_e, &g_r, "AT+INIT", 7000, g_text);
    CHECK_STR_EQ(g_text, "ERROR:(17)\r\n");
    emu_cmd(&g_e, &g_r, "AT+INQ", 7000, g_text);
    CHECK_EQ(g_r.stops, stops + 1u);
    CHECK(!hc05emu_connected(&g_e));
}

TEST_CASE(links_nobody_asked_for_are_closed)
{
    radio_init(&g_r);
    hc05emu_init(&g_e, NULL, NULL, false, 0);
    (void)emu_drain(&g_e, &g_r, 0, NULL);
    CHECK_EQ(g_e.link, HC05EMU_LINK_IDLE);
    hc05emu_link_event(&g_e, k_sensor, true, 1);
    (void)emu_drain(&g_e, &g_r, 1, NULL);
    CHECK_EQ(g_r.stops, 1u);
    CHECK_EQ(g_e.link, HC05EMU_LINK_IDLE);
    hc05emu_link_event(&g_e, k_sensor, false, 2);
    CHECK_EQ(g_e.link, HC05EMU_LINK_IDLE);
    data_bound(0, false);
    hc05emu_link_event(&g_e, k_other, true, 1);
    (void)emu_drain(&g_e, &g_r, 1, NULL);
    CHECK_EQ(g_r.stops, 1u);
    CHECK_EQ(g_e.link, HC05EMU_LINK_CONNECTING);
    hc05emu_link_event(&g_e, k_other, false, 2);
    CHECK_EQ(g_e.link, HC05EMU_LINK_CONNECTING);
    hc05emu_link_event(&g_e, k_sensor, true, 3);
    CHECK_EQ(g_e.link, HC05EMU_LINK_UP);
    hc05emu_link_event(&g_e, k_sensor, true, 4);
    hc05emu_link_event(&g_e, k_other, true, 4);
    (void)emu_drain(&g_e, &g_r, 4, NULL);
    CHECK_EQ(g_r.stops, 1u);
    CHECK_EQ(g_e.link, HC05EMU_LINK_UP);
    /* A connect refused at once waits for the retry. */
    data_bound(0, false);
    hc05emu_link_event(&g_e, k_sensor, false, 5);
    CHECK_EQ(g_e.link, HC05EMU_LINK_WAIT);
}

TEST_CASE(as_a_slave_unbound_or_in_slave_loop_the_bridge_asks_for_no_link)
{
    hc05emu_cfg_t c;
    for (int k = 0; k < 4; ++k) {
        hc05emu_cfg_default(&c);
        c.bound = 1u;
        memcpy(c.bind, k_sensor, 6);
        if (k == 0) {
            c.role = 0u;
        } else if (k == 1) {
            c.role = 2u;
        } else if (k == 2) {
            c.cmode = 2u;
        } else {
            c.bound = 0u;
        }
        radio_init(&g_r);
        hc05emu_init(&g_e, &c, NULL, false, 0);
        emu_run(&g_e, &g_r, 0, 3000, NULL);
        CHECK_EQ(g_r.connects + g_r.inqs, 0u);
        CHECK_EQ(g_e.link, HC05EMU_LINK_IDLE);
        CHECK(!hc05emu_led(&g_e, 100));
    }
}

TEST_CASE(cmode_1_tries_the_devices_by_rssi_and_searches_again_after_the_last)
{
    hc05emu_cfg_t c;
    hc05emu_cfg_default(&c);
    c.cmode = 1u;
    radio_init(&g_r);
    radio_add(&g_r, k_phone, INT16_MAX, "PHONE", 100u)->pin = "0";
    radio_add(&g_r, k_other, -80, "OTHER", 200u)->pin = "0";
    radio_add(&g_r, k_sensor, -50, "HC-06", 300u)->pin = "0";
    hc05emu_init(&g_e, &c, NULL, false, 0);
    (void)emu_drain(&g_e, &g_r, 0, NULL);
    CHECK_EQ(g_e.link, HC05EMU_LINK_SEARCH);
    CHECK_EQ(g_r.last_units, 8u);
    CHECK_EQ(g_r.last_max, HC05EMU_ANY_MAX);
    emu_run(&g_e, &g_r, 1, 400, NULL);
    CHECK_EQ(g_e.found_n, 3u);
    /* The radio's inquiry ends early: the search ends with it. */
    hc05emu_inq_done(&g_e, 401);
    CHECK(memcmp(g_e.found[0].addr, k_sensor, 6) == 0);
    CHECK(memcmp(g_e.found[1].addr, k_other, 6) == 0);
    CHECK(memcmp(g_e.found[2].addr, k_phone, 6) == 0);
    CHECK(memcmp(g_e.target, k_sensor, 6) == 0);
    /* Each refuses the PIN: three connects, then the retry's new search. */
    emu_run(&g_e, &g_r, 401, 401u + 3u * g_r.fail_ms + 2u, NULL);
    CHECK_EQ(g_r.connects, 3u);
    CHECK_EQ(g_e.link, HC05EMU_LINK_WAIT);
    emu_run(&g_e, &g_r, 401u + 3u * g_r.fail_ms + 3u,
            401u + 3u * g_r.fail_ms + 3u + 1000u, NULL);
    CHECK_EQ(g_r.inqs, 2u);
    CHECK_EQ(g_e.link, HC05EMU_LINK_SEARCH);
    /* The radio ends this one 600ms late: the search stops at 8 units of
     * 1.28s, 10240ms, and tries its devices from the first again. */
    const uint32_t s = g_e.link_ms;
    const unsigned stops = g_r.stops;
    emu_run(&g_e, &g_r, s + 1u, s + 10239u, NULL);
    CHECK_EQ(g_r.stops, stops);
    CHECK_EQ(g_e.found_n, 3u);
    emu_run(&g_e, &g_r, s + 10240u, s + 10240u, NULL);
    CHECK_EQ(g_r.stops, stops + 1u);
    CHECK_EQ(g_e.link, HC05EMU_LINK_CONNECTING);
    CHECK_EQ(g_r.connects, 4u);
    CHECK(memcmp(g_e.target, k_sensor, 6) == 0);
}

int main(void)
{
    RUN(inquiry_lines_print_as_the_hc05_prints_them);
    RUN(an_inquiry_keeps_sixteen_devices);
    RUN(an_inquiry_ends_with_ok_at_exactly_its_time);
    RUN(a_name_read_fails_at_exactly_its_limit);
    RUN(a_name_is_cut_shown_printable_and_kept_for_the_next_read);
    RUN(a_pairing_fails_at_exactly_its_time);
    RUN(a_pairing_answers_ok_or_fail_as_the_radio_says);
    RUN(a_command_while_the_radio_works_waits_and_a_second_is_dropped);
    RUN(key_low_ends_a_waiting_command_without_an_answer);
    RUN(the_restart_comes_at_exactly_its_time_and_reads_key);
    RUN(a_connect_that_never_ends_is_stopped_at_its_limit_and_tried_again);
    RUN(a_lost_link_is_tried_again_after_the_retry_time);
    RUN(a_link_lost_in_at_mode_waits_for_key_low);
    RUN(links_nobody_asked_for_are_closed);
    RUN(as_a_slave_unbound_or_in_slave_loop_the_bridge_asks_for_no_link);
    RUN(cmode_1_tries_the_devices_by_rssi_and_searches_again_after_the_last);
    return test_summary("hc05emu_radio");
}
