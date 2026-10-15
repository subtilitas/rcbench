/*
 * S.BUS and S.BUS2 on one RP2350 pin.  See sbus_line.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sbus_line.h"

#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "pico.h"

#include "sbus_line.pio.h"

/* The instruction the state machine stalls on between transactions. */
#define IDLE_PC 1u

/* A transfer count's low 28 bits are the count; the rest is the mode. */
#define DMA_COUNT_MASK 0x0FFFFFFFu

static PIO line_pio(const sbus_line_t *l)
{
    return (PIO)l->pio;
}

/* The state machine waits for a stream: it stalls on the first `out`, the
 * FIFO is empty and the channel has handed over every word.  A stream is 3
 * words or more and the program reads its count before the first bit, so
 * this is false from the start of a transfer to the end of the window. */
static bool line_idle(const sbus_line_t *l)
{
    const PIO pio = line_pio(l);

    return !dma_channel_is_busy((uint)l->dma_tx)
           && pio_sm_is_tx_fifo_empty(pio, l->sm)
           && pio_sm_get_pc(pio, l->sm) == l->offset + IDLE_PC;
}

/* Put the state machine at the start of the program with the pad driving
 * its latch at the idle level, whatever it was doing. */
static void line_restart(sbus_line_t *l)
{
    const PIO pio = line_pio(l);

    pio_sm_set_enabled(pio, l->sm, false);
    dma_channel_abort((uint)l->dma_tx);
    dma_channel_abort((uint)l->dma_rx);
    pio_sm_clear_fifos(pio, l->sm);
    pio_sm_restart(pio, l->sm);
    pio_sm_exec(pio, l->sm, pio_encode_set(pio_pins, 1));
    pio_sm_exec(pio, l->sm, pio_encode_set(pio_pindirs, 1));
    pio_sm_exec(pio, l->sm, pio_encode_jmp(l->offset));
    pio_sm_set_enabled(pio, l->sm, true);
}

void sbus_line_init(sbus_line_t *line)
{
    if (line == NULL) {
        return;
    }
    memset(line, 0, sizeof(*line));
    line->dma_tx = -1;
    line->dma_rx = -1;
}

sbus_line_err_t sbus_line_open(sbus_line_t *line, unsigned pin)
{
    sbus_pio_clock_t clock;
    PIO pio;
    uint sm;
    uint offset;
    int tx;
    int rx;

    if (line == NULL || pin >= NUM_BANK0_GPIOS) {
        return SBUS_LINE_ERR_ARG;
    }
    sbus_line_close(line);
    if (!sbus_pio_clock(clock_get_hz(clk_sys), &clock)) {
        return SBUS_LINE_ERR_CLOCK;
    }
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            &sbus_line_program, &pio, &sm, &offset, pin, 1, true)) {
        return SBUS_LINE_ERR_PIO;
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
        pio_remove_program_and_unclaim_sm(&sbus_line_program, pio, sm,
                                          offset);
        return SBUS_LINE_ERR_DMA;
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
     * right; the transmit side pulls at 32 bits, the receive side pushes
     * only where the program says. */
    pio_sm_config c = sbus_line_program_get_default_config(offset);
    sm_config_set_out_pins(&c, pin, 1);
    sm_config_set_set_pins(&c, pin, 1);
    sm_config_set_in_pins(&c, pin);
    sm_config_set_jmp_pin(&c, pin);
    sm_config_set_out_shift(&c, true, true, 32);
    sm_config_set_in_shift(&c, true, false, 32);
    sm_config_set_clkdiv_int_frac8(&c, clock.div_int, clock.div_frac);
    pio_sm_init(pio, sm, offset, &c);

    /* The pad goes from whatever it was to an input held low by its
     * pull-down, then to driven low; never high.  Selecting the PIO
     * function clears the overrides, so they are set after it, while the
     * state machine does not drive. */
    pio_sm_exec(pio, sm, pio_encode_set(pio_pins, 1));
    pio_sm_exec(pio, sm, pio_encode_set(pio_pindirs, 0));
    gpio_set_pulls(pin, false, true);
    pio_gpio_init(pio, pin);
    gpio_set_outover(pin, GPIO_OVERRIDE_INVERT);
    gpio_set_inover(pin, GPIO_OVERRIDE_INVERT);
    pio_sm_exec(pio, sm, pio_encode_set(pio_pindirs, 1));
    pio_sm_set_enabled(pio, sm, true);
    line->open = 1;
    return SBUS_LINE_OK;
}

void sbus_line_close(sbus_line_t *line)
{
    if (line == NULL || line->open == 0u) {
        return;
    }
    const PIO pio = line_pio(line);

    /* The pad first: an input with its pull-down and both overrides off
     * (gpio_init() selects SIO and clears them), so nothing the state
     * machine does while it is taken down reaches the wire. */
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
    pio_remove_program_and_unclaim_sm(&sbus_line_program, pio, line->sm,
                                      line->offset);
    line->dma_tx = -1;
    line->dma_rx = -1;
    line->open = 0;
    line->busy = 0;
}

bool sbus_line_is_open(const sbus_line_t *line)
{
    return line != NULL && line->open != 0u;
}

bool sbus_line_busy(const sbus_line_t *line)
{
    return line != NULL && line->busy != 0u;
}

bool sbus_line_start(void *ctx, const uint8_t *frame, uint32_t window_us)
{
    sbus_line_t *l = (sbus_line_t *)ctx;

    if (l == NULL || frame == NULL || l->open == 0u || l->busy != 0u
        || !line_idle(l)) {
        return false;
    }
    const PIO pio = line_pio(l);
    const unsigned n = sbus_pio_tx(&l->clock, frame, SBUS_FRAME_BYTES,
                                   window_us, l->tx, &l->run);

    if (n == 0u) {
        return false;
    }
    pio_sm_clear_fifos(pio, l->sm);

    /* The receive channel first, so the first byte has a place. */
    dma_channel_config rc = dma_channel_get_default_config((uint)l->dma_rx);
    channel_config_set_transfer_data_size(&rc, DMA_SIZE_32);
    channel_config_set_read_increment(&rc, false);
    channel_config_set_write_increment(&rc, true);
    channel_config_set_dreq(&rc, pio_get_dreq(pio, l->sm, false));
    dma_channel_configure((uint)l->dma_rx, &rc, l->rx, &pio->rxf[l->sm],
                          SBUS_PIO_RX_WORDS, true);

    dma_channel_config tc = dma_channel_get_default_config((uint)l->dma_tx);
    channel_config_set_transfer_data_size(&tc, DMA_SIZE_32);
    channel_config_set_read_increment(&tc, true);
    channel_config_set_write_increment(&tc, false);
    channel_config_set_dreq(&tc, pio_get_dreq(pio, l->sm, true));
    l->t_start_us = time_us_32();
    dma_channel_configure((uint)l->dma_tx, &tc, &pio->txf[l->sm], l->tx, n,
                          true);
    l->busy = 1;
    return true;
}

bool sbus_line_poll(void *ctx, sbus2_capture_t *out)
{
    sbus_line_t *l = (sbus_line_t *)ctx;

    if (l == NULL || l->open == 0u || l->busy == 0u) {
        return false;
    }
    /* The frame and the window by the clock, with 2 us for the hand-over
     * and the rounding. */
    const uint32_t total_us = (l->run.lead_ns + 999u) / 1000u
                              + l->run.window_us + 2u;
    const uint32_t elapsed = time_us_32() - l->t_start_us;

    if (elapsed < total_us) {
        return false;
    }
    bool lost = false;

    if (!line_idle(l)) {
        /* A window the received bytes made longer ends at the time asked;
         * a frame without a window should have ended, and gets the grace
         * before it is cut. */
        if (l->run.window_us == 0u && elapsed - total_us < SBUS_LINE_GRACE_US) {
            return false;
        }
        lost = l->run.window_us == 0u;
    }
    /* The words so far, read before a restart clears the channel. */
    const uint32_t left = dma_channel_hw_addr((uint)l->dma_rx)->transfer_count
                          & DMA_COUNT_MASK;
    const unsigned n = left >= SBUS_PIO_RX_WORDS
                           ? 0u : SBUS_PIO_RX_WORDS - (unsigned)left;

    if (!line_idle(l)) {
        line_restart(l);
    } else {
        dma_channel_abort((uint)l->dma_rx);
    }
    if (out != NULL) {
        sbus_pio_capture(&l->clock, &l->run, l->rx, n,
                         lost || n >= SBUS_PIO_RX_WORDS, out);
    }
    l->busy = 0;
    return true;
}

uint32_t sbus_line_now_us(void *ctx)
{
    (void)ctx;
    return time_us_32();
}

sbus_driver_t sbus_line_driver(sbus_line_t *line)
{
    sbus_driver_t d;

    d.ctx = line;
    d.start = sbus_line_start;
    d.poll = sbus_line_poll;
    d.now_us = sbus_line_now_us;
    return d;
}
