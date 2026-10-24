/**
 * @file test_files_open.c
 * @brief OPEN on FILES' reports: every file the list labels a report opens
 *        in LOGS, on its folder's tab, as text; LOGS' own list holds the
 *        same reports; a file OPEN refuses says why, truly.
 *
 * LOGS reads the same card in memory as FILES, through a lister that walks
 * a folder of it by the panel's rule (card_take() in panel_card.c): the
 * names log_select_lists() takes, less a file the logger has open.
 *
 * Cases:
 * - every_report_the_list_labels_opens_in_logs_on_its_tab
 * - logs_own_list_holds_and_opens_the_same_reports
 * - a_text_file_that_is_no_report_stays_in_files
 * - a_report_being_written_or_gone_says_so
 *
 * SPDX-License-Identifier: MIT
 */
#include "files_rig.h"

#include "files_int.h"

#include "log_name.h"
#include "log_select.h"
#include "log_take.h"
#include "log_text.h"
#include "log_viewer_screen.h"
#include "logview_spots.h"

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
 *        report in USER.  Each folder fits a pane's 6 rows.
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

/**
 * @brief OPEN on @p name of pane @p i: LOGS, on @p tab, with the file open
 *        in @p view; and back to FILES.
 * @return true when all of it held
 */
static bool opens(int i, const char *name, log_viewer_tab_t tab,
                  log_viewer_view_t view)
{
    if (!pick(i, name)) {
        T_FAIL("%s is not on pane %d", name, i);
        return false;
    }
    tap_foot(FS_SPOT_OPEN);
    const bool ok = ui_router_current() == SCREEN_LOGS
                    && log_viewer_view() == view
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

/** @brief The text view's first row, "" without one. */
static const char *text_row0(void)
{
    static char row[128];
    row[0] = '\0';
    const log_text_t *t = log_viewer_text();
    if (t != NULL) {
        (void)log_text_row((log_text_t *)t, 0, row, sizeof(row));
    }
    return row;
}

/**
 * @brief Every report of one folder of pane @p i labelled a report, and
 *        opened in LOGS on @p tab as text; the run's CSV in the import.
 */
static void folder_opens(int i, log_viewer_tab_t tab)
{
    static const char *const reports[] = { "ATEST011.TXT", "ATEST012.TXT",
                                           "LIMIT013.TXT" };
    for (size_t k = 0; k < sizeof(reports) / sizeof(reports[0]); ++k) {
        CHECK_EQ(kind_of(i, reports[k]), FM_KIND_REPORT);
        CHECK(opens(i, reports[k], tab, LOG_VIEW_TEXT));
    }
    CHECK(opens(i, "ATEST011.CSV", tab, LOG_VIEW_IMPORT));
}

TEST_CASE(every_report_the_list_labels_opens_in_logs_on_its_tab)
{
    fresh_reports();
    /* USER/LOGS in pane 1: the tester's folder. */
    folder_opens(1, LOG_TAB_USER);
    CHECK(pick(1, "ATEST011.TXT"));
    tap_foot(FS_SPOT_OPEN);
    CHECK_STR_EQ(text_row0(), "RCBENCH SERVO TEST REPORT");
    ui_router_goto(SCREEN_FILES);
    settle();
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
        const bool text = log_report_number(want[r]) >= 0;
        CHECK_EQ(log_viewer_view(), text ? LOG_VIEW_TEXT : LOG_VIEW_IMPORT);
        CHECK_STR_EQ(log_viewer_open_name(), want[r]);
        log_viewer_screen()->back();
        CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    }
    /* Row n is past the list: nothing else is there. */
    lv_tap(LV_ROW_CX, LV_ROW_CY(n));
    lv_tap(LV_OPEN_CX, LV_FOOT_CY);
    CHECK_STR_EQ(log_viewer_open_name(), want[n - 1]);
    log_viewer_set_io(NULL);
}

TEST_CASE(a_text_file_that_is_no_report_stays_in_files)
{
    fresh_reports();
    /* A note, and ATEST000.TXT: 000 is no run's number. */
    static const char *const notes[] = { "NOTES.TXT", "ATEST000.TXT" };
    for (size_t k = 0; k < sizeof(notes) / sizeof(notes[0]); ++k) {
        CHECK_EQ(kind_of(1, notes[k]), FM_KIND_TEXT);
        CHECK(pick(1, notes[k]));
        CHECK(!fs_foot_live(FS_OPEN, NULL));
        tap_foot(FS_SPOT_OPEN);
        CHECK_EQ(ui_router_current(), SCREEN_FILES);
        CHECK_STR_EQ(files_screen_message(), TR(FM_OPEN_WHERE));
    }
    /* A report outside LOGS' three folders is labelled one and not
     * opened: LOGS has no tab for USER. */
    CHECK(open_dir(0, "USER"));
    CHECK_EQ(kind_of(0, "ATEST014.TXT"), FM_KIND_REPORT);
    CHECK(pick(0, "ATEST014.TXT"));
    tap_foot(FS_SPOT_OPEN);
    CHECK_EQ(ui_router_current(), SCREEN_FILES);
    CHECK_STR_EQ(files_screen_message(), TR(FM_OPEN_WHERE));
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
    CHECK_EQ(ui_router_current(), SCREEN_FILES);
    CHECK_STR_EQ(files_screen_message(), msg(TX_FM_E_BUSY, "ATEST012.TXT"));
    /* The same number in the root is another folder's file: it opens. */
    CHECK(opens(0, "ATEST012.TXT", LOG_TAB_ROOT, LOG_VIEW_TEXT));
    /* Done: it opens in USER/LOGS too. */
    g_h.marks.test = 0u;
    g_h.marks.txt = 0u;
    CHECK(opens(1, "ATEST012.TXT", LOG_TAB_USER, LOG_VIEW_TEXT));
    /* Gone from the card since FILES listed it (deleted elsewhere): not
     * on LOGS' list, and FILES says it is not on the card. */
    CHECK(pick(1, "LIMIT013.TXT"));
    for (int i = 0; i < g_h.card.count; ++i) {
        if (strcmp(g_h.card.n[i].path, "USER/LOGS/LIMIT013.TXT") == 0) {
            g_h.card.n[i].path[strlen(g_h.card.n[i].path) - 1u] = 'X';
        }
    }
    tap_foot(FS_SPOT_OPEN);
    CHECK_EQ(ui_router_current(), SCREEN_FILES);
    CHECK_STR_EQ(files_screen_message(),
                 msg(TX_FM_E_NOT_FOUND, "LIMIT013.TXT"));
    log_viewer_set_io(NULL);
}

int main(void)
{
    RUN(every_report_the_list_labels_opens_in_logs_on_its_tab);
    RUN(logs_own_list_holds_and_opens_the_same_reports);
    RUN(a_text_file_that_is_no_report_stays_in_files);
    RUN(a_report_being_written_or_gone_says_so);
    fh_free(&g_h);
    free(s_fb);
    return test_summary("files_open");
}
