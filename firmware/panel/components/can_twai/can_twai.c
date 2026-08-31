/*
 * SPDX-License-Identifier: MIT
 */

#include "can_twai.h"

#include <string.h>

#include "driver/twai.h"
#include "esp_log.h"

#include "board.h"
#include "board_pins.h"
#include "can_timing.h"

static const char *TAG = "can";
static bool s_running;

esp_err_t can_twai_start(uint32_t bitrate)
{
    if (s_running) {
        return ESP_OK;
    }

    can_timing_limits_t lim;
    can_timing_limits_twai(&lim);
    can_timing_t t;
    if (!can_timing_solve(&lim, bitrate, CAN_SAMPLE_POINT_LINK, &t)) {
        ESP_LOGE(TAG, "%u bit/s cannot be made exactly from the APB clock",
                 (unsigned)bitrate);
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "%u bit/s: brp %u, tseg1 %u, tseg2 %u, sjw %u, "
                  "sample point %u.%u%%",
             (unsigned)bitrate, t.div, t.tseg1, t.tseg2, t.sjw,
             t.sample_permille / 10u, t.sample_permille % 10u);

    /*
     * The multiplexer, and the cost of it: GPIO19 and GPIO20 are the native
     * USB pins, so from here until can_twai_stop() there is no USB. The
     * console is on UART because of this.
     */
    /*
     * Reported, not asserted.  Every other failure here returns esp_err_t and
     * leaves the caller to decide; aborting the whole panel because one I2C
     * transaction to the expander glitched would take the STOP button with it.
     */
    esp_err_t sel = board_select_can(true);
    if (sel != ESP_OK) {
        ESP_LOGE(TAG, "could not route the multiplexer to CAN: %s",
                 esp_err_to_name(sel));
        return sel;
    }

    const twai_general_config_t g = {
        .mode = TWAI_MODE_NORMAL,
        .tx_io = PANEL_CAN_PIN_TX,
        .rx_io = PANEL_CAN_PIN_RX,
        .clkout_io = TWAI_IO_UNUSED,
        .bus_off_io = TWAI_IO_UNUSED,
        .tx_queue_len = 8,
        .rx_queue_len = 16,
        .alerts_enabled = TWAI_ALERT_NONE,
        .clkout_divider = 0,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    const twai_timing_config_t timing = {
        .brp = t.div,
        .tseg_1 = t.tseg1,
        .tseg_2 = t.tseg2,
        .sjw = t.sjw,
        .triple_sampling = false,
    };
    /* Accept everything: two nodes, and a bring-up wants to see traffic it
     * did not expect rather than have the controller hide it. */
    const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g, &timing, &filter);
    if (err != ESP_OK) {
        board_select_can(false);
        return err;
    }
    err = twai_start();
    if (err != ESP_OK) {
        twai_driver_uninstall();
        board_select_can(false);
        return err;
    }
    s_running = true;
    return ESP_OK;
}

void can_twai_stop(void)
{
    if (!s_running) {
        return;
    }
    twai_stop();
    twai_driver_uninstall();
    board_select_can(false);
    s_running = false;
}

bool can_twai_send(const link_can_frame_t *f, uint32_t timeout_ms)
{
    if (!s_running || f == NULL || f->dlc > 8) {
        return false;
    }
    twai_message_t m;
    memset(&m, 0, sizeof(m));
    m.extd = 1;                       /* this link is extended-identifier only */
    m.identifier = f->id;
    m.data_length_code = f->dlc;
    memcpy(m.data, f->data, f->dlc);
    return twai_transmit(&m, pdMS_TO_TICKS(timeout_ms)) == ESP_OK;
}

bool can_twai_recv(link_can_frame_t *f, uint32_t timeout_ms)
{
    if (!s_running || f == NULL) {
        return false;
    }
    twai_message_t m;
    if (twai_receive(&m, pdMS_TO_TICKS(timeout_ms)) != ESP_OK) {
        return false;
    }
    /* Nothing here sends 11-bit identifiers or remote frames, so either is
     * somebody else's traffic and not something to interpret. */
    if (!m.extd || m.rtr || m.data_length_code > 8) {
        return false;
    }
    memset(f, 0, sizeof(*f));
    f->id = m.identifier;
    f->dlc = m.data_length_code;
    memcpy(f->data, m.data, m.data_length_code);
    return true;
}

void can_twai_errors(uint32_t *tx_err, uint32_t *rx_err, uint32_t *bus_err,
                     bool *bus_off)
{
    twai_status_info_t st;
    if (!s_running || twai_get_status_info(&st) != ESP_OK) {
        return;
    }
    if (tx_err != NULL)  { *tx_err = st.tx_error_counter; }
    if (rx_err != NULL)  { *rx_err = st.rx_error_counter; }
    if (bus_err != NULL) { *bus_err = st.bus_error_count; }
    if (bus_off != NULL) { *bus_off = (st.state == TWAI_STATE_BUS_OFF); }
}
