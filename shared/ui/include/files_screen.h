/**
 * @file files_screen.h
 * @brief FILES: the SD (Secure Digital) card in two panes, commander style,
 *        and the operations on its files and folders.
 *
 * Two panes side by side, each a folder of the card with its own page,
 * selection and scroll.  The active pane has the accent heading; the other
 * pane's folder is where COPY and MOVE put an entry.  A tap on a row of the
 * other pane makes that pane the active one and selects the row.  A pane's
 * header is a button that goes up one folder; PREV and NEXT stand beside it
 * while the folder holds more than one page of FM_PAGE_MAX entries.
 *
 * The footer holds OPEN, COPY, MOVE, RENAME, DELETE, NEW, INFO and RESCAN.
 * OPEN enters a folder, and hands a .CSV file or a report standing alone in
 * one of LOGS' three folders to LOGS (log_viewer_show()).  DELETE asks
 * first.  NEW makes a folder or an empty text file, its name typed on the
 * file-name keyboard (ui_textkey_open_file()), as RENAME takes one.  INFO
 * shows an entry's name, folder, kind, size and time, and a folder's
 * entries.  RESCAN looks at the slot and reads both panes from their first
 * pages; a card swapped since is found there, and both panes go back to
 * their start folders: the card's root and USER LOG FOLDER.
 *
 * Writes are allowed only while the bench is SAFE and no log file is open
 * (fm_gate()); browsing and INFO are allowed in every state.  The places
 * the bench keeps take no RENAME, MOVE or DELETE, the servo programmer's
 * journal takes INFO only, and a file the logger writes takes INFO only
 * (fm_rules.h).  A footer button that would be refused is drawn dimmed;
 * a tap on it says why in the message line.
 *
 * The screen makes no card call.  Every listing, INFO and job is a request
 * (fm_req.h) handed to the card's task through files_io_t, one at a time;
 * the screen reads the answer once the io says it is in.  A copy shows its
 * progress in a box with CANCEL, and stops at its next chunk when the bench
 * is ARMED or a run starts logging.
 *
 * The screen drives no output.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "card_state.h"
#include "fm_req.h"
#include "fm_rules.h"
#include "fm_tree.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The card's task as the screen reaches it.  Every function may be
 *        called on the task that renders; none of them waits for the card.
 */
typedef struct {
    /**
     * @brief Hand @p req to the card's task.  The screen does not touch
     *        @p req again until idle() is true.
     * @param req  the request, filled
     * @param ctx  the io's context
     * @return false when it cannot be taken: one is under way, or the
     *         task could not start
     */
    bool (*submit)(fm_req_t *req, void *ctx);
    /**
     * @brief Whether the request handed over last is answered.
     * @param ctx  the io's context
     * @return true with no request under way
     */
    bool (*idle)(void *ctx);
    /**
     * @brief The bytes done of the job under way, for the progress box.
     *        May be NULL: no progress shows.
     * @param done   takes the bytes done
     * @param total  takes the bytes to do
     * @param ctx    the io's context
     */
    void (*progress)(uint64_t *done, uint64_t *total, void *ctx);
    /**
     * @brief Ask the job under way to stop at its next chunk.  May be NULL.
     * @param ctx  the io's context
     */
    void (*cancel)(void *ctx);
    /**
     * @brief The bench as the write gate and the rules see it now: ARMED,
     *        the logger's open files, the IOmcu link.  The log folders are
     *        the screen's own (log_viewer_tab_dir()).  May be NULL: SAFE,
     *        nothing open, the link up.
     * @param out  takes the bench; its pointers outlive the frame
     * @param ctx  the io's context
     */
    void (*bench)(fm_bench_t *out, void *ctx);
    /**
     * @brief The card's name for the root's header: its volume label, or
     *        the name in its CID.  May be NULL: the header reads SD CARD.
     * @param ctx  the io's context
     * @return the name, "" for none
     */
    const char *(*volume)(void *ctx);
    /**
     * @brief What the slot holds, for a pane with no card.  May be NULL:
     *        no card.
     * @param ctx  the io's context
     * @return the slot's state
     */
    card_state_t (*card)(void *ctx);
    void *ctx;  /**< passed to every function */
} files_io_t;

/**
 * @brief What the screen shows over its panes.
 */
typedef enum {
    FILES_VIEW_PANES = 0,  /**< the two panes and the footer             */
    FILES_VIEW_DELETE,     /**< the DELETE question                      */
    FILES_VIEW_INFO,       /**< the INFO box                             */
    FILES_VIEW_NEW,        /**< NEW's choice: FOLDER or TEXT FILE        */
    FILES_VIEW_NAME,       /**< the keyboard for a new name              */
    FILES_VIEW_JOB         /**< a job's progress box                     */
} files_view_t;

/**
 * @brief Point the screen at the card's task.  NULL leaves it without one:
 *        both panes show no card.
 * @param io  the io, copied
 */
void files_screen_set_io(const files_io_t *io);

/**
 * @brief The view on top.
 * @return the view
 */
files_view_t files_screen_view(void);

/**
 * @brief The active pane: the one OPEN, RENAME, DELETE, NEW and INFO act
 *        in, and whose entry COPY and MOVE take to the other.
 * @return 0 for the left pane, 1 for the right
 */
int files_screen_active(void);

/**
 * @brief A pane as last read.
 * @param i  0 for the left pane, 1 for the right
 * @return the pane, or NULL for another @p i
 */
const fm_tree_t *files_screen_pane(int i);

/**
 * @brief A pane's first row on screen.
 * @param i  0 for the left pane, 1 for the right
 * @return the row's index on the page, 0 for another @p i
 */
int files_screen_scroll(int i);

/**
 * @brief The message line: the last result or refusal, or the write
 *        gate's sentence; "" for none.
 * @return the line
 */
const char *files_screen_message(void);

/**
 * @brief Whether a request is under way on the card's task.
 * @return true until its answer is taken
 */
bool files_screen_busy(void);

/**
 * @brief A size as the panes and INFO write it, no space before the unit:
 *        "300B", "4kB", "3.0MB" from 1MiB, "8.00GB" from 1GiB.  The units
 *        are binary: 1kB is 1024 bytes.
 * @param out    takes the text
 * @param n      bytes at @p out
 * @param bytes  the size
 */
void files_size_text(char *out, size_t n, uint64_t bytes);

/**
 * @brief A time as INFO writes it: "2024-05-17 12:34", read as UTC (the
 *        panel keeps no time zone), or "" for -1.
 * @param out    takes the text
 * @param n      bytes at @p out
 * @param mtime  seconds since 1970-01-01, or -1
 */
void files_time_text(char *out, size_t n, int64_t mtime);

/**
 * @brief The screen.
 * @return its table
 */
const ui_screen_t *files_screen(void);

/** @brief Repaint the cached chrome into every framebuffer. */
void files_screen_invalidate(void);

#ifdef __cplusplus
}
#endif
