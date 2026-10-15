/*
 * SPDX-License-Identifier: MIT
 */

#include "touch_inject.h"

#include "esp_log.h"
#include "gt911.h"
#include "touch_merge.h"

static const char *TAG = "touch_inject";

/* The probe's own timeout, in ms: an address-only write. */
#define PROBE_TIMEOUT_MS 5

/* Written by touch_init() before the touch task exists, then by that task
 * alone; s_ok_ms is read from other tasks as one aligned 32-bit load. */
static gt911_handle_t    s_dev;
static bool              s_attached;
static touch_merge_t     s_merge;
static volatile uint32_t s_ok_ms;

void touch_inject_attach(i2c_master_bus_handle_t bus, uint16_t glass_addr,
                         uint32_t now_ms)
{
    touch_merge_reset(&s_merge);
    s_ok_ms = now_ms;
    if (glass_addr == TOUCH_INJECT_ADDR) {
        /* No controller at 0x5D, and the injector took the glass's place. */
        s_attached = true;
        ESP_LOGW(TAG, "no GT911 at 0x5d; the injector is the only touch");
        return;
    }
    if (i2c_master_probe(bus, TOUCH_INJECT_ADDR, PROBE_TIMEOUT_MS) != ESP_OK) {
        ESP_LOGI(TAG, "no injector at 0x%02x", TOUCH_INJECT_ADDR);
        return;
    }
    const gt911_config_t cfg = { .i2c_addr = TOUCH_INJECT_ADDR };
    if (gt911_new(bus, &cfg, &s_dev) != ESP_OK) {
        s_dev = NULL;
        ESP_LOGW(TAG, "0x%02x answers, but not as a GT911", TOUCH_INJECT_ADDR);
        return;
    }
    s_attached = true;
    ESP_LOGW(TAG, "touch injector attached at 0x%02x", TOUCH_INJECT_ADDR);
}

bool touch_injector_attached(void)
{
    return s_attached;
}

bool touch_inject_glass(touch_point_t *pts, int *count, bool fresh)
{
    if (s_dev == NULL || !fresh) {
        return fresh;
    }
    touch_merge_glass(&s_merge, pts, *count);
    *count = touch_merge_frame(&s_merge, pts, TOUCH_MAX_POINTS);
    return true;
}

bool touch_inject_read(touch_point_t *pts, int *count, uint32_t now_ms)
{
    if (s_dev == NULL || !touch_merge_inject_due(&s_merge, now_ms)) {
        return false;
    }
    touch_point_t got[TOUCH_MAX_POINTS];
    int n = 0;
    bool fresh = false;
    bool changed;
    if (gt911_read(s_dev, got, TOUCH_MAX_POINTS, &n, &fresh) == ESP_OK) {
        s_ok_ms = now_ms;
        changed = touch_merge_inject(&s_merge, got, n, fresh, now_ms);
    } else {
        changed = touch_merge_inject_failed(&s_merge, now_ms);
    }
    if (changed) {
        *count = touch_merge_frame(&s_merge, pts, TOUCH_MAX_POINTS);
    }
    return changed;
}

uint32_t touch_inject_age_ms(uint32_t now_ms)
{
    return (s_dev != NULL) ? (uint32_t)(now_ms - s_ok_ms) : 0u;
}
