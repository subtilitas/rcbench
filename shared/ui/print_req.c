/**
 * @file print_req.c
 * @brief The label printer's frame-task side (print_ctl.h): the requests
 *        the screens post, the view they read, and the error texts.  The
 *        controller (print_ctl.c) runs on the `blec` task.
 *
 * SPDX-License-Identifier: MIT
 */
#include "print_ctl.h"

#include <string.h>

/**
 * @brief Copy a text into a field, cut to fit.
 * @param dst The field.
 * @param n   Its size.
 * @param src The text; NULL is "".
 */
static void copy_text(char *dst, size_t n, const char *src)
{
    if (src == NULL) {
        src = "";
    }
    size_t k = strlen(src);
    if (k >= n) {
        k = n - 1u;
    }
    memcpy(dst, src, k);
    dst[k] = '\0';
}

ui_text_id_t print_err_text(uint8_t err)
{
    static const ui_text_id_t k[PRINT_ERR_COUNT] = {
        [PRINT_OK] = TX_PRINT_ERR_SILENT,
        [PRINT_ERR_NO_PRINTER] = TX_PRINT_ERR_NO_PRINTER,
        [PRINT_ERR_NOT_FOUND] = TX_PRINT_ERR_NOT_FOUND,
        [PRINT_ERR_RADIO] = TX_PRINT_ERR_RADIO,
        [PRINT_ERR_NOT_PRINTER] = TX_PRINT_ERR_NOT_PRINTER,
        [PRINT_ERR_MTU] = TX_PRINT_ERR_MTU,
        [PRINT_ERR_COVER] = TX_PRINT_ERR_COVER,
        [PRINT_ERR_NO_LABEL] = TX_PRINT_ERR_NO_LABEL,
        [PRINT_ERR_LABEL] = TX_PRINT_ERR_LABEL,
        [PRINT_ERR_BATTERY] = TX_PRINT_ERR_BATTERY,
        [PRINT_ERR_HOT] = TX_PRINT_ERR_HOT,
        [PRINT_ERR_BUSY] = TX_PRINT_ERR_BUSY,
        [PRINT_ERR_SILENT] = TX_PRINT_ERR_SILENT,
        [PRINT_ERR_WRITE] = TX_PRINT_ERR_WRITE,
        [PRINT_ERR_LINK] = TX_PRINT_ERR_LINK,
        [PRINT_ERR_LINK_NOT_PRINTED] = TX_PRINT_ERR_LINK_NOT_PRINTED,
        [PRINT_ERR_NOT_PRINTED] = TX_PRINT_ERR_NOT_PRINTED,
        [PRINT_ERR_ROLL] = TX_PRINT_ERR_ROLL,
        [PRINT_ERR_SMALL] = TX_PRINT_ERR_SMALL,
        [PRINT_ERR_LARGE] = TX_PRINT_ERR_LARGE,
        [PRINT_ERR_ABORTED] = TX_PRINT_ERR_ABORTED,
        [PRINT_ERR_UPDATE] = TX_PRINT_ERR_UPDATE,
        [PRINT_ERR_MEMORY] = TX_PRINT_ERR_MEMORY,
        [PRINT_ERR_ARMED] = TX_PRINT_ERR_ARMED,
    };
    return err < PRINT_ERR_COUNT ? k[err] : TX_PRINT_ERR_SILENT;
}

bool print_view_busy(const print_view_t *v)
{
    return v != NULL && v->phase >= PRINT_CONNECTING
           && v->phase <= PRINT_CHECKING;
}

/* The frame task's side. */

/** @brief The request that waits for the controller's task. */
static print_req_t  s_req;
/** @brief s_req holds one. */
static bool         s_req_due;
/** @brief An abort waits. */
static bool         s_abort_due;
/** @brief The controller's last view as the glue kept it. */
static print_view_t s_view = { .left = -1 };

/**
 * @brief Queue a request when none waits and none is in work.
 * @param r The request.
 * @return true when queued.
 */
static bool post(const print_req_t *r)
{
    if (s_req_due || s_abort_due || print_view_busy(&s_view)) {
        return false;
    }
    s_req = *r;
    s_req_due = true;
    return true;
}

bool print_ctl_label(const char *id, const char *dut)
{
    servo_id_t sid;
    if (id == NULL || !servo_id_parse(id, &sid)) {
        return false;
    }
    print_req_t r;
    memset(&r, 0, sizeof(r));
    r.kind = PRINT_REQ_LABEL;
    copy_text(r.id, sizeof(r.id), id);
    copy_text(r.dut, sizeof(r.dut), dut);
    return post(&r);
}

bool print_ctl_test(void)
{
    print_req_t r;
    memset(&r, 0, sizeof(r));
    r.kind = PRINT_REQ_TEST;
    return post(&r);
}

void print_ctl_query(void)
{
    print_req_t r;
    memset(&r, 0, sizeof(r));
    r.kind = PRINT_REQ_QUERY;
    (void)post(&r);
}

bool print_ctl_scan(void)
{
    if (s_view.scanning) {
        return false;
    }
    print_req_t r;
    memset(&r, 0, sizeof(r));
    r.kind = PRINT_REQ_SCAN;
    return post(&r);
}

void print_ctl_abort(void)
{
    s_req_due = false;
    s_abort_due = true;
}

bool print_ctl_take(print_req_t *out)
{
    if (out == NULL) {
        return false;
    }
    if (s_abort_due) {
        s_abort_due = false;
        memset(out, 0, sizeof(*out));
        out->kind = PRINT_REQ_ABORT;
        return true;
    }
    if (!s_req_due) {
        return false;
    }
    *out = s_req;
    s_req_due = false;
    return true;
}

void print_ctl_show(const print_view_t *v)
{
    if (v != NULL) {
        s_view = *v;
    }
}

const print_view_t *print_ctl_view(void)
{
    return &s_view;
}

void print_ctl_reset(void)
{
    memset(&s_req, 0, sizeof(s_req));
    s_req_due = false;
    s_abort_due = false;
    memset(&s_view, 0, sizeof(s_view));
    s_view.left = -1;
}
