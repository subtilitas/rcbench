/*
 * The panel asking GitHub for its newest release, and fetching it.
 *
 * The failures under test: a job that holds the radio past its budget, one
 * that goes on after STOP, ARMED, the supply, a log run, WI-FI or
 * CANCEL, a late report from the runner that revives an ended job, and a
 * download that leaves an image staged that GitHub did not vouch for -- a
 * redirect elsewhere, a size over the slot, a body shorter than announced,
 * a SHA256SUMS missing, out of form or without the file's line, a hash that
 * differs.  The HTTP (hypertext transfer protocol) client here is scripted;
 * the clock is the test's.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "fw_fixture.h"
#include "fw_sha256.h"
#include "tick_wrap.h"
#include "wa_fetch.h"

#define SLOT 4063232u
/* The ESP32-P4 panel's application slot, ota_0 and ota_1 of
 * firmware/panel/partitions.csv: 8,126,464 bytes. */
#define SLOT_P4 0x7C0000u

/* ------------------------------------------------- the scripted client */

typedef struct {
    int         status;
    int64_t     length;       /* -1: none                               */
    bool        chunked;
    const char *loc;          /* NULL: none                             */
    bool        loc_cut;
    const uint8_t *body;
    uint32_t    body_n;       /* bytes it actually sends                */
    bool        no_connect;   /* open() fails                           */
    bool        read_breaks;  /* read() fails after half the body       */
    bool        overrun;      /* read() hands back more than asked      */
    uint32_t    open_ms;      /* the clock moves this much in open()    */
    uint32_t    read_ms;      /* and in each read()                     */
} answer_t;

#define ANSWERS 8

typedef struct {
    answer_t a[ANSWERS];
    unsigned n, at;
    const answer_t *cur;
    uint32_t sent;
    char     urls[ANSWERS][FW_REL_URL_MAX + 1u];
    unsigned opens, closes;
    unsigned api_opens;          /* opens with GitHub's API headers     */
    uint32_t last_timeout;
    uint32_t now;
    bool     stop;
    unsigned stop_after_reads;   /* 0: never                            */
    unsigned reads;
    wa_phase_t phases[16];
    unsigned   phase_n;
    uint32_t   done, total;
    uint8_t   *alloc_buf;
    bool       alloc_fails;
    uint32_t   alloc_bytes;
} fake_t;

static fake_t g;

static bool f_open(void *ctx, const char *url, uint32_t timeout_ms,
                   bool api, wa_http_head_t *head, char *loc, size_t loc_cap)
{
    (void)ctx;
    g.last_timeout = timeout_ms;
    g.api_opens += api ? 1u : 0u;
    if (g.opens < ANSWERS) {
        snprintf(g.urls[g.opens], sizeof(g.urls[0]), "%s", url);
    }
    ++g.opens;
    if (g.at >= g.n) {
        return false;
    }
    g.cur = &g.a[g.at++];
    g.sent = 0u;
    g.now += g.cur->open_ms;
    if (g.cur->no_connect) {
        return false;
    }
    head->status = g.cur->status;
    head->length = g.cur->length;
    head->chunked = g.cur->chunked;
    head->loc_n = 0u;
    head->loc_cut = g.cur->loc_cut;
    if (g.cur->loc != NULL) {
        const size_t n = strlen(g.cur->loc);
        head->loc_n = (n > loc_cap) ? loc_cap : n;
        head->loc_cut = head->loc_cut || n > loc_cap;
        memcpy(loc, g.cur->loc, head->loc_n);
    }
    return true;
}

static int32_t f_read(void *ctx, uint8_t *dst, uint32_t n,
                      uint32_t timeout_ms)
{
    (void)ctx;
    g.last_timeout = timeout_ms;
    ++g.reads;
    if (g.stop_after_reads != 0u && g.reads >= g.stop_after_reads) {
        g.stop = true;
    }
    g.now += g.cur->read_ms;
    if (g.cur->read_breaks && g.sent >= g.cur->body_n / 2u) {
        return -1;
    }
    const uint32_t left = g.cur->body_n - g.sent;
    uint32_t k = (n < left) ? n : left;
    if (g.cur->overrun && k > 0u) {
        memcpy(dst, g.cur->body + g.sent, k);
        g.sent += k;
        return (int32_t)k + 1;
    }
    memcpy(dst, g.cur->body + g.sent, k);
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
    return g.now;
}

static bool f_stopped(void *ctx)
{
    (void)ctx;
    return g.stop;
}

static void f_report(void *ctx, wa_phase_t ph, uint32_t done, uint32_t total)
{
    (void)ctx;
    if (g.phase_n == 0u || g.phases[g.phase_n - 1u] != ph) {
        if (g.phase_n < 16u) {
            g.phases[g.phase_n++] = ph;
        }
    }
    g.done = done;
    g.total = total;
}

static uint8_t *f_alloc(void *ctx, uint32_t bytes)
{
    (void)ctx;
    g.alloc_bytes = bytes;
    return g.alloc_fails ? NULL : g.alloc_buf;
}

static const wa_http_t k_http = { f_open, f_read, f_close, NULL };

static char     g_url[FW_REL_URL_MAX + 1u];
static char     g_loc[FW_REL_URL_MAX + 1u];
static uint8_t  g_io[4096];
static char     g_sums[FW_REL_SUMS_MAX];
static fw_upload_t g_up;
static uint8_t *g_img;
static uint32_t g_img_n;

static wa_run_t run_for(uint32_t start, uint32_t budget)
{
    wa_run_t r;
    memset(&r, 0, sizeof(r));
    r.http = &k_http;
    r.now = f_now;
    r.stopped = f_stopped;
    r.report = f_report;
    r.alloc = f_alloc;
    r.start_ms = start;
    r.budget_ms = budget;
    r.slot = SLOT;
    r.running = (fw_version_t){ 0u, 19u, 0u };
    r.running_ok = true;
    r.url = g_url;
    r.loc = g_loc;
    r.io = g_io;
    r.io_cap = sizeof(g_io);
    r.sums = g_sums;
    r.up = &g_up;
    return r;
}

static void fresh(uint32_t now)
{
    uint8_t *keep = g.alloc_buf;
    memset(&g, 0, sizeof(g));
    g.alloc_buf = keep;
    g.now = now;
    memset(&g_up, 0, sizeof(g_up));
}

static void answer(answer_t a)
{
    g.a[g.n++] = a;
}

static answer_t latest(const char *loc)
{
    return (answer_t){ .status = 302, .length = 0, .loc = loc };
}

#define TAG FW_REL_TAG_PREFIX

/* ------------------------------------------------------------ CHECK */

TEST_CASE(a_newer_release_is_found)
{
    fresh(1000u);
    answer(latest(TAG "v0.20.0"));
    const wa_run_t r = run_for(1000u, WA_FETCH_CHECK_MS);
    wa_fetch_res_t out;
    wa_run_check(&r, &out);
    CHECK_EQ(out.got, WA_GOT_NEWER);
    CHECK_EQ(out.found.minor, 20u);
    CHECK_STR_EQ(g.urls[0], FW_REL_LATEST_URL);
    CHECK_EQ(g.api_opens, 0u);
    /* One request, closed; only its head was read. */
    CHECK_EQ(g.opens, 1u);
    CHECK_EQ(g.closes, 1u);
    CHECK_EQ(g.reads, 0u);
    CHECK_EQ(g.last_timeout, WA_FETCH_OP_MS);
}

TEST_CASE(equal_and_older_releases_are_current)
{
    const char *const locs[] = { TAG "v0.19.0", TAG "v0.18.9", TAG "v0.0.0" };
    for (size_t i = 0; i < 3u; ++i) {
        fresh(0u);
        answer(latest(locs[i]));
        const wa_run_t r = run_for(0u, WA_FETCH_CHECK_MS);
        wa_fetch_res_t out;
        wa_run_check(&r, &out);
        CHECK_EQ(out.got, WA_GOT_CURRENT);
    }
    /* Each part counts: a patch, a minor, a major ahead. */
    const char *const newer[] = { TAG "v0.19.1", TAG "v0.20.0",
                                  TAG "v1.0.0" };
    for (size_t i = 0; i < 3u; ++i) {
        fresh(0u);
        answer(latest(newer[i]));
        const wa_run_t r = run_for(0u, WA_FETCH_CHECK_MS);
        wa_fetch_res_t out;
        wa_run_check(&r, &out);
        CHECK_EQ(out.got, WA_GOT_NEWER);
    }
}

TEST_CASE(a_hostile_location_is_an_unexpected_answer)
{
    static char big[FW_REL_URL_MAX + 100u];
    memset(big, 'a', sizeof(big) - 1u);
    const struct { const char *loc; fw_rel_err_t e; } cases[] = {
        { NULL, FW_REL_MISSING },
        { "", FW_REL_MISSING },
        { big, FW_REL_LONG },
        { "https://evil.com/subtilitas/rcbench/releases/tag/v9.9.9",
          FW_REL_PLACE },
        { TAG "v9.9.9x", FW_REL_VERSION },
        { TAG "v65536.0.0", FW_REL_VERSION },
        { TAG "v01.0.0", FW_REL_VERSION },
        { TAG "v1.2.3\r\nX: 1", FW_REL_CHAR },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        fresh(0u);
        answer(latest(cases[i].loc));
        const wa_run_t r = run_for(0u, WA_FETCH_CHECK_MS);
        wa_fetch_res_t out;
        wa_run_check(&r, &out);
        CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
        CHECK_EQ(out.rel, cases[i].e);
    }
}

TEST_CASE(a_status_other_than_a_redirect_is_refused)
{
    const int statuses[] = { 200, 404, 403, 429, 500, 0 };
    for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); ++i) {
        fresh(0u);
        answer((answer_t){ .status = statuses[i], .length = 10,
                           .loc = TAG "v9.0.0" });
        const wa_run_t r = run_for(0u, WA_FETCH_CHECK_MS);
        wa_fetch_res_t out;
        wa_run_check(&r, &out);
        CHECK_EQ(out.got, WA_GOT_REFUSED);
        CHECK_EQ(out.status, statuses[i]);
    }
}

TEST_CASE(no_connection_is_connect_or_the_budget)
{
    fresh(0u);
    answer((answer_t){ .no_connect = true, .open_ms = 3000u });
    wa_run_t r = run_for(0u, WA_FETCH_CHECK_MS);
    wa_fetch_res_t out;
    wa_run_check(&r, &out);
    CHECK_EQ(out.got, WA_GOT_CONNECT);
    CHECK_EQ(g.closes, 1u);
    /* A connection that waited out the rest of the budget timed out. */
    fresh(0u);
    answer((answer_t){ .no_connect = true, .open_ms = 30000u });
    r = run_for(0u, WA_FETCH_CHECK_MS);
    wa_run_check(&r, &out);
    CHECK_EQ(out.got, WA_GOT_TIMEOUT);
}

static void check_waits_within_the_budget(uint32_t t0)
{
    /* The join took 25 s: the request waits 5 s at most. */
    fresh(t0 + 25000u);
    answer(latest(TAG "v1.0.0"));
    wa_run_t r = run_for(t0, WA_FETCH_CHECK_MS);
    wa_fetch_res_t out;
    wa_run_check(&r, &out);
    CHECK_EQ(out.got, WA_GOT_NEWER);
    CHECK_EQ(g.last_timeout, 5000u);
    /* One ms left: one ms. */
    fresh(t0 + 29999u);
    answer(latest(TAG "v1.0.0"));
    wa_run_check(&r, &out);
    CHECK_EQ(g.last_timeout, 1u);
    /* None left: no request at all. */
    fresh(t0 + 30000u);
    answer(latest(TAG "v1.0.0"));
    wa_run_check(&r, &out);
    CHECK_EQ(out.got, WA_GOT_TIMEOUT);
    CHECK_EQ(g.opens, 0u);
}

TEST_CASE(the_check_waits_within_its_budget_across_the_wrap)
{
    at_tick_0_and_before_the_wrap(check_waits_within_the_budget, 27000u);
}

TEST_CASE(a_check_stopped_before_the_request_sends_none)
{
    fresh(0u);
    g.stop = true;
    answer(latest(TAG "v1.0.0"));
    const wa_run_t r = run_for(0u, WA_FETCH_CHECK_MS);
    wa_fetch_res_t out;
    wa_run_check(&r, &out);
    CHECK_EQ(out.got, WA_GOT_ABORTED);
    CHECK_EQ(g.opens, 0u);
}

/* --------------------------------------------------------- DOWNLOAD */

static char g_sums_text[512];
static const fw_version_t V20 = { 0u, 20u, 0u };

/* The image of release 0.20.0 and a SHA256SUMS that names it. */
static void make_release(const char *version, bool right_hash)
{
    fw_fix_t f = fw_fix_default();
    f.version = version;
    g_img_n = fw_fix_make(&f, g_img);
    uint8_t h[FW_SHA256_BYTES];
    fw_sha256(g_img, g_img_n, h);
    if (!right_hash) {
        h[5] ^= 0x01u;
    }
    char hex[2u * FW_SHA256_BYTES + 1u];
    for (unsigned i = 0; i < FW_SHA256_BYTES; ++i) {
        snprintf(hex + 2u * i, 3u, "%02x", h[i]);
    }
    snprintf(g_sums_text, sizeof(g_sums_text),
             "%s  ./rcbench-coproc-v0.20.0.zip\n%s  ./rcbench-panel-p4-v0.20.0"
             ".bin\n", "00000000000000000000000000000000"
                       "00000000000000000000000000000000", hex);
}

#define ASSET "https://release-assets.githubusercontent.com/github-" \
              "production-release-asset/1?sp=r&sig=abc"

static answer_t to_asset(void)
{
    return (answer_t){ .status = 302, .length = 0, .loc = ASSET };
}

static answer_t sums_body(void)
{
    const uint32_t n = (uint32_t)strlen(g_sums_text);
    return (answer_t){ .status = 200, .length = n,
                       .body = (const uint8_t *)g_sums_text, .body_n = n };
}

static answer_t image_body(void)
{
    return (answer_t){ .status = 200, .length = g_img_n, .body = g_img,
                       .body_n = g_img_n };
}

static void script_good(void)
{
    answer(to_asset());
    answer(sums_body());
    answer(to_asset());
    answer(image_body());
}

static wa_fetch_res_t download(uint32_t start)
{
    const wa_run_t r = run_for(start, WA_FETCH_DOWNLOAD_MS);
    wa_fetch_res_t out;
    wa_run_download(&r, V20, &out);
    return out;
}

/* As download(), into a slot of @p slot bytes. */
static wa_fetch_res_t download_slot(uint32_t start, uint32_t slot)
{
    wa_run_t r = run_for(start, WA_FETCH_DOWNLOAD_MS);
    r.slot = slot;
    wa_fetch_res_t out;
    wa_run_download(&r, V20, &out);
    return out;
}

TEST_CASE(the_p4_slot_takes_its_own_size_and_refuses_a_byte_more)
{
    fresh(0u);
    make_release("0.20.0", true);
    answer(to_asset());
    answer(sums_body());
    answer((answer_t){ .status = 200, .length = (int64_t)SLOT_P4 + 1 });
    wa_fetch_res_t out = download_slot(0u, SLOT_P4);
    CHECK_EQ(out.rel, FW_REL_TOO_BIG);
    CHECK_EQ(g.alloc_bytes, 0u);
    /* The slot's size itself gets its buffer; the body ends short. */
    fresh(0u);
    answer(sums_body());
    answer((answer_t){ .status = 200, .length = SLOT_P4, .body = g_img,
                       .body_n = 10u });
    out = download_slot(0u, SLOT_P4);
    CHECK_EQ(g.alloc_bytes, SLOT_P4);
    CHECK_EQ(out.rel, FW_REL_SHORT);
}

TEST_CASE(a_release_downloads_checked_and_staged)
{
    fresh(0u);
    make_release("0.20.0", true);
    script_good();
    const wa_fetch_res_t out = download(0u);
    CHECK_EQ(out.got, WA_GOT_STAGED);
    CHECK_EQ(g_up.state, FW_UP_STAGED);
    CHECK_EQ(g_up.bytes, g_img_n);
    CHECK_STR_EQ(g_up.info.version_text, "0.20.0");
    CHECK_EQ(memcmp(g.alloc_buf, g_img, g_img_n), 0);
    /* The buffer is the size announced, no more. */
    CHECK_EQ(g.alloc_bytes, g_img_n);
    CHECK_STR_EQ(g.urls[0], "https://github.com/subtilitas/rcbench/releases/"
                            "download/v0.20.0/SHA256SUMS");
    CHECK_STR_EQ(g.urls[1], ASSET);
    CHECK_STR_EQ(g.urls[2], "https://github.com/subtilitas/rcbench/releases/"
                            "download/v0.20.0/rcbench-panel-p4-v0.20.0.bin");
    CHECK_EQ(g.opens, 4u);
    CHECK_EQ(g.closes, 4u);
    /* The phases in order, and the bytes of the last report. */
    CHECK_EQ(g.phase_n, 3u);
    CHECK_EQ(g.phases[0], WA_PH_SUMS);
    CHECK_EQ(g.phases[1], WA_PH_IMAGE);
    CHECK_EQ(g.phases[2], WA_PH_CHECKING);
    CHECK_EQ(g.done, g_img_n);
    CHECK_EQ(g.total, g_img_n);
}

TEST_CASE(a_hash_other_than_sha256sums_stages_nothing)
{
    fresh(0u);
    make_release("0.20.0", false);
    script_good();
    const wa_fetch_res_t out = download(0u);
    CHECK_EQ(out.got, WA_GOT_HASH);
    CHECK(g_up.state != FW_UP_STAGED);
}

TEST_CASE(an_image_that_says_another_version_stages_nothing)
{
    fresh(0u);
    make_release("0.19.5", true);   /* hashed right, named 0.20.0 */
    script_good();
    const wa_fetch_res_t out = download(0u);
    CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
    CHECK_EQ(out.rel, FW_REL_VERSION);
    CHECK(g_up.state != FW_UP_STAGED);
}

TEST_CASE(an_image_the_checks_refuse_stages_nothing)
{
    fresh(0u);
    fw_fix_t f = fw_fix_default();
    f.version = "0.20.0";
    f.chip = 2u;                    /* another chip */
    g_img_n = fw_fix_make(&f, g_img);
    uint8_t h[FW_SHA256_BYTES];
    fw_sha256(g_img, g_img_n, h);
    char hex[2u * FW_SHA256_BYTES + 1u];
    for (unsigned i = 0; i < FW_SHA256_BYTES; ++i) {
        snprintf(hex + 2u * i, 3u, "%02x", h[i]);
    }
    snprintf(g_sums_text, sizeof(g_sums_text),
             "%s  ./rcbench-panel-p4-v0.20.0.bin\n", hex);
    script_good();
    const wa_fetch_res_t out = download(0u);
    CHECK_EQ(out.got, WA_GOT_IMAGE);
    CHECK_EQ(out.img, FW_ERR_CHIP);
    CHECK(g_up.state != FW_UP_STAGED);
}

TEST_CASE(sha256sums_missing_out_of_form_or_without_the_line)
{
    /* 404: the release has no SHA256SUMS. */
    fresh(0u);
    make_release("0.20.0", true);
    answer((answer_t){ .status = 404, .length = 9 });
    wa_fetch_res_t out = download(0u);
    CHECK_EQ(out.got, WA_GOT_SUMS);
    CHECK_EQ(out.rel, FW_REL_STATUS);
    CHECK_EQ(out.status, 404);
    CHECK_EQ(g.opens, 1u);           /* the image was not asked for */
    /* Out of form. */
    fresh(0u);
    snprintf(g_sums_text, sizeof(g_sums_text), "not a sums file\n");
    answer(to_asset());
    answer(sums_body());
    out = download(0u);
    CHECK_EQ(out.got, WA_GOT_SUMS);
    CHECK_EQ(out.rel, FW_REL_SUMS_BAD);
    CHECK_EQ(g.opens, 2u);
    /* Without the image's line. */
    fresh(0u);
    make_release("0.20.0", true);
    snprintf(g_sums_text, sizeof(g_sums_text), "%064d  ./other.bin\n", 0);
    answer(to_asset());
    answer(sums_body());
    out = download(0u);
    CHECK_EQ(out.got, WA_GOT_SUMS);
    CHECK_EQ(out.rel, FW_REL_SUMS_NONE);
    /* Over its bound, without a length, empty. */
    fresh(0u);
    answer((answer_t){ .status = 200, .length = FW_REL_SUMS_MAX + 1 });
    out = download(0u);
    CHECK_EQ(out.got, WA_GOT_SUMS);
    CHECK_EQ(out.rel, FW_REL_TOO_BIG);
    CHECK_EQ(g.reads, 0u);
    fresh(0u);
    answer((answer_t){ .status = 200, .length = -1, .chunked = true });
    out = download(0u);
    CHECK_EQ(out.got, WA_GOT_SUMS);
    CHECK_EQ(out.rel, FW_REL_NO_LENGTH);
    fresh(0u);
    answer((answer_t){ .status = 200, .length = 0 });
    out = download(0u);
    CHECK_EQ(out.got, WA_GOT_SUMS);
    CHECK_EQ(out.rel, FW_REL_EMPTY);
}

TEST_CASE(a_redirect_chain_is_held_to_its_hosts_and_its_length)
{
    /* To another host. */
    fresh(0u);
    make_release("0.20.0", true);
    answer((answer_t){ .status = 302, .loc = "https://evil.com/x" });
    wa_fetch_res_t out = download(0u);
    CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
    CHECK_EQ(out.rel, FW_REL_HOST);
    CHECK_EQ(g.opens, 1u);
    /* Without a Location. */
    fresh(0u);
    answer((answer_t){ .status = 302 });
    out = download(0u);
    CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
    CHECK_EQ(out.rel, FW_REL_MISSING);
    /* Plain HTTP. */
    fresh(0u);
    answer((answer_t){ .status = 301, .loc = "http://github.com/x" });
    out = download(0u);
    CHECK_EQ(out.rel, FW_REL_PLACE);
    /* FW_REL_HOPS_MAX redirects are followed; one more is not. */
    fresh(0u);
    for (unsigned i = 0; i < FW_REL_HOPS_MAX; ++i) {
        answer(to_asset());
    }
    answer(sums_body());
    answer(to_asset());
    answer(image_body());
    out = download(0u);
    CHECK_EQ(out.got, WA_GOT_STAGED);
    fresh(0u);
    for (unsigned i = 0; i <= FW_REL_HOPS_MAX; ++i) {
        answer(to_asset());
    }
    out = download(0u);
    CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
    CHECK_EQ(out.rel, FW_REL_HOPS);
    CHECK_EQ(g.opens, FW_REL_HOPS_MAX + 1u);
    /* A Location longer than the buffer. */
    fresh(0u);
    answer((answer_t){ .status = 302, .loc = ASSET, .loc_cut = true });
    out = download(0u);
    CHECK_EQ(out.rel, FW_REL_LONG);
}

TEST_CASE(an_image_over_the_slot_is_refused_before_a_buffer)
{
    fresh(0u);
    make_release("0.20.0", true);
    answer(to_asset());
    answer(sums_body());
    answer((answer_t){ .status = 200, .length = (int64_t)SLOT + 1 });
    wa_fetch_res_t out = download(0u);
    CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
    CHECK_EQ(out.rel, FW_REL_TOO_BIG);
    CHECK_EQ(g.alloc_bytes, 0u);
    /* Without a length, chunked: refused the same way. */
    fresh(0u);
    answer(sums_body());
    answer((answer_t){ .status = 200, .length = -1 });
    out = download(0u);
    CHECK_EQ(out.rel, FW_REL_NO_LENGTH);
    CHECK_EQ(g.alloc_bytes, 0u);
    fresh(0u);
    answer(sums_body());
    answer((answer_t){ .status = 200, .length = 1000, .chunked = true });
    out = download(0u);
    CHECK_EQ(out.rel, FW_REL_NO_LENGTH);
    /* The slot's size itself gets its buffer. */
    fresh(0u);
    answer(sums_body());
    answer((answer_t){ .status = 200, .length = SLOT, .body = g_img,
                       .body_n = 10u });
    out = download(0u);
    CHECK_EQ(g.alloc_bytes, SLOT);
    CHECK_EQ(out.rel, FW_REL_SHORT);
    /* No memory for it. */
    fresh(0u);
    g.alloc_fails = true;
    answer(sums_body());
    answer(image_body());
    out = download(0u);
    CHECK_EQ(out.got, WA_GOT_MEMORY);
}

TEST_CASE(a_content_length_that_lies_stages_nothing)
{
    /* Fewer bytes than announced. */
    fresh(0u);
    make_release("0.20.0", true);
    answer(sums_body());
    answer((answer_t){ .status = 200, .length = g_img_n, .body = g_img,
                       .body_n = g_img_n - 1u });
    wa_fetch_res_t out = download(0u);
    CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
    CHECK_EQ(out.rel, FW_REL_SHORT);
    CHECK(g_up.state != FW_UP_STAGED);
    /* Fewer announced than the image: the hash of the part differs. */
    fresh(0u);
    answer(sums_body());
    answer((answer_t){ .status = 200, .length = g_img_n - 100u,
                       .body = g_img, .body_n = g_img_n });
    out = download(0u);
    CHECK_EQ(out.got, WA_GOT_HASH);
    /* A client that hands back more than asked for. */
    fresh(0u);
    answer(sums_body());
    answer((answer_t){ .status = 200, .length = g_img_n, .body = g_img,
                       .body_n = g_img_n, .overrun = true });
    out = download(0u);
    CHECK_EQ(out.rel, FW_REL_OVERRUN);
    CHECK(g_up.state != FW_UP_STAGED);
    /* A connection that breaks off half way. */
    fresh(0u);
    answer(sums_body());
    answer((answer_t){ .status = 200, .length = g_img_n, .body = g_img,
                       .body_n = g_img_n, .read_breaks = true });
    out = download(0u);
    CHECK_EQ(out.got, WA_GOT_CONNECT);
    CHECK(g_up.state != FW_UP_STAGED);
    CHECK_EQ(g.closes, g.opens);
}

TEST_CASE(a_refused_image_request_names_its_status)
{
    fresh(0u);
    make_release("0.20.0", true);
    answer(sums_body());
    answer((answer_t){ .status = 404, .length = 9 });
    const wa_fetch_res_t out = download(0u);
    CHECK_EQ(out.got, WA_GOT_REFUSED);
    CHECK_EQ(out.status, 404);
}

TEST_CASE(a_download_stopped_at_each_phase_stages_nothing)
{
    /* Before the first request, during SHA256SUMS, during the image, at
     * its last read. */
    make_release("0.20.0", true);
    const unsigned image_reads = (g_img_n + 4095u) / 4096u;
    const unsigned at[] = { 0u, 1u, 2u, 1u + image_reads };
    for (size_t i = 0; i < sizeof(at) / sizeof(at[0]); ++i) {
        fresh(0u);
        g.stop = (at[i] == 0u);
        g.stop_after_reads = at[i];
        answer(sums_body());
        answer(image_body());
        const wa_fetch_res_t out = download(0u);
        CHECK_EQ(out.got, WA_GOT_ABORTED);
        CHECK(g_up.state != FW_UP_STAGED);
        CHECK_EQ(g.closes, g.opens);
    }
}

static void download_times_out_across_the_wrap(uint32_t t0)
{
    /* Each read takes 1.5 s; the budget runs out inside the image of 3
     * pieces, after the second. */
    fresh(t0);
    make_release("0.20.0", true);
    answer(sums_body());
    answer_t slow = image_body();
    slow.read_ms = 1500u;
    answer(slow);
    const wa_run_t r = run_for(t0, 2500u);
    wa_fetch_res_t out;
    wa_run_download(&r, V20, &out);
    CHECK_EQ(out.got, WA_GOT_TIMEOUT);
    CHECK_EQ(g.reads, 1u + 2u);
    CHECK(g_up.state != FW_UP_STAGED);
    CHECK(g.last_timeout <= 2500u);
    CHECK((uint32_t)(g.now - t0) < 4000u);
    /* The image complete, but past the budget: not offered either. */
    fresh(t0);
    answer(sums_body());
    slow.read_ms = 900u;
    answer(slow);
    const wa_run_t r2 = run_for(t0, 2500u);
    wa_run_download(&r2, V20, &out);
    CHECK_EQ(g.reads, 1u + (g_img_n + 4095u) / 4096u);
    CHECK_EQ(out.got, WA_GOT_TIMEOUT);
    CHECK(g_up.state != FW_UP_STAGED);
}

TEST_CASE(a_download_times_out_across_the_wrap)
{
    at_tick_0_and_before_the_wrap(download_times_out_across_the_wrap, 1500u);
}

/* ------------------------------------------------------------ the job */

static wa_t joined(void)
{
    wa_t w;
    wa_init(&w);
    w.mode = WA_MODE_FETCH;
    w.state = WA_ST_JOINED;
    return w;
}

static wa_t joining(void)
{
    wa_t w = joined();
    w.state = WA_ST_JOINING;
    return w;
}

TEST_CASE(a_job_needs_a_network_and_an_open_gate)
{
    wa_fetch_t f;
    wa_fetch_init(&f, (fw_version_t){ 0u, 19u, 0u });
    CHECK_EQ(f.phase, WA_PH_IDLE);
    CHECK(!wa_fetch_busy(&f));
    CHECK_EQ(wa_fetch_start(&f, WA_JOB_CHECK, V20, WA_CH_STABLE, false, WA_WHY_NONE, 0u),
             WA_GOT_NO_NET);
    CHECK(!f.owe_off);           /* the radio never came on */
    CHECK_EQ(wa_fetch_start(&f, WA_JOB_CHECK, V20, WA_CH_STABLE, true, WA_WHY_ARMED, 0u),
             WA_GOT_BLOCKED);
    CHECK_EQ(f.why, WA_WHY_ARMED);
    CHECK_EQ(wa_fetch_start(&f, WA_JOB_CHECK, V20, WA_CH_STABLE, true, WA_WHY_NONE, 0u),
             WA_GOT_NONE);
    CHECK_EQ(f.phase, WA_PH_JOINING);
    /* A second start while one runs changes nothing. */
    const uint32_t gen = f.gen;
    CHECK_EQ(wa_fetch_start(&f, WA_JOB_DOWNLOAD, V20, WA_CH_STABLE, true, WA_WHY_NONE,
                            5u), WA_GOT_BLOCKED);
    CHECK_EQ(f.gen, gen);
    CHECK_EQ(f.job, WA_JOB_CHECK);
    CHECK_EQ(f.phase, WA_PH_JOINING);
}

TEST_CASE(the_gate_names_the_first_reason)
{
    CHECK_EQ(wa_fetch_gate(false, false, false, false, false), WA_WHY_NONE);
    CHECK_EQ(wa_fetch_gate(true, true, true, true, true), WA_WHY_ARMED);
    CHECK_EQ(wa_fetch_gate(false, true, true, true, true), WA_WHY_SUPPLY);
    CHECK_EQ(wa_fetch_gate(false, false, true, true, true), WA_WHY_RUN);
    CHECK_EQ(wa_fetch_gate(false, false, false, true, true), WA_WHY_INSTALL);
    CHECK_EQ(wa_fetch_gate(false, false, false, true, false), WA_WHY_COPROC);
}

TEST_CASE(a_join_hands_the_job_to_the_runner_once)
{
    wa_fetch_t f;
    wa_fetch_init(&f, (fw_version_t){ 0u, 19u, 0u });
    (void)wa_fetch_start(&f, WA_JOB_CHECK, V20, WA_CH_STABLE, true, WA_WHY_NONE, 0u);
    const wa_t jn = joining();
    wa_fetch_tick(&f, 100u, WA_WHY_NONE, &jn);
    CHECK(!wa_fetch_take_run(&f));
    const wa_t w = joined();
    wa_fetch_tick(&f, 200u, WA_WHY_NONE, &w);
    CHECK_EQ(f.phase, WA_PH_ASKING);
    CHECK(wa_fetch_take_run(&f));
    wa_fetch_tick(&f, 300u, WA_WHY_NONE, &w);
    CHECK(!wa_fetch_take_run(&f));
    /* The runner's end. */
    wa_fetch_res_t r = { .got = WA_GOT_NEWER, .found = V20 };
    wa_fetch_on_end(&f, f.gen, &r);
    CHECK_EQ(f.phase, WA_PH_ENDED);
    CHECK_EQ(f.got, WA_GOT_NEWER);
    CHECK_EQ(f.found.minor, 20u);
    CHECK(wa_fetch_take_off(&f));
    CHECK(!wa_fetch_take_off(&f));
    /* A download starts with SHA256SUMS. */
    (void)wa_fetch_start(&f, WA_JOB_DOWNLOAD, V20, WA_CH_STABLE, true, WA_WHY_NONE, 400u);
    CHECK_EQ(f.found.minor, 20u);
    wa_fetch_tick(&f, 500u, WA_WHY_NONE, &w);
    CHECK_EQ(f.phase, WA_PH_SUMS);
}

static void check_budget_across_the_wrap(uint32_t t0)
{
    wa_fetch_t f;
    wa_fetch_init(&f, (fw_version_t){ 0u, 19u, 0u });
    (void)wa_fetch_start(&f, WA_JOB_CHECK, V20, WA_CH_STABLE, true, WA_WHY_NONE, t0);
    const wa_t w = joined();
    wa_fetch_tick(&f, t0 + 1u, WA_WHY_NONE, &w);
    CHECK(wa_fetch_busy(&f));
    CHECK_EQ(wa_fetch_left(&f, t0 + 1u), WA_FETCH_CHECK_MS - 1u);
    wa_fetch_tick(&f, t0 + WA_FETCH_CHECK_MS - 1u, WA_WHY_NONE, &w);
    CHECK(wa_fetch_busy(&f));
    CHECK_EQ(wa_fetch_left(&f, t0 + WA_FETCH_CHECK_MS - 1u), 1u);
    wa_fetch_tick(&f, t0 + WA_FETCH_CHECK_MS, WA_WHY_NONE, &w);
    CHECK(!wa_fetch_busy(&f));
    CHECK_EQ(f.got, WA_GOT_TIMEOUT);
    CHECK_EQ(f.budget_ms, WA_FETCH_CHECK_MS);
    CHECK(wa_fetch_take_off(&f));
    CHECK_EQ(wa_fetch_left(&f, t0 + WA_FETCH_CHECK_MS), 0u);
}

TEST_CASE(the_check_ends_at_30_s_across_the_wrap)
{
    at_tick_0_and_before_the_wrap(check_budget_across_the_wrap, 20000u);
}

static void download_budgets_across_the_wrap(uint32_t t0)
{
    wa_fetch_t f;
    wa_fetch_init(&f, (fw_version_t){ 0u, 19u, 0u });
    /* Still joining at WA_FETCH_JOIN_MS: over. */
    (void)wa_fetch_start(&f, WA_JOB_DOWNLOAD, V20, WA_CH_STABLE, true, WA_WHY_NONE, t0);
    const wa_t jn = joining();
    wa_fetch_tick(&f, t0 + WA_FETCH_JOIN_MS - 1u, WA_WHY_NONE, &jn);
    CHECK(wa_fetch_busy(&f));
    wa_fetch_tick(&f, t0 + WA_FETCH_JOIN_MS, WA_WHY_NONE, &jn);
    CHECK_EQ(f.got, WA_GOT_TIMEOUT);
    CHECK_EQ(f.budget_ms, WA_FETCH_JOIN_MS);
    /* Joined in time: WA_FETCH_DOWNLOAD_MS from the start. */
    (void)wa_fetch_start(&f, WA_JOB_DOWNLOAD, V20, WA_CH_STABLE, true, WA_WHY_NONE, t0);
    const wa_t w = joined();
    wa_fetch_tick(&f, t0 + 1000u, WA_WHY_NONE, &w);
    CHECK_EQ(f.phase, WA_PH_SUMS);
    wa_fetch_tick(&f, t0 + WA_FETCH_DOWNLOAD_MS - 1u, WA_WHY_NONE, &w);
    CHECK(wa_fetch_busy(&f));
    wa_fetch_tick(&f, t0 + WA_FETCH_DOWNLOAD_MS, WA_WHY_NONE, &w);
    CHECK_EQ(f.got, WA_GOT_TIMEOUT);
    CHECK_EQ(f.budget_ms, WA_FETCH_DOWNLOAD_MS);
    /* The runner's own time-out names the same budget, and a job that
     * ended otherwise names none. */
    (void)wa_fetch_start(&f, WA_JOB_DOWNLOAD, V20, WA_CH_STABLE, true, WA_WHY_NONE, t0);
    wa_fetch_tick(&f, t0 + 1000u, WA_WHY_NONE, &w);
    const wa_fetch_res_t to = { .got = WA_GOT_TIMEOUT };
    wa_fetch_on_end(&f, f.gen, &to);
    CHECK_EQ(f.budget_ms, WA_FETCH_DOWNLOAD_MS);
    (void)wa_fetch_start(&f, WA_JOB_DOWNLOAD, V20, WA_CH_STABLE, true, WA_WHY_NONE, t0);
    wa_fetch_abort(&f, WA_WHY_CANCEL);
    CHECK_EQ(f.budget_ms, 0u);
}

TEST_CASE(the_download_budgets_hold_across_the_wrap)
{
    at_tick_0_and_before_the_wrap(download_budgets_across_the_wrap, 100000u);
}

TEST_CASE(an_abort_ends_the_job_at_each_phase)
{
    const wa_why_t whys[] = { WA_WHY_ARMED, WA_WHY_SUPPLY, WA_WHY_RUN,
                              WA_WHY_STOP, WA_WHY_FILES, WA_WHY_COPROC,
                              WA_WHY_CANCEL };
    const wa_phase_t phases[] = { WA_PH_JOINING, WA_PH_SUMS, WA_PH_IMAGE,
                                  WA_PH_CHECKING };
    for (size_t p = 0; p < 4u; ++p) {
        for (size_t i = 0; i < sizeof(whys) / sizeof(whys[0]); ++i) {
            wa_fetch_t f;
            wa_fetch_init(&f, (fw_version_t){ 0u, 19u, 0u });
            (void)wa_fetch_start(&f, WA_JOB_DOWNLOAD, V20, WA_CH_STABLE, true,
                                 WA_WHY_NONE, 0u);
            const wa_t w = (phases[p] == WA_PH_JOINING) ? joining()
                                                        : joined();
            wa_fetch_tick(&f, 10u, WA_WHY_NONE, &w);
            wa_fetch_on_phase(&f, f.gen, phases[p]);
            CHECK_EQ(f.phase, phases[p]);
            (void)wa_fetch_take_off(&f);
            wa_fetch_tick(&f, 20u, whys[i], &w);
            CHECK_EQ(f.got, WA_GOT_ABORTED);
            CHECK_EQ(f.why, whys[i]);
            CHECK(wa_fetch_take_off(&f));
            /* What the runner says after it is dropped. */
            const wa_fetch_res_t r = { .got = WA_GOT_STAGED };
            wa_fetch_on_end(&f, f.gen, &r);
            wa_fetch_on_phase(&f, f.gen, WA_PH_IMAGE);
            wa_fetch_on_bytes(&f, f.gen, 5u, 10u);
            CHECK_EQ(f.got, WA_GOT_ABORTED);
            CHECK_EQ(f.phase, WA_PH_ENDED);
            CHECK(!wa_fetch_take_off(&f));
        }
    }
}

TEST_CASE(the_session_going_off_ends_the_job_with_its_reason)
{
    const struct { wa_off_t off; wa_got_t got; wa_why_t why; } cases[] = {
        { WA_OFF_JOIN, WA_GOT_JOIN, WA_WHY_NONE },
        { WA_OFF_FAILED, WA_GOT_RADIO, WA_WHY_NONE },
        { WA_OFF_ARMED, WA_GOT_ABORTED, WA_WHY_ARMED },
        { WA_OFF_SUPPLY, WA_GOT_ABORTED, WA_WHY_SUPPLY },
        { WA_OFF_RUN, WA_GOT_ABORTED, WA_WHY_RUN },
        { WA_OFF_STOP, WA_GOT_ABORTED, WA_WHY_STOP },
        { WA_OFF_INSTALL, WA_GOT_ABORTED, WA_WHY_INSTALL },
        { WA_OFF_IDLE, WA_GOT_TIMEOUT, WA_WHY_NONE },
        { WA_OFF_ASKED, WA_GOT_ABORTED, WA_WHY_CANCEL },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        wa_fetch_t f;
        wa_fetch_init(&f, (fw_version_t){ 0u, 19u, 0u });
        (void)wa_fetch_start(&f, WA_JOB_CHECK, V20, WA_CH_STABLE, true, WA_WHY_NONE, 0u);
        wa_t w = joining();
        w.state = WA_ST_OFF;
        w.off = cases[i].off;
        w.join_err = WA_JOIN_PASSWORD;
        wa_fetch_tick(&f, 10u, WA_WHY_NONE, &w);
        CHECK_EQ(f.got, cases[i].got);
        CHECK_EQ(f.why, cases[i].why);
        if (cases[i].got == WA_GOT_JOIN) {
            CHECK_EQ(f.join, WA_JOIN_PASSWORD);
        }
    }
    /* WI-FI took the radio: another mode. */
    wa_fetch_t f;
    wa_fetch_init(&f, (fw_version_t){ 0u, 19u, 0u });
    (void)wa_fetch_start(&f, WA_JOB_CHECK, V20, WA_CH_STABLE, true, WA_WHY_NONE, 0u);
    wa_t w = joined();
    w.mode = WA_MODE_AP;
    wa_fetch_tick(&f, 10u, WA_WHY_NONE, &w);
    CHECK_EQ(f.got, WA_GOT_ABORTED);
    CHECK_EQ(f.why, WA_WHY_FILES);
}

TEST_CASE(a_report_for_an_earlier_job_is_dropped)
{
    wa_fetch_t f;
    wa_fetch_init(&f, (fw_version_t){ 0u, 19u, 0u });
    (void)wa_fetch_start(&f, WA_JOB_CHECK, V20, WA_CH_STABLE, true, WA_WHY_NONE, 0u);
    const uint32_t old = f.gen;
    wa_fetch_abort(&f, WA_WHY_CANCEL);
    (void)wa_fetch_start(&f, WA_JOB_DOWNLOAD, V20, WA_CH_STABLE, true, WA_WHY_NONE, 10u);
    const wa_t w = joined();
    wa_fetch_tick(&f, 20u, WA_WHY_NONE, &w);
    const wa_fetch_res_t r = { .got = WA_GOT_CURRENT };
    wa_fetch_on_end(&f, old, &r);
    wa_fetch_on_bytes(&f, old, 1u, 2u);
    CHECK(wa_fetch_busy(&f));
    CHECK_EQ(f.done, 0u);
    wa_fetch_on_bytes(&f, f.gen, 1u, 2u);
    CHECK_EQ(f.done, 1u);
    CHECK_EQ(f.total, 2u);
    /* While joining no runner exists: a report then is dropped too. */
    wa_fetch_abort(&f, WA_WHY_CANCEL);
    (void)wa_fetch_start(&f, WA_JOB_DOWNLOAD, V20, WA_CH_STABLE, true, WA_WHY_NONE, 30u);
    wa_fetch_on_phase(&f, f.gen, WA_PH_IMAGE);
    CHECK_EQ(f.phase, WA_PH_JOINING);
    /* An abort with nothing running changes nothing. */
    wa_fetch_init(&f, (fw_version_t){ 0u, 19u, 0u });
    wa_fetch_abort(&f, WA_WHY_STOP);
    CHECK_EQ(f.phase, WA_PH_IDLE);
    CHECK(!wa_fetch_take_off(&f));
}

int main(void)
{
    g_img = malloc(SLOT_P4 + 64u);
    g.alloc_buf = malloc(SLOT_P4);
    REQUIRE_ALLOC(g_img);
    REQUIRE_ALLOC(g.alloc_buf);
    RUN(a_newer_release_is_found);
    RUN(equal_and_older_releases_are_current);
    RUN(a_hostile_location_is_an_unexpected_answer);
    RUN(a_status_other_than_a_redirect_is_refused);
    RUN(no_connection_is_connect_or_the_budget);
    RUN(the_check_waits_within_its_budget_across_the_wrap);
    RUN(a_check_stopped_before_the_request_sends_none);
    RUN(a_release_downloads_checked_and_staged);
    RUN(the_p4_slot_takes_its_own_size_and_refuses_a_byte_more);
    RUN(a_hash_other_than_sha256sums_stages_nothing);
    RUN(an_image_that_says_another_version_stages_nothing);
    RUN(an_image_the_checks_refuse_stages_nothing);
    RUN(sha256sums_missing_out_of_form_or_without_the_line);
    RUN(a_redirect_chain_is_held_to_its_hosts_and_its_length);
    RUN(an_image_over_the_slot_is_refused_before_a_buffer);
    RUN(a_content_length_that_lies_stages_nothing);
    RUN(a_refused_image_request_names_its_status);
    RUN(a_download_stopped_at_each_phase_stages_nothing);
    RUN(a_download_times_out_across_the_wrap);
    RUN(a_job_needs_a_network_and_an_open_gate);
    RUN(the_gate_names_the_first_reason);
    RUN(a_join_hands_the_job_to_the_runner_once);
    RUN(the_check_ends_at_30_s_across_the_wrap);
    RUN(the_download_budgets_hold_across_the_wrap);
    RUN(an_abort_ends_the_job_at_each_phase);
    RUN(the_session_going_off_ends_the_job_with_its_reason);
    RUN(a_report_for_an_earlier_job_is_dropped);
    free(g_img);
    free(g.alloc_buf);
    return test_summary("wa_fetch");
}
