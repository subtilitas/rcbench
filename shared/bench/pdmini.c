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
    if (d->cmd == PDMINI_READ_INPUT && d->rx_n == 0u
        && d->input_misses < PDMINI_INPUT_MISSES) {
        ++d->input_misses;        /* firmware before v1.0.2.0 has none */
    }
    if (d->cmd == PDMINI_WHO_AM_I && d->rx_n == 0u) {
        /* Not a byte back: silence, where a blind OFF may be heard.  Any
         * answer -- garbled, or another device's -- is not silence. */
        d->blind_due = true;
    }
    if (++d->fails >= PDMINI_FAILS) {
        /*
         * Gone: nothing it said before is known any more, and the next
         * thing asked is who is there.  A write unanswered by its confirming
         * read looks the same as a module that is not there, so the pending
         * write is dropped with the rest and made again once it answers.
         * An OFF owed stays owed until a state read says otherwise.
         */
        d->fails        = 0u;
        d->off_owed     = d->off_owed || d->st.output
                          || (d->en_pending && d->en_for);
        d->identified   = false;
        d->st.online    = false;
        d->state_known  = false;
        d->slot         = -1;
        d->data_known   = false;
        d->en_pending   = false;
        d->data_pending = false;
        d->last_identify = now - PDMINI_IDENTIFY_MS;
    }
}

/* Whether @p n bytes at @p p hold @p s. */
static bool holds(const uint8_t *p, size_t n, const char *s)
{
    const size_t k = strlen(s);
    for (size_t i = 0; i + k <= n; ++i) {
        if (memcmp(&p[i], s, k) == 0) {
            return true;
        }
    }
    return false;
}

/* A whole reply, its check passed: taken, or false for one that cannot be
 * the module's answer, which fails the transaction. */
static bool take_reply(pdmini_t *d, uint32_t now)
{
    const uint8_t *r = d->rx;
    switch (d->cmd) {
    case PDMINI_WHO_AM_I:
        /* Another device on the pins may frame a reply the same way and
         * mean something else by OUTPUT_EN: only this module is written. */
        if (!holds(&r[2], r[1], PDMINI_WHO)) {
            return false;
        }
        d->identified  = true;
        d->st.online   = true;
        d->state_known = false;
        d->blind_due   = false;
        /* The argument learnt is kept: the same module back after a fault
         * still has to be switched off, blind if it goes quiet again.  One
         * swapped for another that reads it the other way round is caught
         * by four writes that do not take. */
        d->input_misses = 0u;
        break;
    case PDMINI_READ_STATE:
        d->st.output   = (r[1] & 1u) != 0u;
        d->st.mode     = (uint8_t)((r[1] >> 1) & 3u);
        d->state_known = true;
        d->off_owed    = false;
        if (d->en_pending && d->en_for && !d->want_output) {
            /*
             * An OFF asked for while an ON, its argument not yet shown to
             * mean on, waited to be confirmed: the output is watched for
             * PDMINI_WATCH_MS.  On, and the argument is learnt and the OFF
             * that follows switches it off; still off once the time is up,
             * and the ON is taken never to have come -- one that does come
             * later is an output on while OFF is asked, seen by the next
             * state read.
             */
            if (d->st.output) {
                d->on_value     = d->en_value;
                d->on_confirmed = true;
                d->en_pending   = false;
            } else if ((uint32_t)(now - d->en_at) >= PDMINI_WATCH_MS) {
                d->en_pending = false;
            }
            break;
        }
        if (d->st.output == d->want_output) {
            /* There, however it got there: nothing is stuck. */
            d->en_tries = 0u;
            d->st.stuck = false;
            if (d->en_pending && d->en_for && d->want_output) {
                /*
                 * And the argument just written means on.  Learnt only from
                 * an output that came on, which it does not do by itself;
                 * one that went off may have been the module's own
                 * protection, whatever was written.
                 */
                d->on_value     = d->en_value;
                d->on_confirmed = true;
            }
        }
        d->en_pending = false;
        break;
    case PDMINI_READ_ID: {
        if (r[1] > 4u) {
            /* No slot: none is known, and nothing is switched on.  A try
             * at the set points, so a module that keeps saying it pauses
             * them rather than the readings. */
            d->slot       = -1;
            d->data_known = false;
            ++d->data_tries;
            d->data_at = now;
            return false;
        }
        const int slot = (int)r[1];
        if (d->on_step == 3u && slot == d->slot) {
            d->on_step = 4u;   /* the slot the set points were just read from */
            break;
        }
        /* Read again every second: the module's own buttons can change the
         * slot, or what is in it, so its data is read again with it. */
        d->slot       = slot;
        d->data_known = false;
        break;
    }
    case PDMINI_READ_DATA:
        if ((int)r[1] != d->slot) {
            /* Another slot's: which is active is read again, as a try at
             * the set points. */
            d->slot       = -1;
            d->data_known = false;
            ++d->data_tries;
            d->data_at = now;
            return false;
        }
        d->st.set_mv    = (uint16_t)(r[2] | (r[3] << 8));
        d->st.set_ma    = (uint16_t)(r[4] | (r[5] << 8));
        d->data_known   = true;
        d->data_pending = false;
        if (d->st.set_mv == d->want_mv && d->st.set_ma == d->want_ma) {
            d->data_tries   = 0u;
            d->st.set_stuck = false;
            if (d->on_step == 1u) {
                d->on_step = 2u;
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
        d->input_misses = 0u;
        break;
    default:
        break;
    }
    return true;
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
        /* Longer by a byte's time, never past the whole transaction's:
         * a reply that drips is not waited for while an OFF is asked. */
        uint32_t until = now_ms + PDMINI_BYTE_MS;
        const uint32_t cap = d->t + PDMINI_TXN_MS;
        if ((int32_t)(until - cap) > 0) {
            until = cap;
        }
        d->deadline = until;
    }
    const pdmini_frame_t f = pdmini_reply_frame(d->cmd, d->rx, d->rx_n);
    if (f == PDMINI_DONE) {
        finish(d, now_ms, take_reply(d, now_ms));
    } else if (f == PDMINI_BAD || d->rx_n >= sizeof(d->rx)) {
        finish(d, now_ms, false);
    }
}

/* --------------------------------------------------------------- the jobs */

/*
 * Whether an ON may be written: four writes that do not take leave the
 * output stuck -- an input not ready for an ON -- and it is tried again two
 * seconds on.  False while it waits out that pause.
 */
static bool en_ready(pdmini_t *d, uint32_t now)
{
    if (d->en_tries >= 4u) {
        d->st.stuck = true;
        d->on_confirmed = false;   /* the argument is in doubt again */
        if ((uint32_t)(now - d->en_at) < PDMINI_RETRY_MS) {
            return false;
        }
        d->en_tries = 0u;
    }
    return true;
}

/*
 * OUTPUT_EN towards what is asked: twice with the argument taken to mean
 * it, then -- until a read-back has shown which argument means on -- twice
 * with the other, in case this module reads it the way the vendor's sheet
 * says.  Written when the output reads otherwise, so an OFF that tries the
 * other argument goes only to an output that is on and cannot switch on
 * one that was off.  An ON is false when en_ready() says not now; an OFF
 * is never held back, and four that do not take only say stuck.
 */
static bool write_en(pdmini_t *d, uint32_t now)
{
    if (d->want_output && !en_ready(d, now)) {
        return false;
    }
    if (!d->want_output && d->en_tries >= 4u) {
        /* An OFF is not paused: stuck is said, and it goes on -- trying
         * both arguments again, as the module may have been swapped for
         * one that reads them the other way round. */
        d->st.stuck = true;
        d->on_confirmed = false;
        d->en_tries = 0u;
    }
    const uint8_t mean = d->want_output ? d->on_value
                                        : (uint8_t)(1u - d->on_value);
    d->en_value = (d->en_tries < 2u || d->on_confirmed)
                      ? mean : (uint8_t)(1u - mean);
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
    /* The reads before an ON go one straight after the other: anything else
     * chosen here starts them again. */
    const uint8_t on_step = d->on_step;
    d->on_step = 0u;

    /*
     * Who is there, before anything else is said to it.  An OFF owed to a
     * module that went quiet with its output on goes blind, straight after
     * a WHO_AM_I nothing answered -- never to a module that answers, which
     * may be another one, and is read before anything is written to it.
     */
    if (!d->identified) {
        const bool blind = d->blind_due;
        d->blind_due = false;
        if (blind && d->off_owed && !d->want_output && d->on_confirmed) {
            const uint8_t req[2] = { PDMINI_OUTPUT_EN,
                                     (uint8_t)(1u - d->on_value) };
            start(d, now, req, 2u, true);
            return true;
        }
        if ((uint32_t)(now - d->last_identify) >= PDMINI_IDENTIFY_MS) {
            d->last_identify = now;
            read1(d, now, PDMINI_WHO_AM_I);
            return true;
        }
        return false;
    }
    /*
     * An OFF asked for while an ON waits to be confirmed.  With the
     * argument for off shown by a read-back, it is written at once, and the
     * module takes it after the ON.  Without, the state is read now and
     * again until the ON has had PDMINI_WATCH_MS, and the output switched
     * off the moment it reads on.
     */
    if (d->en_pending && d->en_for && !d->want_output) {
        if (d->on_confirmed && write_en(d, now)) {
            return true;
        }
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
     * the output never comes on at what the slot held before.  Three tries
     * that do not take -- writes, or replies naming no slot or another --
     * are stuck, and the set points left alone for two seconds while the
     * readings go on.
     */
    bool data_paused = false;
    if (d->data_tries >= 3u) {
        d->st.set_stuck = true;
        data_paused = (uint32_t)(now - d->data_at) < PDMINI_RETRY_MS;
        if (!data_paused) {
            d->data_tries = 0u;
        }
    }
    if (d->want_set && !data_paused) {
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
    /*
     * An ON, once the set points are what is asked -- read back straight
     * before it, then the slot, with nothing between, so a slot chosen or
     * a set point turned on the module's own buttons since is not what
     * comes on.
     */
    const bool set_ok = d->want_set && d->data_known
                        && d->st.set_mv == d->want_mv
                        && d->st.set_ma == d->want_ma;
    if (!d->en_pending && !d->st.output && d->want_output && set_ok
        && en_ready(d, now)) {
        if (on_step == 4u && write_en(d, now)) {
            return true;
        }
        if (on_step == 2u) {
            d->on_step   = 3u;
            d->last_slot = now;
            read1(d, now, PDMINI_READ_ID);
            return true;
        }
        d->on_step = 1u;
        const uint8_t req[2] = { PDMINI_READ_DATA, (uint8_t)d->slot };
        start(d, now, req, 2u, false);
        return true;
    }
    /*
     * And what it is doing: whichever reading is furthest past its due,
     * so a slow module answering every display read near its period does
     * not starve the others.  Unsigned, so a reading untaken for 2^31 ms
     * or more -- a module first seen 25 days after start -- is late rather
     * than early.
     */
    struct { uint32_t *last; uint32_t period; uint8_t cmd; } polls[4] = {
        { &d->last_display, PDMINI_DISPLAY_MS, PDMINI_READ_DISPLAY },
        { &d->last_state,   PDMINI_STATE_MS,   PDMINI_READ_STATE },
        { &d->last_input,   PDMINI_STATE_MS,   PDMINI_READ_INPUT },
        { &d->last_slot,    PDMINI_SLOT_MS,    PDMINI_READ_ID },
    };
    int best = -1;
    uint32_t late_most = 0u;
    for (int k = 0; k < 4; ++k) {
        if (polls[k].cmd == PDMINI_READ_STATE && d->en_pending) {
            continue;   /* the confirming read is the next state read */
        }
        if (polls[k].cmd == PDMINI_READ_INPUT
            && d->input_misses >= PDMINI_INPUT_MISSES) {
            continue;   /* not answered by this module's firmware */
        }
        const uint32_t since = now - *polls[k].last;
        if (since < polls[k].period) {
            continue;
        }
        const uint32_t late = since - polls[k].period;
        if (best < 0 || late > late_most) {
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

/*
 * Whether an OFF waits on the transaction under way: next_job() would
 * write it to an output read on, or write it -- or read the state for it --
 * over an ON still being confirmed.
 */
static bool off_now(const pdmini_t *d)
{
    return d->identified && !d->want_output
           && ((!d->en_pending && d->state_known && d->st.output)
               || (d->en_pending && d->en_for));
}

/*
 * Whether the write waiting for its pins has been overtaken: an OUTPUT_EN
 * towards what is no longer asked, an ON whose set points have changed
 * since they were read back for it, set points no longer the ones asked,
 * or a set point while an OFF is asked and the output is or may be on --
 * the OFF goes first.
 */
static bool overtaken(const pdmini_t *d)
{
    if (!d->write) {
        return false;
    }
    if (d->cmd == PDMINI_OUTPUT_EN) {
        /* Towards what is no longer asked -- or an ON whose set points,
         * read back for it, are no longer the ones asked. */
        return d->en_pending
               && (d->en_for != d->want_output
                   || (d->en_for && (d->st.set_mv != d->want_mv
                                     || d->st.set_ma != d->want_ma)));
    }
    if (d->cmd != PDMINI_OUTPUT_DATA) {
        return false;
    }
    const uint16_t mv = (uint16_t)(d->req[2] | (d->req[3] << 8));
    const uint16_t ma = (uint16_t)(d->req[4] | (d->req[5] << 8));
    return mv != d->want_mv || ma != d->want_ma
           || (!d->want_output
               && (d->st.output || (d->en_pending && d->en_for)));
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
            if (overtaken(d)) {
                /* Asked otherwise while the pins were handed over: not
                 * sent, and the next job is the one now asked. */
                if (d->cmd == PDMINI_OUTPUT_EN) {
                    d->en_pending = false;
                    if (d->en_tries > 0u) {
                        --d->en_tries;     /* not if pdmini_want() reset it */
                    }
                } else {
                    d->data_pending = false;
                    if (d->data_tries > 0u) {
                        --d->data_tries;   /* not if pdmini_want() reset it */
                    }
                }
                if (d->io.detach != NULL) {
                    d->io.detach(d->io.ctx);
                }
                d->phase = PD_GAP;
                d->t     = now_ms;
                break;
            }
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
            d->t     = now_ms;
        }
        break;
    case PD_WAIT:
        if (!d->write && d->cmd != PDMINI_READ_STATE && off_now(d)) {
            /* An OFF to write overtakes a read: not waited for, and not
             * counted a failure.  A state read is the OFF's own. */
            if (d->io.detach != NULL) {
                d->io.detach(d->io.ctx);
            }
            d->phase = PD_GAP;
            d->t     = now_ms;
            break;
        }
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
