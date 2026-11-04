/**
 * @file
 * @brief SPDX-License-Identifier: MIT
 */

#include "stop_latch.h"

#include <stddef.h>
#include <string.h>

#include "link_pages.h"

/**
 * @brief Wrap-safe: true from the millisecond @p deadline_ms on, for half
 *        the range.
 * @param now_ms      Current time, in milliseconds.
 * @param deadline_ms The deadline.
 * @return true at the deadline and after it.
 */
static bool reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(deadline_ms - now_ms) <= 0;
}

/**
 * @brief Wrap-safe: true from the millisecond after @p deadline_ms on.
 * @param now_ms      Current time, in milliseconds.
 * @param deadline_ms The deadline.
 * @return true after the deadline.
 */
static bool past(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(deadline_ms - now_ms) < 0;
}

/**
 * @brief Whether a reset is under way and not yet resumed.
 * @param s The state.
 * @return true for HOLD_DONE, ASKING and CONFIRMED.
 */
static bool resetting(stop_latch_state_t s)
{
    return s == STOP_LATCH_HOLD_DONE || s == STOP_LATCH_ASKING
           || s == STOP_LATCH_CONFIRMED;
}

void stop_latch_init(stop_latch_t *l, const stop_latch_in_t *in)
{
    if (l == NULL) {
        return;
    }
    memset(l, 0, sizeof(*l));
    if (in != NULL) {
        l->pressed_seen = in->pressed;
        l->breaks_seen  = in->breaks;
        l->state = in->held ? STOP_LATCH_LATCHED : STOP_LATCH_UNLATCHED;
        l->seen_ms = in->now_ms;
    }
}

/**
 * @brief Take in the STOPs and contact breaks counted since the last look.
 *
 * A STOP sends the machine back to LATCHED from any state.  A break sends a
 * reset that is not yet resumed back: the contact it rested on is gone.
 * @param l  State.
 * @param in The inputs.
 * @return true when the machine went back.
 */
static bool follow(stop_latch_t *l, const stop_latch_in_t *in)
{
    bool back = false;
    if (in->breaks != l->breaks_seen) {
        l->breaks_seen = in->breaks;
        /* The later of the two events counts for the next hold. */
        if (!l->event_known || reached(in->break_ms, l->event_ms)) {
            l->event_ms = in->break_ms;
        }
        l->event_known = true;
        if (resetting(l->state)) {
            l->state = STOP_LATCH_LATCHED;
            back = true;
        }
    }
    if (in->pressed != l->pressed_seen) {
        l->pressed_seen = in->pressed;
        l->seen_ms      = in->now_ms;
        if (!l->event_known || reached(in->press_ms, l->event_ms)) {
            l->event_ms = in->press_ms;
        }
        l->event_known = true;
        l->grace       = false;
        back = back || l->state != STOP_LATCH_LATCHED;
        l->state = STOP_LATCH_LATCHED;
    }
    return back;
}

/**
 * @brief One register read through @p io, the inputs read again after it.
 * @param l    State.
 * @param in   The inputs.
 * @param io   The caller.
 * @param page The page.
 * @param reg  The register.
 * @param out  Receives the value.
 * @return true when the read was answered and nothing sent the machine
 *         back meanwhile.
 */
static bool far_read(stop_latch_t *l, stop_latch_in_t *in,
                     const stop_latch_io_t *io, uint8_t page, uint8_t reg,
                     uint16_t *out)
{
    const bool ok = io->read != NULL && io->read(io->ctx, page, reg, out);
    if (io->refresh != NULL) {
        io->refresh(io->ctx, in);
    }
    return !follow(l, in) && ok;
}

/**
 * @brief Whether everything reads off now: this end, and with the link up
 *        every page the IOmcu serves, each read in this call and stopping
 *        at the first that reads on or goes unanswered.
 * @param l  State.
 * @param in The inputs.
 * @param io The caller.
 * @return true when everything reads off.
 */
static bool read_all_off(stop_latch_t *l, stop_latch_in_t *in,
                         const stop_latch_io_t *io)
{
    if (in->local_on) {
        return false;
    }
    if (!in->link_up) {
        return true;
    }
    uint16_t v = 0u;
    if (!far_read(l, in, io, LINK_PAGE_STATUS, LINK_ST_STATE, &v)
        || v == (uint16_t)LINK_STATE_ARMED) {
        return false;
    }
    if (in->supply_page
        && (!far_read(l, in, io, LINK_PAGE_SUPPLY, LINK_SP_FLAGS, &v)
            || (v & LINK_SP_ON) != 0u)) {
        return false;
    }
    if (in->prog_page
        && (!far_read(l, in, io, LINK_PAGE_PROG_SUPPLY, LINK_PS_FLAGS, &v)
            || (v & LINK_PS_ON) != 0u)) {
        return false;
    }
    /* This end again: an exchange pumps, and the inputs are read anew. */
    return !in->local_on;
}

/**
 * @brief A reading, kept for the next step.
 * @param l  State.
 * @param in The inputs.
 * @param io The caller.
 */
static void ask(stop_latch_t *l, stop_latch_in_t *in, const stop_latch_io_t *io)
{
    l->read_any = true;
    l->read_ms  = in->now_ms;
    l->read_off = read_all_off(l, in, io);
}

/**
 * @brief The reset is done.
 * @param l  State.
 * @param in The inputs.
 * @return STOP_LATCH_DONE.
 */
static stop_latch_act_t done(stop_latch_t *l, const stop_latch_in_t *in)
{
    l->state    = STOP_LATCH_UNLATCHED;
    l->grace    = true;
    l->grace_ms = in->now_ms;
    ++l->resets;
    return STOP_LATCH_DONE;
}

/**
 * @brief LATCHED: take the hold's request when the hold spans the latest
 *        STOP and the latest contact break.
 * @param l  State.
 * @param in The inputs.
 */
static void take_hold(stop_latch_t *l, const stop_latch_in_t *in)
{
    if (l->event_known
        && !reached(in->hold_ms, l->event_ms + STOP_LATCH_HOLD_SEEN_MS)) {
        ++l->refused;
        return;
    }
    l->state    = STOP_LATCH_HOLD_DONE;
    l->hold_ms  = in->hold_ms;
    l->read_any = false;
    l->read_off = false;
}

stop_latch_act_t stop_latch_run(stop_latch_t *l, stop_latch_in_t *in,
                                const stop_latch_io_t *io)
{
    if (l == NULL || in == NULL || io == NULL) {
        return STOP_LATCH_NONE;
    }
    (void)follow(l, in);
    switch (l->state) {
    case STOP_LATCH_UNLATCHED:
        if (in->held) {
            /* A STOP the count did not show: latched from now. */
            l->state   = STOP_LATCH_LATCHED;
            l->seen_ms = in->now_ms;
        }
        break;
    case STOP_LATCH_LATCHED:
        if (!in->held) {
            l->state = STOP_LATCH_UNLATCHED;   /* released some other way */
        } else if (in->hold_done) {
            take_hold(l, in);
        }
        break;
    case STOP_LATCH_HOLD_DONE:
        /* A controller that went silent in the hold's last moment counts as
         * a break by now. */
        if (reached(in->now_ms, l->hold_ms + STOP_LATCH_FRESH_MS)) {
            l->ask_ms = in->now_ms;
            ask(l, in, io);
            if (l->state == STOP_LATCH_HOLD_DONE) {
                l->state = STOP_LATCH_ASKING;
            }
        }
        break;
    case STOP_LATCH_ASKING:
        /* The bound first: a reading that came back past it is an exchange
         * that came back late, and it resets nothing. */
        if (past(in->now_ms, l->ask_ms + STOP_LATCH_CONFIRM_MS)) {
            l->state = STOP_LATCH_LATCHED;
            return STOP_LATCH_GIVE_UP;
        }
        if (l->read_off
            && reached(in->now_ms, l->seen_ms + STOP_LATCH_FLOOR_MS)) {
            l->state = STOP_LATCH_CONFIRMED;
        } else if (reached(in->now_ms, l->read_ms + STOP_LATCH_ASK_MS)) {
            ask(l, in, io);
        }
        break;
    case STOP_LATCH_CONFIRMED:
        if (io->resume != NULL) {
            io->resume(io->ctx);
        }
        l->state = STOP_LATCH_RESUMED;
        break;
    case STOP_LATCH_RESUMED:
        if (!in->link_up) {
            return done(l, in);   /* no IOmcu to write to */
        }
        /* CLEAR releases the IOmcu's arm latch, which its monitor set when
         * the line went quiet.  Taken whether or not its monitor trusts the
         * line yet: CLEAR carries no ARM. */
        if (io->clear != NULL) {
            io->clear(io->ctx);
        }
        l->state = STOP_LATCH_CLEAR_WRITTEN;
        break;
    case STOP_LATCH_CLEAR_WRITTEN:
    default:
        return done(l, in);
    }
    return STOP_LATCH_NONE;
}

bool stop_latch_error(stop_latch_t *l)
{
    if (l == NULL || !resetting(l->state)) {
        return false;
    }
    l->state = STOP_LATCH_LATCHED;
    return true;
}

stop_latch_state_t stop_latch_state(const stop_latch_t *l)
{
    return (l != NULL) ? l->state : STOP_LATCH_UNLATCHED;
}

uint16_t stop_latch_faults(const stop_latch_t *l, uint32_t now_ms,
                           bool latched, uint16_t faults)
{
    const bool hide =
        latched
        || (l != NULL
            && (l->state == STOP_LATCH_RESUMED
                || l->state == STOP_LATCH_CLEAR_WRITTEN
                || (l->grace
                    && !past(now_ms, l->grace_ms + STOP_LATCH_GRACE_MS))));
    return hide ? (uint16_t)(faults & (uint16_t)~LINK_FAULT_HEARTBEAT)
                : faults;
}

uint32_t stop_latch_resets(const stop_latch_t *l)
{
    return (l != NULL) ? l->resets : 0u;
}

uint32_t stop_latch_refused(const stop_latch_t *l)
{
    return (l != NULL) ? l->refused : 0u;
}
