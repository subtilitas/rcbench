/**
 * @file
 * @brief The Guition JC3636K518C knob: its pins, INFO hardware value and
 *        update kind (dial_board.h).
 *
 * An ESP32-S3R8 (8MB octal PSRAM in the package) with 16MB flash, an
 * ST77916 round display of 360 x 360 pixels over QSPI, a CST816S touch
 * controller, a two-line encoder without a push switch, a DRV2605 haptic
 * driver with an LRA (linear resonant actuator), and a LiPo cell behind a
 * slide switch.  Pins from Guition's Arduino demo
 * (esp_panel_board_custom_conf.h of demo_arduino/JC3636K518) and from
 * community firmware for this board; no schematic of Guition's was read for
 * this file.  The pins are named here for the slices that drive them; this
 * build drives none of them yet (dial_k518.c).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

/** @brief INFO's hardware value. */
#define DIAL_BOARD_HW   DK_HW_K518
/** @brief The update kind: DEVOTA_ROLE() and BEGIN's kind. */
#define DIAL_BOARD_KIND BLEOTA_KIND_DIAL_K518
/** @brief The board's name, for the console. */
#define DIAL_BOARD_NAME "Guition JC3636K518C"

/** @brief GPIO numbers. @{ */
#define DIAL_PIN_BOOT      0  /**< BOOT, behind the pinhole beside USB-C;
                                   the download mode strap at reset */
#define DIAL_PIN_BATT      1  /**< the cell through a 10k/10k divider,
                                   ADC1 channel 0 */
#define DIAL_PIN_ENC_CCW   7  /**< encoder: one low pulse per
                                   anticlockwise detent */
#define DIAL_PIN_ENC_CW    8  /**< encoder: one low pulse per clockwise
                                   detent */
#define DIAL_PIN_TOUCH_INT 9  /**< CST816S interrupt */
#define DIAL_PIN_TOUCH_RST 10 /**< CST816S reset, low active */
#define DIAL_PIN_I2C_SDA   11 /**< I2C: touch (0x15) and DRV2605 (0x5A) */
#define DIAL_PIN_I2C_SCL   12 /**< I2C clock */
#define DIAL_PIN_LCD_SCLK  13 /**< ST77916 QSPI clock */
#define DIAL_PIN_LCD_CS    14 /**< ST77916 chip select */
#define DIAL_PIN_LCD_D0    15 /**< ST77916 QSPI data 0 */
#define DIAL_PIN_LCD_D1    16 /**< ST77916 QSPI data 1 */
#define DIAL_PIN_LCD_D2    17 /**< ST77916 QSPI data 2 */
#define DIAL_PIN_LCD_D3    18 /**< ST77916 QSPI data 3 */
#define DIAL_PIN_LCD_RST   21 /**< ST77916 reset, low active */
#define DIAL_PIN_HAPTIC_EN 38 /**< DRV2605 enable on V1.0 boards, high on;
                                   V2.0 ties it to 3.3V and uses GPIO38
                                   for the UART to the ESP32-U4WDH */
#define DIAL_PIN_LCD_BL    47 /**< backlight, LEDC PWM, high on */
/** @} */

/** @brief The display's size in pixels. */
#define DIAL_K518_LCD_SIZE 360
