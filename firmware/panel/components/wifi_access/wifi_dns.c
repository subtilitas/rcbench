/**
 * @file
 * @brief The name servers a join gave, on the console.  See wifi_dns.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi_dns.h"

#include <stdio.h>

#include "esp_log.h"

/** @brief The console tag: the radio's lines. */
static const char *TAG = "wifi";

/** @brief This join's line is written. */
static bool s_logged;

/**
 * @brief Append one server to @p line.
 * @param line The line so far.
 * @param cap  Its size in bytes.
 * @param at   Bytes in it, terminator not counted.
 * @param d    The server.
 * @return Bytes in it after the server; @p at when @p d is no address.
 */
static size_t put(char *line, size_t cap, size_t at,
                  const esp_netif_dns_info_t *d)
{
    const char *sep = (at > 0u) ? ", " : "";
    const esp_ip6_addr_t *v6 = &d->ip.u_addr.ip6;
    int n = 0;
    if (d->ip.type == ESP_IPADDR_TYPE_V4 && d->ip.u_addr.ip4.addr != 0u) {
        n = snprintf(line + at, cap - at, "%s" IPSTR, sep,
                     IP2STR(&d->ip.u_addr.ip4));
    } else if (d->ip.type == ESP_IPADDR_TYPE_V6
               && (v6->addr[0] | v6->addr[1] | v6->addr[2] | v6->addr[3])
                      != 0u) {
        n = snprintf(line + at, cap - at, "%s" IPV6STR, sep, IPV62STR(*v6));
    }
    if (n <= 0) {
        return at;
    }
    return ((size_t)n < cap - at) ? at + (size_t)n : cap - 1u;
}

void wifi_dns_poll(esp_netif_t *netif, bool joined)
{
    if (!joined || netif == NULL) {
        s_logged = false;
        return;
    }
    if (s_logged) {
        return;
    }
    s_logged = true;
    char line[3u * 48u];
    size_t at = 0u;
    line[0] = '\0';
    for (int t = ESP_NETIF_DNS_MAIN; t < ESP_NETIF_DNS_MAX; ++t) {
        esp_netif_dns_info_t d = { 0 };
        if (esp_netif_get_dns_info(netif, (esp_netif_dns_type_t)t, &d)
            == ESP_OK) {
            at = put(line, sizeof(line), at, &d);
        }
    }
    ESP_LOGI(TAG, "dns %s", (at > 0u) ? line : "none");
}
