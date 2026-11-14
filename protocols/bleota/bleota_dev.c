/**
 * @file
 * @brief The device's side of a bleota transfer (bleota.h): BEGIN's checks,
 *        the confirmation, the buffer and its credit, END's compare and the
 *        timeouts.
 *
 * SPDX-License-Identifier: MIT
 */
#include "bleota.h"

#include <string.h>

/** @brief No skipped chunk answered at this rx. */
#define NAK_NONE 0xFFFFFFFFu

/**
 * @brief Queue a notification; a full queue drops the oldest.
 * @param d the device
 * @param s the notification
 */
static void notify(bleota_dev_t *d, const bleota_status_t *s)
{
    if (d->out_n == BLEOTA_DEV_OUT) {
        d->out_head = (uint8_t)((d->out_head + 1u) % BLEOTA_DEV_OUT);
        --d->out_n;
    }
    const uint8_t i = (uint8_t)((d->out_head + d->out_n) % BLEOTA_DEV_OUT);
    (void)bleota_status_encode(s, d->out[i]);
    ++d->out_n;
}

/**
 * @brief The limit the device gives: BLEOTA_WINDOW past the last written
 *        byte, at most the image's size.
 * @param d the device
 * @return the limit
 */
static uint32_t limit_of(const bleota_dev_t *d)
{
    const uint32_t l = d->wr + BLEOTA_WINDOW;
    return l > d->size ? d->size : l;
}

/**
 * @brief Notify CREDIT with the next offset and the limit.
 * @param d   the device
 * @param gap BLEOTA_CREDIT_GAP: bytes from next on did not arrive, and the
 *            central sends again from next; BLEOTA_CREDIT_ROUTINE otherwise
 */
static void credit(bleota_dev_t *d, uint8_t gap)
{
    bleota_status_t s;
    memset(&s, 0, sizeof s);
    s.ev = BLEOTA_EV_CREDIT;
    s.code = gap;
    s.next = d->rx;
    s.limit = limit_of(d);
    d->told = s.limit;
    notify(d, &s);
}

/**
 * @brief Refuse or end the transfer: notify FAIL, drop an open slot, keep
 *        the slot the device runs.
 * @param d    the device
 * @param code BLEOTA_E_*
 */
static void fail(bleota_dev_t *d, uint8_t code)
{
    bleota_status_t s;
    memset(&s, 0, sizeof s);
    s.ev = BLEOTA_EV_FAIL;
    s.code = code;
    s.next = d->rx;
    notify(d, &s);
    d->code = code;
    d->st = BLEOTA_DEV_IDLE;
    d->close_due = d->open;
}

bool bleota_dev_init(bleota_dev_t *d, const bleota_dev_cfg_t *cfg,
                     uint8_t *ring, uint32_t ring_size)
{
    if (d == NULL || cfg == NULL || ring == NULL || ring_size < BLEOTA_WINDOW
        || (ring_size & (ring_size - 1u)) != 0u || cfg->kind == 0u
        || cfg->kind > BLEOTA_KIND_MAX) {
        return false;
    }
    memset(d, 0, sizeof *d);
    d->cfg = *cfg;
    d->ring = ring;
    d->ring_size = ring_size;
    d->nak_at = NAK_NONE;
    return true;
}

/**
 * @brief The code BEGIN is refused with, or BLEOTA_OK.
 * @param d the device
 * @param c the BEGIN
 * @return BLEOTA_E_* or BLEOTA_OK
 */
static uint8_t begin_refusal(const bleota_dev_t *d, const bleota_ctrl_t *c)
{
    if (c->proto != BLEOTA_PROTO || (c->flags & (uint8_t)~BLEOTA_F_ALL) != 0u) {
        return BLEOTA_E_PROTO;
    }
    if (c->kind != d->cfg.kind) {
        return BLEOTA_E_KIND;
    }
    if (c->size == 0u || c->size > d->cfg.slot) {
        return BLEOTA_E_SIZE;
    }
    if ((c->flags & BLEOTA_F_REINSTALL) == 0u
        && memcmp(c->ver, d->cfg.ver, sizeof c->ver) == 0) {
        return BLEOTA_E_SAME;
    }
    return BLEOTA_OK;
}

/**
 * @brief BEGIN: checked before anything else happens.
 * @param d   the device
 * @param c   the write
 * @param now the clock
 */
static void begin(bleota_dev_t *d, const bleota_ctrl_t *c, uint32_t now)
{
    if (d->st != BLEOTA_DEV_IDLE || d->close_due || d->opening) {
        bleota_status_t s;
        memset(&s, 0, sizeof s);
        s.ev = BLEOTA_EV_FAIL;
        s.code = BLEOTA_E_BUSY;
        notify(d, &s);
        return;
    }
    const uint8_t why = begin_refusal(d, c);
    if (why != BLEOTA_OK) {
        fail(d, why);
        return;
    }
    d->size = c->size;
    d->flags = c->flags;
    memcpy(d->ver, c->ver, sizeof d->ver);
    d->halves = 0u;
    d->rx = d->wr = d->taken = d->told = 0u;
    d->nak_at = NAK_NONE;
    d->end_asked = false;
    d->head_ok = false;
    d->code = BLEOTA_OK;
    d->st = BLEOTA_DEV_HASH;
    d->t = now;
}

/**
 * @brief Open the confirmation window.  A key already down must be let go
 *        first, so a stuck key confirms nothing.
 * @param d   the device
 * @param now the clock
 */
static void ask_confirm(bleota_dev_t *d, uint32_t now)
{
    bleota_status_t s;
    memset(&s, 0, sizeof s);
    d->st = BLEOTA_DEV_CONFIRM;
    d->t = now;
    d->key_armed = !d->key_down;
    s.ev = BLEOTA_EV_WAIT;
    s.secs = (uint16_t)(BLEOTA_CONFIRM_MS / 1000u);
    notify(d, &s);
}

/**
 * @brief END: every byte taken; the compare waits for the last write.
 * @param d the device
 */
static void end(bleota_dev_t *d)
{
    if (d->st != BLEOTA_DEV_RECV) {
        fail(d, BLEOTA_E_STATE);
        return;
    }
    if (d->rx != d->size) {
        fail(d, BLEOTA_E_SHORT);
        return;
    }
    d->end_asked = true;
}

void bleota_dev_ctrl(bleota_dev_t *d, const uint8_t *b, size_t n,
                     uint32_t now)
{
    bleota_ctrl_t c;
    if (d == NULL || !bleota_ctrl_decode(b, n, &c)) {
        return;
    }
    switch (c.op) {
    case BLEOTA_OP_BEGIN:
        begin(d, &c, now);
        break;
    case BLEOTA_OP_HASH:
        if (d->st != BLEOTA_DEV_HASH) {
            fail(d, BLEOTA_E_STATE);
            break;
        }
        memcpy(d->sha + (size_t)16u * c.part, c.half, 16u);
        d->halves |= (uint8_t)(1u << c.part);
        if (d->halves == 3u) {
            ask_confirm(d, now);
        }
        break;
    case BLEOTA_OP_END:
        end(d);
        d->t = now;
        break;
    case BLEOTA_OP_ABORT:
        /* An install under way may have set the boot slot already: its
         * result decides, not the ABORT. */
        if (d->st != BLEOTA_DEV_IDLE && d->st != BLEOTA_DEV_DONE
            && d->st != BLEOTA_DEV_INSTALL) {
            fail(d, BLEOTA_E_ABORTED);
        }
        break;
    case BLEOTA_OP_POLL:
        /* The central polls after a second without a CREDIT: bytes it was
         * allowed to send and that have not come by then are lost. */
        if (d->st == BLEOTA_DEV_RECV) {
            credit(d, d->rx < d->told ? BLEOTA_CREDIT_GAP
                                      : BLEOTA_CREDIT_ROUTINE);
            d->t = now;
        }
        break;
    default:
        break;
    }
}

void bleota_dev_data(bleota_dev_t *d, const uint8_t *b, size_t n,
                     uint32_t now)
{
    uint32_t off;
    const uint8_t *p;
    uint16_t len;
    if (d == NULL || d->st != BLEOTA_DEV_RECV || d->end_asked
        || !bleota_chunk_decode(b, n, &off, &p, &len)) {
        return;
    }
    if (off > d->rx) {
        /* A chunk before this one was lost: answer once per offset. */
        ++d->gaps;
        if (d->nak_at != d->rx) {
            d->nak_at = d->rx;
            credit(d, BLEOTA_CREDIT_GAP);
        }
        return;
    }
    const uint32_t skip = d->rx - off;
    if (skip >= len) {
        ++d->dups;
        return;
    }
    const uint32_t take = len - skip;
    if (d->rx + take > limit_of(d)) {
        ++d->over;
        return;
    }
    for (uint32_t i = 0; i < take; ++i) {
        d->ring[(d->rx + i) & (d->ring_size - 1u)] = p[skip + i];
    }
    d->rx += take;
    d->nak_at = NAK_NONE;
    ++d->chunks;
    d->t = now;
    if (d->rx == d->size) {
        credit(d, BLEOTA_CREDIT_ROUTINE); /* every byte taken: END is due */
    }
}

void bleota_dev_key(bleota_dev_t *d, bool down, uint32_t now)
{
    if (d == NULL) {
        return;
    }
    const bool was = d->key_down;
    d->key_down = down;
    if (d->st != BLEOTA_DEV_CONFIRM) {
        return;
    }
    if (!down) {
        if (was && d->key_armed && d->cfg.confirm == BLEOTA_CONFIRM_PRESS
            && now - d->key_t < BLEOTA_HOLD_MS) {
            d->st = BLEOTA_DEV_OPEN;
            d->t = now;
        }
        d->key_armed = true;
        return;
    }
    if (!was) {
        d->key_t = now;
        return;
    }
    if (!d->key_armed || now - d->key_t < BLEOTA_HOLD_MS) {
        return;
    }
    if (d->cfg.confirm == BLEOTA_CONFIRM_HOLD) {
        d->st = BLEOTA_DEV_OPEN;
        d->t = now;
    } else {
        d->key_armed = false;
        fail(d, BLEOTA_E_REFUSED);
    }
}

void bleota_dev_lost(bleota_dev_t *d)
{
    if (d == NULL) {
        return;
    }
    if (d->st == BLEOTA_DEV_DONE || d->st == BLEOTA_DEV_INSTALL) {
        /* Installed, or the install under way decides: nobody is left
         * to tell. */
        d->out_n = 0u;
        return;
    }
    if (d->st != BLEOTA_DEV_IDLE) {
        d->code = BLEOTA_E_ABORTED;
    }
    d->st = BLEOTA_DEV_IDLE;
    d->close_due = d->open;
    d->out_n = 0u;
}

bleota_do_t bleota_dev_poll(bleota_dev_t *d, uint32_t now)
{
    if (d == NULL) {
        return BLEOTA_DO_NONE;
    }
    if (d->opening) {
        return BLEOTA_DO_NONE; /* the open under way reports first */
    }
    if (d->close_due) {
        return BLEOTA_DO_CLOSE;
    }
    const uint32_t age = now - d->t;
    switch (d->st) {
    case BLEOTA_DEV_HASH:
        if (age >= BLEOTA_HASH_MS) {
            fail(d, BLEOTA_E_TIMEOUT);
        }
        return BLEOTA_DO_NONE;
    case BLEOTA_DEV_CONFIRM:
        if (age >= BLEOTA_CONFIRM_MS) {
            fail(d, BLEOTA_E_NO_CONFIRM);
        }
        return BLEOTA_DO_NONE;
    case BLEOTA_DEV_OPEN:
        /* The slot counts as open from here on: a fail while the caller
         * opens it leaves a CLOSE due, and BEGIN stays refused until
         * bleota_dev_opened() settles it. */
        d->opening = true;
        d->open = true;
        return BLEOTA_DO_OPEN;
    case BLEOTA_DEV_RECV:
        if (!d->head_ok && (d->rx >= BLEOTA_HEAD_LEN || d->rx == d->size)) {
            /* Nothing is written before the head names this device. */
            const uint8_t why = bleota_head_check(d->ring, d->rx,
                                                  d->cfg.chip, d->cfg.kind);
            if (why != BLEOTA_OK) {
                fail(d, why);
                return d->close_due ? BLEOTA_DO_CLOSE : BLEOTA_DO_NONE;
            }
            d->head_ok = true;
        }
        if (d->head_ok && d->rx != d->wr) {
            return BLEOTA_DO_WRITE;
        }
        if (d->end_asked && d->wr == d->size) {
            d->st = BLEOTA_DEV_VERIFY;
            return BLEOTA_DO_VERIFY;
        }
        if (age >= BLEOTA_IDLE_MS) {
            fail(d, BLEOTA_E_TIMEOUT);
            return d->close_due ? BLEOTA_DO_CLOSE : BLEOTA_DO_NONE;
        }
        return BLEOTA_DO_NONE;
    case BLEOTA_DEV_VERIFY:
        return BLEOTA_DO_VERIFY;
    case BLEOTA_DEV_INSTALL:
        return BLEOTA_DO_INSTALL;
    case BLEOTA_DEV_DONE:
        return BLEOTA_DO_RESTART;
    case BLEOTA_DEV_IDLE:
    default:
        return BLEOTA_DO_NONE;
    }
}

void bleota_dev_opened(bleota_dev_t *d, bool ok, uint32_t now)
{
    if (d == NULL || !d->opening) {
        return;
    }
    d->opening = false;
    if (!ok) {
        /* Nothing opened: nothing to drop. */
        d->open = false;
        if (d->st == BLEOTA_DEV_OPEN) {
            fail(d, BLEOTA_E_FLASH);
        } else {
            d->close_due = false;
        }
        return;
    }
    if (d->st != BLEOTA_DEV_OPEN) {
        return; /* ended while it opened: its fail left the CLOSE due */
    }
    d->st = BLEOTA_DEV_RECV;
    d->t = now;
    credit(d, BLEOTA_CREDIT_ROUTINE);
}

uint32_t bleota_dev_take(bleota_dev_t *d, uint8_t *out, uint32_t max)
{
    if (d == NULL || out == NULL || d->st != BLEOTA_DEV_RECV || !d->head_ok) {
        return 0u;
    }
    uint32_t n = d->rx - d->wr;
    if (n > max) {
        n = max;
    }
    for (uint32_t i = 0; i < n; ++i) {
        out[i] = d->ring[(d->wr + i) & (d->ring_size - 1u)];
    }
    d->taken = n;
    return n;
}

void bleota_dev_written(bleota_dev_t *d, bool ok, uint32_t now)
{
    if (d == NULL || d->st != BLEOTA_DEV_RECV) {
        return;
    }
    if (!ok) {
        d->taken = 0u;
        fail(d, BLEOTA_E_FLASH);
        return;
    }
    d->wr += d->taken;
    d->taken = 0u;
    (void)now;
    const uint32_t l = limit_of(d);
    if (l - d->told >= BLEOTA_CREDIT_STEP
        || (l == d->size && d->told != d->size)) {
        credit(d, BLEOTA_CREDIT_ROUTINE);
    }
}

bool bleota_dev_verify(bleota_dev_t *d, const uint8_t digest[32],
                       uint32_t now)
{
    if (d == NULL || digest == NULL || d->st != BLEOTA_DEV_VERIFY) {
        return false;
    }
    d->t = now;
    if (memcmp(digest, d->sha, sizeof d->sha) != 0) {
        fail(d, BLEOTA_E_SHA);
        return false;
    }
    d->st = BLEOTA_DEV_INSTALL;
    return true;
}

void bleota_dev_installed(bleota_dev_t *d, bool ok, uint8_t code,
                          uint32_t now)
{
    if (d == NULL || d->st != BLEOTA_DEV_INSTALL) {
        return;
    }
    d->t = now;
    /* The image is closed either way: installed, or refused by the
     * device's own check, which drops it. */
    d->open = false;
    if (!ok) {
        fail(d, code == BLEOTA_E_IMAGE ? BLEOTA_E_IMAGE : BLEOTA_E_FLASH);
        return;
    }
    bleota_status_t s;
    memset(&s, 0, sizeof s);
    s.ev = BLEOTA_EV_DONE;
    s.next = d->size;
    notify(d, &s);
    d->st = BLEOTA_DEV_DONE;
}

void bleota_dev_closed(bleota_dev_t *d)
{
    if (d != NULL) {
        d->open = false;
        d->close_due = false;
    }
}

bool bleota_dev_status(bleota_dev_t *d, uint8_t *out)
{
    if (d == NULL || out == NULL || d->out_n == 0u) {
        return false;
    }
    memcpy(out, d->out[d->out_head], BLEOTA_LEN_STATUS);
    d->out_head = (uint8_t)((d->out_head + 1u) % BLEOTA_DEV_OUT);
    --d->out_n;
    return true;
}

uint16_t bleota_dev_secs(const bleota_dev_t *d, uint32_t now)
{
    if (d == NULL || d->st != BLEOTA_DEV_CONFIRM) {
        return 0u;
    }
    const uint32_t age = now - d->t;
    if (age >= BLEOTA_CONFIRM_MS) {
        return 0u;
    }
    return (uint16_t)((BLEOTA_CONFIRM_MS - age + 999u) / 1000u);
}

unsigned bleota_dev_percent(const bleota_dev_t *d)
{
    if (d == NULL || d->size == 0u) {
        return 0u;
    }
    return (unsigned)(((uint64_t)d->rx * 100u) / d->size);
}
