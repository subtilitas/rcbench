/*
 * An EX Bus line on one RP2350 pin.  See exbus_line.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "exbus_line.h"

#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "pico.h"

#include "exbus_line.pio.h"

/* The instruction the state machine stalls on between transactions. */
#define IDLE_PC 1u

/* A transfer count's low 28 bits are the count; the rest is the mode. */
#define DMA_COUNT_MASK 0x0FFFFFFFu

static PIO line_pio(const exbus_line_t *l)
{
    return (PIO)l->pio;
}

/* The state machine waits for a stream: it stalls on the first `pull`, the
 * transmit FIFO is empty and the channel has handed over every word.  A
 * stream is 3 words or more, and from the first word to the end of the
 * window the machine is elsewhere or the FIFO holds a word. */
static bool line_idle(const exbus_line_t *l)
{
    const PIO pio = line_pio(l);

    return !dma_channel_is_busy((uint)l->dma_tx)
           && pio_sm_is_tx_fifo_empty(pio, l->sm)
           && pio_sm_get_pc(pio, l->sm) == l->offset + IDLE_PC;
}

/* Put the state machine at the start of the program with the pin driven
 * high, whatever it was doing. */
static void line_restart(exbus_line_t *l)
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

void exbus_line_init(exbus_line_t *line)
{
    if (line == NULL) {
        return;
    }
    memset(line, 0, sizeof(*line));
    line->dma_tx = -1;
    line->dma_rx = -1;
}

exbus_line_err_t exbus_line_open(exbus_line_t *line, unsigned pin,
                                 uint32_t baud)
{
    exbus_pio_clock_t clock;
    PIO pio;
    uint sm;
    uint offset;
    int tx;
    int rx;

    if (line == NULL || pin >= NUM_BANK0_GPIOS) {
        return EXBUS_LINE_ERR_ARG;
    }
    exbus_line_close(line);
    if (!exbus_pio_clock(clock_get_hz(clk_sys), baud, &clock)) {
        return EXBUS_LINE_ERR_CLOCK;
    }
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            &exbus_line_program, &pio, &sm, &offset, pin, 1, true)) {
        return EXBUS_LINE_ERR_PIO;
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
        pio_remove_program_and_unclaim_sm(&exbus_line_program, pio, sm,
                                          offset);
        return EXBUS_LINE_ERR_DMA;
    }
    line->clock = clock;
    line->pio = pio;
    line->sm = sm;
    line->offset = offset;
    line->dma_tx = (int8_t)tx;
    line->dma_rx = (int8_t)rx;
    line->pin = (uint8_t)pin;
    line->busy = 0;

    /* The pin is the OUT, SET, IN and JMP pin.  Both shift registers shift
     * right, so bit 0 goes out first and a received byte lands in the low
     * bits of its word; the program pulls and pushes itself. */
    pio_sm_config c = exbus_line_program_get_default_config(offset);
    sm_config_set_out_pins(&c, pin, 1);
    sm_config_set_set_pins(&c, pin, 1);
    sm_config_set_in_pins(&c, pin);
    sm_config_set_jmp_pin(&c, pin);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_in_shift(&c, true, false, 32);
    sm_config_set_clkdiv_int_frac(&c, (uint16_t)(clock.div256 >> 8),
                                  (uint8_t)(clock.div256 & 0xFFu));
    pio_sm_init(pio, sm, offset, &c);

    /* The state machine's latch high and driving before the pin is its:
     * the pin goes from whatever it was to driven high, the idle level.
     * The pull-up holds the line while the program releases it. */
    pio_sm_exec(pio, sm, pio_encode_set(pio_pins, 1));
    pio_sm_exec(pio, sm, pio_encode_set(pio_pindirs, 1));
    gpio_set_pulls(pin, true, false);
    pio_gpio_init(pio, pin);
    pio_sm_set_enabled(pio, sm, true);
    line->open = 1;
    return EXBUS_LINE_OK;
}

void exbus_line_close(exbus_line_t *line)
{
    if (line == NULL || line->open == 0u) {
        return;
    }
    const PIO pio = line_pio(line);

    /* The pin first: an input with its pull-down, so nothing the state
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
    pio_remove_program_and_unclaim_sm(&exbus_line_program, pio, line->sm,
                                      line->offset);
    line->dma_tx = -1;
    line->dma_rx = -1;
    line->open = 0;
    line->busy = 0;
}

bool exbus_line_is_open(const exbus_line_t *line)
{
    return line != NULL && line->open != 0u;
}

bool exbus_line_busy(const exbus_line_t *line)
{
    return line != NULL && line->busy != 0u;
}

bool exbus_line_start(void *ctx, const uint8_t *tx, unsigned n,
                      uint32_t window_us)
{
    exbus_line_t *l = (exbus_line_t *)ctx;

    if (l == NULL || l->open == 0u || l->busy != 0u || !line_idle(l)) {
        return false;
    }
    const PIO pio = line_pio(l);
    const unsigned words = exbus_pio_tx(&l->clock, tx, n, window_us, l->tx,
                                        &l->run);

    if (words == 0u) {
        return false;
    }
    pio_sm_clear_fifos(pio, l->sm);

    /* The received bytes first, so the first one has a place. */
    dma_channel_config rc = dma_channel_get_default_config((uint)l->dma_rx);
    channel_config_set_transfer_data_size(&rc, DMA_SIZE_32);
    channel_config_set_read_increment(&rc, false);
    channel_config_set_write_increment(&rc, true);
    channel_config_set_dreq(&rc, pio_get_dreq(pio, l->sm, false));
    dma_channel_configure((uint)l->dma_rx, &rc, l->rx, &pio->rxf[l->sm],
                          EXBUS_PIO_RX_WORDS, true);

    dma_channel_config tc = dma_channel_get_default_config((uint)l->dma_tx);
    channel_config_set_transfer_data_size(&tc, DMA_SIZE_32);
    channel_config_set_read_increment(&tc, true);
    channel_config_set_write_increment(&tc, false);
    channel_config_set_dreq(&tc, pio_get_dreq(pio, l->sm, true));
    /* The longest the transaction takes: the bytes out, the window, every
     * byte a capture keeps with the longest gap after it, and 1 ms for the
     * hand-over. */
    l->limit_us = exbus_air_us(n, l->clock.baud)
                  + exbus_pio_release_us(&l->clock, l->run.window_ticks,
                                         EXBUS_PIO_RX_WORDS)
                  + 1000u;
    l->t_start_us = time_us_32();
    dma_channel_configure((uint)l->dma_tx, &tc, &pio->txf[l->sm], l->tx,
                          words, true);
    l->busy = 1;
    return true;
}

bool exbus_line_poll(void *ctx, exbus_capture_t *out)
{
    exbus_line_t *l = (exbus_line_t *)ctx;

    if (l == NULL || out == NULL || l->open == 0u || l->busy == 0u) {
        return false;
    }
    const PIO pio = line_pio(l);
    const uint32_t elapsed = time_us_32() - l->t_start_us;
    bool cut = false;

    /* Done: the machine waits for the next stream and the channel has
     * taken every word it pushed, or has no room left. */
    if (!line_idle(l)
        || (!pio_sm_is_rx_fifo_empty(pio, l->sm)
            && dma_channel_is_busy((uint)l->dma_rx))) {
        if (elapsed < l->limit_us + EXBUS_LINE_GRACE_US) {
            return false;
        }
        cut = true;
    }
    /* The words so far, read before a restart clears the channel. */
    const uint32_t left = dma_channel_hw_addr((uint)l->dma_rx)->transfer_count
                          & DMA_COUNT_MASK;
    const unsigned n = left >= EXBUS_PIO_RX_WORDS
                           ? 0u : EXBUS_PIO_RX_WORDS - (unsigned)left;
    const bool lost = cut || !pio_sm_is_rx_fifo_empty(pio, l->sm);

    if (cut) {
        line_restart(l);
    } else {
        dma_channel_abort((uint)l->dma_rx);
    }
    exbus_pio_capture(&l->clock, &l->run, l->rx, n, lost, out);
    l->busy = 0;
    return true;
}

void exbus_line_abort(void *ctx)
{
    exbus_line_t *l = (exbus_line_t *)ctx;

    if (l == NULL || l->open == 0u) {
        return;
    }
    line_restart(l);
    l->busy = 0;
}

exbus_driver_t exbus_line_driver(exbus_line_t *line)
{
    exbus_driver_t d;

    d.ctx = line;
    d.start = exbus_line_start;
    d.poll = exbus_line_poll;
    d.abort = exbus_line_abort;
    return d;
}
