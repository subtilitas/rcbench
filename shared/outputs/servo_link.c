/*
 * The panel's half of the SERVO screen on the link (servo_link.h).
 *
 * The control task is the only caller but for the atomics servo_link.h
 * names: it is the only task that puts anything on the wire.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_link.h"

#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

#include "bind_link.h"
#include "link_pages.h"
#include "outputs.h"
#include "servo_page.h"

/* The panel, as servo_link_init() handed it over. */
static const servo_link_ctx_t *s_ctx;

/*
 * The callbacks, each called from one function of its own.  Not inlined,
 * so tools/stack_check.py follows each by name (SERVO_CALLS).
 */
static __attribute__((noinline)) uint32_t sl_now_ms(void)
{
    return s_ctx->port->now_ms(s_ctx->port->ctx);
}

static __attribute__((noinline)) bool sl_armed(void)
{
    return s_ctx->armed(s_ctx->ctx);
}

static __attribute__((noinline)) bool sl_stopped(void)
{
    return s_ctx->stopped(s_ctx->ctx);
}

static __attribute__((noinline)) void sl_alert(servo_link_alert_t a)
{
    s_ctx->alert(s_ctx->ctx, a);
}

static __attribute__((noinline)) void sl_disarm(bool link_up)
{
    s_ctx->disarm(s_ctx->ctx, link_up);
}

static __attribute__((noinline)) uint32_t sl_stop_count(void)
{
    return s_ctx->stop_count(s_ctx->ctx);
}

static __attribute__((noinline)) void sl_arm_surfaces(void)
{
    s_ctx->arm_surfaces(s_ctx->ctx);
}

/*
 * A write of any width goes out one frame per exchange, each acknowledged
 * before the next is sent (link_write_acked()).
 */
static bool write_regs(uint8_t page, uint8_t offset, uint8_t count,
                       const uint16_t *regs, link_msg_t *reply)
{
    return link_write_acked(s_ctx->host, s_ctx->port, page, offset, count,
                            regs, reply);
}

/*
 * The servo bench's output is whichever channels the operator bound as
 * surfaces, and this file names no pin.  The wiring is described once, on the
 * OUTPUTS screen, and the horn asks the binding which channels carry it; a
 * pin named here as well would be a second record of the same wiring, and the
 * two would disagree the first time the operator rebound one.
 *
 * The slots stay the operator's.  This screen writes channels and never the
 * OUTPUTS page, so no drag can take a pin away from what it was bound to, and
 * no channel the binding marked a motor is written by a servo horn.
 */
/*
 * How often the servo's position is said again, against the far end's
 * OUT_DEFAULT_TIMEOUT_MS of 500 ms.  Five times the margin, and one
 * register on the wire each time.
 */
#define SERVO_HOLD_MS   100u
#define SERVO_MIN_US    1000u
#define SERVO_MAX_US    2000u

/* A pulse in microseconds as a proportion of the servo's own travel, which is
 * what the CHANNELS page carries.  Clamped, not wrapped, below the range, as
 * the coprocessor's conversion is. */
static uint16_t us_to_span(uint16_t us, uint16_t min_us, uint16_t max_us)
{
    if (max_us <= min_us || us <= min_us) {
        return 0u;
    }
    const uint32_t span = (uint32_t)(us - min_us) * LINK_CH_SPAN
                          / (uint32_t)(max_us - min_us);
    return (span > LINK_CH_SPAN) ? (uint16_t)LINK_CH_SPAN : (uint16_t)span;
}

/* What the servo screen is holding, so it can be said again before the far
 * end times it out.  SERVO_CMD_NONE means nothing is being held. */
static servo_cmd_t s_servo_held;
static uint32_t    s_servo_next_ms;
/*
 * A position the far end still holds and this end has finished with.
 *
 * Kept as a debt rather than written and forgotten: a screen left while the
 * link is down cannot send the release, and the far end keeps the channel
 * command through a failsafe.  A later arm -- from either screen -- would then
 * drive the surface to where it was before.
 */
static bool        s_servo_release_owed;
static atomic_bool s_servo_release_request;
/* HOLD asked for: a sweep being written, which can take several exchanges,
 * gives way to it at once.  Cleared when the drain reaches a servo
 * command, the HOLD itself or one after it. */
static atomic_bool s_servo_hold_request;

/*
 * Which channels the horn drives: the ones the binding marks as surfaces.
 *
 * Written and read by the control task alone, which is the only task that
 * puts anything on the wire, and refreshed from the binding read back
 * (servo_link_binding()).
 *
 * Zero is a bench with nothing bound as a surface, and then the horn drives
 * nothing.  Falling back to a channel number instead is how this screen came
 * to command whatever was bound first, which on a bench with an ESC on the
 * lowest pin is the motor.
 */
static uint8_t s_servo_channels;
/* For the screen's SWEEP: a binding read, with a surface in it. */
static atomic_bool s_servo_surfaces;
/* A sweep command with no surface to sweep: the screen stops waiting. */
static atomic_bool s_sweep_refused;
/* The pause the panel last let go of: commands asked during it and queued
 * behind the HOLD that ended it are not sent (servo_cmd_stale()). */
static servo_pause_end_t s_pause_ended;

/*
 * Whether that mask is an answer at all.
 *
 * Empty and unknown are different benches.  Empty is a binding that was read
 * and names no surface -- a bench carrying only a motor, which must still
 * arm.  Unknown is a binding nobody has read, or one whose write did not come
 * back: the far end may be rendering surfaces this end cannot name, and
 * treating that as empty would let a release report success without settling
 * them and let the arm that follows drive them.
 *
 * False until a binding has been read from the far end.  Nothing this end
 * merely sent counts: a write whose acknowledgement was lost was applied over
 * there all the same.
 */
static bool s_servo_known;

/*
 * And whether the last position actually reached any of them.
 *
 * Separate from s_servo_channels because the two can differ: a write that
 * failed part way through leaves some surfaces holding a position and not
 * others.  It says a release is owed, not where the release goes -- that is
 * always the channels bound as surfaces now, because a channel that has
 * stopped being one must not be written by this screen.
 */
static uint8_t s_servo_written;

/*
 * For the screen's SWEEP and its horn: there is something to move.  A
 * programming port as target is one output; with the standard outputs it is
 * a binding read with a surface in it.
 */
void servo_link_surfaces_publish(void)
{
    atomic_store(&s_servo_surfaces,
                 prog_link_target(s_ctx->prog) != 0u
                 || (s_servo_known && s_servo_channels != 0u));
}

/*
 * One run's range and speed, to the standard outputs' CHAN_CFG or, with
 * programming port @p port (1 + n) as target, to that port's registers.  A
 * port is written as channel 0 of @p cfg.
 */
static bool servo_cfg_write(uint8_t port, uint8_t first, uint8_t count,
                            const uint16_t *cfg, link_msg_t *reply)
{
    if (port != 0u) {
        uint16_t regs[PROG_LINK_CFG_REGS];
        prog_link_cfg(cfg, regs);
        return write_regs(LINK_PAGE_PROG,
                          LINK_PG_REG(port - 1u, LINK_PG_SLEW),
                          PROG_LINK_CFG_REGS, regs, reply)
               && reply->op == LINK_OP_ACK;
    }
    return write_regs(LINK_PAGE_CHAN_CFG,
                      (uint8_t)(first * LINK_CC_STRIDE),
                      (uint8_t)(count * LINK_CC_STRIDE),
                      &cfg[(size_t)first * LINK_CC_STRIDE], reply)
           && reply->op == LINK_OP_ACK;
}

/* And its command, 0 to LINK_CH_SPAN: CHANNELS, or the port's CMD. */
static bool servo_cmd_write(uint8_t port, uint8_t first, uint8_t count,
                            const uint16_t *cmd, link_msg_t *reply)
{
    if (port != 0u) {
        return write_regs(LINK_PAGE_PROG,
                          LINK_PG_REG(port - 1u, LINK_PG_CMD), 1u, cmd, reply)
               && reply->op == LINK_OP_ACK;
    }
    return write_regs(LINK_PAGE_CHANNELS, first, count, &cmd[first],
                      reply)
           && reply->op == LINK_OP_ACK;
}

/* The channels of one run, as bits. */
static uint8_t servo_run_bits(uint8_t first, uint8_t count)
{
    uint8_t bits = 0u;
    for (uint8_t c = first; c < first + count; ++c) {
        bits |= (uint8_t)(1u << c);
    }
    return bits;
}

/*
 * The next run of set channels in @p mask at or after @p from.
 *
 * Runs rather than single channels: a binding's surfaces are contiguous
 * unless a motor sits between them, and each exchange on this wire can wait a
 * second.  Returns false when there is no run left.
 */
static bool servo_next_run(uint8_t mask, uint8_t from, uint8_t *first,
                           uint8_t *count)
{
    uint8_t i = from;
    while (i < LINK_OUT_CHANNELS && (mask & (uint8_t)(1u << i)) == 0u) {
        ++i;
    }
    if (i >= LINK_OUT_CHANNELS) {
        return false;
    }
    *first = i;
    while (i < LINK_OUT_CHANNELS && (mask & (uint8_t)(1u << i)) != 0u) {
        ++i;
    }
    *count = (uint8_t)(i - *first);
    return true;
}

/*
 * One servo command, as configuration and pulse.
 */
/* Whether the operator has asked, since this began, for the thing being
 * written to stop: a stop applied by the pump, or a disarm or release posted
 * while an exchange was on the wire. */
static bool servo_countermanded(void)
{
    return sl_stopped()
           || atomic_load(&s_servo_release_request)
           || atomic_load(&s_servo_hold_request);
}

/*
 * The range and speed the SERVO screen last named.  A release goes to the
 * midpoint of the range the far end holds, which is the one the last
 * position carried; a profile chosen since, with no position under it yet,
 * would be released at the old profile's centre.  So a release restates the
 * range first.  Kind NONE until a command names one.
 */
static servo_cmd_t s_servo_range;

/* Whether a command names a range the far end takes.  A command that names
 * none keeps the standard one. */
static bool servo_named(const servo_cmd_t *sv)
{
    return sv->max_us > sv->min_us
           && sv->min_us >= LINK_CC_FLOOR_US
           && sv->max_us <= LINK_CC_CEILING_US;
}

/* The CHAN_CFG page for the surfaces under @p sv's range and speed. */
static void servo_cfg(const servo_cmd_t *sv, uint16_t *cfg)
{
    const bool named = servo_named(sv);
    for (uint8_t i = 0; i < LINK_OUT_CHANNELS; ++i) {
        uint16_t *r = &cfg[(size_t)i * LINK_CC_STRIDE];
        r[LINK_CC_ROLE] = LINK_CC_ROLE_SURFACE;
        /* What the screen's SPEED means at this end: the rate the bench
         * is allowed to move the output, rather than a number that only
         * changed the drawing. */
        r[LINK_CC_SLEW]   = sv->slew_per_s;
        r[LINK_CC_MIN_US] = named ? sv->min_us : (uint16_t)SERVO_MIN_US;
        r[LINK_CC_MAX_US] = named ? sv->max_us : (uint16_t)SERVO_MAX_US;
    }
}

/*
 * The sweep on the SERVO page (protocol 4.2).  While one runs the
 * coprocessor commands the surfaces itself every pass, so a centre or a
 * position written under it would last one pass: anything but a sweep stops
 * it first.  Not known after the link comes up: then the next command, a
 * sweep included, writes a stop first, which also clears a finished sweep
 * that the same curve would otherwise not start again.
 */
static bool        s_servo_sweep_page;
static bool        s_servo_sweeping;
static bool        s_servo_holding;      /* LINK_SV_HOLD in force there */
static uint32_t    s_servo_hold_ms;      /* when the far end last took it */
static atomic_bool s_servo_hold_lost;    /* for the screen: it let go */
static atomic_uint s_servo_hold_lost_seq; /* of this pause */
static bool        s_servo_sweep_unknown;
static atomic_bool s_servo_sweep_able;   /* for the screen's SWEEP */
static uint16_t    s_servo_minor;        /* its protocol minor: 6 resumes */
/* The curve the far end last took, and when it started one: a curve that
 * differs from the one running starts over there, and the screen draws the
 * horn from that moment rather than from its tap. */
static uint16_t    s_servo_curve[4];
static uint32_t    s_servo_curve_ms;   /* when the far end last took it */
static atomic_uint s_sweep_start_ms;
static atomic_uint s_sweep_start_from;   /* servo_sweep_from_t */
static atomic_uint s_sweep_frozen_ms;    /* when it froze, for FROZEN */
static atomic_bool s_sweep_start_new;
static atomic_uint s_sweep_start_seq;    /* the command it acknowledges */
/* The far end's phase as acknowledgements time it, and the HOLD that
 * paused a running sweep: which pause, and the phase it kept. */
static servo_phase_t s_far_phase;
/* A HOLD write that went unanswered: it may have reached the far end, so
 * the phase it kept is not known here when a retry is acknowledged. */
static bool          s_hold_unanswered;
static atomic_uint s_sweep_held_seq;
static atomic_uint s_sweep_held_kept;
static atomic_bool s_sweep_held_new;

/* The held sweep's resume has been taken: its repeats say the curve, and
 * publish no further resumed start. */
static void servo_resume_taken(uint16_t start_seq)
{
    if (s_servo_held.kind == SERVO_CMD_SWEEP
        && s_servo_held.start_seq == start_seq) {
        s_servo_held.resume = false;
    }
}

static bool write_servo(const servo_cmd_t sv)
{
    link_msg_t reply;
    /*
     * What a command moves is the target the PROG page holds (4.13): the
     * standard outputs or a programming port.  Nothing is sent before the
     * page is read, because whose pulse it would be is not known.  A release
     * goes to the target in force, which is where the position was written;
     * every other command waits until that target is the one the screen
     * asks for (servo_link_prog_service()).  A coprocessor without the page
     * has the standard outputs.
     */
    if (!prog_link_command(s_ctx->prog, sv.kind == SERVO_CMD_RELEASE)) {
        return false;
    }
    const uint8_t port = prog_link_target(s_ctx->prog);
    if (s_servo_sweep_unknown
        || ((s_servo_sweeping || s_servo_holding)
            && sv.kind != SERVO_CMD_SWEEP && sv.kind != SERVO_CMD_HOLD)) {
        const uint16_t stop = 0u;
        if (!write_regs(LINK_PAGE_SERVO, LINK_SV_SWEEP, 1u, &stop,
                        &reply)
            || reply.op != LINK_OP_ACK) {
            return false;
        }
        s_servo_sweeping      = false;
        s_servo_holding       = false;
        s_servo_sweep_unknown = false;
        servo_phase_stopped(&s_far_phase);
    }
    if (sv.kind == SERVO_CMD_HOLD) {
        /*
         * The far end freezes every surface where its output is and keeps
         * it there while this is repeated.  A coprocessor that cannot sweep
         * has nothing to hold.
         */
        if (!s_servo_sweep_page) {
            return true;
        }
        /*
         * Unrepeated for as long as a channel command is trusted, the far
         * end ended the hold and the surfaces rested; a HOLD now would hold
         * wherever resting had got them.  So the hold is over: the surfaces
         * are released to their centre, a state both ends know, and the
         * screen is told.
         */
        const uint32_t since = sl_now_ms() - s_servo_hold_ms;
        if (s_servo_holding && since > OUT_DEFAULT_TIMEOUT_MS) {
            s_pause_ended.on        = true;
            s_pause_ended.pause_seq = servo_cmd_pause_root(&sv);
            s_servo_holding      = false;
            s_servo_held.kind    = SERVO_CMD_NONE;
            s_servo_release_owed = true;
            atomic_store(&s_servo_hold_lost_seq,
                         (unsigned)servo_cmd_pause_root(&sv));
            atomic_store(&s_servo_hold_lost, true);
            /* Not taken as held: a caller that voids an owed release on
             * success would cancel the one just owed. */
            return false;
        }
        const uint16_t hold = LINK_SV_HOLD;
        const uint32_t sent = sl_now_ms();
        if (!write_regs(LINK_PAGE_SERVO, LINK_SV_SWEEP, 1u, &hold,
                        &reply)) {
            s_hold_unanswered = s_hold_unanswered
                                || (s_servo_sweeping && !s_servo_holding);
            return false;
        }
        if (reply.op != LINK_OP_ACK) {
            return false;
        }
        const uint32_t took = sl_now_ms();
        /*
         * A pause whose HOLD was answered only at a retry may have frozen at
         * an earlier attempt, been let go 500 ms later and frozen again
         * where resting had got it; an exchange that outlasted a HOLD's life
         * may have been let go meanwhile too.  Where the output is held is
         * then not known here, and an angle drawn from the tap would be
         * said again to the servo by the next change of profile.  So the
         * hold is over: the surfaces are released to their centre, a state
         * both ends know, and the screen is told, as for a HOLD left
         * unrepeated.
         */
        if ((s_hold_unanswered && s_servo_sweeping && !s_servo_holding)
            || (uint32_t)(took - sent) > OUT_DEFAULT_TIMEOUT_MS) {
            /*
             * The HOLD just acknowledged is live there, and while it is the
             * far end stamps every surface with that HOLD's arrival: a
             * position written under it would go to rest 500 ms after the
             * arrival, at once and unslewed.  So it is ended first with a
             * stop; one that does not land is written before the next
             * command instead.
             */
            const uint16_t stop = 0u;
            if (!write_regs(LINK_PAGE_SERVO, LINK_SV_SWEEP, 1u,
                            &stop, &reply)
                || reply.op != LINK_OP_ACK) {
                s_servo_sweep_unknown = true;
            }
            s_pause_ended.on        = true;
            s_pause_ended.pause_seq = servo_cmd_pause_root(&sv);
            s_hold_unanswered    = false;
            s_servo_sweeping     = false;
            s_servo_holding      = false;
            servo_phase_stopped(&s_far_phase);
            s_servo_held.kind    = SERVO_CMD_NONE;
            s_servo_release_owed = true;
            atomic_store(&s_servo_hold_lost_seq,
                         (unsigned)servo_cmd_pause_root(&sv));
            atomic_store(&s_servo_hold_lost, true);
            /* Not taken as held: a caller that voids an owed release on
             * success would cancel the one just owed. */
            return false;
        }
        /* The hold that paused a running sweep: the far end keeps the
         * curve's phase as of now, and the screen draws the pause from it
         * rather than from the tap. */
        if (s_servo_sweeping && !s_servo_holding) {
            const uint32_t kept = servo_phase_held(&s_far_phase, took);
            atomic_store(&s_sweep_held_seq, (unsigned)sv.pause_seq);
            atomic_store(&s_sweep_held_kept, kept);
            atomic_store(&s_sweep_held_new, true);
        }
        if (!s_servo_sweeping && !s_servo_holding) {
            /* A hold of no sweep: nothing to resume. */
            servo_phase_stopped(&s_far_phase);
        }
        s_hold_unanswered = false;
        s_servo_sweeping = false;
        s_servo_holding  = true;
        /* From the send: the far end heard it no earlier than that. */
        s_servo_hold_ms  = sent;
        s_servo_written |= port != 0u ? (uint8_t)0x01u : s_servo_channels;
        return true;
    }
    /*
     * Every command, a release included, goes to the channels the binding
     * marks as surfaces now -- never to the ones this process wrote earlier.
     *
     * A channel that has stopped being a surface is not this screen's to
     * settle, and centring it would be the defect this file just stopped
     * committing from the other side: mid-travel is a surface's rest and half
     * power on a throttle, so a channel rebound as a motor between the drag
     * and the release would be commanded to half throttle.  What settles it
     * instead is the rebinding itself, which carries the new role and the
     * rest that goes with it, or the unbinding, after which no slot renders
     * the channel at all.
     *
     * Reaching what this process never wrote is deliberate: after a panel
     * restart the far end still holds whatever it was left at, and centring a
     * surface that is not holding anything costs nothing.
     */
    /*
     * An unknown binding is not an empty one.  Returning true here would pay
     * a release that settled nothing and let the bench arm onto surfaces the
     * far end is still rendering; returning false keeps the debt, and
     * ARMING_ACT_ARM refuses the arm and says so.  It is paid as soon as a
     * read succeeds, which the poll loop retries every second.
     */
    if (port == 0u && !s_servo_known) {
        return false;
    }
    /* A port is one output, kept as bit 0 of what was written. */
    const uint8_t mask = port != 0u ? (uint8_t)0x01u : s_servo_channels;
    /*
     * Nothing is bound as a surface, or nothing is holding a position, so
     * there is nothing to say.  True rather than false: a false here would
     * leave a release owed for ever, and ARMING_ACT_ARM refuses to arm while
     * one is -- a bench with only a motor on it would stop arming.
     */
    if (mask == 0u) {
        /* A sweep of nothing never starts, so no start is published for it:
         * the screen is told instead, or it would wait for one. */
        if (sv.kind == SERVO_CMD_SWEEP) {
            atomic_store(&s_sweep_refused, true);
            /* And not kept to be said again: a binding with a surface,
             * after a reconnect, would sweep it with no tap. */
            s_servo_held.kind = SERVO_CMD_NONE;
        }
        return true;
    }
    uint8_t first = 0u, count = 0u;
    if (sv.kind == SERVO_CMD_RELEASE) {
        /*
         * Stop holding the surfaces where the horn put them: each is
         * commanded to the centre it rests at.  The slots are the operator's,
         * written from the OUTPUTS screen, and are not touched -- a screen
         * that cleared one would take away the wiring the operator described,
         * and on a bench where a motor holds the lowest pin it would take
         * away the motor's.
         *
         * Whether the far end took it is returned rather than assumed: a
         * write that did not land leaves the surface where it was, and the
         * caller's record of owing the release is the only thing that would
         * notice.
         */
        uint16_t centre[LINK_OUT_CHANNELS];
        for (uint8_t i = 0; i < LINK_OUT_CHANNELS; ++i) {
            centre[i] = (uint16_t)(LINK_CH_SPAN / 2u);
        }
        /* The midpoint of the range the screen last named, which the
         * screen centres on its PULSE CENTRE. */
        const bool ranged = s_servo_range.kind != SERVO_CMD_NONE;
        uint16_t cfg[LINK_CC_COUNT];
        servo_cfg(&s_servo_range, cfg);
        for (uint8_t at = 0u; servo_next_run(mask, at, &first, &count);
             at = (uint8_t)(first + count)) {
            if (ranged
                && !servo_cfg_write(port, first, count, cfg, &reply)) {
                return false;
            }
            if (!servo_cmd_write(port, first, count, centre, &reply)) {
                return false;
            }
            /* Settled, so no longer holding anything.  Run by run, because a
             * later one can still fail and the debt is what is left. */
            s_servo_written &= (uint8_t)~servo_run_bits(first, count);
        }
        /*
         * And nothing is held anywhere else either.  A bit left over names a
         * channel that has stopped being a surface, which this screen must
         * not write and whose own rebinding has already given it a rest;
         * keeping it would owe a release that no write can ever pay.
         */
        s_servo_written = 0u;
        return true;
    } else {
        /*
         * Configuration and command, sent whole every time.  The coprocessor
         * may have reset since the last write, so the range the pulse is
         * clamped against is restated with each pulse.
         */
        /*
         * The endpoints the screen named, not this file's.  A narrow servo
         * runs 660 to 860 us and its centre is below a standard servo's
         * floor, so clamping it against 1000 to 2000 would send its whole
         * travel to one end.  A command that names no range keeps the
         * standard one.
         */
        const bool named = servo_named(&sv);
        const uint16_t min_us = named ? sv.min_us : (uint16_t)SERVO_MIN_US;
        const uint16_t max_us = named ? sv.max_us : (uint16_t)SERVO_MAX_US;
        const uint16_t span = us_to_span(sv.value_us, min_us, max_us);

        /*
         * Only the channels the binding marked surfaces are written, and the
         * role written to them is the one they already carry.  A write that
         * reached further would be this screen deciding what a channel is
         * for, and a motor channel told it is a surface rests at mid-travel,
         * which on a throttle is half power.
         */
        uint16_t cfg[LINK_CC_COUNT];
        uint16_t cmd[LINK_OUT_CHANNELS];
        servo_cfg(&sv, cfg);
        for (uint8_t i = 0; i < LINK_OUT_CHANNELS; ++i) {
            cmd[i] = span;
        }
        if (sv.kind == SERVO_CMD_SWEEP) {
            /*
             * The range first, as for a position, then the curve in one
             * frame.  A sweep that is starting first clears the movement
             * count, so it runs until it is stopped; one that is running is
             * only repeated, which keeps it going.
             */
            for (uint8_t at = 0u; servo_next_run(mask, at, &first, &count);
                 at = (uint8_t)(first + count)) {
                if (!servo_cfg_write(port, first, count, cfg, &reply)) {
                    return false;
                }
                if (servo_countermanded()) {
                    return false;
                }
            }
            const uint16_t curve[4] = { sv.sweep_kind, sv.sweep_mhz,
                                        sv.sweep_span, sv.sweep_dwell_ms };
            /*
             * A resume of the sweep held there carries it on from its phase
             * (4.6).  An older coprocessor, or one that refuses -- the hold
             * ended there, or the curve changed -- gets the curve whole and
             * starts it over, and the operator is told once it has.
             */
            /*
             * A HOLD that went unanswered and was never acknowledged may or
             * may not have reached the far end, so it is either running or
             * holding: the resume starts the curve over from a stop, and the
             * operator is told.
             */
            const bool held_sweep = (s_servo_holding
                                     && servo_phase_resumable(&s_far_phase))
                                    || s_hold_unanswered;
            const bool timed = !s_hold_unanswered;
            servo_resume_t plan = servo_page_resume_plan(
                sv.resume, held_sweep, timed, s_servo_minor, false);
            if (plan == SERVO_RESUME_WRITE) {
                const uint16_t resume = LINK_SV_RESUME;
                if (!write_regs(LINK_PAGE_SERVO, LINK_SV_SWEEP, 1u,
                                &resume, &reply)) {
                    /* It may have taken: the next command stops first. */
                    s_servo_sweep_unknown = true;
                    return false;
                }
                if (reply.op == LINK_OP_ACK) {
                    const uint32_t took = sl_now_ms();
                    memcpy(s_servo_curve, curve, sizeof(curve));
                    s_servo_curve_ms = took;
                    s_servo_sweeping = true;
                    s_servo_holding  = false;
                    /* Phase 0 moved on by the hold; the screen draws from
                     * it whatever phase it took the pause to have. */
                    uint32_t start = took;
                    (void)servo_phase_resumed(&s_far_phase, took, &start);
                    s_hold_unanswered = false;
                    /* And when it began to move again, for the drawing. */
                    atomic_store(&s_sweep_frozen_ms, took);
                    atomic_store(&s_sweep_start_ms, start);
                    atomic_store(&s_sweep_start_seq, (unsigned)sv.start_seq);
                    atomic_store(&s_sweep_start_from,
                                 (unsigned)SERVO_SWEEP_RESUMED);
                    atomic_store(&s_sweep_start_new, true);
                    servo_resume_taken(sv.start_seq);
                    s_servo_written |= mask;
                    return true;
                }
                plan = servo_page_resume_plan(sv.resume, held_sweep, timed,
                                              s_servo_minor, true);
            }
            if (plan == SERVO_RESUME_TOO_OLD || plan == SERVO_RESUME_REFUSED
                || plan == SERVO_RESUME_UNTIMED) {
                /*
                 * Over from the beginning whatever the far end is doing.  A
                 * RESUME whose acknowledgement was lost has it running from
                 * the kept phase, and a refusal of the retry would leave the
                 * curve written next carrying that on while the screen draws
                 * it from the start.  A stop first makes it a start there.
                 */
                const uint16_t stop = 0u;
                if (!write_regs(LINK_PAGE_SERVO, LINK_SV_SWEEP, 1u,
                                &stop, &reply)
                    || reply.op != LINK_OP_ACK) {
                    return false;
                }
                /* Stopped there: what follows is a start, from rest. */
                s_servo_sweeping = false;
                s_servo_holding  = false;
                servo_phase_stopped(&s_far_phase);
            }
            if (!s_servo_sweeping) {
                const uint16_t endless = 0u;
                if (!write_regs(LINK_PAGE_SERVO, LINK_SV_SWEEP_MOVES,
                                1u, &endless, &reply)
                    || reply.op != LINK_OP_ACK) {
                    return false;
                }
                /* And asked again before the write that starts it moving:
                 * the pump runs inside that wait as inside the others. */
                if (servo_countermanded()) {
                    return false;
                }
            }
            if (!write_regs(LINK_PAGE_SERVO, LINK_SV_SWEEP, 4u, curve,
                            &reply)) {
                /* It may have taken: the next command stops first. */
                s_servo_sweep_unknown = true;
                return false;
            }
            if (reply.op != LINK_OP_ACK) {
                return false;
            }
            if (plan == SERVO_RESUME_TOO_OLD) {
                sl_alert(SERVO_LINK_ALERT_RESUME_OLD);
            } else if (plan == SERVO_RESUME_REFUSED) {
                sl_alert(SERVO_LINK_ALERT_RESUME_REFUSED);
            } else if (plan == SERVO_RESUME_UNTIMED) {
                sl_alert(SERVO_LINK_ALERT_RESUME_UNTIMED);
            }
            /* A start there: no sweep running, a changed curve, or one the
             * far end stopped because nothing repeated it for as long as a
             * channel command is trusted, and starts again from zero. */
            const uint32_t took = sl_now_ms();
            const uint32_t gap = took - s_servo_curve_ms;
            servo_sweep_from_t from = SERVO_SWEEP_FROM_HERE;
            bool started = true;
            if (!s_servo_sweeping) {
                from = SERVO_SWEEP_FROM_REST;
            } else if (gap > OUT_DEFAULT_TIMEOUT_MS) {
                /* It froze when the last write went unrepeated for that
                 * long; this starts it again from there. */
                from = SERVO_SWEEP_FROM_FROZEN;
                atomic_store(&s_sweep_frozen_ms,
                             s_servo_curve_ms + OUT_DEFAULT_TIMEOUT_MS);
            } else if (memcmp(curve, s_servo_curve, sizeof(curve)) == 0) {
                started = false;   /* repeated: it carries on */
            }
            if (!started && sv.resume) {
                /*
                 * A resume whose HOLD never left: the far end ran on, and
                 * this write only repeats its curve.  The screen waits for a
                 * start to draw from, so it gets one, at the curve's own
                 * phase 0, from where the output is.
                 */
                atomic_store(&s_sweep_start_ms, s_far_phase.start_ms);
                atomic_store(&s_sweep_start_seq, (unsigned)sv.start_seq);
                atomic_store(&s_sweep_frozen_ms, took);
                atomic_store(&s_sweep_start_from,
                             (unsigned)SERVO_SWEEP_RESUMED);
                atomic_store(&s_sweep_start_new, true);
                s_hold_unanswered = false;
                servo_resume_taken(sv.start_seq);
            }
            if (started) {
                servo_phase_started(&s_far_phase, took);
                s_hold_unanswered = false;
                /* A resume that fell back to this start is used up: its
                 * repeats are the curve, not resumes. */
                servo_resume_taken(sv.start_seq);
                memcpy(s_servo_curve, curve, sizeof(curve));
                atomic_store(&s_sweep_start_ms, took);
                atomic_store(&s_sweep_start_seq, (unsigned)sv.start_seq);
                atomic_store(&s_sweep_start_from, (unsigned)from);
                atomic_store(&s_sweep_start_new, true);
            }
            s_servo_curve_ms = took;
            s_servo_sweeping = true;
            s_servo_holding  = false;
            /* Every surface is moving, so a release owes each a centre. */
            s_servo_written |= mask;
            return true;
        }
        /*
         * What the channel is, then what it is to do.  Each is its own
         * transaction and the far end steps its outputs between them, so a
         * range that had not arrived would clamp the pulse against the one
         * before it: a narrow servo selected against a standard configuration
         * renders 1500 us, past its 860 us maximum.  The 100 ms refresh is
         * the retry.
         *
         * Both waits can take a second and the pump runs inside them, so a
         * stop can be applied and a disarm posted between one and the next.
         * Giving up part way leaves the surfaces already written holding what
         * was asked for, and the next refresh or the release settles them;
         * no pin changes hands either way, because no slot is written here.
         */
        for (uint8_t at = 0u; servo_next_run(mask, at, &first, &count);
             at = (uint8_t)(first + count)) {
            if (!servo_cfg_write(port, first, count, cfg, &reply)) {
                return false;
            }
            if (servo_countermanded()) {
                return false;
            }
            if (!servo_cmd_write(port, first, count, cmd, &reply)) {
                return false;
            }
            /* Landed, so this run is holding a position and a release owes
             * it a centre.  Recorded before the next run is attempted: one
             * that fails must not lose what an earlier one did. */
            s_servo_written |= servo_run_bits(first, count);
            if (servo_countermanded()) {
                return false;
            }
        }
        return true;
    }
}

/*
 * The SERVO page: the frame rate of the PWM target -- every PWM output
 * rendering a surface for the standard target, the one programming port
 * for a port target (protocol 4.19).  A coprocessor before protocol 4.1 has
 * no such page and runs each slot at its binding's rate, 50 Hz for a servo.
 * From 4.19 a TARGET change sets the page back to 0; prog_link_rate() says
 * so and servo_link_rate_reset_note() records it here.
 *
 * The rate is written with every held position, not once: a coprocessor that
 * restarts between two polls goes back to each slot's own rate without the
 * link ever going down, and only a write that lands says what the pins run
 * at.  A refusal is the binding's answer, not the wire's -- a surface sharing
 * a PWM slice with an output at another rate -- so the same rate is not
 * asked again until it changes, the binding is rewritten or the link comes
 * back.
 */
#define SERVO_BIND_HZ    50u      /* the binding's SERVO PWM rate         */
static bool     s_servo_rate_page;
static uint16_t s_servo_hz_sent = SERVO_HZ_UNKNOWN;  /* acknowledged; 0 own */
static uint16_t s_servo_hz_refused;  /* the last rate it refused; 0 none     */
static atomic_uint s_servo_rate_shown;  /* (servo_rate_state_t << 16) | Hz */

static void servo_rate_show(servo_rate_state_t st, uint16_t hz)
{
    atomic_store(&s_servo_rate_shown, ((unsigned)st << 16) | hz);
}

/* What became of a rate written: the pins run at it, the binding will not
 * have it -- final until something changes -- or nothing answered. */
typedef enum { RATE_LANDED, RATE_REFUSED, RATE_NO_ANSWER } rate_result_t;

static rate_result_t write_servo_rate(uint16_t hz)
{
    if (hz == 0u) {
        return RATE_LANDED;   /* a command that names no rate */
    }
    if (!s_servo_rate_page) {
        /* Every PWM output at its binding's rate, which no profile is
         * faster than. */
        servo_rate_show(SERVO_RATE_UNSUPPORTED, hz);
        return RATE_LANDED;
    }
    if (hz == s_servo_hz_refused) {
        return RATE_REFUSED;
    }
    link_msg_t reply;
    if (!write_regs(LINK_PAGE_SERVO, LINK_SV_FRAME_HZ, 1u, &hz,
                    &reply)) {
        return RATE_NO_ANSWER;
    }
    if (reply.op == LINK_OP_ACK) {
        s_servo_hz_sent    = hz;
        s_servo_hz_refused = 0u;
        servo_rate_show(servo_rate_in_force(s_servo_minor), hz);
        return RATE_LANDED;
    }
    if (reply.op == LINK_OP_NACK) {
        s_servo_hz_refused = hz;
        servo_rate_show(SERVO_RATE_REFUSED, hz);
        return RATE_REFUSED;
    }
    return RATE_NO_ANSWER;
}

/* The rate the PWM target runs at as far as this end knows, or
 * SERVO_HZ_UNKNOWN. */
static uint16_t servo_rate_now(void)
{
    if (s_servo_hz_sent == SERVO_HZ_UNKNOWN) {
        return SERVO_HZ_UNKNOWN;
    }
    return (s_servo_hz_sent != 0u) ? s_servo_hz_sent : (uint16_t)SERVO_BIND_HZ;
}

/*
 * One position with its rate.  The pins must never carry a fast rate with
 * the wider pulses of a slower profile: a 760 us tail servo going back to
 * STANDARD PWM takes 50 Hz first and then 1000 to 2000 us, never 1500 us at
 * 560 Hz.  So a rate going up follows the pulse widths, and only once every
 * one of them landed; any other rate leads them, and a position waits for
 * it unless the pins are known to run no faster than it.  A rate not known
 * counts as fast.  A position held back is asked again with the next hold.
 *
 * Done only when the rate is settled too: a rate nobody answered leaves a
 * release owed, so it is written again.  A refused one is settled -- the
 * screen says REFUSED and the pins keep the slower rate -- because a release
 * owed for ever would stop the bench arming.
 */
static bool write_servo_and_rate(const servo_cmd_t sv)
{
    const uint16_t now_hz = servo_rate_now();
    if (now_hz != SERVO_HZ_UNKNOWN && sv.frame_hz > now_hz) {
        if (!write_servo(sv)) {
            return false;
        }
        return write_servo_rate(sv.frame_hz) != RATE_NO_ANSWER;
    }
    if (write_servo_rate(sv.frame_hz) != RATE_LANDED
        && (now_hz == SERVO_HZ_UNKNOWN || now_hz > sv.frame_hz)) {
        return false;
    }
    return write_servo(sv);
}

/*
 * Each slot back at its own rate.  Before a binding: the rate the SERVO page
 * holds was checked against the binding being replaced, and left in place a
 * motor bound beside a 560 Hz surface would be refused by the silicon and
 * stay still.  At link-up: a panel that restarted, or a link that only went
 * quiet, can leave the far end holding a heli rate the screen no longer
 * shows.  True when it landed, or there is no page to reset; otherwise the
 * rate in force is not known.
 *
 * servo_link_rate_reset_note() is what a reset leaves behind at this end,
 * for the reset a binding's own sequence sends (bind_link_write()) as well.
 */
void servo_link_rate_reset_note(bool page, bool landed)
{
    s_servo_hz_refused = 0u;
    if (!page) {
        s_servo_hz_sent = 0u;
        servo_rate_show(SERVO_RATE_UNSUPPORTED, 0u);
        return;
    }
    s_servo_hz_sent = landed ? 0u : (uint16_t)SERVO_HZ_UNKNOWN;
    servo_rate_show(SERVO_RATE_UNSENT, 0u);
}

static bool servo_rate_reset(void)
{
    if (!s_servo_rate_page) {
        servo_link_rate_reset_note(false, true);
        return true;
    }
    const uint16_t own = 0u;
    link_msg_t reply;
    const bool landed = write_regs(LINK_PAGE_SERVO, LINK_SV_FRAME_HZ,
                                   1u, &own, &reply)
                        && reply.op == LINK_OP_ACK;
    servo_link_rate_reset_note(true, landed);
    return landed;
}

/*
 * Whether the rate the surfaces run at is known, after one more try at
 * putting each slot back at its own rate if it is not.  Asked before every
 * arm: a panel that restarted while the far end held a heli rate, and whose
 * reset at link-up went unanswered, would otherwise arm the surfaces at it
 * from any screen, while the SERVO screen shows STANDARD PWM.
 */
bool servo_link_rate_settled(void)
{
    if (s_servo_hz_sent == SERVO_HZ_UNKNOWN) {
        (void)servo_rate_reset();
    }
    return s_servo_hz_sent != SERVO_HZ_UNKNOWN;
}

/*
 * One servo command.
 *
 * Arming and disarming go to the same policy the motor screen's do -- the
 * bench has one armed state and one set of rules for reaching it, whichever
 * screen is up.  A disarm is acted on with or without a link, because the
 * part of it that matters most is at this end.
 */
/*
 * A servo command, onto the link: a position goes with its frame rate, on
 * the SERVO page (see write_servo_and_rate()).
 */
void servo_link_apply(const servo_cmd_t sv, bool link_up, uint32_t stops)
{
    /* The drain has reached a command at least as new as any HOLD. */
    atomic_store(&s_servo_hold_request, false);
    /* Every command from the screen carries its range, a release included,
     * and the release the arm owes is paid with it. */
    if (servo_named(&sv)) {
        s_servo_range = sv;
    }
    if (sv.kind == SERVO_CMD_ARM) {
        /*
         * The surfaces are centred first, whether or not this process cached
         * a position.  After a panel restart the far end can still hold the
         * command from before it, and arming would render that: the same
         * reason DISARM and RELEASE ask unconditionally.  The arm itself
         * waits for it -- see ARMING_ACT_ARM.
         */
        s_servo_held.kind = SERVO_CMD_NONE;
        s_servo_release_owed = true;
        servo_link_service(link_up);
        /*
         * And the gesture is asked about again, because that call can wait a
         * second on the wire and the pump runs inside it: a stop, or touch
         * dying and recovering, can happen between the drain's check and
         * this line, and the arm would then be one the bench has already
         * invalidated.
         */
        if (stops != sl_stop_count()) {
            return;
        }
        /* The surfaces and the programming ports, and no motor. */
        sl_arm_surfaces();
        return;
    }
    if (sv.kind == SERVO_CMD_DISARM) {
        /*
         * Asked for by this screen, so the surfaces are centred whether or not
         * this process cached a position for them -- the far end keeps its
         * channel commands across a panel restart, and the screen promises to
         * let go of the output.
         */
        s_servo_held.kind = SERVO_CMD_NONE;
        s_servo_release_owed = true;
        sl_disarm(link_up);
        servo_link_service(link_up);
        return;
    }
    if (sv.kind == SERVO_CMD_RELEASE) {
        /*
         * Asked for, so the surfaces are centred whether or not this process
         * remembers commanding them: after a panel restart the far end holds
         * a position this end never wrote, and the button says it releases
         * the output.
         */
        s_servo_held.kind = SERVO_CMD_NONE;
        s_servo_release_owed = true;
        servo_link_service(link_up);
        return;
    }
    /*
     * A sweep or a hold reaching here with the link down was ended with the
     * link on the screen: kept, it would be said when the link comes back
     * and start motion nobody asked for.
     */
    if (!link_up && !servo_cmd_survives_link_loss(&sv)) {
        /*
         * It superseded the drive held before it on the screen, which ends
         * it with the link and draws the surfaces at rest: that one is not
         * said again either, and a position written out there is owed its
         * release, as servo_link_let_go() owes it.
         */
        if (s_servo_held.kind != SERVO_CMD_NONE || s_servo_written != 0u) {
            s_servo_release_owed = true;
        }
        s_servo_held.kind = SERVO_CMD_NONE;
        return;
    }
    /*
     * A command asked during a pause the panel has let go of -- queued
     * behind the HOLD whose late acknowledgement ended it: a resume, or a
     * position said again under a changed profile -- is stale.  The screen
     * shows the surfaces released to rest; sent, the resume would start
     * motion it does not show and the position would void the release and
     * hold an angle drawn from the abandoned pause.
     */
    if (servo_cmd_stale(&s_pause_ended, &sv)) {
        return;
    }
    s_servo_held = sv;
    s_servo_next_ms = sl_now_ms() + SERVO_HOLD_MS;
    /*
     * And a sweep goes only once an owed release is paid: the surfaces at
     * their centre, a start both ends know, not wherever a stop froze them.
     * One not yet paid keeps the sweep held, and the refresh pays it first.
     */
    if (sv.kind == SERVO_CMD_SWEEP && s_servo_release_owed) {
        servo_link_service(link_up);
        if (s_servo_release_owed) {
            return;
        }
    }
    if (link_up && write_servo_and_rate(sv)) {
        /*
         * The surfaces are holding this position now, so an older release
         * still owed for them is void.  Paying it afterwards would centre
         * what was just asked for and leave the output at rest until the next
         * refresh.  A write that failed leaves the debt where it was.
         */
        s_servo_release_owed = false;
    }
}

/*
 * Stop holding the servo, the way throttle_to_zero() stops holding the
 * throttle, and for the same reason: a disarm that leaves a position behind
 * is an arm that steps straight back to it.
 *
 * The far end keeps the channel command through a disarm and a failsafe, and
 * outputs_arm() stamps every channel's clock, so on the next arm the surface
 * is neither overdue nor at rest -- it is at its old position, with nobody
 * having touched anything.  So the position has to be returned to centre, and
 * until it can be the release is owed.
 *
 * Owed for what was written rather than for what the screen is holding: a
 * position that landed and then stopped being held is still out there, and it
 * is the far end's copy that arms the bench into it.
 */
void servo_link_let_go(void)
{
    if (s_servo_held.kind != SERVO_CMD_NONE || s_servo_written != 0u) {
        s_servo_release_owed = true;
    }
    s_servo_held.kind = SERVO_CMD_NONE;
}

/*
 * Say the servo's position again before the far end stops believing it, and
 * pay off a release that is owed.
 *
 * A channel nobody has commanded for OUT_DEFAULT_TIMEOUT_MS (500 ms) goes to
 * its rest, which for a surface is mid-travel: a servo held at an endpoint
 * would swing back to centre half a second after the finger stopped, with
 * the screen still showing where it was put.  The throttle is kept alive by
 * the control page, which is written every poll; nothing writes this channel
 * between touches.
 *
 * One register, and only while there is something to say.
 */
void servo_link_service(bool link_up)
{
    if (!link_up) {
        return;   /* nothing can be said, and the debt keeps */
    }
    /*
     * A rate not known is settled before anything that depends on it: while
     * a release is owed, whose rate a binding may refuse until the slots are
     * back at their own, and while the bank is armed, which poll_bench()
     * does not pass on to the far end until the rate is known.  Each slot
     * back at its own rate; a screen holding a position says its own again
     * with the next hold.
     */
    if (s_servo_hz_sent == SERVO_HZ_UNKNOWN
        && (s_servo_release_owed || sl_armed())) {
        (void)servo_rate_reset();
    }
    if (s_servo_release_owed) {
        /* At the screen's rate, in the same order as a position: a release
         * restates the range, and a profile chosen since the last position
         * may be slower than the rate the pins still run at. */
        servo_cmd_t release = { .kind = SERVO_CMD_RELEASE };
        release.frame_hz = s_servo_range.frame_hz;
        /* Only a write the far end acknowledged pays it off.  link_up is a
         * snapshot and the link can go during the transaction; forgetting an
         * unacknowledged clear would leave the slot bound with nothing left
         * to remember it. */
        s_servo_release_owed = !write_servo_and_rate(release);
        return;
    }
    if (s_servo_held.kind == SERVO_CMD_NONE) {
        return;
    }
    const uint32_t now = sl_now_ms();
    if ((int32_t)(now - s_servo_next_ms) < 0) {
        return;
    }
    s_servo_next_ms = now + SERVO_HOLD_MS;
    (void)write_servo_and_rate(s_servo_held);
}

void servo_link_init(const servo_link_ctx_t *ctx)
{
    s_ctx = ctx;
    memset(&s_servo_held, 0, sizeof(s_servo_held));
    s_servo_next_ms      = 0u;
    s_servo_release_owed = false;
    atomic_store(&s_servo_release_request, false);
    atomic_store(&s_servo_hold_request, false);
    s_servo_channels = 0u;
    atomic_store(&s_servo_surfaces, false);
    atomic_store(&s_sweep_refused, false);
    memset(&s_pause_ended, 0, sizeof(s_pause_ended));
    s_servo_known   = false;
    s_servo_written = 0u;
    memset(&s_servo_range, 0, sizeof(s_servo_range));
    s_servo_sweep_page = false;
    s_servo_sweeping   = false;
    s_servo_holding    = false;
    s_servo_hold_ms    = 0u;
    atomic_store(&s_servo_hold_lost, false);
    atomic_store(&s_servo_hold_lost_seq, 0u);
    s_servo_sweep_unknown = false;
    atomic_store(&s_servo_sweep_able, false);
    s_servo_minor = 0u;
    memset(s_servo_curve, 0, sizeof(s_servo_curve));
    s_servo_curve_ms = 0u;
    atomic_store(&s_sweep_start_ms, 0u);
    atomic_store(&s_sweep_start_from, 0u);
    atomic_store(&s_sweep_frozen_ms, 0u);
    atomic_store(&s_sweep_start_new, false);
    atomic_store(&s_sweep_start_seq, 0u);
    memset(&s_far_phase, 0, sizeof(s_far_phase));
    s_hold_unanswered = false;
    atomic_store(&s_sweep_held_seq, 0u);
    atomic_store(&s_sweep_held_kept, 0u);
    atomic_store(&s_sweep_held_new, false);
    s_servo_rate_page  = false;
    s_servo_hz_sent    = SERVO_HZ_UNKNOWN;
    s_servo_hz_refused = 0u;
    atomic_store(&s_servo_rate_shown, 0u);
}

void servo_link_came_up(uint16_t minor)
{
    /*
     * Whether it takes a frame rate, and each slot back at its own.  A
     * coprocessor that only went quiet, or a panel that restarted, can leave
     * the far end holding a heli rate the screen no longer shows; a servo
     * plugged in since would meet it on the next arm.  The screen sends its
     * rate again with its next position.
     */
    s_servo_rate_page = minor >= LINK_MINOR_SERVO_RATE;
    /* 4.6 carries a held sweep on from its phase (LINK_SV_RESUME), and
     * 4.19 sends the rate to the PWM target alone (servo_rate_in_force()). */
    s_servo_minor         = minor;
    (void)servo_rate_reset();
    s_servo_sweep_page    = minor >= 2u;
    s_servo_sweeping      = false;
    s_servo_holding       = false;
    s_hold_unanswered     = false;
    servo_phase_stopped(&s_far_phase);
    s_servo_sweep_unknown = s_servo_sweep_page;
    atomic_store(&s_servo_sweep_able, s_servo_sweep_page);
}

void servo_link_lost(void)
{
    /* A sweep or a hold ends with the link, here as on the screen
     * and at the far end: kept, it would be said again when the
     * link comes back and start motion nobody asked for. */
    if (!servo_cmd_survives_link_loss(&s_servo_held)) {
        s_servo_held.kind = SERVO_CMD_NONE;
    }
}

void servo_link_binding(uint8_t channels, bool known)
{
    s_servo_channels = channels;
    s_servo_known    = known;
    servo_link_surfaces_publish();
}

void servo_link_forget_binding(void)
{
    s_servo_known = false;
}

bool servo_link_binding_known(void)
{
    return s_servo_known;
}

void servo_link_prog_service(void)
{
    const bool rest = !sl_armed() && !s_servo_sweeping
                      && !s_servo_holding && !s_servo_sweep_unknown;
    if (rest && prog_link_pending(s_ctx->prog)) {
        servo_link_let_go();
    }
    const bool quiet = rest && !s_servo_release_owed && s_servo_written == 0u
                       && s_servo_held.kind == SERVO_CMD_NONE;
    prog_link_write_t w;
    if (prog_link_next(s_ctx->prog, quiet, &w)) {
        link_msg_t reply = { 0 };
        /* A pulse comes on under the range the screen names, stated to the
         * port first: from 4.16 a range longer than the frame refuses it,
         * and the port may still hold another profile's. */
        const uint8_t on = prog_link_pulse_on(s_ctx->prog, &w);
        bool ack = true;
        if (on != 0u && servo_named(&s_servo_range)) {
            uint16_t cfg[LINK_CC_COUNT];
            servo_cfg(&s_servo_range, cfg);
            ack = servo_cfg_write(on, 0u, 1u, cfg, &reply);
        }
        ack = ack
              && write_regs(LINK_PAGE_PROG, w.off, 1u, &w.value, &reply)
              && reply.op == LINK_OP_ACK;
        prog_link_written(s_ctx->prog, &w, ack);
    }
    /* From 4.19 a TARGET change returns the SERVO rate to 0: the next
     * position that wants a faster one leads with it again, and one whose
     * TARGET write went unanswered counts as fast until a reset lands. */
    switch (prog_link_rate(s_ctx->prog)) {
    case PROG_LINK_RATE_RESET:
        servo_link_rate_reset_note(s_servo_rate_page, true);
        break;
    case PROG_LINK_RATE_UNKNOWN:
        servo_link_rate_reset_note(s_servo_rate_page, false);
        break;
    case PROG_LINK_RATE_KEPT:
    default:
        break;
    }
}

bool servo_link_release_owed(void)
{
    return s_servo_release_owed;
}

uint16_t servo_link_hz_sent(void)
{
    return s_servo_hz_sent;
}

bool servo_link_take_release(void)
{
    if (!atomic_exchange(&s_servo_release_request, false)) {
        return false;
    }
    s_servo_held.kind = SERVO_CMD_NONE;
    s_servo_release_owed = true;
    return true;
}

void servo_link_ask_release(void)
{
    atomic_store(&s_servo_release_request, true);
}

void servo_link_ask_hold(void)
{
    atomic_store(&s_servo_hold_request, true);
}

unsigned servo_link_rate_shown(void)
{
    return atomic_load(&s_servo_rate_shown);
}

bool servo_link_sweep_able(void)
{
    return atomic_load(&s_servo_sweep_able);
}

bool servo_link_surfaces(void)
{
    return atomic_load(&s_servo_surfaces);
}

bool servo_link_take_sweep_refused(void)
{
    return atomic_exchange(&s_sweep_refused, false);
}

bool servo_link_take_hold_lost(uint16_t *pause_seq)
{
    if (!atomic_exchange(&s_servo_hold_lost, false)) {
        return false;
    }
    *pause_seq = (uint16_t)atomic_load(&s_servo_hold_lost_seq);
    return true;
}

bool servo_link_take_sweep_held(uint16_t *pause_seq, uint32_t *kept_ms)
{
    if (!atomic_exchange(&s_sweep_held_new, false)) {
        return false;
    }
    *pause_seq = (uint16_t)atomic_load(&s_sweep_held_seq);
    *kept_ms   = atomic_load(&s_sweep_held_kept);
    return true;
}

bool servo_link_take_sweep_start(void)
{
    return atomic_exchange(&s_sweep_start_new, false);
}

servo_link_start_t servo_link_sweep_start(void)
{
    servo_link_start_t st;
    st.start_seq = (uint16_t)atomic_load(&s_sweep_start_seq);
    st.start_ms  = atomic_load(&s_sweep_start_ms);
    st.from      = (servo_sweep_from_t)atomic_load(&s_sweep_start_from);
    st.frozen_ms = atomic_load(&s_sweep_frozen_ms);
    return st;
}
