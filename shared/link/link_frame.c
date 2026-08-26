#include "link_frame.h"

#include <string.h>

#include "link_crc.h"

/* A READ says how many registers it wants and carries none of them; every
 * other op carries what it is talking about. */
static bool op_carries_payload(uint8_t op)
{
    return op != (uint8_t)LINK_OP_READ;
}

static bool op_is_known(uint8_t op)
{
    switch (op) {
    case LINK_OP_READ:
    case LINK_OP_WRITE:
    case LINK_OP_DATA:
    case LINK_OP_ACK:
    case LINK_OP_NACK:
        return true;
    default:
        return false;
    }
}

size_t link_encode(uint8_t *out, size_t cap, const link_msg_t *msg)
{
    if (out == NULL || msg == NULL) {
        return 0;
    }
    if (!op_is_known(msg->op) || msg->count > LINK_MAX_REGS) {
        return 0;
    }
    /* A page is LINK_MAX_REGS wide, so a transfer that starts inside one and
     * runs past its end is a caller bug, not a wire condition. */
    if ((size_t)msg->offset + (size_t)msg->count > LINK_MAX_REGS) {
        return 0;
    }

    const size_t payload = op_carries_payload(msg->op) ? (size_t)msg->count * 2u : 0u;
    const size_t total   = LINK_HEADER_BYTES + payload + 2u;
    if (total > cap || total > LINK_MAX_FRAME) {
        return 0;
    }

    out[0] = LINK_SYNC;
    out[1] = (uint8_t)(total - 2u); /* every byte after LEN, CRC included */
    out[2] = msg->op;
    out[3] = msg->page;
    out[4] = msg->offset;
    out[5] = msg->count;

    for (size_t i = 0; i < payload / 2u; ++i) {
        out[LINK_HEADER_BYTES + 2u * i]      = (uint8_t)(msg->regs[i] & 0xFFu);
        out[LINK_HEADER_BYTES + 2u * i + 1u] = (uint8_t)(msg->regs[i] >> 8);
    }

    const uint16_t crc = link_crc(LINK_CRC_INIT, out, total - 2u);
    out[total - 2u] = (uint8_t)(crc & 0xFFu);
    out[total - 1u] = (uint8_t)(crc >> 8);

    return total;
}

void link_decoder_reset(link_decoder_t *d)
{
    if (d != NULL) {
        memset(d, 0, sizeof(*d));
    }
}

typedef enum {
    PARSE_INVALID    = -1, /**< nothing that starts here can be a frame */
    PARSE_INCOMPLETE = 0,  /**< could still be one; more bytes needed   */
    PARSE_OK         = 1,
} parse_result_t;

/*
 * Examine the candidate frame starting at @p at.  Does not modify the
 * decoder: the caller decides what to keep, which is what makes scanning
 * several candidates possible.
 */
static parse_result_t parse_at(const link_decoder_t *d, size_t at,
                               link_msg_t *out, size_t *total_out,
                               bool *crc_failed)
{
    const uint8_t *p    = d->buf + at;
    const size_t avail  = (size_t)d->len - at;

    *crc_failed = false;

    if (avail < 2) {
        return PARSE_INCOMPLETE;
    }

    const size_t total = (size_t)p[1] + 2u;
    if (total < LINK_HEADER_BYTES + 2u || total > LINK_MAX_FRAME) {
        return PARSE_INVALID; /* a length no frame can have -- a false sync */
    }
    if (avail < total) {
        return PARSE_INCOMPLETE;
    }

    const uint16_t want = (uint16_t)((uint16_t)p[total - 2u]
                                     | (uint16_t)((uint16_t)p[total - 1u] << 8));
    if (link_crc(LINK_CRC_INIT, p, total - 2u) != want) {
        *crc_failed = true;
        return PARSE_INVALID;
    }

    /* The CRC held, so the bytes are the bytes that were sent.  What is left
     * is whether they mean anything -- and a frame that verifies but claims
     * an impossible shape is a version mismatch or a bug at the far end, not
     * line noise.  It is still not something to act on. */
    const uint8_t op     = p[2];
    const uint8_t offset = p[4];
    const uint8_t count  = p[5];

    if (!op_is_known(op) || count > LINK_MAX_REGS
        || (size_t)offset + (size_t)count > LINK_MAX_REGS) {
        return PARSE_INVALID;
    }

    const size_t payload = op_carries_payload(op) ? (size_t)count * 2u : 0u;
    if (LINK_HEADER_BYTES + payload + 2u != total) {
        return PARSE_INVALID; /* the length and the count disagree */
    }

    if (out != NULL) {
        memset(out, 0, sizeof(*out));
        out->op     = op;
        out->page   = p[3];
        out->offset = offset;
        out->count  = count;
        for (size_t i = 0; i < payload / 2u; ++i) {
            const uint8_t *r = p + LINK_HEADER_BYTES + 2u * i;
            out->regs[i] = (uint16_t)((uint16_t)r[0]
                                      | (uint16_t)((uint16_t)r[1] << 8));
        }
    }

    *total_out = total;
    return PARSE_OK;
}

/* Discard @p n bytes from the front. */
static void drop(link_decoder_t *d, size_t n)
{
    if (n == 0) {
        return;
    }
    if (n >= d->len) {
        d->len = 0;
        return;
    }
    memmove(d->buf, d->buf + n, (size_t)d->len - n);
    d->len = (uint8_t)((size_t)d->len - n);
}

bool link_decode_byte(link_decoder_t *d, uint8_t byte, link_msg_t *out)
{
    if (d == NULL) {
        return false;
    }

    /* Full and still no frame means the buffer is holding rubbish; the oldest
     * byte is the least likely to start a real one. */
    if (d->len >= LINK_MAX_FRAME) {
        drop(d, 1);
        ++d->resyncs;
    }
    d->buf[d->len++] = byte;

    /*
     * Every sync byte in the buffer is a candidate, and they are examined in
     * order rather than one at a time.  The reason is the case that breaks the
     * obvious decoder: noise containing a byte that looks like a sync, with a
     * length byte that happens to be plausible, in front of a genuine frame.
     * Commit to that first candidate and the decoder sits waiting for bytes
     * that will never make sense while the real frame -- already whole,
     * already in the buffer -- goes unreported.
     *
     * Preferring the earliest *complete* candidate over an earlier incomplete
     * one is safe: for a later candidate to verify inside a genuine frame, its
     * own CRC would have to hold by accident.
     */
    size_t first_incomplete = (size_t)-1;

    for (size_t i = 0; i < d->len; ++i) {
        if (d->buf[i] != LINK_SYNC) {
            continue;
        }

        size_t total = 0;
        bool   crc_failed = false;
        const parse_result_t r = parse_at(d, i, out, &total, &crc_failed);

        if (r == PARSE_OK) {
            if (i > 0) {
                ++d->resyncs; /* what came before it was noise */
            }
            drop(d, i + total);
            ++d->frames;
            return true;
        }
        if (r == PARSE_INCOMPLETE) {
            if (first_incomplete == (size_t)-1) {
                first_incomplete = i;
            }
            continue;
        }
        /* Invalid.  Counted only at the front, because that is the candidate
         * about to be discarded -- a speculative one further in would be
         * counted again on every byte that arrives after it. */
        if (i == 0 && crc_failed) {
            ++d->crc_errors;
        }
    }

    /* Keep the earliest candidate that could still become a frame, and throw
     * away everything in front of it.  With nothing plausible left there is
     * nothing to keep. */
    const size_t keep = (first_incomplete == (size_t)-1) ? d->len : first_incomplete;
    if (keep > 0) {
        drop(d, keep);
        ++d->resyncs;
    }
    return false;
}
