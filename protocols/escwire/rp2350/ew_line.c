/**
 * @file ew_line.c
 * @brief The signal wire on one RP2350 pin; see ew_line.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ew_line.h"

#include <stddef.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "pico.h"

#include "ew_line.pio.h"

/** @brief A transfer count's low 28 bits are the count; the rest is the mode. */
#define DMA_COUNT_MASK 0x0FFFFFFFu

/**
 * @brief The PIO block of a line.
 * @param l the line
 * @return the block
 */
static PIO line_pio(const ew_pin_line_t *l)
{
    return (PIO)l->pio;
}

/**
 * @brief The pad released at once: the direction of a running or stopped
 *        machine, without its clock.
 * @param l the line
 */
static void pad_release(const ew_pin_line_t *l)
{
    pio_sm_exec(line_pio(l), l->sm, pio_encode_set(pio_pindirs, 0));
}

/**
 * @brief The pad driven at a level: the latch first, so nothing else shows.
 * @param l     the line
 * @param level 0 or 1
 */
static void pad_drive(const ew_pin_line_t *l, uint level)
{
    pio_sm_exec(line_pio(l), l->sm, pio_encode_set(pio_pins, level));
    pio_sm_exec(line_pio(l), l->sm, pio_encode_set(pio_pindirs, 1));
}

/**
 * @brief The input with its pull-down and its buffer off: EW_LINE_PARK.  The
 *        pull first: a pad released with its pull-up still on rises, for a few
 *        us out of a low bit.
 * @param l the line
 */
static void pad_park(const ew_pin_line_t *l)
{
    gpio_set_pulls(l->pin, false, true);
    pad_release(l);
    gpio_set_input_enabled(l->pin, false);
}

/**
 * @brief The pull-up on and the buffer on, the pad's direction left alone.
 * @param l the line
 */
static void pad_listen(const ew_pin_line_t *l)
{
    gpio_set_pulls(l->pin, true, false);
    gpio_set_input_enabled(l->pin, true);
}

/**
 * @brief The state machine waits for a stream: it stalls on the first `pull`,
 *        the FIFO is empty and the channel has handed over every word.
 *
 * A stream is 4 words or more and the program reads all of them before it comes
 * back, so this is false from the start of a transfer to the end of the
 * transaction.
 *
 * @param l the line
 * @return true at rest
 */
static bool line_idle(const ew_pin_line_t *l)
{
    const PIO pio = line_pio(l);

    return !dma_channel_is_busy((uint)l->dma_tx)
           && pio_sm_is_tx_fifo_empty(pio, l->sm)
           && pio_sm_get_pc(pio, l->sm) == l->offset + EW_PIO_IDLE_PC;
}

/**
 * @brief Put the state machine on its first `pull` with the pad released and
 *        both channels stopped, whatever it was doing.
 * @param l the line
 */
static void line_restart(ew_pin_line_t *l)
{
    const PIO pio = line_pio(l);

    pio_sm_set_enabled(pio, l->sm, false);
    pad_release(l);
    dma_channel_abort((uint)l->dma_tx);
    dma_channel_abort((uint)l->dma_rx);
    pio_sm_clear_fifos(pio, l->sm);
    pio_sm_restart(pio, l->sm);
    pio_sm_exec(pio, l->sm, pio_encode_jmp(l->offset + EW_PIO_IDLE_PC));
    pio_sm_set_enabled(pio, l->sm, true);
}

/**
 * @brief Bytes the receive channel has written.
 * @param l the line
 * @return the count; 0 for a run that receives none
 */
static uint32_t rx_count(const ew_pin_line_t *l)
{
    const uint32_t max = l->run.max_rx;
    const uint32_t left = dma_channel_hw_addr((uint)l->dma_rx)->transfer_count
                          & DMA_COUNT_MASK;

    return (max == 0u || left >= max) ? 0u : max - left;
}

/**
 * @brief Wait a bounded time for the channel to take the words still in the
 *        receive FIFO; the machine pushes nothing more by now.
 * @param l the line
 */
static void rx_settle(const ew_pin_line_t *l)
{
    for (uint i = 0u; i < EW_LINE_RX_SETTLE_SPINS
                      && !pio_sm_is_rx_fifo_empty(line_pio(l), l->sm)
                      && rx_count(l) < l->run.max_rx;
         ++i) {
        tight_loop_contents();
    }
}

/**
 * @brief Whether the machine has found its receive FIFO full.
 * @param l the line
 * @return true since the start of the transaction
 */
static bool rx_stalled(const ew_pin_line_t *l)
{
    const uint32_t mask = 1u << (PIO_FDEBUG_RXSTALL_LSB + l->sm);

    return (line_pio(l)->fdebug & mask) != 0u;
}

void ew_line_init(ew_pin_line_t *line)
{
    if (line == NULL) {
        return;
    }
    memset(line, 0, sizeof(*line));
    line->dma_tx = -1;
    line->dma_rx = -1;
}

ew_line_err_t ew_line_open(ew_pin_line_t *line, unsigned pin)
{
    ew_pio_clock_t clock[EW_RATES];
    const uint32_t sys_hz = clock_get_hz(clk_sys);
    PIO pio;
    uint sm;
    uint offset;
    int tx;
    int rx;

    if (line == NULL || pin >= NUM_BANK0_GPIOS) {
        return EW_LINE_ERR_ARG;
    }
    ew_line_close(line);
    for (uint r = 0u; r < (uint)EW_RATES; ++r) {
        if (!ew_pio_clock(sys_hz, (uint8_t)r, &clock[r])) {
            return EW_LINE_ERR_CLOCK;
        }
    }
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            &ew_line_program, &pio, &sm, &offset, pin, 1, true)) {
        return EW_LINE_ERR_PIO;
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
        pio_remove_program_and_unclaim_sm(&ew_line_program, pio, sm, offset);
        return EW_LINE_ERR_DMA;
    }
    memcpy(line->clock, clock, sizeof(line->clock));
    line->pio = pio;
    line->sm = sm;
    line->offset = offset;
    line->dma_tx = (int8_t)tx;
    line->dma_rx = (int8_t)rx;
    line->pin = (uint8_t)pin;
    line->busy = 0u;
    line->rest_down = 0u;
    line->was_drive = (uint8_t)gpio_get_drive_strength(pin);
    line->was_slew = (uint8_t)gpio_get_slew_rate(pin);

    /* The pad is the OUT, SET, IN and JMP pin.  Both shift registers shift
     * right; the program pulls, and the receive side pushes at 9 bits. */
    pio_sm_config c = ew_line_program_get_default_config(offset);
    sm_config_set_out_pins(&c, pin, 1);
    sm_config_set_set_pins(&c, pin, 1);
    sm_config_set_in_pin_base(&c, pin);
    sm_config_set_jmp_pin(&c, pin);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_in_shift(&c, true, true, 9);
    sm_config_set_clkdiv_int_frac8(&c, clock[EW_RATE_A].div256 >> 8,
                                   (uint8_t)(clock[EW_RATE_A].div256 & 0xFFu));
    pio_sm_init(pio, sm, offset + EW_PIO_IDLE_PC, &c);

    /* The latch high and the direction an input before the pad is the
     * machine's: the pad goes from whatever it was to a released input with
     * its pull-down, never to a driven level.  The pull lives in the pad block
     * and stays through pio_gpio_init(). */
    pio_sm_exec(pio, sm, pio_encode_set(pio_pins, 1));
    pio_sm_exec(pio, sm, pio_encode_set(pio_pindirs, 0));
    gpio_set_pulls(pin, false, true);
    gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_2MA);
    gpio_set_slew_rate(pin, GPIO_SLEW_RATE_SLOW);
    pio_gpio_init(pio, pin);
    gpio_set_input_enabled(pin, false);
    pio_sm_set_enabled(pio, sm, true);
    line->open = 1u;
    return EW_LINE_OK;
}

void ew_line_close(ew_pin_line_t *line)
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
    dma_channel_abort((uint)line->dma_tx);
    dma_channel_abort((uint)line->dma_rx);
    dma_channel_unclaim((uint)line->dma_tx);
    dma_channel_unclaim((uint)line->dma_rx);
    pio_sm_clear_fifos(pio, line->sm);
    pio_sm_restart(pio, line->sm);
    pio_remove_program_and_unclaim_sm(&ew_line_program, pio, line->sm,
                                      line->offset);
    line->dma_tx = -1;
    line->dma_rx = -1;
    line->open = 0u;
    line->busy = 0u;
}

bool ew_line_is_open(const ew_pin_line_t *line)
{
    return line != NULL && line->open != 0u;
}

bool ew_line_busy(const ew_pin_line_t *line)
{
    return line != NULL && line->busy != 0u;
}

uint32_t ew_line_bit_ns(const ew_pin_line_t *line, uint8_t rate)
{
    return (ew_line_is_open(line) && rate < (uint8_t)EW_RATES)
               ? ew_pio_bit_ns(&line->clock[rate])
               : 0u;
}

void ew_line_rest_down(ew_pin_line_t *line, bool on)
{
    if (ew_line_is_open(line)) {
        line->rest_down = on ? 1u : 0u;
    }
}

bool ew_line_start(void *ctx, const ew_xfer_t *x)
{
    ew_pin_line_t *l = (ew_pin_line_t *)ctx;

    if (!ew_line_is_open(l) || l->busy != 0u || x == NULL
        || x->rate >= (uint8_t)EW_RATES || !line_idle(l)) {
        return false;
    }
    const ew_pio_clock_t *clock = &l->clock[x->rate];
    const uint32_t n = ew_pio_stream(clock, x, l->tx, &l->run);

    if (n == 0u) {
        return false;
    }
    const PIO pio = line_pio(l);

    pio_sm_clear_fifos(pio, l->sm);
    pio->fdebug = 1u << (PIO_FDEBUG_RXSTALL_LSB + l->sm);
    pio_sm_set_clkdiv_int_frac8(pio, l->sm, clock->div256 >> 8,
                                (uint8_t)(clock->div256 & 0xFFu));
    if (l->rest_down != 0u && x->max_rx == 0u) {
        /* Driven high first: a pull-down on a released pad dips the line for
         * the rest of this call, before the frame. */
        pad_drive(l, 1u);
        gpio_set_pulls(l->pin, false, true);
        gpio_set_input_enabled(l->pin, false);
    } else {
        pad_listen(l);
    }

    /* The bytes first, so the first one has a place.  The channel reads the
     * FIFO word's upper half, where the 9 bits of a byte land. */
    if (l->run.max_rx > 0u) {
        const volatile uint8_t *const upper =
            (const volatile uint8_t *)&pio->rxf[l->sm] + 2;
        dma_channel_config rc = dma_channel_get_default_config((uint)l->dma_rx);

        channel_config_set_transfer_data_size(&rc, DMA_SIZE_16);
        channel_config_set_read_increment(&rc, false);
        channel_config_set_write_increment(&rc, true);
        channel_config_set_dreq(&rc, pio_get_dreq(pio, l->sm, false));
        dma_channel_configure((uint)l->dma_rx, &rc, l->rx, upper,
                              l->run.max_rx, true);
    }

    dma_channel_config tc = dma_channel_get_default_config((uint)l->dma_tx);

    channel_config_set_transfer_data_size(&tc, DMA_SIZE_32);
    channel_config_set_read_increment(&tc, true);
    channel_config_set_write_increment(&tc, false);
    channel_config_set_dreq(&tc, pio_get_dreq(pio, l->sm, true));
    l->t_start_us = time_us_32();
    dma_channel_configure((uint)l->dma_tx, &tc, &pio->txf[l->sm], l->tx, n,
                          true);
    l->busy = 1u;
    return true;
}

bool ew_line_poll(void *ctx, ew_capture_t *out)
{
    ew_pin_line_t *l = (ew_pin_line_t *)ctx;

    if (!ew_line_is_open(l) || l->busy == 0u || out == NULL) {
        return false;
    }
    /* Idle first: once it is, the machine pushes nothing more, and the words
     * it pushed are in the buffer or still in the FIFO. */
    const bool idle = line_idle(l);
    uint32_t n = rx_count(l);
    const bool full = l->run.max_rx > 0u && n >= l->run.max_rx;
    bool cut = false;

    if (!full && !idle) {
        if (!ew_pio_late(&l->run, time_us_32(), l->t_start_us)) {
            return false;
        }
        /* A driver fault: stopped and parked before anything else, with the
         * pad possibly still driven. */
        cut = true;
        pio_sm_set_enabled(line_pio(l), l->sm, false);
        pad_park(l);
    }
    rx_settle(l);
    n = rx_count(l);
    /* The flag before the restart; the next start clears it. */
    const bool stalled = rx_stalled(l);

    /* The count and the bytes before the channel stops, then back to the
     * first `pull`: a machine that ended at the count still waits on the
     * wire. */
    line_restart(l);
    ew_pio_capture(&l->run, l->rx, n, stalled, cut, out);
    if (out->overflow) {
        pad_park(l);
    }
    l->busy = 0u;
    return true;
}

void ew_line_abort(void *ctx)
{
    ew_pin_line_t *l = (ew_pin_line_t *)ctx;

    if (!ew_line_is_open(l)) {
        return;
    }
    pad_park(l);
    line_restart(l);
    l->busy = 0u;
}

void ew_line_level(void *ctx, ew_line_t level)
{
    ew_pin_line_t *l = (ew_pin_line_t *)ctx;

    if (!ew_line_is_open(l)) {
        return;
    }
    if (l->busy != 0u) {
        ew_line_abort(l);
    }
    switch (level) {
    case EW_LINE_HIGH:
        pad_listen(l);
        pad_drive(l, 1u);
        break;
    case EW_LINE_UP:
        pad_listen(l);
        pad_release(l);
        break;
    case EW_LINE_LOW:
        gpio_set_pulls(l->pin, false, true);
        gpio_set_input_enabled(l->pin, false);
        pad_drive(l, 0u);
        break;
    case EW_LINE_PARK:
    default:
        pad_park(l);
        break;
    }
}

uint32_t ew_line_now_us(void *ctx)
{
    (void)ctx;
    return time_us_32();
}

ew_driver_t ew_line_driver(ew_pin_line_t *line)
{
    ew_driver_t d;

    d.ctx = line;
    d.start = ew_line_start;
    d.poll = ew_line_poll;
    d.abort = ew_line_abort;
    d.line = ew_line_level;
    d.now_us = ew_line_now_us;
    return d;
}
