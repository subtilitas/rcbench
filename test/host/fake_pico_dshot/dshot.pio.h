/*
 * What protocols/dshot/rp2350/dshot.pio gives out_dshot.c, for
 * test_out_dshot.c: the three programs and their init functions.  The test
 * defines the init functions to do on its pad what the ones in dshot.pio do
 * to the chip's, in the same order.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FAKE_DSHOT_PIO_H
#define FAKE_DSHOT_PIO_H

#include "fake_pico_dshot.h"

#define DSHOT_RX_OVERSAMPLE 5u

extern const pio_program_t dshot_tx_program;
extern const pio_program_t dshot_bidir_tx_program;
extern const pio_program_t dshot_rx_program;

void dshot_tx_program_init(PIO pio, uint sm, uint offset, uint pin,
                           uint32_t bits_per_second);
void dshot_bidir_tx_program_init(PIO pio, uint sm, uint offset, uint pin,
                                 uint32_t bits_per_second);
void dshot_rx_program_init(PIO pio, uint sm, uint offset, uint pin,
                           uint32_t bits_per_second);

#endif /* FAKE_DSHOT_PIO_H */
