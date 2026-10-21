/**
 * @file
 * @brief The Hitec wire on one RP2350 pin.
 *
 * See hitec_line.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "hitec_line.h"

#include <stdint.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/structs/io_bank0.h"
#include "hardware/structs/pads_bank0.h"
#include "hardware/timer.h"
#include "pico.h"

#include "hitec_line.pio.h"

/**
 * @brief A transfer count's low 28 bits are the count; the rest is the mode.
 */
#define DMA_COUNT_MASK 0x0FFFFFFFu

/**
 * @brief Reads of the receive FIFO a poll or an abort makes at most while the
 *        channel takes the words still in it: 4 words, each a few system clock
 *        cycles.
 */
#define RX_SETTLE_SPINS 64u

/**
 * @brief Get the PIO block of a line.
 *
 */
static PIO line_pio(const hitec_line_t *l)
{
    return (PIO)l->pio;
}

/** @brief The family's pull: classic down, the D series up. */
static void set_pull(unsigned pin, uint8_t family)
{
    gpio_set_pulls(pin, family == HITEC_D, family == HITEC_CLASSIC);
}

/**
 * @brief The state machine waits for a stream: it stalls on the first `out`,
 *        the FIFO is empty and the channel has handed over every word.
 *
 * A stream is 3 words or more and the program reads all of them before the
 * receiver starts, so this is false from the start of a transfer to the end of
 * the transaction.
 */
static bool line_idle(const hitec_line_t *l)
{
    const PIO pio = line_pio(l);

    return !dma_channel_is_busy((uint)l->dma_tx)
           && pio_sm_is_tx_fifo_empty(pio, l->sm)
           && pio_sm_get_pc(pio, l->sm) == l->offset + HITEC_PIO_IDLE_PC;
}

/**
 * @brief Run the program's drain on the stopped machine and put it on the `out`
 *        it stalls on between transactions: the output shift register is empty,
 *        whatever a restart left in it, and line_idle() holds from the moment
 *        the machine runs again.
 *
 * The transmit FIFO is empty, so autopull takes nothing.
 */
static void line_ready(PIO pio, uint sm, uint offset)
{
    pio_sm_exec(pio, sm, pio_encode_mov(pio_osr, pio_null));
    pio_sm_exec(pio, sm, pio_encode_out(pio_null, 32));
    pio_sm_exec(pio, sm, pio_encode_jmp(offset + HITEC_PIO_IDLE_PC));
}

/**
 * @brief Put the state machine at idle with the pad released, whatever it was
 *        doing.
 */
static void line_restart(hitec_line_t *l)
{
    const PIO pio = line_pio(l);

    pio_sm_set_enabled(pio, l->sm, false);
    pio_sm_exec(pio, l->sm, pio_encode_set(pio_pindirs, 0));
    dma_channel_abort((uint)l->dma_tx);
    dma_channel_abort((uint)l->dma_rx);
    pio_sm_clear_fifos(pio, l->sm);
    pio_sm_restart(pio, l->sm);
    line_ready(pio, l->sm, l->offset);
    pio_sm_set_enabled(pio, l->sm, true);
}

/**
 * @brief Whether the answer is still to go out with @p n bytes received: a
 *        restart now cuts it.
 */
static bool answer_due(const hitec_line_t *l, unsigned n)
{
    return hitec_pio_answer_due(&l->run, n,
                                pio_sm_get_pc(line_pio(l), l->sm) - l->offset);
}

/** @brief Bytes the receive channel has written. */
static unsigned rx_count(const hitec_line_t *l)
{
    const uint32_t left = dma_channel_hw_addr((uint)l->dma_rx)->transfer_count
                          & DMA_COUNT_MASK;

    return left >= l->run.max_rx ? 0u : l->run.max_rx - (unsigned)left;
}

/**
 * @brief The receive channel has every word the machine pushed, or all it
 *        takes.
 */
static bool rx_settled(const hitec_line_t *l)
{
    return l->run.max_rx == 0u || pio_sm_is_rx_fifo_empty(line_pio(l), l->sm)
           || rx_count(l) >= l->run.max_rx;
}

/**
 * @brief Wait up to RX_SETTLE_SPINS FIFO reads for the channel to take the
 *        words still in the receive FIFO.
 *
 * The caller has made sure the machine pushes nothing more.
 */
static void rx_settle(const hitec_line_t *l)
{
    for (unsigned i = 0; i < RX_SETTLE_SPINS && !rx_settled(l); ++i) {
        tight_loop_contents();
    }
}

/* --- PRECHARGE: the sampler ---------------------------------------------- */

/** @brief The longest @p words sample words take, in us, rounded up. */
static uint32_t pre_words_us(const hitec_line_t *l, uint32_t words)
{
    return hitec_pre_cycles_ns(&l->pre_timing,
                               (uint64_t)words * 32u * l->pre_timing.period)
               / 1000u
           + 1u;
}

/**
 * @brief The pad to the sampler, and the sampler started: the pin's control and
 *        pad registers kept, the pin in the sampler's block with no input
 *        override, 2 mA and slow slew, the samples to the line object.
 */
static void pre_take(hitec_line_t *l)
{
    const PIO pio = (PIO)l->pre_pio;
    const uint sm = l->pre_sm;
    const uint ch = (uint)l->dma_pre;
    const unsigned pin = l->pin;
    pio_sm_config c = pio_get_default_sm_config();
    dma_channel_config dc = dma_channel_get_default_config(ch);

    sm_config_set_wrap(&c, l->pre_offset, l->pre_offset + l->pre_len - 1u);
    sm_config_set_set_pins(&c, pin, 1);
    sm_config_set_in_pin_base(&c, pin);
    sm_config_set_in_shift(&c, false, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv_int_frac8(&c, 1u, 0u);
    pio_sm_init(pio, sm, l->pre_offset, &c);
    /* The latch high and the direction an input before the pad is the
     * sampler's: the hand-over leaves the pad released. */
    pio_sm_exec(pio, sm, pio_encode_set(pio_pins, 1));
    pio_sm_exec(pio, sm, pio_encode_set(pio_pindirs, 0));

    l->pre_ctrl = io_bank0_hw->io[pin].ctrl;
    l->pre_pad = pads_bank0_hw->io[pin];
    l->pre_held = 1;
    l->pre_stall = 0;
    /* pio_gpio_init() writes the whole control register: the function, no
     * override, so the sampler reads the pad as it is. */
    pio_gpio_init(pio, pin);
    gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_2MA);
    gpio_set_slew_rate(pin, GPIO_SLEW_RATE_SLOW);

    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, false);
    channel_config_set_write_increment(&dc, true);
    channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, false));
    dma_channel_configure(ch, &dc, l->samples, &pio->rxf[sm], l->pre.words,
                          true);
    pio->fdebug = 1u << (PIO_FDEBUG_RXSTALL_LSB + sm);
    l->pre_t_us = time_us_32();
    pio_sm_set_enabled(pio, sm, true);
}

/**
 * @brief Sample words written so far: the channel's write address moves after
 *        each write.
 */
static uint32_t pre_words_in(const hitec_line_t *l)
{
    const uintptr_t at = (uintptr_t)dma_channel_hw_addr((uint)l->dma_pre)
                             ->write_addr;
    const uintptr_t base = (uintptr_t)l->samples;
    const uint32_t n = at > base ? (uint32_t)((at - base) / 4u) : 0u;

    return n > l->pre.words ? l->pre.words : n;
}

/** @brief Whether the sampler's receive FIFO has run full since its start. */
static bool pre_rx_stalled(const hitec_line_t *l)
{
    const PIO pio = (PIO)l->pre_pio;

    return ((pio->fdebug >> (PIO_FDEBUG_RXSTALL_LSB + l->pre_sm)) & 1u) != 0u;
}

/**
 * @brief The sampler stopped with the pad released and the pin's two registers
 *        back as they were.
 *
 * True when samples went missing: the receive FIFO ran full while the channel
 * still had words to take.  Once the channel has every word, the state machine
 * runs on until this stops it and fills the FIFO in 8 words, 278 us at 150 MHz;
 * that stall loses nothing.
 */
static bool pre_give(hitec_line_t *l)
{
    const PIO pio = (PIO)l->pre_pio;
    const uint sm = l->pre_sm;
    bool stalled;

    if (l->pre_held == 0u) {
        return false;
    }
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_exec(pio, sm, pio_encode_set(pio_pindirs, 0));
    stalled = l->pre_stall != 0u
              || (dma_channel_is_busy((uint)l->dma_pre) && pre_rx_stalled(l));
    /* The channel takes what is still in the FIFO; its write address
     * counts it. */
    for (unsigned i = 0; i < RX_SETTLE_SPINS
                         && !pio_sm_is_rx_fifo_empty(pio, sm)
                         && dma_channel_is_busy((uint)l->dma_pre);
         ++i) {
        tight_loop_contents();
    }
    io_bank0_hw->io[l->pin].ctrl = l->pre_ctrl;
    pads_bank0_hw->io[l->pin] = l->pre_pad;
    l->pre_held = 0;
    if (dma_channel_is_busy((uint)l->dma_pre)) {
        dma_channel_abort((uint)l->dma_pre);
    }
    pio_sm_clear_fifos(pio, sm);
    return stalled;
}

/**
 * @brief The sampler from the start of a LISTEN, or once the request is out.
 */
static void pre_sample(hitec_line_t *l)
{
    l->pre_span_us = pre_words_us(l, l->pre.words);
    pre_take(l);
}

/** @brief poll() of a transaction the sampler receives. */
static bool pre_poll(hitec_line_t *l, hitec_capture_t *out)
{
    const uint32_t now = time_us_32();
    int64_t offset;

    if (l->pre.phase == (uint8_t)HITEC_PRE_TX) {
        if (line_idle(l)) {
            hitec_pre_tx_done(&l->pre);
            pre_sample(l);
            return false;
        }
        if (!hitec_due(now, l->t_start_us, l->pre_tx_us + HITEC_LINE_GRACE_US)) {
            return false;
        }
        line_restart(l);
        hitec_pre_lost(&l->pre);
    } else if (l->pre.phase == (uint8_t)HITEC_PRE_SAMPLE) {
        const bool all_in = !dma_channel_is_busy((uint)l->dma_pre);
        const uint32_t n = pre_words_in(l);

        /* A stall seen while the channel still takes words is a loss; one
         * after its last word is not (pre_give()). */
        if (!all_in && pre_rx_stalled(l)) {
            l->pre_stall = 1;
        }

        if (hitec_pre_step(&l->pre, &l->pre_timing, l->samples, n, all_in)) {
            if (pre_give(l)) {
                hitec_pre_lost(&l->pre);
            }
        } else if (hitec_due(now, l->pre_t_us,
                             l->pre_span_us + HITEC_LINE_GRACE_US)) {
            (void)pre_give(l);
            hitec_pre_lost(&l->pre);
        } else {
            return false;
        }
    }
    /* The receiver's time 0: the release and the settle bit after a SEND,
     * the start of a LISTEN.  The sampler's start from it. */
    offset = ((int64_t)(uint32_t)(l->pre_t_us - l->t_start_us)) * 1000
             - (int64_t)l->run.lead_ns;
    if (offset > INT32_MAX) {
        offset = INT32_MAX;
    } else if (offset < INT32_MIN) {
        offset = INT32_MIN;
    }
    hitec_pre_capture(&l->pre, &l->pre_timing, (int32_t)offset, out);
    l->busy = 0;
    l->aborted = 0;
    l->cut = 0;
    l->pre_on = 0;
    return true;
}

void hitec_line_init(hitec_line_t *line)
{
    if (line == NULL) {
        return;
    }
    memset(line, 0, sizeof(*line));
    line->dma_tx = -1;
    line->dma_rx = -1;
    line->dma_pre = -1;
}

hitec_line_err_t hitec_line_open(hitec_line_t *line, unsigned pin,
                                 uint8_t family, uint8_t drive)
{
    return hitec_line_open_rx(line, pin, family, drive, HITEC_RX_PULL_UP);
}

/** @brief The sampler's program as the SDK takes it. */
static pio_program_t pre_program(const hitec_line_t *l)
{
    pio_program_t p;

    memset(&p, 0, sizeof(p));
    p.instructions = l->pre_prog;
    p.length = (uint8_t)l->pre_len;
    p.origin = -1;
    return p;
}

/**
 * @brief Claim the sampler's state machine, program and DMA channel for @p pin.
 *
 * Touches no pin; on false nothing stays claimed.
 */
static hitec_line_err_t pre_claim(hitec_line_t *l, unsigned pin)
{
    pio_program_t prog;
    PIO pio;
    uint sm;
    uint offset;
    int ch;

    if (!hitec_pre_timing(clock_get_hz(clk_sys), &l->pre_timing)) {
        return HITEC_LINE_ERR_CLOCK;
    }
    l->pre_len = hitec_pre_program(&l->pre_timing, l->pre_prog,
                                   HITEC_PRE_PROGRAM_MAX);
    if (l->pre_len == 0u) {
        return HITEC_LINE_ERR_CLOCK;
    }
    prog = pre_program(l);
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            &prog, &pio, &sm, &offset, pin, 1, true)) {
        return HITEC_LINE_ERR_PIO;
    }
    ch = dma_claim_unused_channel(false);
    if (ch < 0) {
        pio_remove_program_and_unclaim_sm(&prog, pio, sm, offset);
        return HITEC_LINE_ERR_DMA;
    }
    l->pre_pio = pio;
    l->pre_sm = sm;
    l->pre_offset = offset;
    l->dma_pre = (int8_t)ch;
    return HITEC_LINE_OK;
}

/**
 * @brief Give back the sampler state machine, its program and its DMA channel.
 *
 */
static void pre_unclaim(hitec_line_t *l)
{
    if (l->dma_pre >= 0) {
        const pio_program_t prog = pre_program(l);

        dma_channel_abort((uint)l->dma_pre);
        dma_channel_unclaim((uint)l->dma_pre);
        pio_remove_program_and_unclaim_sm(&prog, (PIO)l->pre_pio, l->pre_sm,
                                          l->pre_offset);
    }
    l->dma_pre = -1;
    l->pre_pio = NULL;
}

hitec_line_err_t hitec_line_open_rx(hitec_line_t *line, unsigned pin,
                                    uint8_t family, uint8_t drive,
                                    uint8_t rx_mode)
{
    hitec_pio_clock_t clock;
    PIO pio;
    uint sm;
    uint offset;
    int tx;
    int rx;

    if (line == NULL || pin >= NUM_BANK0_GPIOS || hitec_bit_rate(family) == 0u
        || drive > HITEC_DRIVE_PUSH_PULL
        || (drive == HITEC_DRIVE_PUSH_PULL && family != HITEC_D)
        || rx_mode > HITEC_RX_PRECHARGE
        || (rx_mode == HITEC_RX_PRECHARGE && family != HITEC_D)) {
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
    /* The sampler after the line's own program, so the line's 29 words
     * find a block first. */
    line->dma_pre = -1;
    line->pre_pio = NULL;
    if (rx_mode == HITEC_RX_PRECHARGE) {
        const hitec_line_err_t e = pre_claim(line, pin);

        if (e != HITEC_LINE_OK) {
            dma_channel_unclaim((uint)tx);
            dma_channel_unclaim((uint)rx);
            pio_remove_program_and_unclaim_sm(&hitec_line_program, pio, sm,
                                              offset);
            return e;
        }
    }
    line->rx_mode = rx_mode;
    line->pre_on = 0;
    line->pre_held = 0;
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
    pio_sm_init(pio, sm, offset + HITEC_PIO_IDLE_PC, &c);

    /* The state machine's latch low and its direction an input before the
     * pad is its: the pad goes from whatever it was to a released input,
     * never to a driven level. */
    pio_sm_exec(pio, sm, pio_encode_set(pio_pins, 0));
    pio_sm_exec(pio, sm, pio_encode_set(pio_pindirs, 0));
    set_pull(pin, family);
    /* pio_gpio_init() writes the pin's whole control register, overrides
     * included, so the input override comes after it. */
    pio_gpio_init(pio, pin);
    gpio_set_inover(pin, family == HITEC_D ? GPIO_OVERRIDE_INVERT
                                           : GPIO_OVERRIDE_NORMAL);
    line_ready(pio, sm, offset);
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

    /* The sampler gives the pad back first, as it found it. */
    (void)pre_give(line);
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
    pre_unclaim(line);
    line->dma_tx = -1;
    line->dma_rx = -1;
    line->open = 0;
    line->busy = 0;
    line->aborted = 0;
    line->cut = 0;
    line->pre_on = 0;
    line->rx_mode = HITEC_RX_PULL_UP;
}

bool hitec_line_is_open(const hitec_line_t *line)
{
    return line != NULL && line->open != 0u;
}

bool hitec_line_busy(const hitec_line_t *line)
{
    return line != NULL && line->busy != 0u;
}

uint8_t hitec_line_rx_mode(const hitec_line_t *line)
{
    return hitec_line_is_open(line) ? line->rx_mode
                                    : (uint8_t)HITEC_RX_PULL_UP;
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
    if (line->pre_on != 0u) {
        /* The request on the line: it stops where it is.  The sampler: it
         * stops, and the words it wrote are decoded; a stall it had is
         * kept as the capture's overflow beside the abort. */
        bool lost = false;

        if (line->pre.phase == (uint8_t)HITEC_PRE_TX) {
            pio_sm_set_enabled(line_pio(line), line->sm, false);
            line_restart(line);
        } else if (line->pre_held != 0u) {
            lost = pre_give(line);
        }
        hitec_pre_abort(&line->pre, &line->pre_timing, line->samples,
                        line->dma_pre >= 0 ? pre_words_in(line) : 0u);
        if (lost) {
            hitec_pre_lost(&line->pre);
        }
        line->aborted = 1;
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

/**
 * @brief Start a transaction on the line's own state machine.
 *
 */
static bool line_start(hitec_line_t *l, const hitec_xfer_t *xfer);

bool hitec_line_start(void *ctx, const hitec_xfer_t *xfer)
{
    hitec_line_t *l = (hitec_line_t *)ctx;
    hitec_xfer_t tx;

    if (l == NULL || l->open == 0u || l->busy != 0u || !line_idle(l)) {
        return false;
    }
    if (l->rx_mode != (uint8_t)HITEC_RX_PRECHARGE) {
        return line_start(l, xfer);
    }
    switch (hitec_pre_begin(&l->pre, &l->pre_timing, xfer)) {
    case HITEC_PRE_PLAN_LINE:
        return line_start(l, xfer);
    case HITEC_PRE_PLAN_SEND:
        hitec_pre_tx_xfer(xfer, &tx);
        if (!line_start(l, &tx)) {
            return false;
        }
        /* The request alone has the line's time; the run, the sampling's
         * as well. */
        l->pre_tx_us = l->run.span_us;
        l->run.span_us += pre_words_us(l, l->pre.words);
        l->pre_on = 1;
        return true;
    case HITEC_PRE_PLAN_LISTEN:
        memset(&l->run, 0, sizeof(l->run));
        l->run.family = xfer->family;
        l->run.window_ns = xfer->window_us * 1000u;
        l->run.span_us = pre_words_us(l, l->pre.words);
        l->aborted = 0;
        l->cut = 0;
        l->pre_on = 1;
        l->busy = 1;
        l->t_start_us = time_us_32();
        pre_sample(l);
        return true;
    case HITEC_PRE_PLAN_REFUSE:
    default:
        return false;
    }
}

/** @brief A transaction on the line's own state machine. */
static bool line_start(hitec_line_t *l, const hitec_xfer_t *xfer)
{
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
    if (l->pre_on != 0u) {
        return pre_poll(l, out);
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
