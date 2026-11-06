/**
 * @file
 * @brief WitMotion serial protocol: the 11-byte frames of the BWT901CL
 *        and its family, their decoding to units, and the 5-byte command
 *        encoder.  No pin driver: the caller moves the bytes.
 *
 * Frame: 0x55, type, four int16 little-endian, sum of the first 10 bytes
 * (low byte).  Types 0x51 acceleration (/32768 x 16g, temperature /100°C),
 * 0x52 angular rate (/32768 x 2000°/s, battery /100V), 0x53 angle
 * (/32768 x 180°, version), 0x54 magnetic field (0.15µT/LSB), 0x59
 * quaternion (/32768).  A byte that breaks the sync is dropped and the
 * parser looks for the next 0x55 inside what it holds (resync).  Commands:
 * FF AA reg lo hi; the unlock FF AA 69 88 B5 goes first on the newer
 * firmware and is harmless on the older one (not measured on this
 * sensor).  A RATE change needs SAVE and a power cycle of the sensor.
 *
 * WitMotion is a name or trademark of its owner; it is used here only to
 * say which sensors this module decodes (README.md, "Trademarks").
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef WITMOTION_H
#define WITMOTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIT_FRAME_LEN   11u     /**< bytes of one frame */
#define WIT_SYNC        0x55u   /**< the first byte */
#define WIT_CMD_LEN     5u      /**< bytes of one command */

/** @brief Frame types. */
typedef enum {
    WIT_T_TIME  = 0x50,
    WIT_T_ACC   = 0x51,
    WIT_T_GYRO  = 0x52,
    WIT_T_ANGLE = 0x53,
    WIT_T_MAG   = 0x54,
    WIT_T_QUAT  = 0x59,
} wit_type_t;

/** @brief Registers the command encoder writes. */
typedef enum {
    WIT_REG_SAVE  = 0x00,
    WIT_REG_CALSW = 0x01,
    WIT_REG_RSW   = 0x02,
    WIT_REG_RATE  = 0x03,
    WIT_REG_AXIS6 = 0x24,
} wit_reg_t;

/** @brief RATE codes (register 0x03). */
typedef enum {
    WIT_RATE_0_2HZ = 0x01, WIT_RATE_0_5HZ = 0x02, WIT_RATE_1HZ = 0x03,
    WIT_RATE_2HZ = 0x04,   WIT_RATE_5HZ = 0x05,   WIT_RATE_10HZ = 0x06,
    WIT_RATE_20HZ = 0x07,  WIT_RATE_50HZ = 0x08,  WIT_RATE_100HZ = 0x09,
    WIT_RATE_125HZ = 0x0A, WIT_RATE_200HZ = 0x0B,
} wit_rate_t;

/** @brief CALSW modes (register 0x01). */
typedef enum { WIT_CAL_EXIT = 0, WIT_CAL_ACC = 1, WIT_CAL_MAG = 2 } wit_cal_t;

/** @brief RSW bits (register 0x02): which frames the sensor sends. */
#define WIT_RSW_TIME  0x01u
#define WIT_RSW_ACC   0x02u   /**< 0x51 acceleration */
#define WIT_RSW_GYRO  0x04u   /**< 0x52 angular rate */
#define WIT_RSW_ANGLE 0x08u   /**< 0x53 angle */
#define WIT_RSW_MAG   0x10u   /**< 0x54 magnetic field */
#define WIT_RSW_QUAT  0x200u  /**< 0x59 quaternion */
/** @brief The sensor's default content: 0x51 to 0x54. */
#define WIT_RSW_DEFAULT (WIT_RSW_ACC | WIT_RSW_GYRO | WIT_RSW_ANGLE | WIT_RSW_MAG)

/** @brief One frame as received: raw words. */
typedef struct {
    uint8_t type;      /**< wit_type_t */
    int16_t v[4];      /**< the four words, little-endian decoded */
} wit_frame_t;

/** @brief The parser's state: at most one frame in flight. */
typedef struct {
    uint8_t  buf[WIT_FRAME_LEN];  /**< the bytes held, from a 0x55 */
    uint8_t  n;          /**< bytes held */
    uint32_t frames;     /**< frames handed out */
    uint32_t bad_sum;    /**< frames dropped for their sum */
    uint32_t resyncs;    /**< bytes dropped looking for 0x55 */
} wit_parser_t;

/** @brief Clear the parser. @param p The parser; NULL does nothing. */
void wit_parser_init(wit_parser_t *p);

/**
 * @brief Feed one byte.
 * @param p   The parser.
 * @param b   The byte.
 * @param out Receives the frame when one completes with a good sum.
 * @return true when @p out holds a frame.
 */
bool wit_parser_feed(wit_parser_t *p, uint8_t b, wit_frame_t *out);

/** @brief 0x51 decoded: m/s² (1g = 9.80665m/s²) and °C. */
typedef struct {
    float ax;       /**< X, m/s² */
    float ay;       /**< Y, m/s² */
    float az;       /**< Z, m/s² */
    float temp_c;   /**< temperature, °C */
} wit_acc_t;
/** @brief 0x52 decoded: °/s and the battery in V. */
typedef struct {
    float wx;         /**< X, °/s */
    float wy;         /**< Y, °/s */
    float wz;         /**< Z, °/s */
    float battery_v;  /**< battery, V */
} wit_gyro_t;
/** @brief 0x53 decoded: roll (X), pitch (Y), yaw (Z) in °, -180 to 180,
 *         and the firmware version word. */
typedef struct {
    float roll;        /**< about X, ° */
    float pitch;       /**< about Y, ° */
    float yaw;         /**< about Z, ° */
    uint16_t version;  /**< the fourth word */
} wit_angle_t;
/** @brief 0x54 decoded: µT. */
typedef struct {
    float mx;  /**< X, µT */
    float my;  /**< Y, µT */
    float mz;  /**< Z, µT */
} wit_mag_t;

/** @brief Decode a 0x51 frame.
 *  @param f   The frame.
 *  @param out Receives the values.
 *  @return false for another type or NULL. */
bool wit_decode_acc(const wit_frame_t *f, wit_acc_t *out);
/** @brief Decode a 0x52 frame.
 *  @param f   The frame.
 *  @param out Receives the values.
 *  @return false for another type or NULL. */
bool wit_decode_gyro(const wit_frame_t *f, wit_gyro_t *out);
/** @brief Decode a 0x53 frame.
 *  @param f   The frame.
 *  @param out Receives the values.
 *  @return false for another type or NULL. */
bool wit_decode_angle(const wit_frame_t *f, wit_angle_t *out);
/** @brief Decode a 0x54 frame.
 *  @param f   The frame.
 *  @param out Receives the values.
 *  @return false for another type or NULL. */
bool wit_decode_mag(const wit_frame_t *f, wit_mag_t *out);

/**
 * @brief Encode a command: FF AA @p reg lo hi.
 * @param out Receives WIT_CMD_LEN bytes.
 * @param reg wit_reg_t.
 * @param value The 16-bit value, little-endian.
 * @return WIT_CMD_LEN; 0 for NULL.
 */
size_t wit_cmd(uint8_t out[WIT_CMD_LEN], uint8_t reg, uint16_t value);
/** @brief FF AA 69 88 B5.
 *  @param out Receives WIT_CMD_LEN bytes.
 *  @return WIT_CMD_LEN; 0 for NULL. */
size_t wit_cmd_unlock(uint8_t out[WIT_CMD_LEN]);
/** @brief SAVE: FF AA 00 00 00.
 *  @param out Receives WIT_CMD_LEN bytes.
 *  @return WIT_CMD_LEN; 0 for NULL. */
size_t wit_cmd_save(uint8_t out[WIT_CMD_LEN]);
/** @brief RATE @p code (wit_rate_t).
 *  @param out  Receives WIT_CMD_LEN bytes.
 *  @param code The rate code.
 *  @return WIT_CMD_LEN; 0 for NULL or a code outside 0x01 to 0x0B. */
size_t wit_cmd_rate(uint8_t out[WIT_CMD_LEN], uint8_t code);
/** @brief RSW @p mask.
 *  @param out  Receives WIT_CMD_LEN bytes.
 *  @param mask The WIT_RSW_* bits.
 *  @return WIT_CMD_LEN; 0 for NULL. */
size_t wit_cmd_rsw(uint8_t out[WIT_CMD_LEN], uint16_t mask);
/** @brief CALSW @p mode (wit_cal_t).
 *  @param out  Receives WIT_CMD_LEN bytes.
 *  @param mode The calibration mode.
 *  @return WIT_CMD_LEN; 0 for NULL or a mode above 2. */
size_t wit_cmd_calsw(uint8_t out[WIT_CMD_LEN], uint8_t mode);
/** @brief AXIS6: 1 for the 6-axis algorithm, 0 for 9-axis.
 *  @param out Receives WIT_CMD_LEN bytes.
 *  @param six true for the 6-axis algorithm.
 *  @return WIT_CMD_LEN; 0 for NULL. */
size_t wit_cmd_axis6(uint8_t out[WIT_CMD_LEN], bool six);

/** @brief The RSW bit of a frame type: bit (type - 0x50), so 0x02 for
 *         0x51 and 0x200 for 0x59.
 *  @param type The frame's type byte.
 *  @return The bit; 0 for a type outside 0x50 to 0x5F. */
uint16_t wit_rsw_bit(uint8_t type);

/** @brief The output rate of a code, in 0.1Hz: 2 for 0x01, 2000 for 0x0B.
 *  @param code The rate code.
 *  @return The rate in 0.1Hz; 0 for a code the table does not hold. */
uint16_t wit_rate_dhz(uint8_t code);

#ifdef __cplusplus
}
#endif
#endif
