/**
 * @file
 * @brief The writes of the page's file API on the panel's server
 *        (wa_api.h): PUT of a file and the MKDIR, RENAME and DELETE forms,
 *        whether a browser may write (SETUP's Browser writes, the hold on
 *        WI-FI), and the card job lock they share with FILES.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WIFI_WRITE_H
#define RCBENCH_WIFI_WRITE_H

#include <stdbool.h>

#include "esp_http_server.h"
#include "wa_api.h"

/** @brief Handlers wifi_write_register() adds. */
#define WIFI_WRITE_URIS 4

/**
 * @brief Register PUT /api/file, POST /api/mkdir, /api/rename and
 *        /api/delete.
 * @param h  the page's server, started
 */
void wifi_write_register(httpd_handle_t h);

/**
 * @brief The radio went off: the hold's window closes and the count of
 *        writes starts again.  Only with the server stopped.
 */
void wifi_write_reset(void);

/**
 * @brief SETUP's Browser writes and the hold's window, for /api/status.
 * @param window  takes whether the hold opened writes this session
 * @return the mode
 */
wa_writes_t wifi_write_mode(bool *window);

#endif /* RCBENCH_WIFI_WRITE_H */
