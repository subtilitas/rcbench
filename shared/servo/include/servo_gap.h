/**
 * @file
 * @brief A measurement sensor's dropouts during a run: how long each lasts,
 *        how often they come, and whether the run rides through them.
 *
 * A run rides through a dropout of a measurement sensor that lasts at most
 * SERVO_GAP_MAX_MS and starts SERVO_GAP_EVERY_MS or more after the start of
 * the one before.  A dropout that lasts longer, or a second one that starts
 * sooner, ends the run as an error.  The run's caller says when the sensor
 * stops answering (servo_gap_lost()) and when it answers again
 * (servo_gap_back()), and checks an open dropout against the clock
 * (servo_gap_check()), so a sensor that never answers again ends the run
 * 1ms past SERVO_GAP_MAX_MS.
 *
 * Only for a reading that feeds no protection: the output encoder (AS5600)
 * in the automatic test and the limit run.  A reading a protection depends
 * on -- the INA3221 in either run, which feeds the stall stop, the limit
 * run's current ceilings and the IOmcu's current limit trip, and the
 * supply's readings, which feed its trips -- ends a run at its first loss.
 *
 * Pure C, no clock of its own; its millisecond clock wraps at 2^32.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The longest dropout of a measurement sensor a run rides through, ms.
 *
 * Motor and ESC runs at high current disturb the shared I2C bus, and a
 * measurement sensor on it can stop answering for a moment under that
 * interference; plugging a part into the bus during a run does the same.  Such
 * a dropout does not end a run.  The allowance is for measurement readings
 * only, never for a reading a protection depends on.
 */
#define SERVO_GAP_MAX_MS   300u
/**
 * @brief A second dropout that starts less than this after the start of the
 *        one before ends the run, ms: interference that comes back this often
 *        is a fault of the set-up, not a moment's disturbance.
 */
#define SERVO_GAP_EVERY_MS 10000u

/** @brief What a dropout means for the run. */
typedef enum {
    SERVO_GAP_RIDES = 0,    /**< the run goes on                         */
    SERVO_GAP_TOO_LONG,     /**< longer than SERVO_GAP_MAX_MS: it ends   */
    SERVO_GAP_TOO_SOON,     /**< within SERVO_GAP_EVERY_MS of the last
                                 one's start: it ends                    */
} servo_gap_verdict_t;

/** @brief One sensor's dropouts in one run. */
typedef struct {
    bool     open;          /**< the sensor does not answer now           */
    uint32_t from_ms;       /**< since when                               */
    bool     had;           /**< a dropout has started in this run        */
    uint32_t last_from_ms;  /**< when the last one started                */
    uint16_t count;         /**< dropouts started, the one open included  */
    uint32_t longest_ms;    /**< the longest, the one open up to the last
                                 servo_gap_check() or servo_gap_back()    */
} servo_gap_t;

/**
 * @brief No dropout yet.
 *
 * @param g The state. NULL is ignored.
 */
void servo_gap_init(servo_gap_t *g);

/**
 * @brief The sensor stopped answering at @p at_ms.  Nothing while a dropout is
 *        open already.
 *
 * @param g     The state. NULL gives SERVO_GAP_RIDES.
 * @param at_ms When it was found not answering, ms.
 * @return SERVO_GAP_TOO_SOON for a dropout that starts less than
 *         SERVO_GAP_EVERY_MS after the start of the last one, else
 *         SERVO_GAP_RIDES.
 */
servo_gap_verdict_t servo_gap_lost(servo_gap_t *g, uint32_t at_ms);

/**
 * @brief The sensor answers again at @p at_ms: the open dropout is over and
 *        counted.  Nothing while none is open.
 *
 * @param g     The state. NULL gives SERVO_GAP_RIDES.
 * @param at_ms When it answered, ms.  One earlier than the dropout's start
 *              counts as a dropout of 0ms.
 * @return SERVO_GAP_TOO_LONG for a dropout longer than SERVO_GAP_MAX_MS, else
 *         SERVO_GAP_RIDES.
 */
servo_gap_verdict_t servo_gap_back(servo_gap_t *g, uint32_t at_ms);

/**
 * @brief The open dropout against the clock at @p now_ms.
 *
 * @param g      The state. NULL gives SERVO_GAP_RIDES.
 * @param now_ms The time, ms.
 * @return SERVO_GAP_TOO_LONG once an open dropout is longer than
 *         SERVO_GAP_MAX_MS, else SERVO_GAP_RIDES.
 */
servo_gap_verdict_t servo_gap_check(servo_gap_t *g, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
