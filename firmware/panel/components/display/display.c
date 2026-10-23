/**
 * @file display.c
 * @brief The EK79007 panel on the ESP32-P4's DSI host, written from the
 *        EK79007 datasheet and ESP-IDF's esp_lcd headers.
 *
 * Bring-up order: the PHY supply, the bus, the command interface, the DPI
 * panel (frame buffers allocated and black, video not started), the
 * panel's reset, the initialisation commands, then the video stream.
 *
 * Screen rotation 180° turns the picture in the flush (gfx_turn.h): the UI
 * draws into a logical buffer, and display_flip() copies what it drew into
 * the frame buffer, turned, through the PPA (pixel-processing accelerator)
 * before it presents that buffer.  The EK79007's scan direction (0x36)
 * cannot do it on this board: its PINCTL strap holds the scan direction
 * and the register is ignored, at start-up as while the video runs.
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
#include "driver/ppa.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gfx_turn.h"

static const char *TAG = "display";

/**
 * @brief Two frame buffers, always: the DPI panel scans one while the CPU
 *        draws the other, and display_flip() hands the finished one over.
 */
#define DISPLAY_NUM_FBS 2

/** @brief The longest display_flip() waits for a frame-done event. */
#define FLIP_WAIT_MS 200

/** @brief One frame buffer's bytes: 1024 x 600 x 2, 1,228,800. */
#define FB_BYTES ((size_t)BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(gfx_color_t))

/** @brief The logical buffers' alignment: the PSRAM's L2 cache line, 128
 *         bytes. */
#define LFB_ALIGN 128

/**
 * @brief The panel's reset: low for 10 ms (the EK79007 needs 1 ms), then 20 ms
 * before the first command (the datasheet states no minimum).
 */
#define RESET_LOW_MS   10
#define RESET_WAIT_MS  20

/**
 * @brief After sleep out (0x11): the EK79007 takes the next command after 5 ms
 * and shows a picture about 7 frames later.  120 ms covers both.
 */
#define SLEEP_OUT_MS   120

/** @brief DCS (Display Command Set) commands, EK79007 register list. */
#define EK_CMD_SLEEP_OUT     0x11
#define EK_CMD_ADDRESS_MODE  0x36
#define EK_CMD_PANEL_CTRL2   0xB2   /**< bit 4: 2 lanes; bit 6: normally black */

/** @brief One register write: a command and its one parameter byte. */
typedef struct {
    uint8_t cmd;      /**< the register */
    uint8_t value;    /**< its parameter */
} ek_reg_t;

/**
 * @brief The initialisation table.
 *
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

/** @brief The driver's state; all zero while the display is down. */
typedef struct {
    bool initialised;                   /**< display_init() succeeded      */
    esp_ldo_channel_handle_t phy_ldo;   /**< the DSI PHY supply            */
    esp_lcd_dsi_bus_handle_t bus;       /**< the DSI bus                   */
    esp_lcd_panel_io_handle_t io;       /**< the command interface         */
    esp_lcd_panel_handle_t panel;       /**< the DPI panel                 */
    SemaphoreHandle_t done_sem;         /**< a frame buffer's scan ended   */
    SemaphoreHandle_t vsync_sem;        /**< a VSYNC event came            */
    atomic_uint vsyncs;                 /**< VSYNC events since init       */

    gfx_color_t *fb[2];                 /**< the frame buffers             */
    uint8_t back;                       /**< index the CPU draws into      */

    gfx_canvas_t canvas;                /**< fb[back], or lfb[back] turned */

    gfx_color_t *lfb[2];                /**< the logical buffers, turned   */
    ppa_client_handle_t ppa;            /**< the PPA's rotate client       */
    SemaphoreHandle_t ppa_sem;          /**< a PPA copy finished           */
    gfx_turn_t turn;                    /**< Screen rotation's state       */

    uint32_t frames;                    /**< flips since init              */
    uint32_t fps_frames;                /**< flips in the current window   */
    int64_t fps_window_us;              /**< start of the current window   */
    float fps;                          /**< the last window's rate        */
    uint32_t last_wait_us;              /**< the last flip's wait          */
} display_state_t;

/** @brief The one display. */
static display_state_t s_disp;

/**
 * @brief What Screen rotation asks for (display_set_turned()).  Outside
 *        s_disp: it is set before display_init() and from any task; the
 *        flip reads it.
 */
static atomic_bool s_want;

/**
 * @brief Frame-done callback: a frame buffer's scan ended.  ISR (interrupt
 *        service routine) context.
 * @param panel Unused.
 * @param edata Unused.
 * @param user_ctx The display_state_t.
 * @return true when a task woke and a context switch is due.
 */
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

/**
 * @brief VSYNC callback: count it and wake display_wait_vsync().  ISR
 *        context.
 * @param panel Unused.
 * @param edata Unused.
 * @param user_ctx The display_state_t.
 * @return true when a task woke and a context switch is due.
 */
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

/**
 * @brief Point the canvas at the UI's area of the back buffer, or of its
 *        logical buffer while turned, and start the frame's damage record.
 */
static void rebind_canvas(void)
{
    const bool turned = gfx_turn_drawing(&s_disp.turn);
    gfx_canvas_init(&s_disp.canvas,
                    turned ? s_disp.lfb[s_disp.back] : s_disp.fb[s_disp.back],
                    BOARD_UI_H_RES, BOARD_UI_V_RES, BOARD_LCD_H_RES);
    gfx_turn_begin(&s_disp.turn, &s_disp.canvas);
}

/**
 * @brief PPA transaction done: wake ppa_copy().  ISR context.
 * @param client Unused.
 * @param edata Unused.
 * @param user_ctx The display_state_t.
 * @return true when a task woke and a context switch is due.
 */
static IRAM_ATTR bool on_ppa_done(ppa_client_handle_t client,
                                  ppa_event_data_t *edata, void *user_ctx)
{
    (void)client;
    (void)edata;
    display_state_t *st = (display_state_t *)user_ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(st->ppa_sem, &woken);
    return woken == pdTRUE;
}

/**
 * @brief One block of the logical buffer into the frame buffer, turned by
 *        180°, through the PPA; waits at most @p budget_us for it.
 * @param ctx The display_state_t.
 * @param src The logical buffer.
 * @param dst The frame buffer of the same index.
 * @param r The block, in logical coordinates.
 * @param budget_us The longest wait, us.
 * @return How the copy ended.
 */
static gfx_turn_copy_t ppa_copy(void *ctx, const gfx_color_t *src,
                                gfx_color_t *dst, gfx_rect_t r,
                                uint32_t budget_us)
{
    display_state_t *st = (display_state_t *)ctx;
    const gfx_rect_t o = gfx_damage_turn(r, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    const ppa_srm_oper_config_t op = {
        .in = {
            .buffer = src,
            .pic_w = BOARD_LCD_H_RES,
            .pic_h = BOARD_LCD_V_RES,
            .block_w = (uint32_t)r.w,
            .block_h = (uint32_t)r.h,
            .block_offset_x = (uint32_t)r.x,
            .block_offset_y = (uint32_t)r.y,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = dst,
            .buffer_size = FB_BYTES,
            .pic_w = BOARD_LCD_H_RES,
            .pic_h = BOARD_LCD_V_RES,
            .block_offset_x = (uint32_t)o.x,
            .block_offset_y = (uint32_t)o.y,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_180,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .mode = PPA_TRANS_MODE_NON_BLOCKING,
    };
    /* A token left from an earlier copy is not this one's end. */
    (void)xSemaphoreTake(st->ppa_sem, 0);
    if (ppa_do_scale_rotate_mirror(st->ppa, &op) != ESP_OK) {
        return GFX_TURN_COPY_ERROR;
    }
    /* Whole milliseconds up, at least one tick: CONFIG_FREERTOS_HZ is
     * 1000. */
    TickType_t ticks = pdMS_TO_TICKS((budget_us + 999u) / 1000u);
    if (ticks == 0) {
        ticks = 1;
    }
    return (xSemaphoreTake(st->ppa_sem, ticks) == pdTRUE)
               ? GFX_TURN_COPY_DONE : GFX_TURN_COPY_TIMEOUT;
}

/**
 * @brief The time for gfx_turn_flush(), us.
 * @param ctx Unused.
 * @return esp_timer's time, wrapping at 2^32.
 */
static uint32_t turn_now_us(void *ctx)
{
    (void)ctx;
    return (uint32_t)esp_timer_get_time();
}

/** @brief The PPA as gfx_turn_flush()'s copy engine. */
static const gfx_turn_ops_t k_turn_ops = {
    .copy = ppa_copy,
    .now_us = turn_now_us,
    .ctx = &s_disp,
};

/**
 * @brief Screen rotation's buffers and engine: two logical buffers in
 *        PSRAM and a PPA client for 180° copies.  A failure leaves the
 *        panel NORMAL (gfx_turn_fail()) and the display working.
 */
static void turn_up(void)
{
    gfx_turn_init(&s_disp.turn, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    const char *missing = NULL;
    for (int b = 0; b < DISPLAY_NUM_FBS && missing == NULL; ++b) {
        s_disp.lfb[b] = heap_caps_aligned_calloc(LFB_ALIGN, 1, FB_BYTES,
                                                 MALLOC_CAP_SPIRAM);
        missing = (s_disp.lfb[b] == NULL) ? "logical buffers" : NULL;
    }
    s_disp.ppa_sem = (missing == NULL) ? xSemaphoreCreateBinary() : NULL;
    if (missing == NULL && s_disp.ppa_sem == NULL) {
        missing = "semaphore";
    }
    if (missing == NULL) {
        const ppa_client_config_t cfg = {
            .oper_type = PPA_OPERATION_SRM,
            .max_pending_trans_num = 1,
        };
        const ppa_event_callbacks_t cbs = { .on_trans_done = on_ppa_done };
        if (ppa_register_client(&cfg, &s_disp.ppa) != ESP_OK
            || ppa_client_register_event_callbacks(s_disp.ppa, &cbs)
                   != ESP_OK) {
            missing = "PPA client";
        }
    }
    if (missing != NULL) {
        gfx_turn_fail(&s_disp.turn, GFX_TURN_FAIL_ENGINE);
        ESP_LOGW(TAG, "rotation 180 off: no %s", missing);
    }
}

/** @brief Free what turn_up() got. */
static void turn_down(void)
{
    if (s_disp.ppa != NULL) {
        (void)ppa_unregister_client(s_disp.ppa);
    }
    if (s_disp.ppa_sem != NULL) {
        vSemaphoreDelete(s_disp.ppa_sem);
    }
    for (int b = 0; b < DISPLAY_NUM_FBS; ++b) {
        heap_caps_free(s_disp.lfb[b]);
    }
    gfx_damage_attach(NULL);
    (void)gfx_turn_presented(&s_disp.turn, GFX_TURN_SHOW_NORMAL);
}

/**
 * @brief Pulse the panel's reset line, RESET_LOW_MS low, then RESET_WAIT_MS.
 * @return ESP_OK, or the GPIO error.
 */
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

/**
 * @brief Send the register table, then sleep out.
 *
 * All in LP (low-power) mode on virtual channel 0, before the video stream
 * starts.
 *
 * @return ESP_OK, or the error of the first command that failed.
 */
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

/**
 * @brief Take the PHY supply, then create the DSI bus, the command
 *        interface and the DPI panel with its frame buffers.
 * @return ESP_OK, or the error of the step that failed; bus_down() frees
 *         what was taken.
 */
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
        .num_fbs = DISPLAY_NUM_FBS,
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

/** @brief Free whatever bus_up() and start() got, in reverse, after a
 *         failure or at deinit, and zero the state. */
static void bus_down(void)
{
    turn_down();
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

/**
 * @brief Bring the display up: semaphores, bus, callbacks, frame buffers,
 *        panel reset and commands, video stream.
 * @param c The set-up.
 * @return ESP_OK, or the error of the step that failed; the caller runs
 *         bus_down().
 */
static esp_err_t start(const display_config_t *c)
{
    s_disp.done_sem = xSemaphoreCreateBinary();
    s_disp.vsync_sem = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_disp.done_sem && s_disp.vsync_sem, ESP_ERR_NO_MEM,
                        TAG, "semaphores");

    ESP_RETURN_ON_ERROR(bus_up(), TAG, "bus");
    turn_up();

    const esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_frame_buf_complete = on_frame_done,
        .on_vsync = on_vsync,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_dpi_panel_register_event_callbacks(s_disp.panel, &cbs, &s_disp),
        TAG, "callbacks");

    ESP_RETURN_ON_ERROR(
        esp_lcd_dpi_panel_get_frame_buffer(s_disp.panel, DISPLAY_NUM_FBS,
                                           (void **)&s_disp.fb[0],
                                           (void **)&s_disp.fb[1]),
        TAG, "frame buffers");

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
     * fb[1]; turned, on lfb[1]. */
    s_disp.back = 1;
    gfx_turn_request(&s_disp.turn, atomic_load(&s_want));
    gfx_turn_next(&s_disp.turn);
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
                  "requested, %d frame buffers and %d logical buffers in "
                  "PSRAM (%u KiB each), UI %dx%d, PSRAM free %u KiB",
             BOARD_LCD_H_RES, BOARD_LCD_V_RES, BOARD_DSI_LANES,
             BOARD_DSI_LANE_MBPS, (double)BOARD_LCD_DPI_CLOCK_MHZ,
             DISPLAY_NUM_FBS,
             (s_disp.lfb[1] != NULL) ? DISPLAY_NUM_FBS : 0,
             (unsigned)(FB_BYTES / 1024u),
             BOARD_UI_H_RES, BOARD_UI_V_RES,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024u));
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

/** @brief Count a flip and update the rolling rate every 500 ms. */
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

/**
 * @brief Wait for the next frame-done event, at most FLIP_WAIT_MS.
 * @return ESP_OK, or ESP_ERR_TIMEOUT.
 */
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

    /*
     * Turned, what the frame drew goes into its frame buffer first, turned
     * by the PPA, on this task: the control task, which owns STOP, never
     * waits on it.  A copy that fails or runs out of its 50ms sends this
     * frame out unturned, and the shown flag below follows.
     */
    const gfx_turn_show_t show =
        gfx_turn_flush(&s_disp.turn, s_disp.lfb[s_disp.back],
                       s_disp.fb[s_disp.back], &k_turn_ops);
    if (show == GFX_TURN_SHOW_FALLBACK) {
        ESP_LOGW(TAG, "rotation 180 off: PPA %s after %u us",
                 (s_disp.turn.why == GFX_TURN_FAIL_TIMEOUT) ? "timeout"
                                                            : "error",
                 (unsigned)s_disp.turn.copy_us);
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

    /* The frame is on the glass: touch turns with it, from here on. */
    if (gfx_turn_presented(&s_disp.turn, show)
        && show == GFX_TURN_SHOW_TURNED) {
        ESP_LOGI(TAG, "rotation 180 applied in flush: %u blocks, %u px, "
                      "%u us",
                 (unsigned)s_disp.turn.rects, (unsigned)s_disp.turn.pixels,
                 (unsigned)s_disp.turn.copy_us);
    }

    s_disp.last_wait_us = (uint32_t)(esp_timer_get_time() - wait_start);
    s_disp.back ^= 1u;
    gfx_turn_request(&s_disp.turn, atomic_load(&s_want));
    gfx_turn_next(&s_disp.turn);
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

void display_set_turned(bool turned)
{
    atomic_store(&s_want, turned);
}

bool display_turned(void)
{
    return gfx_turn_shown();
}

bool display_take_repaint(void)
{
    return gfx_turn_take_repaint(&s_disp.turn);
}

bool display_take_turn_alert(void)
{
    return gfx_turn_take_alert(&s_disp.turn);
}

uint32_t display_last_turn_us(void)
{
    return s_disp.turn.copy_us;
}

esp_err_t display_backlight(bool on)
{
    return board_backlight(on);
}
