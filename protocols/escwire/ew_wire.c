/**
 * @file ew_wire.c
 * @brief The transaction's limits and timing; see ew_wire.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ew_wire.h"

bool ew_xfer_valid(const ew_xfer_t *x)
{
    if (x == NULL || x->rate >= (uint8_t)EW_RATES || x->tx == NULL
        || x->n_tx == 0u || x->n_tx > EW_TX_MAX || x->n_head >= x->n_tx
        || x->pause_bits > EW_PAUSE_BITS_MAX
        || (x->pause_bits != 0u && x->n_head == 0u)
        || x->max_rx > EW_RX_MAX) {
        return false;
    }
    if (x->max_rx == 0u) {
        return x->rx == NULL && x->first_us == 0u && x->gap_us == 0u;
    }
    return x->rx != NULL && x->first_us != 0u
           && x->first_us <= EW_FIRST_US_MAX && x->gap_us != 0u
           && x->gap_us <= EW_GAP_US_MAX;
}

uint32_t ew_xfer_span_us(const ew_xfer_t *x)
{
    const uint32_t bit_ns = (x->rate == (uint8_t)EW_RATE_A) ? EW_BIT_A_NS
                                                            : EW_BIT_TEXT_NS;
    const uint32_t bits = (uint32_t)x->n_tx * 10u + x->pause_bits
                          + (uint32_t)x->max_rx * 12u;
    const uint64_t send_us = ((uint64_t)bits * bit_ns + 999u) / 1000u;
    uint64_t waits = 0u;

    if (x->max_rx != 0u) {
        waits = (uint64_t)x->first_us
                + (uint64_t)(x->max_rx - 1u) * x->gap_us;
    }
    return (uint32_t)(send_us + waits);
}

uint32_t ew_elapsed_us(uint32_t now, uint32_t then)
{
    return now - then;
}

bool ew_due(uint32_t now, uint32_t then, uint32_t wait_us)
{
    return ew_elapsed_us(now, then) > wait_us;
}
