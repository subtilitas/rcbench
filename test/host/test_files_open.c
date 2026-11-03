/**
 * @file test_files_open.c
 * @brief OPEN on FILES' reports: every file the list labels a report, and
 *        every other text file, opens in the text viewer; a CSV file of
 *        LOGS' folders opens in LOGS on its folder's tab; LOGS' own list
 *        holds the same reports and opens them in the same viewer; a file
 *        OPEN refuses says why, truly.
 *
 * LOGS reads the same card in memory as FILES, through a lister that walks
 * a folder of it by the panel's rule (card_take() in panel_card.c): the
 * names log_select_lists() takes, less a file the logger has open.
 *
 * Cases:
 * - every_report_the_list_labels_opens_in_the_viewer
 * - logs_own_list_holds_and_opens_the_same_reports
 * - a_text_file_that_is_no_report_opens_in_the_viewer
 * - a_report_being_written_or_gone_says_so
 * - a_report_finished_off_screen_opens_from_logs
 *
 * SPDX-License-Identifier: MIT
 */
#include "files_rig.h"

#include "files_int.h"

#include "log_name.h"
#include "log_select.h"
#include "log_take.h"
#include "log_viewer_screen.h"
#include "logview_spots.h"
#include "text_view.h"

/* ----------------------------------------------- LOGS over the card --- */

/** @brief The folder LOGS points its calls at. */
static char s_lv_dir[CARD_DIR_MAX];
/** @brief The open file's bytes as LOGS reads them. */
static log_mem_ctx_t s_lv_mem;

static const char k_csv[] = "time,voltage\n0.0,22.3\n0.1,22.2\n0.2,22.1\n";
static const char k_report[] = "RCBENCH SERVO TEST REPORT\nResult: PASS\n";
static const char k_note[] = "a note\n";

/**
 * @brief The name of card entry @p i when it is a file of folder @p dir.
 * @param i    the entry
 * @param dir  the folder, "" for the root
 * @return the name, or NULL for a folder or a file of another folder
 */
static const char *name_in(int i, const char *dir)
{
    const fk_node_t *e = &g_h.card.n[i];
    if (e->dir) {
        return NULL;
    }
    const char *slash = strrchr(e->path, '/');
    const size_t len = (slash != NULL) ? (size_t)(slash - e->path) : 0u;
    if (strlen(dir) != len || strncmp(e->path, dir, len) != 0) {
        return NULL;
    }
    return (slash != NULL) ? slash + 1 : e->path;
}

/** @brief LOGS' folder(): the folder the next call reads. */
static void lv_folder(const char *dir, void *ctx)
{
    (void)ctx;
    snprintf(s_lv_dir, sizeof(s_lv_dir), "%s", (dir != NULL) ? dir : "");
}

/** @brief LOGS' list(): the folder's files by the panel's rule. */
static int lv_list(log_viewer_file_t *out, int max_entries,
                   log_viewer_page_t *page, void *ctx)
{
    (void)ctx;
    log_select_page_t sel;
    log_select_page_begin(&sel, out, max_entries, page);
    int n = 0;
    for (int i = 0; i < g_h.card.count; ++i) {
        const char *name = name_in(i, s_lv_dir);
        if (name == NULL || !log_select_lists(name)
            || log_marks_busy(&g_h.marks, s_lv_dir, name)) {
            continue;
        }
        log_viewer_file_t f;
        memset(&f, 0, sizeof(f));
        snprintf(f.name, sizeof(f.name), "%s", name);
        f.size = g_h.card.n[i].size;
        log_select_page_offer(&sel, &f);
        ++n;
    }
    (void)log_select_page_end(&sel, page);
    return n;
}

/** @brief LOGS' open(): the file's bytes from the card. */
static bool lv_open(const char *name, log_source_t *src, void *ctx)
{
    (void)ctx;
    for (int i = 0; i < g_h.card.count; ++i) {
        const char *here = name_in(i, s_lv_dir);
        if (here != NULL && strcmp(here, name) == 0) {
            log_source_memory(src, &s_lv_mem,
                              (const char *)g_h.card.n[i].data,
                              g_h.card.n[i].size);
            return true;
        }
    }
    return false;
}

/** @brief LOGS' mounts(): the card's mount number. */
static unsigned lv_mounts(void *ctx)
{
    (void)ctx;
    return g_h.card.mount;
}

/** @brief Put @p text at @p path on the card. */
static void put(const char *path, const char *text)
{
    fk_file(&g_h.card, path, (const uint8_t *)text, strlen(text));
}

/**
 * @brief The card of these cases, FILES entered over it and LOGS' I/O on
 *        it: in each of LOGS' three folders a servo automatic test's CSV
 *        with its report, a report whose CSV is missing and a LIMIT RUN's
 *        report; two text files that are no reports in USER/LOGS, and a
 *        report in USER.
 */
static void fresh_reports(void)
{
    fresh_bare();
    fk_dir(&g_h.card, "BENCH/LOGS");
    fk_dir(&g_h.card, "USER/LOGS");
    static const char *const dirs[] = { "USER/LOGS/", "BENCH/LOGS/", "" };
    for (size_t d = 0; d < sizeof(dirs) / sizeof(dirs[0]); ++d) {
        char p[64];
        snprintf(p, sizeof(p), "%sATEST011.CSV", dirs[d]);
        put(p, k_csv);
        snprintf(p, sizeof(p), "%sATEST011.TXT", dirs[d]);
        put(p, k_report);
        snprintf(p, sizeof(p), "%sATEST012.TXT", dirs[d]);
        put(p, k_report);
        snprintf(p, sizeof(p), "%sLIMIT013.TXT", dirs[d]);
        put(p, k_report);
    }
    put("USER/LOGS/NOTES.TXT", k_note);
    put("USER/LOGS/ATEST000.TXT", k_note);
    put("USER/ATEST014.TXT", k_report);
    const log_viewer_io_t lv = { .list = lv_list, .open = lv_open,
                                 .folder = lv_folder, .mounts = lv_mounts };
    log_viewer_set_io(&lv);
    ui_router_goto(SCREEN_SETUP);
    ui_router_goto(SCREEN_FILES);
    settle();
}

/** @brief The kind FILES labels row @p name of pane @p i with. */
static fm_kind_t kind_of(int i, const char *name)
{
    const int r = row_of(i, name);
    return (r >= 0) ? files_screen_pane(i)->page[r].kind : FM_KIND_OTHER;
}

/** @brief The viewer's first row on screen, "" without one. */
static const char *view_row0(void)
{
    static char row[160];
    if (!text_view_row_text(0, row, sizeof(row))) {
        row[0] = '\0';
    }
    return row;
}

/**
 * @brief OPEN on @p name of pane @p i: the text viewer on FILES, the file
 *        read from pane @p i's folder, its first row the report's title;
 *        the home tag back to the panes.
 * @return true when all of it held
 */
static bool views(int i, const char *name)
{
    if (!pick(i, name)) {
        T_FAIL("%s is not on pane %d", name, i);
        return false;
    }
    tap_foot(FS_SPOT_OPEN);
    const bool ok = ui_router_current() == SCREEN_FILES
                    && files_screen_view() == FILES_VIEW_TEXT
                    && text_view_state() == TEXT_VIEW_SHOWN
                    && strcmp(text_view_name(), name) == 0
                    && strcmp(text_view_dir(), files_screen_pane(i)->dir) == 0;
    if (!ok) {
        T_FAIL("OPEN on %s: screen %d view %d viewer %d %s, FILES says "
               "\"%s\"", name, (int)ui_router_current(),
               (int)files_screen_view(), (int)text_view_state(),
               text_view_name(), files_screen_message());
    }
    CHECK(screen()->back());
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    CHECK_EQ(text_view_state(), TEXT_VIEW_CLOSED);
    return ok;
}

/**
 * @brief OPEN on @p name of pane @p i: LOGS, on @p tab, with the CSV in
 *        the import; and back to FILES.
 * @return true when all of it held
 */
static bool opens_in_logs(int i, const char *name, log_viewer_tab_t tab)
{
    if (!pick(i, name)) {
        T_FAIL("%s is not on pane %d", name, i);
        return false;
    }
    tap_foot(FS_SPOT_OPEN);
    const bool ok = ui_router_current() == SCREEN_LOGS
                    && log_viewer_view() == LOG_VIEW_IMPORT
                    && strcmp(log_viewer_open_name(), name) == 0
                    && log_viewer_tab() == tab;
    if (!ok) {
        T_FAIL("OPEN on %s: screen %d view %d name %s tab %d, FILES says "
               "\"%s\"", name, (int)ui_router_current(),
               (int)log_viewer_view(), log_viewer_open_name(),
               (int)log_viewer_tab(), files_screen_message());
    }
    ui_router_goto(SCREEN_FILES);
    settle();
    return ok;
}

/**
 * @brief Every report of one folder of pane @p i labelled a report and
 *        opened in the viewer; the run's CSV in LOGS' import on @p tab.
 */
static void folder_opens(int i, log_viewer_tab_t tab)
{
    static const char *const reports[] = { "ATEST011.TXT", "ATEST012.TXT",
                                           "LIMIT013.TXT" };
    for (size_t k = 0; k < sizeof(reports) / sizeof(reports[0]); ++k) {
        CHECK_EQ(kind_of(i, reports[k]), FM_KIND_REPORT);
        CHECK(views(i, reports[k]));
    }
    CHECK(opens_in_logs(i, "ATEST011.CSV", tab));
}

TEST_CASE(every_report_the_list_labels_opens_in_the_viewer)
{
    fresh_reports();
    /* USER/LOGS in pane 1: the tester's folder. */
    folder_opens(1, LOG_TAB_USER);
    CHECK(pick(1, "ATEST011.TXT"));
    tap_foot(FS_SPOT_OPEN);
    CHECK_STR_EQ(view_row0(), "RCBENCH SERVO TEST REPORT");
    CHECK(screen()->back());
    /* The card's root in pane 0. */
    folder_opens(0, LOG_TAB_ROOT);
    /* BENCH/LOGS. */
    CHECK(open_dir(0, "BENCH"));
    CHECK(open_dir(0, "LOGS"));
    folder_opens(0, LOG_TAB_BENCH);
    log_viewer_set_io(NULL);
}

/** @brief A tap on LOGS at (@p x, @p y), in its own coordinates. */
static void lv_tap(int x, int y)
{
    const ui_screen_t *lv = log_viewer_screen();
    touch_event_t e = {
        .type = TOUCH_EVENT_DOWN,
        .point = { .id = 1u, .x = (int16_t)x, .y = (int16_t)y },
    };
    lv->event(&e);
    e.type = TOUCH_EVENT_UP;
    lv->event(&e);
}

/*
 * LOGS lists the same reports, and OPEN on one shows it in the viewer on
 * FILES, read through FILES' card task from LOGS' folder; the home tag
 * returns to LOGS, and LOGS is where it was.
 */
TEST_CASE(logs_own_list_holds_and_opens_the_same_reports)
{
    fresh_reports();
    ui_router_goto(SCREEN_LOGS);
    log_viewer_set_tab(LOG_TAB_USER);
    /* Newest first, a run's CSV before its report; the notes are not
     * listed. */
    static const char *const want[] = {
        "LIMIT013.TXT", "ATEST012.TXT", "ATEST011.CSV", "ATEST011.TXT",
    };
    const int n = (int)(sizeof(want) / sizeof(want[0]));
    char title[64];
    log_viewer_browse_title(title, sizeof(title));
    CHECK_STR_EQ(title, "FILES");
    for (int r = 0; r < n; ++r) {
        lv_tap(LV_ROW_CX, LV_ROW_CY(r));        /* select */
        lv_tap(LV_OPEN_CX, LV_FOOT_CY);         /* OPEN */
        CHECK_STR_EQ(log_viewer_open_name(), want[r]);
        if (log_report_number(want[r]) < 0) {
            CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
            log_viewer_screen()->back();
            CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
            continue;
        }
        settle();
        CHECK_EQ(ui_router_current(), SCREEN_FILES);
        CHECK_EQ(files_screen_view(), FILES_VIEW_TEXT);
        CHECK_EQ(text_view_state(), TEXT_VIEW_SHOWN);
        CHECK_STR_EQ(text_view_dir(), CARD_USER_LOGS);
        CHECK_STR_EQ(text_view_name(), want[r]);
        CHECK_STR_EQ(view_row0(), "RCBENCH SERVO TEST REPORT");
        /* The panes stay unread: the viewer's read alone went out. */
        CHECK(!files_screen_busy());
        CHECK(screen()->back());
        CHECK_EQ(ui_router_current(), SCREEN_LOGS);
        CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
        CHECK_EQ(log_viewer_tab(), LOG_TAB_USER);
        /* LOGS chose the report again: OPEN opens it, not the newest. */
        lv_tap(LV_OPEN_CX, LV_FOOT_CY);
        settle();
        CHECK_STR_EQ(text_view_name(), want[r]);
        ui_router_goto(SCREEN_LOGS);
    }
    /* Row n is past the list: nothing else is there. */
    lv_tap(LV_ROW_CX, LV_ROW_CY(n));
    lv_tap(LV_OPEN_CX, LV_FOOT_CY);
    CHECK_STR_EQ(log_viewer_open_name(), want[n - 1]);
    log_viewer_set_io(NULL);
}

TEST_CASE(a_text_file_that_is_no_report_opens_in_the_viewer)
{
    fresh_reports();
    /* A note, and ATEST000.TXT: 000 is no run's number. */
    static const char *const notes[] = { "NOTES.TXT", "ATEST000.TXT" };
    for (size_t k = 0; k < sizeof(notes) / sizeof(notes[0]); ++k) {
        CHECK_EQ(kind_of(1, notes[k]), FM_KIND_TEXT);
        CHECK(pick(1, notes[k]));
        CHECK(fs_foot_live(FS_OPEN, NULL));
        tap_foot(FS_SPOT_OPEN);
        CHECK_EQ(files_screen_view(), FILES_VIEW_TEXT);
        CHECK_STR_EQ(view_row0(), "a note");
        CHECK(screen()->back());
    }
    /* A report outside LOGS' three folders: LOGS has no tab for USER,
     * the viewer reads it all the same. */
    CHECK(open_dir(0, "USER"));
    CHECK_EQ(kind_of(0, "ATEST014.TXT"), FM_KIND_REPORT);
    CHECK(views(0, "ATEST014.TXT"));
    log_viewer_set_io(NULL);
}

/*
 * FILES read the bench while a LIMIT RUN wrote its report, the run ended
 * while FILES was off screen, and LOGS opens the report: the viewer reads
 * it, with the bench read again, not the one FILES held.
 */
TEST_CASE(a_report_finished_off_screen_opens_from_logs)
{
    fresh_reports();
    g_h.marks.test = 13u;
    g_h.marks.txt = 13u;
    settle();                           /* FILES holds LIMIT013 as open */
    CHECK(pick(1, "LIMIT013.TXT"));
    fm_err_t why = FM_OK;
    CHECK(!fs_foot_live(FS_OPEN, &why));
    CHECK_EQ(why, FM_ERR_BUSY);
    g_h.marks.test = 0u;                /* the run ends, FILES off screen */
    g_h.marks.txt = 0u;
    ui_router_goto(SCREEN_LOGS);
    log_viewer_set_tab(LOG_TAB_USER);
    /* The newest, LIMIT013.TXT, is chosen on the way in. */
    lv_tap(LV_OPEN_CX, LV_FOOT_CY);
    CHECK_STR_EQ(log_viewer_open_name(), "LIMIT013.TXT");
    settle();
    CHECK_EQ(ui_router_current(), SCREEN_FILES);
    CHECK_EQ(files_screen_view(), FILES_VIEW_TEXT);
    CHECK_EQ(text_view_state(), TEXT_VIEW_SHOWN);
    CHECK_STR_EQ(text_view_name(), "LIMIT013.TXT");
    CHECK_STR_EQ(view_row0(), "RCBENCH SERVO TEST REPORT");
    CHECK(screen()->back());
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
    log_viewer_set_io(NULL);
}

/** @brief The message the table writes for @p id and @p name. */
static const char *msg(ui_text_id_t id, const char *name)
{
    static char out[192];
    snprintf(out, sizeof(out), ui_tr(id), name);
    return out;
}

TEST_CASE(a_report_being_written_or_gone_says_so)
{
    fresh_reports();
    /* The test the logger still writes, report 12 in USER/LOGS: OPEN is
     * shut, and says so. */
    g_h.marks.test = 12u;
    g_h.marks.txt = 12u;
    CHECK(pick(1, "ATEST012.TXT"));
    fm_err_t why = FM_OK;
    CHECK(!fs_foot_live(FS_OPEN, &why));
    CHECK_EQ(why, FM_ERR_BUSY);
    tap_foot(FS_SPOT_OPEN);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    CHECK_STR_EQ(files_screen_message(), msg(TX_FM_E_BUSY, "ATEST012.TXT"));
    /* The same number in the root is another folder's file: it opens. */
    CHECK(views(0, "ATEST012.TXT"));
    /* Done: it opens in USER/LOGS too. */
    g_h.marks.test = 0u;
    g_h.marks.txt = 0u;
    CHECK(views(1, "ATEST012.TXT"));
    /* Gone from the card since FILES listed it (deleted elsewhere): the
     * viewer says it is not on the card. */
    CHECK(pick(1, "LIMIT013.TXT"));
    for (int i = 0; i < g_h.card.count; ++i) {
        if (strcmp(g_h.card.n[i].path, "USER/LOGS/LIMIT013.TXT") == 0) {
            g_h.card.n[i].path[strlen(g_h.card.n[i].path) - 1u] = 'X';
        }
    }
    tap_foot(FS_SPOT_OPEN);
    CHECK_EQ(files_screen_view(), FILES_VIEW_TEXT);
    CHECK_EQ(text_view_state(), TEXT_VIEW_FAILED);
    CHECK_EQ(text_view_why(NULL), FM_ERR_NOT_FOUND);
    draw();
    CHECK(screen()->back());
    log_viewer_set_io(NULL);
}

int main(void)
{
    RUN(every_report_the_list_labels_opens_in_the_viewer);
    RUN(logs_own_list_holds_and_opens_the_same_reports);
    RUN(a_text_file_that_is_no_report_opens_in_the_viewer);
    RUN(a_report_being_written_or_gone_says_so);
    RUN(a_report_finished_off_screen_opens_from_logs);
    fh_free(&g_h);
    free(s_fb);
    return test_summary("files_open");
}
