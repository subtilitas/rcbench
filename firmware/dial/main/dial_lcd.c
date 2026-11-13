/**
 * @file
 * @brief The GC9A01 round display over SPI (serial peripheral interface)
 *        through esp_lcd, and its backlight.
 *
 * Settings as the board support package for this device has them: SPI at
 * 40MHz, mode 0, 8-bit commands and parameters, BGR element order, colour
 * inversion on, the X axis mirrored.  The frame is RGB565 in the CPU's
 * byte order; the panel takes the high byte first, so each strip is copied
 * with its bytes swapped into a DMA-capable (direct memory access) bounce
 * buffer of 20 rows before it is sent.  A full frame is 115,200 bytes:
 * about 23ms on the wire at 40MHz (arithmetic, not measured).
 *
 * SPDX-License-Identifier: MIT
 */
#include "dial.h"

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/** @brief The SPI host, its clock and the rows of one bounce buffer. @{ */
#define LCD_HOST     SPI2_HOST
#define LCD_PCLK_HZ  (40u * 1000u * 1000u) /**< the SPI clock, Hz */
#define LCD_STRIP    20 /**< rows a bounce buffer holds */
/** @} */
/** @brief The backlight's LEDC timer and channel; the buzzer has 0. @{ */
#define BL_TIMER   LEDC_TIMER_1
#define BL_CHANNEL LEDC_CHANNEL_1 /**< the LEDC channel */
/** @} */
/** @brief How long a strip may take on the wire before the flush gives up,
 *         ms: 9,600 bytes take about 2ms at 40MHz. */
#define LCD_STRIP_TIMEOUT_MS 50u

/** @brief The log's tag. */
static const char *TAG = "lcd";
/** @brief The panel, its IO and the bounce buffer. @{ */
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io; /**< the panel IO */
static uint16_t *s_strip; /**< the DMA bounce buffer */
/** @} */
/** @brief Given when a strip's transfer ends. */
static SemaphoreHandle_t s_done;
/** @brief A strip's transfer was queued and its end not yet taken: the
 *         DMA may still read s_strip. */
static bool s_busy;
/** @brief How long a flush waits for a transfer left from the flush
 *         before, ms, before it skips its frame. */
#define LCD_LATE_MS 200u

/**
 * @brief esp_lcd's callback at the end of a colour transfer, in the SPI
 *        interrupt.
 * @param io   the IO
 * @param edata unused
 * @param ctx  unused
 * @return whether a higher-priority task woke
 */
static bool strip_done(esp_lcd_panel_io_handle_t io,
                       esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    BaseType_t woke = pdFALSE;
    (void)io;
    (void)edata;
    (void)ctx;
    xSemaphoreGiveFromISR(s_done, &woke);
    return woke == pdTRUE;
}

/** @brief Set up the backlight's PWM at 5kHz, off. */
static void backlight_init(void)
{
    const ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BL_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t ch = {
        .gpio_num = DIAL_PIN_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BL_CHANNEL,
        .timer_sel = BL_TIMER,
        .duty = 0,
    };
    ledc_timer_config(&t);
    ledc_channel_config(&ch);
}

void dial_lcd_backlight(unsigned pct)
{
    if (pct > 100u) {
        pct = 100u;
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, 1023u * pct / 100u);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
}

bool dial_lcd_init(void)
{
    const size_t strip_bytes = (size_t)DIAL_LCD_SIZE * LCD_STRIP * 2u;
    const spi_bus_config_t bus = {
        .sclk_io_num = DIAL_PIN_LCD_SCLK,
        .mosi_io_num = DIAL_PIN_LCD_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)strip_bytes,
    };
    const esp_lcd_panel_io_spi_config_t io = {
        .cs_gpio_num = DIAL_PIN_LCD_CS,
        .dc_gpio_num = DIAL_PIN_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_PCLK_HZ,
        .trans_queue_depth = 4,
        .on_color_trans_done = strip_done,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    const esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = DIAL_PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };

    backlight_init();
    s_done = xSemaphoreCreateBinary();
    s_strip = heap_caps_malloc(strip_bytes,
                               MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (s_done == NULL || s_strip == NULL
        || spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK
        || esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io,
                                    &s_io) != ESP_OK
        || esp_lcd_new_panel_gc9a01(s_io, &dev, &s_panel) != ESP_OK) {
        ESP_LOGE(TAG, "the display's set-up failed");
        return false;
    }
    if (esp_lcd_panel_reset(s_panel) != ESP_OK
        || esp_lcd_panel_init(s_panel) != ESP_OK
        || esp_lcd_panel_invert_color(s_panel, true) != ESP_OK
        || esp_lcd_panel_mirror(s_panel, true, false) != ESP_OK
        || esp_lcd_panel_disp_on_off(s_panel, true) != ESP_OK) {
        ESP_LOGE(TAG, "the GC9A01 did not take its set-up");
        return false;
    }
    return true;
}

void dial_lcd_flush(const uint16_t *fb, int y0, int y1)
{
    if (s_panel == NULL || fb == NULL) {
        return;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 > DIAL_LCD_SIZE) {
        y1 = DIAL_LCD_SIZE;
    }
    /* A strip of the flush before that timed out may still be on the
     * wire: s_strip is not written before its end is taken.  A late end
     * taken here cannot then count as the end of a strip of this flush. */
    if (s_busy) {
        if (xSemaphoreTake(s_done, pdMS_TO_TICKS(LCD_LATE_MS)) != pdTRUE) {
            ESP_LOGW(TAG, "the strip before has not gone: frame skipped");
            return;
        }
        s_busy = false;
    }
    for (int y = y0; y < y1; y += LCD_STRIP) {
        const int rows = (y1 - y < LCD_STRIP) ? y1 - y : LCD_STRIP;
        const uint16_t *src = fb + (size_t)y * DIAL_LCD_SIZE;
        const size_t n = (size_t)rows * DIAL_LCD_SIZE;
        for (size_t i = 0; i < n; ++i) {
            s_strip[i] = (uint16_t)((src[i] >> 8) | (src[i] << 8));
        }
        if (esp_lcd_panel_draw_bitmap(s_panel, 0, y, DIAL_LCD_SIZE, y + rows,
                                      s_strip) != ESP_OK) {
            ESP_LOGW(TAG, "a strip at row %d was refused", y);
            return;
        }
        s_busy = true;
        if (xSemaphoreTake(s_done, pdMS_TO_TICKS(LCD_STRIP_TIMEOUT_MS))
            != pdTRUE) {
            ESP_LOGW(TAG, "a strip at row %d did not go", y);
            return;     /* s_busy stays: the next flush waits for it */
        }
        s_busy = false;
    }
}
