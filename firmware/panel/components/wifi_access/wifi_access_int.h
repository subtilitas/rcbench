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
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "wa_page.h"
#include "wa_send.h"
#include "wa_walk.h"
#include "wa_text.h"

/** @brief What wifi_page_begin() made of a request. */
typedef enum {
    WIFI_PAGE_ANSWER = 0,  /**< answer it, then wifi_page_end()          */
    WIFI_PAGE_DONE,        /**< refused and answered: return the error   */
    WIFI_PAGE_NO_CODE,     /**< no access code, or a wrong one, counted:
                                refuse it, then wifi_page_end()          */
} wifi_page_go_t;

/**
 * @brief The start of a read of the file API: the guard (Host and body,
 *        wa_guard.h), the request counted for the idle time-out, and the
 *        access code.
 * @param r  the request
 * @param e  takes the handler's return for WIFI_PAGE_DONE
 * @return what to do with it
 */
wifi_page_go_t wifi_page_begin(httpd_req_t *r, esp_err_t *e);

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

/**
 * @brief The card as the page's walks and downloads read it: every file
 *        and folder open under a hold, the walk stopped with the radio.
 * @return the card
 */
const wa_fs_t *wifi_page_card(void);

/**
 * @brief The server's chunk buffer, in PSRAM: the request's between
 *        wifi_page_begin() and wifi_page_end().
 * @param cap  takes its bytes
 * @return the buffer
 */
uint8_t *wifi_page_io(size_t *cap);

/**
 * @brief The panel's name on the network, "rcbench-1A2B".
 * @return the name
 */
const char *wifi_page_name(void);

/**
 * @brief Count a long download's progress as traffic: the idle time-out
 *        starts again from now.
 */
void wifi_page_traffic(void);

/**
 * @brief The header of a response of known length that the browser saves
 *        as @p name, on the socket.
 * @param r       the request
 * @param length  the bytes that follow it
 * @param name    the file's name
 * @return WA_SEND_DONE when it went out
 */
wa_send_res_t wifi_page_raw_head(httpd_req_t *r, uint64_t length,
                                 const char *name);

/**
 * @brief Raw bytes of a response of known length, on the socket
 *        (wifi_sock.h); they stop with the radio.
 * @param r     the request
 * @param data  the bytes
 * @param n     how many
 * @return WA_SEND_DONE when every byte went out
 */
wa_send_res_t wifi_page_raw_send(httpd_req_t *r, const void *data, size_t n);

#endif /* RCBENCH_WIFI_ACCESS_INT_H */
