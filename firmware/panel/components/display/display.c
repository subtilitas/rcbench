/*
 * SPDX-License-Identifier: MIT
 */

#include "display.h"

#include <inttypes.h>
#include <string.h>

#include "board.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "display";

#define VSYNC_WAIT_DEFAULT_MS 200

typedef struct {
    bool initialised;
    esp_lcd_panel_handle_t panel;
    SemaphoreHandle_t vsync_sem;
    SemaphoreHandle_t swap_sem;

    gfx_color_t *fb[2];
    size_t fb_bytes;
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

/* Runs in ISR (interrupt service routine) context.  In IRAM (instruction
 * random-access memory) so it also works with CONFIG_LCD_RGB_ISR_IRAM_SAFE
 * and while flash is busy. */
static IRAM_ATTR bool on_vsync(esp_lcd_panel_handle_t panel,
                               const esp_lcd_rgb_panel_event_data_t *edata,
                               void *user_ctx)
{
    (void)panel;
    (void)edata;
    display_state_t *st = (display_state_t *)user_ctx;
    BaseType_t high_prio_woken = pdFALSE;
    if (st->vsync_sem) {
        xSemaphoreGiveFromISR(st->vsync_sem, &high_prio_woken);
    }
    return high_prio_woken == pdTRUE;
}

/* Fires when a whole framebuffer has been handed to the panel.  In bounce
 * mode that is the instant the driver latches the requested buffer
 * (`bb_fb_index = cur_fb_index`), a tighter signal than VSYNC (vertical
 * sync): from then on nothing reads the buffer about to be reclaimed. */
static IRAM_ATTR bool on_frame_buf_complete(esp_lcd_panel_handle_t panel,
                                            const esp_lcd_rgb_panel_event_data_t *edata,
                                            void *user_ctx)
{
    (void)panel;
    (void)edata;
    display_state_t *st = (display_state_t *)user_ctx;
    BaseType_t high_prio_woken = pdFALSE;
    if (st->swap_sem) {
        xSemaphoreGiveFromISR(st->swap_sem, &high_prio_woken);
    }
    return high_prio_woken == pdTRUE;
}

static void rebind_canvas(void)
{
    gfx_canvas_init(&s_disp.canvas, s_disp.fb[s_disp.back],
                    BOARD_LCD_H_RES, BOARD_LCD_V_RES, BOARD_LCD_H_RES);
}

esp_err_t display_init(const display_config_t *cfg)
{
    if (s_disp.initialised) {
        return ESP_OK;
    }

    display_config_t c = DISPLAY_CONFIG_DEFAULT();
    if (cfg) {
        c = *cfg;
    }
    if (c.pclk_hz == 0) {
        c.pclk_hz = BOARD_LCD_PCLK_HZ;
    }

    ESP_RETURN_ON_ERROR(board_init(), TAG, "board init");

    /* The RGB (red, green, blue) peripheral wants fb_size % bounce_size == 0;
     * whole scanlines satisfy that. */
    size_t bounce_px = (size_t)c.bounce_buffer_lines * BOARD_LCD_H_RES;

    const esp_lcd_rgb_panel_config_t panel_cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = c.pclk_hz,
            .h_res = BOARD_LCD_H_RES,
            .v_res = BOARD_LCD_V_RES,
            .hsync_pulse_width = BOARD_LCD_HSYNC_PULSE_WIDTH,
            .hsync_back_porch = BOARD_LCD_HSYNC_BACK_PORCH,
            .hsync_front_porch = BOARD_LCD_HSYNC_FRONT_PORCH,
            .vsync_pulse_width = BOARD_LCD_VSYNC_PULSE_WIDTH,
            .vsync_back_porch = BOARD_LCD_VSYNC_BACK_PORCH,
            .vsync_front_porch = BOARD_LCD_VSYNC_FRONT_PORCH,
            .flags = {
                .pclk_active_neg = BOARD_LCD_PCLK_ACTIVE_NEG,
            },
        },
        .data_width = BOARD_LCD_DATA_WIDTH,
        .bits_per_pixel = BOARD_LCD_BITS_PER_PIXEL,
        .num_fbs = c.double_buffer ? 2 : 1,
        .bounce_buffer_size_px = bounce_px,
        .dma_burst_size = 64,
        .hsync_gpio_num = BOARD_LCD_PIN_HSYNC,
        .vsync_gpio_num = BOARD_LCD_PIN_VSYNC,
        .de_gpio_num = BOARD_LCD_PIN_DE,
        .pclk_gpio_num = BOARD_LCD_PIN_PCLK,
        .disp_gpio_num = BOARD_LCD_PIN_DISP,
        .data_gpio_nums = {
            BOARD_LCD_PIN_DATA0,  BOARD_LCD_PIN_DATA1,  BOARD_LCD_PIN_DATA2,
            BOARD_LCD_PIN_DATA3,  BOARD_LCD_PIN_DATA4,  BOARD_LCD_PIN_DATA5,
            BOARD_LCD_PIN_DATA6,  BOARD_LCD_PIN_DATA7,  BOARD_LCD_PIN_DATA8,
            BOARD_LCD_PIN_DATA9,  BOARD_LCD_PIN_DATA10, BOARD_LCD_PIN_DATA11,
            BOARD_LCD_PIN_DATA12, BOARD_LCD_PIN_DATA13, BOARD_LCD_PIN_DATA14,
            BOARD_LCD_PIN_DATA15,
        },
        .flags = {
            .fb_in_psram = true,
        },
    };

    s_disp.vsync_sem = xSemaphoreCreateBinary();
    s_disp.swap_sem = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_disp.vsync_sem && s_disp.swap_sem, ESP_ERR_NO_MEM, TAG,
                        "semaphores");

    esp_err_t err = esp_lcd_new_rgb_panel(&panel_cfg, &s_disp.panel);
    if (err != ESP_OK) {
        vSemaphoreDelete(s_disp.vsync_sem);
        vSemaphoreDelete(s_disp.swap_sem);
        s_disp.vsync_sem = NULL;
        s_disp.swap_sem = NULL;
        ESP_RETURN_ON_ERROR(err, TAG, "esp_lcd_new_rgb_panel");
    }

    const esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_vsync = on_vsync,
        .on_frame_buf_complete = on_frame_buf_complete,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_rgb_panel_register_event_callbacks(s_disp.panel, &cbs, &s_disp),
        TAG, "register vsync callback");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_disp.panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_disp.panel), TAG, "panel init");

    s_disp.num_fbs = panel_cfg.num_fbs;
    s_disp.fb_bytes = (size_t)BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(gfx_color_t);

    if (s_disp.num_fbs == 2) {
        ESP_RETURN_ON_ERROR(
            esp_lcd_rgb_panel_get_frame_buffer(s_disp.panel, 2,
                                               (void **)&s_disp.fb[0],
                                               (void **)&s_disp.fb[1]),
            TAG, "get frame buffers");
    } else {
        ESP_RETURN_ON_ERROR(
            esp_lcd_rgb_panel_get_frame_buffer(s_disp.panel, 1,
                                               (void **)&s_disp.fb[0]),
            TAG, "get frame buffer");
        s_disp.fb[1] = s_disp.fb[0];
    }

    memset(s_disp.fb[0], 0, s_disp.fb_bytes);
    if (s_disp.num_fbs == 2) {
        memset(s_disp.fb[1], 0, s_disp.fb_bytes);
    }

    /* fb[0] is the one the driver scans out first, so the CPU (central
     * processing unit) starts on fb[1], or on fb[0] when single-buffered. */
    s_disp.back = (s_disp.num_fbs == 2) ? 1 : 0;
    rebind_canvas();

    s_disp.frames = 0;
    s_disp.fps_frames = 0;
    s_disp.fps = 0.0f;
    s_disp.fps_window_us = esp_timer_get_time();
    s_disp.initialised = true;

    ESP_RETURN_ON_ERROR(board_backlight(c.backlight_on), TAG, "backlight");

    ESP_LOGI(TAG, "%dx%d RGB565 @ %" PRIu32 " Hz pclk, %d framebuffer(s) in PSRAM"
             " (%u KiB each), bounce %u lines",
             BOARD_LCD_H_RES, BOARD_LCD_V_RES, c.pclk_hz, s_disp.num_fbs,
             (unsigned)(s_disp.fb_bytes / 1024), (unsigned)c.bounce_buffer_lines);
    return ESP_OK;
}

esp_err_t display_deinit(void)
{
    if (!s_disp.initialised) {
        return ESP_OK;
    }
    esp_err_t err = esp_lcd_panel_del(s_disp.panel);
    if (s_disp.vsync_sem) {
        vSemaphoreDelete(s_disp.vsync_sem);
    }
    if (s_disp.swap_sem) {
        vSemaphoreDelete(s_disp.swap_sem);
    }
    memset(&s_disp, 0, sizeof(s_disp));
    return err;
}

gfx_canvas_t *display_canvas(void)
{
    return &s_disp.canvas;
}

esp_err_t display_wait_vsync(uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_disp.initialised, ESP_ERR_INVALID_STATE, TAG,
                        "display_init() first");
    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    /* Drain first, as display_flip() does for swap_sem.  Nothing else takes
     * this semaphore in the double-buffered path, so it is saturated within a
     * frame of init, and a bare take would return immediately on a token
     * from an earlier frame rather than at a frame boundary. */
    xSemaphoreTake(s_disp.vsync_sem, 0);
    return (xSemaphoreTake(s_disp.vsync_sem, ticks) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void account_frame(void)
{
    s_disp.frames++;
    s_disp.fps_frames++;
    int64_t now = esp_timer_get_time();
    int64_t elapsed = now - s_disp.fps_window_us;
    if (elapsed >= 500000) {
        s_disp.fps = (float)s_disp.fps_frames * 1000000.0f / (float)elapsed;
        s_disp.fps_frames = 0;
        s_disp.fps_window_us = now;
    }
}

esp_err_t display_flip(void)
{
    ESP_RETURN_ON_FALSE(s_disp.initialised, ESP_ERR_INVALID_STATE, TAG,
                        "display_init() first");

    int64_t wait_start = esp_timer_get_time();

    if (s_disp.num_fbs < 2) {
        esp_err_t err = display_wait_vsync(VSYNC_WAIT_DEFAULT_MS);
        s_disp.last_wait_us = (uint32_t)(esp_timer_get_time() - wait_start);
        account_frame();
        return err;
    }

    /* Drop any completion that fired during drawing, so the wait below is for
     * the swap requested next and not for a stale one. */
    xSemaphoreTake(s_disp.swap_sem, 0);

    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_draw_bitmap(s_disp.panel, 0, 0,
                                  BOARD_LCD_H_RES, BOARD_LCD_V_RES,
                                  s_disp.fb[s_disp.back]),
        TAG, "present");

    /* The peripheral latches the new buffer at the end of the frame in flight.
     * When it signals "framebuffer complete", nothing reads the other buffer
     * and it is free to draw into. */
    esp_err_t err = (xSemaphoreTake(s_disp.swap_sem,
                                    pdMS_TO_TICKS(VSYNC_WAIT_DEFAULT_MS)) == pdTRUE)
                        ? ESP_OK : ESP_ERR_TIMEOUT;

    s_disp.last_wait_us = (uint32_t)(esp_timer_get_time() - wait_start);
    s_disp.back ^= 1u;
    rebind_canvas();
    account_frame();
    return err;
}

esp_err_t display_flip_retain(void)
{
    ESP_RETURN_ON_FALSE(s_disp.initialised, ESP_ERR_INVALID_STATE, TAG,
                        "display_init() first");
    if (s_disp.num_fbs < 2) {
        return display_flip();
    }
    const gfx_color_t *shown = s_disp.fb[s_disp.back];
    esp_err_t err = display_flip();
    if (err != ESP_OK && s_disp.fb[s_disp.back] == shown) {
        /* The present failed, so no swap happened and the two pointers are the
         * same buffer: memcpy onto itself is undefined and copies nothing
         * useful either way. */
        return err;
    }
    memcpy(s_disp.fb[s_disp.back], shown, s_disp.fb_bytes);
    return err;
}

int display_width(void)
{
    return BOARD_LCD_H_RES;
}

int display_height(void)
{
    return BOARD_LCD_V_RES;
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

esp_err_t display_set_pclk(uint32_t hz)
{
    ESP_RETURN_ON_FALSE(s_disp.initialised, ESP_ERR_INVALID_STATE, TAG,
                        "display_init() first");
    return esp_lcd_rgb_panel_set_pclk(s_disp.panel, hz);
}

esp_err_t display_backlight(bool on)
{
    return board_backlight(on);
}

esp_lcd_panel_handle_t display_panel(void)
{
    return s_disp.panel;
}
