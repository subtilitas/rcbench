/*
 * SPDX-License-Identifier: MIT
 */

#include "feetech_bus.h"

#include <string.h>

/* Who the transaction in flight is for. */
enum { FL_NONE = 0, FL_OP = 1, FL_WATCH = 2, FL_STOP = 3 };

/* What a stop still owes. */
#define STOP_TORQUE 0x01u
#define STOP_LOCK   0x02u
#define STOP_BAUD   0x04u

/* The steps of a WRITE_EE. */
enum { EE_UNLOCK = 0, EE_WRITE, EE_LOCK, EE_VERIFY };

/* No baud change. */
#define SAME_BAUD 0xFFu

/* No write to go back from. */
#define NO_OLD_ID 0xFFu

bool feetech_bus_init(feetech_bus_t *b, const feetech_driver_t *d,
                      uint32_t baud)
{
    if (b == NULL || d == NULL || d->start == NULL || d->poll == NULL
        || d->set_baud == NULL || baud == 0u) {
        return false;
    }
    memset(b, 0, sizeof(*b));
    b->drv = *d;
    b->baud = baud;
    b->watch_id = 0xFFu;
    b->rx_id = 0xFFu;
    b->watch_res = FEETECH_RES_OK;
    return true;
}

bool feetech_bus_busy(const feetech_bus_t *b)
{
    return b->op != FEETECH_OP_NONE || b->stop_due != 0u
           || b->flight == FL_STOP;
}

bool feetech_bus_on_wire(const feetech_bus_t *b)
{
    return b->flight != FL_NONE;
}

bool feetech_bus_found(const feetech_bus_t *b, uint8_t id)
{
    return id <= FEETECH_ID_MAX
           && (b->found[id / 8u] & (1u << (id % 8u))) != 0u;
}

feetech_res_t feetech_bus_set_baud(feetech_bus_t *b, uint32_t baud)
{
    if (feetech_bus_busy(b) || b->flight != FL_NONE) {
        return FEETECH_RES_BUSY;
    }
    if (baud == 0u) {
        return FEETECH_RES_ARG;
    }
    if (!b->drv.set_baud(b->drv.ctx, baud)) {
        return FEETECH_RES_DRIVER;
    }
    b->baud = baud;
    return FEETECH_RES_OK;
}

/* An operation is taken: the reply fields of the last one go. */
static feetech_res_t begin(feetech_bus_t *b, feetech_op_t op)
{
    b->op = (uint8_t)op;
    b->last_op = (uint8_t)op;
    b->step = 0;
    b->step_res = FEETECH_RES_OK;
    b->rx_id = 0xFFu;
    b->rx_error = 0;
    b->read_n = 0;
    return FEETECH_RES_OK;
}

feetech_res_t feetech_bus_scan(feetech_bus_t *b, uint8_t from, uint8_t to)
{
    if (feetech_bus_busy(b)) {
        return FEETECH_RES_BUSY;
    }
    if (to > FEETECH_ID_MAX || from > to) {
        return FEETECH_RES_ARG;
    }
    b->scan_from = from;
    b->scan_to = to;
    b->scan_at = from;
    b->found_n = 0;
    b->bad_n = 0;
    memset(b->found, 0, sizeof(b->found));
    return begin(b, FEETECH_OP_SCAN);
}

/* An ID, an address and a count a request takes. */
static bool span_ok(uint8_t id, uint8_t addr, uint8_t n)
{
    return id <= FEETECH_ID_MAX && n >= 1u && n <= FEETECH_DATA_MAX
           && (unsigned)addr + n <= 256u;
}

feetech_res_t feetech_bus_read(feetech_bus_t *b, uint8_t id, uint8_t addr,
                               uint8_t n)
{
    if (feetech_bus_busy(b)) {
        return FEETECH_RES_BUSY;
    }
    if (!span_ok(id, addr, n)) {
        return FEETECH_RES_ARG;
    }
    b->id = id;
    b->addr = addr;
    b->n = n;
    return begin(b, FEETECH_OP_READ);
}

feetech_res_t feetech_bus_write(feetech_bus_t *b, uint8_t id, uint8_t addr,
                                const uint8_t *data, uint8_t n)
{
    if (feetech_bus_busy(b)) {
        return FEETECH_RES_BUSY;
    }
    if (data == NULL || !span_ok(id, addr, n)) {
        return FEETECH_RES_ARG;
    }
    b->id = id;
    b->addr = addr;
    b->n = n;
    memcpy(b->data, data, n);
    return begin(b, FEETECH_OP_WRITE);
}

feetech_res_t feetech_bus_write_ee(feetech_bus_t *b, feetech_series_t series,
                                   uint8_t id, uint8_t addr,
                                   const uint8_t *data, uint8_t n)
{
    const uint8_t lock = feetech_lock_addr(series);
    uint8_t new_id = id;
    uint8_t new_baud = SAME_BAUD;

    if (feetech_bus_busy(b)) {
        return FEETECH_RES_BUSY;
    }
    if (data == NULL || lock == 0u || !span_ok(id, addr, n)) {
        return FEETECH_RES_ARG;
    }
    if (addr == lock) {
        if (n != 1u || data[0] > 1u) {
            return FEETECH_RES_ARG;
        }
    } else if ((unsigned)addr + n > FEETECH_RAM_FIRST) {
        return FEETECH_RES_ARG;
    } else {
        if (addr <= FEETECH_REG_ID && FEETECH_REG_ID < addr + n) {
            new_id = data[FEETECH_REG_ID - addr];
            if (new_id > FEETECH_ID_MAX) {
                return FEETECH_RES_ARG;
            }
        }
        if (addr <= FEETECH_REG_BAUD && FEETECH_REG_BAUD < addr + n) {
            new_baud = data[FEETECH_REG_BAUD - addr];
            if (new_baud >= FEETECH_BAUD_COUNT) {
                return FEETECH_RES_ARG;
            }
        }
    }
    b->id = id;
    b->addr = addr;
    b->n = n;
    b->series = (uint8_t)series;
    b->new_id = new_id;
    b->new_baud = new_baud;
    b->old_id = NO_OLD_ID;
    memcpy(b->data, data, n);
    (void)begin(b, FEETECH_OP_WRITE_EE);
    /* The lock flag alone is one write and its read. */
    b->step = addr == lock ? (uint8_t)EE_WRITE : (uint8_t)EE_UNLOCK;
    return FEETECH_RES_OK;
}

feetech_res_t feetech_bus_torque_off(feetech_bus_t *b)
{
    if (feetech_bus_busy(b)) {
        return FEETECH_RES_BUSY;
    }
    return begin(b, FEETECH_OP_TORQUE_OFF);
}

void feetech_bus_watch(feetech_bus_t *b, uint8_t id, feetech_series_t series)
{
    if (id > FEETECH_ID_MAX || feetech_live_bytes(series) == 0u) {
        b->watch_id = 0xFFu;
        return;
    }
    if (id != b->watch_id || (uint8_t)series != b->watch_series) {
        b->live_n = 0;
        b->watch_res = FEETECH_RES_OK;
        b->watch_error = 0;
    }
    b->watch_id = id;
    b->watch_series = (uint8_t)series;
    /* The first read at the next pass. */
    b->watch_now = 1;
}

void feetech_bus_stop(feetech_bus_t *b)
{
    b->stop_due = STOP_TORQUE | STOP_LOCK;
    b->stop_res = FEETECH_RES_OK;
    b->stop_tries = 0;
    /* An EEPROM write on the wire is finished by the driver: an ID or a
     * rate it moves is the servo's from its end, and the stop follows it.
     * The torque switch goes to 0 at the old rate and again at the new
     * one; the lock goes to the new ID. */
    if (b->op == FEETECH_OP_WRITE_EE && b->step == EE_WRITE
        && b->flight == FL_OP) {
        if (b->unlocked != 0u && b->unlocked_id == b->id) {
            b->unlocked_id = b->new_id;
        }
        if (b->new_baud != SAME_BAUD) {
            b->follow_baud = b->new_baud;
            b->stop_due |= STOP_BAUD;
        }
    }
    if (b->op != FEETECH_OP_NONE) {
        b->result = FEETECH_RES_ABORTED;
    }
    b->op = FEETECH_OP_NONE;
    b->watch_id = 0xFFu;
}

/* --------------------------------------------------------- transactions */

/* Hand the frame in b->tx to the driver: a reply of @p params parameters
 * from @p want_id, or none for @p params 0xFF. */
static bool send(feetech_bus_t *b, uint8_t who, uint8_t want_id,
                 uint8_t params, uint32_t now)
{
    const unsigned n_rx = params == 0xFFu
                              ? 0u : (unsigned)params + FEETECH_FRAME_OVERHEAD;
    const uint32_t wait = n_rx == 0u
        ? 0u : FEETECH_BUS_REPLY_US + 2u * feetech_bytes_us(b->baud, n_rx);

    if (b->tx.n == 0u
        || !b->drv.start(b->drv.ctx, b->tx.bytes, b->tx.n, n_rx, wait)) {
        return false;
    }
    b->flight = who;
    b->want_id = want_id;
    b->want_params = params;
    b->limit_us = now + feetech_bytes_us(b->baud, b->tx.n) + wait
                  + FEETECH_BUS_GRACE_US;
    return true;
}

/* What came of the transaction that ended, as a result. */
static feetech_res_t decode(feetech_bus_t *b, feetech_status_t *st)
{
    const unsigned skip = feetech_echo(b->cap.bytes, b->cap.n, b->tx.bytes,
                                       b->tx.n);

    if (b->want_params == 0xFFu) {
        return FEETECH_RES_OK;
    }
    switch (feetech_parse(&b->cap.bytes[skip], b->cap.n - skip,
                          b->cap.framing, b->want_id, b->want_params, st)) {
    case FEETECH_RX_OK:        return FEETECH_RES_OK;
    case FEETECH_RX_NONE:      return FEETECH_RES_NO_REPLY;
    case FEETECH_RX_TRUNCATED: return FEETECH_RES_TRUNCATED;
    case FEETECH_RX_CHECKSUM:  return FEETECH_RES_CHECKSUM;
    case FEETECH_RX_WRONG_ID:  return FEETECH_RES_WRONG_ID;
    case FEETECH_RX_LENGTH:    return FEETECH_RES_LENGTH;
    case FEETECH_RX_FRAMING:   return FEETECH_RES_FRAMING;
    default:                   return FEETECH_RES_NOISE;
    }
}

static void end_op(feetech_bus_t *b, feetech_res_t r)
{
    b->result = (uint8_t)r;
    b->op = FEETECH_OP_NONE;
}

/* The WRITE_EE's next step after a failure at @p r: the lock again, unless
 * the failure was the lock's. */
static void ee_failed(feetech_bus_t *b, feetech_res_t r)
{
    if (b->step_res == FEETECH_RES_OK) {
        b->step_res = (uint8_t)r;
    }
    b->step = EE_LOCK;
}

/* Neither a write that moves the ID or the rate nor the lock after it
 * answered at the new ID and rate: the write is taken as lost.  The line
 * goes back to the old rate and the lock to the old ID, and the operation
 * ends with the write's result.  A servo that moved and answered neither
 * stays unlocked at the new ID; not measured whether that happens. */
static void back_to_old(feetech_bus_t *b)
{
    if (b->step_res == FEETECH_RES_OK) {
        b->step_res = b->write_res;
    }
    if (b->old_baud != b->baud) {
        if (b->drv.set_baud(b->drv.ctx, b->old_baud)) {
            b->baud = b->old_baud;
        } else {
            b->step_res = FEETECH_RES_DRIVER;
        }
    }
    if (b->unlocked != 0u && b->unlocked_id == b->id) {
        b->unlocked_id = b->old_id;
    }
    b->id = b->old_id;
    b->old_id = NO_OLD_ID;
    b->step = EE_LOCK;
}

static void op_reply(feetech_bus_t *b, feetech_res_t r,
                     const feetech_status_t *st)
{
    if (r == FEETECH_RES_OK || r == FEETECH_RES_LENGTH
        || r == FEETECH_RES_WRONG_ID) {
        b->rx_id = st->id;
        b->rx_error = st->error;
    }
    switch ((feetech_op_t)b->op) {
    case FEETECH_OP_SCAN:
        if (r == FEETECH_RES_OK || r == FEETECH_RES_LENGTH) {
            b->found[b->scan_at / 8u] |= (uint8_t)(1u << (b->scan_at % 8u));
            ++b->found_n;
        } else if (r != FEETECH_RES_NO_REPLY && b->bad_n < 0xFFu) {
            ++b->bad_n;
        }
        if (b->scan_at >= b->scan_to) {
            end_op(b, FEETECH_RES_OK);
        } else {
            ++b->scan_at;
        }
        return;
    case FEETECH_OP_READ:
        if (r == FEETECH_RES_OK) {
            b->read_n = st->n_params;
            memcpy(b->read, st->params, st->n_params);
        }
        end_op(b, r);
        return;
    case FEETECH_OP_WRITE:
    case FEETECH_OP_TORQUE_OFF:
        end_op(b, r);
        return;
    case FEETECH_OP_WRITE_EE:
        break;
    default:
        return;
    }
    switch (b->step) {
    case EE_UNLOCK:
        if (r != FEETECH_RES_OK) {
            ee_failed(b, r);
        } else {
            b->step = EE_WRITE;
        }
        return;
    case EE_WRITE: {
        const bool moves = b->new_id != b->id || b->new_baud != SAME_BAUD;
        const bool lock_only = b->addr == feetech_lock_addr(
                                              (feetech_series_t)b->series);

        /* A write that moves the ID or the rate may be answered from the
         * new ID, or not at all: the read decides.  Not measured. */
        if (r != FEETECH_RES_OK && !moves) {
            if (lock_only) {
                end_op(b, r);
                return;
            }
            ee_failed(b, r);
            return;
        }
        /* Unanswered: the lock at the new ID and rate tells whether the
         * servo moved; if it does not answer either, the old ones get it. */
        b->old_id = r != FEETECH_RES_OK ? b->id : NO_OLD_ID;
        b->old_baud = b->baud;
        b->write_res = (uint8_t)r;
        if (b->new_baud != SAME_BAUD) {
            const uint32_t baud = feetech_baud(b->new_baud);

            if (b->drv.set_baud(b->drv.ctx, baud)) {
                b->baud = baud;
            } else if (b->step_res == FEETECH_RES_OK) {
                b->step_res = FEETECH_RES_DRIVER;
            }
        }
        if (b->unlocked != 0u && b->unlocked_id == b->id) {
            b->unlocked_id = b->new_id;
        }
        b->id = b->new_id;
        b->step = lock_only ? (uint8_t)EE_VERIFY : (uint8_t)EE_LOCK;
        return;
    }
    case EE_LOCK:
        if (r == FEETECH_RES_OK) {
            b->unlocked = 0;
        } else if (b->old_id <= FEETECH_ID_MAX) {
            back_to_old(b);
            return;
        } else if (b->step_res == FEETECH_RES_OK) {
            b->step_res = (uint8_t)r;
        }
        if (b->step_res != FEETECH_RES_OK) {
            end_op(b, (feetech_res_t)b->step_res);
        } else {
            b->step = EE_VERIFY;
        }
        return;
    default:
        if (r == FEETECH_RES_OK) {
            b->read_n = st->n_params;
            memcpy(b->read, st->params, st->n_params);
            r = memcmp(st->params, b->data, b->n) == 0 ? FEETECH_RES_OK
                                                       : FEETECH_RES_VERIFY;
        }
        end_op(b, r);
        return;
    }
}

/* The operation's next frame, or its end. */
static void op_next(feetech_bus_t *b, uint32_t now)
{
    const uint8_t zero = 0;
    const uint8_t one = 1;
    const feetech_series_t series = (feetech_series_t)b->series;
    bool ok = false;

    switch ((feetech_op_t)b->op) {
    case FEETECH_OP_SCAN:
        ok = feetech_ping(&b->tx, b->scan_at)
             && send(b, FL_OP, b->scan_at, 0, now);
        break;
    case FEETECH_OP_READ:
        ok = feetech_read(&b->tx, b->id, b->addr, b->n)
             && send(b, FL_OP, b->id, b->n, now);
        break;
    case FEETECH_OP_WRITE:
        ok = feetech_write(&b->tx, b->id, b->addr, b->data, b->n)
             && send(b, FL_OP, b->id, 0, now);
        break;
    case FEETECH_OP_TORQUE_OFF:
        ok = feetech_write(&b->tx, FEETECH_ID_BROADCAST, FEETECH_REG_TORQUE,
                           &zero, 1u)
             && send(b, FL_OP, FEETECH_ID_BROADCAST, 0xFFu, now);
        break;
    case FEETECH_OP_WRITE_EE:
        if (b->step == EE_UNLOCK) {
            ok = feetech_write(&b->tx, b->id, feetech_lock_addr(series),
                               &zero, 1u)
                 && send(b, FL_OP, b->id, 0, now);
            if (ok) {
                /* Unlocked from the frame on: the reply may be lost. */
                b->unlocked = 1;
                b->unlocked_id = b->id;
                b->unlocked_series = b->series;
            }
        } else if (b->step == EE_WRITE) {
            ok = feetech_write(&b->tx, b->id, b->addr, b->data, b->n)
                 && send(b, FL_OP, b->id, 0, now);
        } else if (b->step == EE_LOCK) {
            ok = feetech_write(&b->tx, b->id, feetech_lock_addr(series),
                               &one, 1u)
                 && send(b, FL_OP, b->id, 0, now);
        } else {
            ok = feetech_read(&b->tx, b->id, b->addr, b->n)
                 && send(b, FL_OP, b->id, b->n, now);
        }
        break;
    default:
        return;
    }
    if (!ok) {
        end_op(b, FEETECH_RES_DRIVER);
    }
}

/* One frame of the stop, in b->tx when @p built: true when it is on the
 * wire, or when the driver refused it FEETECH_BUS_STOP_TRIES times and the
 * stop goes on without it; false to try again at the next pass. */
static bool stop_frame(feetech_bus_t *b, bool built, uint8_t want_id,
                       uint8_t params, uint32_t now)
{
    if (built && send(b, FL_STOP, want_id, params, now)) {
        b->stop_tries = 0;
        return true;
    }
    if (++b->stop_tries < FEETECH_BUS_STOP_TRIES) {
        return false;
    }
    b->stop_tries = 0;
    b->stop_res = FEETECH_RES_DRIVER;
    return true;
}

/* The stop's next frame: torque off, the rate an EEPROM write moved and
 * torque off again, then the lock where it is owed. */
static void stop_next(feetech_bus_t *b, uint32_t now)
{
    const uint8_t zero = 0;
    const uint8_t one = 1;

    if ((b->stop_due & STOP_TORQUE) == 0u
        && (b->stop_due & STOP_BAUD) != 0u) {
        const uint32_t baud = feetech_baud(b->follow_baud);

        b->stop_due &= (uint8_t)~STOP_BAUD;
        if (b->drv.set_baud(b->drv.ctx, baud)) {
            b->baud = baud;
            b->stop_due |= STOP_TORQUE;
        } else {
            b->stop_res = FEETECH_RES_DRIVER;
        }
    }
    if ((b->stop_due & STOP_TORQUE) != 0u) {
        if (stop_frame(b,
                       feetech_write(&b->tx, FEETECH_ID_BROADCAST,
                                     FEETECH_REG_TORQUE, &zero, 1u),
                       FEETECH_ID_BROADCAST, 0xFFu, now)) {
            b->stop_due &= (uint8_t)~STOP_TORQUE;
        }
        return;
    }
    if (b->unlocked != 0u
        && !stop_frame(b,
                       feetech_write(&b->tx, b->unlocked_id,
                                     feetech_lock_addr((feetech_series_t)
                                                           b->unlocked_series),
                                     &one, 1u),
                       b->unlocked_id, 0, now)) {
        return;
    }
    /* A lock that never went out leaves the flag owed: bit 4 of STATE. */
    if (b->flight == FL_STOP) {
        b->unlocked = 0;
    }
    b->stop_due = 0;
}

static void watch_next(feetech_bus_t *b, uint32_t now)
{
    const uint8_t n = feetech_live_bytes((feetech_series_t)b->watch_series);

    b->due_us = now + FEETECH_BUS_WATCH_US;
    b->watch_now = 0;
    if (!feetech_read(&b->tx, b->watch_id, FEETECH_REG_PRESENT, n)
        || !send(b, FL_WATCH, b->watch_id, n, now)) {
        b->watch_res = FEETECH_RES_DRIVER;
        b->live_n = 0;
        ++b->watch_bad;
    }
}

static void watch_reply(feetech_bus_t *b, feetech_res_t r,
                        const feetech_status_t *st)
{
    if (b->watch_id > FEETECH_ID_MAX) {
        return;
    }
    b->watch_res = (uint8_t)r;
    if (r == FEETECH_RES_OK) {
        b->watch_error = st->error;
        b->live_n = st->n_params;
        memcpy(b->live, st->params, st->n_params);
        ++b->watch_ok;
    } else {
        /* A block that is not this read's is no reading. */
        b->live_n = 0;
        ++b->watch_bad;
    }
}

feetech_res_t feetech_bus_step(feetech_bus_t *b, uint32_t now_us)
{
    if (b->flight != FL_NONE) {
        const uint8_t who = b->flight;
        feetech_status_t st;
        feetech_res_t r;

        memset(&st, 0, sizeof(st));
        if (b->drv.poll(b->drv.ctx, &b->cap)) {
            r = decode(b, &st);
        } else if ((int32_t)(now_us - b->limit_us) > 0) {
            r = FEETECH_RES_DRIVER;
        } else {
            return feetech_bus_busy(b) ? FEETECH_RES_BUSY
                                       : (feetech_res_t)b->result;
        }
        b->flight = FL_NONE;
        b->t_us = now_us;
        b->timed = 1;
        if (who == FL_OP && b->op != FEETECH_OP_NONE) {
            op_reply(b, r, &st);
        } else if (who == FL_WATCH) {
            watch_reply(b, r, &st);
        }
    }
    if (b->timed != 0u && now_us - b->t_us < FEETECH_BUS_GAP_US) {
        return feetech_bus_busy(b) ? FEETECH_RES_BUSY
                                   : (feetech_res_t)b->result;
    }
    if (b->stop_due != 0u) {
        stop_next(b, now_us);
    } else if (b->op != FEETECH_OP_NONE) {
        op_next(b, now_us);
    } else if (b->watch_id <= FEETECH_ID_MAX
               && (b->watch_now != 0u
                   || (int32_t)(now_us - b->due_us) >= 0)) {
        watch_next(b, now_us);
    }
    if (b->flight != FL_NONE) {
        b->t_us = now_us;
        b->timed = 1;
    }
    return feetech_bus_busy(b) ? FEETECH_RES_BUSY : (feetech_res_t)b->result;
}
