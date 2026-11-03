/**
 * @file test_logview_report.c
 * @brief Host tests for the reports on LOGS.
 *
 * A servo automatic test writes ATESTnnn.CSV (comma-separated values) and
 * its report ATESTnnn.TXT, a LIMIT RUN writes LIMITnnn.TXT and no CSV, and
 * firmware 0.21.0 and older wrote a limit run's SERVOnnn.TXT alone into the
 * card's root.  The failures under test: a report missing from the list,
 * beside its run's CSV or without it, a text file that is no report listed,
 * the list's rule and FILES' report label apart, a report sorted after the
 * runs it is newer than, a page that loses or doubles an entry, a report
 * opened in the CSV import, and a report not handed to the text viewer or
 * handed over from the wrong folder.  The viewer itself is
 * test_files_view.c's.
 *
 * The fake card offers each folder through log_select_lists(), as the
 * panel's lister does, in one walk.
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include <stdlib.h>

#include "card_tree.h"
#include "files_screen.h"
#include "fm_rules.h"
#include "log_name.h"
#include "log_select.h"
#include "log_viewer_screen.h"
#include "logview_spots.h"
#include "settings.h"
#include "text_view.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W UI_PANEL_W
#define H UI_PANEL_H

/* ------------------------------------------------------- the fake card --- */

/** One file of the fake card: its folder, its name and its text. */
typedef struct {
    const char *dir;
    char        name[LOG_VIEWER_NAME_MAX];
    const char *text;
    uint64_t    size;   /* the size listed; 0 lists the text's length */
    bool        gone;
} fake_file_t;

#define FAKE_MAX 2000
static fake_file_t g_files[FAKE_MAX];
static int g_n;
static char g_dir[CARD_DIR_MAX];
static log_mem_ctx_t g_mem;
static int g_walks;             /* walks of a folder */
static bool g_open_refuses;
static char g_removed[LOG_VIEWER_NAME_MAX];

static const char k_limit_text[] =
    "RCBENCH SERVO LIMIT REPORT\n"
    "Result:         PASS\n"
    "Device:         KST X20-830\n"
    "\n"
    "Protections:    a sample at 4.0 A, above 2.5 A for 300 ms, no window "
    "for 500 ms, 60 s a search\n";

static const char k_csv_text[] = "t,v\n0,1\n1,2\n";

/** @brief Put @p name with @p text into folder @p dir of the fake card.
 *  @param dir   the folder, CARD_ROOT, CARD_USER_LOGS or CARD_AUTO_LOGS
 *  @param name  the file's name
 *  @param text  its contents */
static void add(const char *dir, const char *name, const char *text)
{
    if (g_n >= FAKE_MAX) {
        T_FAIL("the fake card is full at %d", g_n);
        return;
    }
    fake_file_t *f = &g_files[g_n++];
    f->dir = dir;
    snprintf(f->name, sizeof(f->name), "%s", name);
    f->text = text;
    f->size = 0u;
    f->gone = false;
}

/** @brief Whether file @p i is in the folder the viewer points at.
 *  @param i  the file
 *  @return true while it is there */
static bool here(int i)
{
    return !g_files[i].gone && strcmp(g_files[i].dir, g_dir) == 0;
}

/** @brief The panel's walk of one folder, into @p sel.
 *  @param sel  the page being chosen
 *  @return the files the list holds */
static int walk(log_select_page_t *sel)
{
    int n = 0;
    ++g_walks;
    for (int i = 0; i < g_n; ++i) {
        if (!here(i) || !log_select_lists(g_files[i].name)) {
            continue;
        }
        log_viewer_file_t f;
        memset(&f, 0, sizeof(f));
        snprintf(f.name, sizeof(f.name), "%s", g_files[i].name);
        f.size = (g_files[i].size != 0u) ? g_files[i].size
                                         : strlen(g_files[i].text);
        log_select_page_offer(sel, &f);
        ++n;
    }
    return n;
}

static int fake_list(log_viewer_file_t *out, int max_entries,
                     log_viewer_page_t *page, void *ctx)
{
    (void)ctx;
    log_select_page_t sel;
    log_select_page_begin(&sel, out, max_entries, page);
    const int n = walk(&sel);
    (void)log_select_page_end(&sel, page);
    return n;
}

static bool fake_open(const char *name, log_source_t *src, void *ctx)
{
    (void)ctx;
    if (g_open_refuses) {
        return false;
    }
    for (int i = 0; i < g_n; ++i) {
        if (here(i) && strcmp(g_files[i].name, name) == 0) {
            log_source_memory(src, &g_mem, g_files[i].text,
                              strlen(g_files[i].text));
            return true;
        }
    }
    return false;
}

static bool fake_remove(const char *name, void *ctx)
{
    (void)ctx;
    snprintf(g_removed, sizeof(g_removed), "%s", name);
    for (int i = 0; i < g_n; ++i) {
        if (here(i) && strcmp(g_files[i].name, name) == 0) {
            g_files[i].gone = true;
            return true;
        }
    }
    return false;
}

static void fake_folder(const char *dir, void *ctx)
{
    (void)ctx;
    snprintf(g_dir, sizeof(g_dir), "%s", dir);
}

static const char *fake_volume(void *ctx)
{
    (void)ctx;
    return "UNITEST";
}

static const log_viewer_io_t k_io = {
    .list   = fake_list,
    .open   = fake_open,
    .close  = NULL,
    .volume = fake_volume,
    .remove = fake_remove,
    .folder = fake_folder,
    .ctx    = NULL,
};

/* ------------------------------------------------------------ harness --- */

static gfx_color_t *s_fb;
static gfx_canvas_t s_c;

static const ui_screen_t *screen(void)
{
    return log_viewer_screen();
}

/** @brief An empty card and the screen as a start leaves it. */
static void fresh(void)
{
    settings_init();
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
    memset(g_files, 0, sizeof(g_files));
    g_n = 0;
    g_walks = 0;
    g_open_refuses = false;
    g_removed[0] = '\0';
    if (s_fb == NULL) {
        s_fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(s_fb);
    }
    gfx_canvas_init(&s_c, s_fb, W, H, W);
    screen()->reset();
    log_viewer_set_io(&k_io);
}

/** @brief Show folder @p tab, read from its first page.
 *  @param tab  the tab */
static void show(log_viewer_tab_t tab)
{
    g_walks = 0;
    log_viewer_set_tab(tab);
}

static void send(int type, int x, int y)
{
    touch_event_t e = {
        .type = (touch_event_type_t)type,
        .point = { .id = 1u, .x = (int16_t)x, .y = (int16_t)y },
    };
    screen()->event(&e);
}

static void tap(int x, int y)
{
    send(TOUCH_EVENT_DOWN, x, y);
    send(TOUCH_EVENT_UP, x, y);
}

static void draw(void)
{
    log_viewer_invalidate();
    screen()->render(&s_c, 0);
}

/**
 * @brief Whether OPEN handed @p name of folder @p dir to the text viewer:
 *        FILES on top with the viewer, which names that file.
 * @param name  the report
 * @param dir   its folder
 * @return true when it did
 */
static bool in_viewer(const char *name, const char *dir)
{
    const bool ok = ui_router_current() == SCREEN_FILES
                    && files_screen_view() == FILES_VIEW_TEXT
                    && strcmp(text_view_name(), name) == 0
                    && strcmp(text_view_dir(), dir) == 0;
    if (!ok) {
        T_FAIL("OPEN on %s/%s: screen %d, FILES view %d, viewer %s/%s",
               dir, name, (int)ui_router_current(), (int)files_screen_view(),
               text_view_dir(), text_view_name());
    }
    return ok;
}

/* Geometry mirrored from the screen (logview_spots.h), in its own
 * coordinates. */
#define BR_ROW_Y(i) LV_ROW_CY(i)
#define FOOT_CY     LV_FOOT_CY
#define OPEN_X      LV_OPEN_CX
#define DELETE_X    LV_DELETE_CX
#define DQ_CY       LV_DQ_CY
#define DQ_DELETE_X LV_DQ_DELETE_CX

/** @brief The names on the page in the order the list draws them, joined
 *  by spaces, into @p out.
 *  @param out  takes the names
 *  @param n    bytes of @p out */
static void rows(char *out, size_t n)
{
    /* The list keeps no public copy; the page is read again here as the
     * screen read it, and the screen's own count is held to it. */
    log_viewer_file_t page[LOG_VIEWER_MAX_FILES];
    log_viewer_page_t q;
    memset(&q, 0, sizeof(q));
    q.dir = LOG_PAGE_FROM;
    fake_folder(log_viewer_tab_dir(log_viewer_tab()), NULL);
    const int walks = g_walks;
    const int total = fake_list(page, LOG_VIEWER_MAX_FILES, &q, NULL);
    g_walks = walks;
    out[0] = '\0';
    size_t at = 0u;
    for (int i = 0; i < q.held && i < total; ++i) {
        at += (size_t)snprintf(out + at, n - at, "%s%s", (i > 0) ? " " : "",
                               page[i].name);
        if (at >= n) {
            return;
        }
    }
}

/* ----------------------------------------------- which names are listed --- */

TEST_CASE(the_list_holds_csv_files_and_every_report)
{
    /* A run's CSV and its report, with the CSV there or not. */
    CHECK(log_select_lists("ATEST001.CSV"));
    CHECK(log_select_lists("ATEST001.TXT"));
    CHECK(log_select_lists("atest002.txt"));
    CHECK(log_select_lists("LIMIT002.TXT"));
    CHECK(log_select_lists("limit003.txt"));
    CHECK(log_select_lists("SERVO004.TXT"));
    CHECK(log_select_lists("SWEEP_920KV.CSV"));
    CHECK(log_select_lists("sweep.csv"));
    /* The edges of a report's number: 001 and 999 are, 000 and four
     * digits are not. */
    CHECK(log_select_lists("ATEST999.TXT"));
    CHECK(!log_select_lists("ATEST000.TXT"));
    CHECK(!log_select_lists("LIMIT1000.TXT"));
    /* Text files that are no report, and names that are no CSV. */
    CHECK(!log_select_lists("NOTES.TXT"));
    CHECK(!log_select_lists("KST.TXT"));
    CHECK(!log_select_lists(".csv"));
    CHECK(!log_select_lists("RUN.CSV.BAK"));
    CHECK(!log_select_lists(""));
    CHECK(!log_select_lists(NULL));
}

/*
 * One rule: the list holds a name exactly when FILES labels it a run, a CSV
 * or a report (fm_kind_of()), in each of LOGS' three folders.
 */
TEST_CASE(the_list_and_files_report_label_are_one_rule)
{
    static const char *const names[] = {
        "ATEST001.CSV", "ATEST001.TXT", "ATEST000.TXT", "LIMIT002.TXT",
        "LIMIT1000.TXT", "SERVO004.TXT", "BENCH041.CSV", "MOTOR999.CSV",
        "SWEEP_920KV.CSV", "NOTES.TXT", "MG90S.TXT", "PROFILE.JSON",
        "RCBENCH.LOG", "ATEST001.TXT.BAK", "atest003.txt",
    };
    static const char *const dirs[] = { CARD_USER_LOGS, CARD_AUTO_LOGS,
                                        CARD_ROOT };
    for (size_t d = 0; d < sizeof(dirs) / sizeof(dirs[0]); ++d) {
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
            const fm_kind_t k = fm_kind_of(dirs[d], names[i], false);
            const bool files = k == FM_KIND_RUN || k == FM_KIND_CSV
                               || k == FM_KIND_REPORT;
            if (files != log_select_lists(names[i])) {
                T_FAIL("%s/%s: FILES kind %d, LOGS lists %d", dirs[d],
                       names[i], (int)k, (int)log_select_lists(names[i]));
            }
            if ((k == FM_KIND_REPORT) != (log_report_number(names[i]) >= 0)) {
                T_FAIL("%s: the report label is not log_report_number()",
                       names[i]);
            }
        }
    }
}

TEST_CASE(a_report_and_a_run_of_one_prefix_and_number_are_told_apart)
{
    /* One number under two prefixes, a card edited on a computer: both
     * are listed, each its own row. */
    CHECK(log_select_lists("BENCH007.CSV"));
    CHECK(log_select_lists("SERVO007.TXT"));
    CHECK(log_select_lists("ESC001.TXT"));
    CHECK(log_select_lists("BENCH999.TXT"));

    log_prefix_t k = LOG_PREFIX_COUNT;
    bool report = false;
    CHECK_EQ(log_name_parse("LIMIT004.TXT", &k, &report), 4);
    CHECK_EQ(k, LOG_PREFIX_LIMIT);
    CHECK(report);
    CHECK_EQ(log_name_parse("esc999.csv", &k, &report), 999);
    CHECK_EQ(k, LOG_PREFIX_ESC);
    CHECK(!report);
    CHECK_EQ(log_name_parse("ATEST001.TXT", NULL, NULL), 1);
    k = LOG_PREFIX_COUNT;
    report = true;
    CHECK_EQ(log_name_parse("NOTES.TXT", &k, &report), -1);
    CHECK_EQ(log_name_parse("LIMIT000.TXT", &k, &report), -1);
    CHECK_EQ(log_name_parse(NULL, &k, &report), -1);
    CHECK_EQ(k, LOG_PREFIX_COUNT);          /* left as it was */
    CHECK(report);
}

/* --------------------------------------------------------------- LOGS --- */

TEST_CASE(a_limit_report_and_a_runs_report_are_listed_newest_first)
{
    fresh();
    add(CARD_USER_LOGS, "ATEST004.CSV", k_csv_text);
    add(CARD_USER_LOGS, "ATEST004.TXT", k_limit_text);
    add(CARD_USER_LOGS, "LIMIT005.TXT", k_limit_text);
    show(LOG_TAB_USER);
    char list[256];
    rows(list, sizeof(list));
    CHECK_STR_EQ(list, "LIMIT005.TXT ATEST004.CSV ATEST004.TXT");
    char title[64];
    log_viewer_browse_title(title, sizeof(title));
    CHECK_STR_EQ(title, "FILES");
    CHECK_EQ(g_walks, 1);
    /* The newest number is chosen, and OPEN opens it in the viewer. */
    tap(OPEN_X, FOOT_CY);
    CHECK(in_viewer("LIMIT005.TXT", CARD_USER_LOGS));
    CHECK_STR_EQ(log_viewer_open_name(), "LIMIT005.TXT");
    ui_router_goto(SCREEN_LOGS);
    draw();
}

TEST_CASE(a_servo_report_of_an_older_card_is_listed_in_the_root)
{
    fresh();
    add(CARD_ROOT, "SERVO042.CSV", k_csv_text);
    add(CARD_ROOT, "SERVO042.TXT", k_limit_text);
    add(CARD_ROOT, "SERVO043.TXT", k_limit_text);
    add(CARD_ROOT, "BENCH041.CSV", k_csv_text);
    add(CARD_ROOT, "NOTES.TXT", "a note\n");
    add(CARD_ROOT, "SWEEP_920KV.CSV", k_csv_text);
    show(LOG_TAB_ROOT);
    char list[256];
    rows(list, sizeof(list));
    CHECK_STR_EQ(list, "SERVO043.TXT SERVO042.CSV SERVO042.TXT BENCH041.CSV "
                       "SWEEP_920KV.CSV");
    CHECK_EQ(g_walks, 1);
    tap(400, BR_ROW_Y(0));                  /* chosen already: opens */
    CHECK(in_viewer("SERVO043.TXT", CARD_ROOT));
    CHECK_STR_EQ(log_viewer_open_name(), "SERVO043.TXT");
    ui_router_goto(SCREEN_LOGS);
}

/*
 * The tester's folder: a servo automatic test's CSV with its report, and a
 * report whose CSV was deleted on a computer.  Both reports are rows, and
 * OPEN reads a run's CSV into the import and hands its report to the
 * viewer.
 */
TEST_CASE(a_runs_report_is_a_row_with_its_csv_there_or_not)
{
    fresh();
    add(CARD_USER_LOGS, "ATEST001.TXT", k_limit_text);
    add(CARD_USER_LOGS, "ATEST001.CSV", k_csv_text);
    add(CARD_USER_LOGS, "ATEST002.TXT", k_limit_text);
    show(LOG_TAB_USER);
    char list[128];
    rows(list, sizeof(list));
    CHECK_STR_EQ(list, "ATEST002.TXT ATEST001.CSV ATEST001.TXT");
    CHECK_EQ(g_walks, 1);
    tap(OPEN_X, FOOT_CY);
    CHECK(in_viewer("ATEST002.TXT", CARD_USER_LOGS));
    CHECK_STR_EQ(log_viewer_open_name(), "ATEST002.TXT");
    ui_router_goto(SCREEN_LOGS);
    tap(400, BR_ROW_Y(1));                  /* the run's CSV */
    tap(OPEN_X, FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    CHECK(log_viewer_analysis() != NULL);
    screen()->back();
    tap(400, BR_ROW_Y(2));                  /* its report */
    tap(OPEN_X, FOOT_CY);
    CHECK(in_viewer("ATEST001.TXT", CARD_USER_LOGS));
    CHECK_STR_EQ(log_viewer_open_name(), "ATEST001.TXT");
    ui_router_goto(SCREEN_LOGS);
    /* The same OPEN as FILES hands over: each report by its name. */
    CHECK_EQ(log_viewer_show(LOG_TAB_USER, "ATEST001.TXT", 0u),
             LOG_SHOW_OPEN);
    CHECK(in_viewer("ATEST001.TXT", CARD_USER_LOGS));
    ui_router_goto(SCREEN_LOGS);
    CHECK_EQ(log_viewer_show(LOG_TAB_USER, "ATEST002.TXT", 0u),
             LOG_SHOW_OPEN);
    CHECK_STR_EQ(log_viewer_open_name(), "ATEST002.TXT");
    ui_router_goto(SCREEN_LOGS);
    CHECK_EQ(log_viewer_show(LOG_TAB_USER, "ATEST003.TXT", 0u),
             LOG_SHOW_NOT_LISTED);
}

TEST_CASE(a_card_of_reports_only_lists_them_newest_first)
{
    fresh();
    add(CARD_USER_LOGS, "LIMIT001.TXT", k_limit_text);
    add(CARD_USER_LOGS, "LIMIT003.TXT", k_limit_text);
    add(CARD_USER_LOGS, "LIMIT002.TXT", k_limit_text);
    add(CARD_ROOT, "SERVO007.TXT", k_limit_text);
    add(CARD_ROOT, "SERVO009.TXT", k_limit_text);
    show(LOG_TAB_USER);
    char list[128];
    rows(list, sizeof(list));
    CHECK_STR_EQ(list, "LIMIT003.TXT LIMIT002.TXT LIMIT001.TXT");
    CHECK_EQ(g_walks, 1);
    tap(OPEN_X, FOOT_CY);
    CHECK_STR_EQ(log_viewer_open_name(), "LIMIT003.TXT");
    screen()->back();
    show(LOG_TAB_ROOT);
    rows(list, sizeof(list));
    CHECK_STR_EQ(list, "SERVO009.TXT SERVO007.TXT");
    CHECK_EQ(g_walks, 1);
    draw();
}

TEST_CASE(reports_sort_among_the_runs_by_their_number)
{
    fresh();
    add(CARD_USER_LOGS, "ATEST001.CSV", k_csv_text);
    add(CARD_USER_LOGS, "ATEST001.TXT", k_limit_text);
    add(CARD_USER_LOGS, "LIMIT002.TXT", k_limit_text);
    add(CARD_USER_LOGS, "ATEST003.CSV", k_csv_text);
    add(CARD_USER_LOGS, "ATEST003.TXT", k_limit_text);
    add(CARD_USER_LOGS, "LIMIT004.TXT", k_limit_text);
    add(CARD_USER_LOGS, "MY_TEST.CSV", k_csv_text);
    show(LOG_TAB_USER);
    char list[256];
    rows(list, sizeof(list));
    CHECK_STR_EQ(list, "LIMIT004.TXT ATEST003.CSV ATEST003.TXT LIMIT002.TXT "
                       "ATEST001.CSV ATEST001.TXT MY_TEST.CSV");
    /* The rank: a report by its number, a run first on one number. */
    CHECK(log_name_rank("LIMIT004.TXT", "ATEST003.CSV") < 0);
    CHECK(log_name_rank("SERVO005.TXT", "BENCH001.CSV") < 0);
    CHECK(log_name_rank("ATEST003.CSV", "ATEST003.TXT") < 0);
    CHECK(log_name_rank("ATEST003.TXT", "LIMIT003.CSV") < 0);
    CHECK(log_name_rank("LIMIT001.TXT", "MY_TEST.CSV") < 0);
    CHECK(log_name_rank("NOTES.TXT", "MY_TEST.CSV") > 0);
    CHECK_EQ(log_name_rank("limit004.txt", "LIMIT004.TXT"), 0);
}

/*
 * A full folder: 999 numbers, every third a LIMIT report, the rest ATEST
 * runs with their reports, and every seventh of those runs' CSV deleted on
 * a computer, which leaves its report without it.  The pages, read from the
 * first with NEXT's key, hold every listed name once, in rank order, and
 * the count is the list's: 1,570 names, one walk a page.
 */
TEST_CASE(pages_of_a_full_folder_hold_every_listed_name_once_in_order)
{
    fresh();
    static char names[2 * LOG_RUN_LAST][16];
    int listed = 0;
    for (int r = LOG_RUN_LAST; r >= 1; --r) {
        if (r % 3 == 0) {
            log_name_build(names[listed], 16, LOG_PREFIX_LIMIT, r, true);
            add(CARD_USER_LOGS, names[listed++], k_limit_text);
            continue;
        }
        if (r % 7 != 0) {
            log_name_build(names[listed], 16, LOG_PREFIX_ATEST, r, false);
            add(CARD_USER_LOGS, names[listed++], k_csv_text);
        }
        log_name_build(names[listed], 16, LOG_PREFIX_ATEST, r, true);
        add(CARD_USER_LOGS, names[listed++], k_limit_text);
    }
    CHECK_EQ(listed, 333 + 666 + 666 - 95);
    fake_folder(CARD_USER_LOGS, NULL);
    log_viewer_file_t page[LOG_VIEWER_MAX_FILES];
    log_viewer_page_t q;
    memset(&q, 0, sizeof(q));
    q.dir = LOG_PAGE_FROM;
    int seen = 0;
    g_walks = 0;
    int pages = 0;
    for (int p = 0; p < 60; ++p) {
        const int total = fake_list(page, LOG_VIEWER_MAX_FILES, &q, NULL);
        CHECK_EQ(total, listed);
        CHECK_EQ(q.ahead, seen);
        if (q.held == 0) {
            break;
        }
        ++pages;
        for (int i = 0; i < q.held; ++i) {
            /* Every name once, newest number first, a run's CSV before
             * its report. */
            if (strcmp(page[i].name, names[seen + i]) != 0) {
                T_FAIL("page %d row %d: %s, want %s", p, i, page[i].name,
                       names[seen + i]);
                return;
            }
        }
        seen += q.held;
        q.dir = LOG_PAGE_AFTER;
        snprintf(q.key, sizeof(q.key), "%.*s", (int)sizeof(q.key) - 1,
                 page[q.held - 1].name);
    }
    CHECK_EQ(seen, listed);
    CHECK_EQ(pages, (listed + LOG_VIEWER_MAX_FILES - 1) / LOG_VIEWER_MAX_FILES);
    CHECK_EQ(g_walks, pages + 1);          /* the last, empty read too */
    /* The screen's own count: the tab names the part of the folder. */
    show(LOG_TAB_USER);
    char title[64];
    log_viewer_browse_title(title, sizeof(title));
    char want[64];
    snprintf(want, sizeof(want), "FILES 1 TO 48 OF %d", listed);
    CHECK_STR_EQ(title, want);
    CHECK_EQ(g_walks, 1);
}

/* ---------------------------------------------- the report in the viewer --- */

TEST_CASE(opening_a_report_hands_it_to_the_viewer_on_files)
{
    static const struct {
        log_viewer_tab_t tab;
        const char *dir;
    } k_tabs[] = {
        { LOG_TAB_USER, CARD_USER_LOGS },
        { LOG_TAB_BENCH, CARD_AUTO_LOGS },
        { LOG_TAB_ROOT, CARD_ROOT },
    };
    for (size_t k = 0; k < sizeof(k_tabs) / sizeof(k_tabs[0]); ++k) {
        fresh();
        add(k_tabs[k].dir, "LIMIT005.TXT", k_limit_text);
        show(k_tabs[k].tab);
        tap(OPEN_X, FOOT_CY);
        CHECK(in_viewer("LIMIT005.TXT", k_tabs[k].dir));
        /* LOGS stays on its list, the report its open file. */
        CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
        CHECK_STR_EQ(log_viewer_open_name(), "LIMIT005.TXT");
        /* The home tag leaves the viewer for LOGS, which opened it. */
        CHECK(files_screen()->back());
        CHECK_EQ(ui_router_current(), SCREEN_LOGS);
        CHECK_EQ(ui_router_parent(SCREEN_FILES), SCREEN_OVERVIEW);
        CHECK_EQ(text_view_state(), TEXT_VIEW_CLOSED);
        CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    }
    draw();
}

/*
 * The viewer's limit, 1MiB: a report of exactly that is handed over, one
 * byte more stays on the list with the reason, in both languages.
 */
TEST_CASE(a_report_over_1mib_stays_on_the_list_with_the_reason)
{
    fresh();
    add(CARD_USER_LOGS, "LIMIT005.TXT", k_limit_text);
    g_files[0].size = (uint64_t)TEXT_BUF_FILE_MAX + 1u;
    show(LOG_TAB_USER);
    tap(OPEN_X, FOOT_CY);
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    CHECK_STR_EQ(log_viewer_message(), ui_tr(TX_ED_TOO_LARGE));
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(log_viewer_message(), ui_tr_in(UI_LANG_DE, TX_ED_TOO_LARGE));
    draw();
    ui_text_set_language(UI_LANG_EN);
    fresh();
    add(CARD_USER_LOGS, "LIMIT005.TXT", k_limit_text);
    g_files[0].size = TEXT_BUF_FILE_MAX;
    show(LOG_TAB_USER);
    tap(OPEN_X, FOOT_CY);
    CHECK(in_viewer("LIMIT005.TXT", CARD_USER_LOGS));
    ui_router_goto(SCREEN_LOGS);
}

/*
 * Back from the viewer LOGS chooses the report again: on the first page
 * when it is there, else on the page that starts at it; a report gone from
 * the card meanwhile leaves the first page with its own choice.
 */
TEST_CASE(back_from_the_viewer_the_report_is_chosen_again)
{
    fresh();
    static char names[60][16];
    for (int r = 1; r <= 60; ++r) {
        log_name_build(names[r - 1], 16, LOG_PREFIX_LIMIT, r, true);
        add(CARD_USER_LOGS, names[r - 1], k_limit_text);
    }
    show(LOG_TAB_USER);
    /* Row 2 of the first page: LIMIT058.TXT. */
    tap(400, BR_ROW_Y(2));
    tap(OPEN_X, FOOT_CY);
    CHECK(in_viewer("LIMIT058.TXT", CARD_USER_LOGS));
    ui_router_goto(SCREEN_LOGS);
    tap(OPEN_X, FOOT_CY);
    CHECK(in_viewer("LIMIT058.TXT", CARD_USER_LOGS));
    ui_router_goto(SCREEN_LOGS);
    /* The second page's first row: LIMIT012.TXT, 48 below the top. */
    tap(LV_NEXT_CX, LV_HEAD_CY);
    tap(400, BR_ROW_Y(0));
    tap(OPEN_X, FOOT_CY);
    CHECK(in_viewer("LIMIT012.TXT", CARD_USER_LOGS));
    ui_router_goto(SCREEN_LOGS);
    tap(OPEN_X, FOOT_CY);
    CHECK(in_viewer("LIMIT012.TXT", CARD_USER_LOGS));
    /* Gone while the viewer showed it: the first page, the newest. */
    for (int i = 0; i < g_n; ++i) {
        if (strcmp(g_files[i].name, "LIMIT012.TXT") == 0) {
            g_files[i].gone = true;
        }
    }
    ui_router_goto(SCREEN_LOGS);
    char title[64];
    log_viewer_browse_title(title, sizeof(title));
    CHECK_STR_EQ(title, "FILES 1 TO 48 OF 59");
    tap(OPEN_X, FOOT_CY);
    CHECK(in_viewer("LIMIT060.TXT", CARD_USER_LOGS));
    ui_router_goto(SCREEN_LOGS);
}

TEST_CASE(deleting_a_report_removes_that_file)
{
    fresh();
    add(CARD_USER_LOGS, "LIMIT005.TXT", k_limit_text);
    add(CARD_USER_LOGS, "ATEST004.CSV", k_csv_text);
    show(LOG_TAB_USER);
    tap(OPEN_X, FOOT_CY);
    CHECK(in_viewer("LIMIT005.TXT", CARD_USER_LOGS));
    ui_router_goto(SCREEN_LOGS);
    tap(DELETE_X, FOOT_CY);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_STR_EQ(g_removed, "LIMIT005.TXT");
    CHECK_STR_EQ(log_viewer_open_name(), "");
    char list[128];
    rows(list, sizeof(list));
    CHECK_STR_EQ(list, "ATEST004.CSV");
}

/*
 * An I/O without open: OPEN on a CSV file stays on the list and says there
 * is no source, in the language showing.  A report needs none of LOGS: the
 * viewer reads it through FILES' card task.
 */
TEST_CASE(an_io_without_open_says_so_in_the_language_showing)
{
    static log_viewer_io_t no_open;
    fresh();
    no_open = k_io;
    no_open.open = NULL;
    log_viewer_set_io(&no_open);
    add(CARD_USER_LOGS, "ATEST001.CSV", "t,v\n0,1\n1,2\n");
    show(LOG_TAB_USER);
    tap(OPEN_X, FOOT_CY);
    CHECK(log_viewer_analysis() == NULL);
    CHECK_STR_EQ(log_viewer_message(),
                 ui_tr_in(UI_LANG_EN, TX_LOG_NO_SOURCE));
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(log_viewer_message(),
                 ui_tr_in(UI_LANG_DE, TX_LOG_NO_SOURCE));
    draw();
    ui_text_set_language(UI_LANG_EN);
    fresh();
    log_viewer_set_io(&no_open);
    add(CARD_USER_LOGS, "LIMIT005.TXT", k_limit_text);
    show(LOG_TAB_USER);
    tap(OPEN_X, FOOT_CY);
    CHECK(in_viewer("LIMIT005.TXT", CARD_USER_LOGS));
    ui_router_goto(SCREEN_LOGS);
    log_viewer_set_io(&k_io);
}

int main(void)
{
    RUN(the_list_holds_csv_files_and_every_report);
    RUN(the_list_and_files_report_label_are_one_rule);
    RUN(a_report_and_a_run_of_one_prefix_and_number_are_told_apart);
    RUN(a_limit_report_and_a_runs_report_are_listed_newest_first);
    RUN(a_servo_report_of_an_older_card_is_listed_in_the_root);
    RUN(a_runs_report_is_a_row_with_its_csv_there_or_not);
    RUN(a_card_of_reports_only_lists_them_newest_first);
    RUN(reports_sort_among_the_runs_by_their_number);
    RUN(pages_of_a_full_folder_hold_every_listed_name_once_in_order);
    RUN(opening_a_report_hands_it_to_the_viewer_on_files);
    RUN(a_report_over_1mib_stays_on_the_list_with_the_reason);
    RUN(back_from_the_viewer_the_report_is_chosen_again);
    RUN(deleting_a_report_removes_that_file);
    RUN(an_io_without_open_says_so_in_the_language_showing);
    free(s_fb);
    return test_summary("logview_report");
}
