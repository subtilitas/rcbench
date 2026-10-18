/*
 * The newest release's tag out of GitHub's API answer, and which release a
 * panel is offered.
 *
 * The failure under test is a tag that is taken and should not be: one
 * deeper in the answer than the first release's own keys, one of a later
 * release, one past the scan's bound, one too long, one in an escaped or
 * non-string form, one that is not v + MAJOR.MINOR.PATCH, or one read out
 * of an answer whose JSON (JavaScript Object Notation) is out of form.  And
 * an offer of a release that is not newer than the running one: equal,
 * older, a usable one older than a running testing one, a running version
 * that did not parse.  Each bound is tested on both sides, and every answer
 * is fed whole, a byte at a time and in pieces.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "fw_release.h"

/* An answer as GitHub writes it, cut down: the release's keys before the
 * tag, the author object, the assets with their uploaders after it, and a
 * body that quotes a tag_name of its own. */
static const char k_answer[] =
    "[{\"url\":\"https://api.github.com/repos/subtilitas/rcbench/releases/1\","
    "\"html_url\":\"https://github.com/subtilitas/rcbench/releases/tag/v0.21.0\","
    "\"id\":414120794,"
    "\"author\":{\"login\":\"github-actions[bot]\",\"id\":41898282,"
    "\"tag_name\":\"v9.9.9\",\"site_admin\":false,\"type\":\"Bot\"},"
    "\"node_id\":\"RE_kwDOUEt2aM4YrxBa\","
    "\"tag_name\":\"v0.21.0\","
    "\"target_commitish\":\"main\",\"name\":\"v0.21.0\",\"draft\":false,"
    "\"prerelease\":true,\"created_at\":\"2026-10-18T20:00:00Z\","
    "\"assets\":[{\"name\":\"SHA256SUMS\",\"size\":512,"
    "\"uploader\":{\"login\":\"github-actions[bot]\"}}],"
    "\"body\":\"Says \\\"tag_name\\\": \\\"v8.8.8\\\" and } ] { [\"}]";

static fw_rel_err_t scan_pieces(const char *text, size_t n, size_t piece,
                                fw_version_t *v)
{
    fw_rel_scan_t s;
    fw_release_scan_init(&s);
    for (size_t at = 0; at < n; at += piece) {
        const size_t k = (n - at < piece) ? n - at : piece;
        if (fw_release_scan_feed(&s, (const uint8_t *)text + at, k)) {
            break;
        }
    }
    return fw_release_scan_end(&s, v);
}

/* The same answer whole, a byte at a time and in pieces of 7 bytes: one
 * result for all three. */
static fw_rel_err_t scan(const char *text, fw_version_t *v)
{
    const size_t n = strlen(text);
    fw_version_t a = { 1u, 1u, 1u }, b = a, c = a;
    const fw_rel_err_t whole = scan_pieces(text, n, n ? n : 1u, &a);
    const fw_rel_err_t bytes = scan_pieces(text, n, 1u, &b);
    const fw_rel_err_t seven = scan_pieces(text, n, 7u, &c);
    if (whole != bytes || whole != seven || memcmp(&a, &b, sizeof(a)) != 0
        || memcmp(&a, &c, sizeof(a)) != 0) {
        fprintf(stderr, "  pieces disagree: %d %d %d\n", (int)whole,
                (int)bytes, (int)seven);
        return FW_REL_COUNT;
    }
    if (v != NULL) {
        *v = a;
    }
    return whole;
}

/* A body string of @p pad bytes, then the tag: its closing quote is byte
 * PAD_FIXED + pad, counted from 1. */
#define PAD_HEAD  "[{\"body\":\""
#define PAD_TAIL  "\",\"tag_name\":\"v1.2.3\""
#define PAD_FIXED (sizeof(PAD_HEAD) - 1u + sizeof(PAD_TAIL) - 1u)

static char *padded(size_t pad)
{
    char *t = malloc(pad + 64u);
    if (t == NULL) {
        return NULL;
    }
    size_t k = (size_t)sprintf(t, PAD_HEAD);
    memset(t + k, 'x', pad);
    k += pad;
    sprintf(t + k, PAD_TAIL "}]");
    return t;
}

TEST_CASE(the_first_releases_tag_name_is_read)
{
    fw_version_t v = { 0u, 0u, 0u };
    CHECK_EQ(scan(k_answer, &v), FW_REL_OK);
    CHECK_EQ(v.major, 0u);
    CHECK_EQ(v.minor, 21u);
    CHECK_EQ(v.patch, 0u);
    /* Whitespace wherever JSON allows it. */
    CHECK_EQ(scan(" \r\n\t[ \n{ \"tag_name\" \t:\r\n \"v0.22.3\" } ]", &v),
             FW_REL_OK);
    CHECK_EQ(v.minor, 22u);
    CHECK_EQ(v.patch, 3u);
    /* The largest tag the panel takes, 18 bytes. */
    CHECK_EQ(scan("[{\"tag_name\":\"v65535.65535.65535\"}]", &v), FW_REL_OK);
    CHECK_EQ(v.major, 65535u);
    CHECK_EQ(v.minor, 65535u);
    CHECK_EQ(v.patch, 65535u);
    CHECK_EQ(scan("[{\"tag_name\":\"v0.0.0\"}]", &v), FW_REL_OK);
}

TEST_CASE(a_tag_name_deeper_or_as_a_value_is_not_the_releases)
{
    fw_version_t v = { 0u, 0u, 0u };
    /* In a nested object, in an array, in a string, as a value: passed. */
    CHECK_EQ(scan("[{\"a\":{\"tag_name\":\"v9.9.9\"},"
                  "\"b\":[\"tag_name\",{\"tag_name\":\"v8.8.8\"}],"
                  "\"name\":\"tag_name\",\"c\":\"\\\"tag_name\\\":\","
                  "\"tag_name\":\"v0.21.4\"}]", &v),
             FW_REL_OK);
    CHECK_EQ(v.minor, 21u);
    CHECK_EQ(v.patch, 4u);
    /* Escapes in strings at every level, a quote among them. */
    CHECK_EQ(scan("[{\"a\":[\"x\\\"]\\\\\"],\"b\":\"\\\"}\","
                  "\"tag_name\":\"v0.21.5\"}]", &v),
             FW_REL_OK);
    CHECK_EQ(v.patch, 5u);
    /* Keys that only start or end like it, and one with an escape. */
    CHECK_EQ(scan("[{\"tag_nam\":\"v1.0.0\",\"tag_names\":\"v2.0.0\","
                  "\"xtag_name\":\"v3.0.0\",\"tag\\u005fname\":\"v4.0.0\","
                  "\"tag_name\":\"v5.0.0\"}]", &v),
             FW_REL_OK);
    CHECK_EQ(v.major, 5u);
    /* Scalars before it, ended by a comma, a space or the brace. */
    CHECK_EQ(scan("[{\"id\":-1.5e+3,\"d\":false ,\"p\":null,\"n\":true,"
                  "\"tag_name\":\"v0.1.2\"}]", &v),
             FW_REL_OK);
    CHECK_EQ(v.patch, 2u);
}

TEST_CASE(only_the_first_release_counts)
{
    CHECK_EQ(scan("[{\"name\":\"x\"},{\"tag_name\":\"v1.0.0\"}]", NULL),
             FW_REL_TAG_NONE);
    CHECK_EQ(scan("[{\"n\":1},{\"tag_name\":\"v1.0.0\"}]", NULL),
             FW_REL_TAG_NONE);
    CHECK_EQ(scan("[{\"a\":{\"b\":[1,2]}},{\"tag_name\":\"v1.0.0\"}]", NULL),
             FW_REL_TAG_NONE);
    CHECK_EQ(scan("[{}]", NULL), FW_REL_TAG_NONE);
    /* No release at all. */
    CHECK_EQ(scan("[]", NULL), FW_REL_TAG_NONE);
    CHECK_EQ(scan(" [ ] ", NULL), FW_REL_TAG_NONE);
}

TEST_CASE(an_answer_that_ends_before_the_tag_has_none)
{
    CHECK_EQ(scan("", NULL), FW_REL_TAG_NONE);
    CHECK_EQ(scan("[", NULL), FW_REL_TAG_NONE);
    CHECK_EQ(scan("[{\"tag_name\":\"v0.21", NULL), FW_REL_TAG_NONE);
    CHECK_EQ(scan("[{\"tag_name\":", NULL), FW_REL_TAG_NONE);
    CHECK_EQ(scan("[{\"author\":{\"login\":\"x\"", NULL), FW_REL_TAG_NONE);
    /* Nothing fed, or nothing to feed. */
    fw_rel_scan_t s;
    fw_release_scan_init(&s);
    CHECK(!fw_release_scan_feed(&s, NULL, 5u));
    CHECK(!fw_release_scan_feed(&s, (const uint8_t *)"[", 0u));
    CHECK_EQ(s.seen, 0u);
    CHECK_EQ(fw_release_scan_end(&s, NULL), FW_REL_TAG_NONE);
    CHECK(fw_release_scan_feed(NULL, (const uint8_t *)"[", 1u));
    CHECK_EQ(fw_release_scan_end(NULL, NULL), FW_REL_TAG_NONE);
}

TEST_CASE(a_tag_name_over_its_bound_is_too_long)
{
    /* 18 bytes are the bound; 19 are over it, however they read. */
    CHECK_EQ(scan("[{\"tag_name\":\"v65535.65535.655350\"}]", NULL),
             FW_REL_TAG_LONG);
    CHECK_EQ(scan("[{\"tag_name\":\"v0.21.0-testing-build\"}]", NULL),
             FW_REL_TAG_LONG);
    char t[400];
    size_t k = (size_t)sprintf(t, "[{\"tag_name\":\"v");
    memset(t + k, '1', 300u);
    sprintf(t + k + 300u, "\"}]");
    CHECK_EQ(scan(t, NULL), FW_REL_TAG_LONG);
    /* The answer refused there: its bytes after it are not looked at. */
    fw_rel_scan_t s;
    fw_release_scan_init(&s);
    CHECK(fw_release_scan_feed(&s, (const uint8_t *)t, strlen(t)));
    /* 14 bytes to the opening quote, 18 of the tag kept, the 19th. */
    CHECK_EQ(s.seen, (uint32_t)(14u + FW_REL_TAG_MAX + 1u));
}

TEST_CASE(a_tag_name_that_is_no_plain_string_is_out_of_form)
{
    CHECK_EQ(scan("[{\"tag_name\":21}]", NULL), FW_REL_TAG_FORM);
    CHECK_EQ(scan("[{\"tag_name\":null}]", NULL), FW_REL_TAG_FORM);
    CHECK_EQ(scan("[{\"tag_name\":{\"v\":1}}]", NULL), FW_REL_TAG_FORM);
    CHECK_EQ(scan("[{\"tag_name\":[\"v0.21.0\"]}]", NULL), FW_REL_TAG_FORM);
    /* An escape, though it would spell a version. */
    CHECK_EQ(scan("[{\"tag_name\":\"v0.\\u00321.0\"}]", NULL),
             FW_REL_TAG_FORM);
    CHECK_EQ(scan("[{\"tag_name\":\"v0.21.0\\\"\"}]", NULL),
             FW_REL_TAG_FORM);
    /* A control byte in it. */
    CHECK_EQ(scan("[{\"tag_name\":\"v0.21\n.0\"}]", NULL), FW_REL_TAG_FORM);
    CHECK_EQ(scan("[{\"tag_name\":\"\tv0.21.0\"}]", NULL), FW_REL_TAG_FORM);
}

TEST_CASE(json_around_the_tag_out_of_form_is_refused)
{
    /* Not an array of objects: GitHub's error answer is an object. */
    CHECK_EQ(scan("{\"message\":\"Not Found\"}", NULL), FW_REL_TAG_FORM);
    CHECK_EQ(scan("[\"v0.21.0\"]", NULL), FW_REL_TAG_FORM);
    CHECK_EQ(scan("[[{\"tag_name\":\"v0.21.0\"}]]", NULL), FW_REL_TAG_FORM);
    CHECK_EQ(scan("\xEF\xBB\xBF[{\"tag_name\":\"v0.21.0\"}]", NULL),
             FW_REL_TAG_FORM);
    /* Keys and separators. */
    CHECK_EQ(scan("[{tag_name:\"v0.21.0\"}]", NULL), FW_REL_TAG_FORM);
    CHECK_EQ(scan("[{\"tag_name\" \"v0.21.0\"}]", NULL), FW_REL_TAG_FORM);
    CHECK_EQ(scan("[{\"a\":1,}]", NULL), FW_REL_TAG_FORM);
    CHECK_EQ(scan("[{\"a\":1 \"tag_name\":\"v0.21.0\"}]", NULL),
             FW_REL_TAG_FORM);
    CHECK_EQ(scan("[{\"a\":,\"tag_name\":\"v0.21.0\"}]", NULL),
             FW_REL_TAG_FORM);
    CHECK_EQ(scan("[{\"a\":1]", NULL), FW_REL_TAG_FORM);
    /* A control byte in a key, a value or a nested string. */
    CHECK_EQ(scan("[{\"a\nb\":1,\"tag_name\":\"v0.21.0\"}]", NULL),
             FW_REL_TAG_FORM);
    CHECK_EQ(scan("[{\"a\":\"x\ny\",\"tag_name\":\"v0.21.0\"}]", NULL),
             FW_REL_TAG_FORM);
    CHECK_EQ(scan("[{\"a\":[\"x\ny\"],\"tag_name\":\"v0.21.0\"}]", NULL),
             FW_REL_TAG_FORM);
    CHECK_EQ(scan("[{\"a\\\n\":1}]", NULL), FW_REL_TAG_FORM);
    CHECK_EQ(scan("[{\"a\":\"\\\n\"}]", NULL), FW_REL_TAG_FORM);
    CHECK_EQ(scan("[{\"a\":[\"\\\n\"]}]", NULL), FW_REL_TAG_FORM);
    /* A bracket closed by the other kind, deeper down. */
    CHECK_EQ(scan("[{\"a\":[1},\"tag_name\":\"v0.21.0\"}]", NULL),
             FW_REL_TAG_FORM);
    CHECK_EQ(scan("[{\"a\":{\"b\":1],\"tag_name\":\"v0.21.0\"}]", NULL),
             FW_REL_TAG_FORM);
}

TEST_CASE(nesting_is_followed_to_its_bound)
{
    /* The array and the object are levels 1 and 2: a value nested to level
     * FW_REL_SCAN_DEPTH passes, one level more is refused. */
    char t[256];
    for (unsigned extra = FW_REL_SCAN_DEPTH - 2u;
         extra <= FW_REL_SCAN_DEPTH - 1u; ++extra) {
        size_t k = (size_t)sprintf(t, "[{\"a\":");
        for (unsigned i = 0; i < extra; ++i) {
            t[k++] = '[';
        }
        for (unsigned i = 0; i < extra; ++i) {
            t[k++] = ']';
        }
        sprintf(t + k, ",\"tag_name\":\"v0.21.0\"}]");
        CHECK_EQ(scan(t, NULL), (extra == FW_REL_SCAN_DEPTH - 2u)
                                    ? FW_REL_OK : FW_REL_TAG_FORM);
    }
}

TEST_CASE(a_tag_that_is_not_a_version_is_refused)
{
    static const char *const bad[] = {
        "", "v", "0.21.0", "V0.21.0", "v0.21", "v0.21.", "v.21.0",
        "v0.21.0.1", "v01.21.0", "v0.021.0", "v0.21.00", "v65536.0.0",
        "v0.21.0-rc1", "v0.21.0 ", " v0.21.0", "vx.y.z", "v0..0",
        "v\xC3\xA9.1.1",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        char t[64];
        snprintf(t, sizeof(t), "[{\"tag_name\":\"%s\"}]", bad[i]);
        const fw_rel_err_t e = scan(t, NULL);
        if (e != FW_REL_VERSION) {
            fprintf(stderr, "  \"%s\": %d\n", bad[i], (int)e);
        }
        CHECK_EQ(e, FW_REL_VERSION);
    }
}

TEST_CASE(the_scan_stops_at_its_bound)
{
    /* The tag's closing quote at byte FW_REL_SCAN_MAX, the last byte the
     * scan reads; then one byte later. */
    const size_t at_bound = FW_REL_SCAN_MAX - PAD_FIXED;
    char *t = padded(at_bound);
    CHECK(t != NULL);
    fw_version_t v = { 0u, 0u, 0u };
    CHECK_EQ(scan(t, &v), FW_REL_OK);
    CHECK_EQ(v.major, 1u);
    CHECK_EQ(v.patch, 3u);
    free(t);
    t = padded(at_bound + 1u);
    CHECK(t != NULL);
    CHECK_EQ(scan(t, NULL), FW_REL_SCAN_LONG);
    /* Fed past it: the scan read FW_REL_SCAN_MAX bytes and no more. */
    fw_rel_scan_t s;
    fw_release_scan_init(&s);
    CHECK(fw_release_scan_feed(&s, (const uint8_t *)t, strlen(t)));
    CHECK_EQ(s.seen, FW_REL_SCAN_MAX);
    CHECK(fw_release_scan_feed(&s, (const uint8_t *)"]", 1u));
    CHECK_EQ(s.seen, FW_REL_SCAN_MAX);
    CHECK_EQ(fw_release_scan_end(&s, NULL), FW_REL_SCAN_LONG);
    free(t);
}

TEST_CASE(the_scan_keeps_little)
{
    /* What the runner holds on its stack for the scan. */
    fprintf(stderr, "  fw_rel_scan_t: %zu bytes\n", sizeof(fw_rel_scan_t));
    CHECK(sizeof(fw_rel_scan_t) <= 64u);
    CHECK_STR_EQ(fw_rel_err_str(FW_REL_TAG_NONE), "no tag_name");
    CHECK_STR_EQ(fw_rel_err_str(FW_REL_TAG_LONG), "tag_name too long");
    CHECK_STR_EQ(fw_rel_err_str(FW_REL_TAG_FORM), "JSON out of form");
    CHECK_STR_EQ(fw_rel_err_str(FW_REL_SCAN_LONG),
                 "no tag_name within the scan");
    CHECK_STR_EQ(FW_REL_API_URL, "https://api.github.com/repos/subtilitas/"
                                 "rcbench/releases?per_page=1");
}

/* ------------------------------------------------------------ the offer */

static bool offered(unsigned fm, unsigned fn, unsigned fp, unsigned rm,
                    unsigned rn, unsigned rp)
{
    const fw_version_t f = { (uint16_t)fm, (uint16_t)fn, (uint16_t)fp };
    const fw_version_t r = { (uint16_t)rm, (uint16_t)rn, (uint16_t)rp };
    return fw_release_offered(f, r, true);
}

TEST_CASE(only_a_newer_release_is_offered)
{
    /* Equal, older, newer. */
    CHECK(!offered(0, 20, 0, 0, 20, 0));
    CHECK(!offered(0, 19, 9, 0, 20, 0));
    CHECK(offered(0, 20, 1, 0, 20, 0));
    CHECK(offered(0, 21, 0, 0, 20, 5));
    CHECK(offered(1, 0, 0, 0, 65535, 65535));
    CHECK(!offered(0, 65535, 65535, 1, 0, 0));
    /* Each part a number, not text: 0.10 is after 0.9. */
    CHECK(offered(0, 10, 0, 0, 9, 0));
    CHECK(!offered(0, 9, 0, 0, 10, 0));
    CHECK(offered(0, 9, 10, 0, 9, 9));
    CHECK(offered(10, 0, 0, 9, 0, 0));
    /* Testing (odd minor) and usable (even minor) mixed: a panel on a
     * testing release is offered no older usable one, and a newer of
     * either kind. */
    CHECK(!offered(0, 20, 0, 0, 21, 0));
    CHECK(!offered(0, 20, 3, 0, 21, 0));
    CHECK(offered(0, 22, 0, 0, 21, 0));
    CHECK(offered(0, 21, 1, 0, 21, 0));
    CHECK(offered(0, 21, 0, 0, 20, 0));
    CHECK(!offered(0, 21, 0, 0, 22, 0));
    CHECK(!offered(0, 21, 0, 0, 21, 0));
}

TEST_CASE(a_running_version_that_did_not_parse_is_offered_nothing)
{
    const fw_version_t zero = { 0u, 0u, 0u };
    const fw_version_t big = { 65535u, 65535u, 65535u };
    const fw_version_t v = { 0u, 21u, 0u };
    CHECK(!fw_release_offered(v, zero, false));
    CHECK(!fw_release_offered(big, zero, false));
    CHECK(!fw_release_offered(big, v, false));
    /* What fw_ota_running() leaves it as: 0.0.0, which every release is
     * newer than once it is believed. */
    CHECK(fw_release_offered(v, zero, true));
}

int main(void)
{
    RUN(the_first_releases_tag_name_is_read);
    RUN(a_tag_name_deeper_or_as_a_value_is_not_the_releases);
    RUN(only_the_first_release_counts);
    RUN(an_answer_that_ends_before_the_tag_has_none);
    RUN(a_tag_name_over_its_bound_is_too_long);
    RUN(a_tag_name_that_is_no_plain_string_is_out_of_form);
    RUN(json_around_the_tag_out_of_form_is_refused);
    RUN(nesting_is_followed_to_its_bound);
    RUN(a_tag_that_is_not_a_version_is_refused);
    RUN(the_scan_stops_at_its_bound);
    RUN(the_scan_keeps_little);
    RUN(only_a_newer_release_is_offered);
    RUN(a_running_version_that_did_not_parse_is_offered_nothing);
    return test_summary("fw_release_scan");
}
