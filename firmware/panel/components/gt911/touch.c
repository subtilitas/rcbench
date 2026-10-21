/**
 * @file touch.c
 * @brief The GT911 polling task: reads contacts, maps them into the
 *        panel's coordinates, crops them to the UI's area and publishes
 *        snapshots and events.
 *
 * SPDX-License-Identifier: MIT
 */

#include "touch.h"

#include <stdatomic.h>
#include <string.h>

#include "board.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gt911.h"
#ifdef RCBENCH_TOUCH_INJECTOR
#include "touch_inject.h"
#include "touch_merge.h"
#endif

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
     * an aligned 32-bit load is atomic on the ESP32-P4, so the application
     * reads it from its own task without the lock, and unsigned subtraction
     * stays correct across the 49-day wrap.
     *
     * "Answered" means the I2C (Inter-Integrated Circuit) read succeeded,
     * not that a finger was down; an untouched panel is healthy.
     */
    volatile uint32_t last_ok_ms;

    /*
     * Every event offered to the queue is numbered, from 1, by this task --
     * the queue's one producer -- whether or not the queue keeps it.  The
     * consumer finds a dropped event as a gap in the numbers (touch_loss.h),
     * on the first event after it and before that event is handled, so no
     * count published on one core has to be read in time on the other.
     *
     * seq is this task's alone.  published is the number of the last event
     * offered, stored after the offer, for a loss at the end of the stream
     * with nothing after it to show the gap.
     */
    uint32_t    seq;
    atomic_uint published;
} touch_state_t;

/* What the queue carries: the event and its number. */
typedef struct {
    touch_event_t evt;
    uint32_t      seq;
} queued_t;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static touch_state_t s_touch;

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
            const queued_t q = { .evt = evts[i], .seq = ++s_touch.seq };
            if (xQueueSend(s_touch.events, &q, 0) != pdTRUE) {
                /*
                 * The consumer is behind: drop the oldest and keep the
                 * newest.  Nothing is counted here.  The dropped event's
                 * number is missing from what the consumer takes, and it
                 * sees the gap on the event after it.  The retry runs
                 * whether or not the receive took anything: the consumer
                 * can empty the queue between the two, and then there is
                 * room without anything having been evicted.
                 */
                queued_t dropped;
                (void)xQueueReceive(s_touch.events, &dropped, 0);
                (void)xQueueSend(s_touch.events, &q, 0);
            }
            atomic_store(&s_touch.published, q.seq);
        }
    }
}

/**
 * @brief Publish only the contacts inside the UI's area: the UI draws
 *        BOARD_UI_H_RES x BOARD_UI_V_RES of the panel.
 * @param pts Contacts in panel pixels, each source already mapped with its
 *            own map; cropped in place.
 * @param count How many.
 */
static void crop_and_publish(touch_point_t *pts, int count)
{
    publish(pts, touch_map_crop(pts, count, BOARD_UI_H_RES, BOARD_UI_V_RES));
}

static void touch_task(void *arg)
{
    (void)arg;
    const TickType_t wait = pdMS_TO_TICKS(s_touch.cfg.poll_interval_ms ?: 10);

    while (!s_touch.stop) {
        /* Polled: the GT911's INT line does not reach the SoC.  The wait
         * is a notification, so touch_deinit() ends it early. */
        (void)ulTaskNotifyTake(pdTRUE, wait);

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
        /* The glass's report into panel pixels before any merge: the
         * injector's contacts are panel pixels already (touch_inject.c). */
        if (fresh) {
            touch_map_points(&s_touch.map, raw, count);
        }
#ifdef RCBENCH_TOUCH_INJECTOR
        /* The glass's report goes out before the injector is read, so a
         * slow or failing injector never delays a contact on the glass. */
        if (touch_inject_glass(raw, &count, fresh)) {
            crop_and_publish(raw, count);
        }
        fresh = touch_inject_read(raw, &count, now_ms());
#endif
        if (!fresh) {
            continue;
        }
        crop_and_publish(raw, count);
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
    /*
     * The GT911 takes 0x5D or 0x14 at the end of its reset from the level
     * on its INT line, which no GPIO drives on this board; which one it
     * takes is not measured.
     */
    const uint16_t glass_addr = gt911_address(s_touch.gt911);
    ESP_LOGI(TAG, "GT911 answers at 0x%02x after the reset (0x5d probed "
                  "first, then 0x14)", glass_addr);

#ifdef RCBENCH_TOUCH_INJECTOR
    /* Before the task exists: adding a device to the bus is not safe
     * against a transaction on it. */
    touch_inject_attach(board_i2c_bus(), gt911_address(s_touch.gt911),
                        now_ms());
#endif

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
#ifdef RCBENCH_TOUCH_INJECTOR
    if (glass_addr == TOUCH_INJECT_ADDR) {
        /* 0x14 is the injector's address in this build, so what answers
         * there is taken as the injector and not as the glass: panel
         * pixels of the UI's area, unmirrored. */
        s_touch.map = touch_inject_map();
        ESP_LOGW(TAG, "0x14 refused as the glass: it is the touch "
                      "injector's address in this build; touch is the "
                      "injector alone");
    }
#endif
    touch_tracker_reset(&s_touch.tracker);
    s_touch.point_count = 0;
    s_touch.stop = false;

    s_touch.lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_touch.lock, ESP_ERR_NO_MEM, TAG, "mutex");

    if (s_touch.cfg.event_queue_len > 0) {
        s_touch.events = xQueueCreate(s_touch.cfg.event_queue_len,
                                      sizeof(queued_t));
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

    /* Start the health clock here, so a stack that has just come up is not
     * momentarily reported as stale before its first poll. */
    s_touch.last_ok_ms = now_ms();
    s_touch.initialised = true;
    ESP_LOGI(TAG, "GT911 ready at 0x%02x, %ux%u, mirror x %d y %d, "
                  "rotation %d, polled every %u ms",
             gt911_address(s_touch.gt911), x_max, y_max,
             (int)s_touch.cfg.mirror_x, (int)s_touch.cfg.mirror_y,
             (int)s_touch.cfg.rotation * 90,
             (unsigned)s_touch.cfg.poll_interval_ms);
    return ESP_OK;
}

esp_err_t touch_deinit(void)
{
    if (!s_touch.initialised) {
        return ESP_OK;
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

bool touch_wait_event(touch_event_t *out, uint32_t *seq, uint32_t timeout_ms)
{
    if (!s_touch.events || !out) {
        return false;
    }
    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY
                                                  : pdMS_TO_TICKS(timeout_ms);
    queued_t q;
    if (xQueueReceive(s_touch.events, &q, ticks) != pdTRUE) {
        return false;
    }
    *out = q.evt;
    if (seq != NULL) {
        *seq = q.seq;
    }
    return true;
}

uint32_t touch_published(void)
{
    return atomic_load(&s_touch.published);
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
#ifdef RCBENCH_TOUCH_INJECTOR
    /* An injector that stops answering is touch that stops answering,
     * until it is detached 1000 ms after its last answer. */
    const uint32_t now = now_ms();
    return touch_merge_age(now - s_touch.last_ok_ms, touch_inject_age_ms(now));
#else
    return now_ms() - s_touch.last_ok_ms;
#endif
}


