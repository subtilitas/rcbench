/*
 * The HC-05 emulator's commands and lines (protocols/hc05emu), the module
 * alone, line by line.
 *
 * Under test: every query and set command and its answer, the save of
 * every change; each argument out of range with the datasheet's code; the
 * name in either case; a line past the buffer, an empty line, a NUL and a
 * CR; the settings record from storage, field by field; the version text;
 * the LED in each mode; the queue's limit; NULL arguments.  The radio's
 * side and the timeouts are in test_hc05emu_radio.c.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <string.h>

#include "greatest.h"

#include "hc05emu.h"
#include "hc05emu_rig.h"

static char g_text[RIG_TEXT_MAX];

/* A fresh emulator in AT mode at 38400 baud, its start drained. */
static void at_mode(hc05emu_t *e, radio_t *r)
{
    radio_init(r);
    hc05emu_init(e, NULL, NULL, true, 0);
    (void)emu_drain(e, r, 0, NULL);
}

/* @p line answers @p want, nothing else queued. */
static void answers(hc05emu_t *e, radio_t *r, const char *line,
                    const char *want)
{
    emu_cmd(e, r, line, 0, g_text);
    CHECK_STR_EQ(g_text, want);
}

TEST_CASE(every_query_answers_the_setting_and_ok)
{
    hc05emu_t e;
    radio_t r;
    at_mode(&e, &r);
    answers(&e, &r, "AT", "OK\r\n");
    answers(&e, &r, "AT+VERSION?", "+VERSION:btbridge\r\nOK\r\n");
    answers(&e, &r, "AT+ROLE?", "+ROLE:1\r\nOK\r\n");
    answers(&e, &r, "AT+CMODE?", "+CMODE:0\r\nOK\r\n");
    answers(&e, &r, "AT+PSWD?", "+PSWD:1234\r\nOK\r\n");
    answers(&e, &r, "AT+BIND?", "+BIND:0:0:0\r\nOK\r\n");
    answers(&e, &r, "AT+UART?", "+UART:115200,0,0\r\nOK\r\n");
    answers(&e, &r, "AT+INQM?", "+INQM:1,1,48\r\nOK\r\n");
    answers(&e, &r, "AT+STATE?", "+STATE:INITIALIZED\r\nOK\r\n");
    CHECK_EQ(r.saves, 0u);
}

TEST_CASE(every_set_is_stored_saved_and_answered_ok)
{
    hc05emu_t e;
    radio_t r;
    at_mode(&e, &r);
    answers(&e, &r, "AT+ROLE=0", "OK\r\n");
    answers(&e, &r, "AT+ROLE?", "+ROLE:0\r\nOK\r\n");
    answers(&e, &r, "AT+CMODE=2", "OK\r\n");
    answers(&e, &r, "AT+PSWD=0000", "OK\r\n");
    answers(&e, &r, "AT+PSWD?", "+PSWD:0000\r\nOK\r\n");
    answers(&e, &r, "AT+PSWD=\"1234567890ABCDEF\"", "OK\r\n");
    CHECK_STR_EQ(e.cfg.pin, "1234567890ABCDEF");
    answers(&e, &r, "AT+BIND=98d3,31,fd5c81", "OK\r\n");
    answers(&e, &r, "AT+BIND?", "+BIND:98D3:31:FD5C81\r\nOK\r\n");
    answers(&e, &r, "AT+BIND=0,1,02", "OK\r\n");
    answers(&e, &r, "AT+BIND?", "+BIND:0:1:2\r\nOK\r\n");
    answers(&e, &r, "AT+BIND=1A:7D:DA7113", "OK\r\n");
    answers(&e, &r, "AT+BIND?", "+BIND:1A:7D:DA7113\r\nOK\r\n");
    answers(&e, &r, "AT+UART=9600,1,2", "OK\r\n");
    answers(&e, &r, "AT+UART?", "+UART:9600,1,2\r\nOK\r\n");
    /* Taken at the next start, not now. */
    CHECK_EQ(e.baud_now, HC05EMU_BAUD_FULL_AT);
    answers(&e, &r, "AT+INQM=0,255,1", "OK\r\n");
    answers(&e, &r, "AT+INQM?", "+INQM:0,255,1\r\nOK\r\n");
    answers(&e, &r, "AT+INQM=2,1,48", "OK\r\n");
    answers(&e, &r, "AT+INQM?", "+INQM:2,1,48\r\nOK\r\n");
    CHECK_EQ(r.saves, 10u);
    CHECK(memcmp(&r.nvs, &e.cfg, sizeof r.nvs) == 0);
    answers(&e, &r, "AT+ORGL", "OK\r\n");
    hc05emu_cfg_t d;
    hc05emu_cfg_default(&d);
    CHECK(memcmp(&e.cfg, &d, sizeof d) == 0);
    CHECK(memcmp(&r.nvs, &d, sizeof d) == 0);
    CHECK_EQ(r.saves, 11u);
    answers(&e, &r, "AT+INIT", "OK\r\n");
    answers(&e, &r, "AT+INIT", "ERROR:(17)\r\n");
    answers(&e, &r, "AT+STATE?", "+STATE:PAIRABLE\r\nOK\r\n");
}

TEST_CASE(arguments_out_of_range_answer_the_datasheet_codes)
{
    static const char *const cases[][2] = {
        { "AT+ROLE=3", "ERROR:(11)" },
        { "AT+ROLE=99999999999", "ERROR:(0)" },
        { "AT+ROLE=1x", "ERROR:(0)" },
        { "AT+ROLE", "ERROR:(0)" },
        { "AT+CMODE=3", "ERROR:(0)" },
        { "AT+UART=1234,0,0", "ERROR:(12)" },
        { "AT+UART=9600,2,0", "ERROR:(13)" },
        { "AT+UART=9600,0,3", "ERROR:(14)" },
        { "AT+UART=9600", "ERROR:(0)" },
        { "AT+UART=9600,0,0,", "ERROR:(0)" },
        { "AT+INQM=3,9,9", "ERROR:(18)" },
        { "AT+INQM=1,9,49", "ERROR:(19)" },
        { "AT+INQM=1,9,0", "ERROR:(19)" },
        { "AT+INQM=1,0,9", "ERROR:(0)" },
        { "AT+INQM=1,256,9", "ERROR:(0)" },
        { "AT+INQM=1", "ERROR:(0)" },
        { "AT+PSWD=", "ERROR:(F)" },
        { "AT+PSWD=\"\"", "ERROR:(F)" },
        { "AT+PSWD=12345678901234567", "ERROR:(10)" },
        { "AT+PSWD=\"12", "ERROR:(0)" },
        { "AT+PSWD=\"", "ERROR:(0)" },
        { "AT+PSWD=12\"3", "ERROR:(0)" },
        { "AT+PSWD=12,3", "ERROR:(0)" },
        { "AT+PSWD=12\x7F", "ERROR:(0)" },
        { "AT+PSWD", "ERROR:(0)" },
        { "AT+BIND=", "ERROR:(1A)" },
        { "AT+BIND=12345,1,1", "ERROR:(5)" },
        { "AT+BIND=1,123,1", "ERROR:(6)" },
        { "AT+BIND=1,1,1234567", "ERROR:(7)" },
        { "AT+BIND=1,1", "ERROR:(0)" },
        { "AT+BIND=1,,1", "ERROR:(0)" },
        { "AT+BIND=1,1:1", "ERROR:(0)" },
        { "AT+BIND=1;1;1", "ERROR:(0)" },
        { "AT+BIND=G", "ERROR:(0)" },
        { "AT+BIND=1,1,1x", "ERROR:(0)" },
        { "AT+BIND", "ERROR:(0)" },
        { "AT+RNAME?", "ERROR:(1A)" },
        { "AT+RNAME?1,1,1x", "ERROR:(0)" },
        { "AT+RNAME?12345,1,1", "ERROR:(5)" },
        { "AT+RNAME=1,1,1", "ERROR:(0)" },
        { "AT+PAIR=1,1,1", "ERROR:(0)" },
        { "AT+PAIR=1,1,1,0", "ERROR:(0)" },
        { "AT+PAIR=1,1,1,256", "ERROR:(0)" },
        { "AT+PAIR=1,1,1,20x", "ERROR:(0)" },
        { "AT+PAIR=12345,1,1,20", "ERROR:(5)" },
        { "AT+PAIR?", "ERROR:(0)" },
        { "AT+INQ", "ERROR:(16)" },
        { "AT+INQ?", "ERROR:(0)" },
        { "AT+INIT?", "ERROR:(0)" },
        { "AT+STATE", "ERROR:(0)" },
        { "AT+VERSION", "ERROR:(0)" },
        { "AT+VERSION??", "ERROR:(0)" },
        { "AT+RESET=1", "ERROR:(0)" },
        { "AT+ORGL?", "ERROR:(0)" },
        { "AT+NAME?", "ERROR:(0)" },
        { "AT+VERSIONXY?", "ERROR:(0)" },
        { "AT+", "ERROR:(0)" },
        { "ATZ", "ERROR:(0)" },
        { "XAT", "ERROR:(0)" },
        { "A", "ERROR:(0)" },
    };
    hc05emu_t e;
    radio_t r;
    at_mode(&e, &r);
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        char want[32];
        snprintf(want, sizeof want, "%s\r\n", cases[i][1]);
        emu_cmd(&e, &r, cases[i][0], 0, g_text);
        if (strcmp(g_text, want) != 0) {
            printf("      %s\n", cases[i][0]);
        }
        CHECK_STR_EQ(g_text, want);
    }
    /* Nothing was stored, nothing asked of the radio. */
    hc05emu_cfg_t d;
    hc05emu_cfg_default(&d);
    CHECK(memcmp(&e.cfg, &d, sizeof d) == 0);
    CHECK_EQ(r.saves, 0u);
    CHECK_EQ(r.inqs + r.names + r.pairs, 0u);
    /* As a slave: no inquiry. */
    answers(&e, &r, "AT+INIT", "OK\r\n");
    answers(&e, &r, "AT+ROLE=0", "OK\r\n");
    answers(&e, &r, "AT+INQ", "ERROR:(11)\r\n");
}

TEST_CASE(names_are_read_in_either_case_and_arguments_as_given)
{
    hc05emu_t e;
    radio_t r;
    at_mode(&e, &r);
    answers(&e, &r, "at", "OK\r\n");
    answers(&e, &r, "aT+rOlE?", "+ROLE:1\r\nOK\r\n");
    answers(&e, &r, "at+pswd=abCD", "OK\r\n");
    CHECK_STR_EQ(e.cfg.pin, "abCD");
    /* Every printable byte but `"` and `,`, the space and `~` among them. */
    answers(&e, &r, "AT+PSWD= 1~", "OK\r\n");
    CHECK_STR_EQ(e.cfg.pin, " 1~");
}

TEST_CASE(a_line_past_the_buffer_a_nul_and_an_empty_line)
{
    hc05emu_t e;
    radio_t r;
    at_mode(&e, &r);
    char longline[HC05EMU_LINE_MAX + 10u];
    memset(longline, 'A', sizeof longline - 1u);
    memcpy(longline, "AT+PSWD=", 8);
    longline[sizeof longline - 1u] = '\0';
    answers(&e, &r, longline, "ERROR:(0)\r\n");
    /* The buffer holds 79 bytes: a line of 79 is taken, one of 80 is not. */
    char fits[81];
    memset(fits, '0', sizeof fits - 1u);
    memcpy(fits, "AT+ROLE=", 8);
    fits[78] = '2';
    fits[79] = '\0';
    answers(&e, &r, fits, "OK\r\n");
    CHECK_EQ(e.cfg.role, 2u);
    fits[78] = '0';
    fits[79] = '1';
    fits[80] = '\0';
    answers(&e, &r, fits, "ERROR:(0)\r\n");
    CHECK_EQ(e.cfg.role, 2u);
    /* KEY's change drops a line begun before it. */
    hc05emu_feed(&e, (const uint8_t *)"AT+RO", 5u, 0);
    hc05emu_key(&e, false, 0);
    hc05emu_key(&e, true, 0);
    (void)emu_drain(&e, &r, 0, NULL);
    answers(&e, &r, "LE?", "ERROR:(0)\r\n");
    answers(&e, &r, "", "");
    static const uint8_t nul[] = { 'A', 'T', 0, '\r', '\n' };
    hc05emu_feed(&e, nul, sizeof nul, 0);
    (void)emu_drain(&e, &r, 0, g_text);
    CHECK_STR_EQ(g_text, "ERROR:(0)\r\n");
    /* CR is dropped wherever it stands; LF alone ends a line. */
    static const uint8_t cr[] = "A\rT\n";
    hc05emu_feed(&e, cr, sizeof cr - 1u, 0);
    (void)emu_drain(&e, &r, 0, g_text);
    CHECK_STR_EQ(g_text, "OK\r\n");
    /* Two commands in one feed. */
    static const uint8_t two[] = "AT\r\nAT+CMODE?\r\n";
    hc05emu_feed(&e, two, sizeof two - 1u, 0);
    (void)emu_drain(&e, &r, 0, g_text);
    CHECK_STR_EQ(g_text, "OK\r\n+CMODE:0\r\nOK\r\n");
}

TEST_CASE(bytes_outside_at_mode_are_not_commands)
{
    hc05emu_t e;
    radio_t r;
    radio_init(&r);
    hc05emu_init(&e, NULL, NULL, false, 0);
    (void)emu_drain(&e, &r, 0, NULL);
    CHECK(!hc05emu_at_mode(&e));
    emu_cmd(&e, &r, "AT", 0, g_text);
    CHECK_STR_EQ(g_text, "");
    /* AT+RESET: deaf until the restart. */
    hc05emu_key(&e, true, 10);
    CHECK(hc05emu_at_mode(&e));
    emu_cmd(&e, &r, "AT+RESET", 10, g_text);
    CHECK_STR_EQ(g_text, "OK\r\n");
    emu_cmd(&e, &r, "AT", 11, g_text);
    CHECK_STR_EQ(g_text, "");
    CHECK(!hc05emu_at_mode(&e));
}

TEST_CASE(a_stored_record_is_taken_only_when_every_field_is_in_range)
{
    hc05emu_cfg_t good;
    hc05emu_cfg_default(&good);
    CHECK(hc05emu_cfg_valid(&good));
    CHECK(!hc05emu_cfg_valid(NULL));
    hc05emu_cfg_t c;
#define BAD(field, value)                                                     \
    do {                                                                      \
        c = good;                                                             \
        c.field = value;                                                      \
        CHECK(!hc05emu_cfg_valid(&c));                                        \
    } while (0)
    BAD(role, 3u);
    BAD(cmode, 3u);
    BAD(bound, 2u);
    BAD(baud, 115201u);
    BAD(stop, 2u);
    BAD(parity, 3u);
    BAD(inq_mode, 3u);
    BAD(inq_max, 0u);
    BAD(inq_units, 0u);
    BAD(inq_units, 49u);
#undef BAD
    c = good;
    c.role = 2u;
    c.cmode = 2u;
    c.bound = 1u;
    c.stop = 1u;
    c.parity = 2u;
    c.inq_mode = 2u;
    c.inq_max = 255u;
    c.inq_units = 48u;
    c.baud = 1382400u;
    CHECK(hc05emu_cfg_valid(&c));
    c.baud = 4800u;
    CHECK(hc05emu_cfg_valid(&c));
    /* The PIN: 1 to 16 printable bytes, no quote, no comma, terminated. */
    c = good;
    c.pin[0] = '\0';
    CHECK(!hc05emu_cfg_valid(&c));
    memset(c.pin, '7', sizeof c.pin);
    CHECK(!hc05emu_cfg_valid(&c));
    c.pin[16] = '\0';
    CHECK(hc05emu_cfg_valid(&c));
    static const char bad_chars[] = { '"', ',', 0x1F, 0x7F };
    for (size_t i = 0; i < sizeof bad_chars; ++i) {
        c = good;
        c.pin[1] = bad_chars[i];
        CHECK(!hc05emu_cfg_valid(&c));
    }
    c = good;
    c.pin[1] = ' ';
    c.pin[2] = '~';
    CHECK(hc05emu_cfg_valid(&c));
    /* A bad record starts the defaults; a good one is taken whole. */
    hc05emu_t e;
    c = good;
    c.role = 9u;
    hc05emu_init(&e, &c, NULL, false, 0);
    CHECK(memcmp(&e.cfg, &good, sizeof good) == 0);
    c = good;
    c.baud = 9600u;
    hc05emu_init(&e, &c, NULL, false, 0);
    CHECK_EQ(e.cfg.baud, 9600u);
    CHECK_EQ(e.baud_now, 9600u);
    /* Full AT mode is 8N1 at 38400; KEY low takes the stored UART. */
    radio_t r;
    radio_init(&r);
    c.stop = 1u;
    c.parity = 2u;
    hc05emu_init(&e, &c, NULL, true, 0);
    (void)emu_drain(&e, &r, 0, NULL);
    CHECK_EQ(r.baud, 38400u);
    CHECK_EQ(r.stop, 0u);
    CHECK_EQ(r.parity, 0u);
    hc05emu_key(&e, false, 1);
    (void)emu_drain(&e, &r, 1, NULL);
    CHECK_EQ(r.baud, 9600u);
    CHECK_EQ(r.stop, 1u);
    CHECK_EQ(r.parity, 2u);
    /* Raised and dropped again: no second BAUD. */
    hc05emu_key(&e, true, 2);
    hc05emu_key(&e, false, 3);
    (void)emu_drain(&e, &r, 3, NULL);
    CHECK_EQ(r.bauds, 2u);
}

TEST_CASE(the_version_text_is_cut_to_23_bytes)
{
    hc05emu_t e;
    radio_t r;
    radio_init(&r);
    hc05emu_init(&e, NULL, "btbridge-0.23.7-and-a-long-tail", true, 0);
    (void)emu_drain(&e, &r, 0, NULL);
    answers(&e, &r, "AT+VERSION?", "+VERSION:btbridge-0.23.7-and-a-l\r\nOK\r\n");
    CHECK_EQ(strlen(e.version), HC05EMU_VERSION_MAX - 1u);
}

TEST_CASE(the_led_shows_the_mode_and_the_link)
{
    hc05emu_t e;
    radio_t r;
    radio_init(&r);
    hc05emu_init(&e, NULL, NULL, true, 0);
    (void)emu_drain(&e, &r, 0, NULL);
    CHECK(hc05emu_led(&e, 0));
    CHECK(hc05emu_led(&e, 999));
    CHECK(!hc05emu_led(&e, 1000));
    CHECK(!hc05emu_led(&e, 1999));
    CHECK(hc05emu_led(&e, 2000));
    hc05emu_key(&e, false, 0);
    CHECK(hc05emu_led(&e, 99));
    CHECK(!hc05emu_led(&e, 100));
    CHECK(hc05emu_led(&e, 200));
    e.link = HC05EMU_LINK_UP;
    CHECK(hc05emu_led(&e, 100));
    e.mode = HC05EMU_MODE_RESTART;
    CHECK(!hc05emu_led(&e, 0));
    CHECK(!hc05emu_led(NULL, 0));
}

TEST_CASE(a_full_queue_drops_the_newest_and_counts_it)
{
    hc05emu_t e;
    radio_t r;
    at_mode(&e, &r);
    for (unsigned i = 0; i < HC05EMU_Q_MAX + 3u; ++i) {
        hc05emu_feed(&e, (const uint8_t *)"AT\n", 3u, 0);
    }
    CHECK_EQ(e.q_n, HC05EMU_Q_MAX);
    CHECK_EQ(e.q_lost, 3u);
    /* A set on a full queue: neither SAVE nor OK fits, the value is set. */
    hc05emu_feed(&e, (const uint8_t *)"AT+ROLE=0\n", 10u, 0);
    CHECK_EQ(e.cfg.role, 0u);
    CHECK_EQ(e.q_lost, 5u);
    (void)emu_drain(&e, &r, 0, g_text);
    CHECK_EQ(strlen(g_text), 4u * HC05EMU_Q_MAX);
    /* Every action with its fields on a full queue is dropped whole. */
    for (unsigned i = 0; i < HC05EMU_Q_MAX; ++i) {
        hc05emu_feed(&e, (const uint8_t *)"AT\n", 3u, 0);
    }
    hc05emu_feed(&e, (const uint8_t *)"AT+INIT\nAT+ROLE=1\n", 18u, 0);
    hc05emu_feed(&e, (const uint8_t *)"AT+RNAME?1,1,1\n", 15u, 0);
    CHECK_EQ(e.op, HC05EMU_OP_NAME);
    e.op = HC05EMU_OP_NONE;
    hc05emu_feed(&e, (const uint8_t *)"AT+PAIR=1,1,1,5\n", 16u, 0);
    CHECK_EQ(e.op, HC05EMU_OP_PAIR);
    e.op = HC05EMU_OP_NONE;
    hc05emu_feed(&e, (const uint8_t *)"AT+INQ\n", 7u, 0);
    CHECK_EQ(e.op, HC05EMU_OP_INQ);
    (void)emu_drain(&e, &r, 0, NULL);
    CHECK_EQ(r.names + r.pairs + r.inqs, 0u);
}

TEST_CASE(null_arguments_change_nothing)
{
    hc05emu_t e;
    hc05emu_act_t a;
    static const uint8_t addr[6] = { 0 };
    hc05emu_init(NULL, NULL, NULL, false, 0);
    hc05emu_key(NULL, true, 0);
    hc05emu_feed(NULL, addr, 1u, 0);
    hc05emu_found(NULL, addr, 0, 0, NULL, 0);
    hc05emu_inq_done(NULL, 0);
    hc05emu_name_done(NULL, addr, "x", 0);
    hc05emu_pair_done(NULL, addr, true, 0);
    hc05emu_link_event(NULL, addr, true, 0);
    hc05emu_cfg_default(NULL);
    CHECK(!hc05emu_step(NULL, 0, &a));
    CHECK_EQ(a.kind, HC05EMU_ACT_NONE);
    CHECK(!hc05emu_at_mode(NULL));
    CHECK(!hc05emu_forwarding(NULL));
    CHECK(!hc05emu_connected(NULL));
    hc05emu_init(&e, NULL, NULL, true, 0);
    CHECK(!hc05emu_step(&e, 0, NULL));
    /* A step with nothing to hand out leaves no length behind. */
    hc05emu_feed(&e, (const uint8_t *)"AT\n", 3u, 0);
    CHECK(hc05emu_step(&e, 0, &a) && a.kind == HC05EMU_ACT_BAUD);
    CHECK(hc05emu_step(&e, 0, &a) && a.len == 4u);
    CHECK(!hc05emu_step(&e, 0, &a));
    CHECK_EQ(a.len, 0u);
    hc05emu_init(&e, NULL, NULL, true, 0);
    hc05emu_feed(&e, NULL, 4u, 0);
    hc05emu_found(&e, NULL, 0, 0, NULL, 0);
    hc05emu_name_done(&e, NULL, "x", 0);
    hc05emu_pair_done(&e, NULL, true, 0);
    hc05emu_link_event(&e, NULL, true, 0);
    CHECK_EQ(e.q_n, 1u); /* the start's BAUD */
    CHECK_EQ(e.line_n, 0u);
}

int main(void)
{
    RUN(every_query_answers_the_setting_and_ok);
    RUN(every_set_is_stored_saved_and_answered_ok);
    RUN(arguments_out_of_range_answer_the_datasheet_codes);
    RUN(names_are_read_in_either_case_and_arguments_as_given);
    RUN(a_line_past_the_buffer_a_nul_and_an_empty_line);
    RUN(bytes_outside_at_mode_are_not_commands);
    RUN(a_stored_record_is_taken_only_when_every_field_is_in_range);
    RUN(the_version_text_is_cut_to_23_bytes);
    RUN(the_led_shows_the_mode_and_the_link);
    RUN(a_full_queue_drops_the_newest_and_counts_it);
    RUN(null_arguments_change_nothing);
    return test_summary("hc05emu");
}
