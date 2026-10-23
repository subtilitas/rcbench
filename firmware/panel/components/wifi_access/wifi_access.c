/**
 * @file wifi_access.c
 * @brief The panel's Wi-Fi over esp_wifi, esp_http_server, esp_http_client
 *        and a UDP (user datagram protocol) socket for the mDNS name.
 *
 * The radio under esp_wifi is the board's ESP32-C6 on SDIO, brought up by
 * wifi_radio.h.
 *
 * Three tasks touch the session: the UI loop (start, close, tick), the event
 * loop (what the radio reports) and the HTTP server (requests).  Everything
 * they share is under one mutex, held for copies and counters only, never
 * across a socket or a card read.  A fourth task, the worker, owns bringing
 * the radio up and down: it compares what the session wants with what runs
 * and acts, so a stop that waits for a download to end never waits on the
 * UI loop.  A fifth, the fetch task, runs one job's requests to GitHub
 * (wa_fetch.h) and ends with it.
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
#include <strings.h>
#include <unistd.h>

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
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
#include "wifi_radio.h"
#include "wifi_sock.h"
#include "wa_text.h"
#include "wa_walk.h"
#include "wa_zip.h"

static const char *TAG = "wifi";

#define NVS_NS        "wifi"
#define WORKER_STACK  5120u
#define MDNS_STACK    4096u
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
/* The fetch task: TLS (transport layer security) needs the stack of the
 * page's server, which never runs beside it. */
#define FETCH_STACK   8192u
/* A read waits this long at a time, so a job ended from outside stops its
 * task within about this much once the data flows. */
#define FETCH_SLICE_MS 1000u
/* The radio goes off after this much, task ended or not: a connection or a
 * name lookup that waits on the network ends when the radio does. */
#define FETCH_GRACE_MS 1500u
/* And the task's end is waited for this long at most after it. */
#define FETCH_EXIT_MS  20000u
#define FETCH_AGENT    "rcbench-panel"

static struct {
    SemaphoreHandle_t lock;

    /* Under the lock. */
    wa_t            w;
    wa_conf_t       conf;
    wa_pending_t    pend;         /* the settings' writes owed to NVS   */
    wa_scan_t       scan;
    bool            scanned;
    fw_upload_t     up;
    bool            taken;
    fw_offer_t      offer;
    uint32_t        gen;          /* sessions started                   */
    wa_fetch_t      f;            /* the job asking GitHub              */
    struct {
        uint32_t     gen;         /* the job's, wa_fetch_t.gen          */
        wa_job_t     job;
        wa_channel_t channel;     /* CHECK's                            */
        fw_version_t want;
        uint32_t     start_ms;
    } job;                        /* what the fetch task runs           */

    /* Set once. */
    char            name[WA_NAME_MAX];
    uint32_t        slot;
    fw_image_info_t running;
    bool            running_ok;   /* running.version parsed            */
    TaskHandle_t    worker;

    /* The worker's. */
    bool            net_ready;
    bool            radio_up;
    uint32_t        up_gen;
    wa_mode_t       up_mode;
    esp_netif_t    *netif;
    httpd_handle_t  httpd;
    atomic_bool     mdns_alive;
    wa_mdns_tries_t mdns_tries;
    uint8_t         ip[4];

    atomic_bool     abort;        /* requests under way end now         */
    atomic_bool     mdns_run;
    atomic_bool     down;
    /* The session wants the radio: written under the lock, by a start and
     * by the worker as it reads the session, so wifi_access_down() never
     * sees a start that has not reached the radio as a radio off. */
    atomic_bool     asked;
    atomic_bool     de;

    /* The fetch task's.  fetch_alive from its creation to its last touch
     * of anything shared; fetch_stop asks it to end. */
    atomic_bool     fetch_alive;
    atomic_bool     fetch_stop;
    fw_upload_t     fup;          /* its image, until staged            */
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
    /* The fetch task's. */
    char            url[FW_REL_URL_MAX + 1u];
    char            loc[FW_REL_URL_MAX + 1u];
    uint8_t         fio[IO_CHUNK];
    char            sums[FW_REL_SUMS_MAX];
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

/*
 * Every folder and file open here holds the volume (storage_hold()) until
 * its close, so a card swapped while a download runs is let go after the
 * download's last close and never under a read.
 */
static void *card_opendir(void *ctx, const char *path)
{
    (void)ctx;
    if (!storage_hold()) {
        return NULL;
    }
    card_dir_t *d = heap_caps_malloc(sizeof(*d), MALLOC_CAP_SPIRAM);
    if (d == NULL) {
        storage_release();
        return NULL;
    }
    d->len = card_path(d->path, sizeof(d->path), path);
    d->d = (d->len > 0u) ? opendir(d->path) : NULL;
    if (d->d == NULL) {
        heap_caps_free(d);
        storage_release();
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
        /* storage_stat() rather than stat(): stat()'s 32-bit st_size reads
         * an exFAT file of 4 GiB and more as a small one. */
        storage_stat_t st;
        const esp_err_t r = storage_stat(d->path, &st);
        d->path[d->len] = '\0';
        if (r != ESP_OK) {
            return -1;
        }
        memset(out, 0, sizeof(*out));
        memcpy(out->name, e->d_name, nl);
        out->dir = st.is_dir;
        out->size = st.size;
        out->mtime = st.mtime;
        return 1;
    }
}

static void card_closedir(void *ctx, void *dir)
{
    (void)ctx;
    card_dir_t *d = dir;
    (void)closedir(d->d);
    heap_caps_free(d);
    storage_release();
}

static void *card_open(void *ctx, const char *path)
{
    (void)ctx;
    char full[sizeof(STORAGE_MOUNT_POINT) + WA_PATH_MAX + 2u];
    if (card_path(full, sizeof(full), path) == 0u || !storage_hold()) {
        return NULL;
    }
    const int fd = open(full, O_RDONLY);
    if (fd < 0) {
        storage_release();
        return NULL;
    }
    return (void *)(intptr_t)(fd + 1);
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
    storage_release();
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

/* Raw bytes, at most IO_CHUNK, of a response of known length (wifi_sock.h). */
static wa_send_res_t raw_send(httpd_req_t *r, const void *data, size_t n)
{
    wifi_sock_t c = { httpd_req_to_sockfd(r), &s.abort };
    const wa_sink_t k = wifi_sock_sink(&c);
    return wa_sink_all(&k, data, (uint32_t)n);
}

static wa_send_res_t raw_head(httpd_req_t *r, uint64_t length,
                              const char *name)
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
    return (n > 0 && (size_t)n < sizeof(b->head))
           ? raw_send(r, b->head, (size_t)n) : WA_SEND_GONE;
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
    storage_stat_t st;
    /* One volume from the size to the last byte: held across the stat and
     * the open, so a card swapped between them is not read under the old
     * card's size. */
    if (!storage_hold()) {
        req_end();
        return refused(r, "404 Not Found");
    }
    if (card_path(full, sizeof(full), path) == 0u
        || storage_stat(full, &st) != ESP_OK || st.is_dir) {
        storage_release();
        req_end();
        return refused(r, "404 Not Found");
    }
    wa_send_t *snd = &b->snd;
    if (!wa_send_begin(snd, &k_card, path, st.size)) {
        storage_release();
        req_end();
        return refused(r, "404 Not Found");
    }
    const uint32_t t0 = ms();
    wa_send_res_t res = raw_head(r, st.size, wa_basename(path));
    if (res == WA_SEND_DONE) {
        wifi_sock_t c = { httpd_req_to_sockfd(r), &s.abort };
        const wa_sink_t k = wifi_sock_sink(&c);
        res = wa_send_run(snd, b->io, sizeof(b->io), &k);
    }
    wa_send_end(snd);
    storage_release();
    req_end();
    /* Cut short, the browser has fewer bytes than announced: failed. */
    const esp_log_level_t lv = (res == WA_SEND_DONE) ? ESP_LOG_INFO
                                                     : ESP_LOG_WARN;
    ESP_LOG_LEVEL_LOCAL(lv, TAG, "file: %s, %llu of %llu B read in %lu ms",
                        wa_send_res_name(res), (unsigned long long)snd->sent,
                        (unsigned long long)st.size,
                        (unsigned long)(ms() - t0));
    return (res == WA_SEND_DONE) ? ESP_OK
                                 : wifi_sock_drop(httpd_req_to_sockfd(r));
}

/**
 * @brief GET /card.zip: the whole card as one ZIP archive, its length
 *        announced before the first byte.
 * @param r the request
 * @return ESP_OK when the archive went out whole.  A failure part way sends
 *         the bytes wa_zip_next() made up to it, then resets the connection
 *         (ESP_FAIL): the browser has fewer bytes than announced and reports
 *         the download failed.
 */
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
    wa_zip_slot_t *slot = NULL;
    esp_err_t res = ESP_FAIL;
    /* One volume from the plan's sizes to the last file: held across the
     * whole archive, as h_file() holds one file. */
    const bool held = storage_hold();
    if (z == NULL || !held || wa_zip_plan(&k_card, &plan) != WA_ZIP_OK) {
        res = refused(r, (z == NULL) ? "503 Service Unavailable"
                                     : "404 Not Found");
    } else {
        /* A slot an entry counted, and WA_ZIP_SPARE more for files added
         * before wa_zip_begin() walks the card again. */
        const uint64_t slots = plan.entries + WA_ZIP_SPARE;
        slot = (slots <= SIZE_MAX / sizeof(*slot))
               ? heap_caps_malloc((size_t)slots * sizeof(*slot),
                                  MALLOC_CAP_SPIRAM)
               : NULL;
        const wa_zip_err_t be = (slot == NULL)
                                ? WA_ZIP_MEMORY
                                : wa_zip_begin(z, &k_card, slot, slots);
        if (be != WA_ZIP_OK) {
            res = refused(r, (be == WA_ZIP_NO_CARD) ? "404 Not Found"
                                                    : "503 Service Unavailable");
        } else {
            char zname[WA_NAME_MAX + 8u];
            snprintf(zname, sizeof(zname), "%s.zip", s.name);
            bool ok = raw_head(r, z->plan.bytes, zname) == WA_SEND_DONE;
            while (ok) {
                const int32_t got = wa_zip_next(z, b->io, sizeof(b->io));
                if (got == 0) {
                    break;
                }
                ok = (got > 0)
                     && raw_send(r, b->io, (size_t)got) == WA_SEND_DONE;
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
            res = ok ? ESP_OK : wifi_sock_drop(httpd_req_to_sockfd(r));
        }
    }
    heap_caps_free(slot);
    heap_caps_free(z);
    if (held) {
        storage_release();
    }
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
    /* Asked to end, or failed to start: either way not running, so the
     * worker may start it again (wa_mdns_tries_due()). */
    atomic_store(&s.mdns_run, false);
    atomic_store(&s.mdns_alive, false);
    xTaskNotifyGive(s.worker);
    vTaskDelete(NULL);
}

/* ----------------------------------------------------------- GitHub */

/*
 * The HTTP client wa_fetch.c runs against: esp_http_client over TLS, the
 * server's chain checked against the certificate bundle, one request per
 * connection, no redirect followed by the client.  esp_http_client keeps
 * every header value in memory of its own, without a bound of its own;
 * only a server whose chain checks reaches it.
 */
typedef struct {
    esp_http_client_handle_t c;
    char  *loc;
    size_t loc_cap;
    size_t loc_n;
    bool   loc_cut;
    bool   loc_seen;
    bool   chunked;     /* Transfer-Encoding: chunked                    */
    /* The last answer's head and body bytes read, for the console at the
     * job's end: kept past h_close(). */
    int      last_status;
    int64_t  last_length;
    bool     last_chunked;
    uint32_t last_read;
} http_ctx_t;

static http_ctx_t s_http;

static esp_err_t on_http(esp_http_client_event_t *e)
{
    http_ctx_t *h = e->user_data;
    if (e->event_id != HTTP_EVENT_ON_HEADER || h == NULL
        || e->header_key == NULL || e->header_value == NULL) {
        return ESP_OK;
    }
    /* The client's own test, exactly "chunked": anything else is read as
     * an answer without a length. */
    if (strcasecmp(e->header_key, "Transfer-Encoding") == 0) {
        h->chunked = strcasecmp(e->header_value, "chunked") == 0;
        return ESP_OK;
    }
    if (h->loc == NULL || strcasecmp(e->header_key, "Location") != 0) {
        return ESP_OK;
    }
    if (h->loc_seen) {
        /* Two of them: neither is taken, and the answer is refused as one
         * whose Location does not fit. */
        h->loc_n = 0u;
        h->loc_cut = true;
        return ESP_OK;
    }
    h->loc_seen = true;
    size_t n = strlen(e->header_value);
    if (n > h->loc_cap) {
        n = h->loc_cap;
        h->loc_cut = true;
    }
    memcpy(h->loc, e->header_value, n);
    h->loc_n = n;
    return ESP_OK;
}

static void h_close(void *ctx)
{
    http_ctx_t *h = ctx;
    if (h->c != NULL) {
        (void)esp_http_client_close(h->c);
        (void)esp_http_client_cleanup(h->c);
        h->c = NULL;
    }
}

static bool h_open(void *ctx, const char *url, uint32_t timeout_ms,
                   bool api, wa_http_head_t *head, char *loc, size_t loc_cap)
{
    http_ctx_t *h = ctx;
    h_close(h);
    h->loc = loc;
    h->loc_cap = loc_cap;
    h->loc_n = 0u;
    h->loc_cut = false;
    h->loc_seen = false;
    h->chunked = false;
    h->last_status = 0;
    h->last_length = -1;
    h->last_chunked = false;
    h->last_read = 0u;
    const esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = (int)timeout_ms,
        .disable_auto_redirect = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = on_http,
        .user_data = h,
        .user_agent = FETCH_AGENT,
        .keep_alive_enable = false,
        .buffer_size = 2048,
        /* The request line carries the whole address: FW_REL_URL_MAX. */
        .buffer_size_tx = (int)(FW_REL_URL_MAX + 512u),
    };
    h->c = esp_http_client_init(&cfg);
    if (h->c == NULL) {
        return false;
    }
    /* GitHub's REST API asks for both: the media type of its answer, and
     * the version of the API it is written in, so its form stays put. */
    if (api
        && (esp_http_client_set_header(h->c, "Accept", FW_REL_API_ACCEPT)
                != ESP_OK
            || esp_http_client_set_header(h->c, "X-GitHub-Api-Version",
                                          FW_REL_API_VERSION)
                   != ESP_OK)) {
        return false;
    }
    if (esp_http_client_open(h->c, 0) != ESP_OK) {
        return false;
    }
    if (esp_http_client_fetch_headers(h->c) < 0) {
        return false;
    }
    head->status = esp_http_client_get_status_code(h->c);
    head->length = esp_http_client_get_content_length(h->c);
    /* The header itself: esp_http_client_is_chunked_response() calls every
     * answer without a length chunked (esp_http_client_fetch_headers()). */
    head->chunked = h->chunked;
    h->last_status = head->status;
    h->last_length = head->length;
    h->last_chunked = head->chunked;
    head->loc_n = h->loc_n;
    head->loc_cut = h->loc_cut;
    return true;
}

static int32_t h_read(void *ctx, uint8_t *dst, uint32_t n,
                      uint32_t timeout_ms)
{
    http_ctx_t *h = ctx;
    const uint32_t t0 = ms();
    for (;;) {
        if (atomic_load(&s.fetch_stop) || h->c == NULL) {
            return -1;
        }
        const uint32_t spent = (uint32_t)(ms() - t0);
        if (spent >= timeout_ms) {
            return -1;
        }
        const uint32_t left = timeout_ms - spent;
        (void)esp_http_client_set_timeout_ms(
            h->c, (int)((left < FETCH_SLICE_MS) ? left : FETCH_SLICE_MS));
        const int got = esp_http_client_read(h->c, (char *)dst, (int)n);
        if (got == 0 && h->chunked
            && !esp_http_client_is_complete_data_received(h->c)) {
            return -1;           /* closed before the last chunk */
        }
        if (got >= 0) {
            /* 0: the end; with a Content-Length the server closed: short */
            h->last_read += (uint32_t)got;
            return got;
        }
        if (got != -ESP_ERR_HTTP_EAGAIN) {
            return -1;
        }
    }
}

static const wa_http_t k_http = { h_open, h_read, h_close, &s_http };

static uint32_t f_now(void *ctx)
{
    (void)ctx;
    return ms();
}

/* Asked to end, or the job ended under the task. */
static bool f_stopped(void *ctx)
{
    (void)ctx;
    if (atomic_load(&s.fetch_stop)) {
        return true;
    }
    lock();
    const bool over = (s.f.gen != s.job.gen) || !wa_fetch_busy(&s.f);
    unlock();
    return over;
}

static void f_report(void *ctx, wa_phase_t ph, uint32_t done, uint32_t total)
{
    (void)ctx;
    lock();
    wa_fetch_on_phase(&s.f, s.job.gen, ph);
    if (ph == WA_PH_IMAGE) {
        wa_fetch_on_bytes(&s.f, s.job.gen, done, total);
    }
    unlock();
}

static uint8_t *f_alloc(void *ctx, uint32_t bytes)
{
    (void)ctx;
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
}

/*
 * One job's requests, from the join on.  A staged image moves to where an
 * upload leaves one, for wifi_access_take(), if the job is still the one
 * running; anything else is freed here.
 */
static void fetch_task(void *arg)
{
    (void)arg;
    lock();
    const uint32_t     gen = s.job.gen;
    const wa_job_t     job = s.job.job;
    const wa_channel_t channel = s.job.channel;
    const fw_version_t want = s.job.want;
    const uint32_t     start_ms = s.job.start_ms;
    unlock();
    /* No answer yet in this job: none of an earlier one is logged. */
    s_http.last_status = 0;
    s_http.last_length = -1;
    s_http.last_chunked = false;
    s_http.last_read = 0u;
    fw_upload_init(&s.fup, NULL, 0u, s.slot);
    const wa_run_t run = {
        .http = &k_http,
        .now = f_now,
        .stopped = f_stopped,
        .report = f_report,
        .alloc = f_alloc,
        .ctx = NULL,
        .start_ms = start_ms,
        .budget_ms = (job == WA_JOB_CHECK) ? WA_FETCH_CHECK_MS
                                           : WA_FETCH_DOWNLOAD_MS,
        .slot = s.slot,
        .running = s.running.version,
        .running_ok = s.running_ok,
        .channel = channel,
        .url = b->url,
        .loc = b->loc,
        .io = b->fio,
        .io_cap = sizeof(b->fio),
        .sums = b->sums,
        .up = &s.fup,
    };
    wa_fetch_res_t res;
    if (job == WA_JOB_CHECK) {
        wa_run_check(&run, &res);
    } else {
        wa_run_download(&run, want, &res);
    }
    h_close(&s_http);

    bool moved = false;
    lock();
    if (res.got == WA_GOT_STAGED && s.f.gen == gen && wa_fetch_busy(&s.f)
        && !atomic_load(&s.fetch_stop)) {
        if (s.taken) {
            res.got = WA_GOT_ABORTED;
            res.why = WA_WHY_INSTALL;
        } else {
            heap_caps_free(s.up.buf);
            s.up = s.fup;
            s.offer = fw_offer(&s.running, &s.up.info);
            moved = true;
        }
    }
    wa_fetch_on_end(&s.f, gen, &res);
    unlock();
    if (!moved) {
        heap_caps_free(s.fup.buf);
    }
    memset(&s.fup, 0, sizeof(s.fup));
    /* The last answer's head, for a result the screen names only by its
     * kind: no address, no header value but these. */
    ESP_LOGI(TAG, "GitHub: job %lu ended: %d", (unsigned long)gen,
             (int)res.got);
    ESP_LOGI(TAG, "GitHub: job %lu last answer: HTTP %d, Content-Length "
             "%lld, chunked %d, body %lu B read, rel %d (%s)",
             (unsigned long)gen, s_http.last_status,
             (long long)s_http.last_length, (int)s_http.last_chunked,
             (unsigned long)s_http.last_read, (int)res.rel,
             fw_rel_err_str(res.rel));
    atomic_store(&s.fetch_alive, false);
    poke();
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------ the radio */

/*
 * The reasons a refused key gives, auth-class to wa_on_drop().  ESP-IDF
 * reports 15 (4WAY_HANDSHAKE_TIMEOUT) and 204 (HANDSHAKE_TIMEOUT) for a
 * wrong key and for a key handshake that ran out of time, and its
 * supplicant sends 15 itself after a 4th message 1 of the handshake: one of
 * them alone does not tell the two apart (WA_AUTH_TRIES).
 */
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
            const bool auth = auth_refused(d->reason);
            lock();
            const wa_state_t was = s.w.state;
            retry = wa_on_drop(&s.w, auth);
            const unsigned drops = s.w.auth_drops;
            unlock();
            ESP_LOGW(TAG, "station dropped: reason %u%s, state %d, "
                     "auth-class %u of %u, %s",
                     (unsigned)d->reason, auth ? " (auth-class)" : "",
                     (int)was, drops, WA_AUTH_TRIES,
                     retry ? "connecting again" : "no retry");
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

/*
 * A step of a start that failed: its name, the error, and what internal
 * RAM held at that moment.  A start that finds too little internal RAM
 * fails at the allocation that runs short; how much the radio's driver
 * through the ESP32-C6 takes is not measured.
 */
static void start_failed(const char *step, esp_err_t e)
{
    ESP_LOGE(TAG, "%s: %s; internal RAM %u B free, largest block %u B",
             step, esp_err_to_name(e),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

/* The network interface layer, the default event loop and the handlers,
 * once.  A step that fails is tried again at the next start. */
static bool net_once(void)
{
    if (s.net_ready) {
        return true;
    }
    esp_err_t e = esp_netif_init();
    if (e != ESP_OK) {
        start_failed("esp_netif_init", e);
        return false;
    }
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        start_failed("esp_event_loop_create_default", e);
        return false;
    }
    e = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event,
                                   NULL);
    if (e != ESP_OK) {
        start_failed("esp_event_handler_register WIFI_EVENT", e);
        return false;
    }
    e = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event,
                                   NULL);
    if (e != ESP_OK) {
        (void)esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                           on_event);
        start_failed("esp_event_handler_register IP_EVENT", e);
        return false;
    }
    s.net_ready = true;
    return true;
}

/* Up to @p wait_ms for the fetch task to end; whether it has. */
static bool fetch_wait(uint32_t wait_ms)
{
    const uint32_t t0 = ms();
    while (atomic_load(&s.fetch_alive)
           && (uint32_t)(ms() - t0) < wait_ms) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
    }
    return !atomic_load(&s.fetch_alive);
}

/**
 * @brief Take the radio down: mDNS, the fetch, the page's server, the
 *        driver and the interface.
 *
 * After a failed C6 link, esp_wifi_stop() and esp_wifi_deinit() are not
 * called: each is a request to the C6, which is held in reset then, and
 * would end only at esp_hosted's response time-out, 5 s each.
 */
static void radio_down(void)
{
    atomic_store(&s.abort, true);
    atomic_store(&s.fetch_stop, true);
    atomic_store(&s.mdns_run, false);
    while (atomic_load(&s.mdns_alive)) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
    }
    /* A read ends within FETCH_SLICE_MS; a connection or a name lookup
     * under way ends when the radio goes, after the grace. */
    (void)fetch_wait(FETCH_GRACE_MS);
    if (s.httpd != NULL) {
        /* Waits for a request under way, which ends within a slice. */
        (void)httpd_stop(s.httpd);
        s.httpd = NULL;
    }
    if (s.radio_up) {
        if (!wifi_radio_failed()) {
            (void)esp_wifi_stop();
            (void)esp_wifi_deinit();
        }
        if (s.netif != NULL) {
            esp_netif_destroy_default_wifi(s.netif);
            s.netif = NULL;
        }
    }
    s.radio_up = false;
    memset(s.ip, 0, sizeof(s.ip));
    if (!fetch_wait(FETCH_EXIT_MS)) {
        /* No new job starts while it lives (wifi_access_fetch_start()). */
        ESP_LOGW(TAG, "GitHub: the fetch task has not ended");
    }
    atomic_store(&s.abort, false);
    atomic_store(&s.down, true);
}

/**
 * @brief Bring the radio up in @p mode: the C6's link, the interface, the
 *        driver, the configuration and, in AP and JOIN, the page's server.
 *
 * @param mode The session's mode.
 * @param w    The session, for the access point's password.
 * @param c    The kept home network, for JOIN and FETCH.
 * @return true when every step worked; false after the first that failed,
 *         logged by start_failed().
 */
static bool radio_bring_up(wa_mode_t mode, const wa_t *w, const wa_conf_t *c)
{
    atomic_store(&s.down, false);
    if (!net_once()) {
        return false;
    }
    esp_err_t e = wifi_radio_link();
    if (e != ESP_OK) {
        start_failed("wifi_radio_link", e);
        return false;
    }
    s.up_mode = mode;
    s.netif = (mode == WA_MODE_AP) ? esp_netif_create_default_wifi_ap()
                                   : esp_netif_create_default_wifi_sta();
    if (s.netif == NULL) {
        start_failed((mode == WA_MODE_AP)
                         ? "esp_netif_create_default_wifi_ap"
                         : "esp_netif_create_default_wifi_sta", ESP_FAIL);
        return false;
    }
    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    ic.nvs_enable = 0;   /* nothing of the radio's is kept */
    e = esp_wifi_init(&ic);
    if (e != ESP_OK) {
        start_failed("esp_wifi_init", e);
        esp_netif_destroy_default_wifi(s.netif);
        s.netif = NULL;
        return false;
    }
    s.radio_up = true;
    (void)esp_wifi_set_storage(WIFI_STORAGE_RAM);
    wifi_config_t cfg = { 0 };
    const char *step = "esp_wifi_set_mode";
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
            step = "esp_wifi_set_config";
            e = esp_wifi_set_config(WIFI_IF_AP, &cfg);
        }
    } else {
        e = esp_wifi_set_mode(WIFI_MODE_STA);
        /* No power save: with the default, the access point holds TCP's
         * acks to each DTIM beacon (docs/Performance.md, Wi-Fi downloads). */
        if (e == ESP_OK && esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK) {
            ESP_LOGW(TAG, "esp_wifi_set_ps failed");
        }
        if (e == ESP_OK
            && (mode == WA_MODE_JOIN || mode == WA_MODE_FETCH)) {
            memcpy(cfg.sta.ssid, c->ssid, strnlen(c->ssid, sizeof(cfg.sta.ssid)));
            memcpy(cfg.sta.password, c->pass,
                   strnlen(c->pass, sizeof(cfg.sta.password) - 1u));
            /* WPA2 or WPA3 personal; nothing weaker, no enterprise. */
            cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            cfg.sta.pmf_cfg.capable = true;
            cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
            cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
            cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
            step = "esp_wifi_set_config";
            e = esp_wifi_set_config(WIFI_IF_STA, &cfg);
            if (e == ESP_OK) {
                (void)esp_netif_set_hostname(s.netif, s.name);
            }
        }
    }
    if (e == ESP_OK) {
        step = "esp_wifi_start";
        e = esp_wifi_start();
    }
    /* The page in AP and JOIN only: FETCH serves nothing. */
    if (e == ESP_OK && (mode == WA_MODE_AP || mode == WA_MODE_JOIN)) {
        step = "httpd_start";
        e = httpd_up();
    }
    if (e != ESP_OK) {
        start_failed(step, e);
        return false;
    }
    return true;
}

/**
 * @brief The task that owns the radio: compares what the session wants
 *        with what runs, and brings the radio up or down; a failed C6 link
 *        takes it down.
 *
 * @param arg Unused.
 */
static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        lock();
        const bool want = wa_radio(&s.w);
        atomic_store(&s.asked, want);
        const uint32_t gen = s.gen;
        const wa_t w = s.w;
        const wa_conf_t c = s.conf;
        unlock();
        if (s.radio_up || !atomic_load(&s.down)) {
            if (!want || gen != s.up_gen || wifi_radio_failed()) {
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
                wa_mdns_tries_reset(&s.mdns_tries);
                ESP_LOGI(TAG, "radio up, mode %d; internal RAM %u B free",
                         (int)w.mode, (unsigned)heap_caps_get_free_size(
                                          MALLOC_CAP_INTERNAL));
            }
        }
        if (s.radio_up && w.state == WA_ST_JOINED
            && !atomic_load(&s.mdns_alive) && !atomic_load(&s.mdns_run)
            && s.up_mode == WA_MODE_JOIN
            && wa_mdns_tries_due(&s.mdns_tries, ms())) {
            wa_mdns_tries_started(&s.mdns_tries, ms());
            atomic_store(&s.mdns_run, true);
            atomic_store(&s.mdns_alive, true);
            if (xTaskCreatePinnedToCore(mdns_task, "mdns", MDNS_STACK, NULL,
                                        WORKER_PRIO, NULL, NET_CORE)
                != pdPASS) {
                start_failed("mdns task", ESP_ERR_NO_MEM);
                atomic_store(&s.mdns_run, false);
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
        /* Neither half is kept: the caller does not call in again. */
        if (s.lock != NULL) {
            vSemaphoreDelete(s.lock);
            s.lock = NULL;
        }
        heap_caps_free(b);
        b = NULL;
        return ESP_ERR_NO_MEM;
    }
    s.slot = slot;
    s.running = *running;
    s.running_ok = fw_version_parse(running->version_text,
                                    strnlen(running->version_text,
                                            sizeof(running->version_text)),
                                    NULL);
    wa_init(&s.w);
    wa_conf_load(&k_store, &s.conf);
    wa_pending_init(&s.pend);
    wa_scan_clear(&s.scan);
    fw_upload_init(&s.up, NULL, 0u, slot);
    wa_fetch_init(&s.f, running->version);
    uint8_t mac[6] = { 0 };
    wifi_radio_mac(mac);
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
        start_failed("wifi_ctl task", ESP_ERR_NO_MEM);
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
        atomic_store(&s.asked, true);
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
    const uint32_t now = ms();
    lock();
    const bool closed_now = wa_tick(&s.w, now, gate, stop);
    /* An NVS commit closes the cache for its length: only on a frame that
     * may write flash, as the settings' save. */
    const bool write = wa_pending_due(&s.pend, may_write, now);
    const wa_pending_t snap = s.pend;
    const wa_conf_t c = s.conf;
    unlock();
    if (closed_now) {
        poke();
    }
    if (write) {
        bool erase_ok, save_ok;
        wa_pending_write(&k_store, &snap, &c, &erase_ok, &save_ok);
        lock();
        wa_pending_done(&s.pend, &snap, erase_ok, save_ok, ms());
        unlock();
        if (!erase_ok || !save_ok) {
            ESP_LOGE(TAG, "the Wi-Fi settings were not written (%s)",
                     erase_ok ? "save" : "erase");
        }
    }
    return closed_now;
}

void wifi_access_get(wifi_access_state_t *out)
{
    lock();
    out->w = s.w;
    out->conf = s.conf;
    out->save_failed = s.pend.failed;
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

bool wifi_access_down(void)
{
    return atomic_load(&s.down) && !atomic_load(&s.asked);
}

bool wifi_access_save_net(const char *ssid, const char *pass)
{
    lock();
    const bool ok = wa_conf_set_net(&s.conf, ssid, strlen(ssid), pass,
                                    strlen(pass)) == WA_CONF_OK;
    if (ok) {
        wa_pending_change(&s.pend);
    }
    unlock();
    return ok;
}

void wifi_access_forget(void)
{
    lock();
    wa_conf_defaults(&s.conf);
    wa_pending_forget(&s.pend);
    unlock();
}

void wifi_access_set_code(bool on)
{
    lock();
    s.conf.code_on = on;
    wa_pending_change(&s.pend);
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

wa_got_t wifi_access_fetch_start(wa_job_t job, fw_version_t want,
                                 wa_channel_t ch, wa_why_t gate)
{
    bool no_worker = false;
    if (s.worker == NULL && gate == WA_WHY_NONE
        && xTaskCreatePinnedToCore(worker, "wifi_ctl", WORKER_STACK, NULL,
                                   WORKER_PRIO, &s.worker, NET_CORE)
           != pdPASS) {
        s.worker = NULL;
        start_failed("wifi_ctl task", ESP_ERR_NO_MEM);
        no_worker = true;
    }
    lock();
    wa_why_t g = gate;
    if (g == WA_WHY_NONE
        && (s.taken || atomic_load(&s.fetch_alive)
            || (wa_radio(&s.w) && s.w.mode != WA_MODE_FETCH))) {
        g = WA_WHY_BUSY;
    }
    const wa_got_t r = wa_fetch_start(&s.f, job, want, ch, s.conf.have_net,
                                      g, ms());
    if (r == WA_GOT_NONE) {
        if (job == WA_JOB_DOWNLOAD && s.up.state != FW_UP_RECEIVING) {
            /* What the page left: the download takes its place. */
            fw_upload_discard(&s.up);
            heap_caps_free(s.up.buf);
            s.up.buf = NULL;
            s.up.cap = 0u;
        }
        (void)wa_start(&s.w, WA_MODE_FETCH, WA_OFF_NONE, ms(), rand32, NULL);
        atomic_store(&s.asked, true);
        ++s.gen;
        if (no_worker) {
            /* Ends the job as the radio's failure on the next tick. */
            wa_close(&s.w, WA_OFF_FAILED);
        }
    }
    unlock();
    poke();
    return r;
}

void wifi_access_fetch_tick(wa_why_t abort)
{
    lock();
    wa_fetch_tick(&s.f, ms(), abort, &s.w);
    const bool off = wa_fetch_take_off(&s.f);
    const bool run = wa_fetch_take_run(&s.f);
    if (off) {
        atomic_store(&s.fetch_stop, true);
        if (s.w.mode == WA_MODE_FETCH) {
            wa_close(&s.w, WA_OFF_ASKED);   /* not WI-FI's session */
        }
    }
    if (run) {
        s.job.gen = s.f.gen;
        s.job.job = s.f.job;
        s.job.channel = s.f.channel;
        s.job.want = s.f.found;
        s.job.start_ms = s.f.start_ms;
        atomic_store(&s.fetch_stop, false);
        atomic_store(&s.fetch_alive, true);
    }
    unlock();
    if (run
        && xTaskCreatePinnedToCore(fetch_task, "wifi_get", FETCH_STACK, NULL,
                                   NET_PRIORITY, NULL, NET_CORE)
           != pdPASS) {
        start_failed("wifi_get task", ESP_ERR_NO_MEM);
        atomic_store(&s.fetch_alive, false);
        const wa_fetch_res_t r = { .got = WA_GOT_MEMORY };
        lock();
        wa_fetch_on_end(&s.f, s.job.gen, &r);
        unlock();
    }
    if (off) {
        poke();
    }
}

void wifi_access_fetch_get(wa_fetch_t *out)
{
    lock();
    *out = s.f;
    unlock();
}

bool wifi_access_home(char *ssid, size_t n)
{
    lock();
    const bool have = s.conf.have_net;
    snprintf(ssid, n, "%s", have ? s.conf.ssid : "");
    unlock();
    return have;
}
