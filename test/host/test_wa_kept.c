/**
 * @file test_wa_kept.c
 * @brief HOTSPOT PASSWORD and WEB CODE in the Wi-Fi session (wa_session.h): the
 *        rule at 7, 8, 63 and 64 characters, a refused character, RANDOM after
 *        a value, a store with no key, FORGET, the code check with a kept code
 *        of every length, the 16 wrong ones, the cookie's form, and the value
 *        in no answer of the page or the file API.
 *
 * Cases:
 * - the_rule_holds_at_7_8_63_and_64_characters
 * - a_character_outside_printable_ascii_is_refused
 * - random_after_a_value_and_a_refusal_leaves_what_stands
 * - a_store_with_no_key_loads_as_random_with_the_rest_unchanged
 * - a_stored_value_the_rule_refuses_loads_as_random
 * - values_survive_a_save_and_a_load
 * - forget_keeps_both_values_through_the_store
 * - forget_with_nothing_kept_owes_only_the_erase
 * - a_value_typed_after_forget_is_saved_with_no_erase_before_it
 * - a_start_takes_the_kept_values_and_random_stays_fresh
 * - a_code_changed_ends_the_old_one_at_the_next_start
 * - the_check_takes_kept_codes_of_8_and_63_and_nothing_near
 * - sixteen_wrong_codes_end_the_radio_with_a_kept_code
 * - the_code_switch_off_still_passes_with_a_kept_code
 * - a_kept_code_survives_the_cookie_and_the_form
 * - the_code_form_takes_the_longest_code_at_its_limit
 * - the_value_is_in_no_page_and_no_answer_of_the_file_api
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "wa_api.h"
#include "wa_fakefs.h"
#include "wa_guard.h"
#include "wa_page.h"
#include "wa_session.h"
#include "wa_text.h"

/** @brief A marker no text of the page holds. */
#define MARK "S3cretMark;% +"

static wa_conf_t g_stored;
static bool g_have;
static int g_saves;
static int g_erases;
static bool g_save_fails;

/**
 * @brief The store's load.
 * @param ctx  unused
 * @param out  takes what is kept
 * @return false for nothing kept
 */
static bool st_load(void *ctx, wa_conf_t *out)
{
    (void)ctx;
    if (g_have) {
        *out = g_stored;
    }
    return g_have;
}

/**
 * @brief The store's save.
 * @param ctx  unused
 * @param c    what to keep
 * @return true
 */
static bool st_save(void *ctx, const wa_conf_t *c)
{
    (void)ctx;
    if (g_save_fails) {
        return false;
    }
    g_stored = *c;
    g_have = true;
    ++g_saves;
    return true;
}

/**
 * @brief The store's erase: all of it, as nvs_erase_all().
 * @param ctx  unused
 * @return true
 */
static bool st_erase(void *ctx)
{
    (void)ctx;
    memset(&g_stored, 0, sizeof(g_stored));
    g_have = false;
    ++g_erases;
    return true;
}

/** @brief The store under test. */
static const wa_store_t k_st = { st_load, st_save, st_erase, NULL };

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

/**
 * @brief @p n characters of "x".
 * @param buf  takes them, terminated
 * @param n    how many
 * @return @p buf
 */
static char *xs(char *buf, size_t n)
{
    memset(buf, 'x', n);
    buf[n] = '\0';
    return buf;
}

/**
 * @brief A session started with what @p c keeps, as wifi_access_start().
 * @param w  the session
 * @param c  the settings
 */
static void start(wa_t *w, const wa_conf_t *c)
{
    static uint32_t seed;
    wa_init(w);
    CHECK_EQ(wa_start(w, WA_MODE_AP, WA_OFF_NONE, 0u, count_rand, &seed),
             WA_OFF_NONE);
    wa_kept_use(w, c);
    wa_on_ap_up(w, 0u);
}

TEST_CASE(the_rule_holds_at_7_8_63_and_64_characters)
{
    char b[80];
    for (int which = 0; which < 2; ++which) {
        wa_conf_t c;
        wa_conf_defaults(&c);
        const wa_kept_t k = (wa_kept_t)which;
        const char *field = (which == 0) ? c.ap_pass : c.web_code;
        CHECK_EQ(wa_conf_set_kept(&c, k, xs(b, 7u), 7u), WA_CONF_PASS_SHORT);
        CHECK_STR_EQ(field, "");
        CHECK_EQ(wa_conf_set_kept(&c, k, xs(b, 8u), 8u), WA_CONF_OK);
        CHECK_EQ(strlen(field), 8u);
        CHECK_EQ(wa_conf_set_kept(&c, k, xs(b, 63u), 63u), WA_CONF_OK);
        CHECK_EQ(strlen(field), 63u);
        CHECK_EQ(wa_conf_set_kept(&c, k, xs(b, 64u), 64u), WA_CONF_PASS_LONG);
        CHECK_EQ(strlen(field), 63u);
    }
    CHECK_EQ(wa_kept_check(NULL, 8u), WA_CONF_PASS_SHORT);
}

TEST_CASE(a_character_outside_printable_ascii_is_refused)
{
    char b[16] = "12345678";
    /* The two ends of the range are taken. */
    b[3] = 0x20;
    CHECK_EQ(wa_kept_check(b, 8u), WA_CONF_OK);
    b[3] = 0x7E;
    CHECK_EQ(wa_kept_check(b, 8u), WA_CONF_OK);
    /* Just outside them, a control byte, and UTF-8. */
    b[3] = 0x1F;
    CHECK_EQ(wa_kept_check(b, 8u), WA_CONF_PASS_CHAR);
    b[3] = 0x7F;
    CHECK_EQ(wa_kept_check(b, 8u), WA_CONF_PASS_CHAR);
    b[3] = '\0';
    CHECK_EQ(wa_kept_check(b, 8u), WA_CONF_PASS_CHAR);
    b[3] = (char)0xC3;
    CHECK_EQ(wa_kept_check(b, 8u), WA_CONF_PASS_CHAR);
    b[7] = '\n';
    b[3] = '4';
    CHECK_EQ(wa_kept_check(b, 8u), WA_CONF_PASS_CHAR);
}

TEST_CASE(random_after_a_value_and_a_refusal_leaves_what_stands)
{
    wa_conf_t c;
    wa_conf_defaults(&c);
    CHECK_STR_EQ(c.ap_pass, "");
    CHECK_STR_EQ(c.web_code, "");
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_PASS, "password", 8u), WA_CONF_OK);
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_CODE, "webcode1", 8u), WA_CONF_OK);
    /* Each is its own. */
    CHECK_STR_EQ(c.ap_pass, "password");
    CHECK_STR_EQ(c.web_code, "webcode1");
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_PASS, "short", 5u),
             WA_CONF_PASS_SHORT);
    CHECK_STR_EQ(c.ap_pass, "password");
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_PASS, NULL, 0u), WA_CONF_OK);
    CHECK_STR_EQ(c.ap_pass, "");
    CHECK_STR_EQ(c.web_code, "webcode1");
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_CODE, "", 0u), WA_CONF_OK);
    CHECK_STR_EQ(c.web_code, "");
    /* A short value after a long one leaves nothing of the long one. */
    char b[80];
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_CODE, xs(b, 63u), 63u), WA_CONF_OK);
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_CODE, "12345678", 8u), WA_CONF_OK);
    for (size_t i = 8u; i < sizeof(c.web_code); ++i) {
        CHECK_EQ(c.web_code[i], '\0');
    }
}

TEST_CASE(a_store_with_no_key_loads_as_random_with_the_rest_unchanged)
{
    /* What nvs_conf_load() leaves on a store a build without the keys wrote. */
    wa_conf_defaults(&g_stored);
    g_stored.code_on = false;
    CHECK_EQ(wa_conf_set_net(&g_stored, "Workshop", 8u, "netpass1", 8u),
             WA_CONF_OK);
    g_have = true;
    wa_conf_t c;
    wa_conf_load(&k_st, &c);
    CHECK_STR_EQ(c.ap_pass, "");
    CHECK_STR_EQ(c.web_code, "");
    CHECK(!c.code_on);
    CHECK(c.have_net);
    CHECK_STR_EQ(c.ssid, "Workshop");
    CHECK_STR_EQ(c.pass, "netpass1");
    /* Nothing kept at all. */
    g_have = false;
    wa_conf_load(&k_st, &c);
    CHECK_STR_EQ(c.ap_pass, "");
    CHECK(c.code_on);
}

TEST_CASE(a_stored_value_the_rule_refuses_loads_as_random)
{
    wa_conf_defaults(&g_stored);
    g_have = true;
    /* Too short, a bad byte, no terminator in its field. */
    strcpy(g_stored.ap_pass, "short");
    strcpy(g_stored.web_code, "bad\x01" "byte");
    wa_conf_t c;
    wa_conf_load(&k_st, &c);
    CHECK_STR_EQ(c.ap_pass, "");
    CHECK_STR_EQ(c.web_code, "");
    memset(g_stored.ap_pass, 'y', sizeof(g_stored.ap_pass));
    wa_conf_load(&k_st, &c);
    CHECK_EQ(strlen(c.ap_pass), 63u);
}

TEST_CASE(values_survive_a_save_and_a_load)
{
    wa_conf_t c;
    wa_conf_defaults(&c);
    g_have = false;
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_PASS, "pass word", 9u), WA_CONF_OK);
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_CODE, "web;code", 8u), WA_CONF_OK);
    CHECK(wa_conf_save(&k_st, &c));
    wa_conf_t back;
    wa_conf_load(&k_st, &back);
    CHECK_STR_EQ(back.ap_pass, "pass word");
    CHECK_STR_EQ(back.web_code, "web;code");
}

TEST_CASE(forget_keeps_both_values_through_the_store)
{
    wa_conf_t c;
    wa_conf_defaults(&c);
    g_have = false;
    CHECK_EQ(wa_conf_set_net(&c, "Workshop", 8u, "netpass1", 8u), WA_CONF_OK);
    c.code_on = false;
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_PASS, "password", 8u), WA_CONF_OK);
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_CODE, "webcode1", 8u), WA_CONF_OK);
    CHECK(wa_conf_save(&k_st, &c));
    wa_pending_t p;
    wa_pending_init(&p);
    wa_forget(&c, &p);
    /* In memory: the network gone and the code on, the values kept. */
    CHECK(!c.have_net);
    CHECK_STR_EQ(c.ssid, "");
    CHECK_STR_EQ(c.pass, "");
    CHECK(c.code_on);
    CHECK_STR_EQ(c.ap_pass, "password");
    CHECK_STR_EQ(c.web_code, "webcode1");
    /* In the store, after its write: the same. */
    CHECK(wa_pending_due(&p, true, 0u));
    CHECK(!p.erase);
    bool erase_ok = false;
    bool save_ok = false;
    const int erases = g_erases;
    /* A save that fails, or a power cut before it: the store is as it was,
     * the values there, and the save still owed. */
    g_save_fails = true;
    wa_pending_write(&k_st, &p, &c, &erase_ok, &save_ok);
    g_save_fails = false;
    CHECK(erase_ok && !save_ok);
    wa_pending_done(&p, &p, erase_ok, save_ok, 0u);
    CHECK(p.save);
    CHECK_STR_EQ(g_stored.ap_pass, "password");
    CHECK_STR_EQ(g_stored.web_code, "webcode1");
    wa_pending_write(&k_st, &p, &c, &erase_ok, &save_ok);
    CHECK(erase_ok && save_ok);
    CHECK_EQ(g_erases, erases);
    wa_conf_t back;
    wa_conf_load(&k_st, &back);
    CHECK(!back.have_net);
    CHECK(back.code_on);
    CHECK_STR_EQ(back.ap_pass, "password");
    CHECK_STR_EQ(back.web_code, "webcode1");
}

TEST_CASE(forget_with_nothing_kept_owes_only_the_erase)
{
    wa_conf_t c;
    wa_conf_defaults(&c);
    CHECK_EQ(wa_conf_set_net(&c, "Workshop", 8u, "netpass1", 8u), WA_CONF_OK);
    wa_pending_t p;
    wa_pending_init(&p);
    wa_forget(&c, &p);
    CHECK(p.erase);
    CHECK(!p.save);
}

TEST_CASE(a_value_typed_after_forget_is_saved_with_no_erase_before_it)
{
    wa_conf_t c;
    wa_conf_defaults(&c);
    CHECK_EQ(wa_conf_set_net(&c, "Workshop", 8u, "netpass1", 8u), WA_CONF_OK);
    g_have = false;
    CHECK(wa_conf_save(&k_st, &c));
    wa_pending_t p;
    wa_pending_init(&p);
    wa_forget(&c, &p);
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_CODE, "webcode1", 8u), WA_CONF_OK);
    wa_pending_change(&p);
    CHECK(p.erase && p.save);
    bool erase_ok = false;
    bool save_ok = false;
    const int erases = g_erases;
    wa_pending_write(&k_st, &p, &c, &erase_ok, &save_ok);
    CHECK(erase_ok && save_ok);
    CHECK_EQ(g_erases, erases);
    CHECK(!g_stored.have_net);
    CHECK_STR_EQ(g_stored.web_code, "webcode1");
}

TEST_CASE(a_start_takes_the_kept_values_and_random_stays_fresh)
{
    wa_conf_t c;
    wa_conf_defaults(&c);
    wa_t w;
    /* RANDOM: 8 characters, new at each start. */
    start(&w, &c);
    CHECK_EQ(strlen(w.pass), WA_SECRET_LEN);
    CHECK_EQ(strlen(w.code), WA_SECRET_LEN);
    char first[WA_SECRET_LEN + 1u];
    memcpy(first, w.code, sizeof(first));
    start(&w, &c);
    CHECK(strcmp(first, w.code) != 0);
    /* One kept: the other stays random. */
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_PASS, "password", 8u), WA_CONF_OK);
    start(&w, &c);
    CHECK_STR_EQ(w.pass, "password");
    CHECK_EQ(strlen(w.code), WA_SECRET_LEN);
    /* Both, of the longest. */
    char b[80];
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_PASS, xs(b, 63u), 63u), WA_CONF_OK);
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_CODE, xs(b, 63u), 63u), WA_CONF_OK);
    start(&w, &c);
    CHECK_EQ(strlen(w.pass), 63u);
    CHECK_EQ(strlen(w.code), 63u);
    /* The session's copy goes with the session; the settings keep theirs. */
    wa_close(&w, WA_OFF_ASKED);
    CHECK_EQ(w.pass[0], '\0');
    CHECK_EQ(w.code[0], '\0');
    for (size_t i = 0u; i < sizeof(w.code); ++i) {
        CHECK_EQ(w.code[i], '\0');
    }
    CHECK_EQ(strlen(c.web_code), 63u);
    /* RANDOM after a value. */
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_CODE, NULL, 0u), WA_CONF_OK);
    start(&w, &c);
    CHECK_EQ(strlen(w.code), WA_SECRET_LEN);
    wa_kept_use(&w, NULL);
    CHECK_EQ(strlen(w.code), WA_SECRET_LEN);
}

TEST_CASE(a_code_changed_ends_the_old_one_at_the_next_start)
{
    wa_conf_t c;
    wa_conf_defaults(&c);
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_CODE, "old code", 8u), WA_CONF_OK);
    wa_t w;
    start(&w, &c);
    CHECK_EQ(wa_code_check(&w, "old code", true), WA_CODE_OK);
    /* A change while a session runs does not touch that session. */
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_CODE, "new code", 8u), WA_CONF_OK);
    CHECK_EQ(wa_code_check(&w, "old code", true), WA_CODE_OK);
    CHECK_EQ(wa_code_check(&w, "new code", true), WA_CODE_WRONG);
    wa_close(&w, WA_OFF_ASKED);
    CHECK_EQ(wa_code_check(&w, "old code", true), WA_CODE_CLOSED);
    start(&w, &c);
    CHECK_EQ(wa_code_check(&w, "old code", true), WA_CODE_WRONG);
    CHECK_EQ(wa_code_check(&w, "new code", true), WA_CODE_OK);
    /* A kept code stays right over a restart of the radio, until changed. */
    wa_close(&w, WA_OFF_ASKED);
    start(&w, &c);
    CHECK_EQ(wa_code_check(&w, "new code", true), WA_CODE_OK);
}

TEST_CASE(the_check_takes_kept_codes_of_8_and_63_and_nothing_near)
{
    char code[80];
    char guess[80];
    const size_t lens[] = { 8u, 63u };
    for (size_t k = 0u; k < 2u; ++k) {
        const size_t n = lens[k];
        wa_conf_t c;
        wa_conf_defaults(&c);
        CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_CODE, xs(code, n), n), WA_CONF_OK);
        wa_t w;
        start(&w, &c);
        CHECK_EQ(wa_code_check(&w, code, true), WA_CODE_OK);
        /* One short, one over, a difference at the first and the last. */
        CHECK_EQ(wa_code_check(&w, xs(guess, n - 1u), true), WA_CODE_WRONG);
        CHECK_EQ(wa_code_check(&w, xs(guess, n + 1u), true), WA_CODE_WRONG);
        xs(guess, n);
        guess[0] = 'y';
        CHECK_EQ(wa_code_check(&w, guess, true), WA_CODE_WRONG);
        xs(guess, n);
        guess[n - 1u] = 'y';
        CHECK_EQ(wa_code_check(&w, guess, true), WA_CODE_WRONG);
        CHECK_EQ(wa_code_check(&w, "", true), WA_CODE_MISSING);
        CHECK_EQ(wa_code_check(&w, NULL, true), WA_CODE_MISSING);
        CHECK_EQ(w.guesses, 4u);
    }
    /* A guess of more than the field's characters, terminated in its room:
     * the 64-character one above; a RANDOM code still takes its 8 alone. */
    wa_conf_t r;
    wa_conf_defaults(&r);
    wa_t w;
    start(&w, &r);
    char right[WA_SECRET_LEN + 2u];
    memcpy(right, w.code, WA_SECRET_LEN + 1u);
    CHECK_EQ(wa_code_check(&w, right, true), WA_CODE_OK);
    right[WA_SECRET_LEN] = 'x';
    right[WA_SECRET_LEN + 1u] = '\0';
    CHECK_EQ(wa_code_check(&w, right, true), WA_CODE_WRONG);
}

TEST_CASE(sixteen_wrong_codes_end_the_radio_with_a_kept_code)
{
    wa_conf_t c;
    wa_conf_defaults(&c);
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_CODE, "kept code", 9u), WA_CONF_OK);
    wa_t w;
    start(&w, &c);
    for (unsigned i = 0u; i + 1u < WA_GUESS_MAX; ++i) {
        CHECK_EQ(wa_code_check(&w, "wrong one", true), WA_CODE_WRONG);
        CHECK(wa_serving(&w));
    }
    CHECK_EQ(wa_code_check(&w, "wrong one", true), WA_CODE_WRONG);
    CHECK(!wa_serving(&w));
    CHECK_EQ(w.off, WA_OFF_GUESSES);
    CHECK_EQ(wa_code_check(&w, "kept code", true), WA_CODE_CLOSED);
    /* The next start counts from nothing, the same code right. */
    start(&w, &c);
    CHECK_EQ(w.guesses, 0u);
    CHECK_EQ(wa_code_check(&w, "kept code", true), WA_CODE_OK);
}

TEST_CASE(the_code_switch_off_still_passes_with_a_kept_code)
{
    wa_conf_t c;
    wa_conf_defaults(&c);
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_CODE, "kept code", 9u), WA_CONF_OK);
    wa_t w;
    start(&w, &c);
    CHECK_EQ(wa_code_check(&w, NULL, false), WA_CODE_OK);
    CHECK_EQ(wa_code_check(&w, "anything", false), WA_CODE_OK);
    CHECK_EQ(w.guesses, 0u);
}

TEST_CASE(a_kept_code_survives_the_cookie_and_the_form)
{
    /* Every printable character, the longest a code can be. */
    char code[WA_PASS_MAX + 1u];
    for (size_t off = 0u; off < 95u; off += 63u) {
        for (size_t i = 0u; i < WA_PASS_MAX; ++i) {
            code[i] = (char)(0x20u + (off + i) % 95u);
        }
        code[WA_PASS_MAX] = '\0';
        char value[WA_CODE_COOKIE_MAX + 1u];
        CHECK(wa_code_cookie(code, value, sizeof(value)));
        /* Nothing that ends or breaks a cookie is in its value. */
        CHECK(strpbrk(value, " ;,\"\\") == NULL);
        char hdr[400];
        snprintf(hdr, sizeof(hdr), "other=1; " WA_COOKIE "=%s; last=2", value);
        char back[WA_PASS_MAX + 2u];
        CHECK(wa_code_of_cookie(hdr, WA_COOKIE, back, sizeof(back)));
        CHECK_STR_EQ(back, code);
    }
    /* A RANDOM code is its own value: a cookie of an older build reads. */
    char v8[16];
    CHECK(wa_code_cookie("abcd2345", v8, sizeof(v8)));
    CHECK_STR_EQ(v8, "abcd2345");
    /* The worst case fits; one byte less room does not, and leaves nothing. */
    memset(code, ' ', WA_PASS_MAX);
    char full[WA_CODE_COOKIE_MAX + 1u];
    CHECK(wa_code_cookie(code, full, sizeof(full)));
    CHECK_EQ(strlen(full), (size_t)WA_CODE_COOKIE_MAX);
    CHECK(!wa_code_cookie(code, full, sizeof(full) - 1u));
    CHECK_STR_EQ(full, "");
    CHECK(!wa_code_cookie(NULL, full, sizeof(full)));
    CHECK(!wa_code_cookie("a", NULL, 8u));
    CHECK(!wa_code_cookie("a", full, 0u));
    /* What the check treats as no code: a broken form, no cookie, too long. */
    char b[8];
    CHECK(!wa_code_of_cookie("x=1", WA_COOKIE, b, sizeof(b)));
    CHECK(!wa_code_of_cookie(WA_COOKIE "=ab%2", WA_COOKIE, b, sizeof(b)));
    CHECK_STR_EQ(b, "");
    CHECK(!wa_code_of_cookie(WA_COOKIE "=ab%", WA_COOKIE, b, sizeof(b)));
    CHECK(!wa_code_of_cookie(WA_COOKIE "=ab%zz", WA_COOKIE, b, sizeof(b)));
    CHECK(!wa_code_of_cookie(WA_COOKIE "=12345678", WA_COOKIE, b, sizeof(b)));
    CHECK_STR_EQ(b, "");
    CHECK(wa_code_of_cookie(WA_COOKIE "=1234567", WA_COOKIE, b, sizeof(b)));
    CHECK(!wa_code_of_cookie(WA_COOKIE "=1", WA_COOKIE, NULL, 8u));
    CHECK(!wa_code_of_cookie(WA_COOKIE "=1", WA_COOKIE, b, 0u));
}

TEST_CASE(the_code_form_takes_the_longest_code_at_its_limit)
{
    /* 63 spaces, as a script sends them: "code=" and 189 bytes. */
    char body[WA_CODE_BODY_MAX + 1u];
    size_t n = (size_t)snprintf(body, sizeof(body), "code=");
    for (size_t i = 0u; i < WA_PASS_MAX; ++i) {
        n += (size_t)snprintf(body + n, sizeof(body) - n, "%%20");
    }
    CHECK(n <= WA_CODE_BODY_MAX);
    char given[WA_PASS_MAX + 2u];
    CHECK(wa_form_get(body, n, "code", given, sizeof(given)));
    CHECK_EQ(strlen(given), (size_t)WA_PASS_MAX);
    /* The guard's limit at its edge. */
    const wa_self_t me = { .name = "rcbench-1A2B",
                           .ip = { 192u, 168u, 4u, 1u } };
    wa_req_t q = { .host = "192.168.4.1", .origin = "http://192.168.4.1",
                   .body = n };
    CHECK_EQ(wa_guard(WA_REQ_CODE, &q, &me), WA_GUARD_OK);
    q.body = WA_CODE_BODY_MAX;
    CHECK_EQ(wa_guard(WA_REQ_CODE, &q, &me), WA_GUARD_OK);
    q.body = WA_CODE_BODY_MAX + 1u;
    CHECK_EQ(wa_guard(WA_REQ_CODE, &q, &me), WA_GUARD_BIG);
}

/** @brief What a page or an answer wrote. */
static char g_out[200000];
/** @brief Its bytes. */
static size_t g_n;

/**
 * @brief wa_out_t's sink into g_out.
 * @param ctx   unused
 * @param data  the bytes
 * @param n     how many
 * @return true
 */
static bool catch_out(void *ctx, const void *data, size_t n)
{
    (void)ctx;
    if (g_n + n < sizeof(g_out)) {
        memcpy(g_out + g_n, data, n);
        g_n += n;
        g_out[g_n] = '\0';
    }
    return true;
}

TEST_CASE(the_value_is_in_no_page_and_no_answer_of_the_file_api)
{
    /* The settings and the session hold the marker... */
    wa_conf_t c;
    wa_conf_defaults(&c);
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_PASS, MARK, strlen(MARK)), WA_CONF_OK);
    CHECK_EQ(wa_conf_set_kept(&c, WA_KEPT_CODE, MARK, strlen(MARK)), WA_CONF_OK);
    wa_t w;
    start(&w, &c);
    CHECK_STR_EQ(w.code, MARK);
    /* ...and what a browser can read of the panel does not: the two pages in
     * both languages and the status answer, with a card that lists files. */
    fake_fs_t fs;
    fake_init(&fs);
    fake_add(&fs, "LOG00001.CSV", false, 1234u, 1791961199);
    const wa_fs_t card = fake_wa(&fs);
    wa_out_t o;
    for (int de = 0; de < 2; ++de) {
        const wa_page_info_t pi = { .name = "rcbench-1A2B", .version = "0.0.0",
                                    .slot = 8126464u, .de = de != 0 };
        g_n = 0u;
        g_out[0] = '\0';
        wa_out_init(&o, catch_out, NULL);
        wa_page_main(&o, &pi, &card);
        CHECK(wa_out_flush(&o));
        CHECK(g_n > 1000u);
        CHECK(strstr(g_out, MARK) == NULL);
        g_n = 0u;
        wa_out_init(&o, catch_out, NULL);
        wa_page_code(&o, &pi, true);
        CHECK(wa_out_flush(&o));
        CHECK(g_n > 100u);
        CHECK(strstr(g_out, MARK) == NULL);
        /* The code's entry takes the longer value. */
        CHECK(strstr(g_out, "maxlength=\"63\"") != NULL);
        wa_api_status_t st;
        memset(&st, 0, sizeof(st));
        st.name = pi.name;
        st.version = pi.version;
        st.de = pi.de;
        g_n = 0u;
        wa_out_init(&o, catch_out, NULL);
        wa_api_status_json(&o, &st);
        CHECK(wa_out_flush(&o));
        CHECK(g_n > 10u);
        CHECK(strstr(g_out, MARK) == NULL);
    }
    fake_free(&fs);
}

int main(void)
{
    RUN(the_rule_holds_at_7_8_63_and_64_characters);
    RUN(a_character_outside_printable_ascii_is_refused);
    RUN(random_after_a_value_and_a_refusal_leaves_what_stands);
    RUN(a_store_with_no_key_loads_as_random_with_the_rest_unchanged);
    RUN(a_stored_value_the_rule_refuses_loads_as_random);
    RUN(values_survive_a_save_and_a_load);
    RUN(forget_keeps_both_values_through_the_store);
    RUN(forget_with_nothing_kept_owes_only_the_erase);
    RUN(a_value_typed_after_forget_is_saved_with_no_erase_before_it);
    RUN(a_start_takes_the_kept_values_and_random_stays_fresh);
    RUN(a_code_changed_ends_the_old_one_at_the_next_start);
    RUN(the_check_takes_kept_codes_of_8_and_63_and_nothing_near);
    RUN(sixteen_wrong_codes_end_the_radio_with_a_kept_code);
    RUN(the_code_switch_off_still_passes_with_a_kept_code);
    RUN(a_kept_code_survives_the_cookie_and_the_form);
    RUN(the_code_form_takes_the_longest_code_at_its_limit);
    RUN(the_value_is_in_no_page_and_no_answer_of_the_file_api);
    return test_summary("wa_kept");
}
