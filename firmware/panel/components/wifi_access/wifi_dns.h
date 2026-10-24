/**
 * @file
 * @brief The name servers a join of the home network gave, on the console.
 *
 * One line a join, tag wifi, for example `wifi: dns 192.0.2.53, 192.0.2.1`:
 * the DNS (Domain Name System) servers lwIP asks, in the order it asks
 * them.  lwIP tries one server at 0s, 1s, 2s and 4s and moves to the next
 * at 7s (DNS_MAX_RETRIES 4, DNS_TMR_INTERVAL 1000ms; ESP-IDF v5.5.5 offers
 * neither in its configuration), so a first server that does not answer
 * costs each name lookup about 7s.  The line goes to the console only: the
 * addresses are the home network's.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WIFI_DNS_H
#define RCBENCH_WIFI_DNS_H

#include <stdbool.h>

#include "esp_netif.h"

/**
 * @brief Log the name servers once a join: on the first call with
 *        @p joined after one without.
 *
 * The Wi-Fi worker task's, while @p netif exists.
 *
 * @param netif  The station's interface; not read unless @p joined.
 * @param joined The station holds an address on the home network.
 */
void wifi_dns_poll(esp_netif_t *netif, bool joined);

#endif /* RCBENCH_WIFI_DNS_H */
