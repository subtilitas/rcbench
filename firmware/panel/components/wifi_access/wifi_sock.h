/*
 * The page server's raw sends on its socket (wa_send.h's sink).
 *
 * The page's server has no TLS (transport layer security) and no send of
 * its own, so a response whose length is known up front is written to the
 * socket directly, without waiting (MSG_DONTWAIT), and a full send buffer
 * is waited out in slices of WA_SEND_SLICE_MS.  Each slice looks at the
 * radio going off and at the receiver: a receiver that closed ends the send
 * at its next slice, not at the next retransmission.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WIFI_SOCK_H
#define RCBENCH_WIFI_SOCK_H

#include <stdatomic.h>

#include "esp_err.h"
#include "wa_send.h"

typedef struct {
    int                fd;      /* the request's socket       */
    const atomic_bool *abort;   /* true: the radio goes off   */
} wifi_sock_t;

/** The sink of @p c, which must outlive it. */
wa_sink_t wifi_sock_sink(wifi_sock_t *c);

/**
 * A send that failed: the connection on @p fd is reset when the server
 * closes it, not closed in order.  Returns ESP_FAIL, which closes it.
 */
esp_err_t wifi_sock_drop(int fd);

#endif /* RCBENCH_WIFI_SOCK_H */
