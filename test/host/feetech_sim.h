/*
 * Feetech servos on a simulated bus, for the host suites: a
 * feetech_driver_t whose wire is a table of servos with their registers.
 *
 * A servo answers PING, READ and WRITE to its ID at its rate, as the
 * protocol manual says (protocols/feetech/README.md, "Sources"); a
 * broadcast WRITE is applied by every servo at the rate and answered by
 * none.  A write below FEETECH_RAM_FIRST while the servo's lock flag is 1
 * is answered and not kept, which is one reading of the manual and not
 * measured.  A write of register 5 or 6 takes effect after the reply.
 * Two servos with one ID answer at once: the bytes are ANDed, as on a
 * line both pull low.
 *
 * Faults the suites set: no start, no end, a set_baud refused, the echo of
 * the request ahead of the reply, the next reply's checksum broken, cut
 * short, from another ID, or dropped, a lock flag that keeps its value.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FEETECH_SIM_H
#define FEETECH_SIM_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "feetech_bus.h"
#include "feetech_reg.h"
#include "feetech_wire.h"

#define FTSIM_SERVOS 8u
#define FTSIM_LOG    64u

typedef struct {
    bool     present;
    uint8_t  series;
    uint8_t  regs[256];
} ftsim_servo_t;

typedef struct {
    ftsim_servo_t servo[FTSIM_SERVOS];
    uint32_t baud;
    /* faults */
    bool     refuse_start;
    bool     hang;
    bool     refuse_baud;
    bool     echo;
    bool     bad_checksum;     /* the next reply */
    unsigned truncate;         /* the next reply loses this many bytes */
    bool     wrong_id;         /* the next reply */
    bool     drop;             /* the next reply */
    bool     stuck_lock;       /* writes of the lock flag are answered and
                                  not kept */
    /* what went on the wire */
    feetech_frame_t log[FTSIM_LOG];
    uint32_t log_baud[FTSIM_LOG];
    unsigned n_log;
    unsigned n_start;
    /* the transaction */
    bool     open;
    feetech_capture_t cap;
    unsigned n_rx;
    uint32_t wait_us;
    /* register 5 or 6 written: applied when the transaction ends */
    int      pending_servo;
    uint8_t  pending_addr, pending_value;
} ftsim_bus_t;

static inline void ftsim_init(ftsim_bus_t *s, uint32_t baud)
{
    memset(s, 0, sizeof(*s));
    s->baud = baud;
    s->pending_servo = -1;
}

/* A servo at @p id, the factory settings of @p series otherwise. */
static inline ftsim_servo_t *ftsim_add(ftsim_bus_t *s, uint8_t id,
                                   feetech_series_t series)
{
    for (unsigned i = 0; i < FTSIM_SERVOS; ++i) {
        ftsim_servo_t *v = &s->servo[i];

        if (!v->present) {
            memset(v, 0, sizeof(*v));
            v->present = true;
            v->series = (uint8_t)series;
            v->regs[FEETECH_REG_ID] = id;
            v->regs[FEETECH_REG_BAUD] = FEETECH_BAUD_DEFAULT;
            v->regs[feetech_lock_addr(series)] = 1;
            feetech_put16(series, &v->regs[FEETECH_REG_MAX_ANGLE],
                          feetech_position_max(series));
            v->regs[62] = 74;   /* 7.4 V */
            v->regs[63] = 31;   /* 31 deg C */
            return v;
        }
    }
    return NULL;
}

static inline bool ftsim_hears(const ftsim_bus_t *s, const ftsim_servo_t *v)
{
    return v->present
           && feetech_baud(v->regs[FEETECH_REG_BAUD]) == s->baud;
}

/* One servo's answer to the frame, appended to @p out. */
static inline void ftsim_answer(ftsim_bus_t *s, int idx, const uint8_t *f,
                              uint8_t *out, unsigned *n_out, bool *any)
{
    ftsim_servo_t *v = &s->servo[idx];
    const uint8_t id = f[2];
    const uint8_t len = f[3];
    const uint8_t ins = f[4];
    const uint8_t *p = &f[5];
    const unsigned np = len - 2u;
    uint8_t params[FEETECH_DATA_MAX];
    unsigned n_params = 0;
    feetech_frame_t r;
    const uint8_t lock = feetech_lock_addr((feetech_series_t)v->series);
    bool reply = id != FEETECH_ID_BROADCAST;

    if (ins == FEETECH_PING) {
        reply = true;
    } else if (ins == FEETECH_READ && np == 2u) {
        n_params = p[1] > FEETECH_DATA_MAX ? FEETECH_DATA_MAX : p[1];
        memcpy(params, &v->regs[p[0]], n_params);
    } else if (ins == FEETECH_WRITE && np >= 2u) {
        const uint8_t addr = p[0];

        for (unsigned i = 1; i < np; ++i) {
            const unsigned a = (unsigned)addr + i - 1u;

            if (a >= 256u) {
                break;
            }
            if (a < FEETECH_RAM_FIRST && v->regs[lock] != 0u) {
                continue;
            }
            if (a == lock && s->stuck_lock) {
                continue;
            }
            if (a == FEETECH_REG_ID || a == FEETECH_REG_BAUD) {
                s->pending_servo = idx;
                s->pending_addr = (uint8_t)a;
                s->pending_value = p[i];
                continue;
            }
            v->regs[a] = p[i];
        }
    } else {
        return;
    }
    if (!reply) {
        return;
    }
    /* A status frame: FF FF ID LEN ERR params CHK, the ERR byte the
     * servo's status register. */
    {
        uint8_t body[2 + FEETECH_DATA_MAX + 1];

        body[0] = v->regs[FEETECH_REG_ID];
        body[1] = (uint8_t)(n_params + 2u);
        body[2] = v->regs[FEETECH_REG_STATUS];
        memcpy(&body[3], params, n_params);
        r.bytes[0] = 0xFF;
        r.bytes[1] = 0xFF;
        memcpy(&r.bytes[2], body, n_params + 3u);
        r.bytes[5u + n_params] = feetech_checksum(body, n_params + 3u);
        r.n = (uint8_t)(n_params + 6u);
    }
    if (*any) {
        for (unsigned i = 0; i < r.n && i < *n_out; ++i) {
            out[i] &= r.bytes[i];
        }
    } else {
        memcpy(out, r.bytes, r.n);
        *n_out = r.n;
        *any = true;
    }
}

static inline bool ftsim_start(void *ctx, const uint8_t *tx, unsigned n_tx,
                             unsigned n_rx, uint32_t wait_us)
{
    ftsim_bus_t *s = (ftsim_bus_t *)ctx;
    uint8_t out[FEETECH_CAPTURE_MAX];
    unsigned n_out = 0;
    bool any = false;

    s->n_start++;
    if (s->refuse_start || s->open || n_tx < 6u || n_tx > FEETECH_FRAME_MAX) {
        return false;
    }
    if (s->n_log < FTSIM_LOG) {
        memcpy(s->log[s->n_log].bytes, tx, n_tx);
        s->log[s->n_log].n = (uint8_t)n_tx;
        s->log_baud[s->n_log] = s->baud;
        s->n_log++;
    }
    memset(&s->cap, 0, sizeof(s->cap));
    s->open = true;
    s->n_rx = n_rx;
    s->wait_us = wait_us;
    if (s->echo) {
        memcpy(s->cap.bytes, tx, n_tx);
        s->cap.n = (uint8_t)n_tx;
    }
    /* A frame whose checksum is right reaches the servos at the rate. */
    if (feetech_checksum(&tx[2], n_tx - 3u) != tx[n_tx - 1u]
        || tx[3] + 4u != n_tx) {
        return true;
    }
    for (unsigned i = 0; i < FTSIM_SERVOS; ++i) {
        const ftsim_servo_t *v = &s->servo[i];

        if (ftsim_hears(s, v)
            && (tx[2] == FEETECH_ID_BROADCAST
                || tx[2] == v->regs[FEETECH_REG_ID])) {
            ftsim_answer(s, (int)i, tx, out, &n_out, &any);
        }
    }
    if (!any || s->drop) {
        s->drop = false;
        return true;
    }
    if (s->bad_checksum) {
        out[n_out - 1u] ^= 0x01u;
        s->bad_checksum = false;
    }
    if (s->wrong_id) {
        out[2] = (uint8_t)(out[2] + 1u);
        out[n_out - 1u] = (uint8_t)(out[n_out - 1u] - 1u);
        s->wrong_id = false;
    }
    if (s->truncate > 0u) {
        n_out = s->truncate >= n_out ? 0u : n_out - s->truncate;
        s->truncate = 0;
    }
    for (unsigned i = 0; i < n_out && s->cap.n < FEETECH_CAPTURE_MAX; ++i) {
        s->cap.bytes[s->cap.n++] = out[i];
    }
    return true;
}

static inline bool ftsim_poll(void *ctx, feetech_capture_t *out)
{
    ftsim_bus_t *s = (ftsim_bus_t *)ctx;

    if (!s->open || s->hang) {
        return false;
    }
    *out = s->cap;
    s->open = false;
    if (s->pending_servo >= 0) {
        s->servo[s->pending_servo].regs[s->pending_addr] = s->pending_value;
        s->pending_servo = -1;
    }
    return true;
}

static inline bool ftsim_set_baud(void *ctx, uint32_t baud)
{
    ftsim_bus_t *s = (ftsim_bus_t *)ctx;

    if (s->refuse_baud || s->open) {
        return false;
    }
    s->baud = baud;
    return true;
}

static inline feetech_driver_t ftsim_driver(ftsim_bus_t *s)
{
    feetech_driver_t d;

    d.ctx = s;
    d.start = ftsim_start;
    d.poll = ftsim_poll;
    d.set_baud = ftsim_set_baud;
    return d;
}

/* The servo now at @p id. */
static inline ftsim_servo_t *ftsim_find(ftsim_bus_t *s, uint8_t id)
{
    for (unsigned i = 0; i < FTSIM_SERVOS; ++i) {
        if (s->servo[i].present && s->servo[i].regs[FEETECH_REG_ID] == id) {
            return &s->servo[i];
        }
    }
    return NULL;
}

/* Whether a logged frame is a write of @p v to @p addr at @p id. */
static inline bool ftsim_is_write1(const feetech_frame_t *f, uint8_t id,
                                 uint8_t addr, uint8_t v)
{
    return f->n == 8u && f->bytes[2] == id && f->bytes[4] == FEETECH_WRITE
           && f->bytes[5] == addr && f->bytes[6] == v;
}

#endif /* FEETECH_SIM_H */
