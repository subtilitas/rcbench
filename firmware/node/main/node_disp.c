/**
 * @file node_disp.c
 * @brief The display node's screen and touch on the ES3C28P: the `disp`
 *        task that reads the FT6336G, keeps the values (node_view.h) and
 *        draws what changed.
 *
 * The `pwr` task hands every closed window to node_disp_window(); the
 * values sit behind a spinlock, as `pwr` runs every 1130µs.  `disp` runs
 * every 20ms: one touch read, the tap and the hold, and every NV_FRAME_MS
 * (200ms) a frame of the values.  A tap, a hold and the hold's bar draw at
 * once.  shared/gfx draws into a frame of 153,600 bytes in the PSRAM
 * (pseudo-static RAM): the internal RAM's 245KB free at link time are
 * Bluetooth LE's and the tasks'.  A gfx_damage record collects the
 * rectangles the drawing wrote, and only those go to the display
 * (node_lcd.c).  Without PSRAM the node runs without a display.
 *
 * The FT6336G sits on the INA228's bus (node_pwr_bus()), at 400kHz, so a
 * touch read of 5 bytes (about 170µs on the wire) can hold a sample of
 * `pwr` back by as long.  The controller's registers are the FT5x06
 * family's: 0x02 the number of touch points (low 4 bits), then the first
 * point's X high (low 4 bits), X low, Y high, Y low.  Its interrupt line is
 * not used: the read is polled.
 *
 * The screen's texts are English unless a linked panel wrote LANG German
 * (node_disp_lang()); without a link they are English again.
 *
 * SPDX-License-Identifier: MIT
 */
#include "node.h"

#if NODE_BOARD_DISPLAY

#include <stdatomic.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gfx_damage.h"

#include "node_view.h"

/** @brief The task's period, ms, and its stack and priority. @{ */
#define DISP_TICK_MS  20u
#define DISP_STACK  4096u   /**< bytes */
#define DISP_PRIO      2    /**< under `pwr` and the BLE tasks */
/** @} */
/** @brief The touch controller's bus clock and one read's timeout. @{ */
#define TP_HZ         400000u
#define TP_TIMEOUT_MS 5
#define TP_REG_STATUS 0x02u  /**< TD_STATUS, the first register a read covers */
/** @} */
/** @brief The controller's reset: low this long, then this long before the
 *         first read (FT6336G datasheet: 5ms and 300ms at least). @{ */
#define TP_RST_LOW_MS  10u
#define TP_RST_WAIT_MS 300u
/** @} */

/** @brief The log's tag. */
static const char *TAG = "disp";

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;  /**< guards s_stats */
static nv_stats_t   s_stats;          /**< the values the windows give */
static atomic_int   s_lang;           /**< RCNODE_LANG_* from the last LANG */

static i2c_master_dev_handle_t s_tp;  /**< the FT6336G, NULL without one */
static gfx_color_t  *s_fb;            /**< the frame */
static gfx_canvas_t  s_canvas;        /**< the canvas over it */
static gfx_damage_t  s_damage;        /**< what the drawing wrote */
static nv_view_t     s_view;          /**< what the frame shows */
static nv_touch_t    s_touch;         /**< the press */
static nv_frame_t    s_frame;         /**< the last frame's values */

void node_disp_window(const rcnode_pwr_win_t *w)
{
    taskENTER_CRITICAL(&s_mux);
    nv_stats_window(&s_stats, w);
    taskEXIT_CRITICAL(&s_mux);
}

void node_disp_lang(uint8_t lang)
{
    atomic_store(&s_lang, lang == RCNODE_LANG_DE ? RCNODE_LANG_DE
                                                 : RCNODE_LANG_EN);
}

/**
 * @brief The FT6336G: its reset, then its device on the INA228's bus.
 * @return true when it answered.
 */
static bool touch_init(void)
{
    const gpio_config_t rst = {
        .pin_bit_mask = 1ull << NODE_PIN_TP_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    (void)gpio_config(&rst);
    (void)gpio_set_level(NODE_PIN_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(TP_RST_LOW_MS));
    (void)gpio_set_level(NODE_PIN_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(TP_RST_WAIT_MS));
    i2c_master_bus_handle_t bus = node_pwr_bus();
    if (bus == NULL) {
        ESP_LOGE(TAG, "touch: no I2C bus");
        return false;
    }
    const i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = NODE_TP_ADDR,
        .scl_speed_hz = TP_HZ,
    };
    if (i2c_master_bus_add_device(bus, &dev, &s_tp) != ESP_OK) {
        s_tp = NULL;
        ESP_LOGE(TAG, "touch: device not added");
        return false;
    }
    if (i2c_master_probe(bus, NODE_TP_ADDR, TP_TIMEOUT_MS) != ESP_OK) {
        ESP_LOGW(TAG, "touch: no answer at 0x%02X", (unsigned)NODE_TP_ADDR);
    }
    return true;
}

/**
 * @brief One read of the first touch point.
 * @param x Receives the screen's x.
 * @param y Receives the screen's y.
 * @return true when a finger is on the screen.
 */
static bool touch_read(int *x, int *y)
{
    const uint8_t reg = TP_REG_STATUS;
    uint8_t d[5];
    if (s_tp == NULL
        || i2c_master_transmit_receive(s_tp, &reg, 1, d, sizeof d,
                                       TP_TIMEOUT_MS) != ESP_OK) {
        return false;
    }
    if ((d[0] & 0x0Fu) == 0u || (d[0] & 0x0Fu) > 2u) {
        return false;   /* no point, or a count the part does not give */
    }
    const int tx = (int)(((unsigned)(d[1] & 0x0Fu) << 8) | d[2]);
    const int ty = (int)(((unsigned)(d[3] & 0x0Fu) << 8) | d[4]);
    nv_touch_map(tx, ty, x, y);
    if (NODE_LCD_TURN) {
        *x = NV_W - 1 - *x;
        *y = NV_H - 1 - *y;
    }
    return true;
}

/**
 * @brief Act on a touch event: a tap on a row's highest and lowest resets
 *        that row's, the hold resets all.
 * @param ev The event.
 * @return true when the values changed.
 */
static bool touch_act(nv_touch_ev_t ev)
{
    int q = -1;
    if (ev == NV_TOUCH_HOLD) {
        q = NV_Q_N;
    } else if (ev == NV_TOUCH_TAP) {
        q = nv_hit(s_touch.x, s_touch.y);
        ESP_LOGI(TAG, "tap at %d,%d: %s", (int)s_touch.x, (int)s_touch.y,
                 q < 0 ? "no target" : "highest and lowest reset");
    }
    if (q < 0) {
        return false;
    }
    if (q == NV_Q_N) {
        ESP_LOGI(TAG, "hold: every highest and lowest reset");
    }
    taskENTER_CRITICAL(&s_mux);
    nv_stats_reset(&s_stats, q);
    for (int k = 0; k < NV_Q_N; ++k) {
        s_frame.ext[k] = s_stats.ext[k];
    }
    taskEXIT_CRITICAL(&s_mux);
    return true;
}

/**
 * @brief Draw the model and send what the drawing wrote.
 * @param hold_pm The hold's progress, per mille.
 */
static void show(uint16_t hold_pm)
{
    nv_model_t m;
    memset(&m, 0, sizeof m);
    m.linked = node_ble_linked() && node_ble_subscribed();
    m.lang = m.linked ? (uint8_t)atomic_load(&s_lang) : RCNODE_LANG_EN;
    m.f = s_frame;
    m.hold_pm = hold_pm;
    gfx_rect_t box[GFX_DAMAGE_MAX_RECTS];
    gfx_damage_reset(&s_damage);
    gfx_damage_attach(&s_damage);
    (void)nv_render(&s_view, &s_canvas, &m);
    gfx_damage_attach(NULL);
    const int n = gfx_damage_rects(&s_damage, box, GFX_DAMAGE_MAX_RECTS);
    for (int i = 0; i < n; ++i) {
        node_lcd_flush_rect(s_fb, box[i].x, box[i].y, box[i].w, box[i].h);
    }
}

/**
 * @brief `disp`: the display's set-up, then the touch and the frames.
 * @param arg Unused.
 */
static void disp_task(void *arg)
{
    (void)arg;
    s_fb = heap_caps_malloc((size_t)NV_W * NV_H * sizeof(gfx_color_t),
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_fb == NULL || !node_lcd_init()) {
        ESP_LOGE(TAG, "no display: %s", s_fb == NULL ? "no PSRAM for the frame"
                                                     : "the set-up failed");
        vTaskDelete(NULL);
        return;
    }
    gfx_canvas_init(&s_canvas, s_fb, NV_W, NV_H, 0);
    gfx_damage_init(&s_damage, &s_canvas);
    nv_view_init(&s_view);
    show(0u);
    node_lcd_backlight(true);
    if (!touch_init()) {
        ESP_LOGW(TAG, "no touch: the values show, the resets do not work");
    }
    TickType_t wake = xTaskGetTickCount();
    uint32_t next_frame = 0u;
    uint16_t shown_pm = 0u;
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(DISP_TICK_MS));
        const uint32_t now = (uint32_t)(node_us() / 1000u);
        int x = 0;
        int y = 0;
        const bool down = touch_read(&x, &y);
        const bool acted = touch_act(nv_touch_step(&s_touch, down, x, y, now));
        const uint16_t pm = nv_touch_progress(&s_touch, now);
        const bool due = (int32_t)(now - next_frame) >= 0;
        if (due) {
            next_frame = now + NV_FRAME_MS;
            taskENTER_CRITICAL(&s_mux);
            nv_stats_frame(&s_stats, &s_frame);
            taskEXIT_CRITICAL(&s_mux);
        }
        if (due || acted || pm != shown_pm) {
            shown_pm = pm;
            show(pm);
        }
    }
}

void node_disp_start(void)
{
    nv_stats_init(&s_stats);
    if (xTaskCreate(disp_task, "disp", DISP_STACK, NULL, DISP_PRIO, NULL)
        != pdPASS) {
        ESP_LOGE(TAG, "the display task did not start");
    }
}

#endif /* NODE_BOARD_DISPLAY */
