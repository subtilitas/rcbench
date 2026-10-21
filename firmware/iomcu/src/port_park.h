/**
 * @file
 * @brief A programming port's pin held with no driver (FUNC NONE).
 *
 * The pin is an input with its pull-down, so a lead on it reads low rather
 * than floating.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_PORT_PARK_H
#define RCBENCH_PORT_PARK_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Holds @p pin as an input with its pull-down.
 *
 * @param pin  GPIO number of the pin to hold.
 * @return True when the pin was held. False when the chip does not have the
 *         pin, and nothing was changed.
 */
bool port_park(uint8_t pin);

/**
 * @brief Lets @p pin go: an input with no pull.
 *
 * This is the state out_pwm_release() leaves a pin in, for whoever takes it
 * next.
 *
 * @param pin  GPIO number of the pin to release.
 */
void port_unpark(uint8_t pin);

#endif /* RCBENCH_PORT_PARK_H */
