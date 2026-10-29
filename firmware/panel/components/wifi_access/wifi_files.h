/**
 * @file
 * @brief The page's file API on the panel's server (wa_api.h): its four
 *        GET handlers, and its PSRAM (pseudo-static random-access memory),
 *        taken at the first request and given back with the server.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WIFI_FILES_H
#define RCBENCH_WIFI_FILES_H

#include "esp_http_server.h"

/** @brief Handlers wifi_files_register() adds. */
#define WIFI_FILES_URIS 4

/**
 * @brief Register /api/status, /api/list, /api/info and /api/file.
 * @param h  the page's server, started
 */
void wifi_files_register(httpd_handle_t h);

/**
 * @brief Give the API's PSRAM back: the request's room, 1MiB of text and
 *        the 256KiB line index.  Only with the server stopped.
 */
void wifi_files_free(void);

#endif /* RCBENCH_WIFI_FILES_H */
