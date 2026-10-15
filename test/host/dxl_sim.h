/*
 * A bus of modelled ROBOTIS Dynamixel X-series servos behind a
 * dxl_driver_t, for the host suite.  ROBOTIS and Dynamixel are names or
 * trademarks of their owner; they are used here only to say which servos
 * the model stands for.
 *
 * The driver keeps a microsecond clock the test moves.  start() decodes
 * the instruction packet as each servo on the bus does, at the line's rate,
 * and lays the replies out in time: the packet takes 10 bit times per byte,
 * each servo answers its Return Delay Time after the packet's end, and its
 * bytes follow 10 bit times apart.  Two servos that answer at once garble
 * each other: the line carries the AND of their bytes, as an idle-high bus
 * driven low wins.  poll() hands over the bytes whose time has come, at most
 * `chunk` a call.  A fault set on the bus changes the next reply.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "dxl_bus.h"
#include "dxl_packet.h"
#include "dxl_table.h"

#define SIM_SERVOS 24u
#define SIM_REPLY  1024u

typedef enum {
    SIM_FAULT_NONE = 0,
    SIM_FAULT_SILENT,      /* no reply */
    SIM_FAULT_TRUNCATE,    /* the last fault_arg bytes not sent */
    SIM_FAULT_CRC,         /* the last byte flipped */
    SIM_FAULT_WRONG_ID,    /* the reply carries fault_arg as its ID */
    SIM_FAULT_OVERSIZE,    /* Length 0xFFFF */
    SIM_FAULT_NOISE,       /* fault_arg bytes of 0x00 in front */
    SIM_FAULT_EXTRA,       /* fault_arg bytes of 0x5A behind */
    SIM_FAULT_NOT_STATUS,  /* a PING instruction comes back */
    SIM_FAULT_ERR,         /* the ERR byte is fault_arg */
    SIM_FAULT_PARAMS,      /* one parameter more */
    SIM_FAULT_LATE,        /* fault_arg us later */
    SIM_FAULT_UNSTUFFED,   /* the first stuffing byte left out, CRC fitted */
    SIM_FAULT_ID253,       /* ID 253, CRC fitted */
} sim_fault_t;

typedef struct {
    uint8_t  id;
    uint8_t  baud_index;
    uint8_t  firmware;
    uint8_t  srl;           /* Status Return Level */
    uint8_t  reply_new_id;  /* after a write to ID, the reply comes from it */
    uint16_t model;
    uint8_t  table[256];
    unsigned writes;
    unsigned reboots;
    unsigned resets;
} sim_servo_t;

typedef struct {
    uint32_t now;
    uint32_t baud;
    uint8_t  refuse_mask;   /* rates set_baud() refuses, by index */
    bool     refuse_start;
    bool     running;
    size_t   chunk;         /* bytes per poll at most; 0 = all */
    sim_fault_t fault;
    uint32_t fault_arg;
    sim_servo_t servo[SIM_SERVOS];
    unsigned n_servo;
    uint8_t  line[SIM_REPLY];
    uint32_t at[SIM_REPLY]; /* the time each byte is in */
    size_t   n_line;
    size_t   given;
    uint8_t  sent[DXL_WIRE_MAX];
    size_t   n_sent;
    unsigned starts;
    unsigned stops;
    unsigned bauds_set;
} sim_t;

static uint32_t sim_byte_us_x1000(uint32_t baud)
{
    return (uint32_t)(10000000000ull / baud); /* ns per byte */
}

static sim_servo_t *sim_add(sim_t *s, uint8_t id, uint16_t model,
                            uint8_t baud_index)
{
    sim_servo_t *v = &s->servo[s->n_servo++];

    memset(v, 0, sizeof(*v));
    v->id = id;
    v->model = model;
    v->baud_index = baud_index;
    v->firmware = 45;
    v->srl = 2;
    v->table[0] = (uint8_t)(model & 0xFFu);
    v->table[1] = (uint8_t)(model >> 8);
    v->table[6] = v->firmware;
    v->table[7] = id;
    v->table[8] = baud_index;
    v->table[9] = 250;
    v->table[13] = 2;
    v->table[68] = 2;
    return v;
}

/* Lay @p n bytes on the line from @p t0 (ns), 1 byte time apart, AND-ed
 * with what is there already. */
static void sim_lay(sim_t *s, const uint8_t *b, size_t n, uint64_t t0_ns)
{
    const uint32_t step = sim_byte_us_x1000(s->baud);

    for (size_t i = 0; i < n; ++i) {
        const uint32_t t = (uint32_t)((t0_ns + (uint64_t)step * (i + 1u))
                                      / 1000u);
        size_t k = 0;

        /* A byte that overlaps one already laid garbles it. */
        while (k < s->n_line
               && (uint32_t)(t - s->at[k] + step / 1000u) >= 2u * (step / 1000u)) {
            ++k;
        }
        if (k < s->n_line) {
            s->line[k] &= b[i];
            continue;
        }
        if (s->n_line < SIM_REPLY) {
            s->line[s->n_line] = b[i];
            s->at[s->n_line] = t;
            s->n_line++;
        }
    }
    /* Keep the line in time order. */
    for (size_t i = 1; i < s->n_line; ++i) {
        for (size_t j = i; j > 0u && (int32_t)(s->at[j] - s->at[j - 1u]) < 0;
             --j) {
            const uint32_t ta = s->at[j];
            const uint8_t ba = s->line[j];

            s->at[j] = s->at[j - 1u];
            s->line[j] = s->line[j - 1u];
            s->at[j - 1u] = ta;
            s->line[j - 1u] = ba;
        }
    }
}

/* The status reply of servo @p v to packet @p p, into @p out; 0 for none. */
static size_t sim_answer(sim_t *s, sim_servo_t *v, const dxl_packet_t *p,
                         uint8_t *out)
{
    uint8_t params[DXL_PARAM_MAX + 1u];
    size_t np = 0;
    uint8_t reply_id = v->id;
    bool reply = p->id != DXL_ID_BROADCAST;

    switch (p->instr) {
    case DXL_INS_PING:
        params[0] = (uint8_t)(v->model & 0xFFu);
        params[1] = (uint8_t)(v->model >> 8);
        params[2] = v->firmware;
        np = 3;
        break;
    case DXL_INS_READ: {
        const uint16_t a = dxl_get_u16(&p->params[0]);
        const uint16_t len = dxl_get_u16(&p->params[2]);

        for (uint16_t i = 0; i < len && np < DXL_PARAM_MAX; ++i) {
            params[np++] = v->table[(a + i) & 0xFFu];
        }
        reply = reply && v->srl >= 1u;
        break;
    }
    case DXL_INS_WRITE: {
        const uint16_t a = dxl_get_u16(&p->params[0]);

        for (uint16_t i = 2; i < p->n_params; ++i) {
            v->table[(a + i - 2u) & 0xFFu] = p->params[i];
        }
        v->writes++;
        if (a == 7u && p->n_params == 3u) {
            v->id = p->params[2];
            if (v->reply_new_id != 0u) {
                reply_id = v->id;
            }
        }
        if (a == 68u && p->n_params == 3u) {
            v->srl = p->params[2];
        }
        reply = reply && v->srl >= 2u;
        break;
    }
    case DXL_INS_REBOOT:
        v->reboots++;
        reply = reply && v->srl >= 2u;
        break;
    case DXL_INS_FACTORY_RESET:
        v->resets++;
        reply = reply && v->srl >= 2u;
        break;
    default:
        reply = reply && v->srl >= 2u;
        break;
    }
    if (!reply || s->fault == SIM_FAULT_SILENT) {
        return 0;
    }
    uint8_t err = 0;

    if (s->fault == SIM_FAULT_ERR) {
        err = (uint8_t)s->fault_arg;
    }
    if (s->fault == SIM_FAULT_PARAMS) {
        params[np++] = 0;
    }
    if (s->fault == SIM_FAULT_WRONG_ID) {
        reply_id = (uint8_t)s->fault_arg;
    }
    if (s->fault == SIM_FAULT_NOT_STATUS) {
        return dxl_ping(reply_id, out, DXL_WIRE_MAX);
    }
    size_t n = dxl_encode_status(reply_id, err, params, np, out, 2u * DXL_WIRE_MAX);

    if (s->fault == SIM_FAULT_TRUNCATE) {
        n -= s->fault_arg;
    } else if (s->fault == SIM_FAULT_CRC) {
        out[n - 1u] ^= 0x01u;
    } else if (s->fault == SIM_FAULT_OVERSIZE) {
        out[5] = 0xFF;
        out[6] = 0xFF;
    } else if (s->fault == SIM_FAULT_EXTRA) {
        for (uint32_t i = 0; i < s->fault_arg; ++i) {
            out[n++] = 0x5A;
        }
    } else if (s->fault == SIM_FAULT_NOISE) {
        memmove(&out[s->fault_arg], out, n);
        memset(out, 0, s->fault_arg);
        n += s->fault_arg;
    } else if (s->fault == SIM_FAULT_UNSTUFFED) {
        for (size_t i = DXL_HEADER_SIZE; i + 3u < n - DXL_CRC_SIZE; ++i) {
            if (out[i] == 0xFFu && out[i + 1u] == 0xFFu
                && out[i + 2u] == 0xFDu && out[i + 3u] == 0xFDu) {
                memmove(&out[i + 3u], &out[i + 4u], n - i - 4u);
                --n;
                dxl_put_u16(&out[5], (uint16_t)(dxl_get_u16(&out[5]) - 1u));
                break;
            }
        }
        dxl_put_u16(&out[n - 2u], dxl_crc(0, out, n - 2u));
    } else if (s->fault == SIM_FAULT_ID253) {
        out[4] = 253;
        dxl_put_u16(&out[n - 2u], dxl_crc(0, out, n - 2u));
    }
    return n;
}

static bool sim_set_baud(void *ctx, uint32_t baud)
{
    sim_t *s = (sim_t *)ctx;
    const int i = dxl_baud_index(baud);

    if (i < 0 || (s->refuse_mask & (1u << i)) != 0u) {
        return false;
    }
    s->baud = baud;
    s->bauds_set++;
    return true;
}

static bool sim_start(void *ctx, const uint8_t *tx, size_t n)
{
    sim_t *s = (sim_t *)ctx;
    dxl_packet_t p;
    uint8_t reply[2u * DXL_WIRE_MAX];

    if (s->refuse_start || s->running || n > sizeof(s->sent)) {
        return false;
    }
    memcpy(s->sent, tx, n);
    s->n_sent = n;
    s->starts++;
    s->running = true;
    s->n_line = 0;
    s->given = 0;
    if (dxl_parse(tx, n, &p, NULL) != DXL_RX_OK) {
        return true;
    }
    /* The packet's end, in ns. */
    const uint64_t end_ns = (uint64_t)s->now * 1000u
                            + (uint64_t)sim_byte_us_x1000(s->baud) * n;

    for (unsigned i = 0; i < s->n_servo; ++i) {
        sim_servo_t *v = &s->servo[i];

        if (dxl_baud_rate(v->baud_index) != s->baud
            || (p.id != v->id && p.id != DXL_ID_BROADCAST)) {
            continue;
        }
        const size_t m = sim_answer(s, v, &p, reply);

        if (m > 0u) {
            uint64_t t0 = end_ns + 2000u * (uint64_t)v->table[9];

            if (s->fault == SIM_FAULT_LATE) {
                t0 += 1000u * (uint64_t)s->fault_arg;
            }
            sim_lay(s, reply, m, t0);
        }
    }
    return true;
}

static size_t sim_poll(void *ctx, uint8_t *out, size_t cap)
{
    sim_t *s = (sim_t *)ctx;
    size_t n = 0;

    if (!s->running) {
        return 0;
    }
    while (s->given < s->n_line && n < cap
           && (int32_t)(s->now - s->at[s->given]) >= 0
           && (s->chunk == 0u || n < s->chunk)) {
        out[n++] = s->line[s->given++];
    }
    return n;
}

static void sim_stop(void *ctx)
{
    sim_t *s = (sim_t *)ctx;

    s->running = false;
    s->stops++;
}

/* dxl_driver_t's now_us takes a non-const void *ctx; the clock only reads
 * through it, and a const parameter would need a cast at the driver. */
// cppcheck-suppress constParameterCallback
static uint32_t sim_now(void *ctx)
{
    return ((const sim_t *)ctx)->now;
}

static dxl_driver_t sim_driver(sim_t *s)
{
    dxl_driver_t d;

    d.ctx = s;
    d.set_baud = sim_set_baud;
    d.start = sim_start;
    d.poll = sim_poll;
    d.stop = sim_stop;
    d.now_us = sim_now;
    return d;
}
