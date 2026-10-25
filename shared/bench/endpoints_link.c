/**
 * @file
 * @brief The panel's half of the throttle's pulse endpoints.
 *
 * See endpoints_link.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "endpoints_link.h"

#include <stddef.h>

#include "link_pages.h"

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
    case ENDPOINTS_DIFFERS:
        return false;
    case ENDPOINTS_NO_ANSWER:
    default:
        /* Not known to be in force: owed, and a switch to ARMED waits. */
        return true;
    }
}

bool endpoints_page_holds(const uint16_t *page, uint16_t min_us,
                          uint16_t max_us)
{
    if (page == NULL) {
        return false;
    }
    for (unsigned c = 0; c < LINK_OUT_CHANNELS; ++c) {
        const uint16_t *r = &page[(size_t)c * LINK_CC_STRIDE];
        if (r[LINK_CC_ROLE] == LINK_CC_ROLE_THROTTLE
            && (r[LINK_CC_MIN_US] != min_us || r[LINK_CC_MAX_US] != max_us)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief What an answered payment leaves for the switch to ARMED.
 *
 * @param outcome The outcome of the payment.
 * @return The block reason for the outcome.
 */
static endpoints_block_t block_of(endpoints_outcome_t outcome)
{
    switch (outcome) {
    case ENDPOINTS_READ_REFUSED:  return ENDPOINTS_BLOCK_UNKNOWN;
    case ENDPOINTS_ORDER:         return ENDPOINTS_BLOCK_ORDER;
    case ENDPOINTS_WRITE_REFUSED: return ENDPOINTS_BLOCK_REFUSED;
    case ENDPOINTS_DIFFERS:       return ENDPOINTS_BLOCK_DIFFERS;
    case ENDPOINTS_HELD:
    case ENDPOINTS_TAKEN:
    case ENDPOINTS_NO_ANSWER:
    default:                      return ENDPOINTS_BLOCK_NONE;
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

void endpoints_recheck(endpoints_debt_t *d)
{
    if (d != NULL) {
        atomic_store(&d->owed, true);
    }
}

endpoints_block_t endpoints_block(const endpoints_debt_t *d)
{
    if (d == NULL) {
        return ENDPOINTS_BLOCK_NONE;
    }
    if (atomic_load(&d->owed)) {
        return ENDPOINTS_BLOCK_OWED;
    }
    return (endpoints_block_t)d->refused;
}

bool endpoints_blocked(const endpoints_debt_t *d)
{
    return endpoints_block(d) != ENDPOINTS_BLOCK_NONE;
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
        d->hold    = false;
        d->refused = (uint8_t)block_of(outcome);
    }
}
