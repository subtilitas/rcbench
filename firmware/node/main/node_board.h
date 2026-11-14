/**
 * @file node_board.h
 * @brief The board the node image is built for (main/Kconfig.projbuild,
 *        "Board"): its pins, its INFO board word, the update kind of its
 *        power role, and whether it drives a display.
 *
 * Two boards:
 *
 * - Waveshare ESP32-C6-Zero (CONFIG_RCNODE_BOARD_C6_ZERO): the power and
 *   the balance node, on a LiPo cell, asleep when idle.
 * - LCDWIKI ES3C28P, the 2.8" ESP32-S3 display board (Hosyond sells it;
 *   CONFIG_RCNODE_BOARD_ES3C28P): the power node with a local display,
 *   on 5V from the pack, never asleep.
 *
 * The ES3C28P's pins come from LCDWIKI's documents: the specification
 * ES3C28P_ES2N28P_Specification_V1.0.pdf, section 4.2 "ESP32-S3 pin
 * allocation"; the user manual
 * 2.8inch_IPS_ESP32-S3_ES3C28P_ES3N28P_User_Manual.pdf, sections on the
 * touch and backlight circuits; the schematic
 * 2.8inch_ESP32-S3_Display_Schematic.pdf; and the vendor's display set-up
 * ILI9341V_Init.txt (all under https://www.lcdwiki.com/res/ES3C28P/).  A
 * value marked "unconfirmed" is not in those documents in a form read for
 * this file.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "sdkconfig.h"

#include "bleota.h"
#include "rcnode.h"

#if CONFIG_RCNODE_BOARD_ES3C28P

/** @brief The board's INFO word. */
#define NODE_BOARD_ID RCNODE_BOARD_ES3C28P
/** @brief The update kind of the power role: images of the C6 power node
 *         are refused (bleota_head_check(), BEGIN's kind). */
#define NODE_KIND_POWER BLEOTA_KIND_NODE_POWER_S3
/** @brief The board drives its display and touch (node_disp.c). */
#define NODE_BOARD_DISPLAY 1
/** @brief The board never sleeps on its own: its 5V comes from the pack. */
#define NODE_BOARD_ALWAYS_ON 1

/** @brief The BOOT key: IO0, the strapping pin the key pulls low
 *         (specification 4.2, "Button"). */
#define NODE_PIN_BOOT 0

/** @brief The I2C (Inter-Integrated Circuit) bus: I2C0 on the 1.25mm 4P
 *         "I2C peripheral interface" socket, shared with the FT6336G touch
 *         controller (0x38) and the ES8311 audio codec (0x18)
 *         (specification 4.2; user manual "I2C Peripheral Interface").  The
 *         board pulls both lines up to 3.3V with 4.7kΩ (R29, R30,
 *         schematic). @{ */
#define NODE_PIN_SDA 16  /**< IO16, IIC_SDA = CTP_SDA */
#define NODE_PIN_SCL 15  /**< IO15, IIC_SCL = CTP_SCL */
/** @} */

/** @brief The battery's ADC (analogue-to-digital converter) pin: IO9, the
 *         cell through a divider of 2 (user manual, "Battery level
 *         detection circuit"; the ratio node_policy.h's divider has). */
#define NODE_PIN_BAT 9

/** @brief The display: ILI9341V over 4-wire SPI (serial peripheral
 *         interface), 240 x 320 IPS (specification 4.2, "LCD").  Its reset
 *         is the chip's own CHIP_PU, so the firmware resets it by command.
 *         @{ */
#define NODE_PIN_LCD_CS   10  /**< IO10, low active */
#define NODE_PIN_LCD_DC   46  /**< IO46, high data, low command */
#define NODE_PIN_LCD_SCLK 12  /**< IO12 */
#define NODE_PIN_LCD_MOSI 11  /**< IO11 */
#define NODE_PIN_LCD_MISO 13  /**< IO13, not read by the firmware */
#define NODE_PIN_LCD_BL   45  /**< IO45, high lights the backlight (Q4) */
/** @} */
/** @brief 1 turns the screen and the touch by 180° (node_lcd.c): which edge
 *         of the board is the top in the vendor's landscape direction 1 is
 *         unconfirmed. */
#define NODE_LCD_TURN 0

/** @brief The FT6336G capacitive touch controller at 0x38 on the bus above
 *         (specification 4.2, "Capacitive touch screen"; user manual
 *         figure 3.8). @{ */
#define NODE_PIN_TP_RST 18  /**< IO18, low resets */
#define NODE_PIN_TP_INT 17  /**< IO17, low on a touch event; not used */
#define NODE_TP_ADDR  0x38u /**< the controller's address */
/** @} */

#elif CONFIG_RCNODE_BOARD_C6_ZERO

/** @brief The board's INFO word. */
#define NODE_BOARD_ID RCNODE_BOARD_C6_ZERO
/** @brief The update kind of the power role. */
#define NODE_KIND_POWER BLEOTA_KIND_NODE_POWER
/** @brief No display. */
#define NODE_BOARD_DISPLAY 0
/** @brief The sleep policy (node_policy.h) runs. */
#define NODE_BOARD_ALWAYS_ON 0

/** @brief The Waveshare ESP32-C6-Zero's BOOT key: GPIO9, the strapping pin
 *         the key pulls low; once the node runs, the confirming key of a
 *         firmware update over Bluetooth LE (firmware/devota). */
#define NODE_PIN_BOOT 9
/** @brief The power role's I2C bus, 2.2kΩ pull-ups to 3.3V on the node.
 *         @{ */
#define NODE_PIN_SDA 19  /**< GPIO19 */
#define NODE_PIN_SCL 18  /**< GPIO18 */
/** @} */
/** @brief The battery divider's pin: GPIO3, ADC1 channel 3. */
#define NODE_PIN_BAT 3

#else
#error "no board: set CONFIG_RCNODE_BOARD_* (main/Kconfig.projbuild)"
#endif
