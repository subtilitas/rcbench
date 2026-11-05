/**
 * @file panel_c6.c
 * @brief The C6 radio's update from the image this panel carries: SETUP >
 *        UPDATES' C6 RADIO row, the gate, and the steps over SDIO (Secure
 *        Digital input/output) through esp_hosted's OTA (over-the-air) calls.
 *
 * c6_service() runs on app_main once a frame: it reads the bench, hands the
 * row its view, takes the row's 2s hold, and drives the update
 * (shared/fwupdate/fw_c6.h), one step at a time.  Each step runs on the
 * task c6upd, created at the first update, because esp_hosted's calls
 * block for up to 15s: c6upd takes a step from a queue of one, runs its
 * calls and answers on a queue of one.  A step past its limit ends the
 * update on app_main; c6upd's late answer is then ignored, and the gates
 * stay closed (FW_C6_WHY_ENDING) until c6upd is out of the call.
 *
 * While an update runs, or c6upd is in a call, panel_c6_busy() is true:
 * Wi-Fi does not start, ARM is refused, and the panel's own install, the
 * GitHub check and the IOmcu's update wait.  An update that ends
 * FW_C6_RES_UPDATED restarts the panel 3s later, so the host side and the
 * C6 start their link afresh; every other end leaves the panel running,
 * the C6 on the firmware it ran, and Wi-Fi usable (except after a failed
 * link, which holds the C6 in reset until the panel restarts).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_hosted_ota.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "c6_link.h"
#include "fw_c6.h"
#include "panel.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "upd_c6.h"
#include "wifi_access.h"

/** @brief Log tag, the panel's. */
static const char *TAG = "rcbench";

/** @brief c6upd's stack, bytes: esp_hosted's RPC (remote procedure call)
 *         path under the OTA calls; docs/Performance.md holds the depth. */
#define C6_STACK      4096
/** @brief c6upd's priority: above the card's job, below the radio's own. */
#define C6_PRIO       2
/** @brief c6upd's core: 0, with the radio's tasks. */
#define C6_CORE       0
/** @brief From an update's end FW_C6_RES_UPDATED to the restart, ms: the
 *         alert shows that long. */
#define C6_RESTART_MS 3000u

#ifdef RCBENCH_C6_IMAGE
/** @brief First byte of the carried C6 image (main/CMakeLists.txt). */
extern const uint8_t _binary_rcbench_c6_bin_start[];
/** @brief One past its last byte. */
extern const uint8_t _binary_rcbench_c6_bin_end[];
#endif

/** @brief One step's answer, c6upd to app_main. */
typedef struct {
    uint32_t     gen;     /**< the step's gen                        */
    int32_t      err;     /**< 0, or the failing call's esp_err_t    */
    bool         known;   /**< LINK: the C6 reported its version     */
    fw_version_t running; /**< LINK: that version                    */
} c6_answer_t;

static fw_c6_ota_t     s_ota;        /**< app_main only */
static fw_image_info_t s_info;       /**< the carried image, once checked */
static bool            s_checked;    /**< the image was checked */
static fw_err_t        s_check;      /**< and what the check said */
static bool            s_seen_end;   /**< the update's end was reported */
static bool            s_restart;    /**< a restart is due at s_restart_at */
static uint32_t        s_restart_at; /**< when the restart is due, ms */
static TaskHandle_t    s_task;       /**< c6upd, once created */
static QueueHandle_t   s_steps;      /**< app_main to c6upd, one deep */
static QueueHandle_t   s_answers;    /**< c6upd to app_main, one deep */
static atomic_bool     s_in_call;    /**< c6upd runs a step */
static atomic_bool     s_busy;       /**< panel_c6_busy() */
/** @brief One chunk in internal RAM: esp_hosted's write takes a pointer
 *         it may hand to SDIO, and the image lies in flash. */
static uint8_t         s_chunk[FW_C6_CHUNK];

/**
 * @brief The carried image and its length.
 * @param len receives the length, 0 for none
 * @return the image, NULL for none
 */
static const uint8_t *carried(uint32_t *len)
{
#ifdef RCBENCH_C6_IMAGE
    /* cppcheck takes the two symbols for two arrays; see CU_SPAN(). */
    // cppcheck-suppress subtractPointers
    *len = (uint32_t)(_binary_rcbench_c6_bin_end - _binary_rcbench_c6_bin_start);
    return _binary_rcbench_c6_bin_start;
#else
    *len = 0u;
    return NULL;
#endif
}

/** @brief Checks the carried image once and logs what it is. */
static void check_once(void)
{
    if (s_checked) {
        return;
    }
    s_checked = true;
    uint32_t len = 0u;
    const uint8_t *img = carried(&len);
    if (img == NULL) {
        s_check = FW_ERR_EMPTY;
        ESP_LOGI(TAG, "C6: this build carries no C6 image");
        return;
    }
    s_check = fw_c6_image_check(img, len, &s_info);
    if (s_check == FW_OK) {
        ESP_LOGI(TAG, "C6: carries esp_hosted %s, %lu bytes",
                 s_info.version_text, (unsigned long)len);
    } else {
        ESP_LOGE(TAG, "C6: the carried image fails its check: %s",
                 fw_err_str(s_check));
    }
}

/**
 * @brief Runs one step's calls on c6upd.
 * @param st the step
 * @param a  its answer, filled
 */
static void run_step(const fw_c6_step_t *st, c6_answer_t *a)
{
    uint32_t len = 0u;
    const uint8_t *img = carried(&len);
    esp_err_t e = ESP_OK;
    switch (st->act) {
    case FW_C6_ACT_LINK: {
        e = c6_link_up();
        c6_link_version_t v;
        if (e == ESP_OK && c6_link_version(&v)) {
            a->known = true;
            a->running = (fw_version_t){ v.major, v.minor, v.patch };
        }
        break;
    }
    case FW_C6_ACT_BEGIN:
        e = esp_hosted_slave_ota_begin();
        break;
    case FW_C6_ACT_WRITE:
        for (uint32_t at = st->offset; e == ESP_OK && at < st->offset + st->len;
             at += FW_C6_CHUNK) {
            const uint32_t end = st->offset + st->len;
            const uint32_t n = (end - at < FW_C6_CHUNK) ? end - at : FW_C6_CHUNK;
            if (img == NULL || at + n > len) {
                e = ESP_ERR_INVALID_SIZE;
                break;
            }
            memcpy(s_chunk, img + at, n);
            e = esp_hosted_slave_ota_write(s_chunk, n);
        }
        break;
    case FW_C6_ACT_END:
    case FW_C6_ACT_CLOSE:
        e = esp_hosted_slave_ota_end();
        break;
    case FW_C6_ACT_ACTIVATE:
        e = esp_hosted_slave_ota_activate();
        break;
    case FW_C6_ACT_NONE:
    default:
        break;
    }
    a->err = (int32_t)e;
}

/**
 * @brief c6upd: a step from the queue, its calls, its answer.
 * @param arg unused
 */
static void c6_task(void *arg)
{
    (void)arg;
    for (;;) {
        fw_c6_step_t st;
        if (xQueueReceive(s_steps, &st, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        c6_answer_t a = { .gen = st.gen };
        run_step(&st, &a);
        (void)xQueueOverwrite(s_answers, &a);
        atomic_store(&s_in_call, false);
    }
}

/**
 * @brief c6upd and its queues, once.
 * @return true when they run
 */
static bool task_once(void)
{
    if (s_task != NULL) {
        return true;
    }
    if (s_steps == NULL) {
        s_steps = xQueueCreate(1, sizeof(fw_c6_step_t));
    }
    if (s_answers == NULL) {
        s_answers = xQueueCreate(1, sizeof(c6_answer_t));
    }
    if (s_steps == NULL || s_answers == NULL
        || xTaskCreatePinnedToCore(c6_task, "c6upd", C6_STACK, NULL, C6_PRIO,
                                   &s_task, C6_CORE)
               != pdPASS) {
        s_task = NULL;
        ESP_LOGE(TAG, "C6: no RAM for the update's task");
        return false;
    }
    return true;
}

bool panel_c6_busy(void)
{
    return atomic_load(&s_busy);
}

/**
 * @brief How an update's failure reads in the alert.
 * @param res how it ended
 * @return the reason's text
 */
static const char *fail_text(fw_c6_res_t res)
{
    switch (res) {
    case FW_C6_RES_WRITE:    return TR(C6_FAIL_WRITE);
    case FW_C6_RES_VERIFY:   return TR(C6_FAIL_VERIFY);
    case FW_C6_RES_ACTIVATE: return TR(C6_FAIL_ACTIVATE);
    case FW_C6_RES_NO_ANSWER:
    default:                 return TR(CU_WHY_NO_ANSWER);
    }
}

/**
 * @brief Once, at an update's end: the console line, the alert, the
 *        restart after a success.
 * @param now_ms now
 */
static void report_end(uint32_t now_ms)
{
    char line[ALERT_MAX];
    const fw_c6_ota_t *o = &s_ota;
    switch (o->result) {
    case FW_C6_RES_UPDATED:
        ESP_LOGI(TAG, "C6: updated to esp_hosted %s, %lu bytes; restarting "
                      "in %ums", s_info.version_text, (unsigned long)o->total,
                 (unsigned)C6_RESTART_MS);
        snprintf(line, sizeof(line), TR(ALERT_C6_DONE), s_info.version_text);
        control_alert(line);
        s_restart = true;
        s_restart_at = now_ms + C6_RESTART_MS;
        break;
    case FW_C6_RES_SAME:
        ESP_LOGI(TAG, "C6: runs esp_hosted %s already; nothing sent",
                 s_info.version_text);
        snprintf(line, sizeof(line), TR(ALERT_C6_SAME), s_info.version_text);
        control_alert(line);
        break;
    case FW_C6_RES_NO_LINK:
        ESP_LOGE(TAG, "C6 update: the C6 did not answer");
        control_alert(TR(ALERT_C6_NO_LINK));
        break;
    case FW_C6_RES_REFUSED:
        ESP_LOGE(TAG, "C6 update: begin refused, error 0x%x; the C6 takes "
                      "no update over SDIO", (unsigned)o->error);
        control_alert(TR(ALERT_C6_NO_OTA));
        break;
    default:
        ESP_LOGE(TAG, "C6 update ended %u in phase %u, error 0x%x, %lu of "
                      "%lu bytes; the C6 keeps its firmware",
                 (unsigned)o->result, (unsigned)o->failed_in,
                 (unsigned)o->error, (unsigned long)o->done,
                 (unsigned long)o->total);
        snprintf(line, sizeof(line), TR(ALERT_C6_FAILED),
                 fail_text(o->result));
        control_alert(line);
        break;
    }
}

/**
 * @brief What the row's gate reads of the bench now.
 * @param armed ARMED or an arm waiting
 * @return the feed
 */
static fw_c6_feed_t feed_now(bool armed)
{
    fw_c6_feed_t f;
    memset(&f, 0, sizeof(f));
    uint32_t len = 0u;
    f.have_image = carried(&len) != NULL;
    f.image_ok = s_check == FW_OK;
    f.carried = s_info.version;
    c6_link_version_t v;
    if (c6_link_version(&v)) {
        f.known = true;
        f.running = (fw_version_t){ v.major, v.minor, v.patch };
    }
    f.link_failed = c6_link_failed();
    f.updating = fw_c6_ota_active(&s_ota);
    f.ending = atomic_load(&s_in_call);
    f.armed = armed;
    f.supply = supplies_live_or_coming();
    f.run = panel_run_open();
    f.drawing = s_artbusy || atomic_load(&s_keeping);
    f.iomcu = panel_cu_busy();
    f.installing = panel_fw_busy();
    f.radio_up = s_wifi_ok && !wifi_access_down();
    return f;
}

/**
 * @brief The completed hold: the gate once more, then the update's start.
 * @param armed ARMED or an arm waiting
 * @param now   now
 */
static void start_update(bool armed, uint32_t now)
{
    /* Busy first: from here Wi-Fi does not start (wifi_service()), so the
     * radio found down below stays down. */
    atomic_store(&s_busy, true);
    if (s_wifi_ok) {
        wifi_access_end_linger();
    }
    fw_c6_why_t why = FW_C6_WHY_NONE;
    const fw_c6_feed_t f = feed_now(armed);
    const fw_c6_row_t row = fw_c6_row(&f, &why);
    uint32_t len = 0u;
    (void)carried(&len);
    if (row != FW_C6_ROW_READY || !task_once()
        || !fw_c6_ota_start(&s_ota, len, s_info.version, now)) {
        const upd_c6_view_t v = { .row = row, .why = why };
        ESP_LOGW(TAG, "C6 update refused: row %u, reason %u", (unsigned)row,
                 (unsigned)why);
        ui_router_set_notice(upd_c6_reason(&v) != NULL
                                 ? upd_c6_reason(&v) : TR(C6_WHY_ENDING));
        atomic_store(&s_busy, false);
        return;
    }
    s_seen_end = false;
    ESP_LOGI(TAG, "C6 update: esp_hosted %s, %lu bytes", s_info.version_text,
             (unsigned long)len);
}

void c6_service(bool armed)
{
    check_once();
    const uint32_t now = now_ms();

    /* The answer to the step out, then its limit. */
    c6_answer_t a;
    if (s_answers != NULL && xQueueReceive(s_answers, &a, 0) == pdTRUE) {
        (void)fw_c6_ota_answer(&s_ota, a.gen, a.err, a.known, a.running);
    }
    fw_c6_ota_tick(&s_ota, now);
    if (s_ota.phase == FW_C6_PH_ENDED && !s_seen_end) {
        s_seen_end = true;
        report_end(now);
    }
    fw_c6_step_t st;
    if (!atomic_load(&s_in_call) && fw_c6_ota_next(&s_ota, now, &st)) {
        atomic_store(&s_in_call, true);
        if (xQueueSend(s_steps, &st, 0) != pdTRUE) {
            atomic_store(&s_in_call, false);
        }
    }

    if (upd_c6_take()) {
        start_update(armed, now);
    }

    fw_c6_why_t why = FW_C6_WHY_NONE;
    const fw_c6_feed_t f = feed_now(armed);
    upd_c6_view_t v = {
        .row = fw_c6_row(&f, &why), .why = why, .known = f.known,
        .running = f.running, .have_image = f.have_image && f.image_ok,
        .carried = f.carried, .percent = fw_c6_ota_percent(&s_ota),
    };
    upd_c6_set(&v);

    atomic_store(&s_busy, fw_c6_ota_active(&s_ota) || atomic_load(&s_in_call)
                              || s_restart);
    if (s_restart && (int32_t)(now - s_restart_at) >= 0) {
        esp_restart();
    }
}
