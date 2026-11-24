/**
 * @file wifi_http.h
 * @brief The HTTPS (HTTP over TLS, transport layer security) client the
 *        fetch jobs of wa_fetch.c run against: esp_http_client, the
 *        server's chain checked against the certificate bundle, one request
 *        per connection, no redirect followed by the client.
 *
 * Only the fetch task calls it, one request at a time.  A read waits in
 * slices of 1000ms and ends with -1 once the stop flag given to
 * wifi_http_init() is set.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WIFI_HTTP_H
#define RCBENCH_WIFI_HTTP_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "wa_fetch.h"

/** @brief The client, as wa_run_t.http takes it. */
extern const wa_http_t wifi_http;

/**
 * @brief Name the flag that ends a read under way.
 * @param stop the fetch task's stop flag, set from outside
 */
void wifi_http_init(const atomic_bool *stop);

/** @brief Forget the last answer's head: a new job begins. */
void wifi_http_forget(void);

/**
 * @brief The last answer's head and the body bytes read of it, for the
 *        console line at a job's end.
 * @param status  takes its HTTP status, 0 for none
 * @param length  takes its Content-Length, -1 for none
 * @param chunked takes whether it was chunked
 * @param read    takes the body bytes read
 */
void wifi_http_last(int *status, int64_t *length, bool *chunked,
                    uint32_t *read);

#endif /* RCBENCH_WIFI_HTTP_H */
