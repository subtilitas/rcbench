/*
 * The pins and parts of each board the test knows.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef P4TEST_BOARD_H
#define P4TEST_BOARD_H

#include "driver/i2c_types.h"
#include "sdkconfig.h"

#if defined(CONFIG_P4TEST_BOARD_WIFI6_HOSYOND7)
/* J2, the 22-pin DSI connector: I2C on pins 2 and 3, 2.2 kOhm pull-ups to
 * 3.3 V on the board (R44, R46). */
#define BOARD_NAME     "Waveshare ESP32-P4-WIFI6 + Hosyond 7in 800x480 DSI"
#define BOARD_I2C_PORT I2C_NUM_0
#define BOARD_I2C_SDA  7
#define BOARD_I2C_SCL  8
#else
#error "no board chosen in menuconfig"
#endif

#endif
