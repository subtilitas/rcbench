/**
 * @file c6_link.h
 * @brief The SDIO (Secure Digital input/output) link to the board's
 *        ESP32-C6, shared by Wi-Fi and Bluetooth LE.
 *
 * The ESP32-P4 has no radio: the 7-inch display board from Waveshare
 * carries an ESP32-C6 on SDIO slot 1, and esp_hosted carries the esp_wifi
 * calls (through esp_wifi_remote) and the HCI (host controller interface)
 * packets of Bluetooth LE to it.  This component brings that link up once,
 * reports the C6's firmware version, and turns a failure of the link into a
 * failed start instead of a restart of the panel.  Every caller goes
 * through one mutex, so the first start's reset of the C6 happens once,
 * whichever task asks first.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_C6_LINK_H
#define RCBENCH_C6_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The C6's esp_hosted firmware version, as the C6 reports it. */
typedef struct {
    uint8_t major; /**< major number */
    uint8_t minor; /**< minor number */
    uint8_t patch; /**< patch number */
} c6_link_version_t;

/**
 * @brief Bring the link to the C6 up.
 *
 * Wi-Fi calls it before esp_wifi_init(), Bluetooth LE before the host
 * stack starts.  The calls take one mutex: a second caller waits until the
 * first call has ended, then gets its result at once.  The first call
 * creates the default event loop when nobody has, resets the C6 (EN low
 * for 10ms, then a fixed 1,500ms, CONFIG_ESP_HOSTED_SDIO_RESET_DELAY_MS),
 * starts it as an SDIO card and waits for its start event, polling every
 * 200ms.  A C6 that starts as a card and sends no start event is reset
 * again after 50 and after 100 polls; the call returns after about 25s.  A
 * C6 that does not start as a card ends the call after the card's
 * start-up, at least 1,500ms.  Then esp_hosted's TRANSPORT_UP event is
 * awaited for at most 1,000ms.  The C6's firmware version is asked for and
 * logged once, at the first call with the link up; a C6 that does not
 * answer the request costs 1s more, once.  Later calls with the link up
 * return without a reset.
 *
 * @return ESP_OK with the link up; ESP_FAIL when the C6 did not answer,
 *         when the default event loop or the handlers of esp_hosted's
 *         events cannot be set up, and on every call once the link has
 *         failed, until the panel restarts.  A C6 that did not answer is
 *         held in reset.
 */
esp_err_t c6_link_up(void);

/**
 * @brief Whether the link has failed since the panel started.
 *
 * @return true after the C6 did not answer or esp_hosted reported a failed
 *         SDIO link; callable from any task, takes no lock.
 */
bool c6_link_failed(void);

/**
 * @brief The C6's firmware version, once c6_link_up() has asked for it.
 *
 * @param v Receives the version; untouched when the result is false.
 * @return true when the C6 answered the version request; false before the
 *         first c6_link_up() with the link up, and when the C6's firmware
 *         did not answer it.  Callable from any task.
 */
bool c6_link_version(c6_link_version_t *v);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_C6_LINK_H */
