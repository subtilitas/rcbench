/**
 * @file
 * @brief RENAME LOG on MOTOR & ESC: the render loop hands the label over, and the
 *        logger renames the MOTOR log it closed last (fm_logname.h) under the
 *        card's job lock, which FILES and the Wi-Fi page's writes share.
 *
 * The logger runs it because it owns every log file: a rename and the open
 * of the next run's file cannot interleave.  The rename is a FILES rename
 * (fm_job_rename()), so the write gate is asked again on the logger task
 * just before the card call: a bench ARMED or a log opened since the tap
 * refuses it and changes nothing.  The log's file is closed by then:
 * FatFs runs without its file lock (CONFIG_FATFS_FS_LOCK 0), and a renamed
 * open file would lose the rows written after the rename.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>

#include "esp_log.h"

#include "card_tree.h"
#include "fm_logname.h"
#include "fm_rules.h"
#include "log_name.h"
#include "log_take.h"
#include "motor_screen.h"
#include "panel.h"
#include "storage.h"

/** @brief The log's tag. */
static const char *TAG = "rcbench";

/** @brief How long the logger waits for the card's job lock, ms: a FILES
 *         job or a write of the Wi-Fi page holds it. */
#define REN_LOCK_MS 100u

/** @brief The label the render loop handed over (motor_screen_log_take()). */
static char        s_ren_label[LOG_LABELLED_SIZE];
/** @brief The new name the rename asked for, filled by the logger. */
static char        s_ren_name[LOG_LABELLED_SIZE];
/** @brief The answer, an fm_err_t. */
static atomic_int  s_ren_err;
/** @brief Requests handed over. */
static atomic_uint s_ren_sent;
/** @brief Requests answered. */
static atomic_uint s_ren_answered;
/** @brief The logger's job. */
static fm_job_t    s_ren_job;
/** @brief The logger's open files as the rename's bench reads them. */
static log_marks_t s_ren_marks;
/** @brief The folder of s_ren_marks' run. */
static char        s_ren_run_dir[CARD_DIR_MAX];
/** @brief The folder of s_ren_marks' test. */
static char        s_ren_test_dir[CARD_DIR_MAX];

void log_rename_serve(void)
{
    const unsigned sent = atomic_load(&s_ren_sent);

    if (sent == atomic_load(&s_ren_answered)) {
        return;
    }
    fm_err_t err = FM_ERR_BUSY;
    s_ren_name[0] = '\0';
    if (panel_files_lock(REN_LOCK_MS)) {
        fm_logname_t last;
        fm_bench_t bench = { 0 };

        log_last_motor(&last);
        panel_files_bench(&bench, &s_ren_marks, s_ren_run_dir,
                          s_ren_test_dir);
        err = fm_logname_run(&s_ren_job, panel_files_card(), &bench, &last,
                             s_ren_label, s_ren_name, sizeof(s_ren_name));
        if (err == FM_OK) {
            log_last_motor_named(s_ren_name);
            ESP_LOGI(TAG, "renamed %s to %s", last.name, s_ren_name);
        } else {
            ESP_LOGW(TAG, "rename of %s refused or failed (%d)", last.name,
                     (int)err);
        }
        /* The numbering is read again at the next run, as after any
         * write of FILES: the number stays taken either way. */
        panel_files_unlock(err == FM_OK);
    }
    atomic_store(&s_ren_err, (int)err);
    atomic_store(&s_ren_answered, sent);
}

void motor_log_ui_service(void)
{
    static bool out;
    static fm_logname_t last;
    static log_marks_t marks;
    static char run_dir[CARD_DIR_MAX];
    static char test_dir[CARD_DIR_MAX];

    if (out && atomic_load(&s_ren_answered) == atomic_load(&s_ren_sent)) {
        out = false;
        motor_screen_log_done(atomic_load(&s_ren_err), s_ren_name);
    }
    log_last_motor(&last);
    log_open_marks(&marks, run_dir, test_dir);
    const bool on_card = last.name[0] != '\0' && storage_mounted()
                         && storage_mount_count() == last.mount;
    motor_screen_set_last_log(last.name, on_card, fm_logging(&marks));
    if (!out && motor_screen_log_take(s_ren_label, sizeof(s_ren_label))) {
        /* The label before the count, which hands the request over. */
        atomic_fetch_add(&s_ren_sent, 1u);
        out = true;
    }
}
