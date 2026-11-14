/**
 * @file
 * @brief The Dial's power latch and its supply reading.
 *
 * The WAKE button powers the Dial while it is held; GPIO46 high keeps the
 * power on after it is let go (M5Stack's documentation for battery-only
 * operation).  GPIO46 is a strapping pin of the ESP32-S3, so it is driven
 * from app_main(), after the chip has read it at reset, and never earlier.
 *
 * The cell's voltage: no source read for this device (the Dial's pages,
 * its schematic text, M5Unified, the board support package) names an ADC
 * (analog-to-digital converter) pin that reaches the cell, and the charger
 * (TP4057 in the schematic text) has no status pin named either.  Every
 * reading is therefore unknown, and STATUS carries 0 with its "battery
 * unknown" flag.  This is the one function to change once the pin is found.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dial.h"

#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"

/** @brief A test build's injected reading, mV; 0 none.  Written by the
 *         console task, read by the face task: one aligned 16-bit store. */
static volatile uint16_t s_inject_mv;

void dial_power_hold(void)
{
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << DIAL_PIN_HOLD,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_set_level(DIAL_PIN_HOLD, 1);
    gpio_config(&io);
    gpio_set_level(DIAL_PIN_HOLD, 1);
}

void dial_supply_read(dial_supply_t *out)
{
    const uint16_t mv = s_inject_mv;
    out->mv = mv;
    out->known = mv != 0u;
    out->charging = false;
    out->usb = usb_serial_jtag_is_connected();
}

void dial_supply_inject(uint16_t mv)
{
    s_inject_mv = mv;
}
