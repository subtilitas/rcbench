/**
 * @file
 * @brief The logger: the runlog task, which makes every card call of a run, a
 * servo test and the fault log.  A run is written to AUTO LOG FOLDER, a
 * test to USER LOG FOLDER (SETUP > SD CARD; CARD_AUTO_LOGS and
 * CARD_USER_LOGS by default, card_tree.h), each under its folder's next
 * number (log_take.h).  The control task's side, which numbers
 * the runs and posts the rows, is in main.c.
 *
 * SPDX-License-Identifier: MIT
 */
#include <errno.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>        /* mkdir(), stat() */
#include <unistd.h>          /* fsync(), which is what commits a row to FAT */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "card_tree.h"
#include "fault_log.h"
#include "log_name.h"
#include "log_take.h"
#include "log_viewer_screen.h"
#include "log_writer.h"
#include "panel.h"
#include "storage.h"
#include "ui_card_text.h"
#include "ui_text.h"

static const char *TAG = "rcbench";

/*
 * A run is written while the bench is armed and closed when it disarms; that
 * is the bench's definition of a run.  The file format is the one the log
 * viewer reads, and a host test writes a run and parses it back.
 *
 * The control task touches no file.  It beats the safety line, its ceiling is
 * HEARTBEAT_MAX_GAP_MS (150 ms) and the IOmcu calls the link silent
 * after 200 ms, while the SD (Secure Digital) specification allows a card
 * 250 ms to finish a single-block write before a host may give it up.  How
 * long this board's card takes is not measured.  A card write on that task is
 * therefore a dropped heartbeat and an IOmcu that fails safe, not a late
 * row -- so rows cross to a task of its own and every fopen, fwrite, fsync
 * and fclose happens there.
 *
 * That task holds the run's handle.  It is not the only handle on the card:
 * the viewer opens a log from the task that renders, and the Wi-Fi access
 * reads from its HTTP server.  Each open file and each path call is made
 * under a hold of the storage component (storage_hold(), storage_fopen()),
 * and a volume is unmounted only with no hold left, so a card swapped while
 * the panel runs is let go after the last handle on it is closed and never
 * from under one.
 */

/*
 * How long the logger waits for a row before looking at the run again.  It
 * bounds how long after the disarm the file is closed and how often a run
 * whose rows have stopped is offered a commit.
 */
#define LOG_TICK_MS    20u
/*
 * A run whose rows have stopped -- the far end gone quiet with the bench
 * still armed -- is committed anyway once this long has passed since the last
 * row, so its tail is not held for the rest of the run.
 */
#define LOG_QUIET_MS   1000u

QueueHandle_t s_log_q;    /**< control task -> logger, one row each */
QueueHandle_t s_note_q;   /**< control task -> logger, one line each */

/*
 * Which run is being recorded, or 0 for none.
 *
 * A level rather than a queued event, for the reason send_cmd() gives for the
 * disarm: a queue drops its oldest entry when it is full, and an arm or a
 * disarm that was dropped would lose a run or leave a file open across the
 * next one.  A number rather than a flag, so that two runs are two runs even
 * when the logger never saw the gap between them.  Written by the control
 * task on a run's edges, read by the logger; atomic because the two are
 * different tasks and either can be preempted mid-word.  Both are pinned to
 * core 1, the control task at priority 10 and the logger at 3, so the
 * preemption that matters is the control task taking the core back.
 */
atomic_uint s_log_run_now;

/* The logger task's own, from here down: nothing else reads or writes them. */
static FILE        *s_log_file;
/*
 * Which run this task has a file open for, or 0 for none.  Not the same
 * question as whether a file is open: a card that is full or unwritable
 * leaves s_log_file NULL, and reading the run's existence off that pointer
 * would make every pass look like a fresh run and run the whole file-name
 * scan again for as long as the run lasted.  A failed open is a run
 * without a log, not a retry.
 */
static uint32_t     s_log_run_id;
/*
 * Where each folder's numbering got to (log_take.h): one directory read
 * at the first run a folder takes since the start, or since a take in it
 * failed, puts it above every number the folder carries; after that it
 * starts from what it handed out last.  The logger task's own.
 */
static log_series_set_t s_series;
/* The mount s_series numbers the folders of (storage_mount_count()):
 * another mount is another card, or the same card edited elsewhere, and
 * every folder is walked again.  The logger task's own. */
static unsigned s_series_mount;

/*
 * The log folders.  s_dir_want is what SETUP > SD CARD names, written by
 * the text observer on app_main (log_set_folder()); s_dir_open is the
 * folder of each kind's open file, written by this task as it takes one
 * and read by the task that renders with the markers (log_open_marks()).
 * Both under s_dir_mux: a folder is 24 bytes, not one word.  A folder of
 * a kind changes only while that kind has no file open: a run is closed
 * before the next is opened, and a test's CSV and report share one take.
 */
static portMUX_TYPE s_dir_mux = portMUX_INITIALIZER_UNLOCKED;
static char s_dir_want[LOG_FOLDERS][CARD_DIR_MAX] = { CARD_AUTO_LOGS,
                                                      CARD_USER_LOGS };
static char s_dir_open[LOG_FOLDERS][CARD_DIR_MAX] = { CARD_AUTO_LOGS,
                                                      CARD_USER_LOGS };
/* The folders this task took last; a change forgets every folder's
 * numbering, so the next take walks its folder.  The logger task's own. */
static char s_dir_took[LOG_FOLDERS][CARD_DIR_MAX] = { CARD_AUTO_LOGS,
                                                      CARD_USER_LOGS };

void log_set_folder(log_folder_t kind, const char *dir)
{
    if (kind < 0 || kind >= LOG_FOLDERS || dir == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_dir_mux);
    snprintf(s_dir_want[kind], CARD_DIR_MAX, "%s", dir);
    taskEXIT_CRITICAL(&s_dir_mux);
}

/* The folder of @p kind's next file, into @p dir (CARD_DIR_MAX), marked as
 * the folder its file is opened in.  The logger task's. */
static void folder_take(log_folder_t kind, char *dir)
{
    taskENTER_CRITICAL(&s_dir_mux);
    memcpy(dir, s_dir_want[kind], CARD_DIR_MAX);
    memcpy(s_dir_open[kind], dir, CARD_DIR_MAX);
    taskEXIT_CRITICAL(&s_dir_mux);
    if (log_series_follow(&s_series, s_dir_took[kind], dir)) {
        ESP_LOGI(TAG, "log folder %d: %s", (int)kind, dir);
    }
}

void log_open_marks(log_marks_t *marks, char *run_dir, char *test_dir)
{
    taskENTER_CRITICAL(&s_dir_mux);
    memcpy(run_dir, s_dir_open[LOG_FOLDER_AUTO], CARD_DIR_MAX);
    memcpy(test_dir, s_dir_open[LOG_FOLDER_USER], CARD_DIR_MAX);
    marks->run  = atomic_load(&s_log_open_run);
    marks->test = atomic_load(&s_test_open_run);
    marks->txt  = atomic_load(&s_test_open_txt);
    taskEXIT_CRITICAL(&s_dir_mux);
    marks->run_dir  = run_dir;
    marks->test_dir = test_dir;
}

/* The files the logger has created since the start, and the LOGS tab of
 * the last one, for log_viewer_io_t.written. */
atomic_uint s_card_written;
atomic_int  s_card_written_tab = LOG_TAB_BENCH;

/* ------------------------------------------- the card, for log_take() */

/* What a create opens and which open marker it sets. */
typedef struct {
    FILE       **fp;
    atomic_uint *mark;
    int          tab;          /* log_viewer_tab_t of the folder */
    bool         held;         /* the volume is held for the take */
} take_ctx_t;

static bool take_mounted(void *ctx)
{
    return ((const take_ctx_t *)ctx)->held;
}

/* "/sdcard/<dir>", or "/sdcard/<dir>/<name>" with a name. */
static void take_path(const char *dir, const char *name, char *path, size_t n)
{
    if (name == NULL) {
        snprintf(path, n, "%s/%s", STORAGE_MOUNT_POINT, dir);
    } else {
        storage_path(dir, name, path, n);
    }
}

/*
 * One level of a folder of the card (card_make_fn), for every writer of the
 * panel: the logs, the fault log and the servo files.  mkdir() answers
 * EEXIST for a folder that is there and for a file of that name alike, so
 * the answer is the stat() after it: a folder, made or found, or a file.
 */
card_dir_t panel_card_make_dir(void *ctx, const char *level)
{
    (void)ctx;
    char path[sizeof(STORAGE_MOUNT_POINT) + CARD_DIR_MAX];
    take_path(level, NULL, path, sizeof(path));
    const int err = (mkdir(path, 0775) == 0) ? 0 : errno;
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return CARD_DIR_OK;
        }
        ESP_LOGW(TAG, "%s is a file, not a folder", path);
        return CARD_DIR_FILE;
    }
    ESP_LOGW(TAG, "cannot make %s: errno %d", path, err);
    return (err == EROFS || err == EACCES || err == EPERM) ? CARD_DIR_DENIED
                                                           : CARD_DIR_FAILED;
}

static bool take_make_dir(void *ctx, const char *dir)
{
    return panel_card_make_dir(ctx, dir) == CARD_DIR_OK;
}

typedef struct {
    log_take_visit_fn visit;
    void *ctx;
} take_walk_t;

static void take_walk_entry(const storage_entry_t *entry, void *ctx)
{
    const take_walk_t *w = (const take_walk_t *)ctx;
    w->visit(entry->name, w->ctx);
}

/*
 * The folder's runs and the reports beside them, and its folders, which
 * take a number as a file does.  This runs on the runlog task, so the
 * directory read costs the card's own latency and nothing else: the
 * control task beats the safety line and hit-tests STOP on its own
 * schedule while this runs.  What it costs is the first row of a run,
 * which waits in the queue until the scan finishes; a folder holding 400
 * runs is one directory read, not 400 opens.
 */
static bool take_walk(void *ctx, const char *dir, log_take_visit_fn visit,
                      void *visit_ctx)
{
    (void)ctx;
    take_walk_t w = { visit, visit_ctx };
    return storage_walk(dir, ".csv .txt", take_walk_entry, &w, NULL) >= 0;
}

/* One card transaction: FAT looks the name up from the folder's start. */
static bool take_exists(void *ctx, const char *dir, const char *name)
{
    (void)ctx;
    char path[64];
    take_path(dir, name, path, sizeof(path));
    struct stat st;
    return stat(path, &st) == 0;
}

static bool take_create(void *ctx, const char *dir, const char *name,
                        int number)
{
    const take_ctx_t *t = (const take_ctx_t *)ctx;
    char path[64];
    take_path(dir, name, path, sizeof(path));
    /*
     * Claimed before the file exists, not after it does.  fopen makes the
     * directory entry visible, and the task that renders can walk the card
     * between the two calls: it would find a run with no marker against
     * it, cache its size and analyse it while the logger is about to start
     * appending.  Cleared again if the open fails, so a card that refuses
     * every number leaves no run claimed.
     */
    atomic_store(t->mark, (unsigned)number);
    /* Its own hold, kept until the file is closed (storage_fclose()). */
    *t->fp = storage_fopen(path, "w");
    if (*t->fp == NULL) {
        atomic_store(t->mark, 0u);
        return false;
    }
    ESP_LOGI(TAG, "logging to %s", path);
    atomic_store(&s_card_written_tab, t->tab);
    atomic_fetch_add(&s_card_written, 1u);
    return true;
}

/*
 * Run @p number's file in @p dir under @p prefix, opened into @p fp with
 * @p mark set: log_take() over the card.
 */
static log_take_t take(const char *dir, log_prefix_t prefix, bool report,
                       FILE **fp, atomic_uint *mark, int tab, int *number)
{
    /*
     * The card in the slot now: a run's start is one of the events that
     * looks (storage_check()), so a card pulled, swapped or put in since
     * the last run is what this run is written to, or what its alert
     * names.  Then held for the take's folder, walk and probes, so another
     * task cannot let the volume go under them.
     */
    (void)storage_check();
    take_ctx_t t = { fp, mark, tab, storage_hold() };
    if (storage_mount_count() != s_series_mount) {
        s_series_mount = storage_mount_count();
        log_series_init(&s_series);
    }
    const log_take_io_t io = {
        .ctx      = &t,
        .mounted  = take_mounted,
        .make_dir = take_make_dir,
        .walk     = take_walk,
        .exists   = take_exists,
        .create   = take_create,
    };
    const log_take_t r = log_take(&s_series, &io, dir, prefix, report,
                                  number);
    if (t.held) {
        storage_release();
    }
    return r;
}

/* The band's word for a take that wrote no file. */
static void take_alert(log_take_t r, const char *dir, const char *other)
{
    if (r == LOG_TAKE_NO_FOLDER) {
        char line[96];
        snprintf(line, sizeof(line), TR(ALERT_CARD_NO_FOLDER), dir);
        control_alert(line);
    } else {
        control_alert(other);
    }
}

static log_writer_t s_log;
static uint32_t     s_log_last_row_ms;

static int file_write(void *ctx, const void *data, size_t len)
{
    FILE *f = (FILE *)ctx;
    return (int)fwrite(data, 1, len, f);
}

/*
 * Make the rows the file already holds survive a power cut.
 *
 * fflush() moves them out of the C library's buffer and no further.  FAT
 * (file allocation table) keeps a file's length in its directory entry and
 * writes that entry on f_sync, so a file that has only been fflushed reads as
 * 0 bytes after a power cut whatever its data sectors hold -- the whole run
 * lost.  fsync() is the call that reaches f_sync.
 *
 * log_writer_row() calls this every LOG_WRITER_FLUSH_ROWS (20) rows or
 * LOG_WRITER_FLUSH_S (1.0 s) of run, whichever comes first, which at
 * PANEL_SAMPLE_HZ is the same instant.
 */
static bool file_flush(void *ctx)
{
    FILE *f = (FILE *)ctx;
    return fflush(f) == 0 && fsync(fileno(f)) == 0;
}

/* A run kind's name: MOTORnnn, SERVOnnn or SUPPLnnn (log_name.h). */
static log_prefix_t run_prefix(log_run_kind_t kind)
{
    switch (kind) {
    case LOG_RUN_MOTOR:  return LOG_PREFIX_MOTOR;
    case LOG_RUN_SUPPLY: return LOG_PREFIX_SUPPL;
    case LOG_RUN_SERVO:
    default:             return LOG_PREFIX_SERVO;
    }
}

/*
 * Open the run's file in AUTO LOG FOLDER, on the logger task: MOTORnnn.CSV
 * for a run armed on MOTOR & ESC or ESC STICK, SERVOnnn.CSV for one armed
 * on SERVO, SUPPLnnn.CSV for a supply run.
 *
 * Every probe is a card transaction and the loop can make hundreds of them,
 * which is why the scan is here and not on the task that beats the safety
 * line.  It runs when the run's first row arrives, one sample interval
 * (50 ms) after the arm or the supply's switch-on.
 */
static void log_open(uint32_t run, log_run_kind_t kind)
{
    s_log_run_id      = run;
    s_log_file        = NULL;
    s_log_last_row_ms = now_ms();

    /*
     * Numbered, not timestamped: no clock on this board survives a power
     * cycle, so every file would carry the FAT (File Allocation Table)
     * epoch, 1980-01-01.  The number is the only order the card carries,
     * and the viewer reads it back with log_run_number() to decide which
     * runs it can still show once a folder holds more of them than the
     * screen does; log_name_build() is the one place the name is built.
     *
     * Every way to no file is said, and said on the panel rather than to
     * a console: the panel's console is not reachable on every bench, and
     * nothing tries again until the next run.  A card that would not list
     * is a run not recorded rather than one numbered from 1 into the first
     * gap, which would rank as the oldest in a full list and hide itself.
     * A failed take is walked again next time (log_take.h): the operator's
     * answer to a full folder is to delete a file.  A card taken out to be
     * edited on a computer and put back is found by take()'s look and
     * mounted again, so its folder is walked afresh.
     */
    int number = -1;
    char dir[CARD_DIR_MAX];
    folder_take(LOG_FOLDER_AUTO, dir);
    const log_take_t r = take(dir, run_prefix(kind), false,
                              &s_log_file, &s_log_open_run, LOG_TAB_BENCH,
                              &number);
    if (r != LOG_TAKE_OK) {
        s_log_file = NULL;
        ESP_LOGW(TAG, "no log file (take %d); this run is not recorded",
                 (int)r);
        take_alert(r, dir,
                   (r == LOG_TAKE_NO_CARD)
                       ? ui_card_text(storage_card_state(), UI_CARD_AT_RUN)
                   : (r == LOG_TAKE_UNREADABLE) ? TR(ALERT_CARD_UNREADABLE)
                                                : TR(ALERT_CARD_FULL));
        return;
    }
    const log_sink_t sink = { .write = file_write, .flush = file_flush,
                              .ctx = s_log_file };
    log_writer_init(&s_log, &sink);
    /* The INA3221 columns as the map the IOmcu holds makes them now: a
     * run's file keeps them, as the map takes no change while it drives. */
    log_cols_t cols;
    log_writer_set_cols(&s_log, sense_log_cols(&cols) ? &cols : NULL);
}

static void log_close(void)
{
    s_log_run_id = 0u;
    if (s_log_file == NULL) {
        return;
    }
    const bool failed = log_writer_failed(&s_log);
    if (failed) {
        ESP_LOGW(TAG, "the log is short: a write failed after %u rows",
                 (unsigned)s_log.rows);
        /* On the band as well as the console.  A write that failed mid-run
         * is the difference between an experiment and half of one, and the
         * operator at the bench has no console.  Dropped rows are the control
         * task's to report; see log_follow_runs(). */
        control_alert(TR(ALERT_CARD_ROWS_SHORT));
    }
    /*
     * The close is the last commit and the largest one: FATFS writes the
     * directory entry in f_close, so every row the writer has not committed
     * is kept by this call and by nothing else.  A card that fails here fails
     * silently otherwise: the file is left at its last committed length and
     * the viewer, which reads it as soon as s_log_open_run is cleared, shows
     * a complete-looking run that stops early.
     */
    const unsigned uncommitted = (unsigned)log_writer_pending(&s_log);
    if (storage_fclose(s_log_file) != 0) {
        ESP_LOGW(TAG, "the log did not close: %u rows are not in it",
                 uncommitted);
        if (!failed) {
            control_alert(TR(ALERT_CARD_LAST_WRITE));
        }
    } else if (!failed) {
        ESP_LOGI(TAG, "%u rows written", (unsigned)s_log.rows);
    }
    s_log_file = NULL;
    /*
     * And only now is the run something the viewer may read.  The length of a
     * FAT file lives in its directory entry, and f_close is what writes it --
     * so this is cleared after the call whether or not the call succeeded: a
     * failed close has still given up the handle, and holding the run back
     * for ever would hide the rows that did reach the card.
     */
    atomic_store(&s_log_open_run, 0u);
}

/*
 * The run's card is gone (storage_check() after a failed write): the file
 * is closed now, best effort, rather than at the run's end, so the volume
 * can be let go and the next look finds the card in the slot.  FatFs
 * refuses the close of a volume found gone without card traffic.  The run
 * goes on unrecorded: ALERT_CARD_ROWS_PAST has said so, and its later rows
 * are dropped against s_log_run_id, which stays the run's.
 */
static void log_drop(void)
{
    ESP_LOGW(TAG, "card gone: the run goes on unrecorded after %u rows",
             (unsigned)s_log.rows);
    (void)storage_fclose(s_log_file);
    s_log_file = NULL;
    atomic_store(&s_log_open_run, 0u);
}

/*
 * The tests' files, in USER LOG FOLDER: the AUTOMATIC TEST's CSV as
 * ATESTnnn.CSV under the folder's next number and its report as
 * ATESTnnn.TXT under the same number; a limit run's report alone as
 * LIMITnnn.TXT under the next number.  The render loop hands the lines
 * over through s_test_q (servo_screen_test_peek()) and this task writes
 * them; with USER LOGS off the render loop hands over nothing.  The armed
 * bench's own log writes no row while a test runs (log_cadence_bench_run()),
 * so a test is one CSV.  One test file is open at a time: the CSV is closed
 * before the report is opened.
 */
QueueHandle_t s_test_q;           /**< render loop -> logger */
/* OPENs the render loop queued, and those this task has answered with a
 * number in s_test_file (-1: not recorded). */
atomic_uint s_test_opens_sent;
atomic_uint s_test_opens_done;
/* ENDs the render loop queued, and those this task has taken: a run's
 * file is closed and s_test_report set for it once they match. */
unsigned    s_test_ends_sent;
atomic_uint s_test_ends_done;
atomic_int  s_test_file;
/* Whether the last run's report reached the card whole; set at its END. */
atomic_bool s_test_report;

/* The logger task's own. */
static FILE    *s_test_fp;
static int      s_test_num;
static bool     s_test_txt;      /* s_test_fp is the report               */
static unsigned s_test_lines;    /* since the last commit                 */
static bool     s_test_failed;
static bool     s_test_txt_bad;  /* a write or the close of the report failed */
static char     s_test_dir[CARD_DIR_MAX];   /* the folder of the run's files */
/** @brief The open test file's path, for the console lines. */
static char     s_test_path[64];
/** @brief The lines written to the open test file. */
static unsigned s_test_count;
/** @brief A write to the open test file failed. */
static bool     s_test_file_bad;

/**
 * @brief Run @p number's file in the test folder as a path on the card:
 *        ATESTnnn.CSV, ATESTnnn.TXT or LIMITnnn.TXT.
 *
 * @param prefix  LOG_PREFIX_ATEST or LOG_PREFIX_LIMIT.
 * @param number  The run's number.
 * @param txt     true for the .TXT report, false for the CSV.
 * @param path    Receives the path.
 * @param n       Size of @p path, bytes.
 */
static void test_path(log_prefix_t prefix, int number, bool txt, char *path,
                      size_t n)
{
    char name[LOG_RUN_NAME_MAX];
    log_name_build(name, sizeof(name), prefix, number, txt);
    storage_path(s_test_dir, name, path, n);
}

/**
 * @brief Close the open test file, if any, and say so on the console with
 *        the lines it holds: "closed" when every write and the close went
 *        through, "closed short" otherwise.
 */
static void test_close(void)
{
    if (s_test_fp == NULL) {
        return;
    }
    bool bad = fflush(s_test_fp) != 0 || fsync(fileno(s_test_fp)) != 0;
    bad = (storage_fclose(s_test_fp) != 0) || bad;
    /* This file's failure, not the run's: a CSV that failed does not make
     * a report written whole a bad one. */
    if (bad) {
        s_test_failed  = true;
        s_test_txt_bad = s_test_txt_bad || s_test_txt;
    }
    if (bad || s_test_file_bad) {
        ESP_LOGW(TAG, "%s closed short: a write or the close failed, "
                      "%u lines sent", s_test_path, s_test_count);
    } else {
        ESP_LOGI(TAG, "%s closed, %u lines", s_test_path, s_test_count);
    }
    s_test_fp = NULL;
    if (s_test_txt) {
        atomic_store(&s_test_open_txt, 0u);
    } else {
        atomic_store(&s_test_open_run, 0u);
    }
}

/**
 * @brief A run's OPEN: the folder's next number, as log_open() takes one.
 *
 * The file's path goes to the console as take_create() opens it,
 * "logging to ..."; a take that opens no file logs why.
 *
 * @param txt  true for a report alone (SERVO_TEST_OUT_OPEN_TXT, the limit
 *             run): its LIMITnnn.TXT is opened under the number, and no
 *             CSV.  false for the AUTOMATIC TEST's ATESTnnn.CSV.
 */
static void test_open(bool txt)
{
    test_close();
    s_test_num    = -1;
    s_test_txt    = txt;
    s_test_lines  = 0u;
    s_test_failed = false;
    s_test_txt_bad = false;
    atomic_store(&s_test_report, false);
    int number = -1;
    folder_take(LOG_FOLDER_USER, s_test_dir);
    const log_take_t r = take(s_test_dir,
                              txt ? LOG_PREFIX_LIMIT : LOG_PREFIX_ATEST, txt,
                              &s_test_fp,
                              txt ? &s_test_open_txt : &s_test_open_run,
                              LOG_TAB_USER, &number);
    if (r == LOG_TAKE_OK) {
        s_test_num = number;
        test_path(txt ? LOG_PREFIX_LIMIT : LOG_PREFIX_ATEST, number, txt,
                  s_test_path, sizeof(s_test_path));
        s_test_count = 0u;
        s_test_file_bad = false;
    } else {
        s_test_fp = NULL;
        ESP_LOGW(TAG, "no test file in %s (take %d); this test is not "
                      "recorded", s_test_dir, (int)r);
        /* "Card missing or full" for every other way to no file. */
        take_alert(r, s_test_dir,
                   (r == LOG_TAKE_NO_CARD)
                       ? ui_card_text(storage_card_state(), UI_CARD_AT_TEST)
                       : TR(ALERT_CARD_SERVO));
    }
    atomic_store(&s_test_file, s_test_num);
    atomic_fetch_add(&s_test_opens_done, 1u);
}

/**
 * @brief One line from the render loop: an OPEN, an END, or a line of the
 *        open run's CSV or report.
 *
 * The report's ATESTnnn.TXT opens at its first line, after the CSV
 * closes, and its path goes to the console as it opens.
 *
 * @param l  The line.
 */
static void test_write(const test_line_t *l)
{
    if (l->kind == SERVO_TEST_OUT_OPEN
        || l->kind == SERVO_TEST_OUT_OPEN_TXT) {
        test_open(l->kind == SERVO_TEST_OUT_OPEN_TXT);
        return;
    }
    if (l->kind == SERVO_TEST_OUT_END) {
        test_close();
        /* The report is on the card when it was opened and nothing about
         * it failed: what the SERVO screen names, and only then. */
        atomic_store(&s_test_report,
                     s_test_num > 0 && s_test_txt && !s_test_txt_bad);
        if (s_test_num > 0 && s_test_failed) {
            control_alert(TR(ALERT_CARD_SERVO_SHORT));
        }
        s_test_num = -1;
        atomic_fetch_add(&s_test_ends_done, 1u);
        return;
    }
    if (s_test_num <= 0) {
        return;                         /* not recorded */
    }
    if (l->kind == SERVO_TEST_OUT_TXT && !s_test_txt) {
        /* The report follows the CSV: one file open at a time.  Its marker
         * is set before the CSV's goes, so the run stays open between. */
        atomic_store(&s_test_open_txt, (unsigned)s_test_num);
        test_close();
        test_path(LOG_PREFIX_ATEST, s_test_num, true, s_test_path,
                  sizeof(s_test_path));
        s_test_txt = true;
        s_test_count = 0u;
        s_test_file_bad = false;
        s_test_fp = storage_fopen(s_test_path, "w");
        if (s_test_fp == NULL) {
            s_test_failed  = true;
            s_test_txt_bad = true;
            atomic_store(&s_test_open_txt, 0u);
            ESP_LOGW(TAG, "cannot open %s: errno %d; the report is not "
                          "recorded", s_test_path, errno);
        } else {
            ESP_LOGI(TAG, "logging to %s", s_test_path);
        }
    }
    if (s_test_fp == NULL) {
        return;
    }
    bool bad = fputs(l->text, s_test_fp) < 0
               || fputc('\n', s_test_fp) == EOF;
    ++s_test_count;
    /* Committed as the run log is, every LOG_WRITER_FLUSH_ROWS lines. */
    if (++s_test_lines >= LOG_WRITER_FLUSH_ROWS) {
        s_test_lines = 0u;
        bad = fflush(s_test_fp) != 0 || fsync(fileno(s_test_fp)) != 0 || bad;
    }
    if (bad) {
        /* One look, at the run's first failure and not per line: a card
         * gone has its file closed now, as log_drop() closes a run's, and
         * a full one keeps it to the END, which says the files are
         * incomplete. */
        const bool first = !s_test_failed;
        s_test_failed  = true;
        s_test_file_bad = true;
        s_test_txt_bad = s_test_txt_bad || s_test_txt;
        if (first && storage_check() != CARD_STATE_MOUNTED) {
            test_close();
        }
    }
}

/* -------------------------------------------- the fault log (fault_log.h) */

/* Whether log_note() holds the volume for its line. */
static bool s_note_held;

static bool note_mounted(void *ctx)
{
    (void)ctx;
    return s_note_held;
}

/* "/sdcard/BENCH/RCBENCH.LOG" and the like: a path from the root. */
#define NOTE_PATH_MAX (sizeof(STORAGE_MOUNT_POINT) + CARD_DIR_MAX)

static int32_t note_size(void *ctx, const char *path)
{
    (void)ctx;
    char full[NOTE_PATH_MAX];
    take_path(path, NULL, full, sizeof(full));
    /* storage_stat() rather than stat(): stat()'s 32-bit st_size reads a
     * file of 4 GiB and more as a small one, which the cap would let grow. */
    storage_stat_t st;
    const esp_err_t err = storage_stat(full, &st);
    if (err != ESP_OK) {
        return (err == ESP_ERR_NOT_FOUND) ? FAULT_SIZE_NONE
                                          : FAULT_SIZE_UNREAD;
    }
    if (st.is_dir) {
        return FAULT_SIZE_UNREAD;    /* a folder holds the name */
    }
    /* INT32_MAX for a larger file, as fault_log.h's size() asks. */
    return (st.size > (uint64_t)INT32_MAX) ? INT32_MAX : (int32_t)st.size;
}

static bool note_remove(void *ctx, const char *path)
{
    (void)ctx;
    char full[NOTE_PATH_MAX];
    take_path(path, NULL, full, sizeof(full));
    return unlink(full) == 0 || errno == ENOENT;
}

static bool note_rename(void *ctx, const char *from, const char *to)
{
    (void)ctx;
    char a[NOTE_PATH_MAX];
    char b[NOTE_PATH_MAX];
    take_path(from, NULL, a, sizeof(a));
    take_path(to, NULL, b, sizeof(b));
    return rename(a, b) == 0;
}

/* Opened and closed around the line, so that each line is on the card
 * before the next fault can happen. */
static bool note_append(void *ctx, const char *path, const char *line)
{
    (void)ctx;
    char full[NOTE_PATH_MAX];
    take_path(path, NULL, full, sizeof(full));
    FILE *f = fopen(full, "a");
    if (f == NULL) {
        return false;
    }
    bool ok = fputs(line, f) >= 0;
    ok = (fputc('\n', f) != EOF) && ok;
    ok = (fclose(f) == 0) && ok;
    return ok;
}

/* One line to BENCH/RCBENCH.LOG, whatever AUTO LOGS says. */
static void log_note(const char *line)
{
    static const fault_log_io_t io = {
        .ctx      = NULL,
        .mounted  = note_mounted,
        .make_dir = panel_card_make_dir,
        .size     = note_size,
        .remove   = note_remove,
        .rename   = note_rename,
        .append   = note_append,
    };
    bool rotated = false;
    s_note_held = storage_hold();
    const fault_log_t r = fault_log_note(&io, line, &rotated);
    if (s_note_held) {
        storage_release();
        s_note_held = false;
    }
    if (rotated) {
        ESP_LOGI(TAG, "%s full at %u bytes, now %s", CARD_FAULT_LOG,
                 (unsigned)FAULT_LOG_CAP, CARD_FAULT_OLD);
    }
    if (r != FAULT_LOG_OK && r != FAULT_LOG_NO_CARD) {
        ESP_LOGW(TAG, "fault log line not written (%d)", (int)r);
    }
}

/**
 * @brief Everything that touches the card.
 *
 * On the control task's core and below it in priority: the control task
 * blocks for CONTROL_PERIOD_MS (5 ms) every pass and inside every receive
 * window of a link exchange, and this runs in those gaps.  It touches no
 * PSRAM, so it does not share bandwidth with the display's scan-out.
 *
 * @param arg Unused.
 */
void log_task(void *arg)
{
    (void)arg;

    for (;;) {
        log_row_t row;
        const bool got =
            (xQueueReceive(s_log_q, &row, pdMS_TO_TICKS(LOG_TICK_MS))
             == pdTRUE);

        /*
         * A row belongs to the run it was sampled in, and that is what
         * decides its file.  A row for a run this task has no file for
         * ends the run it does have open and starts one for that run --
         * which is what keeps two runs out of one file when the bench is
         * disarmed and armed again faster than a pass, whether or not this
         * task ever saw the level between them go down.
         *
         * A file is opened by the first row and never by the run's start, so
         * a run that produces no row leaves no empty CSV (comma-separated
         * values) file behind and costs the card nothing.
         */
        if (got && row.run != s_log_run_id) {
            if (s_log_run_id != 0u) {
                log_close();
            }
            log_open(row.run, row.kind);
        }
        /*
         * A failed write latches the writer and every later row is rejected,
         * so the run stops being recorded at that point rather than at its
         * end.  Said on the edge, so the operator can stop and see to the
         * card while the run still means something -- and the edge is taken
         * around both calls that can fail, because a run whose rows have
         * paused fails at the commit and would otherwise be latched before
         * the next row could notice.
         */
        const bool was_failed = log_writer_failed(&s_log);

        if (got && s_log_file != NULL) {
            if (row.kind == LOG_RUN_SUPPLY) {
                (void)log_writer_supply_row(&s_log, row.t_s, &row.u.supply);
            } else if (row.kind == LOG_RUN_SERVO) {
                (void)log_writer_servo_row(&s_log, row.t_s, &row.u.bench,
                                           &row.rail);
            } else {
                (void)log_writer_row(&s_log, row.t_s, &row.u.bench);
            }
            s_log_last_row_ms = now_ms();
        }

        /* A run whose rows have stopped is committed anyway.  Not once the
         * writer has failed: the card is gone and an fsync a pass is work for
         * nothing. */
        if (s_log_file != NULL && !log_writer_failed(&s_log)
            && log_writer_pending(&s_log) > 0u
            && (uint32_t)(now_ms() - s_log_last_row_ms) >= LOG_QUIET_MS) {
            (void)log_writer_commit(&s_log);
        }

        if (s_log_file != NULL && !was_failed && log_writer_failed(&s_log)) {
            control_alert(TR(ALERT_CARD_ROWS_PAST));
            /* One look, at the failure's edge and not per row: a card
             * taken out is let go now, a full one is kept to the end. */
            if (storage_check() != CARD_STATE_MOUNTED) {
                log_drop();
            }
        }

        /*
         * And the end of the run, which is the only thing the level decides.
         * The close waits for the queue: the rows sampled in front of the
         * run's end are still in it and they belong in this file.
         */
        if (s_log_run_id != 0u && atomic_load(&s_log_run_now) != s_log_run_id
            && uxQueueMessagesWaiting(s_log_q) == 0u) {
            log_close();
        }

        char note[LOG_NOTE_MAX];
        while (xQueueReceive(s_note_q, note, 0) == pdTRUE) {
            log_note(note);
        }
        /* The servo test's lines, as many as wait. */
        test_line_t tl;
        while (xQueueReceive(s_test_q, &tl, 0) == pdTRUE) {
            test_write(&tl);
        }
        /* And the servo programmer's record and its file request, when one
         * waits. */
        kst_record_write();
        kst_card_serve();
    }
}
