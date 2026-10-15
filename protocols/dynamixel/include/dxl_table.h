/**
 * @file dxl_table.h
 * @brief The X-series control table of ROBOTIS Dynamixel servos under
 *        Protocol 2.0: addresses, sizes, EEPROM or RAM, units, ranges, the
 *        model numbers, the baud rate register and the restore of an EEPROM
 *        backup.
 *
 * Implements the published control table of ROBOTIS Dynamixel X-series
 * servos.  ROBOTIS and Dynamixel are names or trademarks of their owner;
 * they are used here only to say which servos this module works with
 * (README.md, "Trademarks").
 *
 * The table is the XL430-W250 table with the 3 current fields of the
 * XM430 (Current Limit 38, Goal Current 102, Present Current 126) beside
 * it.  Address 126 is Present Load in 0.1 % on a model without current
 * sensing and Present Current in 2.69 mA on one with it; dxl_model_t says
 * which.  Other Protocol 2.0 families (MX 2.0, PRO, P, Y) have other
 * tables and are not described.
 *
 * Addresses 0 to 63 are EEPROM: the servo keeps them over a power cycle,
 * takes a write only while Torque Enable (64) is 0, and every write wears
 * its flash.  The write endurance is not published.  Addresses 64 and up
 * are RAM and return to their defaults at power-up.
 *
 * The module includes nothing outside the C standard headers and holds no
 * state.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DXL_TABLE_H
#define DXL_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** EEPROM is addresses 0 to DXL_EEPROM_SIZE - 1. */
#define DXL_EEPROM_SIZE 64u
/** The table ends with Present Temperature at 146. */
#define DXL_TABLE_END   147u

typedef enum {
    DXL_AREA_EEPROM = 0,
    DXL_AREA_RAM,
} dxl_area_t;

typedef enum {
    DXL_ACC_R = 0,  /**< read only */
    DXL_ACC_RW,
} dxl_access_t;

/** The unit of one raw step. */
typedef enum {
    DXL_UNIT_RAW = 0,   /**< a number, a mode or a bit field */
    DXL_UNIT_PULSE,     /**< position: 4096 per turn, 0.088 deg */
    DXL_UNIT_RPM_229,   /**< velocity: 0.229 rev/min */
    DXL_UNIT_ACC_214,   /**< acceleration: 214.577 rev/min^2 */
    DXL_UNIT_PERMILLE,  /**< load: 0.1 % */
    DXL_UNIT_MA_269,    /**< current: 2.69 mA */
    DXL_UNIT_PWM_113,   /**< PWM: 0.113 % */
    DXL_UNIT_DECIVOLT,  /**< voltage: 0.1 V */
    DXL_UNIT_DEG_C,     /**< temperature: 1 deg C */
    DXL_UNIT_US_2,      /**< time: 2 us */
    DXL_UNIT_MS_20,     /**< time: 20 ms */
    DXL_UNIT_MS,        /**< time: 1 ms */
    DXL_UNIT_BAUD,      /**< an index into the baud rate table */
} dxl_unit_t;

/** Field flags. */
#define DXL_F_SIGNED  0x01u /**< two's complement */
#define DXL_F_LINK    0x02u /**< ID, baud rate, protocol: the servo answers
                                 differently after a write */
#define DXL_F_CURRENT 0x04u /**< exists on a model with current sensing only */
#define DXL_F_NO_WRITE 0x08u /**< writable on the servo, never written by
                                  this module: Protocol Type, whose value 1
                                  ends Protocol 2.0 */

typedef enum {
    DXL_R_MODEL_NUMBER = 0,
    DXL_R_FIRMWARE,
    DXL_R_ID,
    DXL_R_BAUD,
    DXL_R_RETURN_DELAY,
    DXL_R_DRIVE_MODE,
    DXL_R_OPERATING_MODE,
    DXL_R_SHADOW_ID,
    DXL_R_PROTOCOL,
    DXL_R_HOMING_OFFSET,
    DXL_R_MOVING_THRESHOLD,
    DXL_R_TEMPERATURE_LIMIT,
    DXL_R_MAX_VOLTAGE,
    DXL_R_MIN_VOLTAGE,
    DXL_R_PWM_LIMIT,
    DXL_R_CURRENT_LIMIT,
    DXL_R_VELOCITY_LIMIT,
    DXL_R_MAX_POSITION,
    DXL_R_MIN_POSITION,
    DXL_R_STARTUP,
    DXL_R_SHUTDOWN,
    DXL_R_TORQUE_ENABLE,
    DXL_R_LED,
    DXL_R_STATUS_RETURN,
    DXL_R_REGISTERED,
    DXL_R_HW_ERROR,
    DXL_R_BUS_WATCHDOG,
    DXL_R_GOAL_PWM,
    DXL_R_GOAL_CURRENT,
    DXL_R_GOAL_VELOCITY,
    DXL_R_PROFILE_ACCELERATION,
    DXL_R_PROFILE_VELOCITY,
    DXL_R_GOAL_POSITION,
    DXL_R_REALTIME_TICK,
    DXL_R_MOVING,
    DXL_R_MOVING_STATUS,
    DXL_R_PRESENT_PWM,
    DXL_R_PRESENT_LOAD,     /**< Present Current on a model with sensing */
    DXL_R_PRESENT_VELOCITY,
    DXL_R_PRESENT_POSITION,
    DXL_R_PRESENT_VOLTAGE,
    DXL_R_PRESENT_TEMPERATURE,
    DXL_R_COUNT,
} dxl_reg_id_t;

typedef struct {
    const char *name;
    uint8_t addr;
    uint8_t size;    /**< 1, 2 or 4 bytes */
    uint8_t area;    /**< dxl_area_t */
    uint8_t access;  /**< dxl_access_t */
    uint8_t unit;    /**< dxl_unit_t */
    uint8_t flags;   /**< DXL_F_* */
    int32_t min;     /**< accepted range of a write; for a read-only field */
    int32_t max;     /**< the range of its values */
} dxl_reg_t;

/** The field, or NULL for an id out of range. */
const dxl_reg_t *dxl_reg(dxl_reg_id_t id);

/** The field that starts at @p addr, or NULL. */
const dxl_reg_t *dxl_reg_at(uint16_t addr);

/** Read a field's value from @p table, which holds the bytes from address
 *  @p base on, @p n of them; false when the field lies outside them. */
bool dxl_reg_get(const dxl_reg_t *r, const uint8_t *table, uint16_t base,
                 size_t n, int32_t *value);

/** What a write of @p len bytes at @p addr does. */
typedef enum {
    DXL_WR_RAM = 0,     /**< only RAM fields: written freely */
    DXL_WR_EEPROM,      /**< at least one EEPROM field: kept over a power
                             cycle, wears flash, needs torque off */
    DXL_WR_READ_ONLY,   /**< touches a read-only field */
    DXL_WR_UNKNOWN,     /**< touches an address no field has, part of a
                             field, or a field this module never writes */
} dxl_wclass_t;

/** Classify a write: whole writable fields, back to back, are RAM or
 *  EEPROM; anything else is refused by class. */
dxl_wclass_t dxl_write_class(uint16_t addr, uint16_t len);

/** @p value is in the field's range and the field may be written. */
bool dxl_reg_value_ok(const dxl_reg_t *r, int32_t value);

/* Hardware Error Status (70) and Shutdown (63) bits, as the XM430-W350
 * page states them.  The XL430-W250 page read in the research names 0x10
 * for the input voltage error; which is right for the XL430 is unresolved. */
#define DXL_HW_INPUT_VOLTAGE 0x01u
#define DXL_HW_OVERHEATING   0x04u
#define DXL_HW_ENCODER       0x08u
#define DXL_HW_SHOCK         0x10u
#define DXL_HW_OVERLOAD      0x20u

/* Baud Rate register (8): index 0 to 7. */
#define DXL_BAUD_COUNT 8u
/** The rate of index @p index in bit/s, 0 for an index above 7. */
uint32_t dxl_baud_rate(uint8_t index);
/** The index of @p rate, or -1. */
int dxl_baud_index(uint32_t rate);

/** A model this module knows by its Model Number (address 0). */
typedef struct {
    uint16_t number;
    const char *name;
    bool current;  /**< 126 is Present Current; 38, 102 exist */
} dxl_model_t;

/** The model of @p number, or NULL: a caller shows the number. */
const dxl_model_t *dxl_model_find(uint16_t number);

/* Units to display values, integer arithmetic, rounded to nearest. */
/** Position in pulses to millidegrees: 360000 / 4096 = 87.890625 each. */
int32_t dxl_pulse_to_mdeg(int32_t pulse);
/** Millidegrees to the nearest pulse. */
int32_t dxl_mdeg_to_pulse(int32_t mdeg);
/** Present Current in 2.69 mA steps to mA. */
int32_t dxl_current_ma(int32_t raw);
/** Voltage in 0.1 V steps to mV. */
int32_t dxl_voltage_mv(int32_t raw);
/** Return Delay Time (9) in us: 2 us a step. */
uint32_t dxl_return_delay_us(uint8_t raw);

/** One write of a restore: a whole field. */
typedef struct {
    uint8_t  addr;
    uint8_t  size;
    uint32_t value;
} dxl_wr_t;

/** Writes dxl_restore_plan() gives at most: one per writable EEPROM field. */
#define DXL_RESTORE_MAX 20u

/** dxl_restore_plan()'s @p refused when @p max is too small. */
#define DXL_RESTORE_NO_ROOM (-2)

/**
 * The writes that take a servo's EEPROM from @p now back to @p backup, each
 * DXL_EEPROM_SIZE bytes from address 0.  Only writable EEPROM fields that
 * differ are written, never Protocol Type.  ID goes after every other
 * field and Baud Rate last: after the ID write the servo answers at the
 * backup's ID, after the baud write at the backup's rate.  Returns the
 * count; @p *refused is -1.  Returns 0 with @p *refused set to a field's
 * address when the backup holds a value out of that field's range, and to
 * DXL_RESTORE_NO_ROOM when more than @p max writes are due.  @p refused may
 * be NULL.
 */
size_t dxl_restore_plan(const uint8_t *backup, const uint8_t *now,
                        dxl_wr_t *out, size_t max, int *refused);

#ifdef __cplusplus
}
#endif

#endif /* DXL_TABLE_H */
