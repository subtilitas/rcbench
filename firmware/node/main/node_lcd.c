/**
 * @file node_lcd.c
 * @brief The ES3C28P's ILI9341V display over SPI (serial peripheral
 *        interface) through esp_lcd's panel IO, and its backlight.
 *
 * The set-up is the vendor's (ILI9341V_Init.txt, lcdwiki.com): power and
 * gamma registers as given there, colour inversion on (0x21), RGB565
 * (0x3A = 0x55), BGR element order, and the memory access control of the
 * vendor's landscape direction 1 (0x36 = 0x68: MX, MV, BGR).  Which edge of
 * the board is the screen's top in that direction is unconfirmed;
 * NODE_LCD_TURN 1 (node_board.h) turns the screen by 180° (0x36 = 0xA8,
 * the vendor's direction 3) and the touch with it.
 *
 * The panel's reset is the chip's CHIP_PU: a software reset (0x01) starts
 * the set-up.  SPI2 runs at 20MHz, mode 0, the clock of ESP-IDF's ILI9341
 * example; the board's own limit is not measured.  The frame is RGB565 in
 * the CPU's byte order; each strip is copied with its bytes swapped into a
 * DMA-capable (direct memory access) bounce buffer of 6,400 pixels (20
 * rows of 320) before it is sent.  The window's commands before each strip
 * wait for the strip before it to leave (esp_lcd's tx_param waits for the
 * queued transfers), so the buffer is filled only once its last transfer
 * ended.  A full frame is 153,600 bytes: 61ms on the wire at 20MHz
 * (arithmetic, not measured).
 *
 * SPDX-License-Identifier: MIT
 */
#include "node.h"

#if NODE_BOARD_DISPLAY

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "node_view.h"

/** @brief The bus, its clock and the bounce buffer. @{ */
#define LCD_HOST     SPI2_HOST
#define LCD_PCLK_HZ  (20u * 1000u * 1000u) /**< the SPI clock, Hz */
#define LCD_STRIP    20                     /**< rows a bounce buffer holds */
/** @} */

/** @brief The memory access control: MX, MV and BGR, or MY, MV and BGR
 *         turned. */
#define LCD_MADCTL (NODE_LCD_TURN ? 0xA8u : 0x68u)

/** @brief One set-up command: its bytes and the wait after it. */
typedef struct {
    uint8_t cmd;       /**< the command */
    uint8_t len;       /**< its parameter bytes */
    uint8_t ms;        /**< the wait after it, ms */
    uint8_t data[15];  /**< the parameters */
} lcd_cmd_t;

/** @brief The vendor's set-up, ILI9341V_Init.txt, in its order. */
static const lcd_cmd_t k_init[] = {
    { 0x01u, 0u, 150u, { 0 } },                          /* software reset */
    { 0xCFu, 3u, 0u, { 0x00u, 0xC1u, 0x30u } },
    { 0xEDu, 4u, 0u, { 0x64u, 0x03u, 0x12u, 0x81u } },
    { 0xE8u, 3u, 0u, { 0x85u, 0x00u, 0x78u } },
    { 0xCBu, 5u, 0u, { 0x39u, 0x2Cu, 0x00u, 0x34u, 0x02u } },
    { 0xF7u, 1u, 0u, { 0x20u } },
    { 0xEAu, 2u, 0u, { 0x00u, 0x00u } },
    { 0xC0u, 1u, 0u, { 0x13u } },                        /* power control 1 */
    { 0xC1u, 1u, 0u, { 0x13u } },                        /* power control 2 */
    { 0xC5u, 2u, 0u, { 0x22u, 0x35u } },                 /* VCOM control 1 */
    { 0xC7u, 1u, 0u, { 0xBDu } },                        /* VCOM control 2 */
    { 0x21u, 0u, 0u, { 0 } },                            /* inversion on */
    { 0x36u, 1u, 0u, { LCD_MADCTL } },                   /* memory access */
    { 0xB6u, 2u, 0u, { 0x0Au, 0xA2u } },
    { 0x3Au, 1u, 0u, { 0x55u } },                        /* RGB565 */
    { 0xF6u, 2u, 0u, { 0x01u, 0x30u } },
    { 0xB1u, 2u, 0u, { 0x00u, 0x1Bu } },                 /* frame rate */
    { 0xF2u, 1u, 0u, { 0x00u } },                        /* 3 gamma off */
    { 0x26u, 1u, 0u, { 0x01u } },                        /* gamma curve 1 */
    { 0xE0u, 15u, 0u, { 0x0Fu, 0x35u, 0x31u, 0x0Bu, 0x0Eu, 0x06u, 0x49u,
                        0xA7u, 0x33u, 0x07u, 0x0Fu, 0x03u, 0x0Cu, 0x0Au,
                        0x00u } },
    { 0xE1u, 15u, 0u, { 0x00u, 0x0Au, 0x0Fu, 0x04u, 0x11u, 0x08u, 0x36u,
                        0x58u, 0x4Du, 0x07u, 0x10u, 0x0Cu, 0x32u, 0x34u,
                        0x0Fu } },
    { 0x11u, 0u, 120u, { 0 } },                          /* sleep out */
    { 0x29u, 0u, 0u, { 0 } },                            /* display on */
};

/** @brief The log's tag. */
static const char *TAG = "lcd";
/** @brief The panel IO and the bounce buffer. @{ */
static esp_lcd_panel_io_handle_t s_io;
static uint16_t *s_strip; /**< the DMA bounce buffer */
/** @} */

/**
 * @brief Set the window a following memory write fills.  Waits for the
 *        colour transfers queued before it.
 * @param x0 The first column.
 * @param y0 The first row.
 * @param x1 The column after the last.
 * @param y1 The row after the last.
 * @return true when both commands went.
 */
static bool set_window(int x0, int y0, int x1, int y1)
{
    const uint8_t col[4] = { (uint8_t)(x0 >> 8), (uint8_t)x0,
                             (uint8_t)((x1 - 1) >> 8), (uint8_t)(x1 - 1) };
    const uint8_t row[4] = { (uint8_t)(y0 >> 8), (uint8_t)y0,
                             (uint8_t)((y1 - 1) >> 8), (uint8_t)(y1 - 1) };
    return esp_lcd_panel_io_tx_param(s_io, 0x2A, col, sizeof col) == ESP_OK
           && esp_lcd_panel_io_tx_param(s_io, 0x2B, row, sizeof row) == ESP_OK;
}

bool node_lcd_init(void)
{
    const size_t strip_bytes = (size_t)NV_W * LCD_STRIP * 2u;
    const gpio_config_t bl = {
        .pin_bit_mask = 1ull << NODE_PIN_LCD_BL,
        .mode = GPIO_MODE_OUTPUT,
    };
    (void)gpio_config(&bl);
    (void)gpio_set_level(NODE_PIN_LCD_BL, 0);
    const spi_bus_config_t bus = {
        .sclk_io_num = NODE_PIN_LCD_SCLK,
        .mosi_io_num = NODE_PIN_LCD_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)strip_bytes,
    };
    const esp_lcd_panel_io_spi_config_t io = {
        .cs_gpio_num = NODE_PIN_LCD_CS,
        .dc_gpio_num = NODE_PIN_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_PCLK_HZ,
        .trans_queue_depth = 4,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    s_strip = heap_caps_malloc(strip_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (s_strip == NULL
        || spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK
        || esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io,
                                    &s_io) != ESP_OK) {
        ESP_LOGE(TAG, "the display's SPI set-up failed");
        s_io = NULL;
        return false;
    }
    for (size_t k = 0; k < sizeof k_init / sizeof k_init[0]; ++k) {
        const lcd_cmd_t *c = &k_init[k];
        if (esp_lcd_panel_io_tx_param(s_io, c->cmd, c->len > 0u ? c->data : NULL,
                                      c->len) != ESP_OK) {
            ESP_LOGE(TAG, "the ILI9341V refused command 0x%02X", (unsigned)c->cmd);
            s_io = NULL;
            return false;
        }
        if (c->ms > 0u) {
            vTaskDelay(pdMS_TO_TICKS(c->ms));
        }
    }
    return true;
}

void node_lcd_backlight(bool on)
{
    (void)gpio_set_level(NODE_PIN_LCD_BL, on ? 1u : 0u);
}

void node_lcd_flush_rect(const uint16_t *fb, int x, int y, int w, int h)
{
    if (s_io == NULL || fb == NULL) {
        return;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > NV_W) {
        w = NV_W - x;
    }
    if (y + h > NV_H) {
        h = NV_H - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    const int per = (NV_W * LCD_STRIP) / w;
    for (int row = y; row < y + h; row += per) {
        const int rows = (y + h - row < per) ? y + h - row : per;
        /* The window first: it waits for the strip before to leave. */
        if (!set_window(x, row, x + w, row + rows)) {
            ESP_LOGW(TAG, "the window at row %d was refused", row);
            return;
        }
        uint16_t *dst = s_strip;
        for (int r = 0; r < rows; ++r) {
            const uint16_t *src = fb + (size_t)(row + r) * NV_W + (size_t)x;
            for (int i = 0; i < w; ++i) {
                *dst++ = (uint16_t)((src[i] >> 8) | (src[i] << 8));
            }
        }
        if (esp_lcd_panel_io_tx_color(s_io, 0x2C, s_strip,
                                      (size_t)w * (size_t)rows * 2u) != ESP_OK) {
            ESP_LOGW(TAG, "a strip at row %d was refused", row);
            return;
        }
    }
}

#endif /* NODE_BOARD_DISPLAY */
