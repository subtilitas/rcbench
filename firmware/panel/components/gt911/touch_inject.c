/**
 * @file
 * @brief The touch injector at 0x14, read beside the glass (touch_inject.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "touch_inject.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "gt911.h"
#include "touch_merge.h"

/** @brief The log tag. */
static const char *TAG = "touch_inject";

/** @brief The probe's own timeout, in ms: an address-only write. */
#define PROBE_TIMEOUT_MS 5

/*
 * Written by touch_init() before the touch task exists, then by that task
 * alone.  s_ok_ms and s_attached are copies of the merge's state for
 * touch_age_ms() and the status band, which run in other tasks: each is one
 * aligned load, and s_ok_ms is stored before s_attached is set.  A reader
 * that sees s_attached with an s_ok_ms from before a detach gets an age of
 * at least TOUCH_MERGE_DETACH_MS, which touch_merge_inject_age() counts as
 * 0, so the order of the two loads cannot age touch.
 */
/**
 * @brief The injector's map: its contacts are panel pixels of the UI's
 *        BOARD_UI_H_RES x BOARD_UI_V_RES area, unmirrored and unrotated
 *        (testbench/touch-injector/README.md).
 */
static const touch_map_t k_inject_map = {
    .src_w = BOARD_UI_H_RES,
    .src_h = BOARD_UI_V_RES,
    .mirror_x = false,
    .mirror_y = false,
    .rotation = TOUCH_ROTATION_0,
};

static gt911_handle_t    s_dev;       /**< @brief the injector's handle   */
static bool              s_is_glass;  /**< @brief alone, in the glass's place */
static touch_merge_t     s_merge;     /**< @brief the two sources merged   */
static volatile uint32_t s_ok_ms;     /**< @brief its last good read       */
static volatile bool     s_attached;  /**< @brief attached, for the band   */

void touch_inject_attach(i2c_master_bus_handle_t bus, uint16_t glass_addr,
                         touch_role_t glass_role, uint32_t now_ms)
{
    /* Absent until the probe below says otherwise, and absent for good
     * when it does: nothing after start-up looks for the injector. */
    touch_merge_start(&s_merge, false, now_ms);
    s_ok_ms = now_ms;
    if (glass_role == TOUCH_ROLE_INJECTOR) {
        /* No controller at 0x5D, and the injector's product ID at 0x14;
         * touch_init() says so on the console. */
        s_is_glass = true;
        return;
    }
    if (glass_role != TOUCH_ROLE_GLASS) {
        return;
    }
    if (glass_addr == TOUCH_INJECT_ADDR) {
        /* The glass latched 0x14: no injector can answer there beside it. */
        ESP_LOGW(TAG, "the glass answers at 0x%02x: no injector looked for",
                 TOUCH_INJECT_ADDR);
        return;
    }
    if (i2c_master_probe(bus, TOUCH_INJECT_ADDR, PROBE_TIMEOUT_MS) != ESP_OK) {
        ESP_LOGI(TAG, "no injector at 0x%02x", TOUCH_INJECT_ADDR);
        return;
    }
    /* Beside the glass at 0x5D, a GT911 at 0x14 is the injector only with
     * the injector's product ID. */
    const gt911_config_t cfg = { .i2c_addr = TOUCH_INJECT_ADDR };
    if (gt911_new(bus, &cfg, &s_dev) != ESP_OK) {
        s_dev = NULL;
        ESP_LOGW(TAG, "0x%02x answers, but not as a GT911", TOUCH_INJECT_ADDR);
        return;
    }
    char id[5] = { 0 };
    if (gt911_info(s_dev, id, NULL) != ESP_OK
        || touch_merge_role(TOUCH_INJECT_ADDR, true, id)
               != TOUCH_ROLE_INJECTOR) {
        ESP_LOGW(TAG, "0x%02x answers as a GT911 with product ID \"%s\", "
                      "not the touch injector's %s: not read",
                 TOUCH_INJECT_ADDR, id, TOUCH_MERGE_INJECT_PRODUCT);
        (void)gt911_del(s_dev);
        s_dev = NULL;
        return;
    }
    touch_merge_start(&s_merge, true, now_ms);
    s_attached = true;
    ESP_LOGW(TAG, "touch injector attached at 0x%02x", TOUCH_INJECT_ADDR);
}

touch_map_t touch_inject_map(void)
{
    return k_inject_map;
}

bool touch_injector_attached(void)
{
    if (s_is_glass) {
        return true;
    }
    if (!s_attached) {
        return false;
    }
    /* touch_age_ms()'s rule: an injector silent this long counts as
     * detached before the touch task has detached it.  s_ok_ms is loaded
     * before the clock, so a good read in between cannot put it ahead of
     * now and wrap the age. */
    const uint32_t ok_ms = s_ok_ms;
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    return (uint32_t)(now - ok_ms) < TOUCH_MERGE_DETACH_MS;
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

/**
 * @brief A detached injector, asked once a second whether it answers as
 *        the injector again: its product ID, TOUCH_MERGE_INJECT_PRODUCT,
 *        read on the handle made at start.
 * @param now_ms The clock.
 */
static void probe(uint32_t now_ms)
{
    if (!touch_merge_probe_due(&s_merge, now_ms)) {
        return;
    }
    char id[5] = { 0 };
    const bool answered = gt911_info(s_dev, id, NULL) == ESP_OK
                          && touch_merge_is_injector(id);
    if (touch_merge_probe(&s_merge, answered, now_ms)) {
        s_ok_ms = now_ms;
        s_attached = true;
        ESP_LOGW(TAG, "touch injector at 0x%02x answers again; re-attached",
                 TOUCH_INJECT_ADDR);
    }
}

bool touch_inject_read(touch_point_t *pts, int *count, uint32_t now_ms)
{
    if (s_dev == NULL) {
        return false;
    }
    bool changed = false;
    /*
     * Silent for TOUCH_MERGE_DETACH_MS (1000ms): its contacts go, and the
     * glass alone decides touch age, so the heartbeat comes back.  Touch
     * read as dead 300ms before this at the latest (200ms to lose touch in
     * main.c, then ARMING_TOUCH_DEAD_MS), which counted a stop and disarmed
     * (shared/safety/arming.c:259-274).  The IOmcu latched its arm
     * when the heartbeat stopped (shared/safety/safety_gate.c:43-46), and
     * only a new ARM written by the panel releases it; nothing here writes
     * one.
     */
    if (touch_merge_expire(&s_merge, now_ms)) {
        s_attached = false;
        changed = true;
        ESP_LOGW(TAG, "touch injector at 0x%02x silent for %u ms; detached",
                 TOUCH_INJECT_ADDR, (unsigned)TOUCH_MERGE_DETACH_MS);
    }
    if (!touch_merge_attached(&s_merge)) {
        probe(now_ms);
    } else if (touch_merge_inject_due(&s_merge, now_ms)) {
        touch_point_t got[TOUCH_MAX_POINTS];
        int n = 0;
        bool fresh = false;
        if (gt911_read(s_dev, got, TOUCH_MAX_POINTS, &n, &fresh) == ESP_OK) {
            s_ok_ms = now_ms;
            if (fresh) {
                touch_map_points(&k_inject_map, got, n);
            }
            changed = touch_merge_inject(&s_merge, got, n, fresh, now_ms);
        } else {
            changed = touch_merge_inject_failed(&s_merge, now_ms);
        }
    }
    if (changed) {
        *count = touch_merge_frame(&s_merge, pts, TOUCH_MAX_POINTS);
    }
    return changed;
}

void touch_inject_mark(touch_event_t *evts, int n)
{
    if (s_dev != NULL) {
        (void)touch_merge_mark_cut(&s_merge, evts, n);
    }
}

uint32_t touch_inject_age_ms(uint32_t now_ms)
{
    if (s_dev == NULL) {
        return 0u;
    }
    const bool attached = s_attached;
    const uint32_t ok_ms = s_ok_ms;
    return touch_merge_inject_age(attached, ok_ms, now_ms);
}
