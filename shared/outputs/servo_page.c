/**
 * @file
 * @brief The SERVO link page.
 *
 * See servo_page.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_page.h"

#include <stddef.h>
#include <string.h>

#include "link_msg.h"
#include "out_pwm_map.h"
#include "outputs_pages.h"
#include "prog_port.h"

/*
 * Clears the page: all registers, no sweep, no hold.
 *
 * @param p The page state, overwritten.
 */
void servo_page_init(servo_page_t *p)
{
    if (p != NULL) {
        memset(p, 0, sizeof(*p));   /* each slot's own rate, no sweep */
    }
}

/**
 * @brief The sweep curve the page's registers describe.
 *
 * @param r The page registers, LINK_SV_COUNT of them.
 * @return The curve: kind, MHz, amplitude, dwell and movements.
 */
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

/**
 * @brief Whether two sweeps trace the same curve; the movement count is not
 *        the curve, and it applies to the next start.
 *
 * @param a The first sweep.
 * @param b The second sweep.
 * @return true when kind, mhz, amplitude and dwell_ms are equal.
 */
static bool same_curve(const sweep_cfg_t *a, const sweep_cfg_t *b)
{
    return a->kind == b->kind && a->mhz == b->mhz
           && a->amplitude == b->amplitude && a->dwell_ms == b->dwell_ms;
}

/**
 * @brief The channels the sweep and the hold command: the standard servo
 *        ports, or the range servo_page_target() named.
 *
 * @param p The page state.
 * @return The first channel index, 0 for the standard servo ports.
 */
static uint8_t target_first(const servo_page_t *p)
{
    return (p->target_n != 0u) ? p->target_first : 0u;
}

/**
 * @brief One past the last channel the sweep and the hold command.
 *
 * @param p The page state.
 * @return target_first + target_n, or LINK_OUT_CHANNELS for the standard servo
 *         ports.
 */
static uint8_t target_end(const servo_page_t *p)
{
    return (p->target_n != 0u) ? (uint8_t)(p->target_first + p->target_n)
                               : (uint8_t)LINK_OUT_CHANNELS;
}

/**
 * @brief Every surface of the target commanded to @p cmd.
 *
 * @param p The page state, for the target range.
 * @param o The outputs bank. Only channels with role OUT_ROLE_SURFACE change.
 * @param cmd The command for each surface.
 * @param now_ms Current time in milliseconds, passed to outputs_set().
 */
static void command_surfaces(const servo_page_t *p, outputs_t *o,
                             uint16_t cmd, uint32_t now_ms)
{
    for (uint8_t ch = target_first(p); ch < target_end(p); ++ch) {
        if (o->channel[ch].role == OUT_ROLE_SURFACE) {
            (void)outputs_set(o, ch, cmd, now_ms);
        }
    }
}

/**
 * @brief Every surface held where its output has got to.
 *
 * The last command a sweep left would otherwise be slewed towards for as long
 * again as the channel's own timeout, past the moment the sweep stopped.  The
 * channel's clock is left alone, so it rests when it would have.
 *
 * @param p The page state, for the target range.
 * @param o The outputs bank. Each surface's command becomes its actual value.
 */
static void freeze_surfaces(const servo_page_t *p, outputs_t *o)
{
    for (uint8_t ch = target_first(p); ch < target_end(p); ++ch) {
        if (o->channel[ch].role == OUT_ROLE_SURFACE) {
            o->channel[ch].command = outputs_actual(o, ch);
        }
    }
}

/**
 * @brief Whether a RESUME can carry on the sweep held now: a phase kept by a
 *        hold still in force and a page that still describes that sweep.
 *
 * A curve changed while held is a new sweep, which starts only whole.
 *
 * @param p The page state.
 * @param next The page registers as the write would leave them.
 * @return true when a hold is in force, a paused sweep is kept, and the
 *         registers MHZ, SPAN, DWELL_MS and MOVES match that sweep.
 */
static bool resumable(const servo_page_t *p, const uint16_t *next)
{
    if (!p->holding || !p->sweep.paused) {
        return false;
    }
    const sweep_cfg_t *k = &p->sweep.cfg;
    return next[LINK_SV_SWEEP_MHZ] == k->mhz
           && next[LINK_SV_SWEEP_SPAN] == k->amplitude
           && next[LINK_SV_SWEEP_DWELL_MS] == k->dwell_ms
           && next[LINK_SV_SWEEP_MOVES] == k->moves;
}

/**
 * @brief Whether the frame rate @p hz may be the target's.
 *
 * The standard servo ports: every PWM slot whose first channel is a surface,
 * judged against the slices it shares (outputs_servo_rate_check()) and against
 * every PWM slot's endpoints at the rate it runs at, 0 sending the surfaces
 * back to their own.  A programming port: its own pin alone, which shares no
 * slice with a slot; a pin that shares its slice with another port's stays at
 * PROG_PORT_RATE_HZ, and a port whose pulse is on takes no rate its endpoints
 * do not fit.  0 always fits: the port goes back to 50Hz, whose 19999us frame
 * holds every endpoint the pages take.
 *
 * @param p The page state, for the target and the port masks.
 * @param o The outputs bank, for the slots and the channels' endpoints.
 * @param hz The frame rate in Hz to judge.
 * @return 0 when @p hz fits. Otherwise the refusal of
 *         outputs_servo_rate_check() or outputs_pulse_check() for the
 *         standard servo ports, or LINK_NACK_BAD_VALUE for a programming port
 *         whose range or endpoints do not fit @p hz.
 */
static uint8_t rate_check(const servo_page_t *p, const outputs_t *o,
                          uint16_t hz)
{
    if (p->target_n == 0u) {
        const uint8_t nack = outputs_servo_rate_check(o, hz);
        return (nack != 0u) ? nack : outputs_pulse_check(o, hz, 0u, 0u);
    }
    if (hz == 0u) {
        return 0u;
    }
    const out_driver_def_t *pwm = out_driver(OUT_DRIVER_PWM);
    if (hz < pwm->rate_min_hz || hz > pwm->rate_max_hz) {
        return LINK_NACK_BAD_VALUE;
    }
    for (uint8_t ch = target_first(p); ch < target_end(p); ++ch) {
        const uint32_t bit = (ch < 16u) ? (1u << ch) : 0u;
        if (hz != PROG_PORT_RATE_HZ && (p->sliced & bit) != 0u) {
            return LINK_NACK_BAD_VALUE;
        }
        if ((p->ports & bit) != 0u
            && o->channel[ch].max_us > out_pwm_pulse_max(hz)) {
            return LINK_NACK_BAD_VALUE;
        }
    }
    return 0u;
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
     * The page as a pass at this moment leaves it.  The IOmcu serves
     * a pass's writes before its servo_page_step(), so a write can land
     * after a sweep has run out -- unwritten for OUT_DEFAULT_TIMEOUT_MS,
     * disarmed, or past its last movement -- and before the pass that ends
     * it.  Judged as that pass leaves it, the sweep has ended: its last
     * pass has commanded the surfaces to the centre, a hold keeps no phase
     * of it, a resume is refused and a repeat does not carry it on.  A pass
     * repeated at the same time changes nothing.
     */
    (void)servo_page_step(p, o, now_ms);
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
        const uint8_t nack = rate_check(p, o, next[LINK_SV_FRAME_HZ]);
        if (nack != 0u) {
            return nack;
        }
    }
    const sweep_cfg_t cfg = cfg_of(next);
    const bool hold = sweep && next[LINK_SV_SWEEP] == LINK_SV_HOLD;
    const bool resume = sweep && next[LINK_SV_SWEEP] == LINK_SV_RESUME;
    if ((hold || resume) && !outputs_role_armed(o, OUT_ROLE_SURFACE)) {
        return LINK_NACK_NOT_ARMED;
    }
    if (resume && !resumable(p, next)) {
        return LINK_NACK_BAD_VALUE;
    }
    if (sweep && next[LINK_SV_SWEEP] != 0u && !hold && !resume) {
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
        if (!outputs_role_armed(o, OUT_ROLE_SURFACE)) {
            return LINK_NACK_NOT_ARMED;
        }
    }

    const bool was_running = p->sweep.running;
    const sweep_cfg_t was  = p->sweep.cfg;
    memcpy(p->regs, next, sizeof(next));
    if (resume) {
        /*
         * On from the kept phase.  The surfaces are commanded along the
         * curve from the next pass and slew there from where they were
         * held; the curve's clock runs from now, not from their arrival.
         */
        (void)sweep_resume(&p->sweep, now_ms);
        p->regs[LINK_SV_SWEEP] = (uint16_t)p->sweep.cfg.kind;
        p->holding  = false;
        p->finished = false;
        p->heard_ms = now_ms;
        return 0u;
    }
    if (hold) {
        /*
         * Held where the outputs are: a sweep stopped exactly where its
         * output had got to, which only this end knows -- the panel's
         * drawing is an estimate without feedback.  Each write keeps it.
         * A running sweep keeps its phase for a RESUME; anything else held
         * keeps none.  A sweep that has made its movements is held at the
         * centre its last pass commanded, the place it ends on whichever
         * of the hold and that pass came first.
         */
        if (!p->holding) {
            if (p->sweep.running) {
                sweep_pause(&p->sweep, now_ms);
                freeze_surfaces(p, o);
            } else if (p->finished) {
                sweep_stop(&p->sweep);
            } else {
                sweep_stop(&p->sweep);
                freeze_surfaces(p, o);
            }
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
                freeze_surfaces(p, o);
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
     * A hold keeps each surface at the command it was frozen at, while the
     * panel repeats it.  Stamped with the time of the last repeat, not this
     * pass, so the channel's own timeout runs from the panel's last word as
     * it does for any command: silence rests it 500ms after that, not
     * 500ms after this end stopped holding.
     */
    if (p != NULL && o != NULL && p->holding) {
        if (!outputs_role_armed(o, OUT_ROLE_SURFACE)
            || (uint32_t)(now_ms - p->heard_ms) > OUT_DEFAULT_TIMEOUT_MS) {
            /* And the phase it kept: nothing is left to resume. */
            p->holding = false;
            sweep_stop(&p->sweep);
            p->regs[LINK_SV_SWEEP] = 0u;
        } else {
            for (uint8_t ch = target_first(p); ch < target_end(p); ++ch) {
                if (o->channel[ch].role == OUT_ROLE_SURFACE) {
                    (void)outputs_set(o, ch, o->channel[ch].command,
                                      p->heard_ms);
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
    if (p != NULL && o != NULL && p->finished
        && outputs_role_armed(o, OUT_ROLE_SURFACE)
        && (uint32_t)(now_ms - p->heard_ms) <= OUT_DEFAULT_TIMEOUT_MS) {
        command_surfaces(p, o, (uint16_t)SWEEP_CENTRE, now_ms);
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
    if (!outputs_role_armed(o, OUT_ROLE_SURFACE)
        || (uint32_t)(now_ms - p->heard_ms) > OUT_DEFAULT_TIMEOUT_MS) {
        sweep_stop(&p->sweep);
        freeze_surfaces(p, o);
        p->regs[LINK_SV_SWEEP] = 0u;
        return false;
    }
    uint16_t cmd = (uint16_t)SWEEP_CENTRE;
    const bool running = sweep_step(&p->sweep, now_ms, &cmd);
    const uint32_t done = sweep_moves(&p->sweep, now_ms);
    p->regs[LINK_SV_SWEEP_DONE] = (done > 0xFFFFu) ? 0xFFFFu : (uint16_t)done;
    /* The last pass puts the surfaces at the centre the sweep ends on. */
    command_surfaces(p, o, cmd, now_ms);
    if (!running) {
        p->regs[LINK_SV_SWEEP] = 0u;
        p->finished = true;
    }
    return running;
}

bool servo_page_target(servo_page_t *p, outputs_t *o, uint8_t first,
                       uint8_t count)
{
    if (p == NULL || o == NULL
        || (unsigned)first + (unsigned)count > OUT_MAX_CHANNELS) {
        return false;
    }
    if (count == 0u) {
        first = 0u;
    }
    if (first == p->target_first && count == p->target_n) {
        return true;
    }
    /* Stopped as a SWEEP of 0 stops it, on the channels it moved. */
    if (p->sweep.running) {
        freeze_surfaces(p, o);
    }
    sweep_stop(&p->sweep);
    p->holding  = false;
    p->finished = false;
    p->regs[LINK_SV_SWEEP] = 0u;
    /* The rate was the old target's: the new one starts at its own. */
    p->regs[LINK_SV_FRAME_HZ] = 0u;
    p->target_first = first;
    p->target_n     = count;
    return true;
}

void servo_page_ports(servo_page_t *p, uint16_t channels, uint16_t sliced)
{
    if (p != NULL) {
        p->ports  = channels;
        p->sliced = sliced;
    }
}

uint16_t servo_page_hz(const servo_page_t *p)
{
    return (p != NULL) ? p->regs[LINK_SV_FRAME_HZ] : 0u;
}

uint16_t servo_page_slots_hz(const servo_page_t *p)
{
    return (p != NULL && p->target_n == 0u) ? p->regs[LINK_SV_FRAME_HZ] : 0u;
}

servo_resume_t servo_page_resume_plan(bool asked, bool held, bool timed,
                                      uint16_t proto_minor, bool refused)
{
    if (!asked || !held) {
        return SERVO_RESUME_CURVE;
    }
    if (proto_minor < 6u) {
        return SERVO_RESUME_TOO_OLD;
    }
    if (!timed) {
        return SERVO_RESUME_UNTIMED;
    }
    return refused ? SERVO_RESUME_REFUSED : SERVO_RESUME_WRITE;
}

void servo_phase_started(servo_phase_t *ph, uint32_t ack_ms)
{
    if (ph != NULL) {
        ph->start_ms = ack_ms;
        ph->kept     = false;
    }
}

void servo_phase_stopped(servo_phase_t *ph)
{
    if (ph != NULL) {
        ph->kept    = false;
    }
}

bool servo_phase_resumable(const servo_phase_t *ph)
{
    return ph != NULL && ph->kept;
}

uint32_t servo_phase_held(servo_phase_t *ph, uint32_t ack_ms)
{
    if (ph == NULL) {
        return 0u;
    }
    ph->kept_ms = ack_ms - ph->start_ms;
    ph->kept    = true;
    return ph->kept_ms;
}

bool servo_phase_resumed(servo_phase_t *ph, uint32_t ack_ms,
                         uint32_t *start_ms)
{
    if (ph == NULL || !ph->kept) {
        return false;
    }
    ph->start_ms = ack_ms - ph->kept_ms;
    ph->kept     = false;
    if (start_ms != NULL) {
        *start_ms = ph->start_ms;
    }
    return true;
}
