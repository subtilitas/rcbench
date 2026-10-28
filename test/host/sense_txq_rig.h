/*
 * The sensor bus on the I2C queue (shared/sense/sense_txq.c) over the
 * modelled bus of i2c_txq_model.c: a queue, the model as its driver, and a
 * sense_txq_io_t whose pump advances the model's clock 1us, takes an end
 * the model reports and polls the queue -- as the IOmcu's wait loop takes
 * the I2C block's interrupt and polls.  test_sense_txq and
 * test_sense_txq_svc share it.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <string.h>

#include "i2c_txq.h"
#include "i2c_txq_model.h"
#include "sense_txq.h"

/** @brief One modelled bus behind the sensor wire. */
typedef struct {
    model_t     m;
    i2c_txq_t   q;
    sense_txq_t t;
    bool        frozen;     /* the pump advances the clock and polls not */
    uint32_t    freeze_n;   /* or: the next freeze_n pumps do so          */
    unsigned    pumps;
    unsigned    aborts;     /* abort() calls, and the time of the last    */
    uint64_t    abort_at;
} txq_rig_t;

static inline i2c_txq_err_t rig_submit(void *ctx, i2c_txq_xfer_t *x)
{
    txq_rig_t *r = ctx;
    const i2c_txq_err_t e = i2c_txq_submit(&r->q, x);
    if (e == I2C_TXQ_E_OK) {
        i2c_txq_poll(&r->q, r->m.now);
    }
    return e;
}

static inline void rig_pump(void *ctx)
{
    txq_rig_t *r = ctx;
    r->pumps++;
    if (r->frozen || r->freeze_n > 0u) {
        if (r->freeze_n > 0u) {
            r->freeze_n--;
        }
        r->m.now++;
        return;
    }
    model_tick(&r->m, &r->q);
}

static inline bool rig_abort(void *ctx, i2c_txq_xfer_t *x)
{
    txq_rig_t *r = ctx;
    r->aborts++;
    r->abort_at = r->m.now;
    return i2c_txq_abort(&r->q, x, r->m.now);
}

static inline bool rig_recover(void *ctx)
{
    txq_rig_t *r = ctx;
    return i2c_txq_recover(&r->q, r->m.now);
}

static inline const i2c_txq_t *rig_queue(void *ctx)
{
    const txq_rig_t *r = ctx;
    return &r->q;
}

static inline uint64_t rig_now(void *ctx)
{
    const txq_rig_t *r = ctx;
    return r->m.now;
}

/* A bus with pull-ups and no part, the queue at its defaults, at @p now. */
static inline void txq_rig_init(txq_rig_t *r, uint64_t now)
{
    memset(r, 0, sizeof(*r));
    model_init(&r->m, now);
    (void)i2c_txq_init(&r->q, &model_ops, &r->m, NULL);
    const sense_txq_io_t io = {
        rig_submit, rig_pump, rig_abort, rig_recover, rig_queue, rig_now, r,
    };
    sense_txq_init(&r->t, &io);
}
