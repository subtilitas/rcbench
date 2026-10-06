/*
 * The PD mini.  See pdmini.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pdmini.h"

#include <string.h>

uint8_t pdmini_crc8(const uint8_t *p, size_t n)
{
    uint8_t crc = 0xFFu;
    for (size_t i = 0; p != NULL && i < n; ++i) {
        crc = (uint8_t)(crc ^ p[i]);
        for (int b = 0; b < 8; ++b) {
            if ((crc & 0x80u) != 0u) {
                crc = (uint8_t)(((unsigned)crc << 1) ^ 0x31u);
            } else {
                crc = (uint8_t)((unsigned)crc << 1);
            }
        }
    }
    return crc;
}

/* A fixed-length reply's whole length, or 0 for one that is not. */
static size_t fixed_len(uint8_t cmd)
{
    switch (cmd) {
    case PDMINI_READ_STATE:
    case PDMINI_READ_ID:
        return 3u;
    case PDMINI_READ_DISPLAY:
        return 6u;
    case PDMINI_READ_DATA:
    case PDMINI_READ_INPUT:
        return 7u;
    default:
        return 0u;
    }
}

pdmini_frame_t pdmini_reply_frame(uint8_t cmd, const uint8_t *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return PDMINI_MORE;
    }
    if (buf[0] != cmd) {
        return PDMINI_BAD;
    }
    size_t len = fixed_len(cmd);
    if (len == 0u) {
        if (cmd != PDMINI_WHO_AM_I) {
            return PDMINI_BAD;   /* nothing else this driver reads */
        }
        if (n < 2u) {
            return PDMINI_MORE;
        }
        len = 3u + (size_t)buf[1];   /* command, length, text, end */
    }
    if (n < len) {
        return PDMINI_MORE;
    }
    if (n > len) {
        return PDMINI_BAD;
    }
    const uint8_t last = buf[len - 1u];
    if (last == pdmini_crc8(buf, len - 1u)) {
        return PDMINI_DONE;
    }
    /* WHO_AM_I's reply ends in 0x0A by the vendor's sheet, unchecked on the
     * bench: either is taken. */
    if (cmd == PDMINI_WHO_AM_I && last == 0x0Au) {
        return PDMINI_DONE;
    }
    return PDMINI_BAD;
}

void pdmini_init(pdmini_t *d, const pdmini_io_t *io, uint32_t now_ms)
{
    if (d == NULL) {
        return;
    }
    memset(d, 0, sizeof(*d));
    if (io != NULL) {
        d->io = *io;
    }
    d->slot     = -1;
    d->on_value = 1u;          /* the bench's and the vendor's Python's */
    d->phase    = PD_IDLE;
    d->t        = now_ms;
    d->last_identify = now_ms - PDMINI_IDENTIFY_MS;
    if (d->io.detach != NULL) {
        d->io.detach(d->io.ctx);   /* the pins at rest from the start */
    }
}

static uint16_t clamp16(uint16_t v, uint16_t lo, uint16_t hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

void pdmini_want(pdmini_t *d, bool output, uint16_t set_mv, uint16_t set_ma)
{
    if (d == NULL) {
        return;
    }
    if (output != d->want_output) {
        d->en_tries = 0u;
        d->st.stuck = false;
    }
    const uint16_t mv = clamp16(set_mv, (uint16_t)PDMINI_V_MIN_MV,
                                (uint16_t)PDMINI_V_MAX_MV);
    const uint16_t ma = clamp16(set_ma, (uint16_t)PDMINI_I_MIN_MA,
                                (uint16_t)PDMINI_I_MAX_MA);
    if (mv != d->want_mv || ma != d->want_ma) {
        d->data_tries   = 0u;
        d->st.set_stuck = false;
    }
    d->want_output = output;
    d->want_set    = true;
    d->want_mv     = mv;
    d->want_ma     = ma;
}

const pdmini_status_t *pdmini_status(const pdmini_t *d)
{
    return (d != NULL) ? &d->st : NULL;
}

/* ------------------------------------------------------------ transactions */

static void start(pdmini_t *d, uint32_t now, const uint8_t *req, size_t n,
                  bool write)
{
    memcpy(d->req, req, n);
    d->req[n] = pdmini_crc8(req, n);
    d->req_n  = n + 1u;
    d->cmd    = req[0];
    d->write  = write;
    d->rx_n   = 0u;
    if (d->io.attach != NULL) {
        d->io.attach(d->io.ctx);
    }
    d->phase = PD_ATTACH;
    d->t     = now;
}

static void read1(pdmini_t *d, uint32_t now, uint8_t cmd)
{
    start(d, now, &cmd, 1u, false);
}

static void finish(pdmini_t *d, uint32_t now, bool ok)
{
    if (d->io.detach != NULL) {
        d->io.detach(d->io.ctx);
    }
    d->phase = PD_GAP;
    d->t     = now;
    if (ok) {
        d->fails = 0u;
        return;
    }
    ++d->st.errors;
    if (++d->fails >= PDMINI_FAILS) {
        /*
         * Gone: nothing it said before is known any more, and the next
         * thing asked is who is there.  A write unanswered by its confirming
         * read looks the same as a module that is not there, so the pending
         * write is dropped with the rest and made again once it answers.
         */
        d->fails        = 0u;
        d->off_owed     = d->st.output || (d->en_pending && d->en_for);
        d->identified   = false;
        d->st.online    = false;
        d->state_known  = false;
        d->slot         = -1;
        d->data_known   = false;
        d->en_pending   = false;
        d->data_pending = false;
    }
}

static void take_reply(pdmini_t *d, uint32_t now)
{
    const uint8_t *r = d->rx;
    switch (d->cmd) {
    case PDMINI_WHO_AM_I:
        d->identified  = true;
        d->st.online   = true;
        d->state_known = false;
        break;
    case PDMINI_READ_STATE:
        d->st.output   = (r[1] & 1u) != 0u;
        d->st.mode     = (uint8_t)((r[1] >> 1) & 3u);
        d->state_known = true;
        d->off_owed    = false;
        if (d->en_pending && d->en_for != d->want_output) {
            /*
             * An OFF asked for while an ON was waiting to be confirmed: the
             * output is watched until the ON has had its time.  On, and the
             * OFF that follows switches it off; still off once the time is
             * up, and the ON never came.
             */
            if (d->st.output
                || (uint32_t)(now - d->en_at) >= PDMINI_CONFIRM_MS) {
                d->en_pending = false;
            }
            break;
        }
        if (d->st.output == d->want_output) {
            /* There, however it got there: nothing is stuck. */
            d->en_tries = 0u;
            d->st.stuck = false;
            if (d->en_pending) {
                /* And the argument just written is the one that means
                 * this, and its complement the other. */
                d->on_value = d->want_output ? d->en_value
                                             : (uint8_t)(1u - d->en_value);
                d->on_confirmed = true;
            }
        }
        d->en_pending = false;
        break;
    case PDMINI_READ_ID:
        /* Read again every second: the module's own buttons can change the
         * slot, or what is in it, so its data is read again with it. */
        d->slot       = (r[1] <= 4u) ? (int)r[1] : 0;
        d->data_known = false;
        break;
    case PDMINI_READ_DATA:
        if ((int)r[1] == d->slot) {
            d->st.set_mv    = (uint16_t)(r[2] | (r[3] << 8));
            d->st.set_ma    = (uint16_t)(r[4] | (r[5] << 8));
            d->data_known   = true;
            d->data_pending = false;
            if (d->st.set_mv == d->want_mv && d->st.set_ma == d->want_ma) {
                d->data_tries   = 0u;
                d->st.set_stuck = false;
            }
        }
        break;
    case PDMINI_READ_DISPLAY:
        d->st.v_mv = (uint16_t)(r[1] | (r[2] << 8));
        d->st.i_ma = (uint16_t)(r[3] | (r[4] << 8));
        ++d->st.samples;
        break;
    case PDMINI_READ_INPUT:
        d->st.in_state = r[1];
        d->st.vin_mv   = (uint16_t)(r[2] | (r[3] << 8));
        break;
    default:
        break;
    }
}

void pdmini_rx(pdmini_t *d, uint8_t byte, uint32_t now_ms)
{
    /* Nothing counts until the request is out: a byte from the pin handover
     * would otherwise start the reply.  A write is answered by nothing. */
    if (d == NULL || d->phase != PD_WAIT || d->write) {
        return;
    }
    if (d->rx_n < sizeof(d->rx)) {
        d->rx[d->rx_n++] = byte;
    }
    if ((int32_t)(now_ms + PDMINI_BYTE_MS - d->deadline) > 0) {
        d->deadline = now_ms + PDMINI_BYTE_MS;
    }
    const pdmini_frame_t f = pdmini_reply_frame(d->cmd, d->rx, d->rx_n);
    if (f == PDMINI_DONE) {
        take_reply(d, now_ms);
        finish(d, now_ms, true);
    } else if (f == PDMINI_BAD || d->rx_n >= sizeof(d->rx)) {
        finish(d, now_ms, false);
    }
}

/* --------------------------------------------------------------- the jobs */

/*
 * OUTPUT_EN towards what is asked: first with the argument known to mean
 * it, then twice with the other, in case this module reads it the way the
 * vendor's sheet says.  Only ever written when the output reads otherwise,
 * so an OFF goes only to an output that is on and cannot switch on one that
 * was off.  Four writes that do not take leave it stuck -- an input not
 * ready for an ON -- and it is tried again two seconds on.  False when it
 * is waiting out that pause.
 */
static bool write_en(pdmini_t *d, uint32_t now)
{
    if (d->en_tries >= 4u) {
        d->st.stuck = true;
        if ((uint32_t)(now - d->en_at) >= PDMINI_RETRY_MS) {
            d->en_tries = 0u;
        }
        return false;
    }
    const uint8_t mean = d->want_output ? d->on_value
                                        : (uint8_t)(1u - d->on_value);
    d->en_value = (d->en_tries < 2u) ? mean : (uint8_t)(1u - mean);
    ++d->en_tries;
    const uint8_t req[2] = { PDMINI_OUTPUT_EN, d->en_value };
    start(d, now, req, 2u, true);
    d->en_pending = true;
    d->en_for     = d->want_output;
    d->en_at      = now;
    return true;
}

/* The next transaction, or false for none now. */
static bool next_job(pdmini_t *d, uint32_t now)
{
    /* Who is there, before anything else is said to it -- but an OFF owed
     * to a module that went quiet with its output on goes first, blind. */
    if (!d->identified) {
        if ((uint32_t)(now - d->last_identify) >= PDMINI_IDENTIFY_MS) {
            if (d->off_owed && !d->want_output && d->on_confirmed
                && !d->blind_sent) {
                const uint8_t req[2] = { PDMINI_OUTPUT_EN,
                                         (uint8_t)(1u - d->on_value) };
                start(d, now, req, 2u, true);
                d->blind_sent = true;
                return true;
            }
            d->blind_sent    = false;
            d->last_identify = now;
            read1(d, now, PDMINI_WHO_AM_I);
            return true;
        }
        return false;
    }
    /* An OFF asked for while an ON waits to be confirmed: the state now,
     * not in 250 ms. */
    if (d->en_pending && d->en_for && !d->want_output) {
        d->last_state = now;
        read1(d, now, PDMINI_READ_STATE);
        return true;
    }
    /* A write of the output is confirmed by reading it back, and nothing
     * else is decided about the output until it has been. */
    if (d->en_pending
        && (uint32_t)(now - d->en_at) >= PDMINI_CONFIRM_MS) {
        d->last_state = now;
        read1(d, now, PDMINI_READ_STATE);
        return true;
    }
    if (!d->state_known) {
        d->last_state = now;
        read1(d, now, PDMINI_READ_STATE);
        return true;
    }
    /* An OFF first, before anything else waiting. */
    if (!d->en_pending && d->st.output && !d->want_output
        && write_en(d, now)) {
        return true;
    }
    /*
     * The set points, into the active slot, read back -- before any ON, so
     * the output never comes on at what the slot held before.  A write that
     * does not take three times is stuck, and tried again two seconds on.
     */
    if (d->want_set) {
        if (d->slot < 0) {
            d->last_slot = now;
            read1(d, now, PDMINI_READ_ID);
            return true;
        }
        if (d->data_pending || !d->data_known) {
            const uint8_t req[2] = { PDMINI_READ_DATA, (uint8_t)d->slot };
            start(d, now, req, 2u, false);
            return true;
        }
        if (d->st.set_mv != d->want_mv || d->st.set_ma != d->want_ma) {
            if (d->data_tries >= 3u) {
                d->st.set_stuck = true;
                if ((uint32_t)(now - d->data_at) >= PDMINI_RETRY_MS) {
                    d->data_tries = 0u;
                }
            } else {
                const uint8_t req[6] = {
                    PDMINI_OUTPUT_DATA, (uint8_t)d->slot,
                    (uint8_t)(d->want_mv & 0xFFu), (uint8_t)(d->want_mv >> 8),
                    (uint8_t)(d->want_ma & 0xFFu), (uint8_t)(d->want_ma >> 8),
                };
                ++d->data_tries;
                d->data_at = now;
                start(d, now, req, 6u, true);
                d->data_pending = true;
                return true;
            }
        }
    }
    /* An ON, once the set points are what is asked. */
    const bool set_ok = d->want_set && d->data_known
                        && d->st.set_mv == d->want_mv
                        && d->st.set_ma == d->want_ma;
    if (!d->en_pending && !d->st.output && d->want_output && set_ok) {
        if (write_en(d, now)) {
            return true;
        }
    }
    /*
     * And what it is doing: whichever reading is furthest past its due,
     * so a slow module answering every display read near its period does
     * not starve the others.
     */
    struct { uint32_t *last; uint32_t period; uint8_t cmd; } polls[4] = {
        { &d->last_display, PDMINI_DISPLAY_MS, PDMINI_READ_DISPLAY },
        { &d->last_state,   PDMINI_STATE_MS,   PDMINI_READ_STATE },
        { &d->last_input,   PDMINI_STATE_MS,   PDMINI_READ_INPUT },
        { &d->last_slot,    PDMINI_SLOT_MS,    PDMINI_READ_ID },
    };
    int best = -1;
    int32_t late_most = -1;
    for (int k = 0; k < 4; ++k) {
        if (polls[k].cmd == PDMINI_READ_STATE && d->en_pending) {
            continue;   /* the confirming read is the next state read */
        }
        const int32_t late = (int32_t)(now - *polls[k].last)
                             - (int32_t)polls[k].period;
        if (late >= 0 && late > late_most) {
            late_most = late;
            best = k;
        }
    }
    if (best >= 0) {
        *polls[best].last = now;
        read1(d, now, polls[best].cmd);
        return true;
    }
    return false;
}

void pdmini_step(pdmini_t *d, uint32_t now_ms)
{
    if (d == NULL) {
        return;
    }
    switch (d->phase) {
    case PD_IDLE:
        (void)next_job(d, now_ms);
        break;
    case PD_ATTACH:
        if ((uint32_t)(now_ms - d->t) >= PDMINI_ATTACH_MS) {
            d->rx_n = 0u;
            if (d->io.send != NULL) {
                d->io.send(d->io.ctx, d->req, d->req_n);
            }
            /* A read waits for its reply; a write only for its bytes to
             * leave, a millisecond each at 19200 baud and a margin. */
            d->deadline = now_ms + (d->write ? (uint32_t)d->req_n + 5u
                                             : PDMINI_REPLY_MS);
            if (d->cmd == PDMINI_OUTPUT_EN && d->write) {
                d->en_at = now_ms;
            }
            d->phase = PD_WAIT;
        }
        break;
    case PD_WAIT:
        if ((int32_t)(now_ms - d->deadline) >= 0) {
            finish(d, now_ms, d->write);   /* a read that never came fails */
        }
        break;
    case PD_GAP:
    default:
        if ((uint32_t)(now_ms - d->t) >= PDMINI_GAP_MS) {
            d->phase = PD_IDLE;
            (void)next_job(d, now_ms);
        }
        break;
    }
}
