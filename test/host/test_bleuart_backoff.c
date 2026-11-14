/*
 * The connect backoff of protocols/bleuart: a bound peer that does not
 * answer, alone and beside a Dial on the same hub (hub_rig.h).
 *
 * Under test: an absent peer's connect gaps doubling from 1,000ms to
 * 30,000ms, 7 connects in 100s; a Dial dropped while the absent peer waits
 * up again after its 1,000ms first retry, where without the backoff it
 * stays down; a scan and a bind starting the gaps over; connects the host
 * stack ends with a failure doubling from 7,000ms to 30,000ms, a link that
 * comes up starting them over and its drop retried after 1,000ms.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "hub_rig.h"

/* The times of owner @p o's CONNECT actions, at most @p cap. */
static unsigned rig_connects(const rig_t *r, int o, uint32_t *t, unsigned cap)
{
    unsigned n = 0;
    for (unsigned i = 0; i < r->ndone && n < cap; ++i) {
        const bleu_act_t *a = &r->done[i].a;
        if (a->kind == BLEU_ACT_CONNECT && a->token != 0u
            && bleu_token_owner(a->token) == o) {
            t[n++] = r->done[i].t;
        }
    }
    return n;
}

/* Peer @p i drops at @p t; the ms until its client is up again, run tick
 * by tick, at most @p limit. */
static uint32_t rig_back_up(rig_t *r, unsigned i, uint32_t t, uint32_t limit)
{
    bsim_drop(&r->s[i], t, 0x08u);
    rig_tick(r, t);
    uint32_t k = 1;
    for (; k < limit; ++k) {
        rig_tick(r, t + k);
        if (bleu_link_up(&r->b[i])) {
            break;
        }
    }
    return k;
}

/* A bound node that is away beside a Dial: each unanswered connect doubles
 * the node's next gap, 1,000ms to 30,000ms, so its 5,000ms connects hold
 * the host stack's one connect ever less often.  The Dial, dropped while
 * the node waits, is up again after its 1,000ms first retry, not held
 * back for up to 5,000ms by a node connect every 6,000ms. */
TEST_CASE(an_absent_peer_backs_off_and_a_dropped_dial_returns_in_1000ms)
{
    rig_t *r = rig_init(2);
    r->s[0].absent = true;
    rig_on(r, 0, 0);
    rig_on(r, 1, 0);
    rig_run(r, 0, 100000);
    CHECK(bleu_link_up(&r->b[1]));
    uint32_t t[16];
    const unsigned n = rig_connects(r, 0, t, 16u);
    /* 0ms, 6,000ms, 13,000ms, 22,000ms, 35,000ms, 56,000ms and
     * 91,000ms, give or take the cancel's settle: 7 connects in 100s
     * instead of 17. */
    CHECK_EQ(n, 7u);
    uint32_t want = BLEU_RETRY_MS;
    for (unsigned k = 1; k < n; ++k) {
        const uint32_t gap = t[k] - t[k - 1u] - BLEU_CONNECT_MS;
        CHECK(gap >= want && gap <= want + 10u);
        want = 2u * want > BLEU_BACKOFF_MAX_MS ? BLEU_BACKOFF_MAX_MS
                                               : 2u * want;
    }
    CHECK_EQ(r->b[0].backoff_ms, BLEU_BACKOFF_MAX_MS);
    /* The node waits from 96,000ms to 126,000ms: the Dial dropped in that
     * time connects again on its first retry. */
    for (uint32_t d = 100000u; d < 120000u; d += 5000u) {
        const uint32_t back = rig_back_up(r, 1u, d, 6000u);
        CHECK(back >= BLEU_RETRY_MS && back <= BLEU_RETRY_MS + 200u);
        if (back + 1u < 5000u) {
            rig_run(r, d + back + 1u, d + 5000u);
        }
    }
    CHECK_EQ(r->b[1].backoff_ms, 0u);
    /* A scan starts the node's gaps over at 1,000ms. */
    CHECK(bleu_scan(&r->b[0], 120000u));
    CHECK_EQ(r->b[0].backoff_ms, 0u);
    /* A bind does too, after the node failed once more. */
    r->b[0].backoff_ms = 8000u;
    bleu_bind(&r->b[0], &k_addr[2], 120001u);
    CHECK_EQ(r->b[0].backoff_ms, 0u);
}

/* A connect the host stack ends with a failure waits 7,000ms the first
 * time, then twice the gap before, up to 30,000ms; a link that comes up
 * starts them over, and its drop is retried after 1,000ms. */
TEST_CASE(failed_connects_double_their_gap_from_7000ms)
{
    bleu_t b;
    bleu_init(&b);
    bleu_bind(&b, &k_addr[0], 0);
    bleu_enable(&b, true, 0);
    bleu_act_t a;
    static const uint32_t gaps[] = { 7000u, 14000u, 28000u, 30000u, 30000u };
    uint32_t now = 0;
    for (unsigned k = 0; k < sizeof gaps / sizeof gaps[0]; ++k) {
        CHECK(bleu_step(&b, now, &a));
        CHECK_EQ(a.kind, BLEU_ACT_CONNECT);
        bleu_evt_t e;
        memset(&e, 0, sizeof e);
        e.kind = BLEU_EV_CONNECT;
        e.token = a.token;
        e.status = 13u;                 /* BLE_HS_ETIMEOUT */
        (void)bleu_event(&b, &e, now + 10u);
        CHECK_EQ(b.gap_ms, gaps[k]);
        now += 10u + gaps[k];
        CHECK(!bleu_step(&b, now - 1u, &a));
    }
    CHECK(bleu_step(&b, now, &a));
    CHECK_EQ(a.kind, BLEU_ACT_CONNECT);
    bleu_evt_t e;
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_CONNECT;
    e.token = a.token;
    e.conn = 5u;
    (void)bleu_event(&b, &e, now + 10u);
    CHECK_EQ(b.backoff_ms, 0u);
    memset(&e, 0, sizeof e);
    e.kind = BLEU_EV_DISCONNECT;
    e.conn = 5u;
    e.status = 0x08u;
    (void)bleu_event(&b, &e, now + 20u);
    CHECK_EQ(b.gap_ms, BLEU_RETRY_MS);
}

int main(void)
{
    RUN(an_absent_peer_backs_off_and_a_dropped_dial_returns_in_1000ms);
    RUN(failed_connects_double_their_gap_from_7000ms);
    return test_summary("bleuart_backoff");
}
