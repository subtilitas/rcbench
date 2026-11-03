/**
 * @file
 * @brief GET /card.zip on the page's server: the whole card as one ZIP
 *        archive (wa_zip.h), read through an aligned read buffer and
 *        logged at its end.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WIFI_ZIP_H
#define RCBENCH_WIFI_ZIP_H

#include "esp_http_server.h"

/**
 * @brief GET /card.zip: the whole card as one ZIP archive, its length
 *        announced before the first byte.  Logs its end: the bytes sent of
 *        the length, the time, and for a failure the entry and the cause.
 * @param r the request
 * @return ESP_OK when the archive went out whole.  A failure part way sends
 *         the bytes wa_zip_next() made up to it, then resets the connection
 *         (ESP_FAIL): the browser has fewer bytes than announced and reports
 *         the download failed.
 */
esp_err_t wifi_zip_get(httpd_req_t *r);

#endif /* RCBENCH_WIFI_ZIP_H */
