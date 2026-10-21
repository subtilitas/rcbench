/**
 * @file panel_i2c.h
 * @brief The I2C (Inter-Integrated Circuit) addresses taken on the 7-inch
 *        ESP32-P4 display board's buses, which a device added there must
 *        stay clear of.
 *
 * The board has two buses (board_pins.h):
 *
 *   I2C_NUM_0  the touch bus, GPIO7 (SDA) and GPIO8 (SCL): the GT911 touch
 *              controller, and in a build with it the touch injector at
 *              the GT911's second address (testbench/touch-injector)
 *   I2C_NUM_1  the knob's bus, BOARD_KNOB_PIN_SDA and BOARD_KNOB_PIN_SCL
 *              (placeholder pins): the MT6701 alone
 *
 * The table, from board_pins.h and the I2C specification (NXP UM10204,
 * table 4, reserved addresses):
 *
 *   0x00 - 0x03  I2C reserved: general call / START byte (0x00), CBUS
 *                (0x01), a different bus format (0x02), future use (0x03)
 *   0x14         GT911 touch controller, its second address
 *                (BOARD_TOUCH_I2C_ADDR_ALT); the touch injector's
 *   0x5D         GT911 touch controller, its first address
 *                (BOARD_TOUCH_I2C_ADDR)
 *   0x78 - 0x7F  I2C reserved: 10-bit addressing (0x78 - 0x7B) and future
 *                use (0x7C - 0x7F)
 *
 * The GT911's two addresses are kept off both buses, so a device can move
 * between them.  0x04 to 0x07 are the specification's Hs-mode (high-speed
 * mode) master codes and stay open: the MT6701 knob's factory address is
 * 0x06, and no device on these buses runs Hs-mode.  An address above 0x7F
 * is no 7-bit address and is refused with the table.  What answers where on
 * the board is not measured.
 *
 * Owns no hardware, so the host tests hold it.  knob_attach() (knob.h)
 * refuses an address here.
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
     (unsigned)(a) == 0x14u || (unsigned)(a) == 0x5Du ||                    \
     (unsigned)(a) >= 0x78u)

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
 * The owner of @p addr: "GT911" or "I2C reserved", "not a 7-bit
 * address" above 0x7F, NULL for a free address.
 */
const char *panel_i2c_owner(unsigned addr);

#ifdef __cplusplus
}
#endif
