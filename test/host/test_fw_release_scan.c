/*
 * The highest release's tag out of GitHub's API answer of up to 10
 * releases, and which release a panel is offered.
 *
 * The failure under test is a tag that is taken and should not be: one
 * deeper in the answer than a release's own keys, a draft's, one of an
 * 11th release, the first by date instead of the highest, one past the
 * scan's bound, one too long, one in an escaped or non-string form, one
 * that is not v + MAJOR.MINOR.PATCH, or one read out of an answer whose
 * JSON (JavaScript Object Notation) is out of form, a single release of
 * it included.  And
 * an offer of a release that is not newer than the running one (equal,
 * older, a running version that did not parse) or past the panel's
 * release line, 0.22 (0.23.0, 0.24.0, 1.0.0), and a patch release of the
 * line lost behind a higher release for another panel.  Each bound is
 * tested on both sides, and every answer is fed whole, a byte at a time
 * and in pieces.
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

/* A body string of @p pad bytes, then the tag: the answer's last byte,
 * the array's ']', is byte PAD_FIXED + pad, counted from 1. */
#define PAD_HEAD  "[{\"body\":\""
#define PAD_TAIL  "\",\"tag_name\":\"v1.2.3\"}]"
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
    sprintf(t + k, PAD_TAIL);
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

TEST_CASE(a_release_without_a_tag_refuses_the_answer)
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
    /* A release whole, but not the array, and not 10 releases. */
    CHECK_EQ(scan("[{\"tag_name\":\"v0.21.0\"}", NULL), FW_REL_TAG_NONE);
    CHECK_EQ(scan("[{\"tag_name\":\"v0.21.0\"},", NULL), FW_REL_TAG_NONE);
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
    /* The answer's last byte at byte FW_REL_SCAN_MAX, the last byte the
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
    CHECK(sizeof(fw_rel_scan_t) <= 84u);
    CHECK_STR_EQ(fw_rel_err_str(FW_REL_TAG_NONE), "no tag_name");
    CHECK_STR_EQ(fw_rel_err_str(FW_REL_TAG_LONG), "tag_name too long");
    CHECK_STR_EQ(fw_rel_err_str(FW_REL_TAG_FORM), "JSON out of form");
    CHECK_STR_EQ(fw_rel_err_str(FW_REL_SCAN_LONG),
                 "no tag_name within the scan");
    CHECK_STR_EQ(FW_REL_API_URL, "https://api.github.com/repos/subtilitas/"
                                 "rcbench/releases?per_page=10");
}

/* An answer of @p n releases tagged @p tags, each with @p extra before its
 * close, as GitHub writes them: newest by date first. */
static char *releases(const char *const *tags, size_t n, const char *extra)
{
    char *t = malloc(n * 200u + 8u);
    if (t == NULL) {
        return NULL;
    }
    size_t k = (size_t)sprintf(t, "[");
    for (size_t i = 0; i < n; ++i) {
        k += (size_t)sprintf(t + k,
                             "%s{\"id\":%zu,\"tag_name\":\"%s\","
                             "\"assets\":[{\"name\":\"SHA256SUMS\"}]%s}",
                             (i == 0u) ? "" : ",", i, tags[i], extra);
    }
    sprintf(t + k, "]");
    return t;
}

static fw_rel_err_t scan_tags(const char *const *tags, size_t n,
                              const char *extra, fw_version_t *v)
{
    char *t = releases(tags, n, extra);
    if (t == NULL) {
        return FW_REL_COUNT;
    }
    const fw_rel_err_t e = scan(t, v);
    free(t);
    return e;
}

TEST_CASE(the_highest_of_the_releases_is_taken_not_the_newest_by_date)
{
    /* 0.22.1 published after 0.23.0 and 0.23.1. */
    static const char *const late[] = { "v0.22.1", "v0.23.1", "v0.23.0",
                                        "v0.22.0", "v0.21.0" };
    fw_version_t v = { 0u, 0u, 0u };
    CHECK_EQ(scan_tags(late, 5u, "", &v), FW_REL_OK);
    CHECK_EQ(v.minor, 23u);
    CHECK_EQ(v.patch, 1u);
    /* Each part as a number: 0.10 over 0.9, 1.0.0 over 0.65535.65535. */
    static const char *const nums[] = { "v0.9.9", "v0.10.0", "v0.9.10" };
    CHECK_EQ(scan_tags(nums, 3u, "", &v), FW_REL_OK);
    CHECK_EQ(v.minor, 10u);
    static const char *const big[] = { "v0.65535.65535", "v1.0.0" };
    CHECK_EQ(scan_tags(big, 2u, "", &v), FW_REL_OK);
    CHECK_EQ(v.major, 1u);
    /* All 10 the same. */
    static const char *const same[] = {
        "v0.21.0", "v0.21.0", "v0.21.0", "v0.21.0", "v0.21.0",
        "v0.21.0", "v0.21.0", "v0.21.0", "v0.21.0", "v0.21.0",
    };
    CHECK_EQ(scan_tags(same, 10u, "", &v), FW_REL_OK);
    CHECK_EQ(v.minor, 21u);
    CHECK_EQ(v.patch, 0u);
    /* One release alone. */
    CHECK_EQ(scan_tags(late, 1u, "", &v), FW_REL_OK);
    CHECK_EQ(v.minor, 22u);
}

TEST_CASE(ten_releases_are_read_and_an_eleventh_is_not)
{
    static const char *const tags[] = {
        "v0.1.0", "v0.2.0", "v0.3.0", "v0.4.0", "v0.5.0",
        "v0.6.0", "v0.7.0", "v0.8.0", "v0.9.0", "v0.10.0", "v9.0.0",
    };
    fw_version_t v = { 0u, 0u, 0u };
    /* The highest is the 10th: taken. */
    CHECK_EQ(scan_tags(tags, 10u, "", &v), FW_REL_OK);
    CHECK_EQ(v.minor, 10u);
    /* An 11th, higher still, is not looked at, nor anything after the
     * 10th release's close. */
    CHECK_EQ(scan_tags(tags, 11u, "", &v), FW_REL_OK);
    CHECK_EQ(v.major, 0u);
    CHECK_EQ(v.minor, 10u);
    char *t = releases(tags, 11u, "");
    REQUIRE_ALLOC(t);
    fw_rel_scan_t s;
    fw_release_scan_init(&s);
    CHECK(fw_release_scan_feed(&s, (const uint8_t *)t, strlen(t)));
    CHECK_EQ(s.releases, 10u);
    CHECK(t[s.seen - 1u] == '}');
    CHECK(t[s.seen] == ',');
    free(t);
    /* An 11th out of form is not looked at either. */
    static const char *const bad11[] = {
        "v0.1.0", "v0.1.0", "v0.1.0", "v0.1.0", "v0.1.0",
        "v0.1.0", "v0.1.0", "v0.1.0", "v0.1.0", "v0.1.0", "junk",
    };
    CHECK_EQ(scan_tags(bad11, 11u, "", &v), FW_REL_OK);
    CHECK_EQ(scan_tags(bad11, 10u, "", &v), FW_REL_OK);
}

TEST_CASE(one_release_out_of_form_refuses_the_whole_answer)
{
    fw_version_t v = { 0u, 0u, 0u };
    static const char *const bad[] = { "v0.21.0", "v0.23.0", "0.24.0",
                                       "v0.22.0" };
    CHECK_EQ(scan_tags(bad, 4u, "", &v), FW_REL_VERSION);
    static const char *const at_end[] = { "v0.21.0", "v0.22.0",
                                          "v0.23.0-rc1" };
    CHECK_EQ(scan_tags(at_end, 3u, "", &v), FW_REL_VERSION);
    static const char *const first[] = { "V0.25.0", "v0.22.0" };
    CHECK_EQ(scan_tags(first, 2u, "", &v), FW_REL_VERSION);
    static const char *const longer[] = { "v0.22.0",
                                          "v65535.65535.655350" };
    CHECK_EQ(scan_tags(longer, 2u, "", &v), FW_REL_TAG_LONG);
    /* A release without its tag, one with two, between good ones. */
    CHECK_EQ(scan("[{\"tag_name\":\"v0.22.0\"},{\"id\":2},"
                  "{\"tag_name\":\"v0.23.0\"}]", NULL),
             FW_REL_TAG_NONE);
    CHECK_EQ(scan("[{\"tag_name\":\"v0.22.0\","
                  "\"tag_name\":\"v0.23.0\"}]", NULL),
             FW_REL_TAG_FORM);
    /* The array between releases: a comma each, no trailing one. */
    CHECK_EQ(scan("[{\"tag_name\":\"v0.22.0\"} {\"tag_name\":\"v0.23.0\"}]",
                  NULL),
             FW_REL_TAG_FORM);
    CHECK_EQ(scan("[{\"tag_name\":\"v0.22.0\"},]", NULL), FW_REL_TAG_FORM);
    CHECK_EQ(scan("[{\"tag_name\":\"v0.22.0\"},,{\"tag_name\":\"v0.2.0\"}]",
                  NULL),
             FW_REL_TAG_FORM);
    CHECK_EQ(scan("[{\"tag_name\":\"v0.22.0\"},\"x\"]", NULL),
             FW_REL_TAG_FORM);
}

TEST_CASE(a_draft_is_passed_over_wherever_its_key_stands)
{
    fw_version_t v = { 0u, 0u, 0u };
    /* The highest is a draft: the next one is taken. */
    CHECK_EQ(scan("[{\"draft\":true,\"tag_name\":\"v0.24.0\"},"
                  "{\"tag_name\":\"v0.23.0\",\"draft\":false},"
                  "{\"tag_name\":\"v0.25.0\",\"draft\" : true }]", &v),
             FW_REL_OK);
    CHECK_EQ(v.minor, 23u);
    /* Only drafts: none. */
    CHECK_EQ(scan("[{\"tag_name\":\"v0.24.0\",\"draft\":true}]", NULL),
             FW_REL_TAG_NONE);
    /* A draft's tag is held to the form too. */
    CHECK_EQ(scan("[{\"draft\":true,\"tag_name\":\"draft-1\"},"
                  "{\"tag_name\":\"v0.23.0\"}]", NULL),
             FW_REL_VERSION);
    /* draft as anything but true or false, or twice. */
    static const char *const odd[] = {
        "[{\"draft\":\"true\",\"tag_name\":\"v1.0.0\"}]",
        "[{\"draft\":1,\"tag_name\":\"v1.0.0\"}]",
        "[{\"draft\":null,\"tag_name\":\"v1.0.0\"}]",
        "[{\"draft\":truex,\"tag_name\":\"v1.0.0\"}]",
        "[{\"draft\":falsehood,\"tag_name\":\"v1.0.0\"}]",
        "[{\"draft\":tru,\"tag_name\":\"v1.0.0\"}]",
        "[{\"draft\":{},\"tag_name\":\"v1.0.0\"}]",
        "[{\"draft\":false,\"draft\":false,\"tag_name\":\"v1.0.0\"}]",
    };
    for (size_t i = 0; i < sizeof(odd) / sizeof(odd[0]); ++i) {
        CHECK_EQ(scan(odd[i], NULL), FW_REL_TAG_FORM);
    }
    /* Keys like it are not it, and a nested one is passed over. */
    CHECK_EQ(scan("[{\"drafts\":true,\"draf\":true,\"a\":{\"draft\":true},"
                  "\"tag_name\":\"v0.3.0\"}]", &v),
             FW_REL_OK);
    CHECK_EQ(v.minor, 3u);
}

/* ------------------------------------------------------------ the offer */

static bool offered(unsigned fm, unsigned fn, unsigned fp, unsigned rm,
                    unsigned rn, unsigned rp)
{
    const fw_version_t f = { (uint16_t)fm, (uint16_t)fn, (uint16_t)fp };
    const fw_version_t r = { (uint16_t)rm, (uint16_t)rn, (uint16_t)rp };
    return fw_release_offered(f, r, true);
}

TEST_CASE(only_a_newer_release_of_the_line_is_offered)
{
    /* The line: 0.22, the last release for the ESP32-S3 panel. */
    CHECK_EQ(FW_REL_LINE_MAJOR, 0u);
    CHECK_EQ(FW_REL_LINE_MINOR, 22u);
    /* A patch release of the line: newer offered, equal and older not. */
    CHECK(offered(0, 22, 1, 0, 22, 0));
    CHECK(!offered(0, 22, 0, 0, 22, 1));
    CHECK(!offered(0, 22, 0, 0, 22, 0));
    CHECK(!offered(0, 22, 1, 0, 22, 1));
    CHECK(offered(0, 22, 65535, 0, 22, 65534));
    CHECK(!offered(0, 22, 65535, 0, 22, 65535));
    /* Each part a number, not text: 0.22.10 is after 0.22.9. */
    CHECK(offered(0, 22, 10, 0, 22, 9));
    CHECK(!offered(0, 22, 9, 0, 22, 10));
    /* Past the line, however much newer: never, testing or stable. */
    CHECK(!offered(0, 23, 0, 0, 22, 0));
    CHECK(!offered(0, 23, 0, 0, 22, 65535));
    CHECK(!offered(0, 24, 0, 0, 22, 0));
    CHECK(!offered(1, 0, 0, 0, 22, 0));
    CHECK(!offered(1, 22, 0, 0, 22, 0));
    CHECK(!offered(1, 22, 1, 0, 22, 0));
    CHECK(!offered(65535, 65535, 65535, 0, 22, 0));
    /* Before the line: older, and not offered either. */
    CHECK(!offered(0, 21, 65535, 0, 22, 0));
    CHECK(!offered(0, 20, 0, 0, 22, 0));
    /* A build before the line is offered the line and nothing else. */
    CHECK(offered(0, 22, 0, 0, 21, 2));
    CHECK(!offered(0, 21, 3, 0, 21, 2));
    CHECK(!offered(0, 23, 0, 0, 21, 2));
    /* The line is MAJOR and MINOR, any PATCH. */
    CHECK(fw_release_in_line((fw_version_t){ 0u, 22u, 0u }));
    CHECK(fw_release_in_line((fw_version_t){ 0u, 22u, 65535u }));
    CHECK(!fw_release_in_line((fw_version_t){ 0u, 21u, 65535u }));
    CHECK(!fw_release_in_line((fw_version_t){ 0u, 23u, 0u }));
    CHECK(!fw_release_in_line((fw_version_t){ 1u, 22u, 0u }));
}

/* The ESP32-P4 panel's line, as the panel starts: not capped, every
 * release newer than the running build offered, 0.23.0 and later
 * included; equal and older still not.  Run before the cap is set. */
TEST_CASE(the_p4_panels_line_offers_every_newer_release)
{
    CHECK_EQ(FW_REL_LINE_CAPPED, 0);
    CHECK(!fw_release_line_capped());
    CHECK(offered(0, 23, 0, 0, 22, 0));
    CHECK(offered(0, 24, 0, 0, 23, 0));
    CHECK(offered(1, 0, 0, 0, 24, 0));
    CHECK(offered(0, 22, 1, 0, 22, 0));
    CHECK(offered(65535, 65535, 65535, 65535, 65535, 65534));
    CHECK(!offered(0, 23, 0, 0, 23, 0));
    CHECK(!offered(0, 22, 1, 0, 23, 0));
    CHECK(!offered(0, 24, 0, 0, 24, 1));
    CHECK(fw_release_in_line((fw_version_t){ 0u, 23u, 0u }));
    CHECK(fw_release_in_line((fw_version_t){ 1u, 22u, 0u }));
    CHECK(fw_release_in_line((fw_version_t){ 0u, 21u, 65535u }));
    /* The cap goes on and off again. */
    fw_release_set_line_capped(true);
    CHECK(fw_release_line_capped());
    CHECK(!offered(0, 23, 0, 0, 22, 0));
    fw_release_set_line_capped(false);
    CHECK(!fw_release_line_capped());
    CHECK(offered(0, 23, 0, 0, 22, 0));
}

TEST_CASE(a_running_version_that_did_not_parse_is_offered_nothing)
{
    const fw_version_t zero = { 0u, 0u, 0u };
    const fw_version_t big = { 65535u, 65535u, 65535u };
    const fw_version_t v = { 0u, 22u, 1u };
    CHECK(!fw_release_offered(v, zero, false));
    CHECK(!fw_release_offered(big, zero, false));
    CHECK(!fw_release_offered(v, big, false));
    /* What fw_ota_running() leaves it as: 0.0.0, which every release of
     * the line is newer than once it is believed; one past it is not
     * offered even then. */
    CHECK(fw_release_offered(v, zero, true));
    CHECK(!fw_release_offered(big, zero, true));
}

/* The highest release and the line's highest of @p text, fed in pieces of
 * @p piece bytes: the line's result, and the answer's in @p e. */
static bool scan_line(const char *text, size_t piece, fw_rel_err_t *e,
                      fw_version_t *v, fw_version_t *line)
{
    fw_rel_scan_t s;
    fw_release_scan_init(&s);
    const size_t n = strlen(text);
    for (size_t at = 0; at < n; at += piece) {
        const size_t k = (n - at < piece) ? n - at : piece;
        if (fw_release_scan_feed(&s, (const uint8_t *)text + at, k)) {
            break;
        }
    }
    *e = fw_release_scan_end(&s, v);
    return fw_release_scan_line(&s, line);
}

TEST_CASE(the_lines_highest_release_is_kept_beside_the_highest)
{
    fw_rel_err_t e;
    fw_version_t v = { 0u, 0u, 0u }, l = { 9u, 9u, 9u };
    /* 0.22.1 published before 0.23.0 and 0.24.0: the highest is 0.24.0,
     * the line's 0.22.1, whatever the order and the pieces. */
    static const char *const both[] = { "v0.24.0", "v0.22.0", "v0.23.0",
                                        "v0.22.1", "v0.21.2" };
    char *t = releases(both, 5u, "");
    REQUIRE_ALLOC(t);
    static const size_t pieces[] = { 1u, 7u, 4096u };
    for (size_t i = 0; i < sizeof(pieces) / sizeof(pieces[0]); ++i) {
        CHECK(scan_line(t, pieces[i], &e, &v, &l));
        CHECK_EQ(e, FW_REL_OK);
        CHECK_EQ(v.minor, 24u);
        CHECK_EQ(l.major, 0u);
        CHECK_EQ(l.minor, 22u);
        CHECK_EQ(l.patch, 1u);
    }
    free(t);
    /* No release of the line among them: none, and the line untouched. */
    static const char *const none[] = { "v0.23.0", "v1.22.0", "v0.21.2",
                                        "v0.24.0" };
    t = releases(none, 4u, "");
    REQUIRE_ALLOC(t);
    l = (fw_version_t){ 9u, 9u, 9u };
    CHECK(!scan_line(t, 4096u, &e, &v, &l));
    CHECK_EQ(e, FW_REL_OK);
    CHECK_EQ(v.major, 1u);          /* 1.22.0: MINOR 22, another MAJOR */
    CHECK_EQ(l.major, 9u);
    free(t);
    /* A draft of the line is passed over; the one that is no draft is
     * kept. */
    CHECK(scan_line("[{\"tag_name\":\"v0.22.5\",\"draft\":true},"
                    "{\"draft\":false,\"tag_name\":\"v0.22.2\"},"
                    "{\"tag_name\":\"v0.23.0\"}]", 3u, &e, &v, &l));
    CHECK_EQ(l.patch, 2u);
    CHECK(!scan_line("[{\"tag_name\":\"v0.22.5\",\"draft\":true},"
                     "{\"tag_name\":\"v0.23.0\"}]", 3u, &e, &v, &l));
    /* An answer refused after a release of the line was read: none. */
    CHECK(!scan_line("[{\"tag_name\":\"v0.22.1\"},"
                     "{\"tag_name\":\"v0.22.1-rc1\"}]", 1u, &e, &v, &l));
    CHECK_EQ(e, FW_REL_VERSION);
    CHECK(!scan_line("[{\"tag_name\":\"v0.22.1\"},{\"tag_name\":1}]",
                     5u, &e, &v, &l));
    CHECK_EQ(e, FW_REL_TAG_FORM);
    /* An answer that ends before its array does: none. */
    CHECK(!scan_line("[{\"tag_name\":\"v0.22.1\"}", 4096u, &e, &v, &l));
    CHECK_EQ(e, FW_REL_TAG_NONE);
    /* The line's release as the 11th is not read. */
    static const char *const late[] = {
        "v0.23.0", "v0.23.1", "v0.23.2", "v0.23.3", "v0.23.4",
        "v0.24.0", "v0.24.1", "v0.24.2", "v0.25.0", "v0.25.1", "v0.22.9",
    };
    t = releases(late, 11u, "");
    REQUIRE_ALLOC(t);
    CHECK(!scan_line(t, 4096u, &e, &v, &l));
    CHECK_EQ(e, FW_REL_OK);
    CHECK_EQ(v.minor, 25u);
    free(t);
    t = releases(late + 1, 10u, "");
    REQUIRE_ALLOC(t);
    CHECK(scan_line(t, 4096u, &e, &v, &l));
    CHECK_EQ(l.patch, 9u);
    free(t);
    /* A scan not ended, or none: nothing. */
    fw_rel_scan_t s;
    fw_release_scan_init(&s);
    CHECK(!fw_release_scan_line(&s, &l));
    CHECK(!fw_release_scan_line(NULL, &l));
    static const char one[] = "[{\"tag_name\":\"v0.22.3\"}]";
    (void)fw_release_scan_feed(&s, (const uint8_t *)one, sizeof(one) - 1u);
    CHECK(fw_release_scan_line(&s, NULL));
}

int main(void)
{
    RUN(the_p4_panels_line_offers_every_newer_release);
    /* Every case after this one holds the ESP32-S3 panel's 0.22 line. */
    fw_release_set_line_capped(true);
    RUN(the_first_releases_tag_name_is_read);
    RUN(a_tag_name_deeper_or_as_a_value_is_not_the_releases);
    RUN(a_release_without_a_tag_refuses_the_answer);
    RUN(an_answer_that_ends_before_the_tag_has_none);
    RUN(a_tag_name_over_its_bound_is_too_long);
    RUN(a_tag_name_that_is_no_plain_string_is_out_of_form);
    RUN(json_around_the_tag_out_of_form_is_refused);
    RUN(nesting_is_followed_to_its_bound);
    RUN(a_tag_that_is_not_a_version_is_refused);
    RUN(the_scan_stops_at_its_bound);
    RUN(the_scan_keeps_little);
    RUN(the_highest_of_the_releases_is_taken_not_the_newest_by_date);
    RUN(ten_releases_are_read_and_an_eleventh_is_not);
    RUN(one_release_out_of_form_refuses_the_whole_answer);
    RUN(a_draft_is_passed_over_wherever_its_key_stands);
    RUN(only_a_newer_release_of_the_line_is_offered);
    RUN(a_running_version_that_did_not_parse_is_offered_nothing);
    RUN(the_lines_highest_release_is_kept_beside_the_highest);
    return test_summary("fw_release_scan");
}
