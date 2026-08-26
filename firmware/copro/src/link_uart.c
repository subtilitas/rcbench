#include "link_uart.h"

#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"

#include "copro_pins.h"
#include "link_wire.h"

static absolute_time_t s_last_rx;

bool link_uart_init(uint32_t baud)
{
    if (baud < LINK_BAUD_FLOOR) {
        return false;
    }

    uart_init(COPRO_LINK_UART, baud);
    gpio_set_function(COPRO_LINK_PIN_TX, GPIO_FUNC_UART);
    gpio_set_function(COPRO_LINK_PIN_RX, GPIO_FUNC_UART);
    uart_set_format(COPRO_LINK_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(COPRO_LINK_UART, true);
    /* Nothing to arbitrate: this end never speaks unsolicited. */
    uart_set_hw_flow(COPRO_LINK_UART, false, false);

    gpio_init(COPRO_LINK_PIN_DE);
    gpio_set_dir(COPRO_LINK_PIN_DE, GPIO_OUT);
    gpio_put(COPRO_LINK_PIN_DE, 0);   /* receive: the safe default */

    s_last_rx = get_absolute_time();
    return true;
}

void link_uart_write(const uint8_t *frame, size_t len)
{
    if (frame == NULL || len == 0) {
        return;
    }

    gpio_put(COPRO_LINK_PIN_DE, 1);
    uart_write_blocking(COPRO_LINK_UART, frame, len);

    /*
     * uart_tx_wait_blocking spins on the UART's BUSY flag, which clears only
     * once the shift register has emptied -- not merely the FIFO.  That
     * distinction is the whole reason this call is here: releasing on an empty
     * FIFO cuts the final byte off every frame, and the far end sees a
     * truncated frame it correctly refuses, so the link simply never works and
     * nothing says why.
     */
    uart_tx_wait_blocking(COPRO_LINK_UART);
    gpio_put(COPRO_LINK_PIN_DE, 0);
}

int link_uart_read_byte(uint32_t timeout_us)
{
    if (!uart_is_readable_within_us(COPRO_LINK_UART, timeout_us)) {
        return -1;
    }
    const uint8_t byte = uart_getc(COPRO_LINK_UART);
    s_last_rx = get_absolute_time();
    return (int)byte;
}

uint32_t link_uart_since_last_rx_us(void)
{
    return (uint32_t)absolute_time_diff_us(s_last_rx, get_absolute_time());
}
