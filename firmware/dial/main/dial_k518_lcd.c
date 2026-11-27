/**
 * @file
 * @brief The Guition JC3636K518C's ST77916 round display over QSPI (quad
 *        SPI) through esp_lcd, its backlight, and its boot face.
 *
 * The panel takes the vendor's set-up (dial_k518_init.h) through the
 * espressif/esp_lcd_st77916 component: QSPI mode 0 at 40MHz, 32-bit
 * commands and 8-bit parameters, RGB565 with the high byte first.  The
 * frame is 360 x 360 RGB565 pixels, 259,200 bytes in PSRAM; each strip of
 * 20 rows is copied with its bytes swapped into a DMA-capable (direct
 * memory access) bounce buffer of 14,400 bytes in internal RAM before it is
 * sent.  A whole frame takes about 13ms on the wire at 40MHz over four
 * lines (arithmetic, not measured).  The vendor's demo clocks the panel at
 * 80MHz; 40MHz is the component's default.
 *
 * The display shows the boot face (shared/dial, dk518_face.h): the BLE name
 * and the firmware version.  The M5Stack Dial's 240 x 240 faces are not
 * shown: dial_lcd_flush() and dial_lcd_flush_rect() take them as the face
 * task's tick, draw the boot face again when the BLE name changed, and send
 * it whole.  A frame that did not go whole is drawn and sent again at the
 * next call; a strip that timed out is waited for before the bounce buffer
 * is written again.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "dial.h"
#include "dial_k518_init.h"
#include "dk518_face.h"

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st77916.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/** @brief The SPI host, its clock and the rows of one bounce buffer. @{ */
#define LCD_HOST    SPI2_HOST
#define LCD_PCLK_HZ (40u * 1000u * 1000u) /**< the QSPI clock, Hz */
#define LCD_STRIP   20 /**< rows a bounce buffer holds */
/** @} */
/** @brief The backlight's LEDC timer and channel. @{ */
#define BL_TIMER   LEDC_TIMER_1
#define BL_CHANNEL LEDC_CHANNEL_1 /**< the LEDC channel */
/** @} */
/** @brief How long one strip may take on the wire, ms: 14,400 bytes take
 *         about 1ms at 40MHz over four lines. */
#define LCD_STRIP_TIMEOUT_MS 50u

/** @brief The log's tag. */
static const char *TAG = "lcd";
/** @brief The panel, its IO, the frame and the bounce buffer. @{ */
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io; /**< the panel IO */
static gfx_color_t *s_fb;              /**< the frame, in PSRAM */
static uint16_t *s_strip;              /**< the DMA bounce buffer */
/** @} */
/** @brief Given when a strip's transfer ends. */
static SemaphoreHandle_t s_done;
/** @brief A strip's transfer was queued and its end not yet taken: the
 *         DMA may still read s_strip. */
static bool s_busy;
/** @brief How long a frame waits for a strip left from the frame before,
 *         ms, before it gives up. */
#define LCD_LATE_MS 200u
/** @brief The BLE name the boot face shows; "" before the first draw. */
static char s_shown[DK_NAME_MAX + 1u];

/**
 * @brief esp_lcd's callback at the end of a colour transfer, in the SPI
 *        interrupt.
 * @param io    the IO
 * @param edata unused
 * @param ctx   unused
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

/** @brief Send the whole frame, a strip at a time; a strip that does not
 *         end in LCD_STRIP_TIMEOUT_MS ends the frame.
 *  @return true when every strip went */
static bool send_frame(void)
{
    /* A strip of the frame before that timed out may still be on the wire:
     * s_strip is not written before its end is taken, and a late end taken
     * here cannot count as the end of a strip of this frame. */
    if (s_busy) {
        if (xSemaphoreTake(s_done, pdMS_TO_TICKS(LCD_LATE_MS)) != pdTRUE) {
            ESP_LOGW(TAG, "the strip before has not gone: frame skipped");
            return false;
        }
        s_busy = false;
    }
    for (int y = 0; y < DK518_SIZE; y += LCD_STRIP) {
        const int rows = DK518_SIZE - y < LCD_STRIP ? DK518_SIZE - y
                                                    : LCD_STRIP;
        const gfx_color_t *src = s_fb + (size_t)y * DK518_SIZE;
        for (int i = 0; i < rows * DK518_SIZE; ++i) {
            s_strip[i] = (uint16_t)((src[i] << 8) | (src[i] >> 8));
        }
        if (esp_lcd_panel_draw_bitmap(s_panel, 0, y, DK518_SIZE, y + rows,
                                      s_strip) != ESP_OK) {
            ESP_LOGW(TAG, "a strip at row %d was refused", y);
            return false;
        }
        s_busy = true;
        if (xSemaphoreTake(s_done, pdMS_TO_TICKS(LCD_STRIP_TIMEOUT_MS))
            != pdTRUE) {
            ESP_LOGW(TAG, "a strip at row %d did not go", y);
            return false;   /* s_busy stays: the next frame waits for it */
        }
        s_busy = false;
    }
    return true;
}

/** @brief Draw the boot face and send it when the BLE name differs from
 *         the one of the last frame that went whole: a frame that failed
 *         goes again at the next call. */
static void boot_show(void)
{
    char name[sizeof s_shown];
    if (s_panel == NULL) {
        return;
    }
    strncpy(name, dial_ble_name(), sizeof name - 1u);
    name[sizeof name - 1u] = '\0';
    if (strcmp(name, s_shown) == 0) {
        return;
    }
    gfx_canvas_t c;
    gfx_canvas_init(&c, s_fb, DK518_SIZE, DK518_SIZE, 0);
    const dk518_boot_t b = { name, esp_app_get_description()->version };
    if (dk518_boot_draw(&c, &b) && send_frame()) {
        memcpy(s_shown, name, sizeof s_shown);
    }
}

bool dial_lcd_init(void)
{
    const size_t strip_bytes = (size_t)DK518_SIZE * LCD_STRIP * 2u;
    const spi_bus_config_t bus = ST77916_PANEL_BUS_QSPI_CONFIG(
        DIAL_PIN_LCD_SCLK, DIAL_PIN_LCD_D0, DIAL_PIN_LCD_D1, DIAL_PIN_LCD_D2,
        DIAL_PIN_LCD_D3, (int)strip_bytes);
    esp_lcd_panel_io_spi_config_t io =
        ST77916_PANEL_IO_QSPI_CONFIG(DIAL_PIN_LCD_CS, strip_done, NULL);
    io.pclk_hz = LCD_PCLK_HZ;
    st77916_vendor_config_t vendor = {
        .init_cmds = k_k518_lcd_init,
        .init_cmds_size = sizeof k_k518_lcd_init / sizeof k_k518_lcd_init[0],
    };
    vendor.flags.use_qspi_interface = 1;
    const esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = DIAL_PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };

    backlight_init();
    s_done = xSemaphoreCreateBinary();
    s_fb = heap_caps_malloc((size_t)DK518_SIZE * DK518_SIZE
                                * sizeof(gfx_color_t), MALLOC_CAP_SPIRAM);
    s_strip = heap_caps_malloc(strip_bytes,
                               MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (s_done == NULL || s_fb == NULL || s_strip == NULL) {
        ESP_LOGE(TAG, "no memory for the frame (PSRAM) or the strip");
        return false;
    }
    if (spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK
        || esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io,
                                    &s_io) != ESP_OK
        || esp_lcd_new_panel_st77916(s_io, &dev, &s_panel) != ESP_OK) {
        ESP_LOGE(TAG, "the display's set-up failed");
        s_panel = NULL;
        return false;
    }
    if (esp_lcd_panel_reset(s_panel) != ESP_OK
        || esp_lcd_panel_init(s_panel) != ESP_OK
        || esp_lcd_panel_disp_on_off(s_panel, true) != ESP_OK) {
        ESP_LOGE(TAG, "the ST77916 did not take its set-up");
        s_panel = NULL;
        return false;
    }
    boot_show();
    return true;
}

void dial_lcd_flush_rect(const uint16_t *fb, int x, int y, int w, int h)
{
    (void)fb;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    boot_show();
}

void dial_lcd_flush(const uint16_t *fb, int y0, int y1)
{
    (void)fb;
    (void)y0;
    (void)y1;
    boot_show();
}
