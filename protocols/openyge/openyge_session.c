/**
 * @file openyge_session.c
 * @brief The session on one OpenYGE ESC (electronic speed controller): the
 *        states of openyge_session.h, the request clock, tries, and the
 *        compare steps of a write.
 *
 * SPDX-License-Identifier: MIT
 */
#include "openyge_session.h"

#include <string.h>

#include "openyge_map.h"

/** @brief Microseconds a millisecond. */
#define US_PER_MS 1000u
/** @brief Bytes taken from the driver per call. */
#define RX_CHUNK 32u
/** @brief Calls to the driver's rx() per step at most: 256 bytes, more than
 *         the 3.99ms of a poll carries. */
#define RX_CALLS 8u
/** @brief Offset of `pidx` in the telemetry payload (docs/OpenYGE.md 4). */
#define PAY_PIDX 22u
/** @brief Offset of `pdata` in the telemetry payload. */
#define PAY_PDATA 24u
/** @brief Bytes of a request frame. */
#define REQ_BYTES (OPENYGE_HDR_V3 + OPENYGE_CONTROL_BYTES + OPENYGE_CRC_BYTES)

/**
 * @brief The time between two clock readings, across the clock's wrap.
 * @param now  the later reading, us
 * @param then the earlier reading, us
 * @return the difference, us
 */
static uint32_t since(uint32_t now, uint32_t then)
{
    return now - then;
}

/**
 * @brief Little-endian 16-bit field.
 * @param p two bytes, least significant first
 * @return the value
 */
static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

/**
 * @brief Whether a state reads the table.
 * @param st an oy_state_t
 * @return true for READ, RECHECK and VERIFY
 */
static bool reading(uint8_t st)
{
    return st == OY_ST_READ || st == OY_ST_RECHECK || st == OY_ST_VERIFY;
}

/**
 * @brief End the running operation.  OY_OK keeps the ESC; any other result
 *        ends the session.
 * @param s the session
 * @param r the result
 */
static void finish(oy_session_t *s, oy_res_t r)
{
    s->result   = r;
    s->awaiting = false;
    openyge_params_end_writes(&s->params);
    oy_guard_done(&s->guard);
    s->wrote = false;
    s->state = (r == OY_OK) ? OY_ST_READY : OY_ST_IDLE;
}

/**
 * @brief End the operation on a failure: once a write frame went out the
 *        ESC's table is not known, so every failure but the guard's is
 *        OY_PARTIAL.
 * @param s the session
 * @param r the result before a write
 */
static void fail(oy_session_t *s, oy_res_t r)
{
    finish(s, (s->wrote && r != OY_GUARD) ? OY_PARTIAL : r);
}

/**
 * @brief The first index from @p from on that the target changes.
 * @param s    the session
 * @param from the first index to look at
 * @return the index, or @c count when none is left
 */
static uint16_t next_changed(const oy_session_t *s, uint16_t from)
{
    uint16_t i = from;

    while (i < s->count && s->target[i] == s->orig[i]) {
        ++i;
    }
    return i;
}

/**
 * @brief Start reading the whole table in state @p st.
 * @param s   the session
 * @param st  OY_ST_READ, OY_ST_RECHECK or OY_ST_VERIFY
 * @param now the clock, us
 */
static void start_read(oy_session_t *s, uint8_t st, uint32_t now)
{
    openyge_params_reset(&s->params);
    s->state = st;
    s->t_op  = now;
}

/**
 * @brief Send the next frame of the state: a write of @c widx in WRITE,
 *        else a telemetry request.  It passes the guard first.
 * @param s   the session
 * @param now the clock, us
 */
static void send(oy_session_t *s, uint32_t now)
{
    uint8_t tx[REQ_BYTES];
    const uint8_t seq = (uint8_t)(s->seq + 1u);
    const bool write  = (s->state == OY_ST_WRITE);
    const size_t n = write
        ? openyge_build_param_write(tx, sizeof(tx), s->addr, seq, s->widx,
                                    s->target[s->widx])
        : openyge_build_telemetry_request(tx, sizeof(tx), s->addr, seq);
    const oy_ref_t r = oy_guard_frame(&s->guard, tx, n);

    if (r != OY_REF_NONE) {
        s->refuse    = (uint8_t)r;
        s->bad_index = s->widx;
        fail(s, OY_GUARD);
        return;
    }
    s->seq      = seq;
    s->awaiting = true;
    s->t_req    = now;
    ++s->tries;
    if (write) {
        s->wrote = true;   /* handed to the driver: it may be on the wire */
    }
    /* A driver that cannot send gets no reply; the timeout counts the try. */
    (void)s->d.tx(s->d.ctx, tx, n);
}

/**
 * @brief A whole table arrived: keep it as the read copy, compare it before
 *        a write, or judge the write by it.
 * @param s the session
 */
static void table_done(oy_session_t *s)
{
    const uint16_t n = s->params.count;
    const uint16_t *t = s->params.value;
    const size_t bytes = (size_t)n * sizeof(t[0]);

    if (s->state == OY_ST_RECHECK) {
        if (n != s->count || memcmp(t, s->orig, bytes) != 0) {
            finish(s, OY_CHANGED);
            return;
        }
        openyge_params_begin_writes(&s->params);
        s->widx  = next_changed(s, 0u);
        s->state = OY_ST_WRITE;
        return;
    }
    oy_res_t r = OY_OK;
    if (s->state == OY_ST_VERIFY) {
        if (n == s->count && memcmp(t, s->target, bytes) == 0) {
            r = OY_OK;
        } else if (n == s->count && memcmp(t, s->orig, bytes) == 0) {
            r = OY_NOT_TAKEN;
        } else {
            r = OY_PARTIAL;
        }
    }
    /* Any whole table read becomes the read copy and a new read number. */
    memset(s->orig, 0, sizeof(s->orig));
    memcpy(s->orig, t, bytes);
    s->count = n;
    oy_guard_table(&s->guard, s->orig, n, s->legacy);
    finish(s, r);
}

/**
 * @brief Take the (index, value) pair of a telemetry frame into the table.
 * @param s the session
 */
static void observe(oy_session_t *s)
{
    const uint16_t pidx  = rd16(s->fr.payload + PAY_PIDX);
    const uint16_t pdata = rd16(s->fr.payload + PAY_PDATA);

    if (pidx == OPENYGE_P_COUNT
        && (pdata == 0u || pdata > OPENYGE_MAX_PARAMS)) {
        fail(s, OY_NO_TABLE);   /* a table this session cannot hold */
        return;
    }
    openyge_params_observe(&s->params, pidx, pdata);
    if (openyge_params_complete(&s->params)) {
        table_done(s);
    }
}

/**
 * @brief A reply to the outstanding frame came.
 * @param s   the session
 * @param now the clock, us
 */
static void answered(oy_session_t *s, uint32_t now)
{
    s->awaiting = false;
    s->tries    = 0;
    s->t_reply  = now;

    switch (s->state) {
    case OY_ST_TAKE:
        finish(s, OY_OK);
        break;
    case OY_ST_WRITE:
        s->widx = next_changed(s, (uint16_t)(s->widx + 1u));
        if (s->widx >= s->count) {
            start_read(s, OY_ST_VERIFY, now);
        }
        break;
    case OY_ST_READ:
    case OY_ST_RECHECK:
    case OY_ST_VERIFY:
        observe(s);
        break;
    default:
        break;   /* READY: a request between operations */
    }
}

/**
 * @brief Judge one decoded frame.
 * @param s   the session
 * @param now the clock, us
 */
static void on_frame(oy_session_t *s, uint32_t now)
{
    const openyge_frame_t *f = &s->fr;

    if (!openyge_frame_has_telemetry(f)) {
        ++s->dropped;   /* not from an ESC */
        return;
    }
    if (s->state == OY_ST_LISTEN) {
        if (f->hdr.legacy) {
            s->legacy = true;   /* no sequence, no address: listen only */
            finish(s, OY_OK);
            return;
        }
        s->addr = (uint8_t)(f->hdr.device & OPENYGE_DEV_ADDR_MASK);
        if (s->addr == OPENYGE_DEV_BROADCAST) {
            s->addr = OY_ADDR_DEFAULT;
        }
        s->state   = OY_ST_TAKE;
        s->t_reply = now;
        return;
    }
    if (s->legacy) {
        if (f->hdr.legacy && s->state == OY_ST_READ) {
            observe(s);
        } else {
            ++s->dropped;
        }
        return;
    }
    const uint8_t want = (s->state == OY_ST_WRITE)
                             ? OPENYGE_FT_WRITE_PARAM_RESP
                             : OPENYGE_FT_TELE_RESP;
    if (f->hdr.legacy || !s->awaiting || f->hdr.seq != s->seq
        || f->hdr.type != want) {
        ++s->dropped;   /* auto frames, late replies, other sequences */
        return;
    }
    answered(s, now);
}

/**
 * @brief Hand the driver's received bytes to the decoder; dropped in WAIT.
 * @param s   the session
 * @param now the clock, us
 */
static void drain(oy_session_t *s, uint32_t now)
{
    uint8_t buf[RX_CHUNK];

    for (unsigned call = 0; call < RX_CALLS; ++call) {
        size_t n = s->d.rx(s->d.ctx, buf, sizeof(buf));
        if (n == 0u) {
            return;
        }
        if (n > sizeof(buf)) {
            n = sizeof(buf);
        }
        for (size_t i = 0; i < n && s->state != OY_ST_WAIT; ++i) {
            if (openyge_decode_byte(&s->dec, buf[i], &s->fr)) {
                on_frame(s, now);
                if (s->state == OY_ST_IDLE) {
                    return;
                }
            }
        }
    }
}

/**
 * @brief The outstanding frame went unanswered OY_TRIES times.
 * @param s   the session
 * @param now the clock, us
 */
static void missed(oy_session_t *s, uint32_t now)
{
    s->tries = 0;
    if (s->state == OY_ST_WRITE) {
        /* No further write: read what the ESC holds now. */
        start_read(s, OY_ST_VERIFY, now);
        s->t_reply = now - OY_REQ_PERIOD_MS * US_PER_MS;
        return;
    }
    fail(s, OY_SILENT);
}

/**
 * @brief The waits: boot, listen, the table cap, the reply timeout and the
 *        request period.
 * @param s   the session
 * @param now the clock, us
 */
static void timers(oy_session_t *s, uint32_t now)
{
    if (s->state == OY_ST_WAIT) {
        if (since(now, s->t_op) >= s->wait_us) {
            openyge_decoder_reset(&s->dec);
            s->state = OY_ST_LISTEN;
            s->t_op  = now;
        }
        return;
    }
    if (s->state == OY_ST_LISTEN) {
        if (since(now, s->t_op) < OY_LISTEN_MS * US_PER_MS) {
            return;
        }
        s->addr    = OY_ADDR_DEFAULT;   /* no v3 frame: request at once */
        s->state   = OY_ST_TAKE;
        s->t_reply = now - OY_REQ_PERIOD_MS * US_PER_MS;
    }
    if (reading(s->state)
        && since(now, s->t_op) >= OY_TABLE_CAP_MS * US_PER_MS) {
        fail(s, OY_NO_TABLE);
        return;
    }
    if (s->legacy) {
        return;   /* nothing is ever sent to a legacy ESC */
    }
    if (s->awaiting) {
        const uint32_t ms = (s->state == OY_ST_WRITE) ? OY_WRITE_TIMEOUT_MS
                                                      : OY_READ_TIMEOUT_MS;
        if (since(now, s->t_req) < ms * US_PER_MS) {
            return;
        }
        s->awaiting = false;
        ++s->misses;
        if (s->tries >= OY_TRIES) {
            missed(s, now);
            return;
        }
        send(s, now);   /* again, with a new sequence number */
        return;
    }
    if (since(now, s->t_reply) >= OY_REQ_PERIOD_MS * US_PER_MS) {
        send(s, now);
    }
}

oy_res_t oy_session_init(oy_session_t *s, const oy_driver_t *d)
{
    if (s == NULL || d == NULL || d->tx == NULL || d->rx == NULL
        || d->now_us == NULL) {
        return OY_ERR_ARG;
    }
    memset(s, 0, sizeof(*s));
    s->d = *d;
    oy_guard_init(&s->guard);
    s->result = OY_OK;
    return OY_OK;
}

oy_res_t oy_session_enter(oy_session_t *s, uint32_t boot_ms)
{
    if (s == NULL || boot_ms < OY_BOOT_MS_MIN || boot_ms > OY_BOOT_MS_MAX) {
        return OY_ERR_ARG;
    }
    if (s->state != OY_ST_IDLE) {
        return OY_ERR_STATE;
    }
    openyge_decoder_reset(&s->dec);
    openyge_params_reset(&s->params);
    oy_guard_init(&s->guard);
    s->count    = 0;
    s->addr     = 0;
    s->tries    = 0;
    s->legacy   = false;
    s->awaiting = false;
    s->wrote    = false;
    s->cut      = false;
    s->wait_us  = boot_ms * US_PER_MS;
    s->t_op     = s->d.now_us(s->d.ctx);
    s->state    = OY_ST_WAIT;
    s->result   = OY_BUSY;
    return OY_BUSY;
}

oy_res_t oy_session_read(oy_session_t *s)
{
    if (s == NULL) {
        return OY_ERR_ARG;
    }
    if (s->state != OY_ST_READY) {
        return OY_ERR_STATE;
    }
    s->wrote  = false;
    s->cut    = false;
    s->result = OY_BUSY;
    start_read(s, OY_ST_READ, s->d.now_us(s->d.ctx));
    return OY_BUSY;
}

oy_res_t oy_session_write(oy_session_t *s, const uint16_t *draft, size_t n,
                          uint16_t read_no, uint16_t read_crc)
{
    if (s == NULL || draft == NULL) {
        return OY_ERR_ARG;
    }
    if (s->state != OY_ST_READY) {
        return OY_ERR_STATE;
    }
    uint16_t bad = 0;
    const oy_ref_t r =
        oy_guard_approve(&s->guard, draft, n, read_no, read_crc, &bad);
    s->refuse    = (uint8_t)r;
    s->bad_index = bad;
    if (r == OY_REF_NO_TABLE) {
        return OY_NO_TABLE;
    }
    if (r == OY_REF_RO_LEGACY || r == OY_REF_RO_COUNT
        || r == OY_REF_RO_FIRMWARE) {
        return OY_NOT_KNOWN;
    }
    if (r != OY_REF_NONE) {
        return OY_GUARD;
    }
    memset(s->target, 0, sizeof(s->target));
    memcpy(s->target, draft, n * sizeof(draft[0]));
    if (next_changed(s, 0u) >= s->count) {
        oy_guard_done(&s->guard);
        return OY_OK;   /* nothing differs: no frame */
    }
    s->wrote  = false;
    s->cut    = false;
    s->tries  = 0;
    s->result = OY_BUSY;
    start_read(s, OY_ST_RECHECK, s->d.now_us(s->d.ctx));
    return OY_BUSY;
}

void oy_session_abort(oy_session_t *s)
{
    if (s == NULL || s->state == OY_ST_IDLE) {
        return;
    }
    s->cut = oy_session_writing(s);
    finish(s, OY_ABORTED);
}

bool oy_session_writing(const oy_session_t *s)
{
    return s != NULL && s->wrote
           && (s->state == OY_ST_WRITE || s->state == OY_ST_VERIFY);
}

bool oy_session_cut_write(const oy_session_t *s)
{
    return s != NULL && s->cut;
}

oy_res_t oy_session_step(oy_session_t *s)
{
    if (s == NULL) {
        return OY_ERR_ARG;
    }
    if (s->state == OY_ST_IDLE) {
        return s->result;
    }
    const uint32_t now = s->d.now_us(s->d.ctx);

    drain(s, now);
    if (s->state != OY_ST_IDLE) {
        timers(s, now);
    }
    if (s->state == OY_ST_IDLE || s->state == OY_ST_READY) {
        return s->result;
    }
    return OY_BUSY;
}
