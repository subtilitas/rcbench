/**
 * @file
 * @brief Which requests the Wi-Fi page answers at all: the Host header, the
 *        Origin or Referer of a write, the upload's own header and the size
 *        of a body.
 *
 * Three attacks are refused here, before any access code or card read.
 *
 * - DNS (domain name system) rebinding: a web page whose name resolves to
 *   the panel's address sends its own name as Host.  Every request needs a
 *   Host that names the panel: its mDNS (multicast DNS) name
 *   "rcbench-1A2B.local", the bare "rcbench-1A2B" the home router's DNS
 *   may know from the panel's DHCP (dynamic host configuration protocol)
 *   host name, or the IPv4 address the request came in on.  Letters
 *   compare without regard to case; a port, when given, is 80.  Anything
 *   else, an IPv6 literal included, is WA_GUARD_HOST (HTTP 421).
 * - A cross-site write: any page the browser has open can POST to the
 *   panel.  A write needs an Origin that is "http://" and an allowed Host;
 *   without an Origin, a Referer that starts that way.  Neither, "null",
 *   https and a foreign name are WA_GUARD_ORIGIN (HTTP 403).
 * - A cross-site upload that the browser sends without asking: the
 *   firmware upload and every write of the file API need WA_UPLOAD_HDR:
 *   WA_UPLOAD_VAL, which the page's own script sets.
 *   A header of its own makes a cross-site request a CORS (cross-origin
 *   resource sharing) one with a preflight, and the panel answers no
 *   preflight with an allow.  Missing or other: WA_GUARD_UPLOAD (403).
 *
 * And a slow body: a read takes none (WA_GUARD_BODY, HTTP 400), the code
 * form at most WA_CODE_BODY_MAX bytes, a file API's PUT at most
 * WA_PUT_BODY_MAX and its form at most WA_FORM_BODY_MAX (WA_GUARD_BIG,
 * HTTP 413).  The body
 * of a refused request never reaches a buffer of the bench: the connection
 * closes with the answer, the bytes still arriving read and dropped
 * (wa_linger()).
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WA_GUARD_H
#define RCBENCH_WA_GUARD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The upload's own request header. */
#define WA_UPLOAD_HDR     "X-Rcbench-Upload"
/** @brief The value the page's script gives WA_UPLOAD_HDR. */
#define WA_UPLOAD_VAL     "1"
/** @brief Bytes of the code form's body at most: "code=" and a kept code's
 *         63 characters, each up to 3 bytes percent-encoded, with room to
 *         spare. */
#define WA_CODE_BODY_MAX  200u
/** @brief Bytes of a PUT of the file API at most: the viewer's 1MiB
 *         (TEXT_BUF_FILE_MAX), which test_wa_guard holds it to. */
#define WA_PUT_BODY_MAX   1048576u
/** @brief Bytes of the form of a file API write at most (WA_API_FORM_MAX). */
#define WA_FORM_BODY_MAX  1024u
/** @brief The port the page's server listens on. */
#define WA_HTTP_PORT      80u

/** @brief What the request is to the guard. */
typedef enum {
    WA_REQ_READ = 0,  /**< GET: the page, a file, the ZIP              */
    WA_REQ_CODE,      /**< POST of the code form                       */
    WA_REQ_UPLOAD,    /**< POST of a firmware image                    */
    WA_REQ_PUT,       /**< PUT of a file through the file API          */
    WA_REQ_FORM,      /**< POST of a file API write: MKDIR, RENAME,
                           DELETE                                      */
} wa_req_kind_t;

/** @brief The guard's answer. */
typedef enum {
    WA_GUARD_OK = 0,
    WA_GUARD_HOST,    /**< Host missing or not the panel: 421          */
    WA_GUARD_BODY,    /**< a read with a body: 400                     */
    WA_GUARD_BIG,     /**< a code form over WA_CODE_BODY_MAX: 413      */
    WA_GUARD_ORIGIN,  /**< a write from another site, or unknown: 403  */
    WA_GUARD_UPLOAD,  /**< an upload without WA_UPLOAD_HDR: 403        */
} wa_guard_t;

/** @brief The panel as a request may name it. */
typedef struct {
    const char *name;  /**< "rcbench-1A2B", without ".local"             */
    uint8_t     ip[4]; /**< the address the request came in on; 0.0.0.0
                            when not known, which no Host matches     */
} wa_self_t;

/**
 * @brief The request's headers as the guard reads them.  NULL is a header
 *        not sent; a value longer than the platform's buffer is passed as
 *        "", sent and matching nothing.
 */
typedef struct {
    const char *host;      /**< Host                                   */
    const char *origin;    /**< Origin                                 */
    const char *referer;   /**< Referer                                */
    const char *upload;    /**< WA_UPLOAD_HDR's value; read for UPLOAD,
                                PUT and FORM                           */
    uint64_t    body;      /**< Content-Length; 0 for none             */
} wa_req_t;

/**
 * @brief Whether @p host, a Host header's value, names the panel.
 * @param host  the value; NULL refuses
 * @param self  the panel
 * @return true for the panel's name, as above, or its address
 */
bool wa_host_ok(const char *host, const wa_self_t *self);

/**
 * @brief Whether a write comes from the panel's own page.
 * @param origin   the Origin header, or NULL
 * @param referer  the Referer header, read only when @p origin is NULL
 * @param self     the panel
 * @return true for "http://" and a host wa_host_ok() takes, and after it
 *         nothing (Origin) or "/", "?", "#" or nothing (Referer)
 */
bool wa_origin_ok(const char *origin, const char *referer,
                  const wa_self_t *self);

/**
 * @brief The guard: Host first, then the body, then a write's Origin and,
 *        but for the code form, WA_UPLOAD_HDR.
 * @param kind  what the request is
 * @param req   its headers
 * @param self  the panel
 * @return WA_GUARD_OK, or the first check that refuses it
 */
wa_guard_t wa_guard(wa_req_kind_t kind, const wa_req_t *req,
                    const wa_self_t *self);

/**
 * @brief The HTTP status line of a refusal, "421 Misdirected Request" and
 *        the like.
 * @param g  the answer; WA_GUARD_OK gives "200 OK"
 * @return a constant string
 */
const char *wa_guard_status(wa_guard_t g);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WA_GUARD_H */
