/*
 * The page server's raw sends on its socket.  See wifi_sock.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi_sock.h"

#include <errno.h>
#include <stdint.h>
#include <sys/time.h>

#include "esp_timer.h"
#include "lwip/sockets.h"

static int32_t sock_put(void *ctx, const uint8_t *p, uint32_t n)
{
    const wifi_sock_t *c = ctx;
    const ssize_t sent = send(c->fd, p, n, MSG_DONTWAIT);
    if (sent >= 0) {
        return (int32_t)sent;
    }
    return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
}

/* Up to @p wait_ms for room to write, or for the receiver to close. */
static int wait_for(int fd, fd_set *rd, uint32_t wait_ms)
{
    fd_set wr;
    FD_ZERO(&wr);
    FD_SET(fd, &wr);
    if (rd != NULL) {
        FD_ZERO(rd);
        FD_SET(fd, rd);
    }
    struct timeval tv = {
        .tv_sec = (time_t)(wait_ms / 1000u),
        .tv_usec = (suseconds_t)((wait_ms % 1000u) * 1000u),
    };
    const int n = select(fd + 1, rd, &wr, NULL, &tv);
    if (n > 0 && rd != NULL && FD_ISSET(fd, &wr)) {
        FD_CLR(fd, rd);      /* room: what there is to read waits */
    }
    return n;
}

static wa_wait_t sock_wait(void *ctx, uint32_t wait_ms)
{
    const wifi_sock_t *c = ctx;
    fd_set rd;
    int n = wait_for(c->fd, &rd, wait_ms);
    if (n <= 0) {
        return (n == 0) ? WA_WAIT_IDLE : WA_WAIT_GONE;
    }
    if (!FD_ISSET(c->fd, &rd)) {
        return WA_WAIT_READY;
    }
    /* Readable and no room: a close reads as 0 bytes, a reset as an
     * error. */
    uint8_t one;
    const ssize_t got = recv(c->fd, &one, 1, MSG_PEEK | MSG_DONTWAIT);
    if (got == 0 || (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        return WA_WAIT_GONE;
    }
    /* Bytes from the receiver, a request sent ahead, stay for the server;
     * the slice waits for room alone, so they do not make every wait
     * return at once. */
    n = wait_for(c->fd, NULL, wait_ms);
    if (n <= 0) {
        return (n == 0) ? WA_WAIT_IDLE : WA_WAIT_GONE;
    }
    return WA_WAIT_READY;
}

static bool sock_stop(void *ctx)
{
    const wifi_sock_t *c = ctx;
    return atomic_load(c->abort);
}

static uint32_t sock_now(void *ctx)
{
    (void)ctx;
    return (uint32_t)(esp_timer_get_time() / 1000);
}

wa_sink_t wifi_sock_sink(wifi_sock_t *c)
{
    const wa_sink_t k = { sock_put, sock_wait, sock_stop, sock_now, c };
    return k;
}

/*
 * An orderly close keeps the unsent bytes queued and retransmits them to a
 * receiver that is gone for up to 12 tries (CONFIG_LWIP_TCP_MAXRTX); a
 * reset frees them and the connection at once.  SO_LINGER needs
 * CONFIG_LWIP_SO_LINGER.
 */
esp_err_t wifi_sock_drop(int fd)
{
    const struct linger l = { .l_onoff = 1, .l_linger = 0 };
    (void)setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
    return ESP_FAIL;
}
