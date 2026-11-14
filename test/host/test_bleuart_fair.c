/*
 * The queue of protocols/bleuart's hub (bleuart_hub.c): every connect that
 * waits for the host stack's one scan or connect gets the slot after at
 * most one attempt of each other client.
 *
 * Under test: an unhurried client beside two peers that never answer, on
 * 1,000ms retries 1ms behind their grid, connecting after their two
 * attempts; a sweep of 2 to 4 such peers at three phase patterns with
 * an unhurried client switched on at 25 offsets, every client's wait from
 * its first busy answer to its slot within one attempt of each other
 * client, and no client leaving the queue without the slot; the queue's
 * order: first come first, a hurried client ahead of the unhurried ones
 * and behind the hurried ones, a turn kept for an unhurried client given
 * to a hurried one and its client back in front; a connect a scan cut
 * waiting in front; an unhurried client whose turn is kept answered busy
 * while a scan holds the slot; a client that does not come back leaving
 * the queue 200ms after the slot freed and its retry was due, not 199ms.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "hub_rig.h"

/** @brief One attempt of an absent peer as the hub sees it: the connect,
 *         the cancel's event 2ms on, the next client's 20ms retry and one
 *         tick, ms. */
#define ATTEMPT_MS (BLEU_CONNECT_MS + 2u + BLEU_RETRY_TURN_MS + 1u)
/** @brief A measured time on the suite's output. */
#define T_LOG(...) (printf("  " __VA_ARGS__), printf("\n"))

/* The first CONNECT of owner @p o the host stack took, or NULL. */
static const bsim_log_t *first_connect(const rig_t *r, uint8_t o)
{
    for (unsigned i = 0; i < r->ndone; ++i) {
        const bleu_act_t *a = &r->done[i].a;
        if (a->kind == BLEU_ACT_CONNECT && a->token != 0u
            && bleu_token_owner(a->token) == o) {
            return &r->done[i];
        }
    }
    return NULL;
}

/* Clients 0 and 2 bound to peers that never answer, client 1 bound 1ms on,
 * unhurried.  Client 0's connect holds the slot from 0ms to its cancel's
 * event at 5,002ms; client 2 waits first and takes it next.  Client 1, on
 * 1,000ms retries 1ms behind the others' grid, finds the slot taken at
 * every retry; in the queue it waits behind client 2 and connects after
 * two attempts. */
TEST_CASE(an_unhurried_connect_gets_the_slot_within_two_attempts)
{
    rig_t *r = rig_init(3);
    r->s[0].absent = true;
    r->s[2].absent = true;
    rig_on(r, 0, 0);
    rig_on(r, 2, 0);
    rig_tick(r, 0);
    rig_on(r, 1, 1);
    rig_run(r, 1, 40000);
    const bsim_log_t *c1 = first_connect(r, 1);
    CHECK(c1 != NULL);
    if (c1 != NULL) {
        T_LOG("unhurried beside 2 absent: CONNECT %ums after the bind",
              (unsigned)(c1->t - 1u));
        CHECK(c1->t >= 2u * (BLEU_CONNECT_MS + 2u));
        CHECK(c1->t <= 1u + 2u * ATTEMPT_MS);
        CHECK_EQ(c1->a.flags, 0u);
    }
    CHECK(bleu_link_up(&r->b[1]));
    CHECK_EQ(r->b[1].err, BLEU_ERR_NONE);
    CHECK(r->hub.c[1].busy < 300u);
}

/* What a sweep's run saw of each client's waits. */
typedef struct {
    bool     waiting[NCL]; /* in the queue or its turn kept */
    uint32_t since[NCL];   /* from when */
    uint32_t worst[NCL];   /* the longest wait to the slot */
    unsigned takes[NCL];   /* waits that ended with the slot */
    unsigned left;         /* waits that ended without it */
} watch_t;

/* Whether owner @p o waits for a turn. */
static bool waits(const bleu_hub_t *h, uint8_t o)
{
    if (h->turn_set && h->turn_owner == o) {
        return true;
    }
    for (uint8_t i = 0; i < h->qn; ++i) {
        if (h->q[i] == o) {
            return true;
        }
    }
    return false;
}

/* Each client's wait after the ms at @p t. */
static void watch(watch_t *w, const rig_t *r, uint32_t t)
{
    for (unsigned o = 0; o < r->n; ++o) {
        const bool now = waits(&r->hub, (uint8_t)o);
        if (now && !w->waiting[o]) {
            w->since[o] = t;
        } else if (!now && w->waiting[o]) {
            if (r->hub.gap == BLEU_HUB_GAP_CONNECT
                && r->hub.gap_owner == o) {
                const uint32_t d = t - w->since[o];
                w->worst[o] = d > w->worst[o] ? d : w->worst[o];
                ++w->takes[o];
            } else {
                ++w->left;
            }
        }
        w->waiting[o] = now;
    }
}

/* @p k absent peers, owners 0 to k - 1, switched on @p gap ms apart; the
 * unhurried client k on its present peer switched on at @p at.  Each
 * client's waits over 40,000ms into @p w. */
static void sweep_run(unsigned k, uint32_t gap, uint32_t at, watch_t *w)
{
    memset(w, 0, sizeof *w);
    rig_t *r = rig_init(k + 1u);
    for (unsigned i = 0; i < k; ++i) {
        r->s[i].absent = true;
    }
    for (uint32_t t = 0; t < 40000u; ++t) {
        for (unsigned i = 0; i <= k; ++i) {
            const uint32_t on = i < k ? i * gap : at;
            if (t == on) {
                rig_on(r, i, t);
            }
        }
        rig_tick(r, t);
        watch(w, r, t);
    }
    for (unsigned o = 0; o <= k; ++o) {
        if (w->waiting[o]) {
            const uint32_t d = 40000u - w->since[o];
            w->worst[o] = d > w->worst[o] ? d : w->worst[o];
        }
    }
    CHECK(bleu_link_up(&r->b[k]));
    CHECK_EQ(w->takes[k], 1u);
}

/* Every client of 3 to 5 gets the slot within one attempt of each other
 * client: 2 to 4 absent peers on a 1ms, 333ms and 1,000ms grid, the
 * unhurried client switched on every 250ms from 0 to 6,000ms.  For 4
 * absent peers the unhurried client is owner 4, the device update's. */
TEST_CASE(every_client_gets_the_slot_within_one_attempt_of_each_other)
{
    static const uint32_t gaps[] = { 1u, 333u, 1000u };
    for (unsigned k = 2; k <= 4u; ++k) {
        const uint32_t bound = k * ATTEMPT_MS;
        uint32_t worst_present = 0;
        uint32_t worst_absent = 0;
        unsigned runs = 0;
        for (unsigned g = 0; g < sizeof gaps / sizeof gaps[0]; ++g) {
            for (uint32_t at = 0; at <= 6000u; at += 250u) {
                watch_t w;
                sweep_run(k, gaps[g], at, &w);
                ++runs;
                CHECK_EQ(w.left, 0u);
                for (unsigned o = 0; o <= k; ++o) {
                    CHECK(w.worst[o] <= bound);
                    if (o < k) {
                        CHECK(w.takes[o] > 0u);
                        worst_absent = w.worst[o] > worst_absent
                                           ? w.worst[o] : worst_absent;
                    }
                }
                worst_present = w.worst[k] > worst_present ? w.worst[k]
                                                           : worst_present;
            }
        }
        T_LOG("%u absent: %u runs, the unhurried client waits at most %ums, "
              "an absent one %ums; bound %ums", k, runs,
              (unsigned)worst_present, (unsigned)worst_absent,
              (unsigned)bound);
    }
}

/* A CONNECT of owner @p o with token count @p seq, hurried or not. */
static bleu_act_t connect_of(uint8_t o, uint16_t seq, bool hurry)
{
    bleu_act_t a;
    memset(&a, 0, sizeof a);
    a.kind = BLEU_ACT_CONNECT;
    a.token = (uint16_t)(((unsigned)o << BLEU_OWNER_SHIFT) | seq);
    a.ms = (uint16_t)BLEU_CONNECT_MS;
    a.flags = hurry ? BLEU_ACT_F_HURRY : 0u;
    return a;
}

/* The status of the one CONNECT the hub answered to its client, or 0 when
 * it passed the action on to the host stack. */
static uint16_t ask(bleu_hub_t *h, uint8_t o, uint16_t seq, bool hurry,
                    uint32_t t)
{
    const bleu_act_t a = connect_of(o, seq, hurry);
    bleu_hub_action(h, o, &a, t);
    uint8_t who = 0xFFu;
    bleu_evt_t e;
    uint16_t st = 0u;
    while (bleu_hub_next_event(h, &who, &e)) {
        CHECK_EQ(who, o);
        st = e.status;
    }
    bleu_act_t out;
    while (bleu_hub_next_action(h, &out)) {
        CHECK(st == 0u);
    }
    return st;
}

/* The slot's connect, token @p token, ends failed at @p t. */
static void end_connect(bleu_hub_t *h, uint16_t token, uint32_t t)
{
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_CONNECT;
    e.token = token;
    e.status = 9u;
    bleu_hub_event(h, &e, t);
    uint8_t who;
    while (bleu_hub_next_event(h, &who, &e)) { }
}

/* The token of owner @p o's CONNECT with count @p seq. */
static uint16_t tok(uint8_t o, uint16_t seq)
{
    return (uint16_t)(((unsigned)o << BLEU_OWNER_SHIFT) | seq);
}

/* Clients 1, 2 and 3 ask while client 0's connect runs: 1 comes first and
 * its turn is kept, 2 and 3 wait in that order, an ask again keeps the
 * place.  A hurried client 4 takes the turn kept for 1, which goes back
 * in front of 2.  The slot then goes 4, 1, 2, 3. */
TEST_CASE(the_queue_is_first_come_first_with_hurried_clients_ahead)
{
    bleu_hub_t h;
    bleu_hub_init(&h);
    for (uint8_t o = 0; o < BLEU_HUB_CLIENTS; ++o) {
        CHECK(bleu_hub_add(&h, o));
    }
    CHECK_EQ(ask(&h, 0, 1, false, 0), 0u);
    CHECK_EQ(ask(&h, 1, 1, false, 10), BLEU_STATUS_TURN);
    CHECK_EQ(ask(&h, 2, 1, false, 20), BLEU_STATUS_BUSY);
    CHECK_EQ(ask(&h, 3, 1, false, 30), BLEU_STATUS_BUSY);
    CHECK_EQ(ask(&h, 2, 2, false, 1020), BLEU_STATUS_BUSY);
    CHECK_EQ(h.qn, 2u);
    CHECK_EQ(h.q[0], 2u);
    CHECK_EQ(h.q[1], 3u);
    CHECK_EQ(ask(&h, 4, 1, true, 1500), BLEU_STATUS_TURN);
    CHECK(h.turn_hurry);
    CHECK_EQ(h.turn_owner, 4u);
    CHECK_EQ(h.qn, 3u);
    CHECK_EQ(h.q[0], 1u);
    CHECK_EQ(ask(&h, 1, 2, false, 1510), BLEU_STATUS_BUSY);
    end_connect(&h, tok(0, 1), 5002);
    /* The slot is free, the turn 4's: 3 is answered busy. */
    CHECK_EQ(ask(&h, 3, 2, false, 5003), BLEU_STATUS_BUSY);
    CHECK_EQ(ask(&h, 4, 2, true, 5010), 0u);
    CHECK_EQ(h.gap_owner, 4u);
    CHECK(!h.turn_set);
    /* The next ask keeps the turn for the queue's first, client 1. */
    CHECK_EQ(ask(&h, 3, 3, false, 5020), BLEU_STATUS_BUSY);
    CHECK_EQ(h.turn_owner, 1u);
    CHECK_EQ(ask(&h, 1, 3, false, 5030), BLEU_STATUS_TURN);
    end_connect(&h, tok(4, 2), 10010);
    CHECK_EQ(ask(&h, 2, 3, false, 10011), BLEU_STATUS_BUSY);
    CHECK_EQ(ask(&h, 1, 4, false, 10030), 0u);
    end_connect(&h, tok(1, 4), 15030);
    CHECK_EQ(ask(&h, 3, 4, false, 15031), BLEU_STATUS_BUSY);
    CHECK_EQ(h.turn_owner, 2u);
    CHECK_EQ(ask(&h, 2, 4, false, 15040), 0u);
    end_connect(&h, tok(2, 4), 20040);
    CHECK_EQ(ask(&h, 3, 5, false, 20041), 0u);
    CHECK_EQ(h.qn, 0u);
    CHECK(!h.turn_set);
}

/* Two hurried clients: the second goes behind the first and ahead of an
 * unhurried one that came before it; one already queued unhurried moves
 * up when it comes hurried. */
TEST_CASE(a_second_hurried_client_goes_behind_the_first)
{
    bleu_hub_t h;
    bleu_hub_init(&h);
    for (uint8_t o = 0; o < BLEU_HUB_CLIENTS; ++o) {
        CHECK(bleu_hub_add(&h, o));
    }
    CHECK_EQ(ask(&h, 0, 1, false, 0), 0u);
    CHECK_EQ(ask(&h, 1, 1, true, 10), BLEU_STATUS_TURN);
    CHECK_EQ(ask(&h, 2, 1, false, 20), BLEU_STATUS_BUSY);
    CHECK_EQ(ask(&h, 3, 1, false, 30), BLEU_STATUS_BUSY);
    CHECK_EQ(ask(&h, 4, 1, true, 40), BLEU_STATUS_BUSY);
    CHECK_EQ(h.q[0], 4u);
    CHECK_EQ(h.q[1], 2u);
    CHECK_EQ(h.q[2], 3u);
    CHECK_EQ(ask(&h, 3, 2, true, 50), BLEU_STATUS_BUSY);
    CHECK_EQ(h.qn, 3u);
    CHECK_EQ(h.q[0], 4u);
    CHECK_EQ(h.q[1], 3u);
    CHECK_EQ(h.q[2], 2u);
    CHECK(h.c[3].hurry);
    /* Hurried again: it keeps its place. */
    CHECK_EQ(ask(&h, 3, 3, true, 60), BLEU_STATUS_BUSY);
    CHECK_EQ(h.q[1], 3u);
}

/* A scan cuts client 0's connect: its client waits in front of client 1,
 * which asked before.  While the scan holds the slot client 0's kept turn
 * is answered busy, not with the turn; it takes the slot after the scan. */
TEST_CASE(a_connect_a_scan_cut_waits_in_front)
{
    bleu_hub_t h;
    bleu_hub_init(&h);
    for (uint8_t o = 0; o < 4u; ++o) {
        CHECK(bleu_hub_add(&h, o));
    }
    CHECK_EQ(ask(&h, 0, 1, false, 0), 0u);
    CHECK_EQ(ask(&h, 1, 1, false, 10), BLEU_STATUS_TURN);
    CHECK_EQ(ask(&h, 2, 1, false, 20), BLEU_STATUS_BUSY);
    bleu_act_t sc;
    memset(&sc, 0, sizeof sc);
    sc.kind = BLEU_ACT_SCAN;
    sc.token = tok(3, 1);
    sc.ms = 100u;
    bleu_hub_action(&h, 3, &sc, 100);
    bleu_act_t out;
    CHECK(bleu_hub_next_action(&h, &out));
    CHECK_EQ(out.kind, BLEU_ACT_CANCEL);
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_CONNECT;
    e.token = tok(0, 1);
    e.status = 2u;
    bleu_hub_event(&h, &e, 102);
    uint8_t who;
    CHECK(bleu_hub_next_event(&h, &who, &e));
    CHECK_EQ(who, 0u);
    CHECK_EQ(e.status, BLEU_STATUS_BUSY);
    CHECK(bleu_hub_next_action(&h, &out));
    CHECK_EQ(out.kind, BLEU_ACT_SCAN);
    CHECK_EQ(h.turn_owner, 1u);
    CHECK_EQ(h.q[0], 0u);
    CHECK_EQ(h.q[1], 2u);
    CHECK_EQ(h.c[0].due, 102u + BLEU_RETRY_MS);
    /* The turn is 1's; unhurried, it is answered busy while the scan
     * runs. */
    CHECK_EQ(ask(&h, 1, 2, false, 150), BLEU_STATUS_BUSY);
    CHECK_EQ(h.c[1].due, 150u + BLEU_RETRY_MS);
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_SCAN_END;
    e.token = sc.token;
    bleu_hub_event(&h, &e, 202);
    while (bleu_hub_next_event(&h, &who, &e)) { }
    CHECK_EQ(ask(&h, 0, 2, false, 1102), BLEU_STATUS_BUSY);
    CHECK_EQ(ask(&h, 1, 3, false, 1150), 0u);
    end_connect(&h, tok(1, 3), 6150);
    CHECK_EQ(ask(&h, 0, 3, false, 6151), 0u);
}

/* Client 1's turn is kept and client 2 waits; both are switched off.  The
 * slot frees at 5,000ms: client 1, answered with the turn at 10ms, and
 * client 2, answered busy at 4,900ms and due at 5,900ms, leave 200ms after
 * those, not 199ms.  Client 3 then takes the slot. */
TEST_CASE(a_client_that_does_not_come_back_leaves_the_queue)
{
    bleu_hub_t h;
    bleu_hub_init(&h);
    for (uint8_t o = 0; o < 4u; ++o) {
        CHECK(bleu_hub_add(&h, o));
    }
    CHECK_EQ(ask(&h, 0, 1, false, 0), 0u);
    CHECK_EQ(ask(&h, 1, 1, false, 10), BLEU_STATUS_TURN);
    CHECK_EQ(ask(&h, 2, 1, false, 4900), BLEU_STATUS_BUSY);
    bleu_hub_tick(&h, 4990);
    CHECK(h.turn_set);
    CHECK_EQ(h.qn, 1u);
    end_connect(&h, tok(0, 1), 5000);
    CHECK_EQ(ask(&h, 3, 1, false, 5100), BLEU_STATUS_BUSY);
    bleu_hub_tick(&h, 5199);
    CHECK(h.turn_set);
    bleu_hub_tick(&h, 5200);
    CHECK(!h.turn_set);
    CHECK_EQ(h.qn, 2u);
    bleu_hub_tick(&h, 6099);
    CHECK_EQ(h.qn, 2u);
    CHECK_EQ(h.q[0], 2u);
    bleu_hub_tick(&h, 6100);
    CHECK_EQ(h.qn, 1u);
    CHECK_EQ(h.q[0], 3u);
    CHECK_EQ(ask(&h, 3, 2, false, 6100), 0u);
    CHECK_EQ(h.gap_owner, 3u);
    CHECK_EQ(h.qn, 0u);
}

int main(void)
{
    RUN(an_unhurried_connect_gets_the_slot_within_two_attempts);
    RUN(every_client_gets_the_slot_within_one_attempt_of_each_other);
    RUN(the_queue_is_first_come_first_with_hurried_clients_ahead);
    RUN(a_second_hurried_client_goes_behind_the_first);
    RUN(a_connect_a_scan_cut_waits_in_front);
    RUN(a_client_that_does_not_come_back_leaves_the_queue);
    return test_summary("bleuart_fair");
}
