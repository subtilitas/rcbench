/**
 * @file wifi_radio.h
 * @brief The radio under esp_wifi, as wifi_access.c sees it.
 *
 * The ESP32-P4 has no radio: the 7-inch display board from Waveshare
 * carries an ESP32-C6 on SDIO (Secure Digital input/output) slot 1, and the
 * esp_wifi calls reach it through the managed components esp_wifi_remote
 * and esp_hosted.  This file brings that link up, reports the C6's firmware
 * version, and turns a failure of the link into a failed start instead of
 * a restart of the panel.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WIFI_RADIO_H
#define RCBENCH_WIFI_RADIO_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @brief 6 bytes that differ from board to board, for the network name.
 *
 * The ESP32-P4's base MAC (media access control) address from eFuse, read
 * for wa_name().
 *
 * @param mac Receives the 6 bytes.
 */
void wifi_radio_mac(uint8_t mac[6]);

/**
 * @brief Bring the link to the C6 up, before esp_wifi_init().
 *
 * Called from the task that brings the radio up.  The C6 is reset once per
 * start of the panel and answers within
 * CONFIG_ESP_HOSTED_SDIO_RESET_DELAY_MS; its firmware version is logged
 * once and the handler of a link failure is registered once.
 *
 * @return ESP_OK with the link up; ESP_FAIL when the C6 does not answer,
 *         and on every call once the link has failed, until the panel
 *         restarts.
 */
esp_err_t wifi_radio_link(void);

/**
 * @brief Whether the link has failed since the panel started.
 *
 * @return true after esp_hosted reported a failed SDIO link; callable from
 *         any task.
 */
bool wifi_radio_failed(void);

#endif /* RCBENCH_WIFI_RADIO_H */
