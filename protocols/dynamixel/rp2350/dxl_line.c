/*
 * A bus of ROBOTIS Dynamixel servos on one RP2350 pin.  See dxl_line.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "dxl_line.h"

#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "pico.h"

#include "dxl_line.pio.h"

/* A transfer count's low 28 bits are the count; the rest is the mode. */
#define DMA_COUNT_MASK 0x0FFFFFFFu

/* The received byte: bits 31 to 24 of the receive FIFO's word. */
#define RX_BYTE_LANE 3u

static PIO line_pio(const dxl_line_t *l)
{
    return (PIO)l->pio;
}

/* The divider into the state machine; the line is not busy. */
static void apply_divider(const dxl_line_t *l)
{
    const PIO pio = line_pio(l);

    /* 65536 is the register's integer 0. */
    pio_sm_set_clkdiv_int_frac8(pio, l->sm, l->div.div_int & 0xFFFFu,
                                l->div.div_frac);
    pio_sm_clkdiv_restart(pio, l->sm);
}

/* The state machine at `listen` with the pad an input, whatever it was doing. */
static void line_listen(const dxl_line_t *l)
{
    const PIO pio = line_pio(l);

    pio_sm_set_enabled(pio, l->sm, false);
    pio_sm_exec(pio, l->sm, pio_encode_set(pio_pindirs, 0));
    pio_sm_clear_fifos(pio, l->sm);
    pio_sm_restart(pio, l->sm);
    pio_sm_exec(pio, l->sm,
                pio_encode_jmp(l->offset + (uint)dxl_line_offset_listen));
    pio_sm_set_enabled(pio, l->sm, true);
}

void dxl_line_init(dxl_line_t *line)
{
    if (line == NULL) {
        return;
    }
    memset(line, 0, sizeof(*line));
    line->dma_tx = -1;
    line->dma_rx = -1;
}

dxl_line_err_t dxl_line_open(dxl_line_t *line, unsigned pin, uint32_t baud)
{
    dxl_pio_div_t div;
    PIO pio;
    uint sm;
    uint offset;
    int tx;
    int rx;

    if (line == NULL || pin >= NUM_BANK0_GPIOS) {
        return DXL_LINE_ERR_ARG;
    }
    dxl_line_close(line);
    if (!dxl_pio_divider(clock_get_hz(clk_sys), baud, &div)) {
        return DXL_LINE_ERR_RATE;
    }
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            &dxl_line_program, &pio, &sm, &offset, pin, 1, true)) {
        return DXL_LINE_ERR_PIO;
    }
    tx = dma_claim_unused_channel(false);
    rx = dma_claim_unused_channel(false);
    if (tx < 0 || rx < 0) {
        if (tx >= 0) {
            dma_channel_unclaim((uint)tx);
        }
        if (rx >= 0) {
            dma_channel_unclaim((uint)rx);
        }
        pio_remove_program_and_unclaim_sm(&dxl_line_program, pio, sm, offset);
        return DXL_LINE_ERR_DMA;
    }
    line->div = div;
    line->baud = baud;
    line->pio = pio;
    line->sm = sm;
    line->offset = offset;
    line->dma_tx = (int8_t)tx;
    line->dma_rx = (int8_t)rx;
    line->pin = (uint8_t)pin;
    line->busy = 0;
    line->n_read = 0;

    /* The pad is the OUT, SET, IN and JMP pin.  Both shift registers shift
     * right, so bits go out and come in least significant first; the
     * program pulls and pushes itself. */
    pio_sm_config c = dxl_line_program_get_default_config(offset);
    sm_config_set_out_pins(&c, pin, 1);
    sm_config_set_set_pins(&c, pin, 1);
    sm_config_set_in_pins(&c, pin);
    sm_config_set_jmp_pin(&c, pin);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_in_shift(&c, true, false, 32);
    sm_config_set_clkdiv_int_frac8(&c, div.div_int & 0xFFFFu, div.div_frac);
    pio_sm_init(pio, sm, offset, &c);

    /* An input with its pull-up before the pad is the state machine's. */
    pio_sm_exec(pio, sm, pio_encode_set(pio_pins, 1));
    pio_sm_exec(pio, sm, pio_encode_set(pio_pindirs, 0));
    gpio_set_pulls(pin, true, false);
    pio_gpio_init(pio, pin);
    line->open = 1;
    line_listen(line);
    return DXL_LINE_OK;
}

void dxl_line_close(dxl_line_t *line)
{
    if (line == NULL || line->open == 0u) {
        return;
    }
    const PIO pio = line_pio(line);

    /* The pad first: an input with its pull-up, so nothing the state
     * machine does while it is taken down reaches the wire. */
    gpio_init(line->pin);
    gpio_set_dir(line->pin, GPIO_IN);
    gpio_set_pulls(line->pin, true, false);

    pio_sm_set_enabled(pio, line->sm, false);
    dma_channel_abort((uint)line->dma_tx);
    dma_channel_abort((uint)line->dma_rx);
    dma_channel_unclaim((uint)line->dma_tx);
    dma_channel_unclaim((uint)line->dma_rx);
    pio_sm_clear_fifos(pio, line->sm);
    pio_sm_restart(pio, line->sm);
    pio_remove_program_and_unclaim_sm(&dxl_line_program, pio, line->sm,
                                      line->offset);
    line->dma_tx = -1;
    line->dma_rx = -1;
    line->open = 0;
    line->busy = 0;
}

bool dxl_line_is_open(const dxl_line_t *line)
{
    return line != NULL && line->open != 0u;
}

bool dxl_line_set_baud(void *ctx, uint32_t baud)
{
    dxl_line_t *l = (dxl_line_t *)ctx;
    dxl_pio_div_t div;

    if (l == NULL || l->open == 0u || l->busy != 0u
        || !dxl_pio_divider(clock_get_hz(clk_sys), baud, &div)) {
        return false;
    }
    l->div = div;
    l->baud = baud;
    apply_divider(l);
    return true;
}

bool dxl_line_start(void *ctx, const uint8_t *tx, size_t n)
{
    dxl_line_t *l = (dxl_line_t *)ctx;

    if (l == NULL || l->open == 0u || l->busy != 0u) {
        return false;
    }
    const size_t words = dxl_pio_tx(tx, n, l->tx, sizeof(l->tx));

    if (words == 0u) {
        return false;
    }
    const PIO pio = line_pio(l);

    pio_sm_set_enabled(pio, l->sm, false);
    dma_channel_abort((uint)l->dma_tx);
    dma_channel_abort((uint)l->dma_rx);
    pio_sm_clear_fifos(pio, l->sm);
    pio_sm_restart(pio, l->sm);
    pio_sm_exec(pio, l->sm,
                pio_encode_jmp(l->offset + (uint)dxl_line_offset_send));

    /* The reply first, so its first byte has a place.  A byte write to the
     * transmit FIFO repeats the byte in all four lanes; the program uses
     * the low one. */
    dma_channel_config rc = dma_channel_get_default_config((uint)l->dma_rx);
    channel_config_set_transfer_data_size(&rc, DMA_SIZE_8);
    channel_config_set_read_increment(&rc, false);
    channel_config_set_write_increment(&rc, true);
    channel_config_set_dreq(&rc, pio_get_dreq(pio, l->sm, false));
    dma_channel_configure((uint)l->dma_rx, &rc, l->rx,
                          (const volatile uint8_t *)&pio->rxf[l->sm]
                              + RX_BYTE_LANE,
                          DXL_LINE_RX_BYTES, true);

    dma_channel_config tc = dma_channel_get_default_config((uint)l->dma_tx);
    channel_config_set_transfer_data_size(&tc, DMA_SIZE_8);
    channel_config_set_read_increment(&tc, true);
    channel_config_set_write_increment(&tc, false);
    channel_config_set_dreq(&tc, pio_get_dreq(pio, l->sm, true));
    dma_channel_configure((uint)l->dma_tx, &tc, &pio->txf[l->sm], l->tx,
                          (uint)words, true);
    l->n_read = 0;
    l->busy = 1;
    pio_sm_set_enabled(pio, l->sm, true);
    return true;
}

size_t dxl_line_poll(void *ctx, uint8_t *out, size_t cap)
{
    dxl_line_t *l = (dxl_line_t *)ctx;

    if (l == NULL || out == NULL || l->open == 0u || l->busy == 0u) {
        return 0;
    }
    const uint32_t left = dma_channel_hw_addr((uint)l->dma_rx)->transfer_count
                          & DMA_COUNT_MASK;
    const size_t got = left >= DXL_LINE_RX_BYTES
                           ? 0u : DXL_LINE_RX_BYTES - (size_t)left;
    size_t n = got > l->n_read ? got - l->n_read : 0u;

    if (n > cap) {
        n = cap;
    }
    memcpy(out, &l->rx[l->n_read], n);
    l->n_read = (uint16_t)(l->n_read + n);
    return n;
}

void dxl_line_stop(void *ctx)
{
    dxl_line_t *l = (dxl_line_t *)ctx;

    if (l == NULL || l->open == 0u) {
        return;
    }
    dma_channel_abort((uint)l->dma_tx);
    dma_channel_abort((uint)l->dma_rx);
    line_listen(l);
    l->busy = 0;
}

uint32_t dxl_line_now_us(void *ctx)
{
    (void)ctx;
    return time_us_32();
}

dxl_driver_t dxl_line_driver(dxl_line_t *line)
{
    dxl_driver_t d;

    d.ctx = line;
    d.set_baud = dxl_line_set_baud;
    d.start = dxl_line_start;
    d.poll = dxl_line_poll;
    d.stop = dxl_line_stop;
    d.now_us = dxl_line_now_us;
    return d;
}
