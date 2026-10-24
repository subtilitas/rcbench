/**
 * @file
 * @brief Which requests the Wi-Fi page answers. See wa_guard.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wa_guard.h"

#include "wa_session.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

/** @brief Bytes of the longest name the guard compares with,
 *         "rcbench-1A2B.local.", and its terminator. */
#define HOST_MAX (WA_NAME_MAX + 7u)

/**
 * @brief Whether @p n bytes at @p s are @p want, letters compared without
 *        regard to case.
 * @param s     the bytes
 * @param n     how many
 * @param want  the text, NUL-terminated
 * @return true when equal
 */
static bool same(const char *s, size_t n, const char *want)
{
    return strlen(want) == n && strncasecmp(s, want, n) == 0;
}

/**
 * @brief Whether @p n bytes at @p s, a host without its port, name the
 *        panel.
 * @param s     the host
 * @param n     its bytes
 * @param self  the panel
 * @return true for the name, the name with ".local" or ".local.", or the
 *         address in dotted form
 */
static bool host_name_ok(const char *s, size_t n, const wa_self_t *self)
{
    if (self == NULL || self->name == NULL || self->name[0] == '\0') {
        return false;
    }
    char want[HOST_MAX];
    if (same(s, n, self->name)) {
        return true;
    }
    (void)snprintf(want, sizeof(want), "%s.local", self->name);
    if (same(s, n, want)) {
        return true;
    }
    (void)snprintf(want, sizeof(want), "%s.local.", self->name);
    if (same(s, n, want)) {
        return true;
    }
    const uint8_t *ip = self->ip;
    if ((ip[0] | ip[1] | ip[2] | ip[3]) == 0u) {
        return false;
    }
    (void)snprintf(want, sizeof(want), "%u.%u.%u.%u", (unsigned)ip[0],
                   (unsigned)ip[1], (unsigned)ip[2], (unsigned)ip[3]);
    return same(s, n, want);
}

/**
 * @brief Whether @p n bytes at @p s, a host with an optional port, name
 *        the panel.
 * @param s     the host part of a Host, an Origin or a Referer
 * @param n     its bytes
 * @param self  the panel
 * @return true for host_name_ok() and no port or port WA_HTTP_PORT
 */
static bool host_part_ok(const char *s, size_t n, const wa_self_t *self)
{
    /* An empty host and an IPv6 literal match no name: the literal's first
     * ':' starts no port of "80". */
    const char *colon = memchr(s, ':', n);
    size_t name_n = n;
    if (colon != NULL) {
        name_n = (size_t)(colon - s);
        char port[8];
        (void)snprintf(port, sizeof(port), "%u", (unsigned)WA_HTTP_PORT);
        if (!same(colon + 1, n - name_n - 1u, port)) {
            return false;
        }
    }
    return host_name_ok(s, name_n, self);
}

bool wa_host_ok(const char *host, const wa_self_t *self)
{
    return host != NULL && host_part_ok(host, strlen(host), self);
}

/**
 * @brief Whether @p url is "http://" and an allowed host, followed by
 *        nothing or, with @p path, by "/", "?" or "#".
 * @param url   an Origin or a Referer
 * @param path  a Referer: a path may follow
 * @param self  the panel
 * @return true for the panel's own page
 */
static bool url_ok(const char *url, bool path, const wa_self_t *self)
{
    static const char k_scheme[] = "http://";
    const size_t sn = sizeof(k_scheme) - 1u;
    if (strncasecmp(url, k_scheme, sn) != 0) {
        return false;
    }
    const char *h = url + sn;
    const size_t n = path ? strcspn(h, "/?#") : strlen(h);
    return host_part_ok(h, n, self);
}

bool wa_origin_ok(const char *origin, const char *referer,
                  const wa_self_t *self)
{
    if (origin != NULL) {
        return url_ok(origin, false, self);  /* "null" fails the scheme */
    }
    return referer != NULL && url_ok(referer, true, self);
}

wa_guard_t wa_guard(wa_req_kind_t kind, const wa_req_t *req,
                    const wa_self_t *self)
{
    if (req == NULL || !wa_host_ok(req->host, self)) {
        return WA_GUARD_HOST;
    }
    if (kind == WA_REQ_READ && req->body > 0u) {
        return WA_GUARD_BODY;
    }
    if (kind == WA_REQ_CODE && req->body > WA_CODE_BODY_MAX) {
        return WA_GUARD_BIG;
    }
    if (kind == WA_REQ_READ) {
        return WA_GUARD_OK;
    }
    if (!wa_origin_ok(req->origin, req->referer, self)) {
        return WA_GUARD_ORIGIN;
    }
    if (kind == WA_REQ_UPLOAD
        && (req->upload == NULL || strcmp(req->upload, WA_UPLOAD_VAL) != 0)) {
        return WA_GUARD_UPLOAD;
    }
    return WA_GUARD_OK;
}

const char *wa_guard_status(wa_guard_t g)
{
    switch (g) {
    case WA_GUARD_HOST:
        return "421 Misdirected Request";
    case WA_GUARD_BODY:
        return "400 Bad Request";
    case WA_GUARD_BIG:
        return "413 Content Too Large";
    case WA_GUARD_ORIGIN:
    case WA_GUARD_UPLOAD:
        return "403 Forbidden";
    case WA_GUARD_OK:
    default:
        return "200 OK";
    }
}
