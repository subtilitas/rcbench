/**
 * @file
 * @brief The SENSE_LIMIT page: see sense_limit.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_limit.h"

#include <stddef.h>
#include <string.h>

#include "link_msg.h"
#include "link_pages.h"
#include "sense_page_int.h"

_Static_assert(LINK_SL_KEPT == LINK_SM_CHANNELS,
               "a limit for each of the map's channels");
_Static_assert(LINK_SL_COUNT <= LINK_MAX_REGS, "SENSE_LIMIT fits a page");

static bool on_page(uint8_t off, uint8_t n)
{
    return (unsigned)off + (unsigned)n <= (unsigned)LINK_SL_COUNT;
}

void sense_limit_peek(const sense_page_t *p, uint8_t off, uint8_t n,
                      uint16_t *out)
{
    if (p == NULL || out == NULL || !on_page(off, n)) {
        return;
    }
    memcpy(out, &p->limit[off], (size_t)n * sizeof(uint16_t));
}

void sense_limit_read(sense_page_t *p, uint8_t off, uint8_t n,
                      uint16_t *out)
{
    if (p == NULL || out == NULL || !on_page(off, n)) {
        return;
    }
    sense_limit_peek(p, off, n, out);
    if (off <= LINK_SL_OVER && (unsigned)off + (unsigned)n > LINK_SL_OVER) {
        p->limit[LINK_SL_OVER] = 0u;
    }
}

uint8_t sense_limit_write(sense_page_t *p, uint8_t off, uint8_t n,
                          const uint16_t *in)
{
    if (p == NULL || in == NULL || !on_page(off, n)) {
        return LINK_NACK_BAD_RANGE;
    }
    if ((unsigned)off + (unsigned)n > (unsigned)LINK_SL_KEPT) {
        return LINK_NACK_READ_ONLY;
    }
    for (uint8_t i = 0; i < n; ++i) {
        if (in[i] > LINK_SL_MA_MAX) {
            return LINK_NACK_BAD_VALUE;
        }
    }
    memcpy(&p->limit[off], in, (size_t)n * sizeof(uint16_t));
    return 0u;
}

void sense_limit_kept(const sense_page_t *p, uint16_t *kept)
{
    memcpy(kept, p->limit, LINK_SL_KEPT * sizeof(uint16_t));
}

uint8_t sense_limit_restore(sense_page_t *p, const uint16_t *kept)
{
    return sense_limit_write(p, 0u, (uint8_t)LINK_SL_KEPT, kept);
}

void sense_limit_judge(sense_page_t *p, const sense_snap_t *s)
{
    /* Core 1 counts each channel's windows over its limit as they close;
     * what the count gained since the last snapshot taken is OVER's bit
     * and OVER_COUNT's gain, however many windows that snapshot spans. */
    for (unsigned c = 0; c < LINK_SM_CHANNELS; ++c) {
        const uint16_t gain = (uint16_t)(s->over_n[c] - p->lim_seen[c]);
        p->lim_seen[c] = s->over_n[c];
        if (gain != 0u) {
            p->limit[LINK_SL_OVER] |= (uint16_t)(1u << c);
            p->limit[LINK_SL_OVER_COUNT] =
                (uint16_t)(p->limit[LINK_SL_OVER_COUNT] + gain);
        }
    }
}
