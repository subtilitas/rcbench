/**
 * @file test_logview_report.c
 * @brief Host tests for the reports standing alone on LOGS.
 *
 * A LIMIT RUN writes LIMITnnn.TXT and no CSV (comma-separated values), and
 * firmware 0.21.0 and older wrote a limit run's SERVOnnn.TXT alone into the
 * card's root.  The failures under test: such a report missing from the
 * list, a run's report listed beside its CSV, a report sorted after the runs
 * it is newer than, a page that loses or doubles an entry when the reports
 * arrive in a second walk, a report opened in the CSV import, a frame that
 * reads the whole text to draw its last rows, and a report's 32 KiB kept
 * after its view is left.  The rows themselves are test_log_text.c's.
 *
 * The fake card offers each folder through the notes of log_select.h, as
 * the panel's lister does: the CSV files and the LIMIT reports in the first
 * walk, the other reports standing alone in a second one.
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include <stdlib.h>

#include "card_tree.h"
#include "log_name.h"
#include "log_select.h"
#include "log_text.h"
#include "log_viewer_screen.h"
#include "settings.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H 480

/* ------------------------------------------------------- the fake card --- */

/** One file of the fake card: its folder, its name and its text. */
typedef struct {
    const char *dir;
    char        name[LOG_VIEWER_NAME_MAX];
    const char *text;
    bool        gone;
} fake_file_t;

#define FAKE_MAX 2000
static fake_file_t g_files[FAKE_MAX];
static int g_n;
static char g_dir[CARD_DIR_MAX];
static log_mem_ctx_t g_mem;
static int g_walks;             /* walks of a folder, both passes counted */
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
    f->gone = false;
}

/** @brief Whether file @p i is in the folder the viewer points at.
 *  @param i  the file
 *  @return true while it is there */
static bool here(int i)
{
    return !g_files[i].gone && strcmp(g_files[i].dir, g_dir) == 0;
}

/** @brief The panel's walks of one folder, into @p sel.
 *  @param sel  the page being chosen
 *  @return the files the list holds */
static int walk(log_select_page_t *sel)
{
    static log_lone_t lone;
    log_lone_begin(&lone);
    int n = 0;
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1 && !log_lone_again(&lone)) {
            break;
        }
        ++g_walks;
        for (int i = 0; i < g_n; ++i) {
            if (!here(i)) {
                continue;
            }
            const bool listed = (pass == 0)
                                    ? log_lone_first(&lone, g_files[i].name)
                                    : log_lone_second(&lone, g_files[i].name);
            if (!listed) {
                continue;
            }
            log_viewer_file_t f;
            memset(&f, 0, sizeof(f));
            snprintf(f.name, sizeof(f.name), "%s", g_files[i].name);
            f.size = strlen(g_files[i].text);
            log_select_page_offer(sel, &f);
            ++n;
        }
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

/* Geometry mirrored from the screen, in its own coordinates. */
#define BR_ROW_Y(i) (44 + 30 + (i) * 44 + 18)
#define FOOT_CY     373
#define OPEN_X      724
#define DELETE_X    596
#define TX_BACK_X   100
#define DQ_CY       282
#define DQ_DELETE_X 536
/* The text view: its rows' panel, 18 px a row, 14 rows; the scroll bar. */
#define TX_ROWS_SEEN 14
#define TX_PANEL_X  300
#define TX_PANEL_Y  200
#define BAR_X       (16 + 768 - 8 - 42 + 21)
#define BAR_TOP     74
#define BAR_H       260

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

TEST_CASE(the_notes_list_csv_files_and_reports_standing_alone)
{
    static log_lone_t l;
    log_lone_begin(&l);
    /* The first walk: CSV files and LIMIT reports at once. */
    CHECK(log_lone_first(&l, "ATEST001.CSV"));
    CHECK(!log_lone_first(&l, "ATEST001.TXT"));
    CHECK(log_lone_first(&l, "LIMIT002.TXT"));
    CHECK(log_lone_first(&l, "limit003.txt"));
    CHECK(!log_lone_first(&l, "SERVO004.TXT"));
    CHECK(log_lone_first(&l, "SWEEP_920KV.CSV"));
    CHECK(log_lone_first(&l, "sweep.csv"));
    CHECK(!log_lone_first(&l, "NOTES.TXT"));
    CHECK(!log_lone_first(&l, "KST.TXT"));
    CHECK(!log_lone_first(&l, ".csv"));
    CHECK(!log_lone_first(&l, "RUN.CSV.BAK"));
    CHECK(!log_lone_first(&l, NULL));
    /* SERVO004.TXT has no CSV: a second walk, which offers it alone. */
    CHECK(log_lone_again(&l));
    CHECK(log_lone_second(&l, "SERVO004.TXT"));
    CHECK(log_lone_second(&l, "servo004.txt"));
    CHECK(!log_lone_second(&l, "ATEST001.TXT"));    /* beside its CSV    */
    CHECK(!log_lone_second(&l, "LIMIT002.TXT"));    /* offered already   */
    CHECK(!log_lone_second(&l, "ATEST001.CSV"));
    CHECK(!log_lone_second(&l, "SERVO005.TXT"));    /* not in the folder */
    CHECK(!log_lone_second(&l, "NOTES.TXT"));
    CHECK(!log_lone_second(&l, NULL));
    CHECK(!log_lone_second(NULL, "SERVO004.TXT"));
    /* Its CSV found later in the walk: no longer alone, no second walk. */
    CHECK(log_lone_first(&l, "SERVO004.CSV"));
    CHECK(!log_lone_again(&l));
    CHECK(!log_lone_second(&l, "SERVO004.TXT"));
    /* Without notes the CSV files and the LIMIT reports are still listed. */
    CHECK(log_lone_first(NULL, "MOTOR001.CSV"));
    CHECK(log_lone_first(NULL, "LIMIT001.TXT"));
    CHECK(!log_lone_first(NULL, "ATEST001.TXT"));
    CHECK(!log_lone_again(NULL));
    log_lone_begin(NULL);
}

TEST_CASE(a_report_and_a_run_of_one_prefix_and_number_are_told_apart)
{
    static log_lone_t l;
    log_lone_begin(&l);
    /* One number under two prefixes, a card edited on a computer: BENCH007's
     * CSV does not make SERVO007.TXT a run's report. */
    CHECK(log_lone_first(&l, "BENCH007.CSV"));
    CHECK(!log_lone_first(&l, "SERVO007.TXT"));
    CHECK(log_lone_again(&l));
    CHECK(log_lone_second(&l, "SERVO007.TXT"));
    /* The first and the last number. */
    CHECK(!log_lone_first(&l, "ESC001.TXT"));
    CHECK(!log_lone_first(&l, "BENCH999.TXT"));
    CHECK(log_lone_second(&l, "ESC001.TXT"));
    CHECK(log_lone_second(&l, "BENCH999.TXT"));
    CHECK(log_lone_first(&l, "BENCH999.CSV"));
    CHECK(!log_lone_second(&l, "BENCH999.TXT"));

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

TEST_CASE(a_lone_limit_report_is_listed_and_chosen_as_the_newest)
{
    fresh();
    add(CARD_USER_LOGS, "ATEST004.CSV", k_csv_text);
    add(CARD_USER_LOGS, "ATEST004.TXT", k_limit_text);
    add(CARD_USER_LOGS, "LIMIT005.TXT", k_limit_text);
    show(LOG_TAB_USER);
    char list[256];
    rows(list, sizeof(list));
    CHECK_STR_EQ(list, "LIMIT005.TXT ATEST004.CSV");
    char title[64];
    log_viewer_browse_title(title, sizeof(title));
    CHECK_STR_EQ(title, "FILES");
    /* ATEST004.TXT stands beside its CSV: one walk. */
    CHECK_EQ(g_walks, 1);
    /* The newest number is chosen, and OPEN opens it as text. */
    tap(OPEN_X, FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_TEXT);
    CHECK_STR_EQ(log_viewer_open_name(), "LIMIT005.TXT");
    draw();
}

TEST_CASE(a_lone_servo_report_of_an_older_card_is_listed_in_the_root)
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
    CHECK_STR_EQ(list, "SERVO043.TXT SERVO042.CSV BENCH041.CSV "
                       "SWEEP_920KV.CSV");
    /* SERVO043.TXT is known alone only after the whole folder: two walks. */
    CHECK_EQ(g_walks, 2);
    tap(400, BR_ROW_Y(0));                  /* chosen already: opens */
    CHECK_EQ(log_viewer_view(), LOG_VIEW_TEXT);
    CHECK_STR_EQ(log_viewer_open_name(), "SERVO043.TXT");
}

TEST_CASE(a_run_with_its_report_lists_and_opens_as_before)
{
    fresh();
    add(CARD_USER_LOGS, "ATEST001.TXT", k_limit_text);
    add(CARD_USER_LOGS, "ATEST001.CSV", k_csv_text);
    show(LOG_TAB_USER);
    char list[128];
    rows(list, sizeof(list));
    CHECK_STR_EQ(list, "ATEST001.CSV");
    CHECK_EQ(g_walks, 1);
    tap(OPEN_X, FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    CHECK(log_viewer_analysis() != NULL);
    CHECK(log_viewer_text() == NULL);
}

TEST_CASE(a_card_of_lone_reports_only_lists_them_newest_first)
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
    CHECK_EQ(g_walks, 2);
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
    CHECK_STR_EQ(list, "LIMIT004.TXT ATEST003.CSV LIMIT002.TXT ATEST001.CSV "
                       "MY_TEST.CSV");
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
 * a computer, which leaves its report alone for the second walk.  The
 * pages, read from the first with NEXT's key, hold every listed name once,
 * in rank order, and the count is the list's.
 */
TEST_CASE(pages_of_a_full_folder_hold_every_listed_name_once_in_order)
{
    fresh();
    static char names[LOG_RUN_LAST + 1][2][16];
    int listed = 0;
    for (int r = 1; r <= LOG_RUN_LAST; ++r) {
        if (r % 3 == 0) {
            log_name_build(names[r][0], 16, LOG_PREFIX_LIMIT, r, true);
            add(CARD_USER_LOGS, names[r][0], k_limit_text);
            ++listed;
            continue;
        }
        log_name_build(names[r][0], 16, LOG_PREFIX_ATEST, r, false);
        log_name_build(names[r][1], 16, LOG_PREFIX_ATEST, r, true);
        if (r % 7 != 0) {
            add(CARD_USER_LOGS, names[r][0], k_csv_text);
        }
        add(CARD_USER_LOGS, names[r][1], k_limit_text);
        ++listed;
    }
    fake_folder(CARD_USER_LOGS, NULL);
    log_viewer_file_t page[LOG_VIEWER_MAX_FILES];
    log_viewer_page_t q;
    memset(&q, 0, sizeof(q));
    q.dir = LOG_PAGE_FROM;
    int seen = 0;
    int expect = LOG_RUN_LAST;
    char last[LOG_VIEWER_NAME_MAX] = "";
    for (int p = 0; p < 30; ++p) {
        const int total = fake_list(page, LOG_VIEWER_MAX_FILES, &q, NULL);
        CHECK_EQ(total, listed);
        CHECK_EQ(q.ahead, seen);
        if (q.held == 0) {
            break;
        }
        for (int i = 0; i < q.held; ++i) {
            /* Every number once, newest first: the CSV where it is there,
             * the report where it stands alone. */
            const bool alone = expect % 3 != 0 && expect % 7 == 0;
            const char *want = names[expect][alone ? 1 : 0];
            if (strcmp(page[i].name, want) != 0) {
                T_FAIL("page %d row %d: %s, want %s", p, i, page[i].name,
                       want);
                return;
            }
            CHECK(last[0] == '\0' || log_name_rank(last, page[i].name) < 0);
            snprintf(last, sizeof(last), "%s", page[i].name);
            --expect;
        }
        seen += q.held;
        q.dir = LOG_PAGE_AFTER;
        snprintf(q.key, sizeof(q.key), "%s", page[q.held - 1].name);
    }
    CHECK_EQ(seen, listed);
    CHECK_EQ(expect, 0);
    /* The screen's own count: the tab names the part of the folder. */
    show(LOG_TAB_USER);
    char title[64];
    log_viewer_browse_title(title, sizeof(title));
    char want[64];
    snprintf(want, sizeof(want), "FILES 1 TO 48 OF %d", listed);
    CHECK_STR_EQ(title, want);
    CHECK_EQ(g_walks, 2);
}

/* ------------------------------------------------------- the text view --- */

/** @brief A text of @p lines numbered lines, "line 1" up, into a static
 *  buffer.
 *  @param lines  how many
 *  @return the text */
static const char *numbered_lines(int lines)
{
    static char buf[8192];
    size_t at = 0u;
    buf[0] = '\0';
    for (int i = 1; i <= lines && at + 16u < sizeof(buf); ++i) {
        at += (size_t)snprintf(buf + at, sizeof(buf) - at, "line %d\n", i);
    }
    return buf;
}

TEST_CASE(opening_a_report_shows_its_rows_and_back_returns_to_the_list)
{
    fresh();
    add(CARD_USER_LOGS, "LIMIT005.TXT", k_limit_text);
    show(LOG_TAB_USER);
    tap(OPEN_X, FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_TEXT);
    const log_text_t *t = log_viewer_text();
    CHECK(t != NULL);
    if (t == NULL) {
        return;
    }
    CHECK_EQ(t->rows, 6);       /* the long line breaks once at 86 cells */
    CHECK(!t->cut);
    char row[128];
    CHECK(log_text_row((log_text_t *)t, 0, row, sizeof(row)));
    CHECK_STR_EQ(row, "RCBENCH SERVO LIMIT REPORT");
    CHECK(log_text_row((log_text_t *)t, 3, row, sizeof(row)));
    CHECK_STR_EQ(row, "");
    CHECK(log_text_row((log_text_t *)t, 5, row, sizeof(row)));
    CHECK_STR_EQ(row, "a search");
    draw();
    /* A tap on a row does nothing; BACK returns to the list as it was. */
    tap(TX_PANEL_X, TX_PANEL_Y);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_TEXT);
    tap(TX_BACK_X, FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    CHECK(log_viewer_text() == NULL);           /* freed with the view */
    CHECK_STR_EQ(log_viewer_open_name(), "LIMIT005.TXT");
    draw();
    /* The home tag climbs the same step, and frees the text too. */
    tap(OPEN_X, FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_TEXT);
    CHECK(screen()->back());
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    CHECK(log_viewer_text() == NULL);
    CHECK(!screen()->back());
    /* Leaving LOGS frees it; LOGS comes back on the list. */
    tap(OPEN_X, FOOT_CY);
    CHECK(log_viewer_text() != NULL);
    screen()->leave();
    CHECK(log_viewer_text() == NULL);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    screen()->enter();
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    draw();
    /* Leaving from the list or the import keeps what is open. */
    screen()->leave();
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    /* A press that slides off BACK is not a tap on it. */
    tap(OPEN_X, FOOT_CY);
    send(TOUCH_EVENT_DOWN, TX_BACK_X, FOOT_CY);
    send(TOUCH_EVENT_MOVE, TX_BACK_X + 200, FOOT_CY);
    send(TOUCH_EVENT_UP, TX_BACK_X + 200, FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_TEXT);
}

TEST_CASE(a_long_report_scrolls_by_drag_and_by_its_bar_within_its_rows)
{
    fresh();
    add(CARD_USER_LOGS, "LIMIT001.TXT", numbered_lines(40));
    show(LOG_TAB_USER);
    tap(OPEN_X, FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_TEXT);
    CHECK_EQ(log_viewer_text_first(), 0);
    /* Up by 5 rows of 18 px. */
    send(TOUCH_EVENT_DOWN, TX_PANEL_X, 300);
    send(TOUCH_EVENT_MOVE, TX_PANEL_X, 300 - 5 * 18);
    send(TOUCH_EVENT_UP, TX_PANEL_X, 300 - 5 * 18);
    CHECK_EQ(log_viewer_text_first(), 5);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_TEXT);
    /* Past the end it stops at the last screen: 40 - 14. */
    send(TOUCH_EVENT_DOWN, TX_PANEL_X, 330);
    send(TOUCH_EVENT_MOVE, TX_PANEL_X, 330 - 40 * 18);
    send(TOUCH_EVENT_UP, TX_PANEL_X, 330 - 40 * 18);
    CHECK_EQ(log_viewer_text_first(), 40 - TX_ROWS_SEEN);
    draw();
    /* The bar: a tap at its top end is the first row, at its foot the
     * last screen. */
    tap(BAR_X, BAR_TOP + 2);
    CHECK_EQ(log_viewer_text_first(), 0);
    tap(BAR_X, BAR_TOP + BAR_H - 2);
    CHECK_EQ(log_viewer_text_first(), 40 - TX_ROWS_SEEN);
    /* The thumb dragged half way. */
    send(TOUCH_EVENT_DOWN, BAR_X, BAR_TOP + BAR_H - 10);
    send(TOUCH_EVENT_MOVE, BAR_X, BAR_TOP + BAR_H / 2);
    send(TOUCH_EVENT_UP, BAR_X, BAR_TOP + BAR_H / 2);
    CHECK(log_viewer_text_first() > 0);
    CHECK(log_viewer_text_first() < 40 - TX_ROWS_SEEN);
    draw();
    /* A report that fits does not scroll. */
    fresh();
    add(CARD_USER_LOGS, "LIMIT001.TXT", numbered_lines(TX_ROWS_SEEN));
    show(LOG_TAB_USER);
    tap(OPEN_X, FOOT_CY);
    send(TOUCH_EVENT_DOWN, TX_PANEL_X, 300);
    send(TOUCH_EVENT_MOVE, TX_PANEL_X, 100);
    send(TOUCH_EVENT_UP, TX_PANEL_X, 100);
    CHECK_EQ(log_viewer_text_first(), 0);
    tap(BAR_X, BAR_TOP + BAR_H - 2);
    CHECK_EQ(log_viewer_text_first(), 0);
}

TEST_CASE(a_report_that_does_not_open_stays_on_the_list_with_the_reason)
{
    fresh();
    add(CARD_USER_LOGS, "LIMIT005.TXT", k_limit_text);
    show(LOG_TAB_USER);
    g_open_refuses = true;
    tap(OPEN_X, FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    CHECK(log_viewer_text() == NULL);
    char want[96];
    snprintf(want, sizeof(want), ui_tr(TX_LOG_CANNOT_OPEN), "LIMIT005.TXT");
    CHECK_STR_EQ(log_viewer_message(), want);
    draw();
}

/** @brief Pixels of @p col in the TEXT view's first row.
 *  @param col  the colour
 *  @return how many */
static int first_row_pixels(gfx_color_t col)
{
    int n = 0;
    for (int y = 74; y < 74 + 18; ++y) {
        for (int x = 16 + 8 + 8; x < 16 + 8 + 8 + 86 * 8; ++x) {
            n += (s_fb[y * W + x] == col) ? 1 : 0;
        }
    }
    return n;
}

/* An empty report opens on one row that says so, nothing to scroll; a
 * report with text draws its first row in the text colour instead. */
TEST_CASE(an_empty_report_opens_on_one_row_that_says_so)
{
    fresh();
    add(CARD_USER_LOGS, "LIMIT001.TXT", "");
    show(LOG_TAB_USER);
    tap(OPEN_X, FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_TEXT);
    const log_text_t *t = log_viewer_text();
    CHECK(t != NULL && t->rows == 0 && !t->cut);
    send(TOUCH_EVENT_DOWN, TX_PANEL_X, 300);
    send(TOUCH_EVENT_MOVE, TX_PANEL_X, 100);
    send(TOUCH_EVENT_UP, TX_PANEL_X, 100);
    tap(BAR_X, BAR_TOP + BAR_H - 2);
    CHECK_EQ(log_viewer_text_first(), 0);
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        CHECK(gfx_text_cells(ui_tr(TX_LOG_TEXT_EMPTY)) <= 86);
        draw();
        CHECK(first_row_pixels(UI_TEXT_DIM) > 0);
        CHECK_EQ(first_row_pixels(UI_TEXT), 0);
    }
    ui_text_set_language(UI_LANG_EN);
    /* A report with text: its first row, and not the empty one. */
    fresh();
    add(CARD_USER_LOGS, "LIMIT001.TXT", "x\n");
    show(LOG_TAB_USER);
    tap(OPEN_X, FOOT_CY);
    draw();
    CHECK(first_row_pixels(UI_TEXT) > 0);
    CHECK_EQ(first_row_pixels(UI_TEXT_DIM), 0);
}

TEST_CASE(deleting_a_report_removes_that_file_and_drops_its_text)
{
    fresh();
    add(CARD_USER_LOGS, "LIMIT005.TXT", k_limit_text);
    add(CARD_USER_LOGS, "ATEST004.CSV", k_csv_text);
    show(LOG_TAB_USER);
    tap(OPEN_X, FOOT_CY);
    CHECK(log_viewer_text() != NULL);
    tap(TX_BACK_X, FOOT_CY);
    tap(DELETE_X, FOOT_CY);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_STR_EQ(g_removed, "LIMIT005.TXT");
    CHECK(log_viewer_text() == NULL);
    CHECK_STR_EQ(log_viewer_open_name(), "");
    char list[128];
    rows(list, sizeof(list));
    CHECK_STR_EQ(list, "ATEST004.CSV");
    /* Opening a CSV after a report drops the report's text. */
    fresh();
    add(CARD_USER_LOGS, "LIMIT005.TXT", k_limit_text);
    add(CARD_USER_LOGS, "ATEST004.CSV", k_csv_text);
    show(LOG_TAB_USER);
    tap(OPEN_X, FOOT_CY);
    tap(TX_BACK_X, FOOT_CY);
    tap(400, BR_ROW_Y(1));
    tap(OPEN_X, FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    CHECK(log_viewer_text() == NULL);
    screen()->reset();
    CHECK(log_viewer_text() == NULL);
}

/* --------------------------------------------- the cap, the cost, failures --- */

/** @brief Load @p text into @p t at @p cols cells a row.
 *  @param t     the text
 *  @param text  the file
 *  @param cols  cells a row
 *  @return the load's result */
static log_err_t load(log_text_t *t, const char *text, int cols)
{
    log_mem_ctx_t ctx;
    log_source_t src;
    log_source_memory(&src, &ctx, text, strlen(text));
    return log_text_load(t, &src, cols);
}

static int g_alloc_refuse;
static void *refusing_alloc(size_t n)
{
    if (g_alloc_refuse > 0 && --g_alloc_refuse == 0) {
        return NULL;
    }
    return malloc(n);
}

static bool g_read_fails;
static size_t failing_read(void *ctx, char *buf, size_t max)
{
    (void)ctx;
    (void)max;
    buf[0] = 'x';
    return 1u;
}
static bool failing_error(void *ctx)
{
    (void)ctx;
    return g_read_fails;
}

TEST_CASE(a_report_over_the_cap_is_cut_and_the_view_says_so)
{
    /* LOG_TEXT_MAX bytes and one more: the cap, cut, and a character the
     * cap would split goes whole. */
    static char big[LOG_TEXT_MAX + 64];
    memset(big, 'a', sizeof(big) - 1u);
    for (size_t i = 80u; i < sizeof(big) - 1u; i += 81u) {
        big[i] = '\n';
    }
    big[sizeof(big) - 1u] = '\0';
    log_text_t t;
    memset(&t, 0, sizeof(t));
    CHECK_EQ(load(&t, big, 86), LOG_OK);
    CHECK(t.cut);
    CHECK_EQ(t.len, (size_t)LOG_TEXT_MAX);
    /* "ü" across the cap: dropped, not halved. */
    memcpy(big + LOG_TEXT_MAX - 1u, "\xC3\xBC", 2u);
    CHECK_EQ(load(&t, big, 86), LOG_OK);
    CHECK_EQ(t.len, (size_t)LOG_TEXT_MAX - 1u);
    /* Whole at the cap's edge: kept. */
    memset(big, 'a', sizeof(big) - 1u);
    memcpy(big + LOG_TEXT_MAX - 2u, "\xC3\xBC", 2u);
    CHECK_EQ(load(&t, big, 86), LOG_OK);
    CHECK_EQ(t.len, (size_t)LOG_TEXT_MAX);
    /* Exactly the cap: whole, not cut. */
    big[LOG_TEXT_MAX] = '\0';
    CHECK_EQ(load(&t, big, 86), LOG_OK);
    CHECK(!t.cut);
    log_text_free(&t);

    /* The view: the last row says the cut, in both languages within a
     * row. */
    memset(big, 'a', sizeof(big) - 1u);
    for (size_t i = 80u; i < sizeof(big) - 1u; i += 81u) {
        big[i] = '\n';
    }
    big[sizeof(big) - 1u] = '\0';
    fresh();
    add(CARD_USER_LOGS, "LIMIT001.TXT", big);
    show(LOG_TAB_USER);
    tap(OPEN_X, FOOT_CY);
    const log_text_t *v = log_viewer_text();
    CHECK(v != NULL && v->cut);
    if (v == NULL) {
        return;
    }
    send(TOUCH_EVENT_DOWN, TX_PANEL_X, 330);
    send(TOUCH_EVENT_MOVE, TX_PANEL_X, -20000);
    send(TOUCH_EVENT_UP, TX_PANEL_X, -20000);
    CHECK_EQ(log_viewer_text_first(), v->rows + 1 - TX_ROWS_SEEN);
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        char line[128];
        snprintf(line, sizeof(line), ui_tr(TX_LOG_TEXT_CUT),
                 (unsigned)(LOG_TEXT_MAX / 1024u));
        CHECK(gfx_text_cells(line) <= 86);
        draw();
    }
    ui_text_set_language(UI_LANG_EN);
}

/*
 * A frame draws 14 rows.  It reads those and at most LOG_TEXT_MARK - 1
 * rows before them, wherever the text is scrolled to: the bottom of a
 * 32 KiB report costs what its top does, not the 32 KiB above it.
 */
/** @brief Bytes the TEXT view's row lookups read to draw one frame.
 *  @return the bytes, 0 with no report open */
static size_t frame_reads(void)
{
    const log_text_t *t = log_viewer_text();
    const size_t was = (t != NULL) ? t->walked : 0u;
    draw();
    t = log_viewer_text();
    return (t != NULL) ? t->walked - was : 0u;
}

TEST_CASE(a_frame_reads_its_rows_and_not_the_text_above_them)
{
    static char big[LOG_TEXT_MAX + 64];
    memset(big, 'a', sizeof(big) - 1u);
    for (size_t i = 80u; i < sizeof(big) - 1u; i += 81u) {
        big[i] = '\n';
    }
    big[sizeof(big) - 1u] = '\0';
    fresh();
    add(CARD_USER_LOGS, "LIMIT001.TXT", big);
    show(LOG_TAB_USER);
    tap(OPEN_X, FOOT_CY);
    const log_text_t *v = log_viewer_text();
    CHECK(v != NULL);
    if (v == NULL) {
        return;
    }
    CHECK(v->rows > 400);
    /* 81 bytes a row read, the line feed counted. */
    const size_t most = (size_t)(LOG_TEXT_MARK - 1 + TX_ROWS_SEEN) * 82u;
    const size_t top = frame_reads();
    CHECK(top > 0u && top <= most);
    /* The bottom, and every scroll position on the way back up. */
    send(TOUCH_EVENT_DOWN, TX_PANEL_X, 330);
    send(TOUCH_EVENT_MOVE, TX_PANEL_X, -20000);
    send(TOUCH_EVENT_UP, TX_PANEL_X, -20000);
    CHECK_EQ(log_viewer_text_first(), v->rows + 1 - TX_ROWS_SEEN);
    CHECK(frame_reads() <= most);
    for (int y = 100; y <= 116; ++y) {
        send(TOUCH_EVENT_DOWN, TX_PANEL_X, 100);
        send(TOUCH_EVENT_MOVE, TX_PANEL_X, y + 18);
        send(TOUCH_EVENT_UP, TX_PANEL_X, y + 18);
        CHECK(frame_reads() <= most);
    }
    /* Rows asked in any order are the rows of a walk from the top. */
    log_text_t t;
    memset(&t, 0, sizeof(t));
    CHECK_EQ(load(&t, numbered_lines(100), 86), LOG_OK);
    for (int i = 99; i >= 0; i -= 7) {
        char row[32];
        char want[32];
        CHECK(log_text_row(&t, i, row, sizeof(row)));
        snprintf(want, sizeof(want), "line %d", i + 1);
        CHECK_STR_EQ(row, want);
    }
    log_text_free(&t);
}

TEST_CASE(a_load_that_fails_holds_nothing)
{
    log_text_t t;
    memset(&t, 0, sizeof(t));
    CHECK_EQ(load(&t, "abc", 0), LOG_ERR_ARG);
    CHECK(t.buf == NULL);
    CHECK_EQ(log_text_load(&t, NULL, 9), LOG_ERR_ARG);
    CHECK_EQ(log_text_load(NULL, NULL, 9), LOG_ERR_ARG);
    g_alloc_refuse = 1;
    log_text_set_allocator(refusing_alloc, NULL);
    CHECK_EQ(load(&t, "abc", 9), LOG_ERR_MEMORY);
    CHECK(t.buf == NULL);
    /* The text's buffer given and the marks' not: nothing held, the
     * buffer given back (the sanitizer build sees a leak). */
    g_alloc_refuse = 2;
    CHECK_EQ(load(&t, "abc", 9), LOG_ERR_MEMORY);
    CHECK(t.buf == NULL && t.marks == NULL);
    g_alloc_refuse = 0;
    log_text_set_allocator(NULL, NULL);
    /* A read that fails after some bytes: nothing held. */
    g_read_fails = true;
    log_source_t src = { failing_read, NULL, failing_error, NULL, NULL, NULL };
    /* failing_read never ends; the cap ends it, and the error is seen. */
    CHECK_EQ(log_text_load(&t, &src, 9), LOG_ERR_READ);
    CHECK(t.buf == NULL);
    g_read_fails = false;
    CHECK_EQ(log_text_load(&t, &src, 9), LOG_OK);
    CHECK(t.cut);
    log_text_free(&t);

    /* In the view: the reason in the footer, and the list stays. */
    fresh();
    add(CARD_USER_LOGS, "LIMIT005.TXT", k_limit_text);
    show(LOG_TAB_USER);
    g_alloc_refuse = 1;
    log_text_set_allocator(refusing_alloc, NULL);
    tap(OPEN_X, FOOT_CY);
    log_text_set_allocator(NULL, NULL);
    g_alloc_refuse = 0;
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    CHECK_STR_EQ(log_viewer_message(), ui_tr(TX_LOG_ERR_MEMORY));
}

int main(void)
{
    RUN(the_notes_list_csv_files_and_reports_standing_alone);
    RUN(a_report_and_a_run_of_one_prefix_and_number_are_told_apart);
    RUN(a_lone_limit_report_is_listed_and_chosen_as_the_newest);
    RUN(a_lone_servo_report_of_an_older_card_is_listed_in_the_root);
    RUN(a_run_with_its_report_lists_and_opens_as_before);
    RUN(a_card_of_lone_reports_only_lists_them_newest_first);
    RUN(reports_sort_among_the_runs_by_their_number);
    RUN(pages_of_a_full_folder_hold_every_listed_name_once_in_order);
    RUN(opening_a_report_shows_its_rows_and_back_returns_to_the_list);
    RUN(a_long_report_scrolls_by_drag_and_by_its_bar_within_its_rows);
    RUN(a_report_that_does_not_open_stays_on_the_list_with_the_reason);
    RUN(an_empty_report_opens_on_one_row_that_says_so);
    RUN(deleting_a_report_removes_that_file_and_drops_its_text);
    RUN(a_report_over_the_cap_is_cut_and_the_view_says_so);
    RUN(a_frame_reads_its_rows_and_not_the_text_above_them);
    RUN(a_load_that_fails_holds_nothing);
    free(s_fb);
    return test_summary("logview_report");
}
