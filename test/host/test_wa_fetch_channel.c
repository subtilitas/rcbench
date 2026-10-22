/*
 * The release a check offers, on each of RELEASES' two channels.
 *
 * The failures under test: a release offered that is not newer, or past
 * the panel's release line (0.22: 0.23.0, 0.24.0 and 1.0.0 on either
 * channel), a patch release of the line not offered beside a higher
 * release for another panel, an offer to a panel whose own version did
 * not parse, an offer that outlives a change
 * of the channel, and an API (application programming interface) answer
 * acted on that should not be -- over its size, out of form, without a
 * tag, the first release by date taken over the highest -- or one read on
 * past its 10th release, past the budget or past an abort; and an answer
 * in chunks without a Content-Length, as GitHub sends one its cache does
 * not hold, refused instead of read.  GitHub's request limit, 403 and 429, ends
 * as its own result and asks nothing more.  The HTTP (hypertext transfer
 * protocol) client is scripted; the clock is the test's.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "tick_wrap.h"
#include "wa_fetch.h"

/* ------------------------------------------------- the scripted client */

typedef struct {
    int         status;
    int64_t     length;       /* -1: none                               */
    bool        chunked;
    const char *loc;          /* the redirect's Location; NULL none     */
    const char *body;
    uint32_t    body_n;       /* bytes it actually sends                */
    bool        no_connect;
    bool        read_breaks;  /* read() fails after the first piece     */
    bool        overrun;      /* read() hands back more than asked      */
    uint32_t    open_ms;
    uint32_t    read_ms;
} answer_t;

static struct {
    answer_t a;
    bool     have;
    answer_t api_a;       /* STABLE's second request, the API's, if set */
    bool     have_api;
    const answer_t *cur;  /* the answer of the request open now         */
    unsigned api_opens;
    uint32_t sent;
    char     url[FW_REL_URL_MAX + 1u];
    bool     api;
    unsigned opens, closes, reads;
    uint32_t last_timeout;
    uint32_t now;
    bool     stop;
    unsigned stop_after_reads;
} g;

static bool f_open(void *ctx, const char *url, uint32_t timeout_ms,
                   bool api, wa_http_head_t *head, char *loc, size_t loc_cap)
{
    (void)ctx;
    ++g.opens;
    g.last_timeout = timeout_ms;
    g.api = api;
    g.api_opens += api ? 1u : 0u;
    snprintf(g.url, sizeof(g.url), "%s", url);
    const bool second = api && g.have_api;
    const answer_t *a = second ? &g.api_a : &g.a;
    if (second) {
        g.sent = 0u;          /* a body of its own */
    }
    g.cur = a;
    g.now += a->open_ms;
    if (!(second || g.have) || a->no_connect) {
        return false;
    }
    head->status = a->status;
    head->length = a->length;
    head->chunked = a->chunked;
    head->loc_n = 0u;
    head->loc_cut = false;
    if (a->loc != NULL) {
        const size_t n = strlen(a->loc);
        head->loc_n = (n > loc_cap) ? loc_cap : n;
        memcpy(loc, a->loc, head->loc_n);
    }
    return true;
}

static int32_t f_read(void *ctx, uint8_t *dst, uint32_t n,
                      uint32_t timeout_ms)
{
    (void)ctx;
    const answer_t *a = (g.cur != NULL) ? g.cur : &g.a;
    ++g.reads;
    g.last_timeout = timeout_ms;
    g.now += a->read_ms;
    if (g.stop_after_reads != 0u && g.reads >= g.stop_after_reads) {
        g.stop = true;
    }
    if (a->read_breaks && g.sent > 0u) {
        return -1;
    }
    const uint32_t left = a->body_n - g.sent;
    const uint32_t k = (n < left) ? n : left;
    memcpy(dst, a->body + g.sent, k);
    g.sent += k;
    return (int32_t)k + ((a->overrun && k > 0u) ? 1 : 0);
}

static void f_close(void *ctx)
{
    (void)ctx;
    ++g.closes;
}

static uint32_t f_now(void *ctx)
{
    (void)ctx;
    return g.now;
}

static bool f_stopped(void *ctx)
{
    (void)ctx;
    return g.stop;
}

static const wa_http_t k_http = { f_open, f_read, f_close, NULL };

static char    g_url[FW_REL_URL_MAX + 1u];
static char    g_loc[FW_REL_URL_MAX + 1u];
static uint8_t g_io[4096];

static void fresh(uint32_t now)
{
    memset(&g, 0, sizeof(g));
    g.now = now;
}

static wa_run_t run_for(wa_channel_t ch, fw_version_t running, bool ok,
                        uint32_t start)
{
    wa_run_t r;
    memset(&r, 0, sizeof(r));
    r.http = &k_http;
    r.now = f_now;
    r.stopped = f_stopped;
    r.start_ms = start;
    r.budget_ms = WA_FETCH_CHECK_MS;
    r.running = running;
    r.running_ok = ok;
    r.channel = ch;
    r.url = g_url;
    r.loc = g_loc;
    r.io = g_io;
    r.io_cap = sizeof(g_io);
    return r;
}

#define V(a, b, c) ((fw_version_t){ (a), (b), (c) })

/* An API answer whose first release is @p tag, then @p tail bytes of
 * assets after it. */
static char *api_body(const char *tag, size_t tail, uint32_t *n)
{
    char *t = malloc(tail + 256u);
    if (t == NULL) {
        return NULL;
    }
    size_t k = (size_t)sprintf(t,
        "[{\"url\":\"https://api.github.com/repos/subtilitas/rcbench/"
        "releases/1\",\"author\":{\"login\":\"github-actions[bot]\"},"
        "\"tag_name\":\"%s\",\"prerelease\":true,\"assets\":[\"", tag);
    memset(t + k, 'a', tail);
    k += tail;
    k += (size_t)sprintf(t + k, "\"]}]");
    *n = (uint32_t)k;
    return t;
}

static void api_answer(const char *body, uint32_t n)
{
    g.a = (answer_t){ .status = 200, .length = n, .body = body,
                      .body_n = n };
    g.have = true;
}

static void latest_answer(const char *tag)
{
    static char loc[128];
    snprintf(loc, sizeof(loc), FW_REL_TAG_PREFIX "%s", tag);
    g.a = (answer_t){ .status = 302, .length = 0, .loc = loc };
    g.have = true;
}

static wa_fetch_res_t check(wa_channel_t ch, fw_version_t running, bool ok)
{
    const wa_run_t r = run_for(ch, running, ok, g.now);
    wa_fetch_res_t out;
    wa_run_check(&r, &out);
    return out;
}

/* ------------------------------------------------------- the channels */

TEST_CASE(testing_asks_the_api_with_its_headers_and_stops_at_the_10th)
{
    /* 10 releases by date, 0.22.1 newest and 0.23.1 the highest, then an
     * 11th of 40000 bytes that is never read.  0.23.1 is past the panel's
     * release line: 0.22.1, the line's highest, is offered in its place. */
    static char body[60000];
    static const char *const tags[] = {
        "v0.22.1", "v0.23.1", "v0.23.0", "v0.22.0", "v0.21.1",
        "v0.21.0", "v0.20.0", "v0.19.0", "v0.18.0", "v0.17.0",
    };
    uint32_t n = (uint32_t)sprintf(body, "[");
    for (size_t i = 0; i < 10u; ++i) {
        n += (uint32_t)sprintf(body + n, "%s{\"tag_name\":\"%s\"}",
                               (i == 0u) ? "" : ",", tags[i]);
    }
    n += (uint32_t)sprintf(body + n, ",{\"tag_name\":\"v9.0.0\",\"b\":\"");
    memset(body + n, 'b', 40000u);
    n += 40000u;
    n += (uint32_t)sprintf(body + n, "\"}]");
    fresh(1000u);
    api_answer(body, n);
    wa_run_t r = run_for(WA_CH_TESTING, V(0, 22, 0), true, g.now);
    r.io_cap = 64u;
    wa_fetch_res_t out;
    wa_run_check(&r, &out);
    CHECK_EQ(out.got, WA_GOT_NEWER);
    CHECK_EQ(out.found.minor, 22u);
    CHECK_EQ(out.found.patch, 1u);
    CHECK_STR_EQ(g.url, FW_REL_API_URL);
    CHECK(g.api);
    CHECK_EQ(g.opens, 1u);
    CHECK_EQ(g.closes, 1u);
    /* Read up to the piece that closes the 10th release, and no further. */
    CHECK(g.sent < 400u);
    CHECK_EQ(g.last_timeout, WA_FETCH_OP_MS);
    /* A panel on 0.22.1: nothing of the line newer; 0.23.1 named, not
     * offered. */
    fresh(1000u);
    api_answer(body, n);
    r = run_for(WA_CH_TESTING, V(0, 22, 1), true, g.now);
    wa_run_check(&r, &out);
    CHECK_EQ(out.got, WA_GOT_PAST_LINE);
    CHECK_EQ(out.found.minor, 23u);
    CHECK_EQ(out.found.patch, 1u);
}

TEST_CASE(stable_keeps_the_latest_redirect)
{
    fresh(0u);
    latest_answer("v0.22.1");
    const wa_fetch_res_t out = check(WA_CH_STABLE, V(0, 22, 0), true);
    CHECK_EQ(out.got, WA_GOT_NEWER);
    CHECK_EQ(out.found.minor, 22u);
    CHECK_STR_EQ(g.url, FW_REL_LATEST_URL);
    CHECK(!g.api);
    CHECK_EQ(g.reads, 0u);
}

/* STABLE with the latest release at @p latest and, for its second
 * request, the API's list @p list (NULL: that request does not connect). */
static wa_fetch_res_t stable_with_list(const char *latest, const char *list,
                                       fw_version_t running)
{
    fresh(0u);
    latest_answer(latest);
    g.have_api = true;
    if (list != NULL) {
        const uint32_t n = (uint32_t)strlen(list);
        g.api_a = (answer_t){ .status = 200, .length = n, .body = list,
                              .body_n = n };
    } else {
        g.api_a = (answer_t){ .no_connect = true };
    }
    return check(WA_CH_STABLE, running, true);
}

TEST_CASE(stable_past_the_line_reads_the_list_once_for_the_lines_newest)
{
    static const char list[] = "[{\"tag_name\":\"v0.24.0\"},"
                               "{\"tag_name\":\"v0.23.1\"},"
                               "{\"tag_name\":\"v0.22.1\"},"
                               "{\"tag_name\":\"v0.22.0\"}]";
    /* 0.24.0 latest, 0.22.1 in the list, running 0.22.0: offered. */
    wa_fetch_res_t out = stable_with_list("v0.24.0", list, V(0, 22, 0));
    CHECK_EQ(out.got, WA_GOT_NEWER);
    CHECK_EQ(out.found.minor, 22u);
    CHECK_EQ(out.found.patch, 1u);
    CHECK_EQ(g.opens, 2u);
    CHECK_EQ(g.api_opens, 1u);
    CHECK_EQ(g.closes, 2u);
    CHECK_STR_EQ(g.url, FW_REL_API_URL);
    /* Running 0.22.1, 0.22.1 the line's newest: nothing newer on the line,
     * the newest release named. */
    out = stable_with_list("v0.24.0", list, V(0, 22, 1));
    CHECK_EQ(out.got, WA_GOT_PAST_LINE);
    CHECK_EQ(out.found.minor, 24u);
    CHECK_EQ(g.api_opens, 1u);
    /* 0.24.0 latest, no 0.22.x newer, or none at all: PAST_LINE. */
    static const char older[] = "[{\"tag_name\":\"v0.24.0\"},"
                                "{\"tag_name\":\"v0.22.0\"}]";
    out = stable_with_list("v0.24.0", older, V(0, 22, 0));
    CHECK_EQ(out.got, WA_GOT_PAST_LINE);
    CHECK_EQ(out.found.minor, 24u);
    static const char none[] = "[{\"tag_name\":\"v0.24.0\"},"
                               "{\"tag_name\":\"v0.23.0\"}]";
    out = stable_with_list("v0.24.0", none, V(0, 22, 0));
    CHECK_EQ(out.got, WA_GOT_PAST_LINE);
    CHECK_EQ(out.found.minor, 24u);
    /* The list's request fails: as without it, no offer. */
    out = stable_with_list("v0.24.0", NULL, V(0, 22, 0));
    CHECK_EQ(out.got, WA_GOT_PAST_LINE);
    CHECK_EQ(out.found.minor, 24u);
    CHECK_EQ(g.api_opens, 1u);
    out = stable_with_list("v0.24.0", "[{\"tag_name\":\"v0.22.1-rc1\"}]",
                           V(0, 22, 0));
    CHECK_EQ(out.got, WA_GOT_PAST_LINE);
    CHECK_EQ(out.found.minor, 24u);
    fresh(0u);
    latest_answer("v0.24.0");
    g.have_api = true;
    g.api_a = (answer_t){ .status = 403, .length = 10 };
    out = check(WA_CH_STABLE, V(0, 22, 0), true);
    CHECK_EQ(out.got, WA_GOT_PAST_LINE);
    CHECK_EQ(out.status, 0);
    /* No time left for it: no request, as without it. */
    fresh(0u);
    latest_answer("v0.24.0");
    g.a.open_ms = WA_FETCH_CHECK_MS;
    g.have_api = true;
    g.api_a = (answer_t){ .status = 200, .length = (int64_t)strlen(list),
                          .body = list, .body_n = (uint32_t)strlen(list) };
    out = check(WA_CH_STABLE, V(0, 22, 0), true);
    CHECK_EQ(out.got, WA_GOT_PAST_LINE);
    CHECK_EQ(g.api_opens, 0u);
    /* Latest on the line, or not newer: no second request. */
    out = stable_with_list("v0.22.1", list, V(0, 22, 0));
    CHECK_EQ(out.got, WA_GOT_NEWER);
    CHECK_EQ(g.api_opens, 0u);
    out = stable_with_list("v0.21.2", list, V(0, 22, 0));
    CHECK_EQ(out.got, WA_GOT_CURRENT);
    CHECK_EQ(g.api_opens, 0u);
    /* A running version that did not parse: no second request. */
    fresh(0u);
    latest_answer("v0.24.0");
    g.have_api = true;
    g.api_a = (answer_t){ .no_connect = true };
    out = check(WA_CH_STABLE, V(0, 0, 0), false);
    CHECK_EQ(out.got, WA_GOT_UNVERSIONED);
    CHECK_EQ(g.api_opens, 0u);
}

/* One check of either channel: what it offers a panel on @p running; the
 * whole result in g_offer. */
static wa_fetch_res_t g_offer;

static wa_got_t offer(wa_channel_t ch, const char *tag, fw_version_t running,
                      bool ok)
{
    fresh(0u);
    if (ch == WA_CH_TESTING) {
        static char body[256];
        const int n = snprintf(body, sizeof(body),
                               "[{\"id\":1,\"tag_name\":\"%s\"}]", tag);
        api_answer(body, (uint32_t)n);
    } else {
        latest_answer(tag);
    }
    g_offer = check(ch, running, ok);
    return g_offer.got;
}

TEST_CASE(only_a_newer_release_of_the_line_is_offered_on_either_channel)
{
    const wa_channel_t chs[] = { WA_CH_STABLE, WA_CH_TESTING };
    for (size_t i = 0; i < 2u; ++i) {
        const wa_channel_t ch = chs[i];
        /* Of the line, 0.22: newer offered, equal and older not. */
        CHECK_EQ(offer(ch, "v0.22.1", V(0, 22, 0), true), WA_GOT_NEWER);
        CHECK_EQ(offer(ch, "v0.22.0", V(0, 22, 0), true), WA_GOT_CURRENT);
        CHECK_EQ(offer(ch, "v0.22.0", V(0, 22, 1), true), WA_GOT_CURRENT);
        CHECK_EQ(offer(ch, "v0.21.2", V(0, 22, 0), true), WA_GOT_CURRENT);
        /* 0.22.10 after 0.22.9, as numbers. */
        CHECK_EQ(offer(ch, "v0.22.10", V(0, 22, 9), true), WA_GOT_NEWER);
        CHECK_EQ(offer(ch, "v0.22.9", V(0, 22, 10), true), WA_GOT_CURRENT);
        /* Past the line: named, never offered. */
        CHECK_EQ(offer(ch, "v0.23.0", V(0, 22, 0), true), WA_GOT_PAST_LINE);
        CHECK_EQ(offer(ch, "v0.24.0", V(0, 22, 0), true), WA_GOT_PAST_LINE);
        CHECK_EQ(offer(ch, "v0.24.0", V(0, 22, 65535), true),
                 WA_GOT_PAST_LINE);
        CHECK_EQ(offer(ch, "v1.0.0", V(0, 22, 0), true), WA_GOT_PAST_LINE);
        /* A tag out of form is no offer; the card names why. */
        CHECK_EQ(offer(ch, "v0.22.1-rc1", V(0, 22, 0), true),
                 WA_GOT_UNEXPECTED);
        CHECK_EQ(g_offer.rel, FW_REL_VERSION);
        CHECK_EQ(offer(ch, "v0.22", V(0, 22, 0), true), WA_GOT_UNEXPECTED);
        CHECK_EQ(g_offer.rel, FW_REL_VERSION);
        /* A running version that did not parse is offered nothing, and
         * the check still names what it found. */
        CHECK_EQ(offer(ch, "v0.22.1", V(0, 0, 0), false),
                 WA_GOT_UNVERSIONED);
        CHECK_EQ(offer(ch, "v9.0.0", V(0, 0, 0), false),
                 WA_GOT_UNVERSIONED);
    }
    /* TESTING: a 0.23.0 pre-release, the newest by date and the highest,
     * beside 0.22.0: nothing for a panel on 0.22.0. */
    static const char pre[] = "[{\"tag_name\":\"v0.23.0\","
                              "\"prerelease\":true,\"draft\":false},"
                              "{\"tag_name\":\"v0.22.0\","
                              "\"prerelease\":false}]";
    fresh(0u);
    api_answer(pre, (uint32_t)strlen(pre));
    wa_fetch_res_t u = check(WA_CH_TESTING, V(0, 22, 0), true);
    CHECK_EQ(u.got, WA_GOT_PAST_LINE);
    CHECK_EQ(u.found.minor, 23u);
    /* The same list for a build before the line: 0.22.0 is offered. */
    fresh(0u);
    api_answer(pre, (uint32_t)strlen(pre));
    u = check(WA_CH_TESTING, V(0, 21, 2), true);
    CHECK_EQ(u.got, WA_GOT_NEWER);
    CHECK_EQ(u.found.minor, 22u);
    CHECK_EQ(u.found.patch, 0u);
    fresh(0u);
    latest_answer("v0.20.0");
    u = check(WA_CH_STABLE, V(0, 0, 0), false);
    CHECK_EQ(u.got, WA_GOT_UNVERSIONED);
    CHECK_EQ(u.found.minor, 20u);
}

/* The ESP32-P4 panel's line, as the panel starts: not capped.  0.23.0 and
 * later are offered on either channel, STABLE asks GitHub once, and
 * TESTING offers the highest release, a pre-release included.  Run
 * before the cap is set. */
TEST_CASE(the_p4_panel_is_offered_0_23_0_and_later_on_either_channel)
{
    CHECK(!fw_release_line_capped());
    const wa_channel_t chs[] = { WA_CH_STABLE, WA_CH_TESTING };
    for (size_t i = 0; i < 2u; ++i) {
        const wa_channel_t ch = chs[i];
        CHECK_EQ(offer(ch, "v0.23.0", V(0, 22, 0), true), WA_GOT_NEWER);
        CHECK_EQ(offer(ch, "v0.24.0", V(0, 23, 0), true), WA_GOT_NEWER);
        CHECK_EQ(offer(ch, "v1.0.0", V(0, 24, 0), true), WA_GOT_NEWER);
        CHECK_EQ(offer(ch, "v0.23.0", V(0, 23, 0), true), WA_GOT_CURRENT);
        CHECK_EQ(offer(ch, "v0.22.1", V(0, 23, 0), true), WA_GOT_CURRENT);
    }
    /* STABLE: one request, the latest redirect; the list is not read. */
    fresh(0u);
    latest_answer("v0.24.0");
    wa_fetch_res_t u = check(WA_CH_STABLE, V(0, 23, 0), true);
    CHECK_EQ(u.got, WA_GOT_NEWER);
    CHECK_EQ(u.found.minor, 24u);
    CHECK_EQ(g.api_opens, 0u);
    /* TESTING: a 0.25.0 pre-release beside a later 0.22.1 patch release
     * for the ESP32-S3 panel: the highest, 0.25.0, is offered. */
    static const char pre[] = "[{\"tag_name\":\"v0.22.1\"},"
                              "{\"tag_name\":\"v0.25.0\","
                              "\"prerelease\":true},"
                              "{\"tag_name\":\"v0.24.0\"}]";
    fresh(0u);
    api_answer(pre, (uint32_t)strlen(pre));
    u = check(WA_CH_TESTING, V(0, 24, 0), true);
    CHECK_EQ(u.got, WA_GOT_NEWER);
    CHECK_EQ(u.found.minor, 25u);
}

/* ------------------------------------------------- the API's answer */

TEST_CASE(the_request_limit_is_its_own_result)
{
    const int limit[] = { 403, 429 };
    for (size_t i = 0; i < 2u; ++i) {
        fresh(0u);
        g.a = (answer_t){ .status = limit[i], .length = 120 };
        g.have = true;
        const wa_fetch_res_t out = check(WA_CH_TESTING, V(0, 22, 0), true);
        CHECK_EQ(out.got, WA_GOT_LIMIT);
        CHECK_EQ(out.status, limit[i]);
        /* One request, its body unread, nothing asked again. */
        CHECK_EQ(g.opens, 1u);
        CHECK_EQ(g.closes, 1u);
        CHECK_EQ(g.reads, 0u);
    }
    const int other[] = { 404, 500, 301, 302, 304, 0, 401 };
    for (size_t i = 0; i < sizeof(other) / sizeof(other[0]); ++i) {
        fresh(0u);
        g.a = (answer_t){ .status = other[i], .length = 10,
                          .loc = FW_REL_TAG_PREFIX "v9.0.0" };
        g.have = true;
        const wa_fetch_res_t out = check(WA_CH_TESTING, V(0, 22, 0), true);
        CHECK_EQ(out.got, WA_GOT_REFUSED);
        CHECK_EQ(out.status, other[i]);
        CHECK_EQ(g.opens, 1u);
        CHECK_EQ(g.reads, 0u);
    }
}

TEST_CASE(the_answers_size_is_announced_and_within_its_bound)
{
    uint32_t n;
    char *body = api_body("v0.22.1", 10u, &n);
    REQUIRE_ALLOC(body);
    const struct { int64_t len; bool chunked; wa_got_t got;
                   fw_rel_err_t rel; } cases[] = {
        /* Neither a length nor chunks: read to the close, refused. */
        { -1, false, WA_GOT_UNEXPECTED, FW_REL_NO_LENGTH },
        /* Chunked, with or without a length: read in chunks. */
        { -1, true, WA_GOT_NEWER, FW_REL_OK },
        { (int64_t)n, true, WA_GOT_NEWER, FW_REL_OK },
        { 1, true, WA_GOT_NEWER, FW_REL_OK },
        { 0, false, WA_GOT_UNEXPECTED, FW_REL_EMPTY },
        { (int64_t)FW_REL_API_MAX + 1, false, WA_GOT_UNEXPECTED,
          FW_REL_TOO_BIG },
        { (int64_t)FW_REL_API_MAX, false, WA_GOT_NEWER, FW_REL_OK },
        { 1, false, WA_GOT_UNEXPECTED, FW_REL_TAG_NONE },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        fresh(0u);
        api_answer(body, n);
        g.a.length = cases[i].len;
        g.a.chunked = cases[i].chunked;
        const wa_fetch_res_t out = check(WA_CH_TESTING, V(0, 22, 0), true);
        CHECK_EQ(out.got, cases[i].got);
        CHECK_EQ(out.rel, cases[i].rel);
        CHECK_EQ(g.closes, 1u);
        if (cases[i].got == WA_GOT_UNEXPECTED
            && cases[i].rel != FW_REL_TAG_NONE) {
            CHECK_EQ(g.reads, 0u);
        }
    }
    free(body);
}

/* The answer as GitHub sends it when its cache does not hold the request:
 * Transfer-Encoding: chunked, no Content-Length. */
static void chunked_answer(const char *body, uint32_t n)
{
    g.a = (answer_t){ .status = 200, .length = -1, .chunked = true,
                      .body = body, .body_n = n };
    g.have = true;
}

TEST_CASE(a_chunked_answer_is_read_to_its_releases_end)
{
    /* The start-up check's failure: a 200 in chunks ended UNEXPECTED,
     * NO_LENGTH, without a byte read. */
    uint32_t n;
    char *body = api_body("v0.22.1", 9000u, &n);
    REQUIRE_ALLOC(body);
    fresh(0u);
    chunked_answer(body, n);
    wa_run_t r = run_for(WA_CH_TESTING, V(0, 22, 0), true, 0u);
    r.io_cap = 512u;
    wa_fetch_res_t out;
    wa_run_check(&r, &out);
    CHECK_EQ(out.got, WA_GOT_NEWER);
    CHECK_EQ(out.rel, FW_REL_OK);
    CHECK_EQ(out.found.minor, 22u);
    CHECK_EQ(out.found.patch, 1u);
    CHECK_EQ(g.sent, n);
    CHECK_EQ(g.opens, 1u);
    CHECK_EQ(g.closes, 1u);
    /* The same answer on a panel that runs it: CURRENT. */
    fresh(0u);
    chunked_answer(body, n);
    CHECK_EQ(check(WA_CH_TESTING, V(0, 22, 1), true).got, WA_GOT_CURRENT);
    /* With a Content-Length the same bytes give the same result. */
    fresh(0u);
    api_answer(body, n);
    CHECK_EQ(check(WA_CH_TESTING, V(0, 22, 0), true).got, WA_GOT_NEWER);
    free(body);

    /* 10 releases and an 11th of 40000 bytes: in chunks too, the read
     * stops at the piece that closes the 10th.  0.30.0, the highest, is
     * past the line, and 0.22.0, its own, is the running build. */
    static char ten[60000];
    uint32_t k = (uint32_t)sprintf(ten, "[");
    for (unsigned i = 0; i < 10u; ++i) {
        k += (uint32_t)sprintf(ten + k, "%s{\"tag_name\":\"v0.%u.0\"}",
                               (i == 0u) ? "" : ",", 30u - i);
    }
    k += (uint32_t)sprintf(ten + k, ",{\"tag_name\":\"v9.0.0\",\"b\":\"");
    memset(ten + k, 'b', 40000u);
    k += 40000u;
    k += (uint32_t)sprintf(ten + k, "\"}]");
    fresh(0u);
    chunked_answer(ten, k);
    r.io_cap = 64u;
    wa_run_check(&r, &out);
    CHECK_EQ(out.got, WA_GOT_PAST_LINE);
    CHECK_EQ(out.found.minor, 30u);
    CHECK(g.sent < 400u);
    CHECK_EQ(g.closes, 1u);
}

TEST_CASE(a_chunked_answer_is_held_to_the_scan_bound)
{
    /* Exactly FW_REL_SCAN_MAX bytes, the array's ']' the last: taken. */
    char *big = malloc(FW_REL_SCAN_MAX + 64u);
    REQUIRE_ALLOC(big);
    static const char tail[] = "\",\"tag_name\":\"v0.22.1\"}]";
    uint32_t n = (uint32_t)sprintf(big, "[{\"body\":\"");
    const uint32_t pad = FW_REL_SCAN_MAX - n - (uint32_t)(sizeof(tail) - 1u);
    memset(big + n, 'b', pad);
    n += pad;
    n += (uint32_t)sprintf(big + n, "%s", tail);
    CHECK_EQ(n, FW_REL_SCAN_MAX);
    fresh(0u);
    chunked_answer(big, n);
    wa_fetch_res_t out = check(WA_CH_TESTING, V(0, 22, 0), true);
    CHECK_EQ(out.got, WA_GOT_NEWER);
    CHECK_EQ(g.sent, FW_REL_SCAN_MAX);
    /* One byte of padding more: the scan's bound passes before the
     * releases end, and nothing past it is read. */
    memmove(big + 12u, big + 11u, n - 11u);
    big[11] = 'b';
    fresh(0u);
    chunked_answer(big, n + 1u);
    out = check(WA_CH_TESTING, V(0, 22, 0), true);
    CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
    CHECK_EQ(out.rel, FW_REL_SCAN_LONG);
    CHECK_EQ(g.sent, FW_REL_SCAN_MAX);
    CHECK_EQ(g.closes, 1u);
    free(big);
}

TEST_CASE(a_chunked_answer_that_is_empty_cut_or_overruns)
{
    static const char body[] = "[{\"a\":\"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\","
                               "\"tag_name\":\"v0.22.1\"}]";
    const uint32_t n = (uint32_t)strlen(body);
    /* No byte before the last chunk: empty. */
    fresh(0u);
    chunked_answer(body, 0u);
    wa_fetch_res_t out = check(WA_CH_TESTING, V(0, 22, 0), true);
    CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
    CHECK_EQ(out.rel, FW_REL_EMPTY);
    CHECK_EQ(g.reads, 1u);
    CHECK_EQ(g.closes, 1u);
    /* The last chunk before the tag ends: no tag. */
    fresh(0u);
    chunked_answer(body, 20u);
    out = check(WA_CH_TESTING, V(0, 22, 0), true);
    CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
    CHECK_EQ(out.rel, FW_REL_TAG_NONE);
    /* One byte: no tag either. */
    fresh(0u);
    chunked_answer(body, 1u);
    out = check(WA_CH_TESTING, V(0, 22, 0), true);
    CHECK_EQ(out.rel, FW_REL_TAG_NONE);
    /* Closed before the last chunk: the platform's read fails. */
    fresh(0u);
    chunked_answer(body, n);
    g.a.read_breaks = true;
    wa_run_t r = run_for(WA_CH_TESTING, V(0, 22, 0), true, 0u);
    r.io_cap = 8u;
    wa_run_check(&r, &out);
    CHECK_EQ(out.got, WA_GOT_CONNECT);
    CHECK_EQ(g.closes, 1u);
    /* More than asked for: 9 bytes for a piece of 8. */
    fresh(0u);
    chunked_answer(body, n);
    g.a.overrun = true;
    wa_run_check(&r, &out);
    CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
    CHECK_EQ(out.rel, FW_REL_OVERRUN);
    /* Pieces of 4 bytes, 2 s each, from 25 s on: the budget ends it. */
    fresh(25000u);
    chunked_answer(body, n);
    g.a.read_ms = 2000u;
    r.io_cap = 4u;
    wa_run_check(&r, &out);
    CHECK_EQ(out.got, WA_GOT_TIMEOUT);
    CHECK(g.reads <= 3u);
    /* An abort after the first piece. */
    fresh(0u);
    chunked_answer(body, n);
    g.stop_after_reads = 1u;
    r.io_cap = 8u;
    wa_run_check(&r, &out);
    CHECK_EQ(out.got, WA_GOT_ABORTED);
    CHECK_EQ(g.reads, 1u);
    /* STABLE reads no body: chunks change nothing there. */
    fresh(0u);
    latest_answer("v0.22.1");
    g.a.chunked = true;
    g.a.length = -1;
    out = check(WA_CH_STABLE, V(0, 22, 0), true);
    CHECK_EQ(out.got, WA_GOT_NEWER);
    CHECK_EQ(g.reads, 0u);
}

TEST_CASE(a_tag_out_of_form_has_its_own_reason)
{
    char *long_tag = NULL;
    uint32_t n;
    const struct { const char *body; fw_rel_err_t rel; } cases[] = {
        { "[{\"name\":\"v0.21.0\"}]", FW_REL_TAG_NONE },
        { "[]", FW_REL_TAG_NONE },
        { "[{\"tag_name\":\"v0.21.0-testing\"}]", FW_REL_VERSION },
        { "[{\"tag_name\":\"v65535.65535.655350\"}]", FW_REL_TAG_LONG },
        { "[{\"tag_name\":0.21}]", FW_REL_TAG_FORM },
        { "{\"message\":\"API rate limit exceeded\"}", FW_REL_TAG_FORM },
        { "[{\"tag_name\":\"0.21.0\"}]", FW_REL_VERSION },
        /* The answer stops where its length says, before the tag ends. */
        { "[{\"tag_name\":\"v0.2", FW_REL_TAG_NONE },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        fresh(0u);
        api_answer(cases[i].body, (uint32_t)strlen(cases[i].body));
        const wa_fetch_res_t out = check(WA_CH_TESTING, V(0, 22, 0), true);
        CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
        CHECK_EQ(out.rel, cases[i].rel);
        CHECK_EQ(g.closes, 1u);
    }
    /* An answer of exactly FW_REL_API_MAX bytes, the array's ']' its last
     * byte: read whole and taken.  One byte more is refused by its length
     * before a byte is read. */
    long_tag = malloc(FW_REL_API_MAX + 64u);
    REQUIRE_ALLOC(long_tag);
    static const char tail[] = "\",\"tag_name\":\"v0.22.1\"}]";
    n = (uint32_t)sprintf(long_tag, "[{\"body\":\"");
    const uint32_t pad = FW_REL_API_MAX - n - (uint32_t)(sizeof(tail) - 1u);
    memset(long_tag + n, 'b', pad);
    n += pad;
    n += (uint32_t)sprintf(long_tag + n, "%s", tail);
    CHECK_EQ(n, FW_REL_API_MAX);
    fresh(0u);
    api_answer(long_tag, n);
    wa_fetch_res_t out = check(WA_CH_TESTING, V(0, 22, 0), true);
    CHECK_EQ(out.got, WA_GOT_NEWER);
    CHECK_EQ(g.sent, FW_REL_API_MAX);
    fresh(0u);
    api_answer(long_tag, n);
    g.a.length = (int64_t)FW_REL_API_MAX + 1;
    out = check(WA_CH_TESTING, V(0, 22, 0), true);
    CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
    CHECK_EQ(out.rel, FW_REL_TOO_BIG);
    CHECK_EQ(g.reads, 0u);
    /* A release out of form among good ones refuses the answer. */
    static const char mixed[] = "[{\"tag_name\":\"v0.23.0\"},"
                                "{\"tag_name\":\"v0.24\"},"
                                "{\"tag_name\":\"v0.22.0\"}]";
    fresh(0u);
    api_answer(mixed, (uint32_t)strlen(mixed));
    out = check(WA_CH_TESTING, V(0, 22, 0), true);
    CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
    CHECK_EQ(out.rel, FW_REL_VERSION);
    free(long_tag);
}

TEST_CASE(a_body_that_breaks_off_lies_or_overruns)
{
    uint32_t n;
    char *body = api_body("v0.22.1", 9000u, &n);
    REQUIRE_ALLOC(body);
    /* Closed before its length, before the tag: short. */
    fresh(0u);
    api_answer(body, n);
    g.a.body_n = 40u;
    wa_fetch_res_t out = check(WA_CH_TESTING, V(0, 22, 0), true);
    CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
    CHECK_EQ(out.rel, FW_REL_SHORT);
    /* A read that fails: the connection. */
    fresh(0u);
    api_answer(body, n);
    g.a.body_n = 40u;
    g.a.read_breaks = true;
    out = check(WA_CH_TESTING, V(0, 22, 0), true);
    CHECK_EQ(out.got, WA_GOT_CONNECT);
    /* More than asked for. */
    fresh(0u);
    api_answer(body, n);
    g.a.overrun = true;
    out = check(WA_CH_TESTING, V(0, 22, 0), true);
    CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
    CHECK_EQ(out.rel, FW_REL_OVERRUN);
    CHECK_EQ(g.closes, 1u);
    /* No connection, and one that waited out the budget. */
    fresh(0u);
    g.a = (answer_t){ .no_connect = true, .open_ms = 3000u };
    g.have = true;
    out = check(WA_CH_TESTING, V(0, 22, 0), true);
    CHECK_EQ(out.got, WA_GOT_CONNECT);
    CHECK_EQ(g.closes, 1u);
    fresh(0u);
    g.a = (answer_t){ .no_connect = true, .open_ms = WA_FETCH_CHECK_MS };
    g.have = true;
    out = check(WA_CH_TESTING, V(0, 22, 0), true);
    CHECK_EQ(out.got, WA_GOT_TIMEOUT);
    free(body);
}

static void api_within_the_budget(uint32_t t0)
{
    /* Slow pieces of 4 bytes: the body is read while the budget lasts. */
    static const char body[] = "[{\"a\":\"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\","
                               "\"tag_name\":\"v0.22.1\"}]";
    const uint32_t n = (uint32_t)strlen(body);
    /* The join took 25 s: the request waits 5 s at most. */
    fresh(t0 + 25000u);
    api_answer(body, n);
    wa_run_t r = run_for(WA_CH_TESTING, V(0, 22, 0), true, t0);
    wa_fetch_res_t out;
    wa_run_check(&r, &out);
    CHECK_EQ(out.got, WA_GOT_NEWER);
    CHECK(g.last_timeout <= 5000u);
    /* Each read takes 2 s from 25 s on: the budget ends mid-body. */
    fresh(t0 + 25000u);
    api_answer(body, n);
    g.a.read_ms = 2000u;
    r.io_cap = 4u;
    wa_run_check(&r, &out);
    CHECK_EQ(out.got, WA_GOT_TIMEOUT);
    CHECK_EQ(g.closes, 1u);
    CHECK(g.reads <= 3u);
    CHECK(g.last_timeout <= 1000u);
    /* None left at the start: nothing sent. */
    fresh(t0 + WA_FETCH_CHECK_MS);
    api_answer(body, n);
    wa_run_check(&r, &out);
    CHECK_EQ(out.got, WA_GOT_TIMEOUT);
    CHECK_EQ(g.opens, 0u);
}

TEST_CASE(the_api_check_holds_its_budget_across_the_wrap)
{
    at_tick_0_and_before_the_wrap(api_within_the_budget, 40000u);
}

TEST_CASE(an_abort_ends_the_api_read)
{
    static const char body[] = "[{\"a\":\"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\","
                               "\"tag_name\":\"v0.22.1\"}]";
    fresh(0u);
    api_answer(body, (uint32_t)strlen(body));
    g.stop_after_reads = 1u;
    wa_run_t r = run_for(WA_CH_TESTING, V(0, 22, 0), true, 0u);
    r.io_cap = 8u;
    wa_fetch_res_t out;
    wa_run_check(&r, &out);
    CHECK_EQ(out.got, WA_GOT_ABORTED);
    CHECK_EQ(g.reads, 1u);
    CHECK_EQ(g.closes, 1u);
    /* Stopped before the request: none sent. */
    fresh(0u);
    api_answer(body, (uint32_t)strlen(body));
    g.stop = true;
    wa_run_check(&r, &out);
    CHECK_EQ(out.got, WA_GOT_ABORTED);
    CHECK_EQ(g.opens, 0u);
}

/* ------------------------------------------------------------ the job */

static wa_fetch_t ended_check(wa_channel_t ch, wa_got_t got)
{
    wa_fetch_t f;
    wa_fetch_init(&f, V(0, 22, 0));
    (void)wa_fetch_start(&f, WA_JOB_CHECK, V(0, 0, 0), ch, true, WA_WHY_NONE,
                         0u);
    wa_t w;
    memset(&w, 0, sizeof(w));
    w.mode = WA_MODE_FETCH;
    w.state = WA_ST_JOINED;
    wa_fetch_tick(&f, 10u, WA_WHY_NONE, &w);
    const wa_fetch_res_t r = { .got = got, .found = V(0, 21, 0),
                               .status = 403 };
    wa_fetch_on_end(&f, f.gen, &r);
    return f;
}

TEST_CASE(an_offer_holds_only_for_the_channel_it_was_found_on)
{
    wa_fetch_t f = ended_check(WA_CH_TESTING, WA_GOT_NEWER);
    CHECK_EQ(f.channel, WA_CH_TESTING);
    CHECK(wa_fetch_offers(&f, WA_CH_TESTING));
    CHECK(!wa_fetch_stale(&f, WA_CH_TESTING));
    /* RELEASES set back to STABLE: the testing release is not offered,
     * and the check's result is not shown. */
    CHECK(!wa_fetch_offers(&f, WA_CH_STABLE));
    CHECK(wa_fetch_stale(&f, WA_CH_STABLE));
    f = ended_check(WA_CH_STABLE, WA_GOT_NEWER);
    CHECK(wa_fetch_offers(&f, WA_CH_STABLE));
    CHECK(!wa_fetch_offers(&f, WA_CH_TESTING));
    /* Results without an offer. */
    const wa_got_t none[] = { WA_GOT_CURRENT, WA_GOT_UNVERSIONED,
                              WA_GOT_PAST_LINE, WA_GOT_LIMIT,
                              WA_GOT_UNEXPECTED };
    for (size_t i = 0; i < sizeof(none) / sizeof(none[0]); ++i) {
        f = ended_check(WA_CH_TESTING, none[i]);
        CHECK(!wa_fetch_offers(&f, WA_CH_TESTING));
        CHECK(wa_fetch_stale(&f, WA_CH_STABLE));
    }
    /* What the result keeps, and which results name a release. */
    f = ended_check(WA_CH_TESTING, WA_GOT_UNVERSIONED);
    CHECK_EQ(f.found.minor, 21u);
    const wa_got_t named[] = { WA_GOT_NEWER, WA_GOT_CURRENT,
                               WA_GOT_UNVERSIONED, WA_GOT_PAST_LINE };
    for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); ++i) {
        f = ended_check(WA_CH_STABLE, named[i]);
        CHECK(wa_fetch_has_found(&f));
        CHECK_EQ(f.found.minor, 21u);
    }
    for (size_t i = 3u; i < sizeof(none) / sizeof(none[0]); ++i) {
        f = ended_check(WA_CH_STABLE, none[i]);     /* LIMIT, UNEXPECTED */
        CHECK(!wa_fetch_has_found(&f));
        CHECK_EQ(f.found.minor, 0u);
    }
    CHECK(!wa_fetch_has_found(NULL));
    f = ended_check(WA_CH_TESTING, WA_GOT_LIMIT);
    CHECK_EQ(f.status, 403);
    /* A check under way, a download, and nothing run are never stale. */
    wa_fetch_init(&f, V(0, 22, 0));
    CHECK(!wa_fetch_stale(&f, WA_CH_TESTING));
    CHECK(!wa_fetch_offers(&f, WA_CH_STABLE));
    (void)wa_fetch_start(&f, WA_JOB_CHECK, V(0, 0, 0), WA_CH_TESTING, true,
                         WA_WHY_NONE, 0u);
    CHECK(!wa_fetch_stale(&f, WA_CH_STABLE));
    wa_fetch_abort(&f, WA_WHY_CANCEL);
    (void)wa_fetch_start(&f, WA_JOB_DOWNLOAD, V(0, 21, 0), WA_CH_TESTING,
                         true, WA_WHY_NONE, 0u);
    CHECK(wa_fetch_has_found(&f));       /* the release it fetches */
    wa_fetch_abort(&f, WA_WHY_CANCEL);
    CHECK(!wa_fetch_stale(&f, WA_CH_STABLE));
    /* A channel out of range is STABLE. */
    (void)wa_fetch_start(&f, WA_JOB_CHECK, V(0, 0, 0), (wa_channel_t)7,
                         true, WA_WHY_NONE, 0u);
    CHECK_EQ(f.channel, WA_CH_STABLE);
}

int main(void)
{
    RUN(the_p4_panel_is_offered_0_23_0_and_later_on_either_channel);
    /* Every case after this one holds the ESP32-S3 panel's 0.22 line. */
    fw_release_set_line_capped(true);
    RUN(testing_asks_the_api_with_its_headers_and_stops_at_the_10th);
    RUN(stable_keeps_the_latest_redirect);
    RUN(stable_past_the_line_reads_the_list_once_for_the_lines_newest);
    RUN(only_a_newer_release_of_the_line_is_offered_on_either_channel);
    RUN(the_request_limit_is_its_own_result);
    RUN(the_answers_size_is_announced_and_within_its_bound);
    RUN(a_chunked_answer_is_read_to_its_releases_end);
    RUN(a_chunked_answer_is_held_to_the_scan_bound);
    RUN(a_chunked_answer_that_is_empty_cut_or_overruns);
    RUN(a_tag_out_of_form_has_its_own_reason);
    RUN(a_body_that_breaks_off_lies_or_overruns);
    RUN(the_api_check_holds_its_budget_across_the_wrap);
    RUN(an_abort_ends_the_api_read);
    RUN(an_offer_holds_only_for_the_channel_it_was_found_on);
    return test_summary("wa_fetch_channel");
}
