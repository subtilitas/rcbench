/**
 * @file
 * @brief The page server's request headers.  See wifi_req.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi_req.h"

#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"
#include "wifi_access_int.h"

/** @brief The log tag, the component's. */
static const char *TAG = "wifi";

const char *wifi_req_hdr(httpd_req_t *r, const char *key, char *out,
                         size_t cap)
{
    const size_t n = httpd_req_get_hdr_value_len(r, key);
    if (n == 0u) {
        return NULL;
    }
    if (n >= cap || httpd_req_get_hdr_value_str(r, key, out, cap) != ESP_OK) {
        out[0] = '\0';
    }
    return out;
}

/**
 * @brief The panel as @p r may name it.  The server's socket is IPv6 with
 *        IPv4 mapped into it when lwIP has IPv6 (CONFIG_LWIP_IPV6); a
 *        connection over IPv6 itself has no address a Host may name.
 * @param r     the request
 * @param name  the panel's name
 * @param me    takes the panel
 */
static void req_self(httpd_req_t *r, const char *name, wa_self_t *me)
{
    me->name = name;
    memset(me->ip, 0, sizeof(me->ip));
    struct sockaddr_storage a;
    socklen_t al = sizeof(a);
    if (getsockname(httpd_req_to_sockfd(r), (struct sockaddr *)&a, &al)
        != 0) {
        return;
    }
    if (a.ss_family == AF_INET) {
        const struct sockaddr_in *a4 = (const struct sockaddr_in *)&a;
        memcpy(me->ip, &a4->sin_addr.s_addr, sizeof(me->ip));
    }
#if CONFIG_LWIP_IPV6
    if (a.ss_family == AF_INET6) {
        static const uint8_t k_mapped[12] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF,
        };
        const struct sockaddr_in6 *a6 = (const struct sockaddr_in6 *)&a;
        const uint8_t *v = a6->sin6_addr.s6_addr;
        if (memcmp(v, k_mapped, sizeof(k_mapped)) == 0) {
            memcpy(me->ip, v + 12, sizeof(me->ip));
        }
    }
#endif
}

wa_guard_t wifi_req_check(httpd_req_t *r, wa_req_kind_t kind,
                          const char *name, wifi_req_bufs_t *bufs)
{
    wa_self_t me;
    req_self(r, name, &me);
    wa_req_t q = { .body = (uint64_t)r->content_len };
    q.host = wifi_req_hdr(r, "Host", bufs->host, sizeof(bufs->host));
    if (kind != WA_REQ_READ) {
        q.origin = wifi_req_hdr(r, "Origin", bufs->origin,
                                sizeof(bufs->origin));
        q.referer = wifi_req_hdr(r, "Referer", bufs->referer,
                                 sizeof(bufs->referer));
    }
    if (kind != WA_REQ_READ && kind != WA_REQ_CODE) {
        q.upload = wifi_req_hdr(r, WA_UPLOAD_HDR, bufs->upload,
                                sizeof(bufs->upload));
    }
    const wa_guard_t g = wa_guard(kind, &q, &me);
    if (g != WA_GUARD_OK) {
        ESP_LOGW(TAG, "request refused: %s", wa_guard_status(g));
    }
    return g;
}

bool wifi_req_guard(httpd_req_t *r, wa_req_kind_t kind, const char *name,
                    wifi_req_bufs_t *bufs, esp_err_t *e)
{
    const wa_guard_t g = wifi_req_check(r, kind, name, bufs);
    if (g == WA_GUARD_OK) {
        return true;
    }
    httpd_resp_set_status(r, wa_guard_status(g));
    httpd_resp_set_hdr(r, "Connection", "close");
    (void)httpd_resp_sendstr(r, "");
    *e = wifi_req_close(r);
    return false;
}

/**
 * @brief wa_source_t's get(): the request's next bytes.
 * @param ctx  the request
 * @param p    takes them
 * @param n    how many at most
 * @return as wa_source_t says
 */
static int32_t close_get(void *ctx, uint8_t *p, uint32_t n)
{
    const int k = httpd_req_recv(ctx, (char *)p, n);
    if (k == HTTPD_SOCK_ERR_TIMEOUT) {
        return WA_GET_IDLE;
    }
    return (k < 0) ? WA_GET_FAILED : (int32_t)k;
}

/**
 * @brief wa_source_t's stop(): the radio goes off.
 * @param ctx  unused
 * @return true once it does
 */
static bool close_stop(void *ctx)
{
    (void)ctx;
    return atomic_load(wifi_page_abort());
}

/**
 * @brief wa_source_t's now_ms().
 * @param ctx  unused
 * @return milliseconds since the start
 */
static uint32_t close_now(void *ctx)
{
    (void)ctx;
    return (uint32_t)(esp_timer_get_time() / 1000);
}

esp_err_t wifi_req_close(httpd_req_t *r)
{
    const int fd = httpd_req_to_sockfd(r);
    (void)shutdown(fd, SHUT_WR);
    /* One slice a read, so the radio going off is seen; the socket does not
     * serve another request. */
    const struct timeval tv = { .tv_sec = 0,
                                .tv_usec = (suseconds_t)(WA_SEND_SLICE_MS * 1000u) };
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    const wa_source_t src = { close_get, close_stop, close_now, r };
    size_t cap = 0u;
    uint8_t *buf = wifi_page_io(&cap);
    const wa_linger_t end = wa_linger(&src, r->content_len, buf,
                                      (uint32_t)cap);
    if (end != WA_LINGER_DONE) {
        ESP_LOGI(TAG, "refused request's body: %s", wa_linger_name(end));
    }
    if (end == WA_LINGER_LATE || end == WA_LINGER_STOPPED) {
        return ESP_FAIL;
    }
    /* A body read to its end leaves the session up for the server; one cut
     * short ends it in httpd_req_delete(), whose next read fails or passes
     * its slice. */
    if (end == WA_LINGER_DONE) {
        (void)httpd_sess_trigger_close(r->handle, fd);
    }
    return ESP_OK;
}
