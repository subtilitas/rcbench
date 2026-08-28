#include "can_timing.h"

#include <string.h>

void can_timing_limits_mcp2515(can_timing_limits_t *out, uint32_t crystal_hz)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->clock_hz = crystal_hz;
    /*
     * TQ = 2 x (BRP + 1) / Fosc, so the divisor is even and runs 2..128 --
     * the factor of two is in the silicon and is the reason an 8 MHz part
     * runs out of quanta so early.
     */
    out->div_min   = 2;
    out->div_max   = 128;
    out->div_step  = 2;
    /* PropSeg 1..8 plus PhaseSeg1 1..8. */
    out->tseg1_min = 2;
    out->tseg1_max = 16;
    /* PhaseSeg2 2..8; the datasheet forbids 1. */
    out->tseg2_min = 2;
    out->tseg2_max = 8;
    out->sjw_max   = 4;
    out->tq_min    = 8;
    out->tq_max    = 25;
}

void can_timing_limits_twai(can_timing_limits_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->clock_hz  = 80000000u;   /* APB */
    out->div_min   = 2;
    out->div_max   = 16384;
    out->div_step  = 2;
    out->tseg1_min = 1;
    out->tseg1_max = 16;
    /*
     * Two, not the one the peripheral would accept.  Phase 2 bounds the
     * resynchronisation jump width, so a phase 2 of one quantum forces SJW to
     * one -- a node that can absorb a single quantum of drift and no more.
     * The controller at the other end of this bus runs eight quanta to the
     * bit, where one quantum is an eighth of it.  Nothing is gained by
     * allowing it and a margin is lost.
     */
    out->tseg2_min = 2;
    out->tseg2_max = 8;
    out->sjw_max   = 4;
    out->tq_min    = 3;
    out->tq_max    = 25;
}

/* Distance from the target sample point, for choosing between splits. */
static uint32_t sp_error(uint8_t tseg1, uint8_t tq, uint16_t target)
{
    const uint32_t sp = ((uint32_t)(1u + tseg1) * 1000u) / tq;
    return (sp > target) ? (sp - target) : (target - sp);
}

bool can_timing_solve(const can_timing_limits_t *lim, uint32_t bitrate,
                      uint16_t target_permille, can_timing_t *out)
{
    if (lim == NULL || out == NULL || bitrate == 0 || lim->clock_hz == 0
        || lim->div_step == 0) {
        return false;
    }

    bool found = false;
    can_timing_t best;
    memset(&best, 0, sizeof(best));
    uint32_t best_err = 0;

    for (uint32_t div = lim->div_min; div <= lim->div_max;
         div += lim->div_step) {
        /*
         * Exact only.  clock = div x tq x bitrate has to hold in integers, and
         * anything else is a bit rate that is nearly right -- which two nodes
         * will agree about on short frames and disagree about on long ones.
         */
        const uint32_t denom = div * bitrate;
        if (denom == 0 || lim->clock_hz % denom != 0) {
            continue;
        }
        const uint32_t tq = lim->clock_hz / denom;
        if (tq < lim->tq_min || tq > lim->tq_max) {
            continue;
        }

        for (uint32_t t1 = lim->tseg1_min; t1 <= lim->tseg1_max; ++t1) {
            if (t1 + 1u >= tq) {
                break;
            }
            const uint32_t t2 = tq - 1u - t1;
            if (t2 < lim->tseg2_min || t2 > lim->tseg2_max) {
                continue;
            }
            const uint32_t err = sp_error((uint8_t)t1, (uint8_t)tq,
                                          target_permille);
            /*
             * Prefer the closest sample point; break ties on the larger
             * prescaler, which means fewer quanta per bit and so a coarser --
             * and more forgiving -- resynchronisation step.
             */
            if (!found || err < best_err) {
                found    = true;
                best_err = err;
                best.div   = (uint16_t)div;
                best.tseg1 = (uint8_t)t1;
                best.tseg2 = (uint8_t)t2;
                /* SJW cannot exceed phase 2, or a resynchronisation could
                 * shorten the bit past its own sample point. */
                best.sjw = (uint8_t)((t2 < lim->sjw_max) ? t2 : lim->sjw_max);
                best.tq  = (uint8_t)tq;
                best.bitrate_hz = bitrate;
                best.sample_permille =
                    (uint16_t)(((uint32_t)(1u + t1) * 1000u) / tq);
            }
        }
    }

    if (found) {
        *out = best;
    }
    return found;
}

uint32_t can_timing_max_bitrate(const can_timing_limits_t *lim)
{
    if (lim == NULL || lim->clock_hz == 0) {
        return 0;
    }
    /* The fastest bit is the smallest divisor with the fewest quanta. */
    const uint32_t denom = (uint32_t)lim->div_min * lim->tq_min;
    if (denom == 0) {
        return 0;
    }
    const uint32_t rate = lim->clock_hz / denom;
    can_timing_t t;
    return can_timing_solve(lim, rate, CAN_SAMPLE_POINT_DEFAULT, &t) ? rate : 0;
}

bool mcp2515_encode_timing(const can_timing_t *t, uint8_t cnf[3])
{
    if (t == NULL || cnf == NULL || t->div < 2 || (t->div & 1u) != 0) {
        return false;
    }
    const uint32_t brp = (t->div / 2u) - 1u;
    if (brp > 63u || t->sjw < 1u || t->sjw > 4u) {
        return false;
    }
    if (t->tseg2 < 2u || t->tseg2 > 8u) {
        return false;
    }

    /*
     * Split tseg1 into propagation and phase 1.  Both are 1..8, so a tseg1 of
     * 2..16 always splits and 17 never does.  Phase 1 takes the larger half:
     * lengthening phase 1 is what a positive resynchronisation does, so the
     * segment that absorbs it should be the one with room.
     */
    if (t->tseg1 < 2u || t->tseg1 > 16u) {
        return false;
    }
    uint8_t phseg1 = (uint8_t)((t->tseg1 + 1u) / 2u);
    if (phseg1 > 8u) {
        phseg1 = 8u;
    }
    const uint8_t prseg = (uint8_t)(t->tseg1 - phseg1);
    if (prseg < 1u || prseg > 8u) {
        return false;
    }

    cnf[0] = (uint8_t)(((t->sjw - 1u) << 6) | (uint8_t)brp);
    /* BTLMODE set: phase 2 comes from CNF3 rather than from the bit time. */
    cnf[1] = (uint8_t)(0x80u | ((phseg1 - 1u) << 3) | (prseg - 1u));
    cnf[2] = (uint8_t)(t->tseg2 - 1u);
    return true;
}
