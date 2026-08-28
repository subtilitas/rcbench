#include "openyge.h"

#include <string.h>

#include "link_crc.h"

/* Little-endian, assembled rather than cast.  See the note in the header. */
static inline uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static inline void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static bool type_is_known(uint8_t t)
{
    switch (t) {
    case OPENYGE_FT_TELE_AUTO:
    case OPENYGE_FT_TELE_RESP:
    case OPENYGE_FT_TELE_REQ:
    case OPENYGE_FT_WRITE_PARAM_RESP:
    case OPENYGE_FT_WRITE_PARAM_REQ:
        return true;
    default:
        return false;
    }
}

/* A frame from the ESC carries telemetry; one from the master carries the
 * 4-byte control payload.  The parameter-write acknowledgement is an ESC
 * frame and carries telemetry too. */
static bool type_carries_telemetry(uint8_t t)
{
    return t == OPENYGE_FT_TELE_AUTO || t == OPENYGE_FT_TELE_RESP
           || t == OPENYGE_FT_WRITE_PARAM_RESP;
}

static uint8_t header_bytes(uint8_t version)
{
    return (version >= OPENYGE_VERSION) ? OPENYGE_HDR_V3 : OPENYGE_HDR_LEGACY;
}

/* --------------------------------------------------------------- decoding */

void openyge_decoder_reset(openyge_decoder_t *d)
{
    if (d != NULL) {
        memset(d, 0, sizeof(*d));
    }
}

typedef enum {
    PARSE_INVALID    = -1,
    PARSE_INCOMPLETE = 0,
    PARSE_OK         = 1,
} parse_result_t;

/*
 * Examine the candidate frame starting at @p at without modifying the
 * decoder.  Leaving the decoder alone is what makes it possible to weigh
 * several candidates before committing to one.
 */
static parse_result_t parse_at(const openyge_decoder_t *d, size_t at,
                               openyge_frame_t *out, size_t *total_out,
                               bool *crc_failed, bool *rejected)
{
    const uint8_t *p   = d->buf + at;
    const size_t avail = (size_t)d->len - at;

    *crc_failed = false;
    *rejected   = false;

    /* sync, version, type, length */
    if (avail < 4) {
        return PARSE_INCOMPLETE;
    }

    const uint8_t version = p[1];
    const uint8_t type    = p[2];
    const size_t  total   = p[3];
    const uint8_t hdr     = header_bytes(version);

    if (!type_is_known(type)) {
        return PARSE_INVALID;   /* a false sync, most likely */
    }
    if (total < OPENYGE_MIN_FRAME || total > OPENYGE_MAX_FRAME
        || total < (size_t)hdr + OPENYGE_CRC_BYTES) {
        return PARSE_INVALID;
    }
    if (avail < total) {
        return PARSE_INCOMPLETE;
    }

    const uint16_t want = rd16(p + total - OPENYGE_CRC_BYTES);
    if (link_crc(OPENYGE_CRC_INIT, p, total - OPENYGE_CRC_BYTES) != want) {
        *crc_failed = true;
        return PARSE_INVALID;
    }

    /*
     * The CRC held, so these are the bytes that were sent.  What is left is
     * whether they mean anything, and a frame that verifies but claims an
     * impossible shape is a version mismatch or a bug at the far end rather
     * than line noise -- counted separately, because the two want different
     * things done about them.
     */
    const size_t payload = total - hdr - OPENYGE_CRC_BYTES;
    const size_t expect  = type_carries_telemetry(type) ? OPENYGE_TELEMETRY_BYTES
                                                        : OPENYGE_CONTROL_BYTES;
    if (payload != expect) {
        *rejected = true;
        return PARSE_INVALID;
    }

    if (out != NULL) {
        memset(out, 0, sizeof(*out));
        out->hdr.version = version;
        out->hdr.type    = type;
        out->hdr.legacy  = (hdr == OPENYGE_HDR_LEGACY);
        if (!out->hdr.legacy) {
            out->hdr.seq    = p[4];
            out->hdr.device = p[5];
        }
        out->payload_len = (uint8_t)payload;
        memcpy(out->payload, p + hdr, payload);
    }

    *total_out = total;
    return PARSE_OK;
}

static void drop(openyge_decoder_t *d, size_t n)
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

bool openyge_decode_byte(openyge_decoder_t *d, uint8_t byte,
                         openyge_frame_t *out)
{
    if (d == NULL) {
        return false;
    }

    if (d->len >= OPENYGE_MAX_FRAME) {
        drop(d, 1);
        ++d->resyncs;
    }
    d->buf[d->len++] = byte;

    /*
     * Every sync byte is a candidate, examined in order.  The case that breaks
     * the obvious decoder is noise containing a byte that looks like a sync
     * and a length that happens to be plausible, sitting in front of a genuine
     * frame: commit to that first candidate and the decoder waits for bytes
     * that will never make sense while a whole real frame goes unreported.
     *
     * Preferring the earliest *complete* candidate over an earlier incomplete
     * one is safe, because for a later candidate to verify inside a genuine
     * frame its own CRC would have to hold by accident.
     */
    size_t first_incomplete = (size_t)-1;

    for (size_t i = 0; i < d->len; ++i) {
        if (d->buf[i] != OPENYGE_SYNC) {
            continue;
        }

        size_t total = 0;
        bool crc_failed = false, rejected = false;
        const parse_result_t r =
            parse_at(d, i, out, &total, &crc_failed, &rejected);

        if (r == PARSE_OK) {
            if (i > 0) {
                ++d->resyncs;   /* whatever preceded it was noise */
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
        /* Invalid, and counted only at the front -- that is the candidate
         * about to be discarded.  A speculative one further in would be
         * counted again on every byte that arrived after it. */
        if (i == 0) {
            if (crc_failed) {
                ++d->crc_errors;
            } else if (rejected) {
                ++d->rejected;
            }
        }
    }

    const size_t keep = (first_incomplete == (size_t)-1) ? d->len
                                                         : first_incomplete;
    if (keep > 0) {
        drop(d, keep);
        ++d->resyncs;
    }
    return false;
}

/* -------------------------------------------------------------- telemetry */

bool openyge_frame_has_telemetry(const openyge_frame_t *f)
{
    return f != NULL && type_carries_telemetry(f->hdr.type)
           && f->payload_len == OPENYGE_TELEMETRY_BYTES;
}

bool openyge_telemetry_parse(const openyge_frame_t *f, openyge_telemetry_t *out)
{
    if (!openyge_frame_has_telemetry(f) || out == NULL) {
        return false;
    }
    const uint8_t *p = f->payload;
    memset(out, 0, sizeof(*out));

    /* p[0] is reserved. */
    out->temp_c          = (int16_t)((int)p[1] - OPENYGE_TEMP_OFFSET);
    out->volts           = (float)rd16(p + 2) / 100.0f;   /* 10 mV */
    out->amps            = (float)rd16(p + 4) / 100.0f;   /* 10 mA */
    out->consumption_mah = rd16(p + 6);
    /*
     * The field is described as "0.1 eRPM" and the reference multiplies by
     * ten.  Both cannot be true; multiplying is right, because 65535 x 10 is a
     * plausible ceiling where 6553 is not, and because it is what most ESC
     * telemetry sends.  It is the first thing on the measurement list all the
     * same -- getting this backwards is a tachometer reading a hundredfold
     * out, and it will look plausible on a small motor.
     */
    out->erpm            = (uint32_t)rd16(p + 8) * 10u;
    out->pwm_pct         = (int8_t)p[10];
    out->throttle_pct    = (int8_t)p[11];
    out->bec_volts       = (float)rd16(p + 12) / 1000.0f; /* mV */
    out->bec_amps        = (float)rd16(p + 14) / 1000.0f; /* mA */
    out->bec_temp_c      = (int16_t)((int)p[16] - OPENYGE_TEMP_OFFSET);
    out->status1         = p[17];
    out->cap_temp_c      = (int16_t)((int)p[18] - OPENYGE_TEMP_OFFSET);
    out->aux_temp_c      = (int16_t)((int)p[19] - OPENYGE_TEMP_OFFSET);
    out->status2         = p[20];
    /* p[21] reserved; possibly a consumption high byte above 65 Ah. */
    out->pidx            = rd16(p + 22);
    out->pdata           = rd16(p + 24);
    return true;
}

bool openyge_control_parse(const openyge_frame_t *f, uint16_t *index,
                           uint16_t *param)
{
    if (f == NULL || type_carries_telemetry(f->hdr.type)
        || f->payload_len != OPENYGE_CONTROL_BYTES) {
        return false;
    }
    if (index != NULL) {
        *index = rd16(f->payload);
    }
    if (param != NULL) {
        *param = rd16(f->payload + 2);
    }
    return true;
}

/* --------------------------------------------------------------- encoding */

size_t openyge_encode(uint8_t *out, size_t cap, uint8_t type, uint8_t device,
                      uint8_t seq, const void *payload, size_t payload_len)
{
    if (out == NULL || !type_is_known(type)) {
        return 0;
    }
    const size_t expect = type_carries_telemetry(type) ? OPENYGE_TELEMETRY_BYTES
                                                       : OPENYGE_CONTROL_BYTES;
    if (payload_len != expect || (payload == NULL && payload_len != 0)) {
        return 0;
    }

    const size_t total = OPENYGE_HDR_V3 + payload_len + OPENYGE_CRC_BYTES;
    /* Checked before a single byte is written, so a frame that will not fit
     * leaves the buffer and the caller's length untouched. */
    if (total > cap || total > OPENYGE_MAX_FRAME) {
        return 0;
    }

    out[0] = OPENYGE_SYNC;
    out[1] = OPENYGE_VERSION;
    out[2] = type;
    out[3] = (uint8_t)total;
    out[4] = seq;
    out[5] = (uint8_t)(device | OPENYGE_DEV_MASTER);
    memcpy(out + OPENYGE_HDR_V3, payload, payload_len);

    wr16(out + total - OPENYGE_CRC_BYTES,
         link_crc(OPENYGE_CRC_INIT, out, total - OPENYGE_CRC_BYTES));
    return total;
}

size_t openyge_build_telemetry_request(uint8_t *out, size_t cap,
                                       uint8_t device, uint8_t seq)
{
    const uint8_t ctl[OPENYGE_CONTROL_BYTES] = { 0, 0, 0, 0 };
    return openyge_encode(out, cap, OPENYGE_FT_TELE_REQ, device, seq,
                          ctl, sizeof(ctl));
}

size_t openyge_build_param_write(uint8_t *out, size_t cap, uint8_t device,
                                 uint8_t seq, uint16_t index, uint16_t value)
{
    uint8_t ctl[OPENYGE_CONTROL_BYTES];
    wr16(ctl, index);
    wr16(ctl + 2, value);
    return openyge_encode(out, cap, OPENYGE_FT_WRITE_PARAM_REQ, device, seq,
                          ctl, sizeof(ctl));
}
