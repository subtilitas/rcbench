/**
 * @file
 * @brief What wifi_access.c lends the page's file API (wifi_files.c): the
 *        start and the end of a request, the radio's abort flag, the
 *        server's JSON (JavaScript Object Notation) output and the page's
 *        name, version and language.
 *
 * The page's server runs one request at a time, so the output and the
 * buffers behind it are the request's from wifi_page_begin() to
 * wifi_page_end().
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WIFI_ACCESS_INT_H
#define RCBENCH_WIFI_ACCESS_INT_H

#include <stdatomic.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "wa_guard.h"
#include "wa_page.h"
#include "wa_text.h"

/** @brief What wifi_page_begin() made of a request. */
typedef enum {
    WIFI_PAGE_ANSWER = 0,  /**< answer it, then wifi_page_end()          */
    WIFI_PAGE_DONE,        /**< refused and answered: return the error   */
    WIFI_PAGE_NO_CODE,     /**< no access code, or a wrong one, counted:
                                refuse it, then wifi_page_end()          */
    WIFI_PAGE_REFUSED,     /**< the guard refused it, not answered: answer
                                it with the connection closed and return
                                ESP_FAIL; no wifi_page_end()             */
} wifi_page_go_t;

/**
 * @brief The start of a read of the file API: the guard (Host and body,
 *        wa_guard.h), the request counted for the idle time-out, and the
 *        access code.
 * @param r      the request
 * @param e      takes the handler's return for WIFI_PAGE_DONE
 * @param guard  takes the guard's refusal for WIFI_PAGE_REFUSED
 * @return what to do with it
 */
wifi_page_go_t wifi_page_begin(httpd_req_t *r, esp_err_t *e,
                               wa_guard_t *guard);

/**
 * @brief The end of a request wifi_page_begin() did not answer.
 */
void wifi_page_end(void);

/**
 * @brief The radio's abort flag: true from the moment it goes off.
 * @return the flag, for wifi_sock_t
 */
const atomic_bool *wifi_page_abort(void);

/**
 * @brief The server's output, in PSRAM (pseudo-static random-access
 *        memory): the request's between wifi_page_begin() and
 *        wifi_page_end().
 * @return the output
 */
wa_out_t *wifi_page_out(void);

/**
 * @brief The page's name, the firmware's version and the language.
 * @param pi  takes them
 */
void wifi_page_info(wa_page_info_t *pi);

#endif /* RCBENCH_WIFI_ACCESS_INT_H */
