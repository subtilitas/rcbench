/**
 * @file
 * @brief The rcbench sensor nodes' Bluetooth LE (Low Energy) protocol: the
 * GATT (generic attribute profile) service, the packets of the power node
 * and the balance node, the control writes and the advertising.
 *
 * A node is a BLE peripheral on an ESP32-C6.  It sends finished readings;
 * the panel decodes them.  Both ends compile this module: the node encodes,
 * the panel decodes, and the host suite holds both to the layout below.
 *
 * Every packet is little-endian and starts with the same 6-byte header:
 *
 * | Byte | Field    | Meaning                                          |
 * |------|----------|--------------------------------------------------|
 * | 0    | `type`   | RCNODE_T_*                                       |
 * | 1    | `seq`    | counts the packets of one characteristic, wraps at 256 |
 * | 2-5  | `t_node` | the node's clock, µs, wraps at 2^32µs (71.6min)  |
 *
 * The service and its characteristics share the 128-bit base
 * 72636265-6E63-6800-XXXX-000000000000 ("rcbench" in ASCII in the first 7
 * bytes); XXXX is RCNODE_ID_*.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The protocol version INFO carries. */
#define RCNODE_PROTO 1u

/** @brief Roles: which sensor a node carries. @{ */
#define RCNODE_ROLE_POWER   1u  /**< an INA228 on the ESC's (electronic speed controller's) XT60 power path */
#define RCNODE_ROLE_BALANCE 2u  /**< a vibration sensor and an index sensor */
#define RCNODE_ROLE_SERVO   3u  /**< the remote servo node's ES3C28P and its Core2350B */
/** @} */

/** @brief The 16-bit part of each UUID (universally unique identifier). @{ */
#define RCNODE_ID_SERVICE 0x0100u  /**< the node service */
#define RCNODE_ID_STREAM  0x0101u  /**< NOTIFY: windows, totals and reports */
#define RCNODE_ID_STATUS  0x0102u  /**< READ, NOTIFY: RCNODE_T_STATUS */
#define RCNODE_ID_CONTROL 0x0103u  /**< WRITE, WRITE WITHOUT RESPONSE: RCNODE_OP_* */
#define RCNODE_ID_INFO    0x0104u  /**< READ: RCNODE_T_INFO */
/** @} */

/** @brief Packet types, byte 0 of every packet. @{ */
#define RCNODE_T_PWR_WIN    0x01u  /**< STREAM, power: one 20ms window */
#define RCNODE_T_PWR_ACC    0x02u  /**< STREAM, power: the accumulators, every 500ms */
#define RCNODE_T_PWR_RUN    0x03u  /**< STREAM, power: the extremes since the RUN marker, every 500ms */
#define RCNODE_T_BAL_REPORT 0x20u  /**< STREAM, balance: one report, every 250ms */
#define RCNODE_T_STATUS     0x30u  /**< STATUS, both roles */
#define RCNODE_T_INFO       0x31u  /**< INFO, both roles */
#define RCNODE_T_CONFIG     0x32u  /**< STREAM, both roles: the node's own settings, after RCNODE_OP_CONFIG */
/** @} */

/** @brief Packet lengths in bytes, header included. @{ */
#define RCNODE_HDR_LEN        6u  /**< the header */
#define RCNODE_PWR_WIN_LEN   20u  /**< RCNODE_T_PWR_WIN */
#define RCNODE_PWR_ACC_LEN   20u  /**< RCNODE_T_PWR_ACC */
#define RCNODE_PWR_RUN_LEN   18u  /**< RCNODE_T_PWR_RUN */
#define RCNODE_BAL_REPORT_LEN 24u  /**< needs an ATT (attribute protocol) MTU of 27 or more */
#define RCNODE_STATUS_LEN    20u  /**< RCNODE_T_STATUS */
#define RCNODE_INFO_LEN      20u  /**< RCNODE_T_INFO */
#define RCNODE_CONFIG_LEN    30u  /**< RCNODE_T_CONFIG: needs an ATT MTU of 33 or more */
#define RCNODE_PKT_MAX       30u   /**< the longest packet */
/** @} */

/** @brief Timing the node keeps. @{ */
#define RCNODE_WIN_US       20000u  /**< a power window */
#define RCNODE_ACC_MS         500u  /**< RCNODE_T_PWR_ACC and RCNODE_T_PWR_RUN */
#define RCNODE_BAL_MS         250u  /**< RCNODE_T_BAL_REPORT */
#define RCNODE_STATUS_MS     1000u  /**< RCNODE_T_STATUS notified */
/** @} */

/** @brief Units. @{ */
#define RCNODE_V_STEP_UV     2000u  /**< voltage fields: 2mV a step, 0 to 131.07V */
#define RCNODE_PHASE_STEPS   3600u  /**< phase fields: 0.1° a step, 0 to 359.9° */
/** @} */

/** @brief The 40-bit accumulators' mask. */
#define RCNODE_ACC_MASK 0xFFFFFFFFFFull

/** @brief "No value" in a current or voltage extreme. @{ */
#define RCNODE_I_NONE INT32_MIN   /**< i_max without a current sample that is not clipped */
#define RCNODE_V_NONE UINT16_MAX  /**< v_min without a sample */
/** @} */

/** @brief Battery thresholds, mV. @{ */
#define RCNODE_BAT_LOW_MV  3500u  /**< RCNODE_ST_BAT_LOW below this */
#define RCNODE_BAT_OFF_MV  3300u  /**< the node switches itself off below this */
/** @} */

/** @brief Result of a decode. */
typedef enum {
    RCNODE_OK = 0,
    RCNODE_ERR_ARG,    /**< a NULL argument */
    RCNODE_ERR_LEN,    /**< the length is not the type's */
    RCNODE_ERR_TYPE,   /**< an unknown type or control operation */
    RCNODE_ERR_RANGE,  /**< a field outside its range */
} rcnode_err_t;

/** @brief The header every packet starts with. */
typedef struct {
    uint8_t  type;    /**< RCNODE_T_* */
    uint8_t  seq;     /**< per characteristic, wraps at 256 */
    uint32_t t_node;  /**< µs of the node's clock, wraps at 2^32µs */
} rcnode_hdr_t;

/* ---------------------------------------------------------- power node */

/** @brief RCNODE_T_PWR_WIN flags. @{ */
#define RCNODE_WF_CLIP_HIGH 0x01u  /**< a current sample at the top of the range */
#define RCNODE_WF_CLIP_LOW  0x02u  /**< a current sample at the bottom of the range */
#define RCNODE_WF_ONLINE    0x04u  /**< the INA228 is online */
#define RCNODE_WF_PACK      0x08u  /**< the window's mean voltage is 5.0V or more */
#define RCNODE_WF_RUN       0x10u  /**< a RUN marker has been taken since the node started */
#define RCNODE_WF_READ_FAIL 0x20u  /**< a sample's read failed in the window */
/** @} */

/** @brief The pack is present at this bus voltage and above, µV. */
#define RCNODE_PACK_UV 5000000

/**
 * @brief One 20ms window of the power node, RCNODE_T_PWR_WIN, 20 bytes.
 *
 * | Byte  | Field    |
 * |-------|----------|
 * | 0-5   | header   |
 * | 6     | flags    |
 * | 7     | n        |
 * | 8-11  | i_mean   |
 * | 12-15 | i_max    |
 * | 16-17 | v_mean   |
 * | 18-19 | v_min    |
 *
 * A clipped current sample is counted in n and flagged, and is not part of
 * i_mean or i_max.  A window without a current value (n 0, or every sample
 * clipped) carries i_mean 0 and i_max RCNODE_I_NONE; a window with n 0
 * carries v_mean 0 and v_min RCNODE_V_NONE.
 */
typedef struct {
    rcnode_hdr_t hdr;     /**< t_node is the window's end */
    uint8_t      flags;   /**< RCNODE_WF_* */
    uint8_t      n;       /**< samples read in the window, 17 or 18 at 1130µs */
    int32_t      i_mean;  /**< µA, the mean of the samples that are not clipped */
    int32_t      i_max;   /**< µA, the highest sample that is not clipped */
    uint16_t     v_mean;  /**< 2mV steps, the mean bus voltage */
    uint16_t     v_min;   /**< 2mV steps, the lowest bus voltage */
} rcnode_pwr_win_t;

/** @brief RCNODE_T_PWR_ACC flags. @{ */
#define RCNODE_AF_CHARGE_OK 0x01u  /**< CHARGE was read in this period */
#define RCNODE_AF_ENERGY_OK 0x02u  /**< ENERGY was read in this period */
#define RCNODE_AF_TEMP_OK   0x04u  /**< DIETEMP was read in this period */
#define RCNODE_AF_CHARGE_OF 0x08u  /**< DIAG_ALRT reported CHARGEOF since the last packet */
#define RCNODE_AF_ENERGY_OF 0x10u  /**< DIAG_ALRT reported ENERGYOF since the last packet */
#define RCNODE_AF_ONLINE    0x20u  /**< the INA228 is online */
/** @} */

/**
 * @brief The INA228's accumulators, RCNODE_T_PWR_ACC, 20 bytes, every 500ms.
 *
 * | Byte  | Field  |
 * |-------|--------|
 * | 0-5   | header |
 * | 6     | flags  |
 * | 7     | epoch  |
 * | 8-12  | charge |
 * | 13-17 | energy |
 * | 18-19 | temp   |
 *
 * The node never clears the accumulators.  The part clears them at its own
 * reset and at each set-up the node writes; epoch counts those set-ups, so a
 * change of epoch tells the panel the counts restarted from 0.  A register
 * not read in the period keeps its last value and has its _OK flag clear.
 */
typedef struct {
    rcnode_hdr_t hdr;     /**< t_node is the read's time */
    uint8_t      flags;   /**< RCNODE_AF_* */
    uint8_t      epoch;   /**< set-ups of the part since the node started, wraps at 256 */
    uint64_t     charge;  /**< CHARGE as read: 40 bits, two's complement */
    uint64_t     energy;  /**< ENERGY as read: 40 bits, unsigned */
    int16_t      temp;    /**< the die temperature, 0.01°C steps */
} rcnode_pwr_acc_t;

/** @brief RCNODE_T_PWR_RUN flags. @{ */
#define RCNODE_RF_CLIP      0x01u  /**< a window since the marker had a clip */
#define RCNODE_RF_READ_FAIL 0x02u  /**< a window since the marker had a failed read */
#define RCNODE_RF_EMPTY     0x04u  /**< a window since the marker had no sample */
/** @} */

/**
 * @brief The extremes since the last RUN marker, RCNODE_T_PWR_RUN, 18 bytes,
 * every 500ms.
 *
 * | Byte  | Field   |
 * |-------|---------|
 * | 0-5   | header  |
 * | 6     | tag     |
 * | 7     | flags   |
 * | 8-11  | i_max   |
 * | 12-13 | v_min   |
 * | 14-17 | windows |
 *
 * A radio gap loses no peak: the panel reads the run's highest current and
 * lowest voltage here.  Before the first RUN marker tag is 0 and the
 * extremes run from the node's start.
 */
typedef struct {
    rcnode_hdr_t hdr;      /**< t_node is the packet's time */
    uint8_t      tag;      /**< the tag of the last RUN write */
    uint8_t      flags;    /**< RCNODE_RF_* */
    int32_t      i_max;    /**< µA, the highest current; RCNODE_I_NONE without a value */
    uint16_t     v_min;    /**< 2mV steps, the lowest voltage; RCNODE_V_NONE without a value */
    uint32_t     windows;  /**< windows closed since the marker */
} rcnode_pwr_run_t;

/* -------------------------------------------------------- balance node */

/**
 * @brief RCNODE_T_BAL_REPORT flags.  Bits 0 to 11 are shared/vibe's
 * VIBE_F_* bit for bit (vibe_demod.h), so the node copies the demodulator's
 * flags unchanged; bit 15 is the node's own.  @{ */
#define RCNODE_BF_VALID        0x0001u  /**< a revolution counted: the magnitudes hold */
#define RCNODE_BF_INDEX_SEEN   0x0002u  /**< an index edge arrived in the report */
#define RCNODE_BF_INDEX_STEADY 0x0004u  /**< revolutions counted and none refused or lost */
#define RCNODE_BF_INDEX_LOST   0x0008u  /**< the longest revolution passed without an edge */
#define RCNODE_BF_NO_INDEX     0x0010u  /**< no-index mode: rpm from CONTROL, phase 0 */
#define RCNODE_BF_CLIPPED      0x0020u  /**< an ADC (analogue-to-digital converter) sample at an end of its range */
#define RCNODE_BF_DC_OK        0x0040u  /**< the vibration sensor's DC level is in range */
#define RCNODE_BF_BLADE_OK     0x0080u  /**< the blade-pass is below half the sample rate: mag_blade holds */
#define RCNODE_BF_QUALITY      0x0100u  /**< quality holds: 2 revolutions or more behind it */
#define RCNODE_BF_EDGE_DROPPED 0x0200u  /**< an index edge out of order or past a full queue */
#define RCNODE_BF_RPM_RANGE    0x0400u  /**< no-index mode: the rpm written gives a revolution out of range */
#define RCNODE_BF_RESTARTED    0x0800u  /**< the mode or the blade count changed in the report */
#define RCNODE_BF_COUNTS       0x8000u  /**< magnitudes in ADC counts, not mg (a piezo) */
/** @} */

/**
 * @brief One balance report, RCNODE_T_BAL_REPORT, 24 bytes, every 250ms.
 *
 * | Byte  | Field     |
 * |-------|-----------|
 * | 0-5   | header    |
 * | 6-7   | flags     |
 * | 8     | quality   |
 * | 9     | revs      |
 * | 10-11 | rpm       |
 * | 12-13 | spread_pm |
 * | 14-15 | mag_1x    |
 * | 16-17 | phase_1x  |
 * | 18-19 | mag_2x    |
 * | 20-21 | mag_blade |
 * | 22-23 | rms       |
 *
 * The magnitudes are the means over the report's revolutions.  Quality is
 * the coherence |mean vector| / mean |vector| of the 1x component.  Only the
 * 1x component carries a phase: the balance methods use the 1x phase alone,
 * and the 2x and blade-pass phases would take the packet to 28 bytes.  The
 * sensor's DC level travels as RCNODE_BF_DC_OK only.
 */
typedef struct {
    rcnode_hdr_t hdr;        /**< t_node is the report's end */
    uint16_t     flags;      /**< RCNODE_BF_* */
    uint8_t      quality;    /**< %, 0 to 100; holds with RCNODE_BF_QUALITY */
    uint8_t      revs;       /**< revolutions (no-index mode: units) counted, saturates at 255 */
    uint16_t     rpm;        /**< 1rpm steps */
    uint16_t     spread_pm;  /**< (longest - shortest period) / mean period, 0.1% steps */
    uint16_t     mag_1x;     /**< the 1x component, the imbalance: mg, or ADC counts with RCNODE_BF_COUNTS */
    uint16_t     phase_1x;   /**< 0.1° steps from the index edge, against the direction of rotation (shared/vibe's sense), 0 to 3599 */
    uint16_t     mag_2x;     /**< the 2x component, misalignment: as mag_1x */
    uint16_t     mag_blade;  /**< the blade-pass component: as mag_1x */
    uint16_t     rms;        /**< mg, the broadband rms (root mean square) after the DC is removed */
} rcnode_bal_report_t;

/* --------------------------------------------------- status and info */

/** @brief RCNODE_T_STATUS flags. @{ */
#define RCNODE_ST_BAT_LOW   0x0001u  /**< the battery reads under RCNODE_BAT_LOW_MV */
#define RCNODE_ST_BAT_NONE  0x0002u  /**< no battery reading: the divider reads under 1.0V */
#define RCNODE_ST_SENSOR    0x0004u  /**< the role's sensor is online */
#define RCNODE_ST_PACK      0x0008u  /**< power: a pack is on the XT60 */
#define RCNODE_ST_STAY      0x0010u  /**< the console holds the node awake */
#define RCNODE_ST_SUBSCRIBED 0x0020u /**< STREAM notifications are on */
#define RCNODE_ST_BAT_ERR   0x0040u  /**< the battery read failed: vbat 0, the low-battery switch-off holds its count */
/** @} */

/** @brief The sensor's state in RCNODE_T_STATUS. */
typedef enum {
    RCNODE_SENSOR_NONE = 0,   /**< no sensor in this role yet */
    RCNODE_SENSOR_UNPROBED,   /**< set up, not probed yet */
    RCNODE_SENSOR_ABSENT,     /**< nothing answers */
    RCNODE_SENSOR_WRONG_ID,   /**< another part answers */
    RCNODE_SENSOR_REFUSED,    /**< the set-up did not read back */
    RCNODE_SENSOR_ONLINE,     /**< in use */
    RCNODE_SENSOR_OFFLINE,    /**< was online; reads fail */
    RCNODE_SENSOR_COUNT       /**< the number of states */
} rcnode_sensor_t;

/**
 * @brief The node's status, RCNODE_T_STATUS, 20 bytes, on STATUS: read, and
 * notified every 1000ms.
 *
 * | Byte  | Field   |
 * |-------|---------|
 * | 0-5   | header  |
 * | 6-7   | flags   |
 * | 8-9   | vbat    |
 * | 10-12 | fw      |
 * | 13    | sensor  |
 * | 14-15 | errors  |
 * | 16-17 | resets  |
 * | 18-19 | dropped |
 */
typedef struct {
    rcnode_hdr_t hdr;      /**< seq counts STATUS packets */
    uint16_t     flags;    /**< RCNODE_ST_* */
    uint16_t     vbat;     /**< the cell, mV; 0 with RCNODE_ST_BAT_NONE */
    uint8_t      fw[3];    /**< firmware MAJOR, MINOR, PATCH */
    uint8_t      sensor;   /**< rcnode_sensor_t */
    uint16_t     errors;   /**< failed sensor bus transactions, wraps at 65536 */
    uint16_t     resets;   /**< the sensor found reset or lost and set up again, wraps */
    uint16_t     dropped;  /**< STREAM packets not sent: queue full or no buffer, wraps */
} rcnode_status_t;

/** @brief Boards. @{ */
#define RCNODE_BOARD_C6_ZERO 1u  /**< Waveshare ESP32-C6-Zero */
#define RCNODE_BOARD_ES3C28P 2u  /**< 2.8" ESP32-S3 display board, LCDWIKI ES3C28P */
/** @} */

/**
 * @brief What the node is, RCNODE_T_INFO, 20 bytes, on INFO: read.
 *
 * | Byte  | Field     |
 * |-------|-----------|
 * | 0-5   | header    |
 * | 6     | proto     |
 * | 7     | role      |
 * | 8     | board     |
 * | 9     | addr_type |
 * | 10-15 | addr      |
 * | 16-19 | the role's part: power i228_addr, adcrange, shunt_uohm (2) |
 *
 * The power node's part names the INA228's set-up, so the panel turns the
 * raw accumulators into charge and energy with the same calibration: the
 * address the node found the part at, 0x40 to 0x4F, or 0 before one is
 * found; ADCRANGE; the shunt in force.  A balance node sends 0 in those 4
 * bytes.
 */
typedef struct {
    rcnode_hdr_t hdr;         /**< seq counts INFO reads */
    uint8_t      proto;       /**< RCNODE_PROTO */
    uint8_t      role;        /**< RCNODE_ROLE_* */
    uint8_t      board;       /**< RCNODE_BOARD_* */
    uint8_t      addr_type;   /**< 0 public, 1 random */
    uint8_t      addr[6];     /**< least significant byte first */
    uint8_t      i228_addr;   /**< power: the INA228's 7-bit address, 0 none found */
    uint8_t      adcrange;    /**< power: CONFIG.ADCRANGE, 0 or 1 */
    uint16_t     shunt_uohm;  /**< power: the shunt, µΩ */
} rcnode_info_t;

/* ------------------------------------------------------------- control */

/** @brief Control operations, byte 0 of a CONTROL write. @{ */
#define RCNODE_OP_RUN    0x01u  /**< 2 bytes: op, tag 1 to 255; the run extremes restart */
#define RCNODE_OP_SLEEP  0x02u  /**< 1 byte: sleep now, wake on the 10s timer */
#define RCNODE_OP_OFF    0x03u  /**< 1 byte: off until the RESET key or a power cycle */
#define RCNODE_OP_BLADES 0x04u  /**< 2 bytes: op, count; balance only */
#define RCNODE_OP_RPM    0x05u  /**< 3 bytes: op, rpm (16 bits); balance only, 0 = index mode */
#define RCNODE_OP_LANG   0x06u  /**< 2 bytes: op, RCNODE_LANG_*; power only: the language of a display node's texts */
#define RCNODE_OP_SHUNT  0x07u  /**< 3 bytes: op, the INA228's shunt in µΩ (16 bits); power only; also the value both ends agree on */
#define RCNODE_OP_CONFIG 0x08u  /**< 1 byte: RCNODE_T_CONFIG on STREAM now and after each change on this link */
#define RCNODE_OP_NAME   0x09u  /**< 17 bytes: op, the name in 16 bytes, NUL padded (rcnode_uname_ok()) */
#define RCNODE_OP_I228   0x0Au  /**< 2 bytes: op, RCNODE_I228_AUTO or a fixed address 0x40 to 0x4F; power only */
/** @} */

/** @brief RCNODE_OP_LANG's values: the panel's language. @{ */
#define RCNODE_LANG_EN 0u  /**< English */
#define RCNODE_LANG_DE 1u  /**< German */
/** @} */

/** @brief Limits of the control values. @{ */
#define RCNODE_BLADES_MIN  1u  /**< the fewest blades */
#define RCNODE_BLADES_MAX 12u  /**< the most blades: shared/vibe's VIBE_BLADES_MAX */
#define RCNODE_RPM_MAX  60000u  /**< the highest rpm; the ADXL335 at 4.7nF passes 1,060Hz, 63,600rpm */
#define RCNODE_SHUNT_MIN_UOHM    50u  /**< the smallest shunt: 819.2A full scale at ADCRANGE 1 */
#define RCNODE_SHUNT_MAX_UOHM 10000u  /**< the largest shunt: 4.096A full scale at ADCRANGE 1 */
#define RCNODE_SHUNT_DEF_UOHM   200u  /**< the MATEK I2C-INA-BM's shunt, the power node's default */
/** @} */

/** @brief The node's own name and the INA228's address setting. @{ */
#define RCNODE_UNAME_MAX  16u   /**< the longest name, characters */
#define RCNODE_I228_AUTO   0u   /**< RCNODE_OP_I228: the search of 0x40 to 0x4F */
#define RCNODE_I228_MIN 0x40u   /**< the lowest fixed address */
#define RCNODE_I228_MAX 0x4Fu   /**< the highest fixed address */
#define RCNODE_AGE_NONE 0xFFFFu /**< RCNODE_T_CONFIG's shunt_age_s: no change on the node since its start */
/** @} */

/** @brief The longest control write, bytes: RCNODE_OP_NAME. */
#define RCNODE_CTL_MAX 17u

/** @brief A control write. */
typedef struct {
    uint8_t  op;      /**< RCNODE_OP_* */
    uint8_t  tag;     /**< RCNODE_OP_RUN: the tag, 1 to 255; 0 means no RUN yet and is refused */
    uint8_t  blades;  /**< RCNODE_OP_BLADES: the count */
    uint16_t rpm;     /**< RCNODE_OP_RPM: the rotation rate, 0 for index mode */
    uint8_t  lang;    /**< RCNODE_OP_LANG: RCNODE_LANG_* */
    uint16_t shunt_uohm; /**< RCNODE_OP_SHUNT: the shunt, µΩ, RCNODE_SHUNT_MIN_UOHM to RCNODE_SHUNT_MAX_UOHM */
    char     name[RCNODE_UNAME_MAX + 1u]; /**< RCNODE_OP_NAME: the name, NUL-terminated, "" for none */
    uint8_t  i228;    /**< RCNODE_OP_I228: RCNODE_I228_AUTO or RCNODE_I228_MIN to RCNODE_I228_MAX */
} rcnode_ctl_t;

/** @brief RCNODE_T_CONFIG flags. @{ */
#define RCNODE_CF_SHUNT 0x01u  /**< the role keeps a shunt and an INA228 address: the power role */
/** @} */

/**
 * @brief The node's own settings, RCNODE_T_CONFIG, 30 bytes, on STREAM.
 *
 * | Byte  | Field        |
 * |-------|--------------|
 * | 0-5   | header       |
 * | 6     | flags        |
 * | 7     | i228_set     |
 * | 8-9   | shunt_uohm   |
 * | 10-11 | shunt_agreed |
 * | 12-13 | shunt_age_s  |
 * | 14-29 | name         |
 *
 * A node sends it only after a central wrote RCNODE_OP_CONFIG on the link,
 * at once and after each change of a value in it, so a central built
 * before it never sees one.  shunt_agreed is the shunt both ends last
 * agreed on: the value of the last SHUNT write, or the stored shunt before
 * any.  shunt_age_s is the time since the node's own SETUP changed the
 * shunt, in s, RCNODE_AGE_NONE when it did not since the node started
 * (rcnode_shunt_sync()).  The name is NUL padded; all 16 bytes may be
 * characters.  Without RCNODE_CF_SHUNT, i228_set and the three shunt fields
 * are 0.
 */
typedef struct {
    rcnode_hdr_t hdr;            /**< seq: the STREAM's */
    uint8_t      flags;          /**< RCNODE_CF_* */
    uint8_t      i228_set;       /**< RCNODE_I228_AUTO or a fixed address */
    uint16_t     shunt_uohm;     /**< the shunt in force, µΩ */
    uint16_t     shunt_agreed;   /**< the shunt both ends last agreed on, µΩ */
    uint16_t     shunt_age_s;    /**< s since the node's own change, RCNODE_AGE_NONE none */
    char         name[RCNODE_UNAME_MAX + 1u]; /**< NUL-terminated, "" none */
} rcnode_config_t;

/** @brief What rcnode_shunt_sync() tells the central to do. */
typedef enum {
    RCNODE_SYNC_KEEP = 0,  /**< both ends hold the agreed value */
    RCNODE_SYNC_WRITE,     /**< the panel's value is newer: SHUNT with it */
    RCNODE_SYNC_ADOPT,     /**< the node's value is newer: the panel takes it */
    RCNODE_SYNC_ACK,       /**< both hold the same new value: SHUNT with it marks it agreed */
} rcnode_sync_t;

/** @brief Any packet, by type. */
typedef struct {
    rcnode_hdr_t hdr;  /**< the header; hdr.type selects the member */
    union {
        rcnode_pwr_win_t    win;     /**< RCNODE_T_PWR_WIN */
        rcnode_pwr_acc_t    acc;     /**< RCNODE_T_PWR_ACC */
        rcnode_pwr_run_t    run;     /**< RCNODE_T_PWR_RUN */
        rcnode_bal_report_t bal;     /**< RCNODE_T_BAL_REPORT */
        rcnode_status_t     status;  /**< RCNODE_T_STATUS */
        rcnode_info_t       info;    /**< RCNODE_T_INFO */
        rcnode_config_t     config;  /**< RCNODE_T_CONFIG */
    } u;  /**< the decoded packet, its own hdr included */
} rcnode_pkt_t;

/* ------------------------------------------------------------- codec */

/**
 * @brief The 128-bit UUID of a service or characteristic, least significant
 * byte first as on the air and as NimBLE's BLE_UUID128_INIT takes it.
 *
 * @param id RCNODE_ID_*.
 * @param out Receives the 16 bytes.
 */
void rcnode_uuid(uint16_t id, uint8_t out[16]);

/**
 * @brief The length of a packet type.
 *
 * @param type RCNODE_T_*.
 * @return The length in bytes, 0 for an unknown type.
 */
size_t rcnode_len(uint8_t type);

/**
 * @brief Encode a power window.
 *
 * @param p The window; p->hdr.type is ignored and written as RCNODE_T_PWR_WIN.
 * @param out The buffer.
 * @param cap The buffer's size.
 * @return RCNODE_PWR_WIN_LEN, or 0 when @p cap is smaller or an argument NULL.
 */
size_t rcnode_enc_pwr_win(const rcnode_pwr_win_t *p, uint8_t *out, size_t cap);

/**
 * @brief Encode the accumulators; charge and energy are cut to 40 bits.
 *
 * @param p The accumulators.
 * @param out The buffer.
 * @param cap The buffer's size.
 * @return RCNODE_PWR_ACC_LEN, or 0 when @p cap is smaller or an argument NULL.
 */
size_t rcnode_enc_pwr_acc(const rcnode_pwr_acc_t *p, uint8_t *out, size_t cap);

/**
 * @brief Encode the run extremes.
 *
 * @param p The extremes.
 * @param out The buffer.
 * @param cap The buffer's size.
 * @return RCNODE_PWR_RUN_LEN, or 0 when @p cap is smaller or an argument NULL.
 */
size_t rcnode_enc_pwr_run(const rcnode_pwr_run_t *p, uint8_t *out, size_t cap);

/**
 * @brief Encode a balance report.  The phase is written modulo
 * RCNODE_PHASE_STEPS, so 3600 goes out as 0.
 *
 * @param p The report.
 * @param out The buffer.
 * @param cap The buffer's size.
 * @return RCNODE_BAL_REPORT_LEN, or 0 when @p cap is smaller, an argument
 * NULL or the quality over 100.
 */
size_t rcnode_enc_bal_report(const rcnode_bal_report_t *p, uint8_t *out,
                             size_t cap);

/**
 * @brief Encode a status.
 *
 * @param p The status.
 * @param out The buffer.
 * @param cap The buffer's size.
 * @return RCNODE_STATUS_LEN, or 0 when @p cap is smaller or an argument NULL.
 */
size_t rcnode_enc_status(const rcnode_status_t *p, uint8_t *out, size_t cap);

/**
 * @brief Encode the info.
 *
 * @param p The info.
 * @param out The buffer.
 * @param cap The buffer's size.
 * @return RCNODE_INFO_LEN, or 0 when @p cap is smaller or an argument NULL.
 */
size_t rcnode_enc_info(const rcnode_info_t *p, uint8_t *out, size_t cap);

/**
 * @brief Decode any packet.
 *
 * The length must be the type's exactly.  A balance report's phase of 3600
 * or more, or a quality over 100, a status's sensor state past the last, an
 * info's role or protocol of 0, a config's name that rcnode_uname_ok()
 * refuses or its address that rcnode_i228_ok() refuses, are refused as
 * RCNODE_ERR_RANGE.  Flag bits
 * that are not defined are kept and not refused.
 *
 * @param b The bytes.
 * @param n Their count.
 * @param out Receives the packet; on an error it is left as it was.
 * @return RCNODE_OK or the reason.
 */
rcnode_err_t rcnode_decode(const uint8_t *b, size_t n, rcnode_pkt_t *out);

/**
 * @brief Encode a control write.
 *
 * @param c The write.
 * @param out The buffer, RCNODE_CTL_MAX bytes or more.
 * @param cap The buffer's size.
 * @return The bytes written, 0 for an unknown operation, a value out of
 * range, a small buffer or a NULL argument.
 */
size_t rcnode_enc_ctl(const rcnode_ctl_t *c, uint8_t *out, size_t cap);

/**
 * @brief Decode a control write.
 *
 * @param b The bytes.
 * @param n Their count.
 * @param out Receives the write; on an error it is left as it was.
 * @return RCNODE_OK; RCNODE_ERR_TYPE for an unknown operation,
 * RCNODE_ERR_LEN for another length than the operation's,
 * RCNODE_ERR_RANGE for a RUN tag of 0 (the tag before any RUN), a blade
 * count, an rpm or a shunt out of range, a language other than
 * RCNODE_LANG_EN and RCNODE_LANG_DE, a name rcnode_uname_ok() refuses, or
 * an INA228 address rcnode_i228_ok() refuses.
 */
rcnode_err_t rcnode_dec_ctl(const uint8_t *b, size_t n, rcnode_ctl_t *out);

/**
 * @brief Whether a role takes a control operation.
 *
 * @param role RCNODE_ROLE_*.
 * @param op RCNODE_OP_*.
 * @return true for RUN, SLEEP, OFF, CONFIG and NAME on both roles, LANG,
 * SHUNT and I228 on the power role, and BLADES and RPM on the balance role.
 */
bool rcnode_ctl_for_role(uint8_t role, uint8_t op);

/* ------------------------------------------------- the node's settings */

/**
 * @brief Whether a name is one a node keeps: 0 to RCNODE_UNAME_MAX (16)
 * characters of A to Z, 0 to 9, space, "-", "_" and ".", the characters of
 * the panel's name keyboard (ui_textkey.h), and no space at either end.
 * "" is no name.
 *
 * @param s The name, NUL-terminated; NULL is refused.
 * @return true when the node keeps it.
 */
bool rcnode_uname_ok(const char *s);

/**
 * @brief Whether an INA228 address setting is one a node keeps.
 *
 * @param a The setting.
 * @return true for RCNODE_I228_AUTO and RCNODE_I228_MIN to RCNODE_I228_MAX.
 */
bool rcnode_i228_ok(uint8_t a);

/**
 * @brief Encode the node's settings.
 *
 * @param p The settings; p->hdr.type is ignored.
 * @param out The buffer.
 * @param cap The buffer's size.
 * @return RCNODE_CONFIG_LEN; 0 for a small buffer, a NULL argument, a name
 * rcnode_uname_ok() refuses or an address rcnode_i228_ok() refuses.
 */
size_t rcnode_enc_config(const rcnode_config_t *p, uint8_t *out, size_t cap);

/**
 * @brief The last change of the shunt wins: what the central does with the
 * panel's setting and the node's RCNODE_T_CONFIG.
 *
 * A side changed the shunt when its value differs from the agreed one.
 * Only the panel changed: WRITE.  Only the node changed: ADOPT.  Both
 * hold the same value: ACK when it is not the agreed one yet, else KEEP.
 * Both changed to different values: the younger change wins, by the ages
 * at the moment of the decision.  An age that is not known (the node's
 * RCNODE_AGE_NONE, the panel's UINT32_MAX: a change from before its own
 * start) is older than any known one.  Equal ages and two unknown ages go
 * to the node: its value is the one its part measures with.  A known
 * panel age saturates at 65,534s, where the node's shunt_age_s does, so
 * two changes older than 18.2h count as equal.
 *
 * @param panel        The panel's setting, µΩ.
 * @param node         The node's shunt in force, µΩ.
 * @param agreed       The node's shunt_agreed, µΩ.
 * @param node_age_s   The node's shunt_age_s.
 * @param panel_age_ms The time since the panel's setting changed, ms;
 *                     UINT32_MAX for not known.
 * @return The action.
 */
rcnode_sync_t rcnode_shunt_sync(uint16_t panel, uint16_t node,
                                uint16_t agreed, uint16_t node_age_s,
                                uint32_t panel_age_ms);

/* --------------------------------------------------------- arithmetic */

/**
 * @brief Packets lost between two sequence numbers.
 *
 * @param prev The last sequence number taken.
 * @param now The sequence number just taken.
 * @return 0 for consecutive numbers, up to 255 across the wrap.
 */
uint8_t rcnode_seq_lost(uint8_t prev, uint8_t now);

/**
 * @brief The signed time from @p earlier to @p later on the node's clock,
 * across the wrap at 2^32µs.
 *
 * @param later The later t_node.
 * @param earlier The earlier t_node.
 * @return µs; correct while the two are less than 2^31µs (35.8min) apart.
 */
int32_t rcnode_t_diff(uint32_t later, uint32_t earlier);

/**
 * @brief A 40-bit two's complement value, sign-extended.
 *
 * @param raw The value in bits 39-0; higher bits are ignored.
 * @return -2^39 to 2^39 - 1.
 */
int64_t rcnode_sext40(uint64_t raw);

/**
 * @brief ENERGY's increase from @p prev to @p now, across the 40-bit wrap.
 *
 * @param prev The earlier ENERGY.
 * @param now The later ENERGY.
 * @return The steps counted, 0 to 2^40 - 1.
 */
uint64_t rcnode_energy_delta(uint64_t prev, uint64_t now);

/**
 * @brief CHARGE's change from @p prev to @p now, across the 40-bit wrap.
 *
 * @param prev The earlier CHARGE.
 * @param now The later CHARGE.
 * @return The signed steps, -2^39 to 2^39 - 1.
 */
int64_t rcnode_charge_delta(uint64_t prev, uint64_t now);

/**
 * @brief Whether the accumulators restarted from @p prev to @p now: the
 * panel's rule before it takes a difference.
 *
 * A restart is a change of epoch, or a fall in ENERGY under the same epoch.
 * ENERGY only rises while the part counts (it integrates the magnitude of
 * the power), and 2^40 steps of 20mJ, 22GJ, are no wrap a bench reaches, so
 * a fall is a reset the node had not seen when it read the registers.
 * CHARGE is not used: it falls whenever the current flows back into the
 * pack.  Without RCNODE_AF_ENERGY_OK in both packets there is nothing to
 * compare and the answer is false.
 *
 * @param prev The earlier accumulator packet.
 * @param now The later one.
 * @return true when the counts restarted: the panel takes @p now as a new
 * start, not as a difference.
 */
bool rcnode_acc_restarted(const rcnode_pwr_acc_t *prev,
                          const rcnode_pwr_acc_t *now);

/**
 * @brief Whether a notification of @p len bytes fits a link's ATT
 * (attribute protocol) MTU (maximum transmission unit): the MTU less the
 * 3-byte header.  A longer one is cut by the stack without an error.
 *
 * @param mtu The link's ATT MTU; 0 for no link.
 * @param len The packet's length.
 * @return true when it fits.
 */
bool rcnode_fits_mtu(uint16_t mtu, size_t len);

/**
 * @brief A bus voltage in 2mV steps, rounded, clamped to 0 and 65535.
 *
 * @param uv The voltage, µV.
 * @return The field value.
 */
uint16_t rcnode_v_field(int32_t uv);

/* ---------------------------------------------- power window reducer */

/** @brief One sample of the power node: a current and a bus voltage. */
typedef struct {
    bool    ok;      /**< both reads succeeded */
    int8_t  clip;    /**< the current: -1 at the bottom, 1 at the top, 0 a value */
    int32_t i_ua;    /**< µA, when clip is 0 */
    int32_t v_uv;    /**< µV */
} rcnode_sample_t;

/** @brief The window being filled. */
typedef struct {
    uint64_t idx;     /**< the window's number: t / RCNODE_WIN_US */
    bool     open;    /**< a sample has been taken */
    uint8_t  flags;   /**< RCNODE_WF_* gathered */
    uint8_t  n;       /**< samples read */
    uint8_t  ni;      /**< current samples that are not clipped */
    int64_t  i_sum;   /**< µA */
    int32_t  i_max;   /**< µA */
    uint64_t v_sum;   /**< 2mV steps */
    uint16_t v_min;   /**< 2mV steps */
} rcnode_win_t;

/**
 * @brief An empty reducer.
 *
 * @param w The reducer.
 */
void rcnode_win_init(rcnode_win_t *w);

/**
 * @brief Take one sample.  A sample in a later window closes the window
 * being filled first; windows without a sample between them are not sent.
 *
 * @param w The reducer.
 * @param t_us The sample's time, µs on a 64-bit clock that does not wrap.
 * @param s The sample.
 * @param keep_flags RCNODE_WF_ONLINE and RCNODE_WF_RUN as they stand now,
 * written into the window that closes; other bits are ignored.
 * @param out Receives the closed window, with t_node its end cut to 32 bits
 * and seq 0 (the sender numbers it).
 * @return true when a window closed into @p out.
 */
bool rcnode_win_add(rcnode_win_t *w, uint64_t t_us, const rcnode_sample_t *s,
                    uint8_t keep_flags, rcnode_pwr_win_t *out);

/** @brief The extremes since the RUN marker. */
typedef struct {
    uint8_t  tag;      /**< the last RUN tag */
    uint8_t  flags;    /**< RCNODE_RF_* */
    int32_t  i_max;    /**< µA */
    uint16_t v_min;    /**< 2mV steps */
    uint32_t windows;  /**< windows taken */
} rcnode_run_t;

/**
 * @brief Restart the extremes under a new tag.
 *
 * @param r The extremes.
 * @param tag The RUN write's tag.
 */
void rcnode_run_mark(rcnode_run_t *r, uint8_t tag);

/**
 * @brief Take a closed window into the extremes.
 *
 * @param r The extremes.
 * @param w The window.
 */
void rcnode_run_take(rcnode_run_t *r, const rcnode_pwr_win_t *w);

/**
 * @brief One sampler step's part of the extremes: the window that closed in
 * the step first, then a RUN marker taken in the step.  A window closed
 * with the marker is a window from before it and never enters the new run.
 *
 * @param r The extremes.
 * @param closed The window that closed in the step, NULL for none.
 * @param tag The RUN tag taken in the step, 1 to 255; 0 or less for none.
 */
void rcnode_run_step(rcnode_run_t *r, const rcnode_pwr_win_t *closed, int tag);

/* ------------------------------------------------------- advertising */

/** @brief The company identifier in the manufacturer data: 0xFFFF, the
 *  value the Bluetooth SIG leaves for tests and for no registered company. */
#define RCNODE_COMPANY 0xFFFFu
/** @brief A name: "rcbench-pwr-XXXX" + NUL. */
#define RCNODE_NAME_MAX 17u
/** @brief Advertising data the node sends, bytes: flags, the 128-bit service
 *  UUID, manufacturer data. */
#define RCNODE_ADV_LEN  29u
/** @brief The scan response: the complete name, rcnode_name()'s or the
 *  node's own of RCNODE_UNAME_MAX (16) characters at most. */
#define RCNODE_RSP_LEN  18u

/**
 * @brief The node's name: "rcbench-pwr-" or "rcbench-bal-" and the two
 * bytes of the address printed last, addr[1] and addr[0], in upper-case
 * hex ("7C:DF:A1:12:34:56" gives "rcbench-pwr-3456").
 *
 * @param role RCNODE_ROLE_*.
 * @param addr The address, least significant byte first.
 * @param out Receives the name, RCNODE_NAME_MAX bytes.
 * @return true; false for an unknown role or a NULL argument.
 */
bool rcnode_name(uint8_t role, const uint8_t addr[6], char out[RCNODE_NAME_MAX]);

/**
 * @brief The advertising data: flags 0x06, the service's 128-bit UUID in a
 * complete list, and the manufacturer data RCNODE_COMPANY, 'r', 'c',
 * RCNODE_PROTO, role.
 *
 * The UUID travels in the advertising itself, as the M5Stack Dial's does: a
 * central that scans with duplicate filtering lists the node from its first
 * report, whether or not a scan response merges.
 *
 * @param role RCNODE_ROLE_*.
 * @param addr The address, least significant byte first.
 * @param out Receives RCNODE_ADV_LEN bytes.
 * @param cap The buffer's size.
 * @return RCNODE_ADV_LEN, 0 for an unknown role, a small buffer or a NULL argument.
 */
size_t rcnode_adv_build(uint8_t role, const uint8_t addr[6], uint8_t *out,
                        size_t cap);

/**
 * @brief The scan response: the complete name (rcnode_name()).
 *
 * @param role RCNODE_ROLE_*.
 * @param addr The address, least significant byte first.
 * @param out Receives RCNODE_RSP_LEN bytes.
 * @param cap The buffer's size.
 * @return RCNODE_RSP_LEN, 0 for an unknown role, a small buffer or a NULL
 * argument.
 */
size_t rcnode_rsp_build(uint8_t role, const uint8_t addr[6], uint8_t *out,
                        size_t cap);

/**
 * @brief The scan response with the node's own name: the complete name is
 * @p uname when rcnode_uname_ok() takes it and it is not "", else
 * rcnode_name()'s.  A central built before the node's own name lists the
 * node by the manufacturer data and shows the name as it comes.
 *
 * @param role RCNODE_ROLE_*.
 * @param addr The address, least significant byte first.
 * @param uname The node's own name, NULL or "" for none.
 * @param out Receives 2 bytes and the name's length, RCNODE_RSP_LEN at most.
 * @param cap The buffer's size, RCNODE_RSP_LEN or more.
 * @return The bytes written, 0 for an unknown role, a small buffer or a
 * NULL argument.
 */
size_t rcnode_rsp_build_named(uint8_t role, const uint8_t addr[6],
                              const char *uname, uint8_t *out, size_t cap);

/**
 * @brief The role a node's name names: "rcbench-pwr-" or "rcbench-bal-" at
 * its start.
 *
 * @param name The name, NUL-terminated; NULL names none.
 * @return RCNODE_ROLE_*, 0 for another name.
 */
uint8_t rcnode_name_role(const char *name);

/**
 * @brief The role a report's advertising or scan response names: the
 * manufacturer data first, else a name "rcbench-pwr-" or "rcbench-bal-",
 * else the service UUID with no role (0 returned with true).
 *
 * @param ad The AD (advertising data) bytes.
 * @param len Their count.
 * @param role Receives RCNODE_ROLE_*, or 0 when only the service UUID tells.
 * @return true when the report is an rcbench node's; false otherwise, and
 * for a structure whose length runs past the end.
 */
bool rcnode_adv_match(const uint8_t *ad, size_t len, uint8_t *role);

/**
 * @brief The role a report's manufacturer data names, without the name's
 * or the service UUID's fallback of rcnode_adv_match(): a scan lists a node
 * by it, never by a name alone.
 *
 * @param ad The AD (advertising data) bytes.
 * @param len Their count.
 * @return RCNODE_ROLE_POWER or RCNODE_ROLE_BALANCE; 0 without the rcnode
 * manufacturer data, for a structure whose length runs past the end, or
 * for NULL.
 */
uint8_t rcnode_adv_maker_role(const uint8_t *ad, size_t len);

#ifdef __cplusplus
}
#endif
