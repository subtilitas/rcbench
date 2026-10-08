/*
 * Log viewer: browse the card, review the import, plot the file.
 *
 * The screen owns no filesystem.  It is handed an I/O (input/output) vtable
 * (list, open, close), so the firmware points it at the SD card while the
 * host tests and the screenshot renderer point it at a string in memory.
 *
 * Three views, in the order they are used:
 *
 *   BROWSE  the files on the card, and DELETE for the selected one behind a
 *           confirmation that names it
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
    /**
     * Fill @p out with at most @p max_entries entries and return how many the
     * volume holds, or -1 when there is no volume at all.
     *
     * The count may be larger than @p max_entries: a card takes up to 999
     * runs, and the screen shows what fits and says how many there are.  A
     * count above @p max_entries means all @p max_entries were written, and
     * they are the ones worth keeping -- newest run first, see log_select.h.
     * The first @p max_entries in directory order would hide every run made
     * after the card passed that many files.
     */
    int (*list)(log_viewer_file_t *out, int max_entries, void *ctx);
    /** Open a listed name as a rewindable source; false if it fails to open. */
    bool (*open)(const char *name, log_source_t *src, void *ctx);
    /** Release whatever open() acquired. */
    void (*close)(void *ctx);
    /** Volume label for the header, e.g. the card name.  May be NULL. */
    const char *(*volume)(void *ctx);
    /**
     * Delete a listed name from the volume; false if it is refused or fails.
     * May be NULL, and then the browse view offers no DELETE.  The screen
     * calls it only after the operator has confirmed that name on a second
     * panel.
     */
    bool (*remove)(const char *name, void *ctx);
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

/**
 * The samples the plot shows: the first in @p first and how many in
 * @p count.  The whole file until two fingers zoom in; both 0 with nothing
 * plotted.
 */
void log_viewer_window(int *first, int *count);

/** The colour series @p k is drawn in. */
gfx_color_t log_viewer_series_color(int k);

/** The sample the cursor is on, or -1 with nothing plotted. */
int log_viewer_cursor(void);

/**
 * Re-read the directory, e.g. after a card was inserted.  Closes an open
 * DELETE question: the name it asked about may be on another card now.
 * Selects the newest numbered run, so the run just recorded is the one
 * OPEN opens.
 */
void log_viewer_refresh(void);

const ui_screen_t *log_viewer_screen(void);
void log_viewer_invalidate(void);

#ifdef __cplusplus
}
#endif
