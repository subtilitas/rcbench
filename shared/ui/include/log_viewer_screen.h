/*
 * Log viewer: browse the card, review the import, plot the file.
 *
 * The screen owns no filesystem.  It is handed an I/O (input/output) vtable
 * (list, open, close), so the firmware points it at the SD card while the
 * host tests and the screenshot renderer point it at a string in memory.
 *
 * Three views, in the order they are used:
 *
 *   BROWSE  the files on the card
 *   IMPORT  what the CSV (comma-separated values) reader detected, and the
 *           controls to override it
 *   PLOT    the traces, on one time base with a scale each
 *
 * The import view shows the detected delimiter and decimal convention before
 * anything is plotted: a decimal-comma file read as decimal-point turns
 * 22,34 V into 2234 V.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#include "log_csv.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_VIEWER_MAX_FILES 48
#define LOG_VIEWER_NAME_MAX  64

typedef struct {
    char name[LOG_VIEWER_NAME_MAX];
    uint32_t size;
    bool is_dir;
} log_viewer_file_t;

typedef struct {
    /** Fill @p out; return the count, or -1 when there is no volume at all. */
    int (*list)(log_viewer_file_t *out, int max_entries, void *ctx);
    /** Open a listed name as a rewindable source; false if it fails to open. */
    bool (*open)(const char *name, log_source_t *src, void *ctx);
    /** Release whatever open() acquired. */
    void (*close)(void *ctx);
    /** Volume label for the header, e.g. the card name.  May be NULL. */
    const char *(*volume)(void *ctx);
    void *ctx;
} log_viewer_io_t;

typedef enum {
    LOG_VIEW_BROWSE = 0,
    LOG_VIEW_IMPORT,
    LOG_VIEW_PLOT
} log_viewer_view_t;

void log_viewer_set_io(const log_viewer_io_t *io);

/** Which of the three views is showing. */
log_viewer_view_t log_viewer_view(void);

/** The file currently open, or "" -- the overview header shows this. */
const char *log_viewer_open_name(void);

/** What was detected, or NULL when nothing has been analysed. */
const log_analysis_t *log_viewer_analysis(void);

/** The loaded traces, or NULL when nothing is plotted. */
const log_data_t *log_viewer_data(void);

/** Re-read the directory, e.g. after a card was inserted. */
void log_viewer_refresh(void);

const ui_screen_t *log_viewer_screen(void);
void log_viewer_invalidate(void);

#ifdef __cplusplus
}
#endif
