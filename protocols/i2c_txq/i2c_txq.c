/**
 * @file
 * @brief A transaction queue for one I2C bus master.
 *
 * See include/i2c_txq.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "i2c_txq.h"

#include <string.h>

_Static_assert(I2C_TXQ_WR_MAX + I2C_TXQ_RD_MAX <= I2C_TXQ_FIFO,
               "a transaction fits the transmit FIFO whole");
_Static_assert(I2C_TXQ_DEPTH <= 255u, "the count is a byte");

/** @brief What the queue is doing. */
enum {
    P_IDLE = 0, /**< nothing picked                                  */
    P_SETTLE,   /**< picked; a line was low, waiting for it to rise  */
    P_PROBE,    /**< picked; the pad pull-ups on, waiting            */
    P_WIRE,     /**< on the controller                               */
    P_CLEAR,    /**< the bus clear                                   */
};

/** @brief The actions of the bus clear, in order. */
enum {
    C_BEGIN = 0, /**< controller reset, both pins GPIO, released     */
    C_SCL_LOW,   /**< SCL driven low                                 */
    C_SCL_UP,    /**< SCL released, then up to stretch_us to rise    */
    C_SDA_LOW,   /**< SDA driven low                                 */
    C_SDA_UP,    /**< SDA released                                   */
    C_END,       /**< lines read, controller set up again on the pins */
};

/**
 * @brief The clear: 9 clocks, then a STOP -- SDA low while SCL is low, SCL
 *        up, SDA up.  Each action is followed by half a clock.
 */
static const uint8_t k_clear[] = {
    C_BEGIN,
    C_SCL_LOW, C_SCL_UP, C_SCL_LOW, C_SCL_UP, C_SCL_LOW, C_SCL_UP,
    C_SCL_LOW, C_SCL_UP, C_SCL_LOW, C_SCL_UP, C_SCL_LOW, C_SCL_UP,
    C_SCL_LOW, C_SCL_UP, C_SCL_LOW, C_SCL_UP, C_SCL_LOW, C_SCL_UP,
    C_SCL_LOW, C_SDA_LOW, C_SCL_UP, C_SDA_UP,
    C_END,
};

static void poll_in(i2c_txq_t *q, uint64_t now);

/** @brief The names of i2c_txq_res_t, in its order. */
static const char *const k_res_name[I2C_TXQ_RESULTS] = {
    "ok", "nack-addr", "nack-data", "arb-lost", "timeout", "bus-low",
    "no-pullup", "dropped", "aborted", "fault",
};

/**
 * @brief Whether time @p a is after time @p b, across the wrap.
 *
 * @param a  A time, in us.
 * @param b  A time, in us.
 * @return True when @p a is later than @p b.
 */
static bool after(uint64_t a, uint64_t b)
{
    return (int64_t)(a - b) > 0;
}

/**
 * @brief Whether @p a goes before @p b: hard first, then the earlier
 *        deadline, then the earlier submit.
 *
 * @param a  A queued transaction.
 * @param b  Another one.
 * @return True when @p a goes first.
 */
static bool before(const i2c_txq_xfer_t *a, const i2c_txq_xfer_t *b)
{
    if (a->cls != b->cls) {
        return a->cls == I2C_TXQ_HARD;
    }
    if (a->deadline_us != b->deadline_us) {
        return after(b->deadline_us, a->deadline_us);
    }
    return (int32_t)(a->seq - b->seq) < 0;
}

void i2c_txq_cfg_default(i2c_txq_cfg_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->hz = I2C_TXQ_HZ;
    cfg->timeout_us = I2C_TXQ_TIMEOUT_US;
    cfg->settle_us = I2C_TXQ_SETTLE_US;
    cfg->probe_us = I2C_TXQ_PROBE_US;
    cfg->clear_half_us = I2C_TXQ_CLEAR_HALF_US;
    cfg->clear_stretch_us = I2C_TXQ_CLEAR_STRETCH_US;
    cfg->late_us = I2C_TXQ_LATE_US;
}

bool i2c_txq_init(i2c_txq_t *q, const i2c_txq_ops_t *ops, void *ctx,
                  const i2c_txq_cfg_t *cfg)
{
    i2c_txq_cfg_t c;

    if (q == NULL || ops == NULL || ops->get == NULL || ops->start == NULL
        || ops->abort == NULL || ops->reset == NULL || ops->pullups == NULL
        || ops->gpio == NULL || ops->line == NULL) {
        return false;
    }
    if (cfg != NULL) {
        c = *cfg;
    } else {
        i2c_txq_cfg_default(&c);
    }
    if (c.hz == 0u) {
        return false;
    }
    memset(q, 0, sizeof(*q));
    q->ops = *ops;
    q->ctx = ctx;
    q->cfg = c;
    return true;
}

/**
 * @brief Whether @p addr is one a transaction may name: not 0x00 to 0x07
 *        (the general call, START byte, CBUS, high speed and reserved
 *        codes) and not 0x78 to 0x7F (10-bit addressing and reserved).
 *
 * @param addr  The address.
 * @return True for 0x08 to 0x77.
 */
static bool addr_ok(uint8_t addr)
{
    return addr >= 0x08u && addr <= 0x77u;
}

i2c_txq_err_t i2c_txq_submit(i2c_txq_t *q, i2c_txq_xfer_t *x)
{
    if (q == NULL || x == NULL || !addr_ok(x->addr)
        || x->wr_len > I2C_TXQ_WR_MAX || x->rd_len > I2C_TXQ_RD_MAX
        || (x->wr_len == 0u && x->rd_len == 0u)
        || (x->cls != I2C_TXQ_SOFT && x->cls != I2C_TXQ_HARD)) {
        return I2C_TXQ_E_ARG;
    }
    if (x->state == I2C_TXQ_ST_QUEUED || x->state == I2C_TXQ_ST_RUNNING) {
        return I2C_TXQ_E_BUSY;
    }
    if (q->closed) {
        return I2C_TXQ_E_CLOSED;
    }
    if (q->n >= I2C_TXQ_DEPTH) {
        return I2C_TXQ_E_FULL;
    }
    x->state = I2C_TXQ_ST_QUEUED;
    x->result = I2C_TXQ_OK;
    x->flags = 0;
    x->seq = q->seq++;
    x->start_us = 0;
    x->done_us = 0;
    q->slot[q->n++] = x;
    q->st.submitted++;
    if (q->n > q->st.max_queued) {
        q->st.max_queued = q->n;
    }
    return I2C_TXQ_E_OK;
}

/**
 * @brief Take slot @p i out of the queue.  The order of the slots is not
 *        kept: the pick reads them all.
 *
 * @param q  The queue.
 * @param i  The slot.
 */
static void unqueue(i2c_txq_t *q, size_t i)
{
    q->n--;
    q->slot[i] = q->slot[q->n];
}

/**
 * @brief The end of @p x: result, stamp, counters, then the callback.
 *
 * @param q    The queue.
 * @param x    A transaction no longer in the queue nor running.
 * @param r    Its result.
 * @param now  The time of its end, in us.
 */
static void finish(i2c_txq_t *q, i2c_txq_xfer_t *x, i2c_txq_res_t r,
                   uint64_t now)
{
    x->result = (uint8_t)r;
    x->done_us = now;
    x->state = I2C_TXQ_ST_DONE;
    switch (r) {
    case I2C_TXQ_OK:      q->st.ok++;      break;
    case I2C_TXQ_DROPPED: q->st.dropped++; break;
    case I2C_TXQ_ABORTED: q->st.aborted++; break;
    default:
        q->st.failed++;
        if (r == I2C_TXQ_TIMEOUT) {
            q->st.timeouts++;
        }
        break;
    }
    if (q->cfg.done != NULL) {
        q->cfg.done(q->cfg.user, x);
    }
}

/**
 * @brief The running transaction ends with @p r.  The pull-ups go off when
 *        it ends in the probe.
 *
 * @param q    The queue.
 * @param r    Its result.
 * @param now  The time of its end, in us.
 */
static void end_run(i2c_txq_t *q, i2c_txq_res_t r, uint64_t now)
{
    i2c_txq_xfer_t *x = q->run;

    if (q->phase == P_PROBE) {
        q->ops.pullups(q->ctx, false);
    }
    if ((x->flags & I2C_TXQ_F_TRIAL) != 0u && r != I2C_TXQ_ABORTED
        && r != I2C_TXQ_DROPPED) {
        q->trial = false;
    }
    q->run = NULL;
    q->phase = P_IDLE;
    finish(q, x, r, now);
}

/**
 * @brief i2c_txq_abort() inside the guard.
 *
 * @param q    The queue.
 * @param x    The transaction.
 * @param now  The present time, in us.
 * @return False when @p x is neither queued nor running on @p q.
 */
static bool abort_in(i2c_txq_t *q, i2c_txq_xfer_t *x, uint64_t now)
{
    for (size_t i = 0; i < q->n; ++i) {
        if (q->slot[i] == x) {
            unqueue(q, i);
            finish(q, x, I2C_TXQ_ABORTED, now);
            return true;
        }
    }
    if (q->run != x) {
        return false;
    }
    if (q->phase != P_WIRE) {
        end_run(q, I2C_TXQ_ABORTED, now);
    } else if ((x->flags & I2C_TXQ_F_ABORT) == 0u) {
        x->flags |= I2C_TXQ_F_ABORT;
        q->ops.abort(q->ctx);
    }
    return true;
}

/**
 * @brief The running transaction cut: on the wire the controller reset,
 *        and it ends I2C_TXQ_ABORTED.
 *
 * @param q    The queue.
 * @param now  The present time, in us.
 */
static void cut_run(i2c_txq_t *q, uint64_t now)
{
    if (q->run == NULL) {
        return;
    }
    if (q->phase == P_WIRE) {
        q->ops.reset(q->ctx);
    }
    end_run(q, I2C_TXQ_ABORTED, now);
}

/**
 * @brief i2c_txq_flush() inside the guard.
 *
 * @param q    The queue.
 * @param now  The present time, in us.
 */
static void flush_in(i2c_txq_t *q, uint64_t now)
{
    /* The callback may submit again: only what was queued before goes. */
    const uint32_t mark = q->seq;

    if (q->phase == P_CLEAR) {
        q->ops.gpio(q->ctx, false);
        q->phase = P_IDLE;
        q->clear_result = I2C_TXQ_ABORTED;
    }
    cut_run(q, now);
    for (size_t i = 0; i < q->n;) {
        i2c_txq_xfer_t *x = q->slot[i];
        if ((int32_t)(x->seq - mark) >= 0) {
            ++i;
            continue;
        }
        unqueue(q, i);
        finish(q, x, I2C_TXQ_ABORTED, now);
    }
}

/**
 * @brief i2c_txq_complete() inside the guard.
 *
 * @param q     The queue.
 * @param now   The time of the end, in us.
 * @param abrt  The controller's abort causes.
 * @param rx    The bytes read.
 * @param n     Bytes in @p rx.
 */
static void complete_in(i2c_txq_t *q, uint64_t now, uint32_t abrt,
                        const uint8_t *rx, size_t n)
{
    if (q->phase != P_WIRE) {
        q->st.spurious++;
        return;
    }
    i2c_txq_xfer_t *x = q->run;
    i2c_txq_res_t r = i2c_txq_abrt_result(abrt);

    if (r == I2C_TXQ_OK && (n != x->rd_len || (n != 0u && rx == NULL))) {
        r = I2C_TXQ_FAULT;
    }
    if (r != I2C_TXQ_OK && (x->flags & I2C_TXQ_F_ABORT) != 0u) {
        r = I2C_TXQ_ABORTED;
    }
    uint64_t at = now;
    if (r == I2C_TXQ_OK) {
        if (n != 0u) {
            memcpy(x->rd, rx, n);
        }
        const uint64_t end = x->start_us + i2c_txq_bus_us(x, q->cfg.hz);
        if (after(now, end + q->cfg.late_us)) {
            at = end;
            x->flags |= I2C_TXQ_F_LATE_STAMP;
            q->st.late_stamps++;
        }
    }
    end_run(q, r, at);
}

/**
 * @brief Hand the running transaction to the controller.
 *
 * @param q    The queue.
 * @param now  The present time, in us.
 */
static void to_wire(i2c_txq_t *q, uint64_t now)
{
    i2c_txq_xfer_t *x = q->run;

    q->phase = P_WIRE;
    x->flags |= I2C_TXQ_F_WIRE;
    x->start_us = now;
    if (x->cls == I2C_TXQ_HARD && after(now, x->deadline_us)) {
        x->flags |= I2C_TXQ_F_LATE_START;
        q->st.late_starts++;
    }
    q->st.busy_us += i2c_txq_bus_us(x, q->cfg.hz);
    q->ops.start(q->ctx, x);
}

/**
 * @brief Both lines high.
 *
 * @param q  The queue.
 * @return True when SDA and SCL read high.
 */
static bool lines_high(const i2c_txq_t *q)
{
    return q->ops.get(q->ctx, I2C_TXQ_SDA) && q->ops.get(q->ctx, I2C_TXQ_SCL);
}

/**
 * @brief The picked transaction, not yet on the wire, back to the queue
 *        with its submit number.  The trial flag goes; the trial stays.
 *        The slot array has room for it: I2C_TXQ_DEPTH queued and the one
 *        picked.
 *
 * @param q  The queue.
 */
static void requeue(i2c_txq_t *q)
{
    i2c_txq_xfer_t *x = q->run;

    x->state = I2C_TXQ_ST_QUEUED;
    x->flags &= (uint8_t)~I2C_TXQ_F_TRIAL;
    q->slot[q->n++] = x;
    q->run = NULL;
    q->phase = P_IDLE;
}

/**
 * @brief The line check of the picked transaction.  High lines send it; a
 *        soft one whose deadline passed in the settle ends
 *        I2C_TXQ_DROPPED instead.  A low line gets settle_us to rise, then the pad pull-ups for
 *        probe_us: a line that rises on them is held by nothing and has no
 *        pull-up (I2C_TXQ_NO_PULLUP), one still low after them is held by a
 *        part (I2C_TXQ_BUS_LOW).  Nothing is sent after the probe.
 *
 * @param q    The queue.
 * @param now  The present time, in us.
 */
static void check_lines(i2c_txq_t *q, uint64_t now)
{
    const bool high = lines_high(q);

    if (q->phase == P_PROBE) {
        if (high) {
            end_run(q, I2C_TXQ_NO_PULLUP, now);
        } else if (now - q->phase_at >= q->cfg.probe_us) {
            end_run(q, I2C_TXQ_BUS_LOW, now);
        }
        return;
    }
    if (high) {
        /* Held while a line settled: back to the queue, in its old place
         * (its submit number), for the order and the deadline rules. */
        if (q->hold) {
            requeue(q);
            return;
        }
        /* Picked before its deadline, a soft one may pass it while a line
         * settles: it is then dropped, as in the queue. */
        if (q->run->cls == I2C_TXQ_SOFT && after(now, q->run->deadline_us)) {
            end_run(q, I2C_TXQ_DROPPED, now);
        } else {
            to_wire(q, now);
        }
    } else if (now - q->phase_at >= q->cfg.settle_us) {
        q->ops.pullups(q->ctx, true);
        q->phase = P_PROBE;
        q->phase_at = now;
    }
}

/**
 * @brief Take the bus clear's steps that are due.
 *
 * @param q    The queue.
 * @param now  The present time, in us.
 */
static void clear_step(i2c_txq_t *q, uint64_t now)
{
    while (q->phase == P_CLEAR && !after(q->phase_at, now)) {
        const uint8_t act = k_clear[q->step];
        switch (act) {
        case C_BEGIN:   q->ops.gpio(q->ctx, true);                    break;
        case C_SCL_LOW: q->ops.line(q->ctx, I2C_TXQ_SCL, false);      break;
        case C_SDA_LOW: q->ops.line(q->ctx, I2C_TXQ_SDA, false);      break;
        case C_SDA_UP:  q->ops.line(q->ctx, I2C_TXQ_SDA, true);       break;
        case C_SCL_UP:
            if (!q->scl_up) {
                q->ops.line(q->ctx, I2C_TXQ_SCL, true);
                q->scl_up = true;
                q->phase_at = now;
            }
            if (!q->ops.get(q->ctx, I2C_TXQ_SCL)
                && now - q->phase_at < q->cfg.clear_stretch_us) {
                return;
            }
            q->scl_up = false;
            break;
        default: { /* C_END */
            const bool high = lines_high(q);
            q->ops.gpio(q->ctx, false);
            q->clear_result = high ? I2C_TXQ_OK : I2C_TXQ_BUS_LOW;
            q->phase = P_IDLE;
            q->trial = true;
            q->st.clears++;
            return;
        }
        }
        q->step++;
        q->phase_at = now + q->cfg.clear_half_us;
    }
}

/**
 * @brief i2c_txq_recover() inside the guard.
 *
 * @param q    The queue.
 * @param now  The present time, in us.
 */
static void recover_in(i2c_txq_t *q, uint64_t now)
{
    cut_run(q, now);
    /* The cut one's callback may have shut the queue, or started a clear
     * of its own: the pins are then not this clear's. */
    if (q->closed || q->phase == P_CLEAR) {
        return;
    }
    q->phase = P_CLEAR;
    q->step = 0;
    q->scl_up = false;
    q->trial = false;
    q->phase_at = now;
    clear_step(q, now);
}

/**
 * @brief Drop every soft transaction queued before this poll whose deadline
 *        has passed.
 *
 * @param q    The queue.
 * @param now  The present time, in us.
 */
static void drop_late(i2c_txq_t *q, uint64_t now)
{
    const uint32_t mark = q->seq;

    for (size_t i = 0; i < q->n;) {
        i2c_txq_xfer_t *x = q->slot[i];
        if (x->cls == I2C_TXQ_SOFT && after(now, x->deadline_us)
            && (int32_t)(x->seq - mark) < 0) {
            unqueue(q, i);
            finish(q, x, I2C_TXQ_DROPPED, now);
        } else {
            ++i;
        }
    }
}

/**
 * @brief Pick the next transaction whose not-before time has come; a soft
 *        one past its deadline never starts.
 *
 * @param q    The queue.
 * @param now  The present time, in us.
 * @return The slot, or -1 when none is ready.
 */
static int pick(const i2c_txq_t *q, uint64_t now)
{
    int best = -1;

    for (size_t i = 0; i < q->n; ++i) {
        const i2c_txq_xfer_t *x = q->slot[i];
        if (after(x->not_before_us, now)
            || (x->cls == I2C_TXQ_SOFT && after(now, x->deadline_us))) {
            continue;
        }
        if (best < 0 || before(x, q->slot[best])) {
            best = (int)i;
        }
    }
    return best;
}

/**
 * @brief i2c_txq_poll() inside the guard.
 *
 * @param q    The queue.
 * @param now  The present time, in us.
 */
static void poll_in(i2c_txq_t *q, uint64_t now)
{
    if (q->phase == P_CLEAR) {
        clear_step(q, now);
    }
    /* A done callback of the steps below may shut the queue.  It then
     * leaves it empty and idle, and submits are refused, so the rest of the
     * poll finds nothing to pick and calls no driver function. */
    if (q->phase == P_WIRE
        && now - q->run->start_us >= q->cfg.timeout_us) {
        q->ops.reset(q->ctx);
        end_run(q, I2C_TXQ_TIMEOUT, now);
    }
    if (q->phase == P_SETTLE || q->phase == P_PROBE) {
        check_lines(q, now);
    }
    drop_late(q, now);
    if (q->phase != P_IDLE || q->hold) {
        return;
    }
    const int i = pick(q, now);
    if (i < 0) {
        return;
    }
    i2c_txq_xfer_t *x = q->slot[i];
    unqueue(q, (size_t)i);
    x->state = I2C_TXQ_ST_RUNNING;
    if (q->trial) {
        x->flags |= I2C_TXQ_F_TRIAL;
    }
    q->run = x;
    q->phase = P_SETTLE;
    q->phase_at = now;
    check_lines(q, now);
}

/*
 * The guard.  A done callback runs inside a queue call and may submit and
 * poll again, as a driver's submit does.  A poll made while a queue call is
 * under way does not run there: it is noted, and the outermost call runs it
 * after its own work, up to I2C_TXQ_REPOLLS times while callbacks keep
 * asking.  So the calls never nest deeper than one, a transaction a callback
 * submits starts within the call that ran the callback, and a callback that
 * submits an expired transaction on every call ends at most
 * 1 + I2C_TXQ_REPOLLS of them per outer call.
 */

/**
 * @brief Enter a queue call.
 *
 * @param q  The queue.
 */
static void enter(i2c_txq_t *q)
{
    q->depth++;
}

/**
 * @brief Leave a queue call: the outermost runs the polls callbacks asked
 *        for.
 *
 * @param q    The queue.
 * @param now  The present time, in us.
 */
static void leave(i2c_txq_t *q, uint64_t now)
{
    if (q->depth == 1u) {
        for (unsigned k = 0; q->repoll && k < I2C_TXQ_REPOLLS; ++k) {
            q->repoll = false;
            if (!q->closed) {
                poll_in(q, now);
            }
        }
        q->repoll = false;
    }
    q->depth--;
}

bool i2c_txq_abort(i2c_txq_t *q, i2c_txq_xfer_t *x, uint64_t now)
{
    if (q == NULL || x == NULL) {
        return false;
    }
    enter(q);
    const bool ok = abort_in(q, x, now);
    leave(q, now);
    return ok;
}

void i2c_txq_flush(i2c_txq_t *q, uint64_t now)
{
    if (q == NULL) {
        return;
    }
    enter(q);
    flush_in(q, now);
    leave(q, now);
}

void i2c_txq_shut(i2c_txq_t *q, uint64_t now)
{
    if (q == NULL) {
        return;
    }
    q->closed = true;
    enter(q);
    flush_in(q, now);
    leave(q, now);
}

bool i2c_txq_closed(const i2c_txq_t *q)
{
    return q == NULL || q->closed;
}

void i2c_txq_complete(i2c_txq_t *q, uint64_t now, uint32_t abrt,
                      const uint8_t *rx, size_t n)
{
    if (q == NULL) {
        return;
    }
    enter(q);
    complete_in(q, now, abrt, rx, n);
    leave(q, now);
}

bool i2c_txq_recover(i2c_txq_t *q, uint64_t now)
{
    if (q == NULL || q->closed || q->phase == P_CLEAR) {
        return false;
    }
    enter(q);
    recover_in(q, now);
    leave(q, now);
    return true;
}

void i2c_txq_poll(i2c_txq_t *q, uint64_t now)
{
    if (q == NULL || q->closed) {
        return;
    }
    if (q->depth != 0u) {
        q->repoll = true;
        return;
    }
    enter(q);
    poll_in(q, now);
    leave(q, now);
}

void i2c_txq_hold(i2c_txq_t *q, bool on)
{
    if (q != NULL) {
        q->hold = on;
    }
}

bool i2c_txq_clearing(const i2c_txq_t *q)
{
    return q != NULL && q->phase == P_CLEAR;
}

i2c_txq_res_t i2c_txq_clear_result(const i2c_txq_t *q)
{
    return q != NULL ? (i2c_txq_res_t)q->clear_result : I2C_TXQ_OK;
}

bool i2c_txq_idle(const i2c_txq_t *q)
{
    return q == NULL || q->phase == P_IDLE;
}

size_t i2c_txq_queued(const i2c_txq_t *q)
{
    return q != NULL ? q->n : 0u;
}

const i2c_txq_stats_t *i2c_txq_stats(const i2c_txq_t *q)
{
    return q != NULL ? &q->st : NULL;
}

uint32_t i2c_txq_clocks(const i2c_txq_xfer_t *x)
{
    if (x == NULL) {
        return 0;
    }
    const bool restart = x->wr_len != 0u && x->rd_len != 0u;
    const uint32_t bytes = 1u + x->wr_len + (restart ? 1u : 0u) + x->rd_len;
    return 9u * bytes + 2u + (restart ? 1u : 0u);
}

uint32_t i2c_txq_bus_us(const i2c_txq_xfer_t *x, uint32_t hz)
{
    if (x == NULL || hz == 0u) {
        return 0;
    }
    const uint64_t clocks = i2c_txq_clocks(x);
    return (uint32_t)((clocks * 1000000u + hz - 1u) / hz);
}

i2c_txq_res_t i2c_txq_abrt_result(uint32_t abrt)
{
    abrt &= I2C_TXQ_ABRT_CAUSES;
    if (abrt == 0u) {
        return I2C_TXQ_OK;
    }
    if ((abrt & I2C_TXQ_ABRT_ADDR_NOACK) != 0u) {
        return I2C_TXQ_NACK_ADDR;
    }
    if ((abrt & I2C_TXQ_ABRT_DATA_NOACK) != 0u) {
        return I2C_TXQ_NACK_DATA;
    }
    if ((abrt & I2C_TXQ_ABRT_ARB_LOST) != 0u) {
        return I2C_TXQ_ARB_LOST;
    }
    if ((abrt & I2C_TXQ_ABRT_USER) != 0u) {
        return I2C_TXQ_ABORTED;
    }
    return I2C_TXQ_FAULT;
}

const char *i2c_txq_res_name(unsigned r)
{
    return r < (unsigned)I2C_TXQ_RESULTS ? k_res_name[r] : "?";
}
