/**
 * @file
 * @brief The Dial's start and its face task: the power latch first, then
 *        the display, the touch controller, BLE, the input task and the
 *        console; then every 50ms the panel's packets, the face and STATUS.
 *
 * The face is drawn into one RGB565 frame of 240 x 240 pixels, 115,200
 * bytes in internal SRAM (the ESP32-S3FN8 has no PSRAM), and sent whole
 * when it changed, at most every 50ms (DIAL_FACE_MS), and once a second.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "dial.h"

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

/** @brief How long the touch controller needs after the shared reset, ms;
 *         the FT5x06 family's start-up time, not measured on this part. */
#define TOUCH_START_MS 300u
/** @brief The face is sent at least this often, ms. */
#define FACE_REFRESH_MS 1000u

/** @brief The log's tag. */
static const char *TAG = "dial";
/** @brief The frame. */
static gfx_color_t s_fb[DIAL_LCD_SIZE * DIAL_LCD_SIZE];
/** @brief The panel's model. */
static dk_face_t s_face;

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
    gfx_canvas_t canvas;
    dial_view_t view;
    dial_view_t shown;
    uint32_t links = 0;
    uint32_t drawn_ms = 0;
    uint32_t status_ms = 0;
    uint8_t status_seq = 0;
    bool status_lost = true;
    bool first = true;
    TickType_t wake;

    /* First: the latch, or the Dial switches off when WAKE is let go. */
    dial_power_hold();

    nvs_start();
    dial_buzz_init();
    gfx_canvas_init(&canvas, s_fb, DIAL_LCD_SIZE, DIAL_LCD_SIZE, 0);
    dk_face_init(&s_face);
    if (dial_lcd_init()) {
        gfx_clear(&canvas, GFX_BLACK);
        dial_lcd_flush(s_fb, 0, DIAL_LCD_SIZE);
        dial_lcd_backlight(100);
    }
    vTaskDelay(pdMS_TO_TICKS(TOUCH_START_MS));
    if (!dial_touch_init()) {
        ESP_LOGW(TAG, "no touch controller: taps are not read");
    }
    dial_ble_start();
    dial_input_start();
    dial_console_start();

    memset(&shown, 0, sizeof shown);
    wake = xTaskGetTickCount();
    for (;;) {
        const uint32_t now = now_ms();
        const uint32_t l = dial_ble_link_count();
        if (l != links) {
            links = l;
            dk_face_link_reset(&s_face);
        }
        take_packets(now);
        view.ready = dial_ble_ready();
        view.bound = dial_ble_bound();
        view.lost = dk_face_lost(&s_face, now);
        view.name = dial_ble_name();
        if (view.lost && !shown.lost && shown.ready && !first) {
            dial_buzz_tone(800, 80);    /* LINK LOST */
        }
        if (first || s_face.changed != 0u || view.lost != shown.lost
            || view.ready != shown.ready || view.bound != shown.bound
            || now - drawn_ms >= FACE_REFRESH_MS) {
            dial_screen_draw(&canvas, &s_face, &view);
            dial_lcd_flush(s_fb, 0, DIAL_LCD_SIZE);
            s_face.changed = 0;
            shown = view;
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
