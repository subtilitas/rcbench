/**
 * @file
 * @brief The clock set from the home network: one SNTP (Simple Network
 *        Time Protocol) request a join, through ESP-IDF's esp_netif_sntp.
 *
 * Each join of the home network asks WALL_NTP_SERVER once, in both station
 * modes: the boot release check (WA_MODE_FETCH) and WI-FI's home network
 * (WA_MODE_JOIN).  The access point has no route out and asks nothing.
 * The request ends with its answer, after WALL_NTP_TIMEOUT_MS, or with the
 * radio, whichever comes first.  A session that ends while the request
 * asks and lingers (wa_lingers(): the release check's own end, the
 * operator's OFF, the idle time-out) holds the radio up until the answer
 * or the time-out (wall_ntp_holds()); every other end takes the radio down
 * at once and ends the request unanswered.  An answer
 * sets the system time (lwIP's SNTP calls settimeofday()) and logs one
 * console line: the server, the time set and the offset from the old
 * clock (wall_ntp_line()).
 *
 * Both calls are the Wi-Fi worker task's.  The SNTP client runs in lwIP's
 * TCP/IP (transmission control protocol / internet protocol) task; neither
 * the control task nor the heartbeat waits for any of it.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WIFI_NTP_H
#define RCBENCH_WIFI_NTP_H

#include <stdbool.h>

/**
 * @brief Move the request on: start it on a join, log and end it on an
 *        answer or the time-out, end it when the join ends.
 * @param joined  The session holds an address on the home network.
 * @param lingers The session ended joined, its radio still up, and lets
 *                the radio stay up (wa_lingers()).
 * @return Whether the radio stays up for the request (wall_ntp_holds()).
 */
bool wifi_ntp_poll(bool joined, bool lingers);

/**
 * @brief End the request before the network interface goes: what
 *        wifi_ntp_poll(false, false) does, an answer that came first
 *        logged.
 */
void wifi_ntp_stop(void);

#endif /* RCBENCH_WIFI_NTP_H */
