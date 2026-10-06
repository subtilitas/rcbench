/*
 * The SERVO link page.  See servo_page.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_page.h"

#include <stddef.h>
#include <string.h>

#include "link_msg.h"
#include "outputs_pages.h"

void servo_page_init(servo_page_t *p)
{
    if (p != NULL) {
        memset(p, 0, sizeof(*p));   /* each slot's own rate, no sweep */
    }
}

static sweep_cfg_t cfg_of(const uint16_t *r)
{
    const sweep_cfg_t c = {
        .kind      = (sweep_kind_t)r[LINK_SV_SWEEP],
        .mhz       = r[LINK_SV_SWEEP_MHZ],
        .amplitude = r[LINK_SV_SWEEP_SPAN],
        .dwell_ms  = r[LINK_SV_SWEEP_DWELL_MS],
        .moves     = r[LINK_SV_SWEEP_MOVES],
    };
    return c;
}

/* Whether two sweeps trace the same curve; the movement count is not the
 * curve, and it applies to the next start. */
static bool same_curve(const sweep_cfg_t *a, const sweep_cfg_t *b)
{
    return a->kind == b->kind && a->mhz == b->mhz
           && a->amplitude == b->amplitude && a->dwell_ms == b->dwell_ms;
}

/*
 * Every surface held where its output has got to.  The last command a sweep
 * left would otherwise be slewed towards for as long again as the channel's
 * own timeout, past the moment the sweep stopped.  The channel's clock is
 * left alone, so it rests when it would have.
 */
static void freeze_surfaces(outputs_t *o)
{
    for (uint8_t ch = 0; ch < (uint8_t)LINK_OUT_CHANNELS; ++ch) {
        if (o->channel[ch].role == OUT_ROLE_SURFACE) {
            o->channel[ch].command = outputs_actual(o, ch);
        }
    }
}

uint8_t servo_page_write(servo_page_t *p, uint8_t off, uint8_t n,
                         const uint16_t *in, outputs_t *o, uint32_t now_ms)
{
    if (p == NULL || in == NULL || o == NULL) {
        return LINK_NACK_BAD_RANGE;
    }
    if ((unsigned)off + (unsigned)n > (unsigned)LINK_SV_COUNT) {
        return LINK_NACK_BAD_RANGE;
    }
    if (n == 0u) {
        return 0u;
    }
    if ((unsigned)off + (unsigned)n > (unsigned)LINK_SV_SWEEP_DONE) {
        return LINK_NACK_READ_ONLY;
    }
    /*
     * The page as it would be, judged whole before any of it is kept.  The
     * sweep register reads 0 once a sweep has stopped, so a write that
     * leaves it out is judged against a stopped sweep rather than a stale
     * curve.
     */
    uint16_t next[LINK_SV_COUNT];
    memcpy(next, p->regs, sizeof(next));
    if (!p->sweep.running && !p->holding) {
        next[LINK_SV_SWEEP] = 0u;
    }
    for (uint8_t i = 0; i < n; ++i) {
        next[off + i] = in[i];
    }
    const bool rate  = off == (uint8_t)LINK_SV_FRAME_HZ;
    const bool sweep = off <= (uint8_t)LINK_SV_SWEEP_DWELL_MS
                       && (unsigned)off + (unsigned)n > (unsigned)LINK_SV_SWEEP;
    if (rate) {
        const uint8_t nack = outputs_servo_rate_check(o, next[LINK_SV_FRAME_HZ]);
        if (nack != 0u) {
            return nack;
        }
    }
    const sweep_cfg_t cfg = cfg_of(next);
    const bool hold = sweep && next[LINK_SV_SWEEP] == LINK_SV_HOLD;
    if (hold && !outputs_armed(o)) {
        return LINK_NACK_NOT_ARMED;
    }
    if (sweep && next[LINK_SV_SWEEP] != 0u && !hold) {
        /*
         * A sweep starts, or changes, only from its four registers written
         * together: a curve rebuilt from a register or two and what the page
         * held before is one nobody asked for.  A stop is one register.
         */
        const bool whole = off <= (uint8_t)LINK_SV_SWEEP
                           && (unsigned)off + (unsigned)n
                                  > (unsigned)LINK_SV_SWEEP_DWELL_MS;
        if (!whole) {
            return LINK_NACK_BAD_VALUE;
        }
        if (next[LINK_SV_SWEEP] >= (uint16_t)SWEEP_KIND_COUNT
            || !sweep_cfg_valid(&cfg)) {
            return LINK_NACK_BAD_VALUE;
        }
        /* A curve on a bench that drives nothing would start the moment it
         * arms, from wherever its clock had got to. */
        if (!outputs_armed(o)) {
            return LINK_NACK_NOT_ARMED;
        }
    }

    const bool was_running = p->sweep.running;
    const sweep_cfg_t was  = p->sweep.cfg;
    memcpy(p->regs, next, sizeof(next));
    if (hold) {
        /*
         * Held where the outputs are: a sweep stopped exactly where its
         * output had got to, which only this end knows -- the panel's
         * drawing is an estimate without feedback.  Each write keeps it.
         */
        if (!p->holding) {
            sweep_stop(&p->sweep);
            freeze_surfaces(o);
        }
        p->holding  = true;
        p->finished = false;
        p->heard_ms = now_ms;
        return 0u;
    }
    if (sweep) {
        p->holding = false;
        if (next[LINK_SV_SWEEP] == 0u) {
            /* Stopped where the outputs are, as silence stops it: the
             * command the panel follows this with takes a transaction or
             * two, and the servo must not go on towards the curve's last
             * target meanwhile. */
            if (p->sweep.running) {
                freeze_surfaces(o);
            }
            sweep_stop(&p->sweep);
            p->finished = false;
        } else if (p->finished && same_curve(&cfg, &was)) {
            /* Done, and not started again; but still asked for, so the
             * centre it ended on is kept commanded while the output slews
             * there (servo_page_step()). */
            p->regs[LINK_SV_SWEEP] = 0u;
            p->heard_ms = now_ms;
        } else {
            if (!(was_running && same_curve(&cfg, &was))) {
                (void)sweep_start(&p->sweep, &cfg, now_ms);
                p->regs[LINK_SV_SWEEP_DONE] = 0u;
                p->finished = false;
            }
            p->heard_ms = now_ms;
        }
    }
    return 0u;
}

void servo_page_read(servo_page_t *p, uint8_t off, uint8_t n, uint16_t *out)
{
    if (p == NULL || out == NULL
        || (unsigned)off + (unsigned)n > (unsigned)LINK_SV_COUNT) {
        return;
    }
    if (!p->sweep.running && !p->holding) {
        p->regs[LINK_SV_SWEEP] = 0u;
    }
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = p->regs[off + i];
    }
}

bool servo_page_step(servo_page_t *p, outputs_t *o, uint32_t now_ms)
{
    /*
     * A hold keeps each surface at the command it was frozen at, stamped
     * each pass while the panel repeats it; on silence or a disarm it ends
     * and the channels rest as any command does.
     */
    if (p != NULL && o != NULL && p->holding) {
        if (!outputs_armed(o)
            || (uint32_t)(now_ms - p->heard_ms) > OUT_DEFAULT_TIMEOUT_MS) {
            p->holding = false;
            p->regs[LINK_SV_SWEEP] = 0u;
        } else {
            for (uint8_t ch = 0; ch < (uint8_t)LINK_OUT_CHANNELS; ++ch) {
                if (o->channel[ch].role == OUT_ROLE_SURFACE) {
                    (void)outputs_set(o, ch, o->channel[ch].command, now_ms);
                }
            }
            p->regs[LINK_SV_SWEEP] = (uint16_t)LINK_SV_HOLD;
            return false;
        }
    }
    /*
     * A sweep that has made its movements and is still being repeated holds
     * the surfaces at the centre it ended on: stamped each pass, so a slow
     * SPEED slews them there rather than the channel's timeout dropping them
     * to rest part way.  Once the repeats stop, the channels rest as any
     * command does.
     */
    if (p != NULL && o != NULL && p->finished && outputs_armed(o)
        && (uint32_t)(now_ms - p->heard_ms) <= OUT_DEFAULT_TIMEOUT_MS) {
        (void)outputs_set_role_channels(o, OUT_ROLE_SURFACE,
                                        (uint8_t)LINK_OUT_CHANNELS,
                                        (uint16_t)SWEEP_CENTRE, now_ms);
    }
    if (p == NULL || o == NULL || !p->sweep.running) {
        if (p != NULL) {
            p->regs[LINK_SV_SWEEP] = 0u;
        }
        return false;
    }
    /*
     * Stopped where nothing would drive it or nobody is asking for it: the
     * bench disarmed, or a panel that has gone quiet for as long as a
     * channel command is trusted.
     */
    if (!outputs_armed(o)
        || (uint32_t)(now_ms - p->heard_ms) > OUT_DEFAULT_TIMEOUT_MS) {
        sweep_stop(&p->sweep);
        freeze_surfaces(o);
        p->regs[LINK_SV_SWEEP] = 0u;
        return false;
    }
    uint16_t cmd = (uint16_t)SWEEP_CENTRE;
    const bool running = sweep_step(&p->sweep, now_ms, &cmd);
    const uint32_t done = sweep_moves(&p->sweep, now_ms);
    p->regs[LINK_SV_SWEEP_DONE] = (done > 0xFFFFu) ? 0xFFFFu : (uint16_t)done;
    /* The last pass puts the surfaces at the centre the sweep ends on. */
    (void)outputs_set_role_channels(o, OUT_ROLE_SURFACE,
                                    (uint8_t)LINK_OUT_CHANNELS, cmd, now_ms);
    if (!running) {
        p->regs[LINK_SV_SWEEP] = 0u;
        p->finished = true;
    }
    return running;
}

uint16_t servo_page_hz(const servo_page_t *p)
{
    return (p != NULL) ? p->regs[LINK_SV_FRAME_HZ] : 0u;
}
