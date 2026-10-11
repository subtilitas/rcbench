/*
 * The KST wire on one RP2350 pin.  See kst_line.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "kst_line.h"

#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "pico.h"

#include "kst_line.pio.h"

/* The instruction the state machine stalls on between transactions. */
#define IDLE_PC 1u

/* A transfer count's low 28 bits are the count; the rest is the mode. */
#define DMA_COUNT_MASK 0x0FFFFFFFu

static PIO line_pio(const kst_line_t *l)
{
    return (PIO)l->pio;
}

/* The state machine waits for a stream: it stalls on the first `out`, the
 * FIFO is empty and the channel has handed over every word.  A stream is 6
 * words or more and the FIFO and the shift register hold 5, so this is
 * false from the start of a transfer to the end of the capture. */
static bool line_idle(const kst_line_t *l)
{
    const PIO pio = line_pio(l);

    return !dma_channel_is_busy((uint)l->dma_tx)
           && pio_sm_is_tx_fifo_empty(pio, l->sm)
           && pio_sm_get_pc(pio, l->sm) == l->offset + IDLE_PC;
}

/* Put the state machine at the start of the program with the pad driven
 * low, whatever it was doing. */
static void line_restart(kst_line_t *l)
{
    const PIO pio = line_pio(l);

    pio_sm_set_enabled(pio, l->sm, false);
    dma_channel_abort((uint)l->dma_tx);
    dma_channel_abort((uint)l->dma_rx);
    pio_sm_clear_fifos(pio, l->sm);
    pio_sm_restart(pio, l->sm);
    pio_sm_exec(pio, l->sm, pio_encode_set(pio_pins, 0));
    pio_sm_exec(pio, l->sm, pio_encode_set(pio_pindirs, 1));
    pio_sm_exec(pio, l->sm, pio_encode_jmp(l->offset));
    pio_sm_set_enabled(pio, l->sm, true);
}

void kst_line_init(kst_line_t *line)
{
    if (line == NULL) {
        return;
    }
    memset(line, 0, sizeof(*line));
    line->dma_tx = -1;
    line->dma_rx = -1;
}

kst_line_err_t kst_line_open(kst_line_t *line, unsigned pin)
{
    kst_pio_clock_t clock;
    PIO pio;
    uint sm;
    uint offset;
    int tx;
    int rx;

    if (line == NULL || pin >= NUM_BANK0_GPIOS) {
        return KST_LINE_ERR_ARG;
    }
    kst_line_close(line);
    if (!kst_pio_clock(clock_get_hz(clk_sys), &clock)) {
        return KST_LINE_ERR_CLOCK;
    }
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            &kst_line_program, &pio, &sm, &offset, pin, 1, true)) {
        return KST_LINE_ERR_PIO;
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
        pio_remove_program_and_unclaim_sm(&kst_line_program, pio, sm, offset);
        return KST_LINE_ERR_DMA;
    }
    line->clock = clock;
    line->pio = pio;
    line->sm = sm;
    line->offset = offset;
    line->dma_tx = (int8_t)tx;
    line->dma_rx = (int8_t)rx;
    line->pin = (uint8_t)pin;
    line->busy = 0;

    /* The pad is the OUT, SET and JMP pin.  Both shift registers shift
     * left; the transmit side pulls at 32 bits, the receive side pushes
     * only where the program says. */
    pio_sm_config c = kst_line_program_get_default_config(offset);
    sm_config_set_out_pins(&c, pin, 1);
    sm_config_set_set_pins(&c, pin, 1);
    sm_config_set_jmp_pin(&c, pin);
    sm_config_set_out_shift(&c, false, true, 32);
    sm_config_set_in_shift(&c, false, false, 32);
    sm_config_set_clkdiv_int_frac(&c, 1, 0);
    pio_sm_init(pio, sm, offset, &c);

    /* The state machine's latch low and driving before the pad is its:
     * the pad goes from whatever it was to driven low, never high. */
    pio_sm_exec(pio, sm, pio_encode_set(pio_pins, 0));
    pio_sm_exec(pio, sm, pio_encode_set(pio_pindirs, 1));
    gpio_set_pulls(pin, false, true);
    pio_gpio_init(pio, pin);
    pio_sm_set_enabled(pio, sm, true);
    line->open = 1;
    return KST_LINE_OK;
}

void kst_line_close(kst_line_t *line)
{
    if (line == NULL || line->open == 0u) {
        return;
    }
    const PIO pio = line_pio(line);

    /* The pad first: an input with its pull-down, so nothing the state
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
    pio_remove_program_and_unclaim_sm(&kst_line_program, pio, line->sm,
                                      line->offset);
    line->dma_tx = -1;
    line->dma_rx = -1;
    line->open = 0;
    line->busy = 0;
}

bool kst_line_is_open(const kst_line_t *line)
{
    return line != NULL && line->open != 0u;
}

bool kst_line_busy(const kst_line_t *line)
{
    return line != NULL && line->busy != 0u;
}

uint32_t kst_line_half_cell_ns(const kst_line_t *line)
{
    return kst_line_is_open(line) ? kst_pio_half_cell_ns(&line->clock) : 0u;
}

bool kst_line_start(void *ctx, const kst_frame_t *frame, uint32_t window_ns)
{
    kst_line_t *l = (kst_line_t *)ctx;

    if (l == NULL || l->open == 0u || l->busy != 0u || !line_idle(l)) {
        return false;
    }
    const PIO pio = line_pio(l);
    const unsigned n = kst_pio_tx(&l->clock, frame, window_ns, l->tx, &l->run);

    if (n == 0u) {
        return false;
    }
    pio_sm_clear_fifos(pio, l->sm);

    /* The stamps first, so the first one has a place. */
    dma_channel_config rc = dma_channel_get_default_config((uint)l->dma_rx);
    channel_config_set_transfer_data_size(&rc, DMA_SIZE_32);
    channel_config_set_read_increment(&rc, false);
    channel_config_set_write_increment(&rc, true);
    channel_config_set_dreq(&rc, pio_get_dreq(pio, l->sm, false));
    dma_channel_configure((uint)l->dma_rx, &rc, l->rx, &pio->rxf[l->sm],
                          KST_PIO_RX_WORDS, true);

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

bool kst_line_poll(void *ctx, kst_capture_t *out)
{
    kst_line_t *l = (kst_line_t *)ctx;

    if (l == NULL || out == NULL || l->open == 0u || l->busy == 0u) {
        return false;
    }
    /* The window by the clock, with 2 us for the hand-over and the
     * rounding. */
    const uint32_t total_us = (l->run.lead_ns + 999u) / 1000u
                              + (l->run.window_ns + 999u) / 1000u + 2u;
    const uint32_t elapsed = time_us_32() - l->t_start_us;

    if (elapsed < total_us) {
        return false;
    }
    bool lost = false;

    if (!line_idle(l)) {
        if (elapsed - total_us < KST_LINE_GRACE_US) {
            return false;
        }
        lost = true;
    }
    /* The stamps so far, read before a restart clears the channel. */
    const uint32_t left = dma_channel_hw_addr((uint)l->dma_rx)->transfer_count
                          & DMA_COUNT_MASK;
    const unsigned n = left >= KST_PIO_RX_WORDS
                           ? 0u : KST_PIO_RX_WORDS - (unsigned)left;

    if (lost) {
        line_restart(l);
    } else {
        dma_channel_abort((uint)l->dma_rx);
    }
    kst_pio_capture(&l->clock, &l->run, l->rx, n,
                    lost || n >= KST_PIO_RX_WORDS, out);
    l->busy = 0;
    return true;
}

uint32_t kst_line_now_us(void *ctx)
{
    (void)ctx;
    return time_us_32();
}

kst_driver_t kst_line_driver(kst_line_t *line)
{
    kst_driver_t d;

    d.ctx = line;
    d.start = kst_line_start;
    d.poll = kst_line_poll;
    d.now_us = kst_line_now_us;
    return d;
}
