/**
 * @file devota.c
 * @brief A device's end of the firmware update over Bluetooth LE
 *        (devota.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "devota.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "psa/crypto.h"

/** @brief The log's tag. */
static const char *TAG = "ota";

/** @brief No connection. */
#define CONN_NONE 0xFFFFu
/** @brief One write to flash at most, bytes. */
#define WRITE_MAX 1024u

/** @brief The service's UUIDs. @{ */
static const ble_uuid128_t s_uuid_svc =
    BLE_UUID128_INIT(BLEOTA_UUID_LE(BLEOTA_UUID_SVC));
static const ble_uuid128_t s_uuid_ctrl =
    BLE_UUID128_INIT(BLEOTA_UUID_LE(BLEOTA_UUID_CTRL));
static const ble_uuid128_t s_uuid_data =
    BLE_UUID128_INIT(BLEOTA_UUID_LE(BLEOTA_UUID_DATA));
/** @} */

/** @brief The value handles, written by NimBLE at registration. @{ */
static uint16_t s_h_ctrl;
static uint16_t s_h_data; /**< DATA */
/** @} */

/** @brief The device side, under s_lock. */
static bleota_dev_t s_dev;
/** @brief Its buffer of taken bytes. */
static uint8_t s_ring[BLEOTA_WINDOW];
/** @brief Guards s_dev between the host task and `ota`. */
static SemaphoreHandle_t s_lock;
/** @brief The confirming key's reader. */
static devota_key_fn s_key;
/** @brief The connection and CTRL's subscription. @{ */
static atomic_uint_least16_t s_conn = CONN_NONE;
static atomic_bool s_sub; /**< notifications on */
/** @} */
/** @brief The view, copied out under s_lock. */
static devota_view_t s_view;
/** @brief `ota`'s own: the slot being written and its handle. @{ */
static const esp_partition_t *s_part;
static esp_ota_handle_t s_handle; /**< its handle */
/** @} */
/** @brief The SHA-256 over what was written, `ota`'s own. */
static psa_hash_operation_t s_hash;
/** @brief Bytes taken from the buffer, `ota`'s own. */
static uint8_t s_buf[WRITE_MAX];

/** @brief The ms clock, low 32 bits. @return ms since the start */
static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/**
 * @brief GATT access to CTRL and DATA, in the host task.
 * @param conn the connection
 * @param attr the attribute
 * @param ctxt the operation
 * @param arg  unused
 * @return 0 or an ATT error
 */
static int access_cb(uint16_t conn, uint16_t attr,
                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)arg;
    uint8_t b[BLEOTA_CHUNK_HDR + BLEOTA_CHUNK_MAX];
    uint16_t n = 0;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    const uint16_t max = attr == s_h_ctrl ? BLEOTA_CTRL_MAX : sizeof b;
    if (OS_MBUF_PKTLEN(ctxt->om) > max
        || ble_hs_mbuf_to_flat(ctxt->om, b, sizeof b, &n) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    atomic_store(&s_conn, conn);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (attr == s_h_ctrl) {
        bleota_dev_ctrl(&s_dev, b, n, now_ms());
    } else if (attr == s_h_data) {
        bleota_dev_data(&s_dev, b, n, now_ms());
    }
    xSemaphoreGive(s_lock);
    return 0;
}

/** @brief The service. */
static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_uuid_svc.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            { .uuid = &s_uuid_ctrl.u, .access_cb = access_cb,
              .val_handle = &s_h_ctrl,
              .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY },
            { .uuid = &s_uuid_data.u, .access_cb = access_cb,
              .val_handle = &s_h_data,
              .flags = BLE_GATT_CHR_F_WRITE_NO_RSP },
            { 0 },
        },
    },
    { 0 },
};

/** @brief The notifications that wait, to the central when subscribed. */
static void send_status(void)
{
    uint8_t b[BLEOTA_LEN_STATUS];
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        const bool have = bleota_dev_status(&s_dev, b);
        xSemaphoreGive(s_lock);
        if (!have) {
            return;
        }
        const uint16_t conn = (uint16_t)atomic_load(&s_conn);
        if (conn == CONN_NONE || !atomic_load(&s_sub)) {
            continue;
        }
        struct os_mbuf *om = ble_hs_mbuf_from_flat(b, sizeof b);
        if (om == NULL || ble_gatts_notify_custom(conn, s_h_ctrl, om) != 0) {
            ESP_LOGW(TAG, "notification 0x%02X not sent", b[0]);
        }
    }
}

/**
 * @brief Open the slot that is not running, erasing as the writes come.
 * @return true when it opened
 */
static bool open_slot(void)
{
    s_part = esp_ota_get_next_update_partition(NULL);
    if (s_part == NULL) {
        return false;
    }
    esp_err_t e = esp_ota_begin(s_part, OTA_WITH_SEQUENTIAL_WRITES, &s_handle);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "the slot did not open: %s", esp_err_to_name(e));
        return false;
    }
    s_hash = psa_hash_operation_init();
    if (psa_hash_setup(&s_hash, PSA_ALG_SHA_256) != PSA_SUCCESS) {
        (void)esp_ota_abort(s_handle);
        return false;
    }
    ESP_LOGI(TAG, "update confirmed: writing %s at 0x%lx", s_part->label,
             (unsigned long)s_part->address);
    return true;
}

/** @brief Write what the buffer holds, a WRITE_MAX at a time. */
static void write_some(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const uint32_t n = bleota_dev_take(&s_dev, s_buf, sizeof s_buf);
    xSemaphoreGive(s_lock);
    const bool ok = n == 0u
                    || (esp_ota_write(s_handle, s_buf, n) == ESP_OK
                        && psa_hash_update(&s_hash, s_buf, n) == PSA_SUCCESS);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bleota_dev_written(&s_dev, ok, now_ms());
    xSemaphoreGive(s_lock);
}

/** @brief The compare, then the image's own check and the boot slot. */
static void verify_and_install(void)
{
    uint8_t digest[32];
    size_t len = 0;
    const bool hashed = psa_hash_finish(&s_hash, digest, sizeof digest, &len)
                        == PSA_SUCCESS && len == sizeof digest;
    if (!hashed) {
        memset(digest, 0, sizeof digest);
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool same = bleota_dev_verify(&s_dev, digest, now_ms());
    const uint8_t why = s_dev.code;
    xSemaphoreGive(s_lock);
    if (!same) {
        /* A mismatch or an end while the SHA-256 finished: the device
         * left a CLOSE due either way. */
        if (why == BLEOTA_E_SHA) {
            ESP_LOGW(TAG, "the SHA-256 of what was written differs from "
                          "BEGIN's");
        }
        return;
    }
    uint8_t code = BLEOTA_OK;
    esp_err_t e = esp_ota_end(s_handle);
    if (e != ESP_OK) {
        code = BLEOTA_E_IMAGE;
        ESP_LOGE(TAG, "the image's own check failed: %s", esp_err_to_name(e));
    } else if ((e = esp_ota_set_boot_partition(s_part)) != ESP_OK) {
        code = BLEOTA_E_FLASH;
        ESP_LOGE(TAG, "the boot slot did not change: %s", esp_err_to_name(e));
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bleota_dev_installed(&s_dev, code == BLEOTA_OK, code, now_ms());
    xSemaphoreGive(s_lock);
    if (code == BLEOTA_OK) {
        ESP_LOGI(TAG, "installed in %s; restarting", s_part->label);
    }
}

/**
 * @brief The device side's next step, outside the lock.  The host task may
 *        end the transfer while a step runs; the step's report then finds
 *        the transfer over, and bleota_dev_poll() hands out the one CLOSE
 *        that releases the slot and the SHA-256, if anything was opened.
 * @param act the step
 * @return true when another step may follow at once
 */
static bool run_step(bleota_do_t act)
{
    switch (act) {
    case BLEOTA_DO_OPEN: {
        const bool ok = open_slot();
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bleota_dev_opened(&s_dev, ok, now_ms());
        xSemaphoreGive(s_lock);
        return true;
    }
    case BLEOTA_DO_WRITE:
        write_some();
        return true;
    case BLEOTA_DO_VERIFY:
        verify_and_install();
        return true;
    case BLEOTA_DO_CLOSE:
        (void)esp_ota_abort(s_handle);
        psa_hash_abort(&s_hash);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bleota_dev_closed(&s_dev);
        const unsigned code = s_dev.code;
        xSemaphoreGive(s_lock);
        ESP_LOGW(TAG, "update ended, code %u; the running image stays", code);
        return true;
    case BLEOTA_DO_RESTART:
        send_status();
        vTaskDelay(pdMS_TO_TICKS(DEVOTA_RESTART_MS));
        esp_restart();
        return false;
    case BLEOTA_DO_INSTALL:
    case BLEOTA_DO_NONE:
    default:
        return false;
    }
}

/** @brief Copy the view out of the device side, under the lock. */
static void take_view(void)
{
    const uint32_t now = now_ms();
    s_view.st = s_dev.st;
    s_view.code = s_dev.code;
    s_view.secs = bleota_dev_secs(&s_dev, now);
    s_view.percent = bleota_dev_percent(&s_dev);
    memcpy(s_view.from, s_dev.cfg.ver, sizeof s_view.from);
    memcpy(s_view.to, s_dev.ver, sizeof s_view.to);
}

/**
 * @brief The `ota` task.
 * @param arg unused
 */
static void ota_task(void *arg)
{
    (void)arg;
    bool raw = false, key = false;
    uint8_t last_st = BLEOTA_DEV_IDLE;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(DEVOTA_POLL_MS));
        const bool now_raw = s_key != NULL && s_key();
        if (now_raw == raw) {
            key = raw;          /* the same level twice: taken */
        }
        raw = now_raw;
        bleota_do_t act;
        int steps = 0;
        do {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            bleota_dev_key(&s_dev, key, now_ms());
            act = bleota_dev_poll(&s_dev, now_ms());
            if (s_dev.st != last_st) {
                if (last_st != BLEOTA_DEV_IDLE
                    && s_dev.st == BLEOTA_DEV_IDLE) {
                    ++s_view.ends;
                }
                last_st = s_dev.st;
            }
            take_view();
            xSemaphoreGive(s_lock);
            send_status();
        } while (run_step(act) && ++steps < 8);
    }
}

bool devota_start(uint8_t kind, uint8_t confirm, devota_key_fn key)
{
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    const esp_app_desc_t *d = esp_app_get_description();
    unsigned v[3] = { 0u, 0u, 0u };
    (void)sscanf(d->version, "%u.%u.%u", &v[0], &v[1], &v[2]);
    const bleota_dev_cfg_t cfg = {
        .kind = kind, .confirm = confirm,
        .slot = next != NULL ? next->size : 0u,
        .ver = { (uint16_t)v[0], (uint16_t)v[1], (uint16_t)v[2] },
        .chip = (uint16_t)CONFIG_IDF_FIRMWARE_CHIP_ID,
    };
    if (devota_role.magic != BLEOTA_ROLE_MAGIC || devota_role.kind != kind) {
        ESP_LOGE(TAG, "this image's role is kind %u, not %u: updates over "
                      "Bluetooth LE are off", (unsigned)devota_role.kind,
                 (unsigned)kind);
        return false;
    }
    s_lock = xSemaphoreCreateMutex();
    s_key = key;
    if (next == NULL || s_lock == NULL || psa_crypto_init() != PSA_SUCCESS
        || !bleota_dev_init(&s_dev, &cfg, s_ring, sizeof s_ring)) {
        ESP_LOGE(TAG, "no update slot: updates over Bluetooth LE are off");
        return false;
    }
    if (ble_gatts_count_cfg(s_svcs) != 0 || ble_gatts_add_svcs(s_svcs) != 0) {
        ESP_LOGE(TAG, "the update service was refused");
        return false;
    }
    if (xTaskCreate(ota_task, "ota", DEVOTA_STACK, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "the update task did not start");
        return false;
    }
    ESP_LOGI(TAG, "updates over Bluetooth LE: kind %u, slot %s of %lu bytes",
             (unsigned)kind, next->label, (unsigned long)next->size);
    return true;
}

void devota_connected(uint16_t conn)
{
    atomic_store(&s_conn, conn);
    esp_ota_img_states_t st;
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (run != NULL && esp_ota_get_state_partition(run, &st) == ESP_OK
        && st == ESP_OTA_IMG_PENDING_VERIFY) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            ESP_LOGI(TAG, "first connection of this image: kept");
        }
    }
}

void devota_disconnected(void)
{
    atomic_store(&s_conn, CONN_NONE);
    atomic_store(&s_sub, false);
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bleota_dev_lost(&s_dev);
    xSemaphoreGive(s_lock);
}

void devota_subscribe(uint16_t attr, bool notify)
{
    if (attr == s_h_ctrl && s_h_ctrl != 0u) {
        atomic_store(&s_sub, notify);
    }
}

void devota_view(devota_view_t *out)
{
    if (s_lock == NULL) {
        memset(out, 0, sizeof *out);
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_view;
    xSemaphoreGive(s_lock);
}

bool devota_active(void)
{
    devota_view_t v;
    devota_view(&v);
    return v.st != BLEOTA_DEV_IDLE;
}
