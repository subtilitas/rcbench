/**
 * @file upd_fetch.c
 * @brief UPDATES' words for an image fetched on demand (upd_fetch.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "upd_fetch.h"

#include <stdio.h>

#include "ui_text.h"

bool upd_fetch_label(const upd_fetch_t *f, char *out, size_t n)
{
    if (f == NULL || out == NULL || n == 0u) {
        return false;
    }
    switch (f->run) {
    case UPD_FETCH_NET:
        snprintf(out, n, TR(UPD_FETCH_NET),
                 f->percent > 100u ? 100u : (unsigned)f->percent);
        return true;
    case UPD_FETCH_CHECK:
        snprintf(out, n, "%s", TR(UPD_FETCH_CHECK));
        return true;
    case UPD_FETCH_CARD:
        snprintf(out, n, "%s", TR(UPD_FETCH_CARD));
        return true;
    case UPD_FETCH_IDLE:
    default:
        break;
    }
    if (f->src == DEVIMG_SRC_CARD) {
        snprintf(out, n, "%s", TR(UPD_FETCH_HOLD_CARD));
        return true;
    }
    return false;
}

void upd_fetch_line(const upd_fetch_t *f, char *out, size_t n)
{
    if (out == NULL || n == 0u) {
        return;
    }
    out[0] = '\0';
    if (f == NULL || f->file[0] == '\0') {
        return;
    }
    switch (f->src) {
    case DEVIMG_SRC_RAM:
        snprintf(out, n, TR(UPD_FETCH_IN_RAM), f->file);
        break;
    case DEVIMG_SRC_CARD:
        snprintf(out, n, TR(UPD_FETCH_FROM_CARD), f->file);
        break;
    case DEVIMG_SRC_NET:
    default:
        snprintf(out, n, TR(UPD_FETCH_FROM_NET), f->file);
        break;
    }
}

void upd_fetch_fail_line(upd_fetch_fail_t kind, const char *code,
                         const char *reason, char *out, size_t n)
{
    if (out == NULL || n == 0u) {
        return;
    }
    const char *c = (code != NULL) ? code : "";
    const char *r = (reason != NULL) ? reason : "";
    /* Each kind's format is named at its call, so that
     * tools/check_formats.py holds it to its arguments. */
    switch (kind) {
    case UPD_FETCH_FAIL_CARD:
        snprintf(out, n, TR(UPD_FETCH_FAIL_CARD), c, r);
        break;
    case UPD_FETCH_FAIL_CHECK:
        snprintf(out, n, TR(UPD_FETCH_FAIL_CHECK), c, r);
        break;
    case UPD_FETCH_FAIL_NET:
    default:
        snprintf(out, n, TR(UPD_FETCH_FAIL_NET), c, r);
        break;
    }
}

void upd_fetch_card_text(devimg_card_t e, const char *path, char *out,
                         size_t n)
{
    if (out == NULL || n == 0u) {
        return;
    }
    switch (e) {
    case DEVIMG_CARD_NONE:
        snprintf(out, n, "%s", TR(UPD_FETCH_NO_CARD));
        break;
    case DEVIMG_CARD_NO_FILE:
        snprintf(out, n, TR(UPD_FETCH_NO_FILE), path != NULL ? path : "");
        break;
    case DEVIMG_CARD_SIZE:
        snprintf(out, n, "%s", TR(UPD_FETCH_SIZE));
        break;
    case DEVIMG_CARD_MEMORY:
        snprintf(out, n, "%s", TR(UPD_FETCH_MEMORY));
        break;
    case DEVIMG_CARD_SUMS:
        snprintf(out, n, "%s", TR(UPD_FETCH_SUMS));
        break;
    case DEVIMG_CARD_READ:
    case DEVIMG_CARD_OK:
    default:
        snprintf(out, n, "%s", TR(UPD_FETCH_READ));
        break;
    }
}

void upd_fetch_check_text(devimg_err_t e, const char *have, const char *want,
                          char *out, size_t n)
{
    if (out == NULL || n == 0u) {
        return;
    }
    switch (e) {
    case DEVIMG_E_SIZE:
        snprintf(out, n, "%s", TR(UPD_FETCH_SIZE));
        break;
    case DEVIMG_E_HASH:
        snprintf(out, n, "%s", TR(UPD_FAIL_HASH));
        break;
    case DEVIMG_E_ROLE:
        snprintf(out, n, "%s", TR(UPD_FETCH_ROLE));
        break;
    case DEVIMG_E_VERSION:
        snprintf(out, n, TR(UPD_FETCH_VERSION), have != NULL ? have : "",
                 want != NULL ? want : "");
        break;
    case DEVIMG_E_CHECK:
    case DEVIMG_E_ID:
    case DEVIMG_OK:
    default:
        snprintf(out, n, "%s", TR(UPD_FETCH_NOT_FW));
        break;
    }
}
