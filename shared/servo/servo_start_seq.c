/**
 * @file
 * @brief ARM + START's steps.  See servo_start_seq.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_start_seq.h"

#include <stddef.h>
#include <string.h>

void servo_seq_init(servo_seq_t *q)
{
    if (q != NULL) {
        memset(q, 0, sizeof(*q));
    }
}

bool servo_seq_begin(servo_seq_t *q, const servo_seq_need_t *need,
                     uint32_t now_ms)
{
    if (q == NULL || need == NULL || servo_seq_busy(q)) {
        return false;
    }
    memset(q, 0, sizeof(*q));
    q->need    = *need;
    q->step    = (uint8_t)SERVO_SEQ_PWM;
    q->step_ms = now_ms;
    return true;
}

bool servo_seq_busy(const servo_seq_t *q)
{
    return q != NULL && q->step != (uint8_t)SERVO_SEQ_IDLE;
}

servo_seq_step_t servo_seq_step_now(const servo_seq_t *q)
{
    return (q != NULL) ? (servo_seq_step_t)q->step : SERVO_SEQ_IDLE;
}

servo_seq_part_t servo_seq_part(const servo_seq_t *q)
{
    return (q != NULL) ? (servo_seq_part_t)q->part : SERVO_SEQ_PART_NONE;
}

servo_seq_why_t servo_seq_why(const servo_seq_t *q)
{
    return (q != NULL) ? (servo_seq_why_t)q->why : SERVO_SEQ_WHY_NONE;
}

/** @brief The supply part the run needs: PROG 5V before the PD mini. */
static servo_seq_part_t supply_part(const servo_seq_need_t *n)
{
    if (n->prog_5v) {
        return SERVO_SEQ_PART_PROG_5V;
    }
    return n->pd_mini ? SERVO_SEQ_PART_PD_MINI : SERVO_SEQ_PART_NONE;
}

/**
 * @brief The first part asked for that does not read back ON: the supply
 *        before the pulse, which comes only with it on the carrier.
 * @param n what the run needs
 * @param in what the bench reads
 * @return the part; SERVO_SEQ_PART_NONE when every one reads ON
 */
static servo_seq_part_t missing(const servo_seq_need_t *n,
                                const servo_seq_in_t *in)
{
    if (n->prog_5v && !in->p5_on) {
        return SERVO_SEQ_PART_PROG_5V;
    }
    if (n->pd_mini && !in->pd_on) {
        return SERVO_SEQ_PART_PD_MINI;
    }
    if (n->port_pwm && !in->pwm_on) {
        return SERVO_SEQ_PART_PWM;
    }
    return SERVO_SEQ_PART_NONE;
}

/**
 * @brief The part the step under way names when it fails for a reason
 *        other than its time.
 * @param q the sequence
 * @param in what the bench reads; NULL for none
 * @return the part
 */
static servo_seq_part_t part_now(const servo_seq_t *q,
                                 const servo_seq_in_t *in)
{
    switch ((servo_seq_step_t)q->step) {
    case SERVO_SEQ_PWM:
        return SERVO_SEQ_PART_PWM;
    case SERVO_SEQ_SUPPLY: {
        const servo_seq_part_t p = supply_part(&q->need);
        return (p != SERVO_SEQ_PART_NONE) ? p : SERVO_SEQ_PART_PWM;
    }
    case SERVO_SEQ_READBACK: {
        const servo_seq_part_t p = (in != NULL) ? missing(&q->need, in)
                                                : supply_part(&q->need);
        if (p != SERVO_SEQ_PART_NONE) {
            return p;
        }
        return q->need.port_pwm ? SERVO_SEQ_PART_PWM
                                : supply_part(&q->need);
    }
    case SERVO_SEQ_ARM:
        return SERVO_SEQ_PART_ARM;
    default:
        return SERVO_SEQ_PART_RUN;
    }
}

/**
 * @brief The sequence ends failed at the step under way.
 * @param q    the sequence
 * @param part what it names
 * @param why  why
 * @param d    receives all_off
 */
static void fail(servo_seq_t *q, servo_seq_part_t part, servo_seq_why_t why,
                 servo_seq_do_t *d)
{
    q->part = (uint8_t)part;
    q->why  = (uint8_t)why;
    q->step = (uint8_t)SERVO_SEQ_IDLE;
    q->done = false;
    d->all_off = true;
}

/**
 * @brief Whether @p elapsed ms reached @p limit: the time is up at the
 *        limit itself.
 */
static bool up(uint32_t now_ms, uint32_t from_ms, uint32_t limit)
{
    return (uint32_t)(now_ms - from_ms) >= limit;
}

void servo_seq_step(servo_seq_t *q, uint32_t now_ms,
                    const servo_seq_in_t *in, servo_seq_do_t *d)
{
    if (d == NULL) {
        return;
    }
    memset(d, 0, sizeof(*d));
    if (q == NULL || in == NULL || !servo_seq_busy(q)) {
        return;
    }
    if (in->refused && q->step != (uint8_t)SERVO_SEQ_RUN) {
        fail(q, part_now(q, in), SERVO_SEQ_WHY_REFUSED, d);
        return;
    }
    if (q->step == (uint8_t)SERVO_SEQ_PWM) {
        if (q->need.port_pwm && !in->pwm_set) {
            d->set_pwm = true;
        }
        q->step = (uint8_t)SERVO_SEQ_SUPPLY;
    }
    if (q->step == (uint8_t)SERVO_SEQ_SUPPLY) {
        if (q->need.prog_5v && !in->p5_on) {
            d->p5_on = true;
        }
        if (q->need.pd_mini && !in->pd_on) {
            d->pd_on = true;
        }
        q->step    = (uint8_t)SERVO_SEQ_READBACK;
        q->step_ms = now_ms;
        /* The read-back's first look is the next call: an ON asked now has
         * not reached the bench yet. */
        if (d->set_pwm || d->p5_on || d->pd_on) {
            return;
        }
    }
    if (q->step == (uint8_t)SERVO_SEQ_READBACK) {
        const servo_seq_part_t miss = missing(&q->need, in);
        if (miss != SERVO_SEQ_PART_NONE) {
            if (up(now_ms, q->step_ms, SERVO_SEQ_READBACK_MS)) {
                fail(q, miss, SERVO_SEQ_WHY_TIMEOUT, d);
            }
            return;
        }
        q->readback_ms = now_ms - q->step_ms;
        q->step    = (uint8_t)SERVO_SEQ_ARM;
        q->step_ms = now_ms;
        if (!in->armed) {
            d->arm = true;
            q->arm_asked = true;
            return;
        }
    }
    if (q->step == (uint8_t)SERVO_SEQ_ARM) {
        if (!in->armed) {
            if (up(now_ms, q->step_ms, SERVO_SEQ_ARMED_MS)) {
                fail(q, SERVO_SEQ_PART_ARM, SERVO_SEQ_WHY_TIMEOUT, d);
            }
            return;
        }
        q->step    = (uint8_t)SERVO_SEQ_RUN;
        q->step_ms = now_ms;
    }
    if (q->step == (uint8_t)SERVO_SEQ_RUN) {
        /* Asked until the caller answers: a frame that cannot start it
         * asks again on the next. */
        d->start = true;
    }
}

void servo_seq_started(servo_seq_t *q, bool ok, servo_seq_do_t *d)
{
    if (d == NULL) {
        return;
    }
    memset(d, 0, sizeof(*d));
    if (q == NULL || q->step != (uint8_t)SERVO_SEQ_RUN) {
        return;
    }
    if (!ok) {
        fail(q, SERVO_SEQ_PART_RUN, SERVO_SEQ_WHY_REFUSED, d);
        return;
    }
    q->step = (uint8_t)SERVO_SEQ_IDLE;
    q->done = true;
}

void servo_seq_abort(servo_seq_t *q, servo_seq_why_t why, servo_seq_do_t *d)
{
    if (d == NULL) {
        return;
    }
    memset(d, 0, sizeof(*d));
    if (!servo_seq_busy(q)) {
        return;
    }
    fail(q, part_now(q, NULL),
         (why == SERVO_SEQ_WHY_NONE) ? SERVO_SEQ_WHY_STOP : why, d);
}

int servo_seq_number(const servo_seq_need_t *need, servo_seq_step_t step,
                     int *count)
{
    /* Whether each step shows: PORT PWM and the supply where needed, the
     * read-back where either is. */
    const bool pwm = (need == NULL) || need->port_pwm;
    const bool sup = (need == NULL) || need->prog_5v || need->pd_mini;
    const bool shows[SERVO_SEQ_STEPS] = { false, pwm, sup, pwm || sup,
                                          true,  true };
    int n = 0, at = 0;
    for (int k = 1; k < (int)SERVO_SEQ_STEPS; ++k) {
        if (shows[k]) {
            ++n;
            if (k == (int)step) {
                at = n;
            }
        }
    }
    if (count != NULL) {
        *count = n;
    }
    return at;
}
