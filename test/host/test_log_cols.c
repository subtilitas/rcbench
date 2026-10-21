/*
 * The bench log's INA3221 columns with SENSE_MAP in force (log_cols.h): a
 * group per watched target, named after it, FAST's first, at most three;
 * written from bench_state_t.chw on the row of its window, and read back
 * by the reader, which groups them as INA3221.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "log_cols.h"
#include "log_csv.h"
#include "log_fields.h"
#include "log_writer.h"

/* A map: parts @p parts, every shunt 100.0 mOhm, FAST none. */
static void map_of(uint16_t *m, uint16_t parts)
{
    memset(m, 0, LINK_SM_KEPT * sizeof(uint16_t));
    m[LINK_SM_ENABLE] = parts;
    m[LINK_SM_FAST] = LINK_SM_FAST_NONE;
    for (unsigned c = 0u; c < LINK_SM_CHANNELS; ++c) {
        m[LINK_SM_SHUNT(c)] = 1000u;
    }
}

TEST_CASE(a_group_per_watched_target_fast_first)
{
    uint16_t m[LINK_SM_KEPT];
    map_of(m, 0x03u);
    m[LINK_SM_WATCH(0)] = LINK_SM_WATCH_OUT(2);
    m[LINK_SM_WATCH(4)] = LINK_SM_WATCH_PORT(0);
    m[LINK_SM_WATCH(2)] = LINK_SM_WATCH_SUPPLY;
    m[LINK_SM_FAST] = 4u;
    log_cols_t c;
    log_cols_from_map(m, &c);
    CHECK_EQ(c.n, 3u);
    CHECK_EQ(c.ch[0], 4u);
    CHECK_STR_EQ(c.name[0], "port 1");
    CHECK_EQ(c.ch[1], 0u);
    CHECK_STR_EQ(c.name[1], "out 3");
    CHECK_EQ(c.ch[2], 2u);
    CHECK_STR_EQ(c.name[2], "supply");
    /* Without FAST: channel order. */
    m[LINK_SM_FAST] = LINK_SM_FAST_NONE;
    log_cols_from_map(m, &c);
    CHECK_EQ(c.ch[0], 0u);
    CHECK_EQ(c.ch[1], 2u);
    CHECK_EQ(c.ch[2], 4u);
    /* A NULL map, and a map that watches nothing: no group. */
    log_cols_from_map(NULL, &c);
    CHECK_EQ(c.n, 0u);
    map_of(m, 0x0Fu);
    log_cols_from_map(m, &c);
    CHECK_EQ(c.n, 0u);
    log_cols_from_map(m, NULL);
}

/* A channel off, on a part not enabled, or watching NONE takes no group;
 * the fourth watching channel and later are not logged. */
TEST_CASE(only_a_channel_that_reads_a_target_takes_a_group)
{
    uint16_t m[LINK_SM_KEPT];
    map_of(m, 0x01u);
    m[LINK_SM_WATCH(0)] = LINK_SM_WATCH_OUT(0);
    m[LINK_SM_SHUNT(0)] = 0u;                     /* off */
    m[LINK_SM_WATCH(3)] = LINK_SM_WATCH_OUT(1);   /* part 1 not enabled */
    m[LINK_SM_WATCH(1)] = 0x99u;                  /* no such target */
    log_cols_t c;
    log_cols_from_map(m, &c);
    CHECK_EQ(c.n, 0u);
    map_of(m, 0x0Fu);
    for (unsigned k = 0u; k < 8u; ++k) {
        m[LINK_SM_WATCH(k)] = (uint16_t)LINK_SM_WATCH_OUT(k);
    }
    m[LINK_SM_WATCH(11)] = LINK_SM_WATCH_PORT(3);
    log_cols_from_map(m, &c);
    CHECK_EQ(c.n, LOG_COLS_GROUPS);
    CHECK_STR_EQ(c.name[2], "out 3");
    m[LINK_SM_FAST] = 11u;
    log_cols_from_map(m, &c);
    CHECK_STR_EQ(c.name[0], "port 4");
    CHECK_STR_EQ(c.name[1], "out 1");
    CHECK_STR_EQ(c.name[2], "out 2");
}

/* Two channels on one target each name their channel. */
TEST_CASE(two_channels_on_one_target_name_their_channels)
{
    uint16_t m[LINK_SM_KEPT];
    map_of(m, 0x0Au);
    m[LINK_SM_WATCH(4)] = LINK_SM_WATCH_OUT(2);
    m[LINK_SM_WATCH(11)] = LINK_SM_WATCH_OUT(2);
    m[LINK_SM_WATCH(10)] = LINK_SM_WATCH_SUPPLY;
    log_cols_t c;
    log_cols_from_map(m, &c);
    CHECK_EQ(c.n, 3u);
    CHECK_STR_EQ(c.name[0], "out 3 0x41 ch2");
    CHECK_STR_EQ(c.name[1], "supply");
    CHECK_STR_EQ(c.name[2], "out 3 0x43 ch3");
}

/* ------------------------------------------------------- the writer */

typedef struct {
    char   buf[8 * 1024];
    size_t len;
} sink_t;

static int sink_write(void *ctx, const void *data, size_t len)
{
    sink_t *s = (sink_t *)ctx;
    if (s->len + len > sizeof(s->buf) - 1u) {
        return -1;
    }
    memcpy(s->buf + s->len, data, len);
    s->len += len;
    s->buf[s->len] = '\0';
    return (int)len;
}

static sink_t g_sink;

static log_writer_t writer(void)
{
    memset(&g_sink, 0, sizeof(g_sink));
    const log_sink_t sk = { .write = sink_write, .flush = NULL,
                            .ctx = &g_sink };
    log_writer_t w;
    log_writer_init(&w, &sk);
    return w;
}

/* A row carrying window @p window, its chw from window @p chw_window. */
static bench_state_t row(uint16_t window, uint16_t chw_window)
{
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    b.servo_new = true;
    b.servo_window = window;
    b.chw.window = chw_window;
    b.chw.ok = (uint16_t)((1u << 4) | (1u << 0));
    b.chw.no_voltage = (uint16_t)(1u << 0);
    b.chw.mean_ma[4] = 1234;
    b.chw.max_ma[4] = -50;
    b.chw.min_mv[4] = 5012u;
    b.chw.mean_ma[0] = 20;
    b.chw.max_ma[0] = 40;
    return b;
}

TEST_CASE(the_header_and_rows_follow_the_groups)
{
    uint16_t m[LINK_SM_KEPT];
    map_of(m, 0x03u);
    m[LINK_SM_WATCH(4)] = LINK_SM_WATCH_PORT(0);
    m[LINK_SM_WATCH(0)] = LINK_SM_WATCH_OUT(0);
    m[LINK_SM_FAST] = 4u;
    log_cols_t c;
    log_cols_from_map(m, &c);
    log_writer_t w = writer();
    log_writer_set_cols(&w, &c);
    bench_state_t b = row(77u, 77u);
    CHECK(log_writer_row(&w, 0.0f, &b));
    b = row(78u, 77u);                  /* a chw of another window */
    CHECK(log_writer_row(&w, 0.05f, &b));
    CHECK(strstr(g_sink.buf,
                 "window;port 1 current (A);port 1 max (A);"
                 "port 1 voltage (V);out 1 current (A);out 1 max (A);"
                 "out 1 voltage (V);source\n") != NULL);
    CHECK(strstr(g_sink.buf, ";77;1.234;-0.050;5.012;0.020;0.040;;measured\n")
          != NULL);
    CHECK(strstr(g_sink.buf, ";78;;;;;;;measured\n") != NULL);
    /* After the header the columns hold: a late set is ignored. */
    log_writer_set_cols(&w, NULL);
    CHECK(w.by_map);
    log_writer_set_cols(NULL, &c);
}

TEST_CASE(without_a_map_the_file_keeps_ch1_to_ch3)
{
    log_writer_t w = writer();
    log_writer_set_cols(&w, NULL);
    bench_state_t b = row(5u, 5u);
    b.servo_ok = 0x01u;
    b.servo_mean_ma[0] = 500;
    b.servo_max_ma[0] = 600;
    b.servo_min_mv[0] = 4800u;
    CHECK(log_writer_row(&w, 0.0f, &b));
    CHECK(strstr(g_sink.buf, "window;ch1 current (A);") != NULL);
    CHECK(strstr(g_sink.buf, ";5;0.500;0.600;4.800;;;;;;;measured\n")
          != NULL);
}

/* Read back: the columns open in the reader and group as INA3221. */
TEST_CASE(the_reader_takes_the_groups_as_ina3221)
{
    uint16_t m[LINK_SM_KEPT];
    map_of(m, 0x0Fu);
    m[LINK_SM_WATCH(4)] = LINK_SM_WATCH_PORT(0);
    m[LINK_SM_WATCH(5)] = LINK_SM_WATCH_OUT(7);
    m[LINK_SM_WATCH(10)] = LINK_SM_WATCH_SUPPLY;
    log_cols_t c;
    log_cols_from_map(m, &c);
    log_writer_t w = writer();
    log_writer_set_cols(&w, &c);
    for (unsigned k = 0u; k < 4u; ++k) {
        bench_state_t b = row((uint16_t)(10u + k), (uint16_t)(10u + k));
        CHECK(log_writer_row(&w, 0.05f * (float)k, &b));
    }
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, g_sink.buf, g_sink.len);
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, &opts, &a), LOG_OK);
    CHECK_EQ(a.n_columns, 23);
    CHECK_STR_EQ(a.columns[13].name, "port 1 current");
    CHECK_STR_EQ(a.columns[19].name, "supply current");
    CHECK_STR_EQ(log_field_meta("port 1 current").group, "INA3221");
    CHECK_STR_EQ(log_field_meta("out 8 max").group, "INA3221");
    CHECK_STR_EQ(log_field_meta("supply voltage").group, "INA3221");
    CHECK_STR_EQ(log_field_meta("outside").group, "Other");
}

/* A sink that takes @c g_room bytes and refuses the rest. */
static size_t g_room;
static int short_write(void *ctx, const void *data, size_t len)
{
    const sink_t *s = (const sink_t *)ctx;
    if (s->len + len > g_room) {
        return -1;
    }
    return sink_write(ctx, data, len);
}

/* A sink that fails inside the groups' header fails the writer once; more
 * than LOG_COLS_MAX groups asked are held to it. */
TEST_CASE(a_header_cut_in_the_groups_fails_the_writer)
{
    uint16_t m[LINK_SM_KEPT];
    map_of(m, 0x01u);
    m[LINK_SM_WATCH(0)] = LINK_SM_WATCH_OUT(0);
    log_cols_t c;
    log_cols_from_map(m, &c);
    memset(&g_sink, 0, sizeof(g_sink));
    g_room = 151u + 10u;            /* past "window;", inside the group */
    const log_sink_t sk = { .write = short_write, .flush = NULL,
                            .ctx = &g_sink };
    log_writer_t w;
    log_writer_init(&w, &sk);
    log_writer_set_cols(&w, &c);
    CHECK(!log_writer_header(&w));
    CHECK(w.failed);
    CHECK(strstr(g_sink.buf, "window;") != NULL);
    CHECK(strstr(g_sink.buf, "source") == NULL);
    /* Too many groups asked: held to LOG_COLS_MAX. */
    log_cols_t many;
    memset(&many, 0, sizeof(many));
    many.n = LOG_COLS_MAX + 3u;
    log_writer_t w2 = writer();
    log_writer_set_cols(&w2, &many);
    CHECK_EQ(w2.cols.n, LOG_COLS_MAX);
}

/* The longest name, a target two channels watch, fits the reader's 28
 * bytes with its unit: "supply 0x43 ch3 voltage (V)" is 27. */
TEST_CASE(the_longest_column_name_reads_back_whole)
{
    uint16_t m[LINK_SM_KEPT];
    map_of(m, 0x0Fu);
    m[LINK_SM_WATCH(11)] = LINK_SM_WATCH_SUPPLY;
    m[LINK_SM_WATCH(10)] = LINK_SM_WATCH_SUPPLY;
    m[LINK_SM_FAST] = 11u;
    log_cols_t c;
    log_cols_from_map(m, &c);
    CHECK_EQ(c.n, 2u);
    CHECK_STR_EQ(c.name[0], "supply 0x43 ch3");
    log_writer_t w = writer();
    log_writer_set_cols(&w, &c);
    for (unsigned k = 0u; k < 4u; ++k) {
        bench_state_t b = row((uint16_t)(10u + k), (uint16_t)(10u + k));
        b.chw.ok = (uint16_t)(3u << 10);
        b.chw.min_mv[11] = 12000u;
        CHECK(log_writer_row(&w, 0.05f * (float)k, &b));
    }
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, g_sink.buf, g_sink.len);
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    log_analysis_t a;
    CHECK_EQ(log_csv_analyse(&src, &opts, &a), LOG_OK);
    CHECK_EQ(a.n_columns, 20);
    CHECK_STR_EQ(a.columns[15].name, "supply 0x43 ch3 voltage");
    CHECK_STR_EQ(a.columns[15].unit, "V");
}

int main(void)
{
    RUN(a_group_per_watched_target_fast_first);
    RUN(only_a_channel_that_reads_a_target_takes_a_group);
    RUN(two_channels_on_one_target_name_their_channels);
    RUN(the_header_and_rows_follow_the_groups);
    RUN(without_a_map_the_file_keeps_ch1_to_ch3);
    RUN(the_reader_takes_the_groups_as_ina3221);
    RUN(the_longest_column_name_reads_back_whole);
    RUN(a_header_cut_in_the_groups_fails_the_writer);
    return test_summary("log_cols");
}
