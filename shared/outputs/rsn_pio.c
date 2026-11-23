/**
 * @file
 * @brief The remote servo node's PIO block placement; see rsn_pio.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "rsn_pio.h"

#include <stddef.h>
#include <string.h>

/** @brief The two blocks outputs and lines share. */
#define SLOT_BLOCKS ((uint8_t)((1u << 1) | (1u << 2)))

void rsn_pio_init(rsn_pio_t *p)
{
    if (p == NULL) {
        return;
    }
    memset(p, 0, sizeof(*p));
    memset(p->block, RSN_PIO_NONE, sizeof(p->block));
    p->line    = RSN_PIO_NONE;
    p->line_at = RSN_PIO_NONE;
}

uint8_t rsn_pio_output_blocks(const rsn_pio_t *p)
{
    uint8_t m = 0u;
    if (p == NULL) {
        return 0u;
    }
    for (unsigned g = 0; g < RSN_PIO_GPIOS; ++g) {
        if (p->block[g] < RSN_PIO_BLOCKS) {
            m |= (uint8_t)(1u << p->block[g]);
        }
    }
    return m;
}

unsigned rsn_pio_ppm_count(const rsn_pio_t *p)
{
    unsigned n = 0u;
    if (p == NULL) {
        return 0u;
    }
    for (unsigned g = 0; g < RSN_PIO_GPIOS; ++g) {
        if ((p->ppm & ((uint64_t)1u << g)) != 0u) {
            ++n;
        }
    }
    return n;
}

uint8_t rsn_pio_blocks(const rsn_pio_t *p, rsn_pio_use_t use,
                       uint8_t *line_at)
{
    uint8_t at = RSN_PIO_NONE;
    uint8_t m  = 0u;
    if (p != NULL) {
        const uint8_t held = rsn_pio_output_blocks(p);
        const uint8_t line = (p->line < RSN_PIO_BLOCKS)
                                 ? (uint8_t)(1u << p->line) : 0u;
        const uint8_t free_of = (uint8_t)(SLOT_BLOCKS & ~held);
        switch (use) {
        case RSN_PIO_FIXED:
            m = (uint8_t)(1u << RSN_PIO_FIXED_BLOCK);
            break;
        case RSN_PIO_PPM:
            if (rsn_pio_ppm_count(p) >= RSN_PIO_PPM_MAX) {
                break;
            }
            m = (uint8_t)(SLOT_BLOCKS & ~line);
            break;
        case RSN_PIO_OUTPUT:
            m = (uint8_t)(SLOT_BLOCKS & ~line);
            break;
        case RSN_PIO_LINE:
            if (line != 0u) {
                break;
            }
            if ((free_of & (1u << RSN_PIO_LINE_BLOCK)) != 0u) {
                at = RSN_PIO_LINE_BLOCK;
            } else if ((free_of & (1u << 2)) != 0u) {
                at = 2u;
            }
            m = (at != RSN_PIO_NONE) ? (uint8_t)(1u << at) : 0u;
            break;
        case RSN_PIO_LINE_PRE:
            if (line != 0u || free_of == 0u) {
                break;
            }
            /* The search's order: PIO2 first, and a line of 30 words fits
             * no block that holds an output. */
            at = ((free_of & (1u << 2)) != 0u) ? 2u : 1u;
            m  = SLOT_BLOCKS;
            break;
        case RSN_PIO_USES:
        default:
            break;
        }
    }
    if (line_at != NULL) {
        *line_at = at;
    }
    return m;
}

/**
 * @brief The state machines of a block that are claimed now.
 *
 * @param ops The chip's claim calls.
 * @param b The block.
 * @return Bit s for state machine s.
 */
static uint8_t claimed_set(const rsn_pio_ops_t *ops, unsigned b)
{
    uint8_t m = 0u;
    for (unsigned sm = 0; sm < RSN_PIO_SMS; ++sm) {
        if (ops->claimed(ops->ctx, b, sm)) {
            m |= (uint8_t)(1u << sm);
        }
    }
    return m;
}

rsn_pio_err_t rsn_pio_begin(rsn_pio_t *p, const rsn_pio_ops_t *ops,
                            rsn_pio_use_t use, uint8_t pin)
{
    if (p == NULL || ops == NULL || ops->claimed == NULL
        || ops->claim == NULL || ops->unclaim == NULL
        || (unsigned)use >= (unsigned)RSN_PIO_USES || pin >= RSN_PIO_GPIOS
        || p->open) {
        return RSN_PIO_BAD;
    }
    uint8_t at = RSN_PIO_NONE;
    const uint8_t allowed = rsn_pio_blocks(p, use, &at);
    if (allowed == 0u) {
        return (use == RSN_PIO_PPM
                && rsn_pio_ppm_count(p) >= RSN_PIO_PPM_MAX)
                   ? RSN_PIO_PPM_FULL : RSN_PIO_NO_BLOCK;
    }
    for (unsigned b = 0; b < RSN_PIO_BLOCKS; ++b) {
        p->before[b] = claimed_set(ops, b);
        p->fence[b]  = 0u;
        if ((allowed & (1u << b)) != 0u) {
            continue;
        }
        for (unsigned sm = 0; sm < RSN_PIO_SMS; ++sm) {
            if ((p->before[b] & (1u << sm)) == 0u) {
                ops->claim(ops->ctx, b, sm);
                p->fence[b] |= (uint8_t)(1u << sm);
            }
        }
    }
    p->open    = true;
    p->use     = (uint8_t)use;
    p->pin     = pin;
    p->allowed = allowed;
    p->line_at = at;
    return RSN_PIO_OK;
}

void rsn_pio_end(rsn_pio_t *p, const rsn_pio_ops_t *ops, bool ok)
{
    if (p == NULL || ops == NULL || !p->open) {
        return;
    }
    uint8_t where = RSN_PIO_NONE;
    for (unsigned b = 0; b < RSN_PIO_BLOCKS; ++b) {
        if ((p->allowed & (1u << b)) != 0u && where == RSN_PIO_NONE
            && (claimed_set(ops, b) & (uint8_t)~p->before[b]) != 0u) {
            where = (uint8_t)b;
        }
    }
    /* The fence goes whatever the bind did: a driver that failed has let
     * go of its own claims, and these are the placement's. */
    for (unsigned b = 0; b < RSN_PIO_BLOCKS; ++b) {
        for (unsigned sm = 0; sm < RSN_PIO_SMS; ++sm) {
            if ((p->fence[b] & (1u << sm)) != 0u) {
                ops->unclaim(ops->ctx, b, sm);
            }
        }
        p->fence[b] = 0u;
    }
    p->open = false;
    if (!ok) {
        return;
    }
    const rsn_pio_use_t use = (rsn_pio_use_t)p->use;
    if (use == RSN_PIO_LINE || use == RSN_PIO_LINE_PRE) {
        p->line = p->line_at;
    } else if ((use == RSN_PIO_OUTPUT || use == RSN_PIO_PPM)
               && where != RSN_PIO_NONE) {
        p->block[p->pin] = where;
        const uint64_t bit = (uint64_t)1u << p->pin;
        p->ppm = (use == RSN_PIO_PPM) ? (p->ppm | bit) : (p->ppm & ~bit);
    }
}

void rsn_pio_gone(rsn_pio_t *p, uint8_t pin)
{
    if (p == NULL || pin >= RSN_PIO_GPIOS) {
        return;
    }
    p->block[pin] = RSN_PIO_NONE;
    p->ppm &= ~((uint64_t)1u << pin);
}

void rsn_pio_line_gone(rsn_pio_t *p)
{
    if (p != NULL) {
        p->line = RSN_PIO_NONE;
    }
}
