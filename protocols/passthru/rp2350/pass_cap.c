/**
 * @file
 * @brief The pass-through on the RP2350B.
 *
 * See pass_cap.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "pass_cap.h"

#include <string.h>

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/structs/timer.h"

#include "pass.pio.h"
#include "pass_decode.h"
#include "pass_ring.h"

/** @brief One ring's bytes: 8192 words, 32KiB. */
#define RING_BYTES (PASS_RING_EVENTS * 4u)
/** @brief The ring's size is 2^RING_BITS bytes, the DMA's largest ring. */
#define RING_BITS  15u
_Static_assert(RING_BYTES == (1u << RING_BITS), "a ring is 2^15 bytes");

/** @brief The states, written by channel A. */
static volatile uint32_t s_states[PASS_RING_EVENTS]
    __attribute__((aligned(RING_BYTES)));
/** @brief The stamps, written by channel B. */
static volatile uint32_t s_stamps[PASS_RING_EVENTS]
    __attribute__((aligned(RING_BYTES)));

static bool s_running;        /**< pass_cap_start() succeeded */
static uint s_sm_cap;         /**< CAPTURE's state machine in PIO0 */
static int  s_dma_b = -1;     /**< the stamp channel */

/** @brief The GPIO of output @p k. */
#define OUT_PIN(k) (PASS_CAP_PIN0 + 2u * (unsigned)(k))
/** @brief The GPIO of input @p k. */
#define IN_PIN(k)  (PASS_CAP_PIN0 + 1u + 2u * (unsigned)(k))
/** @brief Every output pin, GPIO 16 to GPIO 32 even, as a 64-bit mask. */
#define OUT_MASK64 (0x15555ull << PASS_CAP_PIN0)
/** @brief GPIO 16 to GPIO 32, MIRROR's out pins, as a 64-bit mask. */
#define SPAN_MASK64 (0x1FFFFull << PASS_CAP_PIN0)

/**
 * @brief MIRROR on @p sm at @p off: in from GPIO 17, out to GPIO 16, 17
 *        pins each, outputs on the even pins only.
 *
 * @param sm   The state machine.
 * @param off  The program's offset.
 */
static void mirror_init(uint sm, uint off)
{
    pio_sm_config c = pass_mirror_program_get_default_config(off);
    sm_config_set_in_pin_base(&c, PASS_CAP_PIN0 + 1u);
    sm_config_set_in_pin_count(&c, PASS_CHANNELS * 2u - 1u);
    sm_config_set_out_pins(&c, PASS_CAP_PIN0, PASS_CHANNELS * 2u - 1u);
    sm_config_set_clkdiv_int_frac8(&c, 1u, 0u);
    pio_sm_init(pio0, sm, off, &c);
    pio_sm_set_pindirs_with_mask64(pio0, sm, OUT_MASK64, SPAN_MASK64);
}

/**
 * @brief CAPTURE on @p sm at @p off: in from GPIO 16, 18 pins, Y all ones
 *        so the first sample is pushed.
 *
 * @param sm   The state machine.
 * @param off  The program's offset.
 */
static void capture_init(uint sm, uint off)
{
    pio_sm_config c = pass_capture_program_get_default_config(off);
    sm_config_set_in_pin_base(&c, PASS_CAP_PIN0);
    sm_config_set_in_pin_count(&c, PASS_PINS);
    sm_config_set_in_shift(&c, false, false, 32);
    sm_config_set_clkdiv_int_frac8(&c, 1u, 0u);
    pio_sm_init(pio0, sm, off + pass_capture_offset_sample, &c);
    pio_sm_exec(pio0, sm, pio_encode_mov_not(pio_y, pio_null));
    pio0->fdebug = 1u << (PIO_FDEBUG_RXSTALL_LSB + sm);
}

/**
 * @brief The two chained channels: A from the FIFO to the states, B from
 *        the timer to the stamps.  A is started; it waits for the FIFO.
 *
 * @param a  Channel A.
 * @param b  Channel B.
 */
static void dma_init(uint a, uint b)
{
    dma_channel_config ca = dma_channel_get_default_config(a);
    channel_config_set_transfer_data_size(&ca, DMA_SIZE_32);
    channel_config_set_read_increment(&ca, false);
    channel_config_set_write_increment(&ca, true);
    channel_config_set_ring(&ca, true, RING_BITS);
    channel_config_set_dreq(&ca, pio_get_dreq(pio0, s_sm_cap, false));
    channel_config_set_chain_to(&ca, b);
    dma_channel_configure(a, &ca, s_states, &pio0->rxf[s_sm_cap], 1u, false);

    dma_channel_config cb = dma_channel_get_default_config(b);
    channel_config_set_transfer_data_size(&cb, DMA_SIZE_32);
    channel_config_set_read_increment(&cb, false);
    channel_config_set_write_increment(&cb, true);
    channel_config_set_ring(&cb, true, RING_BITS);
    channel_config_set_chain_to(&cb, a);
    dma_channel_configure(b, &cb, s_stamps, &timer0_hw->timerawl, 1u, false);
    dma_channel_start(a);
}

bool pass_cap_start(void)
{
    if (s_running) {
        return true;
    }
    if (pio_set_gpio_base(pio0, PASS_CAP_PIN0) != PICO_OK
        || !pio_can_add_program(pio0, &pass_mirror_program)) {
        return false;
    }
    const int sm_m = pio_claim_unused_sm(pio0, false);
    const int sm_c = pio_claim_unused_sm(pio0, false);
    const int dma_a = dma_claim_unused_channel(false);
    const int dma_b = dma_claim_unused_channel(false);
    int off_m = -1;
    int off_c = -1;
    if (sm_m >= 0 && sm_c >= 0 && dma_a >= 0 && dma_b >= 0) {
        off_m = pio_add_program(pio0, &pass_mirror_program);
        if (off_m >= 0 && pio_can_add_program(pio0, &pass_capture_program)) {
            off_c = pio_add_program(pio0, &pass_capture_program);
        }
    }
    if (off_c < 0) {
        if (off_m >= 0) {
            pio_remove_program(pio0, &pass_mirror_program, (uint)off_m);
        }
        if (sm_m >= 0) { pio_sm_unclaim(pio0, (uint)sm_m); }
        if (sm_c >= 0) { pio_sm_unclaim(pio0, (uint)sm_c); }
        if (dma_a >= 0) { dma_channel_unclaim((uint)dma_a); }
        if (dma_b >= 0) { dma_channel_unclaim((uint)dma_b); }
        return false;
    }
    s_sm_cap = (uint)sm_c;
    s_dma_b  = dma_b;
    for (unsigned k = 0; k < PASS_CHANNELS; ++k) {
        gpio_init(IN_PIN(k));
        gpio_set_dir(IN_PIN(k), GPIO_IN);
        gpio_pull_down(IN_PIN(k));
    }
    /* A cleared ring: the reader's first check expects 0 and 0 in the last
     * slot (pass_ring.h). */
    for (unsigned i = 0; i < PASS_RING_EVENTS; ++i) {
        s_states[i] = 0u;
        s_stamps[i] = 0u;
    }
    mirror_init((uint)sm_m, (uint)off_m);
    capture_init((uint)sm_c, (uint)off_c);
    dma_init((uint)dma_a, (uint)dma_b);
    pio_set_sm_mask_enabled(pio0, (1u << (uint)sm_m) | (1u << (uint)sm_c),
                            true);
    s_running = true;
    return true;
}

bool pass_cap_running(void)
{
    return s_running;
}

const volatile uint32_t *pass_cap_states(void)
{
    return s_states;
}

const volatile uint32_t *pass_cap_stamps(void)
{
    return s_stamps;
}

uint32_t pass_cap_head(void)
{
    if (s_dma_b < 0) {
        return 0u;
    }
    const uint32_t a = (uint32_t)dma_channel_hw_addr((uint)s_dma_b)->write_addr;
    return ((a - (uint32_t)(uintptr_t)s_stamps) / 4u)
           & (PASS_RING_EVENTS - 1u);
}

bool pass_cap_stalled(void)
{
    if (!s_running) {
        return false;
    }
    const uint32_t bit = 1u << (PIO_FDEBUG_RXSTALL_LSB + s_sm_cap);
    if ((pio0->fdebug & bit) == 0u) {
        return false;
    }
    pio0->fdebug = bit;
    return true;
}

void pass_cap_route(unsigned k, bool mirror)
{
    if (k >= PASS_CHANNELS) {
        return;
    }
    const uint pin = OUT_PIN(k);
    if (mirror && s_running) {
        gpio_set_function(pin, GPIO_FUNC_PIO0);
        return;
    }
    gpio_init(pin);
    gpio_put(pin, false);
    gpio_set_dir(pin, GPIO_OUT);
}
