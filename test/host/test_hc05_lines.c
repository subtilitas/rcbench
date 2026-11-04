/*
 * The HC-05's answer lines and addresses (protocols/hc05/hc05_line.c).
 *
 * Under test: each kind of line hc05_parse_line() knows and every form it
 * refuses; the error code in hex at its limits; an +INQ line's address,
 * class and RSSI through a scan, with the forms a scan drops; both address
 * forms at the limits of each part; a line longer than the buffers it goes
 * through; the remote name cut and kept printable.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "hc05.h"
#include "hc05_sim.h"

TEST_CASE(ok_error_fail_and_each_plus_line_parse)
{
    char v[HC05_LINE_MAX];
    uint8_t code = 0xAAu;

    CHECK_EQ(hc05_parse_line("OK", v, sizeof v, &code), HC05_LINE_OK);
    CHECK_EQ(code, 0);
    CHECK_EQ(hc05_parse_line("FAIL", v, sizeof v, &code), HC05_LINE_FAIL);
    CHECK_EQ(hc05_parse_line("ERROR:(0)", v, sizeof v, &code),
             HC05_LINE_ERROR);
    CHECK_EQ(hc05_parse_line("+INQ:2:72:D2224,3E0104,FFBC", v, sizeof v,
                             NULL), HC05_LINE_INQ);
    CHECK_STR_EQ(v, "2:72:D2224,3E0104,FFBC");
    CHECK_EQ(hc05_parse_line("+RNAME:HC-06", v, sizeof v, NULL),
             HC05_LINE_RNAME);
    CHECK_STR_EQ(v, "HC-06");
    CHECK_EQ(hc05_parse_line("+VERSION:3.0-20170601", v, sizeof v, NULL),
             HC05_LINE_VERSION);
    CHECK_STR_EQ(v, "3.0-20170601");
    CHECK_EQ(hc05_parse_line("+STATE:INITIALIZED", v, sizeof v, NULL),
             HC05_LINE_STATE);
    CHECK_STR_EQ(v, "INITIALIZED");
    CHECK_EQ(hc05_parse_line("+BIND:3014:10:171179", v, sizeof v, NULL),
             HC05_LINE_BIND);
    CHECK_STR_EQ(v, "3014:10:171179");
    /* An empty value. */
    CHECK_EQ(hc05_parse_line("+RNAME:", v, sizeof v, NULL), HC05_LINE_RNAME);
    CHECK_STR_EQ(v, "");

    /* Lines that are none of them; the value is "". */
    const char *other[] = { "", "ok", "OK ", " OK", "OKAY", "FAILED",
                            "+INQ", "+inq:1:2:3,0,0", "+ROLE:1", "AT",
                            "+PIN:\"1234\"" };
    for (size_t i = 0; i < sizeof other / sizeof other[0]; ++i) {
        strcpy(v, "x");
        CHECK_EQ(hc05_parse_line(other[i], v, sizeof v, &code),
                 HC05_LINE_OTHER);
        CHECK_STR_EQ(v, "");
    }
}

TEST_CASE(an_error_code_is_read_in_hex)
{
    uint8_t code = 0;

    CHECK_EQ(hc05_parse_line("ERROR:(17)", NULL, 0, &code), HC05_LINE_ERROR);
    CHECK_EQ(code, 0x17);
    CHECK_EQ(hc05_parse_line("ERROR:(1F)", NULL, 0, &code), HC05_LINE_ERROR);
    CHECK_EQ(code, 0x1F);
    CHECK_EQ(hc05_parse_line("ERROR:(1f)", NULL, 0, &code), HC05_LINE_ERROR);
    CHECK_EQ(code, 0x1F);
    CHECK_EQ(hc05_parse_line("ERROR:(FF)", NULL, 0, &code), HC05_LINE_ERROR);
    CHECK_EQ(code, 0xFF);
    CHECK_EQ(hc05_parse_line("ERROR:(a)", NULL, 0, &code), HC05_LINE_ERROR);
    CHECK_EQ(code, 0x0A);
    /* The form without the colon, and a bare ERROR with code 0. */
    CHECK_EQ(hc05_parse_line("ERROR(16)", NULL, 0, &code), HC05_LINE_ERROR);
    CHECK_EQ(code, 0x16);
    code = 9;
    CHECK_EQ(hc05_parse_line("ERROR", NULL, 0, &code), HC05_LINE_ERROR);
    CHECK_EQ(code, 0);
    /* The code is set to 0 by a line that is no error. */
    code = 9;
    CHECK_EQ(hc05_parse_line("OK", NULL, 0, &code), HC05_LINE_OK);
    CHECK_EQ(code, 0);
    /* A NULL code pointer is allowed. */
    CHECK_EQ(hc05_parse_line("ERROR:(1)", NULL, 0, NULL), HC05_LINE_ERROR);

    /* Three digits, none, no hex, no closing bracket, more after it. */
    const char *bad[] = { "ERROR:(100)", "ERROR:()", "ERROR:(G)",
                          "ERROR:(17", "ERROR:(17))", "ERROR:(17) ",
                          "ERROR:", "ERROR:17", "ERRORS", "ERROR (1)",
                          "ERROR:(-1)" };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        code = 7;
        CHECK_EQ(hc05_parse_line(bad[i], NULL, 0, &code), HC05_LINE_OTHER);
        CHECK_EQ(code, 0);
    }
}

/* A scan on firmware 2.0 whose AT+INQ prints @p raw, run to its end. */
static void scan_raw(hc05_t *h, sim_t *s, const char *const *raw, unsigned n)
{
    sim_init(s, 2, HC05_BAUD_OLD);
    for (unsigned i = 0; i < n; ++i) {
        s->inq_raw[i] = raw[i];
    }
    s->nraw = n;
    hc05_init(h);
    CHECK(hc05_scan(h, 0));
    (void)sim_run(s, h, 0, 60000u);
}

TEST_CASE(an_inq_line_gives_addr_class_rssi)
{
    hc05_t h;
    sim_t s;
    const char *raw[] = {
        "+INQ:2:72:D2224,3E0104,FFBC",  /* a phone: 24-bit class */
        "+INQ:98D3:31:FD5C81,1F00,7FFF", /* no RSSI known */
        "+INQ:0:0:1,0,8000",            /* the lowest word */
        "+INQ:FFFF:FF:FFFFFF,FFFFFF",   /* no RSSI field */
    };
    scan_raw(&h, &s, raw, 4u);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_EQ(h.found_n, 4);
    CHECK_STR_EQ(h.found[0].addr, "2:72:D2224");
    CHECK_EQ(h.found[0].dev_class, 0x0104);
    CHECK_EQ(h.found[0].rssi, -68);
    CHECK_STR_EQ(h.found[1].addr, "98D3:31:FD5C81");
    CHECK_EQ(h.found[1].dev_class, 0x1F00);
    CHECK_EQ(h.found[1].rssi, 32767);
    CHECK_EQ(h.found[2].rssi, -32768);
    CHECK_EQ(h.found[2].dev_class, 0);
    CHECK_STR_EQ(h.found[3].addr, "FFFF:FF:FFFFFF");
    CHECK_EQ(h.found[3].dev_class, 0xFFFF);
    CHECK_EQ(h.found[3].rssi, 32767);
}

TEST_CASE(an_inq_line_of_another_form_is_dropped)
{
    hc05_t h;
    sim_t s;
    const char *raw[] = {
        "+INQ:2:72:D2224",             /* no class */
        "+INQ:2:72:D2224,",            /* an empty class */
        "+INQ:2:72:D2224,1000000,0",   /* a class of 7 digits */
        "+INQ:2:72:D2224,1F00,10000",  /* an RSSI of 5 digits */
        "+INQ:2:72:D2224,1F00,",       /* an empty RSSI */
        "+INQ:2:72:D2224,1F00,FFBC x", /* more after the RSSI */
        "+INQ:2:72:D2224,1F0G,FFBC",   /* no hex in the class */
        "+INQ:2,72,D2224,1F00,FFBC",   /* commas in the address */
        "+INQ:12345:72:D2224,1F00,FFBC", /* a NAP of 5 digits */
        "+INQ:2:72:D2224:1,1F00,FFBC", /* 4 parts */
        "+INQ:2:72:D2224AAAAAAAAAAAAAAAAAAA,1F00,FFBC", /* too long */
    };
    for (unsigned i = 0; i < sizeof raw / sizeof raw[0]; ++i) {
        scan_raw(&h, &s, &raw[i], 1u);
        CHECK_EQ(h.found_n, 0);
        CHECK_EQ(h.state, HC05_ST_FAILED);
        CHECK_EQ(h.err, HC05_ERR_NONE_FOUND);
    }
    /* A fourth field is not read; the line stands. */
    const char *more = "+INQ:2:72:D2224,1F00,FFBC,HC-06";
    scan_raw(&h, &s, &more, 1u);
    CHECK_EQ(h.found_n, 1);
    CHECK_EQ(h.found[0].rssi, -68);
}

TEST_CASE(addr_cmd_and_addr_show_convert_both_ways_and_refuse_bad_forms)
{
    char out[HC05_ADDR_MAX];

    CHECK(hc05_addr_cmd("3014:10:171179", out, sizeof out));
    CHECK_STR_EQ(out, "3014,10,171179");
    CHECK(hc05_addr_show("3014,10,171179", out, sizeof out));
    CHECK_STR_EQ(out, "3014:10:171179");
    /* Either form goes in to either function. */
    CHECK(hc05_addr_cmd("3014,10,171179", out, sizeof out));
    CHECK_STR_EQ(out, "3014,10,171179");
    CHECK(hc05_addr_show("3014:10:171179", out, sizeof out));
    CHECK_STR_EQ(out, "3014:10:171179");
    /* The shortest and the longest of each part; case kept. */
    CHECK(hc05_addr_cmd("0:0:0", out, sizeof out));
    CHECK_STR_EQ(out, "0,0,0");
    CHECK(hc05_addr_cmd("abCD:eF:a1b2c3", out, sizeof out));
    CHECK_STR_EQ(out, "abCD,eF,a1b2c3");
    /* The longest fills 15 bytes: 16 is the cap, 15 too few. */
    CHECK(hc05_addr_show("FFFF,FF,FFFFFF", out, 15u));
    CHECK_STR_EQ(out, "FFFF:FF:FFFFFF");
    CHECK(!hc05_addr_show("FFFF,FF,FFFFFF", out, 14u));
    CHECK_STR_EQ(out, "");
    CHECK(!hc05_addr_cmd("0:0:0", out, 5u));
    CHECK(hc05_addr_cmd("0:0:0", out, 6u));

    const char *bad[] = {
        "", "3014", "3014:10", "3014:10:", ":10:171179", "3014::171179",
        "30145:10:171179", "3014:101:171179", "3014:10:1711790",
        "3014:10,171179", "3014,10:171179", "3014:10:171179:1",
        "3014:10:17117G", "3014-10-171179", " 3014:10:171179",
        "3014:10:171179 ", "3014:10:171179,",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        strcpy(out, "x");
        CHECK(!hc05_addr_cmd(bad[i], out, sizeof out));
        CHECK_STR_EQ(out, "");
        strcpy(out, "x");
        CHECK(!hc05_addr_show(bad[i], out, sizeof out));
        CHECK_STR_EQ(out, "");
    }
    CHECK(!hc05_addr_cmd(NULL, out, sizeof out));
    CHECK(!hc05_addr_cmd("0:0:0", NULL, 16u));
    CHECK(!hc05_addr_cmd("0:0:0", out, 0u));
}

TEST_CASE(a_line_longer_than_the_buffer_is_cut_not_overrun)
{
    char line[300];
    char v[8];
    hc05_t h;
    sim_t s;

    /* The value: cut at cap - 1 bytes, a cap of 0 and 1 write nothing
     * past the buffer. */
    memset(v, 'z', sizeof v);
    CHECK_EQ(hc05_parse_line("+RNAME:ABCDEFGHIJ", v, sizeof v, NULL),
             HC05_LINE_RNAME);
    CHECK_STR_EQ(v, "ABCDEFG");
    memset(v, 'z', sizeof v);
    CHECK_EQ(hc05_parse_line("+RNAME:ABC", v, 1u, NULL), HC05_LINE_RNAME);
    CHECK_EQ(v[0], '\0');
    CHECK_EQ(v[1], 'z');
    memset(v, 'z', sizeof v);
    CHECK_EQ(hc05_parse_line("+RNAME:ABC", v, 0u, NULL), HC05_LINE_RNAME);
    CHECK_EQ(v[0], 'z');
    CHECK_EQ(hc05_parse_line(NULL, v, sizeof v, NULL), HC05_LINE_OTHER);
    CHECK_STR_EQ(v, "");

    /* The machine's line: a 298-byte line ending in OK is cut at 95 bytes
     * and the next line still reads.  The PROBE's AT gets the long line,
     * then OK. */
    hc05_init(&h);
    sim_init(&s, 2, HC05_BAUD_DATA);
    s.silent = true;
    CHECK(hc05_probe(&h, 0));
    for (uint32_t t = 0; t < 120u; ++t) {
        sim_tick(&s, &h, t);
    }
    CHECK_EQ(s.sends, 1);
    memset(line, 'A', sizeof line);
    memcpy(&line[sizeof line - 4u], "OK\r\n", 4u);
    hc05_feed(&h, (const uint8_t *)line, sizeof line);
    CHECK(h.line_n == 0);
    CHECK_EQ(h.step, 2); /* the long line is no OK: still at AT */
    CHECK_EQ(strlen(h.line), HC05_LINE_MAX - 1u);
    hc05_feed(&h, (const uint8_t *)"OK\r\n", 4u);
    CHECK_EQ(h.baud, HC05_BAUD_DATA);
    CHECK_EQ(h.step, 3);
}

TEST_CASE(a_remote_name_is_cut_and_kept_printable)
{
    hc05_t h;
    sim_t s;

    sim_init(&s, 2, HC05_BAUD_OLD);
    sim_add_dev(&s, "1:2:3", 0x1F00u, 0xFFC0u,
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", 1000u);
    sim_add_dev(&s, "1:2:4", 0x1F00u, 0xFFC0u, "A\x01" "B\x7F" "C\xC3\xA9",
                2000u);
    hc05_init(&h);
    CHECK(hc05_scan(&h, 0));
    (void)sim_run(&s, &h, 0, 60000u);
    CHECK_EQ(h.state, HC05_ST_DONE);
    CHECK_STR_EQ(h.found[0].name, "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234");
    CHECK_STR_EQ(h.found[1].name, "A?B?C??");

    /* A NUL inside a line is no end of it: "O\0K" is no OK. */
    hc05_init(&h);
    sim_init(&s, 2, HC05_BAUD_DATA);
    s.silent = true;
    CHECK(hc05_probe(&h, 0));
    for (uint32_t t = 0; t < 120u; ++t) {
        sim_tick(&s, &h, t);
    }
    hc05_feed(&h, (const uint8_t *)"O\0K\r\n", 5u);
    CHECK_EQ(h.step, 2);
    hc05_feed(&h, (const uint8_t *)"\r\nOK\n", 5u);
    CHECK_EQ(h.step, 3);
}

int main(void)
{
    RUN(ok_error_fail_and_each_plus_line_parse);
    RUN(an_error_code_is_read_in_hex);
    RUN(an_inq_line_gives_addr_class_rssi);
    RUN(an_inq_line_of_another_form_is_dropped);
    RUN(addr_cmd_and_addr_show_convert_both_ways_and_refuse_bad_forms);
    RUN(a_line_longer_than_the_buffer_is_cut_not_overrun);
    RUN(a_remote_name_is_cut_and_kept_printable);
    return test_summary("hc05_lines");
}
