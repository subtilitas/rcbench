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
