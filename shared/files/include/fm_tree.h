/**
 * @file fm_tree.h
 * @brief One pane of the file manager: a folder of the card, the page of
 *        its entries on show, and the row selected.
 *
 * A page is up to FM_PAGE_MAX entries in the pane's order: folders first,
 * then names with case folded, ascending; two names that differ only in
 * case, which FAT (File Allocation Table) does not hold, follow by their
 * bytes.  ".." is not a row.  A page is read by one walk of the folder and
 * kept by fm_select.h while the walk runs, so a folder of 999 files costs
 * one walk per page and the memory of one page.  NEXT and PREV are named
 * by the entry at the page's edge, not by a number: a page reads on from
 * the one before it after a file came or went.
 *
 * A name over FM_LIST_NAME_MAX bytes is not listed: the panel's walk
 * cannot hand it over whole, and a cut name opens nothing.  The pane counts
 * them, and the entries the walk leaves out (names starting with "."), as
 * unlisted, so the screen can say so.
 *
 * Every entry carries what the rules say of it (fm_rules.h) when the page
 * was read: its kind, its place and whether the logger writes it.  A job
 * asks the rules again when it starts: a page is a picture of the folder,
 * not a promise.
 *
 * A page is read from one mount (fm_io.h).  A walk that found the card gone
 * and another one mounted is walked again once, on the card in the slot;
 * @p mount says which card the page is of, and a caller that sees it change
 * knows the card was swapped.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "fm_io.h"
#include "fm_rules.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Rows of one page, as LOGS' page (LOG_VIEWER_MAX_FILES). */
#define FM_PAGE_MAX 48

/**
 * @brief One entry of a page.
 */
typedef struct {
    char name[FM_NAME_BUF];  /**< the entry's name                       */
    uint64_t size;           /**< bytes; 0 for a folder                  */
    bool is_dir;             /**< a folder                               */
    bool busy;               /**< the logger writes it (fm_busy())       */
    uint8_t kind;            /**< an fm_kind_t                           */
    uint8_t place;           /**< an fm_place_t                          */
} fm_entry_t;

/**
 * @brief Which page fm_tree_page() reads.
 */
typedef enum {
    FM_GO_FIRST = 0,  /**< the folder's first page                       */
    FM_GO_NEXT,       /**< the page after the one on show                */
    FM_GO_PREV,       /**< the page before the one on show               */
    FM_GO_LAST,       /**< the folder's last page                        */
    FM_GO_SAME        /**< the page on show again: the first page from
                           the folder's first entry, a later page from its
                           own first entry                               */
} fm_go_t;

/**
 * @brief One pane.  The fields are the pane's; read them, change them
 *        through the functions.
 */
typedef struct {
    char dir[FM_PATH_SIZE];         /**< the folder, a path from the root */
    fm_entry_t page[FM_PAGE_MAX];   /**< the page on show                 */
    int held;        /**< rows on the page                                */
    int ahead;       /**< entries of the folder ahead of the first row    */
    int total;       /**< entries of the folder listed; -1 with no card   */
    unsigned unlisted; /**< entries not listed: too long, or hidden       */
    int selected;    /**< the selected row, -1 for none                   */
    unsigned mount;  /**< the mount the page was read from                */
} fm_tree_t;

/**
 * @brief Start a pane in folder @p dir, with nothing read yet.
 * @param tree  the pane
 * @param dir   the folder, a path from the root; NULL is the root.  A path
 *              fm_path_check() refuses is the root.
 */
void fm_tree_init(fm_tree_t *tree, const char *dir);

/**
 * @brief Read a page of the pane's folder.
 *
 * The selection stays on its name when the name is on the new page, and
 * is cleared otherwise.  A page past the folder's end, which a file
 * deleted at the edge leaves, is the last page.
 *
 * @param tree   the pane
 * @param io     the card
 * @param bench  what the rules need; may be NULL
 * @param go     which page
 * @return FM_OK; FM_ERR_NOT_FOUND for a folder not there, with an empty
 *         page; FM_ERR_NO_CARD; FM_ERR_CARD_GONE when the card was gone at
 *         both walks; FM_ERR_IO; FM_ERR_ARG for a NULL
 */
fm_err_t fm_tree_page(fm_tree_t *tree, const fm_io_t *io,
                      const fm_bench_t *bench, fm_go_t go);

/**
 * @brief Enter the folder on row @p row and read its first page.
 *
 * On a failure the pane stays in its folder and reads its page again.
 *
 * @param tree   the pane
 * @param io     the card
 * @param bench  what the rules need; may be NULL
 * @param row    a row of the page
 * @return FM_OK; FM_ERR_ARG for a row that is not a folder;
 *         FM_ERR_NAME for a path fm_path_join() refuses, too long or
 *         too deep; what fm_tree_page()
 *         answers for the new folder
 */
fm_err_t fm_tree_enter(fm_tree_t *tree, const fm_io_t *io,
                       const fm_bench_t *bench, int row);

/**
 * @brief Go to the folder above and read its first page, with the folder
 *        come from selected when it is on that page.
 * @param tree   the pane
 * @param io     the card
 * @param bench  what the rules need; may be NULL
 * @return FM_OK; FM_ERR_ARG in the root; what fm_tree_page() answers
 */
fm_err_t fm_tree_up(fm_tree_t *tree, const fm_io_t *io,
                    const fm_bench_t *bench);

/**
 * @brief Select row @p row, or nothing for -1.
 * @param tree  the pane
 * @param row   a row of the page, or -1
 * @return FM_OK; FM_ERR_ARG for a row not on the page
 */
fm_err_t fm_tree_select(fm_tree_t *tree, int row);

/**
 * @brief The selected entry.
 * @param tree  the pane
 * @return the entry, or NULL with none selected
 */
const fm_entry_t *fm_tree_selected(const fm_tree_t *tree);

/**
 * @brief Whether a page stands before the one on show.
 * @param tree  the pane
 * @return true when PREV has somewhere to go
 */
bool fm_tree_has_prev(const fm_tree_t *tree);

/**
 * @brief Whether a page stands after the one on show.
 * @param tree  the pane
 * @return true when NEXT has somewhere to go
 */
bool fm_tree_has_next(const fm_tree_t *tree);

/**
 * @brief What the page on show says of a file in the pane's folder.
 */
typedef enum {
    FM_HAS_NO = 0,   /**< the page spans where it would stand: not there  */
    FM_HAS_YES,      /**< a file of that name, case folded, is on the page */
    FM_HAS_UNKNOWN   /**< it would stand on another page                  */
} fm_has_t;

/**
 * @brief Whether the folder holds file @p name, as far as the page on show
 *        tells.
 *
 * Names compare with case folded, as FAT finds them; a folder of that name
 * is not the file.  A name that ranks before the page's first row with
 * entries ahead of the page, or after its last row with entries after it,
 * is FM_HAS_UNKNOWN: another page holds where it would stand.
 *
 * @param tree  the pane
 * @param name  the file's name
 * @return FM_HAS_YES, FM_HAS_NO or FM_HAS_UNKNOWN; FM_HAS_UNKNOWN for a
 *         NULL, for an empty name, for a name of FM_NAME_BUF bytes or
 *         more and for a pane with no card (total -1)
 */
fm_has_t fm_tree_has_file(const fm_tree_t *tree, const char *name);

/**
 * @brief The pane's order of two entries: folders first, then names with
 *        case folded, then their bytes.  Strict: zero only for one name.
 * @param a  an fm_entry_t
 * @param b  an fm_entry_t
 * @return negative when @p a stands above @p b
 */
int fm_tree_rank(const void *a, const void *b);

#ifdef __cplusplus
}
#endif
