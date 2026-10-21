/*
 * Host unit tests for the log viewer's folder tabs.
 *
 * Tested is which folder each tab lists, the tab of the file written last,
 * and what each empty folder says.
 *
 * SPDX-License-Identifier: MIT
 */

#include "logview_rig.h"

/* ------------------------------------------------- folders and tabs --- */

/*
 * A card of folders, as panel_card.c serves it: folder() points the calls
 * that follow, list() walks that folder's .csv files and leaves out what
 * the logger still writes there (log_marks_busy()), remove() takes a
 * run's report with it (log_name_report_of()), written() counts the files
 * the logger created.
 */
#define FC_MAX 24

static struct {
    struct { char dir[CARD_DIR_MAX]; char name[LOG_RUN_NAME_MAX + 8];
             bool gone; } e[FC_MAX];
    int n;
    char dir[CARD_DIR_MAX];          /* where folder() pointed last  */
    char opened_in[CARD_DIR_MAX];    /* where the last open() looked */
    char removed_in[CARD_DIR_MAX];
    int folder_calls;
    log_marks_t marks;
    unsigned written;
    log_viewer_tab_t written_tab;
} g_fc;

static void fc_put(const char *dir, const char *name)
{
    if (g_fc.n < FC_MAX) {
        snprintf(g_fc.e[g_fc.n].dir, sizeof(g_fc.e[0].dir), "%s", dir);
        snprintf(g_fc.e[g_fc.n].name, sizeof(g_fc.e[0].name), "%s", name);
        g_fc.e[g_fc.n].gone = false;
        ++g_fc.n;
    }
}

static int fc_find(const char *dir, const char *name)
{
    for (int i = 0; i < g_fc.n; ++i) {
        if (!g_fc.e[i].gone && log_name_case_cmp(g_fc.e[i].dir, dir) == 0
            && log_name_case_cmp(g_fc.e[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static void fc_folder(const char *dir, void *ctx)
{
    (void)ctx;
    ++g_fc.folder_calls;
    snprintf(g_fc.dir, sizeof(g_fc.dir), "%s", dir);
}

static int fc_list(log_viewer_file_t *out, int max_entries,
                   log_viewer_page_t *page, void *ctx)
{
    (void)ctx;
    log_select_page_t sel;
    log_select_page_begin(&sel, out, max_entries, page);
    int n = 0;
    for (int i = 0; i < g_fc.n; ++i) {
        const char *name = g_fc.e[i].name;
        const size_t len = strlen(name);
        if (g_fc.e[i].gone || strcmp(g_fc.e[i].dir, g_fc.dir) != 0
            || len < 4u || log_name_case_cmp(name + len - 4u, ".csv") != 0
            || log_marks_busy(&g_fc.marks, g_fc.dir, name)) {
            continue;
        }
        log_viewer_file_t f;
        memset(&f, 0, sizeof(f));
        snprintf(f.name, sizeof(f.name), "%s", name);
        f.size = 2048u;
        log_select_page_offer(&sel, &f);
        ++n;
    }
    (void)log_select_page_end(&sel, page);
    return n;
}

static bool fc_open(const char *name, log_source_t *src, void *ctx)
{
    (void)ctx;
    snprintf(g_fc.opened_in, sizeof(g_fc.opened_in), "%s", g_fc.dir);
    if (fc_find(g_fc.dir, name) < 0) {
        return false;
    }
    log_source_memory(src, &g_ctx, k_en, strlen(k_en));
    return true;
}

static bool fc_remove(const char *name, void *ctx)
{
    (void)ctx;
    snprintf(g_fc.removed_in, sizeof(g_fc.removed_in), "%s", g_fc.dir);
    if (log_marks_busy(&g_fc.marks, g_fc.dir, name)) {
        return false;
    }
    const int i = fc_find(g_fc.dir, name);
    if (i < 0) {
        return false;
    }
    g_fc.e[i].gone = true;
    char txt[LOG_RUN_NAME_MAX];
    log_name_report_of(txt, sizeof(txt), name);
    const int t = (txt[0] != '\0') ? fc_find(g_fc.dir, txt) : -1;
    if (t >= 0) {
        g_fc.e[t].gone = true;
    }
    return true;
}

static unsigned fc_written(log_viewer_tab_t *tab, void *ctx)
{
    (void)ctx;
    *tab = g_fc.written_tab;
    return g_fc.written;
}

static const log_viewer_io_t k_fc_io = {
    .list    = fc_list,
    .open    = fc_open,
    .close   = NULL,
    .volume  = fake_volume,
    .remove  = fc_remove,
    .folder  = fc_folder,
    .written = fc_written,
    .ctx     = NULL,
};

/* The tabs' middles, in the header row (BR_TABS in the screen).  The row
 * runs ROOT, USER, BENCH: the position of a tab id is one more than the id,
 * modulo 3 (USER 0, BENCH 1, ROOT 2). */
#define TAB_Y       21

#define TAB_POS(t)  (((t) + 1) % 3)

#define TAB_X(t)    (19 + TAB_POS(t) * 84 + 40)

/* A card in the target layout, behind the folder I/O, LOGS just opened. */
static void folder_card(void)
{
    fresh();
    memset(&g_fc, 0, sizeof(g_fc));
    fc_put(CARD_AUTO_LOGS, "MOTOR004.CSV");
    fc_put(CARD_AUTO_LOGS, "SERVO005.CSV");
    fc_put(CARD_AUTO_LOGS, "SUPPL003.CSV");
    fc_put(CARD_USER_LOGS, "ATEST005.CSV");
    fc_put(CARD_USER_LOGS, "ATEST005.TXT");
    fc_put(CARD_USER_LOGS, "LIMIT006.TXT");
    fc_put(CARD_USER_LOGS, "ATEST002.CSV");
    fc_put(CARD_ROOT, "BENCH041.CSV");          /* firmware 0.21.0 */
    fc_put(CARD_ROOT, "SERVO042.CSV");
    fc_put(CARD_ROOT, "SERVO042.TXT");
    fc_put(CARD_ROOT, "ESC040.CSV");
    fc_put(CARD_ROOT, "SWEEP_920KV.CSV");
    screen()->reset();
    log_viewer_set_io(&k_fc_io);
    screen()->enter();
}

/* Whether the folder the screen last pointed at lists @p name: the list
 * read again through the I/O, as the screen reads it. */
static bool listed_now(const char *name)
{
    log_viewer_file_t files[LOG_VIEWER_MAX_FILES];
    log_viewer_page_t q;
    memset(&q, 0, sizeof(q));
    q.dir = LOG_PAGE_FROM;
    const int n = fc_list(files, LOG_VIEWER_MAX_FILES, &q, NULL);
    for (int i = 0; i < n && i < q.held; ++i) {
        if (strcmp(files[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

static void reopen(void);

/* The top row's name: opened, read, and back to the list. */
static void top_name(char *out, size_t n)
{
    tap(400, BR_ROW_Y(0));
    tap(400, BR_ROW_Y(0));
    snprintf(out, n, "%s", log_viewer_open_name());
    tap(BACK_X, IM_BTN_CY);
}

TEST_CASE(three_tabs_list_three_folders_and_bench_comes_first)
{
    folder_card();
    char name[LOG_VIEWER_NAME_MAX];
    /* After a start, BENCH: the automatic logs. */
    CHECK_EQ(log_viewer_tab(), LOG_TAB_BENCH);
    CHECK_STR_EQ(g_fc.dir, CARD_AUTO_LOGS);
    check_tab(0, 0, 0);
    top_name(name, sizeof(name));
    CHECK_STR_EQ(name, "SERVO005.CSV");
    CHECK_STR_EQ(g_fc.opened_in, CARD_AUTO_LOGS);

    /* USER: the tests, newest first; a report is not listed. */
    tap(TAB_X(0), TAB_Y);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_USER);
    CHECK_STR_EQ(g_fc.dir, CARD_USER_LOGS);
    top_name(name, sizeof(name));
    CHECK_STR_EQ(name, "ATEST005.CSV");
    CHECK_STR_EQ(g_fc.opened_in, CARD_USER_LOGS);
    CHECK(!listed_now("ATEST005.TXT"));
    CHECK(!listed_now("LIMIT006.TXT"));

    /* ROOT: 0.21.0's names, ranked by number across the prefixes; a file
     * nobody numbered after them. */
    tap(TAB_X(2), TAB_Y);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_ROOT);
    CHECK_STR_EQ(g_fc.dir, CARD_ROOT);
    top_name(name, sizeof(name));
    CHECK_STR_EQ(name, "SERVO042.CSV");
    CHECK(listed_now("BENCH041.CSV"));
    CHECK(listed_now("ESC040.CSV"));
    CHECK(listed_now("SWEEP_920KV.CSV"));

    /* RESCAN reads the tab on screen; a tap on the tab shown reads
     * nothing new. */
    const int calls = g_fc.folder_calls;
    tap(RESCAN_X, FOOT_CY);
    CHECK_STR_EQ(g_fc.dir, CARD_ROOT);
    CHECK(g_fc.folder_calls > calls);
    tap(TAB_X(2), TAB_Y);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_ROOT);

    /* A slide off the tab is no tap on it. */
    send(TOUCH_EVENT_DOWN, TAB_X(1), TAB_Y);
    send(TOUCH_EVENT_UP, TAB_X(1), TAB_Y + 200);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_ROOT);
    /* A cancelled press leaves the tab as it was. */
    send(TOUCH_EVENT_DOWN, TAB_X(1), TAB_Y);
    screen()->cancel();
    send(TOUCH_EVENT_UP, TAB_X(1), TAB_Y);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_ROOT);
    tap(TAB_X(1), TAB_Y);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_BENCH);
    draw();
}

/* The tabs stand ROOT, USER, BENCH from the left: a tap at each position
 * chooses that folder, and the tab ids and their folders are as they were. */
TEST_CASE(the_tabs_stand_root_user_bench_from_the_left)
{
    folder_card();
    CHECK_EQ(log_viewer_tab(), LOG_TAB_BENCH);
    /* The selected tab is the right-most one, BENCH, at a start. */
    draw();
    const gfx_color_t on = gfx_pixel_get(&s_c, 19 + 2 * 84 + 6, TAB_Y);
    CHECK(gfx_pixel_get(&s_c, 19 + 0 * 84 + 6, TAB_Y) != on);
    CHECK(gfx_pixel_get(&s_c, 19 + 1 * 84 + 6, TAB_Y) != on);

    tap(19 + 0 * 84 + 40, TAB_Y);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_ROOT);
    CHECK_STR_EQ(g_fc.dir, CARD_ROOT);
    tap(19 + 1 * 84 + 40, TAB_Y);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_USER);
    CHECK_STR_EQ(g_fc.dir, CARD_USER_LOGS);
    tap(19 + 2 * 84 + 40, TAB_Y);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_BENCH);
    CHECK_STR_EQ(g_fc.dir, CARD_AUTO_LOGS);

    /* Ids unchanged, and an id set from outside draws its own position. */
    CHECK_EQ((int)LOG_TAB_USER, 0);
    CHECK_EQ((int)LOG_TAB_BENCH, 1);
    CHECK_EQ((int)LOG_TAB_ROOT, 2);
    log_viewer_set_tab(LOG_TAB_ROOT);
    draw();
    const gfx_color_t on_root = gfx_pixel_get(&s_c, 19 + 6, TAB_Y);
    CHECK(gfx_pixel_get(&s_c, 19 + 84 + 6, TAB_Y) != on_root);
    CHECK(gfx_pixel_get(&s_c, 19 + 2 * 84 + 6, TAB_Y) != on_root);
}

/* The right-most column of the header row (y 2 to 40, right of the tabs)
 * that differs between a card named @p a and one named @p b, or -1. */
static int name_edge(const char *a, const char *b, int runs)
{
    static gfx_color_t *first;
    if (first == NULL) {
        first = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(first);
    }
    runs_card(runs);
    g_volume = a;
    log_viewer_refresh();
    draw();
    memcpy(first, s_fb, (size_t)W * H * sizeof(gfx_color_t));
    runs_card(runs);
    g_volume = b;
    log_viewer_refresh();
    draw();
    int edge = -1;
    for (int y = 2; y < 40; ++y) {
        for (int x = 270; x < W; ++x) {
            if (first[y * W + x] != s_fb[y * W + x]) {
                edge = (x > edge) ? x : edge;
            }
        }
    }
    return edge;
}

TEST_CASE(the_card_name_ends_the_header_row_and_stops_short_of_prev)
{
    /* The last letter differs: the difference is the name's right end.
     * Without PREV and NEXT it ends at the list's right edge, 784 px. */
    int edge = name_edge("UNITEST", "UNITESX", 3);
    CHECK(edge >= 784 - 8 && edge < 784);
    /* Exactly one page: still no PREV and NEXT. */
    edge = name_edge("UNITEST", "UNITESX", LOG_VIEWER_MAX_FILES);
    CHECK(edge >= 784 - 8 && edge < 784);
    /* With PREV and NEXT showing (more than 48 runs) it ends 8 px short
     * of PREV, which starts at 536 px. */
    edge = name_edge("UNITEST", "UNITESX", 150);
    CHECK(edge >= 528 - 8 && edge < 528);
    /* The longest label, 11 characters, and the longest folder share the
     * 248 px to PREV: the name's left end stays right of the folder's. */
    edge = name_edge("ABCDEFGHIJK", "ABCDEFGHIJX", 150);
    CHECK(edge >= 528 - 8 && edge < 528);
}

/*
 * With no card the tabs switch as with one: each tap selects its tab, the
 * tab is drawn selected and the others not, the page names that tab's
 * folder, and the empty page says there is no card.  The card is asked
 * again with each tab, so a card put in is found as RESCAN finds it.
 */
TEST_CASE(the_tabs_switch_with_no_card)
{
    fresh();
    g_no_card = true;
    log_viewer_refresh();
    for (int round = 0; round < 2; ++round) {
        for (int t = 0; t < (int)LOG_TAB_COUNT; ++t) {
            const int calls = g_list_calls;
            tap(TAB_X(t), TAB_Y);
            CHECK_EQ(log_viewer_tab(), (log_viewer_tab_t)t);
            CHECK_EQ(g_list_calls, calls + 1);
            CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
            draw();
            /* The selected tab's fill against the other two's, 6 px in
             * from each tab's left edge. */
            const gfx_color_t on = gfx_pixel_get(&s_c, 19 + TAB_POS(t) * 84 + 6,
                                                 TAB_Y);
            for (int o = 0; o < (int)LOG_TAB_COUNT; ++o) {
                if (o != t) {
                    CHECK(gfx_pixel_get(&s_c, 19 + TAB_POS(o) * 84 + 6,
                                        TAB_Y) != on);
                }
            }
        }
    }
    /* A card put in: the next tab lists it. */
    g_no_card = false;
    tap(TAB_X(1), TAB_Y);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_BENCH);
    draw();
}

/*
 * USER and BENCH list the folders SETUP > SD CARD names in force: a new
 * folder's files, and not the files left in the folder before.  Both
 * kinds in one folder list the same files.  A folder the check refuses
 * lists the default, as the logger writes there; lower case is upper.
 */
TEST_CASE(the_tabs_follow_the_folders_of_setup)
{
    folder_card();
    fc_put("RUNS", "SERVO012.CSV");
    fc_put("RUNS", "ATEST013.CSV");
    settings_set_text(SET_TEXT_LOG_AUTO_DIR, "RUNS");
    screen()->enter();                          /* LOGS opened again */
    CHECK_EQ(log_viewer_tab(), LOG_TAB_BENCH);
    CHECK_STR_EQ(log_viewer_tab_dir(LOG_TAB_BENCH), "RUNS");
    CHECK_STR_EQ(g_fc.dir, "RUNS");
    CHECK(listed_now("SERVO012.CSV"));
    CHECK(!listed_now("MOTOR004.CSV"));         /* left in BENCH/LOGS */
    char name[LOG_VIEWER_NAME_MAX];
    top_name(name, sizeof(name));
    CHECK_STR_EQ(name, "ATEST013.CSV");
    CHECK_STR_EQ(g_fc.opened_in, "RUNS");
    tap(TAB_X(0), TAB_Y);
    CHECK_STR_EQ(g_fc.dir, CARD_USER_LOGS);

    /* Both kinds in one folder: USER lists what BENCH lists. */
    settings_set_text(SET_TEXT_LOG_USER_DIR, "runs");
    CHECK_STR_EQ(log_viewer_tab_dir(LOG_TAB_USER), "RUNS");
    tap(RESCAN_X, FOOT_CY);
    CHECK_STR_EQ(g_fc.dir, "RUNS");
    CHECK(listed_now("SERVO012.CSV"));
    CHECK(listed_now("ATEST013.CSV"));

    /* A folder the check refuses is the default. */
    settings_set_text(SET_TEXT_LOG_USER_DIR, "A/B/C");
    CHECK_STR_EQ(log_viewer_tab_dir(LOG_TAB_USER), CARD_USER_LOGS);
    settings_set_text(SET_TEXT_LOG_AUTO_DIR, "BENCH/KST");
    CHECK_STR_EQ(log_viewer_tab_dir(LOG_TAB_BENCH), CARD_AUTO_LOGS);
    CHECK_STR_EQ(log_viewer_tab_dir(LOG_TAB_ROOT), CARD_ROOT);
    CHECK_STR_EQ(log_viewer_tab_dir(LOG_TAB_COUNT), CARD_ROOT);
    /* The longest folder fits the header. */
    settings_set_text(SET_TEXT_LOG_AUTO_DIR, "ABCDEFGH/12345678");
    tap(TAB_X(1), TAB_Y);
    CHECK_STR_EQ(g_fc.dir, "ABCDEFGH/12345678");
    draw();
    settings_set_text(SET_TEXT_LOG_AUTO_DIR, CARD_AUTO_LOGS);
    settings_set_text(SET_TEXT_LOG_USER_DIR, CARD_USER_LOGS);
}

TEST_CASE(delete_on_root_takes_the_report_and_stays_in_its_folder)
{
    folder_card();
    tap(TAB_X(2), TAB_Y);
    tap(DELETE_X, FOOT_CY);         /* SERVO042.CSV, the newest, chosen */
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_STR_EQ(g_fc.removed_in, CARD_ROOT);
    CHECK(fc_find(CARD_ROOT, "SERVO042.CSV") < 0);
    CHECK(fc_find(CARD_ROOT, "SERVO042.TXT") < 0);
    CHECK(fc_find(CARD_ROOT, "BENCH041.CSV") >= 0);
    /* The tab and its folder stay. */
    CHECK_EQ(log_viewer_tab(), LOG_TAB_ROOT);
    char name[LOG_VIEWER_NAME_MAX];
    top_name(name, sizeof(name));
    CHECK_STR_EQ(name, "BENCH041.CSV");

    /* In USER, a test takes its report and leaves the limit run's. */
    tap(TAB_X(0), TAB_Y);
    tap(DELETE_X, FOOT_CY);         /* ATEST005.CSV, chosen */
    tap(DQ_DELETE_X, DQ_CY);
    CHECK_STR_EQ(g_fc.removed_in, CARD_USER_LOGS);
    CHECK(fc_find(CARD_USER_LOGS, "ATEST005.TXT") < 0);
    CHECK(fc_find(CARD_USER_LOGS, "LIMIT006.TXT") >= 0);
}

TEST_CASE(an_open_test_in_user_does_not_hide_the_same_number_in_bench)
{
    folder_card();
    g_fc.marks = (log_marks_t){ .run_dir = CARD_AUTO_LOGS, .run = 0u,
                                .test_dir = CARD_USER_LOGS, .test = 5u,
                                .txt = 0u };
    tap(RESCAN_X, FOOT_CY);
    char name[LOG_VIEWER_NAME_MAX];
    top_name(name, sizeof(name));
    CHECK_STR_EQ(name, "SERVO005.CSV");      /* BENCH 005 is listed */
    tap(TAB_X(0), TAB_Y);
    top_name(name, sizeof(name));
    CHECK_STR_EQ(name, "ATEST002.CSV");      /* USER 005 is held back */
    /* And the open run in BENCH holds back BENCH 005 alone. */
    g_fc.marks = (log_marks_t){ .run_dir = CARD_AUTO_LOGS, .run = 5u,
                                .test_dir = CARD_USER_LOGS, .test = 0u,
                                .txt = 0u };
    tap(RESCAN_X, FOOT_CY);
    top_name(name, sizeof(name));
    CHECK_STR_EQ(name, "ATEST005.CSV");
    tap(TAB_X(1), TAB_Y);
    top_name(name, sizeof(name));
    CHECK_STR_EQ(name, "MOTOR004.CSV");
}

/* The empty list's two lines, as drawn: the reason at box.y + 52 and what
 * the folder holds at box.y + 76. */
static void empty_lines_drawn(gfx_color_t *out)
{
    draw();
    int k = 0;
    for (int y = 205; y < 250; ++y) {
        for (int x = 140; x < 660; ++x) {
            out[k++] = s_fb[y * W + x];
        }
    }
}

#define EMPTY_PX (45 * 520)

TEST_CASE(each_empty_folder_says_what_it_holds_and_what_is_off)
{
    static gfx_color_t a[EMPTY_PX], b[EMPTY_PX], c[EMPTY_PX], d[EMPTY_PX],
                       e[EMPTY_PX];
    folder_card();
    for (int i = 0; i < g_fc.n; ++i) {
        g_fc.e[i].gone = true;              /* every folder empty */
    }
    tap(RESCAN_X, FOOT_CY);
    empty_lines_drawn(a);                   /* BENCH */
    tap(TAB_X(0), TAB_Y);
    empty_lines_drawn(b);                   /* USER */
    tap(TAB_X(2), TAB_Y);
    empty_lines_drawn(c);                   /* ROOT */
    CHECK(memcmp(a, b, sizeof(a)) != 0);
    CHECK(memcmp(b, c, sizeof(b)) != 0);
    CHECK(memcmp(a, c, sizeof(a)) != 0);

    /* AUTO LOGS off: BENCH says where to switch it on. */
    settings_set(SET_LOG_AUTO, 0.0f);
    tap(TAB_X(1), TAB_Y);
    empty_lines_drawn(d);
    CHECK(memcmp(a, d, sizeof(a)) != 0);
    /* USER LOGS off: USER likewise. */
    settings_set(SET_LOG_USER, 0.0f);
    tap(TAB_X(0), TAB_Y);
    empty_lines_drawn(e);
    CHECK(memcmp(b, e, sizeof(b)) != 0);
    CHECK(memcmp(d, e, sizeof(d)) != 0);
    settings_init();

    /* The words, through the same texts. */
    char line[96];
    snprintf(line, sizeof(line), TR(LOG_NO_CSV_IN), CARD_AUTO_LOGS);
    CHECK_STR_EQ(line, "No .csv files in BENCH/LOGS.");
}

TEST_CASE(logs_opens_on_the_tab_of_the_file_written_last)
{
    ui_router_init();
    folder_card();
    ui_router_goto(SCREEN_LOGS);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_BENCH);       /* nothing written */

    /* A test written: LOGS opens on USER. */
    g_fc.written = 1u;
    g_fc.written_tab = LOG_TAB_USER;
    reopen();
    CHECK_EQ(log_viewer_tab(), LOG_TAB_USER);
    CHECK_STR_EQ(g_fc.dir, CARD_USER_LOGS);

    /* Nothing written since: the tab the operator chose stays. */
    tap(TAB_X(2), TAB_Y);
    reopen();
    CHECK_EQ(log_viewer_tab(), LOG_TAB_ROOT);

    /* A run written: BENCH. */
    g_fc.written = 2u;
    g_fc.written_tab = LOG_TAB_BENCH;
    reopen();
    CHECK_EQ(log_viewer_tab(), LOG_TAB_BENCH);

    /* The count wraps: a change is a change. */
    g_fc.written = 0u;
    g_fc.written_tab = LOG_TAB_USER;
    reopen();
    CHECK_EQ(log_viewer_tab(), LOG_TAB_USER);

    /* A tab outside the three is not taken. */
    g_fc.written = 5u;
    g_fc.written_tab = LOG_TAB_COUNT;
    reopen();
    CHECK_EQ(log_viewer_tab(), LOG_TAB_USER);
    log_viewer_set_tab(LOG_TAB_COUNT);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_USER);
}

TEST_CASE(a_file_opened_in_one_tab_is_read_again_from_its_folder)
{
    ui_router_init();
    folder_card();
    ui_router_goto(SCREEN_LOGS);
    tap(TAB_X(0), TAB_Y);
    tap(400, BR_ROW_Y(0));
    tap(400, BR_ROW_Y(0));                   /* ATEST005.CSV, imported */
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    CHECK_STR_EQ(log_viewer_open_name(), "ATEST005.CSV");
    /* Left with the import on screen; a run is written meanwhile, so LOGS
     * comes back on BENCH with the import still showing. */
    g_fc.written = 1u;
    g_fc.written_tab = LOG_TAB_BENCH;
    reopen();
    CHECK_EQ(log_viewer_tab(), LOG_TAB_BENCH);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    /* A forced separator reads the file again: from USER/LOGS. */
    tap(SEP_X, IM_BTN_CY);
    CHECK_STR_EQ(g_fc.opened_in, CARD_USER_LOGS);
    CHECK(log_viewer_analysis() != NULL);
}

/* ------------------------------------------------------ the way back in --- */

/* LOGS left for the menu and opened again from it, through the router. */
static void reopen(void)
{
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_LOGS);
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
}

/*
 * A run written while LOGS is closed is on the list the next time it opens,
 * without RESCAN: the first page, the new run at its top and chosen.  One
 * listing per way in, whichever page was on screen when it was left.
 */
TEST_CASE(a_run_written_while_logs_is_closed_is_listed_on_the_next_open)
{
    ui_router_init();
    runs_card(60);
    ui_router_goto(SCREEN_LOGS);
    check_tab(1, 48, 60);
    next_page();
    check_tab(49, 60, 60);

    g_card_runs = 61;
    int calls = g_list_calls;
    reopen();
    CHECK_EQ(g_list_calls, calls + 1);
    check_tab(1, 48, 61);
    check_top(61);

    /* Nothing new: the same first page, read once more. */
    calls = g_list_calls;
    reopen();
    CHECK_EQ(g_list_calls, calls + 1);
    check_tab(1, 48, 61);
    check_top(61);

    /* An empty card that gets its first run. */
    g_card_runs = 0;
    g_empty_card = true;
    calls = g_list_calls;
    reopen();
    CHECK_EQ(g_list_calls, calls + 1);
    g_empty_card = false;
    g_card_runs = 1;
    reopen();
    check_tab(0, 0, 0);
    check_top(1);

    /* No card when it was left, a card with runs when it opens. */
    g_no_card = true;
    reopen();
    g_no_card = false;
    g_card_runs = 49;
    reopen();
    check_tab(1, 48, 49);
    check_top(49);
}

/* After a DELETE, after a card swap and after leaving from a file's view or
 * from WI-FI, the next way in lists the run written meanwhile. */
TEST_CASE(the_way_back_in_lists_new_runs_after_delete_swap_and_sub_views)
{
    ui_router_init();
    runs_card(50);
    ui_router_goto(SCREEN_LOGS);

    /* DELETE of the newest run, chosen on the way in, leaves nothing
     * chosen; a new run is chosen on the next way in, the deleted number
     * gone from the list. */
    tap(DELETE_X, FOOT_CY);
    tap(DQ_DELETE_X, DQ_CY);
    CHECK(g_run_gone[50]);
    g_card_runs = 51;
    reopen();
    check_tab(1, 48, 50);
    check_top(51);

    /* Another card in the slot, on page 2 when LOGS was left. */
    next_page();
    g_volume = "UNITEST";
    memset(g_run_gone, 0, sizeof(g_run_gone));
    g_card_run0 = 300;
    g_card_runs = 97;
    reopen();
    check_tab(1, 48, 97);
    check_top(396);

    /* Left from a file's import view: the view is kept and the list
     * behind it is read again. */
    tap(OPEN_X, FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    g_card_runs = 98;
    reopen();
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    tap(BACK_X, IM_BTN_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    check_tab(1, 48, 98);
    check_top(397);

    /* WI-FI opened from LOGS and closed by its home tag, which returns to
     * LOGS. */
    ui_router_goto(SCREEN_WIFI);
    g_card_runs = 99;
    ui_router_goto(ui_router_parent(SCREEN_WIFI));
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
    check_tab(1, 48, 99);
    check_top(398);
}

TEST_CASE(the_header_names_the_card_or_says_sd_card)
{
    /* No name, and the header reads what it reads for a card without one:
     * SD CARD.  A name is drawn instead. */
    static gfx_color_t *named;
    if (named == NULL) {
        named = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(named);
    }
    fresh();
    g_volume = "SD CARD";
    log_viewer_refresh();
    draw();
    memcpy(named, s_fb, (size_t)W * H * sizeof(gfx_color_t));

    fresh();
    g_volume = "";
    log_viewer_refresh();
    draw();
    CHECK(memcmp(named, s_fb, (size_t)W * H * sizeof(gfx_color_t)) == 0);

    fresh();
    g_volume = "UNITEST";
    log_viewer_refresh();
    draw();
    CHECK(memcmp(named, s_fb, (size_t)W * H * sizeof(gfx_color_t)) != 0);
}

int main(void)
{
    RUN(a_run_written_while_logs_is_closed_is_listed_on_the_next_open);
    RUN(the_way_back_in_lists_new_runs_after_delete_swap_and_sub_views);
    RUN(the_header_names_the_card_or_says_sd_card);
    RUN(three_tabs_list_three_folders_and_bench_comes_first);
    RUN(the_tabs_stand_root_user_bench_from_the_left);
    RUN(the_card_name_ends_the_header_row_and_stops_short_of_prev);
    RUN(the_tabs_switch_with_no_card);
    RUN(the_tabs_follow_the_folders_of_setup);
    RUN(delete_on_root_takes_the_report_and_stays_in_its_folder);
    RUN(an_open_test_in_user_does_not_hide_the_same_number_in_bench);
    RUN(each_empty_folder_says_what_it_holds_and_what_is_off);
    RUN(logs_opens_on_the_tab_of_the_file_written_last);
    RUN(a_file_opened_in_one_tab_is_read_again_from_its_folder);
    return test_summary("logview_tabs");
}
