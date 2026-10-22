/**
 * @file board_pins.h
 * @brief Pin map for the 7-inch ESP32-P4 display board from Waveshare
 *        (ESP32-P4-WIFI6-Touch-LCD-7B).
 *
 * The board carries a 1024 x 600 IPS (in-plane switching) LCD
 * (liquid-crystal display) on a 2-lane MIPI DSI (Display Serial Interface)
 * link with an EK79007 controller, a GT911 touch controller and an
 * ESP32-C6 for the radio.
 *
 * Values are from the board schematic of 2025-09-25 and the EK79007
 * datasheet.  "Not measured" marks a value no board has confirmed.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ board */

/** @brief The board and the display as the splash names them. */
#define BOARD_NAME_TEXT             "ESP32-P4 7B"
#define BOARD_DISPLAY_TEXT          "1024x600 DSI"

/* ------------------------------------------------------------------ panel */

/** @brief The panel's size in pixels. */
#define BOARD_LCD_H_RES             1024
#define BOARD_LCD_V_RES             600

/**
 * @brief The area the UI (user interface) draws: the whole panel, 1024 x
 * 600.  Touches outside it are dropped (touch.c).
 */
#define BOARD_UI_H_RES              1024
/** @brief The UI area's height: the panel's 600 rows. */
#define BOARD_UI_V_RES              600

/**
 * @brief DPI (Display Pixel Interface) timing, in pixels and lines.  52 MHz is
 * requested; the 240 MHz DPI clock divided by 5 delivers 48 MHz, which
 * gives 60.38 Hz over 1250 x 636 (derived, not measured).  The EK79007
 * accepts 44.9 to 70.3 MHz at 60 Hz.
 */
#define BOARD_LCD_DPI_CLOCK_MHZ     52.0f
#define BOARD_LCD_HSYNC_PULSE_WIDTH 10
#define BOARD_LCD_HSYNC_BACK_PORCH  160
#define BOARD_LCD_HSYNC_FRONT_PORCH 160
#define BOARD_LCD_VSYNC_PULSE_WIDTH 1
#define BOARD_LCD_VSYNC_BACK_PORCH  23
#define BOARD_LCD_VSYNC_FRONT_PORCH 12

/**
 * @brief The DSI link: 2 data lanes.  The lane rate is one define; 1000, 900 and
 * 500 Mbit/s per lane are the three to try on a board.  RGB565 at 60 Hz
 * carries 297 Mbit/s of pixels a lane.  The EK79007 datasheet states
 * 500 Mbit/s as its maximum; Waveshare's examples run 900 and 1000.
 */
#define BOARD_DSI_LANES             2
#define BOARD_DSI_LANE_MBPS         1000

/**
 * @brief The DSI PHY (physical layer) supply: on-chip LDO (low-dropout
 *        regulator) channel 3 at 2500 mV, on before the bus starts.
 */
#define BOARD_DSI_PHY_LDO_CHAN      3
#define BOARD_DSI_PHY_LDO_MV        2500

/**
 * @brief LCD reset, active low; a 10 kOhm pull-down holds the panel in
 *        reset until firmware drives the pin.
 */
#define BOARD_LCD_PIN_RST           GPIO_NUM_33

/**
 * @brief The EK79007's address mode (DCS command 0x36): bit 1 the gate scan
 * direction, bit 0 the source shift direction, 0x01 after reset.  0x02
 * reverses both, which turns the picture by 180 degrees.  Waveshare's own
 * start-up turns its picture by 180 degrees in software, so the panel's
 * reset scan is taken to show the picture upside down; not measured.
 */
#define BOARD_LCD_ADDRESS_MODE      0x02

/* ------------------------------------------------------------- backlight */

/**
 * @brief The backlight's PWM (pulse-width modulation) on GPIO32.
 *
 * GPIO32 feeds the backlight boost converter's feedback node through
 * 10 kOhm: a higher level dims.  LEDC (the LED pulse-width modulation
 * controller) drives it inverted, so a duty of 100 % is a constant low and
 * full brightness.  Whether a constant high is fully dark is not measured.
 */
#define BOARD_BL_PIN                GPIO_NUM_32
#define BOARD_BL_LEDC_TIMER         1
#define BOARD_BL_LEDC_CHANNEL       1
#define BOARD_BL_PWM_HZ             5000
#define BOARD_BL_DUTY_BITS          10

/* -------------------------------------------------------------------- I2C */

/**
 * @brief The touch bus, I2C (Inter-Integrated Circuit): GPIO7 and GPIO8
 *        reach the touch connector J3.
 */
#define BOARD_I2C_PORT              0
#define BOARD_I2C_PIN_SDA           GPIO_NUM_7
#define BOARD_I2C_PIN_SCL           GPIO_NUM_8
#define BOARD_I2C_FREQ_HZ           400000

/* ------------------------------------------------------------------ touch */

/**
 * @brief The GT911 touch controller's addresses and reset line.
 *
 * GT911 at 0x5D, 0x14 its second address.  Its INT line reaches only J3
 * and a test point, so the touch task polls.  The schematic routes GPIO23
 * to the GT911's reset through a 0 Ohm resistor; GPIO_NUM_NC here runs no
 * reset sequence.
 */
#define BOARD_TOUCH_I2C_ADDR        0x5D
#define BOARD_TOUCH_I2C_ADDR_ALT    0x14
#define BOARD_TOUCH_PIN_RST         GPIO_NUM_23

/**
 * @brief Touch mirroring, 1 to mirror an axis.
 *
 * The GT911 reports 1024 x 600 in its own orientation.  These mirror it
 * into the panel's, applied before rotation; both set match
 * BOARD_LCD_ADDRESS_MODE 0x02.  Not measured.
 */
#define BOARD_TOUCH_MIRROR_X        1
#define BOARD_TOUCH_MIRROR_Y        1

/* -------------------------------------------------------------- SD card --- */

/**
 * @brief The SD card: SDMMC (SD card host) slot 0, 4-bit, on its fixed IO MUX pins: D0 to D3
 * GPIO39 to GPIO42, CLK GPIO43, CMD GPIO44.  The card's supply is
 * on-chip LDO channel 4.
 */
#define BOARD_SD_SLOT               0
#define BOARD_SD_WIDTH              4
#define BOARD_SD_PIN_CLK            GPIO_NUM_43
#define BOARD_SD_PIN_CMD            GPIO_NUM_44
#define BOARD_SD_PIN_D0             GPIO_NUM_39
#define BOARD_SD_PIN_D1             GPIO_NUM_40
#define BOARD_SD_PIN_D2             GPIO_NUM_41
#define BOARD_SD_PIN_D3             GPIO_NUM_42
#define BOARD_SD_LDO_CHAN           4
#define BOARD_SD_FREQ_KHZ           20000

/* ------------------------------------------------------------------ radio */

/*
 * The ESP32-C6-MINI-1 that carries Wi-Fi, an SDIO (Secure Digital
 * input/output) device on SDMMC slot 1, 4-bit, through the GPIO matrix.
 * C6 IO19 is CLK, IO18 CMD, IO20 to IO23 D0 to D3; each line has a 51 kOhm
 * pull-up.  The C6's EN (chip enable) has a 10 kOhm pull-up and 1 uF and
 * reaches GPIO54 through 0 Ohm: a low holds the C6 in reset.  esp_hosted
 * reads these from sdkconfig.defaults; wifi_radio.c fails the build when
 * the two differ.  Header H4 carries the C6's UART0 (TXD0, RXD0), GND
 * and its strapping pin IO9.
 */
#define BOARD_C6_SLOT               1
#define BOARD_C6_WIDTH              4
#define BOARD_C6_FREQ_KHZ           40000
#define BOARD_C6_PIN_CLK            GPIO_NUM_18
#define BOARD_C6_PIN_CMD            GPIO_NUM_19
#define BOARD_C6_PIN_D0             GPIO_NUM_14
#define BOARD_C6_PIN_D1             GPIO_NUM_15
#define BOARD_C6_PIN_D2             GPIO_NUM_16
#define BOARD_C6_PIN_D3             GPIO_NUM_17
#define BOARD_C6_PIN_EN             GPIO_NUM_54

/* -------------------------------------------------------------------- CAN */
/**
 * @brief The link's CAN (Controller Area Network) pins.
 *
 * TWAI (Two-Wire Automotive Interface, the ESP32-P4's CAN controller) to
 * the TJA1051T/3 transceiver on header H11.  Pin 1 of H11 is the board's
 * 5 V rail, so the link cable can power the board.
 */
#define PANEL_CAN_PIN_TX   GPIO_NUM_22
#define PANEL_CAN_PIN_RX   GPIO_NUM_21

/**
 * @brief The link's bit rate.  Both ends must agree.  1 Mbit/s is what
 *        the IOmcu's 16 MHz crystal reaches exactly; see IOMCU_CAN_BITRATE
 *        and docs/Link.md.
 */
#define PANEL_CAN_BITRATE  1000000u

/* ----------------------------------------------------------------- safety */
/**
 * @brief The heartbeat, on a free header GPIO: one of IO2-5, IO28-31, IO34, IO36
 * and IO46-52.  IO34 and IO36 are strapping pins.
 */
#define PANEL_HEARTBEAT_PIN   GPIO_NUM_28   /* placeholder: pending the owner's pick */

/* ------------------------------------------------------------------- knob */
/**
 * @brief The rotary knob's sensor, on an I2C controller of its own: nothing of
 * the board shares its bus.
 */
#define BOARD_KNOB_I2C_PORT     1
#define BOARD_KNOB_PIN_SDA      GPIO_NUM_30   /* placeholder: pending the owner's pick */
#define BOARD_KNOB_PIN_SCL      GPIO_NUM_31   /* placeholder: pending the owner's pick */
#define BOARD_KNOB_I2C_FREQ_HZ  400000

#ifdef __cplusplus
}
#endif
