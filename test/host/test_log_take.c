/*
 * The logger's numbering in a folder, log_take(), over a card held in
 * memory: the folders made one level at a time, one walk per folder until
 * a take fails, the 10 probes, the 3 refused creates, and the open marks of
 * one folder that do not hold back a file in another.
 *
 * panel_log.c hands log_take() the card through 5 functions; the fake
 * below answers them as FatFs does: names compare without case, mkdir of a
 * name a file holds fails, a create on a folder's name fails.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "card_tree.h"
#include "log_name.h"
#include "log_take.h"

/* ------------------------------------------------------------ the card */

#define FAKE_MAX  1200
#define PATH_MAX_ 48

typedef struct {
    char path[PATH_MAX_];   /* from the root, "BENCH/LOGS/MOTOR001.CSV" */
    bool dir;
} fake_entry_t;

static struct {
    fake_entry_t e[FAKE_MAX];
    int n;
    bool unmounted;
    bool no_new_dir;        /* write-protected or full: mkdir refused */
    bool walk_fails;
    int  creates_refused;   /* the next this many creates fail        */
    unsigned mkdirs, walks, probes, creates;
    char created[PATH_MAX_];
    int  created_number;
} g;

static void card_clear(void)
{
    memset(&g, 0, sizeof(g));
}

static int find(const char *path)
{
    for (int i = 0; i < g.n; ++i) {
        if (log_name_case_cmp(g.e[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

static void put(const char *path, bool dir)
{
    if (find(path) >= 0 || g.n >= FAKE_MAX) {
        return;
    }
    snprintf(g.e[g.n].path, sizeof(g.e[0].path), "%s", path);
    g.e[g.n].dir = dir;
    ++g.n;
}

static void put_file(const char *dir, const char *name)
{
    char p[PATH_MAX_];
    snprintf(p, sizeof(p), "%s%s%s", dir, (dir[0] != '\0') ? "/" : "", name);
    put(p, false);
}

static void drop(const char *path)
{
    const int i = find(path);
    if (i >= 0) {
        g.e[i] = g.e[g.n - 1];
        --g.n;
    }
}

static bool has(const char *path)
{
    return find(path) >= 0;
}

static bool f_mounted(void *ctx)
{
    (void)ctx;
    return !g.unmounted;
}

static bool f_make_dir(void *ctx, const char *dir)
{
    (void)ctx;
    ++g.mkdirs;
    const int i = find(dir);
    if (i >= 0) {
        return g.e[i].dir;          /* a folder there, or a file's name */
    }
    if (g.no_new_dir) {
        return false;
    }
    /* Its parent has to be a folder, as FatFs's f_mkdir needs. */
    const char *slash = strrchr(dir, '/');
    if (slash != NULL) {
        char parent[PATH_MAX_];
        snprintf(parent, sizeof(parent), "%.*s", (int)(slash - dir), dir);
        const int p = find(parent);
        if (p < 0 || !g.e[p].dir) {
            return false;
        }
    }
    put(dir, true);
    return true;
}

static bool in_dir(const char *path, const char *dir, const char **name)
{
    const size_t n = strlen(dir);
    if (n == 0u) {
        if (strchr(path, '/') != NULL) {
            return false;
        }
        *name = path;
        return true;
    }
    if (strlen(path) <= n + 1u || path[n] != '/') {
        return false;
    }
    char head[PATH_MAX_];
    snprintf(head, sizeof(head), "%.*s", (int)n, path);
    if (log_name_case_cmp(head, dir) != 0
        || strchr(path + n + 1u, '/') != NULL) {
        return false;
    }
    *name = path + n + 1u;
    return true;
}

static bool f_walk(void *ctx, const char *dir, log_take_visit_fn visit,
                   void *visit_ctx)
{
    (void)ctx;
    ++g.walks;
    if (g.walk_fails) {
        return false;
    }
    for (int i = 0; i < g.n; ++i) {
        const char *name = NULL;
        if (in_dir(g.e[i].path, dir, &name)) {
            visit(name, visit_ctx);
        }
    }
    return true;
}

static bool f_exists(void *ctx, const char *dir, const char *name)
{
    (void)ctx;
    ++g.probes;
    char p[PATH_MAX_];
    snprintf(p, sizeof(p), "%s%s%s", dir, (dir[0] != '\0') ? "/" : "", name);
    return has(p);
}

static bool f_create(void *ctx, const char *dir, const char *name,
                     int number)
{
    (void)ctx;
    ++g.creates;
    if (g.creates_refused > 0) {
        --g.creates_refused;
        return false;
    }
    char p[PATH_MAX_];
    snprintf(p, sizeof(p), "%s%s%s", dir, (dir[0] != '\0') ? "/" : "", name);
    const int i = find(p);
    if (i >= 0 && g.e[i].dir) {
        return false;
    }
    put(p, false);
    snprintf(g.created, sizeof(g.created), "%s", p);
    g.created_number = number;
    return true;
}

static const log_take_io_t k_io = {
    .ctx      = NULL,
    .mounted  = f_mounted,
    .make_dir = f_make_dir,
    .walk     = f_walk,
    .exists   = f_exists,
    .create   = f_create,
};

static log_series_set_t g_set;

static void fresh(void)
{
    card_clear();
    log_series_init(&g_set);
}

/* A take; the number it gave, -1 for none. */
static int take(const char *dir, log_prefix_t prefix, bool report,
                log_take_t *result)
{
    int n = 7777;
    const log_take_t r = log_take(&g_set, &k_io, dir, prefix, report, &n);
    if (result != NULL) {
        *result = r;
    }
    if (r != LOG_TAKE_OK && n != -1) {
        T_FAIL("a failed take (%d) left the number at %d", (int)r, n);
    }
    return n;
}

/* ------------------------------------------------------------ tests */

TEST_CASE(the_first_run_makes_each_level_and_takes_001)
{
    fresh();
    log_take_t r;
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_MOTOR, false, &r), 1);
    CHECK_EQ(r, LOG_TAKE_OK);
    CHECK(has("BENCH"));
    CHECK(has("BENCH/LOGS"));
    CHECK_STR_EQ(g.created, "BENCH/LOGS/MOTOR001.CSV");
    CHECK_EQ(g.created_number, 1);
    CHECK_EQ(g.mkdirs, 2u);
    CHECK_EQ(g.walks, 1u);
    CHECK_EQ(g.probes, (unsigned)LOG_NAME_FORMS);   /* 10 probes */
    CHECK_EQ(g.creates, 1u);

    /* The test's folder beside it, its own levels and its own 001. */
    CHECK_EQ(take(CARD_USER_LOGS, LOG_PREFIX_ATEST, false, &r), 1);
    CHECK_STR_EQ(g.created, "USER/LOGS/ATEST001.CSV");
    CHECK(has("USER"));
    CHECK(has("USER/LOGS"));
    CHECK_EQ(take(CARD_USER_LOGS, LOG_PREFIX_LIMIT, true, &r), 2);
    CHECK_STR_EQ(g.created, "USER/LOGS/LIMIT002.TXT");

    /* An existing folder is not an error, and the root needs none. */
    const unsigned before = g.mkdirs;
    CHECK_EQ(take(CARD_ROOT, LOG_PREFIX_SUPPL, false, &r), 1);
    CHECK_EQ(r, LOG_TAKE_OK);
    CHECK_STR_EQ(g.created, "SUPPL001.CSV");
    CHECK_EQ(g.mkdirs, before);
}

TEST_CASE(the_number_is_above_the_highest_and_a_gap_is_not_filled)
{
    fresh();
    put("BENCH", true);
    put("BENCH/LOGS", true);
    put_file(CARD_AUTO_LOGS, "SUPPL001.CSV");
    put_file(CARD_AUTO_LOGS, "MOTOR005.CSV");    /* 2 to 4 deleted */
    put_file(CARD_AUTO_LOGS, "bench003.txt");    /* 0.21's name, copied */
    put_file(CARD_AUTO_LOGS, "SWEEP_920KV.CSV"); /* takes no number */
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_SERVO, false, NULL), 6);
    CHECK_STR_EQ(g.created, "BENCH/LOGS/SERVO006.CSV");
    /* Numbers in another folder are another series. */
    put("USER", true);
    put("USER/LOGS", true);
    put_file(CARD_USER_LOGS, "ATEST040.CSV");
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_SERVO, false, NULL), 7);
    /* A folder whose name is a run's takes that number. */
    put("BENCH/LOGS/MOTOR008.CSV", true);
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_MOTOR, false, NULL), 9);
}

TEST_CASE(numbers_998_and_999_are_taken_and_then_the_folder_is_full)
{
    fresh();
    put("USER", true);
    put("USER/LOGS", true);
    put_file(CARD_USER_LOGS, "ATEST997.CSV");
    log_take_t r;
    CHECK_EQ(take(CARD_USER_LOGS, LOG_PREFIX_ATEST, false, &r), 998);
    CHECK_EQ(take(CARD_USER_LOGS, LOG_PREFIX_LIMIT, true, &r), 999);
    CHECK_STR_EQ(g.created, "USER/LOGS/LIMIT999.TXT");
    const int files = g.n;
    const unsigned creates = g.creates;
    CHECK_EQ(take(CARD_USER_LOGS, LOG_PREFIX_ATEST, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_FULL);
    CHECK_EQ(g.n, files);                 /* no file */
    CHECK_EQ(g.creates, creates);         /* and none tried */

    /* A folder whose only run is 999 is full too, the rest free. */
    fresh();
    put_file(CARD_ROOT, "MOTOR999.TXT");
    CHECK_EQ(take(CARD_ROOT, LOG_PREFIX_SUPPL, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_FULL);
}

TEST_CASE(each_folder_is_walked_once_until_a_take_fails)
{
    fresh();
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_MOTOR, false, NULL), 1);
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_SUPPL, false, NULL), 2);
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_SERVO, false, NULL), 3);
    CHECK_EQ(g.walks, 1u);
    CHECK_EQ(take(CARD_USER_LOGS, LOG_PREFIX_ATEST, false, NULL), 1);
    CHECK_EQ(g.walks, 2u);
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_MOTOR, false, NULL), 4);
    CHECK_EQ(take(CARD_USER_LOGS, LOG_PREFIX_LIMIT, true, NULL), 2);
    CHECK_EQ(g.walks, 2u);
    /* A file written since the walk, under any written name, is stepped
     * over and not overwritten. */
    put_file(CARD_AUTO_LOGS, "ATEST005.TXT");
    put_file(CARD_AUTO_LOGS, "suppl006.csv");
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_MOTOR, false, NULL), 7);
    CHECK_EQ(g.walks, 2u);
    /* The folder named in another case is the same folder. */
    CHECK_EQ(take("bench/logs", LOG_PREFIX_MOTOR, false, NULL), 8);
    CHECK_EQ(g.walks, 2u);
}

TEST_CASE(both_kinds_in_one_folder_are_one_series)
{
    fresh();
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_MOTOR, false, NULL), 1);
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_ATEST, false, NULL), 2);
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_LIMIT, true, NULL), 3);
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_SUPPL, false, NULL), 4);
    CHECK(has("BENCH/LOGS/MOTOR001.CSV"));
    CHECK(has("BENCH/LOGS/ATEST002.CSV"));
    CHECK(has("BENCH/LOGS/LIMIT003.TXT"));
    CHECK(has("BENCH/LOGS/SUPPL004.CSV"));
    CHECK_EQ(g.walks, 1u);
}

TEST_CASE(a_third_folder_gives_up_one_series_and_walks_it_again)
{
    fresh();
    CHECK_EQ(take("A", LOG_PREFIX_MOTOR, false, NULL), 1);
    CHECK_EQ(take("B", LOG_PREFIX_MOTOR, false, NULL), 1);
    CHECK_EQ(g.walks, 2u);
    CHECK_EQ(take("C", LOG_PREFIX_MOTOR, false, NULL), 1);
    CHECK_EQ(g.walks, 3u);
    /* A was given up: walked again, and its number is still above its
     * files. */
    CHECK_EQ(take("A", LOG_PREFIX_MOTOR, false, NULL), 2);
    CHECK_EQ(g.walks, 4u);
    CHECK(log_series_of(&g_set, NULL) == NULL);
    CHECK(log_series_of(NULL, "A") == NULL);
    CHECK(log_series_of(&g_set, "ABCDEFGH/ABCDEFGH/ABCDEF") == NULL);
}

TEST_CASE(a_file_named_bench_stops_the_folder_and_takes_no_number)
{
    fresh();
    put_file(CARD_ROOT, "BENCH");       /* a file, no extension */
    log_take_t r;
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_MOTOR, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_NO_FOLDER);
    CHECK_EQ(g.walks, 0u);
    CHECK_EQ(g.creates, 0u);
    CHECK(!has("BENCH/LOGS"));
    /* A file at the second level, likewise. */
    fresh();
    put("BENCH", true);
    put_file("BENCH", "LOGS");
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_MOTOR, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_NO_FOLDER);
    /* The file gone, the next run is recorded. */
    drop("BENCH/LOGS");
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_MOTOR, false, &r), 1);
    CHECK_EQ(r, LOG_TAKE_OK);
}

TEST_CASE(a_card_that_takes_no_folder_records_no_run)
{
    fresh();
    g.no_new_dir = true;                /* ENOSPC or write-protected */
    log_take_t r;
    CHECK_EQ(take(CARD_USER_LOGS, LOG_PREFIX_ATEST, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_NO_FOLDER);
    CHECK_EQ(g.creates, 0u);
    /* The folders there already, the card still takes files. */
    g.no_new_dir = false;
    put("USER", true);
    put("USER/LOGS", true);
    g.no_new_dir = true;
    CHECK_EQ(take(CARD_USER_LOGS, LOG_PREFIX_ATEST, false, &r), 1);
    CHECK_EQ(r, LOG_TAKE_OK);
}

TEST_CASE(three_refused_creates_call_the_folder_full_and_a_delete_frees_it)
{
    fresh();
    put("BENCH", true);
    put("BENCH/LOGS", true);
    put_file(CARD_AUTO_LOGS, "MOTOR010.CSV");
    g.creates_refused = 1000;
    log_take_t r;
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_MOTOR, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_FULL);
    CHECK_EQ(g.creates, LOG_TAKE_TRIES);   /* 11, 12, 13 and no further */
    CHECK_EQ(g.walks, 1u);
    /* Two refused and the third taken: the number after the refused. */
    g.creates_refused = 2;
    g.creates = 0u;
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_MOTOR, false, &r), 13);
    CHECK_EQ(g.walks, 2u);                 /* walked again after the fail */
    CHECK_EQ(g.creates, 3u);

    /* Full at 999; deleted on the panel; the next run walks again and is
     * recorded above what is left. */
    fresh();
    put("BENCH", true);
    put("BENCH/LOGS", true);
    put_file(CARD_AUTO_LOGS, "SUPPL998.CSV");
    put_file(CARD_AUTO_LOGS, "SUPPL999.CSV");
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_SUPPL, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_FULL);
    drop("BENCH/LOGS/SUPPL999.CSV");
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_SUPPL, false, &r), 999);
    CHECK_EQ(g.walks, 2u);
}

TEST_CASE(a_folder_that_does_not_read_records_no_run_and_is_read_again)
{
    fresh();
    put_file(CARD_ROOT, "MOTOR004.CSV");
    g.walk_fails = true;
    log_take_t r;
    CHECK_EQ(take(CARD_ROOT, LOG_PREFIX_MOTOR, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_UNREADABLE);
    CHECK_EQ(g.creates, 0u);           /* not numbered from 1 into a gap */
    g.walk_fails = false;
    CHECK_EQ(take(CARD_ROOT, LOG_PREFIX_MOTOR, false, &r), 5);
    CHECK_EQ(g.walks, 2u);
}

TEST_CASE(no_card_and_wrong_arguments_take_nothing)
{
    fresh();
    g.unmounted = true;
    log_take_t r;
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_MOTOR, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_NO_CARD);
    CHECK_EQ(g.mkdirs, 0u);
    g.unmounted = false;
    /* BENCH and ESC are read and never written. */
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_BENCH, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_ARG);
    CHECK_EQ(take(CARD_AUTO_LOGS, LOG_PREFIX_ESC, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_ARG);
    CHECK_EQ(take(CARD_AUTO_LOGS, (log_prefix_t)-1, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_ARG);
    /* 24 characters do not fit CARD_DIR_MAX; 23 do. */
    CHECK_EQ(take("ABCDEFGH/ABCDEFGH/ABCDEF", LOG_PREFIX_MOTOR, false, &r),
             -1);
    CHECK_EQ(r, LOG_TAKE_ARG);
    CHECK_EQ(take("ABCDEFGH/ABCDEFGH/ABCDE", LOG_PREFIX_MOTOR, false, &r), 1);
    CHECK_EQ(r, LOG_TAKE_OK);
    CHECK(has("ABCDEFGH/ABCDEFGH/ABCDE/MOTOR001.CSV"));
    int n = 5;
    CHECK_EQ(log_take(NULL, &k_io, CARD_ROOT, LOG_PREFIX_MOTOR, false, &n),
             LOG_TAKE_ARG);
    CHECK_EQ(n, -1);
    CHECK_EQ(log_take(&g_set, NULL, CARD_ROOT, LOG_PREFIX_MOTOR, false, &n),
             LOG_TAKE_ARG);
    CHECK_EQ(log_take(&g_set, &k_io, NULL, LOG_PREFIX_MOTOR, false, &n),
             LOG_TAKE_ARG);
    CHECK_EQ(log_take(&g_set, &k_io, CARD_ROOT, LOG_PREFIX_MOTOR, false,
                      NULL), LOG_TAKE_ARG);
    log_take_io_t half = k_io;
    half.exists = NULL;
    CHECK_EQ(log_take(&g_set, &half, CARD_ROOT, LOG_PREFIX_MOTOR, false, &n),
             LOG_TAKE_ARG);
}

TEST_CASE(an_open_file_holds_back_its_own_folder_only)
{
    log_marks_t m = {
        .run_dir = CARD_AUTO_LOGS, .run = 0u,
        .test_dir = CARD_USER_LOGS, .test = 5u, .txt = 0u,
    };
    /* USER/LOGS/ATEST005.CSV open does not hide BENCH/LOGS/SERVO005.CSV,
     * nor 0.21's SERVO005.CSV in the root. */
    CHECK(log_marks_busy(&m, CARD_USER_LOGS, "ATEST005.CSV"));
    CHECK(!log_marks_busy(&m, CARD_AUTO_LOGS, "SERVO005.CSV"));
    CHECK(!log_marks_busy(&m, CARD_ROOT, "SERVO005.CSV"));
    CHECK(!log_marks_busy(&m, CARD_USER_LOGS, "ATEST004.CSV"));
    /* An automatic run open: its own folder only, in any case. */
    m.test = 0u;
    m.run = 5u;
    CHECK(log_marks_busy(&m, CARD_AUTO_LOGS, "SERVO005.CSV"));
    CHECK(log_marks_busy(&m, "bench/logs", "motor005.csv"));
    CHECK(!log_marks_busy(&m, CARD_USER_LOGS, "ATEST005.CSV"));
    /* A limit run's report open: the report, and a run the delete of
     * which would take that report. */
    m.run = 0u;
    m.txt = 7u;
    CHECK(log_marks_busy(&m, CARD_USER_LOGS, "LIMIT007.TXT"));
    CHECK(log_marks_busy(&m, CARD_USER_LOGS, "ATEST007.CSV"));
    CHECK(!log_marks_busy(&m, CARD_AUTO_LOGS, "LIMIT007.TXT"));
    /* Both kinds in one folder: both kinds' marks apply there. */
    const log_marks_t one = {
        .run_dir = CARD_AUTO_LOGS, .run = 3u,
        .test_dir = CARD_AUTO_LOGS, .test = 4u, .txt = 0u,
    };
    CHECK(log_marks_busy(&one, CARD_AUTO_LOGS, "MOTOR003.CSV"));
    CHECK(log_marks_busy(&one, CARD_AUTO_LOGS, "ATEST004.CSV"));
    CHECK(!log_marks_busy(&one, CARD_USER_LOGS, "ATEST004.CSV"));
    /* Nothing open, no marks, no name. */
    const log_marks_t none = { 0 };
    CHECK(!log_marks_busy(&none, CARD_AUTO_LOGS, "MOTOR003.CSV"));
    CHECK(!log_marks_busy(NULL, CARD_AUTO_LOGS, "MOTOR003.CSV"));
    CHECK(!log_marks_busy(&one, CARD_AUTO_LOGS, NULL));
}

TEST_CASE(every_folder_constant_is_an_8_3_path_that_fits)
{
    static const char *const k_dirs[] = {
        CARD_BENCH, CARD_AUTO_LOGS, CARD_USER, CARD_USER_LOGS,
        CARD_ESC_CONF, CARD_KST_CONF, CARD_HITEC_CONF,
    };
    for (size_t i = 0u; i < sizeof(k_dirs) / sizeof(k_dirs[0]); ++i) {
        const char *d = k_dirs[i];
        CHECK(strlen(d) < CARD_DIR_MAX);
        int seg = 0;
        for (const char *p = d; *p != '\0'; ++p) {
            if (*p == '/') {
                CHECK(seg >= 1 && seg <= 8);
                seg = 0;
                continue;
            }
            const bool ok = (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')
                            || *p == '_' || *p == '-';
            if (!ok) {
                T_FAIL("%s holds '%c'", d, *p);
            }
            ++seg;
        }
        CHECK(seg >= 1 && seg <= 8);
    }
    CHECK_STR_EQ(CARD_ROOT, "");
    CHECK_STR_EQ(CARD_FAULT_LOG, "BENCH/RCBENCH.LOG");
    /* The longest log path the logger opens: "/sdcard/" + the folder +
     * "/" + "SUPPL999.CSV", inside its 64-byte buffer. */
    CHECK(8u + (CARD_DIR_MAX - 1u) + 1u + (LOG_RUN_NAME_MAX - 1u) < 64u);
}

int main(void)
{
    RUN(the_first_run_makes_each_level_and_takes_001);
    RUN(the_number_is_above_the_highest_and_a_gap_is_not_filled);
    RUN(numbers_998_and_999_are_taken_and_then_the_folder_is_full);
    RUN(each_folder_is_walked_once_until_a_take_fails);
    RUN(both_kinds_in_one_folder_are_one_series);
    RUN(a_third_folder_gives_up_one_series_and_walks_it_again);
    RUN(a_file_named_bench_stops_the_folder_and_takes_no_number);
    RUN(a_card_that_takes_no_folder_records_no_run);
    RUN(three_refused_creates_call_the_folder_full_and_a_delete_frees_it);
    RUN(a_folder_that_does_not_read_records_no_run_and_is_read_again);
    RUN(no_card_and_wrong_arguments_take_nothing);
    RUN(an_open_file_holds_back_its_own_folder_only);
    RUN(every_folder_constant_is_an_8_3_path_that_fits);
    return test_summary("log_take");
}
