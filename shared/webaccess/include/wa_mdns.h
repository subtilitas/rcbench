/*
 * The panel's name on the home network: an mDNS (multicast Domain Name
 * System, RFC 6762) answer for "rcbench-1A2B.local".
 *
 * Only the one name and only its address (type A): a query for it, of type
 * A or ANY, is answered; everything else is not.  A query from port 5353 is
 * answered to the group with ID 0 and no question; one from any other port
 * is a one-shot resolver's and gets its ID and its question back, with a
 * TTL (time to live) of 10 s as section 6.7 asks.  Names compare without
 * regard to the case of ASCII letters.
 *
 * Pure C, no sockets: the platform receives on 224.0.0.251 port 5353 and
 * sends what this returns.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WA_MDNS_H
#define RCBENCH_WA_MDNS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WA_MDNS_PORT   5353u
/** Seconds an answer to the group may be kept. */
#define WA_MDNS_TTL    120u
/** Bytes an answer takes at most. */
#define WA_MDNS_MAX    512u

/**
 * The answer to the query @p q of @p n bytes from port @p src_port, for
 * @p host (without ".local") at @p ip, into @p out of @p cap bytes: its
 * length, or 0 when the query is not for this name, is not a query, or is
 * malformed.
 */
size_t wa_mdns_reply(const uint8_t *q, size_t n, uint16_t src_port,
                     const char *host, const uint8_t ip[4], uint8_t *out,
                     size_t cap);

/** An announcement of @p host at @p ip, sent unasked when the address
 *  comes. */
size_t wa_mdns_announce(const char *host, const uint8_t ip[4], uint8_t *out,
                        size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_WA_MDNS_H */
