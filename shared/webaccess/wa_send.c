/*
 * One file off the card. See wa_send.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wa_send.h"

#include <string.h>

bool wa_send_begin(wa_send_t *s, const wa_fs_t *fs, const char *path,
                   uint64_t size)
{
    memset(s, 0, sizeof(*s));
    if (fs == NULL || fs->open == NULL || path == NULL) {
        return false;
    }
    s->fs   = fs;
    s->size = size;
    s->file = fs->open(fs->ctx, path);
    return s->file != NULL;
}

void wa_send_end(wa_send_t *s)
{
    if (s->file != NULL) {
        s->fs->close(s->fs->ctx, s->file);
        s->file = NULL;
    }
}

static int32_t fail(wa_send_t *s)
{
    s->failed = true;
    wa_send_end(s);
    return -1;
}

int32_t wa_send_next(wa_send_t *s, uint8_t *buf, uint32_t cap)
{
    if (s->failed || s->file == NULL || buf == NULL || cap == 0u) {
        return (s->failed || buf == NULL || cap == 0u) ? -1 : 0;
    }
    const uint64_t left = s->size - s->sent;
    if (left == 0u) {
        /* All announced bytes are out; one more read says whether the
         * file ends here as it should. */
        uint8_t probe;
        const int32_t more = s->fs->read(s->fs->ctx, s->file, &probe, 1u);
        if (more != 0) {
            return fail(s);
        }
        wa_send_end(s);
        return 0;
    }
    const uint32_t want = (left < cap) ? (uint32_t)left : cap;
    const int32_t got = s->fs->read(s->fs->ctx, s->file, buf, want);
    if (got <= 0 || (uint32_t)got > want) {
        return fail(s);   /* a read error, or the file ends early */
    }
    s->sent += (uint32_t)got;
    return got;
}

wa_send_res_t wa_sink_all(const wa_sink_t *k, const void *data, uint32_t n)
{
    const uint8_t *p = data;
    uint32_t quiet_from = k->now_ms(k->ctx);
    while (n > 0u) {
        if (k->stop != NULL && k->stop(k->ctx)) {
            return WA_SEND_STOPPED;
        }
        const int32_t took = k->put(k->ctx, p, n);
        if (took < 0 || (uint32_t)took > n) {
            return WA_SEND_GONE;
        }
        if (took > 0) {
            p += took;
            n -= (uint32_t)took;
            quiet_from = k->now_ms(k->ctx);
            continue;
        }
        if ((uint32_t)(k->now_ms(k->ctx) - quiet_from) >= WA_SEND_QUIET_MS) {
            return WA_SEND_QUIET;
        }
        if (k->wait(k->ctx, WA_SEND_SLICE_MS) == WA_WAIT_GONE) {
            return WA_SEND_GONE;
        }
    }
    return WA_SEND_DONE;
}

wa_send_res_t wa_send_run(wa_send_t *s, uint8_t *buf, uint32_t cap,
                          const wa_sink_t *k)
{
    wa_send_res_t r = WA_SEND_DONE;
    for (;;) {
        const int32_t got = wa_send_next(s, buf, cap);
        if (got == 0) {
            break;
        }
        if (got < 0) {
            r = WA_SEND_READ_FAILED;
            break;
        }
        r = wa_sink_all(k, buf, (uint32_t)got);
        if (r != WA_SEND_DONE) {
            break;
        }
    }
    wa_send_end(s);
    return r;
}

_Static_assert(WA_SEND_QUIET_MS == 30000u, "the log name says 30 s");

const char *wa_send_res_name(wa_send_res_t r)
{
    switch (r) {
    case WA_SEND_DONE:        return "sent";
    case WA_SEND_GONE:        return "receiver gone";
    case WA_SEND_QUIET:       return "nothing taken for 30 s";
    case WA_SEND_STOPPED:     return "radio going off";
    case WA_SEND_READ_FAILED: return "card read failed";
    }
    return "?";
}
