/*
 * The radio under esp_wifi, as wifi_access.c sees it.
 *
 * On a chip with its own radio (ESP32-S3) there is nothing to bring up
 * before esp_wifi_init().  The ESP32-P4 has no radio: the 7-inch display
 * board from Waveshare carries an ESP32-C6 on SDIO (Secure Digital
 * input/output) slot 1, and the esp_wifi calls reach it through the
 * managed components esp_wifi_remote and esp_hosted.  This file brings that
 * link up, reports the C6's firmware version, and turns a failure of the
 * link into a failed start instead of a restart of the panel.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WIFI_RADIO_H
#define RCBENCH_WIFI_RADIO_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** 6 bytes that differ from board to board, for the network name
 *  (wa_name()).  The ESP32-S3: its access point's MAC (media access
 *  control) address.  The ESP32-P4: its base MAC address from eFuse. */
void wifi_radio_mac(uint8_t mac[6]);

/**
 * Before esp_wifi_init(), from the task that brings the radio up.  The
 * ESP32-S3: ESP_OK.  The ESP32-P4: the link to the C6 up (the C6 is reset
 * once per start of the panel and answers within
 * CONFIG_ESP_HOSTED_SDIO_RESET_DELAY_MS), its firmware version logged once,
 * the handler of a link failure registered once.  ESP_FAIL once the link
 * has failed: from then on until the panel restarts, every start fails.
 */
esp_err_t wifi_radio_link(void);

/** Whether the link has failed since the panel started; from any task. */
bool wifi_radio_failed(void);

#endif /* RCBENCH_WIFI_RADIO_H */
