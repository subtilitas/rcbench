/*
 * Feetech SCS and STS serial bus servos: the registers a servo tester
 * uses, per series, with their byte order.
 *
 * Implements the serial bus protocol of Feetech SCS and STS servos.
 * Feetech is a name or trademark of its owner; it is used here only to say
 * which servos this module works with (README.md, "Trademarks").
 *
 * Two series share the frame and differ in the register table:
 *
 *   SCS  potentiometer; a two-byte value has its high byte at the lower
 *        address; position 0 to 1023 (SCS15: 220 deg); lock flag at 48;
 *        no acceleration, no mode and no current register.
 *   STS  magnetic encoder; low byte at the lower address; position 0 to
 *        4095 (360 deg, 0.088 deg a step); lock flag at 55; acceleration
 *        at 41, mode at 33, present current at 69.
 *
 * A PING answer does not say which series a servo is: the caller keeps it
 * per servo.  SMS servos use the STS table on an RS-485 line; this module
 * builds the frames for them, and the RP2350 pin driver does not drive
 * RS-485.
 *
 * Addresses below FEETECH_RAM_FIRST are the EEPROM area: a write there is
 * kept over a power cycle only while the lock flag is 0.  The rest is RAM.
 *
 * Sources: Feetech's SCS15 memory table (version 0.5) for SCS, Feetech's
 * instruction generator workbook and ST-3215-C047 datasheet for STS
 * (README.md, "Sources").  The current scale is from a third-party table
 * only.  Other SCS models than the SCS15 are not covered by a document.
 *
 * The module includes nothing outside the C standard headers and holds no
 * global state.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FEETECH_REG_H
#define FEETECH_REG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FEETECH_SCS = 0,
    FEETECH_STS = 1,
    FEETECH_SERIES_COUNT
} feetech_series_t;

/** The baud rates of register 6, by index. */
#define FEETECH_BAUD_COUNT   8u
/** The factory setting: index 0, 1,000,000 baud, ID 1. */
#define FEETECH_BAUD_DEFAULT 0u
#define FEETECH_ID_DEFAULT   1u

/* Addresses, the same in both series unless named for one. */
#define FEETECH_REG_MODEL      3u   /**< STS: model number, 2 bytes, read */
#define FEETECH_REG_ID         5u
#define FEETECH_REG_BAUD       6u
#define FEETECH_REG_MIN_ANGLE  9u   /**< 2 bytes */
#define FEETECH_REG_MAX_ANGLE  11u  /**< 2 bytes */
#define FEETECH_REG_MODE       33u  /**< STS only: 0 position, 1 speed,
                                         2 PWM, 3 step */
#define FEETECH_REG_TORQUE     40u  /**< 0 off, 1 on */
#define FEETECH_REG_ACC        41u  /**< STS only: 0 to 255 */
#define FEETECH_REG_GOAL       42u  /**< 2 bytes */
#define FEETECH_REG_GOAL_TIME  44u  /**< 2 bytes */
#define FEETECH_REG_SPEED      46u  /**< 2 bytes */
#define FEETECH_REG_LOCK_SCS   48u
#define FEETECH_REG_LOCK_STS   55u
#define FEETECH_REG_PRESENT    56u  /**< the first of the live block */
#define FEETECH_REG_STATUS     65u  /**< error bits, as the ERR byte */
#define FEETECH_REG_CURRENT    69u  /**< STS only: 2 bytes */

/** The first RAM address; every lower one is EEPROM. */
#define FEETECH_RAM_FIRST 40u

/** The live block read from FEETECH_REG_PRESENT: 56 to 66 on SCS, 56 to
 *  70 on STS.  That an STS servo answers a 15-byte read is not
 *  measured. */
#define FEETECH_LIVE_SCS 11u
#define FEETECH_LIVE_STS 15u

/** Bits of the ERR byte and of FEETECH_REG_STATUS. */
#define FEETECH_ERR_VOLTAGE     0x01u
#define FEETECH_ERR_SENSOR      0x02u  /**< STS: the magnetic encoder */
#define FEETECH_ERR_TEMPERATURE 0x04u
#define FEETECH_ERR_CURRENT     0x08u
#define FEETECH_ERR_OVERLOAD    0x20u

/** The torque switch value that writes nothing but 0: torque off. */
#define FEETECH_TORQUE_OFF 0u

/** One register the tester shows or writes. */
typedef struct {
    uint8_t  addr;
    uint8_t  size;      /**< bytes: 1 or 2                             */
    uint16_t min, max;  /**< the values a write takes                  */
    bool     eeprom;    /**< below FEETECH_RAM_FIRST, or the lock flag  */
} feetech_reg_t;

/** The registers by role. */
typedef enum {
    FEETECH_R_ID = 0,
    FEETECH_R_BAUD,
    FEETECH_R_MIN_ANGLE,
    FEETECH_R_MAX_ANGLE,
    FEETECH_R_MODE,
    FEETECH_R_LOCK,
    FEETECH_R_TORQUE,
    FEETECH_R_ACC,
    FEETECH_R_GOAL,
    FEETECH_R_SPEED,
    FEETECH_R_COUNT
} feetech_role_t;

/** The register of @p role in @p series; false where the series has none
 *  (mode and acceleration on SCS) and for an unknown series or role. */
bool feetech_reg(feetech_series_t series, feetech_role_t role,
                 feetech_reg_t *out);

/** The baud rate of register 6's @p index; 0 above 7. */
uint32_t feetech_baud(unsigned index);

/** The lock flag's address; 0 for an unknown series. */
uint8_t feetech_lock_addr(feetech_series_t series);

/** Bytes of the live block; 0 for an unknown series. */
uint8_t feetech_live_bytes(feetech_series_t series);

/** The highest goal and present position. */
uint16_t feetech_position_max(feetech_series_t series);

/** A two-byte value into @p at in the series' byte order. */
void feetech_put16(feetech_series_t series, uint8_t *at, uint16_t v);
/** And out of it. */
uint16_t feetech_get16(feetech_series_t series, const uint8_t *at);

/** A signed value as the servo holds it: magnitude in bits 0 to 14, bit
 *  15 the sign.  Clamped to +-32767. */
uint16_t feetech_sign_mag(int32_t v);
/** And back. */
int32_t feetech_from_sign_mag(uint16_t raw);

/** The live block decoded. */
typedef struct {
    uint16_t position;     /**< steps                                    */
    int16_t  speed;        /**< steps/s, bit 15 the sign                 */
    uint16_t load;         /**< 0.1 % of full torque, bits 0 to 9; what
                                the higher bits say is not documented    */
    uint8_t  voltage_dv;   /**< 0.1 V                                    */
    uint8_t  temp_c;       /**< deg C                                    */
    uint8_t  status;       /**< FEETECH_ERR_* bits                       */
    uint8_t  moving;       /**< 1 while the servo moves                  */
    bool     has_current;  /**< STS                                      */
    uint16_t current_raw;  /**< counts of 6.5 mA                         */
} feetech_live_t;

/** Decode @p n bytes read from FEETECH_REG_PRESENT.  False, with @p out
 *  cleared, unless @p n is feetech_live_bytes(series). */
bool feetech_live(feetech_series_t series, const uint8_t *bytes, unsigned n,
                  feetech_live_t *out);

/** Present current in mA from its counts, rounded to nearest. */
uint32_t feetech_current_ma(uint16_t raw);

#ifdef __cplusplus
}
#endif

#endif /* FEETECH_REG_H */
