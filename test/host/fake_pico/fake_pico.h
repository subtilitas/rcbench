/*
 * The part of the pico-sdk protocols/escwire/rp2350/ew_line.c calls, for
 * test_ew_line.c: types and declarations.  The test defines the functions and
 * keeps the pad's state, so it can see what the driver leaves on the pin at
 * each moment.  It says nothing about the chip.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FAKE_PICO_H
#define FAKE_PICO_H

#include <stdbool.h>
#include <stdint.h>

typedef unsigned int uint;

#define NUM_BANK0_GPIOS 48u
#define PIO_FDEBUG_RXSTALL_LSB 0u
#define GPIO_IN false

typedef struct {
    uint32_t fdebug;
    uint32_t txf[4];
    uint32_t rxf[4];
} pio_hw_t;
typedef pio_hw_t *PIO;

typedef struct {
    uint32_t clkdiv_int;
    uint32_t clkdiv_frac;
    uint32_t out_pin;
    uint32_t set_pin;
    uint32_t in_pin;
    uint32_t jmp_pin;
    uint32_t out_right;
    uint32_t out_auto;
    uint32_t out_thresh;
    uint32_t in_right;
    uint32_t in_auto;
    uint32_t in_thresh;
} pio_sm_config;

typedef struct {
    int origin;
} pio_program_t;

typedef struct {
    uint32_t data_size;
    uint32_t read_inc;
    uint32_t write_inc;
    uint32_t dreq;
} dma_channel_config;

typedef struct {
    uint32_t transfer_count;
} dma_channel_hw_t;

enum pio_src_dest { pio_pins = 0, pio_pindirs = 4 };
enum dma_channel_transfer_size { DMA_SIZE_8, DMA_SIZE_16, DMA_SIZE_32 };
enum clock_index { clk_sys };
enum gpio_drive_strength {
    GPIO_DRIVE_STRENGTH_2MA, GPIO_DRIVE_STRENGTH_4MA, GPIO_DRIVE_STRENGTH_8MA
};
enum gpio_slew_rate { GPIO_SLEW_RATE_SLOW, GPIO_SLEW_RATE_FAST };

uint32_t clock_get_hz(enum clock_index c);
uint32_t time_us_32(void);
void tight_loop_contents(void);

bool dma_channel_is_busy(uint ch);
void dma_channel_abort(uint ch);
void dma_channel_unclaim(uint ch);
int dma_claim_unused_channel(bool required);
dma_channel_hw_t *dma_channel_hw_addr(uint ch);
dma_channel_config dma_channel_get_default_config(uint ch);
void channel_config_set_transfer_data_size(dma_channel_config *c,
                                           enum dma_channel_transfer_size s);
void channel_config_set_read_increment(dma_channel_config *c, bool on);
void channel_config_set_write_increment(dma_channel_config *c, bool on);
void channel_config_set_dreq(dma_channel_config *c, uint dreq);
void dma_channel_configure(uint ch, const dma_channel_config *c,
                           volatile void *write, const volatile void *read,
                           uint32_t count, bool trigger);

void gpio_init(uint pin);
void gpio_set_dir(uint pin, bool out);
void gpio_set_pulls(uint pin, bool up, bool down);
void gpio_set_input_enabled(uint pin, bool on);
void gpio_set_drive_strength(uint pin, enum gpio_drive_strength d);
void gpio_set_slew_rate(uint pin, enum gpio_slew_rate s);
enum gpio_drive_strength gpio_get_drive_strength(uint pin);
enum gpio_slew_rate gpio_get_slew_rate(uint pin);

uint pio_encode_set(enum pio_src_dest dest, uint value);
uint pio_encode_jmp(uint addr);
void pio_sm_exec(PIO pio, uint sm, uint instr);
void pio_sm_set_enabled(PIO pio, uint sm, bool on);
void pio_sm_clear_fifos(PIO pio, uint sm);
void pio_sm_restart(PIO pio, uint sm);
void pio_sm_init(PIO pio, uint sm, uint pc, const pio_sm_config *c);
void pio_sm_set_clkdiv_int_frac8(PIO pio, uint sm, uint32_t i, uint8_t f);
uint pio_sm_get_pc(PIO pio, uint sm);
bool pio_sm_is_tx_fifo_empty(PIO pio, uint sm);
bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm);
uint pio_get_dreq(PIO pio, uint sm, bool tx);
void pio_gpio_init(PIO pio, uint pin);
bool pio_claim_free_sm_and_add_program_for_gpio_range(
    const pio_program_t *prog, PIO *pio, uint *sm, uint *offset, uint base,
    uint count, bool set_range);
void pio_remove_program_and_unclaim_sm(const pio_program_t *prog, PIO pio,
                                       uint sm, uint offset);
void sm_config_set_out_pins(pio_sm_config *c, uint base, uint count);
void sm_config_set_set_pins(pio_sm_config *c, uint base, uint count);
void sm_config_set_in_pin_base(pio_sm_config *c, uint base);
void sm_config_set_jmp_pin(pio_sm_config *c, uint pin);
void sm_config_set_out_shift(pio_sm_config *c, bool right, bool autopull,
                             uint thresh);
void sm_config_set_in_shift(pio_sm_config *c, bool right, bool autopush,
                            uint thresh);
void sm_config_set_clkdiv_int_frac8(pio_sm_config *c, uint32_t i, uint8_t f);

extern const pio_program_t ew_line_program;
pio_sm_config ew_line_program_get_default_config(uint offset);

#endif /* FAKE_PICO_H */
