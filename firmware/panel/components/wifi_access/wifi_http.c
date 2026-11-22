/**
 * @file wifi_http.c
 * @brief The HTTPS (HTTP over TLS, transport layer security) client the
 *        fetch jobs of wa_fetch.c run against (wifi_http.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi_http.h"

#include <string.h>
#include <strings.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "fw_release.h"

/**
 * @brief A read waits this long at a time, so a job ended from outside stops
 *        its task within about this much once the data flows.
 */
#define FETCH_SLICE_MS 1000u
/** @brief The User-Agent of the GitHub requests. */
#define FETCH_AGENT    "rcbench-panel"

/** @brief Milliseconds since boot, truncated to 32 bits. */
static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/**
 * @brief The HTTP client wa_fetch.c runs against: esp_http_client over TLS,
 *        the server's chain checked against the certificate bundle, one
 *        request per connection, no redirect followed by the client.
 *        esp_http_client keeps every header value in memory of its own,
 *        without a bound of its own; only a server whose chain checks reaches
 *        it.
 */
typedef struct {
    esp_http_client_handle_t c;  /**< The client handle; NULL when closed. */
    char  *loc;  /**< The caller's buffer for the Location header. */
    size_t loc_cap;  /**< Bytes of loc. */
    size_t loc_n;  /**< Bytes of Location kept. */
    bool   loc_cut;  /**< Whether Location did not fit. */
    bool   loc_seen;  /**< Whether a Location header came. */
    bool   chunked;     /**< Transfer-Encoding: chunked */
    /* The last answer's head and body bytes read, for the console at the
     * job's end: kept past h_close(). */
    int      last_status;  /**< The last answer's HTTP status. */
    int64_t  last_length;  /**< Its Content-Length, -1 for none. */
    bool     last_chunked;  /**< Whether it was chunked. */
    uint32_t last_read;  /**< Body bytes read of it. */
} http_ctx_t;

/** @brief The HTTP client's state; one request at a time. */
static http_ctx_t s_http;

/** @brief The fetch task's stop flag (wifi_http_init()). */
static const atomic_bool *s_stop;

/**
 * @brief esp_http_client's event hook: take Transfer-Encoding and Location
 *        from the headers.
 */
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

/** @brief wa_http_t's close hook: close the client and free it. */
static void h_close(void *ctx)
{
    http_ctx_t *h = ctx;
    if (h->c != NULL) {
        (void)esp_http_client_close(h->c);
        (void)esp_http_client_cleanup(h->c);
        h->c = NULL;
    }
}

/**
 * @brief wa_http_t's open hook: connect to @p url and read the answer's head.
 */
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

/**
 * @brief wa_http_t's read hook: read up to @p n bytes in slices of
 *        FETCH_SLICE_MS; -1 on an error, a timeout or a stopped job.
 */
static int32_t h_read(void *ctx, uint8_t *dst, uint32_t n,
                      uint32_t timeout_ms)
{
    http_ctx_t *h = ctx;
    const uint32_t t0 = now_ms();
    for (;;) {
        if ((s_stop != NULL && atomic_load(s_stop)) || h->c == NULL) {
            return -1;
        }
        const uint32_t spent = (uint32_t)(now_ms() - t0);
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

const wa_http_t wifi_http = { h_open, h_read, h_close, &s_http };

void wifi_http_init(const atomic_bool *stop)
{
    s_stop = stop;
}

void wifi_http_forget(void)
{
    s_http.last_status = 0;
    s_http.last_length = -1;
    s_http.last_chunked = false;
    s_http.last_read = 0u;
}

void wifi_http_last(int *status, int64_t *length, bool *chunked,
                    uint32_t *read)
{
    *status = s_http.last_status;
    *length = s_http.last_length;
    *chunked = s_http.last_chunked;
    *read = s_http.last_read;
}
