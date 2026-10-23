/*
 * The home network scan: its list, and the scan session under it.
 *
 * The failures under test: an open, hidden or repeated network on the list;
 * a full list that drops a stronger network for a weaker one; a second or a
 * third scan that finds nothing after one that listed networks; a second
 * report of the station's start that starts a second scan, which replaces
 * the first; a done report of no scan under way, a failed scan or a list
 * that cannot be read or holds part of the list, each shown as an empty
 * list; a refusal or a retry of a scan that ended, applied to the session
 * started after it; and a scan that never ends.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "wa_session.h"

static uint32_t g_seed;

static uint32_t counter_rand(void *ctx)
{
    (void)ctx;
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed;
}

/* Longer than a name may be. */
static char g_long[WA_SSID_MAX + 8u];

/* ------------------------------------------------------- the scan list */

static void add(wa_scan_t *s, const char *name, int rssi, bool usable)
{
    wa_scan_add(s, (const uint8_t *)name, strlen(name), (int8_t)rssi, usable);
}

TEST_CASE(the_scan_list_holds_each_usable_network_once_strongest_first)
{
    wa_scan_t s;
    wa_scan_clear(&s);
    add(&s, "weak", -80, true);
    add(&s, "strong", -40, true);
    add(&s, "open cafe", -30, false);        /* no WPA2/WPA3 personal */
    add(&s, "", -35, true);                  /* hidden */
    add(&s, "weak", -60, true);              /* a second access point */
    add(&s, "mid", -60, true);
    CHECK_EQ(s.n, 3u);
    CHECK_EQ(s.dropped, 2u);
    CHECK_STR_EQ(wa_scan_pick(&s, 0u), "strong");
    /* Equal signals keep the order they came in. */
    CHECK_STR_EQ(wa_scan_pick(&s, 1u), "weak");
    CHECK_EQ(s.net[1].rssi, -60);
    CHECK_STR_EQ(wa_scan_pick(&s, 2u), "mid");
    CHECK(wa_scan_pick(&s, 3u) == NULL);
    CHECK(wa_scan_pick(NULL, 0u) == NULL);
}

TEST_CASE(a_full_scan_list_keeps_the_strongest)
{
    wa_scan_t s;
    wa_scan_clear(&s);
    char name[8];
    for (int i = 0; i < 20; ++i) {
        snprintf(name, sizeof(name), "n%02d", i);
        add(&s, name, -90 + i, true);        /* each stronger than the last */
    }
    CHECK_EQ(s.n, WA_SCAN_MAX);
    CHECK_EQ(s.dropped, 4u);
    CHECK_STR_EQ(wa_scan_pick(&s, 0u), "n19");
    CHECK_STR_EQ(wa_scan_pick(&s, WA_SCAN_MAX - 1u), "n04");
    add(&s, "weakest", -95, true);
    CHECK_EQ(s.dropped, 5u);
    CHECK_STR_EQ(wa_scan_pick(&s, WA_SCAN_MAX - 1u), "n04");
    /* A 32-byte name is listed whole, a 33-byte one is not a name. */
    memset(g_long, 'z', 33u);
    wa_scan_add(&s, (const uint8_t *)g_long, 32u, -10, true);
    CHECK_EQ(strlen(wa_scan_pick(&s, 0u)), 32u);
    wa_scan_add(&s, (const uint8_t *)g_long, 33u, -10, true);
    CHECK_EQ(s.dropped, 7u);
}

/* ------------------------------------------------------- the session */

static wa_t g_w;

static void up_ap(uint32_t now)
{
    wa_init(&g_w);
    g_seed = 3u;
    CHECK_EQ(wa_start(&g_w, WA_MODE_AP, WA_OFF_NONE, now, counter_rand, NULL),
             WA_OFF_NONE);
    wa_on_ap_up(&g_w, now);
    CHECK_EQ(g_w.state, WA_ST_AP);
    CHECK(wa_serving(&g_w));
}

TEST_CASE(a_scan_runs_the_radio_and_ends_off)
{
    wa_init(&g_w);
    CHECK_EQ(wa_start(&g_w, WA_MODE_SCAN, WA_OFF_NONE, 0u, counter_rand,
                      NULL),
             WA_OFF_NONE);
    CHECK_EQ(g_w.state, WA_ST_SCANNING);
    CHECK(wa_radio(&g_w));
    CHECK(!wa_serving(&g_w));
    wa_on_scan_done(&g_w);
    CHECK(!wa_radio(&g_w));
    CHECK_EQ(g_w.off, WA_OFF_NONE);
    /* One that never ends is ended. */
    (void)wa_start(&g_w, WA_MODE_SCAN, WA_OFF_NONE, 0u, counter_rand, NULL);
    CHECK(wa_tick(&g_w, WA_START_MS, WA_OFF_NONE, false));
    CHECK_EQ(g_w.off, WA_OFF_FAILED);
}

/* The token of the scan the platform runs, as wa_scan_begin() gave it. */
static uint32_t g_tok;

/* One scan session as the platform runs it: the station's start reported
 * twice, the scan ends with @p ok. */
static wa_scan_step_t scan_once(uint32_t now, bool ok)
{
    CHECK_EQ(wa_start(&g_w, WA_MODE_SCAN, WA_OFF_NONE, now, counter_rand,
                      NULL),
             WA_OFF_NONE);
    g_tok = wa_scan_begin(&g_w);
    CHECK(g_tok != 0u);
    CHECK_EQ(wa_scan_begin(&g_w), 0u);  /* a second station start */
    CHECK_EQ(wa_scan_pending(&g_w), g_tok);
    return wa_on_scan_end(&g_w, g_tok, ok, &g_tok);
}

TEST_CASE(a_second_and_a_third_scan_after_a_completed_one_list_again)
{
    wa_init(&g_w);
    uint32_t last = 0u;
    for (uint32_t i = 0; i < 3u; ++i) {
        CHECK_EQ(scan_once(i * 5000u, true), WA_SCAN_TAKE);
        CHECK(!wa_radio(&g_w));
        CHECK_EQ(g_w.off, WA_OFF_NONE);
        CHECK_EQ(g_w.scan_runs, 1u);
        CHECK_EQ(wa_scan_pending(&g_w), 0u);
        CHECK(g_w.scan_seq != last);    /* each scan its own token */
        last = g_w.scan_seq;
    }
}

TEST_CASE(a_scan_done_with_no_scan_of_the_session_under_way_is_ignored)
{
    uint32_t next = 0u;
    wa_init(&g_w);
    CHECK_EQ(wa_on_scan_end(&g_w, 0u, true, &next), WA_SCAN_IGNORE);
    CHECK_EQ(g_w.off, WA_OFF_NONE);                       /* radio off */
    (void)wa_start(&g_w, WA_MODE_SCAN, WA_OFF_NONE, 0u, counter_rand, NULL);
    CHECK_EQ(wa_scan_pending(&g_w), 0u);
    CHECK_EQ(wa_on_scan_end(&g_w, 0u, true, &next), WA_SCAN_IGNORE);
    CHECK_EQ(g_w.state, WA_ST_SCANNING);                  /* none started */
    const uint32_t t = wa_scan_begin(&g_w);
    CHECK_EQ(wa_on_scan_end(&g_w, t, true, &next), WA_SCAN_TAKE);
    CHECK_EQ(wa_on_scan_end(&g_w, t, true, &next), WA_SCAN_IGNORE);
    CHECK_EQ(g_w.off, WA_OFF_NONE);                       /* a second done */
    /* Ended by CANCEL, the gate or STOP: a done after it reads nothing. */
    (void)wa_start(&g_w, WA_MODE_SCAN, WA_OFF_NONE, 0u, counter_rand, NULL);
    const uint32_t u = wa_scan_begin(&g_w);
    wa_close(&g_w, WA_OFF_ASKED);
    CHECK_EQ(wa_scan_pending(&g_w), 0u);
    CHECK_EQ(wa_on_scan_end(&g_w, u, true, &next), WA_SCAN_IGNORE);
    CHECK_EQ(g_w.off, WA_OFF_ASKED);
    CHECK_EQ(wa_scan_begin(&g_w), 0u);
}

TEST_CASE(a_failed_scan_runs_again_and_the_last_failure_is_a_failure)
{
    wa_init(&g_w);
    const uint32_t first = g_w.scan_seq + 1u;
    CHECK_EQ(scan_once(0u, false), WA_SCAN_AGAIN);
    CHECK_EQ(g_w.state, WA_ST_SCANNING);
    CHECK(g_tok != first);              /* the next one has its own token */
    CHECK_EQ(wa_scan_pending(&g_w), g_tok);
    CHECK_EQ(g_w.scan_runs, 2u);
    for (unsigned i = 2u; i < WA_SCAN_TRIES; ++i) {
        CHECK_EQ(wa_on_scan_end(&g_w, g_tok, false, &g_tok), WA_SCAN_AGAIN);
    }
    CHECK_EQ(g_w.scan_runs, WA_SCAN_TRIES);
    CHECK_EQ(wa_on_scan_end(&g_w, g_tok, false, &g_tok), WA_SCAN_FAILED);
    CHECK_EQ(g_tok, 0u);
    CHECK(!wa_radio(&g_w));
    CHECK_EQ(g_w.off, WA_OFF_FAILED);   /* not an empty list */
    /* A failure, then a scan that works: the list. */
    CHECK_EQ(scan_once(0u, false), WA_SCAN_AGAIN);
    CHECK_EQ(wa_on_scan_end(&g_w, g_tok, true, &g_tok), WA_SCAN_TAKE);
    CHECK_EQ(g_w.off, WA_OFF_NONE);
}

TEST_CASE(a_late_done_report_of_a_replaced_scan_changes_nothing)
{
    wa_init(&g_w);
    CHECK_EQ(scan_once(0u, false), WA_SCAN_AGAIN);
    const uint32_t old = g_tok - 1u;    /* the scan the retry replaced */
    uint32_t next = 0u;
    CHECK_EQ(wa_on_scan_end(&g_w, old, true, &next), WA_SCAN_IGNORE);
    CHECK_EQ(wa_scan_pending(&g_w), g_tok);
    CHECK_EQ(g_w.state, WA_ST_SCANNING);
    CHECK_EQ(wa_on_scan_end(&g_w, g_tok, true, &next), WA_SCAN_TAKE);
}

/* CANCEL, then CHOOSE HOME NETWORK, with the first scan still running: the
 * old radio's done report is handled before the new radio's station start
 * (the platform drains the event loop at the radio's stop), so it finds no
 * scan under way, and the new scan's own report is the one that lists. */
TEST_CASE(a_done_report_of_the_radio_taken_down_is_not_the_next_scans)
{
    uint32_t next = 0u;
    wa_init(&g_w);
    (void)wa_start(&g_w, WA_MODE_SCAN, WA_OFF_NONE, 0u, counter_rand, NULL);
    const uint32_t old = wa_scan_begin(&g_w);
    wa_close(&g_w, WA_OFF_ASKED);                         /* CANCEL */
    (void)wa_start(&g_w, WA_MODE_SCAN, WA_OFF_NONE, 10u, counter_rand, NULL);
    CHECK_EQ(wa_scan_pending(&g_w), 0u);
    /* The old report, read as the platform reads it: no scan under way. */
    CHECK_EQ(wa_on_scan_end(&g_w, wa_scan_pending(&g_w), true, &next),
             WA_SCAN_IGNORE);
    CHECK_EQ(wa_on_scan_end(&g_w, old, false, &next), WA_SCAN_IGNORE);
    CHECK_EQ(g_w.state, WA_ST_SCANNING);
    CHECK_EQ(g_w.scan_runs, 0u);                          /* no try spent */
    const uint32_t t = wa_scan_begin(&g_w);
    CHECK(t != old);
    CHECK_EQ(wa_on_scan_end(&g_w, t, true, &next), WA_SCAN_TAKE);
}

TEST_CASE(a_scan_the_radio_refuses_is_a_failure_not_an_empty_list)
{
    uint32_t next = 0u;
    wa_init(&g_w);
    (void)wa_start(&g_w, WA_MODE_SCAN, WA_OFF_NONE, 0u, counter_rand, NULL);
    const uint32_t t = wa_scan_begin(&g_w);
    CHECK(wa_scan_refused(&g_w, t));    /* esp_wifi_scan_start() refused */
    CHECK(!wa_radio(&g_w));
    CHECK_EQ(g_w.off, WA_OFF_FAILED);
    CHECK_EQ(wa_on_scan_end(&g_w, t, true, &next), WA_SCAN_IGNORE);
    CHECK_EQ(g_w.off, WA_OFF_FAILED);
    CHECK(!wa_scan_refused(&g_w, t));   /* once */
}

/* esp_wifi_scan_start() can wait for the radio; the session can end and
 * another start meanwhile.  The old call's refusal belongs to the old scan. */
TEST_CASE(a_refusal_after_the_session_changed_leaves_the_new_session)
{
    wa_init(&g_w);
    (void)wa_start(&g_w, WA_MODE_SCAN, WA_OFF_NONE, 0u, counter_rand, NULL);
    const uint32_t t = wa_scan_begin(&g_w);
    /* CANCEL and ACCESS POINT while the call waits. */
    CHECK_EQ(wa_start(&g_w, WA_MODE_AP, WA_OFF_NONE, 10u, counter_rand, NULL),
             WA_OFF_NONE);
    CHECK(!wa_scan_refused(&g_w, t));
    CHECK_EQ(g_w.state, WA_ST_STARTING);
    CHECK_EQ(g_w.off, WA_OFF_NONE);
    /* SCAN AGAIN while the call waits: the new scan stays under way. */
    (void)wa_start(&g_w, WA_MODE_SCAN, WA_OFF_NONE, 0u, counter_rand, NULL);
    const uint32_t a = wa_scan_begin(&g_w);
    (void)wa_start(&g_w, WA_MODE_SCAN, WA_OFF_NONE, 20u, counter_rand, NULL);
    const uint32_t b = wa_scan_begin(&g_w);
    CHECK(a != b);
    CHECK(!wa_scan_refused(&g_w, a));
    CHECK_EQ(wa_scan_pending(&g_w), b);
    CHECK_EQ(g_w.state, WA_ST_SCANNING);
    /* A join started in its place: likewise. */
    (void)wa_start(&g_w, WA_MODE_JOIN, WA_OFF_NONE, 30u, counter_rand, NULL);
    CHECK(!wa_scan_refused(&g_w, b));
    CHECK_EQ(g_w.state, WA_ST_JOINING);
}

/* A retry is started outside the lock; a request in between ends the
 * session, and the retry is then not started and its refusal is not the new
 * session's. */
TEST_CASE(a_retry_after_the_session_changed_is_not_started)
{
    wa_init(&g_w);
    CHECK_EQ(scan_once(0u, false), WA_SCAN_AGAIN);
    const uint32_t retry = g_tok;
    CHECK(wa_scan_is(&g_w, retry));
    CHECK_EQ(wa_start(&g_w, WA_MODE_AP, WA_OFF_NONE, 10u, counter_rand, NULL),
             WA_OFF_NONE);
    CHECK(!wa_scan_is(&g_w, retry));    /* the platform does not start it */
    CHECK(!wa_scan_refused(&g_w, retry));
    CHECK_EQ(g_w.state, WA_ST_STARTING);
    /* Nor in a new scan session, whose own scan has not started yet. */
    CHECK_EQ(scan_once(40u, false), WA_SCAN_AGAIN);
    const uint32_t r2 = g_tok;
    (void)wa_start(&g_w, WA_MODE_SCAN, WA_OFF_NONE, 50u, counter_rand, NULL);
    CHECK(!wa_scan_is(&g_w, r2));
    CHECK(!wa_scan_refused(&g_w, r2));
    CHECK_EQ(g_w.state, WA_ST_SCANNING);
    CHECK(wa_scan_begin(&g_w) != 0u);
    CHECK(!wa_scan_is(&g_w, 0u));
}

TEST_CASE(tokens_hold_across_a_new_session_and_never_read_as_none)
{
    wa_init(&g_w);
    g_w.scan_seq = 0xFFFFFFFFu;         /* the counter at its wrap */
    (void)wa_start(&g_w, WA_MODE_SCAN, WA_OFF_NONE, 0u, counter_rand, NULL);
    CHECK_EQ(g_w.scan_seq, 0xFFFFFFFFu);
    const uint32_t t = wa_scan_begin(&g_w);
    CHECK(t != 0u);
    CHECK_EQ(wa_scan_pending(&g_w), t);
    uint32_t next = 0u;
    CHECK_EQ(wa_on_scan_end(&g_w, t, false, &next), WA_SCAN_AGAIN);
    CHECK(next != 0u);
    CHECK(next != t);
}

TEST_CASE(the_scans_of_a_session_end_at_the_start_time_out)
{
    wa_init(&g_w);
    CHECK_EQ(scan_once(1000u, false), WA_SCAN_AGAIN);
    CHECK(!wa_tick(&g_w, 1000u + WA_START_MS - 1u, WA_OFF_NONE, false));
    CHECK(wa_tick(&g_w, 1000u + WA_START_MS, WA_OFF_NONE, false));
    CHECK_EQ(g_w.off, WA_OFF_FAILED);
    CHECK_EQ(wa_on_scan_end(&g_w, g_tok, true, &g_tok), WA_SCAN_IGNORE);
}

TEST_CASE(a_scan_with_the_access_point_up_or_joined_ends_that_session_first)
{
    uint32_t next = 0u;
    up_ap(0u);
    CHECK_EQ(wa_start(&g_w, WA_MODE_SCAN, WA_OFF_NONE, 100u, counter_rand,
                      NULL),
             WA_OFF_NONE);
    CHECK_EQ(g_w.state, WA_ST_SCANNING);
    CHECK(!wa_serving(&g_w));
    CHECK_EQ(g_w.scan_runs, 0u);
    uint32_t t = wa_scan_begin(&g_w);
    CHECK(t != 0u);
    CHECK_EQ(wa_on_scan_end(&g_w, t, true, &next), WA_SCAN_TAKE);
    CHECK(!wa_radio(&g_w));

    wa_init(&g_w);
    (void)wa_start(&g_w, WA_MODE_JOIN, WA_OFF_NONE, 0u, counter_rand, NULL);
    wa_on_associated(&g_w, 10u);
    wa_on_address(&g_w, 0x0100A8C0u, 20u);
    CHECK_EQ(g_w.state, WA_ST_JOINED);
    CHECK_EQ(wa_start(&g_w, WA_MODE_SCAN, WA_OFF_NONE, 30u, counter_rand,
                      NULL),
             WA_OFF_NONE);
    CHECK_EQ(g_w.state, WA_ST_SCANNING);
    CHECK_EQ(g_w.ip, 0u);
    t = wa_scan_begin(&g_w);
    CHECK_EQ(wa_on_scan_end(&g_w, t, true, &next), WA_SCAN_TAKE);
    /* A station start in another mode starts no scan. */
    up_ap(0u);
    CHECK_EQ(wa_scan_begin(&g_w), 0u);
}

TEST_CASE(a_scan_counts_as_done_only_with_its_whole_list_read)
{
    /* status 0 and as many read as reported, or a full buffer */
    CHECK(wa_scan_read_ok(0u, 6u, true, 6u, 24u));
    CHECK(wa_scan_read_ok(0u, 0u, true, 0u, 24u));   /* nothing in reach */
    CHECK(wa_scan_read_ok(0u, 24u, true, 24u, 24u)); /* just fits */
    CHECK(wa_scan_read_ok(0u, 30u, true, 24u, 24u)); /* more than fit */
    CHECK(!wa_scan_read_ok(1u, 0u, true, 0u, 24u));  /* the scan failed */
    CHECK(!wa_scan_read_ok(1u, 6u, true, 6u, 24u));
    CHECK(!wa_scan_read_ok(0u, 6u, false, 0u, 24u)); /* the read refused */
    CHECK(!wa_scan_read_ok(0u, 6u, true, 0u, 24u));  /* reported, not read */
    CHECK(!wa_scan_read_ok(0u, 12u, true, 1u, 24u)); /* part of a list */
    CHECK(!wa_scan_read_ok(0u, 30u, true, 23u, 24u));
    CHECK(!wa_scan_read_ok(0u, 3u, true, 6u, 24u));  /* another list */
    CHECK(!wa_scan_read_ok(0u, 0u, true, 6u, 24u));
    CHECK(!wa_scan_read_ok(0u, 30u, true, 30u, 24u)); /* past the buffer */
}

int main(void)
{
    RUN(the_scan_list_holds_each_usable_network_once_strongest_first);
    RUN(a_full_scan_list_keeps_the_strongest);
    RUN(a_scan_runs_the_radio_and_ends_off);
    RUN(a_second_and_a_third_scan_after_a_completed_one_list_again);
    RUN(a_scan_done_with_no_scan_of_the_session_under_way_is_ignored);
    RUN(a_failed_scan_runs_again_and_the_last_failure_is_a_failure);
    RUN(a_late_done_report_of_a_replaced_scan_changes_nothing);
    RUN(a_done_report_of_the_radio_taken_down_is_not_the_next_scans);
    RUN(a_scan_the_radio_refuses_is_a_failure_not_an_empty_list);
    RUN(a_refusal_after_the_session_changed_leaves_the_new_session);
    RUN(a_retry_after_the_session_changed_is_not_started);
    RUN(tokens_hold_across_a_new_session_and_never_read_as_none);
    RUN(the_scans_of_a_session_end_at_the_start_time_out);
    RUN(a_scan_with_the_access_point_up_or_joined_ends_that_session_first);
    RUN(a_scan_counts_as_done_only_with_its_whole_list_read);
    return test_summary("wa_scan");
}
