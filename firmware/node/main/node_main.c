/**
 * @file node_main.c
 * @brief The node's start-up and its `node` task: the timer wake's quick
 *        check, then the role, Bluetooth and the console, then once a
 *        second the battery, the status, the console line and the sleep
 *        policy (node_policy.h), and the control writes in between.
 *
 * A timer wake from deep sleep runs the role's check before anything else
 * starts (power: one bus voltage read) and goes back to sleep unless a pack
 * is present.  Any other start (the RESET key, power on, a flash) starts
 * the node in full.
 *
 * SPDX-License-Identifier: MIT
 */
#include "node.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "node_policy.h"
#include "rcbench_version.h"

/** @brief Control writes waiting for the `node` task. */
#define CTL_QUEUE_LEN 8u
/** @brief The step's period, ms. */
#define STEP_MS RCNODE_STATUS_MS
/** @brief Time for the disconnect to reach the central before sleep, ms. */
#define STOP_MS 200u

/** @brief Timer wakes since the last full start; kept in RTC (real-time
 *  clock) memory through deep sleep. */
RTC_DATA_ATTR static uint32_t s_wakes;
/** @brief Low battery readings in a row (node_policy_t's low_reads), kept
 *  in RTC memory through deep sleep so the count runs on across wakes. */
RTC_DATA_ATTR static uint8_t s_low_reads;

static QueueHandle_t s_ctlq;           /**< rcnode_ctl_t */
static atomic_bool   s_stay;           /**< the console's hold */
static atomic_int    s_ask = NODE_RUN; /**< the console's sleep or off */
static portMUX_TYPE  s_last_mux = portMUX_INITIALIZER_UNLOCKED;  /**< guards s_last */
static rcnode_status_t s_last;         /**< the status as last published */

uint64_t node_us(void)
{
    return (uint64_t)esp_timer_get_time();
}

void node_stay(bool on)
{
    atomic_store(&s_stay, on);
}

bool node_staying(void)
{
    return atomic_load(&s_stay);
}

void node_sleep_now(void)
{
    atomic_store(&s_ask, NODE_SLEEP);
}

void node_off_now(void)
{
    atomic_store(&s_ask, NODE_OFF);
}

void node_last_status(rcnode_status_t *s)
{
    taskENTER_CRITICAL(&s_last_mux);
    *s = s_last;
    taskEXIT_CRITICAL(&s_last_mux);
}

void node_control_post(const rcnode_ctl_t *c)
{
    if (s_ctlq != NULL) {
        (void)xQueueSend(s_ctlq, c, 0);
    }
}

/**
 * @brief Deep sleep, with or without the NODE_WAKE_S timer.  Does not
 *        return.
 *
 * @param timer true for the timer; false for off and for a role without a
 *              timer wake: only the RESET key or a power cycle starts the
 *              node again.
 */
static void deep_sleep(bool timer)
{
    if (timer) {
        (void)esp_sleep_enable_timer_wakeup((uint64_t)NODE_WAKE_S * 1000000u);
    }
    esp_deep_sleep_start();
}

/**
 * @brief Leave: the link ends, the sensor to its lowest current, then deep
 *        sleep.  Does not return.
 *
 * @param act NODE_SLEEP or NODE_OFF.
 * @param low_reads The policy's low battery readings in a row, kept for the
 *        next wake.
 */
static void go_down(node_act_t act, uint8_t low_reads)
{
    const bool timer = act == NODE_SLEEP && node_role.timer_wake;
    ESP_LOGI(NODE_TAG, "%s", act == NODE_OFF
             ? "off: the RESET key starts the node again"
             : timer ? "sleep: a check every 10s, the RESET key starts the node"
                     : "sleep: the RESET key starts the node");
    node_ble_stop();
    vTaskDelay(pdMS_TO_TICKS(STOP_MS));
    node_role.sleep();
    s_wakes = 0u;
    s_low_reads = low_reads;
    deep_sleep(timer);
}

/**
 * @brief Build and publish the status.
 *
 * @param bat_mv The cell, mV.
 */
static void publish_status(uint16_t bat_mv)
{
    rcnode_status_t s;
    memset(&s, 0, sizeof s);
    s.flags = node_bat_flags(bat_mv);
    s.vbat  = (s.flags & (RCNODE_ST_BAT_NONE | RCNODE_ST_BAT_ERR)) != 0u
              ? 0u : bat_mv;
    s.fw[0] = RCBENCH_VERSION_MAJOR;
    s.fw[1] = RCBENCH_VERSION_MINOR;
    s.fw[2] = RCBENCH_VERSION_PATCH;
    if (atomic_load(&s_stay)) {
        s.flags |= RCNODE_ST_STAY;
    }
    if (node_ble_subscribed()) {
        s.flags |= RCNODE_ST_SUBSCRIBED;
    }
    s.dropped = node_ble_dropped();
    node_role.status(&s);
    node_ble_status(&s);
    taskENTER_CRITICAL(&s_last_mux);
    s_last = s;
    taskEXIT_CRITICAL(&s_last_mux);
}

/**
 * @brief The console's line, once a second.
 *
 * @param bat_mv The cell, mV, or NODE_BAT_ERR.
 */
static void print_line(uint16_t bat_mv)
{
    char role[112];
    node_role.line(role, sizeof role);
    rcnode_status_t s;
    node_last_status(&s);
    char link[24] = "none";
    int8_t rssi = 0;
    if (node_ble_linked()) {
        if (node_ble_rssi(&rssi)) {
            snprintf(link, sizeof link, "up %ddBm", (int)rssi);
        } else {
            snprintf(link, sizeof link, "up");
        }
    }
    char bat[16] = "error";
    if (bat_mv != NODE_BAT_ERR) {
        snprintf(bat, sizeof bat, "%u.%02uV", bat_mv / 1000u,
                 (bat_mv % 1000u) / 10u);
    }
    ESP_LOGI(NODE_TAG, "%s %s; link %s, sub %d, bat %s, errors %u, "
             "resets %u, dropped %u", node_role.word, role, link,
             node_ble_subscribed() ? 1 : 0, bat, (unsigned)s.errors,
             (unsigned)s.resets, (unsigned)s.dropped);
}

/**
 * @brief The `node` task's loop, in app_main's task.  Does not return.
 */
static void node_loop(void)
{
    node_policy_t pol;
    node_policy_init(&pol, (uint32_t)(node_us() / 1000u), s_low_reads);
    TickType_t next = xTaskGetTickCount() + pdMS_TO_TICKS(STEP_MS);
    for (;;) {
        const TickType_t now = xTaskGetTickCount();
        const TickType_t wait = (TickType_t)(next - now) > pdMS_TO_TICKS(STEP_MS)
                                ? 0 : (TickType_t)(next - now);
        rcnode_ctl_t c;
        if (xQueueReceive(s_ctlq, &c, wait) == pdTRUE) {
            if (c.op == RCNODE_OP_SLEEP) {
                go_down(NODE_SLEEP, pol.low_reads);
            } else if (c.op == RCNODE_OP_OFF) {
                go_down(NODE_OFF, pol.low_reads);
            } else {
                node_role.control(&c);
            }
            continue;
        }
        const int ask = atomic_exchange(&s_ask, NODE_RUN);
        if (ask != NODE_RUN) {
            go_down((node_act_t)ask, pol.low_reads);
        }
        next += pdMS_TO_TICKS(STEP_MS);

        const uint16_t bat = node_bat_read();
        publish_status(bat);
        print_line(bat);
        const node_inputs_t in = {
            .now_ms = (uint32_t)(node_us() / 1000u),
            .link   = node_ble_linked(),
            .pack   = node_role.busy(),
            .stay   = atomic_load(&s_stay),
            .bat_mv = bat,
        };
        const node_act_t act = node_policy_step(&pol, &in);
        s_low_reads = pol.low_reads;
        if (act != NODE_RUN) {
            go_down(act, pol.low_reads);
        }
    }
}

void app_main(void)
{
    node_bat_init();
    const uint32_t causes = esp_sleep_get_wakeup_causes();
    if ((causes & BIT(ESP_SLEEP_WAKEUP_TIMER)) != 0u && node_role.timer_wake) {
        ++s_wakes;
        node_policy_t p;
        node_policy_init(&p, 0u, s_low_reads);
        const uint16_t bat = node_bat_read();
        int32_t uv = 0;
        const bool answered = node_role.wake(&uv);
        const node_act_t act = node_policy_wake(&p, answered, uv, bat);
        s_low_reads = p.low_reads;
        if (act != NODE_RUN) {
            deep_sleep(act == NODE_SLEEP);
        }
    }
    ESP_LOGI(NODE_TAG, "rcbench node %s, role %s, %lu timer wakes before this start",
             RCBENCH_VERSION_STRING, node_role.word, (unsigned long)s_wakes);
    s_wakes = 0u;

    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        (void)nvs_flash_erase();
        e = nvs_flash_init();
    }
    if (e != ESP_OK) {
        ESP_LOGE(NODE_TAG, "nvs: %s", esp_err_to_name(e));
    }
    s_ctlq = xQueueCreate(CTL_QUEUE_LEN, sizeof(rcnode_ctl_t));
    node_role.start();
    node_ble_start(node_role.role);
    node_console_start();
    node_loop();
}
