/**
 * @file fm_tree.c
 * @brief One pane of the file manager: its folder, its page and its
 *        selection (fm_tree.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "fm_tree.h"

#include <string.h>

#include "fm_select.h"
#include "log_name.h"

/**
 * @brief One walk of the pane's folder on its way into the page.
 */
typedef struct {
    fm_select_t sel;           /**< the page being kept                  */
    const fm_bench_t *bench;   /**< what the rules need                  */
    const char *dir;           /**< the folder walked                    */
    int listed;                /**< entries offered                      */
    unsigned long_names;       /**< names too long to list               */
} pane_walk_t;

int fm_tree_rank(const void *a, const void *b)
{
    const fm_entry_t *x = (const fm_entry_t *)a;
    const fm_entry_t *y = (const fm_entry_t *)b;
    if (x->is_dir != y->is_dir) {
        return x->is_dir ? -1 : 1;
    }
    const int c = log_name_case_cmp(x->name, y->name);
    return (c != 0) ? c : strcmp(x->name, y->name);
}

/**
 * @brief Offer one entry of a walk to the page: every name that fits, with
 *        what the rules say of it; a longer one is counted.
 * @param e     the entry, borrowed for the call
 * @param vctx  the walk's pane_walk_t
 */
static void take(const fm_dirent_t *e, void *vctx)
{
    pane_walk_t *w = (pane_walk_t *)vctx;
    if (e == NULL || e->name == NULL || e->name[0] == '\0'
        || strcmp(e->name, ".") == 0 || strcmp(e->name, "..") == 0) {
        return;
    }
    const size_t n = strlen(e->name);
    if (n > FM_LIST_NAME_MAX) {
        /* A cut name opens nothing, or another file. */
        ++w->long_names;
        return;
    }
    fm_entry_t f;
    memset(&f, 0, sizeof(f));
    memcpy(f.name, e->name, n + 1u);
    f.is_dir = e->is_dir;
    f.size = e->is_dir ? 0u : e->size;
    f.kind = (uint8_t)fm_kind_of(w->dir, f.name, f.is_dir);
    f.place = (uint8_t)fm_place_of(w->bench, w->dir, f.name, f.is_dir);
    f.busy = !f.is_dir && fm_busy(w->bench, w->dir, f.name);
    ++w->listed;
    fm_select_offer(&w->sel, &f);
}

/**
 * @brief Empty the page, for a folder that could not be read.
 * @param tree   the pane
 * @param total  the count to show: 0 for a folder not there, -1 for no
 *               card
 */
static void empty_page(fm_tree_t *tree, int total)
{
    tree->held = 0;
    tree->ahead = 0;
    tree->total = total;
    tree->unlisted = 0u;
}

/**
 * @brief One walk of the folder on the mount in the slot, keeping one page.
 * @param tree   the pane
 * @param io     the card
 * @param bench  what the rules need; may be NULL
 * @param side   the side of @p key the page holds
 * @param key    the entry at the page's edge, the caller's copy; NULL for
 *               none
 * @return FM_OK or why the walk failed
 */
static fm_err_t walk_once(fm_tree_t *tree, const fm_io_t *io,
                          const fm_bench_t *bench, fm_page_dir_t side,
                          const fm_entry_t *key)
{
    if (!io->mounted(io->ctx)) {
        return FM_ERR_NO_CARD;
    }
    const unsigned mount = io->mounts(io->ctx);
    if (!io->hold(io->ctx, mount)) {
        return FM_ERR_CARD_GONE;
    }
    pane_walk_t w;
    memset(&w, 0, sizeof(w));
    w.bench = bench;
    w.dir = tree->dir;
    fm_select_begin(&w.sel, tree->page, sizeof(tree->page[0]), FM_PAGE_MAX,
                    fm_tree_rank, side, key);
    unsigned unlisted = 0u;
    fm_err_t err = io->walk(io->ctx, tree->dir, take, &w, &unlisted);
    io->release(io->ctx);
    if (err == FM_OK && io->mounts(io->ctx) != mount) {
        /* Another task mounted another card while the walk held this one:
         * what was walked is the old card's. */
        err = FM_ERR_CARD_GONE;
    }
    int ahead = 0;
    tree->held = fm_select_end(&w.sel, NULL, &ahead);
    tree->ahead = ahead;
    tree->total = w.listed;
    tree->unlisted = unlisted + w.long_names;
    tree->mount = mount;
    return err;
}

/**
 * @brief One page, walked again once when the walk found the card gone:
 *        the second walk is of the card in the slot.
 * @param tree   the pane
 * @param io     the card
 * @param bench  what the rules need; may be NULL
 * @param side   the side of @p key the page holds
 * @param key    the entry at the page's edge, the caller's copy; NULL for
 *               none
 * @return FM_OK or why not
 */
static fm_err_t walk_twice(fm_tree_t *tree, const fm_io_t *io,
                           const fm_bench_t *bench, fm_page_dir_t side,
                           const fm_entry_t *key)
{
    fm_err_t err = FM_ERR_CARD_GONE;
    for (int walks = 0; walks < 2 && err == FM_ERR_CARD_GONE; ++walks) {
        err = walk_once(tree, io, bench, side, key);
    }
    return err;
}

/**
 * @brief Read one page, and fall back to the first or the last page when
 *        the page asked for holds nothing of a folder that is not empty.
 *        Each walk is walked again once when it found the card gone.
 * @param tree   the pane
 * @param io     the card
 * @param bench  what the rules need; may be NULL
 * @param side   the side of @p key the page holds
 * @param key    the entry at the page's edge, the caller's copy; NULL for
 *               none
 * @return FM_OK or why not
 */
static fm_err_t read_page(fm_tree_t *tree, const fm_io_t *io,
                          const fm_bench_t *bench, fm_page_dir_t side,
                          const fm_entry_t *key)
{
    fm_err_t err = walk_twice(tree, io, bench, side, key);
    if (err == FM_OK && tree->held == 0 && tree->total > 0) {
        /* Nothing on the page's side of its key: before the first entry
         * that is the first page, after the last the last page. */
        const fm_page_dir_t again = (side == FM_PAGE_BEFORE && key != NULL)
                                        ? FM_PAGE_FROM
                                        : FM_PAGE_BEFORE;
        err = walk_twice(tree, io, bench, again, NULL);
    }
    if (err == FM_ERR_NOT_FOUND) {
        empty_page(tree, 0);
    } else if (err != FM_OK) {
        empty_page(tree, -1);
    }
    return err;
}

/**
 * @brief Select the row that holds @p name, or nothing.
 * @param tree  the pane
 * @param name  the name to find; "" for none
 */
static void reselect(fm_tree_t *tree, const char *name)
{
    tree->selected = -1;
    if (name[0] == '\0') {
        return;
    }
    for (int i = 0; i < tree->held; ++i) {
        if (strcmp(tree->page[i].name, name) == 0) {
            tree->selected = i;
            return;
        }
    }
}

void fm_tree_init(fm_tree_t *tree, const char *dir)
{
    if (tree == NULL) {
        return;
    }
    memset(tree, 0, sizeof(*tree));
    if (dir != NULL && fm_path_check(dir) == FM_PATH_OK
        && strlen(dir) < sizeof(tree->dir)) {
        memcpy(tree->dir, dir, strlen(dir) + 1u);
    }
    tree->selected = -1;
}

fm_err_t fm_tree_page(fm_tree_t *tree, const fm_io_t *io,
                      const fm_bench_t *bench, fm_go_t go)
{
    if (tree == NULL || io == NULL) {
        return FM_ERR_ARG;
    }
    /* The key is copied out of the page: the walk writes the page. */
    fm_entry_t key;
    const fm_entry_t *edge = NULL;
    fm_page_dir_t side = FM_PAGE_FROM;
    const bool rows = tree->held > 0;
    switch (go) {
    case FM_GO_NEXT:
        if (rows) {
            key = tree->page[tree->held - 1];
            edge = &key;
            side = FM_PAGE_AFTER;
        }
        break;
    case FM_GO_PREV:
        side = FM_PAGE_BEFORE;
        if (rows) {
            key = tree->page[0];
            edge = &key;
        }
        break;
    case FM_GO_LAST:
        side = FM_PAGE_BEFORE;
        break;
    case FM_GO_SAME:
        /* The first page stays the first: an entry new on the card that
         * ranks before its first row comes onto it.  A later page keeps
         * its first entry, and such an entry goes to the page before. */
        if (rows && tree->ahead > 0) {
            key = tree->page[0];
            edge = &key;
        }
        break;
    case FM_GO_FIRST:
    default:
        break;
    }
    char name[FM_NAME_BUF] = "";
    const fm_entry_t *sel = fm_tree_selected(tree);
    if (sel != NULL) {
        memcpy(name, sel->name, sizeof(name));
    }
    const fm_err_t err = read_page(tree, io, bench, side, edge);
    reselect(tree, name);
    return err;
}

fm_err_t fm_tree_enter(fm_tree_t *tree, const fm_io_t *io,
                       const fm_bench_t *bench, int row)
{
    if (tree == NULL || io == NULL || row < 0 || row >= tree->held
        || !tree->page[row].is_dir) {
        return FM_ERR_ARG;
    }
    char path[FM_PATH_SIZE];
    const fm_err_t joined = fm_path_err(fm_path_join(
        tree->dir, tree->page[row].name, true, path, sizeof(path)));
    if (joined != FM_OK) {
        return joined;
    }
    /* Kept to read the pane's page again when the folder does not read. */
    char old[FM_PATH_SIZE];
    memcpy(old, tree->dir, sizeof(old));
    const fm_entry_t key = tree->page[0];
    const fm_entry_t chosen = tree->page[row];

    memcpy(tree->dir, path, sizeof(tree->dir));
    tree->selected = -1;
    const fm_err_t err = read_page(tree, io, bench, FM_PAGE_FROM, NULL);
    if (err != FM_OK) {
        memcpy(tree->dir, old, sizeof(tree->dir));
        (void)read_page(tree, io, bench, FM_PAGE_FROM, &key);
        reselect(tree, chosen.name);
    }
    return err;
}

fm_err_t fm_tree_up(fm_tree_t *tree, const fm_io_t *io,
                    const fm_bench_t *bench)
{
    if (tree == NULL || io == NULL || tree->dir[0] == '\0') {
        return FM_ERR_ARG;
    }
    char child[FM_NAME_BUF] = "";
    const char *last = fm_path_base(tree->dir);
    if (strlen(last) < sizeof(child)) {
        memcpy(child, last, strlen(last) + 1u);
    }
    (void)fm_path_up(tree->dir);
    tree->selected = -1;
    const fm_err_t err = read_page(tree, io, bench, FM_PAGE_FROM, NULL);
    reselect(tree, child);
    return err;
}

fm_err_t fm_tree_select(fm_tree_t *tree, int row)
{
    if (tree == NULL || row < -1 || row >= tree->held) {
        return FM_ERR_ARG;
    }
    tree->selected = row;
    return FM_OK;
}

const fm_entry_t *fm_tree_selected(const fm_tree_t *tree)
{
    if (tree == NULL || tree->selected < 0 || tree->selected >= tree->held) {
        return NULL;
    }
    return &tree->page[tree->selected];
}

bool fm_tree_has_prev(const fm_tree_t *tree)
{
    return tree != NULL && tree->ahead > 0;
}

bool fm_tree_has_next(const fm_tree_t *tree)
{
    return tree != NULL && tree->ahead + tree->held < tree->total;
}
