/*
 * The PD mini's PIO UART.  See pd_uart.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pd_uart.h"

#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pd_uart.pio.h"

static struct {
    bool    open;
    bool    attached;
    PIO     tx_pio, rx_pio;
    uint    tx_sm, rx_sm;
    uint    tx_off, rx_off;
    uint8_t tx, rx;
} s;

/* A pin at rest: an input with a pull-down, driven by nothing here. */
static void rest(uint8_t pin)
{
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_down(pin);
}

bool pd_uart_open(uint8_t tx, uint8_t rx, uint32_t baud)
{
    pd_uart_close();
    if (baud == 0u) {
        return false;
    }
    /*
     * The SDK picks the block, as for PPM and DShot: a block reaches 32
     * pins from a base of 0 or 16, and the pins arrive from the panel.
     */
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            &pd_uart_tx_program, &s.tx_pio, &s.tx_sm, &s.tx_off, tx, 1, true)) {
        return false;
    }
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            &pd_uart_rx_program, &s.rx_pio, &s.rx_sm, &s.rx_off, rx, 1, true)) {
        pio_remove_program_and_unclaim_sm(&pd_uart_tx_program, s.tx_pio,
                                          s.tx_sm, s.tx_off);
        return false;
    }
    pd_uart_tx_program_init(s.tx_pio, s.tx_sm, s.tx_off, tx, baud);
    pd_uart_rx_program_init(s.rx_pio, s.rx_sm, s.rx_off, rx, baud);
    s.tx       = tx;
    s.rx       = rx;
    s.open     = true;
    s.attached = false;
    rest(tx);
    rest(rx);
    return true;
}

void pd_uart_close(void)
{
    if (!s.open) {
        return;
    }
    pd_uart_detach(NULL);
    pio_remove_program_and_unclaim_sm(&pd_uart_tx_program, s.tx_pio, s.tx_sm,
                                      s.tx_off);
    pio_remove_program_and_unclaim_sm(&pd_uart_rx_program, s.rx_pio, s.rx_sm,
                                      s.rx_off);
    rest(s.tx);
    rest(s.rx);
    s.open = false;
}

/* Both machines stopped, emptied and back at their first instruction. */
static void reset_machines(void)
{
    pio_sm_set_enabled(s.tx_pio, s.tx_sm, false);
    pio_sm_set_enabled(s.rx_pio, s.rx_sm, false);
    pio_sm_clear_fifos(s.tx_pio, s.tx_sm);
    pio_sm_clear_fifos(s.rx_pio, s.rx_sm);
    pio_sm_restart(s.tx_pio, s.tx_sm);
    pio_sm_restart(s.rx_pio, s.rx_sm);
    pio_sm_exec(s.tx_pio, s.tx_sm, pio_encode_jmp(s.tx_off));
    pio_sm_exec(s.rx_pio, s.rx_sm, pio_encode_jmp(s.rx_off));
}

void pd_uart_attach(void *ctx)
{
    (void)ctx;
    if (!s.open || s.attached) {
        return;
    }
    reset_machines();
    /* The transmit line idle -- high -- before the pin is handed over, so
     * the handover is not a start bit. */
    const uint64_t txm = (uint64_t)1u << s.tx;
    pio_sm_set_pins_with_mask64(s.tx_pio, s.tx_sm, txm, txm);
    pio_sm_set_pindirs_with_mask64(s.tx_pio, s.tx_sm, txm, txm);
    pio_gpio_init(s.tx_pio, s.tx);
    /* The receive line read by the machine, undriven and unpulled: the
     * module's transmitter holds it. */
    pio_sm_set_pindirs_with_mask64(s.rx_pio, s.rx_sm, 0u,
                                   (uint64_t)1u << s.rx);
    gpio_disable_pulls(s.rx);
    pio_gpio_init(s.rx_pio, s.rx);
    pio_sm_set_enabled(s.rx_pio, s.rx_sm, true);
    pio_sm_set_enabled(s.tx_pio, s.tx_sm, true);
    s.attached = true;
}

void pd_uart_detach(void *ctx)
{
    (void)ctx;
    if (!s.open || !s.attached) {
        return;
    }
    reset_machines();
    rest(s.tx);
    rest(s.rx);
    s.attached = false;
}

void pd_uart_send(void *ctx, const uint8_t *p, size_t n)
{
    (void)ctx;
    if (!s.attached || p == NULL) {
        return;
    }
    /* A request is at most eight bytes and the joined FIFO holds eight. */
    for (size_t i = 0; i < n; ++i) {
        pio_sm_put_blocking(s.tx_pio, s.tx_sm, p[i]);
    }
}

bool pd_uart_getc(uint8_t *b)
{
    if (!s.attached || b == NULL
        || pio_sm_is_rx_fifo_empty(s.rx_pio, s.rx_sm)) {
        return false;
    }
    *b = (uint8_t)(pio_sm_get(s.rx_pio, s.rx_sm) >> 24);
    return true;
}
