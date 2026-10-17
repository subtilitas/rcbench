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
 * addresses) and board_pins.h:
 *
 *   0x14         GT911 touch controller, the address it takes when INT is
 *                high at reset release (BOARD_TOUCH_I2C_ADDR_ALT)
 *   0x20 - 0x27  CH422G command addresses; WR_OC 0x23, WR_SET 0x24 and
 *                RD_IO 0x26 among them
 *   0x30 - 0x3F  CH422G command addresses; WR_IO 0x38 among them
 *   0x5D         GT911 touch controller, its address as the panel resets it
 *                (BOARD_TOUCH_I2C_ADDR)
 *
 * 0x28 to 0x2F are not in Waveshare's list and stay free.  An address above
 * 0x7F is no 7-bit address and is refused with the table.
 *
 * Owns no hardware, so the host tests hold it.  The panel adds every device
 * outside the board's own through panel_i2c_add_device()
 * (firmware/panel/main/panel_i2c_dev.h), which refuses an address here.
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
    ((unsigned)(a) > 0x7Fu || (unsigned)(a) == 0x14u ||                     \
     ((unsigned)(a) >= 0x20u && (unsigned)(a) <= 0x27u) ||                  \
     ((unsigned)(a) >= 0x30u && (unsigned)(a) <= 0x3Fu) ||                  \
     (unsigned)(a) == 0x5Du)

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
 * The chip that owns @p addr: "CH422G" or "GT911", "not a 7-bit address"
 * above 0x7F, NULL for a free address.
 */
const char *panel_i2c_owner(unsigned addr);

#ifdef __cplusplus
}
#endif
