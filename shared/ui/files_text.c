/**
 * @file files_text.c
 * @brief FILES as the text viewer's host: the file named, its read on the
 *        card's task, the answer handed to the viewer, and the viewer
 *        closed.  text_view.c is the viewer; files_screen.c the screen's
 *        state and its requests; files_int.h what they share.
 *
 * The read is a LOAD job (fm_job_load()) into the viewer's buffer, and
 * goes out before any listing: a read and a listing never run at once, so
 * the card's task alone writes the buffer while the viewer shows that it
 * reads.  Closed while its read runs, the viewer asks the job to stop at
 * its next chunk of 16KiB and drops its answer; a file opened then waits
 * until that answer is in, so no two reads write the buffer.
 *
 * The editor's SAVE is a SAVE job (fm_job_save()) that reads the text
 * through text_view_fill() while the editor holds it still.  An open of
 * another file over an editor with unsaved text, from FILES or from LOGS,
 * is held until the editor's question is answered: DISCARD opens it,
 * STAY drops it.  Leaving FILES keeps such an editor, its text and its
 * save, for the way back in.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "files_screen.h"
#include "fm_ops.h"
#include "text_view.h"
#include "ui_screen.h"

#include "files_int.h"

fm_err_t files_screen_open_text(const char *dir, const char *name,
                                unsigned mount, uint64_t size,
                                bool from_logs)
{
    if (!text_view_fits(size)) {
        return FM_ERR_TOO_BIG;
    }
    if (text_view_dirty() || text_view_saving()) {
        /* The editor's text first: the question, the open held. */
        s.tv_pend = true;
        s.tv_pend_logs = from_logs;
        s.tv_pend_mount = mount;
        s.tv_pend_size = size;
        snprintf(s.tv_pend_dir, sizeof(s.tv_pend_dir), "%s",
                 (dir != NULL) ? dir : "");
        snprintf(s.tv_pend_name, sizeof(s.tv_pend_name), "%s",
                 (name != NULL) ? name : "");
        text_view_ask_open();
        s.view = FILES_VIEW_TEXT;
        files_screen_invalidate();
        return FM_OK;
    }
    fs_text_close();
    text_view_begin(dir, name, size);
    s.tv_need = true;
    s.tv_mount = mount;
    s.tv_from_logs = from_logs;
    s.view = FILES_VIEW_TEXT;
    fs_cancel_press();
    fs_quiet();
    fs_read_bench();
    fs_text_rules();
    fs_pump();
    files_screen_invalidate();
    return FM_OK;
}

/**
 * @brief Hand the editor's save to the card's task, checked against the
 *        bench read again.
 * @return true when it went out
 */
static bool save_send(void)
{
    s.tv_save = false;
    fs_read_bench();
    memset(&s.req, 0, sizeof(s.req));
    const size_t len = text_view_save_len();
    const fm_err_t err = fm_job_save(&s.req.job, &s.bench, s.tv_mount,
                                     text_view_dir(), text_view_name(), len,
                                     text_view_fill, NULL);
    if (err != FM_OK) {
        text_view_saved(err, false, false);
        return false;
    }
    s.req.kind = FM_REQ_JOB;
    s.req_pane = -1;
    if (!fs_submit()) {
        text_view_saved(FM_ERR_IO, false, false);
        return false;
    }
    s.tv_saving = true;
    return true;
}

bool fs_text_send(void)
{
    if (s.tv_save && !s.busy) {
        return save_send();
    }
    if (!s.tv_need || s.busy) {
        return false;
    }
    s.tv_need = false;
    size_t cap = 0u;
    uint8_t *area = text_view_load_area(&cap);
    memset(&s.req, 0, sizeof(s.req));
    /* LOGS opens a report with FILES off screen: the bench FILES last
     * read can still hold a run that has ended since. */
    fs_read_bench();
    const fm_err_t err = fm_job_load(&s.req.job, &s.bench, s.tv_mount,
                                     text_view_dir(), text_view_name(), area,
                                     cap);
    if (err != FM_OK) {
        text_view_loaded(err, 0u);
        return false;
    }
    s.req.kind = FM_REQ_JOB;
    s.req_pane = -1;
    if (!fs_submit()) {
        text_view_loaded(FM_ERR_IO, 0u);
        return false;
    }
    s.tv_live = true;
    return true;
}

void fs_text_take(void)
{
    if (!s.tv_live) {
        return;                     /* the viewer was closed under it */
    }
    s.tv_live = false;
    const uint64_t done = s.req.job.done;
    text_view_loaded(s.req.err, (size_t)done);
}

void fs_text_fail(fm_err_t err)
{
    if (s.tv_need) {
        s.tv_need = false;
        text_view_loaded(err, 0u);
    }
    if (s.tv_save) {
        s.tv_save = false;
        text_view_saved(err, false, false);
    }
}

void fs_text_saved(void)
{
    if (!s.tv_saving) {
        return;
    }
    s.tv_saving = false;
    const fm_job_t *j = &s.req.job;
    text_view_saved(s.req.err, j->state == FM_JOB_STOPPED, j->note_restart);
}

void fs_text_result(text_view_result_t r)
{
    switch (r) {
    case TEXT_VIEW_SAVE:
        s.tv_save = true;
        fs_pump();
        break;
    case TEXT_VIEW_LEAVE:
        if (s.tv_pend) {
            s.tv_pend = false;
            (void)files_screen_open_text(s.tv_pend_dir, s.tv_pend_name,
                                         s.tv_pend_mount, s.tv_pend_size,
                                         s.tv_pend_logs);
        }
        break;
    case TEXT_VIEW_NONE:
        return;
    case TEXT_VIEW_CHANGED:
    default:
        break;
    }
    files_screen_invalidate();
}

void fs_text_rules(void)
{
    if (s.view != FILES_VIEW_TEXT) {
        return;
    }
    const char *dir = text_view_dir();
    const char *name = text_view_name();
    if (text_view_set_rules(
            fm_allowed(&s.bench, FM_OP_EDIT, dir, name, false),
            fm_allowed(&s.bench, FM_OP_SAVE, dir, name, false))) {
        files_screen_invalidate();
    }
}

bool fs_text_back(void)
{
    const bool took = text_view_back();
    if (text_view_ask() != TEXT_VIEW_ASK_OPEN) {
        s.tv_pend = false;          /* STAY: the open is dropped */
    }
    return took;
}

bool fs_text_stays(void)
{
    if (text_view_dirty() || text_view_saving()) {
        text_view_cancel_press();
        return true;
    }
    fs_text_close();
    return false;
}

void fs_text_close(void)
{
    if (s.tv_live && s.io.cancel != NULL) {
        s.io.cancel(s.io.ctx);
    }
    s.tv_live = false;
    s.tv_need = false;
    text_view_close();
    if (s.view == FILES_VIEW_TEXT) {
        s.view = FILES_VIEW_PANES;
    }
}
