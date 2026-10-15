/*
 * One file off the card, a piece at a time.
 *
 * The size is taken before the first byte goes, so the response can say how
 * long it is (Content-Length); the file then has to deliver exactly that.
 * A read error, or a file that ends early or runs long -- a card pulled part
 * way -- ends the transfer as failed, and the response is cut off there: a
 * browser reports a download shorter than announced as failed.
 *
 * No piece is larger than the caller's buffer; nothing holds the whole file.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WA_SEND_H
#define RCBENCH_WA_SEND_H

#include <stdbool.h>
#include <stdint.h>

#include "wa_walk.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bytes a piece: four 512-byte card sectors. */
#define WA_SEND_CHUNK 4096u

typedef struct {
    const wa_fs_t *fs;
    void          *file;
    uint64_t       size;   /**< announced */
    uint64_t       sent;
    bool           failed;
} wa_send_t;

/** Open @p path, announced as @p size bytes.  False when it cannot be. */
bool wa_send_begin(wa_send_t *s, const wa_fs_t *fs, const char *path,
                   uint64_t size);

/**
 * The next piece into @p buf of @p cap bytes: how many, 0 when all @c size
 * bytes have gone, -1 on a failure (the file is closed either way at the
 * end).
 */
int32_t wa_send_next(wa_send_t *s, uint8_t *buf, uint32_t cap);

/** Close the file; for a transfer given up part way. */
void wa_send_end(wa_send_t *s);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WA_SEND_H */
