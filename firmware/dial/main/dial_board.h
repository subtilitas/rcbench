/**
 * @file
 * @brief The knob the Dial image is built for (main/Kconfig.projbuild,
 *        "Board"): its pins, its parts, its update kind and its INFO
 *        hardware byte.
 *
 * Two boards:
 *
 * - M5Stack Dial v1.1 (CONFIG_RCDIAL_BOARD_M5DIAL, the default): pins from
 *   the Dial's documentation, confirmed against the board support package
 *   and M5GFX (RESEARCH-DIAL.md, "Pin map").
 * - ELECROW CrowPanel 1.28in HMI ESP32 Rotary Display, hardware V1.0
 *   (CONFIG_RCDIAL_BOARD_CROW128, sdkconfig.crow128): ESP32-S3R8, 16MB
 *   flash, 8MB octal PSRAM (not used), GC9A01 240 x 240, CST816D touch, an
 *   EC3501 encoder with its press switch, 5 WS2812 LEDs (not used), no
 *   buzzer, no battery, no power latch, 5V from USB-C.  Pins from
 *   ELECROW's repository
 *   Elecrow-RD/CrowPanel-1.28inch-HMI-ESP32-Rotary-Display-240-240-IPS-Round-Touch-Knob-Screen:
 *   example/Arduino/RotaryScreen_1_28/RotaryScreen_1_28.ino (pins, the
 *   LovyanGFX panel set-up, the encoder's direction), CST816D.cpp (address,
 *   reset), example/esphome/Lesson05/128-touch.yaml (the same pins, the
 *   touch transform), Datasheet/EC3501 C15H30P3 (pulses per detent).
 *   Nothing on this board has run this firmware: every value is from those
 *   files, and those marked "unconfirmed" disagree between them or are not
 *   in them.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "sdkconfig.h"

#include "bleota.h"
#include "dialknob.h"

/** @brief No such pin on this board. */
#define DIAL_PIN_NONE (-1)

#if CONFIG_RCDIAL_BOARD_CROW128

/** @brief The board's name in the log. */
#define DIAL_BOARD_NAME "ELECROW CrowPanel 1.28in"
/** @brief The update kind: images of the M5Stack Dial are refused
 *         (bleota_head_check(), BEGIN's kind). */
#define DIAL_BOARD_KIND BLEOTA_KIND_DIAL_CROW
/** @brief INFO's hardware byte. */
#define DIAL_BOARD_HW DK_HW_CROW128

/** @brief GPIO numbers. @{ */
#define DIAL_PIN_HOLD      DIAL_PIN_NONE /**< no power latch */
#define DIAL_PIN_ENC_A     45 /**< encoder channel A (ENCODER_A_PIN) */
#define DIAL_PIN_ENC_B     42 /**< encoder channel B (ENCODER_B_PIN) */
#define DIAL_PIN_BTN       41 /**< the knob's press switch, low when
                                   pressed (SWITCH_PIN, INPUT_PULLUP) */
#define DIAL_PIN_STAMP_BTN 0  /**< BOOT, low when pressed; the download
                                   mode strap at reset */
#define DIAL_PIN_BUZZER    DIAL_PIN_NONE /**< no buzzer */
#define DIAL_PIN_LCD_DC    3  /**< GC9A01 data/command */
#define DIAL_PIN_LCD_MOSI  11 /**< GC9A01 data */
#define DIAL_PIN_LCD_SCLK  10 /**< GC9A01 clock */
#define DIAL_PIN_LCD_CS    9  /**< GC9A01 chip select */
#define DIAL_PIN_LCD_RST   14 /**< GC9A01 reset, the display's own */
#define DIAL_PIN_LCD_BL    46 /**< backlight, LEDC PWM, high lit; a
                                   strapping pin, driven after reset */
#define DIAL_PIN_I2C_SDA   6  /**< the touch bus (TP_I2C_SDA_PIN) */
#define DIAL_PIN_I2C_SCL   7  /**< the touch bus clock (TP_I2C_SCL_PIN) */
#define DIAL_PIN_TOUCH_INT 5  /**< CST816D interrupt (TP_INT): pulsed at
                                   the controller's start, then not used */
#define DIAL_PIN_TOUCH_RST 13 /**< CST816D reset (TP_RST), low active */
#define DIAL_PIN_RAIL_A    1  /**< held high: "These two rails must remain
                                   enabled while the display is
                                   operating" (the vendor's set-up) */
#define DIAL_PIN_RAIL_B    2  /**< the second of the two */
#define DIAL_PIN_POWER_LED 40 /**< the power indicator; the vendor drives
                                   it low at start, its sense is
                                   unconfirmed */
#define DIAL_PIN_MARK      DIAL_PIN_NONE /**< the test hooks' 1ms mark: no
                                   free pin chosen; the hooks build runs
                                   without it */
/** @} */

/** @brief The display's SPI clock, Hz: the M5Stack Dial's 40MHz.  The
 *         vendor's set-up writes at 80MHz; 40MHz is not tried higher. */
#define DIAL_LCD_PCLK_HZ (40u * 1000u * 1000u)
/** @brief Mirror the display's X axis.  The vendor's LovyanGFX set-up
 *         writes MADCTL 0x08 at rotation 0: BGR, no mirror. */
#define DIAL_LCD_MIRROR_X 0

/** @brief The touch controller: CST816D at 0x15.  Its registers from 0x02
 *         read as the FT3267's do: the point count, then X high (low 4
 *         bits), X low, Y high, Y low.  No transform (the vendor's ESPHome
 *         set-up: swap_xy, mirror_x and mirror_y false). */
#define DIAL_TOUCH_ADDR    0x15u
/** @brief 1: the controller is a CST816D, with its own reset and its auto
 *         sleep switched off at start (register 0xFE = 0xFF). */
#define DIAL_TOUCH_CST816D 1

/** @brief Quadrature counts a detent: "Output signal is 1 pulse per 2
 *         detents" (EC3501 datasheet, 4-1), 2 of x4's counts.  The vendor's
 *         example counts 4 a detent instead; which is right on this board
 *         is unconfirmed. */
#define DIAL_ENC_COUNTS 2
/** @brief +1 or -1: which way of the encoder counts up.  "A rising while B
 *         is low is clockwise" (the vendor's example); the PCNT channels
 *         (dial_input.c) count that edge up, and clockwise positive is the
 *         protocol's rule. */
#define DIAL_ENC_SIGN (+1)

#else /* CONFIG_RCDIAL_BOARD_M5DIAL */

/** @brief The board's name in the log. */
#define DIAL_BOARD_NAME "M5Stack Dial v1.1"
/** @brief The update kind. */
#define DIAL_BOARD_KIND BLEOTA_KIND_DIAL
/** @brief INFO's hardware byte. */
#define DIAL_BOARD_HW DK_HW_DIAL11

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
#define DIAL_PIN_TOUCH_RST DIAL_PIN_NONE /**< the display's reset (GPIO8) */
#define DIAL_PIN_RAIL_A    DIAL_PIN_NONE /**< no rail to switch */
#define DIAL_PIN_RAIL_B    DIAL_PIN_NONE /**< no rail to switch */
#define DIAL_PIN_POWER_LED DIAL_PIN_NONE /**< no power indicator */
#define DIAL_PIN_MARK      1  /**< PORT.B G1: the test hooks' 1ms mark */
/** @} */

/** @brief The display's SPI clock, Hz. */
#define DIAL_LCD_PCLK_HZ (40u * 1000u * 1000u)
/** @brief Mirror the display's X axis, as the board support package does. */
#define DIAL_LCD_MIRROR_X 1

/** @brief The touch controller: FT3267 at 0x38. */
#define DIAL_TOUCH_ADDR    0x38u
/** @brief 0: an FT3267, reset with the display. */
#define DIAL_TOUCH_CST816D 0

/** @brief Quadrature counts a detent. */
#define DIAL_ENC_COUNTS 4
/** @brief +1 or -1: which way of the encoder counts up.  The count falls
 *         clockwise on the Dial v1.1, and clockwise positive is the
 *         protocol's rule. */
#define DIAL_ENC_SIGN (-1)

#endif

_Static_assert(DIAL_ENC_COUNTS == 2 || DIAL_ENC_COUNTS == 4,
               "x4 counts a detent: 2 or 4, scaled to DK_COUNTS_DETENT");
/** @brief The factor that takes the encoder's counts to the protocol's
 *         DK_COUNTS_DETENT a detent (dk_detent_follow()). */
#define DIAL_ENC_SCALE ((int32_t)DK_COUNTS_DETENT / DIAL_ENC_COUNTS)
/** @brief 1 when the board has a buzzer. */
#define DIAL_HAS_BUZZER (DIAL_PIN_BUZZER != DIAL_PIN_NONE)
/** @brief 1 when the board has a power latch. */
#define DIAL_HAS_LATCH (DIAL_PIN_HOLD != DIAL_PIN_NONE)
