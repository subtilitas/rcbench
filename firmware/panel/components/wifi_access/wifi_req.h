/**
 * @file
 * @brief The page server's request headers, as the guard (wa_guard.h) and the
 * access code check read them.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WIFI_REQ_H
#define RCBENCH_WIFI_REQ_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "wa_guard.h"

/** @brief Where wifi_req_guard() reads a request's headers into. */
typedef struct {
    char host[64];     /**< Host: the panel's names fit in 30 bytes   */
    char origin[64];   /**< Origin: "http://" and a host              */
    char referer[128]; /**< Referer: the page's own, "/" or "/code"   */
    char upload[8];    /**< WA_UPLOAD_HDR: "1"                        */
} wifi_req_bufs_t;

/**
 * @brief Header @p key of @p r into @p out.
 * @param r    the request
 * @param key  the header's name
 * @param out  takes the value
 * @param cap  bytes of @p out
 * @return @p out; @p out holding "" for a value of @p cap bytes or more,
 *         which matches nothing; NULL for a header not sent or empty
 */
const char *wifi_req_hdr(httpd_req_t *r, const char *key, char *out,
                         size_t cap);

/**
 * @brief wa_guard() over @p r: the Host, the body's size, a write's Origin
 *        or Referer and the upload's header.
 *
 * The panel's address is the IPv4 address the connection came in on: the
 * access point's in AP, the station's in JOIN.  A refusal is answered
 * here, with no body and the connection closed, so a body not read is
 * never read: httpd_req_delete() would read one to its end, a receive
 * timeout at a time.
 *
 * @param r     the request
 * @param kind  what it is
 * @param name  the panel's name, "rcbench-1A2B"
 * @param bufs  the header buffers
 * @param e     takes the handler's return on a refusal
 * @return true when the request may be answered
 */
bool wifi_req_guard(httpd_req_t *r, wa_req_kind_t kind, const char *name,
                    wifi_req_bufs_t *bufs, esp_err_t *e);

#endif /* RCBENCH_WIFI_REQ_H */
