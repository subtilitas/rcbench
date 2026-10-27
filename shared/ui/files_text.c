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
 * SPDX-License-Identifier: MIT
 */

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
    fs_text_close();
    text_view_begin(dir, name, size);
    s.tv_need = true;
    s.tv_mount = mount;
    s.tv_from_logs = from_logs;
    s.view = FILES_VIEW_TEXT;
    fs_cancel_press();
    fs_quiet();
    fs_pump();
    files_screen_invalidate();
    return FM_OK;
}

bool fs_text_send(void)
{
    if (!s.tv_need || s.busy) {
        return false;
    }
    s.tv_need = false;
    size_t cap = 0u;
    uint8_t *area = text_view_load_area(&cap);
    memset(&s.req, 0, sizeof(s.req));
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
