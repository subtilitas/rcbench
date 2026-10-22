/*
 * Run log names by writer: ATESTnnn for the AUTOMATIC TEST, LIMITnnn for a
 * LIMIT RUN's report, SERVOnnn and MOTORnnn for an ARMED run by the screen
 * that armed it, SUPPLnnn for a supply run; BENCHnnn and ESCnnn of
 * firmware 0.21.0 and older read back; one number series across all 7 in a
 * folder.
 *
 * The panel's logger (firmware/panel/main/panel_log.c) and its card side
 * (panel_card.c) are firmware and not in this suite; log_take.c's loop over
 * a folder is test_log_take's.  What the names decide is here: the walk
 * folds log_name_number() over a folder, log_name_next() is the first
 * number tried, each of log_name_form()'s names is probed before a number
 * is taken, log_name_busy() keeps an open file out of the viewer's list and
 * log_name_report_of() names the report a delete takes with its run.
 * card_next() and take_number() below are those loops, written over one
 * folder held in memory.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <string.h>

#include "greatest.h"

#include "arming.h"
#include "log_name.h"
#include "log_select.h"
#include "outputs.h"

/* ------------------------------------------------------------ the card */

#define CARD_MAX 1400

typedef struct {
    char names[CARD_MAX][LOG_RUN_NAME_MAX + 8];
    int  n;
} card_t;

static card_t g_card;

static void card_clear(void)
{
    memset(&g_card, 0, sizeof(g_card));
}

static void card_put(const char *name)
{
    if (g_card.n < CARD_MAX) {
        snprintf(g_card.names[g_card.n], sizeof(g_card.names[0]), "%s", name);
        ++g_card.n;
    }
}

/* FAT compares names without case: "servo007.csv" is SERVO007.CSV. */
static bool card_has(const char *name)
{
    for (int i = 0; i < g_card.n; ++i) {
        if (log_name_case_cmp(g_card.names[i], name) == 0) {
            return true;
        }
    }
    return false;
}

/* log_numbering(): the highest number on the card, and the next above it. */
static int card_next(void)
{
    int highest = LOG_RUN_FIRST - 1;
    for (int i = 0; i < g_card.n; ++i) {
        const int n = log_name_number(g_card.names[i]);
        if (n > highest) {
            highest = n;
        }
    }
    return log_name_next(highest);
}

/* number_free() in log_take.c: none of the number's ten names is on the
 * card. */
static bool number_free(int number)
{
    for (int form = 0; form < LOG_NAME_FORMS; ++form) {
        char name[LOG_RUN_NAME_MAX];
        log_name_form(name, sizeof(name), number, form);
        if (card_has(name)) {
            return false;
        }
    }
    return true;
}

/*
 * log_take(): from @p next up to LOG_RUN_LAST, the first free number, or -1
 * when there is none -- the folder is full.
 */
static int take_number(int next)
{
    for (int i = next; i <= LOG_RUN_LAST; ++i) {
        if (number_free(i)) {
            return i;
        }
    }
    return -1;
}

/* A run written: its number from the card, its name from its writer. */
static int write_run(log_prefix_t prefix, bool with_report, char *csv,
                     size_t n)
{
    const int num = take_number(card_next());
    if (num < 0) {
        csv[0] = '\0';
        return -1;
    }
    log_name_build(csv, n, prefix, num, false);
    card_put(csv);
    if (with_report) {
        char txt[LOG_RUN_NAME_MAX];
        log_name_build(txt, sizeof(txt), prefix, num, true);
        card_put(txt);
    }
    return num;
}

/* ------------------------------------------------------------ the names */

TEST_CASE(every_writer_names_its_runs_and_reads_the_number_back)
{
    static const struct {
        log_prefix_t p;
        const char  *first;
        const char  *last;
        const char  *report;
    } k[] = {
        { LOG_PREFIX_ATEST, "ATEST001.CSV", "ATEST999.CSV", "ATEST007.TXT" },
        { LOG_PREFIX_LIMIT, "LIMIT001.CSV", "LIMIT999.CSV", "LIMIT007.TXT" },
        { LOG_PREFIX_SERVO, "SERVO001.CSV", "SERVO999.CSV", "SERVO007.TXT" },
        { LOG_PREFIX_MOTOR, "MOTOR001.CSV", "MOTOR999.CSV", "MOTOR007.TXT" },
        { LOG_PREFIX_SUPPL, "SUPPL001.CSV", "SUPPL999.CSV", "SUPPL007.TXT" },
        { LOG_PREFIX_BENCH, "BENCH001.CSV", "BENCH999.CSV", "BENCH007.TXT" },
        { LOG_PREFIX_ESC,   "ESC001.CSV",   "ESC999.CSV",   "ESC007.TXT"   },
    };
    CHECK_EQ((int)(sizeof(k) / sizeof(k[0])), (int)LOG_PREFIX_COUNT);
    for (size_t w = 0u; w < sizeof(k) / sizeof(k[0]); ++w) {
        char name[LOG_RUN_NAME_MAX];
        log_name_build(name, sizeof(name), k[w].p, LOG_RUN_FIRST, false);
        CHECK_STR_EQ(name, k[w].first);
        log_name_build(name, sizeof(name), k[w].p, LOG_RUN_LAST, false);
        CHECK_STR_EQ(name, k[w].last);
        log_name_build(name, sizeof(name), k[w].p, 7, true);
        CHECK_STR_EQ(name, k[w].report);
        CHECK_EQ(log_report_number(name), 7);
        CHECK_EQ(log_run_number(name), -1);     /* a report is not a run */
        CHECK_EQ(log_name_number(name), 7);     /* and takes its number  */

        /* Every number in range survives the round trip, the prefix read
         * back with it (the report a deletion takes is the same writer's),
         * and every name is an 8.3 name: at most 8 characters before the
         * dot, 3 after. */
        for (int i = LOG_RUN_FIRST; i <= LOG_RUN_LAST; ++i) {
            char want[LOG_RUN_NAME_MAX];
            char got[LOG_RUN_NAME_MAX];
            log_name_build(name, sizeof(name), k[w].p, i, false);
            log_name_build(want, sizeof(want), k[w].p, i, true);
            log_name_report_of(got, sizeof(got), name);
            const char *dot = strchr(name, '.');
            if (log_run_number(name) != i || strcmp(got, want) != 0
                || dot == NULL || dot - name > 8 || strlen(dot + 1) != 3u) {
                T_FAIL("%d wrote \"%s\", read back %d", i, name,
                       log_run_number(name));
                break;
            }
        }
    }
    /* BENCH's own builders are the ones older tests and callers use. */
    char b[LOG_RUN_NAME_MAX];
    log_run_name(b, sizeof(b), 42);
    CHECK_STR_EQ(b, "BENCH042.CSV");
    log_report_name(b, sizeof(b), 42);
    CHECK_STR_EQ(b, "BENCH042.TXT");

    /* A card written back by a computer in lower case. */
    CHECK_EQ(log_run_number("servo042.csv"), 42);
    CHECK_EQ(log_run_number("Esc042.Csv"), 42);
    CHECK_EQ(log_report_number("servo999.txt"), 999);
    CHECK_EQ(log_run_number("motor042.csv"), 42);
    CHECK_EQ(log_report_number("Atest042.Txt"), 42);
    CHECK_EQ(log_report_number("limit001.txt"), 1);
    log_name_report_of(b, sizeof(b), "esc042.csv");
    CHECK_STR_EQ(b, "ESC042.TXT");
    log_name_report_of(b, sizeof(b), "atest042.csv");
    CHECK_STR_EQ(b, "ATEST042.TXT");
}

TEST_CASE(a_name_that_only_looks_like_a_run_is_not_one)
{
    static const char *const k_not[] = {
        "ESC12.CSV",      "ESC0001.CSV",    "ESC000.CSV",   "ESCABC.CSV",
        "ESCAPE.CSV",     "ESCAPE01.CSV",   "XESC001.CSV",  "ESC001.CSV.BAK",
        "SERVO12.CSV",    "SERVO000.CSV",   "SERVO1000.CSV", "SERV001.CSV",
        "SERVO001.LOG",   "SERVO001",       "SERVO",        "ESC",
        "BENCH000.TXT",   "BENCH12.CSV",    "SWEEP_920KV.CSV", "RCBENCH.LOG",
        "KST.TXT",        "",
        /* The new writers at their edges: four digits, number 0, two
         * digits, a sixth letter. */
        "SUPPL1000.CSV",  "ATEST000.CSV",   "LIMIT01.TXT",  "MOTORS01.CSV",
        "MOTOR1000.TXT",  "SUPPL00.CSV",    "ATEST.CSV",    "LIMIT",
        "XATEST001.CSV",  "ATEST001.CSVX",  "SUPP001.CSV",  "MOTOR-01.CSV",
    };
    char rep[LOG_RUN_NAME_MAX];
    for (size_t i = 0u; i < sizeof(k_not) / sizeof(k_not[0]); ++i) {
        snprintf(rep, sizeof(rep), "x");
        log_name_report_of(rep, sizeof(rep), k_not[i]);
        if (log_run_number(k_not[i]) != -1 || log_report_number(k_not[i]) != -1
            || log_name_number(k_not[i]) != -1 || rep[0] != '\0') {
            T_FAIL("\"%s\" read as a run", k_not[i]);
        }
    }
    CHECK_EQ(log_name_number(NULL), -1);
}

TEST_CASE(a_name_that_cannot_be_written_is_not_written_as_a_near_miss)
{
    char name[LOG_RUN_NAME_MAX];
    log_name_build(name, sizeof(name), LOG_PREFIX_SERVO, LOG_RUN_FIRST - 1,
                   false);
    CHECK_STR_EQ(name, "");
    log_name_build(name, sizeof(name), LOG_PREFIX_ESC, LOG_RUN_LAST + 1,
                   true);
    CHECK_STR_EQ(name, "");
    log_name_build(name, sizeof(name), LOG_PREFIX_SUPPL, LOG_RUN_LAST + 1,
                   false);
    CHECK_STR_EQ(name, "");
    log_name_build(name, sizeof(name), LOG_PREFIX_ATEST, 0, false);
    CHECK_STR_EQ(name, "");
    log_name_build(name, sizeof(name), LOG_PREFIX_COUNT, 1, false);
    CHECK_STR_EQ(name, "");
    log_name_build(name, sizeof(name), (log_prefix_t)-1, 1, false);
    CHECK_STR_EQ(name, "");
    /* The buffer is held to the longest name for every prefix, ESC's too:
     * a caller sized for "ESC001.CSV" is a caller one prefix from an
     * overflow. */
    char small[LOG_RUN_NAME_MAX - 1];
    log_name_build(small, sizeof(small), LOG_PREFIX_ESC, 1, false);
    CHECK_STR_EQ(small, "");
    log_name_build(small, sizeof(small), LOG_PREFIX_SUPPL, 999, false);
    CHECK_STR_EQ(small, "");
    /* SUPPL999.CSV, the longest, fills the buffer exactly. */
    log_name_build(name, sizeof(name), LOG_PREFIX_SUPPL, 999, false);
    CHECK_STR_EQ(name, "SUPPL999.CSV");
    CHECK_EQ((int)strlen(name), LOG_RUN_NAME_MAX - 1);
    log_name_build(NULL, sizeof(name), LOG_PREFIX_SERVO, 1, false);
    log_name_build(name, 0u, LOG_PREFIX_SERVO, 1, false);
}

TEST_CASE(a_number_is_carried_by_ten_names_and_each_takes_it)
{
    char seen[LOG_NAME_FORMS][LOG_RUN_NAME_MAX];
    CHECK_EQ(LOG_NAME_FORMS, 10);
    CHECK_EQ(LOG_PREFIX_WRITTEN, 5);
    for (int f = 0; f < LOG_NAME_FORMS; ++f) {
        log_name_form(seen[f], sizeof(seen[f]), 512, f);
        CHECK_EQ(log_name_number(seen[f]), 512);
        for (int g = 0; g < f; ++g) {
            if (log_name_case_cmp(seen[f], seen[g]) == 0) {
                T_FAIL("forms %d and %d are both \"%s\"", g, f, seen[f]);
            }
        }
    }
    /* Each writer this firmware has appears with both suffixes, once
     * each; BENCH and ESC, which it never writes, do not. */
    for (int p = 0; p < (int)LOG_PREFIX_COUNT; ++p) {
        for (int r = 0; r < 2; ++r) {
            char want[LOG_RUN_NAME_MAX];
            log_name_build(want, sizeof(want), (log_prefix_t)p, 512, r != 0);
            int hits = 0;
            for (int f = 0; f < LOG_NAME_FORMS; ++f) {
                hits += (strcmp(seen[f], want) == 0) ? 1 : 0;
            }
            if (hits != ((p < LOG_PREFIX_WRITTEN) ? 1 : 0)) {
                T_FAIL("\"%s\" is among the forms %d times", want, hits);
            }
        }
    }
    char name[LOG_RUN_NAME_MAX] = "x";
    log_name_form(name, sizeof(name), 512, LOG_NAME_FORMS);
    CHECK_STR_EQ(name, "");
    snprintf(name, sizeof(name), "x");
    log_name_form(name, sizeof(name), 512, -1);
    CHECK_STR_EQ(name, "");
    log_name_form(name, sizeof(name), 0, 0);
    CHECK_STR_EQ(name, "");
    log_name_form(NULL, 0u, 1, 0);
}

/* ------------------------------------------------------------ numbering */

TEST_CASE(the_next_number_is_one_above_the_highest_of_any_prefix)
{
    card_clear();
    CHECK_EQ(card_next(), LOG_RUN_FIRST);       /* an empty card */

    card_put("BENCH041.CSV");
    card_put("SERVO042.CSV");
    card_put("SERVO042.TXT");
    card_put("ESC040.CSV");
    CHECK_EQ(card_next(), 43);

    /* A report alone holds its number. */
    card_put("servo044.txt");
    CHECK_EQ(card_next(), 45);

    /* An ESC run above them all. */
    card_put("ESC100.CSV");
    CHECK_EQ(card_next(), 101);
}

TEST_CASE(a_folder_of_older_bench_files_continues_their_numbering)
{
    /* A folder that holds 0.21.0's names, copied there on a computer. */
    card_clear();
    card_put("BENCH001.CSV");
    card_put("BENCH002.CSV");
    card_put("BENCH002.TXT");       /* a servo test from older firmware */
    card_put("ESC016.CSV");
    card_put("BENCH017.CSV");
    char name[LOG_RUN_NAME_MAX];
    CHECK_EQ(write_run(LOG_PREFIX_ATEST, true, name, sizeof(name)), 18);
    CHECK_STR_EQ(name, "ATEST018.CSV");
    CHECK(card_has("ATEST018.TXT"));
    CHECK_EQ(write_run(LOG_PREFIX_MOTOR, false, name, sizeof(name)), 19);
    CHECK_STR_EQ(name, "MOTOR019.CSV");
    CHECK_EQ(write_run(LOG_PREFIX_SUPPL, false, name, sizeof(name)), 20);
    CHECK_STR_EQ(name, "SUPPL020.CSV");
    CHECK_EQ(write_run(LOG_PREFIX_SERVO, false, name, sizeof(name)), 21);
    CHECK_STR_EQ(name, "SERVO021.CSV");
    CHECK_EQ(write_run(LOG_PREFIX_LIMIT, false, name, sizeof(name)), 22);
    CHECK_STR_EQ(name, "LIMIT022.CSV");
}

TEST_CASE(a_gap_is_not_filled)
{
    card_clear();
    card_put("SUPPL001.CSV");
    card_put("MOTOR005.CSV");       /* 2 to 4 deleted on a computer */
    char name[LOG_RUN_NAME_MAX];
    CHECK_EQ(write_run(LOG_PREFIX_SUPPL, false, name, sizeof(name)), 6);
    CHECK_STR_EQ(name, "SUPPL006.CSV");
}

TEST_CASE(foreign_files_take_no_number)
{
    card_clear();
    card_put("SWEEP_920KV.CSV");
    card_put("RCBENCH.LOG");
    card_put("KST");
    card_put("ESCAPE01.CSV");
    card_put("SERVO1000.CSV");
    card_put("ESC000.CSV");
    card_put("notes.txt");
    CHECK_EQ(card_next(), LOG_RUN_FIRST);
    card_put("BENCH009.CSV");
    CHECK_EQ(card_next(), 10);
}

TEST_CASE(a_number_written_since_the_walk_is_stepped_over_whatever_its_prefix)
{
    /* The walk said 7.  A file carrying 7 under another writer's name, and
     * one carrying 8 as a report alone, appear before the open: neither is
     * overwritten and the run takes 9. */
    card_clear();
    card_put("MOTOR006.CSV");
    const int next = card_next();
    CHECK_EQ(next, 7);
    card_put("SUPPL007.CSV");
    card_put("LIMIT008.TXT");
    CHECK_EQ(take_number(next), 9);
    /* And every written writer's report as well as its run. */
    card_put("ATEST009.TXT");
    card_put("servo010.txt");
    card_put("MOTOR011.TXT");
    CHECK_EQ(take_number(next), 12);
}

TEST_CASE(numbers_998_and_999_are_taken_and_then_the_card_is_full)
{
    CHECK_EQ(log_name_next(LOG_RUN_FIRST - 1), LOG_RUN_FIRST);
    CHECK_EQ(log_name_next(-5), LOG_RUN_FIRST);
    CHECK_EQ(log_name_next(1), 2);
    CHECK_EQ(log_name_next(997), 998);
    CHECK_EQ(log_name_next(998), 999);
    CHECK(log_name_next(999) > LOG_RUN_LAST);
    CHECK(log_name_next(5000) > LOG_RUN_LAST);

    card_clear();
    card_put("MOTOR997.CSV");
    char name[LOG_RUN_NAME_MAX];
    CHECK_EQ(write_run(LOG_PREFIX_ATEST, true, name, sizeof(name)), 998);
    CHECK_STR_EQ(name, "ATEST998.CSV");
    CHECK_EQ(write_run(LOG_PREFIX_SUPPL, false, name, sizeof(name)), 999);
    CHECK_STR_EQ(name, "SUPPL999.CSV");
    CHECK_EQ(write_run(LOG_PREFIX_MOTOR, false, name, sizeof(name)), -1);
    CHECK_STR_EQ(name, "");

    /* Full at 999 whichever writer holds it, with the lower numbers free:
     * a run written into a gap would be the newest run wearing an old
     * number. */
    static const char *const k_last[] = { "BENCH999.CSV", "ESC999.CSV",
                                          "SERVO999.CSV", "SERVO999.TXT",
                                          "bench999.txt", "ATEST999.TXT",
                                          "LIMIT999.TXT", "MOTOR999.CSV",
                                          "suppl999.csv" };
    for (size_t i = 0u; i < sizeof(k_last) / sizeof(k_last[0]); ++i) {
        card_clear();
        card_put("BENCH001.CSV");
        card_put(k_last[i]);
        if (write_run(LOG_PREFIX_SERVO, false, name, sizeof(name)) != -1) {
            T_FAIL("a card holding %s took %s", k_last[i], name);
        }
    }
}

TEST_CASE(a_card_of_999_mixed_runs_is_full)
{
    card_clear();
    char name[LOG_RUN_NAME_MAX];
    for (int i = LOG_RUN_FIRST; i <= LOG_RUN_LAST; ++i) {
        const log_prefix_t p = (log_prefix_t)(i % LOG_PREFIX_WRITTEN);
        if (write_run(p, p == LOG_PREFIX_ATEST, name, sizeof(name)) != i) {
            T_FAIL("run %d was numbered %d", i, log_run_number(name));
            break;
        }
    }
    CHECK_EQ(write_run(LOG_PREFIX_SUPPL, false, name, sizeof(name)), -1);
}

/* ------------------------------------------------------------ writers */

TEST_CASE(a_servo_test_writes_its_csv_and_its_report_under_one_number)
{
    card_clear();
    card_put("ATEST010.CSV");
    char csv[LOG_RUN_NAME_MAX];
    CHECK_EQ(write_run(LOG_PREFIX_ATEST, true, csv, sizeof(csv)), 11);
    CHECK_STR_EQ(csv, "ATEST011.CSV");
    char txt[LOG_RUN_NAME_MAX];
    log_name_report_of(txt, sizeof(txt), csv);
    CHECK_STR_EQ(txt, "ATEST011.TXT");
    CHECK(card_has(txt));
    /* The pair is one number: the next run is above it. */
    CHECK_EQ(card_next(), 12);

    /* A limit run: its report alone, the next number. */
    CHECK_EQ(take_number(card_next()), 12);
    log_name_build(txt, sizeof(txt), LOG_PREFIX_LIMIT, 12, true);
    CHECK_STR_EQ(txt, "LIMIT012.TXT");
    card_put(txt);
    CHECK_EQ(card_next(), 13);
}

TEST_CASE(a_deleted_run_takes_its_own_writers_report)
{
    char txt[LOG_RUN_NAME_MAX];
    log_name_report_of(txt, sizeof(txt), "SERVO011.CSV");
    CHECK_STR_EQ(txt, "SERVO011.TXT");
    log_name_report_of(txt, sizeof(txt), "BENCH007.CSV");   /* older card */
    CHECK_STR_EQ(txt, "BENCH007.TXT");
    log_name_report_of(txt, sizeof(txt), "esc003.csv");
    CHECK_STR_EQ(txt, "ESC003.TXT");
    log_name_report_of(txt, sizeof(txt), "ATEST011.CSV");
    CHECK_STR_EQ(txt, "ATEST011.TXT");
    log_name_report_of(txt, sizeof(txt), "motor004.csv");  /* none written */
    CHECK_STR_EQ(txt, "MOTOR004.TXT");
    /* Not a run log: no report goes with it. */
    snprintf(txt, sizeof(txt), "x");
    log_name_report_of(txt, sizeof(txt), "SERVO011.TXT");
    CHECK_STR_EQ(txt, "");
    snprintf(txt, sizeof(txt), "x");
    log_name_report_of(txt, sizeof(txt), "SWEEP.CSV");
    CHECK_STR_EQ(txt, "");
    snprintf(txt, sizeof(txt), "x");
    log_name_report_of(txt, sizeof(txt), NULL);
    CHECK_STR_EQ(txt, "");
    log_name_report_of(NULL, 0u, "SERVO011.CSV");
}

/*
 * main.c's log_run_wanted() names an ARMED run by log_name_armed_prefix()
 * from the roles the arm asked for, arming_roles(): MOTOR & ESC and ESC
 * STICK ask for the throttle (panel_arm.c), SERVO for the surfaces
 * (servo_ctx_arm_surfaces()).  The same calls, on a real policy and bank.
 */
static void armed_name(const arming_t *a, const outputs_t *o, char *out,
                       size_t n, int number)
{
    const uint8_t roles = arming_roles(a);
    const log_prefix_t p = log_name_armed_prefix(
        roles != 0u, (roles & OUT_ROLE_BIT(OUT_ROLE_THROTTLE)) != 0u,
        outputs_role_armed(o, OUT_ROLE_THROTTLE));
    log_name_build(out, n, p, number, false);
}

TEST_CASE(an_armed_run_is_named_by_the_screen_that_armed_it)
{
    arming_t a;
    arming_init(&a, 0u, 0u);
    outputs_t o;
    outputs_init(&o, 0u);
    char name[LOG_RUN_NAME_MAX];

    /* Nothing asked yet: the throttle rule. */
    CHECK_EQ(arming_roles(&a), 0u);
    outputs_arm_roles(&o, OUT_ROLE_BIT(OUT_ROLE_THROTTLE), 0u);
    armed_name(&a, &o, name, sizeof(name), 1);
    CHECK_STR_EQ(name, "MOTOR001.CSV");
    outputs_arm(&o, false, 0u);
    outputs_arm_roles(&o, OUT_ROLE_BIT(OUT_ROLE_SURFACE), 0u);
    armed_name(&a, &o, name, sizeof(name), 2);
    CHECK_STR_EQ(name, "SERVO002.CSV");
    outputs_arm(&o, false, 0u);

    /* MOTOR & ESC's ARM. */
    arming_request_arm_roles(&a, 0u, OUT_ROLE_BIT(OUT_ROLE_THROTTLE));
    outputs_arm_roles(&o, OUT_ROLE_BIT(OUT_ROLE_THROTTLE), 0u);
    armed_name(&a, &o, name, sizeof(name), 3);
    CHECK_STR_EQ(name, "MOTOR003.CSV");
    outputs_arm(&o, false, 0u);

    /* SERVO's ARM, on an IOmcu older than protocol 4.18 that arms every
     * output whatever the roles: still a SERVO run. */
    arming_request_arm_roles(&a, 0u, OUT_ROLE_BIT(OUT_ROLE_SURFACE));
    outputs_arm_roles(&o, OUT_ROLES_ALL, 0u);
    armed_name(&a, &o, name, sizeof(name), 4);
    CHECK_STR_EQ(name, "SERVO004.CSV");

    CHECK_EQ(log_name_armed_prefix(true, true, false), LOG_PREFIX_MOTOR);
    CHECK_EQ(log_name_armed_prefix(true, false, true), LOG_PREFIX_SERVO);
    CHECK_EQ(log_name_armed_prefix(false, true, true), LOG_PREFIX_MOTOR);
    CHECK_EQ(log_name_armed_prefix(false, false, true), LOG_PREFIX_MOTOR);
    CHECK_EQ(log_name_armed_prefix(false, false, false), LOG_PREFIX_SERVO);
    CHECK_EQ(log_name_armed_prefix(false, true, false), LOG_PREFIX_SERVO);
}

/* ------------------------------------------------------------ readers */

TEST_CASE(the_newest_run_ranks_first_across_prefixes)
{
    CHECK(log_name_rank("SERVO200.CSV", "BENCH199.CSV") < 0);
    CHECK(log_name_rank("BENCH199.CSV", "ESC200.CSV") > 0);
    CHECK(log_name_rank("ESC100.CSV", "SERVO099.CSV") < 0);
    CHECK(log_name_rank("esc100.csv", "SERVO099.CSV") < 0);
    CHECK(log_name_rank("SERVO001.CSV", "SWEEP_920KV.CSV") < 0);
    CHECK(log_name_rank("SWEEP_920KV.CSV", "ESC001.CSV") > 0);
    /* A report ranks by its number, as its run does: a limit run's report
     * is its only file. */
    CHECK(log_name_rank("SERVO005.TXT", "BENCH001.CSV") < 0);
    CHECK(log_name_rank("SERVO005.TXT", "BENCH006.CSV") > 0);
    CHECK(log_name_rank("NOTES.TXT", "SWEEP.CSV") < 0);
    /* One number under two names: by name, both ways round, and one file
     * in two cases is one file. */
    CHECK(log_name_rank("BENCH007.CSV", "SERVO007.CSV") < 0);
    CHECK(log_name_rank("SERVO007.CSV", "BENCH007.CSV") > 0);
    CHECK(log_name_rank("ESC007.CSV", "SERVO007.CSV") < 0);
    CHECK_EQ(log_name_rank("SERVO007.CSV", "servo007.csv"), 0);
    /* All 7 writers in one order: the number first, the name on a tie. */
    static const char *const k_order[] = {
        "SUPPL300.CSV", "MOTOR299.CSV", "LIMIT298.CSV", "ATEST297.CSV",
        "BENCH296.CSV", "ESC295.CSV",   "SERVO294.CSV", "ATEST100.CSV",
        "BENCH100.CSV", "ESC100.CSV",   "LIMIT100.CSV", "LIMIT100.TXT",
        "MOTOR100.CSV", "SERVO100.CSV", "SUPPL100.CSV", "SWEEP.CSV",
    };
    const int n = (int)(sizeof(k_order) / sizeof(k_order[0]));
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            const int r = log_name_rank(k_order[i], k_order[j]);
            if ((i < j && r >= 0) || (i > j && r <= 0) || (i == j && r != 0)) {
                T_FAIL("%s against %s ranks %d", k_order[i], k_order[j], r);
            }
        }
    }
}

TEST_CASE(a_full_list_keeps_the_newest_runs_of_every_writer)
{
    /* 999 runs, the writers taking turns, offered oldest first: the list
     * keeps the 48 highest numbers, whichever prefix carries them. */
    static log_viewer_file_t set[LOG_VIEWER_MAX_FILES];
    int held = 0;
    for (int i = LOG_RUN_FIRST; i <= LOG_RUN_LAST; ++i) {
        log_viewer_file_t f;
        memset(&f, 0, sizeof(f));
        log_name_build(f.name, sizeof(f.name),
                       (log_prefix_t)(i % (int)LOG_PREFIX_COUNT), i, false);
        /* Over 4 GiB, as an exFAT card holds: the size travels whole. */
        f.size = ((uint64_t)i << 32) | (uint64_t)i;
        held = log_select_keep(set, held, LOG_VIEWER_MAX_FILES, &f);
    }
    CHECK_EQ(held, LOG_VIEWER_MAX_FILES);
    bool kept[LOG_RUN_LAST + 1];
    memset(kept, 0, sizeof(kept));
    for (int r = 0; r < held; ++r) {
        const int n = log_run_number(set[r].name);
        CHECK(n > LOG_RUN_LAST - LOG_VIEWER_MAX_FILES);
        CHECK(set[r].size == (((uint64_t)n << 32) | (uint64_t)n));
        if (n > 0) {
            kept[n] = true;
        }
    }
    for (int n = LOG_RUN_LAST - LOG_VIEWER_MAX_FILES + 1; n <= LOG_RUN_LAST;
         ++n) {
        if (!kept[n]) {
            T_FAIL("run %d was dropped", n);
        }
    }
}

/*
 * panel_card.c's card_take(): an entry the logger still writes is neither
 * listed nor counted; everything else is offered to the list.
 */
static int list_card(log_viewer_file_t *set, int cap, unsigned open_run,
                     unsigned open_test, unsigned open_txt, int *files)
{
    int held = 0;
    *files = 0;
    for (int i = 0; i < g_card.n; ++i) {
        if (log_name_busy(g_card.names[i], open_run, open_test, open_txt)) {
            continue;
        }
        ++*files;
        log_viewer_file_t f;
        memset(&f, 0, sizeof(f));
        snprintf(f.name, sizeof(f.name), "%s", g_card.names[i]);
        held = log_select_keep(set, held, cap, &f);
    }
    return held;
}

static bool listed(const log_viewer_file_t *set, int held, const char *name)
{
    for (int i = 0; i < held; ++i) {
        if (log_name_case_cmp(set[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

TEST_CASE(the_file_being_written_is_not_listed_whatever_its_prefix)
{
    card_clear();
    card_put("BENCH001.CSV");
    card_put("ESC002.CSV");
    card_put("SERVO003.CSV");
    card_put("SERVO003.TXT");
    card_put("SERVO004.TXT");
    card_put("SWEEP.CSV");
    static log_viewer_file_t set[LOG_VIEWER_MAX_FILES];
    int files = 0;

    /* An ESC run open. */
    int held = list_card(set, LOG_VIEWER_MAX_FILES, 2u, 0u, 0u, &files);
    CHECK_EQ(files, 5);
    CHECK(!listed(set, held, "ESC002.CSV"));
    CHECK(listed(set, held, "BENCH001.CSV"));

    /* A servo test's CSV open, and a bench run beside it. */
    held = list_card(set, LOG_VIEWER_MAX_FILES, 1u, 3u, 0u, &files);
    CHECK_EQ(files, 4);
    CHECK(!listed(set, held, "BENCH001.CSV"));
    CHECK(!listed(set, held, "SERVO003.CSV"));
    CHECK(listed(set, held, "SERVO003.TXT"));
    CHECK(listed(set, held, "ESC002.CSV"));

    /* A limit run's report open: the report alone is held back. */
    held = list_card(set, LOG_VIEWER_MAX_FILES, 0u, 0u, 4u, &files);
    CHECK_EQ(files, 5);
    CHECK(!listed(set, held, "SERVO004.TXT"));
    CHECK(listed(set, held, "SERVO003.CSV"));

    /* Nothing open: everything, and a lower-case copy is the same file. */
    held = list_card(set, LOG_VIEWER_MAX_FILES, 0u, 0u, 0u, &files);
    CHECK_EQ(files, 6);
    CHECK_EQ(held, 6);
    CHECK(!log_name_busy("servo003.csv", 0u, 0u, 0u));
    CHECK(log_name_busy("servo003.csv", 0u, 3u, 0u));
    /* A report's number open as a run does not hold the report.  A run's
     * number open as a report holds the run: deleting the run would delete
     * the report with it. */
    CHECK(!log_name_busy("SERVO003.TXT", 3u, 3u, 0u));
    CHECK(log_name_busy("SERVO003.CSV", 0u, 0u, 3u));
    CHECK(!log_name_busy("SWEEP.CSV", 1u, 1u, 1u));
    CHECK(!log_name_busy(NULL, 1u, 1u, 1u));
}

/*
 * panel_card.c's card_remove(): refused for a busy name; otherwise the name
 * goes, and the report beside a run goes with it.  Whether it went.
 */
static bool card_delete(const char *name, unsigned open_run,
                        unsigned open_test, unsigned open_txt)
{
    if (log_name_busy(name, open_run, open_test, open_txt)) {
        return false;
    }
    char txt[LOG_RUN_NAME_MAX];
    log_name_report_of(txt, sizeof(txt), name);
    int kept = 0;
    for (int i = 0; i < g_card.n; ++i) {
        if (log_name_case_cmp(g_card.names[i], name) == 0
            || (txt[0] != '\0'
                && log_name_case_cmp(g_card.names[i], txt) == 0)) {
            continue;
        }
        memmove(g_card.names[kept], g_card.names[i], sizeof(g_card.names[0]));
        ++kept;
    }
    g_card.n = kept;
    return true;
}

TEST_CASE(a_servo_test_whose_report_is_open_keeps_its_csv)
{
    /*
     * A servo test's CSV is closed and its report SERVO003.TXT is open.
     * Deleting SERVO003.CSV would remove the open report too, and with no
     * file lock in FatFs the logger would go on writing into freed
     * clusters.  So the CSV is held back with the report.
     */
    card_clear();
    card_put("ESC002.CSV");
    card_put("SERVO003.CSV");
    card_put("SERVO003.TXT");
    card_put("SERVO004.CSV");
    card_put("SERVO004.TXT");
    static log_viewer_file_t set[LOG_VIEWER_MAX_FILES];
    int files = 0;

    int held = list_card(set, LOG_VIEWER_MAX_FILES, 0u, 0u, 3u, &files);
    CHECK_EQ(files, 3);
    CHECK(!listed(set, held, "SERVO003.CSV"));
    CHECK(!listed(set, held, "SERVO003.TXT"));
    CHECK(!card_delete("SERVO003.CSV", 0u, 0u, 3u));
    CHECK(!card_delete("servo003.csv", 0u, 0u, 3u));
    CHECK(card_has("SERVO003.CSV"));
    CHECK(card_has("SERVO003.TXT"));

    /* Another number is not held: SERVO004 goes, with its report. */
    CHECK(listed(set, held, "SERVO004.CSV"));
    CHECK(card_delete("SERVO004.CSV", 0u, 0u, 3u));
    CHECK(!card_has("SERVO004.CSV"));
    CHECK(!card_has("SERVO004.TXT"));
    CHECK(card_has("SERVO003.TXT"));

    /* The report closed: the run is listed and its deletion takes both. */
    held = list_card(set, LOG_VIEWER_MAX_FILES, 0u, 0u, 0u, &files);
    CHECK_EQ(files, 3);
    CHECK(listed(set, held, "SERVO003.CSV"));
    CHECK(card_delete("SERVO003.CSV", 0u, 0u, 0u));
    CHECK(!card_has("SERVO003.CSV"));
    CHECK(!card_has("SERVO003.TXT"));
    CHECK(card_has("ESC002.CSV"));
}

TEST_CASE(the_newest_listed_run_is_found_across_prefixes)
{
    /* log_viewer_screen.c opens the list on the highest run: the same
     * test, log_run_number() and log_name_rank(), over a mixed card. */
    card_clear();
    card_put("BENCH040.CSV");
    card_put("SERVO041.CSV");
    card_put("SERVO041.TXT");
    card_put("ZZZ.CSV");
    card_put("ESC039.CSV");
    card_put("servo042.txt");     /* a report alone: not a run to open */
    static log_viewer_file_t set[LOG_VIEWER_MAX_FILES];
    int files = 0;
    const int held = list_card(set, LOG_VIEWER_MAX_FILES, 0u, 0u, 0u, &files);
    log_select_sort(set, held);
    int best = -1;
    for (int i = 0; i < held; ++i) {
        if (log_run_number(set[i].name) >= 0
            && (best < 0 || log_name_rank(set[i].name, set[best].name) < 0)) {
            best = i;
        }
    }
    CHECK(best >= 0);
    if (best >= 0) {
        CHECK_STR_EQ(set[best].name, "SERVO041.CSV");
    }
}

int main(void)
{
    RUN(every_writer_names_its_runs_and_reads_the_number_back);
    RUN(a_name_that_only_looks_like_a_run_is_not_one);
    RUN(a_name_that_cannot_be_written_is_not_written_as_a_near_miss);
    RUN(a_number_is_carried_by_ten_names_and_each_takes_it);
    RUN(the_next_number_is_one_above_the_highest_of_any_prefix);
    RUN(a_folder_of_older_bench_files_continues_their_numbering);
    RUN(a_gap_is_not_filled);
    RUN(foreign_files_take_no_number);
    RUN(a_number_written_since_the_walk_is_stepped_over_whatever_its_prefix);
    RUN(numbers_998_and_999_are_taken_and_then_the_card_is_full);
    RUN(a_card_of_999_mixed_runs_is_full);
    RUN(a_servo_test_writes_its_csv_and_its_report_under_one_number);
    RUN(a_deleted_run_takes_its_own_writers_report);
    RUN(an_armed_run_is_named_by_the_screen_that_armed_it);
    RUN(the_newest_run_ranks_first_across_prefixes);
    RUN(a_full_list_keeps_the_newest_runs_of_every_writer);
    RUN(the_file_being_written_is_not_listed_whatever_its_prefix);
    RUN(a_servo_test_whose_report_is_open_keeps_its_csv);
    RUN(the_newest_listed_run_is_found_across_prefixes);
    return test_summary("log_name");
}
