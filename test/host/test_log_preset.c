/**
 * @file test_log_preset.c
 * @brief The presets of LOGS' plot (log_preset.h): each kind of file matched
 *        by its header alone, by its name alone and by a labelled name;
 *        the files of firmware 0.21.0 and 0.23.17; a file with a column
 *        missing; a foreign CSV that matches none; and the columns a preset
 *        picks.  The MOTOR, SERVO, SUPPL, ATEST and LIMIT headers are the
 *        writers' own, so a renamed column fails here.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "log_csv.h"
#include "log_fields.h"
#include "log_preset.h"
#include "log_writer.h"
#include "servo_limrun.h"
#include "servo_test.h"

/* A step the rest of a case cannot do without: names it and ends the case. */
#define REQUIRE(cond)                                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("expected %s", #cond);                                     \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ------------------------------------------------------------ the files */

/** @brief A file in memory: the writers' sink and the analysed text. */
typedef struct {
    char   buf[8192];  /**< the bytes */
    size_t len;        /**< their count */
} mem_t;

static mem_t g_mem;

static int mem_write(void *ctx, const void *data, size_t len)
{
    mem_t *m = (mem_t *)ctx;
    if (m->len + len >= sizeof(m->buf)) {
        return -1;
    }
    memcpy(m->buf + m->len, data, len);
    m->len += len;
    m->buf[m->len] = '\0';
    return (int)len;
}

/** @brief The text columns of the bench's files: a word in every row. */
static bool text_column(const char *name)
{
    static const char *const k_text[] = {
        "test", "phase", "mode", "source", "side", "state", "write",
        "kind", "meter", "angle source", "info",
    };
    for (size_t i = 0; i < sizeof(k_text) / sizeof(k_text[0]); ++i) {
        if (strcmp(name, k_text[i]) == 0) {
            return true;
        }
    }
    return false;
}

static log_analysis_t g_an;
static char g_csv[8192];

/**
 * @brief Analyse @p header (one line, no line feed) over 4 rows: a word in a
 *        text column, a number that grows in every other.
 */
static void analyse(const char *header)
{
    char names[LOG_MAX_COLUMNS][LOG_NAME_MAX];
    int n = 0;
    const char *p = header;
    while (*p != '\0' && n < LOG_MAX_COLUMNS) {
        const char *end = strchr(p, ';');
        size_t len = (end != NULL) ? (size_t)(end - p) : strlen(p);
        const char *unit = memchr(p, '(', len);
        size_t name_len = (unit != NULL) ? (size_t)(unit - p) : len;
        while (name_len > 0u && p[name_len - 1u] == ' ') {
            --name_len;
        }
        snprintf(names[n], sizeof(names[n]), "%.*s", (int)name_len, p);
        ++n;
        p = (end != NULL) ? end + 1 : p + len;
    }
    size_t at = (size_t)snprintf(g_csv, sizeof(g_csv), "%s\n", header);
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < n; ++c) {
            at += (size_t)snprintf(g_csv + at, sizeof(g_csv) - at, "%s%s",
                                   (c > 0) ? ";" : "",
                                   text_column(names[c]) ? "A" : "");
            if (!text_column(names[c])) {
                at += (size_t)snprintf(g_csv + at, sizeof(g_csv) - at,
                                       "%d.%d", c + r, r);
            }
        }
        at += (size_t)snprintf(g_csv + at, sizeof(g_csv) - at, "\n");
    }
    log_source_t src;
    log_mem_ctx_t ctx;
    log_source_memory(&src, &ctx, g_csv, strlen(g_csv));
    log_csv_opts_t opts;
    log_csv_opts_default(&opts);
    memset(&g_an, 0, sizeof(g_an));
    REQUIRE(log_csv_analyse(&src, &opts, &g_an) == LOG_OK);
}

/** @brief The first line of what the writer wrote, without its line feed. */
static const char *first_line(char *out, size_t n)
{
    const char *nl = strchr(g_mem.buf, '\n');
    const size_t len = (nl != NULL) ? (size_t)(nl - g_mem.buf) : g_mem.len;
    snprintf(out, n, "%.*s", (int)len, g_mem.buf);
    return out;
}

/** @brief The header of a bench file the writer writes: a MOTOR run's, or
 *         with @p servo a SERVO run's. */
static const char *bench_header(bool servo, char *out, size_t n)
{
    memset(&g_mem, 0, sizeof(g_mem));
    const log_sink_t sink = { .write = mem_write, .flush = NULL,
                              .ctx = &g_mem };
    log_writer_t w;
    log_writer_init(&w, &sink);
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    if (servo) {
        log_servo_t sv;
        log_servo_unpack(log_servo_pack(true, 1500u, true, 1.0f), false, &sv);
        CHECK(log_writer_servo_row(&w, 0.0f, &b, NULL, &sv));
    } else {
        CHECK(log_writer_row(&w, 0.0f, &b));
    }
    return first_line(out, n);
}

/** @brief The header of a SUPPLY run's file. */
static const char *supply_header(char *out, size_t n)
{
    memset(&g_mem, 0, sizeof(g_mem));
    const log_sink_t sink = { .write = mem_write, .flush = NULL,
                              .ctx = &g_mem };
    log_writer_t w;
    log_writer_init(&w, &sink);
    supply_state_t s;
    memset(&s, 0, sizeof(s));
    CHECK(log_writer_supply_row(&w, 0.0f, &s));
    return first_line(out, n);
}

/** @brief RUDDRnnn.CSV's header, as servo_rudder_auto_report.c writes it. */
static const char k_ruddr[] =
    "step;side (us);write;value;measured (deg);goal (deg);measured (mm);"
    "goal (mm);within;kind;pulse (us);current (A);info";
/** @brief RUDDRnnn.CSV of firmware 0.23.17: the first nine columns. */
static const char k_ruddr_0_23_17[] =
    "step;side (us);write;value;measured (deg);goal (deg);measured (mm);"
    "goal (mm);within";
/** @brief A bench file of firmware 0.21.0: BENCHnnn, ESCnnn or SERVOnnn in
 *         the root, no throttle and no pulse. */
static const char k_bench_0_21_0[] =
    "time (s);voltage (V);current (A);power (W);rpm (rpm);esc (C);motor (C);"
    "charge (mAh);energy (Wh);ina voltage (V);ina current (A);"
    "esc current (A);window;ch1 current (A);ch1 max (A);ch1 voltage (V);"
    "ch2 current (A);ch2 max (A);ch2 voltage (V);ch3 current (A);"
    "ch3 max (A);ch3 voltage (V);source";
/** @brief A supply run of firmware 0.21.0, written as BENCHnnn.CSV. */
static const char k_supply_0_21_0[] =
    "time (s);set (V);voltage (V);limit (A);current (A);power (W);mode;"
    "charge (mAh);energy (Wh);source";
/** @brief A servo test of firmware 0.21.0, SERVOnnn.CSV in the root. */
static const char k_atest_0_21_0[] =
    "time (s);test;step;phase;command (us);position (us);set (V);"
    "voltage (V);limit (A);current (A);power (W);mode;travel (ms);"
    "angle (deg);travel angle (ms)";
/** @brief A MOTOR file of firmware 0.23.17: no info column. */
static const char k_motor_0_23_17[] =
    "time (s);voltage (V);current (A);power (W);rpm (rpm);throttle (%);"
    "esc (C);motor (C);charge (mAh);energy (Wh);ina voltage (V);"
    "ina current (A);esc current (A);window;source";

/** @brief The name of column @p c of the analysis, or "" for none. */
static const char *col(int c)
{
    return (c >= 0 && c < g_an.n_columns) ? g_an.columns[c].name : "";
}

/* ------------------------------------------------------- by the header */

TEST_CASE(each_kind_is_matched_by_its_header_alone)
{
    char head[1024];
    analyse(SERVO_LIMRUN_CSV_HEAD);
    CHECK_EQ(log_preset_by_header(&g_an), LOG_PRESET_LIMIT);
    CHECK_EQ(log_preset_match(&g_an, "run.csv"), LOG_PRESET_LIMIT);

    analyse(k_ruddr);
    CHECK_EQ(log_preset_match(&g_an, "run.csv"), LOG_PRESET_RUDDR_SEARCH);

    snprintf(head, sizeof(head), "%s", servo_test_csv_header());
    analyse(head);
    CHECK_EQ(log_preset_match(&g_an, "run.csv"), LOG_PRESET_ATEST);
    snprintf(head, sizeof(head), "%s", servo_test_csv_header_enc());
    analyse(head);
    CHECK_EQ(log_preset_match(&g_an, "run.csv"), LOG_PRESET_ATEST);

    analyse(bench_header(true, head, sizeof(head)));
    CHECK_EQ(log_preset_match(&g_an, "run.csv"), LOG_PRESET_SERVO);
    analyse(bench_header(false, head, sizeof(head)));
    CHECK_EQ(log_preset_match(&g_an, "run.csv"), LOG_PRESET_MOTOR);
    analyse(supply_header(head, sizeof(head)));
    CHECK_EQ(log_preset_match(&g_an, "run.csv"), LOG_PRESET_SUPPL);

    /* The header wins over a name that says otherwise. */
    analyse(supply_header(head, sizeof(head)));
    CHECK_EQ(log_preset_match(&g_an, "MOTOR042.CSV"), LOG_PRESET_SUPPL);
    /* And without a name at all. */
    CHECK_EQ(log_preset_match(&g_an, NULL), LOG_PRESET_SUPPL);
}

TEST_CASE(a_header_is_matched_with_its_case_folded)
{
    analyse("Time (s);Voltage (V);Current (A);RPM (rpm);Throttle (%)");
    CHECK_EQ(log_preset_by_header(&g_an), LOG_PRESET_MOTOR);
}

/* --------------------------------------------------------- by the name */

TEST_CASE(each_kind_is_matched_by_its_name_alone)
{
    analyse("time (s);value");
    CHECK_EQ(log_preset_by_header(&g_an), LOG_PRESET_NONE);
    CHECK_EQ(log_preset_match(&g_an, "LIMIT007.CSV"), LOG_PRESET_LIMIT);
    CHECK_EQ(log_preset_match(&g_an, "RUDDR003.CSV"), LOG_PRESET_RUDDR_SEARCH);
    CHECK_EQ(log_preset_match(&g_an, "ATEST005.CSV"), LOG_PRESET_ATEST);
    CHECK_EQ(log_preset_match(&g_an, "SERVO013.CSV"), LOG_PRESET_SERVO);
    CHECK_EQ(log_preset_match(&g_an, "MOTOR014.CSV"), LOG_PRESET_MOTOR);
    CHECK_EQ(log_preset_match(&g_an, "SUPPL012.CSV"), LOG_PRESET_SUPPL);
    /* The names of firmware 0.21.0. */
    CHECK_EQ(log_preset_match(&g_an, "BENCH041.CSV"), LOG_PRESET_SERVO);
    CHECK_EQ(log_preset_match(&g_an, "ESC040.CSV"), LOG_PRESET_MOTOR);
    /* A card written back by a computer in lower case. */
    CHECK_EQ(log_preset_match(&g_an, "motor014.csv"), LOG_PRESET_MOTOR);
    /* With no analysis, the name alone. */
    CHECK_EQ(log_preset_match(NULL, "LIMIT007.CSV"), LOG_PRESET_LIMIT);
}

TEST_CASE(a_report_and_a_name_that_is_no_run_match_no_preset)
{
    CHECK_EQ(log_preset_by_name("LIMIT007.TXT"), LOG_PRESET_NONE);
    CHECK_EQ(log_preset_by_name("ATEST005.TXT"), LOG_PRESET_NONE);
    CHECK_EQ(log_preset_by_name("MOTOR14.CSV"), LOG_PRESET_NONE);
    CHECK_EQ(log_preset_by_name("MOTOR014x.CSV"), LOG_PRESET_NONE);
    CHECK_EQ(log_preset_by_name("BENCH_01.CSV"), LOG_PRESET_NONE);
    CHECK_EQ(log_preset_by_name(""), LOG_PRESET_NONE);
    CHECK_EQ(log_preset_by_name(NULL), LOG_PRESET_NONE);
    CHECK_EQ(log_preset_match(NULL, NULL), LOG_PRESET_NONE);
}

TEST_CASE(a_labelled_name_is_matched_as_its_plain_name)
{
    analyse("time (s);value");
    CHECK_EQ(log_preset_match(&g_an, "MOTOR042_kv1200.CSV"), LOG_PRESET_MOTOR);
    CHECK_EQ(log_preset_match(&g_an, "LIMIT007_greased.CSV"), LOG_PRESET_LIMIT);
    CHECK_EQ(log_preset_match(&g_an, "SUPPL012_load_2R.CSV"), LOG_PRESET_SUPPL);
    CHECK_EQ(log_preset_match(&g_an, "ruddr003_before.csv"),
             LOG_PRESET_RUDDR_SEARCH);
    /* A labelled LIMIT file with its own header: the header decides. */
    analyse(SERVO_LIMRUN_CSV_HEAD);
    CHECK_EQ(log_preset_match(&g_an, "LIMIT007_greased.CSV"), LOG_PRESET_LIMIT);
}

/* ------------------------------------------------------- older firmware */

TEST_CASE(the_files_of_firmware_0_21_0_match)
{
    /* The bench header names neither the throttle nor the pulse: the name
     * decides. */
    analyse(k_bench_0_21_0);
    CHECK_EQ(log_preset_by_header(&g_an), LOG_PRESET_NONE);
    CHECK_EQ(log_preset_match(&g_an, "ESC040.CSV"), LOG_PRESET_MOTOR);
    CHECK_EQ(log_preset_match(&g_an, "BENCH041.CSV"), LOG_PRESET_SERVO);
    CHECK_EQ(log_preset_match(&g_an, "SERVO042.CSV"), LOG_PRESET_SERVO);
    /* Copied off the card under a name of the user's: a MOTOR run. */
    CHECK_EQ(log_preset_match(&g_an, "sweep.csv"), LOG_PRESET_MOTOR);
    CHECK_EQ(log_preset_match(&g_an, NULL), LOG_PRESET_MOTOR);

    /* A supply run was a BENCHnnn.CSV: its header says SUPPL. */
    analyse(k_supply_0_21_0);
    CHECK_EQ(log_preset_match(&g_an, "BENCH043.CSV"), LOG_PRESET_SUPPL);
    /* A servo test was a SERVOnnn.CSV in the root: its header says ATEST. */
    analyse(k_atest_0_21_0);
    CHECK_EQ(log_preset_match(&g_an, "SERVO044.CSV"), LOG_PRESET_ATEST);
}

TEST_CASE(the_files_of_firmware_0_23_17_match)
{
    analyse(k_ruddr_0_23_17);
    CHECK_EQ(log_preset_match(&g_an, "RUDDR003.CSV"), LOG_PRESET_RUDDR_SEARCH);
    CHECK_EQ(log_preset_by_header(&g_an), LOG_PRESET_RUDDR_SEARCH);
    analyse(k_motor_0_23_17);
    CHECK_EQ(log_preset_by_header(&g_an), LOG_PRESET_MOTOR);
}

/* ---------------------------------------------------------- the picks */

TEST_CASE(a_time_preset_picks_its_series_in_order)
{
    char head[1024];
    int picks[LOG_MAX_SERIES];
    analyse(bench_header(true, head, sizeof(head)));
    REQUIRE(log_preset_picks(LOG_PRESET_SERVO, &g_an, picks,
                             LOG_MAX_SERIES) == 4);
    CHECK_STR_EQ(col(picks[0]), "pulse");
    CHECK_STR_EQ(col(picks[1]), "angle");
    CHECK_STR_EQ(col(picks[2]), "current");
    CHECK_STR_EQ(col(picks[3]), "voltage");

    analyse(bench_header(false, head, sizeof(head)));
    REQUIRE(log_preset_picks(LOG_PRESET_MOTOR, &g_an, picks,
                             LOG_MAX_SERIES) == 4);
    CHECK_STR_EQ(col(picks[0]), "throttle");
    CHECK_STR_EQ(col(picks[1]), "rpm");

    analyse(supply_header(head, sizeof(head)));
    REQUIRE(log_preset_picks(LOG_PRESET_SUPPL, &g_an, picks,
                             LOG_MAX_SERIES) == 4);
    CHECK_STR_EQ(col(picks[0]), "set");
    CHECK_STR_EQ(col(picks[1]), "voltage");
    CHECK_STR_EQ(col(picks[2]), "limit");
    CHECK_STR_EQ(col(picks[3]), "current");

    snprintf(head, sizeof(head), "%s", servo_test_csv_header_enc());
    analyse(head);
    REQUIRE(log_preset_picks(LOG_PRESET_ATEST, &g_an, picks,
                             LOG_MAX_SERIES) == 4);
    CHECK_STR_EQ(col(picks[0]), "command");
    CHECK_STR_EQ(col(picks[3]), "angle");
    /* Never the time axis. */
    for (int k = 0; k < 4; ++k) {
        CHECK(picks[k] != g_an.time_index);
    }
}

TEST_CASE(an_xy_preset_picks_its_x_column_first)
{
    int picks[LOG_MAX_SERIES];
    analyse(SERVO_LIMRUN_CSV_HEAD);
    REQUIRE(log_preset_picks(LOG_PRESET_LIMIT, &g_an, picks,
                             LOG_MAX_SERIES) == 3);
    CHECK_STR_EQ(col(picks[0]), "pulse");
    CHECK_STR_EQ(col(picks[1]), "current");
    CHECK_STR_EQ(col(picks[2]), "angle");

    analyse(k_ruddr);
    REQUIRE(log_preset_picks(LOG_PRESET_RUDDR_SEARCH, &g_an, picks,
                             LOG_MAX_SERIES) == 3);
    CHECK_STR_EQ(col(picks[0]), "pulse");
    CHECK_STR_EQ(col(picks[1]), "current");
    CHECK_STR_EQ(col(picks[2]), "measured");
    /* STEPS, the second picture of the same file. */
    REQUIRE(log_preset_picks(LOG_PRESET_RUDDR_STEPS, &g_an, picks,
                             LOG_MAX_SERIES) == 3);
    CHECK_STR_EQ(col(picks[0]), "step");
    CHECK_STR_EQ(col(picks[1]), "measured");
    CHECK_STR_EQ(col(picks[2]), "goal");
}

TEST_CASE(a_file_with_a_column_missing_picks_the_rest)
{
    int picks[LOG_MAX_SERIES];
    /* A SERVO run with no angle source: no angle column. */
    analyse("time (s);voltage (V);current (A);pulse (us);source");
    CHECK_EQ(log_preset_match(&g_an, "SERVO013.CSV"), LOG_PRESET_SERVO);
    REQUIRE(log_preset_picks(LOG_PRESET_SERVO, &g_an, picks,
                             LOG_MAX_SERIES) == 3);
    CHECK_STR_EQ(col(picks[0]), "pulse");
    CHECK_STR_EQ(col(picks[1]), "current");
    CHECK_STR_EQ(col(picks[2]), "voltage");

    /* A MOTOR run of 0.21.0 has no throttle; nothing takes its place. */
    analyse(k_bench_0_21_0);
    REQUIRE(log_preset_picks(LOG_PRESET_MOTOR, &g_an, picks,
                             LOG_MAX_SERIES) == 3);
    CHECK_STR_EQ(col(picks[0]), "rpm");

    /* A RUDDR file of 0.23.17: no pulse and no current. */
    analyse(k_ruddr_0_23_17);
    REQUIRE(log_preset_picks(LOG_PRESET_RUDDR_SEARCH, &g_an, picks,
                             LOG_MAX_SERIES) == 1);
    CHECK_STR_EQ(col(picks[0]), "measured");

    /* A time run without a numeric `current` and `voltage` takes an
     * INA3221 channel's (test_logview_time.c: a file whose cells of the
     * two are empty). */
    analyse("time (s);rpm (rpm);window;ch1 current (A);ch1 max (A);"
            "ch1 voltage (V)");
    REQUIRE(log_preset_picks(LOG_PRESET_SERVO, &g_an, picks,
                             LOG_MAX_SERIES) == 2);
    CHECK_STR_EQ(col(picks[0]), "ch1 current");
    CHECK_STR_EQ(col(picks[1]), "ch1 voltage");
    /* An XY preset takes no stand-in. */
    analyse("row;search;side;pulse (us);ch1 current (A);state");
    REQUIRE(log_preset_picks(LOG_PRESET_LIMIT, &g_an, picks,
                             LOG_MAX_SERIES) == 1);
    CHECK_STR_EQ(col(picks[0]), "pulse");

    /* A file named for a kind that holds none of its columns. */
    analyse("time (s);value");
    CHECK_EQ(log_preset_picks(LOG_PRESET_LIMIT, &g_an, picks,
                              LOG_MAX_SERIES), 0);
}

TEST_CASE(the_picks_stop_at_max_and_refuse_null)
{
    char head[1024];
    int picks[LOG_MAX_SERIES] = { -1, -1, -1, -1 };
    analyse(bench_header(true, head, sizeof(head)));
    CHECK_EQ(log_preset_picks(LOG_PRESET_SERVO, &g_an, picks, 2), 2);
    CHECK_EQ(picks[2], -1);
    CHECK_EQ(log_preset_picks(LOG_PRESET_SERVO, &g_an, picks, 0), 0);
    CHECK_EQ(log_preset_picks(LOG_PRESET_NONE, &g_an, picks, 4), 0);
    CHECK_EQ(log_preset_picks(LOG_PRESET_COUNT, &g_an, picks, 4), 0);
    CHECK_EQ(log_preset_picks(LOG_PRESET_SERVO, NULL, picks, 4), 0);
    CHECK_EQ(log_preset_picks(LOG_PRESET_SERVO, &g_an, NULL, 4), 0);
}

TEST_CASE(a_steps_picture_without_measured_picks_step_and_goal)
{
    int picks[LOG_MAX_SERIES];
    analyse("step;side (us);write;goal (deg);within");
    REQUIRE(log_preset_picks(LOG_PRESET_RUDDR_STEPS, &g_an, picks,
                             LOG_MAX_SERIES) == 2);
    CHECK_STR_EQ(col(picks[0]), "step");
    CHECK_STR_EQ(col(picks[1]), "goal");
}

/* ------------------------------------------------------- foreign files */

TEST_CASE(a_foreign_csv_matches_no_preset)
{
    analyse("time;voltage;current;rpm;temp");
    CHECK_EQ(log_preset_match(&g_an, "en.csv"), LOG_PRESET_NONE);
    analyse("timestamp (ms);altitude (m);speed (km/h);power (W)");
    CHECK_EQ(log_preset_match(&g_an, "units-header.csv"), LOG_PRESET_NONE);
    /* Three of the four columns of a 0.21.0 bench file are not one. */
    analyse("time (s);rpm (rpm);esc (C);motor (C)");
    CHECK_EQ(log_preset_match(&g_an, "flight.csv"), LOG_PRESET_NONE);
    /* Two of a signature's three columns are not it either. */
    analyse("time (s);test;phase;value");
    CHECK_EQ(log_preset_match(&g_an, "flight.csv"), LOG_PRESET_NONE);
    CHECK_EQ(log_preset_by_header(NULL), LOG_PRESET_NONE);
}

TEST_CASE(every_preset_has_a_picture_and_none_has_none)
{
    CHECK(log_preset_get(LOG_PRESET_NONE) == NULL);
    CHECK(log_preset_get(LOG_PRESET_COUNT) == NULL);
    CHECK(log_preset_get((log_preset_id_t)-1) == NULL);
    int xy = 0;
    for (int id = LOG_PRESET_LIMIT; id < LOG_PRESET_COUNT; ++id) {
        const log_preset_t *p = log_preset_get((log_preset_id_t)id);
        REQUIRE(p != NULL);
        CHECK_EQ(p->id, (log_preset_id_t)id);
        CHECK(p->x != NULL);
        CHECK(p->series[0] != NULL);
        xy += (p->plot == LOG_PLOT_XY) ? 1 : 0;
    }
    /* LIMIT and RUDDR's two pictures place their rows over the pulse or
     * the step; the four run files over time. */
    CHECK_EQ(xy, 3);
    CHECK_EQ(LOG_PRESET_COUNT - 1, 7);
}

TEST_CASE(log_fields_find_folds_case_and_skips_what_it_must)
{
    analyse("Time (s);Pulse (us);side");
    CHECK_EQ(log_fields_find(&g_an, "pulse", true), 1);
    CHECK_EQ(log_fields_find(&g_an, "PULSE", false), 1);
    /* The time axis and a text column are found only when asked for any. */
    CHECK_EQ(log_fields_find(&g_an, "time", true), -1);
    CHECK_EQ(log_fields_find(&g_an, "time", false), 0);
    CHECK_EQ(log_fields_find(&g_an, "side", true), -1);
    CHECK_EQ(log_fields_find(&g_an, "side", false), 2);
    CHECK_EQ(log_fields_find(&g_an, "puls", false), -1);
    CHECK_EQ(log_fields_find(&g_an, "pulse (us)", false), -1);
    CHECK_EQ(log_fields_find(NULL, "pulse", false), -1);
    CHECK_EQ(log_fields_find(&g_an, NULL, false), -1);
}

int main(void)
{
    RUN(each_kind_is_matched_by_its_header_alone);
    RUN(a_header_is_matched_with_its_case_folded);
    RUN(each_kind_is_matched_by_its_name_alone);
    RUN(a_report_and_a_name_that_is_no_run_match_no_preset);
    RUN(a_labelled_name_is_matched_as_its_plain_name);
    RUN(the_files_of_firmware_0_21_0_match);
    RUN(the_files_of_firmware_0_23_17_match);
    RUN(a_time_preset_picks_its_series_in_order);
    RUN(an_xy_preset_picks_its_x_column_first);
    RUN(a_file_with_a_column_missing_picks_the_rest);
    RUN(the_picks_stop_at_max_and_refuse_null);
    RUN(a_steps_picture_without_measured_picks_step_and_goal);
    RUN(a_foreign_csv_matches_no_preset);
    RUN(every_preset_has_a_picture_and_none_has_none);
    RUN(log_fields_find_folds_case_and_skips_what_it_must);
    return test_summary("log_preset");
}
