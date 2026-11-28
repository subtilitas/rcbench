/**
 * @file
 * @brief The Dial's battery, pure: the cell's voltage as the faces' share,
 *        and the Guition JC3636K518C's supply reading turned into USB, a
 *        cell voltage, LOW and EMPTY.
 *
 * The share is linear from 3,300mV (0%) to 4,200mV (100%), clamped.
 *
 * The K518 reads its supply on GPIO1 through a 10k/10k divider (x2) on the
 * switched rail behind its power path: with USB the rail carries VBUS less
 * a Schottky diode, so the reading is not the cell's.  dk518_supply_step()
 * takes one reading a second (16 samples, the two extremes dropped, the
 * rest averaged, times 2):
 * - USB: the console's USB host is there, or the rail reads above
 *   4,450mV (DK518_USB_MV).  The cell is then unknown, LOW and EMPTY end.
 * - Below 2,500mV (DK518_NO_CELL_MV): no plausible cell, unknown.
 * - Else the reading is the cell's.  LOW sets after 3 readings in a row
 *   below 3,500mV and ends after 3 at 3,600mV or more; EMPTY the same at
 *   3,350mV and 3,450mV.  A reading between the two leaves the count.
 *
 * Pure C: no clock read, no I/O; the host suite runs the same code.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The faces' battery scale, mV. @{ */
#define DIAL_BATT_EMPTY_MV 3300u /**< 0% */
#define DIAL_BATT_FULL_MV  4200u /**< 100% */
/** @} */

/** @brief The K518's supply limits, mV. @{ */
#define DK518_USB_MV      4450u /**< above: the rail carries USB */
#define DK518_NO_CELL_MV  2500u /**< below: no plausible cell */
#define DK518_LOW_MV      3500u /**< below: LOW counts */
#define DK518_LOW_OK_MV   3600u /**< from here: LOW's end counts */
#define DK518_EMPTY_MV    3350u /**< below: EMPTY counts */
#define DK518_EMPTY_OK_MV 3450u /**< from here: EMPTY's end counts */
/** @} */
/** @brief Readings in a row that set or end LOW and EMPTY. */
#define DK518_BATT_RUN 3u
/** @brief Samples a reading takes; the lowest and highest are dropped. */
#define DK518_SAMPLES 16u

/** @brief One flag that sets and ends after DK518_BATT_RUN readings. */
typedef struct {
    bool    on;  /**< set */
    uint8_t run; /**< readings in a row toward the change */
} dk518_flag_t;

/** @brief The K518's supply. */
typedef struct {
    bool         usb;   /**< USB powers the rail */
    bool         known; /**< mv is the cell's */
    uint16_t     mv;    /**< the cell, mV; 0 unknown */
    dk518_flag_t low;   /**< LOW */
    dk518_flag_t empty; /**< EMPTY */
} dk518_supply_t;

/**
 * @brief The share of a cell voltage for the faces.
 * @param mv the cell, mV
 * @return 0 to 100
 */
uint8_t dial_batt_pct(uint16_t mv);

/**
 * @brief One reading of the divider: DK518_SAMPLES samples at the pin,
 *        the lowest and the highest dropped, the rest averaged, times 2.
 * @param pin_mv the samples, mV at GPIO1
 * @return the rail, mV
 */
uint16_t dk518_rail_mv(const uint16_t pin_mv[DK518_SAMPLES]);

/** @brief Start with nothing known.  @param s the supply */
void dk518_supply_init(dk518_supply_t *s);

/**
 * @brief One reading, once a second.
 * @param s       the supply
 * @param rail_mv the rail, mV (dk518_rail_mv())
 * @param host    the console's USB host is there
 */
void dk518_supply_step(dk518_supply_t *s, uint16_t rail_mv, bool host);

#ifdef __cplusplus
}
#endif
