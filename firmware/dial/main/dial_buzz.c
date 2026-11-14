/**
 * @file
 * @brief The buzzer on GPIO3: a 3ms click per detent and short tones, as
 *        LEDC (LED controller) PWM at 50% duty, ended by a one-shot timer.
 *
 * GPIO3 is a strapping pin of the ESP32-S3 (the JTAG source); it is driven
 * only after start-up.  The buzzer's frequency response is not published;
 * 4,000Hz for the click is not measured against it.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dial.h"

#include "driver/ledc.h"
#include "esp_timer.h"

/** @brief The LEDC timer, channel and resolution the buzzer uses. @{ */
#define BUZZ_TIMER   LEDC_TIMER_0
#define BUZZ_CHANNEL LEDC_CHANNEL_0 /**< the LEDC channel */
#define BUZZ_RES     LEDC_TIMER_10_BIT /**< the duty resolution */
#define BUZZ_HALF    512u           /**< 50% duty at 10 bits */
/** @} */
/** @brief The click: frequency and length. @{ */
#define CLICK_HZ 4000u
#define CLICK_MS 3u /**< the click's length, ms */
/** @} */

/** @brief The timer that ends a sound. */
static esp_timer_handle_t s_stop;

/** @brief End the sound: duty 0.  @param arg unused */
static void buzz_stop(void *arg)
{
    (void)arg;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZ_CHANNEL, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZ_CHANNEL);
}

void dial_buzz_init(void)
{
    const ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BUZZ_RES,
        .timer_num = BUZZ_TIMER,
        .freq_hz = CLICK_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t ch = {
        .gpio_num = DIAL_PIN_BUZZER,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BUZZ_CHANNEL,
        .timer_sel = BUZZ_TIMER,
        .duty = 0,
    };
    const esp_timer_create_args_t stop = {
        .callback = buzz_stop,
        .name = "buzz",
    };
    ledc_timer_config(&t);
    ledc_channel_config(&ch);
    esp_timer_create(&stop, &s_stop);
}

void dial_buzz_tone(uint32_t hz, uint32_t ms)
{
    if (s_stop == NULL || hz < 200u || hz > 8000u || ms < 1u || ms > 1000u) {
        return;
    }
    esp_timer_stop(s_stop);
    ledc_set_freq(LEDC_LOW_SPEED_MODE, BUZZ_TIMER, hz);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZ_CHANNEL, BUZZ_HALF);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZ_CHANNEL);
    esp_timer_start_once(s_stop, (uint64_t)ms * 1000u);
}

void dial_buzz_click(void)
{
    dial_buzz_tone(CLICK_HZ, CLICK_MS);
}
