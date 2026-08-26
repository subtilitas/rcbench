#include "link_uart.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "link_frame.h"
#include "link_wire.h"

static const char *TAG = "link_uart";

/* Room for a few whole frames, so a burst does not overrun while the UI is
 * drawing a frame of its own.  The panel's loop paces itself against a 39 Hz
 * panel, which is an age beside a 72-byte frame. */
#define RX_BUFFER (8 * LINK_MAX_FRAME)
#define TX_BUFFER (2 * LINK_MAX_FRAME)

static uart_port_t s_port = UART_NUM_1;

esp_err_t link_uart_init(uart_port_t port, uint32_t baud,
                         gpio_num_t tx, gpio_num_t rx)
{
    s_port = port;

    if (baud < LINK_BAUD_FLOOR) {
        ESP_LOGE(TAG, "%u baud is below the direction circuit's floor of %u; "
                      "the driver would switch off mid-frame",
                 (unsigned)baud, (unsigned)LINK_BAUD_FLOOR);
        return ESP_ERR_INVALID_ARG;
    }

    const uart_config_t cfg = {
        .baud_rate = (int)baud,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        /* No flow control of any kind.  RTS would be the direction line on a
         * board that had one; this one does not, and asserting it would drive
         * a pin that goes nowhere. */
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(s_port, RX_BUFFER,
                                        TX_BUFFER, 0, NULL, 0);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_param_config(s_port, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_set_pin(s_port, tx, rx, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "TX=GPIO%d RX=GPIO%d at %u baud, board-switched direction",
             (int)tx, (int)rx, (unsigned)baud);
    return ESP_OK;
}

int link_uart_write(const uint8_t *frame, size_t len)
{
    if (frame == NULL || len == 0) {
        return -1;
    }
    /*
     * Blocking rather than queued.  The link is strictly host-polled, so there
     * is never a second frame waiting behind this one, and returning before
     * the bytes are gone would only make the caller invent a way to find out
     * when they had.
     */
    const int n = uart_write_bytes(s_port, frame, len);
    if (n > 0) {
        (void)uart_wait_tx_done(s_port, pdMS_TO_TICKS(50));
    }
    return n;
}

int link_uart_read(uint8_t *out, size_t max, uint32_t timeout_ms)
{
    if (out == NULL || max == 0) {
        return -1;
    }
    return uart_read_bytes(s_port, out, (uint32_t)max,
                           pdMS_TO_TICKS(timeout_ms));
}

void link_uart_flush(void)
{
    (void)uart_flush_input(s_port);
}
