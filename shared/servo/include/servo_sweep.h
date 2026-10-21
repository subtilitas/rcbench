/**
 * @file
 * @brief A servo swept through a curve: square, sine or triangle.
 *
 * A servo swept through a curve: square, sine or triangle about its centre.
 *
 * The curve runs where the pins are, on the IOmcu, so its timing does
 * not depend on the link: a 5 Hz sine over a CAN round trip of tens of
 * milliseconds would be a staircase.  The panel computes the same curve with
 * the same code to draw the horn.
 *
 * One cycle, of period 1000 / mhz seconds plus the dwells, starts at the
 * centre and goes to +amplitude, holds there for dwell_ms, goes to
 * -amplitude, holds again and comes back to the centre, where the next cycle
 * carries on towards +amplitude.  A square jumps between the ends at the
 * cycle's start and half; a sine and a triangle start at the centre, so a
 * sweep begins without a jump.  An end is counted as a movement when the
 * curve reaches it -- a quarter cycle after the jump for a square.
 *
 * With a movement count the sweep ends once that many ends have been reached
 * and the last dwell is over, and the command goes back to the centre.
 *
 * Commands are in the bank's units, 0..OUT_SPAN of the channel's own travel,
 * with the centre at OUT_SPAN / 2.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Shape of a sweep. */
typedef enum {
    SWEEP_OFF = 0,          /**< no sweep                                 */
    SWEEP_SQUARE,           /**< jumps between the ends                   */
    SWEEP_SINE,             /**< sine about the centre                    */
    SWEEP_TRIANGLE,         /**< straight lines between the ends          */
    SWEEP_KIND_COUNT
} sweep_kind_t;

/**
 * @brief Slowest curve speed, in thousandths of a cycle a second.
 *
 * SWEEP_MHZ_MIN is 50 (0.05 cycles a second) and SWEEP_MHZ_MAX is 5000
 * (5 cycles a second).
 */
#define SWEEP_MHZ_MIN      50u
#define SWEEP_MHZ_MAX    5000u
/** @brief The furthest from the centre a sweep goes, in command units. */
#define SWEEP_AMPLITUDE_MAX 500u
/** @brief The longest hold at an end, ms. */
#define SWEEP_DWELL_MAX_MS 5000u
/** @brief The centre, in command units. */
#define SWEEP_CENTRE        500u

/** @brief Configuration of one sweep. */
typedef struct {
    sweep_kind_t kind;      /**< curve shape, see sweep_kind_t            */
    uint16_t     mhz;        /**< cycles a second, x1000                   */
    uint16_t     amplitude;  /**< command units either side of the centre  */
    uint16_t     dwell_ms;   /**< held at each end                         */
    uint16_t     moves;      /**< ends to reach, 0 for no end              */
} sweep_cfg_t;

/** @brief State of one sweep, advanced by sweep_step(). */
typedef struct {
    sweep_cfg_t cfg;        /**< configuration copied by sweep_start()    */
    uint32_t    start_ms;   /**< time the sweep's phase starts from, ms   */
    bool        running;    /**< the sweep is under way                   */
    bool        paused;      /**< stopped by sweep_pause(), phase kept    */
    uint32_t    paused_ms;   /**< how far into the sweep it was paused    */
} sweep_t;

/**
 * @brief Tell whether a configuration is one a sweep can run.
 *
 * A curve, a speed and an amplitude in range, and a dwell no longer than
 * SWEEP_DWELL_MAX_MS.
 *
 * @param cfg  Configuration to check. NULL gives false.
 * @return true when the configuration is valid.
 */
bool sweep_cfg_valid(const sweep_cfg_t *cfg);

/**
 * @brief Start a sweep.
 *
 * Starts @p cfg at @p now_ms. False, and stopped, for a configuration
 * sweep_cfg_valid() refuses.
 *
 * @param w       Sweep state.
 * @param cfg     Configuration to run.
 * @param now_ms  Start time, ms.
 * @return true when the sweep started.
 */
bool sweep_start(sweep_t *w, const sweep_cfg_t *cfg, uint32_t now_ms);

/**
 * @brief Stop a sweep and forget any paused phase.
 *
 * @param w  Sweep state. NULL is ignored.
 */
void sweep_stop(sweep_t *w);

/**
 * @brief Pause a running sweep and keep its phase.
 *
 * Stops a running sweep at @p now_ms and keeps how far into it it was, so
 * sweep_resume() carries on from there: the same point of the curve, the
 * same dwell, the movements reached so far.  Nothing for a sweep that is not
 * running, finished included.
 *
 * @param w       Sweep state.
 * @param now_ms  Time of the pause, ms.
 */
void sweep_pause(sweep_t *w, uint32_t now_ms);

/**
 * @brief Resume a paused sweep.
 *
 * Carries a paused sweep on at @p now_ms from where it was paused. False, and
 * nothing changes, when nothing is paused: never paused, stopped or started
 * since.
 *
 * @param w       Sweep state.
 * @param now_ms  Time of the resume, ms.
 * @return true when a paused sweep was resumed.
 */
bool sweep_resume(sweep_t *w, uint32_t now_ms);

/**
 * @brief Command of the sweep at a time.
 *
 * The command at @p now_ms into @p command. True while the sweep runs; false
 * once it has stopped or made its movements, with @p command at the centre.
 * The clock may wrap.
 *
 * @param w        Sweep state. Stops the sweep when its movements are made.
 * @param now_ms   Current time, ms.
 * @param command  Out: command in units, 0 to 2 x SWEEP_CENTRE. NULL to skip.
 * @return true while the sweep runs.
 */
bool sweep_step(sweep_t *w, uint32_t now_ms, uint16_t *command);

/**
 * @brief Count the ends a sweep has reached.
 *
 * How many ends the sweep has reached by @p now_ms, the moves limit included
 * once reached.
 *
 * @param w       Sweep state.
 * @param now_ms  Current time, ms.
 * @return Number of ends reached. 0 for an invalid or NULL state.
 */
uint32_t sweep_moves(const sweep_t *w, uint32_t now_ms);

/**
 * @brief Tell whether a slew limit is slower than the curve's fastest change.
 *
 * Whether a slew of @p slew_per_s command units a second is slower than the
 * fastest change @p cfg's curve asks for, so the slew and not the curve
 * decides how the output moves.  The fastest change, at amplitude A and
 * f = mhz / 1000 cycles a second, is 2 pi f A for a sine (at the centre)
 * and 4 f A for a triangle (throughout).  A square jumps, so any slew
 * limits it.  The dwell does not enter: it adds time at the ends, not to
 * the motion.
 *
 * A slew of 0 is no limit, as on an output channel, and limits nothing.
 * False for a configuration sweep_cfg_valid() refuses and for an amplitude
 * of 0, which asks for no change.
 *
 * @param cfg         Configuration of the curve.
 * @param slew_per_s  Slew limit, command units a second. 0 is no limit.
 * @return true when the slew limits the curve.
 */
bool sweep_slew_limited(const sweep_cfg_t *cfg, uint16_t slew_per_s);

#ifdef __cplusplus
}
#endif
