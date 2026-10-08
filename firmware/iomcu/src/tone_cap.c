/*
 * The phase tap's capture.  See tone_cap.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "tone_cap.h"

#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "tone_cap.pio.h"
#include "tone_svc.h"

/* 16 kB, on a 16 kB boundary: the DMA wraps its write address on it. */
#define RING_BYTES (TONE_RING_WORDS * 4u)
#define RING_BITS  14u
_Static_assert(RING_BYTES == (1u << RING_BITS), "the ring's size is 2^14");

static uint32_t s_ring[TONE_RING_WORDS] __attribute__((aligned(RING_BYTES)));

/* Written by core 0 while the capture is stopped or starting, read by
 * core 1: running is set last and cleared first. */
static volatile bool     s_running;
static PIO               s_pio;
static uint              s_sm;
static uint              s_offset;
static int               s_dma = -1;
static volatile uint32_t s_wr_base;      /* the ring's address            */
static uint8_t           s_pin;
static uint64_t          s_start_us;

void tone_cap_rest(uint8_t pin)
{
    if (s_running) {
        return;
    }
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_down(pin);
}

bool tone_cap_start(uint8_t pin)
{
    tone_cap_stop();
    if (clock_get_hz(clk_sys) != TONE_SVC_SYS_HZ) {
        return false;
    }
    PIO pio;
    uint sm;
    uint offset;
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            &tone_cap_program, &pio, &sm, &offset, pin, 1, true)) {
        return false;
    }
    const int dma = dma_claim_unused_channel(false);
    if (dma < 0) {
        pio_remove_program_and_unclaim_sm(&tone_cap_program, pio, sm, offset);
        return false;
    }
    s_pio = pio;
    s_sm = sm;
    s_offset = offset;
    s_dma = dma;
    s_pin = pin;

    /* The state machine, stopped, with the hold-off in its OSR. */
    tone_cap_program_init(pio, sm, offset, pin,
                          (uint)tone_ns_ticks(TONE_SVC_TICK_HZ,
                                              TONE_SVC_HOLD_NS));
    gpio_pull_down(pin);
    pio->fdebug = 1u << (PIO_FDEBUG_RXSTALL_LSB + sm);

    /* The words into the ring, endlessly, the write address wrapping on
     * it.  Started before the state machine so the first word has a place.
     * The ring is cleared first: a word in its last slot before the reader
     * has taken one shows a lap (edge_ring.h). */
    memset(s_ring, 0, sizeof(s_ring));
    dma_channel_config c = dma_channel_get_default_config((uint)dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, RING_BITS);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, false));
    s_wr_base = (uint32_t)(uintptr_t)s_ring;
    dma_channel_configure((uint)dma, &c, s_ring, &pio->rxf[sm],
                          dma_encode_endless_transfer_count(), true);

    const uint64_t t0 = time_us_64();
    pio_sm_set_enabled(pio, sm, true);
    const uint64_t t1 = time_us_64();
    s_start_us = (t0 + t1) / 2u;
    s_running = true;
    return true;
}

bool tone_cap_pause(void)
{
    const bool was = s_running;
    s_running = false;
    return was;
}

void tone_cap_resume(void)
{
    s_running = true;
}

void tone_cap_stop(void)
{
    if (s_dma < 0) {
        return;
    }
    s_running = false;
    pio_sm_set_enabled(s_pio, s_sm, false);
    dma_channel_abort((uint)s_dma);
    dma_channel_unclaim((uint)s_dma);
    s_dma = -1;
    pio_remove_program_and_unclaim_sm(&tone_cap_program, s_pio, s_sm,
                                      s_offset);
    gpio_init(s_pin);
    gpio_set_dir(s_pin, GPIO_IN);
    gpio_pull_down(s_pin);
}

bool tone_cap_running(void)
{
    return s_running;
}

uint64_t tone_cap_start_us(void)
{
    return s_start_us;
}

const uint32_t *tone_cap_ring(void)
{
    return s_ring;
}

uint32_t tone_cap_wr(void)
{
    const int dma = s_dma;
    if (!s_running || dma < 0) {
        return 0u;
    }
    const uint32_t a = (uint32_t)dma_channel_hw_addr((uint)dma)->write_addr;
    return ((a - s_wr_base) / 4u) & (TONE_RING_WORDS - 1u);
}

bool tone_cap_stalled(void)
{
    const int dma = s_dma;
    if (!s_running || dma < 0) {
        return false;
    }
    const uint32_t bit = 1u << (PIO_FDEBUG_RXSTALL_LSB + s_sm);
    if ((s_pio->fdebug & bit) == 0u) {
        return false;
    }
    s_pio->fdebug = bit;
    return true;
}
