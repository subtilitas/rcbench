/**
 * @file devota.h
 * @brief A device's end of the firmware update over Bluetooth LE: bleota's
 *        GATT service on NimBLE, the device side of protocols/bleota run on
 *        a task of its own (`ota`), the slot that is not running written
 *        through ESP-IDF's OTA (over-the-air) calls, and the rollback mark.
 *        Shared by the M5Stack Dial (firmware/dial) and the sensor nodes
 *        (firmware/node).
 *
 * The `ota` task polls every DEVOTA_POLL_MS: the confirming key (the
 * caller's devota_key_fn, debounced over 2 polls), the device side's next
 * step (open the slot, write what was taken, finish the SHA-256, install,
 * restart), and the notifications, which go to the central while it is
 * subscribed to CTRL.  The slot opens only after the confirmation, with
 * esp_ota_begin()'s sequential erase, so nothing is erased before it.
 * END compares the SHA-256 of every byte written with BEGIN's; then
 * esp_ota_end() checks the image itself (chip, segments, its appended
 * SHA-256 read back from flash) and esp_ota_set_boot_partition() makes it
 * the next start.
 *
 * The rollback: the bootloader starts a new image once as PENDING_VERIFY
 * (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE).  devota_connected() marks it
 * valid at its first connection, which the image takes only with its
 * Bluetooth LE stack running.  A start before that, other than a wake from
 * deep sleep with CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP, goes back
 * to the image before it.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bleota.h"
#include "host/ble_gatt.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The `ota` task's period, ms. */
#define DEVOTA_POLL_MS 10u
/** @brief The `ota` task's stack, bytes: esp_ota_end() reads the image back
 *         and checks it on this stack. */
#define DEVOTA_STACK 4096u
/** @brief From DONE's notification to the restart, ms: time for it to go
 *         out. */
#define DEVOTA_RESTART_MS 500u

/**
 * @brief Whether the confirming key is down; called from the `ota` task.
 * @return true while it is pressed
 */
typedef bool (*devota_key_fn)(void);

/** @brief What the device's face or console shows of an update. */
typedef struct {
    uint8_t  st;      /**< bleota_dev_state_t */
    uint8_t  code;    /**< the last FAIL's BLEOTA_E_*, BLEOTA_OK before */
    uint16_t secs;    /**< BLEOTA_DEV_CONFIRM: seconds left */
    unsigned percent; /**< the share taken, 0 to 100 */
    uint16_t from[3]; /**< the version running */
    uint16_t to[3];   /**< the version BEGIN names */
    uint32_t ends;    /**< transfers ended without an install, counted */
} devota_view_t;

/**
 * @brief Set up the device side for this image's kind and its slot, and
 *        add the service to NimBLE's GATT table; after the device's own
 *        services, before the host task starts.
 * @param kind    BLEOTA_KIND_* of this image
 * @param confirm BLEOTA_CONFIRM_*
 * @param key     the confirming key's reader
 * @return false when the slot, the task or the service is not there; the
 *         device then takes no update
 */
bool devota_start(uint8_t kind, uint8_t confirm, devota_key_fn key);

/**
 * @brief A central connected: the running image is marked valid when the
 *        bootloader started it on trial.  From the GAP connect event.
 * @param conn the connection
 */
void devota_connected(uint16_t conn);

/**
 * @brief The central left: an open slot is dropped.  From the GAP
 *        disconnect event.
 */
void devota_disconnected(void);

/**
 * @brief A subscription changed.  From the GAP subscribe event.
 * @param attr   the attribute
 * @param notify notifications on
 */
void devota_subscribe(uint16_t attr, bool notify);

/**
 * @brief What an update shows now.
 * @param out the view
 */
void devota_view(devota_view_t *out);

/**
 * @brief Whether a transfer stands, from BEGIN to its end: the device's
 *        own link rules (the Dial's drop after 500ms without a panel
 *        packet) wait.
 * @return true while one does
 */
bool devota_active(void);

#ifdef __cplusplus
}
#endif
