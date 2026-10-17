/*
 * The I2C (Inter-Integrated Circuit) addresses the panel board answers on
 * its own bus, board_i2c_bus(): GPIO8 (SDA) and GPIO9 (SCL) on the Waveshare
 * ESP32-S3-Touch-LCD-7, the bus the panel's 4-pin I2C terminal carries.
 *
 * A device added to that bus has to sit outside this table.  The CH422G I/O
 * expander takes the address byte as its command, so a transaction to one of
 * its addresses rewrites the expander instead of reaching the device: a
 * write to 0x30 to 0x3F becomes the output byte IO7..IO0, which holds the
 * backlight enable (EXIO2), the touch reset (EXIO1) and the USB/CAN
 * (Universal Serial Bus / Controller Area Network) select (EXIO5).
 *
 * The table, from Waveshare's wiki for the board
 * (https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7, "occupied" slave
 * addresses), board_pins.h, and the I2C specification (NXP UM10204,
 * table 4, reserved addresses):
 *
 *   0x00 - 0x03  I2C reserved: general call / START byte (0x00), CBUS
 *                (0x01), a different bus format (0x02), future use (0x03)
 *   0x14         GT911 touch controller, the address it takes when INT is
 *                high at reset release (BOARD_TOUCH_I2C_ADDR_ALT)
 *   0x20 - 0x27  CH422G command addresses; WR_OC 0x23, WR_SET 0x24 and
 *                RD_IO 0x26 among them
 *   0x30 - 0x3F  CH422G command addresses; WR_IO 0x38 among them
 *   0x5D         GT911 touch controller, its address as the panel resets it
 *                (BOARD_TOUCH_I2C_ADDR)
 *   0x78 - 0x7F  I2C reserved: 10-bit addressing (0x78 - 0x7B) and future
 *                use (0x7C - 0x7F)
 *
 * 0x04 to 0x07 are the specification's Hs-mode (high-speed mode) master
 * codes and stay open: the MT6701 knob's factory address is 0x06, and no
 * device on this bus runs Hs-mode.  0x28 to 0x2F are not in Waveshare's list
 * and stay free.  An address above 0x7F is no 7-bit address and is refused
 * with the table.
 *
 * What answers where on the panel: one bus scan of 0x08 to 0x77 with nothing
 * on the terminal found 0x20 - 0x27, 0x30 - 0x3F and 0x5D, 25 addresses.
 * 0x00 to 0x07 were not scanned, so that nothing on the board answers at
 * 0x06 is the CH422G's and the GT911's documented addressing, not a
 * measurement.
 *
 * Owns no hardware, so the host tests hold it.  The panel adds every device
 * outside the board's own through panel_i2c_add_device(), and probes one
 * through panel_i2c_probe() (firmware/panel/main/panel_i2c_dev.h); both
 * refuse an address here.
 * The test-bench build's touch injector is not such a device: it speaks the
 * GT911's protocol at the GT911's second address, 0x14, through the touch
 * component (testbench/touch-injector).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The table as a constant expression, for a _Static_assert on an address
 * fixed at build time.  panel_i2c_addr_reserved() gives the same answer for
 * every address; test_panel_i2c holds the two to each other.
 */
#define PANEL_I2C_RESERVED(a)                                               \
    ((unsigned)(a) > 0x7Fu || (unsigned)(a) <= 0x03u ||                     \
     (unsigned)(a) == 0x14u ||                                              \
     ((unsigned)(a) >= 0x20u && (unsigned)(a) <= 0x27u) ||                  \
     ((unsigned)(a) >= 0x30u && (unsigned)(a) <= 0x3Fu) ||                  \
     (unsigned)(a) == 0x5Du || (unsigned)(a) >= 0x78u)

typedef struct {
    uint8_t     first;  /**< lowest 7-bit address of the range */
    uint8_t     last;   /**< highest, inclusive */
    const char *owner;  /**< the chip that answers there */
} panel_i2c_range_t;

/** The ranges, in rising address order. */
extern const panel_i2c_range_t panel_i2c_reserved_table[];
extern const unsigned panel_i2c_reserved_count;

/** Whether @p addr belongs to the board, or is no 7-bit address at all. */
bool panel_i2c_addr_reserved(unsigned addr);

/**
 * The owner of @p addr: "CH422G", "GT911" or "I2C reserved", "not a 7-bit
 * address" above 0x7F, NULL for a free address.
 */
const char *panel_i2c_owner(unsigned addr);

#ifdef __cplusplus
}
#endif
