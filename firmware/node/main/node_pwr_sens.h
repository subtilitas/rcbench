/**
 * @file node_pwr_sens.h
 * @brief The power role's INA228 as the node finds and ranges it: the search
 *        of 0x40 to 0x4F, the shunt setting and its range.  Pure C over
 *        shared/sense's bus and driver, under the host suite
 *        (test_node_pwr_sens).
 *
 * The search.  The INA228's address is any of 0x40 to 0x4F (A0 and A1,
 * SLYS021A Table 7-2): the MATEK I2C-INA-BM ships at 0x45, other boards
 * mostly at 0x40.  At the start, and each time the part is to be probed
 * again, the node reads MANUFACTURER_ID (3Eh) and DEVICE_ID (3Fh) from
 * 0x40 upwards and takes the first address that answers 5449h ("TI") and
 * the INA228's die 228h in DEVICE_ID bits 15-4 (ina228_identity_ok()).
 * One address a call, so a tick of 1130µs carries at most two 2-byte
 * reads of the search; an address that does not answer costs its NACK
 * (not acknowledged) address byte.  The search's transactions go to the
 * wire directly and reach the bus's stuck rule through sense_bus_note(): a
 * NACK is a missing address and is not counted in the bus's errors.  A
 * held line or a timeout asks the same address again at the next call;
 * while the bus is stuck the search waits, and the recovery's test
 * transaction may be its own.  Nothing outside 0x40 to 0x4F is addressed: the display
 * board's FT6336G (0x38) and ES8311 (0x18) are never asked.
 *
 * Found, the driver (ina228.h) is set up at that address and probes the
 * part at the next call.  A probe that fails, and a part that goes
 * offline or is found reset (sense_part_lost()), start the search again
 * SENSE_RETRY_MS on.  A search that ends without an INA228 leaves the
 * state SENSE_PART_ABSENT, or SENSE_PART_WRONG_ID when a part answered
 * 5449h with another die (an INA238, say), and starts again SENSE_RETRY_MS
 * after its end.
 *
 * The shunt.  A setting of the node in µΩ, RCNODE_SHUNT_MIN_UOHM (50) to
 * RCNODE_SHUNT_MAX_UOHM (10,000), RCNODE_SHUNT_DEF_UOHM (200, the MATEK's)
 * by default.  The range is always ADCRANGE 1 (±40.96mV): the full scale
 * is 40.96mV over the shunt, PSENS_FS_NV / shunt in mA, 204,800mA on
 * 200µΩ, 81,920mA on 500µΩ, 40,960mA on 1000µΩ.  SHUNT_CAL is 4096 for
 * every shunt (ina228.h), so CURRENT is the shunt voltage's code and the
 * shunt scales the conversion alone: 78.125nV over the shunt a step of
 * CURRENT, 3.2 times that times VBUS's step for POWER.  A change sets the
 * part up again at its address: the set-up clears CHARGE and ENERGY and
 * the accumulators' epoch counts it.  The store keeps the setting through
 * restarts; a stored value out of range reads as the default.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ina228.h"
#include "rcnode.h"
#include "sense_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief ADCRANGE 1's full scale across the shunt, nV (Table 7-9). */
#define PSENS_FS_NV 40960000u

/** @brief Where the setting is kept: the node's NVS (non-volatile storage),
 *         a fake in the host suite. */
typedef struct {
    /** @brief Read the stored shunt.  @param ctx ctx.  @param uohm Receives
     *  it.  @return false when none is stored or the read failed. */
    bool (*load)(void *ctx, uint16_t *uohm);
    /** @brief Store the shunt.  @param ctx ctx.  @param uohm The value.
     *  @return false when the write failed. */
    bool (*save)(void *ctx, uint16_t uohm);
    void *ctx;  /**< passed to load and save unchanged */
} psens_store_t;

/** @brief The INA228, its search and its shunt.  Members are private but
 *         for the tests. */
typedef struct {
    sense_bus_t  *bus;        /**< the sensor bus */
    ina228_t      ina;        /**< the driver, set up at addr while bound */
    ina228_cal_t  cal;        /**< the shunt's calibration, ADCRANGE 1 */
    uint16_t      shunt_uohm; /**< the shunt in force, µΩ */
    uint8_t       addr;       /**< the address of the last find, 0 none */
    uint8_t       next;       /**< the search's next address, 0 none runs */
    bool          bound;      /**< the driver is set up at addr */
    bool          searched;   /**< a search has ended or found */
    bool          other_ti;   /**< this search met 5449h with another die */
    bool          rearmed;    /**< a shunt change asked for the next set-up */
    sense_state_t state;      /**< the state while not bound */
    uint32_t      search_at;  /**< when the next search starts */
} psens_t;

/**
 * @brief Whether a shunt is one the setting takes.
 * @param uohm The shunt, µΩ.
 * @return true for RCNODE_SHUNT_MIN_UOHM to RCNODE_SHUNT_MAX_UOHM.
 */
bool psens_shunt_ok(uint32_t uohm);

/**
 * @brief The full scale a shunt gives at ADCRANGE 1, mA: PSENS_FS_NV over
 *        the shunt, rounded down, so ina228_calibrate() keeps ADCRANGE 1.
 * @param uohm The shunt, µΩ; 0 gives 0.
 * @return The full scale, mA.
 */
uint32_t psens_max_ma(uint16_t uohm);

/**
 * @brief The shunt the store holds.
 * @param st The store; NULL reads as nothing stored.
 * @return The stored value when psens_shunt_ok() takes it, else
 *         RCNODE_SHUNT_DEF_UOHM.
 */
uint16_t psens_shunt_load(const psens_store_t *st);

/**
 * @brief Store a shunt.
 * @param st   The store.
 * @param uohm The shunt, µΩ.
 * @return false for a value psens_shunt_ok() refuses (nothing is written),
 *         a NULL store or a failed write.
 */
bool psens_shunt_save(const psens_store_t *st, uint32_t uohm);

/**
 * @brief Set up: nothing bound, a search due at the first step.
 * @param s    The sensor.
 * @param bus  The bus, set up (sense_bus_init()).
 * @param uohm The shunt; one psens_shunt_ok() refuses is the default.
 */
void psens_init(psens_t *s, sense_bus_t *bus, uint16_t uohm);

/**
 * @brief Every tick: the bound part's probe when it is due, else one
 *        address of the search when one is due.
 * @param s      The sensor.
 * @param now_ms The node's ms clock.
 * @return true when the part is online.
 */
bool psens_step(psens_t *s, uint32_t now_ms);

/**
 * @brief A new shunt: the calibration, and the bound part set up again at
 *        the next step.
 * @param s    The sensor.
 * @param uohm The shunt, µΩ.
 * @return false for a value psens_shunt_ok() refuses: nothing changes.
 */
bool psens_set_shunt(psens_t *s, uint32_t uohm);

/**
 * @brief Whether the last set-up was a shunt change's, read once: such a
 *        set-up is no reset of the part.
 * @param s The sensor.
 * @return true once after each shunt change that set a bound part up.
 */
bool psens_take_rearm(psens_t *s);

/**
 * @brief The address of the last find.
 * @param s The sensor.
 * @return 0x40 to 0x4F, 0 before a find or after a search that found none.
 */
uint8_t psens_addr(const psens_t *s);

/**
 * @brief The part's state for STATUS: the driver's while bound, else the
 *        last probe's or search's; SENSE_PART_UNPROBED before the first
 *        search ends.
 * @param s The sensor.
 * @return The state.
 */
sense_state_t psens_state(const psens_t *s);

/**
 * @brief One address asked at once, for the timer wake's check: whether it
 *        holds an INA228.
 * @param bus  The bus.
 * @param addr The address.
 * @param other_ti Set true when it answered 5449h with another die; may be
 *        NULL.
 * @return true for an INA228.
 */
bool psens_is_ina228(sense_bus_t *bus, uint8_t addr, bool *other_ti);

#ifdef __cplusplus
}
#endif
