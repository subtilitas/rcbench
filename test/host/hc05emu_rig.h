/*
 * What the suites of protocols/hc05emu share: a scripted radio that serves
 * the emulator's actions with results at set times, the line-level driver
 * of the emulator alone, and a bench that runs the panel's own HC-05
 * machine (protocols/hc05) against the emulator over a UART of 1ms
 * latency.
 *
 * The radio knows devices with an address, a class, an RSSI, a name (given
 * with the inquiry, read later, failed or never answered), a PIN and an SPP
 * service.  An inquiry reports each device at its own time, at most the
 * count asked for, and ends units x 1.28s plus inq_tail_ms after its start.
 * A name read answers after name_ms; a pairing or connect with the right
 * PIN to a device with SPP after pair_ms or conn_ms, any other after
 * fail_ms; a silent device never answers.  STOP cancels every result due
 * and closes the link without an event.
 *
 * Bytes cross the UART only at equal bauds.  A command sent at another
 * baud reaches the emulator as two junk bytes without a line end, as a
 * real UART reads them; an answer at another baud is lost.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef HC05EMU_RIG_H
#define HC05EMU_RIG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "hc05.h"
#include "hc05emu.h"

#pragma GCC diagnostic ignored "-Wunused-function"

#define RIG_DEV_MAX  12u
#define RIG_EV_MAX   48u
#define RIG_PIPE_MAX 64u
#define RIG_DATA_MAX 256u
#define RIG_TEXT_MAX 1024u

/* The sensor of the cases, as +INQ prints it, and its bytes. */
#define SENSOR      "98D3:31:FD5C81"
#define SENSOR_CMD  "98D3,31,FD5C81"
static const uint8_t k_sensor[6] = { 0x98, 0xD3, 0x31, 0xFD, 0x5C, 0x81 };

typedef struct {
    uint8_t addr[6];
    uint32_t cod;
    int16_t rssi;
    const char *name;   /* NULL: a name read fails */
    bool name_in_inq;   /* the inquiry gives the name */
    bool name_silent;   /* a name read never answers */
    const char *pin;
    bool spp;
    bool silent;        /* a pairing or connect never answers */
    uint32_t inq_ms;    /* its inquiry result after the start */
} rdev_t;

enum { EV_FOUND, EV_INQ_DONE, EV_NAME, EV_PAIR, EV_LINK };

typedef struct {
    uint32_t at;
    int kind;
    int dev;
    bool ok;
} rev_t;

typedef struct {
    rdev_t devs[RIG_DEV_MAX];
    unsigned ndev;
    rev_t ev[RIG_EV_MAX];
    unsigned nev;
    uint32_t inq_tail_ms;
    uint32_t name_ms;
    uint32_t pair_ms;
    uint32_t conn_ms;
    uint32_t fail_ms;
    bool link_up;
    int link_dev;
    hc05emu_cfg_t nvs;
    unsigned saves, stops, inqs, names, pairs, connects, bauds;
    uint32_t baud;          /* the emulator's UART */
    uint8_t last_max, last_units;
    uint8_t to_sensor[RIG_DATA_MAX];
    size_t to_sensor_n;
} radio_t;

static void radio_init(radio_t *r)
{
    memset(r, 0, sizeof *r);
    r->inq_tail_ms = 600u;
    r->name_ms = 700u;
    r->pair_ms = 3000u;
    r->conn_ms = 1500u;
    r->fail_ms = 4000u;
    r->link_dev = -1;
}

static rdev_t *radio_add(radio_t *r, const uint8_t *addr, int16_t rssi,
                         const char *name, uint32_t inq_ms)
{
    rdev_t *d = &r->devs[r->ndev++];
    memset(d, 0, sizeof *d);
    memcpy(d->addr, addr, 6);
    d->cod = 0x1F00u;
    d->rssi = rssi;
    d->name = name;
    d->pin = "1234";
    d->spp = true;
    d->inq_ms = inq_ms;
    return d;
}

static int radio_dev(const radio_t *r, const uint8_t *addr)
{
    for (unsigned i = 0; i < r->ndev; ++i) {
        if (memcmp(r->devs[i].addr, addr, 6) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void radio_at(radio_t *r, uint32_t at, int kind, int dev, bool ok)
{
    if (r->nev < RIG_EV_MAX) {
        r->ev[r->nev++] = (rev_t){ at, kind, dev, ok };
    }
}

/* Whether a pairing or connect with @p pin to device @p i goes through. */
static bool radio_takes(const radio_t *r, int i, const char *pin)
{
    return i >= 0 && r->devs[i].spp && strcmp(r->devs[i].pin, pin) == 0;
}

/* Serve one emulator action that is not a UART action. */
static void radio_act(radio_t *r, const hc05emu_t *e, const hc05emu_act_t *a,
                      uint32_t now)
{
    const int i = radio_dev(r, a->addr);
    switch (a->kind) {
    case HC05EMU_ACT_BAUD:
        r->baud = a->baud;
        ++r->bauds;
        break;
    case HC05EMU_ACT_INQ: {
        ++r->inqs;
        r->last_max = a->max;
        r->last_units = a->units;
        const uint32_t span = (uint32_t)a->units * 1280u;
        unsigned told = 0;
        for (unsigned k = 0; k < r->ndev && told < a->max; ++k) {
            if (r->devs[k].inq_ms < span) {
                radio_at(r, now + r->devs[k].inq_ms, EV_FOUND, (int)k, true);
                ++told;
            }
        }
        radio_at(r, now + span + r->inq_tail_ms, EV_INQ_DONE, -1, true);
        break;
    }
    case HC05EMU_ACT_NAME:
        ++r->names;
        if (i < 0 || !r->devs[i].name_silent) {
            radio_at(r, now + r->name_ms, EV_NAME, i,
                     i >= 0 && r->devs[i].name != NULL);
        }
        break;
    case HC05EMU_ACT_PAIR:
        ++r->pairs;
        if (i < 0 || !r->devs[i].silent) {
            const bool ok = radio_takes(r, i, a->pin);
            radio_at(r, now + (ok ? r->pair_ms : r->fail_ms), EV_PAIR, i, ok);
        }
        break;
    case HC05EMU_ACT_CONNECT:
        ++r->connects;
        if (i < 0 || !r->devs[i].silent) {
            const bool ok = radio_takes(r, i, a->pin);
            radio_at(r, now + (ok ? r->conn_ms : r->fail_ms), EV_LINK, i, ok);
        }
        break;
    case HC05EMU_ACT_STOP:
        ++r->stops;
        r->nev = 0;
        r->link_up = false;
        r->link_dev = -1;
        break;
    case HC05EMU_ACT_SAVE:
        r->nvs = e->cfg;
        ++r->saves;
        break;
    case HC05EMU_ACT_SEND:
    case HC05EMU_ACT_NONE:
    default:
        break;
    }
}

/* Hand the emulator every radio result due at @p now, in time order. */
static void radio_deliver(radio_t *r, hc05emu_t *e, uint32_t now)
{
    for (;;) {
        int best = -1;
        for (unsigned k = 0; k < r->nev; ++k) {
            if ((int32_t)(now - r->ev[k].at) >= 0
                && (best < 0
                    || (int32_t)(r->ev[k].at - r->ev[best].at) < 0)) {
                best = (int)k;
            }
        }
        if (best < 0) {
            return;
        }
        const rev_t ev = r->ev[best];
        r->ev[best] = r->ev[--r->nev];
        static const uint8_t none[6] = { 0 };
        const rdev_t *d = ev.dev >= 0 ? &r->devs[ev.dev] : NULL;
        const uint8_t *addr = d != NULL ? d->addr : none;
        switch (ev.kind) {
        case EV_FOUND:
            hc05emu_found(e, addr, d->cod, d->rssi,
                          d->name_in_inq ? d->name : NULL, now);
            break;
        case EV_INQ_DONE:
            hc05emu_inq_done(e, now);
            break;
        case EV_NAME:
            hc05emu_name_done(e, addr, ev.ok ? d->name : NULL, now);
            break;
        case EV_PAIR:
            hc05emu_pair_done(e, addr, ev.ok, now);
            break;
        default: /* EV_LINK */
            if (ev.ok) {
                r->link_up = true;
                r->link_dev = ev.dev;
            }
            hc05emu_link_event(e, addr, ev.ok, now);
            break;
        }
    }
}

/* The sensor's end drops the link. */
static void radio_lose(radio_t *r, hc05emu_t *e, uint32_t now)
{
    if (r->link_up) {
        const uint8_t *addr = r->devs[r->link_dev].addr;
        r->link_up = false;
        r->link_dev = -1;
        hc05emu_link_event(e, addr, false, now);
    }
}

/* --- the emulator alone, line by line ----------------------------------- */

/* Every action the emulator has at @p now: SEND text joined into @p text,
 * the other kinds served by @p r; @return the count of non-SEND actions. */
static unsigned emu_drain(hc05emu_t *e, radio_t *r, uint32_t now, char *text)
{
    hc05emu_act_t a;
    unsigned other = 0;
    if (text != NULL) {
        text[0] = '\0';
    }
    while (hc05emu_step(e, now, &a)) {
        if (a.kind == HC05EMU_ACT_SEND) {
            if (text != NULL) {
                const size_t n = strlen(text);
                if (n + a.len < RIG_TEXT_MAX) {
                    memcpy(text + n, a.data, a.len);
                    text[n + a.len] = '\0';
                }
            }
        } else {
            ++other;
            if (r != NULL) {
                radio_act(r, e, &a, now);
            }
        }
    }
    return other;
}

/* Feed @p line with CR LF at @p now and collect the answer into @p text. */
static void emu_cmd(hc05emu_t *e, radio_t *r, const char *line, uint32_t now,
                    char *text)
{
    hc05emu_feed(e, (const uint8_t *)line, strlen(line), now);
    hc05emu_feed(e, (const uint8_t *)"\r\n", 2u, now);
    (void)emu_drain(e, r, now, text);
}

/* Run the radio and the emulator from @p t0 to @p t1 inclusive, 1ms at a
 * time, the answers joined into @p text. */
static void emu_run(hc05emu_t *e, radio_t *r, uint32_t t0, uint32_t t1,
                    char *text)
{
    char part[RIG_TEXT_MAX];
    if (text != NULL) {
        text[0] = '\0';
    }
    for (uint32_t t = t0;; ++t) {
        radio_deliver(r, e, t);
        (void)emu_drain(e, r, t, part);
        if (text != NULL && strlen(text) + strlen(part) < RIG_TEXT_MAX) {
            memcpy(text + strlen(text), part, strlen(part) + 1u);
        }
        if (t == t1) {
            break;
        }
    }
}

/* --- the panel's machine against the emulator --------------------------- */

typedef struct {
    uint32_t at;
    uint32_t baud;
    uint8_t data[HC05EMU_OUT_MAX];
    size_t len;
} chunk_t;

typedef struct {
    chunk_t c[RIG_PIPE_MAX];
    unsigned n;
} pipe_t;

typedef struct {
    hc05_t h;
    hc05emu_t e;
    radio_t r;
    uint32_t panel_baud;
    pipe_t to_emu;
    pipe_t to_panel;
    uint8_t panel_data[RIG_DATA_MAX]; /* bytes the panel got in data mode */
    size_t panel_data_n;
    char cmds[RIG_PIPE_MAX][HC05EMU_OUT_MAX + 1u]; /* the panel's commands */
    unsigned ncmd;
    unsigned junk;      /* commands that reached the emulator as junk */
    unsigned lost;      /* answers lost at the wrong baud */
} bench_t;

static void pipe_put(pipe_t *p, uint32_t at, uint32_t baud,
                     const uint8_t *data, size_t len)
{
    if (p->n < RIG_PIPE_MAX && len <= HC05EMU_OUT_MAX) {
        chunk_t *c = &p->c[p->n++];
        c->at = at;
        c->baud = baud;
        memcpy(c->data, data, len);
        c->len = len;
    }
}

/* A bench: a fresh panel machine, the emulator started from @p stored
 * (NULL defaults) with KEY low, the radio empty. */
static void bench_init(bench_t *b, const hc05emu_cfg_t *stored, uint32_t t0)
{
    memset(b, 0, sizeof *b);
    radio_init(&b->r);
    hc05_init(&b->h);
    hc05emu_init(&b->e, stored, "btbridge-test", false, t0);
    b->panel_baud = HC05_BAUD_DATA;
}

/* The sensor sends @p n bytes over the link. */
static void bench_sensor_says(bench_t *b, const uint8_t *data, size_t n,
                              uint32_t now)
{
    if (b->r.link_up && hc05emu_forwarding(&b->e)) {
        pipe_put(&b->to_panel, now + 1u, b->r.baud, data, n);
    }
}

static void bench_deliver(bench_t *b, uint32_t now)
{
    unsigned keep = 0;
    for (unsigned i = 0; i < b->to_emu.n; ++i) {
        const chunk_t *c = &b->to_emu.c[i];
        if ((int32_t)(now - c->at) < 0) {
            b->to_emu.c[keep++] = *c;
            continue;
        }
        if (hc05emu_at_mode(&b->e)) {
            if (c->baud == b->r.baud) {
                hc05emu_feed(&b->e, c->data, c->len, now);
            } else {
                static const uint8_t junk[2] = { 0xF8, 0x80 };
                hc05emu_feed(&b->e, junk, sizeof junk, now);
                ++b->junk;
            }
        } else if (hc05emu_forwarding(&b->e) && c->baud == b->r.baud
                   && b->r.to_sensor_n + c->len <= RIG_DATA_MAX) {
            memcpy(b->r.to_sensor + b->r.to_sensor_n, c->data, c->len);
            b->r.to_sensor_n += c->len;
        }
    }
    b->to_emu.n = keep;
    keep = 0;
    for (unsigned i = 0; i < b->to_panel.n; ++i) {
        const chunk_t *c = &b->to_panel.c[i];
        if ((int32_t)(now - c->at) < 0) {
            b->to_panel.c[keep++] = *c;
            continue;
        }
        if (c->baud != b->panel_baud) {
            ++b->lost;
        } else if (hc05_busy(&b->h)) {
            hc05_feed(&b->h, c->data, c->len);
        } else if (b->panel_data_n + c->len <= RIG_DATA_MAX) {
            memcpy(b->panel_data + b->panel_data_n, c->data, c->len);
            b->panel_data_n += c->len;
        }
    }
    b->to_panel.n = keep;
}

/* One ms: radio results, the UART, the emulator's actions, the panel's. */
static void bench_tick(bench_t *b, uint32_t now)
{
    hc05emu_act_t ea;
    hc05_act_t pa;
    radio_deliver(&b->r, &b->e, now);
    bench_deliver(b, now);
    for (int i = 0; i < 16 && hc05emu_step(&b->e, now, &ea); ++i) {
        if (ea.kind == HC05EMU_ACT_SEND) {
            pipe_put(&b->to_panel, now + 1u, b->r.baud, ea.data, ea.len);
        } else {
            radio_act(&b->r, &b->e, &ea, now);
        }
    }
    for (int i = 0; i < 8 && hc05_step(&b->h, now, &pa); ++i) {
        if (pa.kind == HC05_ACT_KEY) {
            hc05emu_key(&b->e, pa.key, now);
        } else if (pa.kind == HC05_ACT_BAUD) {
            b->panel_baud = pa.baud;
        } else if (pa.kind == HC05_ACT_SEND) {
            if (b->ncmd < RIG_PIPE_MAX && pa.len >= 2u) {
                memcpy(b->cmds[b->ncmd], pa.data, pa.len - 2u);
                b->cmds[b->ncmd++][pa.len - 2u] = '\0';
            }
            pipe_put(&b->to_emu, now + 1u, b->panel_baud, pa.data, pa.len);
        }
    }
}

/* How many times the panel sent @p cmd. */
static unsigned bench_sent(const bench_t *b, const char *cmd)
{
    unsigned n = 0;
    for (unsigned i = 0; i < b->ncmd; ++i) {
        n += strcmp(b->cmds[i], cmd) == 0;
    }
    return n;
}

/* Run from @p t0 until the panel's operation ended and KEY is low, at
 * most @p max ms; @return the ms the run ended at, from t0. */
static uint32_t bench_run(bench_t *b, uint32_t t0, uint32_t max)
{
    uint32_t k = 0;
    for (; k <= max; ++k) {
        bench_tick(b, t0 + k);
        if (!hc05_busy(&b->h) && !b->h.key) {
            break;
        }
    }
    return k;
}

/* Run @p ms ms from @p t0, whatever the panel does. */
static void bench_idle(bench_t *b, uint32_t t0, uint32_t ms)
{
    for (uint32_t k = 0; k < ms; ++k) {
        bench_tick(b, t0 + k);
    }
}

#endif
