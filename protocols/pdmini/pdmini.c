/**
 * @file
 * @brief The PD mini driver: state machine, readings and the set-point rules.
 *
 * See pdmini.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pdmini_int.h"

#include <string.h>


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

/**
 * @brief Clamp a value into a range.
 *
 * @param v   Value to clamp.
 * @param lo  Lowest value returned.
 * @param hi  Highest value returned.
 * @return    @p v limited to [@p lo, @p hi].
 */
static uint16_t clamp16(uint16_t v, uint16_t lo, uint16_t hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

void pdmini_want(pdmini_t *d, bool output, uint16_t set_mv, uint16_t set_ma)
{
    if (d == NULL) {
        return;
    }
    if (!output) {
        d->held_off   = false;      /* an OFF asked: an ON may follow */
        d->st.tripped = false;
        d->st.sagged  = false;
    }
    output = output && !d->held_off;
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

void pdmini_restored(pdmini_t *d)
{
    if (d != NULL) {
        d->restored = true;
    }
}

void pdmini_reset(pdmini_t *d)
{
    if (d != NULL) {
        d->reset_owed = true;
    }
}

void pdmini_want_off(pdmini_t *d)
{
    if (d == NULL) {
        return;
    }
    if (d->want_output) {
        d->en_tries = 0u;
        d->st.stuck = false;
    }
    d->held_off    = false;
    d->st.tripped  = false;
    d->st.sagged   = false;
    d->want_output = false;
}

void pdmini_check_off(pdmini_t *d)
{
    if (d != NULL) {
        d->check = (uint8_t)PDMINI_CHECK_ASKED;
    }
}

pdmini_check_t pdmini_checked(const pdmini_t *d)
{
    return (d != NULL) ? (pdmini_check_t)d->check : PDMINI_CHECK_NONE;
}

const pdmini_status_t *pdmini_status(const pdmini_t *d)
{
    return (d != NULL) ? &d->st : NULL;
}

bool pdmini_may_be_on(const pdmini_t *d)
{
    /* Not knowing is not off, once something has answered on these pins:
     * its output may have come on by itself since it was last read. */
    return d != NULL
           && (d->st.output || (d->en_pending && d->en_for) || d->off_owed
               || d->on_sent || (d->answered && !d->state_known)
               || (d->restored && !d->answered
                   && d->who_misses < PDMINI_ABSENT_TRIES));
}

/* ------------------------------------------------------------ transactions */

void pdmini_start(pdmini_t *d, uint32_t now, const uint8_t *req, size_t n,
                  bool write)
{
    memcpy(d->req, req, n);
    d->req[n] = pdmini_crc8(req, n);
    d->req_n  = n + 1u;
    d->cmd    = req[0];
    d->write  = write;
    d->rx_n   = 0u;
    if (req[0] == PDMINI_READ_STATE
        && d->check == (uint8_t)PDMINI_CHECK_ASKED) {
        d->check = (uint8_t)PDMINI_CHECK_SENT;   /* asked before it went */
    }
    if (d->io.attach != NULL) {
        d->io.attach(d->io.ctx);
    }
    d->phase = PD_ATTACH;
    d->t     = now;
}

void pdmini_read1(pdmini_t *d, uint32_t now, uint8_t cmd)
{
    pdmini_start(d, now, &cmd, 1u, false);
}

void pdmini_finish(pdmini_t *d, uint32_t now, bool ok)
{
    if (d->io.detach != NULL) {
        d->io.detach(d->io.ctx);
    }
    d->phase = PD_GAP;
    d->t     = now;
    if (d->cmd == PDMINI_READ_STATE
        && d->check == (uint8_t)PDMINI_CHECK_SENT) {
        /* Off only as read now, with nothing on its way that may be on;
         * a read that failed is not off. */
        d->check = (uint8_t)((ok && !pdmini_may_be_on(d))
                                 ? PDMINI_CHECK_OFF : PDMINI_CHECK_NOT_OFF);
    }
    if (d->cmd == PDMINI_SYSTEM_RESET) {
        /* Restarting: nothing it said holds, and it is asked who it is
         * once it has had PDMINI_IDENTIFY_MS to come back. */
        d->identified    = false;
        d->st.online     = false;
        d->state_known   = false;
        d->slot          = -1;
        d->data_known    = false;
        d->en_pending    = false;
        d->data_pending  = false;
        d->last_identify = now;
    }
    if (ok) {
        d->fails = 0u;
        return;
    }
    ++d->st.errors;
    if (d->cmd == PDMINI_READ_INPUT) {
        /* Not read: the last reading is no cap to trust any more. */
        d->input_known = false;
        if (d->rx_n == 0u && d->input_misses < PDMINI_INPUT_MISSES) {
            ++d->input_misses;    /* firmware before v1.0.2.0 has none */
        }
    }
    if (d->cmd == PDMINI_WHO_AM_I) {
        ++d->who_failed;          /* for a caller looking for the rate */
        /* Silence in a row; any answer, even a wrong one, is something
         * there. */
        if (d->rx_n != 0u) {
            d->who_misses = 0u;
        } else if (d->who_misses < PDMINI_ABSENT_TRIES) {
            ++d->who_misses;
        }
    }
    if (d->cmd == PDMINI_WHO_AM_I && d->rx_n == 0u) {
        /* Not a byte back: silence, where a blind OFF may be heard.  Any
         * answer -- garbled, or another device's -- is not silence. */
        d->blind_due = true;
    }
    /* State reads counted on their own: other readings answered would
     * otherwise keep a module online whose output state is not known. */
    const bool state_lost = d->cmd == PDMINI_READ_STATE
                            && ++d->state_fails >= PDMINI_FAILS;
    if (state_lost) {
        /* The same module, answering all but this: an OFF it is owed is
         * sent blind straight away, as after a silent WHO_AM_I -- only if
         * this read was silent too.  Any answer to it is re-identified
         * first, as it may not be the module. */
        d->state_fails = 0u;
        d->blind_due   = d->rx_n == 0u;
    }
    if (++d->fails >= PDMINI_FAILS || state_lost) {
        /*
         * Gone: nothing it said before is known any more, and the next
         * thing asked is who is there.  A write unanswered by its confirming
         * read looks the same as a module that is not there, so the pending
         * write is dropped with the rest and made again once it answers.
         * An OFF owed stays owed until a state read says otherwise.
         */
        d->fails        = 0u;
        d->off_owed     = d->off_owed || d->st.output || d->on_sent
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

/**
 * @brief Whether a byte run holds a string.
 *
 * @param p  Bytes to search.
 * @param n  Number of bytes in @p p.
 * @param s  Nul-terminated string to find.
 * @return   true when @p s occurs in @p p.
 */
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
/*
 * The argument just written was followed by the output coming on.  Taken as
 * the one that means on, and relied on -- for a blind OFF, and without
 * trying the other -- only once two ONs with it have both taken: the
 * module's own button or its AUTO OUT setting can switch the output on in
 * the same 250 ms as one guess.
 */
/*
 * The voltage set point the module is given: the one asked, but no more
 * than PDMINI_CAP_MV under the input it reports, and no less than
 * PDMINI_V_MIN_MV.  A buck cannot put out more than it is fed, and a module
 * asked to shows ERR and needs a power cycle.  The cap sits
 * PDMINI_CAP_MV - PDMINI_HEADROOM_MV above the line sag_check() cuts at, so
 * an input that moves by less than that after the set point is written cuts
 * nothing.  As asked while the input is not known.
 *
 * A live output's set point, up to the one asked, is not lowered to follow
 * its input down: the voltage under test would change unasked mid-run, and
 * a sag that lasts switches the output off instead (sag_check()).
 */
uint16_t pdmini_cap_mv(uint32_t vin_mv)
{
    /* On the 10 mV grid the screen offers, rounded down; an input with no
     * room for the lowest set point gets the lowest, and an ON is then not
     * written (pdmini_sag_hold()). */
    uint32_t cap = PDMINI_V_MIN_MV;
    if (vin_mv >= PDMINI_V_MIN_MV + PDMINI_CAP_MV) {
        cap = (vin_mv - PDMINI_CAP_MV) / 10u * 10u;
    }
    return (cap > PDMINI_V_MAX_MV) ? (uint16_t)PDMINI_V_MAX_MV
                                   : (uint16_t)cap;
}

uint16_t pdmini_target_mv(const pdmini_t *d)
{
    if (!d->input_known) {
        return d->want_mv;      /* not read yet */
    }
    uint32_t cap = pdmini_cap_mv(d->st.vin_mv);
    if (d->st.output && d->want_output && d->st.set_mv > cap
        && d->st.set_mv <= d->want_mv) {
        cap = d->st.set_mv;
    }
    return (d->want_mv > cap) ? (uint16_t)cap : d->want_mv;
}

/* Whether the input last read lies under the set point read back and
 * PDMINI_HEADROOM_MV: the line a live output is cut at. */
bool pdmini_under_line(const pdmini_t *d)
{
    return (uint32_t)d->st.vin_mv
           < (uint32_t)d->st.set_mv + PDMINI_HEADROOM_MV;
}

/* The output switched off for the input, or its ON not written, and held
 * off until an OFF is asked. */
void pdmini_sag_hold(pdmini_t *d)
{
    d->sag_reads   = 0u;
    d->held_off    = true;
    d->want_output = false;
    d->en_tries    = 0u;
    d->st.stuck    = false;
    d->st.sagged   = true;
}

/**
 * @brief Count one input reading against a live output's set point.
 *
 * An input reading, against a live output's set point: under it and the
 * headroom on PDMINI_SAG_READS readings in a row, the output is switched
 * off and held off until an OFF is asked.  Only an output read on with ON
 * asked and a set point read back; a reading at or over the line, or an
 * output not live, starts the count again.  A failed read is no reading
 * and leaves the count as it is.
 */
static void sag_check(pdmini_t *d)
{
    const bool live = d->st.output && d->want_output && d->state_known
                      && d->st.set_mv != 0u;
    if (!live || !pdmini_under_line(d)) {
        d->sag_reads = 0u;
        return;
    }
    if (++d->sag_reads < PDMINI_SAG_READS) {
        return;
    }
    pdmini_sag_hold(d);
}

/**
 * @brief Learn the OUTPUT_EN argument that means on from a seen output.
 *
 * @param d  Driver state.
 */
static void learn_on(pdmini_t *d)
{
    if (d->on_seen && d->on_value == d->en_value) {
        d->on_confirmed = true;
    } else {
        d->on_value     = d->en_value;
        d->on_seen      = true;
        d->on_confirmed = false;
    }
}

/**
 * @brief Act on a whole reply for the command under way.
 *
 * @param d    Driver state; the reply is in @c d->rx.
 * @param now  Current time, in milliseconds.
 * @return     true when the reply is taken; false when it cannot be the
 *             module's answer and the transaction fails.
 */
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
        d->answered    = true;
        d->identified  = true;
        d->st.online   = true;
        d->input_known = false;    /* read again before a set point */
        d->input_seen  = false;    /* perhaps other firmware: asked again */
        d->st.vin_mv   = 0u;
        d->state_known = false;
        d->blind_due   = false;
        d->sag_reads   = 0u;      /* perhaps another module, another input */
        /* The argument learnt is kept: the same module back after a fault
         * still has to be switched off, blind if it goes quiet again.  One
         * swapped for another that reads it the other way round is caught
         * by four writes that do not take. */
        d->input_misses = 0u;
        break;
    case PDMINI_READ_STATE: {
        const bool was_on = d->state_known && d->st.output;
        d->st.output   = (r[1] & 1u) != 0u;
        d->st.mode     = (uint8_t)((r[1] >> 1) & 3u);
        if (was_on && !d->st.output && d->want_output && !d->en_pending
            && d->en_for) {
            /*
             * On, and now off with ON still asked, the last write ours an
             * ON and nothing on its way: the module switched it off -- its
             * overcurrent protection, or its button.  Not switched on again:
             * held off, and said, until an OFF is asked.
             */
            d->held_off    = true;
            d->want_output = false;
            d->en_tries    = 0u;
            d->st.tripped  = true;
        }
        d->state_known = true;
        d->state_fails = 0u;
        d->off_owed    = false;
        /* An ON sent is settled by a read that shows it on, or off with no
         * ON on its way and the last write given its time. */
        if (d->st.output
            || (!(d->en_pending && d->en_for)
                && (uint32_t)(now - d->en_at) >= PDMINI_CONFIRM_MS)) {
            d->on_sent = false;
        }
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
                learn_on(d);
                d->en_pending = false;
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
                 * an output that came on; one that went off may have been
                 * the module's own protection, whatever was written.
                 */
                learn_on(d);
            }
        }
        d->en_pending = false;
        break;
    }
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
        if (d->st.set_mv == pdmini_target_mv(d)
            && d->st.set_ma == d->want_ma) {
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
        d->input_known = true;
        d->input_seen  = true;
        sag_check(d);
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
        pdmini_finish(d, now_ms, take_reply(d, now_ms));
    } else if (f == PDMINI_BAD || d->rx_n >= sizeof(d->rx)) {
        pdmini_finish(d, now_ms, false);
    }
}
