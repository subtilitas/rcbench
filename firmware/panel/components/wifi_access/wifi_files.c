/**
 * @file
 * @brief The page's file API on the panel's server: the handlers of
 *        wa_api.h's four reads over the card FILES uses (fm_io.h).
 *
 * Each request runs on the server's task, one at a time, and makes its
 * card calls through the file manager's runner (fm_req.h) on the card the
 * panel set (wifi_access_set_files()).  Reads run beside FILES' own task
 * (cardjob) as the page's downloads do: FatFs takes one call at a time,
 * and nothing here writes.
 *
 * The PSRAM (pseudo-static random-access memory) is taken at the first
 * request that needs it and kept until the server stops: the request's
 * room (about 21kB) at the first list, INFO or file, and the text's 1MiB
 * and its 256KiB line index at the first file.  No internal RAM is taken.
 *
 * Each list, INFO and file request logs its answer, its time and the
 * stack the server's task left unused: the depth of a card call on that
 * task is not measured.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi_files.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fm_rules.h"
#include "storage.h"
#include "wa_api.h"
#include "wifi_access.h"
#include "wifi_access_int.h"
#include "wifi_sock.h"

/** @brief The log tag, wifi_access.c's. */
static const char *TAG = "wifi";

/** @brief Bytes of the card's name in the status answer (storage.h). */
#define CARD_NAME_MAX 40u

/** @brief One request's room, in PSRAM. */
typedef struct {
    fm_req_t req;                   /**< the core's request            */
    wa_api_t api;                   /**< the API over it               */
    fm_runner_t run;                /**< the card, the bench, the chunk */
    uint8_t chunk[FM_CHUNK];        /**< a load's chunk                */
    char head[WA_API_HEAD_MAX];     /**< a file answer's header        */
} room_t;

/** @brief The card and the bench, set once; the log folders per session. */
typedef struct {
    portMUX_TYPE mux;                  /**< guards the log folders     */
    const fm_io_t *io;                 /**< the card; NULL until set   */
    void (*bench)(fm_bench_t *out, void *ctx); /**< the bench now      */
    char user_logs[CARD_DIR_MAX];      /**< USER LOG FOLDER            */
    char auto_logs[CARD_DIR_MAX];      /**< AUTO LOG FOLDER            */
} card_t;

/** @brief The card the API reads, and what its rules need. */
static card_t s_card = { .mux = portMUX_INITIALIZER_UNLOCKED };

/** @brief The page's words and colours; NULL until set. */
static const wa_hl_t *s_hl;

/** @brief The request's room; NULL until the first request needs it. */
static room_t *s_room;
/** @brief The text of a file, TEXT_BUF_FILE_MAX bytes; NULL until used. */
static uint8_t *s_text;
/** @brief Its line index, TEXT_BUF_ENDS_MAX entries; NULL until used. */
static uint32_t *s_ends;

/** @brief What a handler answers. */
typedef enum {
    API_STATUS = 0,
    API_LIST,
    API_INFO,
    API_FILE
} api_t;

/** @brief Each request's name in the log. */
static const char *const k_name[] = { "status", "list", "info", "file" };

void wifi_access_set_hl(const wa_hl_t *hl)
{
    s_hl = hl;
}

const wa_hl_t *wifi_files_hl(void)
{
    return s_hl;
}

void wifi_access_set_files(const fm_io_t *io,
                           void (*bench)(fm_bench_t *out, void *ctx))
{
    s_card.io = io;
    s_card.bench = bench;
}

/**
 * @brief Copy @p dir into @p out, "" for NULL or a folder too long.
 * @param out  CARD_DIR_MAX bytes
 * @param dir  the folder
 */
static void copy_dir(char *out, const char *dir)
{
    out[0] = '\0';
    if (dir != NULL && strlen(dir) < CARD_DIR_MAX) {
        memcpy(out, dir, strlen(dir) + 1u);
    }
}

void wifi_access_set_log_dirs(const char *user_logs, const char *auto_logs)
{
    char u[CARD_DIR_MAX];
    char a[CARD_DIR_MAX];
    copy_dir(u, user_logs);
    copy_dir(a, auto_logs);
    taskENTER_CRITICAL(&s_card.mux);
    memcpy(s_card.user_logs, u, sizeof(u));
    memcpy(s_card.auto_logs, a, sizeof(a));
    taskEXIT_CRITICAL(&s_card.mux);
}

/**
 * @brief The runner's bench(): the panel's, or SAFE with the link up
 *        before it is set.
 * @param out  takes the bench
 * @param ctx  unused
 */
static void run_bench(fm_bench_t *out, void *ctx)
{
    (void)ctx;
    if (s_card.bench != NULL) {
        s_card.bench(out, NULL);
    } else {
        out->link_up = true;
    }
}

/**
 * @brief The runner's cancelled(): the radio goes off.
 * @param ctx  unused
 * @return true once it does
 */
static bool run_cancelled(void *ctx)
{
    (void)ctx;
    return atomic_load(wifi_page_abort());
}

/**
 * @brief The request's room, taken at the first request.
 * @return false with no card set or no PSRAM
 */
static bool room_take(void)
{
    if (s_card.io == NULL) {
        return false;
    }
    if (s_room == NULL) {
        s_room = heap_caps_calloc(1u, sizeof(*s_room), MALLOC_CAP_SPIRAM);
        if (s_room == NULL) {
            return false;
        }
        const fm_runner_t r = {
            .io = s_card.io, .bench = run_bench, .cancelled = run_cancelled,
            .progress = NULL, .look = NULL, .chunk = s_room->chunk,
            .size = sizeof(s_room->chunk), .ctx = NULL,
        };
        s_room->run = r;
        wa_api_init(&s_room->api, &s_room->run, &s_room->req);
    }
    char u[CARD_DIR_MAX];
    char a[CARD_DIR_MAX];
    taskENTER_CRITICAL(&s_card.mux);
    memcpy(u, s_card.user_logs, sizeof(u));
    memcpy(a, s_card.auto_logs, sizeof(a));
    taskEXIT_CRITICAL(&s_card.mux);
    wa_api_set_logs(&s_room->api, u, a);
    return true;
}

/**
 * @brief The text's room, taken at the first file request.
 * @return false with no PSRAM for it
 */
static bool text_take(void)
{
    if (s_text == NULL) {
        s_text = heap_caps_malloc(TEXT_BUF_FILE_MAX, MALLOC_CAP_SPIRAM);
    }
    if (s_ends == NULL) {
        s_ends = heap_caps_malloc(TEXT_BUF_ENDS_MAX * sizeof(s_ends[0]),
                                  MALLOC_CAP_SPIRAM);
    }
    wa_api_set_text(&s_room->api, s_text, TEXT_BUF_FILE_MAX, s_ends,
                    TEXT_BUF_ENDS_MAX);
    return s_text != NULL && s_ends != NULL;
}

void wifi_files_free(void)
{
    heap_caps_free(s_text);
    heap_caps_free(s_ends);
    heap_caps_free(s_room);
    s_text = NULL;
    s_ends = NULL;
    s_room = NULL;
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
 * @brief The status answer's facts, now.
 * @param st    takes them
 * @param pi    the page's name, version and language
 * @param card  room for the card's name, CARD_NAME_MAX bytes
 * @param logs  room for the User log folder, CARD_DIR_MAX bytes
 */
static void status_now(wa_api_status_t *st, const wa_page_info_t *pi,
                       char *card, char *logs)
{
    memset(st, 0, sizeof(*st));
    st->name = pi->name;
    st->version = pi->version;
    st->de = pi->de;
    st->mounted = storage_mounted();
    st->mount = storage_mount_count();
    card[0] = '\0';
    if (st->mounted) {
        storage_card_name(card, CARD_NAME_MAX);
        st->total = storage_total_bytes();
        st->free = storage_free_bytes();
    }
    st->card = card;
    fm_bench_t bench;
    memset(&bench, 0, sizeof(bench));
    run_bench(&bench, NULL);
    st->gate = fm_gate(FM_OP_SAVE, &bench);
    taskENTER_CRITICAL(&s_card.mux);
    memcpy(logs, s_card.user_logs, CARD_DIR_MAX);
    taskEXIT_CRITICAL(&s_card.mux);
    st->user_logs = logs;
}

/**
 * @brief A JSON answer: the status line, then the body @p kind writes, or
 *        the refusal @p why.
 * @param r     the request
 * @param kind  what was asked
 * @param why   WA_API_OK, or why it is refused
 * @param de    German texts
 * @return ESP_OK when the answer went out
 */
static esp_err_t answer_json(httpd_req_t *r, api_t kind, wa_api_err_t why,
                             bool de)
{
    httpd_resp_set_status(r, wa_api_status_line(why));
    httpd_resp_set_type(r, "application/json; charset=utf-8");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    wa_out_t *o = wifi_page_out();
    wa_out_init(o, sink_chunk, r);
    if (why != WA_API_OK) {
        wa_api_error_json(o, why, de);
    } else if (kind == API_STATUS) {
        wa_page_info_t pi;
        wifi_page_info(&pi);
        wa_api_status_t st;
        char card[CARD_NAME_MAX];
        char logs[CARD_DIR_MAX];
        status_now(&st, &pi, card, logs);
        wa_api_status_json(o, &st);
    } else if (kind == API_LIST) {
        wa_api_list_json(&s_room->api, o);
    } else {
        wa_api_info_json(&s_room->api, o);
    }
    if (!wa_out_flush(o)) {
        return ESP_FAIL;
    }
    return httpd_resp_send_chunk(r, NULL, 0);
}

/**
 * @brief The file wa_api_file() loaded, on the socket: its header, then
 *        its bytes a chunk at a time.
 * @param r  the request
 * @return ESP_OK when every byte went out; a reset connection otherwise
 */
static esp_err_t answer_file(httpd_req_t *r)
{
    wifi_sock_t c = { httpd_req_to_sockfd(r), wifi_page_abort() };
    const wa_sink_t k = wifi_sock_sink(&c);
    const size_t hn = wa_api_file_head(&s_room->api, s_room->head,
                                       sizeof(s_room->head));
    wa_send_res_t res = (hn > 0u) ? wa_sink_all(&k, s_room->head,
                                                (uint32_t)hn)
                                  : WA_SEND_GONE;
    size_t at = 0u;
    while (res == WA_SEND_DONE) {
        const size_t got = wa_api_file_read(&s_room->api, at,
                                            s_room->chunk,
                                            sizeof(s_room->chunk));
        if (got == 0u) {
            break;
        }
        res = wa_sink_all(&k, s_room->chunk, (uint32_t)got);
        at += got;
    }
    return (res == WA_SEND_DONE) ? ESP_OK
                                 : wifi_sock_drop(httpd_req_to_sockfd(r));
}

/**
 * @brief Answer one request of the API.
 * @param r     the request
 * @param kind  what it asks
 * @return the handler's return
 */
static esp_err_t serve(httpd_req_t *r, api_t kind)
{
    esp_err_t e = ESP_OK;
    wa_guard_t guard = WA_GUARD_OK;
    const wifi_page_go_t go = wifi_page_begin(r, &e, &guard);
    if (go == WIFI_PAGE_DONE) {
        return e;
    }
    if (go == WIFI_PAGE_REFUSED) {
        /* Its body is not read: the connection goes with the answer, as
         * for the guard's other refusals (wifi_req.h). */
        wa_page_info_t pi;
        wifi_page_info(&pi);
        httpd_resp_set_hdr(r, "Connection", "close");
        (void)answer_json(r, kind, wa_api_of_guard(guard), pi.de);
        return ESP_FAIL;
    }
    const int64_t t0 = esp_timer_get_time();
    const char *q = strchr(r->uri, '?');
    const char *query = (q != NULL) ? q + 1 : "";
    wa_page_info_t pi;
    wifi_page_info(&pi);
    wa_api_err_t why = WA_API_OK;
    if (go == WIFI_PAGE_NO_CODE) {
        why = WA_API_CODE;
    } else if (kind != API_STATUS && !room_take()) {
        why = (s_card.io == NULL) ? WA_API_NO_CARD : WA_API_MEMORY;
    } else if (kind == API_LIST) {
        why = wa_api_list(&s_room->api, query);
    } else if (kind == API_INFO) {
        why = wa_api_info(&s_room->api, query);
    } else if (kind == API_FILE) {
        why = text_take() ? wa_api_file(&s_room->api, query)
                          : WA_API_MEMORY;
    }
    if (kind == API_FILE && why == WA_API_OK) {
        e = answer_file(r);
    } else {
        e = answer_json(r, kind, why, pi.de);
    }
    if (kind != API_STATUS) {
        ESP_LOGI(TAG, "api: %s %s, %lluB in %llums; httpd stack %uB unused",
                 k_name[kind], (why == WA_API_OK) ? "OK" : wa_api_word(why),
                 (unsigned long long)((kind == API_FILE && why == WA_API_OK)
                                          ? wa_api_file_size(&s_room->api)
                                          : 0u),
                 (unsigned long long)((esp_timer_get_time() - t0) / 1000),
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
    wifi_page_end();
    return e;
}

/**
 * @brief GET /api/status.
 * @param r  the request
 * @return the handler's return
 */
static esp_err_t h_status(httpd_req_t *r) { return serve(r, API_STATUS); }

/**
 * @brief GET /api/list.
 * @param r  the request
 * @return the handler's return
 */
static esp_err_t h_list(httpd_req_t *r) { return serve(r, API_LIST); }

/**
 * @brief GET /api/info.
 * @param r  the request
 * @return the handler's return
 */
static esp_err_t h_info(httpd_req_t *r) { return serve(r, API_INFO); }

/**
 * @brief GET /api/file.
 * @param r  the request
 * @return the handler's return
 */
static esp_err_t h_file(httpd_req_t *r) { return serve(r, API_FILE); }

void wifi_files_register(httpd_handle_t h)
{
    static const httpd_uri_t uris[WIFI_FILES_URIS] = {
        { .uri = WA_API_URL_STATUS, .method = HTTP_GET, .handler = h_status },
        { .uri = WA_API_URL_LIST,   .method = HTTP_GET, .handler = h_list },
        { .uri = WA_API_URL_INFO,   .method = HTTP_GET, .handler = h_info },
        { .uri = WA_API_URL_FILE,   .method = HTTP_GET, .handler = h_file },
    };
    for (size_t i = 0; i < WIFI_FILES_URIS; ++i) {
        (void)httpd_register_uri_handler(h, &uris[i]);
    }
}
