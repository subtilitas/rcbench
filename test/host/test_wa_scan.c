/*
 * The home network scan: its list, and the scan session under it.
 *
 * The failures under test: an open, hidden or repeated network on the list;
 * a full list that drops a stronger network for a weaker one; a second or a
 * third scan that finds nothing after one that listed networks; a second
 * report of the station's start that starts a second scan, which replaces
 * the first; a done report of no scan under way, a failed scan or a list
 * that cannot be read, each shown as an empty list; and a scan that never
 * ends.
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

/* One scan session as the platform runs it: the station's start reported
 * twice, the scan ends with @p ok. */
static wa_scan_step_t scan_once(uint32_t now, bool ok)
{
    CHECK_EQ(wa_start(&g_w, WA_MODE_SCAN, WA_OFF_NONE, now, counter_rand,
                      NULL),
             WA_OFF_NONE);
    CHECK(wa_scan_begin(&g_w));
    CHECK(!wa_scan_begin(&g_w));        /* a second station start */
    CHECK(wa_scan_pending(&g_w));
    return wa_on_scan_end(&g_w, ok);
}

TEST_CASE(a_second_and_a_third_scan_after_a_completed_one_list_again)
{
    wa_init(&g_w);
    for (uint32_t i = 0; i < 3u; ++i) {
        CHECK_EQ(scan_once(i * 5000u, true), WA_SCAN_TAKE);
        CHECK(!wa_radio(&g_w));
        CHECK_EQ(g_w.off, WA_OFF_NONE);
        CHECK_EQ(g_w.scan_runs, 1u);
        CHECK(!wa_scan_pending(&g_w));
    }
}

TEST_CASE(a_scan_done_with_no_scan_of_the_session_under_way_is_ignored)
{
    wa_init(&g_w);
    CHECK_EQ(wa_on_scan_end(&g_w, true), WA_SCAN_IGNORE);     /* radio off */
    CHECK_EQ(g_w.off, WA_OFF_NONE);
    (void)wa_start(&g_w, WA_MODE_SCAN, WA_OFF_NONE, 0u, counter_rand, NULL);
    CHECK(!wa_scan_pending(&g_w));
    CHECK_EQ(wa_on_scan_end(&g_w, true), WA_SCAN_IGNORE);  /* none started */
    CHECK_EQ(g_w.state, WA_ST_SCANNING);
    CHECK(wa_scan_begin(&g_w));
    CHECK_EQ(wa_on_scan_end(&g_w, true), WA_SCAN_TAKE);
    CHECK_EQ(wa_on_scan_end(&g_w, true), WA_SCAN_IGNORE);  /* a second done */
    CHECK_EQ(g_w.off, WA_OFF_NONE);
    /* Ended by CANCEL, the gate or STOP: a done after it reads nothing. */
    (void)wa_start(&g_w, WA_MODE_SCAN, WA_OFF_NONE, 0u, counter_rand, NULL);
    CHECK(wa_scan_begin(&g_w));
    wa_close(&g_w, WA_OFF_ASKED);
    CHECK(!wa_scan_pending(&g_w));
    CHECK_EQ(wa_on_scan_end(&g_w, true), WA_SCAN_IGNORE);
    CHECK_EQ(g_w.off, WA_OFF_ASKED);
    CHECK(!wa_scan_begin(&g_w));
}

TEST_CASE(a_failed_scan_runs_again_and_the_last_failure_is_a_failure)
{
    wa_init(&g_w);
    CHECK_EQ(scan_once(0u, false), WA_SCAN_AGAIN);
    CHECK_EQ(g_w.state, WA_ST_SCANNING);
    CHECK(wa_scan_pending(&g_w));       /* the next one is started */
    CHECK_EQ(g_w.scan_runs, 2u);
    for (unsigned i = 2u; i < WA_SCAN_TRIES; ++i) {
        CHECK_EQ(wa_on_scan_end(&g_w, false), WA_SCAN_AGAIN);
    }
    CHECK_EQ(g_w.scan_runs, WA_SCAN_TRIES);
    CHECK_EQ(wa_on_scan_end(&g_w, false), WA_SCAN_FAILED);
    CHECK(!wa_radio(&g_w));
    CHECK_EQ(g_w.off, WA_OFF_FAILED);   /* not an empty list */
    /* A failure, then a scan that works: the list. */
    CHECK_EQ(scan_once(0u, false), WA_SCAN_AGAIN);
    CHECK_EQ(wa_on_scan_end(&g_w, true), WA_SCAN_TAKE);
    CHECK_EQ(g_w.off, WA_OFF_NONE);
}

TEST_CASE(a_scan_the_radio_refuses_is_a_failure_not_an_empty_list)
{
    wa_init(&g_w);
    (void)wa_start(&g_w, WA_MODE_SCAN, WA_OFF_NONE, 0u, counter_rand, NULL);
    CHECK(wa_scan_begin(&g_w));
    wa_on_failed(&g_w);                 /* esp_wifi_scan_start() refused */
    CHECK(!wa_radio(&g_w));
    CHECK_EQ(g_w.off, WA_OFF_FAILED);
    CHECK_EQ(wa_on_scan_end(&g_w, true), WA_SCAN_IGNORE);
    CHECK_EQ(g_w.off, WA_OFF_FAILED);
}

TEST_CASE(the_scans_of_a_session_end_at_the_start_time_out)
{
    wa_init(&g_w);
    CHECK_EQ(scan_once(1000u, false), WA_SCAN_AGAIN);
    CHECK(!wa_tick(&g_w, 1000u + WA_START_MS - 1u, WA_OFF_NONE, false));
    CHECK(wa_tick(&g_w, 1000u + WA_START_MS, WA_OFF_NONE, false));
    CHECK_EQ(g_w.off, WA_OFF_FAILED);
    CHECK_EQ(wa_on_scan_end(&g_w, true), WA_SCAN_IGNORE);
}

TEST_CASE(a_scan_with_the_access_point_up_or_joined_ends_that_session_first)
{
    up_ap(0u);
    CHECK_EQ(wa_start(&g_w, WA_MODE_SCAN, WA_OFF_NONE, 100u, counter_rand,
                      NULL),
             WA_OFF_NONE);
    CHECK_EQ(g_w.state, WA_ST_SCANNING);
    CHECK(!wa_serving(&g_w));
    CHECK_EQ(g_w.scan_runs, 0u);
    CHECK(wa_scan_begin(&g_w));
    CHECK_EQ(wa_on_scan_end(&g_w, true), WA_SCAN_TAKE);
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
    CHECK(wa_scan_begin(&g_w));
    CHECK_EQ(wa_on_scan_end(&g_w, true), WA_SCAN_TAKE);
    /* A station start in another mode starts no scan. */
    up_ap(0u);
    CHECK(!wa_scan_begin(&g_w));
}

TEST_CASE(a_scan_counts_as_done_only_with_its_list_read)
{
    /* status 0, the list read, as many as reported or none reported */
    CHECK(wa_scan_read_ok(0u, 6u, true, 6u));
    CHECK(wa_scan_read_ok(0u, 0u, true, 0u));   /* nothing in reach */
    CHECK(wa_scan_read_ok(0u, 30u, true, 24u)); /* more than fit */
    CHECK(!wa_scan_read_ok(1u, 0u, true, 0u));  /* the scan failed */
    CHECK(!wa_scan_read_ok(1u, 6u, true, 6u));
    CHECK(!wa_scan_read_ok(0u, 6u, false, 0u)); /* the read refused */
    CHECK(!wa_scan_read_ok(0u, 6u, true, 0u));  /* reported, not read */
}

int main(void)
{
    RUN(the_scan_list_holds_each_usable_network_once_strongest_first);
    RUN(a_full_scan_list_keeps_the_strongest);
    RUN(a_scan_runs_the_radio_and_ends_off);
    RUN(a_second_and_a_third_scan_after_a_completed_one_list_again);
    RUN(a_scan_done_with_no_scan_of_the_session_under_way_is_ignored);
    RUN(a_failed_scan_runs_again_and_the_last_failure_is_a_failure);
    RUN(a_scan_the_radio_refuses_is_a_failure_not_an_empty_list);
    RUN(the_scans_of_a_session_end_at_the_start_time_out);
    RUN(a_scan_with_the_access_point_up_or_joined_ends_that_session_first);
    RUN(a_scan_counts_as_done_only_with_its_list_read);
    return test_summary("wa_scan");
}
