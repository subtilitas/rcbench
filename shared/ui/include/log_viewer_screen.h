/**
 * @file log_viewer_screen.h
 * @brief Log viewer: browse the card, review the import, plot the file.
 *
 * The screen owns no filesystem.  It is handed an I/O (input/output) vtable
 * (list, open, close), so the firmware points it at the SD card while the
 * host tests and the screenshot renderer point it at a string in memory.
 *
 * Three views, in the order they are used:
 *
 *   BROWSE  the files of one folder of the card, and DELETE for the selected
 *           one behind a confirmation that names it.  Three tabs choose the
 *           folder, left to right: ROOT (the card's root, where firmware
 *           0.21.0 and older wrote every run), USER (USER/LOGS, the tests
 *           a user starts) and BENCH (BENCH/LOGS, the automatic logs).  The
 *           list holds the CSV files and the reports
 *           (log_select_lists())
 *   IMPORT  what the CSV (comma-separated values) reader detected, and the
 *           controls to override it
 *   PLOT    the traces, on one time base with a scale each
 *
 * OPEN on a report, ATESTnnn.TXT or LIMITnnn.TXT, opens it in the text
 * viewer FILES hosts (files_screen_open_text(), text_view.h), read-only;
 * the home tag returns from there to LOGS.
 *
 * The import view shows the detected delimiter and decimal convention before
 * anything is plotted: a decimal-comma file read as decimal-point turns
 * 22,34V into 2234V.
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
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_VIEWER_MAX_FILES 48
#define LOG_VIEWER_NAME_MAX  64

/*
 * The largest file OPEN reads: 2GiB less 1 byte.  The panel reads a file
 * through stdio, whose fseek() and ftell() take a long of 32 bits.  A
 * larger file, which an exFAT (extended File Allocation Table) card or a
 * FAT32 one up to 4GiB less 1 byte holds, is listed with its size and
 * refused at OPEN with TX_LOG_TOO_BIG.
 */
#define LOG_VIEWER_OPEN_MAX 0x7FFFFFFFull

typedef struct {
    char name[LOG_VIEWER_NAME_MAX];
    uint64_t size;      /* bytes; 64-bit, as exFAT's */
    /** @brief The time FAT stamped, in seconds since 1970 as mktime()
     *         reads FAT's local date and time (the panel:
     *         storage_stamp_mtime()), as FILES INFO's; 0 or -1 for none.
     *         The row shows it when wall_stamp_known() takes it. */
    int64_t mtime;
    bool is_dir;
} log_viewer_file_t;

/** @brief The cells a row's name takes: a longer name, of up to
 *         LOG_VIEWER_NAME_MAX - 1 (63) bytes, shows its first 58 and "..". */
#define LOG_VIEWER_NAME_CELLS 60

/** @brief The texts of one row of the file list, as the list draws them. */
typedef struct {
    /** @brief The name, cut to LOG_VIEWER_NAME_CELLS cells. */
    char name[LOG_VIEWER_NAME_MAX + 2];
    /** @brief "2026-10-26 09:41" in the zone in force, or "" for a stamp
     *  wall_stamp_known() refuses. */
    char date[20];
    /** @brief The size (log_viewer_size_text()), or LOG_DIR for a folder. */
    char size[24];
} log_viewer_row_t;

/**
 * @brief The texts of the list's row for @p f: its name, cut as FILES cuts
 *        a name; its time as FILES INFO writes it (files_time_text() of
 *        wall_local_s()), and nothing for none or a 1980 stamp; its size.
 * @param f    the entry
 * @param out  takes the texts; all three "" for a NULL @p f
 */
void log_viewer_row_text(const log_viewer_file_t *f, log_viewer_row_t *out);

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
    LOG_VIEW_PLOT
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

/** @brief What log_viewer_show() found. */
typedef enum {
    LOG_SHOW_OPEN = 0,     /**< the list holds the file: it is open in
                                IMPORT, a report in the text viewer, or
                                the list shows it selected with the
                                reason OPEN refused it (a file over
                                LOG_VIEWER_OPEN_MAX, a report over
                                TEXT_BUF_FILE_MAX)                       */
    LOG_SHOW_NOT_LISTED,   /**< a tab outside the three, or a name the list
                                does not hold (a file other than a .csv or
                                a report, one the logger has open, one
                                gone from the card)                      */
    LOG_SHOW_OTHER_CARD    /**< the list was read from another mount than
                                @p mount: the card was swapped; nothing is
                                opened                                   */
} log_viewer_show_t;

/**
 * @brief Show @p tab with file @p name open, as a tap on the tab, a tap on
 *        the file's row and OPEN do: FILES' OPEN on a file of one of the
 *        three folders.  The page read is the one that starts at the name,
 *        and it must come from the mount FILES listed the file from.
 * @param tab    the folder's tab
 * @param name   the file's name in that folder
 * @param mount  the mount number (log_viewer_io_t mounts()) FILES read the
 *               name from
 * @return what was found
 */
log_viewer_show_t log_viewer_show(log_viewer_tab_t tab, const char *name,
                                  unsigned mount);

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
 * @brief A file's size as the list and the DELETE question write it into @p
 *        out (@p n bytes): "300B", "4kB", "3.0MB" from 1MiB, "8.00GB" from
 *        1GiB. The units are binary: 1kB is 1024 bytes, 1GB 1024^3.
 *
 * @param out the text
 * @param n its size, bytes
 * @param bytes the file's size
 */
void log_viewer_size_text(char *out, size_t n, uint64_t bytes);

/**
 * @brief The footer message: the last result or refusal, worded in the
 *        language showing; "" for none.
 * @return the message, in a buffer the next call overwrites
 */
const char *log_viewer_message(void);

/**
 * @brief The range of a numeric column as the import view writes it into @p
 *        out (@p n bytes): "lo..hiunit".  One decimal while both ends are
 *        below 1000 in magnitude, none from 1000 on, so "2392..14639rpm" fits
 *        the 140px box of the view.  A text over LOG_RANGE_CELLS glyphs (code
 *        points, gfx_text_cells(), so "µs" is 2) drops the decimal:
 *        "-999.9..-999.9mOhm" reads "-1000..-1000mOhm".  A text still wider,
 *        such as two 7-digit negative ends, is cut at the box's edge.  No unit
 *        with an empty @p unit.
 */
#define LOG_RANGE_CELLS 17u   /**< the 140px range box in 8px glyphs */

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
