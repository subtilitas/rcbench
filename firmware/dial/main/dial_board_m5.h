/**
 * @file
 * @brief The M5Stack Dial v1.1: its pins, INFO hardware value and update
 *        kind (dial_board.h).
 *
 * Pins from the M5Stack Dial v1.1's documentation, confirmed against the
 * board support package and M5GFX (RESEARCH-DIAL.md, "Pin map").  The
 * battery's reading pin is named by no source and is not used.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

/** @brief INFO's hardware value. */
#define DIAL_BOARD_HW   DK_HW_DIAL11
/** @brief The update kind: DEVOTA_ROLE() and BEGIN's kind. */
#define DIAL_BOARD_KIND BLEOTA_KIND_DIAL
/** @brief The board's name, for the console. */
#define DIAL_BOARD_NAME "M5Stack Dial v1.1"

/** @brief GPIO numbers. @{ */
#define DIAL_PIN_HOLD      46 /**< power latch: high keeps the Dial on */
#define DIAL_PIN_ENC_A     41 /**< encoder channel A */
#define DIAL_PIN_ENC_B     40 /**< encoder channel B */
#define DIAL_PIN_BTN       42 /**< the screen button, low when pressed */
#define DIAL_PIN_STAMP_BTN 0  /**< the Stamp-S3A's button, low when pressed;
                                   the download mode strap at reset */
#define DIAL_PIN_BUZZER    3  /**< buzzer, LEDC (LED controller) PWM */
#define DIAL_PIN_LCD_DC    4  /**< GC9A01 data/command */
#define DIAL_PIN_LCD_MOSI  5  /**< GC9A01 data */
#define DIAL_PIN_LCD_SCLK  6  /**< GC9A01 clock */
#define DIAL_PIN_LCD_CS    7  /**< GC9A01 chip select */
#define DIAL_PIN_LCD_RST   8  /**< GC9A01 reset, shared with the touch and
                                   RFID parts */
#define DIAL_PIN_LCD_BL    9  /**< backlight, LEDC PWM */
#define DIAL_PIN_I2C_SDA   11 /**< internal I2C: touch, RTC, RFID */
#define DIAL_PIN_I2C_SCL   12 /**< internal I2C clock */
#define DIAL_PIN_TOUCH_INT 14 /**< FT3267 interrupt, low on a touch */
#define DIAL_PIN_MARK      1  /**< PORT.B G1: the test hooks' 1ms mark */
/** @} */
