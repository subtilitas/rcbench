/**
 * @file
 * @brief What sense_page.c and sense_map.c share: the SENSE set-up's checks and
 *        its taking, and the map's own helpers the SENSE page calls.
 *
 * Not for any other module.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sense_page.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief @p a / @p d to the nearest, halves away from zero; @p d positive.
 *
 * @param a The dividend.
 * @param d The divisor, positive.
 * @return a / d, rounded to the nearest, halves away from zero.
 */
static inline int64_t div_round(int64_t a, int64_t d)
{
    return (a >= 0) ? (a + d / 2) / d : -((-a + d / 2) / d);
}

/**
 * @brief A signed register: two's complement, held to -32767 .. 32767.
 *
 * @param v The value in register units.
 * @return The value held to -32767 to 32767, as two's complement.
 */
static inline uint16_t reg_i16(int64_t v)
{
    if (v > 32767) {
        v = 32767;
    } else if (v < -32767) {
        v = -32767;
    }
    return (uint16_t)(int16_t)v;
}

/**
 * @brief An unsigned register, held to 0 .. 65535.
 *
 * @param v The value in register units.
 * @return The value held to 0 to 65535.
 */
static inline uint16_t reg_u16(int64_t v)
{
    if (v < 0) {
        return 0u;
    }
    return (v > 65535) ? 65535u : (uint16_t)v;
}

/** @brief A SENSE set-up @p next (LINK_SN_CONFIG_COUNT registers) judged as a
 *        write is: values, pins, addresses and channels; 0 or the refusal.
 *
 * Not
 *  the bank's state: the caller asks that.
 *
 * @param p     The page.
 * @param next  The set-up to judge, LINK_SN_CONFIG_COUNT registers.
 * @param o     The outputs model.
 * @param taken Bit n: pin n is held by another page.
 * @return 0, or LINK_NACK_BAD_VALUE for a refused set-up.
 */
uint8_t sense_cfg_check(const sense_page_t *p, const uint16_t *next,
                        const outputs_t *o, uint64_t taken);

/** @brief @p next in force: the generation moves, and every reading of the
 *        set-up
 *  before it goes (sense_page_forget()).
 *
 * @param p    The page.
 * @param next The set-up, LINK_SN_CONFIG_COUNT registers.
 */
void sense_cfg_take(sense_page_t *p, const uint16_t *next);

/**
 * @brief Every register core 1 fills, on both pages and the map's, cleared.
 *
 * @param p The page.
 */
void sense_page_forget(sense_page_t *p);

/**
 * @brief Whether the map @p map fits an INA3221 at @p addr.
 *
 * @param map  The map's registers.
 * @param addr The INA3221 address.
 * @return true when the map fits an INA3221 at addr.
 */
bool sense_map_on_addr(const uint16_t *map, uint16_t addr);

/** @brief Whether a channel the map reads watches an output channel, a
 *  programming port or a board's rail (LINK_SM_WATCH_RAIL_A to
 *  LINK_SM_WATCH_PROG_5V): the map in force's LINK_CAP_SERVO_SENSE.
 *
 * @param p The page.
 * @return true when a channel the map reads watches an output channel, a
 *         programming port or a rail.
 */
bool sense_map_watches(const sense_page_t *p);

/** @brief Core 1's counts of windows over each channel's limit in @p s into
 *  OVER and OVER_COUNT: what each gained since the last snapshot taken.
 *
 * @param p The page.
 * @param s The snapshot.
 */
void sense_limit_judge(sense_page_t *p, const sense_snap_t *s);

/**
 * @brief The map's kept registers as the schedule's INA3221 set-up.
 *
 * @param map The map's registers.
 * @param out Receives the INA3221 set-up of the schedule.
 */
void sense_map_cfg(const uint16_t *map, sense_i3221_cfg_t *out);

/** @brief The snapshot's per-part state into SENSE_MAP's read-only registers,
 *        and
 *  every channel's window into SENSE_WIN_A and SENSE_WIN_B.
 *
 * @param p The page, updated in place.
 * @param s The snapshot.
 */
void sense_map_publish(sense_page_t *p, const sense_snap_t *s);

#ifdef __cplusplus
}
#endif
