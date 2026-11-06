/*
 * The part of the pico-sdk protocols/hitec/rp2350/hitec_line.c calls, for
 * test_hitec_line.c: types, constants and declarations.  The test defines the
 * functions and keeps two PIO blocks (the line's and the sampler's), three DMA
 * channels and one pin, so it can see the order in which the driver stops a
 * state machine, lets a channel take the receive FIFO, reads a count and
 * restarts.  It says nothing about the chip.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FAKE_PICO_HITEC_H
#define FAKE_PICO_HITEC_H

#include <stdbool.h>
#include <stdint.h>

typedef unsigned int uint;

#define NUM_BANK0_GPIOS        48u
#define PIO_FDEBUG_RXSTALL_LSB 0u

#define GPIO_IN  false
#define GPIO_OUT true

/* The pin's control register: the function in bits 0 to 4, the input
 * override in bits 16 and 17, as the chip has them. */
#define FAKE_CTRL_FUNC_BITS   0x0000001Fu
#define FAKE_CTRL_INOVER_LSB  16u
#define FAKE_CTRL_INOVER_BITS 0x00030000u
#define FAKE_FUNC_SIO         5u
#define FAKE_FUNC_PIO0        6u

/* The pad register: slew bit 0, pull-down bit 2, pull-up bit 3, drive
 * strength bits 4 and 5, as the chip has them. */
#define FAKE_PAD_SLEW_FAST  0x01u
#define FAKE_PAD_PDE        0x04u
#define FAKE_PAD_PUE        0x08u
#define FAKE_PAD_DRIVE_LSB  4u
#define FAKE_PAD_DRIVE_BITS 0x30u

enum gpio_override {
    GPIO_OVERRIDE_NORMAL = 0,
    GPIO_OVERRIDE_INVERT = 1,
    GPIO_OVERRIDE_LOW    = 2,
    GPIO_OVERRIDE_HIGH   = 3
};
enum gpio_drive_strength {
    GPIO_DRIVE_STRENGTH_2MA, GPIO_DRIVE_STRENGTH_4MA,
    GPIO_DRIVE_STRENGTH_8MA, GPIO_DRIVE_STRENGTH_12MA
};
enum gpio_slew_rate { GPIO_SLEW_RATE_SLOW, GPIO_SLEW_RATE_FAST };

typedef struct {
    struct {
        uint32_t status;
        uint32_t ctrl;
    } io[NUM_BANK0_GPIOS];
} io_bank0_hw_t;
typedef struct {
    uint32_t voltage_select;
    uint32_t io[NUM_BANK0_GPIOS];
} pads_bank0_hw_t;

/* The test's one bank of control registers and one of pad registers. */
extern io_bank0_hw_t fake_io_bank0;
extern pads_bank0_hw_t fake_pads_bank0;
#define io_bank0_hw   (&fake_io_bank0)
#define pads_bank0_hw (&fake_pads_bank0)

typedef struct {
    uint32_t fdebug;
    uint32_t txf[4];
    uint32_t rxf[4];
} pio_hw_t;
typedef pio_hw_t *PIO;

typedef struct {
    const uint16_t *instructions;
    uint8_t length;
    int8_t origin;
} pio_program_t;

typedef struct {
    uint32_t wrap_bottom;
    uint32_t wrap_top;
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
    uint32_t clk_int;
    uint32_t clk_frac;
    uint32_t join;
} pio_sm_config;

typedef struct {
    uint32_t data_size;
    uint32_t read_inc;
    uint32_t write_inc;
    uint32_t dreq;
} dma_channel_config;

typedef struct {
    uint32_t transfer_count;
    uintptr_t write_addr;
} dma_channel_hw_t;

/* The real encodings' destinations: OUT, SET and MOV share them. */
enum pio_src_dest {
    pio_pins = 0, pio_x = 1, pio_y = 2, pio_null = 3, pio_pindirs = 4,
    pio_isr = 6, pio_osr = 7
};
enum pio_fifo_join { PIO_FIFO_JOIN_NONE, PIO_FIFO_JOIN_TX, PIO_FIFO_JOIN_RX };
enum dma_channel_transfer_size { DMA_SIZE_8, DMA_SIZE_16, DMA_SIZE_32 };
enum clock_index { clk_sys };

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
void gpio_set_inover(uint pin, uint value);
void gpio_set_drive_strength(uint pin, enum gpio_drive_strength d);
void gpio_set_slew_rate(uint pin, enum gpio_slew_rate s);

uint pio_encode_set(enum pio_src_dest dest, uint value);
uint pio_encode_out(enum pio_src_dest dest, uint count);
uint pio_encode_mov(enum pio_src_dest dest, enum pio_src_dest src);
uint pio_encode_jmp(uint addr);
void pio_sm_exec(PIO pio, uint sm, uint instr);
void pio_sm_set_enabled(PIO pio, uint sm, bool on);
void pio_sm_clear_fifos(PIO pio, uint sm);
void pio_sm_restart(PIO pio, uint sm);
void pio_sm_init(PIO pio, uint sm, uint pc, const pio_sm_config *c);
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
pio_sm_config pio_get_default_sm_config(void);
void sm_config_set_wrap(pio_sm_config *c, uint bottom, uint top);
void sm_config_set_out_pins(pio_sm_config *c, uint base, uint count);
void sm_config_set_set_pins(pio_sm_config *c, uint base, uint count);
void sm_config_set_in_pin_base(pio_sm_config *c, uint base);
void sm_config_set_jmp_pin(pio_sm_config *c, uint pin);
void sm_config_set_out_shift(pio_sm_config *c, bool right, bool autopull,
                             uint thresh);
void sm_config_set_in_shift(pio_sm_config *c, bool right, bool autopush,
                            uint thresh);
void sm_config_set_clkdiv_int_frac8(pio_sm_config *c, uint32_t i, uint8_t f);
void sm_config_set_fifo_join(pio_sm_config *c, enum pio_fifo_join join);

#endif /* FAKE_PICO_HITEC_H */
