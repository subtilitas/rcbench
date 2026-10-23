/**
 * @file log_viewer_screen.h
 * @brief Log viewer: browse the card, review the import, plot the file.
 *
 * The screen owns no filesystem.  It is handed an I/O (input/output) vtable
 * (list, open, close), so the firmware points it at the SD card while the
 * host tests and the screenshot renderer point it at a string in memory.
 *
 * Four views, in the order they are used:
 *
 *   BROWSE  the files of one folder of the card, and DELETE for the selected
 *           one behind a confirmation that names it.  Three tabs choose the
 *           folder, left to right: ROOT (the card's root, where firmware
 *           0.21.0 and older wrote every run), USER (USER/LOGS, the tests
 *           a user starts) and BENCH (BENCH/LOGS, the automatic logs).  The
 *           list holds the CSV files and the reports standing alone
 *           (log_select.h)
 *   IMPORT  what the CSV (comma-separated values) reader detected, and the
 *           controls to override it
 *   PLOT    the traces, on one time base with a scale each
 *   TEXT    a report standing alone, LIMITnnn.TXT, as text (log_text.h):
 *           OPEN on a report shows it in place of IMPORT
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

#include "card_state.h"
#include "log_csv.h"
#include "log_text.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_VIEWER_MAX_FILES 48
#define LOG_VIEWER_NAME_MAX  64

/*
 * The largest file OPEN reads: 2 GiB less 1 byte.  The panel reads a file
 * through stdio, whose fseek() and ftell() take a long of 32 bits.  A
 * larger file, which an exFAT (extended File Allocation Table) card or a
 * FAT32 one up to 4 GiB less 1 byte holds, is listed with its size and
 * refused at OPEN with TX_LOG_TOO_BIG.
 */
#define LOG_VIEWER_OPEN_MAX 0x7FFFFFFFull

typedef struct {
    char name[LOG_VIEWER_NAME_MAX];
    uint64_t size;      /* bytes; 64-bit, as exFAT's */
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

/** The folder BROWSE lists. */
typedef enum {
    LOG_TAB_USER = 0,    /**< USER LOG FOLDER                       */
    LOG_TAB_BENCH,       /**< AUTO LOG FOLDER, the tab after a start */
    LOG_TAB_ROOT,        /**< CARD_ROOT                             */
    LOG_TAB_COUNT
} log_viewer_tab_t;

/** The folder of @p tab, a path from the card's root ("" the root itself);
 *  "" for a tab outside the three.  USER and BENCH list the folders SETUP >
 *  SD CARD names in force, as the logger writes them (card_folder_use():
 *  the default folder for one card_folder_check() refuses).  Valid until
 *  the next call for the same tab. */
const char *log_viewer_tab_dir(log_viewer_tab_t tab);

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
     * entries in the order the browse list draws them (log_select_sort():
     * newest run at the top, as the pages run), set @p page's ahead
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
    /**
     * Point the list(), open() and remove() that follow at folder @p dir of
     * the volume, a path from its root ("" the root itself;
     * log_viewer_tab_dir()).  The screen calls it before each of them.
     * May be NULL: a volume of one folder, which every tab lists.
     */
    void (*folder)(const char *dir, void *ctx);
    /**
     * How many files the logger has created since the panel started, and
     * in @p tab the tab of the last one.  May be NULL: no file is written.
     * LOGS opens on that tab when the count has moved since it last
     * looked, so the run just recorded is on screen.
     */
    unsigned (*written)(log_viewer_tab_t *tab, void *ctx);
    /**
     * What the slot holds, asked after a list() that found no volume: the
     * empty list then says no card, a card neither FAT32 nor exFAT, or a
     * card that cannot be read.  May be NULL: no card.
     */
    card_state_t (*card)(void *ctx);
    /**
     * A number that changes with every mount: another number is another
     * volume, whatever its name.  Two cards with one label are told apart
     * by it.  May be NULL: the name alone tells cards apart.
     */
    unsigned (*mounts)(void *ctx);
    void *ctx;
} log_viewer_io_t;

typedef enum {
    LOG_VIEW_BROWSE = 0,
    LOG_VIEW_IMPORT,
    LOG_VIEW_PLOT,
    LOG_VIEW_TEXT       /**< a report's text */
} log_viewer_view_t;

void log_viewer_set_io(const log_viewer_io_t *io);

/** Which of the three views is showing. */
log_viewer_view_t log_viewer_view(void);

/** The tab BROWSE shows. */
log_viewer_tab_t log_viewer_tab(void);

/**
 * Show @p tab and read its folder from the first page, as a tap on the tab
 * does.  A tab outside the three is ignored.
 */
void log_viewer_set_tab(log_viewer_tab_t tab);

/** The file currently open, or "" -- the overview header shows this. */
const char *log_viewer_open_name(void);

/** What was detected, or NULL when nothing has been analysed. */
const log_analysis_t *log_viewer_analysis(void);

/** The loaded traces, or NULL when nothing is plotted. */
const log_data_t *log_viewer_data(void);

/**
 * @brief The report the TEXT view shows.
 * @return its text and rows (log_text.h), or NULL with no report open
 */
const log_text_t *log_viewer_text(void);

/**
 * @brief The TEXT view's first row on screen.
 * @return the row, 0 at the top and with no report open
 */
int log_viewer_text_first(void);

/**
 * The samples the plot shows: the first in @p first and how many in
 * @p count.  The whole file until two fingers zoom in; both 0 with nothing
 * plotted.
 */
void log_viewer_window(int *first, int *count);

/**
 * The browse list's tab into @p out of @p n bytes: the page's place in the
 * card, "FILES 49 TO 96 OF 121", on a card of more than one page, and
 * "FILES" otherwise.  The list draws this text.
 */
void log_viewer_browse_title(char *out, size_t n);

/** The colour series @p k is drawn in. */
gfx_color_t log_viewer_series_color(int k);

/** The sample the cursor is on, or -1 with nothing plotted. */
int log_viewer_cursor(void);

/**
 * A file's size as the list and the DELETE question write it into @p out
 * (@p n bytes): "300 B", "4 kB", "3.0 MB" from 1 MiB, "8.00 GB" from 1 GiB.
 * The units are binary: 1 kB is 1024 bytes, 1 GB 1024^3.
 */
void log_viewer_size_text(char *out, size_t n, uint64_t bytes);

/** The browse view's footer message, "" for none. */
const char *log_viewer_message(void);

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
