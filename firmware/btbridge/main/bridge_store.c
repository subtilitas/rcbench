/**
 * @file bridge_store.c
 * @brief The settings record in NVS, namespace "btbridge", key "cfg"
 *        (bridge_store.h).  A blob of another size is no record.
 *
 * SPDX-License-Identifier: MIT
 */
#include "bridge_store.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

/** @brief The console lines' tag. */
static const char *TAG = "store";
/** @brief The NVS namespace. */
#define STORE_NS  "btbridge"
/** @brief The record's key in it. */
#define STORE_KEY "cfg"

bool bridge_store_load(hc05emu_cfg_t *out)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES
        || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS of another layout: erased");
        (void)nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS: %s", esp_err_to_name(err));
        return false;
    }
    nvs_handle_t h;
    if (nvs_open(STORE_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t n = sizeof(*out);
    err = nvs_get_blob(h, STORE_KEY, out, &n);
    nvs_close(h);
    return err == ESP_OK && n == sizeof(*out);
}

bool bridge_store_save(const hc05emu_cfg_t *c)
{
    nvs_handle_t h;
    if (nvs_open(STORE_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "NVS not opened: setting not stored");
        return false;
    }
    const bool ok = nvs_set_blob(h, STORE_KEY, c, sizeof(*c)) == ESP_OK
                    && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (!ok) {
        ESP_LOGE(TAG, "setting not stored");
    }
    return ok;
}
