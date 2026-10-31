/**
 * @file
 * @brief The writes of the page's file API on the panel's server: PUT of a
 *        file into the 1MiB text room, then MKDIR, RENAME and DELETE from a
 *        form, each one job of FILES' core (wa_api.h) under the card's job
 *        lock.
 *
 * A write is answered in this order, each refusal before the next step:
 *
 *   1. the guard (wa_guard.h): Host, the body's size (1MiB for PUT, 1kB
 *      for a form), the page's Origin and the script's header;
 *   2. the access code while its switch is on, and SETUP's Browser writes
 *      (wa_api_may_write()): the code with ACCESS CODE, the window the
 *      hold on WI-FI opened with HOLD, none with OFF;
 *   3. the room, then the body, received whole into PSRAM (pseudo-static
 *      random-access memory) in 4,096B pieces; the radio going off, a
 *      closed connection or 30s without a byte end the request with
 *      nothing written;
 *   4. the card's job lock, 2000ms at most behind a FILES job (503
 *      PANEL_BUSY, Retry-After: 2);
 *   5. the core's rules and the card (wa_api_put() and the others), the
 *      gate read before each 16KiB chunk.
 *
 * A refusal before the body is read closes the connection with the answer,
 * as the guard's do: the body is never read.  The radio going off closes
 * the window and starts the count again (wifi_write_reset()).
 *
 * Each write logs its answer, its bytes, its time, the stack the server's
 * task left unused and the internal RAM free: the depth of a card write on
 * that task is not measured on the panel.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi_write.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_access.h"
#include "wifi_access_int.h"
#include "wifi_files.h"
#include "wifi_req.h"

/** @brief The log tag, wifi_access.c's. */
static const char *TAG = "wifi";

/** @brief How long a write waits for a FILES job to end, ms. */
#define LOCK_WAIT_MS 2000u
/** @brief Bytes of one receive of a PUT's body. */
#define RECV_PIECE   4096u
/** @brief Receive time-outs of the server's 5s a body rides out: 30s. */
#define RECV_WAITS   6u

/** @brief What a handler writes. */
typedef enum {
    WR_PUT = 0,
    WR_MKDIR,
    WR_RENAME,
    WR_DELETE
} wr_t;

/** @brief Each write's name in the log. */
static const char *const k_name[] = { "put", "mkdir", "rename", "delete" };

/** @brief SETUP's Browser writes; OFF until the panel first sets it. */
static atomic_int s_mode = WA_WRITES_OFF;
/** @brief The hold on WI-FI opened writes this radio session. */
static atomic_bool s_window;
/** @brief Writes the page made this radio session. */
static atomic_uint s_written;
/** @brief The card's job lock, FILES' (wifi_access_set_card_lock()). */
static bool (*s_take)(uint32_t wait_ms);
/** @brief Its give. */
static void (*s_give)(bool wrote);

void wifi_access_set_writes(wa_writes_t mode)
{
    atomic_store(&s_mode, (int)mode);
}

void wifi_access_writes_allow(bool open)
{
    atomic_store(&s_window, open);
}

bool wifi_access_writes(uint32_t *written)
{
    if (written != NULL) {
        *written = atomic_load(&s_written);
    }
    return atomic_load(&s_window);
}

void wifi_access_set_card_lock(bool (*take)(uint32_t wait_ms),
                               void (*give)(bool wrote))
{
    s_take = take;
    s_give = give;
}

void wifi_write_reset(void)
{
    atomic_store(&s_window, false);
    atomic_store(&s_written, 0u);
}

wa_writes_t wifi_write_mode(bool *window)
{
    *window = atomic_load(&s_window);
    return (wa_writes_t)atomic_load(&s_mode);
}

/**
 * @brief wa_out_t's sink: one chunk of the answer, unless the radio goes
 *        off.
 * @param ctx   the request
 * @param data  the bytes
 * @param n     how many
 * @return false when not sent
 */
static bool sink_chunk(void *ctx, const void *data, size_t n)
{
    if (atomic_load(wifi_page_abort())) {
        return false;
    }
    return httpd_resp_send_chunk(ctx, data, (ssize_t)n) == ESP_OK;
}

/**
 * @brief The answer: the write's JSON (JavaScript Object Notation) with
 *        201 or 200, or the refusal's.
 * @param r      the request
 * @param why    WA_API_OK, or why it is refused
 * @param api    the API, for a write done; may be NULL otherwise
 * @param de     German texts
 * @param close  the body was not read: the connection closes with it
 * @return ESP_OK when the answer went out and the connection stays
 */
static esp_err_t answer(httpd_req_t *r, wa_api_err_t why, const wa_api_t *api,
                        bool de, bool close)
{
    httpd_resp_set_status(r, (why == WA_API_OK) ? wa_api_write_status(api)
                                                : wa_api_status_line(why));
    httpd_resp_set_type(r, "application/json; charset=utf-8");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    if (why == WA_API_PANEL_BUSY) {
        httpd_resp_set_hdr(r, "Retry-After", "2");
    }
    char etag[WA_API_ETAG_SIZE];     /* lives until the answer is sent */
    if (why == WA_API_OK && wa_api_write_etag(api, etag)) {
        httpd_resp_set_hdr(r, "ETag", etag);
    }
    if (close) {
        httpd_resp_set_hdr(r, "Connection", "close");
    }
    wa_out_t *o = wifi_page_out();
    wa_out_init(o, sink_chunk, r);
    if (why == WA_API_OK) {
        wa_api_write_json(api, o);
    } else {
        wa_api_refused_json(api, o, why, de);
    }
    if (!wa_out_flush(o) || httpd_resp_send_chunk(r, NULL, 0) != ESP_OK) {
        return ESP_FAIL;
    }
    return close ? ESP_FAIL : ESP_OK;
}

/**
 * @brief Receive the request's body whole into @p dst.
 * @param r    the request
 * @param dst  room for @p n bytes
 * @param n    the body's bytes, Content-Length
 * @return false when the radio went off, the connection closed or the
 *         client stayed quiet RECV_WAITS receive time-outs in a row
 */
static bool recv_body(httpd_req_t *r, uint8_t *dst, size_t n)
{
    size_t got = 0u;
    unsigned waits = 0u;
    while (got < n) {
        if (atomic_load(wifi_page_abort())) {
            return false;
        }
        const size_t want = (n - got < RECV_PIECE) ? n - got : RECV_PIECE;
        const int k = httpd_req_recv(r, (char *)dst + got, want);
        if (k == HTTPD_SOCK_ERR_TIMEOUT && ++waits < RECV_WAITS) {
            continue;
        }
        if (k <= 0) {
            return false;
        }
        waits = 0u;
        got += (size_t)k;
        wifi_page_traffic();       /* the idle clock waits */
    }
    return true;
}

/**
 * @brief Whether this request may write, by SETUP's Browser writes.
 * @param r  the request, its access code checked while the switch is on
 * @return WA_API_OK, or the setting's refusal
 */
static wa_api_err_t may_write(httpd_req_t *r)
{
    bool window = false;
    const wa_writes_t mode = wifi_write_mode(&window);
    /* The code is asked only when it decides: a wrong one counts. */
    const bool code = (mode == WA_WRITES_CODE)
                      && wifi_page_code(r) == WA_CODE_OK;
    return wa_api_may_write(mode, code, window);
}

/**
 * @brief The body into the API's room: a PUT's into the text room, a
 *        form's into the server's chunk buffer, ended by a NUL.
 * @param r     the request
 * @param kind  the write
 * @param api   the API
 * @param form  takes the form; NULL for a PUT
 * @return WA_API_OK; WA_API_MEMORY with no room; WA_API_CANCELLED for a
 *         body that did not arrive whole
 */
static wa_api_err_t take_body(httpd_req_t *r, wr_t kind, wa_api_t *api,
                              const char **form)
{
    const size_t n = r->content_len;
    size_t cap = 0u;
    uint8_t *dst;
    if (kind == WR_PUT) {
        *form = NULL;
        dst = wa_api_body(api, &cap);
    } else {
        dst = wifi_page_io(&cap);
        cap = (cap > 0u) ? cap - 1u : 0u;      /* the NUL */
        *form = (const char *)dst;
    }
    if (dst == NULL || n > cap) {
        return WA_API_MEMORY;
    }
    if (!recv_body(r, dst, n)) {
        return WA_API_CANCELLED;
    }
    if (kind != WR_PUT) {
        dst[n] = '\0';
    }
    return WA_API_OK;
}

/**
 * @brief The card job of @p kind, under the card's job lock.
 * @param r     the request, for a PUT's headers
 * @param kind  the write
 * @param api   the API, the body taken
 * @param form  a form's body
 * @return WA_API_OK, or why not
 */
static wa_api_err_t run_write(httpd_req_t *r, wr_t kind, wa_api_t *api,
                              const char *form)
{
    if (s_take == NULL || s_give == NULL || !s_take(LOCK_WAIT_MS)) {
        return WA_API_PANEL_BUSY;
    }
    wa_api_err_t why;
    switch (kind) {
    case WR_PUT: {
        /* The headers into the server's chunk buffer, behind nothing: the
         * PUT's body is in the text room. */
        size_t cap = 0u;
        char *h = (char *)wifi_page_io(&cap);
        const char *q = strchr(r->uri, '?');
        const char *im = wifi_req_hdr(r, "If-Match", h, cap / 2u);
        const char *inm = wifi_req_hdr(r, "If-None-Match", h + cap / 2u,
                                       cap / 2u);
        why = wa_api_put(api, (q != NULL) ? q + 1 : "", im, inm,
                         r->content_len);
        break;
    }
    case WR_MKDIR:
        why = wa_api_mkdir(api, form);
        break;
    case WR_RENAME:
        why = wa_api_rename(api, form);
        break;
    case WR_DELETE:
    default:
        why = wa_api_delete(api, form);
        break;
    }
    s_give(why == WA_API_OK);
    if (why == WA_API_OK) {
        atomic_fetch_add(&s_written, 1u);
    }
    return why;
}

/**
 * @brief Answer one write of the API.
 * @param r     the request
 * @param kind  the write
 * @return the handler's return
 */
static esp_err_t serve(httpd_req_t *r, wr_t kind)
{
    esp_err_t e = ESP_OK;
    wa_guard_t guard = WA_GUARD_OK;
    const wifi_page_go_t go = wifi_page_begin(
        r, (kind == WR_PUT) ? WA_REQ_PUT : WA_REQ_FORM, &e, &guard);
    if (go == WIFI_PAGE_DONE) {
        return e;
    }
    wa_page_info_t pi;
    wifi_page_info(&pi);
    if (go == WIFI_PAGE_REFUSED) {
        (void)answer(r, wa_api_of_guard(guard), NULL, pi.de, true);
        return ESP_FAIL;
    }
    const int64_t t0 = esp_timer_get_time();
    wa_api_err_t why = (go == WIFI_PAGE_NO_CODE) ? WA_API_CODE
                                                 : may_write(r);
    wa_api_t *api = NULL;
    if (why == WA_API_OK) {
        api = wifi_files_api(kind == WR_PUT, &why);
    }
    const char *form = NULL;
    if (why == WA_API_OK) {
        why = take_body(r, kind, api, &form);
        if (why == WA_API_CANCELLED) {
            /* No body, no answer anyone waits for: the connection goes. */
            wifi_page_end();
            return ESP_FAIL;
        }
    }
    if (why != WA_API_OK) {
        e = answer(r, why, api, pi.de, true);
    } else {
        why = run_write(r, kind, api, form);
        e = answer(r, why, api, pi.de, false);
    }
    ESP_LOGI(TAG, "api: %s %s, %lluB in %llums; httpd stack %uB unused, "
             "internal RAM %uB free", k_name[kind],
             (why == WA_API_OK) ? "OK" : wa_api_word(why),
             (unsigned long long)r->content_len,
             (unsigned long long)((esp_timer_get_time() - t0) / 1000),
             (unsigned)uxTaskGetStackHighWaterMark(NULL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    wifi_page_end();
    return e;
}

/**
 * @brief PUT /api/file.
 * @param r  the request
 * @return the handler's return
 */
static esp_err_t h_put(httpd_req_t *r) { return serve(r, WR_PUT); }

/**
 * @brief POST /api/mkdir.
 * @param r  the request
 * @return the handler's return
 */
static esp_err_t h_mkdir(httpd_req_t *r) { return serve(r, WR_MKDIR); }

/**
 * @brief POST /api/rename.
 * @param r  the request
 * @return the handler's return
 */
static esp_err_t h_rename(httpd_req_t *r) { return serve(r, WR_RENAME); }

/**
 * @brief POST /api/delete.
 * @param r  the request
 * @return the handler's return
 */
static esp_err_t h_delete(httpd_req_t *r) { return serve(r, WR_DELETE); }

void wifi_write_register(httpd_handle_t h)
{
    static const httpd_uri_t uris[WIFI_WRITE_URIS] = {
        { .uri = WA_API_URL_FILE,   .method = HTTP_PUT,  .handler = h_put },
        { .uri = WA_API_URL_MKDIR,  .method = HTTP_POST, .handler = h_mkdir },
        { .uri = WA_API_URL_RENAME, .method = HTTP_POST,
          .handler = h_rename },
        { .uri = WA_API_URL_DELETE, .method = HTTP_POST,
          .handler = h_delete },
    };
    for (size_t i = 0; i < WIFI_WRITE_URIS; ++i) {
        (void)httpd_register_uri_handler(h, &uris[i]);
    }
}
