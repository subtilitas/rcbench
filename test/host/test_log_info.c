/**
 * @file test_log_info.c
 * @brief The `info` cell (log_info.h): its keys and escape rule, the date
 *        and an unset clock, the longest cell, the reader of a whole file,
 *        and the three bench files (MOTOR, SERVO, SUPPL) written by
 *        log_writer.c and read back with LOGS' reader at 26 and at 25
 *        columns.  ATEST, LIMIT and RUDDR files are read back in
 *        test_servo_test.c, test_servo_limrun_rows.c and test_servo_rauto.c.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "greatest.h"

#include "info_check.h"
#include "log_cols.h"
#include "log_info.h"
#include "log_writer.h"
#include "rcbench_version.h"
#include "run_info.h"
#include "settings.h"
#include "wall_clock.h"

/* ------------------------------------------------------------- the sink */

/** @brief A file in memory. */
typedef struct {
    char   buf[16 * 1024];  /**< the bytes */
    size_t len;             /**< their count */
} mem_sink_t;

static mem_sink_t g_mem;

static int mem_write(void *ctx, const void *data, size_t len)
{
    mem_sink_t *m = (mem_sink_t *)ctx;
    if (m->len + len >= sizeof(m->buf)) {
        return -1;
    }
    memcpy(m->buf + m->len, data, len);
    m->len += len;
    m->buf[m->len] = '\0';
    return (int)len;
}

static log_writer_t writer(void)
{
    memset(&g_mem, 0, sizeof(g_mem));
    const log_sink_t sink = { .write = mem_write, .flush = NULL,
                              .ctx = &g_mem };
    log_writer_t w;
    log_writer_init(&w, &sink);
    return w;
}

/* ------------------------------------------------------------ the clock */

static int64_t g_now;

static int64_t fixed_clock(void)
{
    return g_now;
}

/** @brief 2026-11-24 10:31:00 UTC. */
#define NOV_24_1031 1795516260LL

/** @brief The zone UTC, so a local date is the UTC date. */
static void utc(void)
{
    setenv("TZ", "UTC0", 1);
    tzset();
}

/* ------------------------------------------------------ the cell itself */

TEST_CASE(the_cell_carries_its_keys_in_order_and_reads_back)
{
    log_info_t in;
    memset(&in, 0, sizeof(in));
    snprintf(in.dut, sizeof(in.dut), "MG90S");
    snprintf(in.note, sizeof(in.note), "greased");
    snprintf(in.date, sizeof(in.date), "2026-11-24T10:31");
    snprintf(in.fw, sizeof(in.fw), "0.23.18");
    char cell[LOG_INFO_MAX];
    const size_t n = log_info_format(&in, cell, sizeof(cell));
    CHECK_STR_EQ(cell, "dut=MG90S&note=greased&date=2026-11-24T10:31"
                       "&fw=0.23.18");
    CHECK_EQ(n, strlen(cell));
    log_info_t out;
    CHECK(log_info_parse(cell, &out));
    info_same(&out, &in);

    /* servo, meter and angle after fw. */
    snprintf(in.servo, sizeof(in.servo), "DIGITAL");
    snprintf(in.meter, sizeof(in.meter), "INA3221 0x41 CH2");
    snprintf(in.angle, sizeof(in.angle), "AS5600");
    (void)log_info_format(&in, cell, sizeof(cell));
    CHECK_STR_EQ(cell, "dut=MG90S&note=greased&date=2026-11-24T10:31"
                       "&fw=0.23.18&servo=DIGITAL&meter=INA3221 0x41 CH2"
                       "&angle=AS5600");
    CHECK(log_info_parse(cell, &out));
    info_same(&out, &in);
}

TEST_CASE(an_empty_value_leaves_its_key_out)
{
    log_info_t in;
    memset(&in, 0, sizeof(in));
    char cell[LOG_INFO_MAX] = "x";
    CHECK_EQ(log_info_format(&in, cell, sizeof(cell)), 0u);
    CHECK_STR_EQ(cell, "");
    snprintf(in.fw, sizeof(in.fw), "0.23.18");
    (void)log_info_format(&in, cell, sizeof(cell));
    CHECK_STR_EQ(cell, "fw=0.23.18");
    snprintf(in.angle, sizeof(in.angle), "IMU X+");
    (void)log_info_format(&in, cell, sizeof(cell));
    CHECK_STR_EQ(cell, "fw=0.23.18&angle=IMU X+");
    CHECK_EQ(log_info_format(NULL, cell, sizeof(cell)), 0u);
    CHECK_STR_EQ(cell, "");
    CHECK_EQ(log_info_format(&in, NULL, 8u), 0u);
}

TEST_CASE(amp_equals_semicolon_and_quote_are_written_as_underscores)
{
    log_info_t in;
    memset(&in, 0, sizeof(in));
    snprintf(in.dut, sizeof(in.dut), "A&B=C;D\"E");
    snprintf(in.note, sizeof(in.note), "tab\there\nline\x7f");
    char cell[LOG_INFO_MAX];
    (void)log_info_format(&in, cell, sizeof(cell));
    CHECK_STR_EQ(cell, "dut=A_B_C_D_E&note=tab_here_line_");
    log_info_t out;
    CHECK(log_info_parse(cell, &out));
    CHECK_STR_EQ(out.dut, "A_B_C_D_E");
    CHECK_STR_EQ(out.note, "tab_here_line_");

    char clean[8];
    log_info_clean(clean, sizeof(clean), "a&b=c;d\"e");
    CHECK_STR_EQ(clean, "a_b_c_d");     /* cut to 7 characters */
    log_info_clean(clean, sizeof(clean), NULL);
    CHECK_STR_EQ(clean, "");
    /* Other characters pass, the separators of other files among them. */
    char other[LOG_INFO_TEXT_MAX];
    log_info_clean(other, sizeof(other), "38\xc2\xb0" "C, 5V|x+");
    CHECK_STR_EQ(other, "38\xc2\xb0" "C, 5V|x+");
}

TEST_CASE(the_longest_cell_fits_and_a_byte_less_takes_none)
{
    log_info_t in;
    info_longest(&in);
    char cell[LOG_INFO_MAX];
    const size_t n = log_info_format(&in, cell, sizeof(cell));
    CHECK_EQ(n, LOG_INFO_MAX - 1u);
    CHECK_EQ(strlen(cell), LOG_INFO_MAX - 1u);
    log_info_t out;
    CHECK(log_info_parse(cell, &out));
    info_same(&out, &in);
    CHECK_STR_EQ(out.dut, "MG90S_METAL_GEAR_NO_123");
    CHECK_EQ(strlen(out.dut), 23u);
    CHECK_EQ(strlen(out.note), 23u);

    char small[LOG_INFO_MAX - 1u];
    CHECK_EQ(log_info_format(&in, small, sizeof(small)), 0u);
    CHECK_STR_EQ(small, "");
}

TEST_CASE(a_23_character_dut_name_and_run_note_reach_the_cell_whole)
{
    settings_init();
    utc();
    g_now = NOV_24_1031;
    log_info_set_clock(fixed_clock);
    settings_set_text(SET_TEXT_DUT_NAME, "ABCDEFGHIJKLMNOPQRSTUVW");
    settings_set_text(SET_TEXT_RUN_NOTE, "after 200 cycles, 38\xc2\xb0" "C");
    CHECK_EQ(strlen(settings_text(SET_TEXT_RUN_NOTE)), 23u);
    char cell[LOG_INFO_MAX];
    run_info_servo(cell, sizeof(cell), "KST X20-8.4 HV 333H",
                   "INA3221 0x41 CH2 XY", "IMU X+ YAWS");
    log_info_t out;
    CHECK(log_info_parse(cell, &out));
    CHECK_STR_EQ(out.dut, "ABCDEFGHIJKLMNOPQRSTUVW");
    CHECK_STR_EQ(out.note, "after 200 cycles, 38\xc2\xb0" "C");
    CHECK_STR_EQ(out.date, "2026-11-24T10:31");
    CHECK_STR_EQ(out.fw, RCBENCH_VERSION_STRING);
    CHECK_STR_EQ(out.servo, "KST X20-8.4 HV 333H");
    CHECK_STR_EQ(out.meter, "INA3221 0x41 CH2 XY");
    CHECK_STR_EQ(out.angle, "IMU X+ YAWS");
    /* A note longer than 23 characters is held to 23 by the setting. */
    settings_set_text(SET_TEXT_RUN_NOTE, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    run_info_servo(cell, sizeof(cell), NULL, NULL, NULL);
    CHECK(log_info_parse(cell, &out));
    CHECK_STR_EQ(out.note, "ABCDEFGHIJKLMNOPQRSTUVW");
    CHECK_STR_EQ(out.servo, "");
    /* RUN NOTE is kept as DUT NAME is: by its key, default empty. */
    CHECK_STR_EQ(settings_text_key(SET_TEXT_RUN_NOTE), "srv_note");
    settings_set_text(SET_TEXT_RUN_NOTE, "");
    CHECK_STR_EQ(settings_text(SET_TEXT_RUN_NOTE), "");
    settings_set_text(SET_TEXT_DUT_NAME, "SERVO");
    log_info_set_clock(NULL);
}

TEST_CASE(the_clock_unset_leaves_the_date_out)
{
    utc();
    char date[17] = "x";
    log_info_date(date, sizeof(date), WALL_VALID_FLOOR_S - 1);
    CHECK_STR_EQ(date, "");
    log_info_date(date, sizeof(date), 0);
    CHECK_STR_EQ(date, "");
    log_info_date(date, sizeof(date), WALL_VALID_FLOOR_S);
    CHECK_STR_EQ(date, "2026-01-01T00:00");
    log_info_date(date, sizeof(date), NOV_24_1031);
    CHECK_STR_EQ(date, "2026-11-24T10:31");
    /* The zone in force: Berlin is an hour on in November. */
    setenv("TZ", wall_zone_posix(WALL_ZONE_DEFAULT), 1);
    tzset();
    log_info_date(date, sizeof(date), NOV_24_1031);
    CHECK_STR_EQ(date, "2026-11-24T11:31");
    utc();
    char small[16] = "x";
    log_info_date(small, sizeof(small), NOV_24_1031);
    CHECK_STR_EQ(small, "");            /* no room: no date, not half a one */

    /* No clock named: log_info_now() is 0 and the cell has no date. */
    log_info_set_clock(NULL);
    CHECK_EQ(log_info_now(), 0);
    char cell[LOG_INFO_MAX];
    run_info_bench(cell, sizeof(cell), NULL, "dry");
    CHECK_STR_EQ(cell, "note=dry&fw=" RCBENCH_VERSION_STRING);
    /* A clock that reads 1970: still no date. */
    g_now = 12345;
    log_info_set_clock(fixed_clock);
    run_info_bench(cell, sizeof(cell), "MG90S", "");
    CHECK_STR_EQ(cell, "dut=MG90S&fw=" RCBENCH_VERSION_STRING);
    g_now = NOV_24_1031;
    run_info_bench(cell, sizeof(cell), "MG90S", NULL);
    CHECK_STR_EQ(cell, "dut=MG90S&date=2026-11-24T10:31&fw="
                       RCBENCH_VERSION_STRING);
    log_info_set_clock(NULL);
}

TEST_CASE(the_parser_skips_what_it_does_not_know)
{
    log_info_t out;
    CHECK(!log_info_parse(NULL, &out));
    CHECK(!log_info_parse("", &out));
    CHECK_STR_EQ(out.dut, "");
    CHECK(!log_info_parse("noequals&color=red", &out));
    CHECK(!log_info_parse(NULL, NULL));
    /* A later key, a pair without '=', an empty pair: the rest reads. */
    CHECK(log_info_parse("temp=38&&dut=X8&bare&note=a=b&fw=", &out));
    CHECK_STR_EQ(out.dut, "X8");
    CHECK_STR_EQ(out.note, "a=b");       /* the first '=' splits */
    CHECK_STR_EQ(out.fw, "");
    /* A value longer than its field is cut to it. */
    CHECK(log_info_parse("angle=ABCDEFGHIJKLMNOP", &out));
    CHECK_STR_EQ(out.angle, "ABCDEFGHIJK");
    /* The same key twice: the last one counts. */
    CHECK(log_info_parse("dut=A&dut=B", &out));
    CHECK_STR_EQ(out.dut, "B");
}

TEST_CASE(a_file_is_read_by_its_info_column_on_the_first_data_row)
{
    log_info_t out;
    static log_source_t src;
    static log_mem_ctx_t ctx;
    /* Firmware 0.23.17's file: no column. */
    static const char old[] = "time (s);voltage (V)\n0.000;5.00\n";
    log_source_memory(&src, &ctx, old, strlen(old));
    CHECK(!log_info_read(&src, ';', &out));
    /* The column anywhere, a blank line before the first row. */
    static const char mid[] = "time (s); info ;voltage (V)\n\n"
                              "0.000;dut=X8&note=dry;5.00\n"
                              "0.050;;5.00\n";
    log_source_memory(&src, &ctx, mid, strlen(mid));
    CHECK(log_info_read(&src, ';', &out));
    CHECK_STR_EQ(out.dut, "X8");
    CHECK_STR_EQ(out.note, "dry");
    /* The first row's cell empty: nothing, even with a later row's. */
    static const char late[] = "time (s);info\n0.000;\n0.050;dut=X8\n";
    log_source_memory(&src, &ctx, late, strlen(late));
    CHECK(!log_info_read(&src, ';', &out));
    /* A header alone, a first row cut short of the column. */
    static const char head[] = "time (s);info\n";
    log_source_memory(&src, &ctx, head, strlen(head));
    CHECK(!log_info_read(&src, ';', &out));
    static const char shorter[] = "time (s);v;info\n0.000;5\n";
    log_source_memory(&src, &ctx, shorter, strlen(shorter));
    CHECK(!log_info_read(&src, ';', &out));
    CHECK(!log_info_read(NULL, ';', &out));
    CHECK(!log_info_read(&src, ';', NULL));
}

/* ------------------------------------------------- the three bench files */

/** @brief A bench sample with nothing measured. */
static bench_state_t bench_empty(void)
{
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    return b;
}

TEST_CASE(a_motor_file_carries_note_date_and_fw_within_25_columns)
{
    utc();
    g_now = NOV_24_1031;
    log_info_set_clock(fixed_clock);
    char cell[LOG_INFO_MAX];
    run_info_bench(cell, sizeof(cell), NULL, "prop 9x6 \"APC\"; dry");
    log_writer_t w = writer();
    log_writer_set_info(&w, cell);
    const bench_state_t b = bench_empty();
    for (int k = 0; k < 30; ++k) {
        CHECK(log_writer_row(&w, 0.05f * (float)k, &b));
    }
    /* SERVO_SENSE's three groups: the fullest MOTOR file, 25 columns. */
    log_info_t got;
    info_read_back(g_mem.buf, 25, true, &got);
    CHECK_STR_EQ(got.dut, "");
    CHECK_STR_EQ(got.note, "prop 9x6 _APC__ dry");
    CHECK_STR_EQ(got.date, "2026-11-24T10:31");
    CHECK_STR_EQ(got.fw, RCBENCH_VERSION_STRING);
    CHECK(strstr(g_mem.buf, ";measured;note=prop 9x6 _APC__ dry&date=") != NULL);
    CHECK(strstr(g_mem.buf, ";measured;\n") != NULL);
    log_info_set_clock(NULL);
}

TEST_CASE(a_servo_file_carries_the_dut_name_and_takes_26_columns)
{
    utc();
    g_now = NOV_24_1031;
    log_info_set_clock(fixed_clock);
    char cell[LOG_INFO_MAX];
    run_info_bench(cell, sizeof(cell), "MG90S #2", "before");
    log_writer_t w = writer();
    log_writer_set_info(&w, cell);
    const bench_state_t b = bench_empty();
    log_servo_t sv;
    log_servo_unpack(log_servo_pack(true, 1500u, true, 1.0f), false, &sv);
    for (int k = 0; k < 10; ++k) {
        CHECK(log_writer_servo_row(&w, 0.05f * (float)k, &b, NULL, &sv));
    }
    /* Three groups: 26 columns, one past what 0.23.17's LOGS takes. */
    log_info_t got;
    info_read_back(g_mem.buf, 26, true, &got);
    CHECK_STR_EQ(got.dut, "MG90S #2");
    CHECK_STR_EQ(got.note, "before");
    CHECK_STR_EQ(got.date, "2026-11-24T10:31");

    /* Two groups of the map: 23 columns, which 0.23.17 reads. */
    log_cols_t cols;
    memset(&cols, 0, sizeof(cols));
    cols.n = 2u;
    for (unsigned k = 0u; k < cols.n; ++k) {
        cols.ch[k] = (uint8_t)k;
        snprintf(cols.name[k], sizeof(cols.name[k]), "out %u", k + 1u);
    }
    w = writer();
    log_writer_set_cols(&w, &cols);
    log_writer_set_info(&w, cell);
    for (int k = 0; k < 10; ++k) {
        CHECK(log_writer_servo_row(&w, 0.05f * (float)k, &b, NULL, &sv));
    }
    info_read_back(g_mem.buf, 23, true, &got);
    CHECK_STR_EQ(got.dut, "MG90S #2");
    log_info_set_clock(NULL);
}

TEST_CASE(a_supply_file_carries_its_cell_on_the_first_row)
{
    log_info_set_clock(NULL);           /* the clock unset: no date */
    char cell[LOG_INFO_MAX];
    run_info_bench(cell, sizeof(cell), NULL, "load 2R");
    log_writer_t w = writer();
    log_writer_set_info(&w, cell);
    supply_state_t s;
    memset(&s, 0, sizeof(s));
    s.set_v = 6.0f;
    s.set_i = 2.0f;
    for (int k = 0; k < 5; ++k) {
        CHECK(log_writer_supply_row(&w, 0.05f * (float)k, &s));
    }
    log_info_t got;
    info_read_back(g_mem.buf, 11, true, &got);
    CHECK_STR_EQ(got.note, "load 2R");
    CHECK_STR_EQ(got.date, "");
    CHECK_STR_EQ(got.fw, RCBENCH_VERSION_STRING);
    static const char head[] = "time (s);set (V);voltage (V);limit (A);"
                               "current (A);power (W);mode;charge (mAh);"
                               "energy (Wh);source;info\n";
    CHECK(strncmp(g_mem.buf, head, sizeof(head) - 1u) == 0);
}

TEST_CASE(the_cell_is_taken_until_the_first_row_and_kept_one_cell)
{
    log_writer_t w = writer();
    /* No cell: the column is there, every cell empty. */
    const bench_state_t b = bench_empty();
    CHECK(log_writer_row(&w, 0.0f, &b));
    log_writer_set_info(&w, "note=late");   /* after the first row */
    CHECK(log_writer_row(&w, 0.05f, &b));
    log_info_t got;
    info_read_back(g_mem.buf, 25, false, &got);
    CHECK(strstr(g_mem.buf, "late") == NULL);

    /* A cell from elsewhere with a separator, a quote or a line feed in
     * it still makes one cell. */
    w = writer();
    log_writer_set_info(&w, "note=a;b\"c\nd");
    CHECK(log_writer_row(&w, 0.0f, &b));
    CHECK(log_writer_row(&w, 0.05f, &b));
    info_read_back(g_mem.buf, 25, true, &got);
    CHECK_STR_EQ(got.note, "a_b_c_d");
    w = writer();
    log_writer_set_info(&w, NULL);
    CHECK(log_writer_row(&w, 0.0f, &b));
    info_read_back(g_mem.buf, 25, false, &got);
    log_writer_set_info(NULL, "x");
}

/* The column ceiling: 26 by default, 25 as firmware 0.23.17 reads, and a
 * caller's 0 or a figure past the build's is the build's. */
TEST_CASE(the_column_ceiling_is_the_callers_up_to_the_builds)
{
    static char text[2048];
    for (int cols = 25; cols <= 27; ++cols) {
        size_t n = 0u;
        for (int pass = 0; pass < 3; ++pass) {
            for (int c = 0; c < cols; ++c) {
                n += (size_t)snprintf(text + n, sizeof(text) - n, "%s%s",
                                      c > 0 ? ";" : "",
                                      pass == 0 ? "c (V)" : "1.5");
            }
            n += (size_t)snprintf(text + n, sizeof(text) - n, "\n");
        }
        static log_analysis_t a;
        CHECK_EQ(info_analyse(text, 0, &a),
                 cols <= LOG_MAX_COLUMNS ? LOG_OK : LOG_ERR_TOO_MANY_COLUMNS);
        CHECK_EQ(info_analyse(text, 99, &a),
                 cols <= 26 ? LOG_OK : LOG_ERR_TOO_MANY_COLUMNS);
        CHECK_EQ(info_analyse(text, 25, &a),
                 cols <= 25 ? LOG_OK : LOG_ERR_TOO_MANY_COLUMNS);
        CHECK_EQ(info_analyse(text, 26, &a),
                 cols <= 26 ? LOG_OK : LOG_ERR_TOO_MANY_COLUMNS);
    }
    CHECK_EQ(LOG_MAX_COLUMNS, 26);
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    CHECK_EQ(opts.max_columns, LOG_MAX_COLUMNS);
}

int main(void)
{
    RUN(the_cell_carries_its_keys_in_order_and_reads_back);
    RUN(an_empty_value_leaves_its_key_out);
    RUN(amp_equals_semicolon_and_quote_are_written_as_underscores);
    RUN(the_longest_cell_fits_and_a_byte_less_takes_none);
    RUN(a_23_character_dut_name_and_run_note_reach_the_cell_whole);
    RUN(the_clock_unset_leaves_the_date_out);
    RUN(the_parser_skips_what_it_does_not_know);
    RUN(a_file_is_read_by_its_info_column_on_the_first_data_row);
    RUN(a_motor_file_carries_note_date_and_fw_within_25_columns);
    RUN(a_servo_file_carries_the_dut_name_and_takes_26_columns);
    RUN(a_supply_file_carries_its_cell_on_the_first_row);
    RUN(the_cell_is_taken_until_the_first_row_and_kept_one_cell);
    RUN(the_column_ceiling_is_the_callers_up_to_the_builds);
    return test_summary("log_info");
}
