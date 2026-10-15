/*
 * The serial bus servo protocols the BUS page of the SERVO screen offers,
 * as one table.  The page draws and asks through this table and holds no
 * protocol's numbers of its own: a protocol is a row here and a protocol
 * number of the BUS link page (LINK_BUS_PROTO_*).
 *
 * One row now: Feetech SCS and STS (protocols/feetech).  A series is a
 * per-servo setting.  PING cannot tell the series apart, so the operator
 * names it.
 *
 * Pure C, no state.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The registers the page reads and writes, by what they do. */
typedef enum {
    BUS_R_ID = 0,
    BUS_R_BAUD,
    BUS_R_MIN_ANGLE,
    BUS_R_MAX_ANGLE,
    BUS_R_MODE,
    BUS_R_LOCK,
    BUS_R_TORQUE,
    BUS_R_ACC,
    BUS_R_GOAL,
    BUS_R_SPEED,
    BUS_R_COUNT
} bus_role_t;

/** The error flags of a servo's status, by what they mean. */
typedef enum {
    BUS_E_VOLTAGE = 0,
    BUS_E_SENSOR,
    BUS_E_TEMPERATURE,
    BUS_E_CURRENT,
    BUS_E_OVERLOAD,
    BUS_E_COUNT
} bus_error_t;

typedef struct {
    uint8_t  addr;
    uint8_t  size;        /**< bytes: 1 or 2                              */
    uint16_t min, max;    /**< the values a write takes                   */
    bool     eeprom;      /**< written only by an EEPROM write            */
} bus_reg_t;

/** A servo's live readings. */
typedef struct {
    uint16_t position;    /**< steps                                      */
    int16_t  speed;       /**< steps/s                                    */
    uint16_t load;        /**< 0.1 % of full torque                       */
    uint16_t mv;          /**< supply at the servo, mV                    */
    uint8_t  temp_c;      /**< deg C                                      */
    uint8_t  status;      /**< the protocol's error bits                  */
    bool     moving;
    bool     has_current;
    uint32_t current_ma;
} bus_live_t;

typedef struct {
    const char *name;              /**< as the maker writes it, uppercase */
    uint8_t     link_proto;        /**< LINK_BUS_PROTO_*                  */
    uint8_t     id_max;            /**< the highest servo ID              */
    uint8_t     baud_count;
    uint8_t     baud_default;      /**< index of the factory baud rate    */
    uint32_t  (*baud)(unsigned index);
    uint8_t     series_count;
    uint8_t     series_default;
    const char *const *series_names;
    /** False where the series has no such register. */
    bool      (*reg)(uint8_t series, bus_role_t role, bus_reg_t *out);
    uint16_t  (*position_max)(uint8_t series);
    uint8_t   (*live_bytes)(uint8_t series);
    bool      (*live)(uint8_t series, const uint8_t *bytes, unsigned n,
                      bus_live_t *out);
    void      (*put16)(uint8_t series, uint8_t *at, uint16_t v);
    uint16_t  (*get16)(uint8_t series, const uint8_t *at);
    uint8_t     mode_count;        /**< values of BUS_R_MODE              */
    const char *const *mode_names;
    /** The registers that tell whether a goal position moves the servo to
     *  a position, or runs it as a motor: returns the bytes to read, 0 for
     *  none; @p addr gets the first register. */
    uint8_t   (*goal_check)(uint8_t series, uint8_t *addr);
    /** Whether the @p n bytes goal_check() named show position control. */
    bool      (*goal_mode)(uint8_t series, const uint8_t *bytes, unsigned n);
    uint8_t     error_bit[BUS_E_COUNT];  /**< 0: the protocol has none    */
} bus_proto_t;

unsigned bus_proto_count(void);

/** Row @p i; NULL past the last. */
const bus_proto_t *bus_proto_get(unsigned i);

/** The row of link protocol @p link_proto; NULL for none. */
const bus_proto_t *bus_proto_of(uint8_t link_proto);

/**
 * The bytes of a goal-position write for @p series of @p p: the position,
 * the speed and, where the series has it, the acceleration, from the
 * first register that write starts at.  Returns the bytes, 0 for values
 * outside the registers' ranges; @p addr gets the first register.
 */
unsigned bus_proto_goal(const bus_proto_t *p, uint8_t series,
                        uint16_t position, uint16_t speed, uint8_t acc,
                        uint8_t *addr, uint8_t *out, unsigned room);

#ifdef __cplusplus
}
#endif
