/*
 * Several bleuart machines on one host stack through the hub of
 * protocols/bleuart (bleuart_hub.c), each against its own scripted peer of
 * test/host/ble_sim.h: the hub, up to 3 clients with their own addresses
 * and stream bytes, the actions each peer took, and one ms of the panel's
 * task (rig_tick()).
 *
 * Only static inline helpers and one static rig.
 *
 * Used by:
 * - test_bleuart_hub.c
 * - test_bleuart_backoff.c
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef HUB_RIG_H
#define HUB_RIG_H

#include <string.h>

#include "greatest.h"

#include "ble_sim.h"
#include "bleuart.h"
#include "bleuart_hub.h"

/* Clients the rig holds at most. */
#define NCL 3u
/* Actions the rig logs as the peers take them. */
#define DONE_MAX 512u

/* The clients' addresses. */
static const bleu_addr_t k_addr[NCL] = {
    { { 0x29, 0x19, 0x08, 0xBF, 0x0C, 0x00 }, BLEU_ADDR_PUBLIC },
    { { 0x10, 0x3A, 0x7F, 0xC2, 0xB1, 0xA0 }, BLEU_ADDR_RANDOM },
    { { 0x56, 0x34, 0x12, 0xA1, 0xDF, 0x7C }, BLEU_ADDR_PUBLIC },
};

/* Each peer's stream bytes: its own, so a misrouted one shows. */
static const uint8_t k_bytes[NCL][4] = {
    { 0xA0, 0xA1, 0xA2, 0xA3 },
    { 0xB0, 0xB1, 0xB2, 0xB3 },
    { 0xC0, 0xC1, 0xC2, 0xC3 },
};
static const uint8_t k_cut[] = { 4 };

/* The hub, the clients' machines and a peer for each. */
typedef struct {
    bleu_hub_t hub;
    bleu_t b[NCL];
    bsim_t s[NCL];
    unsigned n;
    bsim_log_t done[DONE_MAX];
    unsigned ndone;
} rig_t;

static rig_t g_rig;

/* @p n clients, owners 0 to n - 1, each peer with its own handles. */
static inline rig_t *rig_init(unsigned n)
{
    rig_t *r = &g_rig;
    memset(r, 0, sizeof *r);
    r->n = n;
    bleu_hub_init(&r->hub);
    for (unsigned i = 0; i < n; ++i) {
        bsim_init(&r->s[i]);
        r->s[i].next_conn = (uint16_t)(1u + i);
        bleu_init(&r->b[i]);
        CHECK(bleu_setup(&r->b[i], &bleu_profile_issc, (uint8_t)i));
        CHECK(bleu_hub_add(&r->hub, (uint8_t)i));
    }
    return r;
}

/* Every event the hub has for a client, to that client's machine. */
static inline void rig_deliver(rig_t *r, uint32_t t)
{
    uint8_t o;
    bleu_evt_t e;
    while (bleu_hub_next_event(&r->hub, &o, &e)) {
        CHECK(o < r->n);
        if (o < r->n) {
            bsim_feed(&r->s[o], &r->b[o], &e, t);
        }
    }
}

/* The peer that takes a hub action: the token's, else the one whose
 * procedure or connection the action ends. */
static inline int rig_peer(const rig_t *r, const bleu_act_t *a)
{
    if (a->token != 0u) {
        return bleu_token_owner(a->token);
    }
    for (unsigned i = 0; i < r->n; ++i) {
        const bsim_t *s = &r->s[i];
        if ((a->kind == BLEU_ACT_CANCEL && s->connecting)
            || (a->kind == BLEU_ACT_SCAN_STOP && s->scanning)
            || (a->kind == BLEU_ACT_DISCONNECT && s->connected
                && s->conn == a->conn)) {
            return (int)i;
        }
    }
    return -1;
}

/* Every action the hub has, to its peer, logged. */
static inline void rig_perform(rig_t *r, uint32_t t)
{
    bleu_act_t a;
    while (bleu_hub_next_action(&r->hub, &a)) {
        if (r->ndone < DONE_MAX) {
            r->done[r->ndone].t = t;
            r->done[r->ndone].a = a;
            ++r->ndone;
        }
        const int p = rig_peer(r, &a);
        if (p >= 0 && (unsigned)p < r->n) {
            bsim_act(&r->s[p], t, &a);
        }
    }
}

/* The next event of peer @p s due at @p t, in time order. */
static inline bool rig_due(bsim_t *s, uint32_t t, bleu_evt_t *out)
{
    unsigned best = s->nev;
    for (unsigned i = 0; i < s->nev; ++i) {
        if (bsim_due(s->ev[i].at, t)
            && (best == s->nev
                || (int32_t)(s->ev[i].at - s->ev[best].at) < 0)) {
            best = i;
        }
    }
    if (best == s->nev) {
        return false;
    }
    *out = s->ev[best].e;
    memmove(&s->ev[best], &s->ev[best + 1u],
            (s->nev - best - 1u) * sizeof s->ev[0]);
    --s->nev;
    return true;
}

/* Peer @p s's next notification on its 1E4D, as an event. */
static inline void rig_notify(bsim_t *s, bleu_evt_t *e)
{
    memset(e, 0, sizeof *e);
    e->kind = BLEU_EV_NOTIFY;
    e->conn = s->conn;
    e->h1 = H_TX_VAL;
    const size_t n = s->ncuts ? s->cuts[s->cut_i++ % s->ncuts] : 20u;
    for (size_t i = 0; i < n; ++i) {
        e->data[i] = s->stream[s->stream_pos++ % s->stream_n];
    }
    e->len = (uint8_t)n;
    ++s->notifies_sent;
}

/* One ms at @p t, as the panel's task runs it: every peer's events, its
 * notification, the hub's timers, then every client's actions. */
static inline void rig_tick(rig_t *r, uint32_t t)
{
    bleu_evt_t e;
    for (unsigned i = 0; i < r->n; ++i) {
        bsim_t *s = &r->s[i];
        while (rig_due(s, t, &e)) {
            bsim_open(s, &e);
            bleu_hub_event(&r->hub, &e, t);
            rig_deliver(r, t);
            rig_perform(r, t);
        }
        if (s->stream != NULL && s->connected && s->subscribed
            && bsim_due(s->next_notify, t)) {
            rig_notify(s, &e);
            s->next_notify = t + s->period;
            bleu_hub_event(&r->hub, &e, t);
            rig_deliver(r, t);
        }
    }
    bleu_hub_tick(&r->hub, t);
    rig_perform(r, t);
    for (unsigned i = 0; i < r->n; ++i) {
        bleu_act_t a;
        while (bleu_step(&r->b[i], t, &a)) {
            bsim_t *s = &r->s[i];
            if (s->nlog < BSIM_LOG_MAX) {
                s->log[s->nlog].t = t;
                s->log[s->nlog].a = a;
                ++s->nlog;
            }
            bleu_hub_action(&r->hub, (uint8_t)i, &a, t);
            rig_deliver(r, t);
            rig_perform(r, t);
        }
    }
}

/* Run from @p from up to, not including, @p to. */
static inline void rig_run(rig_t *r, uint32_t from, uint32_t to)
{
    for (uint32_t t = from; t != to; ++t) {
        rig_tick(r, t);
    }
}

/* How many actions of @p kind the peers took for owner @p o. */
static inline unsigned rig_count(const rig_t *r, uint8_t kind, int o)
{
    unsigned n = 0;
    for (unsigned i = 0; i < r->ndone; ++i) {
        const bleu_act_t *a = &r->done[i].a;
        if (a->kind == kind
            && (o < 0 || (a->token != 0u && bleu_token_owner(a->token) == o))) {
            ++n;
        }
    }
    return n;
}

/* The n-th action of @p kind the peers took, or NULL. */
static const bsim_log_t *rig_nth(const rig_t *r, uint8_t kind, unsigned n)
{
    for (unsigned i = 0; i < r->ndone; ++i) {
        if (r->done[i].a.kind == kind && n-- == 0u) {
            return &r->done[i];
        }
    }
    return NULL;
}

/* Bind client @p i to its address, give its peer a stream, switch on. */
static inline void rig_on(rig_t *r, unsigned i, uint32_t t)
{
    bsim_stream(&r->s[i], k_bytes[i], sizeof k_bytes[i], k_cut,
                sizeof k_cut);
    bleu_bind(&r->b[i], &k_addr[i], t);
    bleu_enable(&r->b[i], true, t);
}

#endif /* HUB_RIG_H */
