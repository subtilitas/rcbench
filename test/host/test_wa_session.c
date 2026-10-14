/*
 * When the panel's Wi-Fi runs, what it keeps, and who its page answers.
 *
 * The failures under test: a radio that stays on through an ARM, a STOP, a
 * supply switched on or a log run opened; a radio left on with nobody using
 * it; a password or access code that can be guessed from the last one, or
 * that still opens the page after the screen was closed and opened again;
 * a home network kept that the radio would refuse; and a join that fails
 * without saying why.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "wa_session.h"

/* ------------------------------------------------------------ random */

static uint32_t g_seed;

static uint32_t counter_rand(void *ctx)
{
    (void)ctx;
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed;
}

/* A source whose bytes are all at or above the fair limit for a while. */
static int g_high_words;
static uint32_t high_then_low(void *ctx)
{
    (void)ctx;
    if (g_high_words > 0) {
        --g_high_words;
        return 0xFFFFFFFFu;
    }
    return 0x03020100u;
}

TEST_CASE(a_secret_is_eight_characters_of_the_alphabet)
{
    char s[WA_SECRET_LEN + 1u];
    g_seed = 1u;
    for (int i = 0; i < 200; ++i) {
        wa_secret_make(s, counter_rand, NULL);
        CHECK_EQ(strlen(s), 8u);
        for (int k = 0; k < 8; ++k) {
            CHECK(strchr(WA_SECRET_CHARS, s[k]) != NULL);
        }
    }
    /* No character that reads like another. */
    CHECK(strpbrk(WA_SECRET_CHARS, "01ilo") == NULL);
    CHECK_EQ(strlen(WA_SECRET_CHARS), 31u);
}

TEST_CASE(bytes_that_would_favour_some_characters_are_drawn_again)
{
    /* 0xFF is past 248, the largest multiple of 31 in a byte: two words of
     * them give nothing, then 00 01 02 03 give a b c d. */
    char s[WA_SECRET_LEN + 1u];
    g_high_words = 2;
    wa_secret_make(s, high_then_low, NULL);
    CHECK_STR_EQ(s, "abcdabcd");
}

TEST_CASE(every_start_makes_a_fresh_password_and_code)
{
    wa_t w;
    wa_init(&w);
    g_seed = 7u;
    CHECK_EQ(wa_start(&w, WA_MODE_AP, WA_OFF_NONE, 0u, counter_rand, NULL),
             WA_OFF_NONE);
    char pass[9], code[9];
    memcpy(pass, w.pass, 9u);
    memcpy(code, w.code, 9u);
    CHECK(strcmp(pass, code) != 0);
    wa_close(&w, WA_OFF_ASKED);
    CHECK_EQ(w.code[0], '\0');               /* gone with the session */
    CHECK_EQ(wa_start(&w, WA_MODE_AP, WA_OFF_NONE, 0u, counter_rand, NULL),
             WA_OFF_NONE);
    CHECK(strcmp(pass, w.pass) != 0);
    CHECK(strcmp(code, w.code) != 0);
}

TEST_CASE(the_name_carries_the_last_two_bytes_of_the_mac)
{
    const uint8_t mac[6] = { 0x24, 0x0A, 0xC4, 0x12, 0x1a, 0x2B };
    char n[WA_NAME_MAX];
    wa_name(mac, n);
    CHECK_STR_EQ(n, "rcbench-1A2B");
    CHECK_EQ(strlen(n), WA_NAME_MAX - 1u);
}

/* ------------------------------------------------------- what is kept */

static wa_conf_t g_stored;
static bool g_have_stored;
static int  g_saves, g_erases;

static bool s_load(void *ctx, wa_conf_t *out)
{
    (void)ctx;
    if (!g_have_stored) {
        return false;
    }
    *out = g_stored;
    return true;
}

static bool s_save(void *ctx, const wa_conf_t *c)
{
    (void)ctx;
    g_stored = *c;
    g_have_stored = true;
    ++g_saves;
    return true;
}

static bool s_erase(void *ctx)
{
    (void)ctx;
    memset(&g_stored, 0, sizeof(g_stored));
    g_have_stored = false;
    ++g_erases;
    return true;
}

static const wa_store_t k_store = { s_load, s_save, s_erase, NULL };

static char g_long[80];

TEST_CASE(a_network_name_is_one_to_32_bytes)
{
    wa_conf_t c;
    wa_conf_defaults(&c);
    memset(g_long, 'n', sizeof(g_long));
    CHECK_EQ(wa_conf_set_net(&c, g_long, 0u, "password", 8u),
             WA_CONF_SSID_EMPTY);
    CHECK_EQ(wa_conf_set_net(&c, NULL, 4u, "password", 8u),
             WA_CONF_SSID_EMPTY);
    CHECK_EQ(wa_conf_set_net(&c, g_long, 1u, "password", 8u), WA_CONF_OK);
    CHECK_EQ(wa_conf_set_net(&c, g_long, 32u, "password", 8u), WA_CONF_OK);
    CHECK_EQ(strlen(c.ssid), 32u);
    CHECK_EQ(wa_conf_set_net(&c, g_long, 33u, "password", 8u),
             WA_CONF_SSID_LONG);
    CHECK_EQ(wa_conf_set_net(&c, "ab\0cd", 5u, "password", 8u),
             WA_CONF_SSID_CHAR);
    /* Spaces and any byte but NUL are a name. */
    CHECK_EQ(wa_conf_set_net(&c, "Werkstatt 2.4 \xc3\xa4", 16u, "password",
                             8u),
             WA_CONF_OK);
}

TEST_CASE(a_password_is_8_to_63_printable_characters)
{
    wa_conf_t c;
    wa_conf_defaults(&c);
    memset(g_long, 'p', sizeof(g_long));
    CHECK_EQ(wa_conf_set_net(&c, "home", 4u, g_long, 7u),
             WA_CONF_PASS_SHORT);
    CHECK(!c.have_net);                      /* unchanged on a refusal */
    CHECK_EQ(wa_conf_set_net(&c, "home", 4u, g_long, 8u), WA_CONF_OK);
    CHECK_EQ(wa_conf_set_net(&c, "home", 4u, g_long, 63u), WA_CONF_OK);
    CHECK_EQ(strlen(c.pass), 63u);
    CHECK_EQ(wa_conf_set_net(&c, "home", 4u, g_long, 64u),
             WA_CONF_PASS_LONG);
    CHECK_EQ(strlen(c.pass), 63u);
    CHECK_EQ(wa_conf_set_net(&c, "home", 4u, "pass\tword", 9u),
             WA_CONF_PASS_CHAR);
    CHECK_EQ(wa_conf_set_net(&c, "home", 4u, "pass\x7fword", 9u),
             WA_CONF_PASS_CHAR);
    CHECK_EQ(wa_conf_set_net(&c, "home", 4u, "p\xc3\xa4sswort", 10u),
             WA_CONF_PASS_CHAR);
    /* Every printable mark and a space are taken. */
    CHECK_EQ(wa_conf_set_net(&c, "home", 4u, " !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~",
                             33u),
             WA_CONF_OK);
}

TEST_CASE(a_fresh_store_reads_as_no_network_and_the_code_on)
{
    s_erase(NULL);
    wa_conf_t c;
    memset(&c, 0x55, sizeof(c));
    wa_conf_load(&k_store, &c);
    CHECK(!c.have_net);
    CHECK(c.code_on);
    CHECK_EQ(wa_modes(&c), WA_CAN_AP);
    wa_conf_load(NULL, &c);
    CHECK(c.code_on);
}

TEST_CASE(what_is_saved_reads_back_and_forget_erases_it)
{
    s_erase(NULL);
    wa_conf_t c;
    wa_conf_load(&k_store, &c);
    CHECK_EQ(wa_conf_set_net(&c, "home", 4u, "secret-pass", 11u), WA_CONF_OK);
    c.code_on = false;
    CHECK(wa_conf_save(&k_store, &c));
    wa_conf_t back;
    wa_conf_load(&k_store, &back);
    CHECK(back.have_net);
    CHECK_STR_EQ(back.ssid, "home");
    CHECK_STR_EQ(back.pass, "secret-pass");
    CHECK(!back.code_on);
    CHECK_EQ(wa_modes(&back), WA_CAN_AP | WA_CAN_JOIN);

    /* FORGET: the network and the switch go, and the switch reads on. */
    CHECK(wa_conf_forget(&k_store, &back));
    CHECK(!back.have_net);
    CHECK(back.code_on);
    CHECK_EQ(back.ssid[0], '\0');
    CHECK_EQ(back.pass[0], '\0');
    CHECK(!g_have_stored);
    wa_conf_load(&k_store, &back);
    CHECK(!back.have_net);
    CHECK(back.code_on);
    CHECK_EQ(wa_modes(&back), WA_CAN_AP);
}

TEST_CASE(a_kept_network_the_radio_would_refuse_is_dropped)
{
    s_erase(NULL);
    wa_conf_t c;
    wa_conf_defaults(&c);
    c.have_net = true;
    strcpy(c.ssid, "home");
    strcpy(c.pass, "short");                /* 5 characters */
    c.code_on = false;
    CHECK(wa_conf_save(&k_store, &c));
    wa_conf_t back;
    wa_conf_load(&k_store, &back);
    CHECK(!back.have_net);
    CHECK(!back.code_on);                    /* the switch stays */
    /* A stored name with no terminator in its field is cut at 32. */
    memset(c.ssid, 'x', sizeof(c.ssid));
    strcpy(c.pass, "longenough");
    CHECK(wa_conf_save(&k_store, &c));
    wa_conf_load(&k_store, &back);
    CHECK(back.have_net);
    CHECK_EQ(strlen(back.ssid), 32u);
}

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
    CHECK_EQ(g_w.state, WA_ST_STARTING);
    CHECK(wa_radio(&g_w));
    CHECK(!wa_serving(&g_w));
    wa_on_ap_up(&g_w, now);
    CHECK_EQ(g_w.state, WA_ST_AP);
    CHECK(wa_serving(&g_w));
}

TEST_CASE(the_radio_does_not_start_armed_with_the_supply_on_or_a_run_open)
{
    const wa_off_t why[] = { wa_gate(true, false, false),
                             wa_gate(false, true, false),
                             wa_gate(false, false, true) };
    CHECK_EQ(why[0], WA_OFF_ARMED);
    CHECK_EQ(why[1], WA_OFF_SUPPLY);
    CHECK_EQ(why[2], WA_OFF_RUN);
    CHECK_EQ(wa_gate(true, true, true), WA_OFF_ARMED);
    CHECK_EQ(wa_gate(false, false, false), WA_OFF_NONE);
    for (int i = 0; i < 3; ++i) {
        wa_init(&g_w);
        CHECK_EQ(wa_start(&g_w, WA_MODE_AP, why[i], 0u, counter_rand, NULL),
                 why[i]);
        CHECK(!wa_radio(&g_w));
        CHECK_EQ(g_w.off, why[i]);
        CHECK_EQ(g_w.code[0], '\0');
    }
}

TEST_CASE(arm_stop_the_supply_and_a_run_each_switch_it_off)
{
    const wa_off_t gates[] = { WA_OFF_ARMED, WA_OFF_SUPPLY, WA_OFF_RUN };
    for (int i = 0; i < 3; ++i) {
        up_ap(1000u);
        CHECK(!wa_tick(&g_w, 1005u, WA_OFF_NONE, false));
        CHECK(wa_tick(&g_w, 1010u, gates[i], false));
        CHECK(!wa_radio(&g_w));
        CHECK_EQ(g_w.off, gates[i]);
        CHECK(!wa_tick(&g_w, 1015u, gates[i], false));   /* once */
    }
    up_ap(1000u);
    CHECK(wa_tick(&g_w, 1010u, WA_OFF_NONE, true));
    CHECK_EQ(g_w.off, WA_OFF_STOP);
    /* STOP while a download is under way switches off as well. */
    up_ap(1000u);
    wa_request_begin(&g_w, 1001u);
    CHECK(wa_tick(&g_w, 1010u, WA_OFF_NONE, true));
    CHECK_EQ(g_w.busy, 0u);
    /* And while it is only coming up, or joining. */
    wa_init(&g_w);
    (void)wa_start(&g_w, WA_MODE_JOIN, WA_OFF_NONE, 0u, counter_rand, NULL);
    CHECK(wa_tick(&g_w, 5u, WA_OFF_ARMED, false));
    CHECK_EQ(g_w.off, WA_OFF_ARMED);
}

TEST_CASE(ten_minutes_without_a_request_switch_it_off)
{
    up_ap(0u);
    CHECK_EQ(wa_idle_left(&g_w, 0u), WA_IDLE_MS);
    CHECK(!wa_tick(&g_w, WA_IDLE_MS - 1u, WA_OFF_NONE, false));
    CHECK_EQ(wa_idle_left(&g_w, WA_IDLE_MS - 1u), 1u);
    CHECK(wa_tick(&g_w, WA_IDLE_MS, WA_OFF_NONE, false));
    CHECK_EQ(g_w.off, WA_OFF_IDLE);
    CHECK_EQ(wa_idle_left(&g_w, WA_IDLE_MS), 0u);

    /* A request starts the ten minutes again from its end. */
    up_ap(0u);
    wa_request_begin(&g_w, 60000u);
    wa_request_end(&g_w, 70000u);
    CHECK(!wa_tick(&g_w, 70000u + WA_IDLE_MS - 1u, WA_OFF_NONE, false));
    CHECK(wa_tick(&g_w, 70000u + WA_IDLE_MS, WA_OFF_NONE, false));

    /* One under way holds it off however long it takes: a ZIP of a full
     * card is not cut at ten minutes. */
    up_ap(0u);
    wa_request_begin(&g_w, 1000u);
    CHECK(!wa_tick(&g_w, 1000u + 3u * WA_IDLE_MS, WA_OFF_NONE, false));
    CHECK_EQ(wa_idle_left(&g_w, 5000u), 0u);
    wa_request_end(&g_w, 1000u + 3u * WA_IDLE_MS);
    CHECK(!wa_tick(&g_w, 1000u + 4u * WA_IDLE_MS - 1u, WA_OFF_NONE, false));
    CHECK(wa_tick(&g_w, 1000u + 4u * WA_IDLE_MS, WA_OFF_NONE, false));

    /* Across the 32-bit millisecond wrap at 49.7 days. */
    up_ap(0xFFFFFF00u);
    CHECK(!wa_tick(&g_w, 0xFFFFFF00u + WA_IDLE_MS - 1u, WA_OFF_NONE, false));
    CHECK(wa_tick(&g_w, 0xFFFFFF00u + WA_IDLE_MS, WA_OFF_NONE, false));
}

TEST_CASE(an_access_point_that_does_not_come_up_is_a_failure)
{
    wa_init(&g_w);
    (void)wa_start(&g_w, WA_MODE_AP, WA_OFF_NONE, 0u, counter_rand, NULL);
    CHECK(!wa_tick(&g_w, WA_START_MS - 1u, WA_OFF_NONE, false));
    CHECK(wa_tick(&g_w, WA_START_MS, WA_OFF_NONE, false));
    CHECK_EQ(g_w.off, WA_OFF_FAILED);
    wa_init(&g_w);
    (void)wa_start(&g_w, WA_MODE_AP, WA_OFF_NONE, 0u, counter_rand, NULL);
    wa_on_failed(&g_w);
    CHECK_EQ(g_w.off, WA_OFF_FAILED);
    CHECK(!wa_radio(&g_w));
}

TEST_CASE(a_join_reaches_the_network_then_an_address)
{
    wa_init(&g_w);
    CHECK_EQ(wa_start(&g_w, WA_MODE_JOIN, WA_OFF_NONE, 0u, counter_rand,
                      NULL),
             WA_OFF_NONE);
    CHECK_EQ(g_w.state, WA_ST_JOINING);
    CHECK(!wa_serving(&g_w));
    wa_on_associated(&g_w, 3000u);
    CHECK_EQ(g_w.state, WA_ST_ADDRESS);
    wa_on_address(&g_w, 0x1701A8C0u, 4000u);
    CHECK_EQ(g_w.state, WA_ST_JOINED);
    CHECK(wa_serving(&g_w));
    CHECK_EQ(g_w.ip, 0x1701A8C0u);
    CHECK_EQ(wa_idle_left(&g_w, 4000u), WA_IDLE_MS);
}

TEST_CASE(each_way_a_join_fails_is_named)
{
    /* The network refused the password. */
    wa_init(&g_w);
    (void)wa_start(&g_w, WA_MODE_JOIN, WA_OFF_NONE, 0u, counter_rand, NULL);
    wa_on_join_failed(&g_w, WA_JOIN_PASSWORD);
    CHECK_EQ(g_w.off, WA_OFF_JOIN);
    CHECK_EQ(g_w.join_err, WA_JOIN_PASSWORD);
    CHECK(!wa_radio(&g_w));
    /* The radio says no such network. */
    wa_init(&g_w);
    (void)wa_start(&g_w, WA_MODE_JOIN, WA_OFF_NONE, 0u, counter_rand, NULL);
    wa_on_join_failed(&g_w, WA_JOIN_NOT_FOUND);
    CHECK_EQ(g_w.join_err, WA_JOIN_NOT_FOUND);
    /* Nothing heard for WA_JOIN_MS: not found either. */
    wa_init(&g_w);
    (void)wa_start(&g_w, WA_MODE_JOIN, WA_OFF_NONE, 0u, counter_rand, NULL);
    CHECK(!wa_tick(&g_w, WA_JOIN_MS - 1u, WA_OFF_NONE, false));
    CHECK(wa_tick(&g_w, WA_JOIN_MS, WA_OFF_NONE, false));
    CHECK_EQ(g_w.off, WA_OFF_JOIN);
    CHECK_EQ(g_w.join_err, WA_JOIN_NOT_FOUND);
    /* On the network, no address for WA_ADDRESS_MS. */
    wa_init(&g_w);
    (void)wa_start(&g_w, WA_MODE_JOIN, WA_OFF_NONE, 0u, counter_rand, NULL);
    wa_on_associated(&g_w, 2000u);
    CHECK(!wa_tick(&g_w, 2000u + WA_ADDRESS_MS - 1u, WA_OFF_NONE, false));
    CHECK(wa_tick(&g_w, 2000u + WA_ADDRESS_MS, WA_OFF_NONE, false));
    CHECK_EQ(g_w.join_err, WA_JOIN_NO_ADDRESS);
    /* Joined, then the network went away. */
    wa_init(&g_w);
    (void)wa_start(&g_w, WA_MODE_JOIN, WA_OFF_NONE, 0u, counter_rand, NULL);
    wa_on_address(&g_w, 1u, 10u);
    wa_on_join_failed(&g_w, WA_JOIN_LOST);
    CHECK_EQ(g_w.join_err, WA_JOIN_LOST);
    CHECK_EQ(g_w.ip, 0u);
    /* A late report after the session ended changes nothing. */
    wa_on_join_failed(&g_w, WA_JOIN_PASSWORD);
    CHECK_EQ(g_w.join_err, WA_JOIN_LOST);
    wa_on_address(&g_w, 5u, 20u);
    CHECK(!wa_radio(&g_w));
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

/* ------------------------------------------------------- the code */

TEST_CASE(with_the_code_on_the_page_answers_only_the_right_code)
{
    up_ap(0u);
    char code[9];
    memcpy(code, g_w.code, 9u);
    CHECK_EQ(wa_code_check(&g_w, NULL, true), WA_CODE_MISSING);
    CHECK_EQ(wa_code_check(&g_w, "", true), WA_CODE_MISSING);
    CHECK_EQ(wa_code_check(&g_w, "aaaaaaaa", true), WA_CODE_WRONG);
    CHECK_EQ(wa_code_check(&g_w, code, true), WA_CODE_OK);
    /* One character short, one long, one changed. */
    char c[12];
    memcpy(c, code, 9u);
    c[7] = '\0';
    CHECK_EQ(wa_code_check(&g_w, c, true), WA_CODE_WRONG);
    memcpy(c, code, 8u);
    c[8] = 'x';
    c[9] = '\0';
    CHECK_EQ(wa_code_check(&g_w, c, true), WA_CODE_WRONG);
    memcpy(c, code, 9u);
    c[3] = (c[3] == 'a') ? 'b' : 'a';
    CHECK_EQ(wa_code_check(&g_w, c, true), WA_CODE_WRONG);
    CHECK_EQ(g_w.guesses, 4u);
}

TEST_CASE(with_the_code_off_every_request_passes)
{
    up_ap(0u);
    CHECK_EQ(wa_code_check(&g_w, NULL, false), WA_CODE_OK);
    CHECK_EQ(wa_code_check(&g_w, "wrongone", false), WA_CODE_OK);
    CHECK_EQ(wa_code_check(&g_w, g_w.code, false), WA_CODE_OK);
    CHECK_EQ(g_w.guesses, 0u);
}

TEST_CASE(a_code_from_before_the_screen_was_reopened_opens_nothing)
{
    up_ap(0u);
    char old[9];
    memcpy(old, g_w.code, 9u);
    CHECK_EQ(wa_code_check(&g_w, old, true), WA_CODE_OK);
    wa_close(&g_w, WA_OFF_ASKED);
    CHECK_EQ(wa_code_check(&g_w, old, true), WA_CODE_CLOSED);
    CHECK_EQ(wa_start(&g_w, WA_MODE_AP, WA_OFF_NONE, 10u, counter_rand, NULL),
             WA_OFF_NONE);
    wa_on_ap_up(&g_w, 10u);
    CHECK(strcmp(old, g_w.code) != 0);
    CHECK_EQ(wa_code_check(&g_w, old, true), WA_CODE_WRONG);
    CHECK_EQ(wa_code_check(&g_w, g_w.code, true), WA_CODE_OK);
    /* The same holds on the home network. */
    wa_init(&g_w);
    (void)wa_start(&g_w, WA_MODE_JOIN, WA_OFF_NONE, 0u, counter_rand, NULL);
    CHECK_EQ(wa_code_check(&g_w, g_w.code, true), WA_CODE_CLOSED);
    wa_on_address(&g_w, 1u, 5u);
    CHECK_EQ(wa_code_check(&g_w, old, true), WA_CODE_WRONG);
    CHECK_EQ(wa_code_check(&g_w, g_w.code, true), WA_CODE_OK);
}

TEST_CASE(sixteen_wrong_codes_switch_the_radio_off)
{
    up_ap(0u);
    for (unsigned i = 1; i < WA_GUESS_MAX; ++i) {
        CHECK_EQ(wa_code_check(&g_w, "zzzzzzzz", true), WA_CODE_WRONG);
        CHECK(wa_radio(&g_w));
    }
    CHECK_EQ(wa_code_check(&g_w, "zzzzzzzz", true), WA_CODE_WRONG);
    CHECK(!wa_radio(&g_w));
    CHECK_EQ(g_w.off, WA_OFF_GUESSES);
    /* A missing code is the form being asked for, not a guess. */
    up_ap(0u);
    for (int i = 0; i < 40; ++i) {
        CHECK_EQ(wa_code_check(&g_w, NULL, true), WA_CODE_MISSING);
    }
    CHECK(wa_radio(&g_w));
}

int main(void)
{
    RUN(a_secret_is_eight_characters_of_the_alphabet);
    RUN(bytes_that_would_favour_some_characters_are_drawn_again);
    RUN(every_start_makes_a_fresh_password_and_code);
    RUN(the_name_carries_the_last_two_bytes_of_the_mac);
    RUN(a_network_name_is_one_to_32_bytes);
    RUN(a_password_is_8_to_63_printable_characters);
    RUN(a_fresh_store_reads_as_no_network_and_the_code_on);
    RUN(what_is_saved_reads_back_and_forget_erases_it);
    RUN(a_kept_network_the_radio_would_refuse_is_dropped);
    RUN(the_scan_list_holds_each_usable_network_once_strongest_first);
    RUN(a_full_scan_list_keeps_the_strongest);
    RUN(the_radio_does_not_start_armed_with_the_supply_on_or_a_run_open);
    RUN(arm_stop_the_supply_and_a_run_each_switch_it_off);
    RUN(ten_minutes_without_a_request_switch_it_off);
    RUN(an_access_point_that_does_not_come_up_is_a_failure);
    RUN(a_join_reaches_the_network_then_an_address);
    RUN(each_way_a_join_fails_is_named);
    RUN(a_scan_runs_the_radio_and_ends_off);
    RUN(with_the_code_on_the_page_answers_only_the_right_code);
    RUN(with_the_code_off_every_request_passes);
    RUN(a_code_from_before_the_screen_was_reopened_opens_nothing);
    RUN(sixteen_wrong_codes_switch_the_radio_off);
    return test_summary("wa_session");
}
