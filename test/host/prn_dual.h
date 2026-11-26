/**
 * @file prn_dual.h
 * @brief One rig for two print jobs: protocols/supvan's sv_job and the
 *        engine's prn_job with the T50M Pro file, each against its own
 *        simulated printer (sv_sim.h), driven the same way.  Every write is
 *        logged with its time, bytes and kind, and every state the
 *        scenario looks at with the job's step, end, relink and counter
 *        flags, so two runs of one scenario compare as two logs.
 *
 * The rig's step is sv_sim.h's rig_step(): the job's tick, the answer the
 * link owes, the job's write, the printer's notifications, one ms.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PRN_DUAL_H
#define PRN_DUAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "prn_job.h"
#include "sv_sim.h"

/** @brief Log entries a run keeps. */
#define DUAL_LOG_MAX 4096u

/** @brief A log entry: a write or a state. */
typedef struct {
    uint32_t t;      /**< the clock */
    uint16_t n;      /**< a write's bytes; 0 for a state */
    uint8_t kind;    /**< a write's prn_write_t; a state's step code */
    uint8_t err;     /**< a state's end */
    uint8_t flags;   /**< a state's ended, relinked, counted bits */
    uint8_t b[128];  /**< a write's bytes */
} dual_ev_t;

/** @brief The rig. */
typedef struct {
    bool prn;                   /**< the engine, else sv_job */
    const prn_spec_t *spec;     /**< the T50M Pro file */
    sv_job_t sv;                /**< sv_job */
    prn_job_t pj;               /**< the engine's job */
    uint8_t stream[RIG_STREAM]; /**< the stream a PRINT sends */
    sv_stream_t z;              /**< its description */
    sv_sim_t sim;               /**< the printer */
    uint32_t now;               /**< the clock, ms */
    bool link;                  /**< the link's state */
    bool owed;                  /**< a write's answer is owed to the job */
    bool owed_ok;               /**< that answer */
    uint32_t owed_at;           /**< when the link gives it */
    uint32_t ack_ms;            /**< a write's answer comes 1 + this ms on */
    unsigned nlog;              /**< log entries */
    dual_ev_t log[DUAL_LOG_MAX]; /**< the log */
} dual_t;

/**
 * @brief The engine's state as an sv_step_t, for the log and run_to.
 * @param j the job
 * @return the step sv_job would be in
 */
static inline int dual_prn_step(const prn_job_t *j)
{
    static const uint8_t k_pos[12] = {
        SV_STEP_CHECK, SV_STEP_MATERIAL, SV_STEP_READY,   SV_STEP_READY,
        SV_STEP_START, SV_STEP_PRINTING, SV_STEP_BUFFER,  SV_STEP_BULK,
        SV_STEP_DATA,  SV_STEP_PAUSE,    SV_STEP_FULL,    SV_STEP_DONE,
    };
    switch (j->phase) {
    case PRN_PH_STEP:
        return (j->pos < 12u) ? k_pos[j->pos] : -1;
    case PRN_PH_VERDICT:
        return SV_STEP_COUNTER;
    case PRN_PH_STOP:
        return SV_STEP_STOP;
    case PRN_PH_RELINK:
        return SV_STEP_RELINK;
    case PRN_PH_END:
        return SV_STEP_END;
    default:
        return SV_STEP_IDLE;
    }
}

/** @brief The job's step as an sv_step_t. */
static inline int dual_step(const dual_t *d)
{
    return d->prn ? dual_prn_step(&d->pj) : d->sv.step;
}

/** @brief Whether the job has ended. */
static inline bool dual_ended(const dual_t *d)
{
    return d->prn ? prn_job_ended(&d->pj) : sv_job_ended(&d->sv);
}

/** @brief Whether the write that arms STOP went out (START). */
static inline bool dual_started(const dual_t *d)
{
    return d->prn ? d->pj.armed : d->sv.started;
}

/** @brief The job's end. */
static inline int dual_err(const dual_t *d)
{
    return d->prn ? d->pj.err : d->sv.err;
}

/**
 * @brief Log the job's state.
 * @param d the rig
 */
static inline void dual_mark(dual_t *d)
{
    if (d->nlog >= DUAL_LOG_MAX) {
        return;
    }
    dual_ev_t *e = &d->log[d->nlog++];
    memset(e, 0, sizeof *e);
    e->t = d->now;
    e->kind = (uint8_t)dual_step(d);
    const bool ended = dual_ended(d);
    e->err = ended ? (uint8_t)dual_err(d) : 0u;
    const bool relinked = d->prn ? d->pj.relinked : d->sv.relinked;
    const bool counted = d->prn ? d->pj.counted : d->sv.counted;
    e->flags = (uint8_t)((ended ? 1u : 0u) | (relinked ? 2u : 0u)
                         | (counted ? 4u : 0u));
}

/**
 * @brief The job's next write, logged.
 * @param d   the rig
 * @param now the clock
 * @param p   set to the bytes
 * @param n   set to their count
 * @return true when a write is handed out
 */
static inline bool dual_tx(dual_t *d, uint32_t now, const uint8_t **p,
                           uint16_t *n)
{
    prn_write_t kind = PRN_W_REQ;
    const bool got = d->prn ? prn_job_tx(&d->pj, now, p, n, &kind)
                            : sv_job_tx(&d->sv, now, p, n);
    if (got && d->nlog < DUAL_LOG_MAX) {
        dual_ev_t *e = &d->log[d->nlog++];
        memset(e, 0, sizeof *e);
        e->t = now;
        e->n = *n;
        e->kind = (uint8_t)kind;
        memcpy(e->b, *p, (*n <= 128u) ? *n : 128u);
    }
    return got;
}

/**
 * @brief One ms of the link (sv_sim.h's rig_step()).
 * @param d the rig
 */
static inline void dual_step_ms(dual_t *d)
{
    if (d->prn) {
        prn_job_tick(&d->pj, d->now);
    } else {
        sv_job_tick(&d->sv, d->now);
    }
    if (d->link) {
        if (d->owed && (uint32_t)(d->now - d->owed_at) < 0x80000000u) {
            d->owed = false;
            if (d->prn) {
                prn_job_written(&d->pj, d->owed_ok, d->now);
            } else {
                sv_job_written(&d->sv, d->owed_ok, d->now);
            }
        }
        const uint8_t *p = NULL;
        uint16_t n = 0u;
        if (dual_tx(d, d->now, &p, &n)) {
            const sim_write_t w = sim_write(&d->sim, p, n, d->now);
            if (w != SIM_W_LOST) {
                d->owed = true;
                d->owed_at = d->now + 1u + d->ack_ms;
                d->owed_ok = (w == SIM_W_OK);
            }
        }
        const uint8_t *b = NULL;
        unsigned k;
        while ((k = sim_notify(&d->sim, d->now, &b)) != 0u) {
            if (d->prn) {
                prn_job_rx(&d->pj, b, k, d->now);
            } else {
                sv_job_rx(&d->sv, b, k, d->now);
            }
        }
    }
    d->now++;
}

/**
 * @brief Run until the job ends or @p ms pass; log the state.
 * @param d  the rig
 * @param ms the time
 */
static inline void dual_run(dual_t *d, uint32_t ms)
{
    const uint32_t start = d->now;
    while (!dual_ended(d) && (uint32_t)(d->now - start) < ms) {
        dual_step_ms(d);
    }
    dual_mark(d);
}

/**
 * @brief Run until the job reaches @p step, at most @p ms; log the state.
 * @param d    the rig
 * @param step the step
 * @param ms   the time
 */
static inline void dual_run_to(dual_t *d, int step, uint32_t ms)
{
    const uint32_t start = d->now;
    while (dual_step(d) != step && !dual_ended(d)
           && (uint32_t)(d->now - start) < ms) {
        dual_step_ms(d);
    }
    dual_mark(d);
}

/**
 * @brief Run a PRINT to @p step; at START on until START is out
 *        (test_supvan_job_ends.c's run_to()).
 * @param d    the rig
 * @param step the step
 */
static inline void dual_to(dual_t *d, int step)
{
    dual_run_to(d, step, 20000u);
    while (step == SV_STEP_START && !dual_started(d)
           && dual_step(d) == SV_STEP_START) {
        dual_step_ms(d);
    }
    dual_mark(d);
}

/**
 * @brief Drop or restore the link for both ends; log the state.
 * @param d  the rig
 * @param up the state
 */
static inline void dual_link(dual_t *d, bool up)
{
    d->link = up;
    d->owed = false;
    if (!up) {
        d->sim.nout = 0u;
        d->sim.sent = 0u;
        d->sim.nframe = 0u;
    }
    if (d->prn) {
        prn_job_link(&d->pj, up, d->now);
    } else {
        sv_job_link(&d->sv, up, d->now);
    }
    dual_mark(d);
}

/**
 * @brief Abort the job; log the state.
 * @param d the rig
 */
static inline void dual_abort(dual_t *d)
{
    if (d->prn) {
        prn_job_abort(&d->pj, d->now);
    } else {
        sv_job_abort(&d->sv, d->now);
    }
    dual_mark(d);
}

/**
 * @brief Feed bytes to the job as a notification; log the state.
 * @param d the rig
 * @param b the bytes
 * @param n their count
 */
static inline void dual_rx(dual_t *d, const uint8_t *b, size_t n)
{
    if (d->prn) {
        prn_job_rx(&d->pj, b, n, d->now);
    } else {
        sv_job_rx(&d->sv, b, n, d->now);
    }
    dual_mark(d);
}

/**
 * @brief Start a job at @p t with the rig's stream description.
 * @param d    the rig
 * @param kind QUERY or PRINT
 * @param t    the clock
 */
static inline void dual_restart(dual_t *d, sv_job_kind_t kind, uint32_t t)
{
    bool ok;
    if (d->prn) {
        const prn_stream_t z = { d->z.data, d->z.len, d->z.buffers, 0u,
                                 d->z.w_mm, d->z.h_mm };
        ok = prn_job_start(&d->pj, d->spec,
                           (kind == SV_JOB_PRINT) ? PRN_JOB_PRINT
                                                  : PRN_JOB_QUERY,
                           (kind == SV_JOB_PRINT) ? &z : NULL, t);
    } else {
        ok = sv_job_start(&d->sv, kind, (kind == SV_JOB_PRINT) ? &d->z : NULL,
                          t);
    }
    if (!ok && d->nlog < DUAL_LOG_MAX) {
        d->log[d->nlog].t = t;
        d->log[d->nlog].kind = 0xEEu;
        d->nlog++;
    }
}

/**
 * @brief Start a job at @p t0 against a fresh printer, as rig_start():
 *        a PRINT sends RIG_STREAM bytes of 3 buffers for 40 x 30mm.
 * @param d    the rig
 * @param kind QUERY or PRINT
 * @param t0   the clock
 */
static inline void dual_start(dual_t *d, sv_job_kind_t kind, uint32_t t0)
{
    sim_init(&d->sim);
    d->now = t0;
    d->link = true;
    d->owed = false;
    d->ack_ms = 0u;
    uint32_t x = 5u;
    for (unsigned i = 0; i < RIG_STREAM; ++i) {
        x = x * 1103515245u + 12345u;
        d->stream[i] = (uint8_t)(x >> 24);
    }
    d->z.data = d->stream;
    d->z.len = RIG_STREAM;
    d->z.buffers = 3u;
    d->z.w_mm = 40u;
    d->z.h_mm = 30u;
    d->sim.expect = d->stream;
    d->sim.expect_len = RIG_STREAM;
    dual_restart(d, kind, t0);
}

#endif /* PRN_DUAL_H */
