/**
 * @file test_wa_fetch_sig.c
 * @brief The release signature a download fetches after the image.
 *
 * The failures under test: a download whose image is shown as signed when
 * the release key did not vouch for it, one that refuses a release without
 * a signature (the panel warns and installs), and one that stages an image
 * after the connection broke, the budget ran out or the job was stopped
 * while the signature was fetched.  The HTTP (hypertext transfer protocol)
 * client is scripted, one answer per request in order, and the verdict is
 * the test's: what fw_sign.c says is test_fw_sign's matter.
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

/** @brief The image slot the runner takes, bytes. */
#define SLOT 4063232u
/** @brief The answers one download takes at most. */
#define ANSWERS 6

/** @brief One scripted answer. */
typedef struct {
    int            status;     /**< HTTP status */
    int64_t        length;     /**< Content-Length; -1 none */
    const char    *loc;        /**< Location; NULL none */
    const uint8_t *body;       /**< the body */
    uint32_t       body_n;     /**< its bytes */
    bool           no_connect; /**< open() fails */
    bool           stop;       /**< the job is stopped at this open() */
    uint32_t       open_ms;    /**< the clock moves this much in open() */
} answer_t;

/** @brief The scripted client and the verdict hook's record. */
static struct {
    answer_t a[ANSWERS];        /**< the script */
    unsigned n;                 /**< answers scripted */
    unsigned at;                /**< answers used */
    const answer_t *cur;        /**< the answer being read */
    uint32_t sent;              /**< its bytes sent */
    char     urls[ANSWERS][FW_REL_URL_MAX + 1u]; /**< the URLs opened */
    unsigned opens;             /**< open() calls */
    uint32_t now;               /**< the clock, ms */
    bool     stopped;           /**< the job is stopped */
    unsigned verifies;          /**< verify() calls */
    fw_sig_t verdict;           /**< what verify() says */
    uint8_t  sig[FW_SIG_MAX];   /**< the signature verify() saw */
    uint32_t sig_n;             /**< its bytes */
    const uint8_t *img;         /**< the image verify() saw */
    uint32_t img_n;             /**< its bytes */
} g;

/** @brief open(): the next answer. */
static bool f_open(void *ctx, const char *url, uint32_t timeout_ms,
                   bool api, wa_http_head_t *head, char *loc, size_t loc_cap)
{
    (void)ctx;
    (void)timeout_ms;
    (void)api;
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
    g.stopped = g.stopped || g.cur->stop;
    if (g.cur->no_connect) {
        return false;
    }
    head->status = g.cur->status;
    head->length = g.cur->length;
    head->chunked = false;
    head->loc_n = 0u;
    head->loc_cut = false;
    if (g.cur->loc != NULL) {
        const size_t n = strlen(g.cur->loc);
        head->loc_n = (n > loc_cap) ? loc_cap : n;
        memcpy(loc, g.cur->loc, head->loc_n);
    }
    return true;
}

/** @brief read(): the answer's body in pieces. */
static int32_t f_read(void *ctx, uint8_t *dst, uint32_t n,
                      uint32_t timeout_ms)
{
    (void)ctx;
    (void)timeout_ms;
    const uint32_t left = g.cur->body_n - g.sent;
    const uint32_t k = (n < left) ? n : left;
    memcpy(dst, g.cur->body + g.sent, k);
    g.sent += k;
    return (int32_t)k;
}

/** @brief close(): nothing to do. */
static void f_close(void *ctx) { (void)ctx; }

/** @brief The clock. */
static uint32_t f_now(void *ctx)
{
    (void)ctx;
    return g.now;
}

/** @brief Whether the job is stopped. */
static bool f_stopped(void *ctx)
{
    (void)ctx;
    return g.stopped;
}

/** @brief The image buffer. */
static uint8_t *g_buf;

/** @brief The allocator: the one buffer. */
static uint8_t *f_alloc(void *ctx, uint32_t bytes)
{
    (void)ctx;
    (void)bytes;
    return g_buf;
}

/** @brief The verdict hook: records what it saw, says g.verdict. */
static fw_sig_t f_verify(void *ctx, const uint8_t *img, uint32_t bytes,
                         const uint8_t *sig, uint32_t sig_n)
{
    (void)ctx;
    ++g.verifies;
    g.img = img;
    g.img_n = bytes;
    g.sig_n = sig_n;
    memcpy(g.sig, sig, (sig_n <= FW_SIG_MAX) ? sig_n : FW_SIG_MAX);
    return g.verdict;
}

/** @brief The scripted client. */
static const wa_http_t k_http = { f_open, f_read, f_close, NULL };

static char        g_url[FW_REL_URL_MAX + 1u];  /**< runner scratch */
static char        g_loc[FW_REL_URL_MAX + 1u];  /**< runner scratch */
static uint8_t     g_io[4096];                  /**< runner scratch */
static char        g_sums[FW_REL_SUMS_MAX];     /**< runner scratch */
static fw_upload_t g_up;                        /**< the staged image */
static uint8_t    *g_img;                       /**< the release's image */
static uint32_t    g_img_n;                     /**< its bytes */
static char        g_sums_text[256];            /**< its SHA256SUMS */
/** @brief The release the tests fetch. */
static const fw_version_t V20 = { 0u, 20u, 0u };

/** @brief A fresh script, the release 0.20.0 and its SHA256SUMS. */
static void fresh(void)
{
    memset(&g, 0, sizeof(g));
    memset(&g_up, 0, sizeof(g_up));
    g.verdict = FW_SIG_OK;
    fw_fix_t f = fw_fix_default();
    f.version = "0.20.0";
    g_img_n = fw_fix_make(&f, g_img);
    uint8_t h[FW_SHA256_BYTES];
    fw_sha256(g_img, g_img_n, h);
    char hex[2u * FW_SHA256_BYTES + 1u];
    for (unsigned i = 0; i < FW_SHA256_BYTES; ++i) {
        snprintf(hex + 2u * i, 3u, "%02x", h[i]);
    }
    snprintf(g_sums_text, sizeof(g_sums_text),
             "%s  ./rcbench-panel-p4-v0.20.0.bin\n", hex);
    const uint32_t sn = (uint32_t)strlen(g_sums_text);
    g.a[g.n++] = (answer_t){ .status = 200, .length = sn,
                             .body = (const uint8_t *)g_sums_text,
                             .body_n = sn };
    g.a[g.n++] = (answer_t){ .status = 200, .length = g_img_n,
                             .body = g_img, .body_n = g_img_n };
}

/** @brief Script the signature's answer. */
static void sig_answer(answer_t a)
{
    g.a[g.n++] = a;
}

/** @brief A signature body of @p n bytes, each 0x30 + i. */
static uint8_t g_sig_body[FW_SIG_MAX + 8u];

/** @brief The answer of a signature of @p n bytes, as announced. */
static answer_t sig_of(uint32_t n)
{
    for (uint32_t i = 0; i < sizeof(g_sig_body); ++i) {
        g_sig_body[i] = (uint8_t)(0x30u + i);
    }
    return (answer_t){ .status = 200, .length = n, .body = g_sig_body,
                       .body_n = n };
}

/** @brief Run the download with the hook @p verify. */
static wa_fetch_res_t download(fw_sig_t (*verify)(void *, const uint8_t *,
                                                  uint32_t, const uint8_t *,
                                                  uint32_t))
{
    wa_run_t r;
    memset(&r, 0, sizeof(r));
    r.http = &k_http;
    r.now = f_now;
    r.stopped = f_stopped;
    r.alloc = f_alloc;
    r.verify = verify;
    r.budget_ms = WA_FETCH_DOWNLOAD_MS;
    r.slot = SLOT;
    r.running = (fw_version_t){ 0u, 19u, 0u };
    r.running_ok = true;
    r.url = g_url;
    r.loc = g_loc;
    r.io = g_io;
    r.io_cap = sizeof(g_io);
    r.sums = g_sums;
    r.up = &g_up;
    wa_fetch_res_t out;
    wa_run_download(&r, V20, &out);
    return out;
}

TEST_CASE(a_signature_is_fetched_after_the_image_and_judged)
{
    fresh();
    sig_answer(sig_of(70u));
    const wa_fetch_res_t out = download(f_verify);
    CHECK_EQ(out.got, WA_GOT_STAGED);
    CHECK_EQ(g_up.state, FW_UP_STAGED);
    CHECK_EQ(g_up.info.sig, FW_SIG_OK);
    CHECK_STR_EQ(g.urls[2], "https://github.com/subtilitas/rcbench/releases/"
                            "download/v0.20.0/rcbench-panel-p4-v0.20.0.bin.sig");
    /* The hook saw the whole staged image and every signature byte. */
    CHECK_EQ(g.verifies, 1u);
    CHECK(g.img == g_buf);
    CHECK_EQ(g.img_n, g_img_n);
    CHECK_EQ(g.sig_n, 70u);
    CHECK_EQ(memcmp(g.sig, g_sig_body, 70u), 0);
}

TEST_CASE(the_hooks_verdict_is_the_images)
{
    static const fw_sig_t says[] = { FW_SIG_BAD, FW_SIG_FORM, FW_SIG_KEY,
                                     FW_SIG_NONE };
    for (size_t i = 0; i < sizeof(says) / sizeof(says[0]); ++i) {
        fresh();
        g.verdict = says[i];
        sig_answer(sig_of(71u));
        const wa_fetch_res_t out = download(f_verify);
        /* A signature that does not verify stages the image: the panel
         * warns, and with RCBENCH_SIGN_ENFORCE asks for a second hold. */
        CHECK_EQ(out.got, WA_GOT_STAGED);
        CHECK_EQ(g_up.info.sig, says[i]);
    }
    /* No hook: nothing is judged. */
    fresh();
    sig_answer(sig_of(70u));
    CHECK_EQ(download(NULL).got, WA_GOT_STAGED);
    CHECK_EQ(g_up.info.sig, FW_SIG_NONE);
}

TEST_CASE(a_release_without_a_signature_stages_unsigned)
{
    static const int statuses[] = { 404, 403, 500 };
    for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); ++i) {
        fresh();
        sig_answer((answer_t){ .status = statuses[i], .length = 0 });
        const wa_fetch_res_t out = download(f_verify);
        CHECK_EQ(out.got, WA_GOT_STAGED);
        CHECK_EQ(g_up.info.sig, FW_SIG_NONE);
        CHECK_EQ(g.verifies, 0u);
    }
}

TEST_CASE(a_signature_of_the_wrong_size_is_out_of_form)
{
    /* FW_SIG_MIN and FW_SIG_MAX are judged; one byte outside either is
     * out of form and never reaches the hook. */
    fresh();
    sig_answer(sig_of(FW_SIG_MIN));
    CHECK_EQ(download(f_verify).got, WA_GOT_STAGED);
    CHECK_EQ(g.sig_n, FW_SIG_MIN);
    fresh();
    sig_answer(sig_of(FW_SIG_MAX));
    CHECK_EQ(download(f_verify).got, WA_GOT_STAGED);
    CHECK_EQ(g.sig_n, FW_SIG_MAX);
    CHECK_EQ(g_up.info.sig, FW_SIG_OK);
    const uint32_t bad[] = { FW_SIG_MIN - 1u, FW_SIG_MAX + 1u, 0u };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        fresh();
        sig_answer(sig_of(bad[i]));
        const wa_fetch_res_t out = download(f_verify);
        CHECK_EQ(out.got, WA_GOT_STAGED);
        CHECK_EQ(g_up.info.sig, FW_SIG_FORM);
        CHECK_EQ(g.verifies, 0u);
    }
    /* A body that ends before its announced length. */
    fresh();
    answer_t a = sig_of(70u);
    a.body_n = 20u;
    sig_answer(a);
    CHECK_EQ(download(f_verify).got, WA_GOT_STAGED);
    CHECK_EQ(g_up.info.sig, FW_SIG_FORM);
    /* A redirect to a host the release check does not allow. */
    fresh();
    sig_answer((answer_t){ .status = 302, .loc = "https://evil.com/x.sig" });
    CHECK_EQ(download(f_verify).got, WA_GOT_STAGED);
    CHECK_EQ(g_up.info.sig, FW_SIG_FORM);
}

TEST_CASE(a_broken_connection_stop_or_budget_stages_nothing)
{
    fresh();
    sig_answer((answer_t){ .no_connect = true });
    wa_fetch_res_t out = download(f_verify);
    CHECK_EQ(out.got, WA_GOT_CONNECT);
    CHECK(g_up.state != FW_UP_STAGED);
    CHECK_EQ(out.found.minor, 20u);
    /* No answer at all is a connection that broke. */
    fresh();
    out = download(f_verify);
    CHECK_EQ(out.got, WA_GOT_CONNECT);
    CHECK(g_up.state != FW_UP_STAGED);
    /* Stopped while the signature is asked for. */
    fresh();
    answer_t a = sig_of(70u);
    a.stop = true;
    sig_answer(a);
    out = download(f_verify);
    CHECK_EQ(out.got, WA_GOT_ABORTED);
    CHECK(g_up.state != FW_UP_STAGED);
    /* The budget runs out in the signature's request: its last ms passes
     * and the job ends TIMEOUT, not STAGED. */
    fresh();
    a = sig_of(70u);
    a.open_ms = WA_FETCH_DOWNLOAD_MS;
    sig_answer(a);
    out = download(f_verify);
    CHECK_EQ(out.got, WA_GOT_TIMEOUT);
    CHECK(g_up.state != FW_UP_STAGED);
    /* One ms short of the budget still stages. */
    fresh();
    a = sig_of(70u);
    a.open_ms = WA_FETCH_DOWNLOAD_MS - 1u;
    sig_answer(a);
    CHECK_EQ(download(f_verify).got, WA_GOT_STAGED);
}

TEST_CASE(the_signature_name_fits_the_longest_version)
{
    char n[FW_REL_SIG_NAME_MAX];
    fw_release_sig_name((fw_version_t){ 65535u, 65535u, 65535u }, n);
    CHECK_STR_EQ(n, "rcbench-panel-p4-v65535.65535.65535.bin.sig");
    CHECK_EQ(strlen(n) + 1u, FW_REL_SIG_NAME_MAX);
    fw_release_sig_name(V20, n);
    CHECK_STR_EQ(n, "rcbench-panel-p4-v0.20.0.bin.sig");
}

int main(void)
{
    g_img = malloc(SLOT);
    g_buf = malloc(SLOT);
    REQUIRE_ALLOC(g_img);
    REQUIRE_ALLOC(g_buf);
    RUN(a_signature_is_fetched_after_the_image_and_judged);
    RUN(the_hooks_verdict_is_the_images);
    RUN(a_release_without_a_signature_stages_unsigned);
    RUN(a_signature_of_the_wrong_size_is_out_of_form);
    RUN(a_broken_connection_stop_or_budget_stages_nothing);
    RUN(the_signature_name_fits_the_longest_version);
    free(g_img);
    free(g_buf);
    return test_summary("wa_fetch_sig");
}
