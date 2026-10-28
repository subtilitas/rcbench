/*
 * The pico-sdk calls firmware/iomcu/src/sense_i2c.c makes, for
 * test_sense_i2c: the 64-bit microsecond timer, which the suite sets, and
 * the busy-wait hint, which moves it 1us -- or once by fake_park_us, core
 * 1 parked by the flash lock-out.  The I2C block driver
 * (i2c_txq_block.h) is the suite's own, over the modelled bus of
 * i2c_txq_model.c.  Nothing else of the SDK (software development kit) is
 * stood in for here.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>

#define NUM_BANK0_GPIOS 48u

extern uint64_t fake_us;
extern uint32_t fake_park_us;

static inline uint64_t time_us_64(void) { return fake_us; }
static inline void tight_loop_contents(void)
{
    fake_us += (fake_park_us != 0u) ? fake_park_us : 1u;
    fake_park_us = 0u;
}
