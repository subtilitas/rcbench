/**
 * @file oy_line.c
 * @brief The telemetry wire of programming for YGE ESCs on one RP2350 pin; see
 *        oy_line.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "oy_line.h"

#include <stddef.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "pico.h"

#include "oy_line.pio.h"

/** @brief A transfer count's low 28 bits are the count; the rest is the mode. */
#define DMA_COUNT_MASK 0x0FFFFFFFu

/**
 * @brief The PIO block of a line.
 * @param l the line
 * @return the block
 */
static PIO line_pio(const oy_pin_line_t *l)
{
    return (PIO)l->pio;
}

/**
 * @brief The pad released at once: the direction of a running or stopped
 *        machine, without its clock.
 * @param l the line
 */
static void pad_release(const oy_pin_line_t *l)
{
    pio_sm_exec(line_pio(l), l->sm, pio_encode_set(pio_pindirs, 0));
}

/**
 * @brief The state machine stopped, the pad released into its pull-down with
 *        the input buffer off, both channels stopped and the FIFOs empty.  The
 *        machine first: it may be about to drive the pad.  The pull before the
 *        release, so the pad does not rise out of a low bit.
 * @param l the line
 */
static void line_stop(oy_pin_line_t *l)
{
    const PIO pio = line_pio(l);

    pio_sm_set_enabled(pio, l->sm, false);
    gpio_set_pulls(l->pin, false, true);
    pad_release(l);
    gpio_set_input_enabled(l->pin, false);
    dma_channel_abort((uint)l->dma_tx);
    dma_channel_abort((uint)l->dma_rx);
    pio_sm_clear_fifos(pio, l->sm);
    pio_sm_restart(pio, l->sm);
    pio_sm_exec(pio, l->sm, pio_encode_jmp(l->offset + OY_PIO_WAIT_LO_PC));
    l->listening = 0u;
    l->pending = 0u;
}

/**
 * @brief Start the receive channel at an entry of the buffer.
 * @param l     the line
 * @param first the entry, below OY_PIO_RX_ENTRIES
 */
static void rx_start(oy_pin_line_t *l, uint32_t first)
{
    const PIO pio = line_pio(l);
    /* The upper half of the FIFO word, where the 9 bits of a byte land. */
    const volatile uint8_t *const upper =
        (const volatile uint8_t *)&pio->rxf[l->sm] + 2;
    dma_channel_config c = dma_channel_get_default_config((uint)l->dma_rx);

    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(pio, l->sm, false));
    dma_channel_configure((uint)l->dma_rx, &c, &l->ent[first], upper,
                          OY_PIO_RX_ENTRIES - first, true);
}

/**
 * @brief Entries the receive channel has written into the buffer.
 * @param l the line
 * @return the count, 0 to OY_PIO_RX_ENTRIES
 */
static uint32_t rx_written(const oy_pin_line_t *l)
{
    const uint32_t left = dma_channel_hw_addr((uint)l->dma_rx)->transfer_count
                          & DMA_COUNT_MASK;

    return left >= OY_PIO_RX_ENTRIES ? 0u : OY_PIO_RX_ENTRIES - left;
}

/**
 * @brief Whether the last stream has gone out: the channel done, the FIFO
 *        empty and the machine off the frame's way out.
 * @param l the line
 * @return true when it has, or when there was none
 */
static bool tx_done(const oy_pin_line_t *l)
{
    const PIO pio = line_pio(l);

    return !dma_channel_is_busy((uint)l->dma_tx)
           && pio_sm_is_tx_fifo_empty(pio, l->sm)
           && !oy_pio_sending(pio_sm_get_pc(pio, l->sm) - l->offset);
}

void oy_line_init(oy_pin_line_t *line)
{
    if (line == NULL) {
        return;
    }
    memset(line, 0, sizeof(*line));
    line->dma_tx = -1;
    line->dma_rx = -1;
}

oy_line_err_t oy_line_open(oy_pin_line_t *line, unsigned pin)
{
    oy_pio_clock_t clock;
    PIO pio;
    uint sm;
    uint offset;
    int tx;
    int rx;

    if (line == NULL || pin >= NUM_BANK0_GPIOS) {
        return OY_LINE_ERR_ARG;
    }
    oy_line_close(line);
    if (!oy_pio_clock(clock_get_hz(clk_sys), &clock)) {
        return OY_LINE_ERR_CLOCK;
    }
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            &oy_line_program, &pio, &sm, &offset, pin, 1, true)) {
        return OY_LINE_ERR_PIO;
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
        pio_remove_program_and_unclaim_sm(&oy_line_program, pio, sm, offset);
        return OY_LINE_ERR_DMA;
    }
    oy_line_init(line);
    line->clock = clock;
    line->pio = pio;
    line->sm = sm;
    line->offset = offset;
    line->dma_tx = (int8_t)tx;
    line->dma_rx = (int8_t)rx;
    line->pin = (uint8_t)pin;
    line->was_drive = (uint8_t)gpio_get_drive_strength(pin);
    line->was_slew = (uint8_t)gpio_get_slew_rate(pin);

    /* The pad is the OUT, SET, IN and JMP pin.  Both shift registers shift
     * right; the program pulls and pushes.  MOV STATUS is all ones while the
     * transmit FIFO holds fewer than 1 word. */
    pio_sm_config c = oy_line_program_get_default_config(offset);
    sm_config_set_out_pins(&c, pin, 1);
    sm_config_set_set_pins(&c, pin, 1);
    sm_config_set_in_pin_base(&c, pin);
    sm_config_set_jmp_pin(&c, pin);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_in_shift(&c, true, false, 32);
    sm_config_set_mov_status(&c, STATUS_TX_LESSTHAN, 1);
    sm_config_set_clkdiv_int_frac8(&c, clock.div256 >> 8,
                                   (uint8_t)(clock.div256 & 0xFFu));
    pio_sm_init(pio, sm, offset + OY_PIO_WAIT_LO_PC, &c);

    /* The latch high and the direction an input before the pad is the
     * machine's: the pad goes from whatever it was to a released input with
     * its pull-down, never to a driven level.  The pull lives in the pad block
     * and stays through pio_gpio_init().  The machine stays stopped until
     * oy_line_listen(). */
    pio_sm_exec(pio, sm, pio_encode_set(pio_pins, 1));
    pio_sm_exec(pio, sm, pio_encode_set(pio_pindirs, 0));
    gpio_set_pulls(pin, false, true);
    gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_4MA);
    gpio_set_slew_rate(pin, GPIO_SLEW_RATE_SLOW);
    pio_gpio_init(pio, pin);
    gpio_set_input_enabled(pin, false);
    line->open = 1u;
    return OY_LINE_OK;
}

void oy_line_close(oy_pin_line_t *line)
{
    if (line == NULL || line->open == 0u) {
        return;
    }
    const PIO pio = line_pio(line);

    /* The pad first: nothing the machine does while it is taken down reaches
     * the wire.  The pull before the release, so the pad does not rise. */
    gpio_set_pulls(line->pin, false, true);
    gpio_init(line->pin);
    gpio_set_dir(line->pin, GPIO_IN);
    gpio_set_drive_strength(line->pin,
                            (enum gpio_drive_strength)line->was_drive);
    gpio_set_slew_rate(line->pin, (enum gpio_slew_rate)line->was_slew);

    pio_sm_set_enabled(pio, line->sm, false);
    /* No direction left behind for the block's next user of this pin. */
    pad_release(line);
    dma_channel_abort((uint)line->dma_tx);
    dma_channel_abort((uint)line->dma_rx);
    dma_channel_unclaim((uint)line->dma_tx);
    dma_channel_unclaim((uint)line->dma_rx);
    pio_sm_clear_fifos(pio, line->sm);
    pio_sm_restart(pio, line->sm);
    pio_remove_program_and_unclaim_sm(&oy_line_program, pio, line->sm,
                                      line->offset);
    line->dma_tx = -1;
    line->dma_rx = -1;
    line->open = 0u;
    line->listening = 0u;
    line->pending = 0u;
}

bool oy_line_is_open(const oy_pin_line_t *line)
{
    return line != NULL && line->open != 0u;
}

void oy_line_listen(oy_pin_line_t *line)
{
    if (!oy_line_is_open(line) || line->listening != 0u) {
        return;
    }
    line_stop(line);
    gpio_set_pulls(line->pin, true, false);
    gpio_set_input_enabled(line->pin, true);
    line->rx.rd = 0u;
    line->rx.filled = false;
    rx_start(line, 0u);
    pio_sm_set_enabled(line_pio(line), line->sm, true);
    line->listening = 1u;
}

void oy_line_park(oy_pin_line_t *line)
{
    if (oy_line_is_open(line)) {
        line_stop(line);
    }
}

uint32_t oy_line_bit_ns(const oy_pin_line_t *line)
{
    return oy_line_is_open(line) ? oy_pio_bit_ns(&line->clock) : 0u;
}

bool oy_line_sent(const oy_pin_line_t *line)
{
    if (!oy_line_is_open(line) || line->listening == 0u) {
        return false;
    }
    return line->pending == 0u || tx_done(line);
}

bool oy_line_tx(void *ctx, const uint8_t *frame, size_t n)
{
    oy_pin_line_t *l = (oy_pin_line_t *)ctx;

    if (!oy_line_is_open(l) || l->listening == 0u || frame == NULL || n == 0u
        || n > OY_PIO_TX_MAX) {
        return false;
    }
    const PIO pio = line_pio(l);

    if (l->pending != 0u && !tx_done(l)) {
        if (!oy_pio_late(l->span_us, time_us_32(), l->t_tx_us)) {
            return false;
        }
        /* Held low past its time: the stream goes, the machine back to
         * waiting with the pad released, the receive channel left running. */
        pio_sm_set_enabled(pio, l->sm, false);
        pad_release(l);
        dma_channel_abort((uint)l->dma_tx);
        pio_sm_clear_fifos(pio, l->sm);
        pio_sm_restart(pio, l->sm);
        pio_sm_exec(pio, l->sm, pio_encode_jmp(l->offset + OY_PIO_WAIT_LO_PC));
        pio_sm_set_enabled(pio, l->sm, true);
        ++l->tx_dropped;
    }
    const uint32_t words = oy_pio_stream(frame, n, l->tx);
    dma_channel_config c = dma_channel_get_default_config((uint)l->dma_tx);

    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, l->sm, true));
    l->span_us = oy_pio_span_us(&l->clock, n);
    l->t_tx_us = time_us_32();
    dma_channel_configure((uint)l->dma_tx, &c, &pio->txf[l->sm], l->tx, words,
                          true);
    l->pending = 1u;
    ++l->sent;
    return true;
}

size_t oy_line_rx(void *ctx, uint8_t *buf, size_t cap)
{
    oy_pin_line_t *l = (oy_pin_line_t *)ctx;

    if (!oy_line_is_open(l) || l->listening == 0u || buf == NULL) {
        return 0u;
    }
    uint32_t written = rx_written(l);
    const size_t n = oy_pio_take(&l->rx, l->ent, written, buf, cap);

    if (written != 0u && l->rx.rd == written) {
        bool drop = false;
        uint32_t first;

        /* Every entry handed over: stopped, then counted again, so an entry
         * that came in meanwhile is kept.  An abort lets a transfer in flight
         * end; the FIFO keeps what the channel has not read. */
        dma_channel_abort((uint)l->dma_rx);
        written = rx_written(l);
        first = oy_pio_rearm(&l->rx, written, &drop);
        if (drop) {
            const PIO pio = line_pio(l);

            while (!pio_sm_is_rx_fifo_empty(pio, l->sm)) {
                (void)pio_sm_get(pio, l->sm);
            }
        }
        if (first < OY_PIO_RX_ENTRIES) {
            rx_start(l, first);
        }
    }
    return n;
}

uint32_t oy_line_now_us(void *ctx)
{
    (void)ctx;
    return time_us_32();
}

oy_driver_t oy_line_driver(oy_pin_line_t *line)
{
    oy_driver_t d;

    d.ctx = line;
    d.tx = oy_line_tx;
    d.rx = oy_line_rx;
    d.now_us = oy_line_now_us;
    return d;
}
