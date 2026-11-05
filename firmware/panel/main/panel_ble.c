/**
 * @file panel_ble.c
 * @brief Bluetooth LE on the panel; see panel_ble.h.
 *
 * The host stack is NimBLE (sdkconfig.defaults, "Bluetooth LE through the
 * ESP32-C6"); esp_hosted's VHCI driver passes its HCI packets to the C6
 * over the SDIO link that Wi-Fi uses too.  NimBLE's transport start
 * (ble_transport_ll_init() in esp_hosted's vhci_drv.c) aborts the panel
 * when the SDIO link is not up, so nimble_port_init() runs only after
 * c6_link_up() returned ESP_OK.
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel_ble.h"

#include <stdatomic.h>
#include <string.h>

#include "c6_link.h"
#include "esp_heap_caps.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

/** @brief The start task's stack: the start's log lines and the C6's
 *         requests.  tools/stack_check.py holds it. */
#define BLE_START_STACK 4096u
/** @brief The start task's priority: below the card and the control task. */
#define BLE_START_PRIO 2u
/** @brief Longest scan panel_ble_scan() takes, ms. */
#define BLE_SCAN_MAX_MS 180000u
/** @brief A controller request that took this long went unanswered:
 *         esp_hosted ends each one after DEFAULT_RPC_RSP_TIMEOUT, 5s
 *         (rpc_slave_if.h), ms. */
#define BLE_RPC_SILENT_MS 4500u

_Static_assert(PANEL_BLE_DATA_MAX >= BLE_HS_ADV_MAX_SZ,
               "a legacy advertising report fits an event");

/** @brief Log tag. */
static const char *TAG = "ble";

/** @brief panel_ble_state_t, written by the start task and the host task. */
static atomic_int s_state = PANEL_BLE_OFF;
/** @brief The start ran once. */
static atomic_flag s_started = ATOMIC_FLAG_INIT;
/** @brief Events dropped on a full queue. */
static atomic_uint s_dropped;
/** @brief The address type the host uses, set at each sync. */
static atomic_uint s_own_type;
/** @brief When the host task started, esp_timer ms; 0 before. */
static atomic_uint s_host_t0;
/** @brief The host's syncs since start-up. */
static atomic_uint s_syncs;

/** @brief The queue to the IMU's link. */
static QueueHandle_t s_queue;
/** @brief The queue's control block: no heap allocation that can fail. */
static StaticQueue_t s_queue_mem;
/** @brief The queue's elements, 32 x 64 = 2,048 bytes of internal RAM. */
static uint8_t s_queue_buf[PANEL_BLE_QUEUE_LEN * sizeof(panel_ble_evt_t)];
/** @brief Given at the host's first sync, for the start task's wait. */
static SemaphoreHandle_t s_synced;
/** @brief s_synced's memory. */
static StaticSemaphore_t s_synced_mem;

/**
 * @brief ms since the panel started, 32 bits, wrapping.
 * @return The time.
 */
static uint32_t ble_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/**
 * @brief Queue one event without waiting; a full queue counts the drop.
 * @param e The event.
 */
static void post(const panel_ble_evt_t *e)
{
    if (s_queue == NULL || xQueueSend(s_queue, e, 0) != pdTRUE) {
        atomic_fetch_add(&s_dropped, 1u);
    }
}

/**
 * @brief An event of @p kind with @p status and nothing else.
 * @param kind   panel_ble_kind_t.
 * @param status Its status.
 */
static void post_plain(panel_ble_kind_t kind, int status)
{
    panel_ble_evt_t e;
    memset(&e, 0, sizeof(e));
    e.kind = (uint8_t)kind;
    e.status = (int16_t)status;
    e.conn = 0xFFFFu;
    post(&e);
}

/**
 * @brief The host's sync: the C6 answered the HCI reset and the reads
 *        after it.  Runs on nimble_host.
 */
static void on_sync(void)
{
    uint8_t type = BLE_OWN_ADDR_PUBLIC;
    const int rc = ble_hs_id_infer_auto(0, &type);
    if (rc != 0) {
        ESP_LOGW(TAG, "no own address: %d", rc);
    }
    atomic_store(&s_own_type, type);
    atomic_store(&s_state, PANEL_BLE_READY);
    if (atomic_fetch_add(&s_syncs, 1u) == 0u) {
        ESP_LOGI(TAG, "synced in %ums, own address type %u",
                 (unsigned)(ble_now_ms() - atomic_load(&s_host_t0)),
                 (unsigned)type);
    } else {
        ESP_LOGI(TAG, "synced again, own address type %u", (unsigned)type);
    }
    (void)xSemaphoreGive(s_synced);
    post_plain(PANEL_BLE_EV_SYNC, rc);
}

/**
 * @brief The host lost its sync, for a controller that went silent or an
 *        HCI error; NimBLE resyncs on its own.  Runs on nimble_host.
 * @param reason NimBLE's reason.
 */
static void on_reset(int reason)
{
    atomic_store(&s_state, PANEL_BLE_NO_SYNC);
    ESP_LOGW(TAG, "host reset, reason %d", reason);
    post_plain(PANEL_BLE_EV_RESET, reason);
}

/**
 * @brief The host task: NimBLE's event loop, which never returns, as the
 *        host is never stopped.
 * @param arg Unused.
 */
static void ble_host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/**
 * @brief A scan's events into the queue.  Runs on nimble_host.
 * @param ev  NimBLE's event.
 * @param arg Unused.
 * @return 0.
 */
static int gap_event(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    panel_ble_evt_t e;
    memset(&e, 0, sizeof(e));
    e.conn = 0xFFFFu;
    switch (ev->type) {
    case BLE_GAP_EVENT_DISC:
        e.kind = PANEL_BLE_EV_ADV;
        e.addr_type = ev->disc.addr.type;
        e.rssi = ev->disc.rssi;
        e.status = (int16_t)ev->disc.event_type;
        memcpy(e.addr, ev->disc.addr.val, sizeof(e.addr));
        e.len = ev->disc.length_data <= PANEL_BLE_DATA_MAX
                    ? ev->disc.length_data : (uint8_t)PANEL_BLE_DATA_MAX;
        if (ev->disc.data != NULL) {
            memcpy(e.data, ev->disc.data, e.len);
        } else {
            e.len = 0u;
        }
        post(&e);
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        e.kind = PANEL_BLE_EV_SCAN_DONE;
        e.status = (int16_t)ev->disc_complete.reason;
        post(&e);
        break;
    default:
        break;
    }
    return 0;
}

/**
 * @brief The C6's controller init and enable requests, each timed.
 *
 * esp_hosted 2.5.2 and later leave the C6's controller off until the host
 * asks; earlier firmware has no such requests and starts its controller
 * itself.  A request the C6 does not know is answered with an error or not
 * at all; esp_hosted ends an unanswered one after 5s.  After an unanswered
 * init the enable is not sent: the C6 does not know that request either.
 */
static void controller_requests(void)
{
    const uint32_t t0 = ble_now_ms();
    const esp_err_t ei = esp_hosted_bt_controller_init();
    const uint32_t t1 = ble_now_ms();
    if (ei != ESP_OK && t1 - t0 >= BLE_RPC_SILENT_MS) {
        ESP_LOGW(TAG, "C6 controller init not answered in %ums: a C6 "
                      "firmware without the requests, its controller "
                      "runs from its own start", (unsigned)(t1 - t0));
        return;
    }
    const esp_err_t ee = esp_hosted_bt_controller_enable();
    const uint32_t t2 = ble_now_ms();
    if (ei == ESP_OK && ee == ESP_OK) {
        ESP_LOGI(TAG, "C6 controller init and enable answered in %ums and "
                      "%ums", (unsigned)(t1 - t0), (unsigned)(t2 - t1));
    } else {
        ESP_LOGW(TAG, "C6 controller init %s in %ums, enable %s in %ums: "
                      "the host start goes on",
                 esp_err_to_name(ei), (unsigned)(t1 - t0),
                 esp_err_to_name(ee), (unsigned)(t2 - t1));
    }
}

/**
 * @brief The start task: the link, the controller, the host, the sync.
 *        Ends when the wait for the sync has ended.
 * @param arg Unused.
 */
static void ble_start_task(void *arg)
{
    (void)arg;
    const size_t in0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t ps0 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (c6_link_up() != ESP_OK) {
        atomic_store(&s_state, PANEL_BLE_NO_LINK);
        ESP_LOGE(TAG, "no link to the C6: no Bluetooth until the panel "
                      "restarts");
        vTaskDelete(NULL);
        return;
    }
    controller_requests();
    const esp_err_t e = nimble_port_init();
    if (e != ESP_OK) {
        atomic_store(&s_state, PANEL_BLE_NO_SYNC);
        ESP_LOGE(TAG, "nimble_port_init: %s; no Bluetooth until the panel "
                      "restarts", esp_err_to_name(e));
        vTaskDelete(NULL);
        return;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    atomic_store(&s_host_t0, ble_now_ms());
    nimble_port_freertos_init(ble_host_task);
    if (xSemaphoreTake(s_synced, pdMS_TO_TICKS(PANEL_BLE_SYNC_MS)) != pdTRUE) {
        /* A sync after this still makes the state READY (on_sync). */
        int expect = PANEL_BLE_STARTING;
        (void)atomic_compare_exchange_strong(&s_state, &expect,
                                             PANEL_BLE_NO_SYNC);
        ESP_LOGW(TAG, "no sync within %ums: the C6 answers no HCI; the host "
                      "keeps trying", (unsigned)PANEL_BLE_SYNC_MS);
    }
    ESP_LOGI(TAG, "host started; internal RAM %uB free before, %uB after; "
                  "PSRAM %uB before, %uB after",
             (unsigned)in0,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)ps0,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    vTaskDelete(NULL);
}

void panel_ble_start(void)
{
    if (atomic_flag_test_and_set(&s_started)) {
        return;
    }
    s_queue = xQueueCreateStatic(PANEL_BLE_QUEUE_LEN, sizeof(panel_ble_evt_t),
                                 s_queue_buf, &s_queue_mem);
    s_synced = xSemaphoreCreateBinaryStatic(&s_synced_mem);
    atomic_store(&s_state, PANEL_BLE_STARTING);
    if (xTaskCreatePinnedToCore(ble_start_task, "blestart", BLE_START_STACK,
                                NULL, BLE_START_PRIO, NULL, 0) != pdPASS) {
        atomic_store(&s_state, PANEL_BLE_NO_SYNC);
        ESP_LOGE(TAG, "no memory for the start task: no Bluetooth until "
                      "the panel restarts");
    }
}

panel_ble_state_t panel_ble_state(void)
{
    return (panel_ble_state_t)atomic_load(&s_state);
}

bool panel_ble_get(panel_ble_evt_t *out)
{
    return s_queue != NULL && out != NULL
           && xQueueReceive(s_queue, out, 0) == pdTRUE;
}

uint32_t panel_ble_dropped(void)
{
    return atomic_load(&s_dropped);
}

esp_err_t panel_ble_scan(uint32_t ms)
{
    if (ms == 0u || ms > BLE_SCAN_MAX_MS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (atomic_load(&s_state) != PANEL_BLE_READY || ble_gap_disc_active()
        || ble_gap_conn_active()) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Active, the controller's duplicate filter on; NimBLE's default
     * interval and window. */
    const struct ble_gap_disc_params p = {
        .passive = 0,
        .filter_duplicates = 1,
    };
    const int rc = ble_gap_disc((uint8_t)atomic_load(&s_own_type), (int32_t)ms,
                                &p, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "scan refused: %d", rc);
        return rc == BLE_HS_EALREADY || rc == BLE_HS_EBUSY
                   ? ESP_ERR_INVALID_STATE : ESP_FAIL;
    }
    return ESP_OK;
}
