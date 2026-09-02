/*
 * NVS (non-volatile storage) backing for the settings model.
 *
 * Values are stored one key at a time rather than as a blob, so a schema that
 * gains or loses an entry does not invalidate the rest: an unknown key is
 * ignored on load and a missing one keeps its default.
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "settings";
static const char *NAMESPACE = "bench";

static bool nvs_load(float *values, int count)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "no stored settings; using defaults");
        return false;
    }
    int found = 0;
    for (int i = 0; i < count; ++i) {
        const setting_def_t *d = settings_def((setting_id_t)i);
        if (!d || !d->key) {
            continue;
        }
        int32_t raw = 0;
        /* Stored as milli-units so a float survives an integer key without
         * dragging a blob format along. */
        if (nvs_get_i32(h, d->key, &raw) == ESP_OK) {
            values[i] = (float)raw / 1000.0f;
            ++found;
        }
    }
    nvs_close(h);
    ESP_LOGI(TAG, "loaded %d/%d settings", found, count);
    return found > 0;
}

static void nvs_save(const float *values, int count)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        return;
    }
    for (int i = 0; i < count; ++i) {
        const setting_def_t *d = settings_def((setting_id_t)i);
        if (!d || !d->key) {
            continue;
        }
        (void)nvs_set_i32(h, d->key, (int32_t)(values[i] * 1000.0f));
    }
    err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "settings saved");
    }
}

static const settings_store_t s_store = { nvs_load, nvs_save };

/*
 * A store that cannot be brought up must not stop the bench from booting:
 * settings_init() runs on the schema defaults when the store is NULL, which
 * is a working bench with unsaved settings.
 */
const settings_store_t *settings_nvs_store(void)
{
    static bool inited = false;
    static bool usable = false;
    if (!inited) {
        inited = true;
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
            err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            err = nvs_flash_erase();
            if (err == ESP_OK) {
                err = nvs_flash_init();
            }
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs unavailable (%s); running on defaults",
                     esp_err_to_name(err));
        }
        usable = (err == ESP_OK);
    }
    return usable ? &s_store : NULL;
}
