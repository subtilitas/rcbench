/**
 * @file test_wa_cookie.c
 * @brief A wrong code in a cookie counts once (wa_code_check_cookie()): what a
 *        browser repeats without anyone typing leaves the radio on, and new
 *        values and the entry still reach WA_GUESS_MAX.
 *
 * Cases:
 * - a_hundred_requests_with_one_stale_cookie_leave_the_radio_on
 * - sixteen_different_wrong_cookies_switch_the_radio_off
 * - a_stale_cookie_then_a_good_code_opens
 * - entry_and_cookie_count_together_and_the_entry_every_time
 * - a_fifth_value_pushes_the_oldest_out_and_it_counts_again
 * - a_start_empties_the_memory_and_makes_a_new_key
 * - the_memory_holds_no_value
 * - no_cookie_a_right_cookie_and_the_switch_off_count_nothing
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "wa_session.h"

/**
 * @brief wa_rand_fn counting up.
 * @param ctx  the counter
 * @return the next word
 */
static uint32_t count_rand(void *ctx)
{
    uint32_t *n = ctx;
    *n += 0x01020304u;
    return *n;
}

/** @brief The rand's state, so every start differs. */
static uint32_t g_seed;

/**
 * @brief A session at the access point: RANDOM, or with @p kept as WEB CODE.
 * @param w     The session.
 * @param kept  The kept code, or NULL for RANDOM.
 */
static void start(wa_t *w, const char *kept)
{
    wa_conf_t c;
    wa_conf_defaults(&c);
    if (kept != NULL) {
        (void)wa_conf_set_kept(&c, WA_KEPT_CODE, kept, strlen(kept));
    }
    wa_init(w);
    (void)wa_start(w, WA_MODE_AP, WA_OFF_NONE, 0u, count_rand, &g_seed);
    wa_kept_use(w, &c);
    wa_on_ap_up(w, 0u);
}

/**
 * @brief A wrong value that differs for each @p i.
 * @param buf  takes it
 * @param n    its room
 * @param i    which
 * @return @p buf
 */
static const char *wrong(char *buf, size_t n, unsigned i)
{
    snprintf(buf, n, "wrong-%03u", i);
    return buf;
}

/** @brief The session under each case: RANDOM, then kept. */
static const char *const k_kinds[] = { NULL, "the new kept code" };

TEST_CASE(a_hundred_requests_with_one_stale_cookie_leave_the_radio_on)
{
    for (size_t k = 0u; k < 2u; ++k) {
        wa_t w;
        start(&w, k_kinds[k]);
        /* A RANDOM code of an earlier session, a kept code since changed. */
        char old[WA_PASS_MAX + 1u] = "oldcode8";
        if (k == 1u) {
            strcpy(old, "the old kept code");
        }
        for (int i = 0; i < 100; ++i) {
            CHECK_EQ(wa_code_check_cookie(&w, old, true), WA_CODE_WRONG);
            CHECK(wa_serving(&w));
        }
        CHECK_EQ(w.guesses, 1u);
    }
}

TEST_CASE(sixteen_different_wrong_cookies_switch_the_radio_off)
{
    for (size_t k = 0u; k < 2u; ++k) {
        wa_t w;
        char b[16];
        start(&w, k_kinds[k]);
        for (unsigned i = 0u; i + 1u < WA_GUESS_MAX; ++i) {
            CHECK_EQ(wa_code_check_cookie(&w, wrong(b, sizeof(b), i), true),
                     WA_CODE_WRONG);
            CHECK(wa_serving(&w));
            CHECK_EQ(w.guesses, i + 1u);
        }
        CHECK_EQ(wa_code_check_cookie(&w, wrong(b, sizeof(b), 99u), true),
                 WA_CODE_WRONG);
        CHECK(!wa_serving(&w));
        CHECK_EQ(w.off, WA_OFF_GUESSES);
        CHECK_EQ(wa_code_check_cookie(&w, b, true), WA_CODE_CLOSED);
    }
}

TEST_CASE(a_stale_cookie_then_a_good_code_opens)
{
    for (size_t k = 0u; k < 2u; ++k) {
        wa_t w;
        start(&w, k_kinds[k]);
        char right[WA_PASS_MAX + 1u];
        strcpy(right, w.code);
        for (int i = 0; i < 20; ++i) {
            CHECK_EQ(wa_code_check_cookie(&w, "oldcode8", true), WA_CODE_WRONG);
        }
        /* The entry, then the cookie the answer set. */
        CHECK_EQ(wa_code_check(&w, right, true), WA_CODE_OK);
        CHECK_EQ(wa_code_check_cookie(&w, right, true), WA_CODE_OK);
        CHECK_EQ(w.guesses, 1u);
        CHECK(wa_serving(&w));
    }
}

TEST_CASE(entry_and_cookie_count_together_and_the_entry_every_time)
{
    wa_t w;
    char b[16];
    start(&w, NULL);
    /* The same value in the entry counts each time, in the cookie once, and
     * the order does not matter. */
    CHECK_EQ(wa_code_check_cookie(&w, "samevalue", true), WA_CODE_WRONG);
    CHECK_EQ(w.guesses, 1u);
    CHECK_EQ(wa_code_check(&w, "samevalue", true), WA_CODE_WRONG);
    CHECK_EQ(wa_code_check(&w, "samevalue", true), WA_CODE_WRONG);
    CHECK_EQ(w.guesses, 3u);
    CHECK_EQ(wa_code_check_cookie(&w, "samevalue", true), WA_CODE_WRONG);
    CHECK_EQ(w.guesses, 3u);
    /* A value tried in the entry alone is new to the cookie's memory. */
    CHECK_EQ(wa_code_check(&w, "entryonly", true), WA_CODE_WRONG);
    CHECK_EQ(wa_code_check_cookie(&w, "entryonly", true), WA_CODE_WRONG);
    CHECK_EQ(w.guesses, 5u);
    /* To the 15th by turns: 5 so far, then 5 cookies and 5 entries. */
    for (unsigned i = 0u; i < 5u; ++i) {
        CHECK_EQ(wa_code_check_cookie(&w, wrong(b, sizeof(b), i), true),
                 WA_CODE_WRONG);
        CHECK_EQ(wa_code_check(&w, "samevalue", true), WA_CODE_WRONG);
    }
    CHECK_EQ(w.guesses, WA_GUESS_MAX - 1u);
    CHECK(wa_serving(&w));
    /* A repeat at 15 does not end it; the entry does. */
    CHECK_EQ(wa_code_check_cookie(&w, b, true), WA_CODE_WRONG);
    CHECK(wa_serving(&w));
    CHECK_EQ(wa_code_check(&w, "samevalue", true), WA_CODE_WRONG);
    CHECK(!wa_serving(&w));
}

TEST_CASE(a_fifth_value_pushes_the_oldest_out_and_it_counts_again)
{
    wa_t w;
    char b[16];
    start(&w, NULL);
    for (unsigned i = 0u; i < WA_STALE_KEPT; ++i) {
        (void)wa_code_check_cookie(&w, wrong(b, sizeof(b), i), true);
    }
    CHECK_EQ(w.guesses, WA_STALE_KEPT);
    /* All four are held, whichever comes. */
    for (unsigned i = 0u; i < WA_STALE_KEPT; ++i) {
        (void)wa_code_check_cookie(&w, wrong(b, sizeof(b), i), true);
    }
    CHECK_EQ(w.guesses, WA_STALE_KEPT);
    /* A fifth takes the oldest's place: the others still hold. */
    (void)wa_code_check_cookie(&w, wrong(b, sizeof(b), 50u), true);
    CHECK_EQ(w.guesses, WA_STALE_KEPT + 1u);
    (void)wa_code_check_cookie(&w, wrong(b, sizeof(b), 1u), true);
    (void)wa_code_check_cookie(&w, wrong(b, sizeof(b), 3u), true);
    CHECK_EQ(w.guesses, WA_STALE_KEPT + 1u);
    (void)wa_code_check_cookie(&w, wrong(b, sizeof(b), 0u), true);
    CHECK_EQ(w.guesses, WA_STALE_KEPT + 2u);
    /* Values cycling past the memory reach the limit: it only ever counts
     * more. */
    for (unsigned i = 0u; wa_serving(&w) && i < 2u * WA_GUESS_MAX; ++i) {
        (void)wa_code_check_cookie(&w,
                                   wrong(b, sizeof(b), i % (WA_STALE_KEPT + 1u)),
                                   true);
    }
    CHECK(!wa_serving(&w));
}

TEST_CASE(a_start_empties_the_memory_and_makes_a_new_key)
{
    for (size_t k = 0u; k < 2u; ++k) {
        wa_t w;
        start(&w, k_kinds[k]);
        CHECK_EQ(wa_code_check_cookie(&w, "oldcode8", true), WA_CODE_WRONG);
        uint8_t key[WA_STALE_KEY];
        uint8_t hash[WA_STALE_HASH];
        memcpy(key, w.stale_key, sizeof(key));
        memcpy(hash, w.stale[0], sizeof(hash));
        /* wa_start() itself, as a restart of the radio does it, no wa_init. */
        wa_close(&w, WA_OFF_ASKED);
        (void)wa_start(&w, WA_MODE_AP, WA_OFF_NONE, 0u, count_rand, &g_seed);
        wa_on_ap_up(&w, 0u);
        CHECK_EQ(w.guesses, 0u);
        CHECK_EQ(w.stale_n, 0u);
        CHECK(memcmp(key, w.stale_key, sizeof(key)) != 0);
        /* The same value counts once more, under another hash. */
        CHECK_EQ(wa_code_check_cookie(&w, "oldcode8", true), WA_CODE_WRONG);
        CHECK_EQ(w.guesses, 1u);
        CHECK(memcmp(hash, w.stale[0], sizeof(hash)) != 0);
    }
}

TEST_CASE(the_memory_holds_no_value)
{
    wa_t w;
    start(&w, NULL);
    static const char mark[] = "S3cretWrongValue";
    CHECK_EQ(wa_code_check_cookie(&w, mark, true), WA_CODE_WRONG);
    const uint8_t *p = (const uint8_t *)&w;
    for (size_t i = 0u; i + 4u <= sizeof(w); ++i) {
        CHECK(memcmp(p + i, mark, 4u) != 0);
    }
}

TEST_CASE(no_cookie_a_right_cookie_and_the_switch_off_count_nothing)
{
    wa_t w;
    start(&w, "the new kept code");
    for (int i = 0; i < 100; ++i) {
        CHECK_EQ(wa_code_check_cookie(&w, NULL, true), WA_CODE_MISSING);
        CHECK_EQ(wa_code_check_cookie(&w, "", true), WA_CODE_MISSING);
        CHECK_EQ(wa_code_check_cookie(&w, "the new kept code", true),
                 WA_CODE_OK);
        CHECK_EQ(wa_code_check_cookie(&w, "anything", false), WA_CODE_OK);
    }
    CHECK_EQ(w.guesses, 0u);
    CHECK_EQ(w.stale_n, 0u);
    /* One byte off either way is a wrong code, once. */
    CHECK_EQ(wa_code_check_cookie(&w, "the new kept cod", true), WA_CODE_WRONG);
    CHECK_EQ(wa_code_check_cookie(&w, "the new kept codes", true),
             WA_CODE_WRONG);
    CHECK_EQ(w.guesses, 2u);
    /* A closed session answers so, the memory untouched. */
    wa_close(&w, WA_OFF_ASKED);
    CHECK_EQ(wa_code_check_cookie(&w, "wrong one", true), WA_CODE_CLOSED);
    CHECK_EQ(w.stale_n, 2u);
}

int main(void)
{
    RUN(a_hundred_requests_with_one_stale_cookie_leave_the_radio_on);
    RUN(sixteen_different_wrong_cookies_switch_the_radio_off);
    RUN(a_stale_cookie_then_a_good_code_opens);
    RUN(entry_and_cookie_count_together_and_the_entry_every_time);
    RUN(a_fifth_value_pushes_the_oldest_out_and_it_counts_again);
    RUN(a_start_empties_the_memory_and_makes_a_new_key);
    RUN(the_memory_holds_no_value);
    RUN(no_cookie_a_right_cookie_and_the_switch_off_count_nothing);
    return test_summary("wa_cookie");
}
