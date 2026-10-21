/**
 * @file
 * @brief A programming port's pin held with no driver (FUNC NONE): an input
 * with its pull-down, so a lead on it reads low rather than floating.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_PORT_PARK_H
#define RCBENCH_PORT_PARK_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief @p pin as an input with its pull-down.
 *
 * False for a pin the chip does not have.
 *
 * @param pin  GPIO number of the pin to hold.
 * @return True when the pin was held. False when the chip does not have the
 *         pin, and nothing was changed.
 */
bool port_park(uint8_t pin);

/**
 * @brief @p pin let go: an input with no pull, as out_pwm_release() leaves a
 * pin, for whoever takes it next.
 *
 * @param pin  GPIO number of the pin to release. A pin the chip does not
 *             have is ignored.
 */
void port_unpark(uint8_t pin);

#endif /* RCBENCH_PORT_PARK_H */
