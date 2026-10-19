/*
 * The Hitec bench harness's command lines and reports
 * (testbench/hitec-harness/core): every verb, its options and their
 * limits, the refusals, and the lines a transaction reports.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "hitec_harness.h"

static const hh_line_t k_closed = { false, HITEC_CLASSIC };
static const hh_line_t k_classic = { true, HITEC_CLASSIC };
static const hh_line_t k_d = { true, HITEC_D };

static hh_cmd_t c;
static char err[HH_OUT_MAX];

static bool ok(const char *line, const hh_line_t *st)
{
    err[0] = '\0';
    return hh_parse(line, st, &c, err, sizeof(err));
}

/* Refused, with a reply that starts with @p want. */
static bool refused(const char *line, const hh_line_t *st, const char *want)
{
    return !ok(line, st) && strncmp(err, want, strlen(want)) == 0;
}

/* --------------------------------------------------- the reports' lines */

#define OUT_LINES 24u
static char out[OUT_LINES][HH_OUT_MAX];
static unsigned n_out;

static void emit(void *ctx, const char *line)
{
    (void)ctx;
    if (n_out < OUT_LINES) {
        (void)snprintf(out[n_out], sizeof(out[0]), "%s", line);
    }
    ++n_out;
}

TEST_CASE(the_verbs_without_arguments_take_none)
{
    CHECK(ok("hello", &k_closed));
    CHECK_EQ(c.verb, HH_HELLO);
    CHECK(ok("help", &k_closed));
    CHECK_EQ(c.verb, HH_HELP);
    CHECK(ok("  status\r", &k_closed));
    CHECK_EQ(c.verb, HH_STATUS);
    CHECK(ok("close", &k_closed));
    CHECK(ok("abort", &k_d));
    CHECK_EQ(c.verb, HH_ABORT);
    CHECK(refused("hello there", &k_closed, "err usage hello"));
    CHECK(refused("", &k_closed, "err empty"));
    CHECK(refused("   ", &k_closed, "err empty"));
    CHECK(refused("jump", &k_closed, "err unknown jump"));
    CHECK(!hh_parse(NULL, &k_closed, &c, err, sizeof(err)));
    CHECK(!hh_parse("hello", NULL, &c, err, sizeof(err)));
    CHECK(!hh_parse("hello", &k_closed, NULL, err, sizeof(err)));
    /* No reply buffer: still refused, nothing written. */
    CHECK(!hh_parse("jump", &k_closed, &c, NULL, 0));
}

TEST_CASE(a_line_is_at_most_127_characters_and_16_words)
{
    char line[HH_LINE_MAX + 2u];

    memset(line, ' ', HH_LINE_MAX);
    memcpy(line, "hello", 5);
    line[HH_LINE_MAX] = '\0';
    CHECK(ok(line, &k_closed));
    line[HH_LINE_MAX] = ' ';
    line[HH_LINE_MAX + 1u] = '\0';
    CHECK(refused(line, &k_closed, "err line"));
    CHECK(ok("send 1 2 3 4 5 6 7 8 lead=1 gap=0 window=10 rx=1 lead=2 "
             "gap=1 window=20", &k_classic));
    CHECK(refused("send 1 2 3 4 5 6 7 8 lead=1 gap=0 window=10 rx=1 lead=2 "
                  "gap=1 window=20 rx=2", &k_classic, "err line"));
}

TEST_CASE(open_takes_a_family_a_pin_a_drive_and_a_marker)
{
    CHECK(ok("open classic 14", &k_closed));
    CHECK_EQ(c.verb, HH_OPEN);
    CHECK_EQ(c.family, HITEC_CLASSIC);
    CHECK_EQ(c.pin, 14);
    CHECK_EQ(c.drive, HITEC_DRIVE_OPEN_DRAIN);
    CHECK_EQ(c.mark, HH_NO_PIN);
    CHECK(ok("open d 0x0F pp mark=15", &k_classic) == false);
    CHECK(refused("open d 0x0F pp mark=15", &k_classic, "err range mark"));
    CHECK(ok("open d 47 pp mark=0", &k_classic));
    CHECK_EQ(c.family, HITEC_D);
    CHECK_EQ(c.pin, 47);
    CHECK_EQ(c.drive, HITEC_DRIVE_PUSH_PULL);
    CHECK_EQ(c.mark, 0);
    CHECK(ok("open d 2 od", &k_closed));
    CHECK_EQ(c.drive, HITEC_DRIVE_OPEN_DRAIN);
    CHECK(ok("open d 2 mark=3", &k_closed));
    CHECK_EQ(c.mark, 3);
    CHECK(refused("open classic 14 pp", &k_closed, "err drive"));
    CHECK(refused("open d 14 hz", &k_closed, "err drive hz"));
    CHECK(refused("open x 14", &k_closed, "err family x"));
    CHECK(refused("open d 48", &k_closed, "err range pin"));
    CHECK(refused("open d -1", &k_closed, "err range pin"));
    CHECK(refused("open d 2 mark=48", &k_closed, "err range mark"));
    CHECK(refused("open d 2 od mark=2", &k_closed, "err range mark"));
    CHECK(refused("open d 2 od led=3", &k_closed, "err option led=3"));
    CHECK(refused("open d", &k_closed, "err usage"));
}

TEST_CASE(numbers_are_decimal_or_hex_within_their_limit)
{
    CHECK(ok("c 0x61 0xff 255", &k_classic));
    CHECK_EQ(c.xfer.tx[0], 0x61);
    CHECK_EQ(c.xfer.tx[1], 0xFF);
    CHECK_EQ(c.xfer.tx[2], 0xFF);
    CHECK(ok("c 0X61 0XAb 0", &k_classic));
    CHECK_EQ(c.xfer.tx[1], 0xAB);
    CHECK(refused("c 0x61 256 0", &k_classic, "err range 256"));
    CHECK(refused("c 0x61 0x100 0", &k_classic, "err range 0x100"));
    CHECK(refused("c 0x61 0x 0", &k_classic, "err range 0x"));
    CHECK(refused("c 0x61 12a 0", &k_classic, "err range 12a"));
    CHECK(refused("c 0x61 0xg 0", &k_classic, "err range 0xg"));
    CHECK(refused("c 0x61 99999999999 0", &k_classic, "err range"));
    CHECK(ok("level 0 4000000", &k_classic));
    CHECK(refused("level 0 4000001", &k_classic, "err range 4000001"));
    CHECK(refused("level 0 0", &k_classic, "err range"));
}

TEST_CASE(c_is_a_classic_command_with_its_decoder)
{
    CHECK(ok("c 0x61 0x05 0", &k_classic));
    CHECK_EQ(c.verb, HH_C);
    CHECK_EQ(c.xfer.kind, HITEC_XFER_SEND);
    CHECK_EQ(c.xfer.n_tx, 4);
    CHECK_EQ(c.xfer.gap_bits, HITEC_C_GAP_BITS);
    CHECK_EQ(c.xfer.window_us, HITEC_C_WINDOW_US);
    CHECK_EQ(c.xfer.max_rx, HITEC_C_REPLY_LEN);
    CHECK_EQ(c.dec, HH_DEC_C);
    CHECK_EQ(c.expect, HITEC_C_EXPECT_VALUE);
    CHECK(ok("c 0x62 0x05 0x10", &k_classic));
    CHECK_EQ(c.expect, HITEC_C_EXPECT_ACK);
    /* A command the module has no reply shape for: sent, not decoded. */
    CHECK(ok("c 0x70 0 0", &k_classic));
    CHECK_EQ(c.dec, HH_DEC_NONE);
    /* The builder's values, replaced. */
    CHECK(ok("c 0x61 5 0 lead=1000 gap=6 window=3000 rx=4", &k_classic));
    CHECK_EQ(c.xfer.lead_us, 1000u);
    CHECK_EQ(c.xfer.gap_bits, 6);
    CHECK_EQ(c.xfer.window_us, 3000u);
    CHECK_EQ(c.xfer.max_rx, 4);
    CHECK(refused("c 0x61 5 0 gap=7", &k_classic, "err range gap"));
    CHECK(refused("c 0x61 5 0 rx=17", &k_classic, "err range rx"));
    CHECK(refused("c 0x61 5 0 lead=4000001", &k_classic, "err range lead"));
    CHECK(refused("c 0x61 5 0 window=4000001", &k_classic,
                  "err range window"));
    CHECK(refused("c 0x61 5 0 rx=0", &k_classic, "err range window and rx"));
    CHECK(refused("c 0x61 5 0 speed=1", &k_classic, "err option speed=1"));
    CHECK(refused("c 0x61 5", &k_classic, "err usage"));
    CHECK(refused("c 0x61 5 lead=1", &k_classic, "err usage"));
    CHECK(refused("c 0x61 5 0", &k_d, "err family"));
    CHECK(refused("c 0x61 5 0", &k_closed, "err closed"));
}

TEST_CASE(dread_and_dwrite_are_d_series_frames)
{
    CHECK(ok("dread 0x0C", &k_d));
    CHECK_EQ(c.verb, HH_DREAD);
    CHECK_EQ(c.xfer.n_tx, 5);
    CHECK_EQ(c.xfer.tx[0], 0x96);
    CHECK_EQ(c.xfer.tx[1], 0x00);
    CHECK_EQ(c.xfer.tx[2], 0x0C);
    CHECK_EQ(c.xfer.lead_us, HITEC_D_LEAD_US);
    CHECK_EQ(c.xfer.max_rx, HITEC_D_READ_RX);
    CHECK_EQ(c.dec, HH_DEC_D);
    CHECK_EQ(c.addr, 0x0C);
    CHECK(ok("dread 0x0C id=3 lead=0 window=40000", &k_d));
    CHECK_EQ(c.xfer.tx[1], 3);
    CHECK_EQ(c.xfer.lead_us, 0u);
    CHECK_EQ(c.xfer.window_us, 40000u);
    CHECK(refused("dread 0x0C id=256", &k_d, "err range id"));
    CHECK(refused("dread 0x0C rate=2", &k_d, "err option rate=2"));
    CHECK(refused("dread", &k_d, "err usage"));
    CHECK(refused("dread 0x0C", &k_classic, "err family"));

    CHECK(ok("dwrite 0x32 0x0003", &k_d));
    CHECK_EQ(c.verb, HH_DWRITE);
    CHECK_EQ(c.xfer.n_tx, 7);
    CHECK_EQ(c.xfer.tx[3], 0x02);
    CHECK_EQ(c.xfer.tx[4], 0x03);
    CHECK_EQ(c.xfer.tx[5], 0x00);
    CHECK_EQ(c.xfer.max_rx, 0);
    CHECK_EQ(c.dec, HH_DEC_NONE);
    /* A receive after a write, to see whether anything answers. */
    CHECK(ok("dwrite 0x32 3 id=1 window=20000 rx=8", &k_d));
    CHECK_EQ(c.xfer.tx[1], 1);
    CHECK_EQ(c.xfer.max_rx, 8);
    CHECK(refused("dwrite 0x32 0x10000", &k_d, "err range 0x10000"));
    CHECK(refused("dwrite 0x32 3 id=x", &k_d, "err range id"));
    CHECK(refused("dwrite 0x32", &k_d, "err usage"));
    CHECK(refused("dwrite 0x32 3", &k_classic, "err family"));
}

TEST_CASE(send_takes_1_to_8_bytes_and_the_line_s_family)
{
    CHECK(ok("send 0x96 0 0x0C 0 0x0C lead=88 window=25000 rx=8", &k_d));
    CHECK_EQ(c.verb, HH_SEND);
    CHECK_EQ(c.xfer.family, HITEC_D);
    CHECK_EQ(c.xfer.n_tx, 5);
    CHECK_EQ(c.xfer.lead_us, 88u);
    CHECK_EQ(c.dec, HH_DEC_NONE);
    CHECK(ok("send 1 2 3 4 5 6 7 8", &k_classic));
    CHECK_EQ(c.xfer.family, HITEC_CLASSIC);
    CHECK_EQ(c.xfer.n_tx, 8);
    CHECK_EQ(c.xfer.max_rx, 0);
    CHECK(refused("send 1 2 3 4 5 6 7 8 9", &k_classic, "err range"));
    CHECK(refused("send", &k_classic, "err usage"));
    CHECK(refused("send lead=5", &k_classic, "err usage"));
    CHECK(refused("send 1 256", &k_classic, "err range 256"));
    CHECK(refused("send 1 window=100", &k_classic, "err range window"));
    CHECK(refused("send 1", &k_closed, "err closed"));
}

TEST_CASE(listen_takes_a_window_a_count_an_answer_and_a_want)
{
    CHECK(ok("listen 20000 2", &k_classic));
    CHECK_EQ(c.verb, HH_LISTEN);
    CHECK_EQ(c.xfer.kind, HITEC_XFER_LISTEN);
    CHECK_EQ(c.xfer.window_us, 20000u);
    CHECK_EQ(c.xfer.max_rx, 2);
    CHECK_EQ(c.xfer.answer, 0);
    CHECK_EQ(c.dec, HH_DEC_NONE);
    CHECK(ok("listen 20000 1 answer=0x2B want=0x2b", &k_classic));
    CHECK_EQ(c.xfer.answer, 1);
    CHECK_EQ(c.xfer.answer_byte, 0x2B);
    CHECK_EQ(c.dec, HH_DEC_LISTEN);
    CHECK_EQ(c.want, 0x2B);
    CHECK(ok("listen 4000000 16", &k_d));
    CHECK_EQ(c.xfer.family, HITEC_D);
    CHECK(refused("listen 0 1", &k_classic, "err range window and rx"));
    CHECK(refused("listen 100 0", &k_classic, "err range window and rx"));
    CHECK(refused("listen 100 17", &k_classic, "err range 17"));
    CHECK(refused("listen 100 1 answer=256", &k_classic, "err range answer"));
    CHECK(refused("listen 100 1 want=-1", &k_classic, "err range want"));
    CHECK(refused("listen 100 1 lead=5", &k_classic, "err option lead=5"));
    CHECK(refused("listen 100", &k_classic, "err usage"));
}

TEST_CASE(level_holds_0_or_1_for_a_time)
{
    CHECK(ok("level 0 1000", &k_classic));
    CHECK_EQ(c.verb, HH_LEVEL);
    CHECK_EQ(c.xfer.kind, HITEC_XFER_LEVEL);
    CHECK_EQ(c.xfer.level, 0);
    CHECK_EQ(c.xfer.level_us, 1000u);
    CHECK(ok("level 1 1", &k_d));
    CHECK_EQ(c.xfer.level, 1);
    CHECK(refused("level 2 1000", &k_d, "err range 2"));
    CHECK(refused("level 0 1000 lead=1", &k_d, "err option lead=1"));
    CHECK(refused("level 0", &k_d, "err usage"));
    CHECK(!hh_is_xfer(HH_ABORT));
    CHECK(hh_is_xfer(HH_C));
    CHECK(hh_is_xfer(HH_LEVEL));
}

TEST_CASE(a_transaction_reports_its_start_bytes_and_decode)
{
    hitec_pio_run_t run;
    hitec_capture_t cap;

    /* A classic read with its reply. */
    CHECK(ok("c 0x61 0x05 0", &k_classic));
    memset(&run, 0, sizeof(run));
    run.lead_ns = 2000;
    run.span_us = 2600;
    n_out = 0;
    hh_report_start(&c, &run, emit, NULL);
    CHECK_EQ(n_out, 1u);
    CHECK(strcmp(out[0], "ok start kind=send family=classic "
                         "tx=61,05,00,9a lead_us=0 gap=1 window_us=1500 "
                         "rx=2 answer=- level=0 level_us=0 lead_ns=2000 "
                         "span_us=2600") == 0);

    memset(&cap, 0, sizeof(cap));
    cap.family = HITEC_CLASSIC;
    cap.window_ns = 1500000u;
    cap.n = 2;
    cap.full = 1;
    cap.byte[0].at_ns = 40000u;
    cap.byte[0].value = 0xC2;
    cap.byte[1].at_ns = 40000u + 521000u;
    cap.byte[1].value = HITEC_C_ACK;
    n_out = 0;
    hh_report_capture(&c, &cap, 3100u, emit, NULL);
    CHECK_EQ(n_out, 5u);
    CHECK(strcmp(out[0], "cap n=2 full=1 answered=0 overflow=0 aborted=0 "
                         "window_ns=1500000 t_us=3100") == 0);
    CHECK(strcmp(out[1], "byte 0 at_ns=40000 value=0xc2 framing=0") == 0);
    CHECK(strcmp(out[2], "byte 1 at_ns=561000 value=0x2b framing=0") == 0);
    CHECK(strncmp(out[3], "dec kind=c rx=OK value=0x00c2 id=0x00 n=2 ", 42)
          == 0);
    CHECK(strcmp(out[4], "done") == 0);

    /* A listen with an answer that heard nothing. */
    CHECK(ok("listen 20000 1 answer=0x2b want=0x2b", &k_classic));
    n_out = 0;
    hh_report_start(&c, &run, emit, NULL);
    CHECK(strstr(out[0], "kind=listen family=classic tx=- ") != NULL);
    CHECK(strstr(out[0], " answer=0x2b ") != NULL);
    memset(&cap, 0, sizeof(cap));
    cap.family = HITEC_CLASSIC;
    cap.window_ns = 20000000u;
    n_out = 0;
    hh_report_capture(&c, &cap, 20100u, emit, NULL);
    CHECK_EQ(n_out, 3u);
    CHECK(strncmp(out[1], "dec kind=listen rx=SILENT ", 26) == 0);

    /* A D write: nothing decoded. */
    CHECK(ok("dwrite 0x32 3", &k_d));
    memset(&cap, 0, sizeof(cap));
    cap.family = HITEC_D;
    n_out = 0;
    hh_report_capture(&c, &cap, 900u, emit, NULL);
    CHECK(strncmp(out[1], "dec kind=none rx=OK ", 20) == 0);

    /* A D read, cut. */
    CHECK(ok("dread 0x0C", &k_d));
    cap.aborted = 1;
    n_out = 0;
    hh_report_capture(&c, &cap, 50u, emit, NULL);
    CHECK(strncmp(out[0], "cap n=0 full=0 answered=0 overflow=0 aborted=1",
                  46) == 0);
    CHECK(strncmp(out[1], "dec kind=d rx=ABORTED ", 22) == 0);

    /* A level. */
    CHECK(ok("level 1 500", &k_d));
    n_out = 0;
    hh_report_start(&c, &run, emit, NULL);
    CHECK(strstr(out[0], "kind=level family=d tx=- ") != NULL);
    CHECK(strstr(out[0], " level=1 level_us=500 ") != NULL);
}

TEST_CASE(every_result_code_has_its_name)
{
    CHECK(strcmp(hh_rx_name(HITEC_RX_OK), "OK") == 0);
    CHECK(strcmp(hh_rx_name(HITEC_RX_ERR_SILENT), "SILENT") == 0);
    CHECK(strcmp(hh_rx_name(HITEC_RX_ERR_LENGTH), "LENGTH") == 0);
    CHECK(strcmp(hh_rx_name(HITEC_RX_ERR_SUM), "SUM") == 0);
    CHECK(strcmp(hh_rx_name((hitec_rx_t)(HITEC_RX_ERR_SUM + 1)), "?") == 0);

    n_out = 0;
    hh_report_help(emit, NULL);
    CHECK(n_out > 2u);
    CHECK(strcmp(out[n_out - 1u], "ok help") == 0);
}

int main(void)
{
    RUN(the_verbs_without_arguments_take_none);
    RUN(a_line_is_at_most_127_characters_and_16_words);
    RUN(open_takes_a_family_a_pin_a_drive_and_a_marker);
    RUN(numbers_are_decimal_or_hex_within_their_limit);
    RUN(c_is_a_classic_command_with_its_decoder);
    RUN(dread_and_dwrite_are_d_series_frames);
    RUN(send_takes_1_to_8_bytes_and_the_line_s_family);
    RUN(listen_takes_a_window_a_count_an_answer_and_a_want);
    RUN(level_holds_0_or_1_for_a_time);
    RUN(a_transaction_reports_its_start_bytes_and_decode);
    RUN(every_result_code_has_its_name);
    return test_summary("hitec_harness");
}
