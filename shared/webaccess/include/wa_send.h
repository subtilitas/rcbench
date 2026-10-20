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

/*
 * The connection a response goes out on.
 *
 * A piece is offered to put(), which queues what fits and never waits.  When
 * nothing fits, wait() waits for room for at most a slice.  A transfer ends
 * when the receiver closes or the connection fails, when stop() says so,
 * and when the receiver takes nothing for WA_SEND_QUIET_MS: a receiver that
 * stops reading without closing would otherwise hold the server, which
 * answers one request at a time, for as long as it stays.
 */

/** A transfer ends after this long with no byte taken. */
#define WA_SEND_QUIET_MS 30000u
/** The longest wait for room, and so for stop() to be seen. */
#define WA_SEND_SLICE_MS 250u

/** What a wait for room found. */
typedef enum {
    WA_WAIT_READY = 0, /**< room, or something to look at: offer again   */
    WA_WAIT_IDLE,      /**< the slice passed with nothing                 */
    WA_WAIT_GONE,      /**< the receiver closed, or the connection failed */
} wa_wait_t;

typedef struct {
    /** Queue up to @p n bytes without waiting: the bytes taken, 0 when
     *  none fit, -1 when the connection failed. */
    int32_t   (*put)(void *ctx, const uint8_t *p, uint32_t n);
    /** Wait at most @p ms for room. */
    wa_wait_t (*wait)(void *ctx, uint32_t ms);
    /** True when the transfer is to end now; NULL for never. */
    bool      (*stop)(void *ctx);
    /** Milliseconds; wraps at 2^32. */
    uint32_t  (*now_ms)(void *ctx);
    void      *ctx;
} wa_sink_t;

/** How a transfer ended. */
typedef enum {
    WA_SEND_DONE = 0,     /**< every byte taken                          */
    WA_SEND_GONE,         /**< the receiver closed, or the connection failed */
    WA_SEND_QUIET,        /**< nothing taken for WA_SEND_QUIET_MS        */
    WA_SEND_STOPPED,      /**< stop() said so                            */
    WA_SEND_READ_FAILED,  /**< the file: a read error, or not its size   */
} wa_send_res_t;

/** All @p n bytes of @p data into @p k. */
wa_send_res_t wa_sink_all(const wa_sink_t *k, const void *data, uint32_t n);

/**
 * The rest of the file begun with wa_send_begin() into @p k, a piece of at
 * most @p cap bytes of @p buf at a time.  The file is closed whatever the
 * outcome.
 */
wa_send_res_t wa_send_run(wa_send_t *s, uint8_t *buf, uint32_t cap,
                          const wa_sink_t *k);

/** A short English name of @p r for the log. */
const char *wa_send_res_name(wa_send_res_t r);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WA_SEND_H */
