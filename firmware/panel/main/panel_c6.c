/**
 * @file panel_c6.c
 * @brief The C6 radio's update from the image of the release the panel
 *        runs, fetched by the row's hold (panel_devimg.h): SETUP >
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
 * stay closed (FW_C6_WHY_ENDING) until c6upd is out of the call; after a
 * success they stay closed until the restart (FW_C6_WHY_RESTART).
 *
 * While an update runs, c6upd is in a call or a restart is due,
 * panel_c6_busy() is true: Wi-Fi does not start, ARM, the supply's ON, the
 * PROG ports' 5V ON and a programmer session's start are refused
 * (fw_c6_refuses(), panel_upd_refuses()), and the panel's own install, the GitHub check and the
 * IOmcu's update wait.  An update that ends FW_C6_RES_UPDATED restarts the
 * panel, so the host side and the C6 start their link afresh: 3s later at
 * the earliest, and only on a bench that is SAFE with the supply and the
 * PROG ports' 5V off and no programmer holding a port; no update starts
 * while a programmer holds one (FW_C6_WHY_PROG).  The update run's
 * note, a flash write, waits for the same bench (fw_c6_restart_step()).
 * Every other end leaves the panel running,
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
#include "flash_gate.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "c6_link.h"
#include "fw_c6.h"
#include "fw_ota.h"
#include "panel.h"
#include "panel_ble.h"
#include "panel_devimg.h"
#include "panel_devupd.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "upd_c6.h"
#include "upd_screen.h"
#include "upd_wizard.h"
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

/** @brief One step's answer, c6upd to app_main. */
typedef struct {
    uint32_t     gen;     /**< the step's gen                        */
    int32_t      err;     /**< 0, or the failing call's esp_err_t    */
    bool         known;   /**< LINK: the C6 reported its version     */
    fw_version_t running; /**< LINK: that version                    */
} c6_answer_t;

static fw_c6_ota_t     s_ota;        /**< app_main only */
static fw_image_info_t s_info;       /**< the image of the update under way */
static const uint8_t  *s_img;        /**< its bytes, PSRAM (panel_devimg.c) */
static uint32_t        s_len;        /**< their count */
static bool            s_seen_end;   /**< the update's end was reported */
static fw_c6_restart_t s_restart;    /**< the restart after a success */
static TaskHandle_t    s_task;       /**< c6upd, once created */
static QueueHandle_t   s_steps;      /**< app_main to c6upd, one deep */
static QueueHandle_t   s_answers;    /**< c6upd to app_main, one deep */
static atomic_bool     s_in_call;    /**< c6upd runs a step */
static atomic_bool     s_busy;       /**< panel_c6_busy() */
/** @brief One chunk in internal RAM: esp_hosted's write takes a pointer
 *         it may hand to SDIO, and the image lies in flash. */
static uint8_t         s_chunk[FW_C6_CHUNK];

/**
 * @brief Runs one step's calls on c6upd.
 * @param st the step
 * @param a  its answer, filled
 */
static void run_step(const fw_c6_step_t *st, c6_answer_t *a)
{
    /* Set before the update's first step and unchanged to its end. */
    const uint32_t len = s_len;
    const uint8_t *img = s_img;
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
 * @brief Once, at an update's end: the console line, the alert, and the
 *        restart owed after a success (restart_service()).
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
                 (unsigned)FW_C6_RESTART_MS);
        snprintf(line, sizeof(line), TR(ALERT_C6_DONE), s_info.version_text);
        control_alert(line);
        upd_screen_set_result(UPD_WIZ_C6, NULL);
        fw_c6_restart_due(&s_restart, now_ms);
        break;
    case FW_C6_RES_NO_LINK:
        ESP_LOGE(TAG, "C6 update: the C6 did not answer");
        control_alert(TR(ALERT_C6_NO_LINK));
        upd_screen_set_result(UPD_WIZ_C6, TR(ALERT_C6_NO_LINK));
        break;
    case FW_C6_RES_REFUSED:
        ESP_LOGE(TAG, "C6 update: begin refused, error 0x%x; the C6 takes "
                      "no update over SDIO", (unsigned)o->error);
        control_alert(TR(ALERT_C6_NO_OTA));
        upd_screen_set_result(UPD_WIZ_C6, TR(ALERT_C6_NO_OTA));
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
        upd_screen_set_result(UPD_WIZ_C6, line);
        break;
    }
}

/**
 * @brief What the row's gate reads of the bench now.
 * @param armed ARMED, an arm waiting or an ARM queued
 * @return the feed
 */
static fw_c6_feed_t feed_now(bool armed)
{
    fw_c6_feed_t f;
    memset(&f, 0, sizeof(f));
    /* The release publishes the image; the hold fetches it when it is not
     * in memory yet (panel_devimg.h). */
    f.have_image = true;
    f.image_ok = true;
    f.carried = panel_devimg_want(DEVIMG_C6);
    c6_link_version_t v;
    if (c6_link_version(&v)) {
        f.known = true;
        f.running = (fw_version_t){ v.major, v.minor, v.patch };
    }
    f.link_failed = c6_link_failed();
    /* The image on its way reads as the update running: no second hold. */
    f.updating = fw_c6_ota_active(&s_ota) || panel_devimg_running(DEVIMG_C6);
    f.ending = atomic_load(&s_in_call);
    f.restarting = fw_c6_restart_pending(&s_restart);
    f.armed = armed;
    f.supply = supplies_live_or_coming();
    f.run = panel_run_open();
    f.drawing = s_artbusy || atomic_load(&s_keeping);
    f.iomcu = panel_cu_busy();
    f.installing = panel_fw_busy();
    f.radio_up = s_wifi_ok && !wifi_access_down();
    /* blestart brings the link up and resets the C6's controller: not
     * under an update's steps. */
    f.ble_starting = panel_ble_state() == PANEL_BLE_STARTING;
    /* A wireless device's update runs on the C6's controller. */
    f.device = panel_devupd_busy();
    /* A programmer on a port, or a session's start claimed: the restart
     * would end its session.  Read after s_busy is set (start_update()). */
    f.prog = atomic_load(&s_prog_open);
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
    const bool ready = row == FW_C6_ROW_READY || row == FW_C6_ROW_REINSTALL;
    if (ready && !panel_devimg_get(DEVIMG_C6, NULL, NULL, NULL, NULL)) {
        /* Not in memory: the download or the card read first; the update
         * starts when the image has passed its checks
         * (owed_service()). */
        atomic_store(&s_busy, false);
        (void)panel_devimg_request(DEVIMG_C6, armed);
        return;
    }
    if (ready) {
        (void)panel_devimg_get(DEVIMG_C6, &s_img, &s_len, &s_info, NULL);
    }
    if (!ready || !task_once()
        || !fw_c6_ota_start(&s_ota, s_len, s_info.version, now)) {
        const upd_c6_view_t v = { .row = row, .why = why };
        ESP_LOGW(TAG, "C6 update refused: row %u, reason %u", (unsigned)row,
                 (unsigned)why);
        ui_router_set_notice(upd_c6_reason(&v) != NULL
                                 ? upd_c6_reason(&v) : TR(C6_WHY_ENDING));
        atomic_store(&s_busy, false);
        return;
    }
    s_seen_end = false;
    ESP_LOGI(TAG, "C6 update%s: esp_hosted %s, %lu bytes",
             row == FW_C6_ROW_REINSTALL ? " (reinstall: the C6 reports this "
                                          "version)" : "",
             s_info.version_text, (unsigned long)s_len);
}

/**
 * @brief The restart owed after a success, one frame: the run note and the
 *        restart only on a quiet bench, the alert while it is not.
 * @param armed ARMED, an arm waiting or an ARM queued
 * @param now   now
 */
static void restart_service(bool armed, uint32_t now)
{
    /* The flash gate, one flash writer at a time, and no programmer on a
     * port: the restart stops the heartbeat and ends its session. */
    const bool quiet = flash_gate_open(armed, supplies_live_or_coming())
                       && !s_artbusy && !atomic_load(&s_keeping)
                       && !atomic_load(&s_prog_open);
    switch (fw_c6_restart_step(&s_restart, quiet, now)) {
    case FW_C6_RST_NOTE: {
        /* The update run goes on after the restart into this build. */
        fw_image_info_t me;
        memset(&me, 0, sizeof(me));
        if (fw_ota_running(&me)) {
            (void)fw_ota_run_begin(me.version_text);
        }
        break;
    }
    case FW_C6_RST_TELL: {
        char line[ALERT_MAX];
        ESP_LOGW(TAG, "C6: the restart waits for SAFE with all outputs off");
        snprintf(line, sizeof(line), TR(ALERT_C6_WAITS), s_info.version_text);
        control_alert(line);
        break;
    }
    case FW_C6_RST_GO:
        esp_restart();
        break;
    case FW_C6_RST_IDLE:
    case FW_C6_RST_SHOWING:
    case FW_C6_RST_WAITING:
    default:
        break;
    }
}

/**
 * @brief The update a hold owes to an image that was on its way: started
 *        once the row's hold would start it, kept while Wi-Fi goes off
 *        after the download, dropped with the row's reason otherwise.
 * @param armed ARMED, an arm waiting or an ARM queued
 * @param now   now
 */
static void owed_service(bool armed, uint32_t now)
{
    fw_c6_why_t why = FW_C6_WHY_NONE;
    const fw_c6_feed_t f = feed_now(armed);
    const fw_c6_row_t row = fw_c6_row(&f, &why);
    const bool open = row == FW_C6_ROW_READY || row == FW_C6_ROW_REINSTALL;
    const bool radio = row == FW_C6_ROW_BLOCKED && why == FW_C6_WHY_RADIO;
    switch (panel_devimg_owed(DEVIMG_C6, open, radio)) {
    case DEVIMG_OWED_FIRE:
        start_update(armed, now);
        break;
    case DEVIMG_OWED_WAIT:
        if (radio && s_wifi_ok) {
            /* The download's time request holds the radio up to 30s
             * longer; the update does not wait for it. */
            wifi_access_end_linger();
        }
        break;
    case DEVIMG_OWED_DROP: {
        const upd_c6_view_t v = { .row = row, .why = why };
        const char *t = upd_c6_reason(&v);
        ESP_LOGW(TAG, "C6: the image arrived; the update does not start: "
                      "row %u, reason %u", (unsigned)row, (unsigned)why);
        ui_router_set_notice(t != NULL ? t : TR(C6_WHY_ENDING));
        break;
    }
    case DEVIMG_OWED_NONE:
    default:
        break;
    }
}

void c6_service(bool armed)
{
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
    owed_service(armed, now);

    fw_c6_why_t why = FW_C6_WHY_NONE;
    fw_c6_feed_t f = feed_now(armed);
    /* The row shows the update under way while its start is still owed
     * (the radio's wait): no SKIP, no second hold. */
    f.updating = f.updating || panel_devimg_pending(DEVIMG_C6);
    upd_c6_view_t v = {
        .row = fw_c6_row(&f, &why), .why = why, .known = f.known,
        .running = f.running, .have_image = f.have_image && f.image_ok,
        .carried = f.carried, .percent = fw_c6_ota_percent(&s_ota),
    };
    panel_devimg_view(DEVIMG_C6, &v.fetch);
    upd_c6_set(&v);

    atomic_store(&s_busy, fw_c6_ota_active(&s_ota) || atomic_load(&s_in_call)
                              || fw_c6_restart_pending(&s_restart));
    restart_service(armed, now);
}
