/**
 * @file
 * @brief The Wi-Fi page's file API. See wa_api.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wa_api.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "fm_name.h"
#include "fm_rules.h"
#include "fm_tree.h"
#include "wa_zip.h"
#include "wall_clock.h"

/** @brief The page's texts, P_<ID> for each UI_WEB_TEXT() of ui_text.def. */
typedef enum {
/** @brief One identifier per UI_WEB_TEXT() entry. */
#define UI_WEB_TEXT(id, en, de) id,
#include "ui_text.def"
    P_COUNT
} p_text_t;

/** @brief The page's texts in English. */
static const char *const k_en[P_COUNT] = {
/** @brief One English entry per identifier. */
#define UI_WEB_TEXT(id, en, de) [id] = (en),
#include "ui_text.def"
};

/** @brief The page's texts in German; NULL where the table has none. */
static const char *const k_de[P_COUNT] = {
/** @brief One German entry per identifier. */
#define UI_WEB_TEXT(id, en, de) [id] = (de),
#include "ui_text.def"
};

/** @brief One reason: its word, its HTTP status and its text. */
typedef struct {
    const char *word;   /**< the reason word                            */
    uint16_t http;      /**< the status                                 */
    p_text_t text;      /**< its sentence; P_COUNT for none             */
} why_t;

/** @brief Every reason, by wa_api_err_t. */
static const why_t k_why[WA_API_COUNT] = {
    [WA_API_OK] = { "", 200u, P_COUNT },
    [WA_API_NO_CARD] = { "NO_CARD", 503u, P_NO_CARD },
    [WA_API_CARD_GONE] = { "CARD_GONE", 503u, P_API_CARD_GONE },
    [WA_API_NOT_FOUND] = { "NOT_FOUND", 404u, P_API_NOT_FOUND },
    [WA_API_EXISTS] = { "EXISTS", 409u, P_API_EXISTS },
    [WA_API_FULL] = { "FULL", 507u, P_API_FULL },
    [WA_API_DENIED] = { "DENIED", 403u, P_API_DENIED },
    [WA_API_BUSY] = { "BUSY", 423u, P_API_BUSY },
    [WA_API_FIXED] = { "FIXED", 403u, P_API_FIXED },
    [WA_API_LOCKED] = { "LOCKED", 403u, P_API_LOCKED },
    [WA_API_NAME] = { "NAME", 400u, P_API_NAME },
    [WA_API_ARG] = { "ARG", 400u, P_API_ARG },
    [WA_API_IO] = { "IO", 500u, P_API_IO },
    [WA_API_NOT_EMPTY] = { "NOT_EMPTY", 409u, P_API_NOT_EMPTY },
    [WA_API_SAME] = { "SAME", 409u, P_API_SAME },
    [WA_API_TOO_BIG] = { "TOO_BIG", 413u, P_API_TOO_BIG },
    [WA_API_ARMED] = { "ARMED", 409u, P_API_ARMED },
    [WA_API_LOGGING] = { "LOGGING", 409u, P_API_LOGGING },
    [WA_API_CANCELLED] = { "CANCELLED", 503u, P_API_CANCELLED },
    [WA_API_NOT_TEXT] = { "NOT_TEXT", 415u, P_API_NOT_TEXT },
    [WA_API_TOO_MANY_LINES] = { "TOO_MANY_LINES", 422u,
                                P_API_TOO_MANY_LINES },
    [WA_API_MEMORY] = { "MEMORY", 503u, P_API_MEMORY },
    [WA_API_CODE] = { "CODE", 403u, P_API_CODE },
    [WA_API_REQUEST] = { "REQUEST", 400u, P_API_REQUEST },
};

/** @brief The API's reason for each answer of the core. */
static const wa_api_err_t k_of[FM_ERR_COUNT] = {
    [FM_OK] = WA_API_OK,
    [FM_ERR_NO_CARD] = WA_API_NO_CARD,
    [FM_ERR_CARD_GONE] = WA_API_CARD_GONE,
    [FM_ERR_NOT_FOUND] = WA_API_NOT_FOUND,
    [FM_ERR_EXISTS] = WA_API_EXISTS,
    [FM_ERR_FULL] = WA_API_FULL,
    [FM_ERR_DENIED] = WA_API_DENIED,
    [FM_ERR_BUSY] = WA_API_BUSY,
    [FM_ERR_FIXED] = WA_API_FIXED,
    [FM_ERR_LOCKED] = WA_API_LOCKED,
    [FM_ERR_NAME] = WA_API_NAME,
    [FM_ERR_ARG] = WA_API_ARG,
    [FM_ERR_IO] = WA_API_IO,
    [FM_ERR_NOT_EMPTY] = WA_API_NOT_EMPTY,
    [FM_ERR_SAME] = WA_API_SAME,
    [FM_ERR_TOO_BIG] = WA_API_TOO_BIG,
    [FM_ERR_ARMED] = WA_API_ARMED,
    [FM_ERR_LOGGING] = WA_API_LOGGING,
    [FM_ERR_CANCELLED] = WA_API_CANCELLED,
};

/** @brief The kind words of fm_kind_t, as the list carries them. */
static const char *const k_kind[FM_KIND_COUNT] = {
    [FM_KIND_FOLDER] = "FOLDER", [FM_KIND_RUN] = "RUN",
    [FM_KIND_REPORT] = "REPORT", [FM_KIND_KST] = "KST",
    [FM_KIND_PROFILE] = "PROFILE", [FM_KIND_FAULT] = "FAULT",
    [FM_KIND_CSV] = "CSV", [FM_KIND_JSON] = "JSON",
    [FM_KIND_TEXT] = "TEXT", [FM_KIND_OTHER] = "OTHER",
};

/** @brief The place words of fm_place_t. */
static const char *const k_place[] = {
    [FM_PLACE_FREE] = "FREE", [FM_PLACE_FIXED] = "FIXED",
    [FM_PLACE_LOCKED] = "LOCKED", [FM_PLACE_FAULT] = "FAULT",
};

/* -------------------------------------------------------------- reasons */

wa_api_err_t wa_api_of(fm_err_t e)
{
    return ((unsigned)e < (unsigned)FM_ERR_COUNT) ? k_of[e] : WA_API_IO;
}

/**
 * @brief The entry of @p e, IO's for a value outside the enum.
 * @param e  the reason
 * @return its entry
 */
static const why_t *why(wa_api_err_t e)
{
    return &k_why[((unsigned)e < (unsigned)WA_API_COUNT) ? e : WA_API_IO];
}

const char *wa_api_word(wa_api_err_t e)
{
    return why(e)->word;
}

unsigned wa_api_http(wa_api_err_t e)
{
    return why(e)->http;
}

const char *wa_api_status_line(wa_api_err_t e)
{
    switch (wa_api_http(e)) {
    case 400u:
        return "400 Bad Request";
    case 403u:
        return "403 Forbidden";
    case 404u:
        return "404 Not Found";
    case 409u:
        return "409 Conflict";
    case 413u:
        return "413 Content Too Large";
    case 415u:
        return "415 Unsupported Media Type";
    case 422u:
        return "422 Unprocessable Content";
    case 423u:
        return "423 Locked";
    case 500u:
        return "500 Internal Server Error";
    case 503u:
        return "503 Service Unavailable";
    case 507u:
        return "507 Insufficient Storage";
    default:
        return "200 OK";
    }
}

const char *wa_api_text(wa_api_err_t e, bool de)
{
    const p_text_t t = why(e)->text;
    if (t == P_COUNT) {
        return "";
    }
    return (de && k_de[t] != NULL) ? k_de[t] : k_en[t];
}

/**
 * @brief A JSON string: @p s between quotes.
 * @param o  the output
 * @param s  the text
 */
static void json_str(wa_out_t *o, const char *s)
{
    wa_out_str(o, "\"");
    wa_out_json(o, s);
    wa_out_str(o, "\"");
}

/**
 * @brief A JSON member: ",\"key\":" with no comma before the first.
 * @param o      the output
 * @param key    the member's name, plain ASCII
 * @param first  the object's first member
 */
static void json_key(wa_out_t *o, const char *key, bool first)
{
    wa_out_str(o, first ? "\"" : ",\"");
    wa_out_str(o, key);
    wa_out_str(o, "\":");
}

/**
 * @brief A JSON boolean.
 * @param o  the output
 * @param b  the value
 */
static void json_bool(wa_out_t *o, bool b)
{
    wa_out_str(o, b ? "true" : "false");
}

/**
 * @brief A JSON number from a signed value.
 * @param o  the output
 * @param v  the value
 */
static void json_i64(wa_out_t *o, int64_t v)
{
    if (v < 0) {
        wa_out_str(o, "-");
        wa_out_u64(o, (uint64_t)(-(v + 1)) + 1u);
    } else {
        wa_out_u64(o, (uint64_t)v);
    }
}

void wa_api_error_json(wa_out_t *o, wa_api_err_t e, bool de)
{
    wa_out_str(o, "{");
    json_key(o, "error", true);
    json_str(o, wa_api_word(e));
    json_key(o, "text", false);
    json_str(o, wa_api_text(e, de));
    wa_out_str(o, "}");
}

void wa_api_status_json(wa_out_t *o, const wa_api_status_t *st)
{
    const bool gate_open = (st->gate == FM_OK);
    wa_out_str(o, "{");
    json_key(o, "name", true);
    json_str(o, st->name);
    json_key(o, "version", false);
    json_str(o, st->version);
    json_key(o, "lang", false);
    json_str(o, st->de ? "de" : "en");
    /* No write goes through the API: the gate's word when it is shut,
     * READ_ONLY when it is open. */
    json_key(o, "writes", false);
    json_str(o, "closed");
    json_key(o, "why", false);
    json_str(o, gate_open ? "READ_ONLY" : wa_api_word(wa_api_of(st->gate)));
    json_key(o, "card", false);
    wa_out_str(o, "{");
    json_key(o, "mounted", true);
    json_bool(o, st->mounted);
    json_key(o, "mount", false);
    wa_out_u64(o, st->mount);
    json_key(o, "name", false);
    json_str(o, (st->card != NULL) ? st->card : "");
    json_key(o, "total", false);
    wa_out_u64(o, st->total);
    json_key(o, "free", false);
    wa_out_u64(o, st->free);
    wa_out_str(o, "}");
    json_key(o, "page", false);
    wa_out_u64(o, FM_PAGE_MAX);
    json_key(o, "file_max", false);
    wa_out_u64(o, TEXT_BUF_FILE_MAX);
    json_key(o, "lines_max", false);
    wa_out_u64(o, TEXT_BUF_ENDS_MAX);
    json_key(o, "user_logs", false);
    json_str(o, (st->user_logs != NULL && st->user_logs[0] != '\0')
                    ? st->user_logs : CARD_USER_LOGS);
    wa_out_str(o, "}");
}

/* ---------------------------------------------------------- the query */

/**
 * @brief The raw value of parameter @p key: the bytes after "key=" up to
 *        the next "&", at the query's start or after an "&".
 * @param query  the query string
 * @param key    the parameter's name
 * @param n      takes the value's bytes
 * @return the value, inside @p query; NULL when not given
 */
static const char *q_raw(const char *query, const char *key, size_t *n)
{
    const size_t k = strlen(key);
    const char *p = query;
    while (*p != '\0') {
        const size_t len = strcspn(p, "&");
        if (len > k && strncmp(p, key, k) == 0 && p[k] == '=') {
            *n = len - k - 1u;
            return p + k + 1u;
        }
        if (len == k && strncmp(p, key, k) == 0) {
            *n = 0u;               /* "key" alone: given, empty */
            return p + k;
        }
        p += len;
        if (*p == '&') {
            ++p;
        }
    }
    return NULL;
}

/**
 * @brief Parameter @p key as a path from the card's root.
 * @param query  the query string
 * @param key    the parameter's name
 * @param out    takes the path; FM_PATH_SIZE bytes
 * @return WA_API_OK, the root for a value not given or empty; WA_API_NAME
 *         for one wa_path_decode() or fm_path_check() refuses
 */
static wa_api_err_t q_path(const char *query, const char *key, char *out)
{
    size_t n = 0u;
    const char *v = q_raw(query, key, &n);
    out[0] = '\0';
    if (v == NULL || n == 0u) {
        return WA_API_OK;
    }
    if (wa_path_decode(v, n, out, FM_PATH_SIZE) != WA_PATH_OK
        || fm_path_check(out) != FM_PATH_OK) {
        out[0] = '\0';
        return WA_API_NAME;
    }
    return WA_API_OK;
}

/**
 * @brief Parameter @p key as 0 or 1.
 * @param query  the query string
 * @param key    the parameter's name
 * @param out    takes the value; false when not given
 * @return false for a value other than "0" and "1"
 */
static bool q_flag(const char *query, const char *key, bool *out)
{
    size_t n = 0u;
    const char *v = q_raw(query, key, &n);
    *out = false;
    if (v == NULL) {
        return true;
    }
    if (n != 1u || (v[0] != '0' && v[0] != '1')) {
        return false;
    }
    *out = (v[0] == '1');
    return true;
}

/**
 * @brief Parameter "mount", a mount number of 1 to 10 digits.
 * @param query  the query string
 * @param io     the card, for the mount now when not given
 * @param out    takes the number
 * @return false for a value that is not a number up to UINT32_MAX
 */
static bool q_mount(const char *query, const fm_io_t *io, unsigned *out)
{
    size_t n = 0u;
    const char *v = q_raw(query, "mount", &n);
    if (v == NULL) {
        *out = io->mounts(io->ctx);
        return true;
    }
    if (n == 0u || n > 10u) {
        return false;
    }
    uint64_t m = 0u;
    for (size_t i = 0u; i < n; ++i) {
        if (v[i] < '0' || v[i] > '9') {
            return false;
        }
        m = m * 10u + (uint64_t)(v[i] - '0');
    }
    if (m > UINT32_MAX) {
        return false;
    }
    *out = (unsigned)m;
    return true;
}

/**
 * @brief Parameter "go" as a page.
 * @param query  the query string
 * @param out    takes the page; FM_GO_FIRST when not given
 * @return false for a word the API does not take
 */
static bool q_go(const char *query, fm_go_t *out)
{
    static const struct {
        const char *word;
        fm_go_t go;
    } k_go[] = {
        { "first", FM_GO_FIRST }, { "next", FM_GO_NEXT },
        { "prev", FM_GO_PREV },   { "last", FM_GO_LAST },
        { "same", FM_GO_SAME },
    };
    size_t n = 0u;
    const char *v = q_raw(query, "go", &n);
    *out = FM_GO_FIRST;
    if (v == NULL) {
        return true;
    }
    for (size_t i = 0u; i < sizeof(k_go) / sizeof(k_go[0]); ++i) {
        if (strlen(k_go[i].word) == n && strncmp(v, k_go[i].word, n) == 0) {
            *out = k_go[i].go;
            return true;
        }
    }
    return false;
}

/**
 * @brief The folder and the name of @p path, which is not the root.
 * @param path  a path fm_path_check() takes
 * @param dir   takes the folder; FM_PATH_SIZE bytes
 * @param name  takes the name; FM_NAME_BUF bytes
 * @return WA_API_REQUEST for the root, WA_API_NAME for a name the walk
 *         does not list
 */
static wa_api_err_t split(const char *path, char *dir, char *name)
{
    const char *base = fm_path_base(path);
    const size_t n = strlen(base);
    if (n == 0u) {
        return WA_API_REQUEST;
    }
    if (n >= FM_NAME_BUF) {
        return WA_API_NAME;
    }
    memcpy(name, base, n + 1u);
    memcpy(dir, path, strlen(path) + 1u);
    (void)fm_path_up(dir);
    return WA_API_OK;
}

/**
 * @brief An empty request with the API's log folders.
 * @param api   the API
 * @param kind  what it asks
 * @return the request
 */
static fm_req_t *fresh_req(wa_api_t *api, fm_req_kind_t kind)
{
    fm_req_t *req = api->req;
    memset(req, 0, sizeof(*req));
    req->kind = kind;
    memcpy(req->user_logs, api->user_logs, sizeof(req->user_logs));
    memcpy(req->auto_logs, api->auto_logs, sizeof(req->auto_logs));
    return req;
}

/* ------------------------------------------------------------- set-up */

void wa_api_init(wa_api_t *api, const fm_runner_t *run, fm_req_t *req)
{
    memset(api, 0, sizeof(*api));
    api->run = run;
    api->req = req;
}

void wa_api_set_text(wa_api_t *api, uint8_t *text, size_t cap,
                     uint32_t *ends, size_t ends_cap)
{
    api->text = text;
    api->text_cap = (text != NULL) ? cap : 0u;
    api->ends = ends;
    api->ends_cap = (ends != NULL) ? ends_cap : 0u;
    memset(&api->tb, 0, sizeof(api->tb));
}

/**
 * @brief A log folder into @p out, "" for none.
 * @param out  CARD_DIR_MAX bytes
 * @param dir  the folder, or NULL
 */
static void set_dir(char *out, const char *dir)
{
    out[0] = '\0';
    if (dir != NULL && strlen(dir) < CARD_DIR_MAX) {
        memcpy(out, dir, strlen(dir) + 1u);
    }
}

void wa_api_set_logs(wa_api_t *api, const char *user_logs,
                     const char *auto_logs)
{
    set_dir(api->user_logs, user_logs);
    set_dir(api->auto_logs, auto_logs);
}

/* ---------------------------------------------------------- the list */

wa_api_err_t wa_api_list(wa_api_t *api, const char *query)
{
    if (query == NULL) {
        query = "";
    }
    char path[FM_PATH_SIZE];
    const wa_api_err_t pe = q_path(query, "path", path);
    if (pe != WA_API_OK) {
        return pe;
    }
    fm_go_t go;
    bool key_dir = false;
    if (!q_go(query, &go) || !q_flag(query, "key_dir", &key_dir)) {
        return WA_API_REQUEST;
    }
    fm_entry_t key;
    memset(&key, 0, sizeof(key));
    size_t n = 0u;
    const char *kv = q_raw(query, "key", &n);
    const bool keyed = (go == FM_GO_NEXT || go == FM_GO_PREV
                        || go == FM_GO_SAME);
    if (keyed) {
        /* A name the walk lists: up to FM_LIST_NAME_MAX bytes, no "/". */
        if (kv == NULL || n == 0u
            || wa_path_decode(kv, n, key.name, sizeof(key.name))
                   != WA_PATH_OK
            || strchr(key.name, '/') != NULL) {
            return WA_API_REQUEST;
        }
        key.is_dir = key_dir;
    }
    fm_req_t *req = fresh_req(api, FM_REQ_PAGE);
    req->go = go;
    fm_tree_at(&req->tree, path, keyed ? &key : NULL);
    return wa_api_of(fm_req_run(req, api->run));
}

void wa_api_list_json(const wa_api_t *api, wa_out_t *o)
{
    const fm_tree_t *t = &api->req->tree;
    wa_out_str(o, "{");
    json_key(o, "path", true);
    json_str(o, t->dir);
    json_key(o, "mount", false);
    wa_out_u64(o, t->mount);
    json_key(o, "ahead", false);
    wa_out_u64(o, (uint64_t)(t->ahead > 0 ? t->ahead : 0));
    json_key(o, "total", false);
    wa_out_u64(o, (uint64_t)(t->total > 0 ? t->total : 0));
    json_key(o, "unlisted", false);
    wa_out_u64(o, t->unlisted);
    json_key(o, "prev", false);
    json_bool(o, fm_tree_has_prev(t));
    json_key(o, "next", false);
    json_bool(o, fm_tree_has_next(t));
    json_key(o, "entries", false);
    wa_out_str(o, "[");
    for (int i = 0; i < t->held; ++i) {
        const fm_entry_t *e = &t->page[i];
        wa_out_str(o, (i == 0) ? "{" : ",{");
        json_key(o, "n", true);
        json_str(o, e->name);
        json_key(o, "d", false);
        json_bool(o, e->is_dir);
        json_key(o, "s", false);
        wa_out_u64(o, e->size);
        json_key(o, "k", false);
        json_str(o, (e->kind < FM_KIND_COUNT) ? k_kind[e->kind] : "OTHER");
        json_key(o, "p", false);
        json_str(o, (e->place <= FM_PLACE_FAULT) ? k_place[e->place]
                                                 : "FREE");
        json_key(o, "b", false);
        json_bool(o, e->busy);
        wa_out_str(o, "}");
    }
    wa_out_str(o, "]}");
}

/* ---------------------------------------------------------- the info */

wa_api_err_t wa_api_info(wa_api_t *api, const char *query)
{
    if (query == NULL) {
        query = "";
    }
    char path[FM_PATH_SIZE];
    const wa_api_err_t pe = q_path(query, "path", path);
    if (pe != WA_API_OK) {
        return pe;
    }
    bool is_dir = false;
    unsigned mount = 0u;
    if (!q_flag(query, "dir", &is_dir)
        || !q_mount(query, api->run->io, &mount)) {
        return WA_API_REQUEST;
    }
    fm_req_t *req = fresh_req(api, FM_REQ_INFO);
    const wa_api_err_t se = split(path, req->dir, req->name);
    if (se != WA_API_OK) {
        return se;
    }
    req->is_dir = is_dir;
    req->mount = mount;
    return wa_api_of(fm_req_run(req, api->run));
}

void wa_api_info_json(const wa_api_t *api, wa_out_t *o)
{
    const fm_req_t *req = api->req;
    char path[FM_PATH_SIZE];
    if (fm_path_join(req->dir, req->name, req->is_dir, path, sizeof(path))
        != FM_PATH_OK) {
        path[0] = '\0';
    }
    wa_out_str(o, "{");
    json_key(o, "path", true);
    json_str(o, path);
    json_key(o, "d", false);
    json_bool(o, req->is_dir);
    json_key(o, "mount", false);
    wa_out_u64(o, req->mount);
    json_key(o, "s", false);
    wa_out_u64(o, req->info.size);
    json_key(o, "t", false);
    json_i64(o, req->info.mtime);
    json_key(o, "entries", false);
    wa_out_u64(o, req->info.entries);
    json_key(o, "unlisted", false);
    wa_out_u64(o, req->info.unlisted);
    json_key(o, "time", false);
    wa_out_str(o, "\"");
    if (wall_stamp_known(req->info.mtime)) {
        wa_out_time(o, wall_local_s(req->info.mtime));
    }
    wa_out_str(o, "\"}");
}

/* ---------------------------------------------------------- the file */

/**
 * @brief The API's reason for a refused text.
 * @param e  text_buf_load_done()'s answer
 * @return the reason
 */
static wa_api_err_t text_why(text_buf_err_t e)
{
    switch (e) {
    case TEXT_BUF_OK:
        return WA_API_OK;
    case TEXT_BUF_NOT_TEXT:
        return WA_API_NOT_TEXT;
    case TEXT_BUF_TOO_MANY:
        return WA_API_TOO_MANY_LINES;
    case TEXT_BUF_TOO_LARGE:
        return WA_API_TOO_BIG;
    case TEXT_BUF_ARG:
    case TEXT_BUF_FULL:
    default:
        return WA_API_IO;
    }
}

/**
 * @brief The CRC-32 of the loaded file's bytes, read a chunk at a time.
 * @param api  the API, a file loaded
 * @return the CRC
 */
static uint32_t file_crc(const wa_api_t *api)
{
    uint32_t crc = 0u;
    const size_t total = wa_api_file_size(api);
    size_t at = 0u;
    while (at < total) {
        const size_t got = wa_api_file_read(api, at, api->run->chunk,
                                            api->run->size);
        if (got == 0u) {
            break;
        }
        crc = wa_crc32(crc, api->run->chunk, got);
        at += got;
    }
    return crc;
}

wa_api_err_t wa_api_file(wa_api_t *api, const char *query)
{
    if (query == NULL) {
        query = "";
    }
    memset(&api->tb, 0, sizeof(api->tb));
    api->etag = 0u;
    char path[FM_PATH_SIZE];
    const wa_api_err_t pe = q_path(query, "path", path);
    if (pe != WA_API_OK) {
        return pe;
    }
    unsigned mount = 0u;
    if (!q_mount(query, api->run->io, &mount)) {
        return WA_API_REQUEST;
    }
    fm_req_t *req = fresh_req(api, FM_REQ_JOB);
    char name[FM_NAME_BUF];
    const wa_api_err_t se = split(path, req->dir, name);
    if (se != WA_API_OK) {
        return se;
    }
    if (api->text == NULL || api->ends == NULL
        || text_buf_init(&api->tb, api->text, api->text_cap, api->ends,
                         api->ends_cap) != TEXT_BUF_OK) {
        return WA_API_MEMORY;
    }
    /* The load's start asks the rules with the bench as it stands. */
    fm_bench_t bench;
    memset(&bench, 0, sizeof(bench));
    api->run->bench(&bench, api->run->ctx);
    fm_req_bench(req, &bench);
    (void)fm_job_load(&req->job, &bench, mount, req->dir, name,
                      text_buf_load_area(&api->tb),
                      text_buf_room(&api->tb));
    const fm_err_t le = fm_req_run(req, api->run);
    if (le != FM_OK) {
        memset(&api->tb, 0, sizeof(api->tb));
        return wa_api_of(le);
    }
    const wa_api_err_t te = text_why(
        text_buf_load_done(&api->tb, (size_t)req->job.done));
    if (te != WA_API_OK) {
        memset(&api->tb, 0, sizeof(api->tb));
        return te;
    }
    api->etag = file_crc(api);
    return WA_API_OK;
}

size_t wa_api_file_size(const wa_api_t *api)
{
    return (api != NULL) ? text_buf_size(&api->tb) : 0u;
}

size_t wa_api_file_read(const wa_api_t *api, size_t at, void *out, size_t n)
{
    return (api != NULL) ? text_buf_read(&api->tb, at, out, n) : 0u;
}

/**
 * @brief The X-Rcbench-Eol word of the loaded file.
 * @param tb  the file
 * @return "mixed" for two kinds or more, else its kind's word
 */
static const char *eol_word(const text_buf_t *tb)
{
    if (text_buf_mixed(tb)) {
        return "mixed";
    }
    switch (text_buf_eol(tb)) {
    case TEXT_EOL_CRLF:
        return "crlf";
    case TEXT_EOL_CR:
        return "cr";
    case TEXT_EOL_LF:
    case TEXT_EOL_KINDS:
    default:
        return "lf";
    }
}

size_t wa_api_file_head(const wa_api_t *api, char *out, size_t cap)
{
    const int n = snprintf(out, cap,
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/plain; charset=utf-8\r\n"
                           "Content-Length: %lu\r\n"
                           "ETag: \"%08" PRIx32 "\"\r\n"
                           "X-Rcbench-Eol: %s\r\n"
                           "X-Rcbench-Bom: %d\r\n"
                           "X-Rcbench-Lines: %lu\r\n"
                           "Cache-Control: no-store\r\n\r\n",
                           (unsigned long)wa_api_file_size(api), api->etag,
                           eol_word(&api->tb),
                           text_buf_bom(&api->tb) ? 1 : 0,
                           (unsigned long)text_buf_lines(&api->tb));
    return (n > 0 && (size_t)n < cap) ? (size_t)n : 0u;
}
