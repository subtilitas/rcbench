/*
 * The Feetech bus on one RP2350 pin.  See ft_line.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ft_line.h"

#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "pico.h"

#include "ft_line.pio.h"

/* A transfer count's low 28 bits are the count; the rest is the mode. */
#define DMA_COUNT_MASK 0x0FFFFFFFu

static PIO line_pio(const ft_line_t *l)
{
    return (PIO)l->pio;
}

/* The state machine at `idle` with the pad an input, whatever it was
 * doing, and both channels stopped. */
static void line_restart(ft_line_t *l)
{
    const PIO pio = line_pio(l);

    pio_sm_set_enabled(pio, l->sm, false);
    pio_sm_exec(pio, l->sm, pio_encode_set(pio_pindirs, 0));
    dma_channel_abort((uint)l->dma_tx);
    dma_channel_abort((uint)l->dma_rx);
    pio_sm_clear_fifos(pio, l->sm);
    pio_sm_restart(pio, l->sm);
    pio_sm_exec(pio, l->sm, pio_encode_jmp(l->offset));
    pio_sm_set_enabled(pio, l->sm, true);
}

void ft_line_init(ft_line_t *line)
{
    if (line == NULL) {
        return;
    }
    memset(line, 0, sizeof(*line));
    line->dma_tx = -1;
    line->dma_rx = -1;
}

ft_line_err_t ft_line_open(ft_line_t *line, unsigned pin, uint32_t baud)
{
    feetech_pio_clock_t clock;
    PIO pio;
    uint sm;
    uint offset;
    int tx;
    int rx;

    if (line == NULL || pin >= NUM_BANK0_GPIOS) {
        return FT_LINE_ERR_ARG;
    }
    ft_line_close(line);
    if (!feetech_pio_clock(clock_get_hz(clk_sys), baud, &clock)) {
        return FT_LINE_ERR_CLOCK;
    }
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            &ft_line_program, &pio, &sm, &offset, pin, 1, true)) {
        return FT_LINE_ERR_PIO;
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
        pio_remove_program_and_unclaim_sm(&ft_line_program, pio, sm, offset);
        return FT_LINE_ERR_DMA;
    }
    line->clock = clock;
    line->pio = pio;
    line->sm = sm;
    line->offset = offset;
    line->dma_tx = (int8_t)tx;
    line->dma_rx = (int8_t)rx;
    line->pin = (uint8_t)pin;
    line->busy = 0;

    /* The pad is the OUT, SET, IN and JMP pin.  Both shift registers shift
     * right: bytes go out from bit 0, and 9 bits in push a word. */
    pio_sm_config c = ft_line_program_get_default_config(offset);
    sm_config_set_out_pins(&c, pin, 1);
    sm_config_set_set_pins(&c, pin, 1);
    sm_config_set_in_pins(&c, pin);
    sm_config_set_jmp_pin(&c, pin);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_in_shift(&c, true, true, 9);
    sm_config_set_clkdiv_int_frac(&c, clock.div_int, clock.div_frac);
    pio_sm_init(pio, sm, offset, &c);

    /* An input before the pad is the state machine's, the latch high: the
     * pad goes from whatever it was to an input pulled up. */
    pio_sm_exec(pio, sm, pio_encode_set(pio_pins, 1));
    pio_sm_exec(pio, sm, pio_encode_set(pio_pindirs, 0));
    gpio_set_pulls(pin, true, false);
    pio_gpio_init(pio, pin);
    pio_sm_set_enabled(pio, sm, true);
    line->open = 1;
    return FT_LINE_OK;
}

void ft_line_close(ft_line_t *line)
{
    if (line == NULL || line->open == 0u) {
        return;
    }
    const PIO pio = line_pio(line);

    /* The pad first: an input with its pull-down, as the pin is between
     * its uses. */
    gpio_init(line->pin);
    gpio_set_dir(line->pin, GPIO_IN);
    gpio_set_pulls(line->pin, false, true);

    pio_sm_set_enabled(pio, line->sm, false);
    dma_channel_abort((uint)line->dma_tx);
    dma_channel_abort((uint)line->dma_rx);
    dma_channel_unclaim((uint)line->dma_tx);
    dma_channel_unclaim((uint)line->dma_rx);
    pio_sm_clear_fifos(pio, line->sm);
    pio_sm_restart(pio, line->sm);
    pio_remove_program_and_unclaim_sm(&ft_line_program, pio, line->sm,
                                      line->offset);
    line->dma_tx = -1;
    line->dma_rx = -1;
    line->open = 0;
    line->busy = 0;
}

bool ft_line_is_open(const ft_line_t *line)
{
    return line != NULL && line->open != 0u;
}

uint32_t ft_line_baud(const ft_line_t *line)
{
    return ft_line_is_open(line) ? line->clock.baud : 0u;
}

bool ft_line_start(void *ctx, const uint8_t *tx, unsigned n_tx, unsigned n_rx,
                   uint32_t wait_us)
{
    ft_line_t *l = (ft_line_t *)ctx;

    if (l == NULL || l->open == 0u || l->busy != 0u
        || n_rx > FEETECH_CAPTURE_MAX) {
        return false;
    }
    const unsigned n = feetech_pio_tx(tx, n_tx, l->tx);

    if (n == 0u) {
        return false;
    }
    const PIO pio = line_pio(l);

    line_restart(l);

    /* The bytes first, so the first one has a place. */
    dma_channel_config rc = dma_channel_get_default_config((uint)l->dma_rx);
    channel_config_set_transfer_data_size(&rc, DMA_SIZE_32);
    channel_config_set_read_increment(&rc, false);
    channel_config_set_write_increment(&rc, true);
    channel_config_set_dreq(&rc, pio_get_dreq(pio, l->sm, false));
    dma_channel_configure((uint)l->dma_rx, &rc, l->rx, &pio->rxf[l->sm],
                          FEETECH_CAPTURE_MAX, true);

    dma_channel_config tc = dma_channel_get_default_config((uint)l->dma_tx);
    channel_config_set_transfer_data_size(&tc, DMA_SIZE_32);
    channel_config_set_read_increment(&tc, true);
    channel_config_set_write_increment(&tc, false);
    channel_config_set_dreq(&tc, pio_get_dreq(pio, l->sm, true));
    l->t_start_us = time_us_32();
    /* The frame, the wait and 2 us for the hand-over and the rounding. */
    l->total_us = feetech_pio_tx_us(&l->clock, n_tx)
                  + (n_rx > 0u ? wait_us : 0u) + 2u;
    l->n_rx = (uint8_t)n_rx;
    dma_channel_configure((uint)l->dma_tx, &tc, &pio->txf[l->sm], l->tx, n,
                          true);
    l->busy = 1;
    return true;
}

bool ft_line_poll(void *ctx, feetech_capture_t *out)
{
    ft_line_t *l = (ft_line_t *)ctx;

    if (l == NULL || out == NULL || l->open == 0u || l->busy == 0u) {
        return false;
    }
    const uint32_t left = dma_channel_hw_addr((uint)l->dma_rx)->transfer_count
                          & DMA_COUNT_MASK;
    const unsigned got = left >= FEETECH_CAPTURE_MAX
                             ? 0u : FEETECH_CAPTURE_MAX - (unsigned)left;
    const uint32_t elapsed = time_us_32() - l->t_start_us;

    if ((l->n_rx == 0u || got < l->n_rx) && elapsed < l->total_us) {
        return false;
    }
    /* The pad an input and the state machine at `idle` before the bytes
     * are read: nothing more lands in the buffer. */
    line_restart(l);
    memset(out, 0, sizeof(*out));
    for (unsigned i = 0; i < got; ++i) {
        uint8_t b = 0;

        if (!feetech_pio_rx(l->rx[i], &b)) {
            ++out->framing;
        }
        out->bytes[i] = b;
    }
    out->n = (uint8_t)got;
    out->lost = got >= FEETECH_CAPTURE_MAX ? 1u : 0u;
    l->busy = 0;
    return true;
}

bool ft_line_set_baud(void *ctx, uint32_t baud)
{
    ft_line_t *l = (ft_line_t *)ctx;
    feetech_pio_clock_t clock;

    if (l == NULL || l->open == 0u || l->busy != 0u
        || !feetech_pio_clock(clock_get_hz(clk_sys), baud, &clock)) {
        return false;
    }
    pio_sm_set_clkdiv_int_frac8(line_pio(l), l->sm, clock.div_int,
                               clock.div_frac);
    l->clock = clock;
    return true;
}

feetech_driver_t ft_line_driver(ft_line_t *line)
{
    feetech_driver_t d;

    d.ctx = line;
    d.start = ft_line_start;
    d.poll = ft_line_poll;
    d.set_baud = ft_line_set_baud;
    return d;
}
