/*
 * SPDX-License-Identifier: MIT
 */

#include "touch.h"

#include <string.h>

#include "board.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gt911.h"

static const char *TAG = "touch";

typedef struct {
    bool initialised;
    volatile bool stop;

    gt911_handle_t gt911;
    touch_config_t cfg;
    touch_map_t map;

    TaskHandle_t task;
    QueueHandle_t events;
    SemaphoreHandle_t lock;

    touch_point_t points[TOUCH_MAX_POINTS];
    uint8_t point_count;

    touch_tracker_t tracker;

    /*
     * When the controller last answered, in milliseconds.  32 bits because
     * an aligned 32-bit load is atomic on the ESP32-S3, so the application
     * reads it from its own task without the lock, and unsigned subtraction
     * stays correct across the 49-day wrap.
     *
     * "Answered" means the I2C (Inter-Integrated Circuit) read succeeded,
     * not that a finger was down; an untouched panel is healthy.
     */
    volatile uint32_t last_ok_ms;
} touch_state_t;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static touch_state_t s_touch;

static void IRAM_ATTR touch_isr(void *arg)
{
    TaskHandle_t task = (TaskHandle_t)arg;
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(task, &woken);
    if (woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void publish(const touch_point_t *pts, int count)
{
    touch_event_t evts[TOUCH_MAX_POINTS * 2];
    int n = touch_tracker_update(&s_touch.tracker, pts, count,
                                 evts, (int)(sizeof(evts) / sizeof(evts[0])));

    xSemaphoreTake(s_touch.lock, portMAX_DELAY);
    if (count > 0) {
        memcpy(s_touch.points, pts, (size_t)count * sizeof(touch_point_t));
    }
    s_touch.point_count = (uint8_t)count;
    xSemaphoreGive(s_touch.lock);

    if (s_touch.events) {
        for (int i = 0; i < n; ++i) {
            if (xQueueSend(s_touch.events, &evts[i], 0) != pdTRUE) {
                /* The consumer is behind: drop the oldest, keep the newest. */
                touch_event_t dropped;
                (void)xQueueReceive(s_touch.events, &dropped, 0);
                (void)xQueueSend(s_touch.events, &evts[i], 0);
            }
        }
    }
}

static void touch_task(void *arg)
{
    (void)arg;
    const TickType_t wait = pdMS_TO_TICKS(s_touch.cfg.poll_interval_ms ?: 10);

    while (!s_touch.stop) {
        if (s_touch.cfg.use_interrupt) {
            /* The timeout doubles as the polling fallback, so a controller
             * whose INT polarity does not match still works. */
            ulTaskNotifyTake(pdTRUE, wait);
        } else {
            vTaskDelay(wait);
        }

        touch_point_t raw[TOUCH_MAX_POINTS];
        int count = 0;
        bool fresh = false;
        esp_err_t err = gt911_read(s_touch.gt911, raw, TOUCH_MAX_POINTS,
                                   &count, &fresh);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "read failed: %s", esp_err_to_name(err));
            continue;
        }
        s_touch.last_ok_ms = now_ms();
        if (!fresh) {
            continue;
        }
        for (int i = 0; i < count; ++i) {
            touch_map_point(&s_touch.map, &raw[i]);
        }
        publish(raw, count);
    }

    s_touch.task = NULL;
    vTaskDelete(NULL);
}

esp_err_t touch_init(const touch_config_t *cfg)
{
    if (s_touch.initialised) {
        return ESP_OK;
    }

    s_touch.cfg = TOUCH_CONFIG_DEFAULT();
    if (cfg) {
        s_touch.cfg = *cfg;
    }
    if (s_touch.cfg.poll_interval_ms == 0) {
        s_touch.cfg.poll_interval_ms = 10;
    }

    ESP_RETURN_ON_ERROR(board_init(), TAG, "board init");
    ESP_RETURN_ON_ERROR(board_touch_reset_sequence(), TAG, "touch reset");

    const gt911_config_t gt_cfg = { 0 }; /* probe 0x5D then 0x14 */
    ESP_RETURN_ON_ERROR(gt911_new(board_i2c_bus(), &gt_cfg, &s_touch.gt911),
                        TAG, "gt911 probe");

    uint16_t x_max = 0, y_max = 0;
    if (gt911_resolution(s_touch.gt911, &x_max, &y_max) != ESP_OK ||
        x_max == 0 || y_max == 0) {
        x_max = BOARD_LCD_H_RES;
        y_max = BOARD_LCD_V_RES;
    }

    s_touch.map = (touch_map_t) {
        .src_w = (int16_t)x_max,
        .src_h = (int16_t)y_max,
        .mirror_x = s_touch.cfg.mirror_x,
        .mirror_y = s_touch.cfg.mirror_y,
        .rotation = s_touch.cfg.rotation,
    };
    touch_tracker_reset(&s_touch.tracker);
    s_touch.point_count = 0;
    s_touch.stop = false;

    s_touch.lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_touch.lock, ESP_ERR_NO_MEM, TAG, "mutex");

    if (s_touch.cfg.event_queue_len > 0) {
        s_touch.events = xQueueCreate(s_touch.cfg.event_queue_len,
                                      sizeof(touch_event_t));
        ESP_RETURN_ON_FALSE(s_touch.events, ESP_ERR_NO_MEM, TAG, "event queue");
    }

    BaseType_t ok;
    if (s_touch.cfg.task_core >= 0) {
        ok = xTaskCreatePinnedToCore(touch_task, "touch", 4096, NULL,
                                     s_touch.cfg.task_priority, &s_touch.task,
                                     s_touch.cfg.task_core);
    } else {
        ok = xTaskCreate(touch_task, "touch", 4096, NULL,
                         s_touch.cfg.task_priority, &s_touch.task);
    }
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "touch task");

    if (s_touch.cfg.use_interrupt) {
        const gpio_config_t int_cfg = {
            .pin_bit_mask = 1ULL << BOARD_TOUCH_PIN_INT,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_NEGEDGE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&int_cfg), TAG, "int pin");
        esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_RETURN_ON_ERROR(err, TAG, "isr service");
        }
        ESP_RETURN_ON_ERROR(
            gpio_isr_handler_add(BOARD_TOUCH_PIN_INT, touch_isr, s_touch.task),
            TAG, "isr handler");
    }

    /* Start the health clock here, so a stack that has just come up is not
     * momentarily reported as stale before its first poll. */
    s_touch.last_ok_ms = now_ms();
    s_touch.initialised = true;
    ESP_LOGI(TAG, "GT911 ready at 0x%02x, %ux%u, rotation %d%s",
             gt911_address(s_touch.gt911), x_max, y_max,
             (int)s_touch.cfg.rotation * 90,
             s_touch.cfg.use_interrupt ? ", INT enabled" : "");
    return ESP_OK;
}

esp_err_t touch_deinit(void)
{
    if (!s_touch.initialised) {
        return ESP_OK;
    }
    if (s_touch.cfg.use_interrupt) {
        gpio_isr_handler_remove(BOARD_TOUCH_PIN_INT);
    }
    s_touch.stop = true;
    if (s_touch.task) {
        xTaskNotifyGive(s_touch.task);
    }
    for (int i = 0; i < 100 && s_touch.task; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    gt911_del(s_touch.gt911);
    if (s_touch.events) {
        vQueueDelete(s_touch.events);
    }
    if (s_touch.lock) {
        vSemaphoreDelete(s_touch.lock);
    }
    memset(&s_touch, 0, sizeof(s_touch));
    return ESP_OK;
}

int touch_snapshot(touch_point_t *out, int max)
{
    if (!s_touch.initialised) {
        return 0;
    }
    xSemaphoreTake(s_touch.lock, portMAX_DELAY);
    int count = s_touch.point_count;
    int written = (count < max) ? count : max;
    if (out && written > 0) {
        memcpy(out, s_touch.points, (size_t)written * sizeof(touch_point_t));
    }
    xSemaphoreGive(s_touch.lock);
    return count;
}

bool touch_pressed(touch_point_t *out)
{
    touch_point_t p;
    int n = touch_snapshot(&p, 1);
    if (n > 0 && out) {
        *out = p;
    }
    return n > 0;
}

bool touch_wait_event(touch_event_t *out, uint32_t timeout_ms)
{
    if (!s_touch.events || !out) {
        return false;
    }
    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY
                                                  : pdMS_TO_TICKS(timeout_ms);
    return xQueueReceive(s_touch.events, out, ticks) == pdTRUE;
}

void touch_flush_events(void)
{
    if (s_touch.events) {
        xQueueReset(s_touch.events);
    }
}

uint16_t touch_i2c_address(void)
{
    return s_touch.gt911 ? gt911_address(s_touch.gt911) : 0;
}

uint32_t touch_age_ms(void)
{
    if (!s_touch.initialised) {
        return UINT32_MAX;
    }
    return now_ms() - s_touch.last_ok_ms;
}
