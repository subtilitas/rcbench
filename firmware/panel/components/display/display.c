/*
 * The EK79007 panel on the ESP32-P4's DSI host, written from the EK79007
 * datasheet and ESP-IDF's esp_lcd headers.
 *
 * Bring-up order: the PHY supply, the bus, the command interface, the DPI
 * panel (frame buffers allocated and black, video not started), the
 * panel's reset, the initialisation commands, then the video stream.
 *
 * SPDX-License-Identifier: MIT
 */

#include "display.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <string.h>

#include "board.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "display";

#define FLIP_WAIT_MS 200

/*
 * The panel's reset: low for 10 ms (the EK79007 needs 1 ms), then 20 ms
 * before the first command (the datasheet states no minimum).
 */
#define RESET_LOW_MS   10
#define RESET_WAIT_MS  20

/*
 * After sleep out (0x11): the EK79007 takes the next command after 5 ms
 * and shows a picture about 7 frames later.  120 ms covers both.
 */
#define SLEEP_OUT_MS   120

/* DCS (Display Command Set) commands, EK79007 register list. */
#define EK_CMD_SLEEP_OUT     0x11
#define EK_CMD_ADDRESS_MODE  0x36
#define EK_CMD_PANEL_CTRL2   0xB2   /* bit 4: 2 lanes; bit 6: normally black */

typedef struct {
    uint8_t cmd;
    uint8_t value;
} ek_reg_t;

/*
 * Every register takes one parameter byte and takes effect at the next
 * valid frame.  The lane count goes first: the chip resets to 4 lanes.
 * 0x80 to 0x86 are the 14 gamma fields, two per register; the values are
 * the ones Espressif's EK79007 driver writes (Apache-2.0), against 0x88 in
 * every register after reset.
 */
static const ek_reg_t k_init[] = {
    { EK_CMD_PANEL_CTRL2, 0x10 },
    { 0x80, 0x8B },
    { 0x81, 0x78 },
    { 0x82, 0x84 },
    { 0x83, 0x88 },
    { 0x84, 0xA8 },
    { 0x85, 0xE3 },
    { 0x86, 0x88 },
    { EK_CMD_ADDRESS_MODE, BOARD_LCD_ADDRESS_MODE },
};

typedef struct {
    bool initialised;
    esp_ldo_channel_handle_t phy_ldo;
    esp_lcd_dsi_bus_handle_t bus;
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    SemaphoreHandle_t done_sem;     /* a frame buffer's scan ended */
    SemaphoreHandle_t vsync_sem;
    atomic_uint vsyncs;

    gfx_color_t *fb[2];
    uint8_t num_fbs;
    uint8_t back;          /* index the CPU is allowed to draw into */

    gfx_canvas_t canvas;

    uint32_t frames;
    uint32_t fps_frames;
    int64_t fps_window_us;
    float fps;
    uint32_t last_wait_us;
} display_state_t;

static display_state_t s_disp;

/* ISR (interrupt service routine) context. */
static IRAM_ATTR bool on_frame_done(esp_lcd_panel_handle_t panel,
                                    esp_lcd_dpi_panel_event_data_t *edata,
                                    void *user_ctx)
{
    (void)panel;
    (void)edata;
    display_state_t *st = (display_state_t *)user_ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(st->done_sem, &woken);
    return woken == pdTRUE;
}

static IRAM_ATTR bool on_vsync(esp_lcd_panel_handle_t panel,
                               esp_lcd_dpi_panel_event_data_t *edata,
                               void *user_ctx)
{
    (void)panel;
    (void)edata;
    display_state_t *st = (display_state_t *)user_ctx;
    atomic_fetch_add(&st->vsyncs, 1u);
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(st->vsync_sem, &woken);
    return woken == pdTRUE;
}

static void rebind_canvas(void)
{
    gfx_canvas_init(&s_disp.canvas, s_disp.fb[s_disp.back],
                    BOARD_UI_H_RES, BOARD_UI_V_RES, BOARD_LCD_H_RES);
}

static esp_err_t panel_reset(void)
{
    const gpio_config_t rst = {
        .pin_bit_mask = 1ULL << BOARD_LCD_PIN_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&rst), TAG, "reset pin");
    ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_LCD_PIN_RST, 0), TAG, "reset");
    vTaskDelay(pdMS_TO_TICKS(RESET_LOW_MS));
    ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_LCD_PIN_RST, 1), TAG, "release");
    vTaskDelay(pdMS_TO_TICKS(RESET_WAIT_MS));
    return ESP_OK;
}

/* The register table, then sleep out.  All in LP (low-power) mode on
 * virtual channel 0, before the video stream starts. */
static esp_err_t panel_commands(void)
{
    for (size_t i = 0; i < sizeof(k_init) / sizeof(k_init[0]); ++i) {
        const ek_reg_t *r = &k_init[i];
        esp_err_t err = esp_lcd_panel_io_tx_param(s_disp.io, r->cmd,
                                                  &r->value, 1);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "command 0x%02x: %s", r->cmd, esp_err_to_name(err));
            return err;
        }
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_disp.io, EK_CMD_SLEEP_OUT,
                                                  NULL, 0),
                        TAG, "sleep out");
    vTaskDelay(pdMS_TO_TICKS(SLEEP_OUT_MS));
    return ESP_OK;
}

static esp_err_t bus_up(void)
{
    const esp_ldo_channel_config_t ldo = {
        .chan_id = BOARD_DSI_PHY_LDO_CHAN,
        .voltage_mv = BOARD_DSI_PHY_LDO_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo, &s_disp.phy_ldo), TAG,
                        "PHY supply");

    const esp_lcd_dsi_bus_config_t bus = {
        .bus_id = 0,
        .num_data_lanes = BOARD_DSI_LANES,
        .phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = (float)BOARD_DSI_LANE_MBPS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus, &s_disp.bus), TAG,
                        "DSI bus");

    const esp_lcd_dbi_io_config_t dbi = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(s_disp.bus, &dbi, &s_disp.io),
                        TAG, "command interface");

    const esp_lcd_dpi_panel_config_t dpi = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = BOARD_LCD_DPI_CLOCK_MHZ,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = s_disp.num_fbs,
        .video_timing = {
            .h_size = BOARD_LCD_H_RES,
            .v_size = BOARD_LCD_V_RES,
            .hsync_pulse_width = BOARD_LCD_HSYNC_PULSE_WIDTH,
            .hsync_back_porch = BOARD_LCD_HSYNC_BACK_PORCH,
            .hsync_front_porch = BOARD_LCD_HSYNC_FRONT_PORCH,
            .vsync_pulse_width = BOARD_LCD_VSYNC_PULSE_WIDTH,
            .vsync_back_porch = BOARD_LCD_VSYNC_BACK_PORCH,
            .vsync_front_porch = BOARD_LCD_VSYNC_FRONT_PORCH,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_dpi(s_disp.bus, &dpi, &s_disp.panel),
                        TAG, "DPI panel");
    return ESP_OK;
}

/* Whatever bus_up() got, in reverse, after a failure or at deinit. */
static void bus_down(void)
{
    if (s_disp.panel != NULL) {
        (void)esp_lcd_panel_del(s_disp.panel);
    }
    if (s_disp.io != NULL) {
        (void)esp_lcd_panel_io_del(s_disp.io);
    }
    if (s_disp.bus != NULL) {
        (void)esp_lcd_del_dsi_bus(s_disp.bus);
    }
    if (s_disp.phy_ldo != NULL) {
        (void)esp_ldo_release_channel(s_disp.phy_ldo);
    }
    if (s_disp.done_sem != NULL) {
        vSemaphoreDelete(s_disp.done_sem);
    }
    if (s_disp.vsync_sem != NULL) {
        vSemaphoreDelete(s_disp.vsync_sem);
    }
    memset(&s_disp, 0, sizeof(s_disp));
}

static esp_err_t start(const display_config_t *c)
{
    s_disp.num_fbs = c->double_buffer ? 2 : 1;
    s_disp.done_sem = xSemaphoreCreateBinary();
    s_disp.vsync_sem = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_disp.done_sem && s_disp.vsync_sem, ESP_ERR_NO_MEM,
                        TAG, "semaphores");

    ESP_RETURN_ON_ERROR(bus_up(), TAG, "bus");

    const esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_frame_buf_complete = on_frame_done,
        .on_vsync = on_vsync,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_dpi_panel_register_event_callbacks(s_disp.panel, &cbs, &s_disp),
        TAG, "callbacks");

    if (s_disp.num_fbs == 2) {
        ESP_RETURN_ON_ERROR(
            esp_lcd_dpi_panel_get_frame_buffer(s_disp.panel, 2,
                                               (void **)&s_disp.fb[0],
                                               (void **)&s_disp.fb[1]),
            TAG, "frame buffers");
    } else {
        ESP_RETURN_ON_ERROR(
            esp_lcd_dpi_panel_get_frame_buffer(s_disp.panel, 1,
                                               (void **)&s_disp.fb[0]),
            TAG, "frame buffer");
        s_disp.fb[1] = s_disp.fb[0];
    }

    ESP_RETURN_ON_ERROR(panel_reset(), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(panel_commands(), TAG, "panel commands");
    /* The video stream: frame buffer 0 first. */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_disp.panel), TAG, "video");

#ifdef RCBENCH_DISPLAY_BARS
    /* The DSI host's own colour bars for 3 s: a picture that needs no
     * frame buffer, for a first look at a new board. */
    ESP_RETURN_ON_ERROR(
        esp_lcd_dpi_panel_set_pattern(s_disp.panel,
                                      MIPI_DSI_PATTERN_BAR_VERTICAL),
        TAG, "bars");
    (void)board_backlight(true);
    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_RETURN_ON_ERROR(
        esp_lcd_dpi_panel_set_pattern(s_disp.panel, MIPI_DSI_PATTERN_NONE),
        TAG, "bars off");
#endif

    /* fb[0] scans first, so the CPU (central processing unit) starts on
     * fb[1], or on fb[0] when single-buffered. */
    s_disp.back = (s_disp.num_fbs == 2) ? 1 : 0;
    rebind_canvas();
    return ESP_OK;
}

esp_err_t display_init(const display_config_t *cfg)
{
    if (s_disp.initialised) {
        return ESP_OK;
    }
    const display_config_t c = (cfg != NULL) ? *cfg : DISPLAY_CONFIG_DEFAULT();

    ESP_RETURN_ON_ERROR(board_init(), TAG, "board init");

    const esp_err_t err = start(&c);
    if (err != ESP_OK) {
        bus_down();
        return err;
    }

    s_disp.frames = 0;
    s_disp.fps_frames = 0;
    s_disp.fps = 0.0f;
    s_disp.fps_window_us = esp_timer_get_time();
    s_disp.initialised = true;

    ESP_RETURN_ON_ERROR(board_backlight(c.backlight_on), TAG, "backlight");

    ESP_LOGI(TAG, "%dx%d RGB565, %d lanes at %d Mbit/s, %.1f MHz pixel clock "
                  "requested, %d frame buffer(s) in PSRAM (%u KiB each), "
                  "UI %dx%d",
             BOARD_LCD_H_RES, BOARD_LCD_V_RES, BOARD_DSI_LANES,
             BOARD_DSI_LANE_MBPS, (double)BOARD_LCD_DPI_CLOCK_MHZ,
             s_disp.num_fbs,
             (unsigned)((size_t)BOARD_LCD_H_RES * BOARD_LCD_V_RES
                        * sizeof(gfx_color_t) / 1024u),
             BOARD_UI_H_RES, BOARD_UI_V_RES);
    return ESP_OK;
}

esp_err_t display_deinit(void)
{
    if (!s_disp.initialised) {
        return ESP_OK;
    }
    bus_down();
    return ESP_OK;
}

gfx_canvas_t *display_canvas(void)
{
    return &s_disp.canvas;
}

esp_err_t display_wait_vsync(uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_disp.initialised, ESP_ERR_INVALID_STATE, TAG,
                        "display_init() first");
    const TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY
                                               : pdMS_TO_TICKS(timeout_ms);
    /* Drain first: a token from an earlier frame is not a frame boundary. */
    (void)xSemaphoreTake(s_disp.vsync_sem, 0);
    return (xSemaphoreTake(s_disp.vsync_sem, ticks) == pdTRUE)
               ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void account_frame(void)
{
    s_disp.frames++;
    s_disp.fps_frames++;
    const int64_t now = esp_timer_get_time();
    const int64_t elapsed = now - s_disp.fps_window_us;
    if (elapsed >= 500000) {
        s_disp.fps = (float)s_disp.fps_frames * 1000000.0f / (float)elapsed;
        s_disp.fps_frames = 0;
        s_disp.fps_window_us = now;
    }
}

static esp_err_t wait_frame_done(void)
{
    return (xSemaphoreTake(s_disp.done_sem, pdMS_TO_TICKS(FLIP_WAIT_MS))
            == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t display_flip(void)
{
    ESP_RETURN_ON_FALSE(s_disp.initialised, ESP_ERR_INVALID_STATE, TAG,
                        "display_init() first");

    const int64_t wait_start = esp_timer_get_time();

    if (s_disp.num_fbs < 2) {
        (void)xSemaphoreTake(s_disp.done_sem, 0);
        const esp_err_t err = wait_frame_done();
        s_disp.last_wait_us = (uint32_t)(esp_timer_get_time() - wait_start);
        account_frame();
        return err;
    }

    /*
     * A pointer inside a frame buffer is taken as it is: the driver writes
     * the cache back and scans that buffer from the next frame on.
     */
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_draw_bitmap(s_disp.panel, 0, 0,
                                  BOARD_LCD_H_RES, BOARD_LCD_V_RES,
                                  s_disp.fb[s_disp.back]),
        TAG, "present");

    /*
     * The frame in flight still reads the other buffer.  The first
     * frame-done after the hand-over is its end, and from then on nothing
     * reads that buffer.  Drained after the hand-over, not before: a token
     * from a frame that ended before it would free a buffer still being
     * scanned.  A frame that ends between the two calls is lost to the
     * drain and costs one frame of waiting, never a torn one.
     */
    (void)xSemaphoreTake(s_disp.done_sem, 0);
    const esp_err_t err = wait_frame_done();

    s_disp.last_wait_us = (uint32_t)(esp_timer_get_time() - wait_start);
    s_disp.back ^= 1u;
    rebind_canvas();
    account_frame();
    return err;
}

int display_width(void)
{
    return BOARD_UI_H_RES;
}

int display_height(void)
{
    return BOARD_UI_V_RES;
}

bool display_double_buffered(void)
{
    return s_disp.num_fbs >= 2;
}

int display_back_index(void)
{
    return s_disp.back;
}

uint32_t display_last_wait_us(void)
{
    return s_disp.last_wait_us;
}

uint32_t display_frame_count(void)
{
    return s_disp.frames;
}

float display_fps(void)
{
    return s_disp.fps;
}

uint32_t display_vsync_count(void)
{
    return atomic_load(&s_disp.vsyncs);
}

esp_err_t display_backlight(bool on)
{
    return board_backlight(on);
}
