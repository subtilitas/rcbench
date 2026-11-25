/*
 * A simulated ESC (electronic speed controller) speaking OpenYGE, behind an
 * oy_driver_t, for the session suites: test_openyge_session.c and
 * test_openyge_session_write.c.
 *
 * The ESC: a table of up to 64 parameters, one (index, value) pair per frame
 * in rising order.  Before a master's first request it sends TELE_AUTO
 * frames every auto_us, the first auto_first_us after power-on; a request
 * takes it over and the auto frames stop.  It answers a request after
 * delay_us with the request's sequence number.  A legacy ESC sends legacy
 * auto frames and answers nothing.  Knobs make it drop, corrupt, delay or
 * mistype the next n replies, or not store writes.
 *
 * Every frame the session hands the driver is recorded with its time, so a
 * case can hold the session to what went out and when.  A frame the guard
 * should have stopped shows up here.
 *
 * The clock: sim_run_ms() steps the session once a millisecond.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string.h>

#include "greatest.h"

#include "openyge.h"
#include "openyge_guard.h"
#include "openyge_map.h"
#include "openyge_session.h"

#define SIM_FRAMES   1024u    /* frames recorded at most */
#define SIM_QUEUE    16u      /* replies in flight at most */
#define SIM_OUT      2048u    /* received bytes not yet taken */
#define SIM_DELAY_US 3000u    /* the ESC's turnaround */
#define SIM_FW       103555u  /* V1.03555 */
#define MS           1000u

typedef struct {
    uint32_t t;               /* when the session handed it over, us */
    uint8_t  n;
    uint8_t  b[16];
} sim_sent_t;

typedef struct {
    uint32_t due;
    uint8_t  n;
    uint8_t  b[40];
} sim_reply_t;

static struct {
    uint32_t us;              /* the clock */
    uint32_t t_on;            /* power-on */
    /* The ESC. */
    uint16_t table[OPENYGE_MAX_PARAMS];
    uint16_t cycle;           /* indices it walks: 0 to cycle - 1 */
    int      skip;            /* an index it never sends, -1 none */
    uint16_t cyc;             /* the next index it sends */
    uint8_t  dev;             /* the device byte of its frames */
    bool     taken;           /* a request came: no more auto frames */
    bool     legacy;          /* legacy headers, answers nothing */
    bool     silent;          /* answers nothing */
    uint32_t auto_us;         /* 0: no auto frames */
    uint32_t auto_first_us;
    uint32_t auto_next;       /* from power-on */
    uint32_t delay_us;
    unsigned drop_n;          /* requests or writes not heard */
    unsigned crc_n;           /* replies with a broken CRC */
    unsigned seq_n;           /* replies with another sequence number */
    unsigned type_n;          /* replies of the other type */
    unsigned late_n;          /* replies after late_us */
    uint32_t late_us;
    unsigned wlate_n;         /* write replies after wlate_us */
    uint32_t wlate_us;
    unsigned wmute_n;         /* writes stored and not answered */
    bool     ignore_writes;   /* writes answered and not stored */
    int      store_max;       /* writes stored at most, -1 all */
    unsigned stored;
    /* What went out. */
    sim_sent_t sent[SIM_FRAMES];
    unsigned   n_sent;
    /* Replies on their way. */
    sim_reply_t q[SIM_QUEUE];
    unsigned    nq;
    uint8_t  out[SIM_OUT];
    unsigned n_out;
} sim;

/* A table a writable ESC holds: count 32, firmware V1.03555. */
static inline void sim_good_table(uint16_t *t)
{
    static const uint16_t good[32] = {
        32, 2, 74, 0, 0, 5, 5, 1, 1, 2, 1, 5712,
        (uint16_t)(SIM_FW & 0xFFFFu), (uint16_t)(SIM_FW >> 16), 0x1234, 0,
        0, 1100, 800, 20, 5, 12, 120, 5, 20, 0, 0, 12000, 0, 0, 0, 0,
    };
    memset(t, 0, OPENYGE_MAX_PARAMS * sizeof(t[0]));
    memcpy(t, good, sizeof(good));
}

/* Power on at @p start_us: a writable ESC at address 1, auto frames every
 * 50ms from 300ms on. */
static inline void sim_reset(uint32_t start_us)
{
    memset(&sim, 0, sizeof(sim));
    sim.us = start_us;
    sim.t_on = start_us;
    sim_good_table(sim.table);
    sim.cycle = 32;
    sim.skip = -1;
    sim.dev = 1;
    sim.auto_us = 50u * MS;
    sim.auto_first_us = 300u * MS;
    sim.auto_next = sim.auto_first_us;
    sim.delay_us = SIM_DELAY_US;
    sim.store_max = -1;
}

static inline bool sim_due(uint32_t now, uint32_t due)
{
    return (int32_t)(now - due) >= 0;
}

/* One frame from the ESC, with the next (index, value) pair. */
static inline uint8_t sim_esc_frame(uint8_t *b, uint8_t type, uint8_t seq)
{
    const uint8_t hdr = sim.legacy ? OPENYGE_HDR_LEGACY : OPENYGE_HDR_V3;
    const uint8_t n = (uint8_t)(hdr + OPENYGE_TELEMETRY_BYTES + 2u);

    if (sim.cycle == 0u) {
        sim.cycle = 1;
    }
    if (sim.skip >= 0 && sim.cyc == (uint16_t)sim.skip) {
        sim.cyc = (uint16_t)((sim.cyc + 1u) % sim.cycle);
    }
    const uint16_t idx = sim.cyc;
    const uint16_t val = idx < OPENYGE_MAX_PARAMS ? sim.table[idx] : 0u;
    sim.cyc = (uint16_t)((sim.cyc + 1u) % sim.cycle);

    memset(b, 0, n);
    b[0] = OPENYGE_SYNC;
    b[1] = sim.legacy ? 2u : OPENYGE_VERSION;
    b[2] = type;
    b[3] = n;
    if (!sim.legacy) {
        b[4] = seq;
        b[5] = sim.dev;
    }
    uint8_t *p = b + hdr;
    p[1] = 65;                         /* 25 degrees C */
    p[22] = (uint8_t)(idx & 0xFFu);
    p[23] = (uint8_t)(idx >> 8);
    p[24] = (uint8_t)(val & 0xFFu);
    p[25] = (uint8_t)(val >> 8);
    const uint16_t crc = openyge_crc(OPENYGE_CRC_INIT, b, n - 2u);
    b[n - 2u] = (uint8_t)(crc & 0xFFu);
    b[n - 1u] = (uint8_t)(crc >> 8);
    return n;
}

static inline void sim_queue(const uint8_t *b, uint8_t n, uint32_t due)
{
    if (sim.nq >= SIM_QUEUE) {
        T_FAIL("sim: %u replies in flight", (unsigned)sim.nq);
        return;
    }
    sim.q[sim.nq].due = due;
    sim.q[sim.nq].n = n;
    memcpy(sim.q[sim.nq].b, b, n);
    ++sim.nq;
}

static inline bool sim_tx(void *ctx, const uint8_t *f, size_t n)
{
    (void)ctx;
    if (sim.n_sent < SIM_FRAMES && n <= sizeof(sim.sent[0].b)) {
        sim.sent[sim.n_sent].t = sim.us;
        sim.sent[sim.n_sent].n = (uint8_t)n;
        memcpy(sim.sent[sim.n_sent].b, f, n);
        ++sim.n_sent;
    }
    openyge_decoder_t d;
    openyge_frame_t fr;
    bool got = false;
    openyge_decoder_reset(&d);
    for (size_t i = 0; i < n; ++i) {
        got = openyge_decode_byte(&d, f[i], &fr);
    }
    if (!got || sim.legacy || sim.silent) {
        return true;
    }
    if (sim.drop_n > 0u) {
        --sim.drop_n;
        return true;
    }
    sim.taken = true;
    uint16_t idx = 0, val = 0;
    (void)openyge_control_parse(&fr, &idx, &val);
    const bool write = fr.hdr.type == OPENYGE_FT_WRITE_PARAM_REQ;
    if (write && !sim.ignore_writes
        && (sim.store_max < 0 || sim.stored < (unsigned)sim.store_max)
        && idx < OPENYGE_MAX_PARAMS) {
        sim.table[idx] = val;
        ++sim.stored;
    }
    if (write && sim.wmute_n > 0u) {
        --sim.wmute_n;
        return true;
    }
    uint8_t type = write ? OPENYGE_FT_WRITE_PARAM_RESP : OPENYGE_FT_TELE_RESP;
    uint8_t seq = fr.hdr.seq;
    uint32_t delay = sim.delay_us;
    if (sim.type_n > 0u) {
        --sim.type_n;
        type = write ? OPENYGE_FT_TELE_RESP : OPENYGE_FT_WRITE_PARAM_RESP;
    }
    if (sim.seq_n > 0u) {
        --sim.seq_n;
        seq = (uint8_t)(seq + 1u);
    }
    if (sim.late_n > 0u) {
        --sim.late_n;
        delay = sim.late_us;
    }
    if (write && sim.wlate_n > 0u) {
        --sim.wlate_n;
        delay = sim.wlate_us;
    }
    uint8_t b[40];
    const uint8_t len = sim_esc_frame(b, type, seq);
    if (sim.crc_n > 0u) {
        --sim.crc_n;
        b[len - 1u] ^= 0x5Au;
    }
    sim_queue(b, len, sim.us + delay);
    return true;
}

static inline size_t sim_rx(void *ctx, uint8_t *buf, size_t cap)
{
    (void)ctx;
    /* Auto frames until a request takes the ESC over. */
    if (sim.auto_us != 0u && !sim.taken
        && sim_due(sim.us, sim.t_on + sim.auto_next)) {
        uint8_t b[40];
        const uint8_t len = sim_esc_frame(b, OPENYGE_FT_TELE_AUTO, 0u);
        sim_queue(b, len, sim.us);
        sim.auto_next += sim.auto_us;
    }
    /* Replies that are due, in the order they fall due. */
    for (;;) {
        unsigned best = SIM_QUEUE;
        for (unsigned i = 0; i < sim.nq; ++i) {
            if (sim_due(sim.us, sim.q[i].due)
                && (best == SIM_QUEUE
                    || (int32_t)(sim.q[i].due - sim.q[best].due) < 0)) {
                best = i;
            }
        }
        if (best == SIM_QUEUE) {
            break;
        }
        if (sim.n_out + sim.q[best].n <= SIM_OUT) {
            memcpy(sim.out + sim.n_out, sim.q[best].b, sim.q[best].n);
            sim.n_out += sim.q[best].n;
        }
        sim.q[best] = sim.q[sim.nq - 1u];
        --sim.nq;
    }
    const size_t n = sim.n_out < cap ? sim.n_out : cap;
    memcpy(buf, sim.out, n);
    memmove(sim.out, sim.out + n, sim.n_out - n);
    sim.n_out -= (unsigned)n;
    return n;
}

static inline uint32_t sim_now(void *ctx)
{
    (void)ctx;
    return sim.us;
}

static const oy_driver_t SIM_DRIVER = { NULL, sim_tx, sim_rx, sim_now };

/* Step the session once a millisecond for @p ms; the last step's result. */
static inline oy_res_t sim_run_ms(oy_session_t *s, uint32_t ms)
{
    oy_res_t r = oy_session_step(s);
    for (uint32_t i = 0; i < ms; ++i) {
        sim.us += MS;
        r = oy_session_step(s);
    }
    return r;
}

/* Step until the operation ends, at most @p ms; OY_BUSY when it did not. */
static inline oy_res_t sim_until(oy_session_t *s, uint32_t ms)
{
    for (uint32_t i = 0; i < ms; ++i) {
        sim.us += MS;
        const oy_res_t r = oy_session_step(s);
        if (r != OY_BUSY) {
            return r;
        }
    }
    return OY_BUSY;
}

/* Step until the session reaches @p st, at most @p ms. */
static inline bool sim_until_state(oy_session_t *s, uint8_t st, uint32_t ms)
{
    for (uint32_t i = 0; i < ms && s->state != st; ++i) {
        sim.us += MS;
        (void)oy_session_step(s);
    }
    return s->state == st;
}

/* Frames of one type recorded from @p from on. */
static inline unsigned sim_count(uint8_t type, unsigned from)
{
    unsigned n = 0;
    for (unsigned i = from; i < sim.n_sent; ++i) {
        n += sim.sent[i].b[2] == type;
    }
    return n;
}

/* The control payload of recorded frame @p i. */
static inline uint16_t sim_index(unsigned i)
{
    return (uint16_t)(sim.sent[i].b[6] | (sim.sent[i].b[7] << 8));
}
static inline uint16_t sim_value(unsigned i)
{
    return (uint16_t)(sim.sent[i].b[8] | (sim.sent[i].b[9] << 8));
}

/* Init, enter with the shortest boot wait, and take the ESC over. */
static inline oy_res_t sim_connect(oy_session_t *s)
{
    if (oy_session_init(s, &SIM_DRIVER) != OY_OK) {
        return OY_ERR_ARG;
    }
    if (oy_session_enter(s, OY_BOOT_MS_MIN) != OY_BUSY) {
        return OY_ERR_STATE;
    }
    return sim_until(s, OY_BOOT_MS_MIN + OY_LISTEN_MS + 1000u);
}

/* Connect and read the whole table. */
static inline oy_res_t sim_connect_read(oy_session_t *s)
{
    oy_res_t r = sim_connect(s);
    if (r != OY_OK) {
        return r;
    }
    if (oy_session_read(s) != OY_BUSY) {
        return OY_ERR_STATE;
    }
    return sim_until(s, OY_TABLE_CAP_MS + 1000u);
}

/* Write a draft of the read copy with its read number and CRC. */
static inline oy_res_t sim_write(oy_session_t *s, const uint16_t *draft)
{
    return oy_session_write(s, draft, s->count, s->guard.read_no,
                            s->guard.read_crc);
}
