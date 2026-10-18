/*
 * The Hitec wire on one RP2350 pin.  See hitec_line.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "hitec_line.h"

#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "pico.h"

#include "hitec_line.pio.h"

/* A transfer count's low 28 bits are the count; the rest is the mode. */
#define DMA_COUNT_MASK 0x0FFFFFFFu

/* Reads of the receive FIFO a poll or an abort makes at most while the
 * channel takes the words still in it: 4 words, each a few system clock
 * cycles. */
#define RX_SETTLE_SPINS 64u

static PIO line_pio(const hitec_line_t *l)
{
    return (PIO)l->pio;
}

/* The family's pull: classic down, the D series up. */
static void set_pull(unsigned pin, uint8_t family)
{
    gpio_set_pulls(pin, family == HITEC_D, family == HITEC_CLASSIC);
}

/* The state machine waits for a stream: it stalls on the first `out`, the
 * FIFO is empty and the channel has handed over every word.  A stream is 3
 * words or more and the program reads all of them before the receiver
 * starts, so this is false from the start of a transfer to the end of the
 * transaction. */
static bool line_idle(const hitec_line_t *l)
{
    const PIO pio = line_pio(l);

    return !dma_channel_is_busy((uint)l->dma_tx)
           && pio_sm_is_tx_fifo_empty(pio, l->sm)
           && pio_sm_get_pc(pio, l->sm) == l->offset + HITEC_PIO_IDLE_PC;
}

/* Put the state machine at the program's drain with the pad released,
 * whatever it was doing.  The drain empties the output shift register,
 * whatever the restart left in it, so the next stream starts at its first
 * word. */
static void line_restart(hitec_line_t *l)
{
    const PIO pio = line_pio(l);

    pio_sm_set_enabled(pio, l->sm, false);
    pio_sm_exec(pio, l->sm, pio_encode_set(pio_pindirs, 0));
    dma_channel_abort((uint)l->dma_tx);
    dma_channel_abort((uint)l->dma_rx);
    pio_sm_clear_fifos(pio, l->sm);
    pio_sm_restart(pio, l->sm);
    pio_sm_exec(pio, l->sm, pio_encode_jmp(l->offset + HITEC_PIO_DRAIN_PC));
    pio_sm_set_enabled(pio, l->sm, true);
}

/* Whether the answer is still to go out with @p n bytes received: a
 * restart now cuts it. */
static bool answer_due(const hitec_line_t *l, unsigned n)
{
    return hitec_pio_answer_due(&l->run, n,
                                pio_sm_get_pc(line_pio(l), l->sm) - l->offset);
}

/* Bytes the receive channel has written. */
static unsigned rx_count(const hitec_line_t *l)
{
    const uint32_t left = dma_channel_hw_addr((uint)l->dma_rx)->transfer_count
                          & DMA_COUNT_MASK;

    return left >= l->run.max_rx ? 0u : l->run.max_rx - (unsigned)left;
}

/* The receive channel has every word the machine pushed, or all it
 * takes. */
static bool rx_settled(const hitec_line_t *l)
{
    return l->run.max_rx == 0u || pio_sm_is_rx_fifo_empty(line_pio(l), l->sm)
           || rx_count(l) >= l->run.max_rx;
}

/* Wait up to RX_SETTLE_SPINS FIFO reads for the channel to take the words
 * still in the receive FIFO.  The caller has made sure the machine pushes
 * nothing more. */
static void rx_settle(const hitec_line_t *l)
{
    for (unsigned i = 0; i < RX_SETTLE_SPINS && !rx_settled(l); ++i) {
        tight_loop_contents();
    }
}

void hitec_line_init(hitec_line_t *line)
{
    if (line == NULL) {
        return;
    }
    memset(line, 0, sizeof(*line));
    line->dma_tx = -1;
    line->dma_rx = -1;
}

hitec_line_err_t hitec_line_open(hitec_line_t *line, unsigned pin,
                                 uint8_t family, uint8_t drive)
{
    hitec_pio_clock_t clock;
    PIO pio;
    uint sm;
    uint offset;
    int tx;
    int rx;

    if (line == NULL || pin >= NUM_BANK0_GPIOS || hitec_bit_rate(family) == 0u
        || drive > HITEC_DRIVE_PUSH_PULL
        || (drive == HITEC_DRIVE_PUSH_PULL && family != HITEC_D)) {
        return HITEC_LINE_ERR_ARG;
    }
    hitec_line_close(line);
    if (!hitec_pio_clock(clock_get_hz(clk_sys), family, &clock)) {
        return HITEC_LINE_ERR_CLOCK;
    }
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            &hitec_line_program, &pio, &sm, &offset, pin, 1, true)) {
        return HITEC_LINE_ERR_PIO;
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
        pio_remove_program_and_unclaim_sm(&hitec_line_program, pio, sm,
                                          offset);
        return HITEC_LINE_ERR_DMA;
    }
    line->clock = clock;
    line->pio = pio;
    line->sm = sm;
    line->offset = offset;
    line->dma_tx = (int8_t)tx;
    line->dma_rx = (int8_t)rx;
    line->pin = (uint8_t)pin;
    line->family = family;
    line->drive = drive;
    line->busy = 0;
    line->aborted = 0;

    /* The pad is the OUT, SET, IN and JMP pin.  Both shift registers shift
     * right; the transmit side pulls at 32 bits, the receive side pushes
     * only where the program says. */
    pio_sm_config c = hitec_line_program_get_default_config(offset);
    sm_config_set_out_pins(&c, pin, 1);
    sm_config_set_set_pins(&c, pin, 1);
    sm_config_set_in_pin_base(&c, pin);
    sm_config_set_jmp_pin(&c, pin);
    sm_config_set_out_shift(&c, true, true, 32);
    sm_config_set_in_shift(&c, true, false, 32);
    sm_config_set_clkdiv_int_frac8(&c, clock.div256 >> 8,
                                   (uint8_t)(clock.div256 & 0xFFu));
    pio_sm_init(pio, sm, offset + HITEC_PIO_DRAIN_PC, &c);

    /* The state machine's latch low and its direction an input before the
     * pad is its: the pad goes from whatever it was to a released input,
     * never to a driven level. */
    pio_sm_exec(pio, sm, pio_encode_set(pio_pins, 0));
    pio_sm_exec(pio, sm, pio_encode_set(pio_pindirs, 0));
    set_pull(pin, family);
    gpio_set_inover(pin, family == HITEC_D ? GPIO_OVERRIDE_INVERT
                                           : GPIO_OVERRIDE_NORMAL);
    pio_gpio_init(pio, pin);
    pio_sm_set_enabled(pio, sm, true);
    line->open = 1;
    return HITEC_LINE_OK;
}

void hitec_line_close(hitec_line_t *line)
{
    if (line == NULL || line->open == 0u) {
        return;
    }
    const PIO pio = line_pio(line);

    /* The pad first: an input with the family's pull and no override, so
     * nothing the state machine does while it is taken down reaches the
     * wire. */
    gpio_init(line->pin);
    gpio_set_dir(line->pin, GPIO_IN);
    gpio_set_inover(line->pin, GPIO_OVERRIDE_NORMAL);
    set_pull(line->pin, line->family);

    pio_sm_set_enabled(pio, line->sm, false);
    dma_channel_abort((uint)line->dma_tx);
    dma_channel_abort((uint)line->dma_rx);
    dma_channel_unclaim((uint)line->dma_tx);
    dma_channel_unclaim((uint)line->dma_rx);
    pio_sm_clear_fifos(pio, line->sm);
    pio_sm_restart(pio, line->sm);
    pio_remove_program_and_unclaim_sm(&hitec_line_program, pio, line->sm,
                                      line->offset);
    line->dma_tx = -1;
    line->dma_rx = -1;
    line->open = 0;
    line->busy = 0;
    line->aborted = 0;
    line->cut = 0;
}

bool hitec_line_is_open(const hitec_line_t *line)
{
    return line != NULL && line->open != 0u;
}

bool hitec_line_busy(const hitec_line_t *line)
{
    return line != NULL && line->busy != 0u;
}

uint32_t hitec_line_bit_ns(const hitec_line_t *line)
{
    return hitec_line_is_open(line) ? hitec_pio_bit_ns(&line->clock) : 0u;
}

void hitec_line_abort(hitec_line_t *line)
{
    if (!hitec_line_busy(line) || line->aborted != 0u) {
        return;
    }
    /* The bytes so far stay in the buffer.  The machine stops first, so it
     * pushes nothing more; the channel takes what is still in the FIFO;
     * the count is read before the restart stops the channel. */
    pio_sm_set_enabled(line_pio(line), line->sm, false);
    rx_settle(line);
    line->aborted = 1;
    line->kept = line->run.max_rx > 0u ? (uint8_t)rx_count(line) : 0u;
    line->cut = answer_due(line, line->kept) ? 1u : 0u;
    line_restart(line);
}

bool hitec_line_start(void *ctx, const hitec_xfer_t *xfer)
{
    hitec_line_t *l = (hitec_line_t *)ctx;

    if (l == NULL || l->open == 0u || l->busy != 0u || !line_idle(l)) {
        return false;
    }
    const PIO pio = line_pio(l);
    const unsigned n = hitec_pio_tx(&l->clock, l->drive, xfer, l->tx, &l->run);

    if (n == 0u) {
        return false;
    }
    pio_sm_clear_fifos(pio, l->sm);
    l->aborted = 0;

    /* The bytes first, so the first one has a place. */
    if (l->run.max_rx > 0u) {
        dma_channel_config rc = dma_channel_get_default_config((uint)l->dma_rx);
        channel_config_set_transfer_data_size(&rc, DMA_SIZE_32);
        channel_config_set_read_increment(&rc, false);
        channel_config_set_write_increment(&rc, true);
        channel_config_set_dreq(&rc, pio_get_dreq(pio, l->sm, false));
        dma_channel_configure((uint)l->dma_rx, &rc, l->rx, &pio->rxf[l->sm],
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
    l->busy = 1;
    return true;
}

bool hitec_line_poll(void *ctx, hitec_capture_t *out)
{
    hitec_line_t *l = (hitec_line_t *)ctx;
    bool full = false;
    bool lost = false;
    bool cut = false;
    unsigned n;

    if (l == NULL || out == NULL || l->open == 0u || l->busy == 0u) {
        return false;
    }
    if (l->aborted != 0u) {
        n = l->kept;
        cut = l->cut != 0u;
    } else {
        /* Idle first: once it is, the machine pushes nothing more, and the
         * words it pushed are in the buffer or still in the FIFO. */
        const bool idle = line_idle(l);

        n = l->run.max_rx > 0u ? rx_count(l) : 0u;
        full = l->run.max_rx > 0u && n >= l->run.max_rx;
        /* A count of 1 is reached before the answer goes out: the restart
         * waits for it. */
        if (full && answer_due(l, n)) {
            full = false;
        }
        if (!full && !idle) {
            if (!hitec_due(time_us_32(), l->t_start_us,
                           l->run.span_us + HITEC_LINE_GRACE_US)) {
                return false;
            }
            lost = true;
        }
        /* A receive that ended at its count or ran past its time still
         * runs: the pad is released and the machine waits again. */
        if (full || lost) {
            cut = answer_due(l, n);
            line_restart(l);
        } else {
            /* The last bytes may still be in the FIFO: the channel takes
             * them before the count is read and the channel stopped. */
            rx_settle(l);
            n = l->run.max_rx > 0u ? rx_count(l) : 0u;
            full = l->run.max_rx > 0u && n >= l->run.max_rx;
            dma_channel_abort((uint)l->dma_rx);
        }
    }
    hitec_pio_capture(&l->clock, &l->run, l->rx, n, full, lost,
                      l->aborted != 0u, cut, out);
    l->busy = 0;
    l->aborted = 0;
    l->cut = 0;
    return true;
}

uint32_t hitec_line_now_us(void *ctx)
{
    (void)ctx;
    return time_us_32();
}

hitec_driver_t hitec_line_driver(hitec_line_t *line)
{
    hitec_driver_t d;

    d.ctx = line;
    d.start = hitec_line_start;
    d.poll = hitec_line_poll;
    d.now_us = hitec_line_now_us;
    return d;
}
