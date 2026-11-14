/**
 * @file
 * @brief The Dial's start and its face task: the power latch first, then
 *        the display, the touch controller, BLE, the input task and the
 *        console; then every 50ms the panel's packets, the face and STATUS.
 *
 * The face is drawn into one RGB565 frame of 240 x 240 pixels, 115,200
 * bytes in internal SRAM (the ESP32-S3FN8 has no PSRAM), every 50ms
 * (DIAL_FACE_MS): shared/dial draws what changed, and only the rectangles
 * it wrote are sent.  The whole frame is drawn and sent again every
 * 10,000ms, so a display that lost its contents shows the face again.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "dial.h"

#include "devota.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

/** @brief How long the touch controller needs after the shared reset, ms;
 *         the FT5x06 family's start-up time, not measured on this part. */
#define TOUCH_START_MS 300u
/** @brief The whole frame is drawn and sent at least this often, ms. */
#define FACE_REFRESH_MS 10000u

/** @brief The log's tag. */
static const char *TAG = "dial";
/** @brief The frame. */
static gfx_color_t s_fb[DIAL_LCD_SIZE * DIAL_LCD_SIZE];
/** @brief The panel's model. */
static dk_face_t s_face;

/**
 * @brief What the face shows besides the panel's model.
 * @param v    the view to fill
 * @param addr 20 bytes for the Dial's address
 * @param now  the time
 */
static void view_read(dial_face_view_t *v, char *addr, uint32_t now)
{
    dial_supply_t sup;
    char panel[20];
    dial_supply_read(&sup);
    dial_ble_addresses(addr, panel);
    v->ready = dial_ble_ready();
    v->bound = dial_ble_bound();
    v->lost = dk_face_lost(&s_face, now);
    v->name = dial_ble_name();
    v->addr = addr;
    v->rssi = dial_ble_rssi();
    v->batt_mv = sup.known ? sup.mv : 0u;
    v->usb = sup.usb;
    devota_view_t u;
    devota_view(&u);
    v->upd = u.st == BLEOTA_DEV_IDLE ? DIAL_UPD_NONE
             : u.st <= BLEOTA_DEV_CONFIRM ? DIAL_UPD_ASK
             : u.st == BLEOTA_DEV_DONE ? DIAL_UPD_DONE : DIAL_UPD_SEND;
    v->upd_secs = u.secs;
    v->upd_pct = (uint8_t)(u.percent > 100u ? 100u : u.percent);
    memcpy(v->upd_from, u.from, sizeof v->upd_from);
    memcpy(v->upd_to, u.to, sizeof v->upd_to);
}

/** @brief The ms clock, low 32 bits. @return ms since start */
static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/** @brief Open NVS; a full or older partition is erased first. */
static void nvs_start(void)
{
    esp_err_t rc = nvs_flash_init();
    if (rc == ESP_ERR_NVS_NO_FREE_PAGES
        || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        rc = nvs_flash_init();
    }
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "NVS did not start: the bind is not kept");
    }
}

/**
 * @brief Send STATUS.
 * @param seq  STATUS's own count
 * @param lost the Dial's LINK LOST
 */
static void send_status(uint8_t seq, bool lost)
{
    dk_status_t st;
    dial_supply_t sup;
    uint8_t pkt[DK_LEN_STATUS];
    unsigned v[3] = { 0, 0, 0 };
    const char *ver = esp_app_get_description()->version;
    memset(&st, 0, sizeof st);
    dial_supply_read(&sup);
    sscanf(ver, "%u.%u.%u", &v[0], &v[1], &v[2]);
    st.seq = seq;
    st.batt_mv = sup.known ? sup.mv : 0u;
    st.flags = (uint8_t)((sup.usb ? DK_ST_USB : 0u)
                         | (sup.charging ? DK_ST_CHARGING : 0u)
                         | (sup.known ? 0u : DK_ST_BATT_UNKNOWN)
                         | (lost ? DK_ST_PANEL_LOST : 0u));
    for (unsigned k = 0; k < 3u; ++k) {
        st.fw[k] = (uint8_t)(v[k] > 255u ? 255u : v[k]);
    }
    st.uptime_s = (uint16_t)(now_ms() / 1000u);
    st.rssi = dial_ble_rssi();
    st.bad = (uint8_t)s_face.bad;
    if (dk_status_encode(&st, pkt) != 0u) {
        dial_ble_send_status(pkt, sizeof pkt);
    }
}

/** @brief Take the panel's packets into the model; STATE's clock is the
 *         echo, SCREEN's sound flag the click.  @param now the time */
static void take_packets(uint32_t now)
{
    uint8_t buf[DK_PKT_MAX];
    size_t len = 0;
    while (dial_ble_take(buf, &len)) {
        if (!dk_face_feed(&s_face, buf, len, now)) {
            continue;
        }
        if (buf[0] == DK_K_STATE) {
            dial_ble_set_echo((uint16_t)s_face.state.panel_ms);
        } else if (buf[0] == DK_K_SCREEN) {
            dial_input_sound((s_face.screen.flags & DK_SCR_SOUND) != 0u);
        }
    }
}

/** @brief The start, then the face task's loop; never returns. */
void app_main(void)
{
    dial_face_view_t view;
    char addr[20];
    uint32_t links = 0;
    uint32_t drawn_ms = 0;
    uint32_t status_ms = 0;
    uint8_t status_seq = 0;
    bool status_lost = true;
    bool was_lost = true;
    bool was_ready = false;
    bool first = true;
    TickType_t wake;

    /* First: the latch, or the Dial switches off when WAKE is let go. */
    dial_power_hold();

    nvs_start();
    dial_buzz_init();
    dk_face_init(&s_face);
    (void)dial_screen_init(s_fb);
    if (dial_lcd_init()) {
        dial_lcd_backlight(100);
    }
    vTaskDelay(pdMS_TO_TICKS(TOUCH_START_MS));
    if (!dial_touch_init()) {
        ESP_LOGW(TAG, "no touch controller: taps are not read");
    }
    dial_ble_start();
    dial_input_start();
    dial_console_start();
    ESP_LOGI(TAG, "internal heap: %u bytes free, %u at the lowest",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));

    wake = xTaskGetTickCount();
    for (;;) {
        const uint32_t now = now_ms();
        const uint32_t l = dial_ble_link_count();
        if (l != links) {
            links = l;
            dk_face_link_reset(&s_face);
        }
        take_packets(now);
        dial_input_link(s_face.any, s_face.last_ms);
        view_read(&view, addr, now);
        if (view.ready && !devota_active() && dk_face_drop(&s_face, now)) {
            /* LINK LOST has held for 500ms: end the link, so the panel
             * sees the disconnect.  No EVENT has gone since LINK LOST
             * (dial_input_link()), and none goes until the panel's first
             * packet on the next connection. */
            ESP_LOGW(TAG, "no panel packet for %ums: the link ends",
                     (unsigned)DK_DIAL_DROP_MS);
            dial_ble_drop();
            view.ready = false;
        }
        if (view.lost && !was_lost && was_ready && !first) {
            dial_buzz_tone(800, 80);    /* LINK LOST */
        }
        const bool all = first || now - drawn_ms >= FACE_REFRESH_MS;
        (void)dial_screen_show(&s_face, &view, all);
        s_face.changed = 0;
        was_lost = view.lost;
        was_ready = view.ready;
        if (all) {
            drawn_ms = now;
            first = false;
        }
        if (now - status_ms >= DK_STATUS_MS || view.lost != status_lost) {
            send_status(status_seq++, view.lost);
            status_ms = now;
            status_lost = view.lost;
        }
        dial_ble_poll(now);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(DIAL_FACE_MS));
    }
}
