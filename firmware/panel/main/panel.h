/*
 * What the panel's files share: firmware/panel/main/main.c and the files
 * cut from it.  One section per file, each holding what that file defines
 * and another file uses.  A file's own state stays static in that file.
 *
 * Nothing in shared/ includes this header.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "bench_state.h"
#include "kst_prog.h"
#include "log_cadence.h"
#include "log_viewer_screen.h"
#include "servo_test_io.h"
#include "supply.h"

/* ------------------------------------------------------------------ main.c */

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* An alert for the band, from the control task or the logger: the slot
 * holds one alert, and a later one replaces it.  Inlined in main.c. */
void control_alert(const char *text);

/* ------------------------------------------------------------ panel_card.c */

#define CARD_DIR      ""          /* the root of the mount point */

/* Read once at start-up, from bring_up(). */
void esc_profiles_load(void);
/* The log viewer's way to the card. */
extern const log_viewer_io_t k_card_io;

/* The runs the logger has open, held back from the viewer. */
extern atomic_uint s_log_open_run;
extern atomic_uint s_test_open_run;
extern atomic_uint s_test_open_txt;

/* The programmer's record, KST.TXT: filled by the render loop, written by
 * the logger. */
extern char        s_kst_file[KST_PROG_FILE_MAX];
extern size_t      s_kst_file_len;
extern atomic_uint s_kst_file_sent;
extern atomic_uint s_kst_file_done;
extern atomic_bool s_kst_file_ok;
void kst_record_write(void);
void kst_record_load(bool late);

/* A servo file request: taken from the model by the render loop, run on
 * the card by the logger. */
extern kst_prog_card_req_t  s_kst_card_req;
extern kst_prog_card_done_t s_kst_card_done;
extern atomic_uint          s_kst_card_sent;
extern atomic_uint          s_kst_card_answered;
void kst_card_serve(void);

/* ------------------------------------------------------------- panel_log.c */

/*
 * A run is one arming of the bench, or one switch-on of the supply's output
 * while the bench is not armed.  The bench takes the log: arming during a
 * supply run ends that run, and a supply output still on at the disarm starts
 * a run of its own.  One run is one file, and a file holds one kind of row,
 * so its header names every column it has.
 */
typedef enum {
    LOG_RUN_NONE = 0,
    LOG_RUN_BENCH,
    LOG_RUN_SUPPLY,
} log_run_kind_t;

/*
 * One sample on its way to the card, filled by the control task.
 *
 * It carries which run it belongs to, because that is the only thing that
 * says which file it goes in: a disarm and an arm inside one pass of the
 * logger would otherwise put two runs in one file, with a time column that
 * goes backwards halfway down.
 */
typedef struct {
    uint32_t       run;
    float          t_s;
    log_run_kind_t kind;     /**< which of the two below */
    union {
        bench_state_t  bench;
        supply_state_t supply;
    } u;
} log_row_t;

/*
 * Sixty-four rows is 3.2 s of run at PANEL_SAMPLE_HZ, against the 250 ms an
 * SD card may take over one write: a card that pauses costs no rows.
 */
#define LOG_Q_LEN      64

/* The report link_report() writes to the card, and how many may be in
 * flight.  The report is made every 5 s at its fastest. */
#define LOG_NOTE_MAX   208
#define LOG_NOTE_Q_LEN 4

/* One line of a servo test's files, render loop -> logger. */
typedef struct {
    uint8_t kind;                        /* servo_test_out_t */
    char    text[SERVO_TEST_LINE_MAX];
} test_line_t;

#define TEST_Q_LEN 24

extern QueueHandle_t s_log_q;
extern QueueHandle_t s_note_q;
extern QueueHandle_t s_test_q;
/* The run being recorded, or 0: the control task's level, which the logger
 * follows. */
extern atomic_uint s_log_run_now;

/* A servo test's OPENs and ENDs, queued by the render loop and taken by
 * the logger, and what the logger made of them. */
extern atomic_uint s_test_opens_sent;
extern atomic_uint s_test_opens_done;
extern unsigned    s_test_ends_sent;
extern atomic_uint s_test_ends_done;
extern atomic_int  s_test_file;
extern atomic_bool s_test_report;

/* The runlog task: every fopen, fwrite, fsync and fclose on the card. */
void log_task(void *arg);
