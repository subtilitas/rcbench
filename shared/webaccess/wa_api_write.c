/**
 * @file
 * @brief The Wi-Fi page's file API: its writes (PUT of a file, MKDIR,
 *        RENAME and its move, DELETE) and whether a browser may write.
 *        See wa_api.h.
 *
 * Each write is one job of FILES' core (fm_ops.h), run to its end through
 * the runner (fm_req_run()): the write gate, the places and the names are
 * FILES', and so are the refusals.  The caller holds the card's job lock
 * around each call (wifi_write.c on the panel), so no FILES job runs
 * between a PUT's check of the old file and its write.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wa_api.h"
#include "wa_api_int.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "fm_name.h"
#include "fm_ops.h"
#include "fm_rules.h"
#include "wa_zip.h"

/** @brief The setting's words, by wa_writes_t. */
static const char *const k_mode[WA_WRITES_COUNT] = {
    [WA_WRITES_OPEN] = "OPEN", [WA_WRITES_CODE] = "CODE",
    [WA_WRITES_HOLD] = "HOLD", [WA_WRITES_OFF] = "OFF",
};

wa_api_err_t wa_api_may_write(wa_writes_t mode, bool code, bool window)
{
    switch (mode) {
    case WA_WRITES_OPEN:
        return WA_API_OK;
    case WA_WRITES_CODE:
        return code ? WA_API_OK : WA_API_WRITES_CODE;
    case WA_WRITES_HOLD:
        return window ? WA_API_OK : WA_API_WRITES_HOLD;
    case WA_WRITES_OFF:
    case WA_WRITES_COUNT:
    default:
        return WA_API_WRITES_OFF;
    }
}

const char *wa_api_writes_word(wa_writes_t mode)
{
    return ((unsigned)mode < (unsigned)WA_WRITES_COUNT) ? k_mode[mode]
                                                         : k_mode[WA_WRITES_OFF];
}

/**
 * @brief Forget the last write's answer.
 * @param api  the API
 */
static void write_begin(wa_api_t *api)
{
    api->created = false;
    api->restart = false;
    api->put = false;
    api->done[0] = '\0';
    api->etag = 0u;
    api->found = false;
    api->found_tag = 0u;
}

/**
 * @brief Run the job the request holds, and keep the path it leaves.
 * @param api   the API
 * @param req   the request, its job started
 * @param path  the path the job leaves, FM_PATH_SIZE bytes at most
 * @return WA_API_OK, or why not
 */
static wa_api_err_t run_job(wa_api_t *api, fm_req_t *req, const char *path)
{
    const fm_err_t e = fm_req_run(req, api->run);
    if (e == FM_OK) {
        (void)snprintf(api->done, sizeof(api->done), "%s", path);
    }
    return wa_api_of(e);
}

/**
 * @brief The bench as it stands, with the request's log folders, for a
 *        job's start.
 * @param api    the API
 * @param req    the request
 * @param bench  takes the bench
 */
static void bench_now(const wa_api_t *api, const fm_req_t *req,
                      fm_bench_t *bench)
{
    memset(bench, 0, sizeof(*bench));
    api->run->bench(bench, api->run->ctx);
    fm_req_bench(req, bench);
}

/**
 * @brief A write's path from parameter @p key: wa_api_q_path()'s, and no
 *        name in it an alias FatFs would read as another entry
 *        (fm_path_plain()), so the rules judge the entry the card changes.
 * @param query  the query string or form
 * @param key    the parameter
 * @param out    takes the path, FM_PATH_SIZE bytes
 * @return WA_API_OK, or WA_API_NAME
 */
static wa_api_err_t write_path(const char *query, const char *key,
                               char *out)
{
    const wa_api_err_t e = wa_api_q_path(query, key, out);
    if (e == WA_API_OK && !fm_path_plain(out)) {
        out[0] = '\0';
        return WA_API_NAME;
    }
    return e;
}

/**
 * @brief An ETag as If-Match carries it: 8 hex digits between quotes.
 * @param s    the header's value
 * @param out  takes the value
 * @return false for any other form, "*" and a list included
 */
static bool etag_of(const char *s, uint32_t *out)
{
    if (strlen(s) != 10u || s[0] != '"' || s[9] != '"') {
        return false;
    }
    uint32_t v = 0u;
    for (size_t i = 1u; i < 9u; ++i) {
        const char c = s[i];
        uint32_t d;
        if (c >= '0' && c <= '9') {
            d = (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            d = (uint32_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            d = (uint32_t)(c - 'A' + 10);
        } else {
            return false;
        }
        v = (v << 4) | d;
    }
    *out = v;
    return true;
}

/**
 * @brief A save's bytes: the body the API's text holds.
 * @param ctx  the API
 * @param at   the offset of the first byte wanted
 * @param dst  takes the bytes
 * @param cap  room in @p dst
 * @return the bytes written into @p dst
 */
static size_t put_fill(void *ctx, uint64_t at, uint8_t *dst, size_t cap)
{
    return wa_api_file_read((const wa_api_t *)ctx, (size_t)at, dst, cap);
}

/**
 * @brief The sum fm_job_match() compares: the ETag's CRC-32.
 * @param sum   the sum so far
 * @param data  the bytes
 * @param n     how many
 * @return the sum with them
 */
static uint32_t put_sum(uint32_t sum, const void *data, size_t n)
{
    return wa_crc32(sum, data, n);
}

uint8_t *wa_api_body(wa_api_t *api, size_t *cap)
{
    *cap = 0u;
    if (api->text == NULL || api->ends == NULL
        || text_buf_init(&api->tb, api->text, api->text_cap, api->ends,
                         api->ends_cap) != TEXT_BUF_OK) {
        return NULL;
    }
    *cap = text_buf_room(&api->tb);
    return text_buf_load_area(&api->tb);
}

/**
 * @brief What PUT checks of the file it replaces, from its headers.
 * @param if_match  If-Match, or NULL
 * @param if_none   If-None-Match, or NULL
 * @param match     takes the check
 * @param want      takes the ETag for FM_MATCH_SUM
 * @return WA_API_OK; WA_API_REQUEST for both, or a form not taken
 */
static wa_api_err_t put_match(const char *if_match, const char *if_none,
                              fm_match_t *match, uint32_t *want)
{
    *match = FM_MATCH_ANY;
    *want = 0u;
    if (if_match != NULL && if_none != NULL) {
        return WA_API_REQUEST;
    }
    if (if_match != NULL) {
        *match = FM_MATCH_SUM;
        return etag_of(if_match, want) ? WA_API_OK : WA_API_REQUEST;
    }
    if (if_none != NULL) {
        *match = FM_MATCH_ABSENT;
        return (strcmp(if_none, "*") == 0) ? WA_API_OK : WA_API_REQUEST;
    }
    return WA_API_OK;
}

/** @brief What a PUT's query and headers ask. */
typedef struct {
    char path[FM_PATH_SIZE];   /**< the file                               */
    char name[FM_NAME_BUF];    /**< its name; its folder is the request's  */
    unsigned mount;            /**< the mount it was listed from           */
    fm_match_t match;          /**< what it checks of the file there       */
    uint32_t want;             /**< FM_MATCH_SUM: the ETag                 */
} put_args_t;

/**
 * @brief A PUT's query and headers, read and held to their rules.
 * @param api       the API
 * @param query     the query string; NULL is ""
 * @param if_match  the If-Match header, or NULL
 * @param if_none   the If-None-Match header, or NULL
 * @param req       the request, which takes the folder in dir
 * @param a         takes the rest
 * @return WA_API_OK, or why not
 */
static wa_api_err_t put_args(const wa_api_t *api, const char *query,
                             const char *if_match, const char *if_none,
                             fm_req_t *req, put_args_t *a)
{
    if (query == NULL) {
        query = "";
    }
    wa_api_err_t e = write_path(query, "path", a->path);
    a->mount = 0u;
    if (e == WA_API_OK && !wa_api_q_mount(query, api->run->io, &a->mount)) {
        e = WA_API_REQUEST;
    }
    a->match = FM_MATCH_ANY;
    a->want = 0u;
    if (e == WA_API_OK) {
        e = put_match(if_match, if_none, &a->match, &a->want);
    }
    if (e == WA_API_OK) {
        e = wa_api_split(a->path, req->dir, a->name);
    }
    /* A name the browser brings follows the panel keyboard's rule; one
     * read from the card and written back keeps the card's. */
    if (e == WA_API_OK && a->match != FM_MATCH_SUM
        && fm_name_check(a->name) != FM_NAME_OK) {
        e = WA_API_NAME;
    }
    return e;
}

/**
 * @brief Start the SAVE of a PUT: fm_job_save() with fm_job_match().
 * @param api  the API
 * @param req  the request
 * @param a    what the PUT asks
 * @param len  the body's bytes
 */
static void put_job(wa_api_t *api, fm_req_t *req, const put_args_t *a,
                    uint64_t len)
{
    fm_bench_t bench;
    bench_now(api, req, &bench);
    const fm_err_t se = fm_job_save(&req->job, &bench, a->mount, req->dir,
                                    a->name, len, put_fill, api);
    if (se == FM_OK) {
        (void)fm_job_match(&req->job, a->match, put_sum, a->want);
    }
}

/**
 * @brief The ETag of the file a refused save found, into the API.
 * @param api  the API
 * @param req  the request, its job refused
 */
static void put_found(wa_api_t *api, const fm_req_t *req)
{
    api->found = fm_job_found(&req->job, &api->found_tag);
}

wa_api_err_t wa_api_put_check(wa_api_t *api, const char *query,
                              const char *if_match, const char *if_none,
                              size_t len)
{
    write_begin(api);
    fm_req_t *req = wa_api_req(api, FM_REQ_JOB);
    put_args_t a;
    wa_api_err_t e = put_args(api, query, if_match, if_none, req, &a);
    if (e != WA_API_OK) {
        return e;
    }
    put_job(api, req, &a, (uint64_t)len);
    (void)fm_job_check_only(&req->job);
    e = wa_api_of(fm_req_run(req, api->run));
    if (e == WA_API_CHANGED) {
        put_found(api, req);
    }
    return e;
}

wa_api_err_t wa_api_put(wa_api_t *api, const char *query,
                        const char *if_match, const char *if_none,
                        size_t len)
{
    write_begin(api);
    fm_req_t *req = wa_api_req(api, FM_REQ_JOB);
    put_args_t a;
    wa_api_err_t e = put_args(api, query, if_match, if_none, req, &a);
    if (e == WA_API_OK && (api->text == NULL || api->tb.mem != api->text)) {
        e = WA_API_MEMORY;          /* no room, or wa_api_body() not asked */
    }
    if (e == WA_API_OK) {
        e = wa_api_text_why(text_buf_load_done(&api->tb, len));
    }
    if (e != WA_API_OK) {
        memset(&api->tb, 0, sizeof(api->tb));
        return e;
    }
    put_job(api, req, &a, (uint64_t)wa_api_file_size(api));
    e = run_job(api, req, a.path);
    if (e == WA_API_OK) {
        api->put = true;
        api->created = !req->job.dst_was;
        api->restart = req->job.note_restart;
        api->etag = wa_api_crc(api);
    } else if (e == WA_API_CHANGED) {
        put_found(api, req);
    }
    /* One body, one PUT: the next asks wa_api_body() again. */
    memset(&api->tb, 0, sizeof(api->tb));
    return e;
}

wa_api_err_t wa_api_mkdir(wa_api_t *api, const char *form)
{
    write_begin(api);
    if (form == NULL) {
        form = "";
    }
    char path[FM_PATH_SIZE];
    wa_api_err_t e = write_path(form, "path", path);
    unsigned mount = 0u;
    if (e == WA_API_OK && !wa_api_q_mount(form, api->run->io, &mount)) {
        e = WA_API_REQUEST;
    }
    fm_req_t *req = wa_api_req(api, FM_REQ_JOB);
    char name[FM_NAME_BUF];
    if (e == WA_API_OK) {
        e = wa_api_split(path, req->dir, name);
    }
    if (e != WA_API_OK) {
        return e;
    }
    fm_bench_t bench;
    bench_now(api, req, &bench);
    (void)fm_job_mkdir(&req->job, &bench, mount, req->dir, name);
    e = run_job(api, req, path);
    api->created = (e == WA_API_OK);
    return e;
}

/**
 * @brief The entry a RENAME or a DELETE names: its path, its folder and
 *        name, whether it is a folder, and the mount it was listed from.
 * @param api     the API
 * @param form    the body
 * @param key     the path's parameter
 * @param req     the request, which takes the folder in dir
 * @param name    takes the name, FM_NAME_BUF bytes
 * @param is_dir  takes the "dir" flag
 * @param mount   takes the mount
 * @return WA_API_OK, or why not
 */
static wa_api_err_t entry_of(const wa_api_t *api, const char *form,
                             const char *key,
                             fm_req_t *req, char *name, bool *is_dir,
                             unsigned *mount)
{
    char path[FM_PATH_SIZE];
    const wa_api_err_t e = write_path(form, key, path);
    if (e != WA_API_OK) {
        return e;
    }
    if (!wa_api_q_flag(form, "dir", is_dir)
        || !wa_api_q_mount(form, api->run->io, mount)) {
        return WA_API_REQUEST;
    }
    return wa_api_split(path, req->dir, name);
}

wa_api_err_t wa_api_rename(wa_api_t *api, const char *form)
{
    write_begin(api);
    if (form == NULL) {
        form = "";
    }
    fm_req_t *req = wa_api_req(api, FM_REQ_JOB);
    char name[FM_NAME_BUF];
    bool is_dir = false;
    unsigned mount = 0u;
    wa_api_err_t e = entry_of(api, form, "from", req, name, &is_dir, &mount);
    char to[FM_PATH_SIZE];
    if (e == WA_API_OK) {
        e = write_path(form, "to", to);
    }
    char to_dir[FM_PATH_SIZE];
    char to_name[FM_NAME_BUF];
    if (e == WA_API_OK) {
        e = wa_api_split(to, to_dir, to_name);
    }
    if (e != WA_API_OK) {
        return e;
    }
    fm_bench_t bench;
    bench_now(api, req, &bench);
    if (strcmp(to_dir, req->dir) == 0) {
        (void)fm_job_rename(&req->job, &bench, mount, req->dir, name, is_dir,
                            to_name);
    } else if (strcmp(to_name, name) == 0) {
        (void)fm_job_move(&req->job, &bench, mount, req->dir, name, is_dir,
                          to_dir);
    } else {
        return WA_API_REQUEST;  /* another folder and another name */
    }
    return run_job(api, req, to);
}

wa_api_err_t wa_api_delete(wa_api_t *api, const char *form)
{
    write_begin(api);
    if (form == NULL) {
        form = "";
    }
    fm_req_t *req = wa_api_req(api, FM_REQ_JOB);
    char name[FM_NAME_BUF];
    bool is_dir = false;
    unsigned mount = 0u;
    wa_api_err_t e = entry_of(api, form, "path", req, name, &is_dir, &mount);
    if (e != WA_API_OK) {
        return e;
    }
    fm_bench_t bench;
    bench_now(api, req, &bench);
    (void)fm_job_delete(&req->job, &bench, mount, req->dir, name, is_dir);
    /* What is left is the folder the entry was in. */
    return run_job(api, req, req->dir);
}

const char *wa_api_write_status(const wa_api_t *api)
{
    return api->created ? "201 Created" : "200 OK";
}

bool wa_api_write_etag(const wa_api_t *api, char *out)
{
    out[0] = '\0';
    if (!api->put) {
        return false;
    }
    (void)snprintf(out, WA_API_ETAG_SIZE, "\"%08" PRIx32 "\"", api->etag);
    return true;
}

void wa_api_refused_json(const wa_api_t *api, wa_out_t *o, wa_api_err_t e,
                         bool de)
{
    if (e != WA_API_CHANGED || api == NULL || !api->found) {
        wa_api_error_json(o, e, de);
        return;
    }
    char tag[12];
    (void)snprintf(tag, sizeof(tag), "%08" PRIx32, api->found_tag);
    wa_out_str(o, "{");
    wa_api_json_key(o, "error", true);
    wa_api_json_str(o, wa_api_word(e));
    wa_api_json_key(o, "text", false);
    wa_api_json_str(o, wa_api_text(e, de));
    wa_api_json_key(o, "etag", false);
    wa_api_json_str(o, tag);
    wa_out_str(o, "}");
}

void wa_api_write_json(const wa_api_t *api, wa_out_t *o)
{
    wa_out_str(o, "{");
    wa_api_json_key(o, "path", true);
    wa_api_json_str(o, api->done);
    wa_api_json_key(o, "created", false);
    wa_api_json_bool(o, api->created);
    if (api->put) {
        char tag[12];
        (void)snprintf(tag, sizeof(tag), "%08" PRIx32, api->etag);
        wa_api_json_key(o, "etag", false);
        wa_api_json_str(o, tag);
    }
    wa_api_json_key(o, "note", false);
    wa_api_json_str(o, api->restart ? "ESC_NEXT_START" : "");
    wa_out_str(o, "}");
}
