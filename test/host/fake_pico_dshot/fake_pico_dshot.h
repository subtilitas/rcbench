/*
 * The part of the pico-sdk protocols/dshot/rp2350/out_dshot.c calls, for
 * test_out_dshot.c: types, constants and declarations.  The test defines the
 * functions and keeps one pad and one PIO block, so it can see the level the
 * driver leaves on the pin at each step.  It says nothing about the chip.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FAKE_PICO_DSHOT_H
#define FAKE_PICO_DSHOT_H

#include <stdbool.h>
#include <stdint.h>

typedef unsigned int uint;

#define NUM_PIO_STATE_MACHINES 4u
#define NUM_BANK0_GPIOS        48u

#define GPIO_IN  false
#define GPIO_OUT true

#define IO_BANK0_GPIO0_CTRL_FUNCSEL_BITS 0x0000001Fu
#define IO_BANK0_GPIO0_CTRL_OUTOVER_LSB  12u
#define IO_BANK0_GPIO0_CTRL_OUTOVER_BITS 0x00003000u

typedef enum {
    GPIO_FUNC_SIO  = 5,
    GPIO_FUNC_PIO0 = 6,
    GPIO_FUNC_NULL = 0x1F
} gpio_function_t;

enum gpio_override {
    GPIO_OVERRIDE_NORMAL = 0,
    GPIO_OVERRIDE_INVERT = 1,
    GPIO_OVERRIDE_LOW    = 2,
    GPIO_OVERRIDE_HIGH   = 3
};

typedef struct {
    struct {
        uint32_t status;
        uint32_t ctrl;
    } io[NUM_BANK0_GPIOS];
} io_bank0_hw_t;

/* The test's one bank of pad control registers. */
extern io_bank0_hw_t fake_io_bank0;
#define io_bank0_hw (&fake_io_bank0)

typedef struct {
    int block;
} pio_hw_t;
typedef pio_hw_t *PIO;

typedef struct {
    int which;
} pio_program_t;

void gpio_set_function(uint gpio, gpio_function_t fn);
void gpio_set_outover(uint gpio, uint value);
void gpio_disable_pulls(uint gpio);
void gpio_pull_up(uint gpio);
void gpio_set_dir(uint gpio, bool out);
void gpio_put(uint gpio, bool value);

bool pio_sm_is_claimed(PIO pio, uint sm);
bool pio_can_add_program(PIO pio, const pio_program_t *program);
void pio_sm_claim(PIO pio, uint sm);
void pio_sm_unclaim(PIO pio, uint sm);
int  pio_add_program(PIO pio, const pio_program_t *program);
void pio_remove_program(PIO pio, const pio_program_t *program, uint offset);
bool pio_claim_free_sm_and_add_program_for_gpio_range(
    const pio_program_t *program, PIO *pio, uint *sm, uint *offset,
    uint gpio_base, uint gpio_count, bool set_gpio_base);
void pio_gpio_init(PIO pio, uint pin);
void pio_sm_set_enabled(PIO pio, uint sm, bool enabled);
void pio_sm_clear_fifos(PIO pio, uint sm);
int  pio_sm_set_consecutive_pindirs(PIO pio, uint sm, uint pin, uint count,
                                    bool is_out);
void pio_sm_set_pins_with_mask64(PIO pio, uint sm, uint64_t values,
                                 uint64_t mask);
void pio_sm_restart(PIO pio, uint sm);
void pio_sm_clkdiv_restart(PIO pio, uint sm);
void pio_sm_exec(PIO pio, uint sm, uint instr);
uint pio_encode_jmp(uint addr);
bool pio_sm_is_tx_fifo_full(PIO pio, uint sm);
void pio_sm_put(PIO pio, uint sm, uint32_t data);
void pio_interrupt_clear(PIO pio, uint irq);
uint pio_sm_get_rx_fifo_level(PIO pio, uint sm);
bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm);
uint32_t pio_sm_get(PIO pio, uint sm);

#endif /* FAKE_PICO_DSHOT_H */
