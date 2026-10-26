/**
 * @file files_screen.c
 * @brief FILES: the state, the requests to the card's task, the footer's
 *        operations and the screen's interface (files_screen.h).
 *        files_draw.c draws the screen, files_events.c takes its touch
 *        events, and files_int.h is what the three share.
 *
 * Every card call of the screen is a request (fm_req.h) on the card's task.
 * The listings wait in a queue of one per pane; INFO and a job go out when
 * nothing else is under way.  An answer is taken in tick(), and a job's
 * answer reads both panes again, since a copy or a move changes the other
 * pane's folder too.
 *
 * SPDX-License-Identifier: MIT
 */

#include "files_screen.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "card_tree.h"
#include "fm_name.h"
#include "fm_ops.h"
#include "log_name.h"
#include "log_select.h"
#include "log_viewer_screen.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_textkey.h"

#include "files_int.h"

/** @brief The screen's state; files_int.h names it s. */
fs_state_t fs_s;

/** @brief A pane's request waiting for the card's task. */
typedef enum {
    NEED_NONE = 0,
    NEED_PAGE,    /**< a page: need_go                  */
    NEED_UP,      /**< the folder above                 */
    NEED_ENTER    /**< the folder on the selected row   */
} need_t;

/** @brief What each pane waits for (need_t), beside s.need_go. */
static need_t s_need[2];

/* ------------------------------------------------------------ the bench */

/**
 * @brief The bench as the gate and the rules see it now: the io's, ARMED
 *        also when the band says so, and the log folders in force.
 */
static void read_bench(void)
{
    snprintf(s.user_dir, sizeof(s.user_dir), "%s",
             log_viewer_tab_dir(LOG_TAB_USER));
    snprintf(s.auto_dir, sizeof(s.auto_dir), "%s",
             log_viewer_tab_dir(LOG_TAB_BENCH));
    memset(&s.bench, 0, sizeof(s.bench));
    s.bench.link_up = true;
    if (s.have_io && s.io.bench != NULL) {
        s.io.bench(&s.bench, s.io.ctx);
    }
    const ui_bench_status_t *st = ui_router_status();
    if (st != NULL && st->armed) {
        s.bench.armed = true;
    }
    s.bench.user_logs = s.user_dir;
    s.bench.auto_logs = s.auto_dir;
}

/**
 * @brief Whether the io can take requests.
 * @return true with an io that has submit() and idle()
 */
static bool io_ok(void)
{
    return s.have_io && s.io.submit != NULL && s.io.idle != NULL;
}

/* -------------------------------------------------------------- message */

/**
 * @brief Copy @p from into the message's argument @p to, "" for NULL.
 * @param to    the argument, FM_NAME_BUF bytes
 * @param from  the name, or NULL
 */
static void keep_arg(char to[FM_NAME_BUF], const char *from)
{
    snprintf(to, FM_NAME_BUF, "%s", (from != NULL) ? from : "");
}

void fs_say(bool ok, ui_text_id_t id, const char *a, const char *b)
{
    s.msg_kind = FS_MSG_TEXT;
    s.msg_id = id;
    s.msg_err = FM_OK;
    keep_arg(s.msg_a, a);
    keep_arg(s.msg_b, b);
    s.message_ok = ok;
    files_screen_invalidate();
}

void fs_say_err(fm_err_t err, const char *name)
{
    s.msg_kind = FS_MSG_ERR;
    s.msg_err = err;
    keep_arg(s.msg_a, name);
    s.msg_b[0] = '\0';
    s.message_ok = false;
    files_screen_invalidate();
}

void fs_quiet(void)
{
    s.msg_kind = FS_MSG_NONE;
}

const char *fs_message(char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    buf[0] = '\0';
    if (s.msg_kind == FS_MSG_ERR) {
        fs_err_text(s.msg_err, s.msg_a, buf, n);
    } else if (s.msg_kind == FS_MSG_TEXT) {
        /* Each format by its own ID, so tools/check_formats.py sees it. */
        switch (s.msg_id) {
        case TX_FM_COPIED:   snprintf(buf, n, TR(FM_COPIED), s.msg_a); break;
        case TX_FM_MOVED:    snprintf(buf, n, TR(FM_MOVED), s.msg_a); break;
        case TX_FM_RENAMED:
            snprintf(buf, n, TR(FM_RENAMED), s.msg_a, s.msg_b);
            break;
        case TX_LOG_DELETED: snprintf(buf, n, TR(LOG_DELETED), s.msg_a); break;
        case TX_FM_MADE:     snprintf(buf, n, TR(FM_MADE), s.msg_a); break;
        default:             snprintf(buf, n, "%s", ui_tr(s.msg_id)); break;
        }
    }
    return buf;
}

void fs_err_text(fm_err_t err, const char *name, char *out, size_t n)
{
    const char *nm = (name != NULL) ? name : "";
    switch (err) {
    case FM_ERR_NO_CARD:    snprintf(out, n, "%s", TR(FM_E_NO_CARD)); break;
    case FM_ERR_CARD_GONE:  snprintf(out, n, "%s", TR(FM_E_CARD_GONE)); break;
    case FM_ERR_NOT_FOUND:  snprintf(out, n, TR(FM_E_NOT_FOUND), nm); break;
    case FM_ERR_EXISTS:     snprintf(out, n, TR(FM_E_EXISTS), nm); break;
    case FM_ERR_FULL:       snprintf(out, n, "%s", TR(FM_E_FULL)); break;
    case FM_ERR_DENIED:     snprintf(out, n, "%s", TR(FM_E_DENIED)); break;
    case FM_ERR_BUSY:       snprintf(out, n, TR(FM_E_BUSY), nm); break;
    case FM_ERR_FIXED:      snprintf(out, n, TR(FM_E_FIXED), nm); break;
    case FM_ERR_LOCKED:     snprintf(out, n, TR(FM_E_LOCKED), nm); break;
    case FM_ERR_NAME:       snprintf(out, n, "%s", TR(FM_E_NAME)); break;
    case FM_ERR_NOT_EMPTY:  snprintf(out, n, TR(FM_E_NOT_EMPTY), nm); break;
    case FM_ERR_SAME:       snprintf(out, n, "%s", TR(FM_E_SAME)); break;
    case FM_ERR_ARMED:      snprintf(out, n, "%s", TR(FM_GATE_ARMED)); break;
    case FM_ERR_LOGGING:    snprintf(out, n, "%s", TR(FM_GATE_LOGGING)); break;
    case FM_ERR_CANCELLED:  snprintf(out, n, "%s", TR(FM_E_CANCELLED)); break;
    case FM_ERR_ARG:        snprintf(out, n, TR(FM_E_ARG), nm); break;
    case FM_ERR_IO:
    case FM_ERR_TOO_BIG:
    case FM_OK:
    case FM_ERR_COUNT:
    default:                snprintf(out, n, "%s", TR(FM_E_IO)); break;
    }
}

/* ------------------------------------------------------------- requests */

/**
 * @brief Hand s.req to the card's task, the log folders in force with it.
 * @return true when it is under way
 */
static bool submit(void)
{
    if (!io_ok() || s.busy) {
        return false;
    }
    snprintf(s.req.user_logs, sizeof(s.req.user_logs), "%s", s.user_dir);
    snprintf(s.req.auto_logs, sizeof(s.req.auto_logs), "%s", s.auto_dir);
    if (!s.io.submit(&s.req, s.io.ctx)) {
        /* With nothing under way, the task did not start. */
        fs_say(false, TX_LOG_ERR_MEMORY, NULL, NULL);
        return false;
    }
    s.busy = true;
    s.busy_s = 0.0f;
    s.prog_done = 0u;
    s.prog_total = 0u;
    s.cancel_asked = false;
    files_screen_invalidate();
    return true;
}

/**
 * @brief Pane @p i is to be read: a page, the folder above, or the folder
 *        on its selected row.  A newer wish replaces the one waiting.
 * @param i     the pane
 * @param need  what is read
 * @param go    for NEED_PAGE, which page
 */
static void want(int i, need_t need, fm_go_t go)
{
    s_need[i] = need;
    s.need[i] = (need != NEED_NONE);
    s.need_go[i] = go;
    if (need != NEED_NONE) {
        s.keep_place[i] = false;    /* RESCAN sets it after its wish */
    }
}

/**
 * @brief Both panes in their start folders, nothing read: the card's root
 *        and USER LOG FOLDER.
 */
static void start_panes(void)
{
    fm_tree_init(&s.pane[0], CARD_ROOT);
    fm_tree_init(&s.pane[1], s.user_dir);
    for (int i = 0; i < 2; ++i) {
        s.scroll[i] = 0;
        s.read[i] = false;
        s.select_after[i][0] = '\0';
        want(i, NEED_PAGE, FM_GO_FIRST);
    }
    s.mount = 0u;
}

/**
 * @brief Select @p name on pane @p i's page when it is there, and scroll
 *        the row into sight.
 * @param i     the pane
 * @param name  the name
 */
static void select_name(int i, const char *name)
{
    fm_tree_t *t = &s.pane[i];
    for (int r = 0; r < t->held; ++r) {
        if (strcmp(t->page[r].name, name) == 0) {
            (void)fm_tree_select(t, r);
            if (r < s.scroll[i]) {
                s.scroll[i] = r;
            } else if (r >= s.scroll[i] + FS_ROWS) {
                s.scroll[i] = r - FS_ROWS + 1;
            }
            return;
        }
    }
}

/**
 * @brief The row of pane @p t where entry @p e stands in the pane's order:
 *        @p e's own row, or the first row that ranks behind it.
 * @param t  the pane
 * @param e  the entry
 * @return the row; t->held when every row ranks ahead of @p e
 */
static int place_of(const fm_tree_t *t, const fm_entry_t *e)
{
    int r = 0;
    while (r < t->held && fm_tree_rank(&t->page[r], e) < 0) {
        ++r;
    }
    return r;
}

/** @brief What of a pane RESCAN's answer keeps: its top row on screen and
 *         its selected entry, copied before the answer replaces the page. */
typedef struct {
    bool have;           /**< the pane held rows                        */
    fm_entry_t top;      /**< the entry of the top row on screen        */
    bool have_sel;       /**< an entry was selected                     */
    fm_entry_t sel;      /**< the selected entry                        */
} place_t;

/**
 * @brief Pane @p i's place, as RESCAN keeps it.
 * @param i  the pane
 * @param p  takes the place
 */
static void place_note(int i, place_t *p)
{
    const fm_tree_t *t = &s.pane[i];
    memset(p, 0, sizeof(*p));
    if (!s.read[i] || t->held <= 0) {
        return;
    }
    const int top = (s.scroll[i] < t->held) ? s.scroll[i] : t->held - 1;
    p->have = true;
    p->top = t->page[(top > 0) ? top : 0];
    const fm_entry_t *sel = fm_tree_selected(t);
    if (sel != NULL) {
        p->have_sel = true;
        p->sel = *sel;
    }
}

/**
 * @brief RESCAN's answer for pane @p i keeps the pane's place: the entry
 *        that was the top row on screen stays the top row, and the
 *        selection stays on its entry (fm_tree_page() keeps it by name).
 *
 * A top row gone from the card leaves the entry now at its place on top.
 * A selected entry gone from the card hands the selection to the entry
 * now at its place, the next in the order; to the last row when it was
 * the folder's last entry; to none when the page is empty, or when every
 * row ranks ahead of it and a next page stands, where it may still be.
 * The scroll is held to the rows after (fs_clamp_scroll()).
 *
 * @param i  the pane
 * @param p  the place before the answer (place_note())
 */
static void place_keep(int i, const place_t *p)
{
    fm_tree_t *t = &s.pane[i];
    if (!p->have) {
        return;
    }
    s.scroll[i] = place_of(t, &p->top);
    if (!p->have_sel || t->selected >= 0 || t->held == 0) {
        return;
    }
    const int r = place_of(t, &p->sel);
    if (r < t->held) {
        (void)fm_tree_select(t, r);
    } else if (!fm_tree_has_next(t)) {
        (void)fm_tree_select(t, t->held - 1);
    }
}

/**
 * @brief The answer of a pane's request: the pane as the card's task read
 *        it.  A pane read from another card than the other's sends both
 *        back to their start folders.
 */
static void take_pane(void)
{
    const int i = s.req_pane;
    if (i < 0 || i > 1) {
        return;
    }
    const bool moved = (s.req.kind == FM_REQ_ENTER || s.req.kind == FM_REQ_UP);
    const fm_err_t err = s.req.err;
    const bool keep = s.req_keep && s.req.kind == FM_REQ_PAGE;
    place_t place = { .have = false };
    if (keep) {
        place_note(i, &place);
    }
    s.pane[i] = s.req.tree;
    s.read[i] = true;
    s.pane_err[i] = err;
    if (moved) {
        s.scroll[i] = 0;
        if (s.pane[i].selected >= 0) {
            select_name(i, s.pane[i].page[s.pane[i].selected].name);
        }
    } else if (keep) {
        place_keep(i, &place);
    }
    if (s.req.kind == FM_REQ_ENTER && err != FM_OK && err != FM_ERR_NO_CARD
        && err != FM_ERR_CARD_GONE) {
        /* The pane stays in its folder, read again (fm_tree_enter()). */
        fs_say_err(err, s.enter_name);
    }
    if (s.select_after[i][0] != '\0') {
        select_name(i, s.select_after[i]);
        s.select_after[i][0] = '\0';
    }
    fs_clamp_scroll(i);
    if (s.pane[i].total < 0) {
        return;                     /* no card: nothing to compare */
    }
    if (s.mount != 0u && s.pane[i].mount != s.mount) {
        /* Another card since the panes were read: its folders are not the
         * ones the panes show. */
        start_panes();
        s.need_look = false;
        return;
    }
    s.mount = s.pane[i].mount;
}

/** @brief The answer of a job: the message, and both panes read again. */
static void take_job(void)
{
    const fm_job_t *j = &s.req.job;
    const char *name = fm_path_base(j->src[0] != '\0' ? j->src : j->dst);
    char from[FM_NAME_BUF];
    snprintf(from, sizeof(from), "%s", name);
    const char *to = fm_path_base(j->dst);
    const int other = 1 - s.active;
    if (j->state == FM_JOB_DONE) {
        switch (j->kind) {
        case FM_JOB_COPY:
            fs_say(true, TX_FM_COPIED, from, NULL);
            snprintf(s.select_after[other], FM_NAME_BUF, "%s", from);
            break;
        case FM_JOB_MOVE:
            fs_say(true, TX_FM_MOVED, from, NULL);
            snprintf(s.select_after[other], FM_NAME_BUF, "%s", from);
            break;
        case FM_JOB_RENAME:
            fs_say(true, TX_FM_RENAMED, from, to);
            snprintf(s.select_after[s.active], FM_NAME_BUF, "%s", to);
            break;
        case FM_JOB_DELETE:
            fs_say(true, TX_LOG_DELETED, from, NULL);
            break;
        case FM_JOB_MKDIR:
        case FM_JOB_CREATE:
        default:
            fs_say(true, TX_FM_MADE, to, NULL);
            snprintf(s.select_after[s.active], FM_NAME_BUF, "%s", to);
            break;
        }
    } else if (j->state == FM_JOB_STOPPED && j->err == FM_ERR_ARMED) {
        fs_say(false, TX_FM_E_STOP_ARMED, NULL, NULL);
    } else if (j->state == FM_JOB_STOPPED && j->err == FM_ERR_LOGGING) {
        fs_say(false, TX_FM_E_STOP_LOGGING, NULL, NULL);
    } else {
        fs_say_err(s.req.err, from);
    }
    if (s.view == FILES_VIEW_JOB) {
        s.view = FILES_VIEW_PANES;
    }
    want(0, NEED_PAGE, FM_GO_SAME);
    want(1, NEED_PAGE, FM_GO_SAME);
}

/** @brief The answer of the request under way, once it is in. */
static void take_answer(void)
{
    s.busy = false;
    switch (s.req.kind) {
    case FM_REQ_PAGE:
    case FM_REQ_ENTER:
    case FM_REQ_UP:
        take_pane();
        break;
    case FM_REQ_INFO:
        s.info = s.req.info;
        s.info_err = s.req.err;
        break;
    case FM_REQ_JOB:
        take_job();
        break;
    case FM_REQ_NONE:
    default:
        break;
    }
    files_screen_invalidate();
}

/**
 * @brief The next pane's request out, when nothing is under way.
 */
static void send_next(void)
{
    for (int i = 0; i < 2 && !s.busy; ++i) {
        if (s_need[i] == NEED_NONE) {
            continue;
        }
        const need_t need = s_need[i];
        memset(&s.req, 0, sizeof(s.req));
        s.req.kind = (need == NEED_UP)      ? FM_REQ_UP
                     : (need == NEED_ENTER) ? FM_REQ_ENTER
                                            : FM_REQ_PAGE;
        s.req.tree = s.pane[i];
        s.req.go = s.need_go[i];
        s.req.row = s.pane[i].selected;
        if (need == NEED_ENTER) {
            const fm_entry_t *e = fm_tree_selected(&s.pane[i]);
            snprintf(s.enter_name, sizeof(s.enter_name), "%s",
                     (e != NULL) ? e->name : "");
        }
        s.req.look = s.need_look;
        s.req_pane = i;
        /* The request carries RESCAN's wish: a PAGE out before RESCAN, a
         * NEXT, is answered as it was asked. */
        s.req_keep = s.keep_place[i];
        s.keep_place[i] = false;
        want(i, NEED_NONE, FM_GO_FIRST);
        if (!submit()) {
            /* No task: no pane is read, and none waits. */
            want(0, NEED_NONE, FM_GO_FIRST);
            want(1, NEED_NONE, FM_GO_FIRST);
            s.read[0] = s.read[1] = true;
            s.pane[0].total = s.pane[1].total = -1;
            return;
        }
        s.need_look = false;
    }
}

/**
 * @brief Take the answer that is in, then send what waits.
 */
static void pump(void)
{
    if (!io_ok()) {
        /* No card's task: both panes say there is no card. */
        for (int i = 0; i < 2; ++i) {
            if (!s.read[i]) {
                s.read[i] = true;
                s.pane[i].total = -1;
                want(i, NEED_NONE, FM_GO_FIRST);
            }
        }
        return;
    }
    if (s.busy) {
        if (!s.io.idle(s.io.ctx)) {
            return;
        }
        take_answer();
    }
    send_next();
}

/* ------------------------------------------------------------- the rows */

const fm_entry_t *fs_selected(void)
{
    return fm_tree_selected(&s.pane[s.active]);
}

bool fs_paged(int i)
{
    const fm_tree_t *t = &s.pane[i];
    return s.read[i] && t->total > 0 && (fm_tree_has_prev(t)
                                         || fm_tree_has_next(t));
}

void fs_clamp_scroll(int i)
{
    int max = s.pane[i].held - FS_ROWS;
    if (max < 0) {
        max = 0;
    }
    if (s.scroll[i] > max) {
        s.scroll[i] = max;
    }
    if (s.scroll[i] < 0) {
        s.scroll[i] = 0;
    }
}

void fs_bar_thumb(int i, int *y, int *h)
{
    const gfx_rect_t t = FS_BAR(i);
    const int total = s.pane[i].held;
    if (total <= FS_ROWS) {
        *y = t.y;
        *h = t.h;
        return;
    }
    int th = t.h * FS_ROWS / total;
    if (th < FS_BAR_MIN) {
        th = FS_BAR_MIN;
    }
    const int travel = t.h - th;
    *y = t.y + travel * s.scroll[i] / (total - FS_ROWS);
    *h = th;
}

int fs_bar_scroll_at(int i, int top)
{
    const gfx_rect_t t = FS_BAR(i);
    const int total = s.pane[i].held;
    if (total <= FS_ROWS) {
        return 0;
    }
    int th = t.h * FS_ROWS / total;
    if (th < FS_BAR_MIN) {
        th = FS_BAR_MIN;
    }
    const int travel = t.h - th;
    int rel = top - t.y;
    if (rel < 0) {
        rel = 0;
    }
    if (rel > travel) {
        rel = travel;
    }
    const int span = total - FS_ROWS;
    return (rel * span + travel / 2) / travel;
}

/**
 * @brief The LOGS tab whose folder @p dir is, when LOGS shows the files of
 *        it.
 * @param dir  a folder
 * @param tab  takes the tab
 * @return true for the folder of ROOT, USER or BENCH
 */
static bool logs_tab_of(const char *dir, log_viewer_tab_t *tab)
{
    static const log_viewer_tab_t tabs[] = { LOG_TAB_USER, LOG_TAB_BENCH,
                                             LOG_TAB_ROOT };
    for (size_t k = 0; k < sizeof(tabs) / sizeof(tabs[0]); ++k) {
        if (fm_same_path(dir, log_viewer_tab_dir(tabs[k]))) {
            *tab = tabs[k];
            return true;
        }
    }
    return false;
}

/**
 * @brief Whether LOGS shows entry @p e of folder @p dir: a .csv file, or a
 *        report, in the folder of one of its tabs.  The names are the ones
 *        LOGS' list holds (log_select_lists()); a report is the name the
 *        list here labels one (fm_kind_of(), log_report_number()).
 * @param dir  the entry's folder
 * @param e    the entry
 * @return true when OPEN hands it to LOGS
 */
static bool logs_shows(const char *dir, const fm_entry_t *e)
{
    log_viewer_tab_t tab;
    return !e->is_dir && logs_tab_of(dir, &tab) && log_select_lists(e->name);
}

/* --------------------------------------------------------------- footer */

bool fs_foot_live(fs_foot_t b, fm_err_t *why)
{
    fm_err_t err = FM_OK;
    const fm_entry_t *e = fs_selected();
    const fm_tree_t *here = &s.pane[s.active];
    const fm_tree_t *there = &s.pane[1 - s.active];
    const bool card = s.read[s.active] && here->total >= 0;
    switch (b) {
    case FS_OPEN:
        if (e == NULL || (!e->is_dir && !logs_shows(here->dir, e))) {
            err = FM_ERR_ARG;
        } else if (!e->is_dir && fm_busy(&s.bench, here->dir, e->name)) {
            /* The logger's open file: LOGS leaves it off its list. */
            err = FM_ERR_BUSY;
        }
        break;
    case FS_COPY:
    case FS_MOVE:
        if (e == NULL) {
            err = FM_ERR_ARG;
        } else if (!s.read[1 - s.active] || there->total < 0) {
            err = FM_ERR_NO_CARD;
        } else {
            err = fm_allowed(&s.bench, (b == FS_COPY) ? FM_OP_COPY
                                                      : FM_OP_MOVE,
                             here->dir, e->name, e->is_dir);
            if (err == FM_OK && fm_same_path(here->dir, there->dir)) {
                err = FM_ERR_SAME;
            }
        }
        break;
    case FS_RENAME:
    case FS_DELETE:
        err = (e == NULL) ? FM_ERR_ARG
              : fm_allowed(&s.bench, (b == FS_RENAME) ? FM_OP_RENAME
                                                      : FM_OP_DELETE,
                           here->dir, e->name, e->is_dir);
        break;
    case FS_NEW:
        err = !card ? FM_ERR_NO_CARD : fm_gate(FM_OP_NEW, &s.bench);
        break;
    case FS_INFO:
        err = (e == NULL) ? FM_ERR_ARG : FM_OK;
        break;
    case FS_RESCAN:
    default:
        break;
    }
    if (why != NULL) {
        *why = err;
    }
    return err == FM_OK;
}

/**
 * @brief Start a job of s.req, filled by an fm_job_*() call that answered
 *        @p err: its box, or the refusal.
 * @param err   the start's answer
 * @param name  the entry's name, for a refusal's words
 */
static void start_job(fm_err_t err, const char *name)
{
    if (err != FM_OK) {
        fs_say_err(err, name);
        return;
    }
    s.req.kind = FM_REQ_JOB;
    s.req_pane = -1;
    fs_quiet();
    if (submit()) {
        s.view = FILES_VIEW_JOB;
    }
}

/**
 * @brief OPEN on a file LOGS shows: LOGS, on that file's tab, with it
 *        open.  A file LOGS' list does not hold now, one gone from the
 *        card since this list was read, stays here with FM_E_NOT_FOUND.
 * @param e  the entry
 */
static void open_in_logs(const fm_entry_t *e)
{
    log_viewer_tab_t tab;
    const log_viewer_show_t r =
        logs_tab_of(s.pane[s.active].dir, &tab)
            ? log_viewer_show(tab, e->name, s.pane[s.active].mount)
            : LOG_SHOW_NOT_LISTED;
    if (r == LOG_SHOW_OTHER_CARD) {
        fs_say_err(FM_ERR_CARD_GONE, e->name);
        return;
    }
    if (r != LOG_SHOW_OPEN) {
        fs_say_err(FM_ERR_NOT_FOUND, e->name);
        return;
    }
    ui_router_goto(SCREEN_LOGS);
}

void fs_foot(fs_foot_t b)
{
    read_bench();
    fm_err_t why = FM_OK;
    const fm_entry_t *e = fs_selected();
    const char *name = (e != NULL) ? e->name : "";
    if (b == FS_RESCAN) {
        /* Each pane's page again from its first entry, its place kept;
         * behind a request under way, as every listing waits. */
        fs_quiet();
        s.need_look = true;
        for (int i = 0; i < 2; ++i) {
            want(i, NEED_PAGE, FM_GO_SAME);
            s.keep_place[i] = true;
        }
        pump();
        files_screen_invalidate();
        return;
    }
    if (s.busy) {
        return;                    /* one request at a time */
    }
    if (!fs_foot_live(b, &why)) {
        if (e == NULL && why == FM_ERR_ARG) {
            fs_say(false, TX_FM_SELECT_FIRST, NULL, NULL);
        } else if (b == FS_OPEN && why == FM_ERR_ARG) {
            fs_say(false, TX_FM_OPEN_WHERE, NULL, NULL);
        } else {
            fs_say_err(why, name);
        }
        return;
    }
    if (e == NULL && b != FS_NEW) {
        return;                    /* fs_foot_live() asked for one */
    }
    const fm_tree_t *here = &s.pane[s.active];
    const fm_tree_t *there = &s.pane[1 - s.active];
    switch (b) {
    case FS_OPEN:
        if (e->is_dir) {
            fs_quiet();
            want(s.active, NEED_ENTER, FM_GO_FIRST);
            pump();
        } else {
            open_in_logs(e);
        }
        break;
    case FS_COPY:
        memset(&s.req, 0, sizeof(s.req));
        start_job(fm_job_copy(&s.req.job, &s.bench, here->mount, here->dir,
                              name, there->dir), name);
        break;
    case FS_MOVE:
        memset(&s.req, 0, sizeof(s.req));
        start_job(fm_job_move(&s.req.job, &s.bench, here->mount, here->dir,
                              name, e->is_dir, there->dir), name);
        break;
    case FS_RENAME:
        s.name_for = FS_NAME_RENAME;
        ui_textkey_open_file(&s.tk, FS_TK_AREA, TR(FM_TK_RENAME), name);
        s.view = FILES_VIEW_NAME;
        break;
    case FS_DELETE:
        s.doomed = *e;
        s.doomed_pane = s.active;
        fs_quiet();
        s.view = FILES_VIEW_DELETE;
        break;
    case FS_NEW:
        fs_quiet();
        s.view = FILES_VIEW_NEW;
        break;
    case FS_INFO:
        s.info_entry = *e;
        snprintf(s.info_dir, sizeof(s.info_dir), "%s", here->dir);
        memset(&s.info, 0, sizeof(s.info));
        s.info.mtime = -1;
        s.info_err = FM_OK;
        memset(&s.req, 0, sizeof(s.req));
        s.req.kind = FM_REQ_INFO;
        snprintf(s.req.dir, sizeof(s.req.dir), "%s", here->dir);
        snprintf(s.req.name, sizeof(s.req.name), "%s", name);
        s.req.is_dir = e->is_dir;
        s.req.mount = here->mount;
        s.req_pane = -1;
        if (submit()) {
            s.view = FILES_VIEW_INFO;
        }
        break;
    case FS_RESCAN:
    default:
        break;
    }
    files_screen_invalidate();
}

void fs_turn(int i, fm_go_t go, bool up)
{
    if (up) {
        if (s.pane[i].dir[0] == '\0') {
            return;                 /* the root has nothing above */
        }
        want(i, NEED_UP, FM_GO_FIRST);
    } else {
        want(i, NEED_PAGE, go);
        s.scroll[i] = 0;
    }
    s.active = i;
    fs_quiet();
    pump();
    files_screen_invalidate();
}

void fs_row_tap(int i, int row)
{
    fm_tree_t *t = &s.pane[i];
    if (row < 0 || row >= t->held) {
        return;
    }
    if (i == s.active && t->selected == row) {
        fs_foot(FS_OPEN);          /* a second tap opens */
        return;
    }
    s.active = i;
    (void)fm_tree_select(t, row);
    files_screen_invalidate();
}

void fs_delete_doomed(void)
{
    read_bench();
    s.view = FILES_VIEW_PANES;
    const int i = s.doomed_pane;
    if (s.busy || i < 0 || i > 1) {
        return;
    }
    memset(&s.req, 0, sizeof(s.req));
    start_job(fm_job_delete(&s.req.job, &s.bench, s.pane[i].mount,
                            s.pane[i].dir, s.doomed.name, s.doomed.is_dir),
              s.doomed.name);
}

void fs_new_name(bool folder)
{
    s.name_for = folder ? FS_NAME_FOLDER : FS_NAME_FILE;
    ui_textkey_open_file(&s.tk, FS_TK_AREA,
                         folder ? TR(FM_TK_FOLDER) : TR(FM_TK_FILE), "");
    s.view = FILES_VIEW_NAME;
    files_screen_invalidate();
}

void fs_name_typed(const char *name)
{
    read_bench();
    s.view = FILES_VIEW_PANES;
    if (s.busy) {
        return;
    }
    const fm_tree_t *here = &s.pane[s.active];
    memset(&s.req, 0, sizeof(s.req));
    fm_err_t err;
    switch (s.name_for) {
    case FS_NAME_FOLDER:
        err = fm_job_mkdir(&s.req.job, &s.bench, here->mount, here->dir,
                           name);
        break;
    case FS_NAME_FILE:
        err = fm_job_create(&s.req.job, &s.bench, here->mount, here->dir,
                            name);
        break;
    case FS_NAME_RENAME:
    default: {
        const fm_entry_t *e = fs_selected();
        if (e == NULL) {
            fs_say(false, TX_FM_SELECT_FIRST, NULL, NULL);
            return;
        }
        err = fm_job_rename(&s.req.job, &s.bench, here->mount, here->dir,
                            e->name, e->is_dir, name);
        break;
    }
    }
    start_job(err, name);
}

void fs_job_cancel(void)
{
    if (s.busy && s.req.kind == FM_REQ_JOB && s.io.cancel != NULL) {
        s.io.cancel(s.io.ctx);
        s.cancel_asked = true;
        files_screen_invalidate();
    }
}

/* ---------------------------------------------------------- the formats */

void files_size_text(char *out, size_t n, uint64_t bytes)
{
    if (out == NULL || n == 0u) {
        return;
    }
    if (bytes >= 1024ull * 1024u * 1024u) {
        snprintf(out, n, "%.2fGB",
                 (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ull * 1024u) {
        snprintf(out, n, "%.1fMB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024u) {
        snprintf(out, n, "%ukB", (unsigned)(bytes / 1024u));
    } else {
        snprintf(out, n, "%uB", (unsigned)bytes);
    }
}

void files_time_text(char *out, size_t n, int64_t mtime)
{
    if (out == NULL || n == 0u) {
        return;
    }
    if (mtime < 0) {
        out[0] = '\0';
        return;
    }
    /* Days to a civil date (proleptic Gregorian), H. Hinnant's
     * days_from_civil inverted; mtime is never negative here. */
    const int64_t days = mtime / 86400 + 719468;
    const int64_t secs = mtime % 86400;
    const int64_t era = days / 146097;
    const int64_t doe = days - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    const int64_t d = doy - (153 * mp + 2) / 5 + 1;
    const int64_t m = (mp < 10) ? mp + 3 : mp - 9;
    const int64_t y = yoe + era * 400 + ((m <= 2) ? 1 : 0);
    snprintf(out, n, "%04" PRId64 "-%02" PRId64 "-%02" PRId64
             " %02" PRId64 ":%02" PRId64, y, m, d, secs / 3600,
             (secs / 60) % 60);
}

/* --------------------------------------------------------- the interface */

void files_screen_set_io(const files_io_t *io)
{
    if (io != NULL) {
        s.io = *io;
        s.have_io = true;
    } else {
        memset(&s.io, 0, sizeof(s.io));
        s.have_io = false;
    }
}

files_view_t files_screen_view(void)
{
    return s.view;
}

int files_screen_active(void)
{
    return s.active;
}

const fm_tree_t *files_screen_pane(int i)
{
    return (i == 0 || i == 1) ? &s.pane[i] : NULL;
}

int files_screen_scroll(int i)
{
    return (i == 0 || i == 1) ? s.scroll[i] : 0;
}

const char *files_screen_message(void)
{
    static char line[FS_MSG_MAX];
    return fs_message(line, sizeof(line));
}

bool files_screen_busy(void)
{
    return s.busy;
}

void files_screen_invalidate(void)
{
    for (int i = 0; i < MAX_FBS; ++i) {
        s.valid[i] = false;
    }
}

/**
 * @brief The screen as a start leaves it: the card's root on the left,
 *        USER LOG FOLDER on the right, neither read.  The io stays; a
 *        request under way stays the card's task's.
 */
static void reset(void)
{
    const files_io_t io = s.io;
    const bool have = s.have_io;
    memset(&s, 0, sizeof(s));
    s.io = io;
    s.have_io = have;
    s.press = FS_HIT_NONE;
    s.req_pane = -1;
    s.doomed_pane = -1;
    want(0, NEED_NONE, FM_GO_FIRST);
    want(1, NEED_NONE, FM_GO_FIRST);
    fm_tree_init(&s.pane[0], CARD_ROOT);
    fm_tree_init(&s.pane[1], CARD_USER_LOGS);
}

/**
 * @brief On the way in: both panes read, from their first pages the first
 *        time and as they were left after that, the slot looked at first.
 */
static void enter(void)
{
    fs_cancel_press();
    read_bench();
    if (!s.read[0] && !s.read[1] && !s.busy && s.mount == 0u) {
        start_panes();
    } else {
        want(0, NEED_PAGE, FM_GO_SAME);
        want(1, NEED_PAGE, FM_GO_SAME);
    }
    s.need_look = true;
    pump();
    files_screen_invalidate();
}

/**
 * @brief On the way out: the keyboard and the boxes close; a job under way
 *        goes on, and its box shows again on the way back in.
 */
static void leave(void)
{
    fs_cancel_press();
    ui_textkey_close(&s.tk);
    if (s.view != FILES_VIEW_JOB || !s.busy) {
        s.view = FILES_VIEW_PANES;
    }
}

/**
 * @brief Time passed: the answer taken when it is in, the progress and the
 *        gate followed for the drawing.
 * @param dt_s  seconds since the last tick
 */
static void tick(float dt_s)
{
    const bool gate = fm_gate(FM_OP_COPY, &s.bench) != FM_OK;
    read_bench();
    if ((fm_gate(FM_OP_COPY, &s.bench) != FM_OK) != gate) {
        files_screen_invalidate();
    }
    if (s.busy) {
        const float before = s.busy_s;
        s.busy_s += (dt_s > 0.0f) ? dt_s : 0.0f;
        if (s.view == FILES_VIEW_JOB && before < FS_JOB_BOX_S
            && s.busy_s >= FS_JOB_BOX_S) {
            files_screen_invalidate();      /* the box shows now */
        }
        if (s.io.progress != NULL && s.req.kind == FM_REQ_JOB) {
            uint64_t done = 0u;
            uint64_t total = 0u;
            s.io.progress(&done, &total, s.io.ctx);
            if (done != s.prog_done || total != s.prog_total) {
                s.prog_done = done;
                s.prog_total = total;
                files_screen_invalidate();
            }
        }
    }
    const bool was = s.busy;
    pump();
    if (was != s.busy) {
        files_screen_invalidate();
    }
}

/**
 * @brief The home tag: a box or the keyboard closes, a job's box cancels
 *        a copy and stays; with the panes on top the screen is left.
 * @return true when the screen went up a level of its own
 */
static bool back(void)
{
    fs_cancel_press();
    switch (s.view) {
    case FILES_VIEW_NAME:
        ui_textkey_close(&s.tk);
        s.view = FILES_VIEW_PANES;
        break;
    case FILES_VIEW_DELETE:
    case FILES_VIEW_INFO:
    case FILES_VIEW_NEW:
        s.view = FILES_VIEW_PANES;
        break;
    case FILES_VIEW_JOB:
        if (s.busy) {
            fs_job_cancel();
        } else {
            s.view = FILES_VIEW_PANES;
        }
        break;
    case FILES_VIEW_PANES:
    default:
        return false;
    }
    files_screen_invalidate();
    return true;
}

/** @brief The screen's table for the router. */
static const ui_screen_t s_screen = {
    .title = "FILES",
    .reset = reset,
    .enter = enter,
    .leave = leave,
    .tick = tick,
    .event = fs_event,
    .cancel = fs_cancel_press,
    .render = fs_render,
    .back = back,
};

const ui_screen_t *files_screen(void)
{
    return &s_screen;
}
