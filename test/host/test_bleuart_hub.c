/*
 * Several bleuart machines on one host stack through the hub of
 * protocols/bleuart (bleuart_hub.c), each against its own scripted peer of
 * test/host/ble_sim.h.
 *
 * Under test: one client through the hub runs action for action as
 * without it; three clients come up, their connects serialised, the later
 * ones answered busy and tried again 1,000ms on; GATT answers routed by
 * token and notifications by handle, each peer's bytes reaching only its
 * own machine; a scan during another client's connect cancels that
 * connect, waits for its end and runs, the cancelled client keeping no
 * error; a second scan answered busy; a DISCONNECT that was lost, the
 * handle given to another client, the old link ended; a DISCONNECT held
 * back past that, reaching its own client; another client's CANCEL,
 * SCAN_STOP and actions on a held handle dropped or refused; a client
 * that holds the slot taking it again; a stale DISCONNECT of a client's
 * old handle keeping its new link; each GATT
 * step answered lost; the slot ended by its SCAN_END and by a connection
 * that failed to be established; the slot's time limits at their limit
 * and 1ms before; strays and NULL.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "ble_capture.h"
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
static rig_t *rig_init(unsigned n)
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
static void rig_deliver(rig_t *r, uint32_t t)
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
static int rig_peer(const rig_t *r, const bleu_act_t *a)
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
static void rig_perform(rig_t *r, uint32_t t)
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
static bool rig_due(bsim_t *s, uint32_t t, bleu_evt_t *out)
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
static void rig_notify(bsim_t *s, bleu_evt_t *e)
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
static void rig_tick(rig_t *r, uint32_t t)
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
static void rig_run(rig_t *r, uint32_t from, uint32_t to)
{
    for (uint32_t t = from; t != to; ++t) {
        rig_tick(r, t);
    }
}

/* How many actions of @p kind the peers took for owner @p o. */
static unsigned rig_count(const rig_t *r, uint8_t kind, int o)
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
static void rig_on(rig_t *r, unsigned i, uint32_t t)
{
    bsim_stream(&r->s[i], k_bytes[i], sizeof k_bytes[i], k_cut,
                sizeof k_cut);
    bleu_bind(&r->b[i], &k_addr[i], t);
    bleu_enable(&r->b[i], true, t);
}

/* The same script with and without the hub: connect, a scan beside the
 * link, the stream, a drop and the reconnect. */
static void script(bsim_t *s, bleu_t *b, rig_t *r)
{
    static const uint32_t marks[] = { 0, 500, 3000, 15000 };
    for (unsigned k = 0; k + 1u < sizeof marks / sizeof marks[0]; ++k) {
        if (marks[k] == 500u) {
            CHECK(bleu_scan(b, 500));
        } else if (marks[k] == 3000u) {
            bsim_drop(s, 3000, 0x08u);
        }
        if (r != NULL) {
            rig_run(r, marks[k], marks[k + 1u]);
        } else {
            bsim_run(s, b, marks[k], marks[k + 1u]);
        }
    }
}

TEST_CASE(one_client_runs_as_without_the_hub)
{
    static const uint8_t ad[] = { 0x02, 0x01, 0x06, 0x03, 0x03, 0xF0, 0x18 };
    bsim_t s;
    bleu_t b;
    bsim_init(&s);
    bsim_adv(&s, 40, &k_addr[1], -60, ad, sizeof ad);
    bsim_stream(&s, k_bytes[0], sizeof k_bytes[0], k_cut, sizeof k_cut);
    bleu_init(&b);
    bleu_bind(&b, &k_addr[0], 0);
    bleu_enable(&b, true, 0);
    script(&s, &b, NULL);

    rig_t *r = rig_init(1);
    bsim_adv(&r->s[0], 40, &k_addr[1], -60, ad, sizeof ad);
    rig_on(r, 0, 0);
    script(&r->s[0], &r->b[0], r);

    CHECK_EQ(s.nlog, 16);
    CHECK_EQ(r->ndone, s.nlog);
    for (unsigned i = 0; i < s.nlog && i < r->ndone; ++i) {
        CHECK_EQ(r->done[i].t, s.log[i].t);
        CHECK(memcmp(&r->done[i].a, &s.log[i].a, sizeof s.log[i].a) == 0);
    }
    const bleu_t *h = &r->b[0];
    CHECK_EQ(h->st, b.st);
    CHECK_EQ(h->err, b.err);
    CHECK_EQ(h->reason, b.reason);
    CHECK_EQ(h->notifies, b.notifies);
    CHECK_EQ(h->stray, b.stray);
    CHECK_EQ(h->attempts, b.attempts);
    CHECK_EQ(h->list.found_n, 1);
    CHECK_EQ(b.list.found_n, 1);
    CHECK_EQ(r->s[0].nrx, s.nrx);
    CHECK_EQ(r->hub.stray, 0);
    CHECK_EQ(r->hub.c[0].busy, 0);
    CHECK(bleu_link_up(h));
}

TEST_CASE(three_clients_come_up_one_connect_at_a_time)
{
    rig_t *r = rig_init(3);
    for (unsigned i = 0; i < 3u; ++i) {
        rig_on(r, i, 0);
    }
    rig_run(r, 0, 1);
    /* Client 0 holds the connect; 1 and 2 are answered busy at once. */
    CHECK_EQ(rig_count(r, BLEU_ACT_CONNECT, -1), 1);
    CHECK_EQ(r->b[1].st, BLEU_ST_WAIT);
    CHECK_EQ(r->b[2].st, BLEU_ST_WAIT);
    CHECK_EQ(r->b[1].err, BLEU_ERR_NONE);
    CHECK_EQ(r->hub.c[1].busy, 1);
    CHECK_EQ(r->hub.c[2].busy, 1);
    /* Their retry comes 1,000ms on, not 999ms: client 1 gets the slot,
     * client 2 is busy again. */
    rig_run(r, 1, 1000);
    CHECK_EQ(rig_count(r, BLEU_ACT_CONNECT, -1), 1);
    rig_tick(r, 1000);
    CHECK_EQ(rig_count(r, BLEU_ACT_CONNECT, 1), 1);
    CHECK_EQ(rig_count(r, BLEU_ACT_CONNECT, 2), 0);
    CHECK_EQ(r->hub.c[2].busy, 2);
    rig_run(r, 1001, 3000);
    for (unsigned i = 0; i < 3u; ++i) {
        CHECK(bleu_can_send(&r->b[i]));
        CHECK_EQ(r->b[i].conn, 1u + i);
        CHECK_EQ(r->b[i].err, BLEU_ERR_NONE);
        CHECK_EQ(r->b[i].stray, 0);
        CHECK(r->hub.c[i].linked);
        CHECK_EQ(r->hub.c[i].conn, 1u + i);
    }
    CHECK_EQ(rig_count(r, BLEU_ACT_CONNECT, 2), 1);
    /* Every peer's bytes reach its own machine only. */
    for (unsigned i = 0; i < 3u; ++i) {
        CHECK(r->b[i].notifies > 50u);
        CHECK_EQ(r->b[i].notifies, r->s[i].notifies_sent);
        const size_t want = 4u * r->b[i].notifies;
        CHECK_EQ(r->s[i].nrx, want < BSIM_RX_MAX ? want : BSIM_RX_MAX);
        for (size_t k = 0; k < r->s[i].nrx; ++k) {
            CHECK_EQ(r->s[i].rx[k] & 0xF0u, k_bytes[i][0] & 0xF0u);
        }
    }
    CHECK_EQ(r->hub.stray, 0);
    CHECK_EQ(r->hub.refused, 0);
    CHECK_EQ(r->hub.drops, 0);
}

TEST_CASE(gatt_answers_go_by_token_notifications_by_handle)
{
    bleu_hub_t h;
    bleu_hub_init(&h);
    CHECK(bleu_hub_add(&h, 0));
    CHECK(bleu_hub_add(&h, 3));
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    /* Client 3's connect comes up on handle 7. */
    e.kind = BLEU_EV_CONNECT;
    e.token = (uint16_t)((3u << BLEU_OWNER_SHIFT) | 9u);
    e.conn = 7;
    bleu_hub_event(&h, &e, 0);
    uint8_t o = 0xFF;
    bleu_evt_t out;
    CHECK(bleu_hub_next_event(&h, &o, &out));
    CHECK_EQ(o, 3);
    CHECK(h.c[3].linked);
    CHECK_EQ(h.c[3].conn, 7);
    /* A GATT answer before any handle: by the token's owner. */
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_SVC_DONE;
    e.token = 5u;
    e.conn = 7;
    bleu_hub_event(&h, &e, 1);
    CHECK(bleu_hub_next_event(&h, &o, &out));
    CHECK_EQ(o, 0);
    /* A notification with no token: by its handle. */
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_NOTIFY;
    e.conn = 7;
    bleu_hub_event(&h, &e, 2);
    CHECK(bleu_hub_next_event(&h, &o, &out));
    CHECK_EQ(o, 3);
    /* By its handle before its token. */
    e.token = 1u;
    e.kind = BLEU_EV_CONN_UPDATE;
    bleu_hub_event(&h, &e, 3);
    CHECK(bleu_hub_next_event(&h, &o, &out));
    CHECK_EQ(o, 3);
    /* An unknown handle: by the token, else stray. */
    e.conn = 8;
    bleu_hub_event(&h, &e, 4);
    CHECK(bleu_hub_next_event(&h, &o, &out));
    CHECK_EQ(o, 0);
    e.token = 0u;
    bleu_hub_event(&h, &e, 5);
    CHECK(!bleu_hub_next_event(&h, &o, &out));
    CHECK_EQ(h.stray, 1);
    /* A DISCONNECT without a token ends the handle's link. */
    e.kind = BLEU_EV_DISCONNECT;
    e.conn = 7;
    bleu_hub_event(&h, &e, 6);
    CHECK(bleu_hub_next_event(&h, &o, &out));
    CHECK_EQ(o, 3);
    CHECK(!h.c[3].linked);
    CHECK_EQ(h.c[3].events, 4);
    CHECK_EQ(h.c[0].events, 2);
}

TEST_CASE(a_scan_during_another_clients_connect_cancels_it_and_runs)
{
    static const uint8_t ad[] = { 0x02, 0x01, 0x06, 0x03, 0x03, 0xF0, 0x18 };
    rig_t *r = rig_init(2);
    r->s[0].absent = true;
    rig_on(r, 0, 0);
    bleu_enable(&r->b[1], true, 0);
    bsim_adv(&r->s[1], 50, &k_addr[2], -55, ad, sizeof ad);
    rig_run(r, 0, 100);
    CHECK_EQ(r->b[0].st, BLEU_ST_CONNECTING);
    CHECK(bleu_scan(&r->b[1], 100));
    rig_tick(r, 100);
    /* The hub cancels client 0's connect; the scan waits for its end. */
    CHECK_EQ(rig_count(r, BLEU_ACT_CANCEL, -1), 1);
    CHECK_EQ(rig_count(r, BLEU_ACT_SCAN, -1), 0);
    rig_run(r, 101, 102);
    CHECK_EQ(rig_count(r, BLEU_ACT_SCAN, -1), 0);
    rig_tick(r, 102);
    /* The cancel's CONNECT came at 102: busy for client 0, no error. */
    CHECK_EQ(rig_count(r, BLEU_ACT_SCAN, 1), 1);
    CHECK_EQ(rig_nth(r, BLEU_ACT_SCAN, 0)->t, 102);
    CHECK_EQ(r->b[0].st, BLEU_ST_WAIT);
    CHECK_EQ(r->b[0].err, BLEU_ERR_NONE);
    CHECK_EQ(r->b[0].stray, 0);
    CHECK_EQ(r->hub.c[0].busy, 1);
    CHECK_EQ(r->hub.gap, BLEU_HUB_GAP_SCAN);
    /* Client 0's retry at 1102 is busy while client 1 scans. */
    rig_run(r, 103, 1200);
    CHECK_EQ(rig_count(r, BLEU_ACT_CONNECT, 0), 1);
    CHECK_EQ(r->hub.c[0].busy, 2);
    CHECK_EQ(r->b[1].list.found_n, 1);
    CHECK(r->b[1].scanning);
    /* Client 1's scan ends at 100 + 10,000ms; client 0's next retry
     * connects. */
    r->s[0].absent = false;
    rig_run(r, 1200, 11200);
    CHECK(!r->b[1].scanning);
    CHECK_EQ(r->b[1].scan_status, 0);
    CHECK(bleu_link_up(&r->b[0]));
    CHECK_EQ(r->b[0].err, BLEU_ERR_NONE);
    CHECK(rig_nth(r, BLEU_ACT_CONNECT, 1)->t >= 10100u);
}

TEST_CASE(a_connect_that_came_up_before_the_cancel_stays_and_the_scan_runs)
{
    rig_t *r = rig_init(2);
    r->s[0].absent = true;
    r->s[0].cancel_race = true;
    rig_on(r, 0, 0);
    bleu_enable(&r->b[1], true, 0);
    rig_run(r, 0, 100);
    CHECK(bleu_scan(&r->b[1], 100));
    rig_run(r, 100, 103);
    CHECK_EQ(rig_count(r, BLEU_ACT_SCAN, 1), 1);
    rig_run(r, 103, 600);
    CHECK(bleu_can_send(&r->b[0]));
    CHECK_EQ(r->b[0].err, BLEU_ERR_NONE);
    CHECK(r->b[1].scanning);
}

TEST_CASE(a_second_scan_is_answered_busy)
{
    rig_t *r = rig_init(2);
    bleu_enable(&r->b[0], true, 0);
    bleu_enable(&r->b[1], true, 0);
    CHECK(bleu_scan(&r->b[0], 0));
    rig_run(r, 0, 10);
    CHECK(bleu_scan(&r->b[1], 10));
    rig_tick(r, 10);
    CHECK_EQ(rig_count(r, BLEU_ACT_SCAN, -1), 1);
    CHECK(!r->b[1].scanning);
    CHECK_EQ(r->b[1].scan_status, BLEU_STATUS_BUSY);
    CHECK_EQ(r->hub.c[1].busy, 1);
    CHECK(r->b[0].scanning);
    /* A scan waiting behind a cancel makes a third one busy too. */
    bleu_hub_t h;
    bleu_hub_init(&h);
    for (uint8_t i = 0; i < 3u; ++i) {
        CHECK(bleu_hub_add(&h, i));
    }
    bleu_act_t a;
    memset(&a, 0, sizeof a);
    a.kind = BLEU_ACT_CONNECT;
    a.token = 1u;
    a.ms = BLEU_CONNECT_MS;
    bleu_hub_action(&h, 0, &a, 0);
    a.kind = BLEU_ACT_SCAN;
    a.token = (uint16_t)((1u << BLEU_OWNER_SHIFT) | 1u);
    a.ms = BLEU_SCAN_MS;
    bleu_hub_action(&h, 1, &a, 1);
    CHECK(h.held);
    a.token = (uint16_t)((2u << BLEU_OWNER_SHIFT) | 1u);
    bleu_hub_action(&h, 2, &a, 2);
    uint8_t o;
    bleu_evt_t e;
    CHECK(bleu_hub_next_event(&h, &o, &e));
    CHECK_EQ(o, 2);
    CHECK_EQ(e.kind, BLEU_EV_SCAN_END);
    CHECK_EQ(e.status, BLEU_STATUS_BUSY);
    CHECK_EQ(h.held_owner, 1);
}

/* Client 0 is up on handle 1 and its peer drops without a DISCONNECT;
 * client 1's peer then gets handle 1 from the host stack. */
static rig_t *lost_disconnect(void)
{
    rig_t *r = rig_init(2);
    r->s[1].next_conn = 1;
    rig_on(r, 0, 0);
    rig_run(r, 0, 500);
    CHECK(bleu_can_send(&r->b[0]));
    CHECK_EQ(r->b[0].conn, 1);
    r->s[0].connected = false;
    r->s[0].subscribed = false;
    r->s[0].absent = true;
    rig_on(r, 1, 500);
    rig_run(r, 500, 1500);
    return r;
}

TEST_CASE(a_lost_disconnect_ends_the_old_link_when_its_handle_comes_back)
{
    rig_t *r = lost_disconnect();
    CHECK(bleu_can_send(&r->b[1]));
    CHECK_EQ(r->b[1].conn, 1);
    CHECK(r->hub.c[1].linked);
    CHECK_EQ(r->hub.c[1].conn, 1);
    CHECK(!r->hub.c[0].linked);
    CHECK_EQ(r->hub.c[0].cut, 1);
    /* Client 0's link ended as a drop, and it tries again. */
    CHECK(!bleu_link_up(&r->b[0]));
    CHECK_EQ(r->b[0].err, BLEU_ERR_LOST);
    CHECK_EQ(r->b[0].reason, BLEU_STATUS_LOST);
    CHECK_EQ(rig_count(r, BLEU_ACT_CONNECT, 0), 1);
    rig_run(r, 1500, 1600);
    CHECK_EQ(rig_count(r, BLEU_ACT_CONNECT, 0), 2);
    /* Handle 1's notifications are client 1's. */
    CHECK(r->b[1].notifies > 10u);
    CHECK_EQ(r->b[1].notifies, r->s[1].notifies_sent);
    CHECK_EQ(r->b[1].stray, 0);
}

TEST_CASE(a_held_back_disconnect_reaches_its_own_client)
{
    rig_t *r = lost_disconnect();
    const uint16_t old = rig_nth(r, BLEU_ACT_CONNECT, 0)->a.token;
    CHECK_EQ(bleu_token_owner(old), 0);
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_DISCONNECT;
    e.conn = 1;
    e.token = old;
    e.status = 0x08u;
    bleu_hub_event(&r->hub, &e, 1500);
    rig_deliver(r, 1500);
    CHECK(bleu_link_up(&r->b[1]));
    CHECK(r->hub.c[1].linked);
    rig_run(r, 1500, 1600);
    CHECK(bleu_can_send(&r->b[1]));
}

TEST_CASE(another_clients_cancel_stop_and_handle_are_left_alone)
{
    bleu_hub_t h;
    bleu_hub_init(&h);
    CHECK(bleu_hub_add(&h, 0));
    CHECK(bleu_hub_add(&h, 1));
    bleu_act_t a, out;
    memset(&a, 0, sizeof a);
    a.kind = BLEU_ACT_CONNECT;
    a.token = 1u;
    a.ms = BLEU_CONNECT_MS;
    bleu_hub_action(&h, 0, &a, 0);
    CHECK(bleu_hub_next_action(&h, &out));
    CHECK_EQ(out.kind, BLEU_ACT_CONNECT);
    memset(&a, 0, sizeof a);
    a.kind = BLEU_ACT_CANCEL;
    bleu_hub_action(&h, 1, &a, 1);
    CHECK(!bleu_hub_next_action(&h, &out));
    bleu_hub_action(&h, 0, &a, 2);
    CHECK(bleu_hub_next_action(&h, &out));
    CHECK_EQ(out.kind, BLEU_ACT_CANCEL);
    /* Client 0's connect comes up on handle 4. */
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_CONNECT;
    e.token = 1u;
    e.conn = 4;
    bleu_hub_event(&h, &e, 3);
    uint8_t o;
    CHECK(bleu_hub_next_event(&h, &o, &e));
    CHECK_EQ(h.gap, BLEU_HUB_GAP_FREE);
    /* Client 1's write and disconnect on handle 4 are refused. */
    memset(&a, 0, sizeof a);
    a.kind = BLEU_ACT_WRITE;
    a.conn = 4;
    a.token = (uint16_t)((1u << BLEU_OWNER_SHIFT) | 2u);
    bleu_hub_action(&h, 1, &a, 4);
    CHECK(!bleu_hub_next_action(&h, &out));
    CHECK(bleu_hub_next_event(&h, &o, &e));
    CHECK_EQ(o, 1);
    CHECK_EQ(e.kind, BLEU_EV_WRITE_DONE);
    CHECK_EQ(e.token, a.token);
    CHECK_EQ(e.status, BLEU_STATUS_LOST);
    a.kind = BLEU_ACT_DISCONNECT;
    a.token = 0u;
    bleu_hub_action(&h, 1, &a, 5);
    CHECK(!bleu_hub_next_action(&h, &out));
    CHECK(!bleu_hub_next_event(&h, &o, &e));
    CHECK_EQ(h.refused, 2);
    /* Client 0's own pass. */
    bleu_hub_action(&h, 0, &a, 6);
    CHECK(bleu_hub_next_action(&h, &out));
    CHECK_EQ(out.kind, BLEU_ACT_DISCONNECT);
    a.kind = BLEU_ACT_READ;
    a.token = 3u;
    bleu_hub_action(&h, 0, &a, 7);
    CHECK(bleu_hub_next_action(&h, &out));
    CHECK_EQ(out.kind, BLEU_ACT_READ);
    /* Client 0 scans; client 1's SCAN_STOP is dropped, client 0's goes
     * and frees the slot. */
    memset(&a, 0, sizeof a);
    a.kind = BLEU_ACT_SCAN;
    a.token = 5u;
    a.ms = BLEU_SCAN_MS;
    bleu_hub_action(&h, 0, &a, 8);
    CHECK(bleu_hub_next_action(&h, &out));
    a.kind = BLEU_ACT_SCAN_STOP;
    a.token = 0u;
    bleu_hub_action(&h, 1, &a, 9);
    CHECK(!bleu_hub_next_action(&h, &out));
    CHECK_EQ(h.gap, BLEU_HUB_GAP_SCAN);
    bleu_hub_action(&h, 0, &a, 10);
    CHECK(bleu_hub_next_action(&h, &out));
    CHECK_EQ(out.kind, BLEU_ACT_SCAN_STOP);
    CHECK_EQ(h.gap, BLEU_HUB_GAP_FREE);
}

TEST_CASE(a_connect_slot_without_its_answer_frees_after_7000ms_not_6999)
{
    bleu_hub_t h;
    bleu_hub_init(&h);
    CHECK(bleu_hub_add(&h, 0));
    CHECK(bleu_hub_add(&h, 1));
    bleu_act_t a, out;
    memset(&a, 0, sizeof a);
    a.kind = BLEU_ACT_CONNECT;
    a.token = 1u;
    a.ms = BLEU_CONNECT_MS;
    bleu_hub_action(&h, 0, &a, 100);
    CHECK(bleu_hub_next_action(&h, &out));
    bleu_hub_tick(&h, 100u + BLEU_CONNECT_MS + BLEU_SETTLE_MS - 1u);
    CHECK_EQ(h.gap, BLEU_HUB_GAP_CONNECT);
    bleu_hub_tick(&h, 100u + BLEU_CONNECT_MS + BLEU_SETTLE_MS);
    CHECK_EQ(h.gap, BLEU_HUB_GAP_FREE);
    a.token = (uint16_t)((1u << BLEU_OWNER_SHIFT) | 1u);
    bleu_hub_action(&h, 1, &a, 7100);
    CHECK(bleu_hub_next_action(&h, &out));
    CHECK_EQ(out.token, a.token);
    /* A scan's slot: its 10,000ms and 2,000ms more, across the wrap. */
    bleu_hub_init(&h);
    CHECK(bleu_hub_add(&h, 0));
    memset(&a, 0, sizeof a);
    a.kind = BLEU_ACT_SCAN;
    a.token = 1u;
    a.ms = BLEU_SCAN_MS;
    const uint32_t t0 = 0xFFFFFF00u;
    bleu_hub_action(&h, 0, &a, t0);
    bleu_hub_tick(&h, t0 + BLEU_SCAN_MS + BLEU_SETTLE_MS - 1u);
    CHECK_EQ(h.gap, BLEU_HUB_GAP_SCAN);
    bleu_hub_tick(&h, t0 + BLEU_SCAN_MS + BLEU_SETTLE_MS);
    CHECK_EQ(h.gap, BLEU_HUB_GAP_FREE);
}

TEST_CASE(a_cancel_without_its_event_lets_the_scan_go_after_2000ms)
{
    bleu_hub_t h;
    bleu_hub_init(&h);
    CHECK(bleu_hub_add(&h, 0));
    CHECK(bleu_hub_add(&h, 1));
    bleu_act_t a, out;
    memset(&a, 0, sizeof a);
    a.kind = BLEU_ACT_CONNECT;
    a.token = 1u;
    a.ms = BLEU_CONNECT_MS;
    bleu_hub_action(&h, 0, &a, 0);
    CHECK(bleu_hub_next_action(&h, &out));
    a.kind = BLEU_ACT_SCAN;
    a.token = (uint16_t)((1u << BLEU_OWNER_SHIFT) | 1u);
    a.ms = BLEU_SCAN_MS;
    bleu_hub_action(&h, 1, &a, 10);
    CHECK(bleu_hub_next_action(&h, &out));
    CHECK_EQ(out.kind, BLEU_ACT_CANCEL);
    bleu_hub_tick(&h, 10u + BLEU_SETTLE_MS - 1u);
    CHECK(!bleu_hub_next_action(&h, &out));
    bleu_hub_tick(&h, 10u + BLEU_SETTLE_MS);
    CHECK(bleu_hub_next_action(&h, &out));
    CHECK_EQ(out.kind, BLEU_ACT_SCAN);
    CHECK_EQ(out.token, a.token);
    CHECK_EQ(h.gap, BLEU_HUB_GAP_SCAN);
    CHECK_EQ(h.gap_owner, 1);
    /* The late CONNECT is client 0's, unchanged: not the slot's now. */
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_CONNECT;
    e.token = 1u;
    e.status = 9u;
    bleu_hub_event(&h, &e, 2100);
    uint8_t o;
    CHECK(bleu_hub_next_event(&h, &o, &e));
    CHECK_EQ(o, 0);
    CHECK_EQ(e.status, 9);
    CHECK_EQ(h.gap, BLEU_HUB_GAP_SCAN);
    /* Client 1 stops its scan while it waits: nothing goes out. */
    bleu_hub_init(&h);
    CHECK(bleu_hub_add(&h, 0));
    CHECK(bleu_hub_add(&h, 1));
    a.kind = BLEU_ACT_CONNECT;
    a.token = 1u;
    bleu_hub_action(&h, 0, &a, 0);
    a.kind = BLEU_ACT_SCAN;
    a.token = (uint16_t)((1u << BLEU_OWNER_SHIFT) | 1u);
    bleu_hub_action(&h, 1, &a, 1);
    while (bleu_hub_next_action(&h, &out)) {
    }
    a.kind = BLEU_ACT_SCAN_STOP;
    a.token = 0u;
    bleu_hub_action(&h, 1, &a, 2);
    CHECK(!h.held);
    CHECK(!bleu_hub_next_action(&h, &out));
    bleu_hub_tick(&h, 2100);
    CHECK(!bleu_hub_next_action(&h, &out));
}

TEST_CASE(the_slot_ends_on_scan_end_and_on_a_failed_establishment)
{
    bleu_hub_t h;
    bleu_hub_init(&h);
    CHECK(bleu_hub_add(&h, 0));
    CHECK(bleu_hub_add(&h, 1));
    bleu_act_t a, out;
    memset(&a, 0, sizeof a);
    a.kind = BLEU_ACT_SCAN;
    a.token = 1u;
    a.ms = BLEU_SCAN_MS;
    bleu_hub_action(&h, 0, &a, 0);
    CHECK(bleu_hub_next_action(&h, &out));
    /* A SCAN_END of another token leaves the slot; the scan's frees it. */
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_SCAN_END;
    e.token = 2u;
    bleu_hub_event(&h, &e, 1);
    CHECK_EQ(h.gap, BLEU_HUB_GAP_SCAN);
    e.token = 1u;
    bleu_hub_event(&h, &e, 2);
    CHECK_EQ(h.gap, BLEU_HUB_GAP_FREE);
    uint8_t o;
    bleu_evt_t got;
    CHECK(bleu_hub_next_event(&h, &o, &got));
    CHECK(bleu_hub_next_event(&h, &o, &got));
    CHECK_EQ(o, 0);
    /* A report after the scan goes to the last scan's client. */
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_ADV;
    bleu_hub_event(&h, &e, 3);
    CHECK(bleu_hub_next_event(&h, &o, &got));
    CHECK_EQ(o, 0);
    CHECK_EQ(got.kind, BLEU_EV_ADV);
    /* Client 1's connect fails to be established: its DISCONNECT with the
     * connect's token ends the slot, and client 0 may connect. */
    memset(&a, 0, sizeof a);
    a.kind = BLEU_ACT_CONNECT;
    a.token = (uint16_t)((1u << BLEU_OWNER_SHIFT) | 4u);
    a.ms = BLEU_CONNECT_MS;
    bleu_hub_action(&h, 1, &a, 4);
    CHECK(bleu_hub_next_action(&h, &out));
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_DISCONNECT;
    e.token = a.token;
    e.conn = 3;
    e.status = 0x23Eu;
    bleu_hub_event(&h, &e, 5);
    CHECK_EQ(h.gap, BLEU_HUB_GAP_FREE);
    CHECK(bleu_hub_next_event(&h, &o, &got));
    CHECK_EQ(o, 1);
    CHECK_EQ(got.status, 0x23E);
    a.token = 5u;
    bleu_hub_action(&h, 0, &a, 6);
    CHECK(bleu_hub_next_action(&h, &out));
    CHECK_EQ(out.token, 5);
}

TEST_CASE(each_gatt_step_on_another_clients_handle_is_answered_lost)
{
    static const struct {
        uint8_t act, ev;
    } k[] = {
        { BLEU_ACT_DISC_SVC, BLEU_EV_SVC_DONE },
        { BLEU_ACT_DISC_CHRS, BLEU_EV_CHR_DONE },
        { BLEU_ACT_DISC_DSCS, BLEU_EV_DSC_DONE },
        { BLEU_ACT_MTU, BLEU_EV_MTU },
        { BLEU_ACT_SUBSCRIBE, BLEU_EV_WRITE_DONE },
        { BLEU_ACT_WRITE, BLEU_EV_WRITE_DONE },
        { BLEU_ACT_WRITE_NR, BLEU_EV_WRITE_DONE },
        { BLEU_ACT_READ, BLEU_EV_READ },
    };
    bleu_hub_t h;
    bleu_hub_init(&h);
    CHECK(bleu_hub_add(&h, 0));
    CHECK(bleu_hub_add(&h, 2));
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_CONNECT;
    e.token = 1u;
    e.conn = 6;
    bleu_hub_event(&h, &e, 0);
    uint8_t o;
    CHECK(bleu_hub_next_event(&h, &o, &e));
    for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
        bleu_act_t a, out;
        memset(&a, 0, sizeof a);
        a.kind = k[i].act;
        a.conn = 6;
        a.token = (uint16_t)((2u << BLEU_OWNER_SHIFT) | (i + 1u));
        bleu_hub_action(&h, 2, &a, 1);
        CHECK(!bleu_hub_next_action(&h, &out));
        CHECK(bleu_hub_next_event(&h, &o, &e));
        CHECK_EQ(o, 2);
        CHECK_EQ(e.kind, k[i].ev);
        CHECK_EQ(e.token, a.token);
        CHECK_EQ(e.status, BLEU_STATUS_LOST);
        /* The same step on a handle nobody holds goes to the host. */
        a.conn = 7;
        bleu_hub_action(&h, 2, &a, 2);
        CHECK(bleu_hub_next_action(&h, &out));
        CHECK_EQ(out.kind, k[i].act);
    }
    CHECK_EQ(h.refused, sizeof k / sizeof k[0]);
}

/* A client that holds the slot asks again: after its own settle ran out
 * it scans while its connect is still the slot's, connects while its own
 * scan is, or scans again.  Each goes to the host stack at once, with no
 * cancel and no busy answer. */
TEST_CASE(a_client_that_holds_the_slot_takes_it_again)
{
    bleu_hub_t h;
    bleu_hub_init(&h);
    CHECK(bleu_hub_add(&h, 0));
    CHECK(bleu_hub_add(&h, 1));
    bleu_act_t a, out;
    memset(&a, 0, sizeof a);
    a.kind = BLEU_ACT_CONNECT;
    a.token = 1u;
    a.ms = BLEU_CONNECT_MS;
    bleu_hub_action(&h, 0, &a, 0);
    CHECK(bleu_hub_next_action(&h, &out));
    a.kind = BLEU_ACT_SCAN;
    a.token = 2u;
    a.ms = BLEU_SCAN_MS;
    bleu_hub_action(&h, 0, &a, 2000);
    CHECK(bleu_hub_next_action(&h, &out));
    CHECK_EQ(out.kind, BLEU_ACT_SCAN);
    CHECK_EQ(out.token, 2);
    CHECK(!bleu_hub_next_action(&h, &out));
    CHECK(!h.held);
    CHECK_EQ(h.gap, BLEU_HUB_GAP_SCAN);
    CHECK_EQ(h.gap_token, 2);
    /* Its scan again, then a connect while its scan holds the slot. */
    a.token = 3u;
    bleu_hub_action(&h, 0, &a, 2001);
    CHECK(bleu_hub_next_action(&h, &out));
    CHECK_EQ(out.token, 3);
    a.kind = BLEU_ACT_CONNECT;
    a.token = 4u;
    a.ms = BLEU_CONNECT_MS;
    bleu_hub_action(&h, 0, &a, 2002);
    CHECK(bleu_hub_next_action(&h, &out));
    CHECK_EQ(out.kind, BLEU_ACT_CONNECT);
    CHECK_EQ(h.gap, BLEU_HUB_GAP_CONNECT);
    CHECK_EQ(h.gap_token, 4);
    /* Its connect again while its connect holds the slot. */
    a.token = 5u;
    bleu_hub_action(&h, 0, &a, 2003);
    CHECK(bleu_hub_next_action(&h, &out));
    CHECK_EQ(out.token, 5);
    uint8_t o;
    bleu_evt_t e;
    CHECK(!bleu_hub_next_event(&h, &o, &e));
    CHECK_EQ(h.c[0].busy, 0);
    /* Client 1 is still kept out. */
    a.token = (uint16_t)((1u << BLEU_OWNER_SHIFT) | 1u);
    bleu_hub_action(&h, 1, &a, 2004);
    CHECK(!bleu_hub_next_action(&h, &out));
    CHECK(bleu_hub_next_event(&h, &o, &e));
    CHECK_EQ(o, 1);
    CHECK_EQ(e.status, BLEU_STATUS_BUSY);
}

/* Client 0 is linked on handle 5 by its second connect; the DISCONNECT of
 * its first link, on handle 3, comes late with that connect's token.  It
 * reaches client 0 and leaves the link on handle 5 its own. */
TEST_CASE(a_stale_disconnect_of_an_old_handle_keeps_the_new_link)
{
    bleu_hub_t h;
    bleu_hub_init(&h);
    CHECK(bleu_hub_add(&h, 0));
    CHECK(bleu_hub_add(&h, 1));
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_CONNECT;
    e.token = 7u;
    e.conn = 5;
    bleu_hub_event(&h, &e, 0);
    uint8_t o;
    bleu_evt_t got;
    CHECK(bleu_hub_next_event(&h, &o, &got));
    CHECK(h.c[0].linked);
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_DISCONNECT;
    e.token = 6u;
    e.conn = 3;
    e.status = 0x08u;
    bleu_hub_event(&h, &e, 1);
    CHECK(bleu_hub_next_event(&h, &o, &got));
    CHECK_EQ(o, 0);
    CHECK_EQ(got.conn, 3);
    CHECK(h.c[0].linked);
    CHECK_EQ(h.c[0].conn, 5);
    /* Handle 5's notifications are still client 0's. */
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_NOTIFY;
    e.conn = 5;
    bleu_hub_event(&h, &e, 2);
    CHECK(bleu_hub_next_event(&h, &o, &got));
    CHECK_EQ(o, 0);
    /* Its own DISCONNECT of handle 5 ends it. */
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_DISCONNECT;
    e.token = 7u;
    e.conn = 5;
    bleu_hub_event(&h, &e, 3);
    CHECK(bleu_hub_next_event(&h, &o, &got));
    CHECK(!h.c[0].linked);
}

TEST_CASE(strays_full_queues_and_null)
{
    bleu_hub_t h;
    bleu_hub_init(&h);
    CHECK(bleu_hub_add(&h, 0));
    CHECK(!bleu_hub_add(&h, BLEU_HUB_CLIENTS));
    CHECK(!bleu_hub_add(NULL, 0));
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    /* An advertising report before any scan; a token of owner 3, never
     * added; an answer with no token. */
    e.kind = BLEU_EV_ADV;
    bleu_hub_event(&h, &e, 0);
    e.kind = BLEU_EV_MTU;
    e.token = (uint16_t)((3u << BLEU_OWNER_SHIFT) | 1u);
    bleu_hub_event(&h, &e, 0);
    e.token = 0u;
    bleu_hub_event(&h, &e, 0);
    e.kind = BLEU_EV_DISCONNECT;
    e.conn = 9;
    bleu_hub_event(&h, &e, 0);
    CHECK_EQ(h.stray, 4);
    uint8_t o;
    bleu_evt_t out;
    CHECK(!bleu_hub_next_event(&h, &o, &out));
    /* An action of a client never added is refused. */
    bleu_act_t a;
    memset(&a, 0, sizeof a);
    a.kind = BLEU_ACT_READ;
    bleu_hub_action(&h, 2, &a, 0);
    CHECK_EQ(h.refused, 1);
    /* Past BLEU_HUB_OUT the out queues drop and count. */
    for (unsigned i = 0; i < BLEU_HUB_OUT + 2u; ++i) {
        bleu_hub_action(&h, 0, &a, 0);
    }
    CHECK_EQ(h.act_n, BLEU_HUB_OUT);
    e.kind = BLEU_EV_READ;
    e.token = 1u;
    for (unsigned i = 0; i < BLEU_HUB_OUT + 1u; ++i) {
        bleu_hub_event(&h, &e, 0);
    }
    CHECK_EQ(h.ev_n, BLEU_HUB_OUT);
    CHECK_EQ(h.drops, 3);
    bleu_hub_init(NULL);
    bleu_hub_event(NULL, &e, 0);
    bleu_hub_event(&h, NULL, 0);
    bleu_hub_action(NULL, 0, &a, 0);
    bleu_hub_action(&h, 0, NULL, 0);
    bleu_hub_tick(NULL, 0);
    CHECK(!bleu_hub_next_event(NULL, &o, &out));
    CHECK(!bleu_hub_next_event(&h, NULL, &out));
    CHECK(!bleu_hub_next_action(NULL, &a));
    CHECK(!bleu_hub_next_action(&h, NULL));
    a.kind = BLEU_ACT_NONE;
    const uint8_t n = h.act_n;
    bleu_hub_init(&h);
    CHECK(bleu_hub_add(&h, 0));
    bleu_hub_action(&h, 0, &a, 0);
    CHECK_EQ(h.act_n, 0);
    CHECK(n > 0u);
}

int main(void)
{
    RUN(one_client_runs_as_without_the_hub);
    RUN(three_clients_come_up_one_connect_at_a_time);
    RUN(gatt_answers_go_by_token_notifications_by_handle);
    RUN(a_scan_during_another_clients_connect_cancels_it_and_runs);
    RUN(a_connect_that_came_up_before_the_cancel_stays_and_the_scan_runs);
    RUN(a_second_scan_is_answered_busy);
    RUN(a_lost_disconnect_ends_the_old_link_when_its_handle_comes_back);
    RUN(a_held_back_disconnect_reaches_its_own_client);
    RUN(another_clients_cancel_stop_and_handle_are_left_alone);
    RUN(a_connect_slot_without_its_answer_frees_after_7000ms_not_6999);
    RUN(a_cancel_without_its_event_lets_the_scan_go_after_2000ms);
    RUN(the_slot_ends_on_scan_end_and_on_a_failed_establishment);
    RUN(each_gatt_step_on_another_clients_handle_is_answered_lost);
    RUN(a_client_that_holds_the_slot_takes_it_again);
    RUN(a_stale_disconnect_of_an_old_handle_keeps_the_new_link);
    RUN(strays_full_queues_and_null);
    return test_summary("bleuart_hub");
}
