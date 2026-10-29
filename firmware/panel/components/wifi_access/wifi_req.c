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
#include "lwip/sockets.h"
#include "sdkconfig.h"

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
    if (kind == WA_REQ_UPLOAD) {
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
    *e = ESP_FAIL;
    return false;
}
