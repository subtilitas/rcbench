/**
 * @file node_bat.c
 * @brief The cell's voltage: the CD42 module's BAT through a 200kΩ/200kΩ
 *        divider (node_policy.h) to GPIO3, ADC1 channel 3, at 12dB
 *        attenuation, 16 samples averaged, through the curve-fitting
 *        calibration.
 *
 * SPDX-License-Identifier: MIT
 */
#include "node.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#include "node_policy.h"

/** @brief The divider's pin. */
#define BAT_GPIO 3
/** @brief Samples averaged per reading. */
#define BAT_SAMPLES 16u

static adc_oneshot_unit_handle_t s_unit;   /**< ADC1 */
static adc_cali_handle_t         s_cali;   /**< the calibration */
static adc_channel_t             s_chan;   /**< the pin's channel */

void node_bat_init(void)
{
    adc_unit_t unit;
    if (adc_oneshot_io_to_channel(BAT_GPIO, &unit, &s_chan) != ESP_OK) {
        ESP_LOGE(NODE_TAG, "bat: GPIO%d has no ADC channel", BAT_GPIO);
        return;
    }
    const adc_oneshot_unit_init_cfg_t uc = { .unit_id = unit };
    if (adc_oneshot_new_unit(&uc, &s_unit) != ESP_OK) {
        s_unit = NULL;
        return;
    }
    const adc_oneshot_chan_cfg_t cc = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    (void)adc_oneshot_config_channel(s_unit, s_chan, &cc);
    const adc_cali_curve_fitting_config_t kc = {
        .unit_id  = unit,
        .chan     = s_chan,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&kc, &s_cali) != ESP_OK) {
        s_cali = NULL;
        ESP_LOGW(NODE_TAG, "bat: no ADC calibration");
    }
}

uint16_t node_bat_read(void)
{
    if (s_unit == NULL || s_cali == NULL) {
        return 0u;
    }
    uint32_t sum = 0u;
    for (unsigned k = 0; k < BAT_SAMPLES; ++k) {
        int raw = 0;
        int mv = 0;
        if (adc_oneshot_read(s_unit, s_chan, &raw) != ESP_OK
            || adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) {
            return 0u;
        }
        sum += (uint32_t)(mv < 0 ? 0 : mv);
    }
    return node_bat_mv(sum / BAT_SAMPLES);
}
