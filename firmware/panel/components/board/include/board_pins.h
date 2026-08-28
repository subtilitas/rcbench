/*
 * Pin map for the Waveshare ESP32-S3-Touch-LCD-7 (800x480 RGB IPS + GT911).
 *
 * Values are taken from the board schematic (hardware/ESP32-S3-Touch-LCD-7-Sch.pdf
 * in waveshareteam/ESP32-S3-Touch-LCD-7) and cross-checked against Waveshare's
 * own ESP-IDF demo.  The RGB bus is 16 data lines driven as RGB565: the panel
 * is an 18-bit part, but R0..R2/G0..G1/B0..B2 are tied off on the board.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ panel */

#define BOARD_LCD_H_RES             800
#define BOARD_LCD_V_RES             480

/* Panel timing.  16 MHz is Waveshare's shipped value and leaves comfortable
 * PSRAM headroom; ~60 Hz would need ~25 MHz, which this board cannot feed
 * reliably from PSRAM.  At 16 MHz the refresh rate is 39 Hz. */
#define BOARD_LCD_PCLK_HZ           (16 * 1000 * 1000)
#define BOARD_LCD_HSYNC_PULSE_WIDTH 4
#define BOARD_LCD_HSYNC_BACK_PORCH  8
#define BOARD_LCD_HSYNC_FRONT_PORCH 8
#define BOARD_LCD_VSYNC_PULSE_WIDTH 4
#define BOARD_LCD_VSYNC_BACK_PORCH  8
#define BOARD_LCD_VSYNC_FRONT_PORCH 8
#define BOARD_LCD_PCLK_ACTIVE_NEG   1

#define BOARD_LCD_DATA_WIDTH        16
#define BOARD_LCD_BITS_PER_PIXEL    16

#define BOARD_LCD_PIN_PCLK          GPIO_NUM_7
#define BOARD_LCD_PIN_HSYNC         GPIO_NUM_46
#define BOARD_LCD_PIN_VSYNC         GPIO_NUM_3
#define BOARD_LCD_PIN_DE            GPIO_NUM_5
#define BOARD_LCD_PIN_DISP          (-1)   /* driven through the CH422G */

/* data[0..4]   = B3..B7
 * data[5..10]  = G2..G7
 * data[11..15] = R3..R7 */
#define BOARD_LCD_PIN_DATA0         GPIO_NUM_14
#define BOARD_LCD_PIN_DATA1         GPIO_NUM_38
#define BOARD_LCD_PIN_DATA2         GPIO_NUM_18
#define BOARD_LCD_PIN_DATA3         GPIO_NUM_17
#define BOARD_LCD_PIN_DATA4         GPIO_NUM_10
#define BOARD_LCD_PIN_DATA5         GPIO_NUM_39
#define BOARD_LCD_PIN_DATA6         GPIO_NUM_0
#define BOARD_LCD_PIN_DATA7         GPIO_NUM_45
#define BOARD_LCD_PIN_DATA8         GPIO_NUM_48
#define BOARD_LCD_PIN_DATA9         GPIO_NUM_47
#define BOARD_LCD_PIN_DATA10        GPIO_NUM_21
#define BOARD_LCD_PIN_DATA11        GPIO_NUM_1
#define BOARD_LCD_PIN_DATA12        GPIO_NUM_2
#define BOARD_LCD_PIN_DATA13        GPIO_NUM_42
#define BOARD_LCD_PIN_DATA14        GPIO_NUM_41
#define BOARD_LCD_PIN_DATA15        GPIO_NUM_40

/* -------------------------------------------------------------------- I2C */

#define BOARD_I2C_PORT              I2C_NUM_0
#define BOARD_I2C_PIN_SDA           GPIO_NUM_8
#define BOARD_I2C_PIN_SCL           GPIO_NUM_9
#define BOARD_I2C_FREQ_HZ           400000

/* -------------------------------------------------------------- SD card --- */

/*
 * The card is on SPI, and its chip select is not a GPIO: the CH422G holds it
 * (EXIO4), so the SPI driver is told there is no CS pin and the expander keeps
 * the line asserted for as long as the card is mounted.  That is safe only
 * because the card is alone on this bus.
 *
 * Note for anyone comparing with the vendor demo: it asserts CS by writing
 * 0x0A straight to expander address 0x38, which also clears EXIO2 and turns
 * the backlight off.  Use board_sd_cs(), which is a read-modify-write against
 * the cached output byte.
 */
#define BOARD_SD_SPI_HOST           SPI2_HOST
#define BOARD_SD_PIN_MOSI           GPIO_NUM_11
#define BOARD_SD_PIN_CLK            GPIO_NUM_12
#define BOARD_SD_PIN_MISO           GPIO_NUM_13
#define BOARD_SD_FREQ_KHZ           20000

/* ------------------------------------------------------------------ touch */

/* GT911 INT.  Its level while RST is released selects the I2C address:
 * low -> 0x5D, high -> 0x14.  The board has no direct RST line; that goes
 * through the CH422G (EXIO1). */
#define BOARD_TOUCH_PIN_INT         GPIO_NUM_4
#define BOARD_TOUCH_I2C_ADDR        0x5D
#define BOARD_TOUCH_I2C_ADDR_ALT    0x14

/* ------------------------------------------------- CH422G I/O expander --- */

/* The CH422G is addressed by "command address" rather than by register: each
 * of these 7-bit addresses is a different function. */
#define BOARD_CH422G_ADDR_WR_SET    0x24  /* system settings                 */
#define BOARD_CH422G_ADDR_WR_OC     0x23  /* open-drain outputs OC0..OC3     */
#define BOARD_CH422G_ADDR_WR_IO     0x38  /* push-pull outputs IO0..IO7      */
#define BOARD_CH422G_ADDR_RD_IO     0x26  /* input read-back                 */

/* WR_SET bits */
#define BOARD_CH422G_SET_IO_OE      (1u << 0)  /* IO0..IO7 are outputs */
#define BOARD_CH422G_SET_A_SCAN     (1u << 1)
#define BOARD_CH422G_SET_OD_EN      (1u << 2)
#define BOARD_CH422G_SET_SLEEP      (1u << 3)

/* EXIO bit positions inside the WR_IO byte, per the board schematic. */
#define BOARD_EXIO_TOUCH_RST        1  /* CTP_RST,    active low  */
#define BOARD_EXIO_DISP             2  /* DISP + BL_EN, active high */
#define BOARD_EXIO_LCD_RST          3  /* LCD_RST,    active low  */
#define BOARD_EXIO_SD_CS            4  /* SDCS,       active low  */
/*
 * The USB/CAN multiplexer, and the reason the console is not on native USB.
 *
 * The board has two USB-C sockets: one behind a USB-UART bridge, and one
 * carrying the ESP32-S3's own USB.  Native USB -- USB-Serial-JTAG and USB-OTG
 * both -- is on GPIO19 and GPIO20, dedicated analog pins that cannot be routed
 * elsewhere, and the FSUSB42UMX switches that pair against CAN.  So CAN and
 * native USB are mutually exclusive whichever way round the mux is wired, and
 * selecting CAN costs the native console exactly when a bring-up wants one.
 *
 * The bridged socket shares nothing with CAN, which is why sdkconfig.defaults
 * makes UART0 the primary console and leaves USB-Serial-JTAG as a secondary.
 *
 * NOT YET CONFIRMED FROM THE SCHEMATIC: which GPIOs the bridged socket lands
 * on.  UART0's defaults are assumed; if the board disagrees the secondary
 * console still works whenever USB is selected, so the assumption fails soft.
 */
#define BOARD_EXIO_USB_SEL          5  /* 0 = USB, 1 = CAN        */
#define BOARD_EXIO_LCD_VDD_EN       6

/* Power-on state: panel and touch out of reset, backlight on, SD deselected,
 * USB (not CAN) routed to the FSUSB42UMX. */
#define BOARD_EXIO_DEFAULT                                                  \
    ((1u << BOARD_EXIO_TOUCH_RST) | (1u << BOARD_EXIO_DISP) |               \
     (1u << BOARD_EXIO_LCD_RST) | (1u << BOARD_EXIO_SD_CS))

/* -------------------------------------------------------------------- CAN */
/*
 * TWAI, on the pins the multiplexer switches.
 *
 * These are the ESP32-S3's native USB pins.  That is not a coincidence and it
 * is the whole reason USB and CAN are mutually exclusive here: GPIO19 and
 * GPIO20 are dedicated analog pins that cannot be routed through the matrix,
 * so a board wanting both has to switch them, which is what the FSUSB42UMX
 * does under BOARD_EXIO_USB_SEL.
 *
 * INFERRED, NOT READ OFF THE SCHEMATIC.  The mux, the "0 = USB, 1 = CAN"
 * comment and the pins' fixed function agree, but nobody has traced it.  Two
 * minutes with the schematic settles it, and getting it wrong presents as a
 * bus that never asserts -- the first line the bring-up report prints.
 */
#define PANEL_CAN_PIN_TX   GPIO_NUM_20
#define PANEL_CAN_PIN_RX   GPIO_NUM_19

/* Both ends must agree.  1 Mbit/s is what the coprocessor's 16 MHz crystal
 * reaches exactly; see COPRO_CAN_BITRATE and docs/Link.md. */
#define PANEL_CAN_BITRATE  1000000u

/* ------------------------------------------------------------------- link */
/*
 * The RS485 link to the coprocessor.  U6 is an SP3485EN -- a 3.3 V
 * transceiver, so there is no level shifting to do and no 5 V anywhere near
 * the module.
 *
 * The direction of these two is the opposite of the obvious reading, and the
 * schematic's own pin table does not settle it: that table calls GPIO15
 * "RS485_TX" and GPIO16 "RS485_RX", which names the *transceiver's* data
 * directions rather than the ESP32's.  The connectivity settles it twice:
 *
 *   GPIO15 is on the net reaching U6 pin 1, RO -- the receiver's *output*,
 *   which the ESP32 cannot drive.  So GPIO15 is an input: RX.
 *
 *   GPIO16 reaches pin 4, DI, and also the input of the buffer that operates
 *   the direction line.  An automatic-direction circuit only makes sense
 *   watching the line the ESP32 transmits on.  So GPIO16 is TX.
 *
 * Getting these the wrong way round costs an afternoon and looks like a dead
 * transceiver, so the reasoning is here rather than only the conclusion.
 *
 * There is no direction pin: the board switches DE and /RE itself.
 */
#define PANEL_LINK_UART_NUM   UART_NUM_1
#define PANEL_LINK_PIN_TX     GPIO_NUM_16
#define PANEL_LINK_PIN_RX     GPIO_NUM_15

/* ----------------------------------------------------------------- safety */
/*
 * The heartbeat.  GPIO6 is on **J8**, a three-pin header carrying 3V3, GND
 * and GPIO6 and nothing else -- so it reaches a connector, which was the open
 * question, and it arrives with a rail and a ground beside it, which is what a
 * retriggerable monostable on a small daughterboard wants.
 *
 * The schematic's pin table lists GPIO6 against no peripheral at all: the one
 * genuinely uncommitted fast pin on the board.
 */
#define PANEL_HEARTBEAT_PIN   GPIO_NUM_6

#ifdef __cplusplus
}
#endif
