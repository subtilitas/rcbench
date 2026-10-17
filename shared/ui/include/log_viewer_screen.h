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

#include <stddef.h>
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

/**
 * Which page of the volume list() writes.
 *
 * A page is up to LOG_VIEWER_MAX_FILES entries that stand next to each other
 * in log_name_rank() order, newest run first.  It is named by the name at
 * its edge, @p key, and not by its number: the directory is read in one walk
 * per page and nothing about the pages before it is held, so the walk keeps
 * the entries on the right side of @p key and counts the rest.  A key whose
 * file has gone still names a place in that order.
 */
typedef enum {
    /** The first entries that rank with @p key or behind it. */
    LOG_PAGE_FROM = 0,
    /** The first entries that rank behind @p key: the next page. */
    LOG_PAGE_AFTER,
    /** The last entries that rank ahead of @p key: the page before.  With
     *  an empty @p key, the last entries of all: the last page. */
    LOG_PAGE_BEFORE
} log_page_dir_t;

typedef struct {
    log_page_dir_t dir;              /**< in */
    /** in: "" for FROM and AFTER is the first entry of all */
    char key[LOG_VIEWER_NAME_MAX];
    int ahead;   /**< out: entries that rank ahead of the first one written */
    int held;    /**< out: entries written */
} log_viewer_page_t;

typedef struct {
    /**
     * Fill @p out with the page @p page asks for, at most @p max_entries
     * entries in the order the browse list draws them, set @p page's ahead
     * and held, and return how many entries the volume holds, or -1 when
     * there is no volume at all.
     *
     * The count may be larger than @p max_entries: a card takes up to 999
     * runs, and the screen shows them a page at a time and says which part
     * of the count it shows.  log_select.h chooses the page while the
     * directory is read.  A @p page of NULL is the first page.
     */
    int (*list)(log_viewer_file_t *out, int max_entries,
                log_viewer_page_t *page, void *ctx);
    /** Open a listed name as a rewindable source; false if it fails to open. */
    bool (*open)(const char *name, log_source_t *src, void *ctx);
    /** Release whatever open() acquired. */
    void (*close)(void *ctx);
    /**
     * The card's name for the header: its volume label, or what the card
     * says of itself when it has none.  May be NULL, and then the header
     * reads SD CARD.
     */
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
 * The range of a numeric column as the import view writes it into @p out
 * (@p n bytes): "lo..hi unit".  One decimal while both ends are below
 * 1000 in magnitude, none from 1000 on, so "2392..14639 rpm" fits the
 * 140 px box of the view.  A text over LOG_RANGE_CELLS glyphs (code points,
 * gfx_text_cells(), so "µs" is 2) drops the
 * decimal, then the space before the unit: "-999.9..-999.9 rpm" reads
 * "-1000..-1000 rpm".  A text still wider, such as two 7-digit negative
 * ends, is cut at the box's edge.  No space and no unit with an empty
 * @p unit.
 */
#define LOG_RANGE_CELLS 17u   /**< the 140 px range box in 8 px glyphs */

void log_viewer_range_text(char *out, size_t n, double lo, double hi,
                           const char *unit);

/**
 * Re-read the directory from its first page, e.g. after a card was
 * inserted.  Closes an open DELETE question: the name it asked about may be
 * on another card now.  Selects the newest numbered run, so the run just
 * recorded is the one OPEN opens.
 */
void log_viewer_refresh(void);

const ui_screen_t *log_viewer_screen(void);
void log_viewer_invalidate(void);

#ifdef __cplusplus
}
#endif
