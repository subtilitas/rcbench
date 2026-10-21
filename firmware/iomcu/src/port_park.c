/**
 * @file
 * @brief A programming port's pin held with no driver.  See port_park.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "port_park.h"

#include "hardware/gpio.h"

bool port_park(uint8_t pin)
{
    if (pin >= NUM_BANK0_GPIOS) {
        return false;
    }
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_down(pin);
    return true;
}

void port_unpark(uint8_t pin)
{
    if (pin >= NUM_BANK0_GPIOS) {
        return;
    }
    gpio_disable_pulls(pin);
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, GPIO_IN);
}
