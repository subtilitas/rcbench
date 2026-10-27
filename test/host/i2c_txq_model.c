/*
 * A modelled I2C bus under protocols/i2c_txq.  See i2c_txq_model.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "i2c_txq_model.h"

#include <string.h>

/* The duration of one byte and its acknowledge at 400kHz, in us. */
#define BYTE_US 23u

void model_init(model_t *m, uint64_t now)
{
    memset(m, 0, sizeof(*m));
    m->now = now;
    m->nack_byte = -1;
    m->pullups = true;
}

bool model_level(const model_t *m, unsigned line)
{
    if (line == I2C_TXQ_SDA) {
        if (m->sda_hold != 0 || m->now < m->sda_hold_us
            || (m->gpio && m->drive_sda)) {
            return false;
        }
    } else if (m->scl_hold || (m->gpio && m->drive_scl)) {
        return false;
    }
    return m->pullups || m->pad_pu;
}

// cppcheck-suppress constParameterCallback
static bool op_get(void *ctx, unsigned line)
{
    return model_level(ctx, line);
}

static void op_start(void *ctx, const i2c_txq_xfer_t *x)
{
    model_t *m = ctx;

    m->starts++;
    m->last = *x;
    m->pending = true;
    m->end_abrt = 0;
    m->rx_n = 0;
    m->hang = m->stall;                 /* never ends: SCL is held */
    if (m->hang) {
        return;
    }
    if (!m->present[x->addr & 0x7Fu]) {
        m->end_abrt = I2C_TXQ_ABRT_ADDR_NOACK | m->abort_extra;
        m->end_at = m->now + BYTE_US + m->extra_us;
        return;
    }
    if (m->nack_byte >= 0 && m->nack_byte < (int)x->wr_len) {
        m->end_abrt = I2C_TXQ_ABRT_DATA_NOACK | m->abort_extra;
        m->end_at = m->now + BYTE_US * (2u + (unsigned)m->nack_byte)
                    + m->extra_us;
        return;
    }
    for (unsigned i = 0; i < x->rd_len; ++i) {
        m->rx[i] = (uint8_t)(x->addr + 0x11u * (i + 1u));
    }
    m->rx_n = (uint8_t)(x->rd_len - (m->short_read && x->rd_len ? 1u : 0u));
    m->end_at = m->now + i2c_txq_bus_us(x, I2C_TXQ_HZ) + m->extra_us;
}

static void op_abort(void *ctx)
{
    model_t *m = ctx;

    m->aborts++;
    if (m->pending && !m->hang
        && (int64_t)(m->end_at - (m->now + BYTE_US)) > 0) {
        m->end_at = m->now + BYTE_US;
        m->end_abrt = I2C_TXQ_ABRT_USER;
        m->rx_n = 0;
    }
}

static void op_reset(void *ctx)
{
    model_t *m = ctx;

    m->resets++;
    m->pending = false;
}

static void op_pullups(void *ctx, bool on)
{
    model_t *m = ctx;

    m->pad_pu = on;
    if (on) {
        m->pullup_ons++;
    }
}

static void op_gpio(void *ctx, bool on)
{
    model_t *m = ctx;

    m->gpio = on;
    m->drive_sda = false;
    m->drive_scl = false;
    if (on) {
        m->pending = false;
        m->gpio_ons++;
    } else {
        m->gpio_offs++;
    }
}

static void op_line(void *ctx, unsigned line, bool high)
{
    model_t *m = ctx;

    if (line == I2C_TXQ_SCL) {
        const bool was_low = m->drive_scl;
        m->drive_scl = !high;
        if (was_low && high) {
            if (m->scl_rises < 16u) {
                m->scl_rise_at[m->scl_rises] = m->now;
            }
            m->scl_rises++;
            /* A part sending a byte moves on one bit per clock. */
            if (m->sda_hold > 0) {
                m->sda_hold--;
            }
        }
    } else {
        if (m->drive_sda && high) {
            m->sda_rises++;
        }
        m->drive_sda = !high;
    }
}

const i2c_txq_ops_t model_ops = {
    .get = op_get,
    .start = op_start,
    .abort = op_abort,
    .reset = op_reset,
    .pullups = op_pullups,
    .gpio = op_gpio,
    .line = op_line,
};

void model_report(model_t *m, i2c_txq_t *q)
{
    if (m->pending && !m->hang && (int64_t)(m->now - m->end_at) >= 0) {
        m->pending = false;
        i2c_txq_complete(q, m->now, m->end_abrt, m->rx, m->rx_n);
    }
}

void model_tick(model_t *m, i2c_txq_t *q)
{
    m->now++;
    model_report(m, q);
    i2c_txq_poll(q, m->now);
}

void model_run_to(model_t *m, i2c_txq_t *q, uint64_t t)
{
    while (m->now != t) {
        model_tick(m, q);
    }
}
