/*
 * When the panel may write its own flash, the board drawing's keep under
 * that rule, and when the coprocessor may not write its own.
 *
 * A flash operation on the panel stalls both cores, the control task's STOP,
 * OUTPUT OFF and the supply's trips with them, so a write starts only on a
 * bench that is disarmed with the supply off.  The second half drives the
 * keep the way the control task does -- one step a pass, the gate taken from
 * the arming policy and the supply in the same pass -- and holds it to
 * pausing on an arm and resuming on the disarm.  The last case is the
 * coprocessor's rule, which holds its store and its firmware update.
 *
 * The arming policy is the real one (arming.c); the flash is memory.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "arming.h"
#include "art_store.h"
#include "flash_gate.h"
#include "heartbeat.h"
#include "link_crc.h"
#include "link_pages.h"

#define SECTOR  4096u
#define PART    (2u * ART_SLOT_BYTES)
/* What the panel passes: four good intervals, plus one. */
#define SETTLE_MS (HEARTBEAT_GOOD_RUN * HEARTBEAT_PERIOD_MS + HEARTBEAT_PERIOD_MS)
/* The control task's period, CONTROL_PERIOD_MS in main.c. */
#define PASS_MS 5u
/* 3 sectors, a slot's capacity, so a keep runs 11 steps and can be stopped
 * part way.  The store keeps any length up to a slot; a drawing is at most
 * 5328 bytes. */
#define PAYLOAD 12288u

static uint8_t *g_flash;
static unsigned g_ops;     /* erases and writes */

static bool fake_read(void *ctx, uint32_t off, void *dst, uint32_t len)
{
    (void)ctx;
    if ((uint64_t)off + len > PART) { return false; }
    memcpy(dst, g_flash + off, len);
    return true;
}

static bool fake_erase(void *ctx, uint32_t off, uint32_t len)
{
    (void)ctx;
    if ((uint64_t)off + len > PART) { return false; }
    ++g_ops;
    memset(g_flash + off, 0xFF, len);
    return true;
}

static bool fake_write(void *ctx, uint32_t off, const void *src, uint32_t len)
{
    (void)ctx;
    if ((uint64_t)off + len > PART) { return false; }
    ++g_ops;
    memcpy(g_flash + off, src, len);
    return true;
}

static const art_flash_t k_flash = {
    fake_read, fake_erase, fake_write, NULL, PART, SECTOR,
};

/* ------------------------------------------------------------- the rule */

TEST_CASE(a_write_may_start_only_disarmed_with_the_supply_off)
{
    CHECK(flash_gate_open(false, false));
    CHECK(!flash_gate_open(true, false));
    CHECK(!flash_gate_open(false, true));
    CHECK(!flash_gate_open(true, true));
}

/* ------------------------------------------------- the keep under the rule */

static arming_t      g_arm;
static bool          g_bank;      /* this end's bank, as outputs_arm() sets */
static bool          g_supply;    /* on, or an ON queued */
static uint32_t      g_now;
static art_keep_t    g_keep;
static uint8_t      *g_payload;
static art_entry_t   g_entry;

static void bench_fresh(void)
{
    if (g_flash == NULL) {
        g_flash = malloc(PART);
        REQUIRE_ALLOC(g_flash);
    }
    memset(g_flash, 0xFF, PART);
    g_ops = 0;
    g_now = 100u;
    g_bank = false;
    g_supply = false;
    arming_init(&g_arm, 0u, SETTLE_MS);
    arming_touch_seen(&g_arm, g_now);

    if (g_payload == NULL) {
        g_payload = malloc(PAYLOAD);
        REQUIRE_ALLOC(g_payload);
    }
    for (uint32_t i = 0; i < PAYLOAD; ++i) {
        g_payload[i] = (uint8_t)(i * 7u + 3u);
    }
    g_entry.board  = 1u;
    g_entry.format = (uint16_t)LINK_ART_DRAWING;
    g_entry.width  = 5100u;
    g_entry.height = 2100u;
    g_entry.bytes  = PAYLOAD;
    g_entry.crc    = link_crc(0u, g_payload, PAYLOAD);
    g_entry.hash   = link_fnv1a32(LINK_FNV_INIT, g_payload, PAYLOAD);
}

/*
 * One control-task pass: the policy, then the keep's step with the gate
 * read in the same pass, as art_keep_service() does after service_arming()
 * and drain_commands().
 */
static art_keep_status_t pass(void)
{
    g_now += PASS_MS;
    arming_touch_seen(&g_arm, g_now);
    switch (arming_step(&g_arm, g_now)) {
    case ARMING_ACT_ARM:    g_bank = true;  break;
    case ARMING_ACT_DISARM: g_bank = false; break;
    default:                                break;
    }
    const bool open = flash_gate_open(arming_link_needed(&g_arm, g_bank),
                                      g_supply);
    return art_keep_step(&g_keep, open);
}

/* The payload is kept, whole. */
static bool kept_whole(void)
{
    art_entry_t got;
    if (!art_store_find(&k_flash, 1u, &got) || got.bytes != PAYLOAD) {
        return false;
    }
    uint8_t *back = malloc(PAYLOAD);
    const bool same = back != NULL
                      && art_store_read(&k_flash, 1u, back, PAYLOAD)
                      && memcmp(back, g_payload, PAYLOAD) == 0;
    free(back);
    return same;
}

TEST_CASE(an_arm_asked_for_during_the_keep_stops_it_before_the_next_sector)
{
    bench_fresh();
    CHECK(art_keep_begin(&g_keep, &k_flash, 1u, &g_entry, g_payload));
    for (int i = 0; i < 5; ++i) {
        CHECK_EQ(pass(), ART_KEEP_RUNNING);
    }
    /*
     * ARM held to the end of its two seconds.  From the request on the
     * policy is arming, before the bank is: the settle's 100 ms are where
     * the far end comes to trust the heartbeat, and a stall there would cost
     * the arm.  Not one more sector from here.
     */
    arming_request_arm(&g_arm, g_now);
    const unsigned ops = g_ops;
    for (int i = 0; i < 200; ++i) {
        CHECK_EQ(pass(), ART_KEEP_WAITING);
    }
    CHECK(g_bank);                           /* it armed, unhindered */
    CHECK_EQ(g_ops, ops);
    CHECK(art_keep_busy(&g_keep));
    art_entry_t got;
    CHECK(!art_store_find(&k_flash, 1u, &got));

    /* A disarm opens the gate on the next pass, and the keep carries on
     * from where it stopped. */
    arming_request_disarm(&g_arm);
    g_bank = false;
    CHECK_EQ(pass(), ART_KEEP_RUNNING);
    CHECK_EQ(g_ops, ops + 1u);
    art_keep_status_t st;
    do {
        st = pass();
    } while (st == ART_KEEP_RUNNING);
    CHECK_EQ(st, ART_KEEP_DONE);
    CHECK(kept_whole());
}

TEST_CASE(a_keep_handed_over_on_an_armed_bench_starts_at_the_disarm)
{
    /* The fetch runs armed or not; the keep it ends in waits, and a stop
     * that disarms is a disarm like any other. */
    bench_fresh();
    arming_request_arm(&g_arm, g_now);
    for (int i = 0; i < 40; ++i) {
        (void)pass();
    }
    CHECK(g_bank);
    CHECK(art_keep_begin(&g_keep, &k_flash, 1u, &g_entry, g_payload));
    for (int i = 0; i < 100; ++i) {
        CHECK_EQ(pass(), ART_KEEP_WAITING);
    }
    CHECK_EQ(g_ops, 0);

    arming_stop_pressed(&g_arm);
    /* The policy disarms on the stop's pass; the gate reads the same pass. */
    CHECK_EQ(pass(), ART_KEEP_RUNNING);
    CHECK(!g_bank);
    art_keep_status_t st;
    do {
        st = pass();
    } while (st == ART_KEEP_RUNNING);
    CHECK_EQ(st, ART_KEEP_DONE);
    CHECK(kept_whole());
}

TEST_CASE(the_supply_coming_on_pauses_the_keep_and_going_off_resumes_it)
{
    bench_fresh();
    CHECK(art_keep_begin(&g_keep, &k_flash, 1u, &g_entry, g_payload));
    /* Paused in each phase in turn: mid-erase, mid-write, mid-read-back. */
    const unsigned at[] = { 1u, 5u, 8u };
    unsigned done = 0;
    for (size_t p = 0; p < sizeof(at) / sizeof(at[0]); ++p) {
        while (done < at[p]) {
            CHECK_EQ(pass(), ART_KEEP_RUNNING);
            ++done;
        }
        g_supply = true;                     /* an ON queued this frame */
        const unsigned ops = g_ops;
        for (int i = 0; i < 20; ++i) {
            CHECK_EQ(pass(), ART_KEEP_WAITING);
        }
        CHECK_EQ(g_ops, ops);
        g_supply = false;                    /* OUTPUT OFF taken */
    }
    art_keep_status_t st;
    do {
        st = pass();
    } while (st == ART_KEEP_RUNNING);
    CHECK_EQ(st, ART_KEEP_DONE);
    CHECK(kept_whole());
}

TEST_CASE(an_arm_and_the_supply_both_hold_it_until_both_are_gone)
{
    bench_fresh();
    CHECK(art_keep_begin(&g_keep, &k_flash, 1u, &g_entry, g_payload));
    CHECK_EQ(pass(), ART_KEEP_RUNNING);
    arming_request_arm(&g_arm, g_now);
    g_supply = true;
    for (int i = 0; i < 40; ++i) {
        CHECK_EQ(pass(), ART_KEEP_WAITING);
    }
    arming_request_disarm(&g_arm);
    g_bank = false;
    CHECK_EQ(pass(), ART_KEEP_WAITING);      /* the supply still on */
    g_supply = false;
    CHECK_EQ(pass(), ART_KEEP_RUNNING);
}

/* ------------------------------------------------ the coprocessor's rule */

/*
 * The coprocessor's flash is busy while any one of its four inputs holds,
 * and free only with all four clear:
 *
 *     driving  supply on  PD on  KST session  busy
 *     false    false      false  false        false
 *     true     false      false  false        true
 *     false    true       false  false        true
 *     false    false      true   false        true
 *     false    false      false  true         true
 *     any two or more of them true            true
 */
TEST_CASE(the_coprocessor_flash_is_busy_while_any_input_holds)
{
    CHECK(!flash_gate_busy(false, false, false, false));
    CHECK(flash_gate_busy(true, false, false, false));
    CHECK(flash_gate_busy(false, true, false, false));
    CHECK(flash_gate_busy(false, false, true, false));
    CHECK(flash_gate_busy(false, false, false, true));
    /* Every one of the sixteen combinations. */
    for (unsigned bits = 0; bits < 16u; ++bits) {
        CHECK_EQ(flash_gate_busy((bits & 1u) != 0u, (bits & 2u) != 0u,
                                 (bits & 4u) != 0u, (bits & 8u) != 0u),
                 bits != 0u);
    }
}

int main(void)
{
    RUN(a_write_may_start_only_disarmed_with_the_supply_off);
    RUN(an_arm_asked_for_during_the_keep_stops_it_before_the_next_sector);
    RUN(a_keep_handed_over_on_an_armed_bench_starts_at_the_disarm);
    RUN(the_supply_coming_on_pauses_the_keep_and_going_off_resumes_it);
    RUN(an_arm_and_the_supply_both_hold_it_until_both_are_gone);
    RUN(the_coprocessor_flash_is_busy_while_any_input_holds);
    free(g_flash);
    free(g_payload);
    return test_summary("flash_gate");
}
