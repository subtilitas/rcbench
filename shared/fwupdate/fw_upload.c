/*
 * Receiving an image. See fw_upload.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "fw_upload.h"

#include <string.h>

void fw_upload_init(fw_upload_t *u, uint8_t *buf, uint32_t cap,
                    uint32_t slot)
{
    memset(u, 0, sizeof(*u));
    u->buf  = buf;
    u->cap  = cap;
    u->slot = slot;
}

fw_err_t fw_upload_begin(fw_upload_t *u, uint32_t bytes)
{
    if (u->state == FW_UP_RECEIVING || u->state == FW_UP_STAGED) {
        return FW_ERR_BUSY;
    }
    if (bytes == 0u) {
        return FW_ERR_EMPTY;
    }
    if (bytes > u->slot) {
        return FW_ERR_TOO_BIG;
    }
    if (u->buf == NULL || u->cap < bytes) {
        return FW_ERR_MEMORY;
    }
    u->bytes = bytes;
    u->done  = 0u;
    u->err   = FW_OK;
    u->sig_n = 0u;
    u->state = FW_UP_RECEIVING;
    memset(&u->info, 0, sizeof(u->info));
    return FW_OK;
}

static fw_err_t refuse(fw_upload_t *u, fw_err_t e)
{
    u->state = FW_UP_FAILED;
    u->err   = e;
    return e;
}

fw_err_t fw_upload_add(fw_upload_t *u, const uint8_t *data, uint32_t len)
{
    if (u->state != FW_UP_RECEIVING) {
        return FW_ERR_BUSY;
    }
    if (len > u->bytes - u->done) {
        return refuse(u, FW_ERR_LENGTH);
    }
    if (len > 0u) {
        memcpy(u->buf + u->done, data, len);
        u->done += len;
    }
    return FW_OK;
}

fw_err_t fw_upload_end(fw_upload_t *u, bool whole)
{
    if (u->state != FW_UP_RECEIVING) {
        return (u->state == FW_UP_FAILED) ? u->err : FW_ERR_BUSY;
    }
    if (!whole || u->done != u->bytes) {
        return refuse(u, FW_ERR_READ);
    }
    const fw_err_t e = fw_image_check(u->buf, u->bytes, u->slot, &u->info);
    if (e != FW_OK) {
        return refuse(u, e);
    }
    u->state = FW_UP_STAGED;
    u->err   = FW_OK;
    return FW_OK;
}

fw_err_t fw_upload_sig(fw_upload_t *u, const uint8_t *sig, uint32_t n)
{
    if (u->state != FW_UP_STAGED) {
        return FW_ERR_BUSY;
    }
    if (sig == NULL || n < FW_SIG_MIN) {
        return FW_ERR_EMPTY;
    }
    if (n > FW_SIG_MAX) {
        return FW_ERR_TOO_BIG;
    }
    memcpy(u->sig, sig, n);
    u->sig_n = n;
    return FW_OK;
}

void fw_upload_discard(fw_upload_t *u)
{
    u->sig_n = 0u;
    u->state = FW_UP_IDLE;
    u->err   = FW_OK;
    u->bytes = 0u;
    u->done  = 0u;
}

unsigned fw_upload_percent(const fw_upload_t *u)
{
    if (u->bytes == 0u) {
        return 0u;
    }
    return (unsigned)(((uint64_t)u->done * 100u) / u->bytes);
}
