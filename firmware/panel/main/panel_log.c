/*
 * The logger: the runlog task, which makes every card call of a run, a
 * servo test and the fault log.  The control task's side, which numbers
 * the runs and posts the rows, is in main.c.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>          /* fsync(), which is what commits a row to FAT */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "log_name.h"
#include "log_writer.h"
#include "panel.h"
#include "storage.h"
#include "ui_text.h"

static const char *TAG = "rcbench";

/*
 * A run is written while the bench is armed and closed when it disarms; that
 * is the bench's definition of a run.  The file format is the one the log
 * viewer reads, and a host test writes a run and parses it back.
 *
 * The control task touches no file.  It beats the safety line, its ceiling is
 * HEARTBEAT_MAX_GAP_MS (150 ms) and the coprocessor calls the link silent
 * after 200 ms, while the SD (Secure Digital) specification allows a card
 * 250 ms to finish a single-block write before a host may give it up.  How
 * long this board's card takes is not measured.  A card write on that task is
 * therefore a dropped heartbeat and a coprocessor that fails safe, not a late
 * row -- so rows cross to a task of its own and every fopen, fwrite, fsync
 * and fclose happens there.
 *
 * That task holds the run's handle.  It is not the only handle on the card:
 * the viewer opens a log from the task that renders, so a card that is
 * unmounted or whose SPI2 (Serial Peripheral Interface) bus is freed would
 * pull the floor from under either of them.  Nothing in this build unmounts,
 * which is what makes that safe rather than any sequencing between the two.
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

/*
 * How many run numbers a failing create is given before the run is called
 * unrecorded.  The scan above starts past every number the card holds, so a
 * refused create is the volume answering and not a name collision.
 */
#define LOG_OPEN_TRIES 3u

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
 * Where the numbering got to.  One directory read at the first run's start
 * puts it above every number the card already carries; after that it starts
 * from what it handed out last.
 */
static int          s_log_next = LOG_RUN_FIRST;
/* Whether s_log_next has been put above what the card already holds.  Once
 * per boot: after that this end has written every number it handed out. */
static bool         s_log_numbered;

/*
 * The highest run number on the card, for the visitor below.
 *
 * This runs on the runlog task, so the directory read costs the card's own
 * latency and nothing else: the control task beats the safety line and
 * hit-tests STOP on its own schedule while this runs.  The walk therefore
 * takes no tick.  What it does cost is the first row of a run, which waits
 * in the queue until the scan finishes; a card holding 400 runs is one
 * directory read, not 400 opens.
 */
static void log_highest(const storage_entry_t *entry, void *ctx)
{
    int *highest = (int *)ctx;
    /*
     * A servo test's report counts as its run does: a number carried only
     * by a BENCHnnn.TXT whose CSV was deleted is still taken, so the next
     * run neither truncates that report nor writes its CSV beside it.
     *
     * Directories count.  The viewer skips them because it cannot open one,
     * but this is about which numbers are taken, and a directory called
     * BENCH003.CSV takes that number as surely as a file does: fopen refuses
     * it in both modes, so a run numbered into it would be a run that cannot
     * be created.  Numbering above it costs a number and nothing else.
     */
    int n = log_run_number(entry->name);
    const int r = log_report_number(entry->name);
    if (r > n) {
        n = r;
    }
    if (n > *highest) {
        *highest = n;
    }
}

/* What the numbering walk looks at: runs and the reports beside them. */
#define NUMBERED_SUFFIXES ".csv .txt"

/* Whether @p number is free: neither BENCHnnn.CSV nor BENCHnnn.TXT is on
 * the card.  @p path is left holding the CSV's path. */
static bool log_number_free(int number, char *path, size_t n)
{
    char name[LOG_RUN_NAME_MAX];
    log_report_name(name, sizeof(name), number);
    storage_path(CARD_DIR, name, path, n);
    FILE *probe = fopen(path, "r");
    if (probe != NULL) {
        fclose(probe);
        return false;
    }
    log_run_name(name, sizeof(name), number);
    storage_path(CARD_DIR, name, path, n);
    probe = fopen(path, "r");
    if (probe != NULL) {
        fclose(probe);
        return false;
    }
    return true;
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

/*
 * Put s_log_next above every number the card carries, once a boot; see
 * log_open().  False when the card would not list.
 */
static bool log_numbering(void)
{
    if (s_log_numbered) {
        return true;
    }
    int highest = LOG_RUN_FIRST - 1;
    if (storage_walk(CARD_DIR, NUMBERED_SUFFIXES, log_highest, &highest,
                     NULL) < 0) {
        return false;
    }
    s_log_next = (highest >= LOG_RUN_FIRST) ? highest + 1 : LOG_RUN_FIRST;
    s_log_numbered = true;
    return true;
}

/*
 * Open the run's file, on the logger task.
 *
 * Every probe is a card transaction and the loop can make hundreds of them,
 * which is why the scan is here and not on the task that beats the safety
 * line.  It runs when the run's first row arrives, one sample interval
 * (50 ms) after the arm or the supply's switch-on.
 */
static void log_open(uint32_t run)
{
    s_log_run_id      = run;
    s_log_file        = NULL;
    s_log_last_row_ms = now_ms();

    /*
     * Said, not swallowed, and said on the panel rather than to a console.
     *
     * A card that is not there when the run's first row arrives means this
     * run is not recorded and nothing tries again until the next run, so it
     * has to be visible: the panel's console is not reachable on every bench.
     */
    if (!storage_mounted()) {
        ESP_LOGW(TAG, "no card mounted; this run is not recorded");
        control_alert(TR(ALERT_NO_CARD_RUN));
        return;
    }
    /*
     * Numbered, not timestamped: no clock on this board survives a power
     * cycle, so every file would carry the FAT (File Allocation Table) epoch,
     * 1980-01-01.  The number is the
     * only order the card carries, and the viewer reads it back with
     * log_run_number() to decide which runs it can still show once a card
     * holds more of them than the screen does; log_run_name() is the one
     * place the name is built.
     *
     * Above every number the card already carries, not in the first gap.
     * A gap is what deleting an old run on a computer leaves, and a run
     * written into one is the newest run wearing the oldest number: the
     * viewer would rank it last and drop it from a full list, hiding the
     * experiment just recorded.  One directory read settles it, which is
     * also cheaper than the probes it replaces -- a card holding 400 runs
     * cost 400 opens before the first free number.
     */
    if (!log_numbering()) {
        /*
         * The card would not list.  Falling through would number this run
         * from 1 and take the first gap, which is the numbering this scan
         * exists to prevent: the run would be recorded and then rank as the
         * oldest on the card, and a full list would hide it.  A run the
         * operator is told is not recorded is better than one that records
         * itself out of sight.
         */
        ESP_LOGW(TAG, "the card would not list; this run is not recorded");
        control_alert(TR(ALERT_CARD_UNREADABLE));
        return;
    }
    /*
     * A volume that refuses one free number refuses them all, and each try is
     * two card transactions on this task.  Walking to LOG_RUN_LAST would be
     * 999 of them before the operator is told the run is not recorded.
     */
    unsigned refused = 0u;
    for (int i = s_log_next; i <= LOG_RUN_LAST && s_log_file == NULL; ++i) {
        char path[64];
        if (!log_number_free(i, path, sizeof(path))) {
            continue;
        }
        /*
         * Claimed before the file exists, not after it does.  fopen makes the
         * directory entry visible, and the task that renders can walk the
         * card between the two calls: it would find a run with no marker
         * against it, cache its size and analyse it while the logger is
         * about to start appending.  Cleared again if the open fails, so a
         * card that refuses every number leaves no run claimed.
         */
        atomic_store(&s_log_open_run, (unsigned)i);
        s_log_file = fopen(path, "w");
        if (s_log_file != NULL) {
            s_log_next = i + 1;
            ESP_LOGI(TAG, "logging to %s", path);
        } else {
            atomic_store(&s_log_open_run, 0u);
            if (++refused >= LOG_OPEN_TRIES) {
                break;   /* the volume, not this name */
            }
        }
    }
    if (s_log_file == NULL) {
        /*
         * And the numbering is asked again next time.  It is latched for the
         * boot so a card holding hundreds of runs is read once, which is
         * right while runs are being written -- but a run that could not be
         * opened has written no number, and the operator's answer to a full
         * card is to delete one.  Without this the scan never runs again and
         * the deletion changes nothing until the panel restarts.  It costs a
         * directory read per failed run, and a failed run is already a run
         * that is not being recorded.
         *
         * It rescues the card that stays in the slot.  It cannot rescue the
         * one taken out to be edited on a computer: storage_mounted() is
         * never cleared, so a swapped card leaves a stale mount and the
         * walk reads the volume that is gone.  That is the open
         * item in STATUS.md, and until it is closed a deletion made off the
         * bench needs the panel restarted.
         */
        s_log_numbered = false;
        ESP_LOGW(TAG, "no log file could be opened; the run is not recorded");
        control_alert(TR(ALERT_CARD_FULL));
        return;
    }
    const log_sink_t sink = { .write = file_write, .flush = file_flush,
                              .ctx = s_log_file };
    log_writer_init(&s_log, &sink);
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
    if (fclose(s_log_file) != 0) {
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
 * The automatic servo test's files: its CSV as BENCHnnn.CSV under the next
 * run number, and its report as BENCHnnn.TXT under the same number.  A limit
 * run writes its report alone, as BENCHnnn.TXT under the next number.  The
 * render loop hands the lines over through s_test_q (servo_screen_test_peek())
 * and this task writes them.  The armed bench's own log writes no row while
 * a test runs (log_cadence_bench_run()), so a test is one CSV.  One test
 * file is open at a time: the CSV is closed before the report is opened.
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

/* BENCHnnn.CSV, or with @p txt BENCHnnn.TXT, as a path on the card. */
static void test_path(int number, bool txt, char *path, size_t n)
{
    char name[LOG_RUN_NAME_MAX];
    if (txt) {
        log_report_name(name, sizeof(name), number);
    } else {
        log_run_name(name, sizeof(name), number);
    }
    storage_path(CARD_DIR, name, path, n);
}

static void test_close(void)
{
    if (s_test_fp == NULL) {
        return;
    }
    bool bad = fflush(s_test_fp) != 0 || fsync(fileno(s_test_fp)) != 0;
    bad = (fclose(s_test_fp) != 0) || bad;
    /* This file's failure, not the run's: a CSV that failed does not make
     * a report written whole a bad one. */
    if (bad) {
        s_test_failed  = true;
        s_test_txt_bad = s_test_txt_bad || s_test_txt;
    }
    s_test_fp = NULL;
    if (s_test_txt) {
        atomic_store(&s_test_open_txt, 0u);
    } else {
        atomic_store(&s_test_open_run, 0u);
    }
}

/* A run's OPEN: the next run number, as log_open() takes one.  With @p txt
 * the run is a report alone (SERVO_TEST_OUT_OPEN_TXT, the limit run): its
 * BENCHnnn.TXT is opened under the number, and no CSV. */
static void test_open(bool txt)
{
    test_close();
    s_test_num    = -1;
    s_test_txt    = false;
    s_test_lines  = 0u;
    s_test_failed = false;
    s_test_txt_bad = false;
    atomic_store(&s_test_report, false);
    if (storage_mounted() && log_numbering()) {
        unsigned refused = 0u;
        for (int i = s_log_next; i <= LOG_RUN_LAST && s_test_fp == NULL; ++i) {
            char path[64];
            if (!log_number_free(i, path, sizeof(path))) {
                continue;
            }
            if (txt) {
                test_path(i, true, path, sizeof(path));
                atomic_store(&s_test_open_txt, (unsigned)i);
            } else {
                atomic_store(&s_test_open_run, (unsigned)i);
            }
            s_test_txt = txt;
            s_test_fp = fopen(path, "w");
            if (s_test_fp != NULL) {
                s_log_next = i + 1;
                s_test_num = i;
                ESP_LOGI(TAG, "servo test to %s", path);
            } else {
                atomic_store(&s_test_open_run, 0u);
                atomic_store(&s_test_open_txt, 0u);
                if (++refused >= LOG_OPEN_TRIES) {
                    break;
                }
            }
        }
        if (s_test_fp == NULL) {
            s_log_numbered = false;   /* asked again next time; see log_open */
        }
    }
    if (s_test_fp == NULL) {
        control_alert(TR(ALERT_CARD_SERVO));
    }
    atomic_store(&s_test_file, s_test_num);
    atomic_fetch_add(&s_test_opens_done, 1u);
}

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
        char path[64];
        test_path(s_test_num, true, path, sizeof(path));
        s_test_txt = true;
        s_test_fp = fopen(path, "w");
        if (s_test_fp == NULL) {
            s_test_failed  = true;
            s_test_txt_bad = true;
            atomic_store(&s_test_open_txt, 0u);
        }
    }
    if (s_test_fp == NULL) {
        return;
    }
    if (fputs(l->text, s_test_fp) < 0 || fputc('\n', s_test_fp) == EOF) {
        s_test_failed  = true;
        s_test_txt_bad = s_test_txt_bad || s_test_txt;
    }
    /* Committed as the run log is, every LOG_WRITER_FLUSH_ROWS lines. */
    if (++s_test_lines >= LOG_WRITER_FLUSH_ROWS) {
        s_test_lines = 0u;
        if (fflush(s_test_fp) != 0 || fsync(fileno(s_test_fp)) != 0) {
            s_test_failed  = true;
            s_test_txt_bad = s_test_txt_bad || s_test_txt;
        }
    }
}

/* One line appended to the fault log, opened and closed around it so that
 * each line is on the card before the next fault can happen. */
static void log_note(const char *line)
{
    if (!storage_mounted()) {
        return;
    }
    FILE *f = fopen(STORAGE_MOUNT_POINT "/RCBENCH.LOG", "a");
    if (f == NULL) {
        return;
    }
    fputs(line, f);
    fputc('\n', f);
    fclose(f);
}

/*
 * Everything that touches the card.
 *
 * On the control task's core and below it in priority: the control task
 * blocks for CONTROL_PERIOD_MS (5 ms) every pass and inside every receive
 * window of a link exchange, and this runs in those gaps.  It touches no
 * PSRAM, so it does not share bandwidth with the display's bounce-buffer
 * refill on the other core.
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
            log_open(row.run);
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
