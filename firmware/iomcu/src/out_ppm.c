/*
 * PPM out: a PIO state machine and a self-restarting pair of DMA channels.
 * See out_ppm.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "out_ppm.h"

#include <string.h>

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"

#include "outputs.h"
#include "ppm.h"
#include "ppm.pio.h"

/*
 * The program renders a run of N microseconds as a one-cycle `out` and N-1
 * cycles of loop, so the count in the FIFO (first in first out) is two less
 * than the duration.  shared/ppm refuses a run shorter than the mark, and the
 * mark is 300 us, so this cannot go negative.
 */
#define RUN_TO_COUNT(us)  ((uint32_t)(us) - 2u)

typedef struct {
    bool     used;
    bool     running;
    uint8_t  pin;
    uint8_t  channels;
    uint16_t frame_us;

    PIO      pio;
    uint     sm;
    uint     offset;

    int      dma_data;
    int      dma_ctrl;
    dma_channel_config cfg_data;
    dma_channel_config cfg_ctrl;

    uint32_t buf[PPM_MAX_RUNS];
    uint32_t words;
    /* The widths the buffer currently holds, so a service pass that has
     * nothing new to say does not rewrite a frame that is being played. */
    uint16_t last_us[PPM_MAX_CHANNELS];
    bool     have_last;
    /* What the control channel writes back into the data channel: the buffer
     * address, in a word of its own so the DMA has somewhere to read it. */
    const uint32_t *restart;
} ppm_out_t;

/* One per slot the bank can hold.  DMA channels run out before these do. */
static ppm_out_t s_out[OUT_MAX_SLOTS];

static ppm_out_t *find(uint8_t pin)
{
    for (unsigned i = 0; i < OUT_MAX_SLOTS; ++i) {
        if (s_out[i].used && s_out[i].pin == pin) {
            return &s_out[i];
        }
    }
    return NULL;
}

/*
 * Erratum RP2350-E5: a channel that is aborted while it or a channel chained
 * to it is still enabled can be retriggered by the abort itself.  Both are
 * therefore disabled first, and the enable bit is put back when the frame is
 * started again.
 */
static void set_enabled(ppm_out_t *s, bool on)
{
    channel_config_set_enable(&s->cfg_data, on);
    channel_config_set_enable(&s->cfg_ctrl, on);
    dma_channel_set_config((uint)s->dma_data, &s->cfg_data, false);
    dma_channel_set_config((uint)s->dma_ctrl, &s->cfg_ctrl, false);
}

static void halt(ppm_out_t *s)
{
    if (s->dma_data < 0 || s->dma_ctrl < 0) {
        return;
    }
    set_enabled(s, false);
    dma_channel_abort((uint)s->dma_ctrl);
    dma_channel_abort((uint)s->dma_data);
    if (s->pio != NULL) {
        pio_sm_set_enabled(s->pio, s->sm, false);
        pio_sm_clear_fifos(s->pio, s->sm);
    }
    /*
     * The pad goes back to the processor and is held low.  Left to the state
     * machine it would keep whatever level the frame stopped on, and a mark
     * that never ends is a pulse of unbounded width.
     */
    gpio_set_function(s->pin, GPIO_FUNC_SIO);
    gpio_set_dir(s->pin, GPIO_OUT);
    gpio_put(s->pin, 0);
    s->running   = false;
    /* The next write starts the frame again, so it has to rebuild the buffer
     * rather than recognise the widths it already holds. */
    s->have_last = false;
}

static void teardown(ppm_out_t *s)
{
    halt(s);
    if (s->dma_data >= 0) {
        dma_channel_unclaim((uint)s->dma_data);
    }
    if (s->dma_ctrl >= 0) {
        dma_channel_unclaim((uint)s->dma_ctrl);
    }
    if (s->pio != NULL) {
        pio_remove_program(s->pio, &ppm_tx_program, s->offset);
        pio_sm_unclaim(s->pio, s->sm);
    }
    const uint8_t pin = s->pin;
    memset(s, 0, sizeof(*s));
    s->dma_data = -1;
    s->dma_ctrl = -1;
    /* An unbound pin drives nothing: the next configuration may want it. */
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, GPIO_IN);
}

static ppm_cfg_t cfg_of(uint16_t frame_us)
{
    return (ppm_cfg_t){
        .mark_us     = PPM_DEFAULT_MARK_US,
        .frame_us    = frame_us,
        .sync_min_us = PPM_SYNC_MIN_US,
    };
}

bool out_ppm_bind(uint8_t pin, uint8_t channels, uint16_t rate_hz)
{
    if (channels == 0u || channels > PPM_MAX_CHANNELS || rate_hz == 0u) {
        return false;
    }
    const uint32_t frame_us = 1000000u / rate_hz;
    if (frame_us < 2u || frame_us > 65535u) {
        return false;
    }

    ppm_out_t *s = find(pin);
    if (s != NULL) {
        /* The same shape it already has: leave the output running rather
         * than rebuilding it and dropping a frame. */
        return s->channels == channels && s->frame_us == (uint16_t)frame_us;
    }

    const ppm_cfg_t cfg = cfg_of((uint16_t)frame_us);
    /* Refused at bind rather than at the first write: a frame rate that
     * cannot carry this many channels at full travel is a configuration
     * mistake, and finding it when the sticks reach the end is finding it
     * once something has already moved. */
    if (ppm_min_frame_us(channels, &cfg) > frame_us) {
        return false;
    }

    for (unsigned i = 0; i < OUT_MAX_SLOTS && s == NULL; ++i) {
        if (!s_out[i].used) {
            s = &s_out[i];
        }
    }
    if (s == NULL) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->dma_data = -1;
    s->dma_ctrl = -1;
    s->pin      = pin;

    /*
     * The SDK picks the block.  A PIO block addresses 32 pins from a base of
     * 0 or 16, fixed while it holds a program, and the pin arrives from the
     * host -- so which block can reach it is not knowable until it does.
     */
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            &ppm_tx_program, &s->pio, &s->sm, &s->offset, pin, 1, true)) {
        s->pio = NULL;
        return false;
    }
    ppm_tx_program_init(s->pio, s->sm, s->offset, pin);

    s->dma_data = dma_claim_unused_channel(false);
    s->dma_ctrl = dma_claim_unused_channel(false);
    if (s->dma_data < 0 || s->dma_ctrl < 0) {
        teardown(s);
        return false;
    }

    s->channels = channels;
    s->frame_us = (uint16_t)frame_us;
    s->words    = ((uint32_t)channels + 1u) * 2u;
    s->restart  = s->buf;

    /* The frame: buffer into the state machine, paced by its FIFO. */
    s->cfg_data = dma_channel_get_default_config((uint)s->dma_data);
    channel_config_set_transfer_data_size(&s->cfg_data, DMA_SIZE_32);
    channel_config_set_read_increment(&s->cfg_data, true);
    channel_config_set_write_increment(&s->cfg_data, false);
    channel_config_set_dreq(&s->cfg_data, pio_get_dreq(s->pio, s->sm, true));
    channel_config_set_chain_to(&s->cfg_data, (uint)s->dma_ctrl);
    dma_channel_configure((uint)s->dma_data, &s->cfg_data,
                          &s->pio->txf[s->sm], s->buf, s->words, false);

    /*
     * And the restart: one transfer that writes the buffer address into the
     * data channel's triggering read-address alias.  Triggering reloads the
     * transfer count from its shadow as well, so the frame plays again from
     * the top with nothing for the processor to do.
     */
    s->cfg_ctrl = dma_channel_get_default_config((uint)s->dma_ctrl);
    channel_config_set_transfer_data_size(&s->cfg_ctrl, DMA_SIZE_32);
    channel_config_set_read_increment(&s->cfg_ctrl, false);
    channel_config_set_write_increment(&s->cfg_ctrl, false);
    dma_channel_configure(
        (uint)s->dma_ctrl, &s->cfg_ctrl,
        &dma_channel_hw_addr((uint)s->dma_data)->al3_read_addr_trig,
        &s->restart, 1, false);

    s->used = true;
    /* Nothing is emitted until a frame exists to emit. */
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, 0);
    return true;
}

void out_ppm_release(uint8_t pin)
{
    ppm_out_t *s = find(pin);
    if (s != NULL) {
        teardown(s);
    }
}

void out_ppm_stop(uint8_t pin)
{
    ppm_out_t *s = find(pin);
    if (s != NULL && s->running) {
        halt(s);
    }
}

bool out_ppm_write(uint8_t pin, const uint16_t *channel_us, uint8_t channels)
{
    ppm_out_t *s = find(pin);
    if (s == NULL || channels != s->channels) {
        return false;
    }
    /*
     * The loop turns over thousands of times a frame, and rewriting the
     * buffer under the DMA every pass would widen the window in which a
     * frame carries values from two updates for no gain at all.
     */
    if (s->have_last && s->running
        && memcmp(s->last_us, channel_us,
                  (size_t)channels * sizeof(uint16_t)) == 0) {
        return true;
    }
    const ppm_cfg_t cfg = cfg_of(s->frame_us);
    uint16_t runs[PPM_MAX_RUNS];
    const size_t n = ppm_frame(channel_us, channels, &cfg, runs, PPM_MAX_RUNS);
    if (n != s->words) {
        /* shared/ppm builds a frame whole or not at all, so nothing here is
         * half-written; the output stops rather than sending the last one
         * for ever. */
        out_ppm_stop(pin);
        return false;
    }
    /*
     * Written in place under the DMA.  A word is a single aligned store, so a
     * frame caught mid-update carries some channels from the update before
     * it -- one frame of staleness on a signal that is a stream of frames.
     */
    for (size_t i = 0; i < n; ++i) {
        s->buf[i] = RUN_TO_COUNT(runs[i]);
    }
    memcpy(s->last_us, channel_us, (size_t)channels * sizeof(uint16_t));
    s->have_last = true;
    if (!s->running) {
        pio_gpio_init(s->pio, pin);
        pio_sm_set_consecutive_pindirs(s->pio, s->sm, pin, 1, true);
        pio_sm_clear_fifos(s->pio, s->sm);
        pio_sm_restart(s->pio, s->sm);
        pio_sm_exec(s->pio, s->sm, pio_encode_jmp(s->offset));
        pio_sm_set_enabled(s->pio, s->sm, true);
        set_enabled(s, true);
        dma_channel_set_read_addr((uint)s->dma_data, s->buf, true);
        s->running = true;
    }
    return true;
}
