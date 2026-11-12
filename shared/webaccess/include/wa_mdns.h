/**
 * @file
 * @brief The panel's name on the home network: an mDNS (multicast Domain Name
 *        System, RFC 6762) answer for "rcbench-1A2B.local".
 *
 * Only the one name and only its address (type A): a query for it, of type
 * A or ANY, is answered; everything else is not.  A query from port 5353 is
 * answered to the group with ID 0 and no question; one from any other port
 * is a one-shot resolver's and gets its ID and its question back, with a
 * TTL (time to live) of 10s as section 6.7 asks.  Names compare without
 * regard to the case of ASCII letters.
 *
 * Pure C, no sockets: the platform receives on 224.0.0.251 port 5353 and
 * sends what this returns.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WA_MDNS_H
#define RCBENCH_WA_MDNS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The multicast DNS (Domain Name System) UDP port. */
#define WA_MDNS_PORT   5353u
/** @brief Seconds an answer to the group may be kept. */
#define WA_MDNS_TTL    120u
/** @brief Bytes an answer takes at most. */
#define WA_MDNS_MAX    512u

/**
 * @brief The answer to the query @p q of @p n bytes from port @p src_port,
 *        for @p host (without ".local") at @p ip, into @p out of @p cap
 *        bytes.
 *
 * @param q The query.
 * @param n The query's length, in bytes.
 * @param src_port The port the query came from.
 * @param host The name, without ".local".
 * @param ip The IPv4 address, four bytes.
 * @param out Receives the answer.
 * @param cap The size of @p out, in bytes.
 * @return The answer's length in bytes, or 0 when the query is not for this
 *         name, is not a query, or is malformed.
 */
size_t wa_mdns_reply(const uint8_t *q, size_t n, uint16_t src_port,
                     const char *host, const uint8_t ip[4], uint8_t *out,
                     size_t cap);

/**
 * @brief An announcement of @p host at @p ip, sent unasked when the address
 *        comes.
 *
 * @param host The name, without ".local".
 * @param ip The IPv4 address, four bytes.
 * @param out Receives the announcement.
 * @param cap The size of @p out, in bytes.
 * @return The announcement's length in bytes, or 0 when it does not fit.
 */
size_t wa_mdns_announce(const char *host, const uint8_t ip[4], uint8_t *out,
                        size_t cap);

/**
 * @brief The responder's starts in one join of the home network.
 *
 * A start that fails -- no task, no socket, no bind, no group membership --
 * is tried again WA_MDNS_RETRIES times, WA_MDNS_RETRY_MS apart; after that the
 * name stays unanswered until the next join, and the address shown on the
 * panel still works.
 */
#define WA_MDNS_RETRIES   3u
#define WA_MDNS_RETRY_MS  5000u  /**< ms between two starts */

/** @brief The starts made in one join. */
typedef struct {
    uint8_t  starts;     /**< starts made in this join                 */
    uint32_t next_ms;    /**< earliest next start                      */
} wa_mdns_tries_t;

/**
 * @brief A new join: no start made.
 *
 * @param t The starts.
 */
void wa_mdns_tries_reset(wa_mdns_tries_t *t);

/**
 * @brief Whether the responder, not running, may be started now.
 *
 * @param t The starts.
 * @param now_ms The current time, in ms.
 * @return true when a start is due.
 */
bool wa_mdns_tries_due(const wa_mdns_tries_t *t, uint32_t now_ms);

/**
 * @brief A start made at @p now_ms.
 *
 * @param t The starts.
 * @param now_ms The current time, in ms.
 */
void wa_mdns_tries_started(wa_mdns_tries_t *t, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WA_MDNS_H */
