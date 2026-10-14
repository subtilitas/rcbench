/*
 * The panel's Wi-Fi over esp_wifi, esp_http_server and a UDP (user datagram
 * protocol) socket for the mDNS name.
 *
 * Three tasks touch the session: the UI loop (start, close, tick), the event
 * loop (what the radio reports) and the HTTP server (requests).  Everything
 * they share is under one mutex, held for copies and counters only, never
 * across a socket or a card read.  A fourth task, the worker, owns bringing
 * the radio up and down: it compares what the session wants with what runs
 * and acts, so a stop that waits for a download to end never waits on the
 * UI loop.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi_access.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "storage.h"
#include "wa_mdns.h"
#include "wa_page.h"
#include "wa_send.h"
#include "wa_text.h"
#include "wa_walk.h"
#include "wa_zip.h"

static const char *TAG = "wifi";

#define NVS_NS        "wifi"
#define WORKER_STACK  5120u
#define MDNS_STACK    3072u
#define HTTPD_STACK   8192u
/* Below the UI loop's priority would starve the page; above it a ZIP
 * download would starve the frame.  Equal shares the core by time slice. */
#define NET_PRIORITY  1u
#define WORKER_PRIO   2u
/* Core 0, with the UI: core 1 runs the control task, which owns STOP and
 * the heartbeat, and nothing of the radio's goes there. */
#define NET_CORE      0
#define IO_CHUNK      4096u
#define SCAN_READ     24u

static struct {
    SemaphoreHandle_t lock;

    /* Under the lock. */
    wa_t            w;
    wa_conf_t       conf;
    bool            conf_dirty;
    bool            conf_erase;
    wa_scan_t       scan;
    bool            scanned;
    fw_upload_t     up;
    bool            taken;
    fw_offer_t      offer;
    uint32_t        gen;          /* sessions started                   */

    /* Set once. */
    char            name[WA_NAME_MAX];
    uint32_t        slot;
    fw_image_info_t running;
    TaskHandle_t    worker;

    /* The worker's. */
    bool            net_ready;
    bool            radio_up;
    uint32_t        up_gen;
    wa_mode_t       up_mode;
    esp_netif_t    *netif;
    httpd_handle_t  httpd;
    atomic_bool     mdns_alive;
    uint8_t         ip[4];

    atomic_bool     abort;        /* requests under way end now         */
    atomic_bool     mdns_run;
    atomic_bool     down;
    atomic_bool     de;
} s;

/*
 * The server's buffers, in PSRAM (pseudo-static random-access memory) and
 * taken once: the server runs one request at a time, and internal RAM is
 * what the display and the radio are short of.  The mDNS task has its own.
 */
typedef struct {
    wa_out_t        out;
    char            text[512];
    uint8_t         io[IO_CHUNK];
    char            disp[WA_NAME_BYTES * 3u + 64u];
    char            head[WA_NAME_BYTES * 3u + 64u + 192u];
    char            path[WA_PATH_MAX + 1u];
    wa_send_t       snd;
    uint8_t         mdns_q[WA_MDNS_MAX];
    uint8_t         mdns_out[WA_MDNS_MAX];
} bufs_t;

static bufs_t *b;

static uint32_t ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void lock(void)   { xSemaphoreTake(s.lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s.lock); }

static void poke(void)
{
    if (s.worker != NULL) {
        xTaskNotifyGive(s.worker);
    }
}

static uint32_t rand32(void *ctx)
{
    (void)ctx;
    return esp_random();
}

/* ------------------------------------------------------------ the store */

static bool nvs_conf_load(void *ctx, wa_conf_t *out)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    wa_conf_defaults(out);
    uint8_t code = 1u;
    if (nvs_get_u8(h, "code", &code) == ESP_OK) {
        out->code_on = (code != 0u);
    }
    size_t sl = sizeof(out->ssid), pl = sizeof(out->pass);
    if (nvs_get_str(h, "ssid", out->ssid, &sl) == ESP_OK
        && nvs_get_str(h, "pass", out->pass, &pl) == ESP_OK) {
        out->have_net = true;
    } else {
        out->ssid[0] = out->pass[0] = '\0';
    }
    nvs_close(h);
    return true;
}

static bool nvs_conf_save(void *ctx, const wa_conf_t *c)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    bool ok = nvs_set_u8(h, "code", c->code_on ? 1u : 0u) == ESP_OK;
    if (c->have_net) {
        ok = ok && nvs_set_str(h, "ssid", c->ssid) == ESP_OK
             && nvs_set_str(h, "pass", c->pass) == ESP_OK;
    } else {
        const esp_err_t a = nvs_erase_key(h, "ssid");
        const esp_err_t b = nvs_erase_key(h, "pass");
        ok = ok && (a == ESP_OK || a == ESP_ERR_NVS_NOT_FOUND)
             && (b == ESP_OK || b == ESP_ERR_NVS_NOT_FOUND);
    }
    ok = ok && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

static bool nvs_conf_erase(void *ctx)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    const bool ok = nvs_erase_all(h) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

static const wa_store_t k_store = {
    nvs_conf_load, nvs_conf_save, nvs_conf_erase, NULL,
};

/* ------------------------------------------------------------- the card */

typedef struct {
    DIR   *d;
    size_t len;                 /* bytes of the folder's own path */
    char   path[sizeof(STORAGE_MOUNT_POINT) + WA_PATH_MAX + WA_NAME_BYTES + 2u];
} card_dir_t;

static size_t card_path(char *out, size_t cap, const char *rel)
{
    const int n = snprintf(out, cap, "%s%s%s", STORAGE_MOUNT_POINT,
                           (rel[0] != '\0') ? "/" : "", rel);
    return (n < 0 || (size_t)n >= cap) ? 0u : (size_t)n;
}

static void *card_opendir(void *ctx, const char *path)
{
    (void)ctx;
    if (!storage_mounted()) {
        return NULL;
    }
    card_dir_t *d = heap_caps_malloc(sizeof(*d), MALLOC_CAP_SPIRAM);
    if (d == NULL) {
        return NULL;
    }
    d->len = card_path(d->path, sizeof(d->path), path);
    d->d = (d->len > 0u) ? opendir(d->path) : NULL;
    if (d->d == NULL) {
        heap_caps_free(d);
        return NULL;
    }
    return d;
}

static int card_readdir(void *ctx, void *dir, wa_dirent_t *out)
{
    (void)ctx;
    card_dir_t *d = dir;
    for (;;) {
        errno = 0;
        const struct dirent *e = readdir(d->d);
        if (e == NULL) {
            return (errno != 0) ? -1 : 0;
        }
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        const size_t nl = strnlen(e->d_name, sizeof(e->d_name));
        if (d->len + 1u + nl + 1u > sizeof(d->path)
            || nl + 1u > sizeof(out->name)) {
            return -1;
        }
        d->path[d->len] = '/';
        memcpy(d->path + d->len + 1u, e->d_name, nl);
        d->path[d->len + 1u + nl] = '\0';
        struct stat st;
        const int r = stat(d->path, &st);
        d->path[d->len] = '\0';
        if (r != 0) {
            return -1;
        }
        memset(out, 0, sizeof(*out));
        memcpy(out->name, e->d_name, nl);
        out->dir = S_ISDIR(st.st_mode);
        out->size = out->dir ? 0u : (uint64_t)st.st_size;
        out->mtime = (int64_t)st.st_mtime;
        return 1;
    }
}

static void card_closedir(void *ctx, void *dir)
{
    (void)ctx;
    card_dir_t *d = dir;
    (void)closedir(d->d);
    heap_caps_free(d);
}

static void *card_open(void *ctx, const char *path)
{
    (void)ctx;
    char full[sizeof(STORAGE_MOUNT_POINT) + WA_PATH_MAX + 2u];
    if (!storage_mounted() || card_path(full, sizeof(full), path) == 0u) {
        return NULL;
    }
    const int fd = open(full, O_RDONLY);
    return (fd < 0) ? NULL : (void *)(intptr_t)(fd + 1);
}

static int32_t card_read(void *ctx, void *file, uint8_t *dst, uint32_t len)
{
    (void)ctx;
    const ssize_t n = read((int)(intptr_t)file - 1, dst, len);
    return (n < 0) ? -1 : (int32_t)n;
}

static void card_close(void *ctx, void *file)
{
    (void)ctx;
    (void)close((int)(intptr_t)file - 1);
}

static const wa_fs_t k_card = {
    card_opendir, card_readdir, card_closedir,
    card_open, card_read, card_close, NULL,
};

/* ------------------------------------------------------------- the page */

/* A request under way: counted, so the idle time-out waits for it. */
static bool req_begin(void)
{
    lock();
    const bool serving = wa_serving(&s.w);
    if (serving) {
        wa_request_begin(&s.w, ms());
    }
    unlock();
    return serving;
}

static void req_end(void)
{
    lock();
    wa_request_end(&s.w, ms());
    unlock();
}

/* The access code check, the cookie's.  Counted like a typed one. */
static wa_code_t req_code(httpd_req_t *r)
{
    char hdr[160], given[WA_SECRET_LEN + 2u];
    given[0] = '\0';
    if (httpd_req_get_hdr_value_str(r, "Cookie", hdr, sizeof(hdr)) == ESP_OK
        && !wa_cookie_get(hdr, WA_COOKIE, given, sizeof(given))) {
        given[0] = '\0';
    }
    lock();
    const wa_code_t c = wa_code_check(&s.w, given, s.conf.code_on);
    unlock();
    return c;
}

static void page_info(wa_page_info_t *pi)
{
    pi->name = s.name;
    pi->version = s.running.version_text;
    pi->slot = s.slot;
    pi->de = atomic_load(&s.de);
}

static bool sink_chunk(void *ctx, const void *data, size_t n)
{
    if (atomic_load(&s.abort)) {
        return false;
    }
    return httpd_resp_send_chunk(ctx, data, (ssize_t)n) == ESP_OK;
}

static esp_err_t send_page(httpd_req_t *r, bool code_form, bool wrong)
{
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    wa_page_info_t pi;
    page_info(&pi);
    wa_out_init(&b->out, sink_chunk, r);
    if (code_form) {
        wa_page_code(&b->out, &pi, wrong);
    } else {
        wa_page_main(&b->out, &pi, &k_card);
    }
    if (!wa_out_flush(&b->out)) {
        return ESP_FAIL;
    }
    return httpd_resp_send_chunk(r, NULL, 0);
}

static esp_err_t closed(httpd_req_t *r)
{
    httpd_resp_set_status(r, "503 Service Unavailable");
    return httpd_resp_sendstr(r, "");
}

static esp_err_t h_root(httpd_req_t *r)
{
    if (!req_begin()) {
        return closed(r);
    }
    const wa_code_t c = req_code(r);
    esp_err_t e;
    if (c == WA_CODE_CLOSED) {
        e = closed(r);
    } else {
        e = send_page(r, c != WA_CODE_OK, false);
    }
    req_end();
    return e;
}

static esp_err_t h_code(httpd_req_t *r)
{
    if (!req_begin()) {
        return closed(r);
    }
    char body[96];
    size_t n = 0u;
    esp_err_t e = ESP_OK;
    while (n < (size_t)r->content_len && n < sizeof(body) - 1u) {
        const int got = httpd_req_recv(r, body + n, sizeof(body) - 1u - n);
        if (got <= 0) {
            e = ESP_FAIL;
            break;
        }
        n += (size_t)got;
    }
    body[n] = '\0';
    char given[WA_SECRET_LEN + 2u];
    if (!wa_form_get(body, n, "code", given, sizeof(given))) {
        given[0] = '\0';
    }
    lock();
    const wa_code_t c = (e == ESP_OK) ? wa_code_check(&s.w, given, true)
                                      : WA_CODE_CLOSED;
    char cookie[64];
    snprintf(cookie, sizeof(cookie),
             WA_COOKIE "=%s; Path=/; HttpOnly; SameSite=Strict", s.w.code);
    unlock();
    if (e != ESP_OK) {
        req_end();
        return ESP_FAIL;
    }
    if (c == WA_CODE_OK) {
        httpd_resp_set_status(r, "303 See Other");
        httpd_resp_set_hdr(r, "Set-Cookie", cookie);
        httpd_resp_set_hdr(r, "Location", "/");
        e = httpd_resp_send(r, "", 0);
    } else if (c == WA_CODE_CLOSED) {
        e = closed(r);
    } else {
        e = send_page(r, true, true);
    }
    req_end();
    return e;
}

static esp_err_t refused(httpd_req_t *r, const char *status)
{
    httpd_resp_set_status(r, status);
    return httpd_resp_sendstr(r, "");
}

static bool upload_text(void *ctx, const void *data, size_t n)
{
    char *t = ctx;
    const size_t have = strlen(t);
    if (have + n >= sizeof(b->text)) {
        return false;
    }
    memcpy(t + have, data, n);
    t[have + n] = '\0';
    return true;
}

static esp_err_t answer_upload(httpd_req_t *r, fw_err_t e,
                               const fw_image_info_t *info, bool same,
                               bool cut)
{
    b->text[0] = '\0';
    wa_out_init(&b->out, upload_text, b->text);
    wa_page_upload_result(&b->out, atomic_load(&s.de), e, info, same);
    (void)wa_out_flush(&b->out);
    httpd_resp_set_type(r, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    if (cut) {
        /* The body was not read: the connection goes with the answer. */
        httpd_resp_set_hdr(r, "Connection", "close");
    }
    const esp_err_t s_ = httpd_resp_sendstr(r, b->text);
    return cut ? ESP_FAIL : s_;
}

/*
 * The upload: the body as sent, Content-Length bytes.  A size the receiver
 * refuses is answered before a byte is read, and the connection closes
 * with the answer.
 */
static esp_err_t h_update(httpd_req_t *r)
{
    if (!req_begin()) {
        return closed(r);
    }
    if (req_code(r) != WA_CODE_OK) {
        req_end();
        return refused(r, "403 Forbidden");
    }
    const uint32_t bytes = (r->content_len > UINT32_MAX)
                           ? UINT32_MAX : (uint32_t)r->content_len;
    static fw_image_info_t none;

    /* Every refusal the receiver makes on the size alone, before a byte
     * is stored or memory is taken. */
    lock();
    fw_err_t e = FW_OK;
    if (s.taken) {
        e = FW_ERR_BUSY;
    } else if (s.up.state != FW_UP_RECEIVING && s.up.state != FW_UP_STAGED) {
        heap_caps_free(s.up.buf);
        s.up.buf = NULL;
        s.up.cap = 0u;
    }
    if (e == FW_OK) {
        e = fw_upload_begin(&s.up, bytes);   /* MEMORY when all else is */
    }
    unlock();
    if (e != FW_ERR_MEMORY) {
        req_end();
        return answer_upload(r, e, &none, false, true);
    }
    uint8_t *buf = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    lock();
    s.up.buf = buf;
    s.up.cap = (buf != NULL) ? bytes : 0u;
    e = fw_upload_begin(&s.up, bytes);
    unlock();
    if (e != FW_OK) {
        req_end();
        return answer_upload(r, e, &none, false, true);
    }

    uint32_t left = bytes;
    bool whole = true;
    unsigned waits = 0u;
    while (left > 0u) {
        if (atomic_load(&s.abort)) {
            whole = false;
            break;
        }
        const int got = httpd_req_recv(r, (char *)b->io,
                                       (left < IO_CHUNK) ? left : IO_CHUNK);
        if (got == HTTPD_SOCK_ERR_TIMEOUT && ++waits < 6u) {
            continue;       /* 6 waits of the server's 5 s: 30 s quiet */
        }
        if (got <= 0) {
            whole = false;
            break;
        }
        waits = 0u;
        lock();
        e = fw_upload_add(&s.up, b->io, (uint32_t)got);
        wa_request_begin(&s.w, ms());   /* traffic: the idle clock waits */
        wa_request_end(&s.w, ms());
        unlock();
        if (e != FW_OK) {
            whole = false;
            break;
        }
        left -= (uint32_t)got;
    }

    fw_image_info_t info;
    bool same = false;
    lock();
    e = fw_upload_end(&s.up, whole);
    info = s.up.info;
    if (e == FW_OK) {
        s.offer = fw_offer(&s.running, &info);
        same = (s.offer == FW_OFFER_INSTALLED);
    } else if (s.up.state != FW_UP_RECEIVING && s.up.state != FW_UP_STAGED) {
        heap_caps_free(s.up.buf);    /* refused: nothing to keep */
        s.up.buf = NULL;
        s.up.cap = 0u;
    }
    unlock();
    req_end();
    if (!whole) {
        return ESP_FAIL;              /* the sender is gone or cut off */
    }
    return answer_upload(r, e, &info, same, false);
}

/* Raw bytes on the socket: a response whose length is known up front. */
static bool raw_send(httpd_req_t *r, const void *data, size_t n)
{
    const char *p = data;
    while (n > 0u) {
        if (atomic_load(&s.abort)) {
            return false;
        }
        const int sent = httpd_send(r, p, n);
        if (sent == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (sent <= 0) {
            return false;
        }
        p += sent;
        n -= (size_t)sent;
    }
    return true;
}

static bool raw_head(httpd_req_t *r, uint64_t length, const char *name)
{
    char *disp = b->disp;
    if (wa_disposition(disp, sizeof(b->disp), name) == 0u) {
        snprintf(disp, sizeof(b->disp), "attachment");
    }
    const int n = snprintf(b->head, sizeof(b->head),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/octet-stream\r\n"
                           "Content-Length: %llu\r\n"
                           "Content-Disposition: %s\r\n"
                           "Cache-Control: no-store\r\n\r\n",
                           (unsigned long long)length, disp);
    return n > 0 && (size_t)n < sizeof(b->head)
           && raw_send(r, b->head, (size_t)n);
}

static esp_err_t h_file(httpd_req_t *r)
{
    if (!req_begin()) {
        return closed(r);
    }
    if (req_code(r) != WA_CODE_OK) {
        req_end();
        return refused(r, "403 Forbidden");
    }
    const char *in = r->uri + strlen(WA_URL_FILE);
    const char *q = strchr(in, '?');
    const size_t n = (q != NULL) ? (size_t)(q - in) : strlen(in);
    char *path = b->path;
    if (wa_path_decode(in, n, path, sizeof(b->path)) != WA_PATH_OK) {
        req_end();
        return refused(r, "400 Bad Request");
    }
    char full[sizeof(STORAGE_MOUNT_POINT) + WA_PATH_MAX + 2u];
    struct stat st;
    if (!storage_mounted() || card_path(full, sizeof(full), path) == 0u
        || stat(full, &st) != 0 || !S_ISREG(st.st_mode)) {
        req_end();
        return refused(r, "404 Not Found");
    }
    wa_send_t *snd = &b->snd;
    if (!wa_send_begin(snd, &k_card, path, (uint64_t)st.st_size)) {
        req_end();
        return refused(r, "404 Not Found");
    }
    bool ok = raw_head(r, (uint64_t)st.st_size, wa_basename(path));
    while (ok) {
        const int32_t got = wa_send_next(snd, b->io, sizeof(b->io));
        if (got == 0) {
            break;
        }
        ok = (got > 0) && raw_send(r, b->io, (size_t)got);
    }
    wa_send_end(snd);
    req_end();
    /* A response cut short closes the connection: the browser sees fewer
     * bytes than announced and reports the download as failed. */
    return ok ? ESP_OK : ESP_FAIL;
}

static esp_err_t h_zip(httpd_req_t *r)
{
    if (!req_begin()) {
        return closed(r);
    }
    if (req_code(r) != WA_CODE_OK) {
        req_end();
        return refused(r, "403 Forbidden");
    }
    wa_zip_t *z = heap_caps_malloc(sizeof(*z), MALLOC_CAP_SPIRAM);
    wa_zip_plan_t plan;
    uint32_t *crc = NULL;
    esp_err_t res = ESP_FAIL;
    if (z == NULL || wa_zip_plan(&k_card, &plan) != WA_ZIP_OK) {
        res = refused(r, (z == NULL) ? "503 Service Unavailable"
                                     : "404 Not Found");
    } else {
        const uint64_t slots = (plan.entries > 0u) ? plan.entries : 1u;
        crc = (slots <= SIZE_MAX / sizeof(uint32_t))
              ? heap_caps_malloc((size_t)slots * sizeof(uint32_t),
                                 MALLOC_CAP_SPIRAM)
              : NULL;
        if (crc == NULL
            || wa_zip_begin(z, &k_card, &plan, crc, slots) != WA_ZIP_OK) {
            res = refused(r, "503 Service Unavailable");
        } else {
            char zname[WA_NAME_MAX + 8u];
            snprintf(zname, sizeof(zname), "%s.zip", s.name);
            bool ok = raw_head(r, plan.bytes, zname);
            while (ok) {
                const int32_t got = wa_zip_next(z, b->io, sizeof(b->io));
                if (got == 0) {
                    break;
                }
                ok = (got > 0) && raw_send(r, b->io, (size_t)got);
                if (ok) {
                    lock();               /* a long download is traffic */
                    wa_request_begin(&s.w, ms());
                    wa_request_end(&s.w, ms());
                    unlock();
                }
            }
            if (!ok) {
                wa_zip_end_early(z);
            }
            res = ok ? ESP_OK : ESP_FAIL;
        }
    }
    heap_caps_free(crc);
    heap_caps_free(z);
    req_end();
    return res;
}

static esp_err_t httpd_up(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = HTTPD_STACK;
    cfg.task_priority = NET_PRIORITY;
    cfg.core_id = NET_CORE;
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 6;
    esp_err_t e = httpd_start(&s.httpd, &cfg);
    if (e != ESP_OK) {
        s.httpd = NULL;
        return e;
    }
    static const httpd_uri_t uris[] = {
        { .uri = "/",               .method = HTTP_GET,  .handler = h_root },
        { .uri = WA_URL_CODE,       .method = HTTP_POST, .handler = h_code },
        { .uri = WA_URL_UPDATE,     .method = HTTP_POST, .handler = h_update },
        { .uri = WA_URL_ZIP,        .method = HTTP_GET,  .handler = h_zip },
        { .uri = WA_URL_FILE "*",   .method = HTTP_GET,  .handler = h_file },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); ++i) {
        (void)httpd_register_uri_handler(s.httpd, &uris[i]);
    }
    return ESP_OK;
}

/* ------------------------------------------------------------ the name */

static void mdns_task(void *arg)
{
    (void)arg;
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock >= 0) {
        const int one = 1;
        (void)setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        struct sockaddr_in a = { 0 };
        a.sin_family = AF_INET;
        a.sin_port = htons(WA_MDNS_PORT);
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        struct ip_mreq m = { 0 };
        m.imr_multiaddr.s_addr = inet_addr("224.0.0.251");
        memcpy(&m.imr_interface.s_addr, s.ip, 4);
        const uint8_t ttl = 255u;
        if (bind(sock, (struct sockaddr *)&a, sizeof(a)) == 0
            && setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m,
                          sizeof(m)) == 0) {
            (void)setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl,
                             sizeof(ttl));
            struct sockaddr_in group = { 0 };
            group.sin_family = AF_INET;
            group.sin_port = htons(WA_MDNS_PORT);
            group.sin_addr.s_addr = m.imr_multiaddr.s_addr;
            uint8_t *q = b->mdns_q, *out = b->mdns_out;
            size_t n = wa_mdns_announce(s.name, s.ip, out, WA_MDNS_MAX);
            if (n > 0u) {
                (void)sendto(sock, out, n, 0, (struct sockaddr *)&group,
                             sizeof(group));
            }
            while (atomic_load(&s.mdns_run)) {
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(sock, &fds);
                struct timeval tv = { .tv_sec = 0, .tv_usec = 250000 };
                if (select(sock + 1, &fds, NULL, NULL, &tv) <= 0) {
                    continue;
                }
                struct sockaddr_in from;
                socklen_t fl = sizeof(from);
                const int got = recvfrom(sock, q, WA_MDNS_MAX, 0,
                                         (struct sockaddr *)&from, &fl);
                if (got <= 0) {
                    continue;
                }
                const uint16_t port = ntohs(from.sin_port);
                n = wa_mdns_reply(q, (size_t)got, port, s.name, s.ip, out,
                                  WA_MDNS_MAX);
                if (n > 0u) {
                    /* A one-shot query from another port is answered to
                     * it alone; the rest to the group. */
                    const struct sockaddr_in *to =
                        (port == WA_MDNS_PORT) ? &group : &from;
                    (void)sendto(sock, out, n, 0, (const struct sockaddr *)to,
                                 sizeof(*to));
                }
            }
        }
        (void)close(sock);
    }
    atomic_store(&s.mdns_alive, false);
    xTaskNotifyGive(s.worker);
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------ the radio */

static bool auth_refused(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_MIC_FAILURE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_802_1X_AUTH_FAILED:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return true;
    default:
        return false;
    }
}

static void on_scan_done(void)
{
    uint16_t n = SCAN_READ;
    wifi_ap_record_t *rec = heap_caps_malloc(n * sizeof(*rec),
                                             MALLOC_CAP_SPIRAM);
    if (rec == NULL || esp_wifi_scan_get_ap_records(&n, rec) != ESP_OK) {
        n = 0u;
        (void)esp_wifi_clear_ap_list();
    }
    lock();
    wa_scan_clear(&s.scan);
    for (uint16_t i = 0; i < n; ++i) {
        const wifi_auth_mode_t m = rec[i].authmode;
        const bool usable = (m == WIFI_AUTH_WPA2_PSK
                             || m == WIFI_AUTH_WPA_WPA2_PSK
                             || m == WIFI_AUTH_WPA3_PSK
                             || m == WIFI_AUTH_WPA2_WPA3_PSK);
        wa_scan_add(&s.scan, rec[i].ssid,
                    strnlen((const char *)rec[i].ssid, sizeof(rec[i].ssid)),
                    rec[i].rssi, usable);
    }
    s.scanned = true;
    wa_on_scan_done(&s.w);
    unlock();
    heap_caps_free(rec);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    bool retry = false;
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_AP_START:
            lock();
            wa_on_ap_up(&s.w, ms());
            unlock();
            break;
        case WIFI_EVENT_STA_START:
            if (s.up_mode == WA_MODE_SCAN) {
                wifi_scan_config_t sc = { 0 };
                if (esp_wifi_scan_start(&sc, false) != ESP_OK) {
                    lock();
                    wa_on_failed(&s.w);
                    unlock();
                }
            } else {
                retry = true;
            }
            break;
        case WIFI_EVENT_SCAN_DONE:
            if (s.up_mode == WA_MODE_SCAN) {
                on_scan_done();
            }
            break;
        case WIFI_EVENT_STA_CONNECTED:
            lock();
            wa_on_associated(&s.w, ms());
            unlock();
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            const wifi_event_sta_disconnected_t *d = data;
            lock();
            if (s.w.state == WA_ST_JOINED || s.w.state == WA_ST_ADDRESS) {
                wa_on_join_failed(&s.w, WA_JOIN_LOST);
            } else if (s.w.state == WA_ST_JOINING) {
                if (auth_refused(d->reason)) {
                    wa_on_join_failed(&s.w, WA_JOIN_PASSWORD);
                } else {
                    retry = true;   /* until WA_JOIN_MS: NOT_FOUND */
                }
            }
            unlock();
            break;
        }
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *g = data;
        memcpy(s.ip, &g->ip_info.ip.addr, 4);
        lock();
        wa_on_address(&s.w, g->ip_info.ip.addr, ms());
        unlock();
    }
    if (retry) {
        (void)esp_wifi_connect();
    }
    poke();
}

static void net_once(void)
{
    if (s.net_ready) {
        return;
    }
    ESP_ERROR_CHECK(esp_netif_init());
    const esp_err_t e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(e);
    }
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               on_event, NULL));
    s.net_ready = true;
}

static void radio_down(void)
{
    atomic_store(&s.abort, true);
    atomic_store(&s.mdns_run, false);
    while (atomic_load(&s.mdns_alive)) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
    }
    if (s.httpd != NULL) {
        /* Waits for a request under way, which ends at its next piece. */
        (void)httpd_stop(s.httpd);
        s.httpd = NULL;
    }
    if (s.radio_up) {
        (void)esp_wifi_stop();
        (void)esp_wifi_deinit();
        if (s.netif != NULL) {
            esp_netif_destroy_default_wifi(s.netif);
            s.netif = NULL;
        }
    }
    s.radio_up = false;
    memset(s.ip, 0, sizeof(s.ip));
    atomic_store(&s.abort, false);
    atomic_store(&s.down, true);
}

static bool radio_bring_up(wa_mode_t mode, const wa_t *w, const wa_conf_t *c)
{
    net_once();
    atomic_store(&s.down, false);
    s.up_mode = mode;
    s.netif = (mode == WA_MODE_AP) ? esp_netif_create_default_wifi_ap()
                                   : esp_netif_create_default_wifi_sta();
    if (s.netif == NULL) {
        return false;
    }
    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    ic.nvs_enable = 0;   /* nothing of the radio's is kept */
    if (esp_wifi_init(&ic) != ESP_OK) {
        esp_netif_destroy_default_wifi(s.netif);
        s.netif = NULL;
        return false;
    }
    s.radio_up = true;
    (void)esp_wifi_set_storage(WIFI_STORAGE_RAM);
    wifi_config_t cfg = { 0 };
    esp_err_t e;
    if (mode == WA_MODE_AP) {
        memcpy(cfg.ap.ssid, s.name, strlen(s.name));
        cfg.ap.ssid_len = (uint8_t)strlen(s.name);
        memcpy(cfg.ap.password, w->pass, strlen(w->pass));
        cfg.ap.channel = WIFI_ACCESS_AP_CHANNEL;
        cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
        cfg.ap.max_connection = 1;
        cfg.ap.pmf_cfg.required = false;
        e = esp_wifi_set_mode(WIFI_MODE_AP);
        if (e == ESP_OK) {
            e = esp_wifi_set_config(WIFI_IF_AP, &cfg);
        }
    } else {
        e = esp_wifi_set_mode(WIFI_MODE_STA);
        if (e == ESP_OK && mode == WA_MODE_JOIN) {
            memcpy(cfg.sta.ssid, c->ssid, strnlen(c->ssid, sizeof(cfg.sta.ssid)));
            memcpy(cfg.sta.password, c->pass,
                   strnlen(c->pass, sizeof(cfg.sta.password) - 1u));
            /* WPA2 or WPA3 personal; nothing weaker, no enterprise. */
            cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            cfg.sta.pmf_cfg.capable = true;
            cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
            cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
            cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
            e = esp_wifi_set_config(WIFI_IF_STA, &cfg);
            if (e == ESP_OK) {
                (void)esp_netif_set_hostname(s.netif, s.name);
            }
        }
    }
    if (e == ESP_OK) {
        e = esp_wifi_start();
    }
    if (e == ESP_OK && mode != WA_MODE_SCAN) {
        e = httpd_up();
    }
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "radio: %s", esp_err_to_name(e));
        return false;
    }
    return true;
}

static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        lock();
        const bool want = wa_radio(&s.w);
        const uint32_t gen = s.gen;
        const wa_t w = s.w;
        const wa_conf_t c = s.conf;
        unlock();
        if (s.radio_up || !atomic_load(&s.down)) {
            if (!want || gen != s.up_gen) {
                radio_down();
                ESP_LOGI(TAG, "radio off");
            }
        }
        if (want && atomic_load(&s.down)) {
            s.up_gen = gen;
            if (!radio_bring_up(w.mode, &w, &c)) {
                radio_down();
                lock();
                if (s.gen == gen) {
                    wa_on_failed(&s.w);
                }
                unlock();
            } else {
                ESP_LOGI(TAG, "radio up, mode %d", (int)w.mode);
            }
        }
        if (s.radio_up && w.state == WA_ST_JOINED
            && !atomic_load(&s.mdns_alive) && !atomic_load(&s.mdns_run)
            && s.up_mode == WA_MODE_JOIN) {
            atomic_store(&s.mdns_run, true);
            atomic_store(&s.mdns_alive, true);
            if (xTaskCreatePinnedToCore(mdns_task, "mdns", MDNS_STACK, NULL,
                                        WORKER_PRIO, NULL, NET_CORE)
                != pdPASS) {
                atomic_store(&s.mdns_alive, false);
            }
        }
    }
}

/* ------------------------------------------------------------ the API */

esp_err_t wifi_access_init(uint32_t slot, const fw_image_info_t *running)
{
    s.lock = xSemaphoreCreateMutex();
    b = heap_caps_calloc(1, sizeof(*b), MALLOC_CAP_SPIRAM);
    if (s.lock == NULL || b == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s.slot = slot;
    s.running = *running;
    wa_init(&s.w);
    wa_conf_load(&k_store, &s.conf);
    wa_scan_clear(&s.scan);
    fw_upload_init(&s.up, NULL, 0u, slot);
    uint8_t mac[6] = { 0 };
    (void)esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    wa_name(mac, s.name);
    atomic_store(&s.down, true);
    return ESP_OK;
}

void wifi_access_set_german(bool de) { atomic_store(&s.de, de); }

wa_off_t wifi_access_start(wa_mode_t mode, wa_off_t gate)
{
    /* The worker's stack is internal RAM: taken at the first start, not at
     * every start of the panel. */
    if (s.worker == NULL && gate == WA_OFF_NONE
        && xTaskCreatePinnedToCore(worker, "wifi_ctl", WORKER_STACK, NULL,
                                   WORKER_PRIO, &s.worker, NET_CORE)
           != pdPASS) {
        s.worker = NULL;
        lock();
        s.w.off = WA_OFF_FAILED;
        unlock();
        return WA_OFF_FAILED;
    }
    lock();
    if (mode == WA_MODE_JOIN && !s.conf.have_net) {
        unlock();
        return WA_OFF_FAILED;
    }
    const wa_off_t r = wa_start(&s.w, mode, gate, ms(), rand32, NULL);
    if (r == WA_OFF_NONE) {
        ++s.gen;
        if (mode == WA_MODE_SCAN) {
            s.scanned = false;
            wa_scan_clear(&s.scan);
        }
    }
    unlock();
    poke();
    return r;
}

void wifi_access_close(wa_off_t why)
{
    lock();
    wa_close(&s.w, why);
    unlock();
    poke();
}

bool wifi_access_tick(wa_off_t gate, bool stop, bool may_write)
{
    lock();
    const bool closed_now = wa_tick(&s.w, ms(), gate, stop);
    const bool erase = may_write && s.conf_erase;
    const bool save = may_write && s.conf_dirty;
    const wa_conf_t c = s.conf;
    if (may_write) {
        s.conf_dirty = s.conf_erase = false;
    }
    unlock();
    if (closed_now) {
        poke();
    }
    /* An NVS commit closes the cache for its length: only on a frame that
     * may write flash, as the settings' save. */
    bool ok = true;
    if (erase) {
        wa_conf_t gone;
        ok = wa_conf_forget(&k_store, &gone);
    }
    if (save) {
        ok = wa_conf_save(&k_store, &c) && ok;
    }
    if (!ok) {
        ESP_LOGE(TAG, "the Wi-Fi settings were not written");
    }
    return closed_now;
}

void wifi_access_get(wifi_access_state_t *out)
{
    lock();
    out->w = s.w;
    out->conf = s.conf;
    memcpy(out->name, s.name, sizeof(out->name));
    out->channel = (s.w.state == WA_ST_AP) ? WIFI_ACCESS_AP_CHANNEL : 0u;
    out->scanned = s.scanned;
    out->scan = s.scan;
    out->up = s.up.state;
    out->up_err = s.up.err;
    out->received = s.up.done;
    out->total = s.up.bytes;
    out->info = s.up.info;
    out->offer = s.offer;
    out->idle_left_ms = wa_idle_left(&s.w, ms());
    unlock();
    out->down = atomic_load(&s.down);
}

bool wifi_access_save_net(const char *ssid, const char *pass)
{
    lock();
    const bool ok = wa_conf_set_net(&s.conf, ssid, strlen(ssid), pass,
                                    strlen(pass)) == WA_CONF_OK;
    if (ok) {
        s.conf_dirty = true;
    }
    unlock();
    return ok;
}

void wifi_access_forget(void)
{
    lock();
    wa_conf_defaults(&s.conf);
    s.conf_erase = true;
    s.conf_dirty = false;
    unlock();
}

void wifi_access_set_code(bool on)
{
    lock();
    s.conf.code_on = on;
    s.conf_dirty = true;
    unlock();
}

void wifi_access_discard(void)
{
    lock();
    if (!s.taken && s.up.state != FW_UP_RECEIVING) {
        fw_upload_discard(&s.up);
        heap_caps_free(s.up.buf);
        s.up.buf = NULL;
        s.up.cap = 0u;
    } else if (!s.taken) {
        /* Under way: refused now, and the sender's next piece finds it so. */
        fw_upload_discard(&s.up);
    }
    unlock();
}

bool wifi_access_take(const uint8_t **img, uint32_t *bytes,
                      fw_image_info_t *info)
{
    lock();
    const bool ok = (s.up.state == FW_UP_STAGED && s.up.buf != NULL);
    if (ok) {
        s.taken = true;
        *img = s.up.buf;
        *bytes = s.up.bytes;
        *info = s.up.info;
    }
    unlock();
    return ok;
}

void wifi_access_release(void)
{
    lock();
    s.taken = false;
    unlock();
    wifi_access_discard();
}
