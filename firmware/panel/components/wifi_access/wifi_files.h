/**
 * @file
 * @brief The page's file API on the panel's server (wa_api.h): its four
 *        GET handlers, the writes' (wifi_write.h), and its PSRAM
 *        (pseudo-static random-access memory), taken at the first request
 *        and given back with the server.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WIFI_FILES_H
#define RCBENCH_WIFI_FILES_H

#include <stdbool.h>

#include "esp_http_server.h"
#include "wa_api.h"
#include "wa_app.h"
#include "wifi_write.h"

/** @brief Handlers wifi_files_register() adds, the writes' included. */
#define WIFI_FILES_URIS (4 + WIFI_WRITE_URIS)

/**
 * @brief Register /api/status, /api/list, /api/info and /api/file, and the
 *        writes (wifi_write_register()).
 * @param h  the page's server, started
 */
void wifi_files_register(httpd_handle_t h);

/**
 * @brief Give the API's PSRAM back: the request's room, 1MiB of text and
 *        the 256KiB line index; and close the writes' window
 *        (wifi_write_reset()).  Only with the server stopped.
 */
void wifi_files_free(void);

/**
 * @brief The API over the request's room, taken at the first request that
 *        needs it, with the log folders in force; with @p text also the
 *        file's 1MiB and its line index.
 * @param text  a file's room is needed
 * @param why   takes WA_API_NO_CARD (no card set), WA_API_MEMORY (no
 *              PSRAM) or WA_API_OK
 * @return the API; NULL with @p why set
 */
wa_api_t *wifi_files_api(bool text, wa_api_err_t *why);

/**
 * @brief The words and colours wifi_access_set_hl() set.
 * @return them; NULL before
 */
const wa_hl_t *wifi_files_hl(void);

#endif /* RCBENCH_WIFI_FILES_H */
