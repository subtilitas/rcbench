/*
 * A labelled servo's numbering, servo_take(), over a card held in memory:
 * Servos and Servos/<id> made on the way, each kind counted on its own from
 * 000 to 999, the report sharing its CSV's number, a gap not filled, a file
 * written since the walk stepped over, a kind at 999 full while the others
 * count, a card without room, without a card, and a walk that fails.  Then
 * the open marks of a servo's folder, where run 000 is a run like any other.
 *
 * The fake answers as FatFs does: names compare without case, mkdir of a
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
#include "servo_take.h"
#include "servo_trend.h"

/* ------------------------------------------------------------ the card */

#define FAKE_MAX  2100
#define PATH_MAX_ 64

typedef struct {
    char path[PATH_MAX_];   /* from the root, "Servos/KST-00001/..." */
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
    snprintf(p, sizeof(p), "%s/%s", dir, name);
    put(p, false);
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
        return g.e[i].dir;
    }
    if (g.no_new_dir) {
        return false;
    }
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

static bool f_walk(void *ctx, const char *dir, log_take_visit_fn visit,
                   void *visit_ctx)
{
    (void)ctx;
    ++g.walks;
    if (g.walk_fails) {
        return false;
    }
    const size_t n = strlen(dir);
    for (int i = 0; i < g.n; ++i) {
        const char *p = g.e[i].path;
        char head[PATH_MAX_];
        snprintf(head, sizeof(head), "%.*s", (int)n, p);
        if (strlen(p) > n + 1u && p[n] == '/'
            && log_name_case_cmp(head, dir) == 0
            && strchr(p + n + 1u, '/') == NULL) {
            visit(p + n + 1u, visit_ctx);
        }
    }
    return true;
}

static bool f_exists(void *ctx, const char *dir, const char *name)
{
    (void)ctx;
    ++g.probes;
    char p[PATH_MAX_];
    snprintf(p, sizeof(p), "%s/%s", dir, name);
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
    snprintf(p, sizeof(p), "%s/%s", dir, name);
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

static servo_series_t g_s;

static void fresh(void)
{
    memset(&g, 0, sizeof(g));
    servo_series_init(&g_s);
}

/* A take; the number it gave, -1 for none. */
static int take(const char *id, servo_kind_t k, bool report, log_take_t *rr)
{
    int n = 7777;
    const log_take_t r = servo_take(&g_s, &k_io, id, k, report, &n);
    if (rr != NULL) {
        *rr = r;
    }
    if (r != LOG_TAKE_OK && n != -1) {
        T_FAIL("a failed take (%d) left the number at %d", (int)r, n);
    }
    return n;
}

#define ID  "KST_X10-0000A"
#define DIR "Servos/KST_X10-0000A"

/* ------------------------------------------------------------ tests */

TEST_CASE(the_first_run_of_each_kind_is_000_in_a_folder_made_on_the_way)
{
    fresh();
    log_take_t r;
    CHECK_EQ(take(ID, SERVO_KIND_LIMIT, false, &r), 0);
    CHECK_EQ(r, LOG_TAKE_OK);
    CHECK(has("Servos"));
    CHECK(has(DIR));
    CHECK_STR_EQ(g.created, DIR "/limit run_000.csv");
    CHECK_EQ(g.created_number, 0);
    CHECK_EQ(g.walks, 1u);
    CHECK_EQ(g.probes, 2u);              /* the CSV's and the report's */
    CHECK_EQ(take(ID, SERVO_KIND_RAUTO, false, NULL), 0);
    CHECK_STR_EQ(g.created, DIR "/rudder auto_000.csv");
    CHECK_EQ(take(ID, SERVO_KIND_ATEST, true, NULL), 0);
    CHECK_STR_EQ(g.created, DIR "/automatic test_000.txt");
    CHECK_EQ(take(ID, SERVO_KIND_RUN, false, NULL), 0);
    CHECK_STR_EQ(g.created, DIR "/servo run_000.csv");
    CHECK_EQ(g.walks, 1u);               /* one walk for the folder */
    /* Lower case names the same folder and takes its numbers. */
    CHECK_EQ(take("kst_x10-0000a", SERVO_KIND_LIMIT, false, NULL), 1);
    CHECK_EQ(g.walks, 1u);
}

TEST_CASE(the_kinds_count_apart_and_a_report_shares_its_number)
{
    fresh();
    put("Servos", true);
    put(DIR, true);
    for (int i = 0; i <= 3; ++i) {
        char name[SERVO_NAME_SIZE];
        servo_run_name(name, sizeof(name), SERVO_KIND_LIMIT, i, false);
        put_file(DIR, name);
    }
    CHECK_EQ(take(ID, SERVO_KIND_LIMIT, false, NULL), 4);
    CHECK_EQ(take(ID, SERVO_KIND_ATEST, false, NULL), 0);
    /* The report is written under the CSV's number by the logger; a LIMIT
     * RUN that judged no position takes the next number for it alone. */
    put_file(DIR, "limit run_004.txt");
    CHECK_EQ(take(ID, SERVO_KIND_LIMIT, true, NULL), 5);
    CHECK_STR_EQ(g.created, DIR "/limit run_005.txt");
    CHECK_EQ(take(ID, SERVO_KIND_LIMIT, false, NULL), 6);
}

TEST_CASE(a_gap_is_not_filled_and_other_names_move_no_number)
{
    fresh();
    put("Servos", true);
    put(DIR, true);
    put_file(DIR, "servo run_000.csv");
    put_file(DIR, "servo run_007.CSV");        /* 1 to 6 moved off */
    put_file(DIR, "trend_servo run.csv");      /* part B's figures */
    put_file(DIR, "baseline.txt");
    put_file(DIR, "SERVO042.CSV");             /* a log folder's name */
    put_file(DIR, "servo run_900.csv.bak");
    CHECK_EQ(take(ID, SERVO_KIND_RUN, false, NULL), 8);
    CHECK_EQ(take(ID, SERVO_KIND_RAUTO, false, NULL), 0);
}

TEST_CASE(a_file_written_since_the_walk_is_stepped_over)
{
    fresh();
    CHECK_EQ(take(ID, SERVO_KIND_ATEST, false, NULL), 0);
    put_file(DIR, "automatic test_001.txt");
    put_file(DIR, "AUTOMATIC TEST_002.CSV");
    CHECK_EQ(take(ID, SERVO_KIND_ATEST, false, NULL), 3);
    CHECK_EQ(g.walks, 1u);
}

TEST_CASE(runs_998_and_999_are_taken_and_then_the_kind_is_full)
{
    fresh();
    put("Servos", true);
    put(DIR, true);
    put_file(DIR, "limit run_997.csv");
    log_take_t r;
    CHECK_EQ(take(ID, SERVO_KIND_LIMIT, false, &r), 998);
    CHECK_EQ(take(ID, SERVO_KIND_LIMIT, false, &r), 999);
    CHECK_STR_EQ(g.created, DIR "/limit run_999.csv");
    const unsigned creates = g.creates;
    CHECK_EQ(take(ID, SERVO_KIND_LIMIT, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_FULL);
    CHECK_EQ(g.creates, creates);        /* none tried */
    /* The other kinds keep counting. */
    CHECK_EQ(take(ID, SERVO_KIND_ATEST, false, &r), 0);
    CHECK_EQ(r, LOG_TAKE_OK);

    /* A folder whose LIMIT RUN 999 is a lone report is full for LIMIT
     * and still takes AUTOMATIC TEST. */
    fresh();
    put("Servos", true);
    put(DIR, true);
    put_file(DIR, "limit run_999.txt");
    CHECK_EQ(take(ID, SERVO_KIND_LIMIT, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_FULL);
    CHECK_EQ(take(ID, SERVO_KIND_ATEST, false, &r), 0);
}

TEST_CASE(a_folder_of_999_runs_of_one_kind_is_walked_once_and_full)
{
    fresh();
    put("Servos", true);
    put(DIR, true);
    for (int i = 0; i <= SERVO_RUN_LAST; ++i) {
        char name[SERVO_NAME_SIZE];
        servo_run_name(name, sizeof(name), SERVO_KIND_RUN, i, false);
        put_file(DIR, name);
    }
    log_take_t r;
    CHECK_EQ(take(ID, SERVO_KIND_RUN, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_FULL);
    CHECK_EQ(g.walks, 1u);
    CHECK_EQ(g.probes, 0u);
    /* Walked again after a full take: a file moved off frees its number,
     * and the next run goes above the highest still there. */
    const int i = find(DIR "/servo run_999.csv");
    g.e[i] = g.e[g.n - 1];
    --g.n;
    CHECK_EQ(take(ID, SERVO_KIND_RUN, false, &r), 999);
    CHECK_EQ(g.walks, 2u);
}

TEST_CASE(each_id_has_its_folder_and_a_files_edit_walks_again)
{
    fresh();
    CHECK_EQ(take(ID, SERVO_KIND_LIMIT, false, NULL), 0);
    CHECK_EQ(take("KST X10 LEFT WING-ZZZZZ", SERVO_KIND_LIMIT, false, NULL),
             0);
    CHECK_STR_EQ(g.created,
                 "Servos/KST X10 LEFT WING-ZZZZZ/limit run_000.csv");
    CHECK_EQ(strlen(g.created), 48u);    /* 53 for automatic test_999.csv */
    CHECK_EQ(g.walks, 2u);
    /* Back to the first: walked again, numbered from what it holds. */
    CHECK_EQ(take(ID, SERVO_KIND_LIMIT, false, NULL), 1);
    CHECK_EQ(g.walks, 3u);
    /* FILES copies a run in at a higher number. */
    put_file(DIR, "limit run_040.csv");
    log_series_forget_all();
    CHECK_EQ(take(ID, SERVO_KIND_LIMIT, false, NULL), 41);
    CHECK_EQ(g.walks, 4u);
    CHECK_EQ(take(ID, SERVO_KIND_LIMIT, false, NULL), 42);
    CHECK_EQ(g.walks, 4u);
}

TEST_CASE(servos_held_by_a_file_or_a_card_without_room_files_nothing)
{
    fresh();
    put("Servos", false);                /* a file named Servos */
    log_take_t r;
    CHECK_EQ(take(ID, SERVO_KIND_LIMIT, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_NO_FOLDER);
    CHECK_EQ(g.creates, 0u);

    /* A full or write-protected card: no folder, no file. */
    fresh();
    g.no_new_dir = true;
    CHECK_EQ(take(ID, SERVO_KIND_ATEST, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_NO_FOLDER);
    CHECK_EQ(g.n, 0);

    /* Servos there, the id's folder refused. */
    fresh();
    put("Servos", true);
    g.no_new_dir = true;
    CHECK_EQ(take(ID, SERVO_KIND_ATEST, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_NO_FOLDER);

    /* Three refused creates: full, and walked again next time. */
    fresh();
    g.creates_refused = 3;
    CHECK_EQ(take(ID, SERVO_KIND_RAUTO, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_FULL);
    CHECK_EQ(g.creates, 3u);
    CHECK_EQ(take(ID, SERVO_KIND_RAUTO, false, &r), 0);
    CHECK_EQ(g.walks, 2u);
}

TEST_CASE(no_card_a_walk_that_fails_and_wrong_arguments_take_nothing)
{
    fresh();
    g.unmounted = true;
    log_take_t r;
    CHECK_EQ(take(ID, SERVO_KIND_LIMIT, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_NO_CARD);
    CHECK_EQ(g.mkdirs, 0u);

    fresh();
    g.walk_fails = true;
    CHECK_EQ(take(ID, SERVO_KIND_LIMIT, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_UNREADABLE);
    CHECK_EQ(g.creates, 0u);
    g.walk_fails = false;
    CHECK_EQ(take(ID, SERVO_KIND_LIMIT, false, &r), 0);

    fresh();
    CHECK_EQ(take("KST-0001", SERVO_KIND_LIMIT, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_ARG);
    CHECK_EQ(take("", SERVO_KIND_LIMIT, false, &r), -1);
    CHECK_EQ(take(NULL, SERVO_KIND_LIMIT, false, &r), -1);
    CHECK_EQ(take(ID, SERVO_KIND_COUNT, false, &r), -1);
    CHECK_EQ(r, LOG_TAKE_ARG);
    CHECK_EQ(servo_take(NULL, &k_io, ID, SERVO_KIND_RUN, false, NULL),
             LOG_TAKE_ARG);
    int n = 5;
    CHECK_EQ(servo_take(&g_s, NULL, ID, SERVO_KIND_RUN, false, &n),
             LOG_TAKE_ARG);
    CHECK_EQ(n, -1);
    log_take_io_t io = k_io;
    io.walk = NULL;
    CHECK_EQ(servo_take(&g_s, &io, ID, SERVO_KIND_RUN, false, &n),
             LOG_TAKE_ARG);
    CHECK_EQ(g.n, 0);
}

/* ------------------------------------------------------------ marks */

TEST_CASE(a_servo_runs_marks_hold_its_csv_and_report_from_run_000)
{
    CHECK_EQ(servo_run_mark(SERVO_KIND_LIMIT, 0), 1000u);
    CHECK_EQ(servo_run_mark(SERVO_KIND_RUN, 999), 4999u);
    CHECK_EQ(servo_run_mark(SERVO_KIND_COUNT, 0), 0u);
    CHECK_EQ(servo_run_mark(SERVO_KIND_LIMIT, -1), 0u);
    CHECK_EQ(servo_run_mark(SERVO_KIND_LIMIT, 1000), 0u);

    const log_marks_t m = {
        .run_dir = CARD_AUTO_LOGS, .run = 0u,
        .test_dir = DIR, .test = servo_run_mark(SERVO_KIND_LIMIT, 0),
        .txt = 0u,
    };
    CHECK(log_marks_busy(&m, DIR, "limit run_000.csv"));
    CHECK(log_marks_busy(&m, DIR, "limit run_000.txt"));
    CHECK(log_marks_busy(&m, "servos/kst_x10-0000a", "LIMIT RUN_000.CSV"));
    CHECK(!log_marks_busy(&m, DIR, "limit run_001.csv"));
    CHECK(!log_marks_busy(&m, DIR, "rudder auto_000.csv"));
    CHECK(!log_marks_busy(&m, DIR, "baseline.txt"));
    /* Another servo's folder, and a log folder, are not held. */
    CHECK(!log_marks_busy(&m, "Servos/KST_X10-0000B", "limit run_000.csv"));
    CHECK(!log_marks_busy(&m, CARD_USER_LOGS, "LIMIT000.CSV"));

    /* The report alone open; and a SERVO run in its folder. */
    const log_marks_t t = {
        .run_dir = DIR, .run = servo_run_mark(SERVO_KIND_RUN, 12),
        .test_dir = DIR, .test = 0u,
        .txt = servo_run_mark(SERVO_KIND_ATEST, 3),
    };
    CHECK(log_marks_busy(&t, DIR, "automatic test_003.txt"));
    CHECK(log_marks_busy(&t, DIR, "automatic test_003.csv"));
    CHECK(log_marks_busy(&t, DIR, "servo run_012.csv"));
    CHECK(!log_marks_busy(&t, DIR, "servo run_011.csv"));
    /* None open: run 000 is free. */
    const log_marks_t none = { DIR, 0u, DIR, 0u, 0u };
    CHECK(!log_marks_busy(&none, DIR, "servo run_000.csv"));
    /* A log folder's marks read as on main. */
    const log_marks_t old = { CARD_AUTO_LOGS, 5u, CARD_USER_LOGS, 7u, 0u };
    CHECK(log_marks_busy(&old, CARD_AUTO_LOGS, "SERVO005.CSV"));
    CHECK(log_marks_busy(&old, CARD_USER_LOGS, "ATEST007.CSV"));
}

TEST_CASE(a_filed_run_is_heard)
{
    const unsigned before = servo_trend_last()->count;
    servo_trend_filed(ID, SERVO_KIND_RAUTO, 0);
    CHECK_EQ(servo_trend_last()->count, before + 1u);
    CHECK_STR_EQ(servo_trend_last()->id, ID);
    CHECK_EQ(servo_trend_last()->kind, SERVO_KIND_RAUTO);
    CHECK_EQ(servo_trend_last()->number, 0);
    servo_trend_filed(NULL, SERVO_KIND_RUN, 999);
    CHECK_STR_EQ(servo_trend_last()->id, "");
}

int main(void)
{
    RUN(the_first_run_of_each_kind_is_000_in_a_folder_made_on_the_way);
    RUN(the_kinds_count_apart_and_a_report_shares_its_number);
    RUN(a_gap_is_not_filled_and_other_names_move_no_number);
    RUN(a_file_written_since_the_walk_is_stepped_over);
    RUN(runs_998_and_999_are_taken_and_then_the_kind_is_full);
    RUN(a_folder_of_999_runs_of_one_kind_is_walked_once_and_full);
    RUN(each_id_has_its_folder_and_a_files_edit_walks_again);
    RUN(servos_held_by_a_file_or_a_card_without_room_files_nothing);
    RUN(no_card_a_walk_that_fails_and_wrong_arguments_take_nothing);
    RUN(a_servo_runs_marks_hold_its_csv_and_report_from_run_000);
    RUN(a_filed_run_is_heard);
    return test_summary("servo_take");
}
