/*
 * The coprocessor's sensor service.  See sense_svc.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_svc.h"

#include <string.h>

void sense_svc_init(sense_svc_t *v, const sense_svc_io_t *io)
{
    memset(v, 0, sizeof(*v));
    v->io = *io;
}

static uint64_t now_us(const sense_svc_t *v)
{
    return v->io.sched.now_us(v->io.sched.ctx);
}

static bool any_part(const sense_sched_cfg_t *c)
{
    return c->ina228_en || c->ina3221_en;
}

/* The set-up: the bus closed, and opened again with a fresh schedule on
 * the command's pins when a part is enabled. */
static void set_up(sense_svc_t *v, const sense_cmd_t *cmd)
{
    if (v->open) {
        v->io.close(v->io.sched.ctx);
        v->open = false;
        /* The count of captures runs on across set-ups. */
        v->seq_base = (uint16_t)(v->seq_base + v->sched.cap.seq);
    }
    v->cfg_gen  = cmd->cfg_gen;
    v->present  = 0u;
    v->scan_due = false;
    /* Whatever happens to the bus, the set-up ends the capture and the run
     * before it: the ones in force are taken as they stand and act from
     * their next move, and a refused arm's LOST does not outlive the
     * set-up the page has just cleared to idle. */
    v->run_gen    = cmd->run_gen;
    v->cap_gen    = cmd->cap_gen;
    v->edge_given = true;
    v->refused    = false;
    if (!any_part(&cmd->parts)
        || !v->io.open(v->io.sched.ctx, cmd->sda, cmd->scl)) {
        return;
    }
    v->open = true;
    v->sda  = cmd->sda;
    v->scl  = cmd->scl;
    sense_sched_init(&v->sched, &v->io.sched, &cmd->parts);
    v->scan_due = true;
}

static void capture(sense_svc_t *v, const sense_cmd_t *cmd)
{
    if (cmd->cap_gen != v->cap_gen) {
        v->cap_gen    = cmd->cap_gen;
        v->edge_given = false;
        v->refused    = false;
        if (!cmd->cap_on) {
            sense_sched_cap_disarm(&v->sched);
        } else if (!sense_sched_cap_arm(&v->sched, &cmd->cap)) {
            /* Ended before it began: the INA3221 is not reading CH1. */
            sense_sched_cap_disarm(&v->sched);
            v->refused = true;
            ++v->refusals;
        }
    }
    if (cmd->edge_set && !v->edge_given) {
        v->edge_given = true;
        sense_sched_cap_edge(&v->sched, cmd->edge_us);
    }
}

/* An enabled part that is not online, whose address a scan may find. */
static bool part_missing(const sense_sched_t *s)
{
    return (s->cfg.ina228_en && ina228_state(&s->i228) != SENSE_PART_ONLINE)
           || (s->cfg.ina3221_en
               && ina3221_state(&s->i3221) != SENSE_PART_ONLINE);
}

static bool capture_under_way(const sense_sched_t *s)
{
    const sense_cap_state_t st = s->cap.state;
    return st == SENSE_CAP_ARMED || st == SENSE_CAP_WAITING
           || st == SENSE_CAP_MOVING;
}

/* The bit a part's address has in PRESENT while it is online, else 0. */
static uint16_t online_bit(sense_state_t st, uint8_t addr)
{
    if (st != SENSE_PART_ONLINE || addr < SENSE_SCAN_FIRST
        || addr >= SENSE_SCAN_FIRST + SENSE_SCAN_COUNT) {
        return 0u;
    }
    return (uint16_t)(1u << (addr - SENSE_SCAN_FIRST));
}

static uint16_t online_mask(const sense_sched_t *s)
{
    return (uint16_t)(online_bit(ina228_state(&s->i228), s->i228.part.addr)
                      | online_bit(ina3221_state(&s->i3221),
                                   s->i3221.part.addr));
}

static void scan(sense_svc_t *v, uint64_t now)
{
    sense_sched_t *s = &v->sched;
    if (!v->scan_due && !(part_missing(s) && (int64_t)(now - v->scan_at_us) >= 0)) {
        return;
    }
    if (s->bus.stuck || capture_under_way(s)) {
        return;
    }
    v->scan_due   = false;
    v->scan_at_us = now + (uint64_t)SENSE_RETRY_MS * 1000u;
    const uint16_t online = online_mask(s);
    uint16_t found    = 0u;
    uint16_t answered = 0u;      /* asked and answered, yes or no */
    for (unsigned k = 0; k < SENSE_SCAN_COUNT && !s->bus.stuck; ++k) {
        const uint16_t bit = (uint16_t)(1u << k);
        if ((online & bit) != 0u) {
            continue;
        }
        const sense_err_t e = v->io.ask(v->io.sched.ctx,
                                        (uint8_t)(SENSE_SCAN_FIRST + k));
        /* The bus hears of every answer: a held line or timeouts make it
         * stuck, and its recovery is due at the next tick. */
        sense_bus_note(&s->bus, e);
        if (e == SENSE_OK || e == SENSE_NACK) {
            answered |= bit;
            if (e == SENSE_OK) {
                found |= bit;
            }
        }
    }
    /* An address without an answer keeps its last one. */
    v->present = (uint16_t)((found & answered)
                            | (v->present & (uint16_t)~answered) | online);
}

/* The capture's fields: the schedule's, or lost for an arm refused. */
static void snap_capture(const sense_svc_t *v, sense_snap_t *out)
{
    const sense_cap_t *c = &v->sched.cap;
    out->cap_seq = (uint16_t)(v->seq_base + (v->open ? c->seq : 0u)
                              + v->refusals);
    if (v->refused) {
        out->cap_state = SENSE_CAP_LOST;
        return;
    }
    if (!v->open) {
        return;
    }
    out->cap_state    = c->state;
    out->cap_move_t   = c->move_t;
    out->cap_arrive_t = c->arrive_t;
    out->cap_peak_ua  = c->peak_ua;
    out->cap_mean_ua  = c->mean_ua;
    out->cap_samples  = c->samples;
    out->cap_clipped  = c->clipped;
}

static void snap(const sense_svc_t *v, sense_snap_t *out)
{
    memset(out, 0, sizeof(*out));
    out->cfg_gen = v->cfg_gen;
    out->run_gen = v->run_gen;
    out->cap_gen = v->cap_gen;
    out->open    = v->open;
    snap_capture(v, out);
    if (!v->open) {
        return;
    }
    const sense_sched_t *s = &v->sched;
    out->held        = ((uint64_t)1u << v->sda) | ((uint64_t)1u << v->scl);
    out->stuck       = s->bus.stuck;
    out->errors      = s->bus.errors;
    out->present     = (uint16_t)(v->present | online_mask(s));
    out->i228        = ina228_state(&s->i228);
    out->i3221       = ina3221_state(&s->i3221);
    out->i228_maker  = s->i228.part.id_maker;
    out->i228_device = s->i228.part.id_device;
    out->i3221_maker = s->i3221.part.id_maker;
    out->i3221_die   = s->i3221.part.id_device;
    out->have_temp   = s->have_temp;
    out->temp_mdegc  = s->temp_mdegc;
    out->have_diag   = s->have_diag;
    out->diag        = s->diag;
    out->have_win    = s->have_last;
    memcpy(out->win, s->last, sizeof(out->win));
    out->run         = s->run;
}

void sense_svc_step(sense_svc_t *v, const sense_cmd_t *cmd, sense_snap_t *out)
{
    if (!v->started || cmd->cfg_gen != v->cfg_gen) {
        v->started = true;
        set_up(v, cmd);
    }
    if (v->open) {
        if (cmd->run_gen != v->run_gen) {
            v->run_gen = cmd->run_gen;
            sense_sched_arm(&v->sched);
        }
        capture(v, cmd);
        sense_sched_tick(&v->sched);
        scan(v, now_us(v));
    } else {
        /* No bus: a run has nothing to restart, and an arm nothing to
         * read, so it ends at once as lost. */
        v->run_gen = cmd->run_gen;
        if (cmd->cap_gen != v->cap_gen) {
            v->cap_gen = cmd->cap_gen;
            v->refused = cmd->cap_on;
            if (cmd->cap_on) {
                ++v->refusals;
            }
        }
    }
    snap(v, out);
}
