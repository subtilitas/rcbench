/*
 * The rig of the I2C queue's core suites (test_i2c_txq, test_i2c_txq_calls):
 * one queue over the modelled bus of i2c_txq_model.c, a done callback that
 * logs and, as a case arms it, submits again, polls, shuts the queue or
 * starts a bus clear.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string.h>

#include "greatest.h"

#include "i2c_txq.h"
#include "i2c_txq_model.h"

static model_t m;
static i2c_txq_t q;
static i2c_txq_xfer_t *done_log[64];
static unsigned done_n;
static bool resubmit;
/* Submit again and poll, as a driver's submit does, this many times more. */
static unsigned repoll_left;
static uint64_t repoll_deadline;
static unsigned cb_depth;
static unsigned cb_max;
/* At a callback site: the first callback shuts the queue, or submits
 * `again` and polls. */
enum { SITE_OFF = 0, SITE_SHUT, SITE_SUBMIT, SITE_RECOVER };
static int site_mode;
static unsigned ops_at_shut;
static i2c_txq_xfer_t again;
static i2c_txq_err_t again_err;

static inline void on_done(void *user, i2c_txq_xfer_t *x)
{
    (void)user;
    cb_depth++;
    if (cb_depth > cb_max) {
        cb_max = cb_depth;
    }
    if (done_n < 64u) {
        done_log[done_n] = x;
    }
    done_n++;
    if (resubmit) {
        (void)i2c_txq_submit(&q, x);
    }
    if (site_mode != SITE_OFF) {
        const int mode = site_mode;
        site_mode = SITE_OFF;
        if (mode == SITE_SHUT) {
            i2c_txq_shut(&q, m.now);
            ops_at_shut = m.ops;
        } else if (mode == SITE_RECOVER) {
            (void)i2c_txq_recover(&q, m.now);
        } else {
            memset(&again, 0, sizeof(again));
            again.addr = 0x40;
            again.wr_len = 1;
            again.rd_len = 2;
            again.cls = I2C_TXQ_SOFT;
            again.not_before_us = m.now;
            again.deadline_us = m.now + 9000u;
            again_err = i2c_txq_submit(&q, &again);
            i2c_txq_poll(&q, m.now);
        }
    }
    if (repoll_left > 0u) {
        repoll_left--;
        x->not_before_us = m.now;
        x->deadline_us = repoll_deadline;
        (void)i2c_txq_submit(&q, x);
        i2c_txq_poll(&q, m.now);
    }
    cb_depth--;
}

/* An empty queue over a bus with parts at 0x40 to 0x4F, at time @p now. */
static inline void setup(uint64_t now)
{
    i2c_txq_cfg_t c;

    model_init(&m, now);
    for (unsigned a = 0x40u; a <= 0x4Fu; ++a) {
        m.present[a] = true;
    }
    i2c_txq_cfg_default(&c);
    c.done = on_done;
    if (!i2c_txq_init(&q, &model_ops, &m, &c)) {
        T_FAIL("init");
    }
    done_n = 0;
    resubmit = false;
    repoll_left = 0;
    cb_depth = 0;
    cb_max = 0;
    site_mode = SITE_OFF;
    memset(&again, 0, sizeof(again));
    again_err = I2C_TXQ_E_OK;
}

/* A register read of 2 bytes. */
static inline void rd2(i2c_txq_xfer_t *x, uint8_t addr, uint8_t cls,
                uint64_t not_before, uint64_t deadline)
{
    memset(x, 0, sizeof(*x));
    x->addr = addr;
    x->wr_len = 1;
    x->wr[0] = 0x01;
    x->rd_len = 2;
    x->cls = cls;
    x->not_before_us = not_before;
    x->deadline_us = deadline;
}
