/*
 * The panel fetching one device's image of its own release (DEVICE,
 * wa_run_device()): the C6 radio's, the M5Stack Dial's or a sensor node's
 * application, for an update that asked for it.
 *
 * The failures under test: a file that GitHub's SHA256SUMS does not vouch
 * for -- no SHA256SUMS, no line for the file, a hash that differs; a
 * redirect to a host not allowed; a file over the device's slot (at the
 * slot, and one byte over), empty, or shorter than announced; no memory;
 * the job ended from outside or past its budget, at each phase; and the
 * job's state: the SHA256SUMS phase after the join, the release kept,
 * a late report dropped.  The HTTP (hypertext transfer protocol) client
 * is scripted; the clock is the test's.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "fw_fixture.h"
#include "fw_sha256.h"
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
    r.running = (fw_version_t){ 0u, 22u, 0u };
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


/* ------------------------------------------------------------ DEVICE */

static char g_sums_text[1024];
static const fw_version_t V16 = { 0u, 23u, 16u };
#define NAME "rcbench-dial-0.23.16.bin"
/* The Dial's slot. */
#define DSLOT 0x300000u

/* A file of release 0.23.16 and a SHA256SUMS that names it among others. */
static void make_release(bool right_hash)
{
    fw_fix_t f = fw_fix_default();
    f.version = "0.23.16";
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
             "%s  ./rcbench-panel-p4-v0.23.16.bin\n%s  ./" NAME "\n"
             "%s  ./rcbench-dial-merged-0.23.16.bin\n",
             "00000000000000000000000000000000"
             "00000000000000000000000000000000", hex,
             "11111111111111111111111111111111"
             "11111111111111111111111111111111");
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

/* wa_run_device() of NAME into a slot of @p slot bytes from @p start. */
static wa_fetch_res_t fetch_dev(uint32_t start, uint32_t slot)
{
    wa_run_t r = run_for(start, WA_FETCH_DOWNLOAD_MS);
    r.slot = slot;
    r.name = NAME;
    r.up = NULL;
    wa_fetch_res_t out;
    wa_run_device(&r, V16, &out);
    return out;
}

TEST_CASE(a_devices_file_downloads_checked_into_its_own_buffer)
{
    fresh(0u);
    make_release(true);
    script_good();
    const wa_fetch_res_t out = fetch_dev(0u, DSLOT);
    CHECK_EQ(out.got, WA_GOT_STAGED);
    CHECK(out.buf == g.alloc_buf);
    CHECK_EQ(out.bytes, g_img_n);
    CHECK_EQ(g.alloc_bytes, g_img_n);
    CHECK_EQ(memcmp(out.buf, g_img, g_img_n), 0);
    CHECK_EQ(out.found.patch, 16u);
    CHECK_STR_EQ(g.urls[0], FW_REL_DL_PREFIX "v0.23.16/SHA256SUMS");
    CHECK_STR_EQ(g.urls[2], FW_REL_DL_PREFIX "v0.23.16/" NAME);
    CHECK_EQ(g.phases[0], WA_PH_SUMS);
    CHECK_EQ(g.phases[1], WA_PH_IMAGE);
    CHECK_EQ(g.phases[2], WA_PH_CHECKING);
    CHECK_EQ(g.opens, g.closes);
}

TEST_CASE(a_hash_other_than_the_sums_line_is_refused)
{
    fresh(0u);
    make_release(false);
    script_good();
    const wa_fetch_res_t out = fetch_dev(0u, DSLOT);
    CHECK_EQ(out.got, WA_GOT_HASH);
    CHECK_EQ(out.bytes, 0u);
    /* The buffer goes back to the caller to free. */
    CHECK(out.buf == g.alloc_buf);
}

TEST_CASE(a_release_without_the_files_line_or_sums_fetches_no_file)
{
    fresh(0u);
    snprintf(g_sums_text, sizeof(g_sums_text),
             "%s  ./rcbench-panel-p4-v0.23.16.bin\n",
             "00000000000000000000000000000000"
             "00000000000000000000000000000000");
    answer(sums_body());
    wa_fetch_res_t out = fetch_dev(0u, DSLOT);
    CHECK_EQ(out.got, WA_GOT_SUMS);
    CHECK_EQ(out.rel, FW_REL_SUMS_NONE);
    CHECK_EQ(g.opens, 1u);
    CHECK_EQ(g.alloc_bytes, 0u);
    CHECK(out.buf == NULL);
    /* 404: no SHA256SUMS. */
    fresh(0u);
    answer((answer_t){ .status = 404, .length = 9 });
    out = fetch_dev(0u, DSLOT);
    CHECK_EQ(out.got, WA_GOT_SUMS);
    CHECK_EQ(out.status, 404);
    /* The file named twice. */
    fresh(0u);
    make_release(true);
    const size_t one = strlen(g_sums_text);
    memcpy(g_sums_text + one, g_sums_text, one + 1u);
    answer(sums_body());
    out = fetch_dev(0u, DSLOT);
    CHECK_EQ(out.got, WA_GOT_SUMS);
    CHECK_EQ(out.rel, FW_REL_SUMS_TWICE);
}

TEST_CASE(the_slot_takes_its_own_size_and_refuses_a_byte_more)
{
    fresh(0u);
    make_release(true);
    answer(sums_body());
    answer((answer_t){ .status = 200, .length = (int64_t)DSLOT + 1 });
    wa_fetch_res_t out = fetch_dev(0u, DSLOT);
    CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
    CHECK_EQ(out.rel, FW_REL_TOO_BIG);
    CHECK_EQ(g.alloc_bytes, 0u);
    CHECK(out.buf == NULL);
    /* At the slot: a buffer of it, and the body ends short. */
    fresh(0u);
    answer(sums_body());
    answer((answer_t){ .status = 200, .length = DSLOT, .body = g_img,
                       .body_n = 10u });
    out = fetch_dev(0u, DSLOT);
    CHECK_EQ(g.alloc_bytes, DSLOT);
    CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
    CHECK_EQ(out.rel, FW_REL_SHORT);
    CHECK(out.buf == g.alloc_buf);
    /* Empty. */
    fresh(0u);
    answer(sums_body());
    answer((answer_t){ .status = 200, .length = 0 });
    out = fetch_dev(0u, DSLOT);
    CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
    CHECK_EQ(out.rel, FW_REL_EMPTY);
}

TEST_CASE(a_redirect_to_a_host_not_allowed_is_refused)
{
    fresh(0u);
    make_release(true);
    answer(sums_body());
    answer((answer_t){ .status = 302, .length = 0,
                       .loc = "https://evil.example/" NAME });
    const wa_fetch_res_t out = fetch_dev(0u, DSLOT);
    CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
    CHECK_EQ(out.rel, FW_REL_HOST);
    CHECK_EQ(g.alloc_bytes, 0u);
}

TEST_CASE(no_memory_for_the_file_is_memory)
{
    fresh(0u);
    make_release(true);
    script_good();
    g.alloc_fails = true;
    const wa_fetch_res_t out = fetch_dev(0u, DSLOT);
    CHECK_EQ(out.got, WA_GOT_MEMORY);
    CHECK(out.buf == NULL);
}

TEST_CASE(a_broken_connection_is_connect)
{
    fresh(0u);
    make_release(true);
    answer(sums_body());
    answer_t a = image_body();
    a.read_breaks = true;
    answer(a);
    const wa_fetch_res_t out = fetch_dev(0u, DSLOT);
    CHECK_EQ(out.got, WA_GOT_CONNECT);
    CHECK(out.buf == g.alloc_buf);
}

TEST_CASE(a_stop_during_the_file_ends_aborted)
{
    fresh(0u);
    make_release(true);
    script_good();
    g.stop_after_reads = 2u;
    const wa_fetch_res_t out = fetch_dev(0u, DSLOT);
    CHECK_EQ(out.got, WA_GOT_ABORTED);
    CHECK_EQ(out.bytes, 0u);
}

TEST_CASE(the_budget_ends_the_job_at_its_millisecond_across_the_wrap)
{
    const uint32_t start = 0xFFFFFFFFu - 1000u;
    /* The last read lands one millisecond inside the budget: staged. */
    fresh(start);
    make_release(true);
    answer(sums_body());
    answer_t a = image_body();
    a.open_ms = WA_FETCH_DOWNLOAD_MS - 1u;
    answer(a);
    wa_fetch_res_t out = fetch_dev(start, DSLOT);
    CHECK_EQ(out.got, WA_GOT_STAGED);
    /* At the budget's end: the file is not handed on. */
    fresh(start);
    answer(sums_body());
    a.open_ms = WA_FETCH_DOWNLOAD_MS;
    answer(a);
    out = fetch_dev(start, DSLOT);
    CHECK_EQ(out.got, WA_GOT_TIMEOUT);
    CHECK_EQ(out.bytes, 0u);
}

TEST_CASE(no_name_fetches_nothing)
{
    fresh(0u);
    wa_run_t r = run_for(0u, WA_FETCH_DOWNLOAD_MS);
    r.name = NULL;
    wa_fetch_res_t out;
    wa_run_device(&r, V16, &out);
    CHECK_EQ(out.got, WA_GOT_UNEXPECTED);
    CHECK_EQ(g.opens, 0u);
}

/* --------------------------------------------------------- the job */

TEST_CASE(a_device_job_keeps_its_release_and_asks_sums_after_the_join)
{
    wa_fetch_t f;
    wa_fetch_init(&f, V16);
    CHECK_EQ(wa_fetch_start(&f, WA_JOB_DEVICE, V16, WA_CH_STABLE, true,
                            WA_WHY_NONE, 100u), WA_GOT_NONE);
    CHECK_EQ(f.job, WA_JOB_DEVICE);
    CHECK_EQ(f.found.patch, 16u);
    CHECK(wa_fetch_has_found(&f));
    CHECK(!wa_fetch_offers(&f, WA_CH_STABLE));
    CHECK_EQ(wa_fetch_left(&f, 100u), WA_FETCH_JOIN_MS);
    wa_t w;
    memset(&w, 0, sizeof(w));
    w.mode = WA_MODE_FETCH;
    w.state = WA_ST_JOINED;
    wa_fetch_tick(&f, 200u, WA_WHY_NONE, &w);
    CHECK_EQ(f.phase, WA_PH_SUMS);
    CHECK(wa_fetch_take_run(&f));
    CHECK_EQ(wa_fetch_left(&f, 200u), WA_FETCH_DOWNLOAD_MS - 100u);
    /* ARMED ends it; a late report is dropped. */
    wa_fetch_tick(&f, 300u, WA_WHY_ARMED, &w);
    CHECK_EQ(f.got, WA_GOT_ABORTED);
    CHECK_EQ(f.why, WA_WHY_ARMED);
    const wa_fetch_res_t late = { .got = WA_GOT_STAGED };
    wa_fetch_on_end(&f, f.gen, &late);
    CHECK_EQ(f.got, WA_GOT_ABORTED);
}

TEST_CASE(a_device_job_is_refused_while_armed_or_without_a_network)
{
    wa_fetch_t f;
    wa_fetch_init(&f, V16);
    CHECK_EQ(wa_fetch_start(&f, WA_JOB_DEVICE, V16, WA_CH_STABLE, true,
                            wa_fetch_gate(true, false, false, false, false),
                            0u), WA_GOT_BLOCKED);
    CHECK_EQ(f.why, WA_WHY_ARMED);
    CHECK(!wa_fetch_busy(&f));
    CHECK_EQ(wa_fetch_start(&f, WA_JOB_DEVICE, V16, WA_CH_STABLE, false,
                            WA_WHY_NONE, 0u), WA_GOT_NO_NET);
}

/* The panel's CHECK ends and a device's job replaces it before any
 * reader saw the end: the panel's view still reads that end.  A CHECK
 * still running refuses the DEVICE job and keeps nothing. */
TEST_CASE(a_device_job_keeps_the_panels_ended_check_readable)
{
    wa_fetch_t f, keep, own;
    wa_fetch_init(&f, V16);
    memset(&keep, 0, sizeof(keep));
    CHECK_EQ(wa_fetch_start(&f, WA_JOB_CHECK, V16, WA_CH_STABLE, true,
                            WA_WHY_NONE, 0u), WA_GOT_NONE);
    const uint32_t check_gen = f.gen;
    /* running: the DEVICE start is refused, nothing is kept */
    wa_fetch_keep(&f, WA_JOB_DEVICE, &keep);
    CHECK_EQ(wa_fetch_start(&f, WA_JOB_DEVICE, V16, WA_CH_STABLE, true,
                            WA_WHY_NONE, 1u), WA_GOT_BLOCKED);
    CHECK_EQ(keep.gen, 0u);
    wa_fetch_own(&f, &keep, &own);
    CHECK_EQ(own.job, WA_JOB_CHECK);
    CHECK(wa_fetch_busy(&own));
    /* the check ends; in the same frame a device's job replaces it */
    wa_fetch_abort(&f, WA_WHY_STOP);
    CHECK(!wa_fetch_busy(&f));
    wa_fetch_keep(&f, WA_JOB_DEVICE, &keep);
    CHECK_EQ(wa_fetch_start(&f, WA_JOB_DEVICE, V16, WA_CH_STABLE, true,
                            WA_WHY_NONE, 2u), WA_GOT_NONE);
    wa_fetch_own(&f, &keep, &own);
    CHECK_EQ(own.job, WA_JOB_CHECK);
    CHECK_EQ(own.gen, check_gen);
    CHECK(!wa_fetch_busy(&own));
    CHECK_EQ(own.got, WA_GOT_ABORTED);
    /* a second DEVICE job keeps the panel's check, not the device's */
    wa_fetch_abort(&f, WA_WHY_CANCEL);
    wa_fetch_keep(&f, WA_JOB_DEVICE, &keep);
    CHECK_EQ(wa_fetch_start(&f, WA_JOB_DEVICE, V16, WA_CH_STABLE, true,
                            WA_WHY_NONE, 3u), WA_GOT_NONE);
    wa_fetch_own(&f, &keep, &own);
    CHECK_EQ(own.gen, check_gen);
    /* the panel's next job reads as itself */
    wa_fetch_abort(&f, WA_WHY_CANCEL);
    wa_fetch_keep(&f, WA_JOB_DOWNLOAD, &keep);
    CHECK_EQ(wa_fetch_start(&f, WA_JOB_DOWNLOAD, V16, WA_CH_STABLE, true,
                            WA_WHY_NONE, 4u), WA_GOT_NONE);
    wa_fetch_own(&f, &keep, &own);
    CHECK_EQ(own.job, WA_JOB_DOWNLOAD);
    CHECK_EQ(own.gen, f.gen);
    CHECK_EQ(keep.gen, check_gen);
}

int main(void)
{
    g_img = malloc(SLOT);
    g.alloc_buf = malloc(DSLOT);
    REQUIRE_ALLOC(g_img);
    REQUIRE_ALLOC(g.alloc_buf);
    RUN(a_devices_file_downloads_checked_into_its_own_buffer);
    RUN(a_hash_other_than_the_sums_line_is_refused);
    RUN(a_release_without_the_files_line_or_sums_fetches_no_file);
    RUN(the_slot_takes_its_own_size_and_refuses_a_byte_more);
    RUN(a_redirect_to_a_host_not_allowed_is_refused);
    RUN(no_memory_for_the_file_is_memory);
    RUN(a_broken_connection_is_connect);
    RUN(a_stop_during_the_file_ends_aborted);
    RUN(the_budget_ends_the_job_at_its_millisecond_across_the_wrap);
    RUN(no_name_fetches_nothing);
    RUN(a_device_job_keeps_its_release_and_asks_sums_after_the_join);
    RUN(a_device_job_is_refused_while_armed_or_without_a_network);
    RUN(a_device_job_keeps_the_panels_ended_check_readable);
    free(g.alloc_buf);
    free(g_img);
    return test_summary("wa_fetch_dev");
}
