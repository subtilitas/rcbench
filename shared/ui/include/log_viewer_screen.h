/*
 * Log viewer: browse the card, see what the file actually is, then plot it.
 *
 * The screen owns no filesystem.  It is handed an I/O vtable -- list, open,
 * close -- so the firmware can point it at the SD card while the host tests
 * and the screenshot renderer point it at a string in memory.  That is what
 * makes an import dialog testable at all.
 *
 * Three views, in the order a person uses them:
 *
 *   BROWSE  what is on the card
 *   IMPORT  what was detected, and every knob to disagree with it
 *   PLOT    the traces, on one time base with a scale each
 *
 * The middle one is not decoration.  A German CSV read as English turns
 * 22,34 V into 2234 V without complaining, so the detection is shown before
 * anything is drawn, along with the evidence it rests on.
 *
 * Pure C, no ESP-IDF.
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
    /** Open a listed name as a rewindable source.  false if it will not open. */
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
