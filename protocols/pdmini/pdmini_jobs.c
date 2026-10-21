/**
 * @file
 * @brief The PD mini's jobs: which transaction goes next, and the step that
 *        runs the phases of one.
 *
 * See pdmini.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pdmini_int.h"

/* --------------------------------------------------------------- the jobs */

/**
 * @brief Whether an ON may be written now.
 *
 * Four writes that do not take leave the output stuck -- an input not ready
 * for an ON -- and it is tried again two seconds on.  False while it waits
 * out that pause.
 *
 * @param d    Driver state.
 * @param now  Current time, in milliseconds.
 * @return     true when an ON may be written.
 */
static bool en_ready(pdmini_t *d, uint32_t now)
{
    if (d->en_tries >= 4u) {
        d->st.stuck = true;
        d->on_confirmed = false;   /* the argument is in doubt again */
        d->on_seen      = false;
        if ((uint32_t)(now - d->en_at) < PDMINI_RETRY_MS) {
            return false;
        }
        d->en_tries = 0u;
    }
    return true;
}

/**
 * @brief Write OUTPUT_EN towards what is asked.
 *
 * Twice with the argument taken to mean it, then -- until a read-back has
 * shown which argument means on -- twice with the other, in case this module
 * reads it the way the vendor's sheet says.  Written when the output reads
 * otherwise, so an OFF that tries the
 * other argument goes only to an output that is on and cannot switch on
 * one that was off.  An ON is false when en_ready() says not now; an OFF
 * is never held back, and four that do not take only say stuck.
 *
 * @param d    Driver state.
 * @param now  Current time, in milliseconds.
 * @return     true when the write was started; false when an ON must wait.
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
        d->on_seen      = false;
        d->en_tries = 0u;
    }
    const uint8_t mean = d->want_output ? d->on_value
                                        : (uint8_t)(1u - d->on_value);
    d->en_value = (d->en_tries < 2u || d->on_confirmed)
                      ? mean : (uint8_t)(1u - mean);
    ++d->en_tries;
    const uint8_t req[2] = { PDMINI_OUTPUT_EN, d->en_value };
    pdmini_start(d, now, req, 2u, true);
    d->en_pending = true;
    d->en_for     = d->want_output;
    d->en_at      = now;
    return true;
}

/**
 * @brief Start the reading furthest past its due by at least @p margin ms.
 *
 * False for none.  Furthest past its due, so a slow module answering every
 * display read near its period does not starve the others.  Unsigned, so a
 * reading untaken for 2^31 ms or more -- a module first seen 25 days after
 * start -- is late rather than early.
 *
 * @param d       Driver state.
 * @param now     Current time, in milliseconds.
 * @param margin  Least time past its due, in milliseconds.
 * @return        true when a reading was started.
 */
static bool poll(pdmini_t *d, uint32_t now, uint32_t margin)
{
    struct { uint32_t *last; uint32_t period; uint8_t cmd; } polls[4] = {
        { &d->last_display, PDMINI_DISPLAY_MS, PDMINI_READ_DISPLAY },
        { &d->last_state,   PDMINI_STATE_MS,   PDMINI_READ_STATE },
        /* An input that has stopped answering is asked once a second, as
         * the read before a set point asks it, sharing its time. */
        { &d->last_input,   d->input_known ? PDMINI_STATE_MS : PDMINI_SLOT_MS,
          PDMINI_READ_INPUT },
        { &d->last_slot,    PDMINI_SLOT_MS,    PDMINI_READ_ID },
    };
    int best = -1;
    uint32_t late_most = 0u;
    for (int k = 0; k < 4; ++k) {
        if (polls[k].cmd == PDMINI_READ_STATE && d->en_pending) {
            continue;   /* the confirming read is the next state read */
        }
        /* Taken to have no input reading: still asked now and then, so a
         * module that only lost a few packets is capped once it answers. */
        const uint32_t period =
            (polls[k].cmd == PDMINI_READ_INPUT && !d->input_seen
             && d->input_misses >= PDMINI_INPUT_MISSES)
                ? PDMINI_INPUT_RETRY_MS : polls[k].period;
        const uint32_t since = now - *polls[k].last;
        if (since < period || since - period < margin) {
            continue;
        }
        const uint32_t late = since - period;
        if (best < 0 || late > late_most) {
            late_most = late;
            best = k;
        }
    }
    if (best < 0) {
        return false;
    }
    *polls[best].last = now;
    pdmini_read1(d, now, polls[best].cmd);
    return true;
}

/**
 * @brief Choose and start the next transaction.
 *
 * @param d    Driver state.
 * @param now  Current time, in milliseconds.
 * @return     true when a transaction was started; false for none now.
 */
static bool next_job(pdmini_t *d, uint32_t now)
{
    /* The reads before an ON go one straight after the other: anything else
     * chosen here starts them again. */
    const uint8_t on_step = d->on_step;
    d->on_step = 0u;
    /* An overdue reading goes before the set points one at a time: another
     * job goes between two. */
    const bool polled_late = d->polled_late;
    d->polled_late = false;

    /*
     * Who is there, before anything else is said to it.  An OFF owed to a
     * module that went quiet with its output on goes blind, straight after
     * a WHO_AM_I nothing answered -- never to a module that answers, which
     * may be another one, and is read before anything is written to it.
     */
    if (!d->identified) {
        /* Not identified: no state read is made, and a check asked has no
         * read to show the output off. */
        if (d->check == (uint8_t)PDMINI_CHECK_ASKED) {
            d->check = (uint8_t)PDMINI_CHECK_NOT_OFF;
        }
        const bool blind = d->blind_due;
        d->blind_due = false;
        if (blind && d->off_owed && !d->want_output && d->on_confirmed) {
            const uint8_t req[2] = { PDMINI_OUTPUT_EN,
                                     (uint8_t)(1u - d->on_value) };
            pdmini_start(d, now, req, 2u, true);
            return true;
        }
        if ((uint32_t)(now - d->last_identify) >= PDMINI_IDENTIFY_MS) {
            d->last_identify = now;
            pdmini_read1(d, now, PDMINI_WHO_AM_I);
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
        pdmini_read1(d, now, PDMINI_READ_STATE);
        return true;
    }
    /* A write of the output is confirmed by reading it back, and nothing
     * else is decided about the output until it has been. */
    if (d->en_pending
        && (uint32_t)(now - d->en_at) >= PDMINI_CONFIRM_MS) {
        d->last_state = now;
        pdmini_read1(d, now, PDMINI_READ_STATE);
        return true;
    }
    if (!d->state_known) {
        d->last_state = now;
        pdmini_read1(d, now, PDMINI_READ_STATE);
        return true;
    }
    /* An OFF first, before anything else waiting. */
    if (!d->en_pending && d->st.output && !d->want_output
        && write_en(d, now)) {
        return true;
    }
    /* A state read asked for (pdmini_check_off()), after the OFF: with no
     * write being confirmed, whose confirming read is a state read too and
     * waits out PDMINI_CONFIRM_MS. */
    if (d->check == (uint8_t)PDMINI_CHECK_ASKED && !d->en_pending) {
        d->last_state = now;
        pdmini_read1(d, now, PDMINI_READ_STATE);
        return true;
    }
    /* A restart asked for, with the output read off: before anything else
     * is written to a module that is about to forget it. */
    if (d->reset_owed && !d->want_output && !d->st.output && !d->en_pending) {
        d->reset_owed = false;
        const uint8_t req[1] = { PDMINI_SYSTEM_RESET };
        pdmini_start(d, now, req, 1u, true);
        return true;
    }
    /*
     * The set points, into the active slot, read back -- before any ON, so
     * the output never comes on at what the slot held before.  Three tries
     * that do not take -- writes, or replies naming no slot or another --
     * are stuck, and the set points left alone for two seconds while the
     * readings go on.
     */
    /*
     * The input, before any set point is written or any ON, so the cap
     * under it holds from the first: read again whenever it is not known.
     * Firmware that has never answered the read gets the set point as
     * asked once the read has gone unanswered PDMINI_INPUT_MISSES times.
     */
    const bool input_ok = d->input_known
                          || (!d->input_seen
                              && d->input_misses >= PDMINI_INPUT_MISSES);
    /* Once a second: an unanswered read waits out PDMINI_REPLY_MS, and the
     * other readings need the rest of the line. */
    if (d->want_set && !input_ok
        && (uint32_t)(now - d->last_input) >= PDMINI_SLOT_MS) {
        d->last_input = now;
        pdmini_read1(d, now, PDMINI_READ_INPUT);
        return true;
    }
    /*
     * Due, it is read before a set point write or an ON as well: set points
     * changing faster than the readings come round -- a slider dragged --
     * or an ON after a pause, are judged on a fresh reading.  Only with a
     * set point to write or an ON to make: at rest it is a reading like the
     * others, and a module slow to answer leaves the display its share.
     * Before the reads for an ON, not between them: a module answering
     * 145 ms or more late takes longer than PDMINI_STATE_MS for them, and
     * the ON would never be written.  It counts as the reading that goes
     * ahead of a set point (polled_late), so a set point follows it.
     */
    const bool fallback_due = !d->input_seen
                              && d->input_misses >= PDMINI_INPUT_MISSES
                              && (uint32_t)(now - d->last_input)
                                     >= PDMINI_INPUT_RETRY_MS;
    const bool to_write =
        d->want_set
        && ((d->slot >= 0 && d->data_known && !d->data_pending
             && (d->st.set_mv != pdmini_target_mv(d)
                 || d->st.set_ma != d->want_ma))
            || (d->want_output && !d->st.output && !d->en_pending));
    if (to_write && on_step == 0u
        && ((d->input_known
             && (uint32_t)(now - d->last_input) >= PDMINI_STATE_MS)
            || fallback_due)) {
        d->last_input = now;
        d->polled_late = true;
        pdmini_read1(d, now, PDMINI_READ_INPUT);
        return true;
    }
    bool data_paused = false;
    if (d->data_tries >= 3u) {
        d->st.set_stuck = true;
        if (d->want_output && (d->st.output || (d->en_pending && d->en_for))) {
            /* Not left on at set points other than asked: off, and held
             * off until an OFF and a new ON are asked for. */
            d->held_off    = true;
            d->want_output = false;
            d->en_tries    = 0u;
        }
        data_paused = (uint32_t)(now - d->data_at) < PDMINI_RETRY_MS;
        if (!data_paused) {
            d->data_tries = 0u;
        }
    }
    /*
     * A reading PDMINI_POLL_LATE_MS past its due goes before the set
     * points: a slider dragged with no confirm slide hands a new set point
     * every 50 ms or so, its write and read-back take about as long, and
     * the readings would otherwise stand until the drag stops.  The display
     * -- long enough for the panel to take the readings as stopped and cut
     * the output -- and the state, which shows an output the module
     * switched on while an OFF is asked, or off by its protection.  One at
     * a time, with a set-point job or the reads for an ON between two, so
     * those still go out with a module that answers late; with one that
     * answers at once, each reading goes ahead at most once per its period
     * and PDMINI_POLL_LATE_MS.  Not between the reads before an ON, which
     * go one after the other.
     */
    if (on_step == 0u && !polled_late
        && poll(d, now, PDMINI_POLL_LATE_MS)) {
        d->polled_late = true;
        return true;
    }
    /* The state is not held back by the one-at-a-time rule: an overdue
     * state read goes straight after another reading, so a tripped output,
     * or one on while an OFF is asked, is seen within 2.1 s on a module
     * answering up to 390 ms late -- the display waiting instead. */
    if (on_step == 0u && polled_late && !d->en_pending
        && (uint32_t)(now - d->last_state)
               >= PDMINI_STATE_MS + PDMINI_POLL_LATE_MS) {
        d->last_state  = now;
        d->polled_late = true;
        pdmini_read1(d, now, PDMINI_READ_STATE);
        return true;
    }
    if (d->want_set && !data_paused && input_ok) {
        if (d->slot < 0) {
            d->last_slot = now;
            pdmini_read1(d, now, PDMINI_READ_ID);
            return true;
        }
        if (d->data_pending || !d->data_known) {
            const uint8_t req[2] = { PDMINI_READ_DATA, (uint8_t)d->slot };
            pdmini_start(d, now, req, 2u, false);
            return true;
        }
        const uint16_t mv = pdmini_target_mv(d);
        if (d->st.set_mv != mv || d->st.set_ma != d->want_ma) {
            const uint8_t req[6] = {
                PDMINI_OUTPUT_DATA, (uint8_t)d->slot,
                (uint8_t)(mv & 0xFFu), (uint8_t)(mv >> 8),
                (uint8_t)(d->want_ma & 0xFFu), (uint8_t)(d->want_ma >> 8),
            };
            ++d->data_tries;
            d->data_at = now;
            pdmini_start(d, now, req, 6u, true);
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
                        && d->st.set_mv == pdmini_target_mv(d)
                        && d->st.set_ma == d->want_ma;
    const bool on_due = !d->en_pending && !d->st.output && d->want_output
                        && set_ok && input_ok;
    if (on_due && d->input_known && pdmini_under_line(d)) {
        /* The input already under the line for the set point: read under
         * PDMINI_V_MIN_MV + PDMINI_HEADROOM_MV, 1500 mV, with no room for
         * the lowest set point.  An ON sag_check() would cut 2 readings on
         * is not written. */
        pdmini_sag_hold(d);
    } else if (on_due && en_ready(d, now)) {
        if (on_step == 4u && write_en(d, now)) {
            return true;
        }
        if (on_step == 2u) {
            d->on_step   = 3u;
            d->last_slot = now;
            pdmini_read1(d, now, PDMINI_READ_ID);
            return true;
        }
        d->on_step = 1u;
        const uint8_t req[2] = { PDMINI_READ_DATA, (uint8_t)d->slot };
        pdmini_start(d, now, req, 2u, false);
        return true;
    }
    /* And what it is doing. */
    return poll(d, now, 0u);
}

/**
 * @brief Whether an OFF waits on the transaction under way.
 *
 * next_job() would write it to an output read on, or write it -- or read the
 * state for it -- over an ON still being confirmed.
 *
 * @param d  Driver state.
 * @return   true when an OFF is waiting.
 */
static bool off_now(const pdmini_t *d)
{
    return d->identified && !d->want_output
           && ((!d->en_pending && d->state_known && d->st.output)
               || (d->en_pending && d->en_for));
}

/**
 * @brief Whether the write waiting for its pins has been overtaken.
 *
 * An OUTPUT_EN towards what is no longer asked, an ON whose set points have
 * changed since they were read back for it, set points no longer the ones
 * asked, or a set point while an OFF is asked and the output is or may be
 * on -- the OFF goes first.
 *
 * @param d  Driver state.
 * @return   true when the write is no longer wanted and is not sent.
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
                   || (d->en_for && (d->st.set_mv != pdmini_target_mv(d)
                                     || d->st.set_ma != d->want_ma)));
    }
    if (d->cmd != PDMINI_OUTPUT_DATA) {
        return false;
    }
    const uint16_t mv = (uint16_t)(d->req[2] | (d->req[3] << 8));
    const uint16_t ma = (uint16_t)(d->req[4] | (d->req[5] << 8));
    return mv != pdmini_target_mv(d) || ma != d->want_ma
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
                if (d->en_pending && d->en_for) {
                    d->on_sent = true;   /* until a read shows where it went */
                }
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
            /* a read that never came fails */
            pdmini_finish(d, now_ms, d->write);
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
