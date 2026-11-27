/**
 * @file
 * @brief The Guition JC3636K518C's supply reading and its soft OFF
 *        (dial.h).
 *
 * The reading: GPIO1, ADC1 channel 0, 12dB attenuation, ESP-IDF's
 * curve-fitting calibration.  Once a second (DK518_READ_MS) 16 samples
 * make one reading of the rail through the 10k/10k divider
 * (dk518_rail_mv()); dk518_supply_step() turns it into USB, the cell's
 * voltage, LOW and EMPTY (shared/dial/dial_batt.h).  A test build's
 * `batt <mV>` stands for the rail, with no USB host, so LOW and EMPTY run
 * on the console.  The face task and the console both read the supply;
 * a mutex keeps the reading whole.
 *
 * The power: no latch.  The slide switch cuts the power.  The switch-off's
 * OFF, and an EMPTY cell after BATTERY EMPTY has stood DIAL_EMPTY_SHOW_MS
 * (dial_main.c), put the K518 into deep sleep: the display asleep with its
 * backlight's pin held low, the touch controller and the haptic driver in
 * their own sleep, and a wake on either encoder line going low (ext1 on
 * GPIO7 and GPIO8, their RTC pull-ups on), so a turn wakes it into a new
 * start, which gives both lines back to the digital GPIO matrix
 * (rtc_gpio_deinit()) before the input task's PCNT units take them.  A
 * line that is low already is waited out for up to 200ms
 * (DK518_WAKE_WAIT_MS).  The deep sleep current is not measured; the
 * divider alone draws about 190µA from a 3.8V cell.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "dial.h"
#include "dial_batt.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/usb_serial_jtag.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/** @brief The reading's period, ms. */
#define DK518_READ_MS 1000u
/** @brief The longest wait for both encoder lines to be high, ms. */
#define DK518_WAKE_WAIT_MS 200u
/** @brief GPIO1's ADC channel. */
#define BATT_CHANNEL ADC_CHANNEL_0

/** @brief The log's tag. */
static const char *TAG = "power";

/** @brief The ADC unit; NULL when it did not start. */
static adc_oneshot_unit_handle_t s_adc;
/** @brief The calibration; NULL when none. */
static adc_cali_handle_t s_cali;
/** @brief Guards the supply's state. */
static SemaphoreHandle_t s_lock;
/** @brief The supply as the last reading left it. */
static dk518_supply_t s_sup;
/** @brief A reading was taken since start. */
static bool s_read_any;
/** @brief When the last reading was taken, ms. */
static uint32_t s_read_ms;
/** @brief A test build's injected rail, mV; 0 none.  One aligned 16-bit
 *         store from the console task. */
static volatile uint16_t s_inject_mv;

/** @brief Start the ADC on GPIO1 and its calibration. */
static void adc_start(void)
{
    const adc_oneshot_unit_init_cfg_t unit = { .unit_id = ADC_UNIT_1 };
    const adc_oneshot_chan_cfg_t ch = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    const adc_cali_curve_fitting_config_t cali = {
        .unit_id = ADC_UNIT_1,
        .chan = BATT_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_oneshot_new_unit(&unit, &s_adc) != ESP_OK
        || adc_oneshot_config_channel(s_adc, BATT_CHANNEL, &ch) != ESP_OK) {
        ESP_LOGE(TAG, "the ADC did not start: the battery is unknown");
        s_adc = NULL;
        return;
    }
    if (adc_cali_create_scheme_curve_fitting(&cali, &s_cali) != ESP_OK) {
        ESP_LOGW(TAG, "no ADC calibration: the battery is unknown");
        s_cali = NULL;
    }
}

/** @brief One reading of the rail.  @return mV; 0 without the ADC */
static uint16_t rail_read(void)
{
    uint16_t mv[DK518_SAMPLES];
    if (s_adc == NULL || s_cali == NULL) {
        return 0u;
    }
    for (unsigned i = 0u; i < DK518_SAMPLES; ++i) {
        int v = 0;
        if (adc_oneshot_get_calibrated_result(s_adc, s_cali, BATT_CHANNEL,
                                              &v) != ESP_OK) {
            return 0u;
        }
        mv[i] = (uint16_t)(v < 0 ? 0 : v > UINT16_MAX ? UINT16_MAX : v);
    }
    return dk518_rail_mv(mv);
}

void dial_power_hold(void)
{
    /* A wake from the soft OFF: the backlight's pin is still held low, and
     * the encoder's lines are still RTC IO for the ext1 wake.  PCNT takes
     * them as digital GPIO only once their RTC mux is cleared. */
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis(DIAL_PIN_LCD_BL);
    (void)rtc_gpio_deinit(DIAL_PIN_ENC_CW);
    (void)rtc_gpio_deinit(DIAL_PIN_ENC_CCW);
    dk518_supply_init(&s_sup);
    s_lock = xSemaphoreCreateMutex();
    adc_start();
}

/**
 * @brief Wait until neither encoder line is low, at most
 *        DK518_WAKE_WAIT_MS.
 * @return true when both are high
 */
static bool lines_high(void)
{
    for (uint32_t t = 0u; t < DK518_WAKE_WAIT_MS; t += 10u) {
        if (gpio_get_level(DIAL_PIN_ENC_CW) != 0
            && gpio_get_level(DIAL_PIN_ENC_CCW) != 0) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

void dial_power_off(void)
{
    const uint64_t lines = (1ULL << DIAL_PIN_ENC_CW)
                           | (1ULL << DIAL_PIN_ENC_CCW);
    ESP_LOGI(TAG, "soft OFF: deep sleep, a turn wakes the K518");
    if (!lines_high()) {
        ESP_LOGW(TAG, "an encoder line stays low: the K518 may wake at once");
    }
    dial_k518_io_sleep();
    dial_k518_lcd_sleep();
    rtc_gpio_pullup_en(DIAL_PIN_ENC_CW);
    rtc_gpio_pulldown_dis(DIAL_PIN_ENC_CW);
    rtc_gpio_pullup_en(DIAL_PIN_ENC_CCW);
    rtc_gpio_pulldown_dis(DIAL_PIN_ENC_CCW);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    if (esp_sleep_enable_ext1_wakeup_io(lines, ESP_EXT1_WAKEUP_ANY_LOW)
        != ESP_OK) {
        ESP_LOGE(TAG, "no wake on the encoder: the slide switch wakes it");
    }
    esp_deep_sleep_start();
}

void dial_supply_read(dial_supply_t *out)
{
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    memset(out, 0, sizeof *out);
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_read_any || now - s_read_ms >= DK518_READ_MS) {
        const uint16_t inj = s_inject_mv;
        const uint16_t rail = inj != 0u ? inj : rail_read();
        const bool host = inj == 0u && usb_serial_jtag_is_connected();
        if (rail != 0u || host) {
            dk518_supply_step(&s_sup, rail, host);
        }
        s_read_any = true;
        s_read_ms = now;
    }
    out->mv = s_sup.mv;
    out->known = s_sup.known;
    out->usb = s_sup.usb;
    out->low = s_sup.low.on;
    out->empty = s_sup.empty.on;
    xSemaphoreGive(s_lock);
}

void dial_supply_inject(uint16_t mv)
{
    s_inject_mv = mv;
}
