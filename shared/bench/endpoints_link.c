/*
 * The panel's half of the throttle's pulse endpoints.  See
 * endpoints_link.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "endpoints_link.h"

#include <stddef.h>

endpoints_next_t endpoints_next(bool owed, bool far_disarmed,
                                uint32_t edit_ms, uint32_t now_ms)
{
    if (!owed) {
        return ENDPOINTS_PAID;
    }
    if (!far_disarmed) {
        return ENDPOINTS_WAIT_DISARM;
    }
    if ((uint32_t)(now_ms - edit_ms) < ENDPOINTS_SETTLE_MS) {
        return ENDPOINTS_WAIT_SETTLE;
    }
    return ENDPOINTS_PAY;
}

bool endpoints_owed_after(endpoints_outcome_t outcome)
{
    switch (outcome) {
    case ENDPOINTS_READ_REFUSED:
    case ENDPOINTS_ORDER:
    case ENDPOINTS_HELD:
    case ENDPOINTS_TAKEN:
    case ENDPOINTS_WRITE_REFUSED:
        return false;
    case ENDPOINTS_NO_ANSWER:
    default:
        /* Not known to be in force: owed, and a switch to ARMED waits. */
        return true;
    }
}

void endpoints_publish(endpoints_debt_t *d, uint16_t min_us, uint16_t max_us,
                       uint32_t now_ms)
{
    if (d == NULL) {
        return;
    }
    atomic_store(&d->pair, ((unsigned)min_us << 16) | (unsigned)max_us);
    atomic_store(&d->edit_ms, (unsigned)now_ms);
    atomic_store(&d->owed, true);
}

void endpoints_link_up(endpoints_debt_t *d)
{
    if (d == NULL) {
        return;
    }
    atomic_store(&d->owed, true);
    d->hold = true;
}

bool endpoints_owed(const endpoints_debt_t *d)
{
    return d != NULL && atomic_load(&d->owed);
}

bool endpoints_held(const endpoints_debt_t *d)
{
    return d != NULL && d->hold;
}

uint16_t endpoints_min(const endpoints_debt_t *d)
{
    return (d == NULL) ? 0u : (uint16_t)(atomic_load(&d->pair) >> 16);
}

uint16_t endpoints_max(const endpoints_debt_t *d)
{
    return (d == NULL) ? 0u : (uint16_t)(atomic_load(&d->pair) & 0xFFFFu);
}

endpoints_next_t endpoints_take(endpoints_debt_t *d, bool far_disarmed,
                                uint32_t now_ms)
{
    if (d == NULL) {
        return ENDPOINTS_WAIT_DISARM;
    }
    const endpoints_next_t next =
        endpoints_next(atomic_load(&d->owed), far_disarmed,
                       (uint32_t)atomic_load(&d->edit_ms), now_ms);
    if (next == ENDPOINTS_PAY && atomic_exchange(&d->owed, false)) {
        return ENDPOINTS_PAY;
    }
    if (next == ENDPOINTS_PAY || next == ENDPOINTS_PAID) {
        /* Nothing owed, or the flag gone between the look and the taking:
         * only this task clears it, so that is nothing owed too. */
        d->hold = false;
        return ENDPOINTS_PAID;
    }
    return next;
}

void endpoints_paid(endpoints_debt_t *d, endpoints_outcome_t outcome)
{
    if (d == NULL) {
        return;
    }
    if (endpoints_owed_after(outcome)) {
        atomic_store(&d->owed, true);
    } else {
        d->hold = false;
    }
}
