/*
 * TESTING's release list at the size GitHub sends it, and past the bound.
 *
 * The failure under test: an answer of the shape GitHub writes, each
 * release with its author, its notes and its assets (about 1648 bytes an
 * asset, nested uploader included), refused for its size although it is
 * within FW_REL_API_MAX, or one past the bound read instead of refused.
 * The answers are built here from that shape, as GitHub's of 2026-11-27
 * (v0.23.18: 26 assets, 8113 bytes of notes).  The HTTP (hypertext
 * transfer protocol) client is scripted; the clock is the test's.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "tick_wrap.h"
#include "wa_fetch.h"

/* ------------------------------------------------- the scripted client */

static struct {
    const char *body;
    uint32_t    body_n;
    int64_t     length;    /* -1: none */
    bool        chunked;
    uint32_t    sent;
    unsigned    opens, closes, reads;
    char        url[FW_REL_URL_MAX + 1u];
} g;

static bool f_open(void *ctx, const char *url, uint32_t timeout_ms,
                   bool api, wa_http_head_t *head, char *loc, size_t loc_cap)
{
    (void)ctx;
    (void)timeout_ms;
    (void)api;
    (void)loc;
    (void)loc_cap;
    ++g.opens;
    snprintf(g.url, sizeof(g.url), "%s", url);
    head->status = 200;
    head->length = g.length;
    head->chunked = g.chunked;
    head->loc_n = 0u;
    head->loc_cut = false;
    return true;
}

static int32_t f_read(void *ctx, uint8_t *dst, uint32_t n,
                      uint32_t timeout_ms)
{
    (void)ctx;
    (void)timeout_ms;
    ++g.reads;
    const uint32_t left = g.body_n - g.sent;
    const uint32_t k = (n < left) ? n : left;
    memcpy(dst, g.body + g.sent, k);
    g.sent += k;
    return (int32_t)k;
}

static void f_close(void *ctx)
{
    (void)ctx;
    ++g.closes;
}

static uint32_t f_now(void *ctx)
{
    (void)ctx;
    return 0u;
}

static bool f_stopped(void *ctx)
{
    (void)ctx;
    return false;
}

static const wa_http_t k_http = { f_open, f_read, f_close, NULL };

static char    g_url[FW_REL_URL_MAX + 1u];
static char    g_loc[FW_REL_URL_MAX + 1u];
static uint8_t g_io[4096];

/* A TESTING check of @p body, by Content-Length or in chunks, on a panel
 * that runs 0.23.0. */
static wa_fetch_res_t check(const char *body, uint32_t n, bool chunked)
{
    memset(&g, 0, sizeof(g));
    g.body = body;
    g.body_n = n;
    g.length = chunked ? -1 : (int64_t)n;
    g.chunked = chunked;
    wa_run_t r;
    memset(&r, 0, sizeof(r));
    r.http = &k_http;
    r.now = f_now;
    r.stopped = f_stopped;
    r.budget_ms = WA_FETCH_CHECK_MS;
    r.running = (fw_version_t){ 0u, 23u, 0u };
    r.running_ok = true;
    r.channel = WA_CH_TESTING;
    r.url = g_url;
    r.loc = g_loc;
    r.io = g_io;
    r.io_cap = sizeof(g_io);
    wa_fetch_res_t out;
    wa_run_check(&r, &out);
    return out;
}

/* ------------------------------------------- the answer, GitHub's shape */

#define USER "{\"login\":\"github-actions[bot]\",\"id\":41898282," \
    "\"node_id\":\"MDM6Qm90NDE4OTgyODI=\",\"avatar_url\":" \
    "\"https://avatars.githubusercontent.com/in/15368?v=4\"," \
    "\"gravatar_id\":\"\",\"url\":" \
    "\"https://api.github.com/users/github-actions%5Bbot%5D\"," \
    "\"html_url\":\"https://github.com/apps/github-actions\"," \
    "\"followers_url\":\"https://api.github.com/users/github-actions%5Bbot" \
    "%5D/followers\",\"following_url\":\"https://api.github.com/users/" \
    "github-actions%5Bbot%5D/following{/other_user}\",\"gists_url\":" \
    "\"https://api.github.com/users/github-actions%5Bbot%5D/gists{/gist_id}" \
    "\",\"starred_url\":\"https://api.github.com/users/github-actions%5Bbot" \
    "%5D/starred{/owner}{/repo}\",\"subscriptions_url\":\"https://api." \
    "github.com/users/github-actions%5Bbot%5D/subscriptions\"," \
    "\"organizations_url\":\"https://api.github.com/users/github-actions" \
    "%5Bbot%5D/orgs\",\"repos_url\":\"https://api.github.com/users/" \
    "github-actions%5Bbot%5D/repos\",\"events_url\":\"https://api.github." \
    "com/users/github-actions%5Bbot%5D/events{/privacy}\"," \
    "\"received_events_url\":\"https://api.github.com/users/" \
    "github-actions%5Bbot%5D/received_events\",\"type\":\"Bot\"," \
    "\"user_view_type\":\"public\",\"site_admin\":false}"

#define API "https://api.github.com/repos/subtilitas/rcbench/releases"

/* v0.23.18's notes, bytes; and the room a release's fixed part and one
 * asset take at most in the buffer the answer is built in. */
#define NOTES_MAX 8113u
#define FIXED_MAX 4096u
#define ASSET_MAX 1700u

typedef struct {
    char  *t;
    size_t n, cap;
} buf_t;

static void put(buf_t *b, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void put(buf_t *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int k = vsnprintf(b->t + b->n, b->cap - b->n, fmt, ap);
    va_end(ap);
    if (k > 0 && (size_t)k < b->cap - b->n) {
        b->n += (size_t)k;
    } else {
        b->n = b->cap;     /* the cap passed: the length check fails */
    }
}

/* One asset as GitHub writes it: 1648 bytes or close to it. */
static void asset(buf_t *b, const char *tag, unsigned i)
{
    put(b, "{\"url\":\"" API "/assets/8005%05u\",\"id\":8005%05u,"
        "\"node_id\":\"RA_kwDOUEVLqM4vtydG\",\"name\":"
        "\"rcbench-assets-%02u.bin\",\"label\":\"\",\"uploader\":%s,"
        "\"content_type\":\"application/octet-stream\",\"state\":"
        "\"uploaded\",\"size\":1283616,\"digest\":\"sha256:8734a24efd99a0"
        "308fdef9d88d6a08ee48e84864b52a6c0f69a6a4e5f910233b\","
        "\"download_count\":2,\"created_at\":\"2026-11-27T13:06:43Z\","
        "\"updated_at\":\"2026-11-27T13:06:44Z\",\"browser_download_url\":"
        "\"https://github.com/subtilitas/rcbench/releases/download/%s/"
        "rcbench-assets-%02u.bin\"}", i, i, i, USER, tag, i);
}

/* One release of @p assets assets and @p notes bytes of notes. */
static void release(buf_t *b, const char *tag, unsigned assets,
                    unsigned notes)
{
    put(b, "{\"url\":\"" API "/460612951\",\"assets_url\":\"" API
        "/460612951/assets\",\"upload_url\":\"https://uploads.github.com/"
        "repos/subtilitas/rcbench/releases/460612951/assets{?name,label}\","
        "\"html_url\":\"" FW_REL_TAG_PREFIX "%s\",\"id\":460612951,"
        "\"author\":%s,\"node_id\":\"RE_kwDOUEVLqM4bdGVX\","
        "\"tag_name\":\"%s\",\"target_commitish\":\"main\",\"name\":\"%s\","
        "\"draft\":false,\"immutable\":false,\"prerelease\":true,"
        "\"created_at\":\"2026-11-27T12:40:21Z\",\"updated_at\":"
        "\"2026-11-27T13:06:49Z\",\"published_at\":\"2026-11-27T13:06:49Z\","
        "\"assets\":[", tag, USER, tag, tag);
    for (unsigned i = 0; i < assets; ++i) {
        if (i > 0u) {
            put(b, ",");
        }
        asset(b, tag, i);
    }
    put(b, "],\"tarball_url\":\"https://api.github.com/repos/subtilitas/"
        "rcbench/tarball/%s\",\"zipball_url\":\"https://api.github.com/"
        "repos/subtilitas/rcbench/zipball/%s\",\"body\":\"", tag, tag);
    /* Notes: lines of text, each ended by an escaped line feed. */
    for (unsigned k = 0; k < notes; ++k) {
        put(b, "%s", ((k % 72u) == 70u) ? "\\" : ((k % 72u) == 71u) ? "n"
                                                                    : "x");
    }
    put(b, "\",\"mentions_count\":1}");
}

/* An answer of @p n releases, newest by date first, the highest the first. */
static char *answer(const char *const *tags, const unsigned *assets,
                    unsigned notes, size_t n, uint32_t *len)
{
    size_t cap = 64u;
    for (size_t i = 0; i < n; ++i) {
        cap += FIXED_MAX + notes + assets[i] * ASSET_MAX;
    }
    buf_t b = { malloc(cap), 0u, cap };
    if (b.t == NULL) {
        return NULL;
    }
    put(&b, "[");
    for (size_t i = 0; i < n; ++i) {
        if (i > 0u) {
            put(&b, ",");
        }
        release(&b, tags[i], assets[i], notes);
    }
    put(&b, "]");
    *len = (uint32_t)b.n;
    return b.t;
}

static const char *const k_tags[] = {
    "v0.23.18", "v0.23.17", "v0.23.16", "v0.23.15", "v0.23.14",
    "v0.23.13", "v0.23.12", "v0.23.11", "v0.23.10", "v0.23.9",
};

/* ------------------------------------------------------------- the cases */

TEST_CASE(the_request_asks_for_the_releases_the_scan_reads)
{
    CHECK_EQ(FW_REL_API_RELEASES, 5u);
    CHECK(strstr(FW_REL_API_URL, "?per_page=5") != NULL);
    CHECK_EQ(FW_REL_API_MAX, 5u * 262144u);
    CHECK_EQ(FW_REL_SCAN_MAX, FW_REL_API_MAX);
    /* One asset of the fixture: GitHub's 1648 bytes, within 2. */
    buf_t b = { malloc(ASSET_MAX), 0u, ASSET_MAX };
    REQUIRE_ALLOC(b.t);
    asset(&b, "v0.23.18", 0u);
    fprintf(stderr, "  one asset: %u bytes\n", (unsigned)b.n);
    CHECK(b.n >= 1646u && b.n <= 1650u);
    free(b.t);
}

TEST_CASE(an_answer_of_todays_size_is_read_to_its_5th_release)
{
    /* The 10 releases GitHub listed on 2026-11-27, their asset counts as
     * then, all with v0.23.18's notes: over 340000 bytes, past the old
     * bound of 262144.  Only the 5 the request asks for are read. */
    static const unsigned assets[] = { 26, 24, 14, 14, 14, 14, 14, 14, 14,
                                       14 };
    uint32_t n;
    char *t = answer(k_tags, assets, NOTES_MAX, 10u, &n);
    REQUIRE_ALLOC(t);
    fprintf(stderr, "  10 releases: %u bytes\n", (unsigned)n);
    CHECK(n >= 340000u);
    for (int chunked = 0; chunked <= 1; ++chunked) {
        const wa_fetch_res_t out = check(t, n, chunked != 0);
        CHECK_EQ(out.got, WA_GOT_NEWER);
        CHECK_EQ(out.rel, FW_REL_OK);
        CHECK_EQ(out.found.patch, 18u);
        CHECK_STR_EQ(g.url, FW_REL_API_URL);
        CHECK(g.sent < n);
        CHECK_EQ(g.closes, 1u);
    }
    free(t);
    /* 5 releases of 40 assets, read whole: over 340000 bytes too. */
    static const unsigned forty[] = { 40, 40, 40, 40, 40 };
    t = answer(k_tags, forty, NOTES_MAX, 5u, &n);
    REQUIRE_ALLOC(t);
    CHECK(n >= 340000u);
    const wa_fetch_res_t out = check(t, n, false);
    CHECK_EQ(out.got, WA_GOT_NEWER);
    CHECK_EQ(g.sent, n);
    free(t);
}

TEST_CASE(five_releases_of_152_assets_fit_and_160_do_not)
{
    /* The headroom the header states: 5 releases of 152 assets and 8113
     * bytes of notes each are within FW_REL_API_MAX and taken. */
    static const unsigned fit[] = { 152, 152, 152, 152, 152 };
    uint32_t n;
    char *t = answer(k_tags, fit, NOTES_MAX, 5u, &n);
    REQUIRE_ALLOC(t);
    fprintf(stderr, "  5 x 152 assets: %u bytes\n", (unsigned)n);
    CHECK(n <= FW_REL_API_MAX);
    for (int chunked = 0; chunked <= 1; ++chunked) {
        const wa_fetch_res_t out = check(t, n, chunked != 0);
        CHECK_EQ(out.got, WA_GOT_NEWER);
        CHECK_EQ(g.sent, n);
    }
    free(t);
    /* 160 each: past the bound.  By Content-Length it is refused before
     * a byte is read; in chunks the read stops at FW_REL_SCAN_MAX. */
    static const unsigned over[] = { 160, 160, 160, 160, 160 };
    t = answer(k_tags, over, NOTES_MAX, 5u, &n);
    REQUIRE_ALLOC(t);
    CHECK(n > FW_REL_API_MAX);
    wa_fetch_res_t out = check(t, n, false);
    CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
    CHECK_EQ(out.rel, FW_REL_TOO_BIG);
    CHECK_EQ(g.reads, 0u);
    CHECK_EQ(g.closes, 1u);
    out = check(t, n, true);
    CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
    CHECK_EQ(out.rel, FW_REL_SCAN_LONG);
    CHECK_EQ(g.sent, FW_REL_SCAN_MAX);
    CHECK_EQ(g.closes, 1u);
    free(t);
}

int main(void)
{
    RUN(the_request_asks_for_the_releases_the_scan_reads);
    RUN(an_answer_of_todays_size_is_read_to_its_5th_release);
    RUN(five_releases_of_152_assets_fit_and_160_do_not);
    return test_summary("wa_fetch_size");
}
